/* port_faction.c - runtime side of the co-op bot's faction layer (rules in port_botlogic.c).
 * The side table is keyed by chrnum; every chr not in it is VANILLA and resolves to
 * HOSTILE or NEUTRAL by the combatant rule. With nothing registered nothing here runs. */
#ifndef TARGET_N64

#include <stdlib.h>
#include <math.h>
#include <ultra64.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include <bondaicommands.h>   /* TARGET_BOND, TARGET_CHR */
#include "random.h"
#include "game/chr.h"
#include "game/chraction.h"
#include "game/bgfog.h"
#include "game/model.h"
#include "game/math_atan2f.h"   /* not declared anywhere else; -ffreestanding -w hides the implicit int */
#include "port_botlogic.h"
#include "port_bot.h"
#include "port_faction.h"
#include "port_mode.h"
#include "port_blocks.h"
#include "port_event.h"

extern void portConLogf(const char *fmt, ...);   /* port_console.c */
extern s32 get_is_ramrom_flag(void);             /* ramromreplay.c */
extern s32 get_recording_ramrom_flag(void);
extern s32 chrlvGetGuard007SpeedRatingInt(ChrRecord *self, s32 scale);   /* chraction.c */

#define FACTION_TABLE_MAX      32
#define FACTION_LOG_SECS       1.0f
#define FACTION_ENGAGE_TIMEOUT 3.0f    /* engaged chr unseen this long -> vanilla again */
#define FACTION_ATTACK_WINDOW  3.0f   /* how recent "who hit me" must be to retaliate */
#define FACTION_LOS_SECS       0.25f   /* engagement LOS re-check, per guard */
#define FACTION_LOS_SLOTS      64      /* LOS cache, indexed by chrnum & 63 */
#define PORT_FACTION_GUARD_HIT_CHANCE 0.35f   /* default chance a guard's chr-ray hit on a Bond-team chr counts */

typedef struct { s32 chrnum; s32 faction; } FactionEntry;

static FactionEntry sTable[FACTION_TABLE_MAX];
static s32 sCount;
static f32 sNow;
static f32 sLogAt;
static s32 sRoster;
static PortEngageTable sEngage;
static PortAttackTable sAttack;
static s32 sLosGuard[FACTION_LOS_SLOTS];   /* chrnum owning the slot, -1 = free */
static f32 sLosAt[FACTION_LOS_SLOTS];      /* sNow of the next re-check */
static s32 sLosVis[FACTION_LOS_SLOTS];
static f32 sGuardHitChance = PORT_FACTION_GUARD_HIT_CHANCE;   /* a setting: survives stage resets */

static s32 factionDebug(void)
{
    static s32 dbg = -1;

    if (dbg < 0)
    {
        const char *e = getenv("GE_BOT_DEBUG");
        dbg = (e != NULL && e[0] != '\0' && e[0] != '0') ? 1 : 0;
    }
    return dbg;
}

/* sNow in 1/60 s ticks, comparable with the bot's t= logs. */
static s32 factionTicks(void)
{
    return (s32)(sNow * 60.0f + 0.5f);
}

/* Bodies that are allies, never targets: unarmed or armed, they do not count as combatants.
 * Trevelyan_Janus is deliberately absent: that is the enemy 006 of later levels. */
static s32 factionIsAllyBody(s32 body)
{
    return body == BODY_Natalya_Skirt || body == BODY_Natalya_Jungle_Fatigues ||
           body == BODY_Trevelyan_006 || body == BODY_Valentin_;
}

void portFactionReset(void)
{
    s32 i;

    sCount = 0;
    sNow = 0.0f;
    sLogAt = 0.0f;
    sRoster = FALSE;
    {   /* arena Whacky (port_whacky.c): minions, launchers and the cheats it set go with the stage */
        extern void portWhackyReset(void);
        portWhackyReset();
    }
    portEngageReset(&sEngage);
    portAttackReset(&sAttack);
    for (i = 0; i < FACTION_LOS_SLOTS; i++)
    {
        sLosGuard[i] = -1;
        sLosAt[i] = 0.0f;
        sLosVis[i] = 0;
    }
}

f32 portFactionNow(void)
{
    return sNow;
}

PortEngageTable *portFactionEngage(void)
{
    return &sEngage;
}

void portFactionSetChr(ChrRecord *chr, s32 faction)
{
    s32 i;

    if (chr == NULL)
    {
        return;
    }
    for (i = 0; i < sCount; i++)
    {
        if (sTable[i].chrnum == chr->chrnum)
        {
            sTable[i].faction = faction;
            return;
        }
    }
    if (sCount < FACTION_TABLE_MAX)
    {
        sTable[sCount].chrnum = chr->chrnum;
        sTable[sCount].faction = faction;
        sCount++;
        return;
    }
    if (faction == PORT_FACTION_BOND || faction == PORT_FACTION_HOSTILE)
    {   /* arena Whacky minions (port_whacky.c) arrive with the table already full of the crowd (HOSTAGE spectators): reuse the entry of
         * a chr that is gone or dead, else take a spectator's (an invincible spectator then resolves as a vanilla neutral: nobody
         * targets it either way). Only these factions evict, so nothing before Whacky changes. */
        s32 hostage = -1;

        for (i = 0; i < sCount; i++)
        {
            ChrRecord *c = chrFindByLiteralId(sTable[i].chrnum);

            if (c == NULL || c->model == NULL || chrIsDead(c))
            {
                break;
            }
            if (hostage < 0 && sTable[i].faction == PORT_FACTION_HOSTAGE)
            {
                hostage = i;
            }
        }
        if (i == sCount)
        {
            i = hostage;
        }
        if (i >= 0)
        {
            sTable[i].chrnum = chr->chrnum;
            sTable[i].faction = faction;
        }
    }
}

static s32 factionRegistered(ChrRecord *chr)
{
    s32 i;

    for (i = 0; i < sCount; i++)
    {
        if (sTable[i].chrnum == chr->chrnum)
        {
            return sTable[i].faction;
        }
    }
    return PORT_FACTION_VANILLA;
}

s32 portFactionOfChr(ChrRecord *chr)
{
    s32 f;
    s32 armed;

    if (chr == NULL)
    {
        return PORT_FACTION_NEUTRAL;
    }
    f = factionRegistered(chr);
    if (f != PORT_FACTION_VANILLA)
    {
        return f;
    }
    armed = chrGetEquippedWeaponPropWithCheck(chr, GUNRIGHT) != NULL ||
            chrGetEquippedWeaponPropWithCheck(chr, GUNLEFT) != NULL;
    return portFactionResolveVanilla(portBotIsCombatant(armed, (chr->chrflags & CHRFLAG_COUNT_DEATH_AS_CIVILIAN) != 0,
                                                        factionIsAllyBody(chr->bodynum)));
}

s32 portFactionActive(void)
{
    s32 i;

    if (sCount == 0 || get_is_ramrom_flag() != 0 || get_recording_ramrom_flag() != 0)
    {
        return FALSE;
    }
    for (i = 0; i < sCount; i++)
    {
        ChrRecord *c = chrFindByLiteralId(sTable[i].chrnum);

        if (sTable[i].faction != PORT_FACTION_VANILLA && c != NULL && c->model != NULL && !chrIsDead(c))
        {
            return TRUE;
        }
    }
    return FALSE;
}

s32 portFactionHostileChrs(ChrRecord *a, ChrRecord *b)
{
    return portFactionRelHostile(portFactionOfChr(a), portFactionOfChr(b));
}

s32 portFactionPropIsBondTeam(PropRecord *p)
{
    return p != NULL && p->type == PROP_TYPE_CHR && p->chr != NULL &&
           portFactionOfChr(p->chr) == PORT_FACTION_BOND;
}

static s32 factionLos(ChrRecord *self, ChrRecord *cand)
{
    if (self->prop->stan == NULL || cand->prop->stan == NULL)
    {
        return 0;   /* the line test walks from self's stan */
    }
    return check_if_position_in_same_room(self, &cand->prop->pos, cand->prop->stan) ? 1 : 0;
}

s32 portFactionLosTo(ChrRecord *self, ChrRecord *cand)
{
    return (self != NULL && cand != NULL && self->prop != NULL && cand->prop != NULL) ? factionLos(self, cand) : 0;
}

ChrRecord *portFactionNearestHostile(ChrRecord *self, f32 maxdist, s32 needLOS)
{
    ChrRecord *best = NULL;
    f32 bestd2 = maxdist * maxdist;
    s32 n = get_numguards();
    s32 i;

    if (self == NULL || self->prop == NULL)
    {
        return NULL;
    }
    for (i = 0; i < n; i++)
    {
        ChrRecord *c = &g_ChrSlots[i];
        f32 dx, dy, dz, d2;

        if (c == self || c->model == NULL || c->prop == NULL || c->prop->stan == NULL || chrIsDead(c))
        {
            continue;
        }
        dx = c->prop->pos.x - self->prop->pos.x;
        dy = c->prop->pos.y - self->prop->pos.y;
        dz = c->prop->pos.z - self->prop->pos.z;
        d2 = dx * dx + dy * dy + dz * dz;
        if (d2 > bestd2 || !portFactionHostileChrs(self, c))
        {
            continue;
        }
        if (needLOS && !factionLos(self, c))
        {
            continue;
        }
        best = c;
        bestd2 = d2;
    }
    return best;
}

/* GE_BOT_DEBUG: live hostiles within maxdist of self, and how many of them pass factionLos. */
void portFactionCountHostile(ChrRecord *self, f32 maxdist, s32 *inrange, s32 *withlos)
{
    s32 n = get_numguards();
    s32 i;

    *inrange = 0;
    *withlos = 0;
    for (i = 0; i < n; i++)
    {
        ChrRecord *c = &g_ChrSlots[i];
        f32 dx, dy, dz;

        if (c == self || c->model == NULL || c->prop == NULL || c->prop->stan == NULL || chrIsDead(c) ||
            !portFactionHostileChrs(self, c))
        {
            continue;
        }
        dx = c->prop->pos.x - self->prop->pos.x;
        dy = c->prop->pos.y - self->prop->pos.y;
        dz = c->prop->pos.z - self->prop->pos.z;
        if (dx * dx + dy * dy + dz * dz <= maxdist * maxdist)
        {
            (*inrange)++;
            *withlos += factionLos(self, c);
        }
    }
}

/* A chr that can engage or be engaged: present, with model, prop and stan, not dying. */
static ChrRecord *factionLiveChr(s32 chrnum)
{
    ChrRecord *c = chrFindByLiteralId(chrnum);

    if (c == NULL || c->model == NULL || c->prop == NULL || c->prop->stan == NULL || chrIsDead(c))
    {
        return NULL;
    }
    return c;
}

static s32 factionEngageIndex(s32 guard)
{
    s32 i;

    for (i = 0; i < sEngage.count; i++)
    {
        if (sEngage.e[i].guard == guard)
        {
            return i;
        }
    }
    return -1;
}

/* Engage or refresh; logs only a new or changed engagement. */
static void factionEngage(s32 guard, s32 target, const char *why)
{
    s32 i = factionEngageIndex(guard);

    if (factionDebug() && (i < 0 || sEngage.e[i].target != target))
    {
        if (target == PORT_BOT_CHRNUM)
        {
            portConLogf("ge: faction: chr %d engaged bot (%s), t=%d\n", (int)guard, why, (int)factionTicks());
        }
        else
        {
            portConLogf("ge: faction: chr %d engaged chr %d (%s), t=%d\n", (int)guard, (int)target, why,
                        (int)factionTicks());
        }
    }
    portEngageSet(&sEngage, guard, target, sNow);
}

/* TRUE when the guard's current attack or stand-face is aimed at chr target (redirected). */
static s32 factionAimsAt(ChrRecord *g, s32 target)
{
    if (g->actiontype == ACT_ATTACK)
    {
        return (g->act_attack.attacktype & TARGET_CHR) != 0 && (s32)g->act_attack.entityid == target;
    }
    if (g->actiontype == ACT_STAND)
    {
        return (g->act_stand.face_entitytype & TARGET_CHR) != 0 && g->act_stand.face_entityid == target;
    }
    return FALSE;
}

/* Drops the engagement; an attack or face still aimed at the old target goes back to Bond,
 * which is what the guard's script asked for before the redirect. */
static void factionDisengage(s32 guard, const char *why)
{
    s32 i = factionEngageIndex(guard);
    ChrRecord *g = chrFindByLiteralId(guard);

    if (i >= 0 && g != NULL && g->model != NULL && !chrIsDead(g) && factionAimsAt(g, sEngage.e[i].target))
    {
        if (g->actiontype == ACT_ATTACK)
        {
            g->act_attack.attacktype = (g->act_attack.attacktype & ~TARGET_CHR) | TARGET_BOND;
            g->act_attack.entityid = 0;
        }
        else
        {
            g->act_stand.face_entitytype = (g->act_stand.face_entitytype & ~TARGET_CHR) | TARGET_BOND;
            g->act_stand.face_entityid = 0;
        }
    }
    if (factionDebug())
    {
        portConLogf("ge: faction: chr %d disengaged (%s), t=%d\n", (int)guard, why, (int)factionTicks());
    }
    portEngageDropGuard(&sEngage, guard);
}

/* The guard's engaged chrnum, or -1; a timed-out entry is dropped and logged here. */
static s32 factionEngagedTarget(s32 guard)
{
    s32 i = factionEngageIndex(guard);

    if (i < 0)
    {
        return -1;
    }
    if (sNow - sEngage.e[i].lastSeen > FACTION_ENGAGE_TIMEOUT)
    {
        factionDisengage(guard, "timeout");
        return -1;
    }
    return portEngageTarget(&sEngage, guard, sNow, FACTION_ENGAGE_TIMEOUT);
}

/* factionLos, re-run at most every FACTION_LOS_SECS per guard. */
static s32 factionLosCached(ChrRecord *self, ChrRecord *cand)
{
    s32 slot = (s32)((u32)self->chrnum & (FACTION_LOS_SLOTS - 1));

    if (sLosGuard[slot] != self->chrnum || sNow >= sLosAt[slot])
    {
        sLosGuard[slot] = self->chrnum;
        sLosAt[slot] = sNow + FACTION_LOS_SECS;
        sLosVis[slot] = factionLos(self, cand);
    }
    return sLosVis[slot];
}

/* chrCheckTargetInSight's cone, range, fog and roll, with cand in Bond's place. */
static s32 factionSightRoll(ChrRecord *self, ChrRecord *cand)
{
    PropRecord *myprop = self->prop;
    f32 dx = cand->prop->pos.x - myprop->pos.x;
    f32 dy = cand->prop->pos.y - myprop->pos.y;
    f32 dz = cand->prop->pos.z - myprop->pos.z;
    f32 roty = getsubroty(self->model);
    f32 atn = atan2f(dx, dz);
    f32 rad = atn - roty;
    f32 d2 = dx * dx + dy * dy + dz * dz;
    s32 distance;

    if (atn < roty)
    {
        rad += M_TAU_F;
    }
    if (!(rad < DegToRad(110) || rad > DegToRad(360 - 110)))
    {
        return FALSE;
    }
    if (!(d2 < self->visionrange * self->visionrange * 100.0f * 100.0f || d2 < SQR(200)))
    {
        return FALSE;
    }
    if (!(d2 < fogGetScaledFarFogIntensitySquared()))
    {
        return FALSE;
    }
    distance = (s32)((sqrtf(d2) * 30.0f) / 16000.0f);
    if (rad > DegToRad(45) && rad < DegToRad(360 - 45))
    {
        f32 f0 = (rad > M_PI_F) ? M_TAU_F - rad : rad;

        f0 -= DegToRad(45);
        distance *= (s32)((f0 * 24.0f) / M_TAU_F) + 1;
    }
    distance = chrlvGetGuard007SpeedRatingInt(self, distance) + 1;
    return ((u32)randomGetNext() % (u32)distance) == 0;   /* RNG only while factions are active */
}

s32 portFactionSightPass(ChrRecord *guard)
{
    s32 i;

    if (!portFactionActive())
    {
        return FALSE;
    }
    if (guard == NULL || guard->model == NULL || guard->prop == NULL || guard->prop->stan == NULL ||
        chrIsDead(guard))
    {
        return FALSE;
    }
    for (i = 0; i < sCount; i++)
    {
        ChrRecord *t;

        if (sTable[i].faction != PORT_FACTION_BOND)
        {
            continue;
        }
        t = factionLiveChr(sTable[i].chrnum);
        if (t == NULL || t == guard || !portFactionHostileChrs(guard, t))
        {
            continue;
        }
        /* Already engaged: like chrSawTargetRecently, skip the cone and roll and keep only the
         * (cached) LOS; a first sighting tests LOS fresh, like chrCanSeeBond. */
        if (factionEngagedTarget(guard->chrnum) == t->chrnum ? factionLosCached(guard, t)
                                                             : (factionSightRoll(guard, t) && factionLos(guard, t)))
        {
            factionEngage(guard->chrnum, t->chrnum, "seen");
            return TRUE;
        }
    }
    return FALSE;
}

void portFactionRedirectAttack(ChrRecord *self, s32 *attacktype, s32 *entityid)
{
    ChrRecord *t;
    s32 tn;

    if (!portFactionActive())
    {
        return;
    }
    if (self == NULL || self->prop == NULL || self->prop->stan == NULL || (*attacktype & TARGET_BOND) == 0)
    {
        return;
    }
    tn = factionEngagedTarget(self->chrnum);
    t = (tn >= 0) ? factionLiveChr(tn) : NULL;
    if (t == NULL)
    {
        return;   /* a dying target is dropped by portFactionTick, with its reason */
    }
    *attacktype = (*attacktype & ~TARGET_BOND) | TARGET_CHR;
    *entityid = tn;
    if (factionLosCached(self, t))
    {
        portEngageSeen(&sEngage, self->chrnum, sNow);
    }
}

/* The live chr an attack-walk or attack-roll guard is engaged with, else NULL (vanilla aims at Bond). */
ChrRecord *portFactionMovingAttackTarget(ChrRecord *self)
{
    ChrRecord *t;
    s32 tn;

    if (!portFactionActive() || self == NULL || self->prop == NULL || self->prop->stan == NULL ||
        (self->actiontype != ACT_ATTACKWALK && self->actiontype != ACT_ATTACKROLL))
    {
        return NULL;
    }
    tn = factionEngagedTarget(self->chrnum);
    t = (tn >= 0) ? factionLiveChr(tn) : NULL;
    if (t == NULL || t->prop == NULL)
    {
        return NULL;
    }
    if (factionLosCached(self, t))
    {
        portEngageSeen(&sEngage, self->chrnum, sNow);
    }
    return t;
}

/* The live chr this guard is engaged with, whatever it is doing, else NULL (the enemy_knives modifier). */
ChrRecord *portFactionEngagedChr(ChrRecord *self)
{
    s32 tn;

    if (!portFactionActive() || self == NULL)
    {
        return NULL;
    }
    tn = factionEngagedTarget(self->chrnum);
    return (tn >= 0) ? factionLiveChr(tn) : NULL;
}

void portFactionNoteHit(ChrRecord *victim, PropRecord *attackerprop)
{
    ChrRecord *a;

    if ((portModeActive() || portEventAnyListeners()) && victim != NULL && attackerprop != NULL && attackerprop->type == PROP_TYPE_CHR &&
        attackerprop->chr != NULL)
    {
        portModeNoteHit(victim->chrnum, attackerprop->chr->chrnum);   /* CHR_DIED killer */
    }
    if (!portFactionActive())
    {
        return;
    }
    if (victim == NULL || attackerprop == NULL || attackerprop->type != PROP_TYPE_CHR || attackerprop->chr == NULL)
    {
        return;   /* Bond's (viewer) hits keep the vanilla retaliation */
    }
    a = attackerprop->chr;
    portAttackNote(&sAttack, victim->chrnum, a->chrnum, sNow);
    if (factionDebug() && portBotIsBot(victim))
    {
        portConLogf("ge: bot: hit by chr %d, damage %.2f/%.2f before it, t=%d\n", (int)a->chrnum, victim->damage,
                    victim->maxdamage, (int)factionTicks());
    }
}

s32 portFactionRetaliate(ChrRecord *self)
{
    ChrRecord *a;
    s32 an;

    if (!portFactionActive())
    {
        return FALSE;
    }
    if (self == NULL || factionRegistered(self) != PORT_FACTION_VANILLA)
    {
        return FALSE;   /* port-driven chrs (the bot) have their own brain */
    }
    an = portAttackLast(&sAttack, self->chrnum, sNow, FACTION_ATTACK_WINDOW);
    a = (an >= 0) ? factionLiveChr(an) : NULL;
    if (a == NULL || a == self || !portFactionHostileChrs(self, a))
    {
        return FALSE;
    }
    factionEngage(self->chrnum, an, "retaliation");
    return TRUE;
}

void portFactionSetGuardHitChance(f32 chance)
{
    sGuardHitChance = (chance < 0.0f) ? 0.0f : (chance > 1.0f) ? 1.0f : chance;
}

f32 portFactionGuardHitChance(void)
{
    return sGuardHitChance;
}

/* Hostile chr-ray hits damage; a hit on a Bond-team chr must also pass the hit-chance roll,
 * since chr-targeted shots skip the vanilla accuracy path and would never miss. */
s32 portFactionShotDamages(ChrRecord *shooter, ChrRecord *victim)
{
    s32 hit;

    if (!portFactionActive())
    {
        return FALSE;
    }
    if (shooter != NULL && victim != NULL && shooter != victim && portFactionOfChr(victim) == PORT_FACTION_HOSTAGE)
    {
        return TRUE;   /* hostages are never targeted, but a shot that physically hits one always counts */
    }
    if (shooter == NULL || victim == NULL || !portFactionHostileChrs(shooter, victim))
    {
        return FALSE;
    }
    if (portFactionOfChr(victim) != PORT_FACTION_BOND)
    {
        return TRUE;
    }
    hit = (f32)(randomGetNext() % 10000U) < sGuardHitChance * 10000.0f;
    if (factionDebug())
    {
        portConLogf("ge: faction: chr %d shot chr %d: %s (chance %.2f), t=%d\n", (int)shooter->chrnum,
                    (int)victim->chrnum, hit ? "hit" : "miss", sGuardHitChance, (int)factionTicks());
    }
    return hit;
}

/* Refreshes engagements whose guard is still aiming at a visible target, then drops those
 * whose target or guard died or was removed, or whose target went unseen too long. It runs
 * every gated tick, active or not, so a dead bot clears on its death tick. */
void portFactionTick(f32 dt)
{
    s32 i;

    sNow += dt;
    for (i = sEngage.count - 1; i >= 0; i--)
    {
        PortEngageEntry e = sEngage.e[i];
        ChrRecord *g = factionLiveChr(e.guard);
        ChrRecord *t = factionLiveChr(e.target);
        const char *why = NULL;

        if (t == NULL)
        {
            why = "target dead";
        }
        else if (g == NULL)
        {
            why = "guard dead";
        }
        else
        {
            /* a long attack makes no new redirect calls, and a guard that also sees Bond
             * skips the bot sight pass, so its own aim keeps the engagement alive */
            if (factionAimsAt(g, e.target) && factionLosCached(g, t))
            {
                portEngageSeen(&sEngage, e.guard, sNow);
                e.lastSeen = sNow;
            }
            if (sNow - e.lastSeen > FACTION_ENGAGE_TIMEOUT)
            {
                why = "timeout";
            }
        }
        if (why != NULL)
        {
            factionDisengage(e.guard, why);   /* swaps the last, already visited, entry into i */
        }
    }

    {   /* test hook (GE_FACTION_FORCE=1): every hostile guard with LOS to the bot engages it */
        static s32 force = -1;
        ChrRecord *fb;

        if (force < 0)
        {
            const char *e = getenv("GE_FACTION_FORCE");

            force = (e != NULL && e[0] == '1');
        }
        fb = force ? portBotChr() : NULL;
        for (i = 0; fb != NULL && i < get_numguards(); i++)
        {
            ChrRecord *c = &g_ChrSlots[i];

            if (c->model != NULL && c != fb && c->prop != NULL && !chrIsDead(c) && portFactionHostileChrs(c, fb) &&
                factionEngagedTarget(c->chrnum) < 0 && factionLosCached(c, fb))
            {
                factionEngage(c->chrnum, fb->chrnum, "forced (test)");
            }
        }
    }

    if (factionDebug() && sNow >= sLogAt)
    {
        ChrRecord *bot = portBotChr();

        sLogAt = sNow + FACTION_LOG_SECS;
        if (bot != NULL && !sRoster)
        {
            sRoster = TRUE;
            for (i = 0; i < get_numguards(); i++)
            {
                ChrRecord *c = &g_ChrSlots[i];

                if (c->model != NULL)
                {
                    portConLogf("ge: faction: roster chr %d body %d flags %08x -> %d\n", (int)c->chrnum,
                                (int)c->bodynum, (unsigned)c->chrflags, (int)portFactionOfChr(c));
                }
            }
        }
        if (bot != NULL)
        {
            ChrRecord *h = portFactionNearestHostile(bot, 100000.0f, FALSE);

            if (h != NULL)
            {
                f32 dx = h->prop->pos.x - bot->prop->pos.x;
                f32 dz = h->prop->pos.z - bot->prop->pos.z;

                portConLogf("ge: faction: bot nearest hostile = chr %d at %.0f (los %d)\n", (int)h->chrnum,
                            sqrtf(dx * dx + dz * dz), (int)factionLos(bot, h));
            }
            else
            {
                portConLogf("ge: faction: bot nearest hostile = none\n");
            }
        }
    }
}

#endif /* TARGET_N64 */
