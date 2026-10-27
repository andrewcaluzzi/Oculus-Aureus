/* port_coop.c - two Bonds in one solo stage (port_coop.h).
 *
 * GE_COOP=1 makes the next solo mission stage allocate two player structs. getPlayerCount() (player.c)
 * then reports 1, the mode value every solo/MP test expects, while portAllPlayerCount() reports 2
 * for the sites that iterate or size per player. With GE_COOP unset nothing here changes any value. */
#ifndef TARGET_N64

#include <stdio.h>
#include <stdlib.h>
#include <ultra64.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include "game/player.h"
#include "game/chraction.h"
#include "game/explosion.h"   /* struct Explosion, EXPLOSION_BUFFER_LEN (the state hash) */
#include "port_coop.h"
#include "port_net.h"
#include "port_modifier.h"
#include "port_cooplogic.h"
#include "port_snap.h"        /* g_PortResim, portSnapLive */

void portCoopReseed(unsigned int seed);

extern s32 gamemode;
extern s32 get_is_ramrom_flag(void);
extern s32 get_recording_ramrom_flag(void);
extern bool bondinvCheckHasKeyFlags(u32 wantkeyflags);   /* bondinv.c, in no header */
extern TICKOP propdoorInteract(PropRecord *doorprop);     /* propobj.c, in no header */

static int sCoopOn;
static int sLocal;
static void pupReset(void);
static void keyTestTick(void);
static void deathReset(void);
static void sessionLog(void);
void portCoopSessionApplyText(const char *text);
static unsigned worldHash(void);
int portCoopStateHash(PortNetHashMsg *m, char *dump, int cap);
void portCoopFpLog(const char *fmt, ...);
static int sFieldNow;                /* game fields (g_ClockTimer summed) since the stage began */

/* ---- job CO-OP-EXITS: session settings, lives, held-at-the-exit state (all sim state, reset every stage in deathReset) ---------------
 * Session settings are the HOST's in netplay (SETUP carries them, port_net.c); local co-op reads the modifiers. */
static struct { int exits; int respawn; int lives; int livesMode; } sSess;   /* coop_exits (0 both / 1 first), coop_respawn (0 start / 1 partner), coop_lives (0 = unlimited), coop_lives_mode (0 player / 1 party) */
static int sLivesLeft[4];            /* lives left per Bond (player mode) */
static int sPartyLives;              /* lives left in the pool (party mode) */
static u8 sOut[4];                   /* out of lives: stays down and watches */
static u8 sCharged[4];               /* this death has been charged against the lives */
static u8 sHeld[4];                  /* held at the level exit: invincible, cannot act */
static int sExitOpen;                /* the exit gate was released: the ending runs */
static int sPartnerGone;             /* netplay: the partner left while a Bond waited at the exit: the mission completes */
static float sStartX[4], sStartZ[4], sStartTheta[4];   /* each Bond's start spawn (snapshot at sim tick 1) */
static StandTile *sStartStan[4];
static u8 sHaveStart[4];
static u8 sPlanStart[4];             /* the respawn that is about to happen goes to the start spawn */
static u8 sWatching[4];              /* an out-of-lives Bond that watches its partner */
static float sSiteX, sSiteZ;         /* the exit's spot (HUD arrow) */
static int sSiteSet;
static int sTrigger = -1;            /* the Bond that reached the exit first */

int portCoopActive(void)
{
    return sCoopOn;
}

int portAllPlayerCount(void)
{
    int count = 0;
    int i;

    for (i = 0; i < 4; i++) {
        if (g_playerPointers[i] != NULL) {
            count++;
        }
    }
    return count;
}

int portLocalPlayer(void)
{
    return sCoopOn ? sLocal : 0;
}

void portCoopSetLocalPlayer(int p)
{
    if (sCoopOn && p >= 0 && p < portAllPlayerCount()) {
        sLocal = p;
    }
}

int portCoopKeepBody(int playernum)
{
    (void) playernum;
    return sCoopOn;   /* both Bonds keep a body, drawn only in the other player's view */
}

int portCoopIsGuest(int playernum)
{
    return sCoopOn && playernum != 0;
}

int g_portCoopEpoch;   /* counts stage starts (chrai.c drops its per-list caches when it changes) */

int portCoopStageBegin(int stage, int players)
{
    const char *e = getenv("GE_COOP");

    g_portCoopEpoch++;
    sCoopOn = 0;
    sLocal = 0;
    if (e == NULL || e[0] == '\0' || e[0] == '0') {
        return players;
    }
    if (stage == LEVELID_TITLE || gamemode == GAMEMODE_MULTI || players != 1
        || get_is_ramrom_flag() != 0 || get_recording_ramrom_flag() != 0) {
        return players;
    }
    sCoopOn = 1;
    {   /* GE_COOP_VIEW=1: start as player 2 (the console's "coop view" does the same later) */
        const char *v = getenv("GE_COOP_VIEW");

        sLocal = (v != NULL && v[0] == '1') ? 1 : 0;
    }
    pupReset();
    if (!portNetActive()) {   /* local co-op: the modifiers (Mods page, ini, GE_MODS); netplay took the HOST's at the handshake / from the log */
        char sess[96];

        portModifierSessionText(sess, sizeof(sess));
        portCoopSessionApplyText(sess);
    }
    deathReset();
    sessionLog();
    printf("ge: coop: two Bonds on stage %d (local player %d)\n", stage, sLocal);
    {   /* the same RNG state at stage start on every machine: netplay reseeds with the session seed, a local run can ask for it */
        const char *rs = getenv("GE_COOP_RESEED");

        if (portNetActive()) {
            portNetStageBegin(stage);
        } else if (rs != NULL && rs[0] != '\0') {
            portCoopReseed((unsigned int) atol(rs));
        }
    }
    return 2;
}

/* ---- netplay job 3c: the canonical sim view --------------------------------------------------------------------------------
 * The camera model the simulation uses in a co-op stage (fr.c viSetViewSize / viSetFovY / viSetAspect fill the player's c_*
 * fields from these, never from the window): the 320x240 screen at (0,0) (the PC's default full-height view), 16:9, the
 * vanilla 60-degree zoom chain widened by 240/220 as the default view draws it. It is a superset of every window up to 16:9
 * and of the default FOV, and identical to the default PC window, so the world the local view draws is always culled with at
 * least its own frustum. The window's real aspect / FOV / screen size go to the display list only (g_ViBackData). A window
 * wider than 16:9, or a field of view wider than the default, shows the edge of the picture without animated characters
 * (the sim never marks them onscreen). */
extern s32 portStageRunning(void);
extern double tan(double);
extern double atan(double);
extern f32 portFovCfg(void);

s32 portSimViewOff(void)   /* GE_NET_TEST_NOSIMVIEW=1 (test, negative control): the pre-3c behaviour, the window feeds the sim */
{
    static int off = -1;

    if (off < 0) {
        const char *e = getenv("GE_NET_TEST_NOSIMVIEW");

        off = (e != NULL && e[0] != '\0' && e[0] != '0');
    }
    return off;
}

s32 portSimViewOn(void)
{
    return sCoopOn && !portSimViewOff() && portStageRunning();
}

f32 portSimViewAspect(void)
{
    return 16.0f / 9.0f;   /* the 320x240 view's 4:3 times the default 16:9 window's wide factor */
}

/* the zoom-chain fov as the canonical camera uses it for culling: widened in tan space by 240/220 like the default full-height
 * view the PC draws (the old lv.c rule), so the sim's frustum always contains that view */
f32 portSimModelFovY(f32 fovy)
{
    return (f32) (2.0 * atan(tan((double) fovy * 3.14159265358979323846 / 360.0) * (240.0 / 220.0)) * 180.0 / 3.14159265358979323846);
}

/* canonical zoom-chain fov -> the vertical fov the window draws: the [display] FOV setting as a tan-space factor, and the
 * 240-line full-height view's widening (the old lv.c rule). Identity at the defaults. */
f32 portSimDisplayFovY(f32 fovy, s32 realViewY)
{
    double k = 1.0;
    const double cfg = (double) portFovCfg();

    if (cfg > 1.0 && cfg < 179.0 && cfg != 60.0) {
        k = tan(cfg * 3.14159265358979323846 / 360.0) / tan(30.0 * 3.14159265358979323846 / 180.0);
    }
    if (realViewY > 220) {
        k *= (double) realViewY / 220.0;
    }
    if (k == 1.0) {
        return fovy;
    }
    return (f32) (2.0 * atan(tan((double) fovy * 3.14159265358979323846 / 360.0) * k) * 180.0 / 3.14159265358979323846);
}

/* ---- Job 2: puppet pad, view switch -------------------------------------------------------- */

#include <math.h>
#include <string.h>
#include "game/bondview.h"

extern void portConLogf(const char *fmt, ...);

enum { PUP_IDLE, PUP_WALK, PUP_FOLLOW, PUP_SPIN, PUP_REPLAY, PUP_ARENA };   /* PUP_ARENA: the arena partner bot (port_arenacoop.c) */
#include "port_arenacoop.h"
static int sPupMode;
/* Job 4: replay:<file> drives the puppet from a recorded stream (GE_COOP_REC=<file> records the local Bond).
 * Netplay job 2: GE_COOP_LOCAL=replay:<file> does the same for the local Bond. */
/* Netplay job 3b: the file may carry four more columns, "R tick buttons sx sy yaw pitch [actions dx dy flags]" (hex actions and
 * flags, decimal dx dy): Port action bits, the digital move and the record flags, so a scripted test can drive everything. */
typedef struct { u32 tick; u16 btn; s8 sx, sy; int yaw, pitch; u32 actions; s8 dx, dy; u16 flags; } RepRec;
typedef struct { RepRec *rec; int n, at; } Replay;
static Replay sPupRep, sLocRep;
static int sPupPitch;                /* the puppet's pitch this tick, milli-degrees (replay only) */
static int sLocPitch;                /* the scripted local Bond's pitch this tick, milli-degrees (replay / netplay) */
static FILE *sRecOut;                /* GE_COOP_REC: one line per tick, written at the start of the next */
static u16 sRecBtn;
static s8 sRecSx, sRecSy;
static void netTickDrive(void);
static int leakEsc(void);
static int repLoad(Replay *r, const char *path);
static const RepRec *repAt(Replay *r, u32 tick);
static u32 sPupTick;                /* sim ticks since the stage began */
static OSContPad sPup;               /* the puppet's pad for this sim tick */
static OSContPad sLoc;               /* GE_COOP_LOCAL test script: replaces the local pad (idle = real devices) */
static int sLocMode;
static f32 sDbgErr;
static int sPupYaw, sLocYaw;          /* scripted look, milli-degrees per tick */
static int sLookY[4], sLookP[4];     /* per-player look channel, milli-degrees this tick (exact integers: serialisable) */
static void pupCompute(int mode, struct player *me, struct player *tg, OSContPad *out, int *yaw);
static u16 sLocalButtons;            /* the local pad's buttons, last poll (follow mirrors its fire) */

/* Netplay job 2 (port_net.c): while lockstep is on, both Bonds' pad, look, Port actions and digital move come from
 * the scheduled records, never from live devices. The live local device state is only sampled, into sNetPend, and
 * scheduled `delay` ticks ahead. */
static int sNetDrive;               /* 1 while the scheduled records drive both Bonds */
static u32 sNetAct[2];               /* Port action bits per device slot (0 local, 1 remote) this tick */
static u16 sNetFlags[2];             /* PORT_NETF_* per PLAYER this tick: the key events the sim reads (Escape, Return-as-Start) */
static s8 sNetDig[2][2];             /* digital move per device slot this tick */
static u16 sLiveBtn;                 /* the live local pad as the last poll saw it, before any override */
static s8 sLiveSx, sLiveSy;
static PortNetInput sNetPend;        /* the live sample of the tick that is running: its look arrives from bondview2 */
extern u32 portJoyPortNetTakeWindow(void);        /* joy_port.c: Port action bits down since the last call */
extern void portJoyPortNetApply(s32 pad, u32 bits); /* joy_port.c: tick-boundary action state for a pad */
extern void portJoyPortNetZoom(s32 pad, u32 zin, u32 zout);      /* joy_port.c (job 4): the record's analog zoom levels for a pad */
extern void portJoyPortNetZoomLive(u32 *zin, u32 *zout);         /* joy_port.c (job 4): the local pad's analog zoom levels, sampled */
extern void portInputDigitalMoveLive(s32 *dx, s32 *dy); /* input_pc.c: the local digital move, live devices */

static const char *pupName(void)
{
    return sPupMode == PUP_WALK ? "walk" : sPupMode == PUP_FOLLOW ? "follow" : sPupMode == PUP_REPLAY ? "replay" : sPupMode == PUP_ARENA ? "arena" : "idle";
}

static int pupParse(const char *s)
{
    if (s != NULL && strncmp(s, "replay:", 7) == 0) {
        return PUP_REPLAY;   /* the caller loads the file with pupSetReplay (puppet only) */
    }
    if (s != NULL && strcmp(s, "walk") == 0) {
        return PUP_WALK;
    }
    if (s != NULL && strcmp(s, "spin") == 0) {
        return PUP_SPIN;   /* test: stands still and turns 3 degrees a tick, so a screenshot sees every side */
    }
    if (s != NULL && strcmp(s, "follow") == 0) {
        return PUP_FOLLOW;
    }
    if (s != NULL && strcmp(s, "arena") == 0) {
        return PUP_ARENA;   /* the arena partner bot (port_arenacoop.c; also Lua coop.bot(true)) */
    }
    return PUP_IDLE;
}

/* Lua coop.bot(on) (port_arenacoop.c): the puppet Bond becomes the arena partner bot, or idle again. Local co-op only: under
 * netplay the records drive both Bonds and the puppet is never computed. Returns 1 if the puppet is now the bot. */
int portCoopPuppetArena(int on)
{
    if (!sCoopOn || portNetActive()) {
        return 0;   /* netplay: both Bonds are people (and a bot on one machine only would desync) */
    }
    if (on) {
        sPupMode = PUP_ARENA;
    } else if (sPupMode == PUP_ARENA) {
        sPupMode = PUP_IDLE;
    }
    printf("ge: coop: puppet source %s\n", pupName());
    return sPupMode == PUP_ARENA;
}

static void pupReset(void)
{
    sPupMode = pupParse(getenv("GE_COOP_P2"));
    if (sPupMode == PUP_REPLAY && !repLoad(&sPupRep, getenv("GE_COOP_P2") + 7)) {
        sPupMode = PUP_IDLE;
    }
    sLocMode = pupParse(getenv("GE_COOP_LOCAL"));
    if (sLocMode == PUP_REPLAY && !repLoad(&sLocRep, getenv("GE_COOP_LOCAL") + 7)) {
        sLocMode = PUP_IDLE;
    }
    sLocPitch = 0;
    sNetDrive = 0;
    memset(&sNetPend, 0, sizeof(sNetPend));
    memset(sNetAct, 0, sizeof(sNetAct));
    memset(sNetDig, 0, sizeof(sNetDig));
    if (sRecOut != NULL) {
        fclose(sRecOut);
        sRecOut = NULL;
    }
    {
        const char *rec = getenv("GE_COOP_REC");

        if (rec != NULL && rec[0] != '\0') {
            sRecOut = fopen(rec, "w");
            printf("ge: coop: recording the local Bond's pad and look to %s%s\n", rec, sRecOut != NULL ? "" : " (cannot create)");
        }
    }
    sPupTick = 0;
    memset(&sPup, 0, sizeof(sPup));
    memset(&sLoc, 0, sizeof(sLoc));
    sLocalButtons = 0;
    printf("ge: coop: puppet source %s\n", pupName());
}

int portCoopPadSwap(int i)
{
    if (!sCoopOn || sLocal == 0) {
        return i;
    }
    return i == 0 ? sLocal : (i == sLocal ? 0 : i);
}

static struct player *pupPlayer(int i)
{
    return (i >= 0 && i < 4) ? g_playerPointers[i] : NULL;
}

static s8 clampStick(f32 v, f32 lim)
{
    return (s8) (v > lim ? lim : (v < -lim ? -lim : v));
}

/* GE_HASHLOG=<file> (test, solo or co-op, no netplay needed): every sim tick of a stage, "tick N hash H" and the 12 sub-hashes of
 * portCoopStateHash, taken at the start of the tick. Two runs with the same inputs must write the same file (tests/heist/run_determinism.sh).
 * GE_HASHLOG_DUMP=<from>[-<to>]: the text dump of those ticks too, to diff two runs at the first tick they part. Ticks count from 1 per stage. */
const char *portCoopStateFieldName(int i);
#define HASHLOG_TEXT 262144

static void hashLogTick(void)
{
    extern s32 g_StageNum;
    extern s32 g_ClockTimer;
    extern enum CAMERAMODE g_CameraMode;
    static int init, dumpFrom, dumpTo;
    static FILE *f;
    static u32 tick;
    static s32 stage = -1;
    static char *text;   /* (heap: a static buffer this size would sit in the rollback snapshot's sim statics) */
    PortNetHashMsg hm;
    int i;

    if (!init) {
        const char *e = getenv("GE_HASHLOG");
        const char *d = getenv("GE_HASHLOG_DUMP");

        init = 1;
        f = (e != NULL && e[0] != '\0') ? fopen(e, "w") : NULL;
        if (d != NULL && d[0] != '\0') {
            const char *dash = strchr(d, '-');

            dumpFrom = atoi(d);
            dumpTo = dash != NULL ? atoi(dash + 1) : dumpFrom;
        }
        text = (f != NULL) ? (char *) malloc(HASHLOG_TEXT) : NULL;
    }
    if (f == NULL || text == NULL || g_PortResim) {
        return;
    }
    if (!portStageRunning()) {
        stage = -1;
        return;
    }
    if (sCoopOn) {   /* co-op / netplay: the session's own tick number (the one about to run), so two machines' logs line up */
        if (sPupTick + 1u == tick && stage == g_StageNum) {
            return;   /* (a held lockstep tick: no sim tick ran) */
        }
    }
    if (stage != g_StageNum) {
        stage = g_StageNum;
        tick = 0;
        fprintf(f, "stage %d\n", (int) stage);
        if (!portNetLockActive() && getenv("GE_NET_RNGTRACE") != NULL) {   /* the RNG trace of a local run (port_net.c), "local" */
            extern void portNetRngTraceOpenLocal(void);

            portNetRngTraceOpenLocal();
        }
    }
    tick = sCoopOn ? sPupTick + 1u : tick + 1u;
    memset(&hm, 0, sizeof(hm));
    text[0] = '\0';
    {
        int dump = dumpFrom > 0 && tick >= (u32) dumpFrom && tick <= (u32) dumpTo;

        if (portCoopStateHash(&hm, text, dump ? HASHLOG_TEXT : 1) != 0) {
            return;
        }
        fprintf(f, "tick %u hash %08x clk %d cam %d", tick, hm.hash, (int) g_ClockTimer, (int) g_CameraMode);
        for (i = 0; i < PORT_NET_HASH_FIELDS; i++) {
            fprintf(f, " %s %08x", portCoopStateFieldName(i), hm.fields[i]);
        }
        fputc('\n', f);
        if (dump) {
            fprintf(f, "%s", text);
        }
    }
    fflush(f);
    fflush(stdout);   /* the port leaves through _exit, which skips stdio: a test's own log lines (Lua print) up to this tick survive */
}

void portCoopTick(void)
{
    hashLogTick();
    if (!sCoopOn) {
        return;
    }
    if (sRecOut != NULL && sPupTick > 0 && !g_PortResim) {   /* the tick that just ended: the local pad and the look it published */
        fprintf(sRecOut, "R %u %x %d %d %d %d\n", sPupTick, (unsigned) sRecBtn, (int) sRecSx, (int) sRecSy, sLookY[sLocal],
                sLookP[sLocal]);
        fflush(sRecOut);   /* the port leaves through _exit, which skips stdio */
    }
    sPupTick++;
    if (portNetLockActive()) {   /* netplay job 3: the desync self-test, and the state hash every 30th tick */
        if (portNetTestDesync(sPupTick)) {   /* GE_NET_TEST_DESYNC=<tick> on ONE side: its sim RNG drifts by one step */
            extern u64 g_randomSeed;

            g_randomSeed ^= 1u;
            portConLogf("ge: net: TEST: perturbed the sim RNG at tick %u\n", sPupTick);
        }
        if (portNetTestExtraDraw(sPupTick)) {   /* GE_NET_TEST_EXTRADRAW=<tick> on ONE side: one extra sim RNG draw (a draw-count desync, for the trace tools' test) */
            extern u32 randomGetNext(void);

            (void) randomGetNext();
            portConLogf("ge: net: TEST: one extra sim RNG draw at tick %u\n", sPupTick);
        }
        if (sPupTick % 30u == 0 && !g_PortResim) {
            portNetStateCheck(sPupTick);
        }
        if (!g_PortResim) {   /* (a re-simulated tick is not logged again: rollback R2) */
            portNetSessionTick(sPupTick);   /* job 3b: this tick's fingerprint, for the session log / the offline replay's comparison */
        }
        if (sPupTick == 1) {   /* what the sim reads for each Bond, through the real getters: both machines must print the same */
            extern int cur_player_get_control_type(void);
            extern u32 get_cur_player_look_vertical_inverted(void);
            extern s32 cur_player_get_autoaim(void);
            extern u32 cur_player_get_lookahead(void);
            extern u32 cur_player_get_aim_control(void);
            s32 saved = get_cur_playernum();
            int k;

            for (k = 0; k < 2; k++) {
                set_cur_player(k);
                portConLogf("ge: net: sim reads player %d: style %d n64 %d look-updown %u (1 = upright, 0 = invert pitch) autoaim %d lookahead %u aimctl %u aimlock %d aimsens %d\n", k,
                            cur_player_get_control_type(), portSimPadIsN64(k), get_cur_player_look_vertical_inverted(), (int) cur_player_get_autoaim(),
                            cur_player_get_lookahead(), cur_player_get_aim_control(), portNetOpt(k, PORT_NETOPT_AIMLOCK), portNetOpt(k, PORT_NETOPT_AIMSENS));
            }
            set_cur_player(saved);
        }
    }
    {   /* GE_COOP_STATEDUMP=<tick> (test, works without netplay): print the state dump at the start of that tick */
        static int dumpAt = -1;
        static int dumpTo;

        if (dumpAt < 0) {
            const char *e = getenv("GE_COOP_STATEDUMP");   /* <tick> or <from>-<to> */
            const char *dash = (e != NULL) ? strchr(e, '-') : NULL;

            dumpAt = (e != NULL && e[0] != '\0') ? atoi(e) : 0;
            dumpTo = dash != NULL ? atoi(dash + 1) : dumpAt;
        }
        if (dumpAt > 0 && sPupTick >= (u32) dumpAt && sPupTick <= (u32) dumpTo && pupPlayer(0) != NULL && pupPlayer(1) != NULL) {
            static char text[65536];
            PortNetHashMsg hm;

            memset(&hm, 0, sizeof(hm));
            portCoopStateHash(&hm, text, sizeof(text));
            portCoopFpLog("ge: coop: statedump tick %u hash %08x\n%s", sPupTick, hm.hash, text);
        }
    }
    {
        static int logEvery = -1;

        if (logEvery < 0) {
            const char *e = getenv("GE_COOP_LOG");
            logEvery = e != NULL ? atoi(e) : 0;
        }
        if (logEvery > 0 && sPupTick % (u32) logEvery == 0 && pupPlayer(0) != NULL && pupPlayer(1) != NULL
            && pupPlayer(0)->prop != NULL && pupPlayer(1)->prop != NULL) {
            extern u64 g_randomSeed;   /* the sim RNG: a view-dependent draw shows up here first */
            extern u64 g_chrObjRandomSeed;

            portCoopFpLog("ge: coop: tick %u local %d p0 %.0f %.0f p1 %.0f %.0f th %.0f %.0f yaw %d err %.1f rng %08x %08x\n", sPupTick,
                   sLocal, pupPlayer(0)->prop->pos.x, pupPlayer(0)->prop->pos.z, pupPlayer(1)->prop->pos.x,
                   pupPlayer(1)->prop->pos.z, pupPlayer(0)->vv_theta, pupPlayer(1)->vv_theta, sPupYaw, sDbgErr,
                   (unsigned) g_randomSeed, (unsigned) g_chrObjRandomSeed);
            if (getenv("GE_COOP_FP") != NULL) {   /* determinism fingerprint: whole-world hash plus life state, view independent */
                portCoopFpLog("ge: coop: fp %u world %08x dead %d%d hp %.4f %.4f fields %d watch %d %d wpn %d %d %d %d mag %d %d\n", sPupTick, worldHash(),
                       (int) pupPlayer(0)->bonddead, (int) pupPlayer(1)->bonddead, pupPlayer(0)->bondhealth, pupPlayer(1)->bondhealth, sFieldNow,
                       (int) pupPlayer(0)->watch_animation_state, (int) pupPlayer(1)->watch_animation_state,
                       (int) pupPlayer(0)->hands[GUNRIGHT].weaponnum, (int) pupPlayer(0)->hands[GUNLEFT].weaponnum,
                       (int) pupPlayer(1)->hands[GUNRIGHT].weaponnum, (int) pupPlayer(1)->hands[GUNLEFT].weaponnum,
                       (int) pupPlayer(0)->hands[GUNRIGHT].weapon_ammo_in_magazine, (int) pupPlayer(1)->hands[GUNRIGHT].weapon_ammo_in_magazine);
            }
            if (getenv("GE_COOP_STATE") != NULL) {   /* per-player sim state, to find what a view switch changes */
                extern u8 g_CheatPlayerTextRelated[];
                int k;

                printf("ge: coop: cheat bits (bit n = player n): invincible 0x%x all_guns 0x%x turbo 0x%x\n",
                       g_CheatPlayerTextRelated[CHEAT_INVINCIBILITY], g_CheatPlayerTextRelated[CHEAT_ALLGUNS],
                       g_CheatPlayerTextRelated[CHEAT_TURBO_MODE]);
                for (k = 0; k < 2; k++) {
                    struct player *q = pupPlayer(k);

                    printf("ge: coop: state %u p%d hp %.4f arm %.4f shot %.3f %.3f dst %.1f hst %.1f vel %.3f %.3f\n", sPupTick, k,
                           q->bondhealth, q->bondarmour, q->bondshotspeed.x, q->bondshotspeed.z, (double) q->damageshowtime,
                           (double) q->healthshowtime, q->prop->pos.y, q->vv_verta);
                }
            }
        }
    }
    {   /* GE_COOP_CHR=n (with GE_COOP_LOG): also print chr n's AI state each logged tick, to compare two runs */
        static int dbgChr = -2;

        if (dbgChr == -2) {
            const char *e = getenv("GE_COOP_CHR");
            dbgChr = e != NULL ? atoi(e) : -1;
        }
        if (dbgChr >= 0 && getenv("GE_COOP_LOG") != NULL) {
            extern ChrRecord *g_ChrSlots;
            extern s32 get_numguards(void);
            s32 k;

            for (k = 0; k < get_numguards(); k++) {
                ChrRecord *c = &g_ChrSlots[k];

                if (c->chrnum == dbgChr && c->prop != NULL) {
                    printf("ge: coop: chr %d tick %u act %d onscr %d sleep %d seen60 %d bsum %.4f pos %.1f %.1f\n", dbgChr, sPupTick,
                           (int) c->actiontype, (c->prop->flags & PROPFLAG_ONSCREEN) != 0, (int) c->sleep, (int) c->lastseetarget60,
                           c->shotbondsum, c->prop->pos.x, c->prop->pos.z);
                }
            }
        }
    }
    keyTestTick();
    sNetDrive = portNetLockActive();
    if (sNetDrive) {
        netTickDrive();
        return;
    }
    pupCompute(sPupMode, pupPlayer(1 - sLocal), pupPlayer(sLocal), &sPup, &sPupYaw);
    pupCompute(sLocMode, pupPlayer(sLocal), pupPlayer(1 - sLocal), &sLoc, &sLocYaw);
}

/* ---- netplay job 2: the scheduled records drive both Bonds ---------------------------------- */

unsigned int portCoopNetTick(void)
{
    return sPupTick;
}

/* The scripted local record for tick `tick`: a GE_COOP_LOCAL=replay:<file> test source (so a netplay run needs no human),
 * else neutral. Only used for the prefill and for what a scripted run schedules. */
static void netScripted(u32 tick, PortNetInput *out)
{
    const RepRec *r = (sLocMode == PUP_REPLAY) ? repAt(&sLocRep, tick) : NULL;

    memset(out, 0, sizeof(*out));
    if (r != NULL) {
        out->buttons = r->btn;
        out->stickX = r->sx;
        out->stickY = r->sy;
        out->yawMilli = r->yaw;
        out->pitchMilli = r->pitch;
        out->actions = r->actions;
        out->digX = r->dx;
        out->digY = r->dy;
        out->flags = r->flags;
    }
}

void portCoopNetPrefill(unsigned int tick, PortNetInput *out)
{
    netScripted(tick, out);
}

int portCoopNetDriven(void)
{
    return sNetDrive && sCoopOn;
}

/* GE_NET_TEST_LEAK=esc (test only, set on ONE side): puts the pre-job-3b live Escape read back into the sim, as a canary: the
 * leak test (net_lockstep.sh phase 7) must then report DESYNC, which proves it can see a leak. */
static int leakEsc(void)
{
    static int v = -1;

    if (v < 0) {
        const char *e = getenv("GE_NET_TEST_LEAK");

        v = (e != NULL && strstr(e, "esc") != NULL) ? 1 : 0;
    }
    return v;
}

/* Escape for the sim (bondview2.c, opens / closes the watch). Under lockstep it is the current player's record flag, consumed
 * like the latch it replaces; else the local player's key as before. */
s32 portSimEscPressed(void)
{
    extern s32 portEscPressed(void);
    const int p = get_cur_playernum();

    if (portCoopNetDriven()) {
        if (p >= 0 && p < 2 && (sNetFlags[p] & PORT_NETF_ESC)) {
            sNetFlags[p] &= (u16) ~PORT_NETF_ESC;
            return 1;
        }
        return leakEsc() && p == portLocalPlayer() && portEscPressed();
    }
    return p == portLocalPlayer() && portEscPressed();
}

/* "This tick's START came from the Return key alone" for the sim (bondview2.c and the watch in options.c): the current player's
 * record flag under lockstep, else this machine's keyboard. Not consumed: two sites ask in the same tick. */
s32 portSimReturnStart(void)
{
    extern s32 portReturnStartThisTick(void);
    const int p = get_cur_playernum();

    if (portCoopNetDriven()) {
        return p >= 0 && p < 2 && (sNetFlags[p] & PORT_NETF_RETSTART) != 0;
    }
    return portReturnStartThisTick();
}

/* Rollback R2: the live samples (the local devices read for the record scheduled ahead) are not sim state: a restore must not rewind them. */
__attribute__((constructor)) static void netLiveRegister(void)
{
    portSnapLive("coop.netpend", &sNetPend, sizeof(sNetPend));
    portSnapLive("coop.livebtn", &sLiveBtn, sizeof(sLiveBtn));
    portSnapLive("coop.livesx", &sLiveSx, sizeof(sLiveSx));
    portSnapLive("coop.livesy", &sLiveSy, sizeof(sLiveSy));
    portSnapLive("coop.localbuttons", &sLocalButtons, sizeof(sLocalButtons));
    portSnapLive("coop.reploc.at", &sLocRep.at, sizeof(sLocRep.at));    /* a search cursor into the scripted records, not state */
    portSnapLive("coop.reppup.at", &sPupRep.at, sizeof(sPupRep.at));
}

void portCoopNetLookCapture(int yawMilli, int pitchMilli)
{
    if (g_PortResim) {
        return;   /* a re-simulated tick must not overwrite the live look sample */
    }
    sNetPend.yawMilli = yawMilli;
    sNetPend.pitchMilli = pitchMilli;
}

/* The device slot's digital move from the scheduled record (slot 0 = local Bond, 1 = the partner); 0 = not netplay. */
int portCoopNetDigital(int slot, int *dx, int *dy)
{
    if (!sNetDrive || slot < 0 || slot > 1) {
        return 0;
    }
    *dx = sNetDig[slot][0];
    *dy = sNetDig[slot][1];
    return 1;
}

/* One sim tick under lockstep: push the sample taken during the previous tick (scheduled `delay` ticks ahead), take
 * this tick's two records, sample the live local devices for a later tick. */
static void netTickDrive(void)
{
    PortNetInput loc, rem;
    struct player *pl = pupPlayer(sLocal);
    struct player *pr = pupPlayer(1 - sLocal);

    if (sPupTick >= 2 && !portNetReplaying() && !g_PortResim) {   /* (a re-simulated tick pushes nothing: the record is already scheduled) */
        PortNetInput out = sNetPend;

        if (sLocMode == PUP_REPLAY) {   /* test source: the replay's record for the tick this one will be applied at */
            netScripted(sPupTick - 1u + (u32) portNetDelay(), &out);
        }
        portNetPushLocal(&out);
    }
    if (!portNetTickInputs(sPupTick, &loc, &rem)) {
        memset(&loc, 0, sizeof(loc));
        memset(&rem, 0, sizeof(rem));
    }
    /* the session log: both players' records as the lock handed them out (before the dead-Bond neutralising below) */
    if (!g_PortResim) {
        portNetSessionRecords(sPupTick, sLocal == 0 ? &loc : &rem, sLocal == 0 ? &rem : &loc);
    }
    if (pl == NULL || pl->bonddead || sHeld[sLocal]) {   /* a dead Bond's (or one held at the exit) pad, look, Port actions and key events are neutral, as for a replayed puppet */
        loc.buttons = 0;
        loc.stickX = loc.stickY = 0;
        loc.yawMilli = loc.pitchMilli = 0;
        loc.actions = 0;
        loc.digX = loc.digY = 0;
        loc.flags = 0;
    }
    if (pr == NULL || pr->bonddead || sHeld[1 - sLocal]) {
        rem.buttons = 0;
        rem.stickX = rem.stickY = 0;
        rem.yawMilli = rem.pitchMilli = 0;
        rem.actions = 0;
        rem.digX = rem.digY = 0;
        rem.flags = 0;
    }
    if (portArenaCoopDowned(sLocal)) {   /* arena co-op: a downed Bond can look around, nothing more */
        loc.buttons = 0;
        loc.stickX = loc.stickY = 0;
        loc.actions = 0;
        loc.digX = loc.digY = 0;
    }
    if (portArenaCoopDowned(1 - sLocal)) {
        rem.buttons = 0;
        rem.stickX = rem.stickY = 0;
        rem.actions = 0;
        rem.digX = rem.digY = 0;
    }
    sNetFlags[sLocal] = loc.flags;
    sNetFlags[1 - sLocal] = rem.flags;
    sLoc.button = loc.buttons;
    sLoc.stick_x = loc.stickX;
    sLoc.stick_y = loc.stickY;
    sLocYaw = loc.yawMilli;
    sLocPitch = loc.pitchMilli;
    sPup.button = rem.buttons;
    sPup.stick_x = rem.stickX;
    sPup.stick_y = rem.stickY;
    sPupYaw = rem.yawMilli;
    sPupPitch = rem.pitchMilli;
    sNetAct[0] = loc.actions;
    sNetAct[1] = rem.actions;
    sNetDig[0][0] = loc.digX;
    sNetDig[0][1] = loc.digY;
    sNetDig[1][0] = rem.digX;
    sNetDig[1][1] = rem.digY;
    portJoyPortNetApply(0, loc.actions);   /* pad 0 is the local Bond's, pad 1 the partner's (portCoopPadSwap) */
    portJoyPortNetApply(1, rem.actions);
    portJoyPortNetZoom(0, PORT_NETF_ZIN(loc.flags), PORT_NETF_ZOUT(loc.flags));   /* job 4: the analog zoom levels of the records */
    portJoyPortNetZoom(1, PORT_NETF_ZIN(rem.flags), PORT_NETF_ZOUT(rem.flags));

    if (g_PortResim) {
        return;   /* rollback: a re-simulated tick samples no live device (sNetPend and the other live samples are not rewound: portSnapLive) */
    }
    /* the live local devices, sampled now for the record `delay` ticks ahead (the look joins it from bondview2) */
    memset(&sNetPend, 0, sizeof(sNetPend));
    sNetPend.buttons = sLiveBtn;
    sNetPend.stickX = sLiveSx;
    sNetPend.stickY = sLiveSy;
    sNetPend.actions = portJoyPortNetTakeWindow();
    if (!portNetReplaying()) {   /* key events the sim used to read live: carried as record flags (job 3b) */
        extern s32 portEscPressed(void);
        extern s32 portReturnStartThisTick(void);

        if (!leakEsc() && portEscPressed()) {   /* (the canary leaves the key to the sim's own read) */
            sNetPend.flags |= PORT_NETF_ESC;
        }
        if (portReturnStartThisTick()) {
            sNetPend.flags |= PORT_NETF_RETSTART;
        }
        {   /* job 4: the local pad's analog zoom levels (a trigger or stick that zooms by how far it is pushed) */
            u32 zi = 0;
            u32 zo = 0;

            portJoyPortNetZoomLive(&zi, &zo);
            sNetPend.flags |= PORT_NETF_ZOOM(zi, zo);
        }
    }
#ifdef GE_VIDEO
    {
        s32 dx = 0;
        s32 dy = 0;

        portInputDigitalMoveLive(&dx, &dy);
        sNetPend.digX = (signed char) dx;
        sNetPend.digY = (signed char) dy;
    }
#endif
}

/* Resets the sim RNG to a known state at a stage launch, so two machines that drew different numbers in the menus start
 * the stage alike. GE_COOP_RESEED=<seed> does it for a local (non-netplay) co-op run too, for the oracle comparison. */
static unsigned int sSessionSeed;
static int sSessionSeedSet;

/* The seed this stage's session was started with (netplay: the host's, per stage; a local co-op run: GE_COOP_RESEED or GE_SEED, else 1).
 * Mode scripts' math.random is derived from it (port_luavm.c), so both machines draw the same numbers. */
unsigned int portCoopSessionSeed(void)
{
    if (sSessionSeedSet) {
        return sSessionSeed;
    } else {
        const char *e = getenv("GE_SEED");

        return (e != NULL && e[0] != '\0') ? (unsigned int) atol(e) : 1u;
    }
}

void portCoopReseed(unsigned int seed)
{
    extern void randomSetSeed(u32 param_1);
    extern void chrObjRandomSetSeed(u32 param_1);

    extern u32 g_portSimRandDraws;
    extern u32 g_portChrObjRandDraws;

    sSessionSeed = seed;
    sSessionSeedSet = 1;
    randomSetSeed(seed);
    chrObjRandomSetSeed(seed);
    g_portSimRandDraws = 0;   /* job 3c: the draw counts are part of the state hash and start from the reseed */
    g_portChrObjRandDraws = 0;
    printf("ge: coop: sim RNG reseeded with %u\n", seed);
}

/* One scripted pad for `me`, chasing/ignoring `tg`; zero when the mode is idle or `me` is dead. */
static void pupCompute(int mode, struct player *me, struct player *tg, OSContPad *out, int *yaw)
{
    int *pitch = (out == &sPup) ? &sPupPitch : &sLocPitch;

    out->button = 0;
    out->stick_x = 0;
    out->stick_y = 0;
    *yaw = 0;
    *pitch = 0;
    if (me == NULL || me->bonddead || mode == PUP_IDLE || (me == pupPlayer(0) && sHeld[0]) || (me == pupPlayer(1) && sHeld[1])) {
        return;
    }
    if (portArenaCoopDowned(me == pupPlayer(0) ? 0 : 1)) {   /* arena co-op: a downed Bond cannot act */
        return;
    }
    if (mode == PUP_ARENA) {   /* the arena partner bot */
        unsigned short b = 0;
        signed char x = 0, y = 0;
        int mi = (me == pupPlayer(0)) ? 0 : 1;

        portArenaBotPad(mi, 1 - mi, &b, &x, &y, yaw, pitch);
        out->button = b;
        out->stick_x = x;
        out->stick_y = y;
        return;
    }
    if (mode == PUP_REPLAY) {
        const RepRec *r = repAt(out == &sPup ? &sPupRep : &sLocRep, sPupTick);

        if (r != NULL) {
            out->button = r->btn;
            out->stick_x = r->sx;
            out->stick_y = r->sy;
            *yaw = r->yaw;
            *pitch = r->pitch;
        }
    } else if (mode == PUP_SPIN) {
        *yaw = 3000;
    } else if (mode == PUP_WALK) {
        u32 ph = sPupTick % 300u;

        /* forward 2.5 s, strafe right 1 s, forward 1.5 s (port-mode sticks strafe; the puppet never turns);
         * B (use) is tapped 2 ticks in every 240 so the puppet opens doors and picks things up on the way */
        if (ph < 150u || ph >= 210u) {
            out->stick_y = 70;
        } else {
            out->stick_x = 60;
        }
        if (ph >= 240u) {
            *yaw = 2000;   /* turn right 2 degrees a tick for 1 s (120 degrees) */
        }
        if (sPupTick % 240u < 2u) {
            out->button |= CONT_B;
        }
    } else if (mode == PUP_FOLLOW && tg != NULL && me->prop != NULL && tg->prop != NULL) {
        f32 dx = tg->prop->pos.x - me->prop->pos.x;
        f32 dz = tg->prop->pos.z - me->prop->pos.z;
        f32 dist = sqrtf(dx * dx + dz * dz);
        /* The puppet never turns (the stick strafes in port mode): it walks the world direction to the
         * target as forward + sideways stick, from its facing f = (-sin th, cos th), right r = (-f.z, f.x). */
        f32 th = me->vv_theta * (3.14159265f / 180.0f);
        f32 fx = -sinf(th);
        f32 fz = cosf(th);

        {   /* turn toward the target: facing f(th) = (-sin th, cos th), so the wanted angle is atan2(-dx, dz) */
            /* __builtin: the game's own atan2f (math_atan2f.c) shadows libm's and is not a plain atan2 */
            f32 err = __builtin_atan2f(-dx, dz) * (180.0f / 3.14159265f) - me->vv_theta;

            while (err > 180.0f) {
                err -= 360.0f;
            }
            while (err < -180.0f) {
                err += 360.0f;
            }
            if (dist > 150.0f) {
                sDbgErr = __builtin_atan2f(-dx, dz) * (180.0f / 3.14159265f);
                f32 y = err * 300.0f;   /* a third of the error per tick, milli-degrees */

                *yaw = (int) (y > 4000.0f ? 4000.0f : (y < -4000.0f ? -4000.0f : y));
            }
        }
        if (dist > 150.0f) {
            f32 fwd = (dx * fx + dz * fz) / dist;
            f32 side = (dx * -fz + dz * fx) / dist;
            f32 speed = dist > 400.0f ? 70.0f : 40.0f;

            out->stick_y = clampStick(fwd * speed, 70.0f);
            out->stick_x = clampStick(side * speed, 70.0f);
        }
        out->button = (u16) (out == &sPup ? sLocalButtons & CONT_G : 0);
    }
}

/* the local Bond's scripted look (GE_COOP_LOCAL), else 0 = use the real mouse */
int portCoopLookLocal(int *yawMilli, int *pitchMilli)
{
    if (!sCoopOn || (sLocMode == PUP_IDLE && !sNetDrive)) {
        return 0;
    }
    *yawMilli = sLocYaw;
    *pitchMilli = sLocPitch;
    return 1;
}

/* bondview2.c: the player's look this tick (the local Bond publishes it, a non-local Bond takes the puppet's) */
void portCoopLookSet(int p, int yawMilli, int pitchMilli)
{
    if (p >= 0 && p < 4) {
        sLookY[p] = yawMilli;
        sLookP[p] = pitchMilli;
    }
}

void portCoopLookTake(int p, int *yawMilli, int *pitchMilli)
{
    *yawMilli = (p >= 0 && p < 4 && p != sLocal) ? sPupYaw : 0;
    *pitchMilli = (p >= 0 && p < 4 && p != sLocal) ? sPupPitch : 0;
    if (p >= 0 && p < 4) {
        sLookY[p] = *yawMilli;
        sLookP[p] = *pitchMilli;
    }
}

void portCoopPadRoute(void *dp)
{
    OSContPad *data = (OSContPad *) dp;
    int l = sLocal;
    int o = 1 - sLocal;

    if (!sCoopOn) {
        return;
    }
    sLocalButtons = data[0].button;
    sLiveBtn = data[0].button;   /* the live pad, sampled for netplay before anything replaces it */
    sLiveSx = data[0].stick_x;
    sLiveSy = data[0].stick_y;
    if (l != 0) {
        data[l] = data[0];
    }
    if (sLocMode != PUP_IDLE || sNetDrive) {   /* test script (or the netplay record) drives the local Bond too */
        data[l].button = sLoc.button;
        data[l].stick_x = sLoc.stick_x;
        data[l].stick_y = sLoc.stick_y;
    }
    data[o].button = sPup.button;
    data[o].stick_x = sPup.stick_x;
    data[o].stick_y = sPup.stick_y;
    if (sHeld[l] || portArenaCoopDowned(l)) {   /* held at the exit (or downed in the arena): cannot act (netplay zeroes the record; this covers the live local pad) */
        data[l].button = 0;
        data[l].stick_x = data[l].stick_y = 0;
    }
    if (sHeld[o] || portArenaCoopDowned(o)) {
        data[o].button = 0;
        data[o].stick_x = data[o].stick_y = 0;
    }
    data[o].errno = 0;
    data[l].errno = 0;
    if (sNetDrive) {   /* netplay: pads 2 and 3 are the 2.x "second controller" slots; a live second pad is not in the record */
        data[2].button = 0;
        data[2].stick_x = data[2].stick_y = 0;
        data[3].button = 0;
        data[3].stick_x = data[3].stick_y = 0;
    }
    sRecBtn = data[l].button;   /* GE_COOP_REC: what the local Bond was given this poll (the last poll of a tick wins) */
    sRecSx = data[l].stick_x;
    sRecSy = data[l].stick_y;
}

/* Loads a GE_COOP_REC file ("R tick buttons stick_x stick_y yaw pitch" per tick); returns 0 when it cannot be read. */
static int repLoad(Replay *rp, const char *path)
{
    FILE *f = (path != NULL) ? fopen(path, "r") : NULL;
    char line[160];
    int cap = 0;

    free(rp->rec);
    rp->rec = NULL;
    rp->n = 0;
    rp->at = 0;
    if (f == NULL) {
        printf("ge: coop: cannot open the replay file %s\n", path != NULL ? path : "(none)");
        return 0;
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        RepRec r;
        unsigned t, b, act = 0, fl = 0;
        int sx, sy, dx = 0, dy = 0;
        int got;

        got = (line[0] == 'R') ? sscanf(line + 1, "%u %x %d %d %d %d %x %d %d %x", &t, &b, &sx, &sy, &r.yaw, &r.pitch, &act, &dx, &dy, &fl) : 0;
        if (got != 6 && got != 10) {
            continue;
        }
        r.tick = t;
        r.btn = (u16) b;
        r.sx = (s8) sx;
        r.sy = (s8) sy;
        r.actions = act;
        r.dx = (s8) dx;
        r.dy = (s8) dy;
        r.flags = (u16) fl;
        if (rp->n == cap) {
            cap = cap ? cap * 2 : 4096;
            rp->rec = (RepRec *) realloc(rp->rec, (size_t) cap * sizeof(RepRec));
        }
        rp->rec[rp->n++] = r;
    }
    fclose(f);
    printf("ge: coop: replay %s: %d ticks\n", path, rp->n);
    return rp->n > 0;
}

/* The record for exactly `tick`, or NULL (neutral). The cursor only moves forward, so ask in rising tick order. */
static const RepRec *repAt(Replay *rp, u32 tick)
{
    while (rp->at > 0 && (rp->at >= rp->n || rp->rec[rp->at].tick > tick)) {   /* (a rollback asks for an earlier tick again) */
        rp->at--;
    }
    while (rp->at < rp->n && rp->rec[rp->at].tick < tick) {
        rp->at++;
    }
    return (rp->at < rp->n && rp->rec[rp->at].tick == tick) ? &rp->rec[rp->at] : NULL;
}

void portCoopConsole(const char *a1, const char *a2)
{
    if (!sCoopOn) {
        portConLogf("coop: not active (start a solo stage with GE_COOP=1)\n");
        return;
    }
    if (a1 != NULL && strcmp(a1, "view") == 0) {
        if (a2 == NULL) {
            portCoopSetLocalPlayer(1 - sLocal);
        } else if (a2[0] == '0' || a2[0] == '1') {
            portCoopSetLocalPlayer(a2[0] - '0');
        }
        printf("ge: coop: view -> player %d\n", sLocal);
        portConLogf("coop: you are player %d\n", sLocal);
    } else if (a1 != NULL && strcmp(a1, "p2") == 0) {
        sPupMode = pupParse(a2);
        if (sPupMode == PUP_REPLAY && !repLoad(&sPupRep, a2 + 7)) {
            sPupMode = PUP_IDLE;
        }
        printf("ge: coop: puppet = %s\n", pupName());
        portConLogf("coop: puppet = %s\n", pupName());
    } else {
        int i;

        portConLogf("coop: local %d, puppet %s (player %d), tick %u\n", sLocal, pupName(), 1 - sLocal, sPupTick);
        for (i = 0; i < 2; i++) {
            struct player *p = pupPlayer(i);

            if (p != NULL && p->prop != NULL) {
                portConLogf("coop: player %d at %.0f %.0f %.0f theta %.0f dead %d\n", i, p->prop->pos.x,
                            p->prop->pos.y, p->prop->pos.z, p->vv_theta, (int) p->bonddead);
            }
        }
    }
}

#include "game/chrai.h"
#include "game/chr.h"
#include "game/propobj.h"
#include "game/bondinv.h"

int portCoopPadRoomAny(ChrRecord *self, s32 pad);
f32 portCoopPadDist(ChrRecord *self, s32 pad, int wantMax);
int portCoopAllDead(void);

/* GE_COOP_GIVEKEY=1 (test): at sim tick 60 player 0 picks up the first key in the level, so a keyed door can be tried by player 1.
 * GE_COOP_GIVEKEY=2 also logs what each Bond's door check sees (ge: coop: keycheck ...). */
static void keyTestTick(void)
{
    static int mode = -1;
    PropRecord *p;

    if (mode < 0) {
        const char *e = getenv("GE_COOP_GIVEKEY");
        mode = e != NULL ? atoi(e) : 0;
    }
    {   /* GE_COOP_PADTEST=<pad id> (test): at tick 70 log the level-script pad tests per Bond and as "any Bond" */
        static int padId = -2;

        if (padId == -2) {
            const char *e = getenv("GE_COOP_PADTEST");
            padId = e != NULL ? atoi(e) : -1;
        }
        if (padId >= 0 && sPupTick == 70) {
            extern ChrRecord *g_ChrSlots;
            ChrRecord *c = &g_ChrSlots[0];
            s32 saved = get_cur_playernum();
            int i;

            for (i = 0; i < portAllPlayerCount(); i++) {
                set_cur_player(i);
                printf("ge: coop: PADTEST player %d at %.0f %.0f %.0f dead %d: in room of pad %d = %d, distance %.0f\n", i,
                       g_playerPointers[i]->prop->pos.x, g_playerPointers[i]->prop->pos.y, g_playerPointers[i]->prop->pos.z,
                       (int) g_playerPointers[i]->bonddead, padId, check_if_actor_is_at_preset(c, padId) != 0,
                       (double) chrGetDistanceFromBondToPad(c, padId));
            }
            set_cur_player(saved);
            printf("ge: coop: PADTEST any Bond in room %d, min distance %.0f, max distance %.0f, all dead %d\n",
                   portCoopPadRoomAny(c, padId), (double) portCoopPadDist(c, padId, 0), (double) portCoopPadDist(c, padId, 1),
                   portCoopAllDead());
        }
    }
    {   /* GE_COOP_SWITCH=<tick>[,<player>] (test): at that tick the player (default 1) cycles to the next weapon */
        static int swTick = -2, swPlayer = 1;

        if (swTick == -2) {
            const char *e = getenv("GE_COOP_SWITCH");
            const char *c = e != NULL ? strchr(e, ',') : NULL;

            swTick = e != NULL ? atoi(e) : -1;
            swPlayer = c != NULL ? atoi(c + 1) : 1;
        }
        if (swTick > 0 && (int) sPupTick == swTick && pupPlayer(swPlayer) != NULL) {
            extern void advance_through_inventory(void);
            s32 saved = get_cur_playernum();

            set_cur_player(swPlayer);
            advance_through_inventory();
            set_cur_player(saved);
            printf("ge: coop: test: player %d cycled weapon at tick %u\n", swPlayer, sPupTick);
        }
    }
    if (getenv("GE_COOP_PADSCAN") != NULL && sPupTick % 20 == 0 && sPupTick >= 40 && pupPlayer(1) != NULL) {
        /* test: for every setup pad, any-Bond room test == (P0 in room || P1 in room); counts pads where only one Bond is in the room */
        extern ChrRecord *g_ChrSlots;
        ChrRecord *c = &g_ChrSlots[0];
        s32 saved = get_cur_playernum();
        int pd, split = 0, bad = 0, firstSplit = -1;

        for (pd = 0; pd < 330; pd++) {
            int r0, r1, any;
            f32 dmin, dmax, d0, d1;

            set_cur_player(0);
            r0 = check_if_actor_is_at_preset(c, pd) != 0;
            d0 = chrGetDistanceFromBondToPad(c, pd);
            set_cur_player(1);
            r1 = check_if_actor_is_at_preset(c, pd) != 0;
            d1 = chrGetDistanceFromBondToPad(c, pd);
            set_cur_player(saved);
            any = portCoopPadRoomAny(c, pd) != 0;
            dmin = portCoopPadDist(c, pd, 0);
            dmax = portCoopPadDist(c, pd, 1);
            if (any != (r0 || r1) || dmin != (d0 < d1 ? d0 : d1) || dmax != (d0 > d1 ? d0 : d1)) {
                bad++;
            }
            if (r0 != r1) {
                split++;
                if (firstSplit < 0) {
                    firstSplit = pd;
                }
            }
        }
        printf("ge: coop: PADSCAN tick %u: pads where exactly one Bond is in the room %d (first %d), any-Bond mismatches %d\n", sPupTick, split,
               firstSplit, bad);
    }
    if (mode == 0) {
        return;
    }
    if (mode >= 2) {   /* player 1 tries the first keyed door before (tick 50) and after (tick 70) player 0 holds the key */
        static PropRecord *dp;
        static u32 doorFlags;

        if (sPupTick == 50 || sPupTick == 70 || sPupTick == 90) {
            if (dp == NULL) {
                u32 keyFlags = 0;

                for (p = chrpropGetActiveTail(); p != NULL; p = p->prev) {   /* the key that tick 60 will pick up */
                    if (p->type == PROP_TYPE_OBJ && p->obj != NULL && p->obj->type == PROPDEF_KEY) {
                        keyFlags = ((KeyRecord *) p->obj)->keyflags;
                        break;
                    }
                }
                for (p = chrpropGetActiveTail(); p != NULL; p = p->prev) {   /* first door that key opens */
                    if (p->type == PROP_TYPE_DOOR && p->door != NULL && p->door->keyflags != 0
                        && (p->door->keyflags & ~keyFlags) == 0) {
                        dp = p;
                        doorFlags = p->door->keyflags;
                        break;
                    }
                }
            }
            if (dp != NULL) {
                s32 saved = get_cur_playernum();

                if (sPupTick != 90) {
                    set_cur_player(1);
                    printf("ge: coop: DOORTEST tick %u door keyflags 0x%x: player 1 check %d, trying (openstate %d pos %.2f)\n", sPupTick,
                           (unsigned) doorFlags, bondinvCheckHasKeyFlags(doorFlags) != 0, (int) dp->door->openstate,
                           (double) dp->door->openPosition);
                    propdoorInteract(dp);
                    set_cur_player(saved);
                }
                printf("ge: coop: DOORTEST tick %u door openstate %d pos %.2f activated %d\n", sPupTick, (int) dp->door->openstate,
                       (double) dp->door->openPosition, (dp->door->runtime_bitflags & RUNTIMEBITFLAG_ACTIVATED) != 0);
            } else {
                printf("ge: coop: DOORTEST no keyed door\n");
            }
        }
    }
    if (sPupTick != 60) {
        return;
    }
    for (p = chrpropGetActiveTail(); p != NULL; p = p->prev) {
        if (p->type == PROP_TYPE_OBJ && p->obj != NULL && p->obj->type == PROPDEF_KEY) {
            s32 saved = get_cur_playernum();
            KeyRecord *k = (KeyRecord *) p->obj;
            TICKOP op;

            set_cur_player(0);
            op = propPickupByPlayer(p, 0);
            propExecuteTickOperation(p, op);
            printf("ge: coop: GIVEKEY: player 0 picked up key flags 0x%x; any-Bond check %d, player 1 own check via current player...\n",
                   (unsigned) k->keyflags, bondinvCheckHasKeyFlags(k->keyflags) != 0);
            set_cur_player(1);
            printf("ge: coop: GIVEKEY: with player 1 current the check says %d\n", bondinvCheckHasKeyFlags(k->keyflags) != 0);
            set_cur_player(saved);
            return;
        }
    }
    printf("ge: coop: GIVEKEY: no key prop in this level\n");
}

/* ---- Job 3: guards and shared rules -------------------------------------------------------- */

#include "port_cooplogic.h"
#include "game/chraction.h"

/* Everything below depends only on player index order, positions and bonddead, never on
 * portLocalPlayer() or on which pass is running. Callers check portCoopActive() first. */

/* fills the per-player arrays; returns the player count */
static int bondsGather(float *x, float *y, float *z, int *dead, int *has)
{
    int n = portAllPlayerCount();
    int i;

    if (n > 4) {
        n = 4;
    }
    for (i = 0; i < n; i++) {
        struct player *p = g_playerPointers[i];

        has[i] = (p != NULL && p->prop != NULL);
        dead[i] = has[i] ? (p->bonddead != 0) : 0;
        x[i] = has[i] ? p->prop->pos.x : 0.0f;
        y[i] = has[i] ? p->prop->pos.y : 0.0f;
        z[i] = has[i] ? p->prop->pos.z : 0.0f;
    }
    return n;
}

int portCoopNearestBond(const PropRecord *near)
{
    float x[4], y[4], z[4];
    int dead[4], has[4];
    int n = bondsGather(x, y, z, dead, has);
    int k;

    for (k = 0; k < n; k++) {   /* arena co-op: the enemies leave a downed Bond alone */
        dead[k] |= portArenaCoopDowned(k);
    }
    if (near == NULL) {
        return portCoopPickNearest(n, x, y, z, dead, has, x[0], y[0], z[0]);
    }
    return portCoopPickNearest(n, x, y, z, dead, has, near->pos.x, near->pos.y, near->pos.z);
}

/* ---- escort NPCs follow the nearest living Bond, with hysteresis (job CO-OP-EXITS) -------------------------------------------------
 * A chr's AI runs as "the current player" (chrlvActionTick), which is the nearest living Bond to the chr, re-picked every tick. For an
 * escort (Natalya, Valentin) that flip-flops when the Bonds are about as far, and a follow command re-plots from scratch every flip.
 * So an escort keeps the Bond it follows until that one is dead / gone, or another living Bond is 30% closer AND 3 s have passed
 * since the last switch (port_cooplogic.c portCoopEscortPick). Sim state: fields, the same on both machines. Non-escort chrs (guards,
 * Trevelyan's conversation in Facility: left as it is by design) keep the plain nearest Bond. */
#define ESCORT_SLOTS 8
static struct { s32 chrnum; int bond; int since; } sEscort[ESCORT_SLOTS];

static int escortBody(s32 body)
{
    return body == BODY_Natalya_Skirt || body == BODY_Natalya_Jungle_Fatigues || body == BODY_Valentin_;
}

int portCoopChrIsEscort(const ChrRecord *chr)
{
    return sCoopOn && chr != NULL && escortBody(chr->bodynum);
}

static void escortReset(void)
{
    memset(sEscort, 0, sizeof(sEscort));
    {
        int i;

        for (i = 0; i < ESCORT_SLOTS; i++) {
            sEscort[i].chrnum = -1;
            sEscort[i].bond = -1;
        }
    }
}

unsigned portCoopEscortHash(unsigned h)
{
    int i;

    for (i = 0; i < ESCORT_SLOTS; i++) {
        h = (h ^ (unsigned) sEscort[i].chrnum) * 16777619u;
        h = (h ^ (unsigned) sEscort[i].bond) * 16777619u;
        h = (h ^ (unsigned) sEscort[i].since) * 16777619u;
    }
    return h;
}

/* The Bond whose view a chr's AI tick runs as (the current player during chrlvActionTick). */
int portCoopActionBond(ChrRecord *self)
{
    float x[4], y[4], z[4], d[4];
    int dead[4], has[4];
    int n, i, slot, pick;

    if (!portCoopChrIsEscort(self) || self->prop == NULL) {
        return portCoopNearestBond(self->prop);
    }
    n = bondsGather(x, y, z, dead, has);
    for (i = 0; i < n; i++) {
        float dx = x[i] - self->prop->pos.x, dy = y[i] - self->prop->pos.y, dz = z[i] - self->prop->pos.z;

        d[i] = __builtin_sqrtf(dx * dx + dy * dy + dz * dz);
    }
    slot = -1;
    for (i = 0; i < ESCORT_SLOTS; i++) {
        if (sEscort[i].chrnum == self->chrnum) {
            slot = i;
            break;
        }
        if (slot < 0 && sEscort[i].chrnum == -1) {
            slot = i;
        }
    }
    if (slot < 0) {
        return portCoopNearestBond(self->prop);
    }
    if (sEscort[slot].chrnum == -1) {
        sEscort[slot].chrnum = self->chrnum;
        sEscort[slot].bond = -1;
        sEscort[slot].since = 0;
    }
    pick = portCoopEscortPick(n, d, dead, has, sEscort[slot].bond, sFieldNow - sEscort[slot].since, PORT_COOP_ESCORT_RATIO, PORT_COOP_ESCORT_MIN_FIELDS);
    if (pick != sEscort[slot].bond) {
        portCoopFpLog("ge: coop: escort chr %d now follows player %d (was %d) at tick %u: distance p0 %.0f p1 %.0f\n", (int) self->chrnum, pick,
                      sEscort[slot].bond, sPupTick, n > 0 ? d[0] : -1.0f, n > 1 ? d[1] : -1.0f);
        sEscort[slot].bond = pick;
        sEscort[slot].since = sFieldNow;
    }
    return pick;
}

/* 1 if `chr` is one of the Bonds' own bodies. In co-op every Bond keeps a chr in the chr slots (solo Bond has none after the swirl), so
 * AI helpers that scan the slots with no team check must skip them (Jungle's Natalya picked a Bond as "a guard within 100/200/300" and
 * stayed in her attack loop shooting at it for ever instead of following). */
int portCoopChrIsBondBody(ChrRecord *chr)
{
    int i;

    static int old = -1;   /* GE_TEST_ESCORTOLD=1 (test only, negative control): the Bonds' chrs are "guards" again, as before the fix */

    if (old < 0) {
        old = getenv("GE_TEST_ESCORTOLD") != NULL;
    }
    if (!sCoopOn || chr == NULL || old) {
        return 0;
    }
    for (i = 0; i < portAllPlayerCount() && i < 4; i++) {
        struct player *p = g_playerPointers[i];

        if (p != NULL && p->prop != NULL && p->prop->chr == chr) {
            return 1;
        }
    }
    return 0;
}

/* 1 if `prop` is a Bond's own prop (its player / viewer prop) or its body's chr prop: the weapons HELD by a Bond hang below them as child
 * weapon props (co-op only), and are not loose items of the level. */
int portCoopPropIsBond(PropRecord *prop)
{
    int i;

    if (!sCoopOn || prop == NULL) {
        return 0;
    }
    for (i = 0; i < portAllPlayerCount() && i < 4; i++) {
        struct player *p = g_playerPointers[i];

        if (p == NULL || p->prop == NULL) {
            continue;
        }
        if (prop == p->prop || (p->prop->chr != NULL && prop == p->prop->chr->prop)) {
            return 1;
        }
    }
    return 0;
}

/* GE_ESCORTDBG=<chrnum> (test only): one line every 30 sim ticks for that chr right after its AI tick: the list it runs, what it does, who it
 * targets and how far each Bond is. This is what found the Jungle Natalya bug (she attacked the Bonds' own chrs). */
void portCoopEscortTrace(ChrRecord *self)
{
    static int want = -2;
    extern s32 chraiGetAIListID(void *ailist, int *isGlobal);

    if (want == -2) {
        const char *e = getenv("GE_ESCORTDBG");

        want = (e != NULL && e[0] != '\0') ? (strcmp(e, "all") == 0 ? -3 : atoi(e)) : -1;   /* "all": every escort NPC */
    }
    if (want == -1 || self == NULL || self->prop == NULL || (sPupTick % 30) != 0) {
        return;
    }
    if (want == -3 ? !portCoopChrIsEscort(self) : self->chrnum != want) {
        return;
    }
    {
        int glob = 0;
        int id = self->ailist != NULL ? (int) chraiGetAIListID(self->ailist, &glob) : -1;
        float d0 = -1.0f, d1 = -1.0f;
        int i;

        for (i = 0; i < portAllPlayerCount() && i < 2; i++) {
            struct player *p = g_playerPointers[i];

            if (p != NULL && p->prop != NULL) {
                float dx = p->prop->pos.x - self->prop->pos.x, dz = p->prop->pos.z - self->prop->pos.z;
                float d = __builtin_sqrtf(dx * dx + dz * dz);

                if (i == 0) { d0 = d; } else { d1 = d; }
            }
        }
        portCoopFpLog("ge: coop: escort chr %d tick %u list %d%s off %d act %d chrpreset %d padpreset %d pos %.0f %.0f dist p0 %.0f p1 %.0f nearest %d\n",
                      (int) self->chrnum, sPupTick, id, glob ? "g" : "", (int) self->aioffset, (int) self->actiontype, (int) self->chrpreset1,
                      (int) self->padpreset1, self->prop->pos.x, self->prop->pos.z, d0, d1, portCoopNearestBond(self->prop));
    }
}

/* "Every Bond is dead and the mission cannot go on" (the mission-lost test every KIA / failure site uses). With limited lives a Bond that is out
 * of lives stays down for good, so a survivor who then dies is NOT the end if it can still respawn: exactly one Bond waiting to respawn (not out)
 * while the others are out means that one comes back. Two waiting at once is the old "both dead at once" failure (design decision 4); none waiting
 * is the end of the run. Unlimited lives: nobody is ever out, so this is the plain all-dead test as before. */
int portCoopAllDead(void)
{
    float x[4], y[4], z[4];
    int dead[4], has[4];
    int n = bondsGather(x, y, z, dead, has);
    int i, pending = 0;

    if (!portCoopAllDeadOf(n, dead, has)) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        if (has[i] && dead[i] && !sOut[i]) {
            pending++;
        }
    }
    return pending != 1;
}

/* chrTick runs once per player pass; the simulation part must run in one pass only (the first in the
 * RNG-shuffled order, the same on both machines), the others only redraw. */
int portCoopSimPass(void)
{
    return !sCoopOn || get_player_position_in_shuffled(get_cur_playernum()) == 0;
}

int portCoopBondIsLive(int i)
{
    struct player *p = (i >= 0 && i < 4) ? g_playerPointers[i] : NULL;

    return p != NULL && p->prop != NULL && !p->bonddead;
}

/* IFBondInRoomWithPad: true if any living Bond is in the pad's room (vanilla test if none is living) */
/* Mission-end job: which Bond the exit / ending cutscene is about. A script that waits for "a Bond in the room of pad N" notes, per script chr,
 * which Bond passed; when that script then locks the controls (the start of every ending cutscene) that Bond becomes THE cutscene Bond: the one
 * the camera cuts to, whose third-person body walks out (script chr id CHR_BOND_CINEMA), in every view, on both machines. Default Bond 0. */
/* CO-OP-EXITS: the same note also says HOW the Bond reached the trigger (a pad's room, a distance to a pad, a use of an object), so the exit gate
 * can tell when the partner has reached it too. kind: 1 room of pad, 2 within `thr` units of pad, 3 used an object (no site: the held Bond's own spot). */
typedef struct { ChrRecord *chr; int bond; int kind; int pad; float thr; } HitNote;
static HitNote sHit[16];
static int sCinema;

static HitNote *hitSlot(ChrRecord *self)
{
    return &sHit[(unsigned) (((size_t) self >> 4) & 15u)];
}

static void noteHit(ChrRecord *self, int bond, int kind, int pad, float thr)
{
    HitNote *h = hitSlot(self);

    h->chr = self;
    h->bond = bond;
    h->kind = kind;
    h->pad = pad;
    h->thr = thr;
}

/* IFBondDistanceToPadLessThan passed for this script chr (chrai.c): the nearest living Bond is the one that reached the pad */
void portCoopNoteDistHit(ChrRecord *self, s32 pad, f32 thr)
{
    s32 saved = get_cur_playernum();
    int best = -1;
    f32 bd = 0.0f;
    int i;

    for (i = 0; i < portAllPlayerCount(); i++) {
        if (portCoopBondIsLive(i)) {
            f32 d;

            set_cur_player(i);
            d = chrGetDistanceFromBondToPad(self, pad);
            if (best < 0 || d < bd) {
                best = i;
                bd = d;
            }
        }
    }
    set_cur_player(saved);
    if (best >= 0) {
        noteHit(self, best, 2, (int) pad, thr);
    }
}

/* IFObjectWasActivated passed (chrai.c): the Bond who used the object is the one that reached the exit (propobj.c notes the activator) */
static int sActivator = -1;

void portCoopNoteActivator(int player)
{
    sActivator = player;
}

void portCoopNoteActivation(ChrRecord *self)
{
    if (sActivator >= 0 && portCoopBondIsLive(sActivator)) {
        noteHit(self, sActivator, 3, -1, 0.0f);
    }
}

void portCoopSetCinemaFor(ChrRecord *self)
{
    HitNote *h = hitSlot(self);
    int b = (h->chr == self && portCoopBondIsLive(h->bond)) ? h->bond : 0;

    if (!portCoopBondIsLive(b)) {
        for (b = 0; b < portAllPlayerCount() && !portCoopBondIsLive(b); b++) {
        }
        if (b >= portAllPlayerCount()) {
            b = 0;
        }
    }
    sCinema = b;
    portCoopFpLog("ge: coop: cutscene Bond is player %d (tick %u)\n", b, sPupTick);
}

int portCoopCinemaBond(void)
{
    return sCinema;
}

int portCoopPadRoomAny(ChrRecord *self, s32 pad)
{
    s32 saved = get_cur_playernum();
    int hit = 0;
    int live = 0;
    int i;

    for (i = 0; i < portAllPlayerCount(); i++) {
        if (portCoopBondIsLive(i)) {
            live = 1;
            set_cur_player(i);
            if (check_if_actor_is_at_preset(self, pad)) {
                hit = 1;
                noteHit(self, i, 1, (int) pad, 0.0f);
                break;
            }
        }
    }
    set_cur_player(saved);
    return live ? hit : check_if_actor_is_at_preset(self, pad);
}

/* IFBondDistanceToPad*: the smallest (Less) or largest (Greater) distance over the living Bonds, so
 * the test passes when any Bond satisfies it */
f32 portCoopPadDist(ChrRecord *self, s32 pad, int wantMax)
{
    s32 saved = get_cur_playernum();
    f32 best = 0.0f;
    int live = 0;
    int i;

    for (i = 0; i < portAllPlayerCount(); i++) {
        if (portCoopBondIsLive(i)) {
            f32 d;

            set_cur_player(i);
            d = chrGetDistanceFromBondToPad(self, pad);
            if (!live || (wantMax ? d > best : d < best)) {
                best = d;
            }
            live = 1;
        }
    }
    set_cur_player(saved);
    return live ? best : chrGetDistanceFromBondToPad(self, pad);
}

/* IFBondYPosLessThan: true if any living Bond is below y (vanilla current player if none is living) */
int portCoopYLessAny(f32 y)
{
    int live = 0;
    int i;

    for (i = 0; i < portAllPlayerCount(); i++) {
        if (portCoopBondIsLive(i)) {
            live = 1;
            if (g_playerPointers[i]->prop->pos.y < y) {
                return 1;
            }
        }
    }
    return live ? 0 : (getCurrentPlayerProp()->pos.y < y);
}


/* ---- Job 4: death, respawn, determinism fingerprint ---------------------------------------- */

#include <stdarg.h>

/* The per-tick fingerprint lines (tick, fp, damage, death, respawn). GE_COOP_FPFILE=<path> sends them to a file
 * so other threads' stdout lines can never split them; else they go to stdout. */
void portCoopFpLog(const char *fmt, ...)
{
    static int init;
    static FILE *fp, *fpResim;
    va_list ap;

    if (!init) {
        const char *e = getenv("GE_COOP_FPFILE");

        init = 1;
        fp = (e != NULL && e[0] != '\0') ? fopen(e, "w") : NULL;
        if (e != NULL && e[0] != '\0' && getenv("GE_SNAPRESIM") == NULL && getenv("GE_SNAPSTRESS") == NULL) {
            fpResim = NULL;
        } else if (e != NULL && e[0] != '\0') {   /* rollback R2: the fingerprint lines of re-simulated ticks go to <file>.resim, so the main file stays the normal run's */
            char path[1100];

            snprintf(path, sizeof(path), "%s.resim", e);
            fpResim = fopen(path, "w");
        }
    }
    if (g_PortResim) {
        FILE *o = fpResim != NULL ? fpResim : (fp != NULL ? fp : stdout);

        if (o == stdout) {
            fputs("[resim] ", o);
        }
        va_start(ap, fmt);
        vfprintf(o, fmt, ap);
        va_end(ap);
        if (o != stdout) {
            fflush(o);
        }
        return;
    }
    va_start(ap, fmt);
    vfprintf(fp != NULL ? fp : stdout, fmt, ap);
    va_end(ap);
    if (fp != NULL) {
        fflush(fp);   /* the port leaves through _exit */
    }
}

extern void mp_respawn_handler(void);
extern void bondviewKillCurrentPlayer(void);
extern void portCoopInvLog(const char *tag, int p);   /* port_coopdrop.c (job 3d) */
extern void portCoopLoadoutSnapshot(int p);
extern void portCoopDeathDrops(int p);
extern void portCoopDropReset(void);
extern void portCoopDropTestTick(unsigned tick);

static int sDeadSince[4];     /* sFieldNow + 1 when death was first seen by the sim, 0 while alive */
static int sFailStarted;      /* the all-dead mission-failed flow has begun */
static int sDeathCam;         /* the shared death camera has been started */
extern void bondviewSetCameraMode(s32 mode);
static int sRespawnFields = PORT_COOP_RESPAWN_FIELDS;
static u32 sDeathTicks;       /* portCoopDeathTick calls = sim ticks, for the GE_COOP_KILL test hook */

void portCoopMissionEndReset(void);

static void deathReset(void)
{
    const char *e = getenv("GE_COOP_RESPAWN");   /* test: respawn delay in fields (60 per second) */

    memset(sDeadSince, 0, sizeof(sDeadSince));
    escortReset();
    {   /* CO-OP-EXITS: lives, holds, the start spawns */
        int i;

        for (i = 0; i < 4; i++) {
            sLivesLeft[i] = sSess.lives;
        }
        sPartyLives = sSess.lives;
        memset(sOut, 0, sizeof(sOut));
        memset(sCharged, 0, sizeof(sCharged));
        memset(sHeld, 0, sizeof(sHeld));
        memset(sHaveStart, 0, sizeof(sHaveStart));
        memset(sPlanStart, 0, sizeof(sPlanStart));
        memset(sWatching, 0, sizeof(sWatching));
        sSiteSet = 0;
        sTrigger = -1;
        sExitOpen = 0;
        sPartnerGone = 0;
    }
    portCoopMissionEndReset();
    memset(sHit, 0, sizeof(sHit));
    sActivator = -1;
    sCinema = 0;
    portCoopDropReset();
    sFieldNow = 0;
    sFailStarted = 0;
    sDeathCam = 0;
    sDeathTicks = 0;
    sRespawnFields = (e != NULL && atoi(e) > 0) ? atoi(e) : PORT_COOP_RESPAWN_FIELDS;
    portArenaCoopReset();   /* arena co-op: downed / revive rules and the partner bot (port_arenacoop.c) */
}

/* The partner a dead Bond respawns next to (index, -1 if none): lowest living other Bond. Independent of the local view. */
int portCoopRespawnPartnerOf(int self)
{
    float x[4], y[4], z[4];
    int dead[4], has[4];
    int n = bondsGather(x, y, z, dead, has);

    return portCoopRespawnPartner(n, dead, has, self);
}

/* "Respawning in N" for the current pass's player: 0 unless this Bond is dead, waiting, and the other is alive. */
int portCoopRespawnSecsFor(int p)
{
    if (!sCoopOn || p < 0 || p >= 4 || sDeadSince[p] == 0 || sOut[p] || portCoopAllDead() || portArenaCoopHeld(p)) {
        return 0;
    }
    return portCoopRespawnSecs(portCoopRespawnFieldsLeft(sDeadSince[p] - 1, sFieldNow, sRespawnFields));
}

/* ==== job CO-OP-EXITS ==========================================================================================================
 * Everything below is sim state, a function of the sim tick order only (never of the local view), the same on both machines. */

/* "coop_exits=1,coop_respawn=0,coop_lives=3,coop_lives_mode=0": the session settings (netplay: the host's, from SETUP / the session log's M line) */
void portCoopSessionApplyText(const char *text)
{
    const char *p = text;

    memset(&sSess, 0, sizeof(sSess));
    while (p != NULL && *p != '\0') {
        char tok[64];
        int n = 0;
        char *eq;

        while (*p != '\0' && *p != ',' && n < (int) sizeof(tok) - 1) {
            tok[n++] = *p++;
        }
        tok[n] = '\0';
        if (*p == ',') {
            p++;
        }
        eq = strchr(tok, '=');
        if (eq != NULL) {
            int v = atoi(eq + 1);

            *eq = '\0';
            if (strcmp(tok, "coop_exits") == 0) {
                sSess.exits = v != 0;
            } else if (strcmp(tok, "coop_respawn") == 0) {
                sSess.respawn = v != 0;
            } else if (strcmp(tok, "coop_lives") == 0) {
                sSess.lives = v < 0 ? 0 : (v > 99 ? 99 : v);
            } else if (strcmp(tok, "coop_lives_mode") == 0) {
                sSess.livesMode = v != 0;
            }
        }
    }
}

static void sessionLog(void)
{
    char lives[16];

    if (sSess.lives > 0) {
        snprintf(lives, sizeof(lives), "%d", sSess.lives);
    } else {
        snprintf(lives, sizeof(lives), "unlimited");
    }
    portCoopFpLog("ge: coop: session settings: exits %s, respawn %s, lives %s (%s)\n", sSess.exits ? "first" : "both", sSess.respawn ? "partner" : "start", lives,
                  sSess.livesMode ? "party" : "player");
}

/* ---- lives ---- */

/* A Bond died (bondviewKillCurrentPlayer, right after bonddead is set): charge a life; out of lives it stays down for good. */
void portCoopNoteDeath(int p)
{
    int left;

    if (!sCoopOn || p < 0 || p >= 4 || sCharged[p]) {
        return;
    }
    sCharged[p] = 1;
    if (sSess.lives <= 0) {
        return;
    }
    if (sSess.livesMode) {
        if (sPartyLives > 0) {
            sPartyLives--;
        }
        left = sPartyLives;
    } else {
        if (sLivesLeft[p] > 0) {
            sLivesLeft[p]--;
        }
        left = sLivesLeft[p];
    }
    if (left <= 0) {
        sOut[p] = 1;
    }
    portCoopFpLog("ge: coop: player %d died: %s lives left %d%s\n", p, sSess.livesMode ? "party" : "its", left, sOut[p] ? " - OUT OF LIVES: it stays down and watches" : "");
}

/* HUD: the lives to show for Bond p, -1 when lives are unlimited */
int portCoopHudLives(int p)
{
    if (!sCoopOn || sSess.lives <= 0 || p < 0 || p >= 4) {
        return -1;
    }
    return sSess.livesMode ? sPartyLives : sLivesLeft[p];
}

int portCoopIsOut(int p)
{
    return sCoopOn && p >= 0 && p < 4 && sOut[p] && g_playerPointers[p] != NULL && g_playerPointers[p]->bonddead;
}

int portCoopLivesPartyMode(void)
{
    return sSess.livesMode;
}

/* ---- the held-at-the-exit state ---- */

int portCoopBondHeld(int p)
{
    return sCoopOn && p >= 0 && p < 4 && sHeld[p];
}

static int anyHeld(void)
{
    int i;

    for (i = 0; i < 4; i++) {
        if (sHeld[i]) {
            return 1;
        }
    }
    return 0;
}

/* a Bond waits at the exit and the ending has not started: the netplay disconnect handler lets the mission complete instead of going to the title */
int portCoopExitWaiting(void)
{
    return sCoopOn && !sExitOpen && anyHeld();
}

void portCoopPartnerGone(void)
{
    if (sCoopOn && !sPartnerGone) {
        sPartnerGone = 1;
        portCoopFpLog("ge: coop: exit: the partner left while a Bond waits at the exit: the mission completes\n");
    }
}

/* ---- start spawns, the reachability check and the respawn plan ---- */

/* snapshot of every Bond's start spawn (sim tick 1, after the solo start code ran) */
static void startSnapshot(void)
{
    int i;

    for (i = 0; i < portAllPlayerCount() && i < 4; i++) {
        struct player *p = g_playerPointers[i];

        if (p != NULL && p->prop != NULL && p->prop->stan != NULL) {
            sStartX[i] = p->prop->pos.x;
            sStartZ[i] = p->prop->pos.z;
            sStartTheta[i] = p->vv_theta * (6.2831855f / 360.0f);
            sStartStan[i] = (StandTile *) p->prop->stan;
            sHaveStart[i] = 1;
            portCoopFpLog("ge: coop: start spawn player %d: %.0f %.0f\n", i, sStartX[i], sStartZ[i]);
        }
    }
}

extern waypoint *chrlvStanPathRelated(coord3d *pos, StandTile *stan);
extern bool doorIsPadlockFree(DoorRecord *door);

#define PORT_WP_MAX 2048
#define PORT_DOOR_MAX 64
#define PORT_DOOR_RADIUS 150.0f

/* Stages whose progression is one-way in a way the waypoint graph and the door locks cannot show (the Train's brakes section, Bunker 2's escape,
 * Cradle's platform): a respawn at the start is only allowed while the partner is still near the start (horizontally within this many units,
 * and on about the same floor). A proper per-level checkpoint table is deferred. */
#define PORT_ONEWAY_NEAR 1500.0f

static int stageIsOneWay(void)
{
    extern s32 g_StageNum;

    return g_StageNum == LEVELID_TRAIN || g_StageNum == LEVELID_BUNKER2 || g_StageNum == LEVELID_CRADLE;
}

/* a door in the way: closed for good (locked and no Bond holds the keys, or a padlock) */
static int doorLockedFor(DoorRecord *d)
{
    if (d->keyflags != 0 && !bondinvCheckHasKeyFlags(d->keyflags)) {
        return 1;
    }
    return !doorIsPadlockFree(d);
}

/* squared distance from point (px,pz) to the segment (ax,az)-(bx,bz) in the XZ plane */
static float segDist2(float px, float pz, float ax, float az, float bx, float bz)
{
    float dx = bx - ax, dz = bz - az;
    float len2 = dx * dx + dz * dz;
    float t = len2 > 0.0f ? ((px - ax) * dx + (pz - az) * dz) / len2 : 0.0f;
    float qx, qz;

    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    qx = ax + t * dx - px;
    qz = az + t * dz - pz;
    return qx * qx + qz * qz;
}

/* Can a Bond standing at (spos, sstan) walk to (tpos, tstan)? The AI waypoint graph (the paths guards walk) searched breadth first, an edge
 * dropped when a locked door lies on it. 1 yes; 0 no, with `why` saying which test failed (the logged reason). */
static int startReaches(coord3d *spos, StandTile *sstan, coord3d *tpos, StandTile *tstan, const char **why)
{
    static u8 seen[PORT_WP_MAX];
    static s16 queue[PORT_WP_MAX];
    float dx[PORT_DOOR_MAX], dy[PORT_DOOR_MAX], dz[PORT_DOOR_MAX];
    int nd = 0;
    waypoint *wps = g_CurrentSetup.pathwaypoints;
    waypoint *a, *b;
    int nwp = 0, head = 0, tail = 0, ia, ib;
    PropRecord *p;

    if (getenv("GE_COOP_FORCEUNREACH") != NULL) {   /* test only: pretend the start cannot reach the partner */
        *why = "forced by GE_COOP_FORCEUNREACH";
        return 0;
    }
    if (getenv("GE_COOP_FORCEREACH") != NULL) {   /* test only: pretend it can (the Dam start gate is locked until the level opens it) */
        return 1;
    }
    if (wps == NULL) {
        *why = "the level has no waypoint graph";
        return 0;
    }
    a = chrlvStanPathRelated(spos, sstan);
    b = chrlvStanPathRelated(tpos, tstan);
    if (a == NULL) {
        *why = "no waypoint at the start spawn";
        return 0;
    }
    if (b == NULL) {
        *why = "no waypoint at the partner";
        return 0;
    }
    while (wps[nwp].padID >= 0 && nwp < PORT_WP_MAX) {
        nwp++;
    }
    ia = (int) (a - wps);
    ib = (int) (b - wps);
    if (ia < 0 || ia >= nwp || ib < 0 || ib >= nwp) {
        *why = "waypoint out of range";
        return 0;
    }
    for (p = chrpropGetActiveTail(); p != NULL && nd < PORT_DOOR_MAX; p = p->prev) {
        if (p->type == PROP_TYPE_DOOR && p->door != NULL && doorLockedFor(p->door)) {
            dx[nd] = p->pos.x;
            dy[nd] = p->pos.y;
            dz[nd] = p->pos.z;
            nd++;
        }
    }
    memset(seen, 0, (size_t) nwp);
    seen[ia] = 1;
    queue[tail++] = (s16) ia;
    while (head < tail) {
        waypoint *w = &wps[queue[head++]];
        PadRecord *pw = &g_CurrentSetup.pads[w->padID];
        s32 *nb;

        if (queue[head - 1] == ib) {
            return 1;
        }
        for (nb = w->neighbours; nb != NULL && *nb >= 0; nb++) {
            PadRecord *pn;
            int blocked = 0, k;

            if (*nb >= nwp || seen[*nb]) {
                continue;
            }
            pn = &g_CurrentSetup.pads[wps[*nb].padID];
            for (k = 0; k < nd && !blocked; k++) {
                float ylo = pw->pos.y < pn->pos.y ? pw->pos.y : pn->pos.y;
                float yhi = pw->pos.y < pn->pos.y ? pn->pos.y : pw->pos.y;

                if (dy[k] > ylo - 250.0f && dy[k] < yhi + 250.0f
                    && segDist2(dx[k], dz[k], pw->pos.x, pw->pos.z, pn->pos.x, pn->pos.z) < PORT_DOOR_RADIUS * PORT_DOOR_RADIUS) {
                    blocked = 1;
                }
            }
            if (blocked) {
                continue;
            }
            seen[*nb] = 1;
            queue[tail++] = (s16) *nb;
        }
    }
    *why = nd > 0 ? "every route crosses a locked door" : "no walkable route in the waypoint graph";
    return 0;
}

/* Where will the respawn go? Decided in the sim tick, just before mp_respawn_handler. The start spawn when coop_respawn=start (and, with
 * coop_respawn=partner, while the partner waits at the exit), but only if the start can reach the partner: else beside the partner, with the
 * reason logged. No living partner: the start spawn (a better place than the random MP pad). */
static void planRespawn(int p)
{
    int partner = portCoopRespawnPartnerOf(p);
    const char *why = "";
    int want;

    sPlanStart[p] = 0;
    if (!sHaveStart[p]) {
        portCoopFpLog("ge: coop: respawn plan player %d: beside the partner (no start spawn was recorded)\n", p);
        return;
    }
    if (partner < 0) {
        sPlanStart[p] = 1;
        portCoopFpLog("ge: coop: respawn plan player %d: start spawn (no living partner)\n", p);
        return;
    }
    want = sSess.respawn == 0;
    if (!want && sHeld[partner] && !sExitOpen) {
        want = 1;
        why = " (coop_respawn=partner, but the partner waits at the exit: back to the start, then walk in)";
    }
    if (!want) {
        portCoopFpLog("ge: coop: respawn plan player %d: beside player %d (coop_respawn=partner)\n", p, partner);
        return;
    }
    {
        struct player *q = g_playerPointers[partner];
        coord3d spos;
        const char *no = "";

        spos.x = sStartX[p];
        spos.z = sStartZ[p];
        spos.y = q->prop->pos.y;
        if (!startReaches(&spos, sStartStan[p], &q->prop->pos, (StandTile *) q->prop->stan, &no)) {
            portCoopFpLog("ge: coop: respawn plan player %d: beside player %d - the start cannot reach the partner (%s)\n", p, partner, no);
            return;
        }
        if (stageIsOneWay()) {
            float ddx = q->prop->pos.x - sStartX[p], ddz = q->prop->pos.z - sStartZ[p];

            if (ddx * ddx + ddz * ddz > PORT_ONEWAY_NEAR * PORT_ONEWAY_NEAR) {
                portCoopFpLog("ge: coop: respawn plan player %d: beside player %d - this stage is one-way and the partner is %.0f from the start\n", p, partner,
                              __builtin_sqrtf(ddx * ddx + ddz * ddz));
                return;
            }
        }
        sPlanStart[p] = 1;
        portCoopFpLog("ge: coop: respawn plan player %d: start spawn, it can reach player %d%s\n", p, partner, why);
    }
}

/* mp_respawn_handler: the start spawn this respawn goes to (1), else the partner / pad logic as before */
int portCoopRespawnStartSpot(int p, float *x, float *z, s32 *stan, float *angle)
{
    if (!sCoopOn || p < 0 || p >= 4 || !sPlanStart[p] || !sHaveStart[p]) {
        return 0;
    }
    *x = sStartX[p];
    *z = sStartZ[p];
    *stan = (s32) (size_t) sStartStan[p];
    *angle = sStartTheta[p];
    return 1;
}

/* ---- the exit gate ---- */

/* 1 = this ending is gated: both living Bonds must reach the exit (coop_exits=both) and it is not released yet */
int portCoopExitGateActive(void)
{
    return sCoopOn && sSess.exits == 0 && !sExitOpen && !sPartnerGone;
}

static int inPlay(int i)
{
    struct player *p = (i >= 0 && i < 4) ? g_playerPointers[i] : NULL;

    if (p == NULL || p->prop == NULL) {
        return 0;
    }
    if (sPartnerGone) {
        return sHeld[i];
    }
    return !(p->bonddead && sOut[i]);
}

static int arrivedAt(int i, ChrRecord *self, const HitNote *h)
{
    s32 saved = get_cur_playernum();
    struct player *p = g_playerPointers[i];
    struct player *t = g_playerPointers[h->bond];
    int r = 0;

    if (p == NULL || p->prop == NULL || p->bonddead) {
        return 0;
    }
    if (h->kind == 1) {
        set_cur_player(i);
        r = check_if_actor_is_at_preset(self, h->pad) != 0;
        set_cur_player(saved);
    } else if (h->kind == 2) {
        set_cur_player(i);
        r = chrGetDistanceFromBondToPad(self, h->pad) < h->thr;
        set_cur_player(saved);
    }
    if (!r && t != NULL && t->prop != NULL) {   /* the exit's own spot (an object that was used, or any exit) is where the first Bond stands */
        float dx = p->prop->pos.x - t->prop->pos.x, dz = p->prop->pos.z - t->prop->pos.z, dy = p->prop->pos.y - t->prop->pos.y;

        r = dx * dx + dz * dz <= 300.0f * 300.0f && dy < 200.0f && dy > -200.0f;
    }
    return r;
}

static void openExit(const char *why)
{
    int i;

    sExitOpen = 1;
    for (i = 0; i < 4; i++) {   /* the ending takes the controls from here (and "press a button to continue" must work for either Bond) */
        sHeld[i] = 0;
    }
    portCoopFpLog("ge: coop: exit: %s: the ending starts at sim tick %u\n", why, sPupTick);
}

/* The script `self` is about to start an ending that leads to an exit (chrai.c checked that). 1 = hold the script (yield): the Bond that triggered
 * it waits at the exit until every other Bond that is still in play has reached it too. */
int portCoopExitGate(ChrRecord *self)
{
    HitNote *h;
    int i, t, all = 1;

    if (!sCoopOn || sExitOpen) {
        return 0;
    }
    if (sSess.exits != 0) {
        openExit("coop_exits=first, the first Bond ends the mission");
        return 0;
    }
    if (sPartnerGone) {
        openExit("the partner is gone");
        return 0;
    }
    h = hitSlot(self);
    if (h->chr != self || !portCoopBondIsLive(h->bond)) {
        /* nobody "reached" this ending (a timer or an objective flag starts it, e.g. Aztec's countdown): not gated */
        openExit("this ending has no trigger Bond (a timer or an objective starts it): not gated");
        return 0;
    }
    t = h->bond;
    if (!sHeld[t]) {
        PadRecord *pd = NULL;

        sHeld[t] = 1;
        sTrigger = t;
        if ((h->kind == 1 || h->kind == 2) && h->pad >= 0) {
            s32 pid = chrResolvePadId(self, h->pad);

            pd = (pid < 10000) ? &g_CurrentSetup.pads[pid] : (PadRecord *) &g_CurrentSetup.boundpads[pid - 10000];
        }
        sSiteX = pd != NULL ? pd->pos.x : g_playerPointers[t]->prop->pos.x;
        sSiteZ = pd != NULL ? pd->pos.z : g_playerPointers[t]->prop->pos.z;
        sSiteSet = 1;
        portCoopFpLog("ge: coop: exit: player %d reached the exit (script chr %d, %s %d) at sim tick %u and is held there: waiting for the partner\n", t,
                      (int) self->chrnum, h->kind == 1 ? "room of pad" : (h->kind == 2 ? "near pad" : "used an object, spot"), h->pad, sPupTick);
    }
    for (i = 0; i < portAllPlayerCount() && i < 4; i++) {
        if (i == t || !inPlay(i) || sHeld[i]) {
            continue;
        }
        if (arrivedAt(i, self, h)) {
            sHeld[i] = 1;
            portCoopFpLog("ge: coop: exit: player %d reached the exit at sim tick %u\n", i, sPupTick);
        } else {
            all = 0;
        }
    }
    if (all) {
        openExit("every living Bond is at the exit");
        return 0;
    }
    return 1;
}

/* HUD: 0 nothing; 1 this Bond is held at the exit, waiting; 2 this Bond must get to the exit (meters away in *m, 0 ahead / 1 right / 2 behind / 3 left in *dir) */
int portCoopHudExit(int p, int *m, int *dir)
{
    struct player *q;
    float dx, dz, d, fx, fz, ahead, side;

    if (!sCoopOn || sExitOpen || sSess.exits != 0 || !anyHeld() || p < 0 || p >= 4) {
        return 0;
    }
    if (sHeld[p]) {
        return 1;
    }
    q = g_playerPointers[p];
    if (q == NULL || q->prop == NULL || q->bonddead || !sSiteSet) {
        return 0;
    }
    dx = sSiteX - q->prop->pos.x;
    dz = sSiteZ - q->prop->pos.z;
    d = __builtin_sqrtf(dx * dx + dz * dz);
    fx = -__builtin_sinf(q->vv_theta * (3.14159265f / 180.0f));
    fz = __builtin_cosf(q->vv_theta * (3.14159265f / 180.0f));
    ahead = d > 1.0f ? (dx * fx + dz * fz) / d : 1.0f;
    side = d > 1.0f ? (dx * -fz + dz * fx) / d : 0.0f;
    *m = (int) (d / 100.0f + 0.5f);
    *dir = ahead > 0.7f ? 0 : (ahead < -0.7f ? 2 : (side > 0.0f ? 1 : 3));
    return 2;
}

/* ---- the watching camera of a Bond who is out of lives ---- */

/* The partner an out-of-lives Bond watches (index), -1 if it is not watching. */
int portCoopWatching(int p)
{
    struct player *q;

    if (!sCoopOn || p < 0 || p >= 4 || !sOut[p] || !sWatching[p]) {
        return -1;
    }
    q = g_playerPointers[p];
    if (q == NULL || !q->bonddead) {
        return -1;
    }
    return portCoopRespawnPartnerOf(p);
}

/* bondview2.c, after MoveBond: the camera of a Bond who watches its partner, a few steps behind the partner's eye looking where it looks */
int portCoopSpectateCamera(int p, coord3d *pos, coord3d *pos2, coord3d *offset, StandTile **tile, coord3d *walkStart)
{
    int w = portCoopWatching(p);
    struct player *q;
    float fx, fz, cp, sp;

    if (w < 0) {
        return 0;
    }
    q = g_playerPointers[w];
    if (q == NULL || q->prop == NULL) {
        return 0;
    }
    fx = -__builtin_sinf(q->vv_theta * (3.14159265f / 180.0f));
    fz = __builtin_cosf(q->vv_theta * (3.14159265f / 180.0f));
    cp = __builtin_cosf(q->vv_verta * (3.14159265f / 180.0f));
    sp = __builtin_sinf(q->vv_verta * (3.14159265f / 180.0f));
    pos->x = q->prop->pos.x - fx * 170.0f;
    pos->y = q->prop->pos.y + 30.0f;
    pos->z = q->prop->pos.z - fz * 170.0f;
    pos2->x = q->prop->pos.x + fx * cp * 120.0f;
    pos2->y = q->prop->pos.y + sp * 120.0f;
    pos2->z = q->prop->pos.z + fz * cp * 120.0f;
    offset->x = 0.0f;
    offset->y = 1.0f;
    offset->z = 0.0f;
    *tile = (StandTile *) q->prop->stan;
    walkStart->x = q->prop->pos.x;
    walkStart->y = q->prop->pos.y;
    walkStart->z = q->prop->pos.z;
    return 1;
}

/* The mission-failed flow (death music, mission state) runs once, when the last Bond dies. */
int portCoopFailOnce(void)
{
    if (sFailStarted) {
        return 0;
    }
    sFailStarted = 1;
    return 1;
}

/* Every Bond is dead and its death animation has finished: time for the shared death camera. */
int portCoopAllDeathsDone(void)
{
    int i;
    int any = 0;

    for (i = 0; i < portAllPlayerCount() && i < 4; i++) {
        struct player *p = g_playerPointers[i];

        if (p == NULL || p->prop == NULL) {
            continue;
        }
        any = 1;
        if (!p->bonddead || !p->deathanimfinished || p->colourfadetimemax60 >= 0) {
            return 0;
        }
    }
    return any;
}

/* ---- the mission end (mission-end job) --------------------------------------------------------------------------------------
 * The level scripts' fades, the exit camera and the "press a button" exit were written for one Bond: a fade set on "the current player"
 * hit whichever Bond was first in the RNG-shuffled pass order of that tick, and the exit followed the pressing Bond's own fade. In co-op the
 * end is shared sim state instead: the script commands act on EVERY Bond, "press to continue" is any Bond's record (netplay: both machines hold
 * both records for the tick), the fades run on all Bonds and the stage is left on the tick the last fade completes, on both machines. */

/* 1 in co-op unless GE_NET_TEST_OLDEND=1 (test only, negative control: the single-Bond mission end of before the mission-end job). */
int portCoopSharedEnd(void)
{
    static int old = -1;

    if (old < 0) {
        old = getenv("GE_NET_TEST_OLDEND") != NULL;
    }
    return sCoopOn && !old;
}

/* 1 when every Bond's screen fade has finished (the script's "fade completed" test in co-op). */
int portCoopAllFadesComplete(void)
{
    int i;

    for (i = 0; i < portAllPlayerCount() && i < 4; i++) {
        struct player *p = g_playerPointers[i];

        if (p != NULL && p->colourfadetimemax60 >= 0) {
            return 0;
        }
    }
    return 1;
}

/* 1 when every Bond's screen is fully faded to black and the fade is finished. */
int portCoopAllScreensBlack(void)
{
    int i;

    for (i = 0; i < portAllPlayerCount() && i < 4; i++) {
        struct player *p = g_playerPointers[i];

        if (p != NULL && (p->colourfadetimemax60 >= 0 || p->colourscreenfrac != 1.0f)) {
            return 0;
        }
    }
    return 1;
}

/* "The ending has started" (shared sim state, the same on both machines): the level's exit sequence is running (the "press to continue" flag is
 * up, or the exit camera / fade-to-title mode is on). From then on the ending is not gameplay: damage and deaths are ignored, the death / damage
 * screen overlays are cleared, and only the cutscene Bond is shown. */
int portCoopEnding(void)
{
    extern enum CAMERAMODE g_CameraMode;
    extern s32 stop_time_flag;

    return portCoopSharedEnd() && (stop_time_flag != 0 || g_CameraMode == CAMERAMODE_POSEND || g_CameraMode == CAMERAMODE_FADE_TO_TITLE);
}

static int sEndingSeen;

/* once per sim tick (portCoopDeathTick): the ending's first tick clears both Bonds' death / damage overlays; every tick keeps the damage flash off */
static void endingTick(void)
{
    int i;

    if (!portCoopEnding()) {
        return;
    }
    for (i = 0; i < portAllPlayerCount() && i < 4; i++) {
        struct player *p = g_playerPointers[i];

        if (p == NULL) {
            continue;
        }
        p->damageshowtime = -1;
        if (!sEndingSeen && p->colourscreenred > 0 && p->colourscreengreen == 0 && p->colourscreenblue == 0 && p->colourscreenfrac < 1.0f) {
            p->colourscreenred = 0;   /* the red death overlay (fade colour 150,0,0): gone */
            p->colourscreenfrac = 0.0f;
        }
    }
    if (!sEndingSeen) {
        sEndingSeen = 1;
        portCoopFpLog("ge: coop: the ending starts at sim tick %u: damage ignored, overlays cleared, only the cutscene Bond (player %d) is shown\n", sPupTick, sCinema);
    }
}

static int sEndLogged;

/* The shared stage exit: logs the sim tick (the mission-end tests compare it between the machines) and returns to the title once. */
void portCoopMissionEnd(const char *why)
{
    extern void bossReturnTitleStage(void);
    extern s32 g_MainStageNum;

    if (sEndLogged) {
        return;
    }
    sEndLogged = 1;
    portCoopFpLog("ge: coop: mission end (%s) at sim tick %u\n", why, sPupTick);
    bossReturnTitleStage();
    (void) g_MainStageNum;
}

/* A game mode's mission.win / mission.fail (port_mode.c portModeEndMission) in co-op: the same shared mission end, on the sim tick the script ran it
 * (both machines run the same script on the same tick). The stock bossReturnTitleStage is suppressed in a mode, so this goes straight to the title
 * stage, whose report page reads the mode's result. */
void portCoopModeMissionEnd(const char *why)
{
    extern void bossRunTitleStage(void);

    if (sEndLogged) {
        return;
    }
    sEndLogged = 1;
    portCoopFpLog("ge: coop: mission end (%s) at sim tick %u\n", why, sPupTick);
    bossRunTitleStage();
}

void portCoopMissionEndReset(void)
{
    sEndLogged = 0;
    sEndingSeen = 0;
}

/* The host's co-op session settings by modifier name (coop_exits / coop_respawn / coop_lives / coop_lives_mode), for Lua's mods.get: a guest's own
 * modifier values are not the session's, so a mode script must read these instead. 1 = known name (value in *out). */
int portCoopSessionGet(const char *name, int *out)
{
    if (!sCoopOn || name == NULL) {
        return 0;
    }
    if (strcmp(name, "coop_exits") == 0) {
        *out = sSess.exits;
    } else if (strcmp(name, "coop_respawn") == 0) {
        *out = sSess.respawn;
    } else if (strcmp(name, "coop_lives") == 0) {
        *out = sSess.lives;
    } else if (strcmp(name, "coop_lives_mode") == 0) {
        *out = sSess.livesMode;
    } else {
        return 0;
    }
    return 1;
}

/* boss.c bossSetLoadedStage (co-op): every way out of a stage (exit_level, the button exit, death, abort) calls it; the tick is what the tests compare. */
void portCoopStageChangeNote(int stage)
{
    extern s32 g_StageNum;

    if (g_StageNum != 0x5a) {   /* (not while the title / menus run) */
        extern void portNetReplayStageEnd(unsigned int tick);

        portCoopFpLog("ge: coop: stage change to %d requested at sim tick %u\n", stage, sPupTick);
        portNetReplayStageEnd(sPupTick);
    }
}

/* GE_COOP_KILL=<tick>:<player>[,<tick>:<player>...] (test): at that sim tick the player dies as if shot (invincibility still protects). */
static void killTestTick(void)
{
    static const char *spec;
    static int init;
    const char *s;

    if (!init) {
        init = 1;
        spec = getenv("GE_COOP_KILL");
    }
    for (s = spec; s != NULL && *s != '\0';) {
        unsigned t = 0;
        int pl = 0;
        int used = 0;

        if (sscanf(s, "%u:%d%n", &t, &pl, &used) < 2 || used <= 0) {
            break;
        }
        if (t == sDeathTicks && pupPlayer(pl) != NULL) {
            s32 saved = get_cur_playernum();

            set_cur_player(pl);
            pupPlayer(pl)->bondhealth = 0.0f;
            bondviewKillCurrentPlayer();
            set_cur_player(saved);
            printf("ge: coop: test: killed player %d at tick %u (dead %d)\n", pl, sDeathTicks, (int) pupPlayer(pl)->bonddead);
        }
        s += used;
        if (*s == ',') {
            s++;
        }
    }
}

/* GE_COOP_HURT=<tick>:<player>:<amount>[,...] (test): at that sim tick the player takes a hit through the REAL damage code
 * (record_damage_kills: the damage flash, the invincibility and watch gates, armour, health, the death), where GE_COOP_KILL
 * skips all that. `amount` is in the code's units before the difficulty and enemy_damage scaling (10 is a certain kill). */
static void hurtTestTick(void)
{
    static const char *spec;
    static int init;
    const char *s;

    if (!init) {
        init = 1;
        spec = getenv("GE_COOP_HURT");
    }
    for (s = spec; s != NULL && *s != '\0';) {
        unsigned t = 0;
        int pl = 0;
        float amount = 0.0f;
        int used = 0;

        if (sscanf(s, "%u:%d:%f%n", &t, &pl, &amount, &used) < 3 || used <= 0) {
            break;
        }
        if (t == sDeathTicks && pupPlayer(pl) != NULL) {
            extern void record_damage_kills(f32 damage_amount, f32 vectorx, f32 vectorz, s32 playerid, s32 affects_armor);
            s32 saved = get_cur_playernum();
            float before;

            set_cur_player(pl);
            before = pupPlayer(pl)->bondhealth;
            record_damage_kills(amount, 0.0f, 1.0f, -1, 1);
            printf("ge: coop: test: hurt player %d by %.2f at tick %u (health %.3f -> %.3f, dead %d, watch %d, damageshowtime %d)\n", pl, amount,
                   sDeathTicks, before, pupPlayer(pl)->bondhealth, (int) pupPlayer(pl)->bonddead, (int) pupPlayer(pl)->watch_animation_state,
                   (int) pupPlayer(pl)->damageshowtime);
            set_cur_player(saved);
        }
        s += used;
        if (*s == ',') {
            s++;
        }
    }
}

/* boss.c, once per sim tick right after lvlTick (current player 0): notes deaths, respawns a Bond whose timer ran out.
 * The timer counts game fields (g_ClockTimer), so it is the same on both machines and stands still when the game does. */
void portCoopDeathTick(void)
{
    extern s32 g_ClockTimer;
    int i;

    if (!sCoopOn) {
        return;
    }
    sDeathTicks++;
    sFieldNow += g_ClockTimer;
    {   /* GE_COOP_CLOCKDBG=1 (test): why a tick's game clock was 0 (locked controls, pause, or a zero frame delta) */
        static int clk = -1;

        if (clk < 0) {
            clk = getenv("GE_COOP_CLOCKDBG") != NULL;
        }
        if (clk && g_ClockTimer != 1) {
            extern s32 g_ControlsLockedFlag;
            extern s32 speedgraphframes;
            extern s32 checkGamePaused(void);

            printf("ge: coop: clockdbg tick %u clock %d locked %d paused %d speedframes %d\n", sDeathTicks, (int) g_ClockTimer,
                   (int) g_ControlsLockedFlag, (int) checkGamePaused(), (int) speedgraphframes);
        }
    }
    {   /* GE_COOP_GOD=<player> (test): that Bond cannot be killed, so the other can die to guards while it lives on */
        static int god = -2;

        if (god == -2) {
            const char *e = getenv("GE_COOP_GOD");

            god = (e != NULL && e[0] != '\0') ? atoi(e) : -1;
        }
        if (god >= 0 && pupPlayer(god) != NULL) {
            pupPlayer(god)->cheatBondInvincible = 1;
        }
    }
    {   /* mission-end job: one line whenever the end-of-mission state changes (camera mode, the exit flag, each Bond's fade); the end tests read them */
        extern enum CAMERAMODE g_CameraMode;
        extern s32 stop_time_flag;
        extern s32 g_ControlsLockedFlag;
        static int lastKey = -1;
        int key = (int) g_CameraMode | (int) (stop_time_flag & 3) << 4 | (g_ControlsLockedFlag ? 1 : 0) << 6;
        int k;

        for (k = 0; k < portAllPlayerCount() && k < 2; k++) {
            struct player *p = g_playerPointers[k];

            key |= (p != NULL && p->colourfadetimemax60 >= 0 ? 1 : 0) << (7 + k);
            key |= (p != NULL && p->colourscreenfrac >= 1.0f ? 1 : 0) << (9 + k);
        }
        if (sDeathTicks <= 1) {
            lastKey = -1;
        }
        if (key != lastKey) {
            lastKey = key;
            portCoopFpLog("ge: coop: end-state tick %u: camera %d stop_time %d locked %d fading %d%d black %d%d\n", sPupTick, (int) g_CameraMode, (int) stop_time_flag,
                          (int) g_ControlsLockedFlag, (key >> 7) & 1, (key >> 8) & 1, (key >> 9) & 1, (key >> 10) & 1);
        }
    }
    endingTick();
    if (portNetLockActive()) {   /* netplay job 5: the session commands due this tick (spawn, give), before any Bond moves, on both machines */
        portNetSessionCmds(sPupTick);
    }
    portCoopDropTestTick(sDeathTicks);
    killTestTick();
    hurtTestTick();
    if (sDeathTicks == 1) {   /* the full starting loadout the stage start built: a respawn gives it back */
        for (i = 0; i < portAllPlayerCount() && i < 4; i++) {
            portCoopLoadoutSnapshot(i);
            portCoopInvLog("start", i);
        }
        startSnapshot();   /* CO-OP-EXITS: where each Bond starts (a respawn at the start goes back there) */
    }
    for (i = 0; i < portAllPlayerCount() && i < 4; i++) {   /* a Bond held at the exit cannot be hurt */
        if (sHeld[i] && g_playerPointers[i] != NULL) {
            g_playerPointers[i]->cheatBondInvincible = 1;
        }
    }
    if (sDeathTicks == 60 || sDeathTicks == 100 || sDeathTicks == 123 || sDeathTicks == 124 || sDeathTicks == 125 || sDeathTicks == 130 || sDeathTicks == 150 || sDeathTicks == 160 || sDeathTicks == 180 || sDeathTicks == 200 || sDeathTicks == 230 || sDeathTicks == 250 || sDeathTicks == 290) {
        for (i = 0; i < portAllPlayerCount() && i < 4; i++) {
            { char tg[16]; snprintf(tg, sizeof(tg), "tick%u", sDeathTicks); portCoopInvLog(tg, i); }
        }
    }
    for (i = 0; i < portAllPlayerCount() && i < 4; i++) {
        struct player *p = g_playerPointers[i];
        int left;

        if (p == NULL || p->prop == NULL) {
            continue;
        }
        if (!p->bonddead) {
            sDeadSince[i] = 0;
            continue;
        }
        portCoopNoteDeath(i);   /* (normally charged where the Bond was killed; idempotent) */
        if (sDeadSince[i] == 0) {
            sDeadSince[i] = sFieldNow + 1;
            extern s32 g_isBondKIA;

            portCoopFpLog("ge: coop: player %d died at field %d (health %.2f, KIA flag %d)\n", i, sFieldNow, p->bondhealth, (int) g_isBondKIA);
            portCoopInvLog("death", i);
            portCoopDeathDrops(i);   /* picked-up guns and quest items leave the inventory, armour is lost */
            portCoopInvLog("afterdrops", i);
            if (portCoopRespawnPartnerOf(i) >= 0) {
                portCoopInvLog("afterdrops-partner", portCoopRespawnPartnerOf(i));
            }
        }
        left = portCoopRespawnFieldsLeft(sDeadSince[i] - 1, sFieldNow, sRespawnFields);
        if (sOut[i]) {   /* out of lives: it stays down and, once its death is over, watches the partner */
            if (!sWatching[i] && p->deathanimfinished && portCoopRespawnPartnerOf(i) >= 0) {
                s32 savedW = get_cur_playernum();

                sWatching[i] = 1;
                set_cur_player(i);
                currentPlayerSetFadeColour(0, 0, 0, 1.0f);
                currentPlayerSetFadeFrac(60.0f, 0.0f);   /* out of the black the death left, into the partner's view */
                set_cur_player(savedW);
                portCoopFpLog("ge: coop: player %d is out of lives and now watches player %d (field %d)\n", i, portCoopRespawnPartnerOf(i), sFieldNow);
            }
            continue;
        }
        if (portArenaCoopHeld(i)) {   /* arena co-op: a bled-out Bond waits for the end of the wave (the mode releases him) */
            continue;
        }
        if (portCoopRespawnDue(left, portCoopAllDead())) {
            s32 saved = get_cur_playernum();

            planRespawn(i);
            set_cur_player(i);
            mp_respawn_handler();
            {   /* netplay job 7: the respawned Bond's room view starts from the world's facts, not from the place it died */
                extern void bgPortalViewInit(s32 pl);

                bgPortalViewInit(i);
            }
            set_cur_player(saved);
            sDeadSince[i] = 0;
            sCharged[i] = 0;
            sPlanStart[i] = 0;
            portCoopInvLog("respawn", i);
            portCoopFpLog("ge: coop: player %d respawned at field %d near player %d: pos %.0f %.0f %.0f (eye %.0f, partner y %.0f)\n", i,
                   sFieldNow, portCoopRespawnPartnerOf(i), p->prop->pos.x, p->prop->pos.y, p->prop->pos.z, p->eyeheight,
                   pupPlayer(1 - i) != NULL && pupPlayer(1 - i)->prop != NULL ? pupPlayer(1 - i)->prop->pos.y : 0.0f);
        }
    }
    portArenaCoopTick();   /* arena co-op: bleed-out and revive timers (port_arenacoop.c; inert unless a mode turned the rules on) */
    if (!sDeathCam && portCoopAllDead() && portCoopAllDeathsDone()) {   /* everybody is down for good: the normal solo death camera, then mission failed */
        int last = 0;
        s32 saved = get_cur_playernum();

        for (i = 1; i < portAllPlayerCount() && i < 4; i++) {
            if (sDeadSince[i] > sDeadSince[last]) {
                last = i;   /* the Bond that died last; ties keep the lower index */
            }
        }
        sDeathCam = 1;
        set_cur_player(last);
        bondviewSetCameraMode(CAMERAMODE_DEATH_CAM_SP);
        set_cur_player(saved);
        {
            extern s32 g_isBondKIA;

            portCoopFpLog("ge: coop: all Bonds dead at field %d: death camera (player %d), mission failed (KIA flag %d)\n", sFieldNow, last,
                   (int) g_isBondKIA);
        }
    }
}

/* FNV-1a over every active prop's type and position, a chr's action, a door's state: the same on both machines
 * whatever the local view, and it moves when anything in the world moves, opens, is picked up or dies. */
static unsigned hashU(unsigned h, unsigned v)
{
    return (h ^ v) * 16777619u;
}

static unsigned fbits(float f)
{
    unsigned u;

    memcpy(&u, &f, sizeof(u));
    return u;
}

static unsigned worldHash(void)
{
    unsigned h = 2166136261u;
    unsigned n = 0;
    PropRecord *p;

    for (p = chrpropGetActiveTail(); p != NULL; p = p->prev) {
        h = hashU(h, (unsigned) p->type);
        h = hashU(h, fbits(p->pos.x));
        h = hashU(h, fbits(p->pos.y));
        h = hashU(h, fbits(p->pos.z));
        if (p->type == PROP_TYPE_CHR && p->chr != NULL) {
            h = hashU(h, (unsigned) p->chr->actiontype);
        } else if (p->type == PROP_TYPE_DOOR && p->door != NULL) {
            h = hashU(h, (unsigned) p->door->openstate);
            h = hashU(h, fbits(p->door->openPosition));
        }
        n++;
    }
    return hashU(h, n);
}

/* GE_NET_PASSDBG=<from>-<to> (diagnostic, job 3c): in those sim ticks every player's render pass prints the camera it used, its place in
 * the shuffled order and the PROPFLAG_ONSCREEN props it left behind (index and type). Two runs (another view, another window) are
 * diffed to find a render-derived difference between machines. */
void portNetPassDebug(const char *where)
{
    static int from = -2, to;

    if (from == -2) {
        const char *e = getenv("GE_NET_PASSDBG");
        const char *dash = (e != NULL) ? strchr(e, '-') : NULL;

        from = (e != NULL && e[0] != '\0') ? atoi(e) : -1;
        to = dash != NULL ? atoi(dash + 1) : from;
    }
    if (from < 0 || !sCoopOn || sPupTick < (u32) from || sPupTick > (u32) to || g_CurrentPlayer == NULL) {
        return;
    }
    {
        const struct player *q = g_CurrentPlayer;
        PropRecord *p;
        unsigned idx = 0;
        char list[600];
        int w = 0;

        list[0] = '\0';
        for (p = chrpropGetActiveTail(); p != NULL; p = p->prev, idx++) {
            if ((p->flags & PROPFLAG_ONSCREEN) && w < (int) sizeof(list) - 24) {
                w += snprintf(list + w, sizeof(list) - (size_t) w, " %u:%d", idx, (int) p->type);
            }
        }
        printf("ge: coop: pass %s tick %u player %d order %d cam %.1f %.1f %.1f dead %d watch %d onscreen:%s\n", where, sPupTick, get_cur_playernum(),
               (int) get_player_position_in_shuffled(get_cur_playernum()), q->viewtoworldmtxf->m[3][0], q->viewtoworldmtxf->m[3][1],
               q->viewtoworldmtxf->m[3][2], (int) q->bonddead, (int) q->watch_animation_state, list);
    }
}

/* ---- netplay job 3: the state hash ---------------------------------------------------------------------------
 * portCoopStateHash hashes the world at the start of a sim tick into PORT_NET_HASH_FIELDS named sub-hashes (so a
 * DESYNC says which part differs) and writes a text dump of the same state for the desync file. To extend it: add a
 * field here and its name below; nothing else changes. Everything read is sim state, never the local view. */

/* job 3c: 12 sub-hashes. rng = both seeds and the draw counts; chrs = every chr's body; chrai = its AI state (script offset, alert,
 * morale, timers, targets); objs = every object / weapon / dropped item / glass; proj = projectiles, explosions, bullet impacts;
 * flags = objectives, the stage flag register, the alarm; onscreen = the PROPFLAG_ONSCREEN bits and rendered rooms (render-derived
 * state the sim reads: the canonical sim view keeps it machine-independent). */
static const char *const sStateFieldNames[PORT_NET_HASH_FIELDS] = { "rng", "players", "chrs", "chrai", "objs", "proj", "doors", "props", "flags", "onscreen", "clock", "settings" };

const char *portCoopStateFieldName(int i)
{
    return (i >= 0 && i < PORT_NET_HASH_FIELDS) ? sStateFieldNames[i] : "?";
}

static void dumpf(char *buf, int cap, int *n, const char *fmt, ...)
{
    va_list ap;
    int w;

    if (*n >= cap - 1) {
        return;
    }
    va_start(ap, fmt);
    w = vsnprintf(buf + *n, (size_t) (cap - *n), fmt, ap);
    va_end(ap);
    if (w > 0) {
        *n = (*n + w >= cap) ? cap - 1 : *n + w;
    }
}

int portCoopStateHash(PortNetHashMsg *m, char *dump, int cap)
{
    extern u64 g_randomSeed;
    extern u64 g_chrObjRandomSeed;
    extern ChrRecord *g_ChrSlots;
    extern s32 get_numguards(void);
    extern s32 objectiveGetCount(void);
    extern OBJECTIVESTATUS get_status_of_objective(s32 objectiveNum);
    extern s32 objectiveregisters1;
    unsigned int f[PORT_NET_HASH_FIELDS];
    unsigned int h, total;
    int n = 0, i;
    PropRecord *p;

    dump[0] = '\0';
    if (!portStageRunning() || g_ChrSlots == NULL) {   /* job 3c: no stage (menus after a mission): the lists are stale or freed */
        memset(m->fields, 0, sizeof(m->fields));
        m->nfields = PORT_NET_HASH_FIELDS;
        m->hash = 0;
        dumpf(dump, cap, &n, "(no stage is running)\n");
        return -1;
    }
    /* 0 rng */
    h = 2166136261u;
    h = hashU(h, (unsigned) g_randomSeed);
    h = hashU(h, (unsigned) (g_randomSeed >> 32));
    h = hashU(h, (unsigned) g_chrObjRandomSeed);
    h = hashU(h, (unsigned) (g_chrObjRandomSeed >> 32));
    {
        extern u32 g_portSimRandDraws;
        extern u32 g_portChrObjRandDraws;

        h = hashU(h, g_portSimRandDraws);   /* how many draws since the reseed: a missing or extra draw shows here even if the seeds meet again */
        h = hashU(h, g_portChrObjRandDraws);
        dumpf(dump, cap, &n, "[rng draws] sim %u chrobj %u\n", g_portSimRandDraws, g_portChrObjRandDraws);
    }
    f[0] = h;
    dumpf(dump, cap, &n, "[rng] sim %016llx chrobj %016llx\n", (unsigned long long) g_randomSeed, (unsigned long long) g_chrObjRandomSeed);
    /* 1 players: position, health, armour, weapons, view angles, dead flag, control definition */
    h = 2166136261u;
    for (i = 0; i < 2; i++) {
        struct player *q = pupPlayer(i);

        if (q == NULL || q->prop == NULL) {
            h = hashU(h, 0xdeadu);
            continue;
        }
        h = hashU(h, fbits(q->prop->pos.x));
        h = hashU(h, fbits(q->prop->pos.y));
        h = hashU(h, fbits(q->prop->pos.z));
        h = hashU(h, fbits(q->bondhealth));
        h = hashU(h, fbits(q->bondarmour));
        h = hashU(h, (unsigned) q->bonddead);
        h = hashU(h, fbits(q->vv_theta));
        h = hashU(h, fbits(q->vv_verta));
        h = hashU(h, (unsigned) q->hands[GUNRIGHT].weaponnum);
        h = hashU(h, (unsigned) q->hands[GUNLEFT].weaponnum);
        h = hashU(h, (unsigned) q->controldef);
        h = hashU(h, fbits(q->crosshair_x_pos));   /* the aim state: where the gun points decides where a shot lands */
        h = hashU(h, fbits(q->crosshair_y_pos));
        h = hashU(h, fbits(q->guncrossdamp));
        h = hashU(h, fbits(q->speedtheta));
        h = hashU(h, fbits(q->speedverta));
        h = hashU(h, fbits(q->autoaimx));
        h = hashU(h, fbits(q->autoaimy));
        {   /* job 3b: the per-player state the input record drives (watch, stance, aim mode, the guns' action state and ammo) */
            unsigned ammo = 0;
            int k;

            for (k = 0; k < 30; k++) {
                ammo = hashU(ammo, (unsigned) q->ammoheldarr[k]);
            }
            h = hashU(h, (unsigned) q->watch_animation_state);
            h = hashU(h, (unsigned) q->crouchpos);
            h = hashU(h, (unsigned) q->insightaimmode);
            h = hashU(h, fbits(q->zoominfovy));
            h = hashU(h, (unsigned) q->hands[GUNRIGHT].weapon_action_state);
            h = hashU(h, (unsigned) q->hands[GUNLEFT].weapon_action_state);
            h = hashU(h, (unsigned) q->hands[GUNRIGHT].weapon_ammo_in_magazine);
            h = hashU(h, (unsigned) q->hands[GUNLEFT].weapon_ammo_in_magazine);
            h = hashU(h, ammo);
            dumpf(dump, cap, &n, "[player %d state] watch %d crouch %d aimmode %d zoomfov %.3f gun action %d %d magazine %d %d ammo hash %08x\n", i,
                  (int) q->watch_animation_state, (int) q->crouchpos, (int) q->insightaimmode, q->zoominfovy,
                  (int) q->hands[GUNRIGHT].weapon_action_state, (int) q->hands[GUNLEFT].weapon_action_state,
                  (int) q->hands[GUNRIGHT].weapon_ammo_in_magazine, (int) q->hands[GUNLEFT].weapon_ammo_in_magazine, ammo);
        }
        dumpf(dump, cap, &n, "[player %d] pos %.3f %.3f %.3f health %.4f armour %.4f dead %d theta %.3f verta %.3f weapons %d %d controldef %d\n", i,
              q->prop->pos.x, q->prop->pos.y, q->prop->pos.z, q->bondhealth, q->bondarmour, (int) q->bonddead, q->vv_theta, q->vv_verta,
              (int) q->hands[GUNRIGHT].weaponnum, (int) q->hands[GUNLEFT].weaponnum, (int) q->controldef);
        dumpf(dump, cap, &n, "[player %d aim] crosshair %.5f %.5f damp %.5f speedtheta %.5f speedverta %.5f autoaim %.5f %.5f\n", i,
              q->crosshair_x_pos, q->crosshair_y_pos, q->guncrossdamp, q->speedtheta, q->speedverta, q->autoaimx, q->autoaimy);
        dumpf(dump, cap, &n, "[player %d bits] pos %08x %08x %08x hp %08x arm %08x th %08x vt %08x cross %08x %08x damp %08x st %08x sv %08x aa %08x %08x zoom %08x\n", i,
              fbits(q->prop->pos.x), fbits(q->prop->pos.y), fbits(q->prop->pos.z), fbits(q->bondhealth), fbits(q->bondarmour), fbits(q->vv_theta),
              fbits(q->vv_verta), fbits(q->crosshair_x_pos), fbits(q->crosshair_y_pos), fbits(q->guncrossdamp), fbits(q->speedtheta),
              fbits(q->speedverta), fbits(q->autoaimx), fbits(q->autoaimy), fbits(q->zoominfovy));
    }
    {   /* arena co-op: downed / held / revive timers (port_arenacoop.c; a constant when the rules are off) */
        char line[160];
        unsigned ah = portArenaCoopHash(line, sizeof(line));

        if (line[0] != '\0') {   /* (rules off: nothing mixed in, so the hash of every other session is unchanged) */
            h = hashU(h, ah);
            dumpf(dump, cap, &n, "%s", line);
        }
    }
    f[1] = h;
    /* 2 chrs: position, damage, action, flags of every chr slot that has a prop */
    h = 2166136261u;
    for (i = 0; i < get_numguards(); i++) {
        ChrRecord *c = &g_ChrSlots[i];

        if (c->model == NULL || c->prop == NULL) {   /* (job 4: an unused slot only has model = 0; its other fields are leftovers of the previous stage in a later stage run) */
            continue;
        }
        h = hashU(h, (unsigned) c->chrnum);
        h = hashU(h, (unsigned) c->actiontype);
        h = hashU(h, (unsigned) c->hidden);
        h = hashU(h, (unsigned) c->chrflags);
        h = hashU(h, fbits(c->damage));
        h = hashU(h, fbits(c->maxdamage));
        h = hashU(h, fbits(c->prop->pos.x));
        h = hashU(h, fbits(c->prop->pos.y));
        h = hashU(h, fbits(c->prop->pos.z));
        dumpf(dump, cap, &n, "[chr %d] act %d hidden %d flags %08x damage %.3f/%.3f pos %.3f %.3f %.3f\n", (int) c->chrnum, (int) c->actiontype,
              (int) c->hidden, (unsigned) c->chrflags, c->damage, c->maxdamage, c->prop->pos.x, c->prop->pos.y, c->prop->pos.z);
    }
    f[2] = h;
    /* 3 chrai: the AI state of every chr (script offset and return, morale, alertness, timers, who it saw / heard / was shot by) */
    h = 2166136261u;
    for (i = 0; i < get_numguards(); i++) {
        ChrRecord *c = &g_ChrSlots[i];

        if (c->model == NULL || c->prop == NULL) {
            continue;
        }
        h = hashU(h, (unsigned) c->chrnum);
        h = hashU(h, (unsigned) c->aioffset);
        h = hashU(h, (unsigned) (int) c->aireturnlist);
        h = hashU(h, (unsigned) c->morale);
        h = hashU(h, (unsigned) c->alertness);
        h = hashU(h, (unsigned) c->flags2);
        h = hashU(h, (unsigned) c->random);
        h = hashU(h, (unsigned) c->timer60);
        h = hashU(h, (unsigned) (int) c->padpreset1);
        h = hashU(h, (unsigned) (int) c->chrpreset1);
        h = hashU(h, (unsigned) (int) c->chrseeshot);
        h = hashU(h, (unsigned) (int) c->chrseedie);
        h = hashU(h, (unsigned) (int) c->sleep);
        h = hashU(h, (unsigned) c->firecount[0]);
        h = hashU(h, (unsigned) c->firecount[1]);
        h = hashU(h, (unsigned) c->lastseetarget60);
        h = hashU(h, (unsigned) c->lastheartarget60);
        h = hashU(h, (unsigned) c->seen_bond_time);   /* the union with lastshooter / timeshooter */
        h = hashU(h, fbits(c->hearingscale));
        h = hashU(h, fbits(c->visionrange));
        h = hashU(h, fbits(c->shotbondsum));
        h = hashU(h, (unsigned) (int) c->flinchcnt);
        h = hashU(h, (unsigned) (int) c->invalidmove);
        dumpf(dump, cap, &n, "[chrai %d] aioffset %u return %d morale %u alert %u flags2 %02x random %u timer60 %d pad %d chr %d seeshot %d seedie %d sleep %d seen60 %d heard60 %d shooter %d\n",
              (int) c->chrnum, (unsigned) c->aioffset, (int) c->aireturnlist, (unsigned) c->morale, (unsigned) c->alertness, (unsigned) c->flags2,
              (unsigned) c->random, (int) c->timer60, (int) c->padpreset1, (int) c->chrpreset1, (int) c->chrseeshot, (int) c->chrseedie, (int) c->sleep,
              (int) c->lastseetarget60, (int) c->lastheartarget60, (int) c->seen_bond_time);
    }
    f[3] = h;
    /* 4 objs: every object, weapon, dropped item and pane of glass (state flags, damage, position) */
    h = 2166136261u;
    for (p = chrpropGetActiveTail(); p != NULL; p = p->prev) {
        if ((p->type == PROP_TYPE_OBJ || p->type == PROP_TYPE_WEAPON) && p->obj != NULL) {
            ObjectRecord *o = p->obj;

            h = hashU(h, (unsigned) p->type);
            h = hashU(h, (unsigned) (int) o->obj);
            h = hashU(h, o->flags2);
            h = hashU(h, o->runtime_bitflags);
            h = hashU(h, fbits(o->damage));
            h = hashU(h, fbits(o->maxdamage));
            h = hashU(h, fbits(p->pos.x));
            h = hashU(h, fbits(p->pos.y));
            h = hashU(h, fbits(p->pos.z));
            h = hashU(h, (unsigned) (int) p->timetoregen);
            h = hashU(h, (unsigned) (p->flags & ~PROPFLAG_ONSCREEN));
        }
    }
    f[4] = h;
    /* 5 proj: projectiles in flight (thrown / dropped items), explosions, bullet impacts */
    h = 2166136261u;
    {
        extern struct Projectile g_Projectiles[];
        extern struct Explosion *g_ExplosionBuffer;
        extern s32 g_NumImpactEntries;

        for (i = 0; i < 20; i++) {   /* PROJECTILES_ARR_MAX */
            const Projectile *pr = &g_Projectiles[i];

            h = hashU(h, pr->flags);
            if (pr->obj != NULL) {
                h = hashU(h, fbits(pr->speed.x));
                h = hashU(h, fbits(pr->speed.y));
                h = hashU(h, fbits(pr->speed.z));
                h = hashU(h, pr->age);
                h = hashU(h, (unsigned) pr->droptype);
                h = hashU(h, (unsigned) getPlayerPointerIndex(pr->ownerprop));
                dumpf(dump, cap, &n, "[projectile %d] flags %08x age %u speed %.3f %.3f %.3f owner %d\n", i, pr->flags, pr->age, pr->speed.x, pr->speed.y,
                      pr->speed.z, getPlayerPointerIndex(pr->ownerprop));
            }
        }
        if (g_ExplosionBuffer != NULL) {
            for (i = 0; i < EXPLOSION_BUFFER_LEN; i++) {
                const struct Explosion *ex = &g_ExplosionBuffer[i];

                if (ex->prop != NULL) {
                    h = hashU(h, (unsigned) i);
                    h = hashU(h, (unsigned) (int) ex->age);
                    h = hashU(h, (unsigned) (int) ex->explosion_type);
                    h = hashU(h, (unsigned) (int) ex->player);
                    h = hashU(h, fbits(ex->pos.x));
                    h = hashU(h, fbits(ex->pos.y));
                    h = hashU(h, fbits(ex->pos.z));
                    h = hashU(h, (unsigned) (int) ex->room);
                    dumpf(dump, cap, &n, "[explosion %d] age %d type %d player %d pos %.2f %.2f %.2f room %d\n", i, (int) ex->age, (int) ex->explosion_type,
                          (int) ex->player, ex->pos.x, ex->pos.y, ex->pos.z, (int) ex->room);
                }
            }
        }
        h = hashU(h, (unsigned) g_NumImpactEntries);
    }
    f[5] = h;
    /* 6 doors, 7 props (type and position of every active prop: pickups, bullets, everything that moves) */
    f[6] = 2166136261u;
    for (p = chrpropGetActiveTail(); p != NULL; p = p->prev) {
        if (p->type == PROP_TYPE_DOOR && p->door != NULL) {
            f[6] = hashU(f[6], (unsigned) p->door->openstate);
            f[6] = hashU(f[6], fbits(p->door->openPosition));
            dumpf(dump, cap, &n, "[door] pos %.1f %.1f %.1f openstate %d open %.3f\n", p->pos.x, p->pos.y, p->pos.z, (int) p->door->openstate,
                  p->door->openPosition);
        }
    }
    f[7] = worldHash();
    dumpf(dump, cap, &n, "[props] hash %08x\n", f[7]);
    for (p = chrpropGetActiveTail(); p != NULL; p = p->prev) {
        if (p->type != PROP_TYPE_DOOR && p->type != PROP_TYPE_CHR) {
            dumpf(dump, cap, &n, "[prop] type %d pos %.3f %.3f %.3f flags %02x\n", (int) p->type, p->pos.x, p->pos.y, p->pos.z, (unsigned) p->flags);
        }
    }
    /* 8 flags: objectives, the stage flag register, the alarm */
    h = 2166136261u;
    for (i = 0; i < objectiveGetCount(); i++) {
        h = hashU(h, (unsigned) get_status_of_objective(i));
        dumpf(dump, cap, &n, "[objective %d] status %d\n", i, (int) get_status_of_objective(i));
    }
    h = hashU(h, (unsigned) objectiveregisters1);
    dumpf(dump, cap, &n, "[stage flags] %08x\n", (unsigned) objectiveregisters1);
    {
        extern s32 alarm_timer;

        h = hashU(h, (unsigned) alarm_timer);
        {
            extern unsigned portHeistStateHash(void);   /* port_heist.c: per-player keys, CCTV overrides and switch-offs, the countdown */
            extern const char *portHeistModeStateText(void);   /* the mode script's mode.state text (phase, crack, vault, loot, waves) */
            unsigned hh = portHeistStateHash();

            h = hashU(h, hh);
            dumpf(dump, cap, &n, "[heist] hash %08x mode %s\n", hh, portHeistModeStateText());
        }
        dumpf(dump, cap, &n, "[alarm] timer %d\n", (int) alarm_timer);
    }
    h = portCoopEscortHash(h);   /* which Bond each escort NPC follows (hysteresis state) */
    f[8] = h;
    /* 9 onscreen: the PROPFLAG_ONSCREEN bit of every active prop and the rooms the last pass rendered. This is render-derived state the
     * sim reads (AI, objectives, hit models); the canonical sim view (fr.c) must make it machine-independent. */
    h = 2166136261u;
    {
        extern u8 getROOMID_isRendered(s32 roomID);
        extern s32 getMaxNumRooms(void);
        unsigned on = 0, idx = 0;
        s32 room;

        for (p = chrpropGetActiveTail(); p != NULL; p = p->prev, idx++) {
            if (p->flags & PROPFLAG_ONSCREEN) {
                h = hashU(h, idx);
                on++;
                dumpf(dump, cap, &n, "[onscreen prop] index %u type %d pos %.1f %.1f %.1f\n", idx, (int) p->type, p->pos.x, p->pos.y, p->pos.z);
            }
        }
        {   /* netplay job 7: every Bond's SIM walk (the per-player snapshots the sim reads), not whichever walk was last or whichever Bond is current */
            extern s32 bgSimRoomIsRenderedFor(s32 pl, s32 room);
            s32 pl;

            for (pl = 0; pl < portAllPlayerCount() && pl < 4; pl++) {
                for (room = 1; room < getMaxNumRooms(); room++) {
                    if (bgSimRoomIsRenderedFor(pl, room)) {
                        h = hashU(h, 0x80000000u | ((unsigned) pl << 16) | (unsigned) room);
                    }
                }
            }
        }
        dumpf(dump, cap, &n, "[onscreen] %u of %u active props flagged\n", on, idx);
    }
    f[9] = h;
    /* 10 clock */
    f[10] = hashU(hashU(2166136261u, (unsigned) sFieldNow), sPupTick);
    dumpf(dump, cap, &n, "[clock] fields %d ticks %u\n", sFieldNow, sPupTick);
    /* 11 the per-player settings blocks and the sim-config list (identical on both sides by construction; a difference is a bug) */
    f[11] = portNetSettingsHash(2166136261u);
    dumpf(dump, cap, &n, "[settings] hash %08x\n", f[11]);

    total = 2166136261u;
    for (i = 0; i < PORT_NET_HASH_FIELDS; i++) {
        total = hashU(total, f[i]);
        m->fields[i] = f[i];
    }
    m->nfields = PORT_NET_HASH_FIELDS;
    m->hash = total;
    return 0;
}

#endif
