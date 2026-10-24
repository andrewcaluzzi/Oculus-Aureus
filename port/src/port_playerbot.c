/*
 * port_playerbot.c - the player bot: plays solo missions as Bond through player 1's controller.
 *
 * Input only. Each sim tick it reads the game (Bond, chrs, props, objectives, the waypoint graph) and writes:
 *   - pad 0 (buttons + stick) in osContGetReadData, through inputrec.c's pad hook, and
 *   - this tick's look as direct-mode mouse degrees (yaw onto vv_theta, pitch onto vv_verta), through inputrec.c's
 *     mouse hook.
 * Both land in a GE_INPUTREC recording exactly like a human's input, so GE_INPUTPLAY replays a bot run without the
 * bot. The bot never writes sim state; the engine queries it makes (stan line tests) have their scratch globals saved
 * and restored around them.
 *
 * Layers (modding/bot.md has the user-facing description):
 *   observe  - per stage: build the graph, track health / kills / objectives, write the RESULT line, exit the process
 *   drive    - per tick: plan step -> (fight | navigate | interact) -> pad + look
 *   plan     - Data/BotPlans/<stage>.plan (or GE_PLAYERBOT_PLANDIR), parsed by port_playerbotlogic.c, else `auto`
 *
 * Read when touching: inputrec.c (the hooks), port_playerbotlogic.c (graph, A*, plan parser), file.c
 * portMissionEndLog (the mission-end hook).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <ultra64.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include <math.h>
#include "game/chr.h"
#include "game/chrai.h"
#include "game/chraction.h"
#include "game/player.h"
#include "game/bondview.h"
#include "game/lv.h"
#include "game/stan.h"
#include "game/bondinv.h"
#include "game/objective_status.h"
#include "game/propobj.h"
#include "game/gun.h"
#include "port_playerbot.h"
#include "port_playerbotlogic.h"

extern void portConLogf(const char *fmt, ...);
extern u32  portInputRecTickNow(void);
extern void portInputRecFlush(void);
extern s32  get_is_ramrom_flag(void);
extern s32  get_recording_ramrom_flag(void);
extern f32  viGetFovY(void);
extern LEVELID bossGetStageNum(void);
extern s32  sizepropdef(PropDefHeaderRecord *pdef);
extern u32  get_cur_player_look_vertical_inverted(void);
extern s32  objectiveregisters1;
extern int  ge_shot_force;
extern s32  gamemode;
extern bool doorIsPadlockFree(DoorRecord *door);
extern s32  get_ammo_type_for_weapon(ITEM_IDS weapon);   /* gunfire.c */
extern bool bondinvCheckHasKeyFlags(u32 wantkeyflags);   /* bondinv.c */
extern s32  get_difficulty_for_objective(s32 objectiveIndex);

/* stan.c scratch globals that a line test writes: saved and restored around every bot query */
extern StandTile     *stanSavedColl_tile;
extern s32            stanSavedColl_pointI;
extern s32            stanSavedColl_unknown;
extern struct coord2d stanSavedColl_pntA;
extern struct coord2d stanSavedColl_pntB;
extern f32            stanSavedColl_someMin;
extern PropRecord    *stanSavedColl_posData;
extern s32            dword_CODE_bss_8007BA08;
extern StandTile     *dword_CODE_bss_8007BA0C;
extern s32            D_800413BC;
extern f32            D_800413C0;
extern f32            D_800413C4;
extern s32            D_800413C8;
extern s32            D_800413CC;
extern s32            D_800413D0[8];
extern s32            D_80040FAC;
/* stan.c: the tile file (the nav mesh) */
extern s32            ptr_firstroom_0;
extern f32            inv_level_scale;
extern u8             list_of_tilesizes[];

#define BTN_A     0x8000
#define BTN_B     0x4000
#define BTN_Z     0x2000
#define BTN_START 0x1000
#define BTN_R     0x0010
#define BTN_CU    0x0008
#define BTN_CD    0x0004
#define BTN_CL    0x0002
#define BTN_CR    0x0001

#define MAXPATH 4096
#define PORTACT_NEXT_INV_INDEX 8   /* joy_port.c PORTACT_NEXT_INV ("next_inventory"; input_pc.c PM_PORT_NEXT_INV) */

/* ---- state ------------------------------------------------------------------------------------------------------- */

typedef struct {
    s32 mode;                  /* 0 off, 1 drive, 2 watch */
    s32 exitOnEnd;
    s32 verbose;
    s32 maxSecs;
    char plandir[256];
    char planfile[256];

    /* stage */
    s32 inStage;
    s32 stage;
    void *setupKey;            /* g_CurrentSetup.pathwaypoints: changes on every stage load */
    u32 lastRecTick;
    s32 tick;                  /* bot ticks in this stage (play ticks: Bond in first person) */
    s32 ticksAll;              /* every tick since the stage loaded */
    s32 resultDone;
    s32 diedTick;
    f32 hpPrev;
    f32 hpLost;
    s32 lastHurtTick;
    s32 kills;
    s32 deadAtStart;
    s32 objStatus[10];
    s32 alarmLogged;
    char blocked[160];

    /* graph: one node per stan tile (Bond's walkable floor), edges = the tiles' links */
    PblGraph g;
    StandTile **tiles;         /* node -> tile, ascending addresses (binary searched) */
    s32 ntiles;
    f32 maxStep;               /* tallest step up Bond can walk (GE_PLAYERBOT_MAXSTEP, default 45) */
    s32 climbCut;

    /* plan */
    PblPlan plan;
    s32 planLoaded;
    s32 step;
    s32 stepTick;
    s32 phase;
    s32 count;
    s32 stepFails;
    s32 peace;

    /* navigation */
    s32 path[MAXPATH];
    s32 pathN, pathAt;
    f32 goalX, goalY, goalZ;
    s32 goalValid;
    s32 planTick;
    PblStuck stuck;
    s32 stuckEvents;
    s32 recoverUntil, recoverDir, backUntil;
    f32 recoverYaw, lastSteerYaw;
    s32 lastDoorTap;
    s32 lastNoRouteLog;
    PropRecord *doorProp;      /* the shut door being opened */
    s32 doorTaps, doorSince, doorOpened;
    PropRecord *blockedDoors[16];
    s32 blockedAt[16];
    s32 nBlockedDoors;
    ObjectRecord *subLock;     /* a detour: shoot this padlock off a door on the route */
    ObjectRecord *photoObj;    /* the object being photographed, and since when */
    s32 photoSince;
    ObjectRecord *shootObj;    /* the object being shot at, and since when */
    s32 shootSince;
    PropRecord *lootProp;      /* a detour: walk over a pick-up */
    PropRecord *lootTried[16];
    s32 lootN, lootUntil, noLoot;
    u32 subKeyNeed;            /* a detour: fetch a key with these flags */
    s32 keyGiveUpAt;
    s32 lastKeyLog;
    PropRecord *subSwitch;     /* a detour: use this switch to open subDoor, then resume the step */
    PropRecord *subDoor;
    PropRecord *subTriesDoor;
    s32 subPhase, subSince, subTries, subGoalSet;
    f32 subGoalX, subGoalZ;
    s32 aimAt;                 /* path index being steered at (>= pathAt: the farthest one in a straight line) */
    s32 offPath;               /* ticks Bond's tile has not been on the path */

    /* combat */
    PropRecord *target;
    s32 targetSince;
    s32 lastSeenTick;
    s32 fireTick;

    /* weapon */
    s32 wantWeapon;
    s32 keepWant;
    s32 actNextInv;            /* this tick: press the port action PORTACT_NEXT_INV */
    s32 lastCycleTick;

    /* output for this tick */
    u16 btn;
    s8  sx, sy;
    f32 yaw, pitch;
    s32 lookFresh;
    s32 heldA;
    s32 pressMask, pressUntil;
    s32 lastSy, lastSx, lastBtn;   /* the previous tick's output, for the status log */
    f32 lastYaw;
    f32 pitchWant;
    s32 deadTicks;             /* ticks since Bond died */
    s32 outTicks;              /* polls outside a playable stage since the stage was left */
} PlayerBot;

static PlayerBot B;
static s32 sInit;
static PortPlayerBotPlanner sPlanner;

void portPlayerBotSetPlanner(PortPlayerBotPlanner fn)
{
    sPlanner = fn;
}

/* ---- config ------------------------------------------------------------------------------------------------------ */

static void botInit(void)
{
    const char *e;

    sInit = 1;
    memset(&B, 0, sizeof(B));
    e = getenv("GE_PLAYERBOT");
    if (e != NULL && e[0] != '\0' && e[0] != '0')
    {
        B.mode = (strcmp(e, "watch") == 0) ? 2 : 1;
    }
    if (B.mode == 1 && getenv("GE_INPUTPLAY") != NULL && getenv("GE_INPUTPLAY")[0] != '\0')
    {
        B.mode = 2;   /* replaying: the recording drives, the bot only reports */
    }
    e = getenv("GE_PLAYERBOT_EXIT");
    B.exitOnEnd = (e == NULL) ? 1 : (e[0] != '0');
    e = getenv("GE_PLAYERBOT_LOG");
    B.verbose = (e == NULL) ? 1 : atoi(e);
    e = getenv("GE_PLAYERBOT_MAXSECS");
    B.maxSecs = (e == NULL) ? 1200 : atoi(e);
    e = getenv("GE_PLAYERBOT_PLANDIR");
    snprintf(B.plandir, sizeof(B.plandir), "%s", (e != NULL) ? e : "Data/BotPlans");
    e = getenv("GE_PLAYERBOT_PLAN");
    snprintf(B.planfile, sizeof(B.planfile), "%s", (e != NULL) ? e : "");
    if (B.mode != 0)
    {
        portConLogf("ge: playerbot: %s (plans %s%s%s, max %d s)\n", B.mode == 1 ? "driving player 1" : "watching",
                    B.planfile[0] ? B.planfile : B.plandir, B.planfile[0] ? "" : "/<stage>.plan", "", B.maxSecs);
    }
}

s32 portPlayerBotMode(void)
{
    if (!sInit)
    {
        botInit();
    }
    if (get_is_ramrom_flag() != 0 || get_recording_ramrom_flag() != 0)
    {
        return 0;   /* never during the attract demos */
    }
    return B.mode;
}

/* ---- names ------------------------------------------------------------------------------------------------------- */

static const struct { s32 id; const char *name; } sStageNames[] = {
    { LEVELID_DAM, "dam" }, { LEVELID_FACILITY, "facility" }, { LEVELID_RUNWAY, "runway" },
    { LEVELID_SURFACE, "surface" }, { LEVELID_BUNKER1, "bunker1" }, { LEVELID_SILO, "silo" },
    { LEVELID_FRIGATE, "frigate" }, { LEVELID_SURFACE2, "surface2" }, { LEVELID_BUNKER2, "bunker2" },
    { LEVELID_STATUE, "statue" }, { LEVELID_ARCHIVES, "archives" }, { LEVELID_STREETS, "streets" },
    { LEVELID_DEPOT, "depot" }, { LEVELID_TRAIN, "train" }, { LEVELID_JUNGLE, "jungle" },
    { LEVELID_CONTROL, "control" }, { LEVELID_CAVERNS, "caverns" }, { LEVELID_CRADLE, "cradle" },
    { LEVELID_AZTEC, "aztec" }, { LEVELID_EGYPT, "egypt" },
};

static const char *stageName(s32 id)
{
    s32 i;

    for (i = 0; i < (s32) (sizeof(sStageNames) / sizeof(sStageNames[0])); i++)
    {
        if (sStageNames[i].id == id)
        {
            return sStageNames[i].name;
        }
    }
    return "unknown";
}

/* ---- read-only engine queries --------------------------------------------------------------------------------------- */

typedef struct {
    StandTile *tile; s32 pointI, unknown; struct coord2d a, b; f32 someMin; PropRecord *posData;
    s32 w08; StandTile *w0c; s32 d3bc; f32 d3c0, d3c4; s32 d3c8, d3cc; s32 d3d0[8]; s32 dfac;
} StanScratch;

static void stanSave(StanScratch *s)
{
    s->tile = stanSavedColl_tile; s->pointI = stanSavedColl_pointI; s->unknown = stanSavedColl_unknown;
    s->a = stanSavedColl_pntA; s->b = stanSavedColl_pntB; s->someMin = stanSavedColl_someMin;
    s->posData = stanSavedColl_posData; s->w08 = dword_CODE_bss_8007BA08; s->w0c = dword_CODE_bss_8007BA0C;
    s->d3bc = D_800413BC; s->d3c0 = D_800413C0; s->d3c4 = D_800413C4; s->d3c8 = D_800413C8; s->d3cc = D_800413CC;
    memcpy(s->d3d0, D_800413D0, sizeof(s->d3d0)); s->dfac = D_80040FAC;
}

static void stanRestore(const StanScratch *s)
{
    stanSavedColl_tile = s->tile; stanSavedColl_pointI = s->pointI; stanSavedColl_unknown = s->unknown;
    stanSavedColl_pntA = s->a; stanSavedColl_pntB = s->b; stanSavedColl_someMin = s->someMin;
    stanSavedColl_posData = s->posData; dword_CODE_bss_8007BA08 = s->w08; dword_CODE_bss_8007BA0C = s->w0c;
    D_800413BC = s->d3bc; D_800413C0 = s->d3c0; D_800413C4 = s->d3c4; D_800413C8 = s->d3c8; D_800413CC = s->d3cc;
    memcpy(D_800413D0, s->d3d0, sizeof(s->d3d0)); D_80040FAC = s->dfac;
}

/* Straight line on the stan tiles from `from` to (x2,z2): 1 if nothing in `cd` blocks it and (when want != NULL) it
 * ends on the tile `want`. */
static s32 botLine(StandTile *from, f32 x1, f32 z1, f32 x2, f32 z2, StandTile *want, s32 cd)
{
    StanScratch s;
    StandTile *t = from;
    s32 ok;

    if (from == NULL)
    {
        return 0;
    }
    stanSave(&s);
    ok = stanTestLineUnobstructed(&t, x1, z1, x2, z2, cd, 140.0f, 140.0f, 0.0f, 1.0f);
    stanRestore(&s);
    return ok && (want == NULL || t == want);
}

static struct player *bond(void)
{
    return g_playerPointers[0];
}

static StandTile *bondTile(void)
{
    return bond()->prop->stan;   /* the tile the line tests must start from: the one under prop->pos */
}

static s32 bondRoom(void)
{
    StandTile *t = bondTile();

    return t != NULL ? (s32) t->room : -1;
}

static PadRecord *padGet(s32 pad)
{
    if (pad < 0)
    {
        return NULL;
    }
    if (isNotBoundPad(pad))
    {
        return &g_CurrentSetup.pads[pad];
    }
    return (PadRecord *) &g_CurrentSetup.boundpads[getBoundPadNum(pad)];
}

static ObjectRecord *objByTag(s32 tag)
{
    return objFindByTagId(tag);
}

static s32 objectiveOpen(s32 i)
{
    if (i < 0 || i >= objectiveGetCount())
    {
        return 0;
    }
    if (get_difficulty_for_objective(i) > lvlGetSelectedDifficulty())
    {
        return 0;
    }
    return get_status_of_objective(i) != OBJECTIVESTATUS_COMPLETE;
}

static s32 stageFlag(s32 mask)
{
    return (objectiveregisters1 & mask) != 0;
}

static s32 isAllyBody(s32 body)
{
    return body == BODY_Natalya_Skirt || body == BODY_Trevelyan_006;
}

static s32 chrHostile(ChrRecord *c)
{
    if (c == NULL || c->model == NULL || c->prop == NULL || chrIsDead(c))
    {
        return 0;
    }
    if (c->chrflags & CHRFLAG_COUNT_DEATH_AS_CIVILIAN)
    {
        return 0;
    }
    if (isAllyBody(c->bodynum))
    {
        return 0;
    }
    return chrGetEquippedWeaponPropWithCheck(c, GUNRIGHT) != NULL || chrGetEquippedWeaponPropWithCheck(c, GUNLEFT) != NULL;
}

/* ---- output helpers ---------------------------------------------------------------------------------------------- */

static void outClear(void)
{
    B.btn = 0;
    B.sx = 0;
    B.sy = 0;
    B.yaw = 0.0f;
    B.pitch = 0.0f;
    B.actNextInv = 0;
}

static f32 fovRatio(void)
{
    /* the same ratio bondview2.c portMouseFovRatio scales the direct-mode look by */
    f32 fov = viGetFovY();
    f32 basefov = (fov > 60.0f) ? (f32) GE_DEFAULT_FOV : 60.0f;

    if (basefov < 1.0f)
    {
        basefov = 60.0f;
    }
    return (fov > 1.0f) ? fov / basefov : 1.0f;
}

/* turn toward a yaw / pitch (degrees); returns the remaining yaw error */
static f32 lookToward(f32 yawWant, f32 pitchWant, f32 maxTurn)
{
    struct player *p = bond();
    f32 err = pblWrap180(yawWant - p->vv_theta);
    f32 perr = pitchWant - p->vv_verta;
    f32 r = fovRatio();

    B.yaw = pblClamp(err, -maxTurn, maxTurn) / r;
    B.pitch = pblClamp(perr * 0.5f, -4.0f, 4.0f) / r;
    B.pitchWant = pitchWant;
    return err;
}

static f32 lookAt(f32 x, f32 y, f32 z, f32 maxTurn)
{
    struct player *p = bond();
    f32 dx = x - p->prop->pos.x, dz = z - p->prop->pos.z;
    f32 dh = sqrtf(dx * dx + dz * dz);

    return lookToward(pblYawTo(dx, dz), pblPitchTo(y - p->prop->pos.y, dh), maxTurn);
}

static void tap(u16 mask, s32 every)
{
    if ((B.ticksAll % every) == 0)
    {
        B.btn |= mask;
    }
}

/* ---- weapons ---------------------------------------------------------------------------------------------------- */

static const s32 sGunPrefs[] = {
    ITEM_FNP90, ITEM_M16, ITEM_AK47, ITEM_SPECTRE, ITEM_MP5K, ITEM_MP5KSIL, ITEM_UZI, ITEM_AUTOSHOT, ITEM_SHOTGUN,
    ITEM_RUGER, ITEM_GOLDENGUN, ITEM_SKORPION, ITEM_TT33, ITEM_WPPKSIL, ITEM_WPPK, ITEM_SILVERWPPK, ITEM_GOLDWPPK,
};

static s32 isAuto(s32 w)
{
    return w == ITEM_FNP90 || w == ITEM_M16 || w == ITEM_AK47 || w == ITEM_SPECTRE || w == ITEM_MP5K ||
           w == ITEM_MP5KSIL || w == ITEM_UZI || w == ITEM_SKORPION || w == ITEM_LASER;
}

static s32 weaponHasAmmo(s32 w)
{
    s32 at = get_ammo_type_for_weapon(w);

    if (at == 0)
    {
        return 1;
    }
    return currentPlayerGetAmmoCount(at) > 0;
}

static s32 bestGun(void)
{
    s32 i;

    for (i = 0; i < (s32) (sizeof(sGunPrefs) / sizeof(sGunPrefs[0])); i++)
    {
        if (bondinvItemAvailable(sGunPrefs[i]) && weaponHasAmmo(sGunPrefs[i]))
        {
            return sGunPrefs[i];
        }
    }
    return ITEM_UNARMED;
}

/* tap A (next weapon) until the wanted weapon is in hand */
static void weaponTick(void)
{
    s32 cur = getCurrentPlayerWeaponId(GUNRIGHT);
    /* a fight always gets the best gun, whatever gadget a step wants */
    s32 want = (B.wantWeapon > 0 && B.target == NULL) ? B.wantWeapon : bestGun();

    if (want < 0 || !bondinvItemAvailable(want) || cur == want)
    {
        return;
    }
    if (B.tick - B.lastCycleTick >= 24)
    {
        if (want >= ITEM_BOMBCASE)
        {   /* A only cycles guns; gadgets come round with the port's "next inventory item" action (a key / pad bind,
             * recorded like any input) */
            B.actNextInv = 1;
        }
        else
        {
            B.btn |= BTN_A;
        }
        B.lastCycleTick = B.tick;
    }
}

/* ---- graph -------------------------------------------------------------------------------------------------------- */

static StandTile *tileNext(StandTile *t)
{
    return (StandTile *) ((u8 *) t + list_of_tilesizes[t->tail.hdrTail.pointCount & 0xf]);
}

static s32 tileCount(StandTile *t)
{
    return t->tail.hdrTail.pointCount & 0xf;
}

/* the node of a tile, or -1 (binary search over the ascending tile addresses) */
static s32 tileIndex(StandTile *t)
{
    s32 lo = 0, hi = B.ntiles - 1;

    while (lo <= hi)
    {
        s32 mid = (lo + hi) / 2;

        if (B.tiles[mid] == t)
        {
            return mid;
        }
        if ((u8 *) B.tiles[mid] < (u8 *) t)
        {
            lo = mid + 1;
        }
        else
        {
            hi = mid - 1;
        }
    }
    return -1;
}

static StandTile *tileLink(StandTile *t, s32 i)
{
    u16 link = t->points[i].link;

    return (link >> 4) != 0 ? (StandTile *) ((u8 *) standTileStart + ((u32) link << 3)) : NULL;
}

#define FUNNEL_N 48

/* the two ends of the edge of tile `t` that leads to tile `to` (x/z); 0 if they are not linked */
static s32 tilePortalEnds(StandTile *t, StandTile *to, f32 *ax, f32 *az, f32 *bx, f32 *bz)
{
    s32 n = tileCount(t), i;

    for (i = 0; i < n; i++)
    {
        if (tileLink(t, i) == to)
        {
            s32 j = (i + 1) % n;

            *ax = (f32) t->points[i].x * inv_level_scale;
            *az = (f32) t->points[i].z * inv_level_scale;
            *bx = (f32) t->points[j].x * inv_level_scale;
            *bz = (f32) t->points[j].z * inv_level_scale;
            return 1;
        }
    }
    return 0;
}

/* the middle of the edge of tile `t` that leads to tile `to`; 0 if they are not linked */
static s32 tilePortal(StandTile *t, StandTile *to, f32 *x, f32 *y, f32 *z)
{
    s32 n = tileCount(t), i;

    for (i = 0; i < n; i++)
    {
        if (tileLink(t, i) == to)
        {
            s32 j = (i + 1) % n;

            *x = ((f32) t->points[i].x + (f32) t->points[j].x) * 0.5f * inv_level_scale;
            *y = ((f32) t->points[i].y + (f32) t->points[j].y) * 0.5f * inv_level_scale;
            *z = ((f32) t->points[i].z + (f32) t->points[j].z) * 0.5f * inv_level_scale;
            return 1;
        }
    }
    return 0;
}

static void graphBuild(void)
{
    StandTile *first = (StandTile *) ptr_firstroom_0, *t;
    s32 n = 0, i, k, big = 0, nc;

    if (B.g.cap > 0)
    {
        pblGraphFree(&B.g);
    }
    free(B.tiles);
    B.tiles = NULL;
    B.ntiles = 0;
    B.climbCut = 0;
    B.maxStep = getenv("GE_PLAYERBOT_MAXSTEP") != NULL ? (f32) atof(getenv("GE_PLAYERBOT_MAXSTEP")) : 45.0f;
    if (first == NULL)
    {
        portConLogf("ge: playerbot: this stage has no stan tiles\n");
        return;
    }
    for (t = first; *(u32 *) t != 0; t = tileNext(t))
    {
        n++;
    }
    B.tiles = (StandTile **) malloc((size_t) n * sizeof(StandTile *));
    if (B.tiles == NULL || !pblGraphAlloc(&B.g, n, n * 12))
    {
        portConLogf("ge: playerbot: out of memory for %d tiles\n", n);
        return;
    }
    for (t = first, i = 0; *(u32 *) t != 0; t = tileNext(t), i++)
    {
        f32 x = 0.0f, y = 0.0f, z = 0.0f;
        s32 c = tileCount(t);

        for (k = 0; k < c; k++)
        {
            x += (f32) t->points[k].x;
            y += (f32) t->points[k].y;
            z += (f32) t->points[k].z;
        }
        if (c > 0)
        {
            x = x / (f32) c * inv_level_scale;
            y = y / (f32) c * inv_level_scale;
            z = z / (f32) c * inv_level_scale;
        }
        B.tiles[i] = t;
        pblGraphNode(&B.g, x, y, z);
    }
    B.ntiles = n;
    for (i = 0; i < n; i++)
    {
        s32 nb[16], m = 0, c = tileCount(B.tiles[i]);

        for (k = 0; k < c && m < 16; k++)
        {
            StandTile *lt = tileLink(B.tiles[i], k);
            s32 j = lt != NULL ? tileIndex(lt) : -1;

            if (j >= 0)
            {
                /* the step at the shared edge: this tile's edge height vs the neighbour's edge back to us. Bond can
                 * drop down any step but only climb a small one (a kerb, a stair), so a tall step up is one-way. */
                StandTile *t = B.tiles[i];
                s32 k2 = (k + 1) % c, n2 = tileCount(lt), q;
                f32 ya = ((f32) t->points[k].y + (f32) t->points[k2].y) * 0.5f * inv_level_scale;
                f32 yb = ya;

                for (q = 0; q < n2; q++)
                {
                    if (tileLink(lt, q) == t)
                    {
                        yb = ((f32) lt->points[q].y + (f32) lt->points[(q + 1) % n2].y) * 0.5f * inv_level_scale;
                        break;
                    }
                }
                if (yb - ya > B.maxStep)
                {
                    B.climbCut++;
                    continue;
                }
                nb[m++] = j;
            }
        }
        pblGraphEdges(&B.g, i, nb, m);
    }
    nc = pblComponents(&B.g, &big);
    portConLogf("ge: playerbot: nav mesh %d stan tiles, %d links (%d too tall to climb), %d component(s), largest %d\n", B.g.n,
                B.g.ne, B.climbCut, nc, big);
}

static f32 segDist2(f32 px, f32 pz, f32 ax, f32 az, f32 bx, f32 bz)
{
    f32 dx = bx - ax, dz = bz - az;
    f32 len2 = dx * dx + dz * dz;
    f32 t = len2 > 0.0f ? ((px - ax) * dx + (pz - az) * dz) / len2 : 0.0f;
    f32 qx, qz;

    t = pblClamp(t, 0.0f, 1.0f);
    qx = ax + t * dx - px;
    qz = az + t * dz - pz;
    return qx * qx + qz * qz;
}

static s32 doorIsBlocked(PropRecord *d);
static s32 doorShut(PropRecord *d);

/* a door Bond cannot get through: it needs a key he lacks (a padlock is not counted: the bot shoots those off) */
static s32 doorLocked(DoorRecord *d)
{
    return d->keyflags != 0 && !bondinvCheckHasKeyFlags(d->keyflags);
}

/* make tiles at a door Bond cannot open expensive (re-done before each plan; a key picked up opens them again) */
static s32 graphApplyLocks(void)
{
    PropRecord *p;
    s32 a, nd = 0;

    pblGraphClearMarks(&B.g);
    for (p = chrpropGetActiveTail(); p != NULL; p = p->prev)
    {   /* the gas of a burst gas tank hurts: keep routes out of it */
        if (p->type == PROP_TYPE_OBJ && p->obj != NULL && p->obj->type == PROPDEF_GAS_RELEASING && !objIsHealthy(p->obj))
        {
            for (a = 0; a < B.g.n; a++)
            {
                f32 dx = B.g.x[a] - p->pos.x, dz = B.g.z[a] - p->pos.z;

                if (dx * dx + dz * dz < 400.0f * 400.0f && fabsf(B.g.y[a] - p->pos.y) < 300.0f)
                {
                    B.g.npen[a] += 4000.0f;
                }
            }
        }
    }
    for (p = chrpropGetActiveTail(); p != NULL; p = p->prev)
    {
        if (p->type != PROP_TYPE_DOOR || p->door == NULL ||
            !(doorLocked(p->door) || doorIsBlocked(p) || (p == B.subDoor && doorShut(p))))
        {   /* (while walking to a door's switch, the shut door itself is not a way to it) */
            continue;
        }
        nd++;
        {
            f32 reach = 90.0f + fabsf(p->door->frac) * 0.5f;   /* a gate is as wide as it slides */

            for (a = 0; a < B.g.n; a++)
            {
                f32 dx = B.g.x[a] - p->pos.x, dz = B.g.z[a] - p->pos.z;

                if (dx * dx + dz * dz < reach * reach && fabsf(B.g.y[a] - p->pos.y) < 250.0f)
                {
                    B.g.npen[a] += 20000.0f;
                }
            }
        }
    }
    return nd;
}

/* the node for a goal: its tile when known, else the nearest tile centre */
static s32 nodeNear(f32 x, f32 y, f32 z, StandTile *tile)
{
    s32 i = tile != NULL ? tileIndex(tile) : -1;

    return i >= 0 ? i : pblNearest(&B.g, x, y, z);
}

/* ---- navigation ------------------------------------------------------------------------------------------------- */

static void navReset(void)
{
    B.pathN = 0;
    B.pathAt = 0;
    B.goalValid = 0;
}

static void navPlan(f32 x, f32 y, f32 z, StandTile *goalTile)
{
    struct player *p = bond();
    s32 a, b;

    graphApplyLocks();
    a = nodeNear(p->prop->pos.x, p->prop->pos.y, p->prop->pos.z, bondTile());
    b = nodeNear(x, y, z, goalTile);
    B.pathN = (a >= 0 && b >= 0) ? pblAstar(&B.g, a, b, B.path, MAXPATH) : 0;
    B.pathAt = 0;
    B.aimAt = 0;
    B.offPath = 0;
    B.planTick = B.tick;
    if (B.pathN == 0 && B.verbose && (B.tick - B.lastNoRouteLog > 600 || B.lastNoRouteLog == 0))
    {
        B.lastNoRouteLog = B.tick > 0 ? B.tick : 1;
        portConLogf("ge: playerbot: no route on the stan mesh from tile %d (room %d) to tile %d (room %d) - walking straight\n", a,
                    a >= 0 ? (s32) B.tiles[a]->room : -1, b, b >= 0 ? (s32) B.tiles[b]->room : -1);
    }
}

static void stuckReport(const char *what)
{
    struct player *p = bond();
    s32 node = B.pathAt < B.pathN ? B.path[B.pathAt] : -1;

    snprintf(B.blocked, sizeof(B.blocked), "%s at room %d pos %.0f,%.0f,%.0f next room %d step %d '%s'", what, bondRoom(),
             p->prop->pos.x, p->prop->pos.y, p->prop->pos.z, node >= 0 ? (s32) B.tiles[node]->room : -1, B.step + 1,
             B.step < B.plan.n ? B.plan.s[B.step].text : "-");
    portConLogf("ge: playerbot: STUCK %s\n", B.blocked);
    ge_shot_force = 1;
    {   /* what is around: doors (locked?) and big objects */
        PropRecord *d;

        for (d = chrpropGetActiveTail(); d != NULL; d = d->prev)
        {
            f32 dx = d->pos.x - p->prop->pos.x, dz = d->pos.z - p->prop->pos.z;

            if (dx * dx + dz * dz > 600.0f * 600.0f || fabsf(d->pos.y - p->prop->pos.y) > 400.0f)
            {
                continue;
            }
            if (d->type == PROP_TYPE_DOOR && d->door != NULL)
            {
                portConLogf("ge: playerbot:   door at %.0f,%.0f,%.0f (%.0f away) state %d open %.2f/%.2f keyflags %x locked %d padlocked %d\n",
                            d->pos.x, d->pos.y, d->pos.z, sqrtf(dx * dx + dz * dz), (s32) d->door->openstate,
                            d->door->openPosition, d->door->maxFrac, (u32) d->door->keyflags, doorLocked(d->door),
                            !doorIsPadlockFree(d->door));
            }
            else if (d->type == PROP_TYPE_OBJ && d->obj != NULL && dx * dx + dz * dz < 300.0f * 300.0f)
            {
                portConLogf("ge: playerbot:   object type %d model %d at %.0f,%.0f,%.0f (%.0f away)\n", (s32) d->obj->type,
                            (s32) d->obj->obj, d->pos.x, d->pos.y, d->pos.z, sqrtf(dx * dx + dz * dz));
            }
        }
    }
}

/* open doors ahead: tap B when a closed, openable door is close and roughly in front */
static void doorTick(void)
{
    struct player *p = bond();
    PropRecord *d;

    if (B.tick - B.lastDoorTap < 20)
    {
        return;
    }
    for (d = chrpropGetActiveTail(); d != NULL; d = d->prev)
    {
        f32 dx, dz, dist, ang;

        if (d->type != PROP_TYPE_DOOR || d->door == NULL)
        {
            continue;
        }
        if (d->door->openstate == DOORSTATE_OPENING || d->door->openPosition > d->door->maxFrac * 0.8f)
        {
            continue;
        }
        dx = d->pos.x - p->prop->pos.x;
        dz = d->pos.z - p->prop->pos.z;
        dist = sqrtf(dx * dx + dz * dz);
        if (dist > 190.0f || fabsf(d->pos.y - p->prop->pos.y) > 220.0f)
        {
            continue;
        }
        ang = pblWrap180(pblYawTo(dx, dz) - p->vv_theta);
        if (fabsf(ang) < 60.0f || dist < 90.0f)
        {
            B.btn |= BTN_B;
            B.lastDoorTap = B.tick;
            return;
        }
    }
}

static void steerTo(f32 x, f32 y, f32 z, f32 slowRadius);

static s32 propIsKey(PropRecord *q, u32 need)
{
    return q->type == PROP_TYPE_OBJ && q->obj != NULL && q->obj->type == PROPDEF_KEY &&
           (((KeyRecord *) q->obj)->keyflags & need) != 0;
}

/* a key with any of the `need` flags: lying in the level (*holder = NULL) or carried by a live chr (*holder = it) */
static PropRecord *findKeyProp(u32 need, ChrRecord **holder)
{
    PropRecord *p, *q;
    s32 n = get_numguards(), i;

    *holder = NULL;
    for (p = chrpropGetActiveTail(); p != NULL; p = p->prev)
    {
        if (propIsKey(p, need))
        {
            return p;
        }
    }
    for (i = 0; i < n; i++)
    {
        ChrRecord *c = &g_ChrSlots[i];

        if (c->model == NULL || c->prop == NULL)
        {
            continue;
        }
        for (q = c->prop->child; q != NULL; q = q->next)
        {
            if (propIsKey(q, need))
            {
                *holder = c;
                return q;
            }
        }
    }
    return NULL;
}

static s32 doorShut(PropRecord *d)
{
    return d->door->openPosition < d->door->maxFrac * 0.7f;   /* openPosition runs 0..maxFrac (frac is the door's size) */
}

static s32 doorIsBlocked(PropRecord *d)
{
    s32 i;

    for (i = 0; i < B.nBlockedDoors; i++)
    {
        if (B.blockedDoors[i] == d)
        {
            return B.tick - B.blockedAt[i] < 1800;   /* try it again after 30 s: scripts open doors later */
        }
    }
    return 0;
}

/* a shut door that the next stretch of the route goes through, within reach */
static PropRecord *routeDoor(void)
{
    struct player *p = bond();
    PropRecord *d;
    s32 k;

    for (d = chrpropGetActiveTail(); d != NULL; d = d->prev)
    {
        f32 dx, dz, reach;

        if (d->type != PROP_TYPE_DOOR || d->door == NULL || !doorShut(d) || doorIsBlocked(d) || d == B.subDoor)
        {
            continue;
        }
        /* a gate is as wide as it slides (frac), and its prop sits in its middle */
        reach = 150.0f + fabsf(d->door->frac) * 0.5f;
        dx = d->pos.x - p->prop->pos.x;
        dz = d->pos.z - p->prop->pos.z;
        if (dx * dx + dz * dz > (300.0f + reach) * (300.0f + reach) || fabsf(d->pos.y - p->prop->pos.y) > 300.0f)
        {
            continue;
        }
        for (k = B.pathAt; k < B.pathN && k < B.pathAt + 12; k++)
        {
            s32 nd = B.path[k];
            f32 ex = B.g.x[nd] - d->pos.x, ez = B.g.z[nd] - d->pos.z;

            if (ex * ex + ez * ez < reach * reach && fabsf(B.g.y[nd] - d->pos.y) < 300.0f)
            {
                return d;
            }
        }
    }
    return NULL;
}

/* Get through a shut door on the route: walk up, face it, press B, wait while it opens. Returns 1 while busy. */
static s32 doorHandle(f32 gx, f32 gy, f32 gz, StandTile *goalTile)
{
    struct player *p = bond();
    PropRecord *d;
    f32 dx, dz, dist;

    if (B.subLock != NULL)
    {
        return 0;   /* walking up to shoot a padlock off: its door is shut, that is the point */
    }
    if (B.doorProp == NULL && (B.tick % 10) == 0)
    {
        B.doorProp = routeDoor();
        B.doorTaps = 0;
        B.doorOpened = 0;
        B.doorSince = B.tick;
        if (B.doorProp != NULL && B.verbose >= 2)
        {
            portConLogf("ge: playerbot: door on the route at %.0f,%.0f,%.0f\n", B.doorProp->pos.x, B.doorProp->pos.y,
                        B.doorProp->pos.z);
        }
    }
    d = B.doorProp;
    if (d == NULL)
    {
        return 0;
    }
    if (d->type != PROP_TYPE_DOOR || d->door == NULL || !doorShut(d))
    {
        B.doorProp = NULL;   /* open: carry on */
        B.stuck.lastGain = B.tick;
        return 0;
    }
    if (!doorIsPadlockFree(d->door) && B.subLock == NULL)
    {   /* padlocked: shoot the lock off (setup "lock door" links) */
        LockDoorRecord *ld;

        for (ld = g_LevelLoadPropLockDoor; ld != NULL; ld = ld->next)
        {
            if (ld->door == d->door && ld->lock != NULL && ld->lock->prop != NULL && objIsHealthy(ld->lock))
            {
                portConLogf("ge: playerbot: door at %.0f,%.0f,%.0f is padlocked - shooting the lock at %.0f,%.0f,%.0f\n", d->pos.x,
                            d->pos.y, d->pos.z, ld->lock->prop->pos.x, ld->lock->prop->pos.y, ld->lock->prop->pos.z);
                B.subLock = ld->lock;
                B.subSince = B.tick;
                B.doorProp = NULL;
                navReset();
                return 0;
            }
        }
    }
    if (d->door->keyflags != 0 && !bondinvCheckHasKeyFlags(d->door->keyflags) && B.subKeyNeed == 0)
    {   /* a key door: fetch the key if it is anywhere in the level (on the floor, or on a guard to take down) */
        ChrRecord *holder = NULL;
        PropRecord *kp = findKeyProp(d->door->keyflags, &holder);

        if (kp != NULL && B.tick - B.keyGiveUpAt > 1800)
        {
            portConLogf("ge: playerbot: door at %.0f,%.0f,%.0f needs key %x: %s at %.0f,%.0f,%.0f - fetching it\n", d->pos.x,
                        d->pos.y, d->pos.z, (u32) d->door->keyflags, holder != NULL ? "carried by a guard" : "lying",
                        holder != NULL ? holder->prop->pos.x : kp->pos.x, holder != NULL ? holder->prop->pos.y : kp->pos.y,
                        holder != NULL ? holder->prop->pos.z : kp->pos.z);
            B.subKeyNeed = d->door->keyflags;
            B.subSince = B.tick;
            B.doorProp = NULL;
            navReset();
            return 0;
        }
        if (B.tick - B.lastKeyLog > 600)
        {
            B.lastKeyLog = B.tick;
            portConLogf("ge: playerbot: door at %.0f,%.0f,%.0f needs a key (flags %x) Bond does not have and cannot find\n",
                        d->pos.x, d->pos.y, d->pos.z, (u32) d->door->keyflags);
        }
    }
    dx = d->pos.x - p->prop->pos.x;
    dz = d->pos.z - p->prop->pos.z;
    dist = sqrtf(dx * dx + dz * dz);
    if (d->door->openstate == DOORSTATE_OPENING || d->door->openstate == DOORSTATE_WAITING)
    {   /* it is coming: wait facing it */
        B.doorOpened = 1;
        lookAt(d->pos.x, p->prop->pos.y, d->pos.z, 15.0f);
        B.stuck.lastGain = B.tick;
        return 1;
    }
    if (dist > 120.0f)
    {
        steerTo(d->pos.x, d->pos.y, d->pos.z, 200.0f);
    }
    else if (fabsf(lookAt(d->pos.x, p->prop->pos.y, d->pos.z, 15.0f)) > 25.0f)
    {
        return 1;
    }
    if (dist < 260.0f + fabsf(d->door->frac) * 0.5f && B.tick - B.lastDoorTap >= 25)
    {
        B.btn |= BTN_B;
        B.lastDoorTap = B.tick;
        B.doorTaps++;
    }
    if (B.doorTaps >= 3 && B.subSwitch == NULL)
    {   /* pressing it does nothing: a console / switch elsewhere may work it (setup "switch" links) */
        LinkRecord *l;
        s32 nsw = 0, pick;

        if (B.subTriesDoor != d)
        {
            B.subTriesDoor = d;   /* tries are counted per door */
            B.subTries = 0;
        }
        for (l = (LinkRecord *) g_LevelLoadPropSwitch; l != NULL; l = l->next)
        {
            nsw += l->second == d && l->first != NULL;
        }
        pick = nsw > 0 ? B.subTries % nsw : 0;   /* several switches can work one door: try them in turn */
        for (l = (LinkRecord *) g_LevelLoadPropSwitch; l != NULL; l = l->next)
        {
            if (l->second == d && l->first != NULL && B.subTries < 8 && pick-- == 0)
            {
                portConLogf("ge: playerbot: door at %.0f,%.0f,%.0f is worked by a switch at %.0f,%.0f,%.0f - going to it\n",
                            d->pos.x, d->pos.y, d->pos.z, l->first->pos.x, l->first->pos.y, l->first->pos.z);
                B.subSwitch = l->first;
                B.subDoor = d;
                B.subGoalSet = 0;
                B.subTries++;
                B.subPhase = 0;
                B.subSince = B.tick;
                B.doorProp = NULL;
                navReset();
                return 0;
            }
        }
    }
    if (B.doorOpened && B.doorTaps > 8)
    {   /* it opens but we keep missing the gap: start over on this door */
        B.doorTaps = 0;
        B.doorOpened = 0;
        B.doorSince = B.tick;
    }
    if (B.doorTaps > 8 || B.tick - B.doorSince > 900)
    {   /* it will not open for us (a switch or a script opens it): route around it if there is a way */
        portConLogf("ge: playerbot: door at %.0f,%.0f,%.0f will not open (state %d, keyflags %x) - avoiding it\n", d->pos.x,
                    d->pos.y, d->pos.z, (s32) d->door->openstate, (u32) d->door->keyflags);
        {
            s32 i;

            for (i = 0; i < B.nBlockedDoors && B.blockedDoors[i] != d; i++)
            {
            }
            if (i < 16)
            {
                B.blockedDoors[i] = d;
                B.blockedAt[i] = B.tick;
                B.nBlockedDoors = i >= B.nBlockedDoors ? i + 1 : B.nBlockedDoors;
            }
        }
        B.doorProp = NULL;
        navPlan(gx, gy, gz, goalTile);
        return 0;
    }
    return 1;
}

/* steer toward (x,z) at full run, easing off on sharp turns */
static void steerTo(f32 x, f32 y, f32 z, f32 slowRadius)
{
    struct player *p = bond();
    f32 dx = x - p->prop->pos.x, dz = z - p->prop->pos.z;
    f32 dist = sqrtf(dx * dx + dz * dz);
    f32 err = lookToward(pblYawTo(dx, dz), 0.0f, 20.0f);
    f32 ae = fabsf(err);
    s32 sp = 80;

    B.lastSteerYaw = pblYawTo(dx, dz);

    (void) y;
    if (ae > 70.0f)
    {
        sp = 0;
    }
    else if (ae > 35.0f)
    {
        sp = 30;
    }
    if (dist < slowRadius)
    {
        sp = sp > 45 ? 45 : sp;
    }
    B.sy = (s8) sp;
}

/* the floor under Bond (his prop sits at eye height, ~150 above it): the centre height of his tile */
static f32 bondGroundY(void)
{
    s32 i = bondTile() != NULL ? tileIndex(bondTile()) : -1;

    return i >= 0 ? B.g.y[i] : bond()->prop->pos.y - 150.0f;
}

static f32 navMetric(f32 dist)
{
    return B.pathN > B.pathAt ? (f32) (B.pathN - B.pathAt) * 100.0f + 1000.0f : dist;
}

/* Walk toward a goal; returns 1 once within `radius` (2D, and within a floor's height). */
static s32 navTo(f32 x, f32 y, f32 z, f32 radius, StandTile *goalTile)
{
    struct player *p = bond();
    f32 dx = x - p->prop->pos.x, dz = z - p->prop->pos.z, dy = y - bondGroundY();
    f32 dist = sqrtf(dx * dx + dz * dz);
    s32 lvl;

    /* arrived: close in x/z and on the same floor (goal heights are floor-ish: pads, objects, doors) */
    if (dist <= radius && dy > -120.0f && dy < 200.0f)
    {
        navReset();
        return 1;
    }
    if (!B.goalValid || fabsf(B.goalX - x) > 30.0f || fabsf(B.goalZ - z) > 30.0f || fabsf(B.goalY - y) > 60.0f)
    {
        B.goalX = x;
        B.goalY = y;
        B.goalZ = z;
        B.goalValid = 1;
        navPlan(x, y, z, goalTile);
        pblStuckReset(&B.stuck, B.tick, p->prop->pos.x, p->prop->pos.z, navMetric(dist));
    }

    /* stuck handling: progress = tiles of the route left (or the distance once off the graph) going down */
    lvl = pblStuckFeed(&B.stuck, B.tick, p->prop->pos.x, p->prop->pos.z, navMetric(dist));
    if (lvl == 0 && B.tick - B.stuck.lastGain < 30)
    {
        B.stepFails = 0;   /* moving again: earlier stuck episodes do not count against the step */
    }
    if (lvl >= 1 && B.tick >= B.recoverUntil && (B.tick - B.stuck.since) % 75 == 0)
    {   /* something the tiles do not know about (a crate, a booth, a rail) is in the way: back off, then walk off at an
         * angle, alternating sides and widening (45, 75, 105 degrees), so the wall sliding of the collision can take us round */
        s32 side = (B.stuckEvents & 1) ? 1 : -1;
        f32 mag = 45.0f + 30.0f * (f32) ((B.stuckEvents / 2) % 3);

        B.recoverYaw = B.lastSteerYaw + (f32) side * mag;
        B.backUntil = B.tick + 12;
        B.recoverUntil = B.tick + 55;
        B.stuckEvents++;
        B.btn |= BTN_B;   /* a door or a switch in the way */
        if (B.verbose >= 2)
        {
            portConLogf("ge: playerbot: stuck level %d: detour heading %+.0f deg\n", lvl, (f32) side * mag);
        }
    }
    if (lvl >= 2 && B.tick - B.stuck.since == 0)
    {
        /* make the link being walked expensive and plan again */
        s32 cur = tileIndex(bondTile());

        if (cur >= 0 && B.pathAt < B.pathN)
        {
            pblGraphPenalize(&B.g, cur, B.path[B.pathAt], 3000.0f);
        }
        navPlan(x, y, z, goalTile);
        B.stuck.bestDist = navMetric(dist);   /* a new route is not progress, nor a step back */
    }
    if (lvl >= 3 && B.tick - B.stuck.since == 0)
    {
        stuckReport("no progress for 12 s");
        B.stepFails++;
    }
    if (B.tick < B.recoverUntil)
    {
        if (B.tick < B.backUntil)
        {
            B.sy = -60;
            lookToward(p->vv_theta, 0.0f, 0.0f);
        }
        else
        {
            f32 e = lookToward(B.recoverYaw, 0.0f, 20.0f);

            B.sy = fabsf(e) < 40.0f ? 80 : 0;
        }
        return 0;
    }

    if (doorHandle(x, y, z, goalTile))
    {
        return 0;
    }

    /* follow the tile path: Bond's own tile tells how far along it he is */
    {
        StandTile *bt = bondTile();
        s32 cur = bt != NULL ? tileIndex(bt) : -1, k, found = 0;

        for (k = B.pathAt > 2 ? B.pathAt - 2 : 0; k < B.pathN && k < B.pathAt + 40; k++)
        {
            if (B.path[k] == cur)
            {
                B.pathAt = k + 1;
                found = 1;
                break;
            }
        }
        B.offPath = found ? 0 : B.offPath + 1;
        if (B.offPath > 90 && B.pathN > 0)
        {
            navPlan(x, y, z, goalTile);   /* knocked off the route (or took a shortcut): plan from here */
            B.stuck.bestDist = navMetric(dist);
        }
        if (B.aimAt < B.pathAt)
        {
            B.aimAt = B.pathAt;
        }
        if (B.pathAt < B.pathN && cur >= 0)
        {   /* string-pull through the shared edges of the next tiles (the funnel keeps Bond's radius off the walls) */
            static f32 ax[FUNNEL_N], az[FUNNEL_N], bx[FUNNEL_N], bz[FUNNEL_N], cx[FUNNEL_N + 1], cz[FUNNEL_N + 1];
            f32 ox[4], oz[4], ex, ez, yy;
            s32 np = 0, nc, last = cur;

            cx[0] = B.g.x[cur];
            cz[0] = B.g.z[cur];
            for (k = B.pathAt; k < B.pathN && np < FUNNEL_N; k++)
            {
                s32 nd = B.path[k];

                if (!tilePortalEnds(B.tiles[last], B.tiles[nd], &ax[np], &az[np], &bx[np], &bz[np]))
                {
                    break;   /* not linked (off the route): stop here */
                }
                cx[np + 1] = B.g.x[nd];
                cz[np + 1] = B.g.z[nd];
                np++;
                last = nd;
            }
            if (k >= B.pathN)
            {
                ex = x;   /* the whole rest of the route: end at the goal */
                ez = z;
            }
            else
            {
                ex = B.g.x[last];
                ez = B.g.z[last];
            }
            nc = pblFunnel(ax, az, bx, bz, cx, cz, np, p->prop->pos.x, p->prop->pos.z, ex, ez, 28.0f, ox, oz, 4);
            if (nc > 0)
            {
                s32 c = 0;
                f32 ddx = ox[0] - p->prop->pos.x, ddz = oz[0] - p->prop->pos.z;

                if (nc > 1 && ddx * ddx + ddz * ddz < 25.0f * 25.0f)
                {
                    c = 1;   /* at the corner already: the next one */
                }
                yy = B.g.y[last];
                steerTo(ox[c], yy, oz[c], (c == nc - 1 && k >= B.pathN) ? radius + 60.0f : 0.0f);
                doorTick();
                return 0;
            }
        }
    }
    steerTo(x, y, z, radius + 60.0f);
    doorTick();
    return 0;
}

/* ---- combat ------------------------------------------------------------------------------------------------------- */

static PropRecord *pickTarget(f32 range)
{
    struct player *p = bond();
    s32 n = get_numguards(), i;
    PropRecord *best = NULL;
    f32 bd = range * range;

    for (i = 0; i < n; i++)
    {
        ChrRecord *c = &g_ChrSlots[i];
        f32 dx, dy, dz, d;

        if (!chrHostile(c) || c->prop->stan == NULL)
        {
            continue;
        }
        dx = c->prop->pos.x - p->prop->pos.x;
        dy = c->prop->pos.y - p->prop->pos.y;
        dz = c->prop->pos.z - p->prop->pos.z;
        d = dx * dx + dy * dy + dz * dz;
        if (d > bd || fabsf(dy) > 400.0f)
        {
            continue;
        }
        if (!botLine(bondTile(), p->prop->pos.x, p->prop->pos.z, c->prop->pos.x, c->prop->pos.z, c->prop->stan,
                     CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_PATHBLOCKER | CDTYPE_AIOPAQUE))
        {
            continue;
        }
        best = c->prop;
        bd = d;
    }
    return best;
}

/* aim at the prop and fire when lined up; `aimY` = height offset to aim at */
static void shootAt(f32 x, f32 y, f32 z, f32 width)
{
    struct player *p = bond();
    f32 dx = x - p->prop->pos.x, dz = z - p->prop->pos.z;
    f32 dist = sqrtf(dx * dx + dz * dz);
    f32 err = lookAt(x, y, z, 15.0f);
    f32 tol = pblPitchTo(width, dist) * 1.5f;
    s32 w = getCurrentPlayerWeaponId(GUNRIGHT);

    if (tol < 2.5f)
    {
        tol = 2.5f;
    }
    if (fabsf(err) < tol)
    {
        s32 ph = (B.tick - B.fireTick) % 8;

        if (isAuto(w) ? ph < 7 : ph < 2)
        {
            B.btn |= BTN_Z;
        }
    }
    else
    {
        B.fireTick = B.tick;
    }
}

/* fight the current target if any; returns 1 when the bot is busy fighting this tick */
static s32 combatTick(f32 range)
{
    PropRecord *t;

    if (B.peace)
    {
        return 0;
    }
    if ((B.tick % 4) == 0 || B.target == NULL)
    {
        t = pickTarget(range);
        if (t == NULL && B.tick - B.lastHurtTick < 120)
        {   /* hurt by someone the tile line test cannot see (a catwalk, another floor, through a window): fight the
             * nearest guard that is in an attack, he has the line on us */
            struct player *p = bond();
            s32 n = get_numguards(), i;
            f32 bd = 2500.0f * 2500.0f;

            for (i = 0; i < n; i++)
            {
                ChrRecord *c = &g_ChrSlots[i];
                f32 dx, dy, dz, d;

                if (!chrHostile(c) || (c->actiontype != ACT_ATTACK && c->actiontype != ACT_ATTACKWALK &&
                                       c->actiontype != ACT_ATTACKROLL))
                {
                    continue;
                }
                dx = c->prop->pos.x - p->prop->pos.x;
                dy = c->prop->pos.y - p->prop->pos.y;
                dz = c->prop->pos.z - p->prop->pos.z;
                d = dx * dx + dy * dy + dz * dz;
                if (d < bd)
                {
                    bd = d;
                    t = c->prop;
                }
            }
        }
        if (t != B.target)
        {
            B.target = t;
            B.targetSince = B.tick;
        }
    }
    t = B.target;
    if (t == NULL || t->chr == NULL || !chrHostile(t->chr))
    {
        B.target = NULL;
        return 0;
    }
    B.lastSeenTick = B.tick;
    if (B.verbose >= 3)
    {
        portConLogf("ge: playerbot: fight chr %d at %.0f,%.0f,%.0f (bond %.0f,%.0f,%.0f)\n", t->chr->chrnum, t->pos.x, t->pos.y,
                    t->pos.z, bond()->prop->pos.x, bond()->prop->pos.y, bond()->prop->pos.z);
    }
    shootAt(t->pos.x, t->pos.y + 20.0f, t->pos.z, 25.0f);
    /* keep a little lateral motion so we are harder to hit */
    if (((B.tick / 45) & 1) != 0)
    {
        B.btn |= BTN_CL;
    }
    else
    {
        B.btn |= BTN_CR;
    }
    return 1;
}

/* ---- loot: dropped guns, ammo boxes, body armour close by ------------------------------------------------------ */

static s32 propActive(PropRecord *q)
{
    PropRecord *p;

    for (p = chrpropGetActiveTail(); p != NULL; p = p->prev)
    {
        if (p == q)
        {
            return 1;
        }
    }
    return 0;
}

static s32 isLoot(PropRecord *p)
{
    s32 i;

    if (p->type == PROP_TYPE_WEAPON && p->weapon != NULL)
    {
        for (i = 0; i < (s32) (sizeof(sGunPrefs) / sizeof(sGunPrefs[0])); i++)
        {
            if (p->weapon->weaponnum == sGunPrefs[i])
            {
                return 1;   /* never grenades or mines: those may be live */
            }
        }
        return 0;
    }
    return p->type == PROP_TYPE_OBJ && p->obj != NULL &&
           (p->obj->type == PROPDEF_AMMO || (p->obj->type == PROPDEF_ARMOUR && bond()->bondarmour < 0.9f));
}

/* returns 1 while walking to a pick-up */
static s32 lootTick(void)
{
    struct player *p = bond();
    PropRecord *q;
    s32 i;
    s32 gun = bestGun();
    /* short of ammo: look further for guns and boxes */
    f32 reach = (gun == ITEM_UNARMED || currentPlayerGetAmmoCount(get_ammo_type_for_weapon(gun)) < 30) ? 900.0f : 420.0f;

    if (B.lootProp != NULL)
    {
        if (!propActive(B.lootProp) || B.tick > B.lootUntil)
        {
            B.lootProp = NULL;
            navReset();
            return 0;
        }
        q = B.lootProp;
        navTo(q->pos.x, q->pos.y, q->pos.z, 8.0f, q->stan);
        return 1;
    }
    if ((B.tick % 30) != 0 || B.noLoot)
    {
        return 0;
    }
    for (q = chrpropGetActiveTail(); q != NULL; q = q->prev)
    {
        f32 dx, dz;
        s32 seen = 0;

        if (!isLoot(q) || q->stan == NULL)
        {
            continue;
        }
        dx = q->pos.x - p->prop->pos.x;
        dz = q->pos.z - p->prop->pos.z;
        if (dx * dx + dz * dz > reach * reach || fabsf(q->pos.y - p->prop->pos.y) > 220.0f)
        {
            continue;
        }
        for (i = 0; i < 16; i++)
        {
            seen |= B.lootTried[i] == q;
        }
        if (seen || !botLine(bondTile(), p->prop->pos.x, p->prop->pos.z, q->pos.x, q->pos.z, q->stan,
                             CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_PATHBLOCKER))
        {
            continue;
        }
        B.lootTried[B.lootN++ & 15] = q;
        B.lootProp = q;
        B.lootUntil = B.tick + (reach > 500.0f ? 600 : 300);
        if (B.verbose >= 2)
        {
            portConLogf("ge: playerbot: picking up %s at %.0f away\n", q->type == PROP_TYPE_WEAPON ? "a gun" : "ammo/armour",
                        sqrtf(dx * dx + dz * dz));
        }
        navReset();
        return 1;
    }
    return 0;
}

/* ---- objectives (the `auto` step) ----------------------------------------------------------------------------- */

/* the next open objective criterion the generic handlers can do: *op = DESTROY / COLLECT / PHOTO, *tag */
static s32 autoNext(s32 *op, s32 *tag)
{
    s32 i;

    for (i = 0; i < objectiveGetCount(); i++)
    {
        MissionObjectiveRecord *o;

        if (!objectiveOpen(i) || objective_ptrs[i] == NULL)
        {
            continue;
        }
        for (o = (MissionObjectiveRecord *) &objective_ptrs[i]->id; o->type != PROPDEF_OBJECTIVE_END;
             o = (MissionObjectiveRecord *) (sizepropdef((PropDefHeaderRecord *) o) + (PropDefHeaderRecord *) o))
        {
            ObjectRecord *obj;

            if (o->type != PROPDEF_OBJECTIVE_DESTROY_OBJECT && o->type != PROPDEF_OBJECTIVE_COLLECT_OBJECT &&
                o->type != PROPDEF_OBJECTIVE_PHOTOGRAPH)
            {
                continue;
            }
            obj = objByTag(o->ObjRefID);
            if (obj == NULL || obj->prop == NULL || !objIsHealthy(obj))
            {
                continue;
            }
            if (o->type == PROPDEF_OBJECTIVE_DESTROY_OBJECT)
            {
                *op = PBL_DESTROY_OBJ;
            }
            else if (o->type == PROPDEF_OBJECTIVE_COLLECT_OBJECT)
            {
                if (bondinvHasPropInInv(obj->prop))
                {
                    continue;
                }
                *op = PBL_COLLECT_OBJ;
            }
            else
            {
                if (o->TextID != 0)
                {
                    continue;   /* already photographed */
                }
                *op = PBL_PHOTO_OBJ;
            }
            *tag = o->ObjRefID;
            return 1;
        }
    }
    return 0;
}

/* ---- plan --------------------------------------------------------------------------------------------------------- */

static void planLoad(void)
{
    char path[400];
    FILE *f;
    char *buf;
    long len;
    char err[160];

    B.planLoaded = 0;
    B.plan.n = 0;
    if (B.planfile[0] != '\0')
    {
        snprintf(path, sizeof(path), "%s", B.planfile);
    }
    else
    {
        snprintf(path, sizeof(path), "%s/%s.plan", B.plandir, stageName(B.stage));
    }
    f = fopen(path, "rb");
    if (f == NULL)
    {
        portConLogf("ge: playerbot: no plan %s - using 'auto' then 'exit'\n", path);
        pblParsePlan("auto\nexit\n", &B.plan, err, sizeof(err));
        B.planLoaded = 1;
        return;
    }
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (char *) malloc((size_t) len + 1);
    if (buf == NULL)
    {
        fclose(f);
        return;
    }
    len = (long) fread(buf, 1, (size_t) len, f);
    buf[len] = '\0';
    fclose(f);
    if (pblParsePlan(buf, &B.plan, err, sizeof(err)) < 0)
    {
        portConLogf("ge: playerbot: plan %s: %s - using 'auto'\n", path, err);
        pblParsePlan("auto\nexit\n", &B.plan, err, sizeof(err));
    }
    else
    {
        portConLogf("ge: playerbot: plan %s: %d steps\n", path, B.plan.n);
    }
    free(buf);
    B.planLoaded = 1;
}

static void stepBegin(void)
{
    B.stepTick = B.tick;
    B.phase = 0;
    B.count = 0;
    B.stepFails = 0;
    if (B.keepWant > 0)
    {
        B.keepWant--;   /* an `equip` holds the item for the steps right after it (press z, wait) */
    }
    else
    {
        B.wantWeapon = 0;
    }
    navReset();
}

static void stepNext(const char *why)
{
    if (B.verbose && B.step < B.plan.n)
    {
        portConLogf("ge: playerbot: step %d '%s' %s at t=%.1fs\n", B.step + 1, B.plan.s[B.step].text, why,
                    (f32) B.tick / 60.0f);
    }
    if (B.step < B.plan.n && B.plan.s[B.step].op == PBL_EQUIP && B.wantWeapon > 0)
    {
        B.keepWant = 2;
    }
    B.step++;
    stepBegin();
}

static s32 stepApplies(const PblStep *s)
{
    s32 d = lvlGetSelectedDifficulty();

    if (d < s->diffMin || d > s->diffMax)
    {
        return 0;
    }
    if (s->condObj >= 0 && !objectiveOpen(s->condObj))
    {
        return 0;
    }
    return 1;
}

/* object interaction: go near, face, act. Returns 1 when finished. */
static s32 doUse(f32 x, f32 y, f32 z, StandTile *tile, f32 radius, u16 button, s32 taps)
{
    if (B.phase == 0)
    {
        if (navTo(x, y, z, radius, tile))
        {
            B.phase = 1;
            B.count = 0;
            B.stepTick = B.tick;
        }
        return 0;
    }
    lookAt(x, y, z, 15.0f);
    if (B.tick - B.stepTick > 10 && ((B.tick - B.stepTick) % 20) == 0)
    {
        B.btn |= button;
        B.count++;
    }
    return B.count >= taps;
}

static s32 doShootObj(ObjectRecord *obj)
{
    struct player *p = bond();
    PropRecord *op = obj->prop;
    f32 dx = op->pos.x - p->prop->pos.x, dz = op->pos.z - p->prop->pos.z;
    f32 d = sqrtf(dx * dx + dz * dz);

    if (B.shootObj != obj)
    {
        B.shootObj = obj;
        B.shootSince = B.tick;
    }
    if (B.tick - B.shootSince > 420 && B.tick - B.shootSince < 600)
    {   /* shooting it for 7 s did nothing (something in the way, or out of reach): get closer for a while */
        navTo(op->pos.x, op->pos.y, op->pos.z, 200.0f, op->stan);
        return 0;
    }
    if (B.tick - B.shootSince >= 600)
    {
        B.shootSince = B.tick;
    }
    /* line of sight to a point just this side of it (a padlock sits on its door, a camera on its wall), from the
     * same floor (the tile line test is flat: from a roof it would "see" a padlock under it) */
    if (d < 1400.0f && d > 1.0f && op->pos.y - bondGroundY() > -100.0f && op->pos.y - bondGroundY() < 400.0f &&
        botLine(bondTile(), p->prop->pos.x, p->prop->pos.z, op->pos.x - dx / d * 40.0f, op->pos.z - dz / d * 40.0f, NULL,
                (obj == B.subLock ? 0 : CDTYPE_OBJS) | CDTYPE_DOORS | CDTYPE_PATHBLOCKER))   /* a padlock hangs on stuff */
    {
        /* aim a little above the prop's origin (most sit on the floor); keep some range so the shots land on it */
        /* padlocks: their own origin (a hatch's locks lie on the floor); floor objects: a little above it */
        shootAt(op->pos.x, op->pos.y + (obj == B.subLock ? 0.0f : 35.0f), op->pos.z, obj == B.subLock ? 6.0f : 15.0f);
        if (obj != B.subLock && d < (obj->type == PROPDEF_GAS_RELEASING ? 650.0f : 450.0f))
        {   /* many of these explode or gas the room (bottling tanks, barrels): shoot from range */
            B.btn &= ~BTN_Z;
            B.sy = -60;
        }
        return 1;
    }
    /* no line yet: walk toward it until the line is clear (checked again every tick) */
    navTo(op->pos.x, op->pos.y, op->pos.z, 100.0f, op->stan);
    return 0;
}

/* a live chr by number (byAi = 0) or by the AI list it runs or returns to (byAi = 1) */
static ChrRecord *findChr(s32 byAi, s32 id)
{
    s32 n = get_numguards(), i, k;
    AIRecord *list = NULL;

    if (byAi && g_CurrentSetup.ailists != NULL)
    {
        for (k = 0; g_CurrentSetup.ailists[k].ailist != NULL; k++)
        {
            if (g_CurrentSetup.ailists[k].ID == id)
            {
                list = g_CurrentSetup.ailists[k].ailist;
                break;
            }
        }
    }
    for (i = 0; i < n; i++)
    {
        ChrRecord *c = &g_ChrSlots[i];

        if (c->model == NULL || c->prop == NULL || chrIsDead(c))
        {
            continue;
        }
        if (byAi ? ((list != NULL && c->ailist == list) || c->aireturnlist == id) : c->chrnum == id)
        {
            return c;
        }
    }
    return NULL;
}

/* photograph an object: camera in hand, a clear view from up to ~900 away (a big screen high on a wall cannot be
 * walked to), then pictures. Returns the number of pictures taken so far (B.count). */
static s32 doPhoto(ObjectRecord *obj)
{
    struct player *p = bond();
    PropRecord *op = obj->prop;
    f32 dx = op->pos.x - p->prop->pos.x, dz = op->pos.z - p->prop->pos.z;
    f32 d = sqrtf(dx * dx + dz * dz);

    B.wantWeapon = ITEM_CAMERA;
    if (B.photoObj != obj)
    {
        B.photoObj = obj;
        B.photoSince = B.tick;
    }
    /* alternate: 4 s trying from here (in range: look at it, shoot when the game has it on screen), then 3 s walking
     * closer; the whole object must fit in the picture, so not from right under it */
    if (d < 900.0f && d > 250.0f && ((B.tick - B.photoSince) % 420) < 240)
    {
        f32 err = lookAt(op->pos.x, op->pos.y, op->pos.z, 10.0f);

        if (getCurrentPlayerWeaponId(GUNRIGHT) == ITEM_CAMERA && fabsf(err) < 3.0f && (op->flags & PROPFLAG_ONSCREEN) &&
            ((B.tick - B.stepTick) % 40) == 39)
        {
            B.btn |= BTN_Z;
            B.count++;
        }
        return B.count;
    }
    if (d <= 250.0f)
    {
        B.sy = -60;   /* back away to get it all in */
        lookAt(op->pos.x, op->pos.y, op->pos.z, 10.0f);
        return B.count;
    }
    navTo(op->pos.x, op->pos.y, op->pos.z, 400.0f, NULL);
    return B.count;
}

/* run one plan step; returns 1 when the step is done */
static s32 stepRun(PblStep *s)
{
    struct player *p = bond();
    ObjectRecord *obj;
    PadRecord *pad;

    switch (s->op)
    {
        case PBL_GOTO_PAD:
            pad = padGet(s->a);
            return pad == NULL || navTo(pad->pos.x, pad->pos.y, pad->pos.z, s->r, pad->stan);
        case PBL_GOTO_XYZ:
            return navTo(s->x, s->y, s->z, s->r, NULL);
        case PBL_USE_PAD:
            pad = padGet(s->a);
            if (pad == NULL)
            {
                return 1;
            }
            if (B.phase == 0)
            {
                if (navTo(pad->pos.x, pad->pos.y, pad->pos.z, s->r < 40.0f ? s->r : 40.0f, pad->stan))
                {
                    B.phase = 1;
                    B.stepTick = B.tick;
                }
                return 0;
            }
            lookToward(pblYawTo(pad->look.x, pad->look.z), 0.0f, 15.0f);
            if (B.tick - B.stepTick > 10 && ((B.tick - B.stepTick) % 20) == 0)
            {
                B.btn |= BTN_B;
                B.count++;
            }
            return B.count >= 3;
        case PBL_USE_OBJ:
            obj = objByTag(s->a);
            if (obj == NULL || obj->prop == NULL)
            {
                return 1;
            }
            return doUse(obj->prop->pos.x, obj->prop->pos.y, obj->prop->pos.z, obj->prop->stan, s->r > 100.0f ? s->r : 100.0f,
                         BTN_B, 3);
        case PBL_COLLECT_OBJ:
            obj = objByTag(s->a);
            if (obj == NULL || obj->prop == NULL || bondinvHasPropInInv(obj->prop))
            {
                return 1;
            }
            if (s->b == 1)
            {
                return navTo(obj->prop->pos.x, obj->prop->pos.y, obj->prop->pos.z, s->r, obj->prop->stan);
            }
            if (navTo(obj->prop->pos.x, obj->prop->pos.y, obj->prop->pos.z, 25.0f, obj->prop->stan))
            {
                /* on top of it and it did not get picked up: try B (some pickups are used, not walked over) */
                tap(BTN_B, 30);
            }
            return 0;
        case PBL_DESTROY_OBJ:
            obj = objByTag(s->a);
            if (obj == NULL || obj->prop == NULL || !objIsHealthy(obj))
            {
                return 1;
            }
            doShootObj(obj);
            return 0;
        case PBL_GADGET_OBJ:
        case PBL_THROW_OBJ:
            obj = objByTag(s->b);
            if (obj == NULL || obj->prop == NULL || !bondinvItemAvailable(s->a))
            {
                return 1;
            }
            B.wantWeapon = s->a;
            if (B.phase == 0)
            {
                if (navTo(obj->prop->pos.x, obj->prop->pos.y, obj->prop->pos.z, s->op == PBL_THROW_OBJ ? 300.0f : 110.0f,
                          obj->prop->stan))
                {
                    B.phase = 1;
                    B.stepTick = B.tick;
                }
                return 0;
            }
            lookAt(obj->prop->pos.x, obj->prop->pos.y, obj->prop->pos.z, 15.0f);
            if (getCurrentPlayerWeaponId(GUNRIGHT) == s->a && B.tick - B.stepTick > 30 && ((B.tick - B.stepTick) % 30) < 3)
            {
                B.btn |= BTN_Z;
                if (((B.tick - B.stepTick) % 30) == 2)
                {
                    B.count++;
                }
            }
            return B.count >= 4 || B.tick - B.stepTick > 600;
        case PBL_PHOTO_OBJ:
            obj = objByTag(s->a);
            if (obj == NULL || obj->prop == NULL)
            {
                return 1;
            }
            return doPhoto(obj) >= 6;
        case PBL_WAIT:
            return B.tick - B.stepTick >= s->a;
        case PBL_EQUIP:
            B.wantWeapon = s->a;
            if (getCurrentPlayerWeaponId(GUNRIGHT) == s->a)
            {
                return 1;   /* keep wanting it: the next steps use it (gadget / press z) */
            }
            if (B.tick - B.stepTick > 600)
            {
                portConLogf("ge: playerbot: equip %d failed (in inventory: %d, in hand: %d)\n", s->a, bondinvItemAvailable(s->a),
                            getCurrentPlayerWeaponId(GUNRIGHT));
                B.wantWeapon = 0;
                return 1;
            }
            return 0;
        case PBL_FLAG:
            if (stageFlag(s->a))
            {
                return 1;
            }
            if (s->b > 0 && B.tick - B.stepTick > s->b)
            {
                portConLogf("ge: playerbot: flag 0x%x not set after %d ticks\n", s->a, s->b);
                return 1;
            }
            return 0;
        case PBL_PRESS:
            if (B.tick - B.stepTick < s->b)
            {
                B.btn |= (u16) s->a;
                return 0;
            }
            return 1;
        case PBL_EXIT:
            tap(BTN_A, 30);
            return 0;
        case PBL_AUTO:
        {
            s32 op = 0, tag = -1;

            if (!autoNext(&op, &tag))
            {
                return 1;
            }
            if (B.count != tag * 16 + op)
            {   /* a new objective target */
                if (B.verbose)
                {
                    ObjectRecord *ao = objByTag(tag);

                    portConLogf("ge: playerbot: auto: %s obj %d at %.0f,%.0f,%.0f\n", pblOpName(op), tag,
                                ao != NULL && ao->prop != NULL ? ao->prop->pos.x : 0.0f,
                                ao != NULL && ao->prop != NULL ? ao->prop->pos.y : 0.0f,
                                ao != NULL && ao->prop != NULL ? ao->prop->pos.z : 0.0f);
                }
                B.count = tag * 16 + op;
                B.phase = 0;
                navReset();
            }
            obj = objByTag(tag);
            if (op == PBL_DESTROY_OBJ)
            {
                doShootObj(obj);
            }
            else if (op == PBL_COLLECT_OBJ)
            {
                navTo(obj->prop->pos.x, obj->prop->pos.y, obj->prop->pos.z, 20.0f, obj->prop->stan);
            }
            else
            {
                doPhoto(obj);
            }
            return 0;
        }
        case PBL_KILL:
            if (combatTick((f32) s->a))
            {
                B.stepTick = B.tick;
                return 0;
            }
            return B.tick - B.stepTick > 90;
        case PBL_FACE:
            return fabsf(lookAt(s->x, s->y, s->z, 10.0f)) < 2.0f;
        case PBL_LOOK:
            lookToward(p->vv_theta, (f32) s->a, 0.0f);
            return fabsf(p->vv_verta - (f32) s->a) < 1.5f;
        case PBL_WALK:
            B.sy = (s8) s->b;
            B.sx = (s8) s->c;
            return B.tick - B.stepTick >= s->a;
        case PBL_PEACE:
            B.peace = s->a;
            return 1;
        case PBL_HUNT:
        {
            s32 n = get_numguards(), i;
            ChrRecord *best = NULL;
            f32 bd = 1e30f;

            if (s->condObj >= 0 && !objectiveOpen(s->condObj))
            {
                return 1;
            }
            for (i = 0; i < n; i++)
            {
                ChrRecord *c = &g_ChrSlots[i];
                f32 dx, dy, dz, d;

                if (!chrHostile(c) || c->prop->stan == NULL)
                {
                    continue;
                }
                dx = c->prop->pos.x - p->prop->pos.x;
                dy = c->prop->pos.y - p->prop->pos.y;
                dz = c->prop->pos.z - p->prop->pos.z;
                d = dx * dx + dz * dz + dy * dy * 9.0f;
                if (d < bd)
                {
                    bd = d;
                    best = c;
                }
            }
            if (best == NULL)
            {
                return 1;
            }
            navTo(best->prop->pos.x, best->prop->pos.y, best->prop->pos.z, 250.0f, best->prop->stan);
            return 0;
        }
        case PBL_MEET:
        {
            ChrRecord *c = findChr(s->a, s->b);

            if (c == NULL)
            {
                if (B.tick - B.stepTick > 300)
                {
                    portConLogf("ge: playerbot: meet: no live chr %s %d\n", s->a ? "with AI list" : "number", s->b);
                    return 1;
                }
                return 0;
            }
            if (s->condObj >= 0 && !objectiveOpen(s->condObj))
            {
                return 1;
            }
            if (navTo(c->prop->pos.x, c->prop->pos.y, c->prop->pos.z, s->r, c->prop->stan))
            {
                lookAt(c->prop->pos.x, c->prop->pos.y + 20.0f, c->prop->pos.z, 10.0f);
                if (B.phase == 0)
                {
                    B.phase = 1;
                    B.count = B.tick;
                }
            }
            return B.phase == 1 && B.tick - B.count >= s->c;
        }
        default:
            return 1;
    }
}

/* ---- result ------------------------------------------------------------------------------------------------------- */

static s32 objectivesDone(s32 *needed)
{
    s32 i, done = 0;

    *needed = 0;
    for (i = 0; i < objectiveGetCount(); i++)
    {
        if (get_difficulty_for_objective(i) > lvlGetSelectedDifficulty())
        {
            continue;
        }
        (*needed)++;
        if (get_status_of_objective(i) == OBJECTIVESTATUS_COMPLETE)
        {
            done++;
        }
    }
    return done;
}

static void result(const char *outcome, s32 code)
{
    s32 needed = 0, done;
    struct player *p = bond();

    if (B.resultDone)
    {
        return;
    }
    B.resultDone = 1;
    done = objectivesDone(&needed);
    portConLogf("ge: playerbot: RESULT stage=%s diff=%d outcome=%s time=%.1fs mission_time=%ds ticks=%d health_lost=%.2f "
                "kills=%d objectives=%d/%d step=%d/%d room=%d pos=%.0f,%.0f,%.0f blocked=%s\n",
                stageName(B.stage), (s32) lvlGetSelectedDifficulty(), outcome, (f32) B.tick / 60.0f, getMissiontimer() / 60,
                B.tick, B.hpLost, B.kills, done, needed, B.step < B.plan.n ? B.step + 1 : B.plan.n, B.plan.n,
                p != NULL && p->prop != NULL ? bondRoom() : -1, p != NULL && p->prop != NULL ? p->prop->pos.x : 0.0f,
                p != NULL && p->prop != NULL ? p->prop->pos.y : 0.0f, p != NULL && p->prop != NULL ? p->prop->pos.z : 0.0f,
                B.blocked[0] ? B.blocked : "-");
    fflush(stdout);
    fflush(stderr);
    if (B.exitOnEnd)
    {
        portInputRecFlush();
        _exit(code);
    }
}

void portPlayerBotMissionEnd(s32 stage, s32 allcomplete)
{
    if (portPlayerBotMode() == 0 || !B.inStage)
    {
        return;
    }
    (void) stage;
    if (allcomplete)
    {
        result("COMPLETE", 0);
    }
    else if (B.diedTick > 0)
    {
        result("FAILED_DEAD", 1);
    }
    else
    {
        result("FAILED_OBJECTIVES", 1);
    }
}

/* ---- per-tick observe ------------------------------------------------------------------------------------------- */

static void stageBegin(void)
{
    s32 i;

    B.inStage = 1;
    B.stage = bossGetStageNum();
    B.setupKey = (void *) (uintptr_t) ptr_firstroom_0;
    B.tick = 0;
    B.ticksAll = 0;
    B.resultDone = 0;
    B.diedTick = 0;
    B.deadTicks = 0;
    B.outTicks = 0;
    B.hpPrev = -1.0f;
    B.hpLost = 0.0f;
    B.lastHurtTick = -1000;
    B.kills = 0;
    B.deadAtStart = -1;
    B.doorProp = NULL;
    B.nBlockedDoors = 0;
    B.subSwitch = NULL;
    B.subLock = NULL;
    B.lootProp = NULL;
    memset(B.lootTried, 0, sizeof(B.lootTried));
    B.noLoot = getenv("GE_PLAYERBOT_NOLOOT") != NULL;
    B.subKeyNeed = 0;
    B.keyGiveUpAt = -100000;
    B.subTries = 0;
    B.blocked[0] = '\0';
    B.step = 0;
    B.peace = 0;
    B.target = NULL;
    B.stuckEvents = 0;
    for (i = 0; i < 10; i++)
    {
        B.objStatus[i] = -1;
    }
    graphBuild();
    planLoad();
    stepBegin();
    portConLogf("ge: playerbot: stage %s (%d) difficulty %d: %d objectives\n", stageName(B.stage), B.stage,
                (s32) lvlGetSelectedDifficulty(), objectiveGetCount());
    for (i = 0; i < objectiveGetCount(); i++)
    {
        portConLogf("ge: playerbot:   objective %d (diff %d)%s\n", i, get_difficulty_for_objective(i),
                    get_difficulty_for_objective(i) <= lvlGetSelectedDifficulty() ? "" : " - not at this difficulty");
    }
}

static void observe(void)
{
    struct player *p = bond();
    s32 i;
    f32 hp;

    hp = p->bondhealth + p->bondarmour;
    if (B.hpPrev >= 0.0f && hp < B.hpPrev)
    {
        B.hpLost += B.hpPrev - hp;
        B.lastHurtTick = B.tick;
    }
    B.hpPrev = hp;
    if (p->bonddead && B.diedTick == 0)
    {
        B.diedTick = B.tick > 0 ? B.tick : 1;
        snprintf(B.blocked, sizeof(B.blocked), "died at room %d pos %.0f,%.0f,%.0f step %d '%s'", bondRoom(), p->prop->pos.x,
                 p->prop->pos.y, p->prop->pos.z, B.step + 1, B.step < B.plan.n ? B.plan.s[B.step].text : "-");
        portConLogf("ge: playerbot: Bond died: %s\n", B.blocked);
        ge_shot_force = 1;
    }
    if (p->bonddead && ++B.deadTicks > 480)
    {
        result("FAILED_DEAD", 1);   /* the death camera has had its time; no need to sit through the menus */
    }
    for (i = 0; i < objectiveGetCount() && i < 10; i++)
    {
        s32 st = get_status_of_objective(i);

        if (st != B.objStatus[i])
        {
            if (B.objStatus[i] >= 0)
            {
                portConLogf("ge: playerbot: objective %d -> %s at t=%.1fs\n", i,
                            st == OBJECTIVESTATUS_COMPLETE ? "complete" : (st == OBJECTIVESTATUS_FAILED ? "FAILED" : "incomplete"),
                            (f32) B.tick / 60.0f);
                ge_shot_force = 1;
            }
            B.objStatus[i] = st;
        }
    }
    if ((B.ticksAll % 30) == 1)
    {   /* guards down since the start (any cause; the bot does nearly all of it) */
        s32 n = get_numguards(), dead = 0;

        for (i = 0; i < n; i++)
        {
            ChrRecord *c = &g_ChrSlots[i];

            if (c->model != NULL && c->prop != NULL && chrIsDead(c))
            {
                dead++;
            }
        }
        if (B.deadAtStart < 0)
        {
            B.deadAtStart = dead;
        }
        if (dead - B.deadAtStart > B.kills)
        {
            B.kills = dead - B.deadAtStart;
        }
    }
    if (B.maxSecs > 0 && (B.tick > B.maxSecs * 60 || B.ticksAll > (B.maxSecs + 300) * 60))
    {
        if (B.blocked[0] == '\0')
        {
            stuckReport("timeout");
        }
        result("TIMEOUT", 2);
    }
    if (B.verbose && (B.tick % 300) == 0 && p->prop != NULL)
    {
        portConLogf("ge: playerbot: t=%.0fs room %d pos %.0f,%.0f,%.0f theta %.0f verta %.1f hp %.2f+%.2f step %d/%d '%s' "
                    "wp %d/%d gun %d%s | out sy %d sx %d btn %04x yaw %.1f\n",
                    (f32) B.tick / 60.0f, bondRoom(), p->prop->pos.x, p->prop->pos.y, p->prop->pos.z, p->vv_theta, p->vv_verta,
                    p->bondhealth, p->bondarmour, B.step + 1, B.plan.n, B.step < B.plan.n ? B.plan.s[B.step].text : "-",
                    B.pathAt, B.pathN, getCurrentPlayerWeaponId(GUNRIGHT), B.target ? " FIGHT" : "", B.lastSy, B.lastSx,
                    B.lastBtn, B.lastYaw);
    }
    if (B.verbose && (B.tick % 1800) == 900)
    {
        ge_shot_force = 1;   /* a screenshot every 30 s on the next rendered frame (GE_SIMONLY_TEST windows) */
    }
}

/* ---- per-tick drive ------------------------------------------------------------------------------------------- */

static void drive(void)
{
    struct player *p = bond();
    PblStep *s;
    s32 busy = 0;

    outClear();
    if (p->bonddead)
    {
        tap(BTN_A, 40);   /* through the death camera to the end */
        return;
    }
    if (lvlGetControlsLockedFlag() != 0 || p->cameramode != 0)
    {
        tap(BTN_A, 30);   /* skip the intro / press through the exit camera */
        return;
    }
    weaponTick();

    /* a mode planner feeds steps one at a time */
    if (sPlanner != NULL && B.step >= B.plan.n)
    {
        PblStep st;
        s32 r = sPlanner(&st, B.tick);

        if (r > 0 && B.plan.n < PBL_MAXSTEPS)
        {
            B.plan.s[B.plan.n++] = st;
        }
    }
    while (B.step < B.plan.n && !stepApplies(&B.plan.s[B.step]))
    {
        stepNext("skipped");
    }
    s = (B.step < B.plan.n) ? &B.plan.s[B.step] : NULL;
    if (s == NULL || (s->op != PBL_PRESS && s->op != PBL_WALK && s->op != PBL_EXIT && s->op != PBL_KILL &&
                      s->op != PBL_PHOTO_OBJ && s->op != PBL_GADGET_OBJ && s->op != PBL_THROW_OBJ))
    {
        busy = combatTick(1600.0f);
    }
    if (busy)
    {
        B.stuck.lastGain = B.tick;   /* fighting is not being stuck */
        return;
    }
    if (s == NULL)
    {
        tap(BTN_A, 60);
        return;
    }
    if (B.subKeyNeed != 0)
    {   /* detour: fetch a key (kill its carrier, then walk over it) */
        ChrRecord *holder = NULL;
        PropRecord *kp = findKeyProp(B.subKeyNeed, &holder);

        if (bondinvCheckHasKeyFlags(B.subKeyNeed) || kp == NULL || B.tick - B.subSince > 7200)
        {
            B.stepFails = 0;
            portConLogf("ge: playerbot: key detour done (%s)\n", bondinvCheckHasKeyFlags(B.subKeyNeed) ? "got it" : "gave up");
            if (!bondinvCheckHasKeyFlags(B.subKeyNeed))
            {
                B.keyGiveUpAt = B.tick;
            }
            B.subKeyNeed = 0;
            navReset();
            return;
        }
        if (holder != NULL)
        {
            if (chrIsDead(holder))
            {
                return;   /* it drops when the body settles */
            }
            if (!chrHostile(holder) && holder->prop != NULL)
            {   /* an unarmed carrier: walk up and shoot it anyway (the key is the mission) */
                if (navTo(holder->prop->pos.x, holder->prop->pos.y, holder->prop->pos.z, 250.0f, holder->prop->stan))
                {
                    shootAt(holder->prop->pos.x, holder->prop->pos.y + 20.0f, holder->prop->pos.z, 25.0f);
                }
                return;
            }
            if (B.stuck.level >= 1 && B.goalValid && ((B.tick - B.subSince) % 900) > 120)
            {   /* cannot reach the carrier (often: he is behind the very door his key opens) - wait here for him, the
                 * combat code shoots him when he shows; try walking again every 15 s */
                lookAt(holder->prop->pos.x, bond()->prop->pos.y, holder->prop->pos.z, 10.0f);
                return;
            }
            navTo(holder->prop->pos.x, holder->prop->pos.y, holder->prop->pos.z, 200.0f, holder->prop->stan);
            return;
        }
        if (navTo(kp->pos.x, kp->pos.y, kp->pos.z, 10.0f, kp->stan))
        {
            tap(BTN_B, 30);
        }
        return;
    }
    if (B.subLock != NULL)
    {   /* detour: shoot a padlock off */
        if (B.subLock->prop == NULL || !objIsHealthy(B.subLock) || B.tick - B.subSince > 1800)
        {
            portConLogf("ge: playerbot: padlock detour done (%s)\n", B.subLock->prop == NULL || !objIsHealthy(B.subLock) ? "lock destroyed" : "gave up");
            B.subLock = NULL;
            navReset();
            return;
        }
        {
            s32 shooting = doShootObj(B.subLock);

            if (B.verbose >= 2 && ((B.tick - B.subSince) % 120) == 0)
            {
                PropRecord *lp = B.subLock->prop;
                f32 ddx = lp->pos.x - p->prop->pos.x, ddz = lp->pos.z - p->prop->pos.z;

                portConLogf("ge: playerbot: padlock: %.0f away, %s, ground %.0f lock y %.0f, verta %.1f theta %.0f btn %04x\n",
                            sqrtf(ddx * ddx + ddz * ddz), shooting ? "shooting" : "walking", bondGroundY(), lp->pos.y,
                            p->vv_verta, p->vv_theta, B.btn);
            }
        }
        return;
    }
    if (B.subSwitch != NULL)
    {   /* detour: work the switch of a door on the route */
        PropRecord *sw = B.subSwitch;
        DoorRecord *dr = B.subDoor->door;

        if (dr == NULL || !doorShut(B.subDoor) || dr->openstate == DOORSTATE_OPENING || B.tick - B.subSince > 2400 ||
            B.subPhase > 12 || (B.subPhase == 0 && B.stuck.level >= 3))
        {
            portConLogf("ge: playerbot: switch detour done (door %s)\n", (dr != NULL && !doorShut(B.subDoor)) ||
                        (dr != NULL && dr->openstate == DOORSTATE_OPENING) ? "opening" : "still shut");
            B.subSwitch = NULL;
            B.subDoor = NULL;
            navReset();
            return;
        }
        if (dr->openstate == DOORSTATE_WAITING)
        {   /* a paired gate: it opens once its sibling has closed */
            B.subSince = B.tick;
            return;
        }
        if (B.subPhase == 0)
        {   /* a wall switch's own tile can be on the far side of the wall: aim for a spot 100 short of it, on our side */
            f32 ddx = bond()->prop->pos.x - sw->pos.x, ddz = bond()->prop->pos.z - sw->pos.z;
            f32 dd = sqrtf(ddx * ddx + ddz * ddz);
            f32 gx = dd > 1.0f ? sw->pos.x + ddx / dd * 100.0f : sw->pos.x;
            f32 gz = dd > 1.0f ? sw->pos.z + ddz / dd * 100.0f : sw->pos.z;

            if (B.subGoalSet == 0)
            {
                B.subGoalX = gx;
                B.subGoalZ = gz;
                B.subGoalSet = 1;   /* fixed once chosen, so the route does not wobble */
            }
            if (navTo(B.subGoalX, bond()->prop->pos.y, B.subGoalZ, 40.0f, NULL) ||
                (dd < 160.0f && B.stuck.level >= 1))
            {
                B.subPhase = 1;
                B.stepTick = B.tick;
            }
            return;
        }
        {   /* the use test (objTestForInteract) measures to obj->runtime_pos: within 200 and 22.5 degrees */
            ObjectRecord *so = sw->type == PROP_TYPE_OBJ ? sw->obj : NULL;
            f32 tx = so != NULL ? so->runtime_pos.x : sw->pos.x, tz = so != NULL ? so->runtime_pos.z : sw->pos.z;
            f32 ey = so != NULL ? so->runtime_pos.y : sw->pos.y;
            f32 err = lookAt(tx, ey, tz, 15.0f);
            f32 ddx = tx - bond()->prop->pos.x, ddz = tz - bond()->prop->pos.z;
            f32 dd = sqrtf(ddx * ddx + ddz * ddz);

            if (dd > 170.0f)
            {
                steerTo(tx, ey, tz, 300.0f);   /* closer */
            }
            if (((B.tick - B.stepTick) % 40) == 39 && fabsf(err) < 10.0f)
            {
                B.btn |= BTN_B;
                B.subPhase++;
                if (B.verbose >= 2)
                {
                    portConLogf("ge: playerbot: switch press %d: %.0f away, %.1f deg off, onscreen %d, door state %d\n",
                                B.subPhase, dd, err, (sw->flags & PROPFLAG_ONSCREEN) != 0, (s32) dr->openstate);
                }
            }
        }
        return;
    }
    if (s->op != PBL_PRESS && s->op != PBL_WALK && s->op != PBL_EXIT && s->op != PBL_MEET && s->op != PBL_FACE &&
        s->op != PBL_LOOK && lootTick())
    {
        return;
    }
    if (B.stepFails >= 3)
    {
        stepNext("GAVE UP");
        return;
    }
    if (stepRun(s))
    {
        stepNext("done");
    }
}

static void driveTrace(void)
{
    struct player *p = bond();

    if (B.verbose >= 3)
    {
        portConLogf("ge: playerbot: trace tick %d all %d step %d phase %d path %d/%d aim %d off %d stuck %d -> sy %d sx %d btn %04x yaw %.2f pitch %.2f (want %.1f, verta %.1f)\n",
                    B.tick, B.ticksAll, B.step + 1, B.phase, B.pathAt, B.pathN, B.aimAt, B.offPath, B.stuck.level, B.sy, B.sx,
                    B.btn, B.yaw, B.pitch, B.pitchWant, p->vv_verta);
    }
}

/* the bot's work for one sim tick: observe, and (drive mode) decide the input */
static void botTick(void)
{
    struct player *p = bond();

    if (gamemode != 0 /* solo only */ || p == NULL || p->prop == NULL || bossGetStageNum() >= LEVELID_TITLE ||
        p->prop->stan == NULL || ptr_firstroom_0 == 0 || !(p->vv_theta >= -720.0f && p->vv_theta <= 720.0f))
    {   /* not in a stage, or Bond not spawned yet */
        if (B.inStage && ++B.outTicks > 600)
        {   /* the stage was left without the mission-end hook (death screen, abort) */
            result(B.diedTick > 0 ? "FAILED_DEAD" : "ABORTED", 1);
            B.inStage = 0;
        }
        outClear();
        return;
    }
    B.outTicks = 0;
    if (!B.inStage || B.setupKey != (void *) (uintptr_t) ptr_firstroom_0 || B.stage != bossGetStageNum())
    {
        stageBegin();
    }
    B.ticksAll++;
    if (lvlGetControlsLockedFlag() == 0 && p->cameramode == 0)
    {
        B.tick++;
    }
    B.lastSy = B.sy;
    B.lastSx = B.sx;
    B.lastBtn = B.btn;
    B.lastYaw = B.yaw;
    observe();
    if (B.mode == 1)
    {
        drive();
        driveTrace();
    }
}

/* ---- inputrec.c hooks --------------------------------------------------------------------------------------------- */

void portPlayerBotPad(OSContPad *pad)
{
    u32 t;
    s32 x, y, ax, ay;

    if (portPlayerBotMode() == 0)
    {
        return;
    }
    t = portInputRecTickNow();
    if (t != B.lastRecTick)
    {
        B.lastRecTick = t;
        botTick();
        B.lookFresh = 1;
    }
    if (B.mode != 1)
    {
        return;
    }
    x = B.sx;
    y = B.sy;
    ax = x < 0 ? -x : x;
    ay = y < 0 ? -y : y;
    if (ax + ay > 130)
    {
        x = x * 130 / (ax + ay);
        y = y * 130 / (ax + ay);
    }
    pad->button = B.btn;
    pad->stick_x = (s8) x;
    pad->stick_y = (s8) y;
}

void portPlayerBotLook(f32 *dx, f32 *dy, s32 *direct, f32 *yawdeg, f32 *pitchdeg)
{
    if (portPlayerBotMode() != 1)
    {
        return;
    }
    *dx = 0.0f;
    *dy = 0.0f;
    *direct = 1;
    if (B.lookFresh)
    {
        f32 pitch = B.pitch;

        /* bondview2 negates the pitch after this hook when the look is not inverted; pre-compensate */
        if (get_cur_player_look_vertical_inverted() != 0)
        {
            pitch = -pitch;
        }
        *yawdeg = B.yaw;
        *pitchdeg = pitch;
        B.lookFresh = 0;   /* a look delta applies once */
    }
    else
    {
        *yawdeg = 0.0f;
        *pitchdeg = 0.0f;
    }
}

void portPlayerBotDigMove(s32 *dx, s32 *dy)
{
    if (portPlayerBotMode() == 1)
    {
        *dx = 0;
        *dy = 0;
    }
}

void portPlayerBotActions(u8 *down, s32 count)
{
    s32 i;

    if (portPlayerBotMode() == 1)
    {
        for (i = 0; i < count; i++)
        {
            down[i] = 0;
        }
        if (B.actNextInv && count > PORTACT_NEXT_INV_INDEX)
        {
            down[PORTACT_NEXT_INV_INDEX] = 1;
        }
    }
}

/* ---- console ------------------------------------------------------------------------------------------------------ */

void portPlayerBotConsole(const char *arg, const char *arg2)
{
    if (!sInit)
    {
        botInit();
    }
    if (arg == NULL || arg[0] == '\0' || strcmp(arg, "info") == 0)
    {
        portConLogf("playerbot: mode %d, stage %s, step %d/%d '%s', tick %d, target %s\n", B.mode, stageName(B.stage), B.step + 1,
                    B.plan.n, B.step < B.plan.n ? B.plan.s[B.step].text : "-", B.tick, B.target ? "yes" : "no");
    }
    else if (strcmp(arg, "on") == 0)
    {
        B.mode = 1;
        B.exitOnEnd = 0;
        B.inStage = 0;
    }
    else if (strcmp(arg, "off") == 0)
    {
        B.mode = 0;
    }
    else if (strcmp(arg, "watch") == 0)
    {
        B.mode = 2;
    }
    else if (strcmp(arg, "plan") == 0 && arg2 != NULL)
    {
        snprintf(B.planfile, sizeof(B.planfile), "%s", arg2);
        B.inStage = 0;
    }
    else if (strcmp(arg, "step") == 0)
    {
        stepNext("skipped (console)");
    }
}
