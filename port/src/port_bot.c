/* port_bot.c - the co-op bot's lifecycle (see port_bot.h).
 *
 * portBotTick runs at the top of chrlvAllChrTick (the sim update, never the render path).
 * It returns at once unless the bot is enabled, Bond is in a mission (his prop has a stan),
 * play is solo, no demo is playing or recording, and the camera is first person (Bond's
 * prop already has a stan during the intro cameras). So with the bot off nothing here draws
 * RNG, takes a slot or touches game state, and all timers pause during cutscenes.
 *
 * States: WAIT (spawn when the timer runs out; 1 s into play, then retry every 1 s),
 * ALIVE (watch for death), DEAD (respawn timer via portBotDecide), OFF (dismissed).
 * The chr is always looked up by chrnum, never cached: a removed chr's slot is reused.
 *
 * The brain (portBotChrTick) runs from chrlvActionTick in place of the bot's ai() call,
 * so it ticks only when the chr's sleep runs out (standing re-arms 14-18 ticks); its dt
 * comes from the stage tick count, not g_ClockTimer. It maps portBotDecide's act to
 * engine actions and re-issues a movement only on a change or every BOT_REISSUE_SECS.
 *
 * Test hooks (env):
 *   GE_BOT=1             enable (the coop_bot ini key joins in Task 9)
 *   GE_BOT_DEBUG=1       act changes, a status line every 2 s, knife and target logs
 *   GE_BOT_STRAND=1      once, 3 s after the first spawn, move Bond to the waypoint pad
 *                        nearest 2000 units away (catch-up test)
 *   GE_BOT_SELFKILL=1    lethal damage 2 s after every spawn (death/respawn test)
 *   GE_BOT_PASSIVE=1     the default pickTarget finds nothing: no throws (guards-vs-bot test)
 *   GE_BOT_TARGETBOND=1  a replacement pickTarget hook returns Bond: knives at Bond (friendly fire test)
 *   GE_BOT_SKINTEST=1    spawn every roster skin in turn (3.5 s each) and log whether it loaded
 *   GE_BOT_SLOTTEST=1    first fill chr slots with idle fillers down to the spawn gate
 *   GE_BOT_RESTART_AT=N  abort the mission N sim ticks (1/60 s) into the stage, once per
 *                        run, via the pause-menu Abort steps, and launch GE_STAGE again */
#ifndef TARGET_N64

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ultra64.h>   /* PR/gu.h declares sqrtf/sinf/cosf; nothing declares fabsf (-ffreestanding) */
#include <bondtypes.h>
#include <bondconstants.h>
#include <bondaicommands.h>   /* GAILIST_COOP_BOT */
#include <math.h>
#include "game/chr.h"
#include "game/chrai.h"
#include "game/chraction.h"
#include "game/player.h"
#include "game/bondview.h"
#include "game/model.h"
#include "game/lv.h"
#include "game/stan.h"
#include <memp.h>
#include "port_botlogic.h"
#include "port_modifier.h"
#include "port_spawn.h"
#include "port_faction.h"
#include "port_chrthrow.h"
#include "port_bot.h"
#include "port_coop.h"
#include "port_arena.h"
#include "game/math_atan2f.h"   /* telemetry angle; not declared anywhere else */

extern void portConLogf(const char *fmt, ...);   /* port_console.c */
extern const char *portLogPath(const char *name);   /* main.c */
extern s32 get_is_ramrom_flag(void);             /* ramromreplay.c */
extern s32 get_recording_ramrom_flag(void);
extern s32 chrGetNumFree(void);                  /* chr.c */
extern enum CAMERAMODE g_CameraMode;             /* bondview2.c (bondview_internal.h) */
/* GE_BOT_RESTART_AT: the pause-menu Abort steps (options.c) and the GE_STAGE relaunch */
extern void set_missionstate(MISSION_STATE_ID state);
extern void bossRunTitleStage(void);
extern s32 mission_failed_or_aborted;
extern s32 g_PortDirectLaunchRearm;              /* front.c */
extern bool chrIsNotDeadOrShot(ChrRecord *self);  /* chraction.c, not in chraction.h */
extern void sub_GAME_7F024CF8(ChrRecord *self, coord3d *pos);        /* chraction.c: run to coordinate (not in the header) */
extern void chrlvWalkingAnimationRelated(ChrRecord *self);          /* chraction.c: the walking animation */
extern s32 bossGetStageNum(void);                /* boss.c */
extern s32 portMpCharBodyHead(s32 c, s32 *body, s32 *head, f32 *pov);   /* front.c: the MP roster entry */

#define BOT_AUTOSPAWN_SECS 1.0f
#define BOT_RETRY_SECS     1.0f
#define BOT_SELFKILL_SECS  2.0f
#define BOT_CORPSE_CHRNUM  6000            /* dead bots are renamed from here up */
#define BOT_SLOTTEST_MAX   64
#define BOT_EPS            0.0001f
#define BOT_APPROACH_MULT  1.6f            /* targets are picked out to knifeRange x this */
#define BOT_ACT_APPROACH   100             /* sIssuedAct only: walking toward a target out of knife range */
#define BOT_UNBLOCK_HOLD   1.5f            /* seconds the step-back run is left alone */
#define BOT_DUCK_EPS      1.0f            /* ducking_height_offset below this = crouched (full is -100) */
#define BOT_REPLOT_DIST    300.0f          /* Bond must move this far from the last route's goal to re-plot */
#define BOT_BLOCK_DIST     70.0f           /* bot this close in front of Bond can block him */
#define BOT_BLOCK_DOT      0.7f
#define BOT_BLOCK_SECS     1.0f
#define BOT_PUSH_SPEED     0.2f            /* speedforwards above this = Bond is pushing forward */
#define BOT_REISSUE_SECS   0.5f
#define BOT_STATUS_SECS    2.0f
#define BOT_STRAND_SECS    3.0f
#define BOT_STRAND_DIST    2000.0f
#define BOT_LOITER_TRIES   10              /* candidate points tried per pick */
#define BOT_LOITER_ARRIVE  45.0f           /* within this of the point = arrived */
#define BOT_LOITER_WALK_MAX 12.0f          /* give up on a walk after this many seconds */
#define BOT_LOITER_MIN_STEP 80.0f          /* a point nearer than this to where she stands is not worth a walk */
#define BOT_LOITER_LOOK    300.0f          /* open-space test length for the facing */
#define BOT_LOITER_CLEAR   70.0f           /* a loiter route keeps this far from Bond */
#define BOT_LOITER_ETA60   (60 * 15)       /* run-to-coordinate widens its arrival radius after this many ticks; a walk is slower than the run it assumes */
#define BOT_TURN_RATE      3.0f            /* radians per second of the covering turn */
#define BOT_SKINTEST_SECS  3.5f
#define BOT_TARGET_CHR     0x0004          /* TARGET_CHR (bondaicommands.h) */
#define BOT_TARGET_BOND    0x0001          /* TARGET_BOND */
#define BOT_ID_BOND        (-2)            /* Bond as a target id in logs */
#ifdef VERSION_EU
#define BOT_FIELD_6C       0.20039999485f  /* bondview_r.c FIELD_6C_FACTOR */
#define BOT_FIELD_3B8      0.118799984f
#else
#define BOT_FIELD_6C       0.170000016689f
#define BOT_FIELD_3B8      0.100000024f
#endif

enum { BOT_WAIT, BOT_ALIVE, BOT_DEAD, BOT_OFF };

static PortBotTuning sTune;
static PortBotMemory sMem;
static s32 sState;
static f32 sWait;          /* WAIT: seconds until the next spawn try */
static s32 sSpawned;       /* spawned at least once this stage ("respawned" after that) */
static s32 sSummon;        /* portBotSummon request, handled in the next tick */
static s32 sCorpseNext;
static f32 sAliveFor;      /* GE_BOT_SELFKILL */
static s32 sSlotTestDone;
static s32 sStageTicks;    /* gated sim ticks this stage (GE_BOT_RESTART_AT, brain dt) */
/* brain */
static s32 sBrainTicks;    /* sStageTicks at the last brain tick */
static s32 sLastAct;       /* last act issued (PortBotAct) */
static s32 sIssuedAct;     /* last act actually issued (a gated switch leaves it behind sLastAct) */
static f32 sHold;          /* seconds left in which the brain issues nothing (unblock step-back) */
static s32 sTeleport;      /* coop_bot_teleport: allow the catch-up warp (default off) */
static f32 sPushFor;      /* seconds Bond has pushed forward with the bot right in front of him */
static f32 sSinceIssue;    /* seconds since the last act was issued */
static s32 sNavFailed;     /* the last RUN_TO_BOND's chrGoToBond failed */
static s32 sFlankPad;      /* padpreset1 of the last TAKE_FLANK, -1 = none */
static coord3d sRunGoal;   /* Bond's position at the last RUN_TO_BOND */
static s32 sTargetNum;     /* chrnum of the last knife target, -1 = none */
static f32 sStatusIn;
static s32 sStrandDone;    /* GE_BOT_STRAND: once per run */
/* idle loiter (a private PRNG seeded per stage, so bot-on runs stay reproducible) */
enum { LT_NONE, LT_WALK, LT_PAUSE };
static unsigned int sRng;      /* loiter points and pauses */
static unsigned int sSkinRng;  /* random skin */
static s32 sLt;                /* LT_* */
static f32 sLtTimer;           /* WALK: seconds walking; PAUSE: seconds left */
static coord3d sLtPoint;
static f32 sLtFace;            /* PAUSE: the yaw to turn to */
static s32 sLtFaceSet;
static s32 sLtPicks;           /* points picked this stage (logs) */
static f32 sBrainDt;           /* dt of the current brain tick */
static s32 sCurSkin = -1;      /* skin index of the live bot; -1 = the legacy body path */
static s32 sCurSkinLast = -1;  /* the last skin drawn (random never repeats it) */

static s32 envOn(const char *name);

static PropRecord *botDefaultPickTarget(ChrRecord *bot)
{
    static s32 passive = -1;
    ChrRecord *c;

    if (passive < 0)
    {
        passive = envOn("GE_BOT_PASSIVE");
    }
    /* beyond knifeRange the brain walks toward the target instead of throwing */
    c = passive ? NULL : portFactionNearestHostile(bot, sTune.knifeRange * BOT_APPROACH_MULT, TRUE);
    return c != NULL ? c->prop : NULL;
}

/* GE_BOT_TARGETBOND, installed through g_PortBotHooks: Bond, at any range and without LOS. */
static PropRecord *botTargetBondPick(ChrRecord *bot)
{
    return getCurrentPlayerProp();
}

PortBotHooks g_PortBotHooks = { NULL, botDefaultPickTarget, NULL, NULL };

static const char *const sActNames[] = {
    "NONE", "IDLE", "RUN_TO_BOND", "TAKE_FLANK", "FACE_TARGET", "THROW", "TELEPORT", "RESPAWN", "LOITER"
};

static s32 botDebug(void)
{
    static s32 dbg = -1;

    if (dbg < 0)
    {
        const char *e = getenv("GE_BOT_DEBUG");
        dbg = (e != NULL && e[0] != '\0' && e[0] != '0') ? 1 : 0;
    }
    return dbg;
}

/* Brain memory that must not survive a spawn, death or stage reset. */
static void botBrainReset(void)
{
    sBrainTicks = sStageTicks;
    sLastAct = PORT_BOTACT_NONE;
    sIssuedAct = PORT_BOTACT_NONE;
    sPushFor = 0.0f;
    sHold = 0.0f;
    sSinceIssue = 0.0f;
    sNavFailed = FALSE;
    sFlankPad = -1;
    sTargetNum = -1;
    sStatusIn = 0.0f;
    sLt = LT_NONE;
    sLtFaceSet = FALSE;
}

static s32 envOn(const char *name)
{
    const char *e = getenv(name);
    return e != NULL && e[0] != '\0' && e[0] != '0';
}

/* ---- telemetry (GE_BOT_TELEMETRY=<path>, default Data/Logs/bot_telemetry.csv in the arena) ----
 * The brain tick captures what portBotDecide saw; portBotTick writes a row every 0.25 s. */

#define BOT_TEL_SECS 0.25f

static struct {
    s32 valid, act, targetChr, los, inRange, nRange, nLos;
    f32 distToBond, targetDist, angle;
    const char *why;
} sTel;
static FILE *sTelFile;
static s32 sTelInit;       /* env parsed */
static const char *sTelPath;
static f32 sTelIn;

static void botTelemetryStageEnd(void)
{
    if (sTelFile != NULL)
    {
        fflush(sTelFile);
    }
    sTel.valid = FALSE;
    sTelIn = 0.0f;
}

static const char *botWhyNot(const PortBotInputs *in, s32 act, ChrRecord *nearest, s32 los)
{
    if (act == PORT_BOTACT_THROW)
    {
        return "none";
    }
    if (sHold > 0.0f)
    {
        return "moving";   /* the unblock step-back is running */
    }
    if (!in->hasTarget)
    {
        return nearest == NULL ? "no_target" : (los ? "other" : "no_los");
    }
    if (in->targetDist > sTune.knifeRange)
    {
        return "out_of_range";
    }
    if (in->distToBond > sTune.leash && in->targetDist > sTune.leash * 0.5f)   /* as portBotDecide */
    {
        return "leash";
    }
    return act == PORT_BOTACT_FACE_TARGET ? "cooldown" : "other";
}

/* Same inputs and act as portBotDecide returned this tick. */
static void botTelemetryCapture(ChrRecord *self, const PortBotInputs *in, s32 act, PropRecord *target)
{
    ChrRecord *tc = (target != NULL && target->type == PROP_TYPE_CHR) ? target->chr : NULL;
    ChrRecord *nearest = NULL;
    f32 dx, dz, rad;

    if (sTelPath == NULL)
    {
        return;
    }
    if (target == NULL)
    {
        nearest = portFactionNearestHostile(self, sTune.knifeRange * BOT_APPROACH_MULT, FALSE);
        tc = nearest;
    }
    else
    {
        nearest = tc;
    }
    sTel.valid = TRUE;
    sTel.act = act;
    sTel.distToBond = in->distToBond;
    sTel.targetChr = tc != NULL ? (s32)tc->chrnum : (target != NULL ? BOT_ID_BOND : -1);
    sTel.los = tc != NULL ? portFactionLosTo(self, tc) : (target != NULL ? 1 : 0);
    sTel.targetDist = in->hasTarget ? in->targetDist : 0.0f;
    sTel.angle = 0.0f;
    if (tc != NULL || target != NULL)
    {
        PropRecord *tp = tc != NULL ? tc->prop : target;
        f32 d2;

        dx = tp->pos.x - self->prop->pos.x;
        dz = tp->pos.z - self->prop->pos.z;
        d2 = dx * dx + dz * dz + (tp->pos.y - self->prop->pos.y) * (tp->pos.y - self->prop->pos.y);
        if (!in->hasTarget)
        {
            sTel.targetDist = sqrtf(d2);
        }
        rad = atan2f(dx, dz) - getsubroty(self->model);
        while (rad > M_PI_F)
        {
            rad -= M_TAU_F;
        }
        while (rad < -M_PI_F)
        {
            rad += M_TAU_F;
        }
        sTel.angle = (rad < 0.0f ? -rad : rad) * (180.0f / M_PI_F);
    }
    sTel.inRange = sTel.targetChr != -1 && sTel.targetDist <= sTune.knifeRange;
    portFactionCountHostile(self, sTune.knifeRange, &sTel.nRange, &sTel.nLos);
    sTel.why = botWhyNot(in, act, nearest, sTel.los);
}

static void botTelemetryRow(ChrRecord *self)
{
    s32 th, hi, ki, de;

    if (!sTel.valid || self == NULL || self->prop == NULL)
    {
        return;
    }
    if (sTelFile == NULL)
    {
        sTelFile = fopen(sTelPath, "w");
        if (sTelFile == NULL)
        {
            portConLogf("ge: bot: telemetry: cannot open %s\n", sTelPath);
            sTelPath = NULL;
            return;
        }
        portConLogf("ge: bot: telemetry -> %s\n", sTelPath);
        fprintf(sTelFile, "t,state,act,bot_x,bot_z,bot_hp,bond_dist,target_chr,target_dist,target_los,target_in_range,"
                          "target_angle_deg,n_hostiles_1000,n_hostiles_los,why_not_attacking,throws_total,hits_total,"
                          "kills_total,deaths_total\n");
    }
    portArenaTotals(&th, &hi, &ki, &de);
    fprintf(sTelFile, "%.2f,ALIVE,%s,%.0f,%.0f,%.2f,%.0f,%d,%.0f,%d,%d,%.0f,%d,%d,%s,%d,%d,%d,%d\n",
            (double)sStageTicks / 60.0, sActNames[sTel.act], self->prop->pos.x, self->prop->pos.z,
            self->maxdamage > 0.0f ? 1.0f - self->damage / self->maxdamage : 0.0f, sTel.distToBond,
            (int)sTel.targetChr, sTel.targetDist, (int)sTel.los, (int)sTel.inRange, sTel.angle,
            (int)sTel.nRange, (int)sTel.nLos, sTel.why, (int)th, (int)hi, (int)ki, (int)de);
    fflush(sTelFile);
}

static void botTelemetryInit(void)
{
    const char *e = getenv("GE_BOT_TELEMETRY");

    sTelInit = TRUE;
    if (e != NULL && e[0] != '\0')
    {
        sTelPath = e;
    }
    else if (portArenaEnabled())
    {
        static char path[1200];

        snprintf(path, sizeof(path), "%s", portLogPath("bot_telemetry.csv"));   /* Data/Logs beside the saves */
        sTelPath = path;
    }
}

/* ---- settings (midas.ini coop_bot*, the console `bot` command) ---- */

static struct {
    s32 enabled;        /* coop_bot */
    s32 body;           /* coop_bot_body */
    f32 health;         /* coop_bot_health: multiplier on the vanilla max damage of 4 */
    f32 respawn;        /* coop_bot_respawn_secs */
    f32 cooldown;       /* coop_bot_knife_cooldown */
    f32 range;          /* coop_bot_knife_range */
    f32 friendlyFire;   /* coop_bot_friendly_fire */
    f32 guardHit;       /* coop_bot_guard_hit */
    f32 followDist;     /* coop_bot_follow_dist: personal space */
    s32 loiter;         /* coop_bot_loiter */
    s32 skin;           /* coop_bot_skin: a skin index, SKIN_RANDOM, or SKIN_UNSET (coop_bot_body, else natalya) */
} sCfg = { 0, BODY_Natalya_Skirt, 4.0f, 5.0f, 1.2f, 1000.0f, 0.5f, 0.35f, 250.0f, 1, -2 };
#define SKIN_RANDOM (-1)
#define SKIN_UNSET  (-2)
static s32 sSessionOff;    /* `bot off`: no auto-spawn until `bot` is issued again */

/* sCfg over the pure defaults; portBotReset runs it each stage, a live set again. */
static void botApplyCfg(void)
{
    sTune.respawnAfter = sCfg.respawn;
    sTune.knifeCooldown = sCfg.cooldown;
    sTune.knifeRange = sCfg.range;
    portBotTuningSetFollowDist(&sTune, sCfg.followDist);
    sTune.loiter = sCfg.loiter;
    portChrThrowSetFriendlyFire(sCfg.friendlyFire);
    portFactionSetGuardHitChance(sCfg.guardHit);
}

static s32 botParseFloat(const char *v, f32 lo, f32 hi, f32 *out)
{
    char *end;
    f32 f = (f32)strtod(v, &end);

    if (end == v || *end != '\0' || !(f >= lo && f <= hi))
    {
        return FALSE;
    }
    *out = f;
    return TRUE;
}

/* "random", or a roster name (portBotSkinName) -> a skin index; FALSE if neither */
static s32 botSkinParse(const char *v, s32 *k)
{
    s32 i;

    if (v == NULL)
    {
        return FALSE;
    }
    if (strcmp(v, "random") == 0 || strcmp(v, "Random") == 0 || strcmp(v, "RANDOM") == 0)
    {
        *k = SKIN_RANDOM;
        return TRUE;
    }
    i = portBotSkinFind(v);
    if (i < 0)
    {
        return FALSE;
    }
    *k = i;
    return TRUE;
}

s32 portBotSetOption(const char *key, const char *value)
{
    char *end;
    f32 f;
    long n;

    if (key == NULL || strncmp(key, "coop_bot", 8) != 0)
    {
        return FALSE;
    }
    if (strcmp(key, "coop_bot_skin") == 0)
    {
        s32 k;

        if (botSkinParse(value, &k))
        {
            sCfg.skin = k;
        }
        else
        {
            portConLogf("ge: bot: bad value for %s (a skin name or random; `bot skin list`)\n", key);
        }
        return TRUE;
    }
    n = strtol(value, &end, 10);
    if (strcmp(key, "coop_bot") == 0 || strcmp(key, "coop_bot_teleport") == 0 || strcmp(key, "coop_bot_loiter") == 0)
    {
        if (end == value || *end != '\0' || (n != 0 && n != 1))
        {
            portConLogf("ge: bot: bad value for %s\n", key);
            return TRUE;
        }
        if (key[8] == '\0')
        {
            sCfg.enabled = (s32)n;
        }
        else if (key[9] == 't')
        {
            sTeleport = (s32)n;
        }
        else
        {
            sCfg.loiter = (s32)n;
            botApplyCfg();
        }
        return TRUE;
    }
    if (strcmp(key, "coop_bot_body") == 0)
    {
        if (end == value || *end != '\0' || n < 0 || n > 0x7fff)
        {
            portConLogf("ge: bot: bad value for %s\n", key);
        }
        else
        {
            sCfg.body = (s32)n;
        }
        return TRUE;
    }
    if (strcmp(key, "coop_bot_health") == 0 && botParseFloat(value, 0.25f, 100.0f, &f))
    {
        sCfg.health = f;
    }
    else if (strcmp(key, "coop_bot_respawn_secs") == 0 && botParseFloat(value, 0.0f, 600.0f, &f))
    {
        sCfg.respawn = f;
    }
    else if (strcmp(key, "coop_bot_knife_cooldown") == 0 && botParseFloat(value, 0.1f, 60.0f, &f))
    {
        sCfg.cooldown = f;
    }
    else if (strcmp(key, "coop_bot_knife_range") == 0 && botParseFloat(value, 50.0f, 20000.0f, &f))
    {
        sCfg.range = f;
    }
    else if (strcmp(key, "coop_bot_friendly_fire") == 0 && botParseFloat(value, 0.0f, 1.0f, &f))
    {
        sCfg.friendlyFire = f;
    }
    else if (strcmp(key, "coop_bot_guard_hit") == 0 && botParseFloat(value, 0.0f, 1.0f, &f))
    {
        sCfg.guardHit = f;
    }
    else if (strcmp(key, "coop_bot_follow_dist") == 0 && botParseFloat(value, 50.0f, 2000.0f, &f))
    {
        sCfg.followDist = f;
    }
    else
    {
        portConLogf("ge: bot: bad value for %s\n", key);   /* also an unknown coop_bot* key: keeps the old value */
        return TRUE;
    }
    botApplyCfg();
    return TRUE;
}

/* The menu bridge: toggles and the body as themselves, floats as tenths. */
s32 portBotOptionGet(const char *key, s32 *out)
{
    if (strcmp(key, "coop_bot") == 0)                   { *out = sCfg.enabled; return TRUE; }
    if (strcmp(key, "coop_bot_teleport") == 0)          { *out = sTeleport; return TRUE; }
    if (strcmp(key, "coop_bot_body") == 0)              { *out = sCfg.body; return TRUE; }
    if (strcmp(key, "coop_bot_health") == 0)            { *out = (s32)(sCfg.health * 10.0f + 0.5f); return TRUE; }
    if (strcmp(key, "coop_bot_respawn_secs") == 0)      { *out = (s32)(sCfg.respawn * 10.0f + 0.5f); return TRUE; }
    if (strcmp(key, "coop_bot_knife_cooldown") == 0)    { *out = (s32)(sCfg.cooldown * 10.0f + 0.5f); return TRUE; }
    if (strcmp(key, "coop_bot_knife_range") == 0)       { *out = (s32)sCfg.range; return TRUE; }
    if (strcmp(key, "coop_bot_friendly_fire") == 0)     { *out = (s32)(sCfg.friendlyFire * 10.0f + 0.5f); return TRUE; }
    if (strcmp(key, "coop_bot_guard_hit") == 0)         { *out = (s32)(sCfg.guardHit * 10.0f + 0.5f); return TRUE; }
    if (strcmp(key, "coop_bot_follow_dist") == 0)       { *out = (s32)sCfg.followDist; return TRUE; }
    if (strcmp(key, "coop_bot_loiter") == 0)            { *out = sCfg.loiter; return TRUE; }
    if (strcmp(key, "coop_bot_skin") == 0)              { *out = sCfg.skin; return TRUE; }
    return FALSE;
}

s32 portBotEnabled(void)
{
    static s32 env = -1;

    if (env < 0)
    {
        env = envOn("GE_BOT");
    }
    return env || sCfg.enabled || portArenaEnabled();   /* PORT: arena implies the bot */
}

void portBotReset(void)
{
    portBotTuningDefaults(&sTune);
    botApplyCfg();
    portBotMemoryReset(&sMem);
    sState = sSessionOff ? BOT_OFF : BOT_WAIT;
    sWait = BOT_AUTOSPAWN_SECS;
    sSpawned = FALSE;
    sSummon = FALSE;
    sCorpseNext = BOT_CORPSE_CHRNUM;
    sAliveFor = 0.0f;
    sSlotTestDone = FALSE;
    sStageTicks = 0;
    portBotRngSeed(&sRng, (unsigned int)bossGetStageNum());              /* the bot's own PRNG, per stage: bot-on runs repeat */
    portBotRngSeed(&sSkinRng, (unsigned int)bossGetStageNum() + 0x5C1Bu);
    sLtPicks = 0;
    sCurSkinLast = -1;
    if (!sTelInit)
    {
        botTelemetryInit();
    }
    botBrainReset();
    portSpawnReset();
    portFactionReset();
    portArenaReset();
    botTelemetryStageEnd();
    if (portBotEnabled())
    {
        portConLogf("ge: bot: reset\n");
        if (envOn("GE_BOT_TARGETBOND"))
        {
            g_PortBotHooks.pickTarget = botTargetBondPick;
            portConLogf("ge: bot: GE_BOT_TARGETBOND: pickTarget hook returns Bond\n");
        }
    }
}

ChrRecord *portBotChr(void)
{
    ChrRecord *chr;

    if (sState != BOT_ALIVE)
    {
        return NULL;
    }
    chr = chrFindByLiteralId(PORT_BOT_CHRNUM);
    if (chr == NULL || chrIsDead(chr))
    {
        return NULL;
    }
    return chr;
}

s32 portBotIsBot(ChrRecord *chr)
{
    return sSpawned && chr != NULL && chr->model != NULL && chr->chrnum == PORT_BOT_CHRNUM;
}

void portBotSummon(void)
{
    sSummon = TRUE;
}

void portBotDismiss(void)
{
    ChrRecord *chr = sSpawned ? chrFindByLiteralId(PORT_BOT_CHRNUM) : NULL;   /* no chr table before a spawn */

    if (chr != NULL && chr->prop != NULL)
    {
        chr->hidden |= CHRHIDDEN_REMOVE;   /* the AI_ChrRemoveInstant removal */
    }
    sState = BOT_OFF;
    sSummon = FALSE;
}

static PropRecord *botGate(void);
static s32 botSkinSource(void);

/* Re-skin a live bot at once: free her chrnum (rename the old chr like a corpse) and remove it, then spawn
 * the new look in the next tick. Otherwise (waiting, dead, dismissed) the next spawn simply uses the new skin. */
static void botReskinNow(void)
{
    ChrRecord *chr;

    if (sState != BOT_ALIVE)
    {
        return;
    }
    chr = chrFindByLiteralId(PORT_BOT_CHRNUM);
    if (chr != NULL)
    {
        s32 n = sCorpseNext;

        while (chrFindByLiteralId(n) != NULL)
        {
            n++;
        }
        chr->chrnum = (s16)n;
        sCorpseNext = n + 1;
        if (chr->prop != NULL)
        {
            chr->hidden |= CHRHIDDEN_REMOVE;
        }
    }
    botBrainReset();
    sState = BOT_WAIT;
    sWait = 0.0f;
}

static void botSkinList(void)
{
    char line[400];
    s32 k, n = 0;

    portConLogf("ge: bot: skins (%d), or `random`:\n", (int)portBotSkinCount());
    line[0] = '\0';
    for (k = 0; k < portBotSkinCount(); k++)
    {
        n += snprintf(line + n, sizeof(line) - (size_t)n, "%s%s", n > 0 ? " " : "", portBotSkinName(k));
        if (n > 90 || k == portBotSkinCount() - 1)
        {
            portConLogf("ge: bot:   %s\n", line);
            line[0] = '\0';
            n = 0;
        }
    }
}

static void botSkinCommand(const char *arg2)
{
    s32 k;

    if (arg2 == NULL || arg2[0] == '\0')
    {
        s32 src = botSkinSource();

        portConLogf("ge: bot: skin is %s (live: %s); usage: bot skin <name|random|list>\n",
                    src == SKIN_RANDOM ? "random" : src >= 0 ? portBotSkinName(src) : "(coop_bot_body)",
                    sCurSkin >= 0 ? portBotSkinName(sCurSkin) : "none");
    }
    else if (strcmp(arg2, "list") == 0)
    {
        botSkinList();
    }
    else if (botSkinParse(arg2, &k))
    {
        s32 mi = portModifierFind("bot_skin");

        sCfg.skin = k;
        if (mi >= 0 && portModifierGet(mi) != 0)
        {
            portModifierSet(mi, 0);   /* the Bot Skin row would otherwise outrank this */
        }
        portConLogf("ge: bot: skin -> %s%s\n", k == SKIN_RANDOM ? "random" : portBotSkinName(k),
                    sState == BOT_ALIVE ? " (re-skinning now)" : " (applies at the next spawn)");
        botReskinNow();
    }
    else
    {
        portConLogf("ge: bot: no skin \"%s\" - try: bot skin list\n", arg2);
    }
}

/* The `bot` console command: "" summons (and enables for this session), "off" dismisses
 * until the next `bot`, "info" prints the state, "skin <name|random|list>" changes her look. */
void portBotConsole(const char *arg, const char *arg2)
{
    static const char *const names[] = { "WAIT", "ALIVE", "DEAD", "OFF" };

    if (arg == NULL || arg[0] == '\0')
    {
        sCfg.enabled = TRUE;
        sSessionOff = FALSE;
        if (sState == BOT_OFF)
        {
            sState = BOT_WAIT;
        }
        portBotSummon();
        portConLogf("ge: bot: summon requested%s\n", botGate() == NULL ? " (no first-person mission right now; it spawns when one starts)" : "");
    }
    else if (strcmp(arg, "off") == 0)
    {
        portBotDismiss();
        sSessionOff = TRUE;
        portConLogf("ge: bot: dismissed until `bot`\n");
    }
    else if (strcmp(arg, "stats") == 0)
    {
        portArenaStatsPrint();
    }
    else if (strcmp(arg, "skin") == 0)
    {
        botSkinCommand(arg2);
    }
    else if (strcmp(arg, "info") == 0)
    {
        ChrRecord *chr = sSpawned ? chrFindByLiteralId(PORT_BOT_CHRNUM) : NULL;

        char health[24] = "";

        if (chr != NULL && sState == BOT_ALIVE && chr->maxdamage > 0.0f)
        {
            snprintf(health, sizeof(health), " health %.0f%%", 100.0f * (1.0f - chr->damage / chr->maxdamage));
        }
        portConLogf("ge: bot: info: enabled %d state %s act %s%s target chr %d engaged guards %d skin %s follow_dist %.0f loiter %d\n",
                    (int)portBotEnabled(), names[sState], sActNames[sLastAct], health, (int)sTargetNum,
                    (int)portFactionEngage()->count, sCurSkin >= 0 ? portBotSkinName(sCurSkin) : "none",
                    (double)sCfg.followDist, (int)sCfg.loiter);
    }
    else
    {
        portConLogf("ge: bot: usage: bot | bot off | bot info | bot stats | bot skin <name|random|list>\n");
    }
}

/* Bond's prop when the bot may act this tick, else NULL. */
static PropRecord *botGate(void)
{
    PropRecord *bp;

    if (!portBotEnabled())
    {
        return NULL;
    }
    if (getPlayerCount() != 1 || portCoopActive() || get_is_ramrom_flag() != 0 || get_recording_ramrom_flag() != 0)
    {
        return NULL;
    }
    if (g_CurrentPlayer == NULL || g_CameraMode != CAMERAMODE_FP)
    {
        return NULL;   /* intro/swirl/cinema/death cams: no spawning mid-cutscene */
    }
    bp = getCurrentPlayerProp();
    if (bp == NULL || bp->stan == NULL)
    {
        return NULL;   /* no stan: frontend viewer, not mission Bond */
    }
    return bp;
}

/* Bond's facing in chr radians (bondview2.c sets MP chr bodies the same way). */
static f32 botBondFacing(void)
{
    f32 a = (360.0f - g_CurrentPlayer->vv_theta) * (M_TAU_F / 360.0f);

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

/* Opposite Bond's facing: where the bot steps off, so it never lands in his path. */
static f32 botBehindBond(void)
{
    f32 a = botBondFacing() + M_PI_F;

    return (a >= M_TAU_F) ? a - M_TAU_F : a;
}

/* Bond is ducking or crouched (Facility starts him in a vent): too tight to spawn into. */
static s32 botBondCrouched(void)
{
    return g_CurrentPlayer->autocrouchpos != CROUCH_STAND || g_CurrentPlayer->ducking_height_offset < -BOT_DUCK_EPS;
}

void portBotSkinModText(int v, char *buf, int n)
{
    if (n < 1)
    {
        return;
    }
    if (v <= 0)
    {
        snprintf(buf, (size_t)n, "Default");
    }
    else if (v == 1)
    {
        snprintf(buf, (size_t)n, "Random");
    }
    else
    {
        const char *s = portBotSkinName(v - 2);

        snprintf(buf, (size_t)n, "%s", s != NULL ? s : "?");
    }
}

int portBotSkinModParse(const char *text, int *v)
{
    int k;

    if (text == NULL)
    {
        return 0;
    }
    if (strcmp(text, "default") == 0 || strcmp(text, "Default") == 0)
    {
        *v = 0;
        return 1;
    }
    if (strcmp(text, "random") == 0 || strcmp(text, "Random") == 0)
    {
        *v = 1;
        return 1;
    }
    k = portBotSkinFind(text);
    if (k < 0)
    {
        return 0;
    }
    *v = k + 2;
    return 1;
}

/* The skin source: the Bot Skin modifier (1 = random, 2+k = skin k) if set, else coop_bot_skin, else the
 * legacy coop_bot_body (when changed from Natalya), else natalya. Returns a skin index, SKIN_RANDOM, or -3
 * for the legacy body-only path. */
static s32 botSkinSource(void)
{
    static s32 modIdx = -2;
    s32 v = 0;

    if (modIdx == -2)
    {
        modIdx = portModifierFind("bot_skin");
    }
    if (modIdx >= 0)
    {
        v = portModifierGet(modIdx);
    }
    if (v == 1)
    {
        return SKIN_RANDOM;
    }
    if (v >= 2 && v - 2 < portBotSkinCount())
    {
        return v - 2;
    }
    if (sCfg.skin != SKIN_UNSET)
    {
        return sCfg.skin;
    }
    if (sCfg.body != BODY_Natalya_Skirt)
    {
        return -3;
    }
    return portBotSkinFind("natalya");
}

/* The body and head to spawn with, from the roster entry (the same pair the MP character select uses).
 * Random draws from the bot's private PRNG (seeded per stage), never the same skin twice in a row.
 * Scale: makeonebody scales a body by its own c_item_entries scale, which is what makes Oddjob short and
 * Jaws tall in MP too; the roster's pov is only the player's eye height, so nothing more to apply. */
static void botResolveSkin(s32 *body, s32 *head)
{
    s32 src = botSkinSource();
    s32 pov_b, pov_h;
    f32 pov;

    *head = -1;
    *body = sCfg.body;
    sCurSkin = -1;
    if (src == -3)
    {
        return;
    }
    if (src == SKIN_RANDOM)
    {
        s32 n = portBotSkinCount();

        do
        {
            src = (s32)(portBotRngNext(&sSkinRng) % (unsigned int)n);
        } while (n > 1 && src == sCurSkinLast);
    }
    if (src >= 0 && portMpCharBodyHead(portBotSkinMp(src), &pov_b, &pov_h, &pov))
    {
        *body = pov_b;
        *head = pov_h;
        sCurSkin = src;
        sCurSkinLast = src;
    }
}

static ChrRecord *botSpawn(PropRecord *bondprop)
{
    PortSpawnDesc d;
    ChrRecord *chr;
    s32 skinBody, skinHead;

    botResolveSkin(&skinBody, &skinHead);
    d.body = skinBody;
    d.head = skinHead;
    d.nearprop = bondprop;
    d.angle = botBehindBond();     /* portSpawnChr steps off along this: behind Bond */
    d.ailistid = GAILIST_COOP_BOT;
    d.maxdamage = 4.0f * sCfg.health;   /* the vanilla default of 4 (chrSetMaxDamage units) x coop_bot_health */
    d.chrflags_on = CHRFLAG_NO_AUTOAIM;   /* not INVINCIBLE, not COUNT_DEATH_AS_CIVILIAN */
    d.chrnum = PORT_BOT_CHRNUM;
    d.nofront = TRUE;              /* a vent or corridor behind Bond defers the spawn rather than block him */

    chr = portSpawnChr(&d);
    if (chr != NULL)
    {
        portFactionSetChr(chr, PORT_FACTION_BOND);
        portConLogf("ge: bot: %s chr %d at (%.0f %.0f %.0f), Bond at (%.0f %.0f %.0f), %d slots free, t=%d\n",
                    sSpawned ? "respawned" : "spawned", (int)chr->chrnum,
                    chr->prop->pos.x, chr->prop->pos.y, chr->prop->pos.z,
                    bondprop->pos.x, bondprop->pos.y, bondprop->pos.z, (int)chrGetNumFree(),
                    (int)sStageTicks);
        portConLogf("ge: bot: skin %s (body %d head %d), model scale %.4f, stage mempool %d bytes free\n",
                    sCurSkin >= 0 ? portBotSkinName(sCurSkin) : "(coop_bot_body)", (int)d.body, (int)d.head,
                    chr->model != NULL ? (double)chr->model->scale : 0.0, (int)mempGetBankSizeLeft(MEMPOOL_STAGE));
        sSpawned = TRUE;
        sAliveFor = 0.0f;
        portBotMemoryReset(&sMem);
        botBrainReset();
        if (botDebug())
        {
            portConLogf("ge: bot: spawn y: prop %.1f ground %.1f, Bond floor %.1f, Bond facing (%.2f %.2f)\n",
                        chr->prop->pos.y, chr->ground,
                        stanGetPositionYValue(bondprop->stan, bondprop->pos.x, bondprop->pos.z),
                        sinf(botBondFacing()), cosf(botBondFacing()));
        }
        if (g_PortBotHooks.onSpawn != NULL)
        {
            g_PortBotHooks.onSpawn(chr);
        }
    }
    return chr;
}

/* The corpse keeps its slot and chrnum until the engine removes it, so rename it to free
 * 4999 for the respawn. */
static void botOnDeath(ChrRecord *chr)
{
    if (chr != NULL)
    {
        s32 n = sCorpseNext;

        while (chrFindByLiteralId(n) != NULL)
        {
            n++;
        }
        chr->chrnum = (s16)n;
        sCorpseNext = n + 1;
        portConLogf("ge: bot: died (corpse renamed chr %d), t=%d\n", (int)n, (int)sStageTicks);
    }
    else
    {
        portConLogf("ge: bot: died (chr removed), t=%d\n", (int)sStageTicks);
    }
    portArenaNoteDeath();
    if (g_PortBotHooks.onDeath != NULL)
    {
        g_PortBotHooks.onDeath(chr);
    }
    sTel.valid = FALSE;
    portBotMemoryReset(&sMem);
    botBrainReset();
    sState = BOT_DEAD;
}

/* GE_BOT_SLOTTEST: idle fillers through portSpawnChr down to its refusal threshold,
 * anchored on Bond and then on each live chr in turn (a cramped start such as Facility's
 * only fits one or two near Bond). The bot's own spawn then hits the refusal and logs it. */
static void botSlotTest(PropRecord *bondprop)
{
    PortSpawnDesc d;
    s32 n = 0;
    s32 i = -1;   /* -1 = Bond, then chr slots */

    memset(&d, 0, sizeof(d));
    d.body = BODY_Natalya_Skirt;
    d.head = -1;
    d.ailistid = GAILIST_COOP_BOT;
    d.chrnum = -1;

    while (n < BOT_SLOTTEST_MAX && i < g_NumChrSlots && chrGetNumFree() >= PORT_SPAWN_MIN_FREE)
    {
        if (i < 0)
        {
            d.nearprop = bondprop;
        }
        else if (g_ChrSlots[i].model != NULL && g_ChrSlots[i].prop != NULL &&
                 g_ChrSlots[i].prop->stan != NULL)
        {
            d.nearprop = g_ChrSlots[i].prop;
        }
        else
        {
            i++;
            continue;
        }
        d.angle = (f32)n * 0.7853982f;
        if (portSpawnChr(&d) != NULL)
        {
            n++;
        }
        i++;
    }
    portConLogf("ge: bot: slottest filled %d (%d free)\n", (int)n, (int)chrGetNumFree());
    portSpawnReset();   /* re-arm the once-per-stage failure logs for the bot's own spawn */
}

static void botRestartTick(void)
{
    static s32 at = -2;   /* -2 unparsed, -1 off */
    static s32 fired;

    if (at == -2)
    {
        const char *e = getenv("GE_BOT_RESTART_AT");
        at = (e != NULL && e[0] >= '0' && e[0] <= '9') ? atoi(e) : -1;
    }
    if (at < 0 || fired || sStageTicks < at)
    {
        return;
    }
    fired = TRUE;
    portConLogf("ge: bot: GE_BOT_RESTART_AT: aborting mission at tick %d\n", (int)sStageTicks);
    /* options.c's Abort CONFIRM, minus deleteCurrentSelectedFolder (keep the save) */
    set_missionstate(MISSION_STATE_0);
    bossRunTitleStage();
    mission_failed_or_aborted = TRUE;
    g_PortDirectLaunchRearm = TRUE;
}

/* GE_BOT_STRAND: Bond to the waypoint pad whose distance is nearest BOT_STRAND_DIST,
 * preferring his own floor; placed like the MP respawn (bondview2.c), facing kept. */
static void botStrandBond(PropRecord *bp)
{
    waypoint *wp;
    PadRecord *best = NULL;
    f32 besterr = 1e9f;
    f32 bondfloor = stanGetPositionYValue(bp->stan, bp->pos.x, bp->pos.z);
    f32 floor;
    coord3d pos, theta;

    for (wp = g_CurrentSetup.pathwaypoints; wp != NULL && wp->padID >= 0; wp++)
    {
        PadRecord *pad = &g_CurrentSetup.pads[wp->padID];
        f32 dx = pad->pos.x - bp->pos.x;
        f32 dz = pad->pos.z - bp->pos.z;
        f32 err;

        if (pad->stan == NULL)
        {
            continue;
        }
        err = sqrtf(dx * dx + dz * dz) - BOT_STRAND_DIST;
        err = (err < 0.0f ? -err : err) + (pad->pos.y > bondfloor ? pad->pos.y - bondfloor : bondfloor - pad->pos.y);
        if (err < besterr)
        {
            besterr = err;
            best = pad;
        }
    }
    if (best == NULL)
    {
        portConLogf("ge: bot: strand: no waypoint pad\n");
        return;
    }

    floor = stanGetPositionYValue(best->stan, best->pos.x, best->pos.z);
    pos.x = best->pos.x;
    pos.y = g_CurrentPlayer->eyeheight + floor;
    pos.z = best->pos.z;
    theta = g_CurrentPlayer->field_488.theta_transform;
    g_CurrentPlayer->field_70 = floor;
    g_CurrentPlayer->stanHeight = floor;
    g_CurrentPlayer->field_6C = (s32)(floor / BOT_FIELD_6C);
    change_player_pos_to_target(&g_CurrentPlayer->field_488, &pos, best->stan);
    g_CurrentPlayer->field_488.theta_transform = theta;
    bp->pos = pos;
    g_CurrentPlayer->bondprevpos = pos;
    bp->stan = best->stan;
    g_CurrentPlayer->field_3B8.x = g_CurrentPlayer->field_488.pos.x / BOT_FIELD_3B8;
    g_CurrentPlayer->field_3B8.y = g_CurrentPlayer->field_488.pos.y / BOT_FIELD_3B8;
    g_CurrentPlayer->field_3B8.z = g_CurrentPlayer->field_488.pos.z / BOT_FIELD_3B8;
    portConLogf("ge: bot: strand: Bond moved to (%.0f %.0f %.0f), t=%d\n", pos.x, pos.y, pos.z, (int)sStageTicks);
}

/* Distance to Bond with his floor, not his eye, as the height (his prop y is eye level). */
static f32 botDistToBond(ChrRecord *self, PropRecord *bp)
{
    f32 dx = bp->pos.x - self->prop->pos.x;
    f32 dy = stanGetPositionYValue(bp->stan, bp->pos.x, bp->pos.z) - self->ground;
    f32 dz = bp->pos.z - self->prop->pos.z;

    return sqrtf(dx * dx + dy * dy + dz * dz);
}

/* chrCanSeeBond as "Bond sees the bot": its line test is symmetric enough here. It
 * stamps seen_bond_time, which the bot must not keep. */
static s32 botBondSeesBot(ChrRecord *self)
{
    s32 seen = self->seen_bond_time;
    s32 sees = chrCanSeeBond(self) ? TRUE : FALSE;

    self->seen_bond_time = seen;
    return sees;
}

/* A hook's target if usable: a live chr's prop (not the bot's) or Bond's, else NULL. */
static PropRecord *botCheckTarget(ChrRecord *self, PropRecord *tp)
{
    if (tp == NULL || tp == self->prop || tp->stan == NULL)
    {
        return NULL;
    }
    if (tp->type == PROP_TYPE_CHR)
    {
        ChrRecord *c = tp->chr;

        return (c != NULL && c->prop == tp && c->model != NULL && !chrIsDead(c)) ? tp : NULL;
    }
    if (tp->type == PROP_TYPE_VIEWER)
    {
        return (tp == getCurrentPlayerProp() && !g_CurrentPlayer->bonddead) ? tp : NULL;
    }
    return NULL;
}

static s32 botTargetId(PropRecord *target)
{
    return target->type == PROP_TYPE_CHR ? (s32)target->chr->chrnum : BOT_ID_BOND;
}

/* the TRYFacingTarget type and id that face this target */
static s32 botFaceType(PropRecord *target)
{
    return target->type == PROP_TYPE_CHR ? BOT_TARGET_CHR : BOT_TARGET_BOND;
}

static s32 botFaceId(PropRecord *target)
{
    return target->type == PROP_TYPE_CHR ? (s32)target->chr->chrnum : 0;
}

static void botFace(ChrRecord *self, PropRecord *target)
{
    check_set_actor_standing_still(self, botFaceType(target), botFaceId(target));
}

/* TRYUnknown6f's pad next to Bond (Natalya tries SIDE1, SIDE2, FRONT), then
 * guard_runs_to_pad_preset. Skipped while already heading for the same pad. */
static void botTakeFlank(ChrRecord *self)
{
    static const u8 quadrants[] = { QUADRANT_SIDE1, QUADRANT_SIDE2, QUADRANT_FRONT };
    s32 i;

    for (i = 0; i < 3; i++)
    {
        if (sub_GAME_7F033AAC(self, quadrants[i]))
        {
            if (self->padpreset1 == sFlankPad && sIssuedAct == PORT_BOTACT_TAKE_FLANK)
            {
                return;   /* same pad: running there, or standing on it */
            }
            sFlankPad = self->padpreset1;
            chrGoToPad(self, self->padpreset1, SPEED_RUN);
            return;
        }
    }
}

/* ---- idle loiter: stroll to points 1x..2x follow_dist from Bond, pause, face outward ---- */

static f32 botWrapPi(f32 a)
{
    while (a > M_PI_F)
    {
        a -= M_TAU_F;
    }
    while (a < -M_PI_F)
    {
        a += M_TAU_F;
    }
    return a;
}

/* Start the pause: stop, face nothing in particular, and choose a yaw looking away from Bond (as if
 * covering his back), preferring a direction with open space ahead. */
static void botLoiterBeginPause(ChrRecord *self, PropRecord *bp, f32 secs)
{
    static const f32 offs[] = { 0.0f, 0.7853982f, -0.7853982f, 1.5707964f, -1.5707964f };
    f32 out = atan2f(self->prop->pos.x - bp->pos.x, self->prop->pos.z - bp->pos.z);
    s32 i;

    if (self->actiontype != ACT_STAND)
    {
        chraiStopAnimation(self);
    }
    check_set_actor_standing_still(self, 0, 0);   /* face nothing: the stand action must not turn her back to Bond */
    sLt = LT_PAUSE;
    sLtTimer = secs;
    sLtFace = botWrapPi(out);
    for (i = 0; i < 5; i++)
    {
        StandTile *st = self->prop->stan;
        f32 a = out + offs[i];

        if (stanTestLineUnobstructed(&st, self->prop->pos.x, self->prop->pos.z,
                                     self->prop->pos.x + sinf(a) * BOT_LOITER_LOOK,
                                     self->prop->pos.z + cosf(a) * BOT_LOITER_LOOK,
                                     CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_PATHBLOCKER, 0.0f, 1.0f, 0.0f, 1.0f))
        {
            sLtFace = botWrapPi(a);
            break;
        }
    }
    sLtFaceSet = TRUE;
    if (botDebug())
    {
        portConLogf("ge: bot: loiter: pause %.1fs facing %.0f deg (out %.0f) at (%.0f %.0f), t=%d\n", (double)secs,
                    (double)(sLtFace * (180.0f / M_PI_F)), (double)(out * (180.0f / M_PI_F)),
                    self->prop->pos.x, self->prop->pos.z, (int)sStageTicks);
    }
}

/* Does the straight walk from her to pt pass within BOT_LOITER_CLEAR of Bond? (He is a player-collision
 * obstacle she would slide around.) */
static s32 botLoiterWalkClearsBond(ChrRecord *self, PropRecord *bp, const coord3d *pt)
{
    f32 px = self->prop->pos.x;
    f32 pz = self->prop->pos.z;
    f32 sx = pt->x - px;
    f32 sz = pt->z - pz;
    f32 len2 = sx * sx + sz * sz;
    f32 t = (len2 > BOT_EPS) ? ((bp->pos.x - px) * sx + (bp->pos.z - pz) * sz) / len2 : 0.0f;
    f32 cx, cz;

    if (t <= 0.0f)
    {
        return TRUE;   /* heading away from him: the start is the closest approach, and she may already be that near */
    }
    t = t > 1.0f ? 1.0f : t;
    cx = px + sx * t - bp->pos.x;
    cz = pz + sz * t - bp->pos.z;
    return cx * cx + cz * cz >= BOT_LOITER_CLEAR * BOT_LOITER_CLEAR;
}

/* Pick a reachable point and set off at walking speed; with none found, pause where she stands.
 * The point must pass the arena ring's stan tests from Bond (clear line, room for her), and the straight line
 * from where she stands must be clear too and miss Bond. The stroll is straight, not the waypoint graph (the
 * graph is coarse: a point 300 from Bond can mean a detour of 1000 through Bond's own waypoint): the engine's
 * "run to coordinate" action with the walking animation, which sets the pace. */
static void botLoiterPick(ChrRecord *self, PropRecord *bp)
{
    s32 tries;
    s32 nRing = 0, nNear = 0, nLine = 0;   /* why candidates failed (debug log) */

    for (tries = 0; tries < BOT_LOITER_TRIES; tries++)
    {
        f32 turn, dist, dx, dz;
        coord3d pt;
        StandTile *stan;
        StandTile *st;
        s32 clear;

        portBotLoiterPick(&sRng, sTune.followDist, &turn, &dist);
        if (!portSpawnRingPoint(bp, turn * M_TAU_F, dist, &pt, &stan))
        {
            nRing++;
            continue;   /* a wall, door or prop between him and the point, or no room there */
        }
        dx = pt.x - self->prop->pos.x;
        dz = pt.z - self->prop->pos.z;
        if (dx * dx + dz * dz < BOT_LOITER_MIN_STEP * BOT_LOITER_MIN_STEP || !botLoiterWalkClearsBond(self, bp, &pt))
        {
            nNear++;
            continue;
        }
        st = self->prop->stan;
        sub_GAME_7F03D058(self->prop, FALSE);   /* her own collision must not block the test */
        clear = stanTestLineUnobstructed(&st, self->prop->pos.x, self->prop->pos.z, pt.x, pt.z,
                                         CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_PATHBLOCKER, 0.0f, 1.0f, 0.0f, 1.0f);
        sub_GAME_7F03D058(self->prop, TRUE);
        if (!clear)
        {
            nLine++;
            continue;   /* something between her and the point */
        }
        pt.y = stanGetPositionYValue(stan, pt.x, pt.z) + (self->prop->pos.y - self->ground);
        self->lastmoveok60 = g_GlobalTimer;     /* the run-to action gives up when it thinks she has been stuck a while */
        self->invalidmove = 0;
        sub_GAME_7F024CF8(self, &pt);           /* run to coordinate ... */
        chrlvWalkingAnimationRelated(self);     /* ... at a walk: the animation sets the speed */
        self->act_runpos.eta60 = BOT_LOITER_ETA60;
        sLt = LT_WALK;
        sLtTimer = 0.0f;
        sLtPoint = pt;
        sLtFaceSet = FALSE;
        sLtPicks++;
        if (botDebug())
        {
            portConLogf("ge: bot: loiter: point %d at (%.0f %.0f), %.0f from Bond, walking (%.0f away), t=%d\n", (int)sLtPicks,
                        pt.x, pt.z, (double)dist, (double)sqrtf(dx * dx + dz * dz), (int)sStageTicks);
        }
        return;
    }
    if (botDebug())
    {
        portConLogf("ge: bot: loiter: no reachable point (ring %d, near/Bond %d, line %d), staying put, t=%d\n", (int)nRing,
                    (int)nNear, (int)nLine, (int)sStageTicks);
    }
    botLoiterBeginPause(self, bp, 1.0f);   /* look around, then try again */
}

/* One step of the walk/pause cycle, once per brain tick while the act is LOITER. */
static void botLoiterStep(ChrRecord *self, PropRecord *bp)
{
    switch (sLt)
    {
        case LT_WALK:
        {
            f32 dx = sLtPoint.x - self->prop->pos.x;
            f32 dz = sLtPoint.z - self->prop->pos.z;

            sLtTimer += sBrainDt;
            if (botDebug() && (sStageTicks % 30) < 2)
            {
                portConLogf("ge: bot: loiter: walking at (%.0f %.0f), %.0f to go, action %d, t=%d\n", self->prop->pos.x,
                            self->prop->pos.z, (double)sqrtf(dx * dx + dz * dz), (int)self->actiontype, (int)sStageTicks);
            }
            if (dx * dx + dz * dz < BOT_LOITER_ARRIVE * BOT_LOITER_ARRIVE || sLtTimer > BOT_LOITER_WALK_MAX ||
                (sLtTimer > 0.6f && self->actiontype != ACT_GOPOS && self->actiontype != ACT_RUNPOS))
            {
                if (botDebug())
                {
                    portConLogf("ge: bot: loiter: %s (%.0f from the point), t=%d\n",
                                sLtTimer > BOT_LOITER_WALK_MAX ? "walk timed out" : "arrived",
                                (double)sqrtf(dx * dx + dz * dz), (int)sStageTicks);
                }
                botLoiterBeginPause(self, bp, portBotLoiterPause(&sRng, &sTune));
            }
            break;
        }
        case LT_PAUSE:
            sLtTimer -= sBrainDt;
            if (sLtTimer <= 0.0f)
            {
                if (botDebug() && sLtFaceSet && self->model != NULL)
                {
                    portConLogf("ge: bot: loiter: pause over, facing %.0f deg (wanted %.0f), t=%d\n",
                                (double)(getsubroty(self->model) * (180.0f / M_PI_F)), (double)(sLtFace * (180.0f / M_PI_F)),
                                (int)sStageTicks);
                }
                botLoiterPick(self, bp);
            }
            break;
        default:
            botLoiterPick(self, bp);
            break;
    }
}

/* The covering turn, every sim tick (the brain only ticks about 4 times a second): ease her yaw toward
 * the pause facing. */
static void botLoiterTurn(ChrRecord *chr, f32 dt)
{
    f32 cur, diff, step;

    if (sLt != LT_PAUSE || !sLtFaceSet || chr->model == NULL || chr->actiontype != ACT_STAND)
    {
        return;
    }
    cur = getsubroty(chr->model);
    diff = botWrapPi(sLtFace - cur);
    step = BOT_TURN_RATE * dt;
    if (diff > -step && diff < step)
    {
        cur = sLtFace;
    }
    else
    {
        cur += (diff > 0.0f) ? step : -step;
    }
    setsubroty(chr->model, cur);
}

/* Re-issue an unchanged act only when it needs refreshing. Re-facing the same target
 * would restart the stand action's turn, so that waits for a new target or a lost face. */
static s32 botShouldIssue(ChrRecord *self, PortBotAct act, PropRecord *target, PropRecord *bp)
{
    if (act != sIssuedAct)
    {
        /* RUN_TO_BOND <-> TAKE_FLANK near followFar would re-plan every tick: same gate as a re-issue */
        s32 follow = (act == PORT_BOTACT_RUN_TO_BOND || act == PORT_BOTACT_TAKE_FLANK) &&
                     (sIssuedAct == PORT_BOTACT_RUN_TO_BOND || sIssuedAct == PORT_BOTACT_TAKE_FLANK);

        return !follow || sSinceIssue >= BOT_REISSUE_SECS - BOT_EPS;
    }
    switch (act)
    {
        case PORT_BOTACT_IDLE:
            return self->actiontype != ACT_STAND;
        case PORT_BOTACT_FACE_TARGET:
            return sSinceIssue >= BOT_REISSUE_SECS - BOT_EPS &&
                   (self->actiontype != ACT_STAND || self->act_stand.face_entitytype != botFaceType(target) ||
                    self->act_stand.face_entityid != botFaceId(target));
        case PORT_BOTACT_RUN_TO_BOND:
        {
            /* Re-plotting mid-route restarts it from the chr's nearest waypoint, which can
             * march it away from Bond; Natalya's script also waits until she stops. */
            f32 dx = bp->pos.x - sRunGoal.x;
            f32 dz = bp->pos.z - sRunGoal.z;

            return sSinceIssue >= BOT_REISSUE_SECS - BOT_EPS &&
                   ((self->actiontype != ACT_GOPOS && self->actiontype != ACT_RUNPOS) || sNavFailed ||
                    dx * dx + dz * dz > BOT_REPLOT_DIST * BOT_REPLOT_DIST);
        }
        case PORT_BOTACT_TAKE_FLANK:
            return sSinceIssue >= BOT_REISSUE_SECS - BOT_EPS;
        default:
            return TRUE;   /* THROW and TELEPORT are one-shots gated by portBotDecide */
    }
}

static void botIssue(ChrRecord *self, PortBotAct act, PropRecord *target, PropRecord *bp)
{
    switch (act)
    {
        case PORT_BOTACT_RUN_TO_BOND:
            /* Bond outruns a guard's run: sprint to catch up when far */
            sNavFailed = chrGoToBond(self, botDistToBond(self, bp) > sTune.catchupDist ? SPEED_SPRINT : SPEED_RUN) ? FALSE : TRUE;
            sRunGoal = bp->pos;
            break;
        case PORT_BOTACT_TAKE_FLANK:
            botTakeFlank(self);
            break;
        case PORT_BOTACT_LOITER:
            botLoiterStep(self, bp);
            break;
        case PORT_BOTACT_IDLE:
            chraiStopAnimation(self);
            break;
        case PORT_BOTACT_FACE_TARGET:
            botFace(self, target);
            break;
        case PORT_BOTACT_THROW:
            botFace(self, target);
            if (chrIsNotDeadOrShot(self) && portChrThrowKnife(self, target))
            {
                f32 dx = target->pos.x - self->prop->pos.x;
                f32 dz = target->pos.z - self->prop->pos.z;

                portArenaNoteThrow(sqrtf(dx * dx + dz * dz));
                if (target->type == PROP_TYPE_CHR)
                {
                    sTargetNum = target->chr->chrnum;
                }
            }
            break;
        case PORT_BOTACT_TELEPORT:
        {
            s32 ok = portSpawnTeleportNearAt(self, bp, botBehindBond());

            if (ok)
            {
                chraiStopAnimation(self);
            }
            portConLogf("ge: bot: teleport %s, now %.0f from Bond, y: prop %.1f ground %.1f, Bond floor %.1f, t=%d\n",
                        ok ? "done" : "no spot", botDistToBond(self, bp), self->prop->pos.y, self->ground,
                        stanGetPositionYValue(bp->stan, bp->pos.x, bp->pos.z), (int)sStageTicks);
            break;
        }
        default:
            break;
    }
}

void portBotChrTick(ChrRecord *self)
{
    PropRecord *bp = botGate();
    PortBotInputs in;
    PropRecord *target;
    PortBotAct act;
    f32 dt;

    if (bp == NULL || sState != BOT_ALIVE || self->prop == NULL || self->prop->stan == NULL ||
        chrIsDead(self))
    {
        return;
    }
    dt = (f32)(sStageTicks - sBrainTicks) / 60.0f;
    if (dt <= 0.0f)
    {
        return;   /* the spawn tick: the chr still has Bond's eye-level y until its model updates */
    }
    sBrainTicks = sStageTicks;
    sBrainDt = dt;

    if (sTargetNum >= 0)
    {
        ChrRecord *t = chrFindByLiteralId(sTargetNum);

        if (t == NULL || chrIsDead(t))
        {
            if (botDebug())
            {
                portConLogf("ge: bot: knife target chr %d is down, t=%d\n", (int)sTargetNum, (int)sStageTicks);
            }
            sTargetNum = -1;
        }
    }

    target = botCheckTarget(self, (g_PortBotHooks.pickTarget != NULL) ? g_PortBotHooks.pickTarget(self) : NULL);
    memset(&in, 0, sizeof(in));
    in.alive = 1;
    in.dt = dt;
    in.distToBond = botDistToBond(self, bp);
    in.bondSeesBot = botBondSeesBot(self);
    in.navFailed = sNavFailed;
    in.bondX = bp->pos.x;
    in.bondZ = bp->pos.z;
    in.hasTarget = target != NULL;
    if (target != NULL)
    {
        f32 dx = target->pos.x - self->prop->pos.x;
        f32 dy = target->pos.y - self->prop->pos.y;
        f32 dz = target->pos.z - self->prop->pos.z;

        in.targetDist = sqrtf(dx * dx + dy * dy + dz * dz);
        in.targetVisible = 1;   /* the default pickTarget already required line of sight */
    }

    act = portBotDecide(&in, &sTune, &sMem);
    if (act == PORT_BOTACT_TELEPORT && !sTeleport)
    {
        act = PORT_BOTACT_RUN_TO_BOND;   /* no warping: the sprint is the catch-up */
    }
    botTelemetryCapture(self, &in, act, target);
    if (sHold > 0.0f)
    {
        sHold -= dt;
        sLastAct = act;
        sLt = LT_NONE;
        return;   /* the unblock step-back is running */
    }
    if (act != PORT_BOTACT_LOITER && sLt != LT_NONE)
    {
        sLt = LT_NONE;   /* Bond moved off or a target showed up: the stroll ends at once (the act switch below stops her) */
        sLtFaceSet = FALSE;
        if (botDebug())
        {
            portConLogf("ge: bot: loiter: stopped (act %s), t=%d\n", sActNames[act], (int)sStageTicks);
        }
    }
    if (target != NULL && target->type == PROP_TYPE_CHR && in.targetDist > sTune.knifeRange &&
        in.distToBond <= sTune.leash && (act == PORT_BOTACT_IDLE || act == PORT_BOTACT_TAKE_FLANK))
    {
        /* visible but out of knife range, inside the leash: close in (the throw needs targetDist <= knifeRange) */
        sSinceIssue += dt;
        if (sIssuedAct != BOT_ACT_APPROACH || sSinceIssue >= BOT_REISSUE_SECS - BOT_EPS)
        {
            chrGoToChr(self, target->chr->chrnum, SPEED_RUN);
            sIssuedAct = BOT_ACT_APPROACH;
            sSinceIssue = 0.0f;
        }
        sLastAct = act;
        return;
    }
    if (act != PORT_BOTACT_RUN_TO_BOND)
    {
        sNavFailed = FALSE;   /* only a failing run feeds the catch-up timer */
    }
    sSinceIssue += dt;
    if ((act == PORT_BOTACT_FACE_TARGET || act == PORT_BOTACT_THROW) && target == NULL)
    {
        act = PORT_BOTACT_IDLE;   /* cannot happen with hasTarget = 0; keeps the switch safe */
    }

    if (botDebug() && act != sLastAct)
    {
        portConLogf("ge: bot: act %s dist %.0f target %d, t=%d\n", sActNames[act], in.distToBond,
                    target != NULL ? (int)botTargetId(target) : -1, (int)sStageTicks);
    }
    if (botShouldIssue(self, act, target, bp))
    {
        botIssue(self, act, target, bp);
        sIssuedAct = act;
        sSinceIssue = 0.0f;
        if (g_PortBotHooks.onAct != NULL)
        {
            g_PortBotHooks.onAct(self, act);
        }
    }
    sLastAct = act;

    sStatusIn -= dt;
    if (botDebug() && sStatusIn <= BOT_EPS)
    {
        ChrRecord *h = portFactionNearestHostile(self, 100000.0f, FALSE);

        s32 nrange, nlos;

        sStatusIn = BOT_STATUS_SECS;
        portFactionCountHostile(self, sTune.knifeRange, &nrange, &nlos);
        portConLogf("ge: bot: targeting: %d hostiles in %.0f, %d with LOS, picked %d; distToBond %.0f (leash %.0f), "
                    "targetDist %.0f, sinceThrow %.1f, t=%d\n",
                    nrange, sTune.knifeRange, nlos, in.hasTarget, in.distToBond, sTune.leash, in.targetDist,
                    sMem.sinceThrow, (int)sStageTicks);
        if (h != NULL)
        {
            portConLogf("ge: bot: status nearest hostile chr %d at (%.0f %.0f %.0f)\n", (int)h->chrnum,
                        h->prop->pos.x, h->prop->pos.y, h->prop->pos.z);
        }
        portConLogf("ge: bot: status act %s dist %.0f nav %s bondsees %d actiontype %d far %.1f "
                    "at (%.0f %.0f %.0f) ground %.0f, Bond (%.0f %.0f %.0f), t=%d\n",
                    sActNames[act], in.distToBond, sNavFailed ? "FAIL" : "ok", in.bondSeesBot,
                    (int)self->actiontype, sMem.farFor, self->prop->pos.x, self->prop->pos.y, self->prop->pos.z,
                    self->ground, bp->pos.x, bp->pos.y, bp->pos.z, (int)sStageTicks);
    }
}

/* The bot right in front of Bond while he pushes forward for BOT_BLOCK_SECS: run it to a pad
 * behind him (no warp). */
static void botUnblock(ChrRecord *chr, PropRecord *bp, f32 dt)
{
    f32 dx = chr->prop->pos.x - bp->pos.x;
    f32 dz = chr->prop->pos.z - bp->pos.z;
    f32 d = sqrtf(dx * dx + dz * dz);
    f32 face = botBondFacing();
    s32 blocking = d < BOT_BLOCK_DIST && d > BOT_EPS &&
                   (dx * sinf(face) + dz * cosf(face)) / d > BOT_BLOCK_DOT &&
                   g_CurrentPlayer->speedforwards > BOT_PUSH_SPEED;

    sPushFor = blocking ? sPushFor + dt : 0.0f;
    if (sPushFor > BOT_BLOCK_SECS)
    {
        s32 ok = sub_GAME_7F033AAC(chr, QUADRANT_BACK);   /* a pad behind Bond, as TAKE_FLANK picks side pads */

        sPushFor = 0.0f;
        if (ok)
        {
            chrGoToPad(chr, chr->padpreset1, SPEED_SPRINT);
            sIssuedAct = PORT_BOTACT_TAKE_FLANK;
            sFlankPad = chr->padpreset1;
            sSinceIssue = 0.0f;
            sHold = BOT_UNBLOCK_HOLD;
        }
        if (botDebug())
        {
            portConLogf("ge: bot: unblock: %s, t=%d\n", ok ? "sprinting to a pad behind Bond" : "no pad behind Bond",
                        (int)sStageTicks);
        }
    }
}

/* GE_BOT_SKINTEST=1: spawn every roster skin in turn (BOT_SKINTEST_SECS each) and log whether it loaded,
 * with the stage mempool left and the model scale (the spawn line). Finds the skins that cannot load safely. */
static void botSkinTest(f32 dt)
{
    static s32 on = -1;
    static s32 started;
    static s32 k;
    static f32 t;

    if (on < 0)
    {
        on = envOn("GE_BOT_SKINTEST");
    }
    if (!on || k >= portBotSkinCount())
    {
        return;
    }
    if (!started)
    {
        started = TRUE;
        sCfg.skin = 0;
        t = 0.0f;
        return;
    }
    t += dt;
    if (t < BOT_SKINTEST_SECS)
    {
        return;
    }
    t = 0.0f;
    portConLogf("ge: bot: skintest: %d %s %s (state %d, %d slots free, mempool %d)\n", (int)k, portBotSkinName(k),
                (sState == BOT_ALIVE && sCurSkin == k && portBotChr() != NULL) ? "OK" : "FAIL", (int)sState,
                (int)chrGetNumFree(), (int)mempGetBankSizeLeft(MEMPOOL_STAGE));
    k++;
    if (k >= portBotSkinCount())
    {
        portConLogf("ge: bot: skintest: done\n");
        sCfg.skin = SKIN_UNSET;
        return;
    }
    sCfg.skin = k;
    botReskinNow();
}

void portBotTick(void)
{
    PropRecord *bp;
    ChrRecord *chr;
    f32 dt;

    if (sState == BOT_ALIVE && !portBotEnabled())
    {
        /* switched off mid-mission: remove her, or she stays frozen and guards keep engaging her */
        portBotDismiss();
        sState = BOT_WAIT;   /* switching it back on spawns her again (the held chrnum just delays that) */
        sWait = BOT_AUTOSPAWN_SECS;
        portConLogf("ge: bot: disabled, dismissed\n");
        return;
    }
    bp = botGate();

    if (bp == NULL)
    {
        return;
    }
    dt = (f32)g_ClockTimer / 60.0f;   /* 0 on zero-delta sub-ticks */
    sStageTicks += g_ClockTimer;
    portFactionTick(dt);
    portArenaTick(dt, bp);
    if (sState == BOT_ALIVE && sTelPath != NULL)
    {
        sTelIn += dt;
        if (sTelIn >= BOT_TEL_SECS - BOT_EPS)
        {
            sTelIn = 0.0f;
            botTelemetryRow(chrFindByLiteralId(PORT_BOT_CHRNUM));
        }
    }

    if (!sSlotTestDone)
    {
        sSlotTestDone = TRUE;
        if (envOn("GE_BOT_SLOTTEST"))
        {
            botSlotTest(bp);
        }
    }
    botRestartTick();

    if (sSummon)
    {
        chr = portBotChr();
        if (chr != NULL)
        {
            sSummon = FALSE;
            portConLogf("ge: bot: summon: %s\n", portSpawnTeleportNearAt(chr, bp, botBehindBond()) ? "teleported" : "no spot");
        }
        else if (sState != BOT_ALIVE)
        {
            sSummon = FALSE;
            sState = BOT_WAIT;
            sWait = 0.0f;
        }
        /* else ALIVE with a dead chr: the death is handled below, summon next tick */
    }

    botSkinTest(dt);

    switch (sState)
    {
        case BOT_ALIVE:
            chr = chrFindByLiteralId(PORT_BOT_CHRNUM);
            if (chr == NULL || chrIsDead(chr))
            {
                botOnDeath(chr);
                break;
            }
            sAliveFor += dt;
            if (sAliveFor >= BOT_SELFKILL_SECS - BOT_EPS && sAliveFor - dt < BOT_SELFKILL_SECS - BOT_EPS &&
                envOn("GE_BOT_SELFKILL"))
            {
                portConLogf("ge: bot: selfkill, t=%d\n", (int)sStageTicks);
                chrlvExplosionDamage(chr, &bp->pos, chr->maxdamage + 1.0f, TRUE);
            }
            botUnblock(chr, bp, dt);
            botLoiterTurn(chr, dt);
            if (!sStrandDone && sAliveFor >= BOT_STRAND_SECS - BOT_EPS && envOn("GE_BOT_STRAND"))
            {
                sStrandDone = TRUE;
                botStrandBond(bp);
            }
            break;

        case BOT_DEAD:
        {
            PortBotInputs in;

            memset(&in, 0, sizeof(in));
            in.alive = 0;
            in.dt = dt;
            if (portBotDecide(&in, &sTune, &sMem) != PORT_BOTACT_RESPAWN)
            {
                break;
            }
            sState = BOT_WAIT;
            sWait = 0.0f;
        }
            /* fall through: respawn now */

        case BOT_WAIT:
            sWait -= dt;
            if (sWait > BOT_EPS)
            {
                break;
            }
            if (botBondCrouched())
            {
                if (botDebug())
                {
                    portConLogf("ge: bot: spawn deferred, Bond is crouched, t=%d\n", (int)sStageTicks);
                }
                sWait = BOT_RETRY_SECS;
                break;
            }
            if (botSpawn(bp) != NULL)
            {
                sState = BOT_ALIVE;
            }
            else
            {
                sWait = BOT_RETRY_SECS;
            }
            break;

        default:   /* BOT_OFF */
            break;
    }
}

#endif /* TARGET_N64 */
