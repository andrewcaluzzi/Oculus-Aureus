/* port_arenacoop.c - arena co-op (drop A7, modding/arena.md "Drop A7 co-op").
 *
 * 1. Downed and revive. In a mode stage with two Bonds, once the mode script turned the rules on (Lua coop.rules), a Bond whose
 *    health runs out goes DOWN instead of dying, as long as another Bond is still up: he keeps a sliver of health, takes no
 *    damage, his pad and look are neutral, and the enemies ignore him (portCoopNearestBond). A partner who stands within
 *    `radius` for `revive` seconds (continuously; stepping away drains the progress twice as fast) brings him back with
 *    `reviveHealth` and `grace` seconds without damage. After `bleed` seconds he bleeds out: a real death (death drops as usual);
 *    with bleedout "wave" the co-op respawn is held until the mode releases him (Lua coop.release, at the end of the wave),
 *    "run" holds him for the rest of the run, "end" ends the run (every Bond dies). The last Bond standing never goes down: he
 *    dies, and every downed Bond dies with him, so "everybody down" is the stock all-dead failure.
 * 2. The partner bot: GE_COOP_P2=arena or Lua coop.bot(true) makes the co-op puppet Bond play the arena through its pad and look
 *    channel (the same input a human gives): it picks the nearest arena enemy (chrnum 8000+) it can see, turns and pitches at it,
 *    fires when lined up, strafes, keeps near its partner, revives him, walks to a point the mode gives (centre ammo, armour) and
 *    cycles past empty guns. Local co-op only (in netplay the partner is a human); its decisions use sim state and its own
 *    xorshift, never the sim RNG.
 *
 * Sim state: classified SIM in scripts/port/snap_embed.py, hashed into the netplay "players" field (portArenaCoopHash). Nothing
 * here reads portLocalPlayer() or the render pass. */
#ifndef TARGET_N64

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ultra64.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include "game/chr.h"
#include "game/chraction.h"
#include "game/player.h"
#include "game/bondview.h"
#include "game/chrai.h"
#include "port_mode.h"
#include "port_arenacoop.h"

extern void portConLogf(const char *fmt, ...);
extern void portCoopFpLog(const char *fmt, ...);
extern int portCoopActive(void);
extern int portAllPlayerCount(void);
extern int portCoopPuppetArena(int on);                 /* port_coop.c */
extern void bondviewKillCurrentPlayer(void);
extern s32 get_ammo_type_for_weapon(ITEM_IDS weapon);
extern s32 bondinvHasInvItem(ITEM_IDS weapon);
extern s32 portArenaChrIsEnemy(const ChrRecord *chr);   /* port_arenamode.c */
extern ChrRecord *g_ChrSlots;
extern s32 get_numguards(void);
extern s32 g_ClockTimer;

#define NP 4
#define DOWN_HEALTH 0.01f

typedef struct {
    u8 downed, held, release, pad;
    s32 downFields;      /* fields (1/60 s) since he went down */
    s32 reviveFields;    /* the partner's continuous presence, fields */
    s32 graceFields;     /* no damage left after a revive */
} DownState;

static DownState sDown[NP];
static PortArenaCoopRules sCfg;
static s32 sBypass;      /* a kill that must not be turned into a down (bleed-out, the last Bond, tests) */
static s32 sDowns, sRevives, sBleeds;

/* the partner bot's memory, per Bond */
typedef struct {
    u32 tick, rng;
    s32 target;          /* chrnum, -1 none */
    u8 los, following, goalSet, act, lootSet;
    s8 strafeDir, unstickDir;
    f32 lx, lz;          /* a dropped gun it walks to when it runs low */
    f32 goalX, goalZ;    /* the mode's errand (coop.bot_goal) */
    s32 strafeLeft, unstick, cycleWait, meleeCycles, lastAnyAmmo;
    s32 bestRank, upCycles, stuckCount;   /* the best gun with ammo it carries (botRank), tries to switch up to it */
    f32 lastX, lastZ, gx, gz;
    s32 shots;
    f32 yawErr;
} BotState;

static BotState sBot[NP];

enum { ACT_IDLE, ACT_FIGHT, ACT_FOLLOW, ACT_REVIVE, ACT_GOAL, ACT_DOWN, ACT_LOOT, ACT_N };
static const char *const sActNames[] = { "idle", "fight", "follow", "revive", "goal", "down", "loot" };

static struct player *pl(int i)
{
    return (i >= 0 && i < NP) ? g_playerPointers[i] : NULL;
}

static s32 rulesOn(void)
{
    return sCfg.on && portCoopActive() && portModeActive();
}

void portArenaCoopReset(void)
{
    s32 i;

    memset(sDown, 0, sizeof(sDown));
    memset(&sCfg, 0, sizeof(sCfg));
    memset(sBot, 0, sizeof(sBot));
    for (i = 0; i < NP; i++) {
        sBot[i].target = -1;
        sBot[i].rng = 0x9E3779B9u ^ (u32) (i * 7919 + 1);
        sBot[i].strafeDir = 1;
        sBot[i].unstickDir = 1;
    }
    sBypass = 0;
    sDowns = sRevives = sBleeds = 0;
}

static u32 botRng(BotState *b)
{
    u32 x = b->rng;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    b->rng = x ? x : 1u;
    return b->rng;
}

/* alive, not downed, not held */
static s32 isUp(int i)
{
    struct player *p = pl(i);

    return p != NULL && p->prop != NULL && !p->bonddead && !sDown[i].downed;
}

static s32 otherUp(int self)
{
    s32 i, n = portAllPlayerCount();

    for (i = 0; i < n && i < NP; i++) {
        if (i != self && isUp(i)) {
            return TRUE;
        }
    }
    return FALSE;
}

int portArenaCoopDowned(int player)
{
    return player >= 0 && player < NP && sDown[player].downed && rulesOn();
}

int portArenaCoopHeld(int player)
{
    return player >= 0 && player < NP && sDown[player].held && rulesOn();
}

int portArenaCoopNoDamage(int player)
{
    return player >= 0 && player < NP && rulesOn() && (sDown[player].downed || sDown[player].graceFields > 0);
}

static void goDown(int p)
{
    struct player *q = pl(p);

    q->bondhealth = DOWN_HEALTH;
    q->crouchpos = CROUCH_SQUAT;   /* on his knees (his own view drops; his input is neutral, so it stays) */
    sDown[p].downed = 1;
    sDown[p].downFields = 0;
    sDown[p].reviveFields = 0;
    sDowns++;
    portCoopFpLog("ge: arena: coop: player %d DOWN (bleeds out in %.0fs, revive %.1fs within %.0f)\n", p, (double) sCfg.bleed,
                  (double) sCfg.revive, (double) sCfg.radius);
}

int portArenaCoopDownInstead(int player)
{
    if (sBypass || !rulesOn() || player < 0 || player >= NP || pl(player) == NULL) {
        return 0;
    }
    if (sDown[player].downed) {
        return 1;   /* already down: nothing more happens to him */
    }
    if (!otherUp(player)) {
        portCoopFpLog("ge: arena: coop: player %d falls with nobody up to revive him: the run is over\n", player);
        return 0;   /* the last Bond standing dies; the tick takes the downed ones with him */
    }
    goDown(player);
    return 1;
}

static void killBond(int p)
{
    struct player *q = pl(p);
    s32 saved;
    u8 inv;

    if (q == NULL || q->prop == NULL || q->bonddead) {
        return;
    }
    saved = get_cur_playernum();
    sBypass = 1;
    set_cur_player(p);
    inv = q->cheatBondInvincible;
    q->cheatBondInvincible = 0;
    q->bondhealth = 0.0f;
    bondviewKillCurrentPlayer();
    q->cheatBondInvincible = inv;
    set_cur_player(saved);
    sBypass = 0;
}

void portArenaCoopTick(void)
{
    s32 i, n, anyUp = FALSE, anyDowned = FALSE;
    s32 dt = g_ClockTimer > 0 ? g_ClockTimer : 0;
    const s32 bleed = (s32) (sCfg.bleed * 60.0f), revive = (s32) (sCfg.revive * 60.0f);

    if (!rulesOn()) {
        return;
    }
    n = portAllPlayerCount();
    if (n > NP) {
        n = NP;
    }
    for (i = 0; i < n; i++) {
        struct player *q = pl(i);
        DownState *d = &sDown[i];

        if (q == NULL || q->prop == NULL) {
            continue;
        }
        if (d->graceFields > 0) {
            d->graceFields -= dt;
        }
        if (d->held && !q->bonddead) {
            d->held = 0;   /* respawned (released) */
            d->release = 0;
            portCoopFpLog("ge: arena: coop: player %d is back (respawned)\n", i);
        }
        if (d->release && q->bonddead) {
            d->held = 0;   /* the co-op death tick respawns him at its next pass */
            d->release = 0;
        }
        if (!d->downed) {
            continue;
        }
        if (q->bonddead) {
            d->downed = 0;   /* died some other way */
            continue;
        }
        q->bondhealth = DOWN_HEALTH;
        q->crouchpos = CROUCH_SQUAT;
        d->downFields += dt;
        {   /* a partner close enough revives him */
            s32 k, near = FALSE;

            for (k = 0; k < n && !near; k++) {
                struct player *r = pl(k);

                if (k != i && isUp(k)) {
                    f32 dx = r->prop->pos.x - q->prop->pos.x, dz = r->prop->pos.z - q->prop->pos.z, dy = r->prop->pos.y - q->prop->pos.y;

                    near = (dx * dx + dz * dz <= sCfg.radius * sCfg.radius) && dy < 250.0f && dy > -250.0f;
                }
            }
            if (near) {
                d->reviveFields += dt;
            } else if (d->reviveFields > 0) {
                d->reviveFields -= 2 * dt;
                if (d->reviveFields < 0) {
                    d->reviveFields = 0;
                }
            }
        }
        if (d->reviveFields >= revive) {
            d->downed = 0;
            d->reviveFields = 0;
            d->graceFields = (s32) (sCfg.grace * 60.0f);
            q->bondhealth = sCfg.reviveHealth;
            q->crouchpos = CROUCH_STAND;
            sRevives++;
            portCoopFpLog("ge: arena: coop: player %d REVIVED after %.1fs down (health %.2f)\n", i, (double) d->downFields / 60.0,
                          (double) q->bondhealth);
            continue;
        }
        if (d->downFields >= bleed) {
            d->downed = 0;
            sBleeds++;
            portCoopFpLog("ge: arena: coop: player %d BLED OUT after %.1fs (%s)\n", i, (double) d->downFields / 60.0,
                          sCfg.bleedout == PORT_AC_BLEED_END ? "the run ends" : (sCfg.bleedout == PORT_AC_BLEED_RUN ? "out for the run" : "back after the wave"));
            d->held = (sCfg.bleedout != PORT_AC_BLEED_END);
            killBond(i);
            if (sCfg.bleedout == PORT_AC_BLEED_END) {
                s32 k;

                for (k = 0; k < n; k++) {
                    sDown[k].downed = 0;
                    killBond(k);
                }
                return;
            }
        }
    }
    for (i = 0; i < n; i++) {
        anyUp |= isUp(i);
        anyDowned |= (sDown[i].downed != 0);
    }
    if (anyDowned && !anyUp) {   /* nobody left to revive anyone: every downed Bond dies (the stock all-dead failure follows) */
        for (i = 0; i < n; i++) {
            if (sDown[i].downed) {
                sDown[i].downed = 0;
                portCoopFpLog("ge: arena: coop: player %d dies (nobody is up)\n", i);
                killBond(i);
            }
        }
    }
}

unsigned portArenaCoopHash(char *dump, int cap)
{
    unsigned h = 2166136261u;
    s32 i;

    if (dump != NULL && cap > 0) {
        dump[0] = '\0';
    }
    if (!sCfg.on) {
        return h;
    }
    for (i = 0; i < NP; i++) {
        h = (h ^ (unsigned) (sDown[i].downed | sDown[i].held << 1 | sDown[i].release << 2)) * 16777619u;
        h = (h ^ (unsigned) sDown[i].downFields) * 16777619u;
        h = (h ^ (unsigned) sDown[i].reviveFields) * 16777619u;
        h = (h ^ (unsigned) sDown[i].graceFields) * 16777619u;
    }
    if (dump != NULL && cap > 0) {
        snprintf(dump, (size_t) cap, "[arena coop] p0 down %d held %d %d/%d/%d  p1 down %d held %d %d/%d/%d\n", sDown[0].downed, sDown[0].held,
                 (int) sDown[0].downFields, (int) sDown[0].reviveFields, (int) sDown[0].graceFields, sDown[1].downed, sDown[1].held,
                 (int) sDown[1].downFields, (int) sDown[1].reviveFields, (int) sDown[1].graceFields);
    }
    return h;
}

/* ---- the partner bot ------------------------------------------------------------------------------------------------------------ */

#define BOT_RANGE      5500.0f   /* engage from here (the pit is 9000 across) */
#define BOT_FOLLOW_FAR 1000.0f   /* start walking to the partner */
#define BOT_FOLLOW_OK  500.0f    /* ...and stop here */
#define BOT_FIRE_CONE  6.0f      /* degrees of yaw error to fire */

static f32 wrap180(f32 a)
{
    while (a > 180.0f) {
        a -= 360.0f;
    }
    while (a < -180.0f) {
        a += 360.0f;
    }
    return a;
}

static f32 yawTo(f32 dx, f32 dz)
{
    return __builtin_atan2f(-dx, dz) * (180.0f / 3.14159265f);   /* facing f(th) = (-sin th, cos th) */
}

static s32 canSee(ChrRecord *c, struct player *me)
{
    if (c->prop == NULL || c->prop->stan == NULL || me->prop->stan == NULL) {
        return FALSE;
    }
    return check_if_position_in_same_room(c, &me->prop->pos, me->prop->stan) ? TRUE : FALSE;
}

static ChrRecord *liveEnemy(s32 chrnum)
{
    ChrRecord *c = chrnum >= 0 ? chrFindByLiteralId(chrnum) : NULL;

    return (c != NULL && c->prop != NULL && c->model != NULL && !chrIsDead(c) && portArenaChrIsEnemy(c)) ? c : NULL;
}

/* the target: the nearest visible enemy (the current one keeps a 25% bonus so it does not flip), else the nearest at all (to face) */
static void botPick(BotState *b, struct player *me)
{
    s32 i, ng = get_numguards(), best = -1, bestVis = 0;
    f32 bd = 1e18f;

    for (i = 0; i < ng; i++) {
        ChrRecord *c = &g_ChrSlots[i];
        f32 dx, dz, d2;
        s32 vis;

        if (c->model == NULL || c->prop == NULL || chrIsDead(c) || !portArenaChrIsEnemy(c)) {
            continue;
        }
        dx = c->prop->pos.x - me->prop->pos.x;
        dz = c->prop->pos.z - me->prop->pos.z;
        d2 = dx * dx + dz * dz;
        if (d2 > BOT_RANGE * BOT_RANGE * 2.0f) {
            continue;
        }
        vis = canSee(c, me);
        if (c->chrnum == b->target) {
            d2 *= 0.56f;
        }
        if ((vis && !bestVis) || (vis == bestVis && d2 < bd)) {
            best = c->chrnum;
            bd = d2;
            bestVis = vis;
        }
    }
    b->target = best;
    b->los = (u8) bestVis;
}

/* how much the bot likes a gun in the open pit (0 = melee; launchers low: their blast hurts the partner too) */
static s32 botRank(s32 w)
{
    switch (w) {
        case ITEM_M16: case ITEM_FNP90: return 9;
        case ITEM_AK47: case ITEM_SPECTRE: case ITEM_AUTOSHOT: return 8;
        case ITEM_MP5K: case ITEM_MP5KSIL: case ITEM_UZI: return 7;
        case ITEM_SHOTGUN: return 6;
        case ITEM_SKORPION: case ITEM_GOLDENGUN: return 5;
        case ITEM_TT33: case ITEM_RUGER: return 4;
        case ITEM_WPPK: case ITEM_WPPKSIL: return 3;
        case ITEM_SNIPERRIFLE: case ITEM_LASER: return 2;
        case ITEM_ROCKETLAUNCH: case ITEM_GRENADELAUNCH: return 1;
        default: return 0;
    }
}

/* the best gun he carries that has ammo (rank), from the real inventory */
static s32 botBestCarried(int mi, struct player *me)
{
    static const s32 guns[] = { ITEM_M16, ITEM_FNP90, ITEM_AK47, ITEM_SPECTRE, ITEM_AUTOSHOT, ITEM_MP5K, ITEM_MP5KSIL, ITEM_UZI, ITEM_SHOTGUN,
                                ITEM_SKORPION, ITEM_GOLDENGUN, ITEM_TT33, ITEM_RUGER, ITEM_WPPK, ITEM_WPPKSIL };
    s32 saved = get_cur_playernum(), i, best = 0;

    set_cur_player(mi);
    for (i = 0; i < (s32) (sizeof(guns) / sizeof(guns[0])); i++) {
        s32 type = get_ammo_type_for_weapon((ITEM_IDS) guns[i]);
        s32 have = (type > 0 && type < 30) ? me->ammoheldarr[type] : 0;

        if (guns[i] == (s32) me->hands[GUNRIGHT].weaponnum) {
            have += me->hands[GUNRIGHT].weapon_ammo_in_magazine;
        }
        if (have > 0 && botRank(guns[i]) > best && bondinvHasInvItem((ITEM_IDS) guns[i])) {
            best = botRank(guns[i]);
        }
    }
    set_cur_player(saved);
    return best;
}

/* out of ammo for the gun in hand (or holding the slapper / a knife) */
static s32 botLowAmmo(struct player *me)
{
    s32 w = (s32) me->hands[GUNRIGHT].weaponnum, type = w > 0 ? get_ammo_type_for_weapon((ITEM_IDS) w) : 0;
    s32 res = (type > 0 && type < 30) ? me->ammoheldarr[type] : 0;

    return w <= ITEM_KNIFE || me->hands[GUNRIGHT].weapon_ammo_in_magazine + res < 20;
}

/* the nearest gun lying on the floor (an enemy's drop, a centre pickup) within `range` that ranks above `minRank`: walking over it picks it up */
static s32 botFindDrop(struct player *me, s32 minRank, f32 range, f32 *x, f32 *z)
{
    s32 i, found = FALSE;
    f32 bd = range * range;

    for (i = 0; i < MAX_WEAPON_SLOTS; i++) {
        WeaponObjRecord *w = &g_WeaponSlots[i];
        PropRecord *p = w->prop;
        f32 dx, dz;

        if (p == NULL || p->parent != NULL || p->type != PROP_TYPE_WEAPON || (w->runtime_bitflags & RUNTIMEBITFLAG_HASPROJECTILE) ||
            (w->state & PROPSTATE_RESPAWN) || (w->runtime_bitflags & RUNTIMEBITFLAG_REMOVE) || botRank((s32) w->weaponnum) <= minRank) {
            continue;
        }
        dx = p->pos.x - me->prop->pos.x;
        dz = p->pos.z - me->prop->pos.z;
        if (dx * dx + dz * dz < bd) {
            bd = dx * dx + dz * dz;
            *x = p->pos.x;
            *z = p->pos.z;
            found = TRUE;
        }
    }
    return found;
}

static s8 stick(f32 v)
{
    return (s8) (v > 70.0f ? 70.0f : (v < -70.0f ? -70.0f : v));
}

void portArenaBotPad(int mi, int pi, unsigned short *buttons, signed char *sx, signed char *sy, int *yawMilli, int *pitchMilli)
{
    struct player *me = pl(mi), *pa = pl(pi);
    BotState *b;
    ChrRecord *tc;
    f32 mvx = 0.0f, mvz = 0.0f, speed = 0.0f, th, fx, fz, tdist = 0.0f;
    f32 wantYaw = 0.0f, wantPitch = 0.0f;
    s32 haveYaw = FALSE, fire = FALSE;

    *buttons = 0;
    *sx = *sy = 0;
    *yawMilli = *pitchMilli = 0;
    if (mi < 0 || mi >= NP || me == NULL || me->prop == NULL || me->bonddead || !portModeActive()) {
        return;
    }
    b = &sBot[mi];
    b->tick++;
    if (sDown[mi].downed) {
        b->act = ACT_DOWN;
        return;
    }
    th = me->vv_theta * (3.14159265f / 180.0f);
    fx = -sinf(th);
    fz = cosf(th);

    /* target: re-picked every 8 ticks, or at once when it dies */
    tc = liveEnemy(b->target);
    if (tc == NULL || (b->tick & 7u) == 0) {
        botPick(b, me);
        tc = liveEnemy(b->target);
    }
    if (tc != NULL) {
        f32 dx = tc->prop->pos.x - me->prop->pos.x, dz = tc->prop->pos.z - me->prop->pos.z;
        f32 dy = (tc->prop->pos.y + 20.0f) - me->prop->pos.y;   /* (chest of the chr vs Bond's eye prop; autoaim does the rest) */

        tdist = sqrtf(dx * dx + dz * dz);
        wantYaw = yawTo(dx, dz);
        wantPitch = wrap180(__builtin_atan2f(dy, tdist > 1.0f ? tdist : 1.0f) * (180.0f / 3.14159265f));   /* (the game's atan2f is 0..2pi) */
        haveYaw = TRUE;
    }

    if ((b->tick % 30u) == 0) {
        s32 best = botBestCarried(mi, me);

        if (best != b->bestRank) {
            b->upCycles = 0;
        }
        b->bestRank = best;
    }

    /* where to go: revive a downed partner, the mode's goal, or stay near the partner */
    b->act = tc != NULL ? ACT_FIGHT : ACT_IDLE;
    if (pa != NULL && pa->prop != NULL && !pa->bonddead && pi >= 0 && pi < NP && sDown[pi].downed) {
        b->gx = pa->prop->pos.x;
        b->gz = pa->prop->pos.z;
        b->act = ACT_REVIVE;
    } else if (b->goalSet) {
        b->act = ACT_GOAL;
        b->gx = b->goalX;
        b->gz = b->goalZ;
    } else if ((b->tick % 30u) == 0
                   ? (b->lootSet = (u8) ((botLowAmmo(me) && botFindDrop(me, 0, 2500.0f, &b->lx, &b->lz)) ||
                                         ((tc == NULL || !b->los) && botFindDrop(me, b->bestRank, tc == NULL ? 6000.0f : 1200.0f, &b->lx, &b->lz))))
                   : b->lootSet) {
        /* re-checked every half second: out of ammo (any gun within 2500), or nobody in sight and a better gun within 1200 (6000 when no
         * enemy is alive at all: the breather; the drops lie where the enemies fell, often at the gates) */
        b->act = ACT_LOOT;
        b->gx = b->lx;
        b->gz = b->lz;
    } else if (pa != NULL && pa->prop != NULL && !pa->bonddead) {
        f32 dx = pa->prop->pos.x - me->prop->pos.x, dz = pa->prop->pos.z - me->prop->pos.z;
        f32 d = sqrtf(dx * dx + dz * dz);

        if (d > BOT_FOLLOW_FAR || (b->following && d > BOT_FOLLOW_OK)) {
            b->following = 1;
            b->gx = pa->prop->pos.x;
            b->gz = pa->prop->pos.z;
            b->act = ACT_FOLLOW;
        } else {
            b->following = 0;
        }
    }
    if (b->act == ACT_REVIVE || b->act == ACT_GOAL || b->act == ACT_FOLLOW || b->act == ACT_LOOT) {
        f32 dx = b->gx - me->prop->pos.x, dz = b->gz - me->prop->pos.z;
        f32 d = sqrtf(dx * dx + dz * dz);
        /* revive: stop well inside the revive radius (walking into his body only makes the stuck sidestep carry it out of it) */
        f32 arrive = b->act == ACT_REVIVE ? 0.5f * sCfg.radius : (b->act == ACT_FOLLOW ? BOT_FOLLOW_OK : 15.0f);

        if (d > arrive && d > 1.0f) {
            mvx = dx / d;
            mvz = dz / d;
            speed = (d > 300.0f) ? 70.0f : 45.0f;
        } else if (b->act == ACT_GOAL) {
            b->goalSet = 0;   /* arrived */
        } else if (b->act == ACT_LOOT) {
            b->lootSet = 0;
        }
        if (!haveYaw && d > 1.0f) {
            wantYaw = yawTo(dx, dz);
            haveYaw = TRUE;
        }
    } else if (tc != NULL) {   /* fighting in place: strafe across the target's line, back off when it is close */
        f32 dx = tc->prop->pos.x - me->prop->pos.x, dz = tc->prop->pos.z - me->prop->pos.z;
        f32 d = tdist > 1.0f ? tdist : 1.0f;

        if (--b->strafeLeft <= 0) {
            b->strafeDir = (botRng(b) & 1u) ? 1 : -1;
            b->strafeLeft = 50 + (s32) (botRng(b) % 70u);
        }
        mvx = (-dz / d) * (f32) b->strafeDir;
        mvz = (dx / d) * (f32) b->strafeDir;
        speed = 45.0f;
        if (me->bondhealth < 0.35f && pa != NULL && pa->prop != NULL && !pa->bonddead) {   /* hurt: fall back to the partner while shooting */
            f32 px = pa->prop->pos.x - me->prop->pos.x, pz = pa->prop->pos.z - me->prop->pos.z, pd = sqrtf(px * px + pz * pz);

            if (pd > 250.0f) {
                mvx = 0.3f * mvx + px / pd;
                mvz = 0.3f * mvz + pz / pd;
                speed = 70.0f;
            }
        } else if (d < 700.0f) {
            mvx -= dx / d;
            mvz -= dz / d;
            speed = 60.0f;
        }
    }

    /* stuck: under 30 units in half a second while walking -> walk sideways (a pillar is 1000 wide): 1 s, then longer each time it
     * stays stuck, the same side for three tries before it turns to the other side */
    if ((b->tick % 30u) == 0) {
        f32 ddx = me->prop->pos.x - b->lastX, ddz = me->prop->pos.z - b->lastZ;
        s32 stuck = speed > 0.0f && ddx * ddx + ddz * ddz < 30.0f * 30.0f;

        f32 gdx = b->gx - me->prop->pos.x, gdz = b->gz - me->prop->pos.z;

        if (stuck && b->unstick <= 0 && (b->act != ACT_REVIVE || gdx * gdx + gdz * gdz > 400.0f * 400.0f)) {   /* (not when it is at his side) */
            if (++b->stuckCount > 3) {
                b->stuckCount = 1;
                b->unstickDir = (s8) -b->unstickDir;
            }
            b->unstick = 30 + 45 * b->stuckCount;
        } else if (!stuck && b->unstick <= 0 && ddx * ddx + ddz * ddz > 150.0f * 150.0f) {
            b->stuckCount = 0;   /* walking freely again */
        }
        b->lastX = me->prop->pos.x;
        b->lastZ = me->prop->pos.z;
    }
    if (b->unstick > 0) {
        f32 ox = mvx, oz = mvz;

        b->unstick--;
        mvx = 0.15f * ox + (-oz) * (f32) b->unstickDir;
        mvz = 0.15f * oz + (ox) * (f32) b->unstickDir;
        if (ox == 0.0f && oz == 0.0f) {
            mvx = fz * (f32) b->unstickDir;
            mvz = -fx * (f32) b->unstickDir;
        }
        speed = 70.0f;
    }

    /* look: ease the yaw and pitch toward the wanted angles (milli-degrees per tick, like the mouse) */
    if (haveYaw) {
        f32 err = wrap180(wantYaw - me->vv_theta), y = err * 350.0f;

        b->yawErr = err;
        *yawMilli = (int) (y > 6000.0f ? 6000.0f : (y < -6000.0f ? -6000.0f : y));
    }
    {
        f32 cur = me->vv_verta > 180.0f ? me->vv_verta - 360.0f : me->vv_verta;
        f32 perr = (tc != NULL ? wantPitch : 0.0f) - cur, pm = perr * 300.0f;

        *pitchMilli = (int) (pm > 3000.0f ? 3000.0f : (pm < -3000.0f ? -3000.0f : pm));
    }

    /* walk: the world direction as forward + sideways stick from the facing (the PC stick strafes) */
    if (speed > 0.0f) {
        f32 len = sqrtf(mvx * mvx + mvz * mvz);

        if (len > 0.001f) {
            mvx /= len;
            mvz /= len;
            *sy = stick((mvx * fx + mvz * fz) * speed);
            *sx = stick((mvx * -fz + mvz * fx) * speed);
        }
    }

    /* weapons: fire when lined up on a visible target in range; cycle past an empty gun */
    {
        s32 w = (s32) me->hands[GUNRIGHT].weaponnum, type = w > 0 ? get_ammo_type_for_weapon((ITEM_IDS) w) : 0;
        s32 mag = me->hands[GUNRIGHT].weapon_ammo_in_magazine, res = (type > 0 && type < 30) ? me->ammoheldarr[type] : 0;
        s32 k, anyAmmo = 0;

        for (k = 1; k < 30; k++) {
            anyAmmo += me->ammoheldarr[k];
        }
        if (b->cycleWait > 0) {
            b->cycleWait--;
        }
        if (w > ITEM_KNIFE || anyAmmo != b->lastAnyAmmo) {
            b->meleeCycles = 0;   /* (ammo that fits no carried gun stops the search after a full round) */
            b->lastAnyAmmo = anyAmmo;
        }
        if (b->cycleWait == 0 && ((w > ITEM_KNIFE && type > 0 && mag == 0 && res == 0) || (w <= ITEM_KNIFE && anyAmmo > 0 && b->meleeCycles < 12))) {
            *buttons |= CONT_A;   /* next weapon (the slapper and the knife count as empty while a gun has ammo) */
            b->cycleWait = 40;
            if (w <= ITEM_KNIFE) {
                b->meleeCycles++;
            }
        } else if (b->cycleWait == 0 && w > ITEM_KNIFE && botRank(w) < b->bestRank && b->upCycles < 12) {
            *buttons |= CONT_A;   /* a better gun with ammo is in the inventory: cycle up to it */
            b->cycleWait = 40;
            b->upCycles++;
        }
        if (tc != NULL && b->los && tdist < BOT_RANGE && b->yawErr < BOT_FIRE_CONE && b->yawErr > -BOT_FIRE_CONE) {
            fire = (w > ITEM_KNIFE) || tdist < 180.0f;
        }
        if (fire && (b->tick % 8u) < 6u) {   /* hold, with a short release so a pistol fires again */
            *buttons |= CONT_G;
            if ((b->tick % 8u) == 0) {
                b->shots++;
            }
        }
    }
    if (sCfg.debug && (b->tick % 120u) == 0) {
        portConLogf("ge: arena: bot p%d act %s target %d dist %.0f los %d yawerr %.1f pitch %.1f->%.1f weapon %d mag %d pos %.0f %.0f partner %.0f hp %.2f\n",
                    mi, sActNames[b->act], (int) b->target, (double) tdist, (int) b->los, (double) b->yawErr, (double) me->vv_verta, (double) wantPitch,
                    (int) me->hands[GUNRIGHT].weaponnum, (int) me->hands[GUNRIGHT].weapon_ammo_in_magazine, (double) me->prop->pos.x,
                    (double) me->prop->pos.z,
                    pa != NULL && pa->prop != NULL ? (double) sqrtf((pa->prop->pos.x - me->prop->pos.x) * (pa->prop->pos.x - me->prop->pos.x) +
                                                                    (pa->prop->pos.z - me->prop->pos.z) * (pa->prop->pos.z - me->prop->pos.z)) : -1.0,
                    (double) me->bondhealth);
    }
}

/* initunk_005520.c: extra spare object model slots on the Colosseum (it has no setup objects, so its 30 spares hold every live enemy's
 * gun, every drop and the centre pickups; with two Bonds and the bigger co-op waves the centre ammo box found none). A stage-load
 * constant, the same on every machine. */
extern s32 portColosseumStage(void);   /* port_flatland.c */
#define COLOSSEUM_EXTRA_OBJ_SLOTS 40

s32 portArenaExtraObjSlots(void)
{
    return portColosseumStage() ? COLOSSEUM_EXTRA_OBJ_SLOTS : 0;
}

/* ---- Lua game half ------------------------------------------------------------------------------------------------------------- */

void portLuaGameCoopRules(const PortArenaCoopRules *r)
{
    if (!portModeActive() || r == NULL) {
        return;
    }
    sCfg = *r;
    if (sCfg.bleed <= 0.0f) sCfg.bleed = 30.0f;
    if (sCfg.revive <= 0.0f) sCfg.revive = 3.0f;
    if (sCfg.radius <= 0.0f) sCfg.radius = 170.0f;
    if (sCfg.reviveHealth <= 0.0f || sCfg.reviveHealth > 1.0f) sCfg.reviveHealth = 0.35f;
    if (sCfg.grace < 0.0f) sCfg.grace = 0.0f;
    portConLogf("ge: arena: coop rules %s: bleed %.0fs revive %.1fs radius %.0f health %.2f grace %.1fs bleedout %d (co-op %d)\n",
                sCfg.on ? "on" : "off", (double) sCfg.bleed, (double) sCfg.revive, (double) sCfg.radius, (double) sCfg.reviveHealth,
                (double) sCfg.grace, sCfg.bleedout, portCoopActive());
}

int portLuaGameCoopActive(void)
{
    return rulesOn();
}

int portLuaGameCoopState(int p, float *secsLeft, float *reviveFrac)
{
    struct player *q = pl(p);

    *secsLeft = 0.0f;
    *reviveFrac = 0.0f;
    if (!portModeActive() || q == NULL || p >= portAllPlayerCount()) {
        return PORT_AC_NONE;
    }
    if (q->bonddead) {
        return sDown[p].held && rulesOn() ? PORT_AC_OUT : PORT_AC_DEAD;
    }
    if (sDown[p].downed && rulesOn()) {
        *secsLeft = sCfg.bleed - (f32) sDown[p].downFields / 60.0f;
        *reviveFrac = sCfg.revive > 0.0f ? (f32) sDown[p].reviveFields / (sCfg.revive * 60.0f) : 0.0f;
        if (*secsLeft < 0.0f) *secsLeft = 0.0f;
        if (*reviveFrac > 1.0f) *reviveFrac = 1.0f;
        return PORT_AC_DOWN;
    }
    return PORT_AC_UP;
}

int portLuaGameCoopForceDown(int p)
{
    if (!rulesOn() || !isUp(p)) {
        return 0;
    }
    goDown(p);
    return 1;
}

int portLuaGameCoopRelease(int p)
{
    if (!rulesOn() || p < 0 || p >= NP || !sDown[p].held) {
        return 0;
    }
    if (sCfg.bleedout == PORT_AC_BLEED_RUN) {
        return 0;   /* out for the run */
    }
    sDown[p].release = 1;
    portCoopFpLog("ge: arena: coop: player %d released: respawns now\n", p);
    return 1;
}

int portLuaGameCoopKill(int p)
{
    if (!portModeActive() || pl(p) == NULL || pl(p)->bonddead) {
        return 0;
    }
    if (p >= 0 && p < NP) {
        sDown[p].downed = 0;
    }
    killBond(p);
    return 1;
}

int portLuaGameCoopBot(int on)
{
    if (!portModeActive()) {
        return 0;
    }
    return portCoopPuppetArena(on);
}

int portLuaGameCoopBotGoal(int p, int set, float x, float z)
{
    if (!portModeActive() || p < 0 || p >= NP) {
        return 0;
    }
    sBot[p].goalSet = (u8) (set != 0);
    sBot[p].goalX = x;
    sBot[p].goalZ = z;
    return 1;
}

int portLuaGameCoopBotInfo(int p, int *target, const char **act, int *ammo, int *weapon, int *shots)
{
    struct player *q = pl(p);
    s32 w, type;

    if (!portModeActive() || q == NULL || p >= NP) {
        return 0;
    }
    w = (s32) q->hands[GUNRIGHT].weaponnum;
    type = w > 0 ? get_ammo_type_for_weapon((ITEM_IDS) w) : 0;
    *target = sBot[p].target;
    *act = sActNames[sBot[p].act < ACT_N ? sBot[p].act : 0];
    *ammo = q->hands[GUNRIGHT].weapon_ammo_in_magazine + ((type > 0 && type < 30) ? q->ammoheldarr[type] : 0);
    *weapon = w;
    *shots = sBot[p].shots;
    return 1;
}

void portLuaGameCoopCounts(int *downs, int *revives, int *bleedouts)
{
    *downs = sDowns;
    *revives = sRevives;
    *bleedouts = sBleeds;
}

#endif /* TARGET_N64 */
