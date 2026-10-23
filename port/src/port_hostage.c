/* port_hostage.c - hostages (WAIT / FOLLOW / EXTRACTED / DEAD) and guarding guards for game modes.
 * See port_hostage.h.
 *
 * The side table is keyed by chrnum; the chr is always looked up by chrnum, never cached. The brain
 * (portHostageChrTick) runs from chrlvActionTick instead of ai(), so it fires when the chr's sleep runs
 * out; its dt comes from sTicks, which portHostageTick (portBlocksTick, first-person gate) advances.
 * FOLLOW chains: each follower trails the nearest earlier follower (by follow order), the first trails
 * Bond. Movement is the bot's: chrGoToBond / chrGoToChr, run, sprint when far, re-plot throttled.
 *
 * Test hooks (env, with GE_MODE set):
 *   GE_MODE_TEST=hostage      hostages A (in front of Bond) and B, a pad hostage C, two guards; use press ->
 *                             A follows, B follows A; log distances (run with GE_DRIVE=1 so Bond walks)
 *   GE_MODE_TEST=hostagenone  no hostage in front: portHostageUse() must NOT consume the press
 *   GE_MODE_TEST=hostagefire  hostage in front of Bond, an AK guard behind it shooting at Bond (stray shots)
 *   GE_HOSTAGE_TEST_USE=joy   hostage test: do not call portHostageUse(); GE_JOYPORT_AUTO=use:<poll> presses it
 *   GE_HOSTAGE_DEBUG=1        log every use press and the brain's moves */
#ifndef TARGET_N64

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ultra64.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include <bondaicommands.h>
#include <math.h>
#include "game/chr.h"
#include "game/chrai.h"
#include "game/chraction.h"
#include "game/player.h"
#include "game/bondview.h"
#include "game/lv.h"
#include "game/stan.h"
#include "game/model.h"
#include "game/objecthandler.h"
#include "game/loadobjectmodel.h"
#include "port_event.h"
#include "port_spawn.h"
#include "port_blocks.h"
#include "port_faction.h"
#include "port_botlogic.h"
#include "port_mode.h"
#include "port_hostage.h"

extern void portConLogf(const char *fmt, ...);   /* port_console.c */
extern s32 getPlayerCount(void);
extern s32 get_is_ramrom_flag(void);
extern s32 get_recording_ramrom_flag(void);
extern enum CAMERAMODE g_CameraMode;             /* bondview2.c */
extern PathRecord *pathFindById(s32 ID);         /* chrai.c */
extern bool portChrGoToProp(ChrRecord *self, PropRecord *target, SPEED speed);   /* chraction.c: a course to a prop, no current-player read */

#define HOSTAGE_CHRNUM0   7200
#define GUARD_CHRNUM0     7300
#define HOSTAGE_USE_RANGE 150.0f
#define HOSTAGE_USE_DOT   0.5f     /* cos 60 deg: "roughly facing" */
#define HOSTAGE_USE_DY    120.0f
#define HOSTAGE_HOLD_DIST 80.0f    /* a follower stops this close to its leader */
#define HOSTAGE_START_DIST 130.0f  /* and starts again from this far */
#define HOSTAGE_SPRINT_DIST 600.0f
#define HOSTAGE_REISSUE   0.5f
#define HOSTAGE_REPLOT    300.0f
#define HOSTAGE_FLASH_SECS 2.0f
#define HOSTAGE_HUD_LINE  2
#define HOSTAGE_MAXDAMAGE 4.0f     /* chrSetMaxDamage units, the vanilla default */

typedef struct
{
    s32 used, chrnum, state, seq;
    s32 leader;                    /* the player index of the Bond that told it to follow (-1 while waiting) */
    s32 brainTicks, issued, navFailed;
    f32 sinceIssue;
    coord3d goal;
} HostageRec;

typedef struct { s32 used, chrnum, pathId, applyAt; } GuardRec;

static HostageRec sH[PORT_HOSTAGE_MAX];
static GuardRec sG[PORT_GUARD_MAX];
static s32 sNextHostage, sNextGuard, sFollowSeq;
static s32 sTicks;                 /* gated sim ticks (1/60 s) since stage start */
static s32 sUsedAt[4] = { -1, -1, -1, -1 };   /* sTicks of each Bond's last consumed use press */
static s32 sDebug = -1;
static f32 sFlash[4];              /* seconds left on each Bond's HUD flash */
static char sFlashText[4][64];
static char sPrevText[4][64];
static s32 sTest;                  /* 0 off, 1 hostage, 2 hostagenone, 3 hostagefire */
static s32 sTestJoy;
static s32 sTestStep[8];
static s32 sTestA = -1, sTestB = -1, sTestC = -1, sTestG1 = -1, sTestG2 = -1, sTestListening;

static s32 hostageDebug(void)
{
    if (sDebug < 0)
    {
        const char *e = getenv("GE_HOSTAGE_DEBUG");

        sDebug = (e != NULL && e[0] != '\0' && e[0] != '0') ? 1 : 0;
    }
    return sDebug;
}

/* Bond p's prop when hostage logic may act on him this tick (a mode, first person, alive, no demo), else NULL. Never the
 * "current" or the local player: under netplay every function here must give the same answer on both machines. */
static PropRecord *hostageGateFor(s32 p)
{
    if (!portModeActive() || !portBondInPlay(p))
    {
        return NULL;
    }
    return g_playerPointers[p]->prop;
}

/* The sim's anchor Bond (lowest index in play): spawns and the test hooks. */
static PropRecord *hostageGate(void)
{
    s32 p, n = portBondCount();

    for (p = 0; p < n; p++)
    {
        PropRecord *bp = hostageGateFor(p);

        if (bp != NULL)
        {
            return bp;
        }
    }
    return NULL;
}

/* Bond p's facing in chr radians (port_bot.c botBondFacing): the chr convention (sin a, cos a). */
static f32 hostageBondFacing(s32 p)
{
    f32 a = (360.0f - g_playerPointers[p]->vv_theta) * (M_TAU_F / 360.0f);

    while (a >= M_TAU_F)
    {
        a -= M_TAU_F;
    }
    while (a < 0.0f)
    {
        a += M_TAU_F;
    }
    return a;
}

static HostageRec *hostageFind(s32 chrnum)
{
    s32 i;

    for (i = 0; i < PORT_HOSTAGE_MAX; i++)
    {
        if (sH[i].used && sH[i].chrnum == chrnum)
        {
            return &sH[i];
        }
    }
    return NULL;
}

s32 portHostageIsHostage(ChrRecord *chr)
{
    HostageRec *h;

    if (chr == NULL || chr->model == NULL || chr->chrnum < HOSTAGE_CHRNUM0 || chr->chrnum >= HOSTAGE_CHRNUM0 + PORT_HOSTAGE_MAX)
    {
        return FALSE;
    }
    h = hostageFind(chr->chrnum);
    return h != NULL && (h->state == PORT_HOSTAGE_WAIT || h->state == PORT_HOSTAGE_FOLLOW);
}

/* chr searches with no team check (sub_GAME_7F033B38: "a guard within N units", used by guard and escort AI lists) must never take a hostage for a
 * guard, as they must not take a Bond's body (portCoopChrIsBondBody). GE_TEST_HOSTAGEPICKOLD=1 (test only, negative control) is the old behaviour. */
s32 portHostageNotAGuard(ChrRecord *chr)
{
    static s32 old = -1;

    if (old < 0)
    {
        const char *e = getenv("GE_TEST_HOSTAGEPICKOLD");

        old = e != NULL && e[0] != '\0' && e[0] != '0';
    }
    return !old && portHostageIsHostage(chr);
}

s32 portHostageState(s32 chrnum)
{
    HostageRec *h = hostageFind(chrnum);

    return h != NULL ? h->state : PORT_HOSTAGE_NONE;
}

s32 portHostageList(s32 *chrnums, s32 *states, s32 max)
{
    s32 i, n = 0;

    for (i = 0; i < PORT_HOSTAGE_MAX && n < max; i++)
    {
        if (sH[i].used)
        {
            if (chrnums != NULL)
            {
                chrnums[n] = sH[i].chrnum;
            }
            if (states != NULL)
            {
                states[n] = sH[i].state;
            }
            n++;
        }
    }
    return n;
}

static const char *stateName(s32 s)
{
    static const char *const names[] = { "WAIT", "FOLLOW", "EXTRACTED", "DEAD" };

    return (s >= 0 && s <= PORT_HOSTAGE_DEAD) ? names[s] : "?";
}

/* The "Hostage following" flash goes to the Bond who pressed, on its own per-player HUD slot (the all-players text of the script is
 * untouched); the previous per-player text comes back afterwards. HUD only: the sim never reads it. */
static void hostageFlash(s32 p, const char *text)
{
    if (p < 0 || p > 3)
    {
        return;
    }
    if (sFlash[p] <= 0.0f)
    {
        const char *cur = portModeHudGetOwn(p, HOSTAGE_HUD_LINE);

        strncpy(sPrevText[p], cur != NULL ? cur : "", sizeof(sPrevText[p]) - 1);   /* put the script's own line back afterwards */
        sPrevText[p][sizeof(sPrevText[p]) - 1] = '\0';
    }
    strncpy(sFlashText[p], text, sizeof(sFlashText[p]) - 1);
    sFlashText[p][sizeof(sFlashText[p]) - 1] = '\0';
    sFlash[p] = HOSTAGE_FLASH_SECS;
    portModeHudSetFor(p, HOSTAGE_HUD_LINE, sFlashText[p]);
}

s32 portHostageLeader(s32 chrnum)
{
    HostageRec *h = hostageFind(chrnum);

    return (h != NULL && h->state == PORT_HOSTAGE_FOLLOW) ? h->leader : -1;
}

s32 portHostageSetStateBy(s32 chrnum, s32 state, s32 player)
{
    HostageRec *h = hostageFind(chrnum);

    if (h == NULL || (state != PORT_HOSTAGE_WAIT && state != PORT_HOSTAGE_FOLLOW) ||
        (h->state != PORT_HOSTAGE_WAIT && h->state != PORT_HOSTAGE_FOLLOW))
    {
        return FALSE;
    }
    if (player < 0 || player > 3)
    {
        player = 0;
    }
    if (h->state == state && !(state == PORT_HOSTAGE_FOLLOW && h->leader != player))
    {
        return FALSE;   /* no change (a FOLLOW that names another leader is a hand-over, which is one) */
    }
    h->state = state;
    h->issued = FALSE;
    h->sinceIssue = 1000.0f;
    if (state == PORT_HOSTAGE_FOLLOW)
    {
        h->seq = ++sFollowSeq;
        h->leader = player;
    }
    else
    {
        h->leader = -1;
    }
    portConLogf("ge: hostage: chr %d -> %s%s, by player %d, t=%d\n", (int)chrnum, stateName(state),
                state == PORT_HOSTAGE_FOLLOW ? " (leader)" : "", (int)player, (int)sTicks);
    portEventEmit(state == PORT_HOSTAGE_FOLLOW ? PORT_EV_HOSTAGE_FOLLOW : PORT_EV_HOSTAGE_WAIT, chrnum, player, 0, 0);
    return TRUE;
}

s32 portHostageSetState(s32 chrnum, s32 state)
{
    return portHostageSetStateBy(chrnum, state, 0);
}

s32 portHostageExtract(s32 chrnum)
{
    HostageRec *h = hostageFind(chrnum);
    ChrRecord *c;

    if (h == NULL || (h->state != PORT_HOSTAGE_WAIT && h->state != PORT_HOSTAGE_FOLLOW))
    {
        return FALSE;
    }
    c = chrFindByLiteralId(chrnum);
    if (c != NULL && c->prop != NULL)
    {
        c->hidden |= CHRHIDDEN_REMOVE;   /* the AI_ChrRemoveInstant removal, as portBotDismiss */
    }
    h->state = PORT_HOSTAGE_EXTRACTED;
    portConLogf("ge: hostage: chr %d EXTRACTED, t=%d\n", (int)chrnum, (int)sTicks);
    return TRUE;
}

/* ---- spawning ---- */

static ChrRecord *spawnAt(s32 body, s32 chrnum, s32 ailist, f32 maxdamage, const coord3d *pos, StandTile *stan,
                          f32 angle, s32 weaponprop, s32 weaponitem)
{
    PortSpawnDesc d;
    ChrRecord *chr;
    PropRecord *bp = hostageGate();

    if (bp == NULL)
    {
        portConLogf("ge: hostage: spawn refused: no first-person mission right now\n");
        return NULL;
    }
    memset(&d, 0, sizeof(d));
    d.body = body;
    d.head = -1;   /* random head: head 0 is a whole body (port_spawn.h) */
    d.nearprop = bp;
    d.angle = angle;
    d.ailistid = ailist;
    d.maxdamage = maxdamage;
    d.chrnum = chrnum;
    chr = portSpawnChrAt(&d, pos, stan);
    if (chr != NULL && weaponprop >= 0)
    {
        chrGiveWeapon(chr, weaponprop, (ITEM_IDS)weaponitem, 0);
    }
    return chr;
}

static s32 hostageSpawnAt(const coord3d *pos, StandTile *stan, f32 angle)
{
    static const s32 bodies[] = { BODY_Scientist_1_Male, BODY_Civilian_1_Female, BODY_Scientist_2_Female, BODY_Civilian_2 };
    HostageRec *h;
    ChrRecord *chr;

    if (sNextHostage >= PORT_HOSTAGE_MAX)
    {
        portConLogf("ge: hostage: spawn refused: table full\n");
        return -1;
    }
    chr = spawnAt(bodies[sNextHostage % 4], HOSTAGE_CHRNUM0 + sNextHostage, GAILIST_COOP_BOT, HOSTAGE_MAXDAMAGE, pos,
                  stan, angle, -1, 0);
    if (chr == NULL)
    {
        return -1;
    }
    h = &sH[sNextHostage++];
    memset(h, 0, sizeof(*h));
    h->used = TRUE;
    h->chrnum = chr->chrnum;
    h->state = PORT_HOSTAGE_WAIT;
    h->leader = -1;
    h->brainTicks = sTicks;
    h->sinceIssue = 1000.0f;
    portFactionSetChr(chr, PORT_FACTION_HOSTAGE);
    portConLogf("ge: hostage: spawned chr %d at (%.0f %.0f %.0f), t=%d\n", (int)chr->chrnum, chr->prop->pos.x,
                chr->prop->pos.y, chr->prop->pos.z, (int)sTicks);
    return chr->chrnum;
}

s32 portHostageSpawn(s32 pad)
{
    coord3d pos;
    StandTile *stan = NULL;
    f32 angle = 0.0f;

    if (!portPadStan(pad, &pos, &stan, &angle))
    {
        portConLogf("ge: hostage: spawn at pad %d refused: invalid pad or no floor\n", (int)pad);
        return -1;
    }
    return hostageSpawnAt(&pos, stan, angle);
}

static s32 guardWeaponProp(s32 item)
{
    switch (item)
    {
        case ITEM_SHOTGUN: return PROP_CHRSHOTGUN;
        case ITEM_WPPK:    return PROP_CHRWPPK;
        case ITEM_MP5K:    return PROP_CHRMP5K;
        case ITEM_M16:     return PROP_CHRM16;
        case ITEM_UZI:     return PROP_CHRUZI;
        case ITEM_TT33:    return PROP_CHRTT33;
        default:           return PROP_CHRKALASH;
    }
}

static s32 guardSpawnAt(const coord3d *pos, StandTile *stan, f32 angle, s32 patrolPath, s32 weaponItem)
{
    static const s32 bodies[] = { BODY_Russian_Soldier, BODY_Siberian_Guard_2, BODY_Janus_Special_Forces };
    s32 prop, item;
    ChrRecord *chr;
    GuardRec *g;

    if (sNextGuard >= PORT_GUARD_MAX)
    {
        portConLogf("ge: hostage: guard spawn refused: table full\n");
        return -1;
    }
    prop = guardWeaponProp(weaponItem);
    item = (prop == PROP_CHRKALASH) ? ITEM_AK47 : weaponItem;
    /* the fire test needs a guard that attacks at once; SIMPLE_GUARD waits for a sighting */
    chr = spawnAt(bodies[sNextGuard % 3], GUARD_CHRNUM0 + sNextGuard,
                  sTest == 3 ? GAILIST_PERSISTENTLY_CHASE_AND_ATTACK_BOND : GAILIST_SIMPLE_GUARD, 0.0f, pos, stan, angle,
                  prop, item);
    if (chr == NULL)
    {
        return -1;
    }
    g = &sG[sNextGuard++];
    g->used = TRUE;
    g->chrnum = chr->chrnum;
    g->pathId = patrolPath;
    g->applyAt = sTicks + 2;
    portConLogf("ge: hostage: guard chr %d (item %d, %s) at (%.0f %.0f %.0f), t=%d\n", (int)chr->chrnum, (int)item,
                patrolPath >= 0 ? "patrol" : "hold", chr->prop->pos.x, chr->prop->pos.y, chr->prop->pos.z, (int)sTicks);
    return chr->chrnum;
}

s32 portGuardSpawn(s32 pad, s32 patrolPath, s32 weaponItem)
{
    coord3d pos;
    StandTile *stan = NULL;
    f32 angle = 0.0f;

    if (!portPadStan(pad, &pos, &stan, &angle))
    {
        portConLogf("ge: hostage: guard spawn at pad %d refused: invalid pad or no floor\n", (int)pad);
        return -1;
    }
    return guardSpawnAt(&pos, stan, angle, patrolPath, weaponItem);
}

/* Patrol starts a tick or two after spawn, once the chr has left its init action. */
static void guardsTick(void)
{
    s32 i;

    for (i = 0; i < sNextGuard; i++)
    {
        GuardRec *g = &sG[i];
        ChrRecord *c;
        PathRecord *path;

        if (!g->used || g->pathId < 0 || sTicks < g->applyAt)
        {
            continue;
        }
        c = chrFindByLiteralId(g->chrnum);
        if (c != NULL && c->model != NULL && !chrIsDead(c))
        {
            path = pathFindById(g->pathId);
            if (path != NULL)
            {
                if_actor_able_set_on_path(c, (s32)(intptr_t)path);
                portConLogf("ge: hostage: guard chr %d on patrol path %d (action %d)\n", (int)g->chrnum, (int)g->pathId,
                            (int)c->actiontype);
            }
            else
            {
                portConLogf("ge: hostage: guard chr %d: no patrol path %d, holding\n", (int)g->chrnum, (int)g->pathId);
            }
        }
        g->pathId = -1;   /* once */
    }
}

/* ---- use press ---- */

s32 portHostageUse(void)
{
    /* The press belongs to the Bond whose input is being processed right now (the current player of that Bond's own move tick: its
     * record, on both machines under netplay), not to the local player. */
    const s32 pl = (s32) get_cur_playernum();
    PropRecord *bp = (pl >= 0 && pl < 4) ? hostageGateFor(pl) : NULL;
    HostageRec *best = NULL;
    f32 bestd = 1e9f, face, bondfloor;
    s32 i;

    if (bp == NULL || sNextHostage == 0)
    {
        return FALSE;
    }
    face = hostageBondFacing(pl);
    bondfloor = stanGetPositionYValue(bp->stan, bp->pos.x, bp->pos.z);
    for (i = 0; i < PORT_HOSTAGE_MAX; i++)
    {
        HostageRec *h = &sH[i];
        ChrRecord *c;
        f32 dx, dz, d, dy;

        if (!h->used || (h->state != PORT_HOSTAGE_WAIT && h->state != PORT_HOSTAGE_FOLLOW))
        {
            continue;
        }
        c = chrFindByLiteralId(h->chrnum);
        if (c == NULL || c->model == NULL || c->prop == NULL || chrIsDead(c))
        {
            continue;
        }
        dx = c->prop->pos.x - bp->pos.x;
        dz = c->prop->pos.z - bp->pos.z;
        d = sqrtf(dx * dx + dz * dz);
        dy = c->ground - bondfloor;
        if (d > HOSTAGE_USE_RANGE || dy > HOSTAGE_USE_DY || dy < -HOSTAGE_USE_DY)
        {
            continue;
        }
        if (d > 30.0f && (dx * sinf(face) + dz * cosf(face)) / d < HOSTAGE_USE_DOT)
        {
            continue;
        }
        if (d < bestd)
        {
            bestd = d;
            best = h;
        }
    }
    if (best != NULL && sUsedAt[pl] == sTicks)
    {
        return TRUE;   /* the Port use key and B in the same tick: one toggle per Bond only */
    }
    if (best == NULL)
    {
        if (hostageDebug())
        {
            portConLogf("ge: hostage: use press by player %d: no hostage in reach, not consumed, t=%d\n", (int)pl, (int)sTicks);
        }
        return FALSE;
    }
    if (best->state == PORT_HOSTAGE_WAIT || best->leader != pl)
    {   /* waiting -> follows this Bond; following the OTHER Bond -> it follows this one from now on */
        portHostageSetStateBy(best->chrnum, PORT_HOSTAGE_FOLLOW, pl);
        hostageFlash(pl, "Hostage following");
    }
    else
    {
        portHostageSetStateBy(best->chrnum, PORT_HOSTAGE_WAIT, pl);
        hostageFlash(pl, "Hostage waiting");
    }
    sUsedAt[pl] = sTicks;
    portConLogf("ge: hostage: use press by player %d consumed by chr %d (dist %.0f), t=%d\n", (int)pl, (int)best->chrnum, bestd, (int)sTicks);
    return TRUE;
}

/* ---- brain ---- */

/* The nearest earlier follower OF THE SAME LEADER by follow order (a chain per Bond); NULL = trail the Bond itself. */
static HostageRec *leaderOf(HostageRec *h)
{
    HostageRec *best = NULL;
    s32 i;

    for (i = 0; i < PORT_HOSTAGE_MAX; i++)
    {
        HostageRec *o = &sH[i];

        if (o->used && o != h && o->state == PORT_HOSTAGE_FOLLOW && o->leader == h->leader && o->seq < h->seq &&
            (best == NULL || o->seq > best->seq))
        {
            best = o;
        }
    }
    return best;   /* NULL = Bond */
}

/* The Bond a follower trails: the one that told it to follow; while that one is dead (waiting to respawn) or out, the lowest-index
 * Bond still in play. -1 = nobody in play (the brain idles). A function of player indices and sim state only. */
static s32 hostageBondToTrail(const HostageRec *h)
{
    s32 p, n = portBondCount();

    if (h->leader >= 0 && h->leader < n && hostageGateFor(h->leader) != NULL)
    {
        return h->leader;
    }
    for (p = 0; p < n; p++)
    {
        if (hostageGateFor(p) != NULL)
        {
            return p;
        }
    }
    return -1;
}

void portHostageChrTick(ChrRecord *self)
{
    HostageRec *h = hostageFind(self->chrnum);
    PropRecord *bp;
    HostageRec *lh;
    ChrRecord *lc = NULL;
    coord3d goal;
    f32 dt, dx, dy, dz, dist;
    s32 moving, lp;

    if (h == NULL || self->prop == NULL || self->prop->stan == NULL || chrIsDead(self))
    {
        return;
    }
    lp = portModeActive() ? hostageBondToTrail(h) : -1;
    bp = lp >= 0 ? g_playerPointers[lp]->prop : NULL;
    if (bp == NULL)
    {
        return;   /* nobody in first-person play */
    }
    dt = (f32)(sTicks - h->brainTicks) / 60.0f;
    if (dt <= 0.0f)
    {
        return;   /* the spawn tick */
    }
    h->brainTicks = sTicks;
    h->sinceIssue += dt;
    moving = self->actiontype == ACT_GOPOS || self->actiontype == ACT_RUNPOS;
    if (h->state != PORT_HOSTAGE_FOLLOW)
    {
        if (h->issued && moving)
        {
            chraiStopAnimation(self);
        }
        h->issued = FALSE;
        return;
    }

    lh = leaderOf(h);
    if (lh != NULL)
    {
        lc = chrFindByLiteralId(lh->chrnum);
        if (lc == NULL || lc->prop == NULL || lc->model == NULL || chrIsDead(lc))
        {
            lc = NULL;   /* the portHostageTick sweep marks it DEAD next tick; trail Bond meanwhile */
        }
    }
    if (lc != NULL)
    {
        goal = lc->prop->pos;
        dy = lc->ground - self->ground;
    }
    else
    {
        goal = bp->pos;
        dy = stanGetPositionYValue(bp->stan, bp->pos.x, bp->pos.z) - self->ground;   /* Bond's prop y is eye level */
    }
    dx = goal.x - self->prop->pos.x;
    dz = goal.z - self->prop->pos.z;
    dist = sqrtf(dx * dx + dy * dy + dz * dz);

    if (dist <= HOSTAGE_HOLD_DIST || (!h->issued && dist < HOSTAGE_START_DIST))
    {
        if (h->issued)
        {
            chraiStopAnimation(self);
            h->issued = FALSE;
            if (hostageDebug())
            {
                portConLogf("ge: hostage: chr %d stops %.0f from %s, t=%d\n", (int)self->chrnum, dist,
                            lc != NULL ? "its leader" : "Bond", (int)sTicks);
            }
        }
        return;
    }
    {
        f32 gx = goal.x - h->goal.x;
        f32 gz = goal.z - h->goal.z;

        if (!h->issued || (h->sinceIssue >= HOSTAGE_REISSUE &&
                           (!moving || h->navFailed || gx * gx + gz * gz > HOSTAGE_REPLOT * HOSTAGE_REPLOT)))
        {
            SPEED speed = dist > HOSTAGE_SPRINT_DIST ? SPEED_SPRINT : SPEED_RUN;   /* Bond outruns a run */

            /* the course goes to the chosen leader's prop (a chain member, or THE Bond that leads this hostage), not to the "current
             * player", which here is whichever Bond is nearest the hostage */
            h->navFailed = portChrGoToProp(self, lc != NULL ? lc->prop : bp, speed) ? FALSE : TRUE;
            h->goal = goal;
            h->issued = TRUE;
            h->sinceIssue = 0.0f;
            if (hostageDebug())
            {
                portConLogf("ge: hostage: chr %d %s %s dist %.0f nav %s, t=%d\n", (int)self->chrnum,
                            speed == SPEED_SPRINT ? "sprints to" : "runs to", lc != NULL ? "its leader" : "Bond", dist,
                            h->navFailed ? "FAIL" : "ok", (int)sTicks);
            }
        }
    }
}

/* ---- tick ---- */

static void testTick(PropRecord *bp);

/* Before the first-person gate (like the CHR_DIED poll), so a kill in the death cam is seen at once. */
void portHostageDeadSweep(void)
{
    s32 i;

    for (i = 0; i < PORT_HOSTAGE_MAX; i++)
    {
        HostageRec *h = &sH[i];
        ChrRecord *c;

        if (!h->used || (h->state != PORT_HOSTAGE_WAIT && h->state != PORT_HOSTAGE_FOLLOW))
        {
            continue;
        }
        c = chrFindByLiteralId(h->chrnum);
        if (c == NULL || chrIsDead(c))
        {
            h->state = PORT_HOSTAGE_DEAD;   /* CHR_DIED comes from port_blocks' own poll */
            portConLogf("ge: hostage: chr %d DEAD, t=%d\n", (int)h->chrnum, (int)sTicks);
        }
    }
}

void portHostageTick(void)
{
    s32 i;
    f32 dt = (f32)g_ClockTimer / 60.0f;

    (void)i;
    sTicks += g_ClockTimer;
    guardsTick();
    for (i = 0; i < 4; i++)
    {
        if (sFlash[i] > 0.0f)
        {
            sFlash[i] -= dt;
            if (sFlash[i] <= 0.0f)
            {
                const char *cur = portModeHudGetOwn(i, HOSTAGE_HUD_LINE);

                if (cur != NULL && strcmp(cur, sFlashText[i]) == 0)
                {
                    portModeHudSetFor(i, HOSTAGE_HUD_LINE, sPrevText[i][0] != '\0' ? sPrevText[i] : NULL);   /* only if the script did not take the line meanwhile */
                }
            }
        }
    }
    {   /* GE_TEST_CHRSEARCH=<tick>:<chrnum>:<dist> (test): that chr runs the "guard within <dist> units" search of the guard / escort AI lists */
        static const char *cs;
        static s32 csInit;

        if (!csInit)
        {
            csInit = 1;
            cs = getenv("GE_TEST_CHRSEARCH");
        }
        if (cs != NULL && cs[0] != '\0')
        {
            unsigned t = 0;
            int cn = 0;
            float dist = 0.0f;

            if (sscanf(cs, "%u:%d:%f", &t, &cn, &dist) == 3 && (unsigned) sTicks == t)
            {
                ChrRecord *g = chrFindByLiteralId(cn);

                if (g != NULL && g->model != NULL && g->prop != NULL && g->prop->stan != NULL)
                {
                    s32 found = sub_GAME_7F033B38(g, dist) ? 1 : 0;

                    printf("ge: hostage: test: chr search from chr %d within %.0f -> %s%d (hostage state %d)\n", (int)cn, dist,
                           found ? "chr " : "nobody ", found ? (int)g->chrpreset1 : 0, found ? (int)portHostageState(g->chrpreset1) : -1);
                    fflush(stdout);
                }
            }
        }
    }
    {   /* GE_HOSTAGE_TEST_KILL=n: kill the first n hostages, one every 2 s from 10 s (mode test) */
        static s32 sKillN = -1, sKilled;
        const char *k = getenv("GE_HOSTAGE_TEST_KILL");

        if (sKillN < 0)
        {
            sKillN = k != NULL ? atoi(k) : 0;
        }
        if (sKillN > 0 && sKilled < sKillN && sTicks >= 600 + 120 * sKilled)
        {
            PropRecord *bp = hostageGate();
            ChrRecord *c = chrFindByLiteralId(HOSTAGE_CHRNUM0 + sKilled);

            if (bp != NULL && c != NULL && c->model != NULL && !chrIsDead(c))
            {
                printf("ge: hostage: test: killing hostage chr %d\n", (int)c->chrnum);
                chrlvExplosionDamage(c, &bp->pos, c->maxdamage + 1.0f, TRUE);
                sKilled++;
            }
        }
    }
    if (sTest > 0)
    {
        PropRecord *bp = hostageGate();

        if (bp != NULL)
        {
            testTick(bp);
        }
    }
}

/* ---- test hooks ---- */

static void testListener(int type, int a, int b, int c, int d, void *ud)
{
    (void)ud;
    printf("ge: hostage: event %s %d %d %d (t=%d)\n", portEventName(type), a, b, c, (int)sTicks);
    fflush(stdout);
}

/* A clear spot `dist` from Bond along `angle`. */
static s32 testRing(PropRecord *bp, f32 angle, f32 dist, coord3d *pos, StandTile **stan)
{
    return portSpawnRingPoint(bp, angle, dist, pos, stan);
}

static f32 testDist(s32 a, s32 b)   /* chr b == -1: Bond */
{
    ChrRecord *ca = chrFindByLiteralId(a);
    ChrRecord *cb = b >= 0 ? chrFindByLiteralId(b) : NULL;
    PropRecord *bp = getCurrentPlayerProp();
    f32 dx, dz;

    if (ca == NULL || ca->prop == NULL || (b >= 0 && (cb == NULL || cb->prop == NULL)) || bp == NULL)
    {
        return -1.0f;
    }
    dx = (b >= 0 ? cb->prop->pos.x : bp->pos.x) - ca->prop->pos.x;
    dz = (b >= 0 ? cb->prop->pos.z : bp->pos.z) - ca->prop->pos.z;
    return sqrtf(dx * dx + dz * dz);
}

/* Test teleport: Bond to pad `pad`, placed like the MP respawn (port_bot.c botStrandBond). */
static void testTeleportBond(PropRecord *bp, s32 pad, f32 offx, f32 offz)
{
#ifdef VERSION_EU
    const f32 f6c = 0.20039999485f, f3b8 = 0.118799984f;
#else
    const f32 f6c = 0.170000016689f, f3b8 = 0.100000024f;
#endif
    PadRecord *p = &g_CurrentSetup.pads[pad];
    StandTile *st;
    f32 floor;
    coord3d pos, theta;

    if (p->stan == NULL)
    {
        printf("ge: hostage: test: teleport: pad %d has no stan\n", (int)pad);
        return;
    }
    st = p->stan;
    pos.x = p->pos.x;
    pos.z = p->pos.z;
    if (offx != 0.0f || offz != 0.0f)
    {   /* A5 test helper: an offset from the pad centre, with the stan tile it really lands on (a pad centre sits on a tile edge) */
        coord3d want, got;
        StandTile *gst = NULL;

        want = p->pos;
        want.x += offx;
        want.z += offz;
        if (getposstan(&want, p->stan, 20.0f, &got, &gst) != 0 && gst != NULL)
        {
            pos.x = got.x;
            pos.z = got.z;
            st = gst;
        }
    }
    floor = stanGetPositionYValue(st, pos.x, pos.z);
    pos.y = g_CurrentPlayer->eyeheight + floor;
    theta = g_CurrentPlayer->field_488.theta_transform;
    g_CurrentPlayer->field_70 = floor;
    g_CurrentPlayer->stanHeight = floor;
    g_CurrentPlayer->field_6C = (s32)(floor / f6c);
    change_player_pos_to_target(&g_CurrentPlayer->field_488, &pos, st);
    g_CurrentPlayer->field_488.theta_transform = theta;
    bp->pos = pos;
    g_CurrentPlayer->bondprevpos = pos;
    bp->stan = st;
    g_CurrentPlayer->field_3B8.x = g_CurrentPlayer->field_488.pos.x / f3b8;
    g_CurrentPlayer->field_3B8.y = g_CurrentPlayer->field_488.pos.y / f3b8;
    g_CurrentPlayer->field_3B8.z = g_CurrentPlayer->field_488.pos.z / f3b8;
    printf("ge: hostage: test: Bond teleported to pad %d (%.0f %.0f %.0f) (t=%d)\n", (int)pad, pos.x, pos.y, pos.z, (int)sTicks);
}

/* A5 test helper for the arena Lua API (bond.warp): the same placement. Caller selects the player (set_cur_player) first. */
void portTeleportBondToPad(PropRecord *bp, s32 pad, f32 offx, f32 offz)
{
    testTeleportBond(bp, pad, offx, offz);
}

static void testListen(void)
{
    if (!sTestListening)
    {
        s32 t;

        sTestListening = TRUE;
        for (t = PORT_EV_CHR_DIED; t < PORT_EV_COUNT; t++)
        {
            portEventOn(t, testListener, NULL);
        }
    }
}

static void testTick(PropRecord *bp)
{
    coord3d pos, bpos;
    StandTile *stan = NULL;
    f32 face = hostageBondFacing(0);   /* the test hooks are solo / player 0 */

    testListen();
    if (sTest == 3)
    {   /* hostagefire: a guard 4xx units ahead shoots at Bond with the hostage between them */
        static const f32 gd[] = { 500.0f, 400.0f, 300.0f, 250.0f };
        s32 k;

        if (sTicks >= 30 && !sTestStep[0]++)
        {
            if (testRing(bp, face, 120.0f, &pos, &stan))
            {
                sTestA = hostageSpawnAt(&pos, stan, face + M_PI_F);
            }
            for (k = 0; k < 4 && sTestG1 < 0; k++)
            {
                if (testRing(bp, face, gd[k], &pos, &stan))
                {
                    sTestG1 = guardSpawnAt(&pos, stan, face + M_PI_F, -1, ITEM_AK47);
                    printf("ge: hostage: test: hostage %d, guard %d at %.0f ahead\n", (int)sTestA, (int)sTestG1, gd[k]);
                }
            }
        }
        if (sTicks >= 60 && (sTicks % 30) == 0 && sTestA >= 0)
        {
            ChrRecord *c = chrFindByLiteralId(sTestA);
            ChrRecord *g = chrFindByLiteralId(sTestG1);

            if (g != NULL && g->prop != NULL)
            {
                printf("ge: hostage: test: guard %d action %d at (%.0f %.0f) Bond-dist %.0f (t=%d)\n", (int)sTestG1,
                       (int)g->actiontype, g->prop->pos.x, g->prop->pos.z, testDist(sTestG1, -1), (int)sTicks);
            }
            if (c != NULL)
            {
                printf("ge: hostage: test: hostage %d damage %.2f/%.2f state %s (t=%d)\n", (int)sTestA, c->damage,
                       c->maxdamage, stateName(portHostageState(sTestA)), (int)sTicks);
            }
            fflush(stdout);
        }
        return;
    }
    if (sTicks >= 30 && !sTestStep[0]++)
    {
        s32 ids[16];
        s32 n;

        portBondPos(&bpos);
        n = portPadList(&bpos, 600.0f, 4000.0f, TRUE, ids, 16);
        if (sTest == 2)
        {   /* hostagenone: one hostage far away, behind nothing: the press must not be consumed */
            sTestC = n > 0 ? portHostageSpawn(ids[0]) : -1;
            printf("ge: hostage: test: far hostage %d\n", (int)sTestC);
            return;
        }
        if (testRing(bp, face, 100.0f, &pos, &stan))
        {
            sTestA = hostageSpawnAt(&pos, stan, face + M_PI_F);
        }
        {
            s32 k;

            for (k = 0; k < 8 && sTestB < 0; k++)   /* any clear direction but straight ahead */
            {
                if (testRing(bp, face + M_PI_F * 0.5f + (f32)k * (M_PI_F * 0.25f), 110.0f, &pos, &stan))
                {
                    sTestB = hostageSpawnAt(&pos, stan, 0.0f);
                }
            }
        }
        sTestC = n > 0 ? portHostageSpawn(ids[0]) : -1;
        sTestG1 = n > 1 ? portGuardSpawn(ids[n - 1], -1, ITEM_AK47) : -1;
        {
            const PathRecord *pr = g_CurrentSetup.patrolpaths;

            sTestG2 = (n > 2 && pr != NULL && pr[0].waypoints != NULL) ? portGuardSpawn(ids[n - 2], pr[0].ID, ITEM_SHOTGUN) : -1;
        }
        portModeHudSet(0, "TEST: hostages");
        printf("ge: hostage: test: pads=%d hostages A=%d B=%d C=%d guards hold=%d patrol=%d\n", (int)n, (int)sTestA,
               (int)sTestB, (int)sTestC, (int)sTestG1, (int)sTestG2);
    }
    if (sTicks >= 60 && !sTestStep[1]++)
    {
        if (sTest == 2 || sTestJoy)
        {
            if (sTest == 2)
            {
                printf("ge: hostage: test: use with nothing in reach -> %d (want 0)\n", (int)portHostageUse());
            }
        }
        else
        {
            printf("ge: hostage: test: use A -> %d (want 1), state A %s\n", (int)portHostageUse(),
                   stateName(portHostageState(sTestA)));
        }
    }
    if (sTest == 1 && sTicks >= 70 && !sTestStep[2]++)
    {
        printf("ge: hostage: test: B -> FOLLOW (chain behind A) %d\n", (int)portHostageSetState(sTestB, PORT_HOSTAGE_FOLLOW));
    }
    if (sTest == 1 && (sTicks == 200 || sTicks == 600))
    {   /* Bond jumps to a waypoint pad 1200..2500 away; the followers must run after him */
        s32 ids[16];
        s32 n;

        portBondPos(&bpos);
        n = portPadList(&bpos, 1200.0f, 2500.0f, TRUE, ids, 16);
        if (n > 0)
        {
            testTeleportBond(bp, ids[(sTicks == 200) ? 0 : n - 1], 0.0f, 0.0f);
        }
    }
    if (sTest == 1 && sTicks >= 90 && (sTicks % 60) == 0)
    {
        printf("ge: hostage: test: t=%d A-Bond %.0f B-A %.0f B-Bond %.0f C-Bond %.0f states A=%s B=%s C=%s\n", (int)sTicks,
               testDist(sTestA, -1), testDist(sTestB, sTestA), testDist(sTestB, -1), testDist(sTestC, -1),
               stateName(portHostageState(sTestA)),
               stateName(portHostageState(sTestB)), stateName(portHostageState(sTestC)));
        fflush(stdout);
    }
    if (sTest == 1 && (sTicks == 300 || sTicks == 900))
    {
        s32 g;

        for (g = 0; g < 2; g++)
        {
            ChrRecord *c = chrFindByLiteralId(g == 0 ? sTestG1 : sTestG2);

            if (c != NULL)
            {
                printf("ge: hostage: test: guard %d action %d at (%.0f %.0f) (t=%d)\n", (int)c->chrnum, (int)c->actiontype,
                       c->prop->pos.x, c->prop->pos.z, (int)sTicks);
            }
        }
    }
    if (sTest == 1 && sTicks >= 1100 && !sTestStep[3]++)
    {
        printf("ge: hostage: test: A -> WAIT %d\n", (int)portHostageSetState(sTestA, PORT_HOSTAGE_WAIT));
    }
    if (sTest == 1 && sTicks >= 1150 && !sTestStep[4]++)
    {
        printf("ge: hostage: test: extract B %d\n", (int)portHostageExtract(sTestB));
    }
}

/* ---- lifecycle ---- */

void portHostageStageStart(int stage)
{
    const char *e = getenv("GE_MODE_TEST");
    const char *j = getenv("GE_HOSTAGE_TEST_USE");

    (void)stage;
    memset(sH, 0, sizeof(sH));
    memset(sG, 0, sizeof(sG));
    sNextHostage = 0;
    sNextGuard = 0;
    sFollowSeq = 0;
    sTicks = 0;
    {
        s32 i;

        for (i = 0; i < 4; i++)
        {
            sUsedAt[i] = -1;
            sFlash[i] = 0.0f;
            sFlashText[i][0] = '\0';
            sPrevText[i][0] = '\0';
        }
    }
    sTest = (e != NULL && strcmp(e, "hostage") == 0) ? 1 : (e != NULL && strcmp(e, "hostagenone") == 0) ? 2
          : (e != NULL && strcmp(e, "hostagefire") == 0) ? 3 : 0;
    sTestJoy = j != NULL && strcmp(j, "joy") == 0;
    memset(sTestStep, 0, sizeof(sTestStep));
    sTestA = sTestB = sTestC = sTestG1 = sTestG2 = -1;
}

#endif /* TARGET_N64 */
