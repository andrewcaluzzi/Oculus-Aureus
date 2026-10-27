/* port_blocks.c - game-mode building blocks (port_blocks.h): pads, zones, HUD text, and the watcher
 * that emits CHR_DIED / ZONE_* / STAGE_START / BOND_DIED. Everything is gated by portModeActive(),
 * so with no GE_MODE the tick returns at once and nothing here touches game state.
 *
 * Test hook: GE_MODE_TEST=blocks (with GE_MODE set) spawns two guards at far waypoint pads, adds
 * zones, sets HUD text, kills the guards and moves a zone, logging every event as "ge: mode: event". */
#ifndef TARGET_N64

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ultra64.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include <bondaicommands.h>
#include <math.h>
#include "game/chr.h"
#include "game/chraction.h"
#include "game/chrai.h"
#include "game/propobj.h"
#include "game/objective_status.h"
#include "game/player.h"
#include "game/bondview.h"
#include "game/lv.h"
#include "game/stan.h"
#include "game/loadobjectmodel.h"
#include "game/objecthandler.h"
#include "game/math_atan2f.h"
#include "port_event.h"
#include "port_chrthrow.h"
#include "port_spawn.h"
#include "port_blocks.h"
#include "port_hostage.h"
#include "port_mode.h"
#include "port_lua.h"
#include "port_coop.h"

extern int portCoopAllDead(void);   /* port_coop.c: every Bond down and nobody left to respawn (the co-op mission-lost test) */
extern s32 getPlayerCount(void);
extern s32 get_is_ramrom_flag(void);
extern s32 get_recording_ramrom_flag(void);
extern s32 chrGetNumFree(void);
extern s32 get_difficulty_for_objective(s32 objectiveIndex);   /* objective_status.c, in no header */

#define PAD_SANE_MAX     20000
#define HUD_LEN          64
#define WATCH_MAX        32
#define SLOT_MAX         512
#define HIT_MAX          64

typedef struct Zone
{
    char name[24];
    coord3d pos;
    f32 radius;
    s32 used;
    u32 bondIn;  /* bit per Bond (player index) */
    u32 chrIn;   /* bit per watched chr */
} Zone;

#define HUD_SLOTS 5   /* 0 = every player, 1 + p = player p */

static Zone sZone[PORT_ZONE_MAX];
static s32 sWatch[WATCH_MAX];
static s32 sWatchUsed[WATCH_MAX];
static char sHud[HUD_SLOTS][PORT_HUD_LINES][HUD_LEN];
static char sPanel[HUD_SLOTS][PORT_HUD_PANEL_LINES][HUD_LEN];   /* hud.panel (the heist's intel box) */
static s32 sStage;
static s32 sStarted;        /* STAGE_START emitted */
static s32 sTicks;          /* gated sim ticks since STAGE_START */
static s32 sBondDead[4];
static s32 sSlotNum[SLOT_MAX];
static u8  sSlotDead[SLOT_MAX];
static s32 sHitVictim[HIT_MAX], sHitAttacker[HIT_MAX], sHitNext;
static s32 sTestStep[4];    /* GE_MODE_TEST one-shot steps */

/* ---- pads ---- */

s32 portPadCount(void)
{
    const PadRecord *p = g_CurrentSetup.pads;
    const PadRecord *b = (const PadRecord *)g_CurrentSetup.boundpads;

    /* no cache: a subtraction per call, so a stage change can never leave a stale count */
    if (p != NULL && b != NULL && b > p && (b - p) < PAD_SANE_MAX)
    {
        return (s32)(b - p);   /* the pad table runs up to the bound pad table */
    }
    if (p != NULL)
    {   /* a pad table that is not the setup file's own (Dam+ appends pads in a copy, port_damplus.c): count to its NULL-name
         * terminator, and count the terminator too, as the subtraction above does */
        s32 n = 0;

        while (n < PAD_SANE_MAX && p[n].plink != NULL)
        {
            n++;
        }
        return (n < PAD_SANE_MAX) ? n + 1 : 0;
    }
    return 0;
}

static s32 padOk(s32 pad)
{
    const PadRecord *p;

    if (pad < 0 || pad >= portPadCount())
    {
        return FALSE;
    }
    p = &g_CurrentSetup.pads[pad];
    return p->stan != NULL && p->pos.x > -1e7f && p->pos.x < 1e7f && p->pos.y > -1e7f && p->pos.y < 1e7f
        && p->pos.z > -1e7f && p->pos.z < 1e7f;
}

s32 portPadPos(s32 pad, coord3d *pos)
{
    if (!padOk(pad))
    {
        return FALSE;
    }
    *pos = g_CurrentSetup.pads[pad].pos;
    return TRUE;
}

s32 portPadStan(s32 pad, coord3d *pos, StandTile **stan, f32 *angle)
{
    PadRecord *p;

    if (!padOk(pad))
    {
        return FALSE;
    }
    p = &g_CurrentSetup.pads[pad];
    if (getposstan(&p->pos, p->stan, 20.0f, pos, stan) == 0)
    {
        return FALSE;
    }
    if (angle != NULL)
    {
        *angle = atan2f(p->look.f[0], p->look.f[2]);
    }
    return TRUE;
}

f32 portPadDist(s32 a, s32 b)
{
    coord3d pa, pb;
    f32 dx, dy, dz;

    if (!portPadPos(a, &pa) || !portPadPos(b, &pb))
    {
        return -1.0f;
    }
    dx = pa.x - pb.x;
    dy = pa.y - pb.y;
    dz = pa.z - pb.z;
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

s32 portPadList(const coord3d *from, f32 mindist, f32 maxdist, s32 wponly, s32 *out, s32 max)
{
    s32 n = 0;

    if (wponly)
    {
        waypoint *wp;

        for (wp = g_CurrentSetup.pathwaypoints; wp != NULL && wp->padID >= 0 && n < max; wp++)
        {
            coord3d pp;
            f32 dx, dz, d;

            if (!portPadPos(wp->padID, &pp))
            {
                continue;
            }
            dx = pp.x - from->x;
            dz = pp.z - from->z;
            d = sqrtf(dx * dx + dz * dz);
            if (d >= mindist && d <= maxdist)
            {
                out[n++] = wp->padID;
            }
        }
    }
    else
    {
        s32 i, count = portPadCount();

        for (i = 0; i < count && n < max; i++)
        {
            coord3d pp;
            f32 dx, dz, d;

            if (!portPadPos(i, &pp))
            {
                continue;
            }
            dx = pp.x - from->x;
            dz = pp.z - from->z;
            d = sqrtf(dx * dx + dz * dz);
            if (d >= mindist && d <= maxdist)
            {
                out[n++] = i;
            }
        }
    }
    return n;
}

/* ---- the Bonds. Modes run in the shared sim: nothing here may read portLocalPlayer() or the current pass (a mode script that did
 * would see another Bond on each machine and desync). Player index p is the same on both machines (0 = the host's Bond). ---- */

s32 portBondCount(void)
{
    s32 n = (s32) portAllPlayerCount();

    return n > 4 ? 4 : n;
}

static struct player *bondPlayer(s32 p)
{
    return (p >= 0 && p < 4) ? g_playerPointers[p] : NULL;
}

s32 portBondDeadOf(s32 p)
{
    struct player *pl = bondPlayer(p);

    return pl != NULL && pl->prop != NULL && pl->bonddead != 0;
}

s32 portBondInPlay(s32 p)
{
    struct player *pl = bondPlayer(p);

    if (getPlayerCount() != 1 || get_is_ramrom_flag() != 0 || get_recording_ramrom_flag() != 0)
    {
        return FALSE;
    }
    if (pl == NULL || g_CameraMode != CAMERAMODE_FP)
    {
        return FALSE;   /* intro/swirl/cinema/death cams */
    }
    /* solo: the death cam (g_CameraMode) is the dead gate, as before; co-op: a dead Bond waiting to respawn keeps the first-person camera */
    return pl->prop != NULL && pl->prop->stan != NULL && (!portCoopActive() || !pl->bonddead);
}

/* The prop of the lowest-index Bond in play: the sim's anchor (spawn gate, test hooks). NULL = nobody in first-person play. */
static PropRecord *bondGate(void)
{
    s32 p, n = portBondCount();

    for (p = 0; p < n; p++)
    {
        if (portBondInPlay(p))
        {
            return g_playerPointers[p]->prop;
        }
    }
    return NULL;
}

s32 portBondPosOf(s32 p, coord3d *pos)
{
    PropRecord *bp;

    if (!portBondInPlay(p))
    {
        return FALSE;
    }
    bp = g_playerPointers[p]->prop;
    pos->x = bp->pos.x;
    pos->y = stanGetPositionYValue(bp->stan, bp->pos.x, bp->pos.z);   /* his prop y is eye level */
    pos->z = bp->pos.z;
    return TRUE;
}

s32 portBondPos(coord3d *pos)
{
    return portBondPosOf(0, pos);   /* player 0 on every machine, never the local player */
}

s32 portBondNearest(f32 x, f32 y, f32 z, f32 *dist)
{
    s32 p, best = -1, n = portBondCount();
    f32 bestd = 0.0f;

    for (p = 0; p < n; p++)
    {
        coord3d bp;
        f32 dx, dy, dz, d;

        if (!portBondPosOf(p, &bp))
        {
            continue;
        }
        dx = bp.x - x;
        dy = bp.y - y;
        dz = bp.z - z;
        d = dx * dx + dy * dy + dz * dz;
        if (best < 0 || d < bestd)   /* strict: a tie keeps the lower index */
        {
            best = p;
            bestd = d;
        }
    }
    if (best >= 0 && dist != NULL)
    {
        *dist = sqrtf(bestd);
    }
    return best;
}

s32 portBondAllDown(void)
{
    if (portCoopActive())
    {
        return portCoopAllDead() ? TRUE : FALSE;
    }
    return portBondDeadOf(0);
}

static s32 allBondsPresent(void)
{
    s32 p, n = portBondCount();

    for (p = 0; p < n; p++)
    {
        struct player *pl = bondPlayer(p);

        if (pl == NULL || pl->prop == NULL || pl->prop->stan == NULL)
        {
            return FALSE;
        }
    }
    return n > 0;
}

/* ---- zones ---- */

s32 portZoneAdd(const char *name, f32 x, f32 y, f32 z, f32 radius)
{
    s32 i;

    for (i = 0; i < PORT_ZONE_MAX; i++)
    {
        if (!sZone[i].used)
        {
            memset(&sZone[i], 0, sizeof(Zone));
            sZone[i].used = TRUE;
            strncpy(sZone[i].name, name != NULL ? name : "", sizeof(sZone[i].name) - 1);
            sZone[i].pos.x = x;
            sZone[i].pos.y = y;
            sZone[i].pos.z = z;
            sZone[i].radius = radius;
            return i;
        }
    }
    return -1;
}

void portZoneMove(s32 zone, f32 x, f32 y, f32 z)
{
    if (zone >= 0 && zone < PORT_ZONE_MAX && sZone[zone].used)
    {
        sZone[zone].pos.x = x;
        sZone[zone].pos.y = y;
        sZone[zone].pos.z = z;
    }
}

void portZoneRemove(s32 zone)
{
    if (zone >= 0 && zone < PORT_ZONE_MAX)
    {
        sZone[zone].used = FALSE;
    }
}

s32 portZoneWatchChr(s32 chrnum)
{
    s32 i, free = -1;

    for (i = 0; i < WATCH_MAX; i++)
    {
        if (sWatchUsed[i] && sWatch[i] == chrnum)
        {
            return TRUE;
        }
        if (!sWatchUsed[i] && free < 0)
        {
            free = i;
        }
    }
    if (free < 0)
    {
        return FALSE;
    }
    sWatchUsed[free] = TRUE;
    sWatch[free] = chrnum;
    return TRUE;
}

void portZoneUnwatchChr(s32 chrnum)
{
    s32 i, z;

    for (i = 0; i < WATCH_MAX; i++)
    {
        if (sWatchUsed[i] && sWatch[i] == chrnum)
        {
            sWatchUsed[i] = FALSE;
            for (z = 0; z < PORT_ZONE_MAX; z++)
            {
                sZone[z].chrIn &= ~(1u << i);
            }
        }
    }
}

s32 portZoneContains(s32 zone, s32 chrnum)
{
    s32 i;

    if (zone < 0 || zone >= PORT_ZONE_MAX || !sZone[zone].used)
    {
        return FALSE;
    }
    if (PORT_CHR_IS_BOND(chrnum))
    {
        return (sZone[zone].bondIn >> PORT_CHR_BOND_PLAYER(chrnum)) & 1u;
    }
    for (i = 0; i < WATCH_MAX; i++)
    {
        if (sWatchUsed[i] && sWatch[i] == chrnum)
        {
            return (sZone[zone].chrIn >> i) & 1u;
        }
    }
    return FALSE;
}

s32 portZoneBondMask(s32 zone)
{
    return (zone >= 0 && zone < PORT_ZONE_MAX && sZone[zone].used) ? (s32) sZone[zone].bondIn : 0;
}

const char *portZoneName(s32 zone)
{
    return (zone >= 0 && zone < PORT_ZONE_MAX && sZone[zone].used) ? sZone[zone].name : NULL;
}

static s32 inZone(const Zone *z, const coord3d *p)
{
    f32 dx = p->x - z->pos.x;
    f32 dy = p->y - z->pos.y;
    f32 dz = p->z - z->pos.z;

    return dx * dx + dz * dz <= z->radius * z->radius && dy <= PORT_ZONE_HALF_HEIGHT && dy >= -PORT_ZONE_HALF_HEIGHT;
}

static void zonesTick(void)
{
    s32 z, w, p, nb = portBondCount();
    coord3d bpos[4];
    s32 have[4];

    for (p = 0; p < 4; p++)
    {
        have[p] = p < nb && portBondPosOf(p, &bpos[p]);
    }
    for (z = 0; z < PORT_ZONE_MAX; z++)
    {
        Zone *zn = &sZone[z];

        if (!zn->used)
        {
            continue;
        }
        for (p = 0; p < nb && p < 4; p++)   /* every Bond, in player order: identical on both machines */
        {
            s32 now = have[p] && inZone(zn, &bpos[p]);

            if (now != (s32) ((zn->bondIn >> p) & 1u))
            {
                zn->bondIn ^= (1u << p);
                portEventEmit(now ? PORT_EV_ZONE_ENTER : PORT_EV_ZONE_LEAVE, z, PORT_CHR_BONDP(p), 0, 0);
            }
        }
        for (w = 0; w < WATCH_MAX; w++)
        {
            ChrRecord *c;
            coord3d cp;
            s32 in = FALSE;

            if (!sWatchUsed[w])
            {
                continue;
            }
            c = chrFindByLiteralId(sWatch[w]);
            if (c != NULL && c->prop != NULL && c->model != NULL && !chrIsDead(c))
            {
                cp.x = c->prop->pos.x;
                cp.y = c->ground;
                cp.z = c->prop->pos.z;
                in = inZone(zn, &cp);
            }
            if (in != (s32)((zn->chrIn >> w) & 1u))
            {
                zn->chrIn ^= (1u << w);
                portEventEmit(in ? PORT_EV_ZONE_ENTER : PORT_EV_ZONE_LEAVE, z, sWatch[w], 0, 0);
            }
        }
    }
}

/* ---- HUD ---- */

/* The HUD is pure presentation: a text per line, for every player or for one. Setting it never touches the sim, so a script may
 * write whatever it likes per player; each machine draws its own Bond's text (portModeHudGetLocal, port_console.c). */
void portModeHudSetFor(s32 player, s32 line, const char *text)
{
    s32 slot = player < 0 ? 0 : player + 1;

    if (line < 0 || line >= PORT_HUD_LINES || slot >= HUD_SLOTS)
    {
        return;
    }
    if (text == NULL)
    {
        sHud[slot][line][0] = '\0';
        return;
    }
    strncpy(sHud[slot][line], text, HUD_LEN - 1);
    sHud[slot][line][HUD_LEN - 1] = '\0';
}

void portModeHudSet(s32 line, const char *text)
{
    portModeHudSetFor(-1, line, text);
}

void portModeHudClearAll(void)
{
    memset(sHud, 0, sizeof(sHud));
    memset(sPanel, 0, sizeof(sPanel));
}

void portModeHudPanelSet(s32 player, s32 line, const char *text)
{
    s32 slot = player < 0 ? 0 : player + 1;

    if (line < 0 || line >= PORT_HUD_PANEL_LINES || slot >= HUD_SLOTS)
    {
        return;
    }
    if (text == NULL)
    {
        sPanel[slot][line][0] = '\0';
        return;
    }
    strncpy(sPanel[slot][line], text, HUD_LEN - 1);
    sPanel[slot][line][HUD_LEN - 1] = '\0';
}

void portModeHudPanelClear(s32 player)
{
    s32 slot = player < 0 ? 0 : player + 1;

    if (slot < HUD_SLOTS)
    {
        memset(sPanel[slot], 0, sizeof(sPanel[slot]));
    }
}

const char *portModeHudPanelGetLocal(s32 line)
{
    const s32 pl = (s32) portLocalPlayer();   /* render only */
    s32 own = 0, i;

    if (!portModeActive() || line < 0 || line >= PORT_HUD_PANEL_LINES)
    {
        return NULL;
    }
    if (pl >= 0 && pl + 1 < HUD_SLOTS)
    {
        for (i = 0; i < PORT_HUD_PANEL_LINES && !own; i++)
        {
            own = sPanel[pl + 1][i][0] != '\0';
        }
    }
    if (own)
    {   /* a Bond with a panel of his own draws only his (the lines of one panel belong together) */
        return sPanel[pl + 1][line][0] != '\0' ? sPanel[pl + 1][line] : NULL;
    }
    return sPanel[0][line][0] != '\0' ? sPanel[0][line] : NULL;
}

void portModeHudClearFor(s32 player)
{
    if (player < 0)
    {
        portModeHudClearAll();
    }
    else if (player + 1 < HUD_SLOTS)
    {
        memset(sHud[player + 1], 0, sizeof(sHud[0]));
    }
}

const char *portModeHudGet(s32 line)
{
    if (!portModeActive() || line < 0 || line >= PORT_HUD_LINES || sHud[0][line][0] == '\0')
    {
        return NULL;
    }
    return sHud[0][line];
}

const char *portModeHudGetFor(s32 player, s32 line)
{
    if (!portModeActive() || line < 0 || line >= PORT_HUD_LINES)
    {
        return NULL;
    }
    if (player >= 0 && player + 1 < HUD_SLOTS && sHud[player + 1][line][0] != '\0')
    {
        return sHud[player + 1][line];
    }
    return sHud[0][line][0] != '\0' ? sHud[0][line] : NULL;
}

const char *portModeHudGetOwn(s32 player, s32 line)
{
    if (player < 0 || player + 1 >= HUD_SLOTS || line < 0 || line >= PORT_HUD_LINES || sHud[player + 1][line][0] == '\0')
    {
        return NULL;
    }
    return sHud[player + 1][line];
}

const char *portModeHudGetLocal(s32 line)
{
    const s32 pl = (s32) portLocalPlayer();   /* render only: the one place the local player is read, and it only picks a string */
    const char *t = portModeHudGetFor(pl, line);

    {   /* GE_MODEHUD_LOG=1 (test): what THIS machine draws on each line, logged when it changes */
        static char last[PORT_HUD_LINES][HUD_LEN];
        static s32 want = -1;

        if (want < 0)
        {
            const char *e = getenv("GE_MODEHUD_LOG");

            want = e != NULL && e[0] != '\0' && e[0] != '0';
        }
        if (want && line >= 0 && line < PORT_HUD_LINES && strcmp(last[line], t != NULL ? t : "") != 0)
        {
            strncpy(last[line], t != NULL ? t : "", HUD_LEN - 1);
            printf("ge: modehud: this machine (player %d) draws line %d: '%s' (t=%d)\n", (int) pl, (int) line + 1, last[line], (int) sTicks);
            fflush(stdout);
        }
    }
    return t;
}

/* ---- deaths ---- */

void portModeNoteHit(s32 victim, s32 attacker)
{
    s32 i;

    for (i = 0; i < HIT_MAX; i++)
    {
        if (sHitVictim[i] == victim)
        {
            sHitAttacker[i] = attacker;
            return;
        }
    }
    sHitVictim[sHitNext] = victim;
    sHitAttacker[sHitNext] = attacker;
    sHitNext = (sHitNext + 1) % HIT_MAX;
}

static s32 takeKiller(s32 victim)
{
    s32 i;

    for (i = 0; i < HIT_MAX; i++)
    {
        if (sHitVictim[i] == victim)
        {
            sHitVictim[i] = PORT_CHR_NONE - 1000;
            return sHitAttacker[i];
        }
    }
    return PORT_CHR_NONE;
}

extern void portHeistChrDied(s32 chrnum, s32 killerPlayer);   /* port_heist.c */
extern void portHeistAiTick(void);                            /* port_heist.c: body discovery, civilians */

static void deathsTick(void)
{
    s32 i, n = g_NumChrSlots < SLOT_MAX ? g_NumChrSlots : SLOT_MAX;

    if (g_ChrSlots == NULL)
    {
        n = 0;
    }
    for (i = 0; i < n; i++)
    {
        ChrRecord *c = &g_ChrSlots[i];

        if (c->model == NULL || c->prop == NULL)
        {
            sSlotDead[i] = FALSE;
            sSlotNum[i] = -1;
            continue;
        }
        if (sSlotNum[i] != c->chrnum)
        {
            sSlotNum[i] = c->chrnum;
            sSlotDead[i] = FALSE;
        }
        if (!sSlotDead[i] && chrIsDead(c))
        {
            sSlotDead[i] = TRUE;
            s32 killer = takeKiller(c->chrnum);

            portEventEmit(PORT_EV_CHR_DIED, c->chrnum, killer, 0, 0);
            portHeistChrDied(c->chrnum, PORT_CHR_IS_BOND(killer) ? (s32)PORT_CHR_BOND_PLAYER(killer) : -1);
        }
    }
    {   /* every Bond, by player index (never the current or the local one): BOND_DIED(killer, player) on each death edge */
        s32 p, nb = portBondCount();

        for (p = 0; p < nb && p < 4; p++)
        {
            s32 dead = g_playerPointers[p]->bonddead ? TRUE : FALSE;

            if (dead && !sBondDead[p])
            {
                portEventEmit(PORT_EV_BOND_DIED, PORT_CHR_NONE, p, 0, 0);
            }
            sBondDead[p] = dead;
        }
    }
}

/* ---- test hook ---- */

static s32 sTestOn = -1;
static s32 sTestZoneBond = -1;
static s32 sTestListening;

static void testListener(int type, int a, int b, int c, int d, void *ud)
{
    (void)ud;
    printf("ge: mode: event %s %d %d %d (t=%d)\n", portEventName(type), a, b, c, (int)sTicks);
    fflush(stdout);
}

static void testTick(PropRecord *bp)
{
    static s32 ids[16];
    static s32 nIds;
    static s32 guardA = -1, guardB = -1;
    coord3d bpos;
    ChrRecord *c;

    if (sTestOn == 2)
    {   /* GE_MODE_TEST=bonddie: lethal damage to Bond at ~2 s, expect BOND_DIED through the death cam */
        if (sTicks >= 120 && !sTestStep[0]++)
        {
            printf("ge: mode: test: killing Bond\n");
            record_damage_kills(1000.0f, 0.0f, 1.0f, -1, TRUE);
        }
        return;
    }
    if (!sTestListening)
    {
        s32 t;

        sTestListening = TRUE;
        for (t = PORT_EV_CHR_DIED; t < PORT_EV_COUNT; t++)
        {
            portEventOn(t, testListener, NULL);
        }
    }
    if (sTicks >= 30 && !sTestStep[0]++)
    {
        PortSpawnDesc d;
        coord3d pa;

        portBondPos(&bpos);
        nIds = portPadList(&bpos, 600.0f, 4000.0f, TRUE, ids, 16);
        printf("ge: mode: test: pads=%d (wp pads in 600..4000 of Bond: %d)\n", (int)portPadCount(), (int)nIds);
        if (nIds < 2)
        {
            printf("ge: mode: test: FAIL need 2 far pads\n");
            return;
        }
        printf("ge: mode: test: pad %d to pad %d dist %.0f\n", (int)ids[0], (int)ids[nIds - 1],
               portPadDist(ids[0], ids[nIds - 1]));
        memset(&d, 0, sizeof(d));
        d.body = BODY_Russian_Soldier;
        d.head = -1;   /* -1 = random head; a bare 0 is a whole body (194 anim records, over the 140 spare slots) */
        d.nearprop = bp;
        d.ailistid = GAILIST_PERSISTENTLY_CHASE_AND_ATTACK_BOND;
        d.chrnum = 7101;
        c = portSpawnChrAtPad(ids[0], &d, PROP_CHRKALASH, ITEM_AK47);
        guardA = c != NULL ? c->chrnum : -1;
        d.chrnum = 7102;
        c = portSpawnChrAtPad(ids[nIds - 1], &d, PROP_CHRSHOTGUN, ITEM_SHOTGUN);
        guardB = c != NULL ? c->chrnum : -1;
        printf("ge: mode: test: spawned chrs %d %d\n", (int)guardA, (int)guardB);
        sTestZoneBond = portZoneAdd("bond", bpos.x, bpos.y, bpos.z, 250.0f);
        if (portPadPos(ids[0], &pa))
        {
            portZoneAdd("padA", pa.x, pa.y, pa.z, 150.0f);
        }
        portZoneWatchChr(guardA);
        portZoneWatchChr(guardB);
        portModeHudSet(0, "TEST: building blocks");
        portModeHudSet(1, "zones: bond, padA");
        portModeHudSet(2, "guards: 2");
    }
    if (sTicks >= 240 && !sTestStep[1]++ && guardA >= 0 && (c = chrFindByLiteralId(guardA)) != NULL)
    {
        portModeNoteHit(guardA, PORT_CHR_BOND);   /* as a Bond shot would */
        chrlvExplosionDamage(c, &bp->pos, c->maxdamage + 1.0f, TRUE);
    }
    if (sTicks >= 300 && !sTestStep[2]++ && guardB >= 0 && (c = chrFindByLiteralId(guardB)) != NULL)
    {
        chrlvExplosionDamage(c, &bp->pos, c->maxdamage + 1.0f, TRUE);
    }
    if (sTicks >= 360 && !sTestStep[3]++ && sTestZoneBond >= 0)
    {
        portZoneMove(sTestZoneBond, 99999.0f, 0.0f, 99999.0f);
        portModeHudSet(2, "guards: 0");
    }
}

/* ---- GE_EVENT_TEST=1: drives every Job 1 event on a plain solo stage (needs a Lua or C listener) ---- */

static s32 sEvTest;
static s32 sEvStep[8];

static ChrRecord *evTestNearestChr(const coord3d *from, s32 skipnum)
{
    s32 i, n = g_NumChrSlots < SLOT_MAX ? g_NumChrSlots : SLOT_MAX;
    ChrRecord *best = NULL;
    f32 bestd = 1e30f;

    for (i = 0; g_ChrSlots != NULL && i < n; i++)
    {
        ChrRecord *c = &g_ChrSlots[i];
        f32 dx, dz, d;

        if (c->model == NULL || c->prop == NULL || chrIsDead(c) || c->chrnum == skipnum)
        {
            continue;
        }
        dx = c->prop->pos.x - from->x;
        dz = c->prop->pos.z - from->z;
        d = dx * dx + dz * dz;
        if (d < bestd)
        {
            bestd = d;
            best = c;
        }
    }
    return best;
}

static void evTestTick(PropRecord *bp)
{
    static s32 victimA = -1;
    ChrRecord *c;
    coord3d dir;

    if (sTicks >= 60 && !sEvStep[0]++)
    {   /* a Bond gun hit: DAMAGE(chr, BOND, amount, ITEM_AK47) */
        c = evTestNearestChr(&bp->pos, -1);
        if (c != NULL)
        {
            victimA = c->chrnum;
            dir.x = c->prop->pos.x - bp->pos.x;
            dir.y = 0.0f;
            dir.z = c->prop->pos.z - bp->pos.z;
            printf("ge: event: test: Bond shoots chr %d\n", (int)victimA);
            handles_shot_actors(c, 0xF, &dir, ITEM_AK47, TRUE);
        }
    }
    if (sTicks >= 90 && !sEvStep[1]++)
    {   /* a blast kill on another chr: DAMAGE(chr, NONE, amount, -1) then CHR_DIED */
        c = evTestNearestChr(&bp->pos, victimA);
        if (c != NULL)
        {
            printf("ge: event: test: blast on chr %d\n", (int)c->chrnum);
            chrlvExplosionDamage(c, &bp->pos, c->maxdamage + 1.0f, TRUE);
        }
    }
    if (sTicks >= 300 && !sEvStep[2]++)
    {   /* damage to Bond with no shooter set: DAMAGE(BOND, NONE, ...) */
        printf("ge: event: test: Bond hurt, unknown source\n");
        record_damage_kills(0.5f, 0.0f, 1.0f, -1, TRUE);
    }
    if (sTicks >= 360 && !sEvStep[3]++)
    {   /* the same with the guard-fire shooter set, as chrlvTriggerFireWeapon does */
        c = evTestNearestChr(&bp->pos, -1);
        if (c != NULL)
        {
            printf("ge: event: test: Bond hurt by chr %d (shooter set by hand)\n", (int)c->chrnum);
            portEventSetAttacker(c->chrnum);
            record_damage_kills(0.5f, 0.0f, 1.0f, -1, TRUE);
            portEventSetAttacker(PORT_CHR_NONE);
        }
    }
    if (sTicks >= 120 && !sEvStep[4]++)
    {   /* the first pickup-able object in the active prop list */
        PropRecord *p;

        for (p = g_ActivePropsHead; p != NULL; p = p->next)
        {
            if ((p->type == PROP_TYPE_OBJ || p->type == PROP_TYPE_WEAPON) && p->obj != NULL &&
                (p->obj->type == PROPDEF_AMMO || p->obj->type == PROPDEF_MAGAZINE || p->obj->type == PROPDEF_COLLECTABLE ||
                 p->obj->type == PROPDEF_ARMOUR))
            {
                printf("ge: event: test: picking up object type %d\n", (int)p->obj->type);
                propExecuteTickOperation(p, propPickupByPlayer(p, FALSE));   /* as propsTickPlayer does */
                break;
            }
        }
        if (p == NULL)
        {
            printf("ge: event: test: no pickup object found\n");
        }
    }
    if (sTicks >= 150 && !sEvStep[5]++)
    {   /* make the next status check see a change: OBJECTIVE_CHANGED(0, real status) */
        extern s32 objective_count;
        extern s32 objective_status_display_disabled;

        printf("ge: event: test: forcing an objective status diff (objective_count=%d, status0=%d, display_disabled=%d)\n",
               (int)objective_count, (int)objectiveStatuses[0], (int)objective_status_display_disabled);
        printf("ge: event: test: real status0=%d, difficulty of obj0=%d, selected=%d\n", (int)get_status_of_objective(0),
               (int)get_difficulty_for_objective(0), (int)lvlGetSelectedDifficulty());
        objectiveStatuses[0] = (OBJECTIVESTATUS)((get_status_of_objective(0) + 1) % 3);
        display_objective_status_text_on_status_change();   /* the HUD pass would do this on its next draw */
    }
    if (sTicks >= 210 && !sEvStep[7]++)
    {   /* a chr-thrown knife hit: DAMAGE attacker must be the thrower, not NONE */
        ChrRecord *thrower = evTestNearestChr(&bp->pos, -1);
        ChrRecord *victim = thrower != NULL ? evTestNearestChr(&thrower->prop->pos, thrower->chrnum) : NULL;

        if (thrower != NULL && victim != NULL)
        {
            dir.x = victim->prop->pos.x - thrower->prop->pos.x;
            dir.y = 0.0f;
            dir.z = victim->prop->pos.z - thrower->prop->pos.z;
            printf("ge: event: test: chr %d's knife hits chr %d\n", (int)thrower->chrnum, (int)victim->chrnum);
            portChrKnifeHit(victim->prop, 0xF, &dir, ITEM_THROWKNIFE, thrower->prop);
        }
    }
    if (sTicks >= 180 && !sEvStep[6]++)
    {   /* a port spawn: CHR_SPAWNED */
        static s32 ids[4];
        PortSpawnDesc d;
        coord3d bpos;

        portBondPos(&bpos);
        if (portPadList(&bpos, 600.0f, 4000.0f, TRUE, ids, 4) > 0)
        {
            memset(&d, 0, sizeof(d));
            d.body = BODY_Russian_Soldier;
            d.head = -1;
            d.nearprop = bp;
            d.ailistid = GAILIST_SIMPLE_GUARD;
            d.chrnum = 7401;
            printf("ge: event: test: spawning chr 7401\n");
            portSpawnChrAtPad(ids[0], &d, PROP_CHRKALASH, ITEM_AK47);
        }
    }
}

/* ---- lifecycle ---- */

void portBlocksStageStart(int stage)
{
    const char *e = getenv("GE_MODE_TEST");

    memset(sZone, 0, sizeof(sZone));
    memset(sWatchUsed, 0, sizeof(sWatchUsed));
    memset(sHud, 0, sizeof(sHud));
    memset(sSlotDead, 0, sizeof(sSlotDead));
    memset(sHitVictim, 0, sizeof(sHitVictim));
    {
        s32 i;

        for (i = 0; i < SLOT_MAX; i++)
        {
            sSlotNum[i] = -1;
        }
        for (i = 0; i < HIT_MAX; i++)
        {
            sHitVictim[i] = PORT_CHR_NONE - 1000;
        }
    }
    portHostageStageStart(stage);
    sStage = stage;
    sStarted = FALSE;
    sTicks = 0;
    memset(sBondDead, 0, sizeof(sBondDead));
    sTestOn = !portModeActive() ? 0 : (e != NULL && strcmp(e, "blocks") == 0) ? 1 : (e != NULL && strcmp(e, "bonddie") == 0) ? 2 : 0;
    sTestZoneBond = -1;
    memset(sTestStep, 0, sizeof(sTestStep));
    memset(sEvStep, 0, sizeof(sEvStep));
    sEvTest = getenv("GE_EVENT_TEST") != NULL && getenv("GE_EVENT_TEST")[0] == '1';
    if (sTestOn > 0 && !sTestListening)
    {
        s32 t;

        sTestListening = TRUE;
        for (t = PORT_EV_CHR_DIED; t < PORT_EV_COUNT; t++)
        {
            portEventOn(t, testListener, NULL);
        }
    }
}

void portBlocksTick(void)
{
    PropRecord *bp;
    coord3d bpos;

    const s32 mode = portModeActive();

    /* PORT: events also run outside a mode, but only while a listener (C bus or Lua tick) exists. */
    if (!mode && !portEventAnyListeners() && !portLuaHasTick())
    {
        return;
    }
    if (mode)
    {
        portHostageDeadSweep();
        portHeistAiTick();
    }
    deathsTick();   /* before the gate: Bond's death switches to the death cam, which fails bondGate */
    bp = bondGate();
    if (bp == NULL)
    {
        return;
    }
    if (!sStarted)
    {
        if (!allBondsPresent())
        {
            return;   /* co-op: STAGE_START waits until every Bond exists, so a script can read all of them */
        }
        sStarted = TRUE;
        portEventEmit(PORT_EV_STAGE_START, sStage, 0, 0, 0);
    }
    sTicks += g_ClockTimer;
    if (sTestOn > 0)
    {
        testTick(bp);
    }
    if (sEvTest)
    {
        evTestTick(bp);
    }
    if (mode)
    {
        zonesTick();
        portHostageTick();
    }
    portLuaTick((int) g_ClockTimer);   /* no-op until a script registers a tick handler */
}

#endif /* TARGET_N64 */
