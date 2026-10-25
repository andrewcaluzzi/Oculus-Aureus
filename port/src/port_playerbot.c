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
#include <bondaicommands.h>
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
extern int  portModeActive(void);                          /* port_mode.c */
extern const char *portModeName(void);
extern bool bondinvCheckHasKeyFlags(u32 wantkeyflags);   /* bondinv.c */
extern s32  get_difficulty_for_objective(s32 objectiveIndex);
extern bool alarmIsActive(void);                           /* propobj.c */
extern int  portHeistKoIs(int chrnum);                     /* port_heist.c: knocked out (modes only) */
extern int  portHeistCivilianState(int chrnum);            /* -1 not a heist civilian, 0 idle, 1 hands up, 2 cowering, 3 KO */
#include "game/model.h"                                     /* getsubroty: a chr's facing (radians) */

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
    u32 flagsPrev;             /* objectiveregisters1 last tick: changes are logged (which AI flag fired, for plans) */
    /* speed-strafing (`speed 1` / GE_PLAYERBOT_SPEED=1): forward + a C strafe held together, the view turned off the travel
     * direction by ssOff (learnt from Bond's real motion per strafe side: [0] C-left, [1] C-right), the side kept per
     * route segment (ssSide: -1 C-left, +1 C-right) */
    s32 speedMode, ssSide, ssHeld, ssLearnN;
    f32 ssDist, wDist;         /* distance covered on straight stretches: speed-strafing / plain running (ticks below) */
    s32 ssTicks, wTicks;
    f32 ssOff[2], ssPrevX, ssPrevZ, ssTurnHint;
    void *spare[6];            /* `spare ai ID`: AI lists whose chrs are never shot (a hostage's captors) */
    s32 nSpare;
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
    s32 doorTaps, doorSince, doorOpened, doorOpenTick;
    f32 doorOpenPos;
    PropRecord *blockedDoors[16];
    s32 blockedAt[16];
    s32 nBlockedDoors;
    ObjectRecord *subLock;     /* a detour: shoot this padlock off a door on the route */
    ChrRecord *huntSkip[8];    /* guards the hunt step could not reach, and when */
    s32 huntSkipAt[8], huntSkipN;
    ObjectRecord *photoObj;    /* the object being photographed, and since when */
    s32 photoSince;
    ObjectRecord *photoSpotObj;   /* ...and the tile chosen to photograph it from (-1 none) */
    s32 photoSpotNode;
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
    s32 holdFire;              /* ticks the last shot was held back (a civilian in the line of fire) */
    s32 fireTicks;             /* ticks with the trigger down at a target (a measure of ammo per kill) */
    s32 lastFireTick;
    s32 shotBase[4];           /* the game's shot registers (total, head, body, limb) when the stage began */
    PropRecord *tgtSkip[4];    /* targets the bot could not shoot for a long time: left alone for a while */
    s32 tgtSkipAt[4], tgtSkipN;
    s32 nThreats;              /* armed hostiles with a line on Bond (last scan) */
    PropRecord *threats[6];
    AIRecord *raiserList;      /* the stock alarm raiser list: those guards are shot first */
    s32 coverNode, coverUntil, coverSince, lastCoverScan, noCover, oldCover;
    s32 kiteSince, kiteBlockedUntil, noKite, glassTest, spareAll;
    s32 avoidRoom[8], nAvoid;  /* rooms the routes keep out of (the `avoid` step) */
    ChrRecord *civExempt;      /* a civilian carrier being shot on purpose (not a bystander for the line-of-fire check) */
    s32 rush, moveOnly;        /* rush: keep walking the route while shooting (the `rush` step); moveOnly: steerTo
                                * moves without turning (the look is on a target) */
    PropRecord *armourProp;
    s32 armourSince, armourGiveUpUntil;
    s32 crIsR1, strafeCal, calTick, calLast;   /* strafe direction, learnt from Bond's own motion (strafeCalibrate) */
    u16 calBtn;
    f32 calX, calZ, calTheta;
    f32 kiteX, kiteZ;
    f32 aimY;                  /* aim this far above a chr's prop (its origin is ~110 above the floor; +52 is the head:
                                * the heist A/B gave 2-3x the head hits of +15..38 for the same shots) */
    f32 lastTX, lastTY, lastTZ;
    s32 quiet;                 /* the `quiet` step: stealth until the alarm sounds */
    /* the key detour's carrier handling: knock-out from behind, search, rob */
    ChrRecord *koChr;
    s32 koPhase, koSince, koSlapTick, searchTick;
    s32 robSince;

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
    f32 pitchWant, pitchErr;
    s32 deadTicks;            /* ticks since Bond died */
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

/* on a `spare ai ID` list (a hostage's captors: shooting one has the hostage killed) */
static s32 chrSpared(ChrRecord *c)
{
    s32 k;

    for (k = 0; k < B.nSpare; k++)
    {
        if ((void *) c->ailist == B.spare[k])
        {
            return 1;
        }
    }
    return 0;
}

static s32 chrHostile(ChrRecord *c)
{
    if (c == NULL || c->model == NULL || c->prop == NULL || chrIsDead(c) || chrSpared(c))
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
    if (portHeistKoIs(c->chrnum))
    {
        return 0;   /* knocked out (heist modes): no threat, and shooting it is noise for nothing */
    }
    return chrGetEquippedWeaponPropWithCheck(c, GUNRIGHT) != NULL || chrGetEquippedWeaponPropWithCheck(c, GUNLEFT) != NULL;
}

/* a living bystander whose death counts against the mission (stock civilians, heist staff) */
static s32 chrIsCivilian(ChrRecord *c)
{
    return c != NULL && c->model != NULL && c->prop != NULL && !chrIsDead(c) && (c->chrflags & CHRFLAG_COUNT_DEATH_AS_CIVILIAN);
}

static s32 chrAttacking(ChrRecord *c)
{
    return c->actiontype == ACT_ATTACK || c->actiontype == ACT_ATTACKWALK || c->actiontype == ACT_ATTACKROLL;
}

/* a guard on the stock alarm raiser list (heist raisers), or one sounding an alarm now */
static s32 chrIsRaiser(ChrRecord *c)
{
    return c->actiontype == ACT_STARTALARM || (B.raiserList != NULL && c->ailist == B.raiserList);
}

/* has this chr noticed Bond? (standing, patrolling or idling = not yet) */
static s32 chrAlert(ChrRecord *c)
{
    return !(c->actiontype == ACT_STAND || c->actiontype == ACT_PATROL || c->actiontype == ACT_ANIM ||
             c->actiontype == ACT_INIT);
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
    B.pitch = pblClamp(perr * 0.8f, -8.0f, 8.0f) / r;
    B.pitchWant = pitchWant;
    B.pitchErr = perr;
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

    if (B.quiet)
    {   /* stealth: a silenced gun is heard ~100 units away, a KF7 2000. Even once the alarm sounds: the alarm brings the
         * reinforcement waves, but the guards on post elsewhere only come for what they hear or see */
        if (bondinvItemAvailable(ITEM_MP5KSIL) && weaponHasAmmo(ITEM_MP5KSIL))
        {
            return ITEM_MP5KSIL;
        }
        if (bondinvItemAvailable(ITEM_WPPKSIL) && weaponHasAmmo(ITEM_WPPKSIL))
        {
            return ITEM_WPPKSIL;
        }
    }
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
    s32 want = (B.wantWeapon > 0 && (B.target == NULL || B.peace)) ? B.wantWeapon : bestGun();

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
    if (B.nAvoid > 0)
    {   /* rooms a plan said to keep out of */
        for (a = 0; a < B.g.n; a++)
        {
            s32 k;

            for (k = 0; k < B.nAvoid; k++)
            {
                if ((s32) B.tiles[a]->room == B.avoidRoom[k])
                {
                    B.g.nbad[a] = 1;   /* closed: a route never enters it (Statue's exit room ends the mission) */
                }
            }
        }
    }
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

static void koConePenalty(void);

static void navPlan(f32 x, f32 y, f32 z, StandTile *goalTile)
{
    struct player *p = bond();
    s32 a, b;

    graphApplyLocks();
    koConePenalty();
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
    if ((d->door->openstate == DOORSTATE_OPENING || d->door->openstate == DOORSTATE_WAITING) && B.tick - B.doorSince <= 1200)
    {   /* it is coming: wait facing it - unless it has not moved for 2 s (Bond, or a body, in its swing): step back.
         * 20 s of this and the door counts as one that will not open (below) */
        if (d->door->openPosition > B.doorOpenPos + 0.01f || B.doorOpenTick == 0)
        {
            B.doorOpenPos = d->door->openPosition;
            B.doorOpenTick = B.tick;
        }
        B.doorOpened = 1;
        lookAt(d->pos.x, p->prop->pos.y, d->pos.z, 15.0f);
        B.stuck.lastGain = B.tick;
        if (B.tick - B.doorOpenTick > 120)
        {
            B.sy = -60;
            if (B.tick - B.doorOpenTick > 150)
            {
                B.doorOpenTick = B.tick;   /* and wait again */
                B.btn |= BTN_B;
            }
        }
        return 1;
    }
    B.doorOpenTick = 0;
    B.doorOpenPos = 0.0f;
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
    f32 err, ae;
    s32 sp = 80;

    B.lastSteerYaw = pblYawTo(dx, dz);
    if (B.moveOnly)
    {   /* moving while the look is on a target: walk the heading with the stick (forward/back) and the C strafe
         * buttons, view-relative. forward(theta + rel) ~ forward(theta) - rel * r1, so a heading to the left of the
         * view (rel > 0) is a step against r1 = (cos theta, sin theta) */
        f32 rel = pblWrap180(B.lastSteerYaw - p->vv_theta) * 3.14159265f / 180.0f;
        f32 fw = cosf(rel), lat = sinf(rel);

        B.sy = (s8) pblClamp(fw * 90.0f, -80.0f, 80.0f);
        if (fabsf(lat) > 0.38f)
        {
            B.btn |= ((lat > 0.0f ? -1 : 1) * B.crIsR1 > 0) ? BTN_CR : BTN_CL;
        }
        (void) y;
        (void) slowRadius;
        return;
    }
    if (B.speedMode && slowRadius <= 0.0f && B.doorProp == NULL && dist > 120.0f)
    {   /* speed-strafe: bondview2.c scales the walk animation's forward root motion by speedforwards / maxspeed (maxspeed =
         * the largest of forward, sideways x MAX_SPEED_FACTOR, turn) and then ADDS the strafe (speedsideways / delta) to it
         * (bondhead.c bheadUpdate), not normalised: forward + strafe is faster than forward. Turn the view off the travel
         * direction by the learnt offset and hold both. */
        s32 si = B.ssSide > 0 ? 1 : 0;
        f32 mx = p->prop->pos.x - B.ssPrevX, mz = p->prop->pos.z - B.ssPrevZ;

        B.ssSide = pblStrafeSide(B.ssSide, B.ssTurnHint, B.ssOff[1]);
        if ((B.ssSide > 0 ? 1 : 0) != si)
        {
            si = B.ssSide > 0 ? 1 : 0;
            B.ssHeld = 0;
        }
        err = lookToward(B.lastSteerYaw - B.ssOff[si], 0.0f, 20.0f);
        if (fabsf(err) < 30.0f)
        {
            B.sy = 80;
            B.btn |= B.ssSide > 0 ? BTN_CR : BTN_CL;
            B.ssHeld++;
            if (B.ssHeld > 20 && fabsf(err) < 4.0f)
            {
                B.ssDist += sqrtf(mx * mx + mz * mz);
                B.ssTicks++;
            }
            if (B.ssHeld > 20 && fabsf(err) < 4.0f && mx * mx + mz * mz > 4.0f)
            {   /* learn the offset from the motion: unfiltered for the first samples (the strafe's sign is not assumed),
                 * then only while the motion runs along the route (sliding on a wall says nothing) */
                f32 o = pblWrap180(pblYawTo(mx, mz) - p->vv_theta);

                if (fabsf(o) < 85.0f &&
                    (B.ssLearnN < 40 || fabsf(pblWrap180(pblYawTo(mx, mz) - B.lastSteerYaw)) < 20.0f))
                {
                    B.ssOff[si] = pblAngleEma(B.ssOff[si], o, B.ssLearnN < 40 ? 0.15f : 0.03f);
                    if (++B.ssLearnN == 200 && B.verbose)
                    {
                        portConLogf("ge: playerbot: speed-strafe offsets learnt: C-left %.1f, C-right %.1f deg\n", B.ssOff[0],
                                    B.ssOff[1]);
                    }
                }
            }
        }
        else
        {
            B.sy = fabsf(err) > 70.0f ? 0 : 30;
            B.ssHeld = 0;
        }
        B.ssPrevX = p->prop->pos.x;
        B.ssPrevZ = p->prop->pos.z;
        (void) y;
        return;
    }
    err = lookToward(B.lastSteerYaw, 0.0f, 20.0f);
    ae = fabsf(err);
    if (ae < 4.0f && B.ssHeld > 20 && slowRadius <= 0.0f && !B.moveOnly)
    {   /* (the plain-running speed, for comparison: B.ssHeld counts straight ticks here too) */
        f32 mx = p->prop->pos.x - B.ssPrevX, mz = p->prop->pos.z - B.ssPrevZ;

        B.wDist += sqrtf(mx * mx + mz * mz);
        B.wTicks++;
    }
    B.ssHeld = ae < 4.0f ? B.ssHeld + 1 : 0;
    B.ssPrevX = p->prop->pos.x;
    B.ssPrevZ = p->prop->pos.z;

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
                /* the route's turn at the corner ahead (speed-strafe picks its side from it) */
                B.ssTurnHint = c + 1 < nc ? pblWrap180(pblYawTo(ox[c + 1] - ox[c], oz[c + 1] - oz[c]) -
                                                       pblYawTo(ox[c] - p->prop->pos.x, oz[c] - p->prop->pos.z))
                                          : 0.0f;
                steerTo(ox[c], yy, oz[c], (c == nc - 1 && k >= B.pathN) ? radius + 60.0f : 0.0f);
                doorTick();
                return 0;
            }
        }
    }
    if (B.pathN == 0 && B.nAvoid > 0)
    {
        return 0;   /* no route: walking straight could take Bond into a room the plan keeps out of - stay put */
    }
    steerTo(x, y, z, radius + 60.0f);
    doorTick();
    return 0;
}

/* ---- combat ------------------------------------------------------------------------------------------------------- */

/* Quiet mode: leave alone an unalerted guard who faces away (he cannot see Bond; a body would alert his room) - unless
 * he is the one in the way of a knock-out, or he is close enough to turn round into us. */
static s32 quietSpare(ChrRecord *c, f32 dist)
{
    if (c == B.koChr && B.koPhase > 0 && !chrAlert(c))
    {
        return 1;   /* the knock-out target, still unaware */
    }
    if (!B.quiet || alarmIsActive() || chrAlert(c))
    {
        return 0;
    }
    if (B.spareAll)
    {
        return 1;   /* experiment: shoot only guards that have noticed Bond */
    }
    return dist > 250.0f && pblOffFacing(getsubroty(c->model), c->prop->pos.x, c->prop->pos.z, bond()->prop->pos.x,
                                         bond()->prop->pos.z) > 120.0f;
}

/* a civilian near the line of fire from Bond toward (x, z), `dist` away (the shot would go along yaw). frontOnly: only
 * one between Bond and the target counts (for an alarm raiser at his panel - the alarm costs more than the risk of a
 * miss carrying on past his head) */
static s32 civilianInLineYawEx(f32 yaw, f32 dist, s32 frontOnly)
{
    struct player *p = bond();
    s32 n = get_numguards(), i;

    for (i = 0; i < n; i++)
    {
        ChrRecord *c = &g_ChrSlots[i];

        if (!chrIsCivilian(c) || c == B.civExempt || fabsf(c->prop->pos.y - p->prop->pos.y) > 300.0f)
        {
            continue;
        }
        if (pblInLineOfFire(p->prop->pos.x, p->prop->pos.z, yaw, frontOnly ? dist - 40.0f - 500.0f : dist, c->prop->pos.x,
                            c->prop->pos.z))
        {
            return 1;
        }
    }
    return 0;
}

static s32 civilianInLineYaw(f32 yaw, f32 dist)
{
    return civilianInLineYawEx(yaw, dist, 0);
}

/* Which way should Bond step so no civilian stands in a guard's line of fire at him (between them, or just behind Bond
 * where the misses go)? +1 along r1 = (cos theta, sin theta), -1 the other way, 0 = nobody exposed. */
static s32 civilianExposureSide(void)
{
    struct player *p = bond();
    s32 n = get_numguards(), i, k;
    f32 mx = 0.0f, mz = 0.0f, th;

    for (k = 0; k < B.nThreats; k++)
    {
        PropRecord *t = B.threats[k];
        f32 ux = p->prop->pos.x - t->pos.x, uz = p->prop->pos.z - t->pos.z;
        f32 len = sqrtf(ux * ux + uz * uz);

        if (len < 1.0f || t->chr == NULL || chrIsDead(t->chr))
        {
            continue;
        }
        ux /= len;
        uz /= len;
        for (i = 0; i < n; i++)
        {
            ChrRecord *c = &g_ChrSlots[i];
            f32 cx, cz, proj, lat;

            if (!chrIsCivilian(c) || fabsf(c->prop->pos.y - p->prop->pos.y) > 300.0f)
            {
                continue;
            }
            cx = c->prop->pos.x - t->pos.x;
            cz = c->prop->pos.z - t->pos.z;
            proj = cx * ux + cz * uz;
            lat = ux * cz - uz * cx;
            if (proj < 30.0f || proj > len + 400.0f || fabsf(lat) > 50.0f + 0.03f * proj)
            {
                continue;
            }
            /* it is on the +lat side of the line (toward n = (-uz, ux)): Bond moves the other way */
            mx += (lat > 0.0f ? 1.0f : -1.0f) * uz;
            mz -= (lat > 0.0f ? 1.0f : -1.0f) * ux;
        }
    }
    if (mx == 0.0f && mz == 0.0f)
    {
        return 0;
    }
    th = p->vv_theta * 3.14159265f / 180.0f;
    return (mx * cosf(th) + mz * sinf(th)) > 0.0f ? 1 : -1;
}

/* Which strafe button moves Bond along r1 = (cos theta, sin theta)? Measured once from his own motion while a strafe is
 * held (20 ticks, theta steady): B.crIsR1 = +1 if C-right does, -1 if C-left does. */
static void strafeCalibrate(void)
{
    struct player *p = bond();
    u16 sb = B.btn & (BTN_CL | BTN_CR);

    if (B.strafeCal || (sb != BTN_CL && sb != BTN_CR))
    {
        B.calTick = 0;
        return;
    }
    if (B.calTick == 0 || sb != B.calBtn || B.tick != B.calLast + 1 || fabsf(pblWrap180(p->vv_theta - B.calTheta)) > 10.0f)
    {
        B.calTick = B.tick;
        B.calBtn = sb;
        B.calX = p->prop->pos.x;
        B.calZ = p->prop->pos.z;
        B.calTheta = p->vv_theta;
    }
    else if (B.tick - B.calTick >= 20)
    {
        f32 th = B.calTheta * 3.14159265f / 180.0f;
        f32 s = (p->prop->pos.x - B.calX) * cosf(th) + (p->prop->pos.z - B.calZ) * sinf(th);

        if (fabsf(s) > 25.0f)
        {
            B.crIsR1 = ((sb == BTN_CR) == (s > 0.0f)) ? 1 : -1;
            B.strafeCal = 1;
            portConLogf("ge: playerbot: strafe calibrated: C-right moves %s (cos t, sin t)\n", B.crIsR1 > 0 ? "along" : "against");
        }
        B.calTick = 0;
    }
    B.calLast = B.tick;
}

/* The armed hostile to shoot: every one in a straight line within range, ranked by pblThreatScore (raisers first, then
 * the ones shooting, then the nearest). Also fills B.threats (the ones with a line on Bond) for the cover logic. */
static PropRecord *pickTarget(f32 range)
{
    struct player *p = bond();
    s32 n = get_numguards(), i;
    PropRecord *best = NULL;
    f32 bs = 1e30f;
    s32 hurt = B.tick - B.lastHurtTick < 90;

    B.nThreats = 0;
    for (i = 0; i < n; i++)
    {
        ChrRecord *c = &g_ChrSlots[i];
        f32 dx, dy, dz, d, sc;

        if (!chrHostile(c) || c->prop->stan == NULL)
        {
            continue;
        }
        dx = c->prop->pos.x - p->prop->pos.x;
        dy = c->prop->pos.y - p->prop->pos.y;
        dz = c->prop->pos.z - p->prop->pos.z;
        d = dx * dx + dy * dy + dz * dz;
        if (d > range * range || fabsf(dy) > 400.0f)
        {
            continue;
        }
        {
            s32 k, skip = 0;

            for (k = 0; k < 4; k++)
            {
                skip |= B.tgtSkip[k] == c->prop && B.tick - B.tgtSkipAt[k] < 600;
            }
            if (skip)
            {
                continue;
            }
        }
        if (!botLine(bondTile(), p->prop->pos.x, p->prop->pos.z, c->prop->pos.x, c->prop->pos.z, c->prop->stan,
                     CDTYPE_OBJS | ((B.glassTest && chrIsRaiser(c)) ? 0 : CDTYPE_DOORS) | CDTYPE_PATHBLOCKER | CDTYPE_AIOPAQUE))
        {
            continue;
        }
        d = sqrtf(d);
        if (chrAlert(c) && B.nThreats < 6)
        {
            B.threats[B.nThreats++] = c->prop;
        }
        if (chrIsRaiser(c) && (chrAlert(c) || (B.quiet && !alarmIsActive())))
        {
            sc = pblThreatScore(d, chrAttacking(c), 1, 0);   /* the alarm: before anything else (quiet: before he moves) */
        }
        else if (quietSpare(c, d))
        {
            continue;
        }
        else
        {
            sc = pblThreatScore(d, chrAttacking(c), 0, hurt && chrAttacking(c));
        }
        if (civilianInLineYawEx(pblYawTo(dx, dz), d, chrIsRaiser(c) && chrAlert(c)))
        {
            continue;   /* a bystander in the way: not a target from here (standing still holding fire got Bond shot) */
        }
        if (sc < bs)
        {
            bs = sc;
            best = c->prop;
        }
    }
    return best;
}

/* a civilian near the line of fire toward a target `dist` away: hold the trigger */
static s32 civilianInLineYawEx(f32 yaw, f32 dist, s32 frontOnly);

/* a civilian near the current line of fire (the view), the target `dist` away */
static s32 civilianInLine(f32 dist)
{
    return civilianInLineYawEx(bond()->vv_theta, dist, B.target != NULL && B.target->chr != NULL && chrIsRaiser(B.target->chr) &&
                                                       chrAlert(B.target->chr));
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

    if (tol < 1.2f)
    {
        tol = 1.2f;   /* the look lands on the exact angle each tick: far heads need it tight */
    }
    if (fabsf(err) < tol && civilianInLine(dist))
    {   /* a bystander in the way: hold fire (and keep the strafe going, the line opens) */
        B.holdFire = B.tick;
        B.fireTick = B.tick;
        return;
    }
    if (fabsf(err) < tol && fabsf(B.pitchErr) < tol * 1.2f)
    {
        s32 ph = (B.tick - B.fireTick) % 8;

        if (isAuto(w) ? ph < 7 : ph < 2)
        {
            B.btn |= BTN_Z;
            B.fireTicks++;
            B.lastFireTick = B.tick;
        }
    }
    else
    {
        B.fireTick = B.tick;
    }
}

/* ---- cover: a tile near Bond that none of the guards with a line on him can see ------------------------------------- */

static s32 sCovQ[512];
static s32 *sCovStamp;
static s32 sCovStampN, sCovGen;

static s32 tileSeenBy(s32 node, PropRecord *t)
{
    return botLine(t->stan, t->pos.x, t->pos.z, B.g.x[node], B.g.z[node], NULL, CDTYPE_DOORS | CDTYPE_PATHBLOCKER | CDTYPE_AIOPAQUE);
}

/* the exposure (pblExposure) of tile `node` to the current threats */
static f32 tileExposure(s32 node)
{
    f32 dist[6];
    s32 seen[6], k;

    for (k = 0; k < B.nThreats && k < 6; k++)
    {
        f32 dx = B.threats[k]->pos.x - B.g.x[node], dz = B.threats[k]->pos.z - B.g.z[node];

        dist[k] = sqrtf(dx * dx + dz * dz);
        seen[k] = B.threats[k]->stan != NULL && tileSeenBy(node, B.threats[k]);
    }
    return pblExposure(dist, seen, k, 0.25f);
}

/* breadth first over the tiles from Bond's, up to ~maxd of walking, on Bond's floor: the tile at least 80 away where the
 * threats hit Bond least (pblExposure: few guards with a line on it, far ones; one far guard beats hiding, hiding beats a
 * close one), plus a little for the walk. -1 if none is clearly better than where he stands. GE_PLAYERBOT_OLDCOVER=1:
 * the round-3 rule (the nearest tile no threat sees). */
static s32 coverFind(f32 maxd)
{
    struct player *p = bond();
    s32 start = bondTile() != NULL ? tileIndex(bondTile()) : -1, qh = 0, qt = 0, k, best = -1, evals = 0;
    f32 gy, here = 0.0f, bestSc = 1e30f;

    if (start < 0 || B.nThreats == 0)
    {
        return -1;
    }
    if (sCovStampN < B.g.n)
    {
        free(sCovStamp);
        sCovStamp = (s32 *) calloc((size_t) B.g.n, sizeof(s32));
        sCovStampN = sCovStamp != NULL ? B.g.n : 0;
        if (sCovStamp == NULL)
        {
            return -1;
        }
    }
    gy = B.g.y[start];
    if (!B.oldCover)
    {
        here = tileExposure(start);
    }
    sCovGen++;
    sCovStamp[start] = sCovGen;
    sCovQ[qt++] = start;
    while (qh < qt)
    {
        s32 a = sCovQ[qh++], e;
        f32 dx = B.g.x[a] - p->prop->pos.x, dz = B.g.z[a] - p->prop->pos.z;
        f32 d2 = dx * dx + dz * dz;

        if (d2 > maxd * maxd)
        {
            continue;
        }
        if (d2 > 80.0f * 80.0f && B.g.npen[a] < 1000.0f && B.oldCover)
        {
            s32 seen = 0;

            for (k = 0; k < B.nThreats && !seen; k++)
            {
                seen = B.threats[k]->stan != NULL && tileSeenBy(a, B.threats[k]);
            }
            if (!seen)
            {
                return a;
            }
        }
        else if (d2 > 80.0f * 80.0f && B.g.npen[a] < 1000.0f && evals < 120 && (qh & 1) == 0)
        {   /* (every other tile in the search order: neighbours are near-alike, and the line tests cost) */
            f32 sc = tileExposure(a) + sqrtf(d2) * 0.0004f;

            evals++;
            if (sc < bestSc)
            {
                bestSc = sc;
                best = a;
            }
        }
        for (e = B.g.start[a]; e < B.g.start[a + 1]; e++)
        {
            s32 b = B.g.to[e];

            if (sCovStamp[b] != sCovGen && qt < (s32) (sizeof(sCovQ) / sizeof(sCovQ[0])) && fabsf(B.g.y[b] - gy) < 120.0f)
            {
                sCovStamp[b] = sCovGen;
                sCovQ[qt++] = b;
            }
        }
    }
    if (best >= 0 && bestSc < here * 0.75f - 0.05f)
    {
        if (B.verbose >= 2)
        {
            portConLogf("ge: playerbot: fight spot: exposure %.2f here -> %.2f (%d tiles scored)\n", here, bestSc, evals);
        }
        return best;
    }
    return -1;
}

static s32 wantCover(f32 hp)
{
    if (B.oldCover)
    {
        return (hp < 0.7f && B.nThreats >= 2) || (hp < 0.35f && B.nThreats >= 1) || B.nThreats >= 3;
    }
    /* the exposure search only moves when it finds a clearly better spot, so it can look whenever it matters */
    return B.nThreats >= 2 || (hp < 0.6f && B.nThreats >= 1);
}

/* fight the current target if any; returns 1 when the bot is busy fighting this tick */
static s32 combatTick(f32 range)
{
    PropRecord *t;

    if (B.peace)
    {
        return 0;
    }
    if (B.target != NULL && B.target->chr != NULL && chrIsDead(B.target->chr) && B.verbose >= 2)
    {
        portConLogf("ge: playerbot: down: chr %d%s after %.1f s\n", (s32) B.target->chr->chrnum,
                    B.target->chr->ailist == B.raiserList && B.raiserList != NULL ? " (alarm raiser)" : "",
                    (f32) (B.tick - B.targetSince) / 60.0f);
        B.target = NULL;
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
                if (d < bd && !civilianInLineYaw(pblYawTo(dx, dz), sqrtf(d)))
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
            if (t != NULL && t->chr != NULL && chrIsRaiser(t->chr) && B.verbose >= 2)
            {
                portConLogf("ge: playerbot: target: alarm raiser chr %d (action %d)\n", (s32) t->chr->chrnum,
                            (s32) t->chr->actiontype);
            }
        }
    }
    t = B.target;
    if (t != NULL && (t->chr == NULL || !chrHostile(t->chr)))
    {
        t = B.target = NULL;
    }
    if (t != NULL && B.tick - B.targetSince > 480 && B.tick - B.lastFireTick > 180)
    {   /* 8 s on this one without a shot (out of reach of the aim, a bystander always in the way): leave him for 10 s */
        B.tgtSkip[B.tgtSkipN & 3] = t;
        B.tgtSkipAt[B.tgtSkipN & 3] = B.tick;
        B.tgtSkipN++;
        if (B.verbose >= 2)
        {
            portConLogf("ge: playerbot: no shot at chr %d for 8 s - leaving him for now\n", (s32) t->chr->chrnum);
        }
        t = B.target = NULL;
    }
    {   /* cover: outnumbered or hurt -> the nearest tile none of them sees; hold it and shoot whoever comes round */
        struct player *p = bond();
        f32 hp = p->bondhealth + p->bondarmour;

        if (B.coverNode < 0 && B.nThreats > 0 && B.tick - B.lastCoverScan > 30 && wantCover(hp) && !B.noCover)
        {
            B.lastCoverScan = B.tick;
            B.coverNode = coverFind(450.0f);
            if (B.coverNode >= 0)
            {
                B.coverSince = B.tick;
                B.coverUntil = B.tick + 360;
                if (B.verbose >= 2)
                {
                    portConLogf("ge: playerbot: cover: %d threat(s), hp %.2f -> tile at %.0f,%.0f\n", B.nThreats, hp,
                                B.g.x[B.coverNode], B.g.z[B.coverNode]);
                }
            }
        }
        if (B.coverNode >= 0)
        {
            s32 n = B.coverNode;
            f32 dx = B.g.x[n] - p->prop->pos.x, dz = B.g.z[n] - p->prop->pos.z;
            s32 there = dx * dx + dz * dz < 45.0f * 45.0f || (B.stuck.level >= 1 && B.tick - B.coverSince > 60);

            if (B.tick > B.coverUntil || (there && t == NULL && B.tick - B.lastSeenTick > 120 && B.tick - B.coverSince > 150))
            {
                B.coverNode = -1;   /* nobody came: go on */
                navReset();
            }
            else if (!there && (t == NULL || B.tick - B.coverSince < 90))
            {
                navTo(B.g.x[n], B.g.y[n], B.g.z[n], 30.0f, B.tiles[n]);
                if (t != NULL)
                {   /* shoot on the way if the gun happens to bear */
                    shootAt(t->pos.x, t->pos.y + B.aimY, t->pos.z, 20.0f);
                    B.yaw = 0.0f;
                    lookToward(B.lastSteerYaw, 0.0f, 20.0f);
                }
                return 1;
            }
            else if (t != NULL)
            {   /* in cover (or caught on the way): fight from here without the strafe */
                B.lastSeenTick = B.tick;
                shootAt(t->pos.x, t->pos.y + B.aimY, t->pos.z, 20.0f);
                return 1;
            }
            else
            {   /* hold, facing where they were: they come to us */
                lookAt(B.lastTX, B.lastTY, B.lastTZ, 15.0f);
                return 1;
            }
        }
    }
    if (t == NULL)
    {
        return 0;
    }
    B.lastSeenTick = B.tick;
    B.lastTX = t->pos.x;
    B.lastTY = t->pos.y;
    B.lastTZ = t->pos.z;
    if (B.verbose >= 3)
    {
        portConLogf("ge: playerbot: fight chr %d at %.0f,%.0f,%.0f (bond %.0f,%.0f,%.0f)\n", t->chr->chrnum, t->pos.x, t->pos.y,
                    t->pos.z, bond()->prop->pos.x, bond()->prop->pos.y, bond()->prop->pos.z);
    }
    if (bestGun() == ITEM_UNARMED)
    {   /* no gun (Bunker 2 and Archives start unarmed): close in and punch, the look on him; his gun drops when he
         * goes down and the loot logic picks it up */
        f32 dx = t->pos.x - bond()->prop->pos.x, dz = t->pos.z - bond()->prop->pos.z;
        f32 d = sqrtf(dx * dx + dz * dz);

        B.moveOnly = 1;
        if (d > 55.0f)
        {
            navTo(t->pos.x, t->pos.y, t->pos.z, 50.0f, t->stan);
        }
        B.moveOnly = 0;
        lookAt(t->pos.x, t->pos.y + 10.0f, t->pos.z, 20.0f);
        if (d < 110.0f && (B.tick % 12) < 2)
        {
            B.btn |= BTN_Z;
        }
        return 1;
    }
    if (chrIsRaiser(t->chr) && chrAlert(t->chr) && !alarmIsActive())
    {   /* an alarm raiser on his way to the panel (or at it): close in while shooting - far shots miss and the alarm
         * brings the waves and the response squads */
        f32 dx = t->pos.x - bond()->prop->pos.x, dz = t->pos.z - bond()->prop->pos.z;

        if (dx * dx + dz * dz > 450.0f * 450.0f)
        {
            B.moveOnly = 1;
            navTo(t->pos.x, t->pos.y, t->pos.z, 300.0f, t->stan);
            B.moveOnly = 0;
            shootAt(t->pos.x, t->pos.y + B.aimY, t->pos.z, 20.0f);
            return 1;
        }
    }
    if (B.rush && B.goalValid)
    {   /* rush: keep walking the route (moves only; the look stays on the target) */
        B.moveOnly = 1;
        navTo(B.goalX, B.goalY, B.goalZ, 30.0f, NULL);
        B.moveOnly = 0;
        shootAt(t->pos.x, t->pos.y + B.aimY, t->pos.z, 20.0f);
        return 1;
    }
    shootAt(t->pos.x, t->pos.y + B.aimY, t->pos.z, 20.0f);
    {   /* a civilian in a guard's line of fire at Bond (his misses carry on): step aside so the line clears it;
         * otherwise keep a little lateral motion (holding fire for a bystander: one way, to open the line) */
        s32 side = civilianExposureSide();

        if (side != 0)
        {
            B.btn |= (side * B.crIsR1 > 0) ? BTN_CR : BTN_CL;
        }
        else if (B.tick - B.holdFire < 10 ? ((B.holdFire / 240) & 1) != 0 : ((B.tick / 45) & 1) != 0)
        {
            B.btn |= BTN_CL;
        }
        else
        {
            B.btn |= BTN_CR;
        }
        strafeCalibrate();
    }
    {   /* keep them at range: a guard's aim builds up per shot by 0.48 x min(1, 300 / distance) (chraction.c), so one
         * at 1500 hits five times less often than one at 300 - and Bond's movement does not matter to it. Back away
         * from an alert guard inside 450 (or 700 when hurt), unless that is blocked (then hold for 2 s). */
        struct player *p = bond();
        f32 hp = p->bondhealth + p->bondarmour;
        f32 dx = t->pos.x - p->prop->pos.x, dz = t->pos.z - p->prop->pos.z;
        f32 keep = hp < 0.5f ? 700.0f : 450.0f;

        if (dx * dx + dz * dz < keep * keep && chrAlert(t->chr) && B.nThreats <= 2 && B.tick >= B.kiteBlockedUntil && !B.noKite)
        {
            B.sy = -70;
            if (B.kiteSince == 0)
            {
                B.kiteSince = B.tick;
                B.kiteX = p->prop->pos.x;
                B.kiteZ = p->prop->pos.z;
            }
            else if (B.tick - B.kiteSince > 30)
            {
                f32 mx = p->prop->pos.x - B.kiteX, mz = p->prop->pos.z - B.kiteZ;

                if (mx * mx + mz * mz < 25.0f * 25.0f)
                {
                    B.kiteBlockedUntil = B.tick + 120;   /* a wall behind us */
                }
                B.kiteSince = 0;
            }
        }
        else
        {
            B.kiteSince = 0;
        }
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
        s32 gun = bestGun();

        if (gun != ITEM_UNARMED && currentPlayerGetAmmoCount(get_ammo_type_for_weapon(gun)) >= 60 &&
            (bondinvItemAvailable(p->weapon->weaponnum) || B.quiet) &&
            get_ammo_type_for_weapon(p->weapon->weaponnum) != get_ammo_type_for_weapon(gun))
        {
            return 0;   /* armed and stocked: a gun we have (or, quiet, a loud one) is only worth its ammo for ours */
        }
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
    if (B.planfile[0] != '\0')
    {
        snprintf(path, sizeof(path), "%s", B.planfile);
    }
    else if (portModeActive())
    {   /* a game mode: its own plan, or none (its script feeds steps through the Lua `bot` library) */
        snprintf(path, sizeof(path), "%s/%s.plan", B.plandir, portModeName());
    }
    else
    {
        snprintf(path, sizeof(path), "%s/%s.plan", B.plandir, stageName(B.stage));
    }
    f = fopen(path, "rb");
    if (f == NULL && portModeActive())
    {   /* keep whatever the script already pushed (its stage_start can run before the bot's first tick) */
        portConLogf("ge: playerbot: mode %s: no plan %s - following bot.push from the mode script (%d step(s) so far)\n",
                    portModeName(), path, B.plan.n);
        B.planLoaded = 1;
        return;
    }
    B.plan.n = 0;
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

extern void modelGetAxisExtents(Model *model, f32 *max, f32 *min, s32 axis);   /* chrprop.c (reads only) */

/* The photo objective counts a picture only when the object's whole screen box is inside the screen
 * (objective_status.c). An object's origin can be its corner or its foot, so aim at the middle of its box: the model's
 * view-space extents (its render matrices, as the objective test reads them) less its origin, turned back into a world
 * offset (view +x is the world's -(cos t, sin t), view +y is up). *frac = the box's width over the distance (0.9+
 * will not fit the picture: back off). 0 when it is not drawn this frame. */
static s32 photoAim(ObjectRecord *obj, f32 *x, f32 *y, f32 *z, f32 *frac)
{
    struct player *p = bond();
    PropRecord *op = obj->prop;
    Mtxf *m;
    f32 xmax, xmin, ymax, ymin, cx, cy, th, depth;

    if (!(op->flags & PROPFLAG_ONSCREEN) || obj->model == NULL || obj->model->render_pos == NULL ||
        (m = getsubmatrix(obj->model)) == NULL ||
        !(m->m[3][2] < -1.0f && m->m[3][2] > -1e6f) || !(fabsf(m->m[3][0]) < 1e6f) || !(fabsf(m->m[3][1]) < 1e6f))
    {
        return 0;   /* not drawn (behind, or a stale matrix) */
    }
    xmax = xmin = ymax = ymin = 1e9f;   /* (left as they are when the model has no bounding box node) */
    modelGetAxisExtents(obj->model, &xmax, &xmin, 0);
    modelGetAxisExtents(obj->model, &ymax, &ymin, 1);
    if (!(fabsf(xmax) < 1e6f && fabsf(xmin) < 1e6f && fabsf(ymax) < 1e6f && fabsf(ymin) < 1e6f))
    {
        return 0;
    }
    cx = (xmax + xmin) * 0.5f - m->m[3][0];
    cy = (ymax + ymin) * 0.5f - m->m[3][1];
    th = p->vv_theta * 3.14159265f / 180.0f;
    depth = -m->m[3][2];
    *x = op->pos.x - cx * cosf(th);
    *y = op->pos.y + cy;
    *z = op->pos.z - cx * sinf(th);
    *frac = depth > 1.0f ? fabsf(xmax - xmin) / depth : 9.0f;
    if (fabsf(ymax - ymin) / (depth > 1.0f ? depth : 1.0f) > *frac)
    {
        *frac = fabsf(ymax - ymin) / (depth > 1.0f ? depth : 1.0f);
    }
    return 1;
}

/* a floor tile 350..850 from the object with a clear line to just this side of it: where to take its picture from
 * (the nearest such tile to Bond; -1 if none) */
static s32 photoSpot(ObjectRecord *obj)
{
    struct player *p = bond();
    PropRecord *op = obj->prop;
    s32 a, best = -1;
    f32 bd = 1e30f;

    for (a = 0; a < B.g.n; a++)
    {
        f32 dx = op->pos.x - B.g.x[a], dz = op->pos.z - B.g.z[a], d2 = dx * dx + dz * dz, d, bx, bz;

        if (d2 < 350.0f * 350.0f || d2 > 850.0f * 850.0f || op->pos.y - B.g.y[a] < -100.0f || op->pos.y - B.g.y[a] > 500.0f)
        {
            continue;
        }
        bx = B.g.x[a] - p->prop->pos.x;
        bz = B.g.z[a] - p->prop->pos.z;
        if (bx * bx + bz * bz >= bd)
        {
            continue;
        }
        d = sqrtf(d2);
        if (botLine(B.tiles[a], B.g.x[a], B.g.z[a], op->pos.x - dx / d * 40.0f, op->pos.z - dz / d * 40.0f, NULL,
                    CDTYPE_DOORS | CDTYPE_PATHBLOCKER))
        {
            bd = bx * bx + bz * bz;
            best = a;
        }
    }
    return best;
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
    {   /* on screen: centre its box, and back off if it does not fit (about 0.8 rad across fits the 60-degree view) */
        f32 ax, ay, az, frac;

        if (d < 1600.0f && photoAim(obj, &ax, &ay, &az, &frac))
        {
            f32 err = lookAt(ax, ay, az, 10.0f);

            if (frac > 0.75f)
            {
                B.sy = -60;
            }
            else if (getCurrentPlayerWeaponId(GUNRIGHT) == ITEM_CAMERA && fabsf(err) < 2.0f && fabsf(B.pitchErr) < 2.0f &&
                     ((B.tick - B.stepTick) % 40) == 39)
            {
                B.btn |= BTN_Z;
                B.count++;
            }
            if (B.photoObj != obj)
            {
                B.photoObj = obj;
                B.photoSince = B.tick;
            }
            if (B.verbose >= 2 && ((B.tick - B.photoSince) % 120) == 0)
            {
                portConLogf("ge: playerbot: photo: %.0f away, box %.2f of the distance, aim %.0f,%.0f,%.0f (origin %.0f,%.0f,%.0f)\n",
                            d, frac, ax, ay, az, op->pos.x, op->pos.y, op->pos.z);
            }
            if (frac <= 0.75f || B.tick - B.photoSince < 900)
            {
                return B.count;
            }
        }
    }
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
    if (B.photoSpotObj != obj)
    {   /* walk to a tile that has a clear line to it (the object itself can be on a wall, its tile on the far side) */
        B.photoSpotObj = obj;
        B.photoSpotNode = photoSpot(obj);
        portConLogf("ge: playerbot: photo spot for the object at %.0f,%.0f,%.0f: %s %.0f,%.0f,%.0f\n", op->pos.x, op->pos.y,
                    op->pos.z, B.photoSpotNode >= 0 ? "tile at" : "none, walking at it", B.photoSpotNode >= 0 ? B.g.x[B.photoSpotNode] : 0.0f,
                    B.photoSpotNode >= 0 ? B.g.y[B.photoSpotNode] : 0.0f, B.photoSpotNode >= 0 ? B.g.z[B.photoSpotNode] : 0.0f);
    }
    if (B.photoSpotNode >= 0)
    {
        s32 n = B.photoSpotNode;

        if (navTo(B.g.x[n], B.g.y[n], B.g.z[n], 40.0f, B.tiles[n]))
        {
            lookAt(op->pos.x, op->pos.y, op->pos.z, 10.0f);
        }
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
                /* big targets (a helicopter) cannot be walked into: close enough, or as close as the bot gets */
                if (navTo(obj->prop->pos.x, obj->prop->pos.y, obj->prop->pos.z, s->op == PBL_THROW_OBJ ? 300.0f : 250.0f,
                          obj->prop->stan) ||
                    (B.stuck.level >= 1 && B.goalValid &&
                     (obj->prop->pos.x - p->prop->pos.x) * (obj->prop->pos.x - p->prop->pos.x) +
                     (obj->prop->pos.z - p->prop->pos.z) * (obj->prop->pos.z - p->prop->pos.z) < 450.0f * 450.0f))
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
            if (s->a == ITEM_UNARMED)
            {
                return 1;   /* `equip unarmed`: stop wanting anything - the best gun again */
            }
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
            /* A presses through an end cut-scene - but A is also "next gun": tapping it through a fight cycled to the
             * fist (Streets, Depot, Archives died punching). Only once nobody has been in sight for 5 s. */
            if (B.target == NULL && B.tick - B.lastSeenTick > 300 && B.tick - B.lastHurtTick > 300)
            {
                tap(BTN_A, 30);
            }
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
        case PBL_QUIET:
            B.quiet = s->a;
            return 1;
        case PBL_RUSH:
            B.rush = s->a;
            return 1;
        case PBL_AVOID:
            if (s->a < 0)
            {
                B.nAvoid = 0;   /* `avoid pad -1`: every room open again (the way out) */
                return 1;
            }
            pad = padGet(s->a);
            if (pad != NULL && pad->stan != NULL && B.nAvoid < 8)
            {
                B.avoidRoom[B.nAvoid++] = (s32) pad->stan->room;
                portConLogf("ge: playerbot: avoiding room %d (pad %d)\n", (s32) pad->stan->room, s->a);
            }
            return 1;
        case PBL_SPARE:
            if (s->a == 0)
            {
                B.nSpare = 0;
                return 1;
            }
            if (g_CurrentSetup.ailists != NULL && B.nSpare < 6)
            {
                s32 k;

                for (k = 0; g_CurrentSetup.ailists[k].ailist != NULL; k++)
                {
                    if (g_CurrentSetup.ailists[k].ID == s->a)
                    {
                        B.spare[B.nSpare++] = (void *) g_CurrentSetup.ailists[k].ailist;
                        break;
                    }
                }
            }
            return 1;
        case PBL_KEY:
        {
            ChrRecord *holder = NULL;

            if (bondinvCheckHasKeyFlags((u32) s->a))
            {
                return 1;
            }
            if (findKeyProp((u32) s->a, &holder) == NULL)
            {
                if (B.tick - B.stepTick > 300)
                {
                    portConLogf("ge: playerbot: key %x: nowhere in the level\n", (u32) s->a);
                    return 1;
                }
                return 0;
            }
            if (B.subKeyNeed == 0)
            {
                B.subKeyNeed = (u32) s->a;
                B.subSince = B.tick;
                B.keyGiveUpAt = -100000;
                navReset();
            }
            if (B.tick - B.stepTick > 7500)
            {
                return 1;
            }
            return 0;
        }
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
                {   /* skip a guard we could not get to lately */
                    s32 k, skip = 0;

                    for (k = 0; k < 8; k++)
                    {
                        skip |= B.huntSkip[k] == c && B.tick - B.huntSkipAt[k] < 3600;
                    }
                    if (skip)
                    {
                        continue;
                    }
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
            if (B.stuck.level >= 2)
            {   /* cannot get to this one: hunt the others for a minute */
                B.huntSkip[B.huntSkipN & 7] = best;
                B.huntSkipAt[B.huntSkipN & 7] = B.tick;
                B.huntSkipN++;
                B.stepFails = 0;
                navReset();
            }
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
            {   /* stand in front of him: a chr's AI "sees" Bond only inside its view cone (chrCanSeeBond), and a
                 * contact that waits for that never starts if Bond walks up behind (Statue's Valentin) */
                f32 face = getsubroty(c->model), fr = s->r > 60.0f ? s->r * 0.6f : 40.0f;
                f32 fx = c->prop->pos.x + sinf(face) * fr, fz = c->prop->pos.z + cosf(face) * fr;
                f32 ddx = c->prop->pos.x - p->prop->pos.x, ddz = c->prop->pos.z - p->prop->pos.z;

                if (ddx * ddx + ddz * ddz > (s->r + 400.0f) * (s->r + 400.0f) ||
                    pblOffFacing(face, c->prop->pos.x, c->prop->pos.z, p->prop->pos.x, p->prop->pos.z) > 60.0f)
                {
                    if (!navTo(fx, c->prop->pos.y, fz, 40.0f, NULL) && B.stuck.level < 2)
                    {
                        return 0;   /* (cannot get round in front of him: walk up to him as he is) */
                    }
                }
            }
            if (navTo(c->prop->pos.x, c->prop->pos.y, c->prop->pos.z, s->r, c->prop->stan))
            {
                lookAt(c->prop->pos.x, c->prop->pos.y + 20.0f, c->prop->pos.z, 10.0f);
                if (B.phase == 0)
                {
                    B.phase = 1;
                    B.count = B.tick;
                    portConLogf("ge: playerbot: meet: with chr %d (AI list 0x%x) at %.0f,%.0f,%.0f\n", (s32) c->chrnum,
                                (u32) s->b, c->prop->pos.x, c->prop->pos.y, c->prop->pos.z);
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
                "kills=%d fire_ticks=%d shots=%d hits=%d/%d/%d objectives=%d/%d step=%d/%d room=%d pos=%.0f,%.0f,%.0f blocked=%s\n",
                stageName(B.stage), (s32) lvlGetSelectedDifficulty(), outcome, (f32) B.tick / 60.0f, getMissiontimer() / 60,
                B.tick, B.hpLost, B.kills, B.fireTicks, get_curplayer_shot_register(SHOT_REGISTER_TOTAL) - B.shotBase[0],
                get_curplayer_shot_register(SHOT_REGISTER_HEAD) - B.shotBase[1],
                get_curplayer_shot_register(SHOT_REGISTER_BODY) - B.shotBase[2],
                get_curplayer_shot_register(SHOT_REGISTER_LIMB) - B.shotBase[3], done, needed, B.step < B.plan.n ? B.step + 1 : B.plan.n, B.plan.n,
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
    B.fireTicks = 0;
    B.lastFireTick = 0;
    memset(B.tgtSkip, 0, sizeof(B.tgtSkip));
    B.shotBase[0] = get_curplayer_shot_register(SHOT_REGISTER_TOTAL);
    B.shotBase[1] = get_curplayer_shot_register(SHOT_REGISTER_HEAD);
    B.shotBase[2] = get_curplayer_shot_register(SHOT_REGISTER_BODY);
    B.shotBase[3] = get_curplayer_shot_register(SHOT_REGISTER_LIMB);
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
    B.quiet = 0;
    B.rush = getenv("GE_PLAYERBOT_RUSH") != NULL;
    B.moveOnly = 0;
    B.nAvoid = 0;
    B.target = NULL;
    B.stuckEvents = 0;
    B.coverNode = -1;
    B.lastCoverScan = -1000;
    B.noCover = getenv("GE_PLAYERBOT_NOCOVER") != NULL;
    B.oldCover = getenv("GE_PLAYERBOT_OLDCOVER") != NULL;
    B.noKite = getenv("GE_PLAYERBOT_NOKITE") != NULL;
    B.glassTest = getenv("GE_PLAYERBOT_GLASSTEST") != NULL;   /* experiment: shoot raisers through closed doors */
    B.spareAll = getenv("GE_PLAYERBOT_SPAREALL") != NULL;
    B.kiteSince = 0;
    B.kiteBlockedUntil = 0;
    B.crIsR1 = 1;
    B.speedMode = getenv("GE_PLAYERBOT_SPEED") != NULL && atoi(getenv("GE_PLAYERBOT_SPEED")) != 0;
    B.ssSide = 1;
    B.ssHeld = 0;
    B.ssLearnN = 0;
    B.ssOff[0] = 45.0f;    /* C-left: motion ~45 to the view's one side, C-right the other (learnt on the first runs) */
    B.ssOff[1] = -45.0f;
    B.ssTurnHint = 0.0f;
    B.strafeCal = 0;
    B.calTick = 0;
    B.armourProp = NULL;
    B.armourSince = 0;
    B.armourGiveUpUntil = 0;
    B.photoSpotObj = NULL;
    B.photoObj = NULL;
    B.aimY = getenv("GE_PLAYERBOT_AIMY") != NULL ? (f32) atof(getenv("GE_PLAYERBOT_AIMY")) : 52.0f;
    B.raiserList = ailistFindById(GAILIST_SIMPLE_GUARD_ALARM_RAISER);
    B.koChr = NULL;
    B.koPhase = 0;
    B.nThreats = 0;
    for (i = 0; i < 10; i++)
    {
        B.objStatus[i] = -1;
    }
    B.flagsPrev = (u32) objectiveregisters1;
    B.nSpare = 0;
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
        if (B.verbose >= 2)
        {   /* who could it be: the guards in an attack, with distance and whether the bot's line test sees them */
            s32 n = get_numguards(), k, shown = 0;
            char buf[200];
            s32 len = snprintf(buf, sizeof(buf), "hit -%.2f (hp %.2f) target %d cover %d:", B.hpPrev - hp, hp,
                               B.target != NULL && B.target->chr != NULL ? (s32) B.target->chr->chrnum : -1, B.coverNode >= 0);

            for (k = 0; k < n && shown < 4 && len < (s32) sizeof(buf) - 30; k++)
            {
                ChrRecord *c = &g_ChrSlots[k];
                f32 dx, dz;

                if (!chrHostile(c) || !chrAttacking(c) || c->prop->stan == NULL)
                {
                    continue;
                }
                dx = c->prop->pos.x - p->prop->pos.x;
                dz = c->prop->pos.z - p->prop->pos.z;
                if (dx * dx + dz * dz > 3000.0f * 3000.0f)
                {
                    continue;
                }
                len += snprintf(buf + len, sizeof(buf) - (size_t) len, " %d@%.0f%s", (s32) c->chrnum, sqrtf(dx * dx + dz * dz),
                                botLine(bondTile(), p->prop->pos.x, p->prop->pos.z, c->prop->pos.x, c->prop->pos.z, c->prop->stan,
                                        CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_PATHBLOCKER | CDTYPE_AIOPAQUE) ? "" : "(hidden)");
                shown++;
            }
            portConLogf("ge: playerbot: %s\n", buf);
        }
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
    if ((u32) objectiveregisters1 != B.flagsPrev)
    {   /* the stage flags the level's AI sets (objective completions, "Janus walks off", ...): which one fired, when */
        u32 now = (u32) objectiveregisters1;

        portConLogf("ge: playerbot: flags +0x%x -0x%x at t=%.1fs room %d\n", now & ~B.flagsPrev, B.flagsPrev & ~now,
                    (f32) B.tick / 60.0f, bondRoom());
        B.flagsPrev = now;
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
    if (B.verbose >= 2 && (B.tick % 1800) == 0 && (B.ssTicks > 0 || B.wTicks > 0))
    {
        portConLogf("ge: playerbot: speed: strafe-run %.2f u/tick (%d ticks), run %.2f u/tick (%d ticks); offsets L %.1f R %.1f "
                    "(%d samples)\n", B.ssTicks ? B.ssDist / B.ssTicks : 0.0f, B.ssTicks, B.wTicks ? B.wDist / B.wTicks : 0.0f,
                    B.wTicks, B.ssOff[0], B.ssOff[1], B.ssLearnN);
    }
    if (B.verbose && (B.tick % 1800) == 900)
    {
        ge_shot_force = 1;   /* a screenshot every 30 s on the next rendered frame (GE_SIMONLY_TEST windows) */
    }
    if (B.verbose >= 2 && B.tick == 300 && B.raiserList != NULL)
    {   /* the alarm raisers the target choice puts first */
        s32 n = get_numguards(), k, nr = 0;

        for (k = 0; k < n; k++)
        {
            ChrRecord *c = &g_ChrSlots[k];

            if (c->model != NULL && c->prop != NULL && !chrIsDead(c) && c->ailist == B.raiserList)
            {
                nr++;
                portConLogf("ge: playerbot: alarm raiser chr %d at %.0f,%.0f,%.0f\n", (s32) c->chrnum, c->prop->pos.x,
                            c->prop->pos.y, c->prop->pos.z);
            }
        }
        portConLogf("ge: playerbot: %d alarm raiser(s)\n", nr);
    }
}

/* ---- key carriers: knock out from behind, search, rob ------------------------------------------------------------ */

/* press use next to a knocked-out chr until what it carries drops */
static void searchTick(ChrRecord *c)
{
    struct player *p = bond();
    f32 dx = c->prop->pos.x - p->prop->pos.x, dz = c->prop->pos.z - p->prop->pos.z;

    if (B.wantWeapon == ITEM_FIST)
    {
        B.wantWeapon = 0;
    }
    if (dx * dx + dz * dz > 70.0f * 70.0f)
    {
        navTo(c->prop->pos.x, c->prop->pos.y, c->prop->pos.z, 60.0f, c->prop->stan);
        return;
    }
    lookAt(c->prop->pos.x, c->prop->pos.y - 80.0f, c->prop->pos.z, 15.0f);
    if (B.tick - B.searchTick >= 30)
    {
        B.btn |= BTN_B;
        B.searchTick = B.tick;
    }
}

/* hold a gun on a civilian carrier (heist staff surrender after 0.5 s of aim and hand over what they carry after 2 more):
 * within 550 with a clear line, the view ray on it, no shot */
static void robTick(ChrRecord *c)
{
    struct player *p = bond();
    f32 dx = c->prop->pos.x - p->prop->pos.x, dz = c->prop->pos.z - p->prop->pos.z;
    f32 d = sqrtf(dx * dx + dz * dz);
    s32 los = d < 550.0f && botLine(bondTile(), p->prop->pos.x, p->prop->pos.z, c->prop->pos.x, c->prop->pos.z, c->prop->stan,
                                     CDTYPE_DOORS | CDTYPE_PATHBLOCKER);

    if (B.wantWeapon == ITEM_FIST)
    {
        B.wantWeapon = 0;
    }
    if (!los || d > 450.0f)
    {   /* (no line: walk right up - "within 200" was met through a shut door and the bot stood there for good) */
        navTo(c->prop->pos.x, c->prop->pos.y, c->prop->pos.z, los ? 200.0f : 50.0f, c->prop->stan);
        B.robSince = B.tick;
        return;
    }
    navReset();
    lookAt(c->prop->pos.x, c->prop->pos.y + 10.0f, c->prop->pos.z, 15.0f);
    if (d < 110.0f)
    {
        B.sy = -40;   /* closer than ~40 straight ahead is a shove, not an aim */
    }
    if (B.verbose >= 2 && ((B.tick - B.robSince) % 120) == 0)
    {
        portConLogf("ge: playerbot: rob: chr %d at %.0f, state %d\n", (s32) c->chrnum, d, portHeistCivilianState(c->chrnum));
    }
}

/* Sneak up behind an unaware guard carrier and slap him twice (a heist mode's knock-out). Returns 0 when that is no
 * longer the plan (he noticed Bond, or it takes too long): the caller fights him instead. */
static s32 koTick(ChrRecord *c)
{
    struct player *p = bond();
    f32 face, off, dx, dz, d, bx, bz;

    if (B.koChr != c)
    {
        B.koChr = c;
        B.koPhase = 1;
        B.koSince = B.tick;
        B.koSlapTick = 0;
        portConLogf("ge: playerbot: knock-out: sneaking up on chr %d\n", (s32) c->chrnum);
    }
    if (B.koPhase == 0)
    {
        return 0;
    }
    if (chrAlert(c) || B.tick - B.koSince > 3600)
    {
        portConLogf("ge: playerbot: knock-out of chr %d abandoned (%s) - fighting him\n", (s32) c->chrnum,
                    chrAlert(c) ? "he saw us" : "too long");
        B.koPhase = 0;
        if (B.wantWeapon == ITEM_FIST)
        {
            B.wantWeapon = 0;
        }
        return 0;
    }
    face = getsubroty(c->model);
    bx = c->prop->pos.x - sinf(face) * 70.0f;
    bz = c->prop->pos.z - cosf(face) * 70.0f;
    dx = c->prop->pos.x - p->prop->pos.x;
    dz = c->prop->pos.z - p->prop->pos.z;
    d = sqrtf(dx * dx + dz * dz);
    off = pblOffFacing(face, c->prop->pos.x, c->prop->pos.z, p->prop->pos.x, p->prop->pos.z);
    if (d > 300.0f || off < 120.0f)
    {   /* get round behind him (the route keeps out of his view: navPlan's cone penalty) */
        B.koPhase = 1;
        navTo(bx, c->prop->pos.y, bz, 40.0f, NULL);
        return 1;
    }
    B.koPhase = 2;
    B.wantWeapon = ITEM_FIST;
    navReset();
    lookAt(c->prop->pos.x, c->prop->pos.y + 10.0f, c->prop->pos.z, 20.0f);
    if (getCurrentPlayerWeaponId(GUNRIGHT) != ITEM_FIST)
    {
        return 1;   /* the slappers come out (about a second) */
    }
    if (d > 60.0f)
    {
        B.sy = 45;
    }
    if (d < 95.0f && B.tick - B.koSlapTick > 30)
    {
        B.btn |= BTN_Z;
        B.koSlapTick = B.tick;
    }
    return 1;
}

/* navPlan: keep the route to a knock-out target out of his field of view (220 degrees, in a line) */
static void koConePenalty(void)
{
    ChrRecord *c = B.koChr;
    f32 face;
    s32 a;

    if (c == NULL || B.koPhase == 0 || c->model == NULL || c->prop == NULL || c->prop->stan == NULL)
    {
        return;
    }
    face = getsubroty(c->model);
    for (a = 0; a < B.g.n; a++)
    {
        f32 dx = B.g.x[a] - c->prop->pos.x, dz = B.g.z[a] - c->prop->pos.z;

        if (dx * dx + dz * dz > 1500.0f * 1500.0f || fabsf(B.g.y[a] - (c->prop->pos.y - 110.0f)) > 150.0f ||
            pblOffFacing(face, c->prop->pos.x, c->prop->pos.z, B.g.x[a], B.g.z[a]) > 115.0f)
        {
            continue;
        }
        if (botLine(c->prop->stan, c->prop->pos.x, c->prop->pos.z, B.g.x[a], B.g.z[a], NULL, CDTYPE_DOORS | CDTYPE_PATHBLOCKER))
        {
            B.g.npen[a] += 3000.0f;
        }
    }
}

/* ---- body armour: when hurt, a lying vest within ~1500 is worth a detour (it doubles what Bond can take) ---------- */

static s32 armourDash(void)
{
    struct player *p = bond();
    f32 hp = p->bondhealth + p->bondarmour;
    PropRecord *q;

    if (hp >= 0.6f || p->bondarmour >= 0.9f || B.noLoot || B.tick < B.armourGiveUpUntil)
    {
        B.armourSince = 0;
        return 0;
    }
    if (B.armourProp == NULL || (B.tick % 15) == 0)
    {
        f32 bd = 1500.0f * 1500.0f;

        if (B.armourProp != NULL && !propActive(B.armourProp))
        {
            portConLogf("ge: playerbot: armour picked up (health %.2f armour %.2f)\n", p->bondhealth, p->bondarmour);
        }
        B.armourProp = NULL;
        for (q = chrpropGetActiveTail(); q != NULL; q = q->prev)
        {
            f32 dx, dz;

            if (q->type != PROP_TYPE_OBJ || q->obj == NULL || q->obj->type != PROPDEF_ARMOUR || q->stan == NULL ||
                fabsf(q->pos.y - p->prop->pos.y) > 300.0f)
            {
                continue;
            }
            dx = q->pos.x - p->prop->pos.x;
            dz = q->pos.z - p->prop->pos.z;
            if (dx * dx + dz * dz < bd)
            {
                bd = dx * dx + dz * dz;
                B.armourProp = q;
            }
        }
    }
    q = B.armourProp;
    if (q == NULL)
    {
        B.armourSince = 0;
        return 0;
    }
    if (B.armourSince == 0)
    {
        B.armourSince = B.tick;
        portConLogf("ge: playerbot: hurt (%.2f): going for the body armour at %.0f,%.0f,%.0f\n", hp, q->pos.x, q->pos.y, q->pos.z);
        navReset();
    }
    if (B.tick - B.armourSince > 1800)
    {
        portConLogf("ge: playerbot: could not get to the armour in 30 s\n");
        B.armourGiveUpUntil = B.tick + 3600;
        B.armourProp = NULL;
        navReset();
        return 0;
    }
    if (B.target != NULL && B.target->chr != NULL && chrHostile(B.target->chr))
    {
        f32 dx = B.target->pos.x - p->prop->pos.x, dz = B.target->pos.z - p->prop->pos.z;

        if (dx * dx + dz * dz < 350.0f * 350.0f)
        {
            return 0;   /* one right on us: fight him first */
        }
    }
    if ((B.tick % 4) == 0)
    {
        B.target = pickTarget(1600.0f);   /* keep the target fresh: a close one stops the dash (above) */
    }
    B.coverNode = -1;
    navTo(q->pos.x, q->pos.y, q->pos.z, 8.0f, q->stan);
    return 1;
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
    if (armourDash())
    {   /* hurt, and body armour lies close by: it comes first (fights only at close range on the way) */
        B.stuck.lastGain = B.tick;
        return;
    }
    /* fight first in every step except the ones that hold the buttons themselves (and gadget steps once they act) */
    if (s == NULL || (s->op != PBL_PRESS && s->op != PBL_WALK && s->op != PBL_KILL &&
                      !((s->op == PBL_GADGET_OBJ || s->op == PBL_THROW_OBJ) && B.phase == 1)))
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
        return;   /* nothing to do (a mode script may push more) */
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
            B.koChr = NULL;
            B.koPhase = 0;
            if (B.wantWeapon == ITEM_FIST)
            {
                B.wantWeapon = 0;
            }
            navReset();
            return;
        }
        if (holder != NULL)
        {
            if (chrIsDead(holder))
            {
                return;   /* it drops when the body settles */
            }
            if (portHeistKoIs(holder->chrnum))
            {   /* knocked out: search him (use within 130) */
                searchTick(holder);
                return;
            }
            if (portHeistCivilianState(holder->chrnum) >= 0)
            {   /* heist staff: never shoot - hold a gun on it until it hands the key over (a stock civilian carrier, a
                 * Facility scientist, is handled as before: walked up to and shot, which only Secret Agent up counts) */
                robTick(holder);
                return;
            }
            if (!chrHostile(holder) && holder->prop != NULL)
            {   /* an unarmed carrier: walk up and shoot it anyway (the key is the mission) */
                if (navTo(holder->prop->pos.x, holder->prop->pos.y, holder->prop->pos.z, 250.0f, holder->prop->stan))
                {
                    B.civExempt = holder;   /* (it is the target, not a bystander) */
                    shootAt(holder->prop->pos.x, holder->prop->pos.y + 20.0f, holder->prop->pos.z, 25.0f);
                    B.civExempt = NULL;
                }
                return;
            }
            if (B.quiet && !alarmIsActive() && koTick(holder))
            {
                return;   /* sneaking up behind him */
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

/* ---- the Lua `bot` library (port/luavm/port_luavm_bot.c) --------------------------------------------------------- */

int portPlayerBotModeForLua(void)
{
    return portPlayerBotMode();
}

int portPlayerBotPushLine(const char *line, char *err, int errlen)
{
    static PblPlan one;

    if (pblParsePlan(line, &one, err, errlen) < 0)
    {
        return 0;
    }
    if (B.plan.n + one.n > PBL_MAXSTEPS)
    {
        /* drop the finished steps to make room */
        s32 done = B.step < B.plan.n ? B.step : B.plan.n;

        memmove(&B.plan.s[0], &B.plan.s[done], (size_t) (B.plan.n - done) * sizeof(PblStep));
        B.plan.n -= done;
        B.step -= done;
    }
    if (B.plan.n + one.n > PBL_MAXSTEPS)
    {
        snprintf(err, errlen, "plan full");
        return 0;
    }
    memcpy(&B.plan.s[B.plan.n], one.s, (size_t) one.n * sizeof(PblStep));
    B.plan.n += one.n;
    return 1;
}

void portPlayerBotClearSteps(void)
{
    B.plan.n = B.step;   /* the running step and the rest go */
    stepBegin();
}

void portPlayerBotStepInfo(int *step, int *count)
{
    *step = B.step + 1;
    *count = B.plan.n;
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
