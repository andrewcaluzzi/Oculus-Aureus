/* port_net.c - netplay: GE_NET parsing, the fake-network knob, the handshake with build and settings
 * verification (job 1), and the lockstep gate, READY/GO and input exchange (job 2). The ENet half is
 * port/netio/port_netio.c (ge_enet library). Wire formats are the pure port_netmsg.c; the schedule and
 * ack logic is the pure port_netlock.c.
 *
 * Nothing here blocks. The handshake is a state machine polled from the boss loop's retrace branch
 * (portNetHold), so the window keeps pumping events and no Windows "not responding" while a partner is
 * missing. Until the handshake is done no sim tick is admitted at all, so both machines count the same
 * menu ticks before the stage launches. In a co-op stage a tick is held until both inputs are here. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <ultra64.h>
#include "port_net.h"
#include "port_netio.h"
#include "port_netmsg.h"
#include "port_netlock.h"
#include "port_nethash.h"
#include "port_modifier.h"
#include "port_modparse.h"
#include "port_lua.h"
#include "port_buildinfo.h"
#include "port_netstats.h"

extern unsigned long long portProfNow(void);   /* pcsched.c: monotonic microseconds */
extern void portConLogf(const char *fmt, ...);
extern s32 portStageRunning(void);                                       /* front.c: a stage is loading or running (not the menus) */
extern s32 get_cur_playernum(void);
extern int portCoopActive(void);
extern const char *portModeChoice(void);                                 /* port_mode.c: GE_MODE if set, else the Mods page's pick */
extern unsigned int portCoopNetTick(void);
extern void portCoopNetPrefill(unsigned int tick, PortNetInput *out);
extern void portCoopReseed(unsigned int seed);
extern void portCoopSessionApplyText(const char *text);                 /* port_coop.c: the co-op session settings (exits, respawn, lives) from "name=value,..." */
extern void portJoyPortNetReset(void);
extern int portCoopStateHash(PortNetHashMsg *m, char *dump, int cap);   /* port_coop.c: the world hash, its fields, a text dump */
extern const char *portCoopStateFieldName(int i);
extern const char *portLogPath(const char *name);                       /* main.c: makes Data/Logs (profile-mapped) */
extern const char *portRootPath(const char *name);
/* the local settings sources (input_pc.c, video.c, options.c; weak defaults in headless builds) */
extern int portInputStyleGet(int slot);
extern int portInputPadIsN64(int slot);
extern int portPlayerOptCfg(int which);
extern int portPlayerOptLive(int which);
extern int portAimLock(void);
extern int portMouseBlocksAutoAim(int player);
extern float portInputMouseAimScale(void);
extern int portJoyPortShootNextPrev(void);       /* joy_port.c */
extern int portJoyPortDetonateUseReload(void);
/* port_simcfg.c: the ini / environment values the sim reads that no settings block carries (job 3c) */
extern void portSimCfgCollect(PortNetSimCfg *c);
extern int portSimCfgDiff(const PortNetSimCfg *mine, const PortNetSimCfg *theirs, char *out, int cap);
extern unsigned int portSimCfgHash(unsigned int h, const PortNetSimCfg *c);
extern int portSimCfgCount(void);
extern const char *portSimCfgName(int i);
static PortNetSimCfg sAgreedCfg;        /* the host's list (the sim's): hashed into the state hash, written to the session log */
static PortNetSimCfg sPartnerSimCfg;    /* the partner's list of the current stage launch (SIMCFG message) */
static unsigned int sPartnerSimCfgRun;
static void netHashResult(int verdict, const PortNetHashMsg *mine, const PortNetHashMsg *theirs);
/* job 4: the next stage of a session, the way back to the title, the console */
extern s32 g_PortNetLaunchStage;                  /* front.c: the guest's launch of the host's next mission (-1 = none) */
extern s32 g_PortNetLaunchDiff;
extern s32 g_PortDirectLaunchRearm;               /* front.c: GE_STAGE launches again once the front end has settled */
extern s32 portSelectedDifficulty(void);          /* front.c */
extern s32 portNetStageValid(s32 id);             /* front.c: a level the direct launch knows */
extern s32 portNetStageMaxId(void);
extern void set_missionstate(int state);          /* mp_music.c */
extern void bossRunTitleStage(void);              /* boss.c */
extern s32 mission_failed_or_aborted;             /* front.c */
extern void portConPrintf(s32 showmsg, const char *fmt, ...);
extern void portNetMusicRelease(void);   /* mp_music.c */
#ifndef _WIN32
extern int putenv(char *);
#endif

/* input_pc.c (video builds) shows the text in the window title; headless builds keep this no-op */
__attribute__((weak)) void portVideoSetWaitText(const char *text)
{
    (void) text;
}

#define NET_DEFAULT_DELAY 3
#define NET_STEP_LIMIT_MS 10000   /* each handshake step */
#define NET_RESEND_MS     12      /* stalled: resend the unacked records at most this often */

enum { NS_IDLE = 0, NS_CONNECT, NS_HOST_HELLO, NS_HOST_ACCEPT, NS_GUEST_SETUP, NS_DONE };

static int sRole;          /* 0 off, 1 host, 2 guest */
static int sPhase;
static int sActive;        /* handshake done */
static int sLost;          /* the partner is gone */
static unsigned int sStepStart;
static int sWaitMs = 120000;
static char sIp[128];
static int sPort;
static PortNetSetup sSetup;   /* host: built at boot; guest: received */
static PortNetHello sHello;   /* guest: what it sent */
static char sMineMods[PORT_NET_STR_MODS];   /* this machine's modifier text, taken at boot on both sides (the modifier
                                             * layer changes once the first stage starts, so the same moment for both) */

static PortNetLock sLock;
static int sLockInit;         /* the lock holds this stage's prefill */
static int sLockOn;           /* GO happened: lockstep drives the sim input */
static int sReadySent;
static int sStageOver;        /* the stage ended (job 3c): no gate and no lock until the next portNetStageBegin */
static unsigned int sRun;                /* stage launches this session (portNetStageBegin) */
static unsigned int sPartnerReadyRun;     /* host: highest READY run from the guest */
static unsigned int sGoRun;               /* guest: highest GO run from the host */
static unsigned int sLastSendMs;
static unsigned int sStallRetraces, sStallTotal, sTicksRun;
static char sWait[96];
static unsigned int sStopTick;   /* GE_NET_STOP (test): leave after this sim tick */
static unsigned int sStopAt;

/* ---- job 4: the next stage of a session, the partner leaving, `net info` ---- */
#define NET_SILENT_MS     5000   /* in a running stage: no datagram (pings count) from the partner for this long = it is gone */
#define NET_SILENT_IDLE_MS 30000 /* in the menus or while it loads a stage: a stage load blocks a machine's loop for seconds */
#define NET_LOST_SHOW_MS  2500   /* how long "Partner disconnected" is shown before the game returns to the title */
static unsigned int sLostMs;        /* when the loss was noticed */
static int sLostDone;               /* the message has been shown and the game sent back to the title */
static int sLostFinish;             /* CO-OP-EXITS: the partner left while a Bond waited at the exit: this stage runs on to its ending first */
extern int portCoopExitWaiting(void);
extern void portCoopPartnerGone(void);
static char sLostWhy[96];
static unsigned int sNextGotRun;    /* guest: the stage launch the host's last NEXTSTAGE was for */
static int sRogue;                  /* guest: it is in a stage the host did not choose; it waits a moment, then returns to the title */
static unsigned int sRogueMs;
/* ---- mission-end job: next-stage handoff that cannot hang ---- */
#define NET_PEND_ABORT_MS   10000   /* guest: the host's NEXTSTAGE arrived while this machine is still in the old stage: wait this long for it to end, then leave it */
#define NET_STAGE_WAIT_MS   60000   /* READY sent and no READY/GO from the partner for this long: end the session cleanly */
#define NET_GATE_STALL_MS   30000   /* in a stage: the partner is linked but sends no input for this long (it left the stage, or stands in a menu): end the session */
#define NET_TAIL_MS         10000   /* after a stage ended: keep resending our unacked last records (the partner may still be a few ticks behind) this long */
static int sPendValid;              /* guest: a NEXTSTAGE received while a stage was still running here, applied when it ends (or after NET_PEND_ABORT_MS) */
static unsigned int sPendMs;
static unsigned int sPendRun, sPendSeed;
static int sPendId, sPendDiff;
static char sPendStage[PORT_NET_STR_STAGE];
static char sPendMode[PORT_NET_STR_MODE];   /* the host's game mode of that stage */
static int sTail;                   /* the stage ended; the lock's records stay for acks / resends */
static unsigned int sTailMs;
static unsigned int sReadyAtMs;     /* when this machine sent READY for the current run */
static unsigned int sReadyResentMs; /* guest: when READY was last repeated */
static unsigned int sGateStallMs0;  /* when the current input stall began (0 = none) */
static unsigned int sLastHashTick;  /* the tick of the last state hash compared with the partner's */
/* ---- job 6: live stats (render only; nothing here reaches the sim) ---- */
static unsigned long long sWaitT0;  /* when the current tick started to be held for the partner's input (0 = not held) */
static float sWaitMsCur;            /* the finished wait of the tick being run, taken by portNetStatsGather */
static unsigned int sStatWaitedPending, sStatResendPending, sStatHashFlag;
static unsigned int sStatResends, sStatOow, sStatDups, sStatLate;   /* session totals: timer resends, records ignored (gap), duplicate records, ticks that waited */
static int sLastVerdict = -1;       /* -1 no hash compared yet, 0 matched, 1 DESYNC */
static unsigned int sDiffMask, sDiffTick;   /* sub-hash fields that differed in the last failed check, and its tick */
static unsigned int sTestEndTick;   /* GE_NET_TEST_ENDSTAGE=<tick> (test, both sides): the first stage ends (abort to the title) after that sim tick */
static char sTestNextStage[40];     /* GE_NET_TEST_NEXT=<stage>:<diff> (test, host): when a stage ends, launch this one without menus */
static int sTestNextDiff;
static int sTestNextArmed;
static unsigned int sTestInfoTick;  /* GE_NET_TEST_INFO=<tick> (test): print `net info` at that sim tick */

/* ---- per-player settings (job 3) ---- */
static PortNetPlayerOpts sMyOpts;    /* this machine's local player, taken at boot */
static PortNetPlayerOpts sOpts[2];   /* [0] the host's Bond, [1] the guest's: what the sim reads */
static int sOptsKnown;               /* both blocks are known (handshake done) */
static int sOptsOff;                 /* GE_NET_OPTS_OFF=1 (test): do not apply them, the pre-job-3 behaviour */
static PortNetPlayerOpts sPartnerReadyOpts;   /* host: the guest's block as its READY of this run carried it */
static PortNetPlayerOpts sGoHostOpts, sGoGuestEcho;   /* guest: what GO carried (the host's block, and the host's copy of ours) */

/* ---- session log and offline replay (job 3b) ---- */
typedef struct NetLogTick
{
    PortNetInput rec[2];                       /* index = player */
    unsigned int hash;
    unsigned int fields[PORT_NET_HASH_FIELDS];
    int has;
} NetLogTick;
static FILE *sLog;                             /* GE_NET_LOG=1: the open log of this stage run */
static int sLogWanted;
static int sReplay;                            /* GE_NET_REPLAY: the sim is driven from a log, offline */
static int sReplayView;
static NetLogTick *sRepTick;                   /* the log's per-tick records and fingerprints, index = tick - 1 */
static unsigned int sRepCount;
static unsigned int sRepBad, sRepChecked, sRepHashes;
static unsigned int sRepFirstBad;
static PortNetHashMsg sTickFp;                 /* this tick's fingerprint, computed by portNetSessionTick */
static char sReplayModsText[PORT_NET_STR_MODS];
static int sReplayHeaderMods;
static unsigned int sRepStopAt;                /* GE_NET_REPLAY_STOP=<tick>: stop early (a prefix check) */
static int sRepHaveCfg;                        /* the log has a K line (sim-config list) */

/* ---- state hash exchange (job 3) ---- */
#define NET_DUMP_CAP 65536
typedef struct NetDump { unsigned int run, tick; char text[NET_DUMP_CAP]; } NetDump;
static NetDump sDumps[8];            /* our world dump of the last hashed ticks, for the DESYNC file */
static unsigned int sDumpNext;
static PortNetHashBook sBook;
static unsigned int sHashSent, sHashCompared, sHashBad, sDesyncFiles;
static unsigned int sTestDesyncTick;   /* GE_NET_TEST_DESYNC */

static const char *styleName(int s)
{
    static const char *const n[8] = { "1.1", "1.2", "1.3", "1.4", "2.1", "2.2", "2.3", "2.4" };

    return (s >= 0 && s < 8) ? n[s] : "?";
}

static void netOptsText(const PortNetPlayerOpts *o, char *out, int cap)
{
    snprintf(out, (size_t) cap, "style %s n64 %d upright %d autoaim %d lookahead %d aimtoggle %d aimlock %d mouseaimoff %d aimsens %u shootnextprev %d detonatereload %d",
             styleName(o->controlStyle), o->padN64, o->lookUpright, o->autoAim, o->lookAhead, o->aimToggle, o->aimLock,
             o->mouseAimOff, (unsigned) o->aimSens, o->shootNextPrev, o->detonateUseReload);
}

static int optFromCfg(int which)
{
    int v = portPlayerOptCfg(which);   /* midas.ini; -1 = none established */

    return (v >= 0) ? (v != 0) : (portPlayerOptLive(which) != 0);
}

/* GE_NET_OPTS=style=2,n64=1,upright=0,autoaim=1,lookahead=0,aimtoggle=1,aimlock=0,mouseaimoff=0 (test): override fields. */
static void netOptsEnv(PortNetPlayerOpts *o, const char *name)
{
    const char *e = getenv(name);
    char tmp[200];
    char *tok;

    if (e == NULL || e[0] == '\0') {
        return;
    }
    snprintf(tmp, sizeof(tmp), "%s", e);
    for (tok = strtok(tmp, ","); tok != NULL; tok = strtok(NULL, ",")) {
        char *eq = strchr(tok, '=');
        int v;

        if (eq == NULL) {
            continue;
        }
        *eq = '\0';
        v = atoi(eq + 1);
        if (strcmp(tok, "style") == 0) o->controlStyle = (unsigned char) (v & 7);
        else if (strcmp(tok, "n64") == 0) o->padN64 = (unsigned char) (v != 0);
        else if (strcmp(tok, "upright") == 0) o->lookUpright = (unsigned char) (v != 0);
        else if (strcmp(tok, "autoaim") == 0) o->autoAim = (unsigned char) (v != 0);
        else if (strcmp(tok, "lookahead") == 0) o->lookAhead = (unsigned char) (v != 0);
        else if (strcmp(tok, "aimtoggle") == 0) o->aimToggle = (unsigned char) (v != 0);
        else if (strcmp(tok, "aimlock") == 0) o->aimLock = (unsigned char) (v != 0);
        else if (strcmp(tok, "mouseaimoff") == 0) o->mouseAimOff = (unsigned char) (v != 0);
        else if (strcmp(tok, "aimsens") == 0) o->aimSens = (unsigned short) v;
        else if (strcmp(tok, "shootnextprev") == 0) o->shootNextPrev = (unsigned char) (v != 0);
        else if (strcmp(tok, "detonatereload") == 0) o->detonateUseReload = (unsigned char) (v != 0);
    }
}

/* This machine's settings for its local Bond. The local pad is device slot 0 whichever Bond it drives (the keyboard and
 * pad 0 are swapped into the local Bond's slot), so slot 0 is asked, not the player index. Taken at boot, after the ini. */
static void netLocalOpts(PortNetPlayerOpts *o, int atReady)
{
    int st = portInputStyleGet(0);   /* padN_style of the local pad, -1 = auto */

    memset(o, 0, sizeof(*o));
    o->controlStyle = (unsigned char) ((st >= 0 && st < 8) ? st : 0);
    o->padN64 = (unsigned char) (portInputPadIsN64(0) != 0);
    o->lookUpright = (unsigned char) optFromCfg(0);   /* PLAYER_OPTION_LOOK */
    o->autoAim = (unsigned char) optFromCfg(1);       /* PLAYER_OPTION_AUTOAIM */
    o->aimToggle = (unsigned char) optFromCfg(2);     /* PLAYER_OPTION_AIM */
    o->lookAhead = (unsigned char) optFromCfg(4);     /* PLAYER_OPTION_LOOKAHEAD */
    o->aimLock = (unsigned char) (portAimLock() != 0);
    o->mouseAimOff = (unsigned char) (portMouseBlocksAutoAim(0) != 0);
    {
        const float sens = portInputMouseAimScale();   /* midas.ini mouse_sens_aim, thousandths */

        o->aimSens = (unsigned short) ((sens > 0.0f && sens < 65.0f) ? (int) (sens * 1000.0f + 0.5f) : 1000);
    }
    o->shootNextPrev = (unsigned char) (portJoyPortShootNextPrev() != 0);
    o->detonateUseReload = (unsigned char) (portJoyPortDetonateUseReload() != 0);
    netOptsEnv(o, "GE_NET_OPTS");
    if (atReady) {   /* GE_NET_OPTS_READY (test): more overrides that apply only to the block taken at READY, to prove the refresh */
        netOptsEnv(o, "GE_NET_OPTS_READY");
    }
}

/* The settings block depends on devices (input mode, who owns player 1) that SDL enumerates a moment after start-up,
 * so the handshake waits this long from boot before it samples them. */
#define NET_SETTLE_MS 1500
static unsigned int sBootMs;

static int netSettled(void)
{
    return (int) (netioNowMs() - sBootMs) >= NET_SETTLE_MS;
}

static const PortNetPlayerOpts *netOptsFor(int player)
{
    if (player < 0 || player > 1 || !portCoopActive()) {
        return NULL;
    }
    if (sReplay) {
        return &sOpts[player];   /* the log's blocks, exactly as the session had them */
    }
    if (sRole == 0 || !sActive || !sOptsKnown || sOptsOff) {
        return NULL;
    }
    return &sOpts[player];
}

int portNetOpt(int player, int which)
{
    const PortNetPlayerOpts *o = netOptsFor(player);

    if (o == NULL) {
        return -1;
    }
    switch (which) {
    case PORT_NETOPT_STYLE:
        if (o->padN64) {
            return o->controlStyle;
        } else {   /* a port-driven pad always plays 1.1 or 1.2 (options.c cur_player_get_control_type) */
            const int s = o->controlStyle;

            return (s == 1 || s == 3 || s == 5 || s == 7) ? 1 : 0;
        }
    case PORT_NETOPT_PADN64: return o->padN64;
    case PORT_NETOPT_LOOK: return o->lookUpright;
    case PORT_NETOPT_AUTOAIM: return o->autoAim;
    case PORT_NETOPT_LOOKAHEAD: return o->lookAhead;
    case PORT_NETOPT_AIMCTL: return o->aimToggle;
    case PORT_NETOPT_AIMLOCK: return o->aimLock;
    case PORT_NETOPT_MOUSEAIMOFF: return o->mouseAimOff;
    case PORT_NETOPT_AIMSENS: return o->aimSens;
    case PORT_NETOPT_SHOOTNEXTPREV: return o->shootNextPrev;
    case PORT_NETOPT_DETONATEUR: return o->detonateUseReload;
    default: return -1;
    }
}

int portSimPadIsN64(int player)
{
    const int v = portNetOpt(player, PORT_NETOPT_PADN64);

    return (v >= 0) ? v : portInputPadIsN64(player);
}

unsigned int portNetSettingsHash(unsigned int h)
{
    h = portNetOptsHash(h, &sOpts[0]);
    h = portNetOptsHash(h, &sOpts[1]);
    return portSimCfgHash(h, &sAgreedCfg);
}

int portNetTestDesync(unsigned int tick)
{
    return sTestDesyncTick != 0 && tick == sTestDesyncTick;
}

int portNetTestExtraDraw(unsigned int tick)
{
    static unsigned int at = 0xFFFFFFFFu;

    if (at == 0xFFFFFFFFu) {
        const char *e = getenv("GE_NET_TEST_EXTRADRAW");

        at = (e != NULL && e[0] != '\0') ? (unsigned int) atol(e) : 0u;
    }
    return at != 0 && tick == at;
}

static const char *netBuildId(void)
{
    static char id[PORT_NET_STR_BUILD];
    const char *t = getenv("GE_NET_TEST_BUILD");   /* test: pretend to be another build */

    if (t != NULL && t[0] != '\0') {
        snprintf(id, sizeof(id), "%s", t);
    } else {
        snprintf(id, sizeof(id), "%s %s", PORT_BUILD_STRING, PORT_BUILD_COMMIT);
    }
    return id;
}

static void netSetenv(const char *key, const char *val)
{
    char *s = (char *) malloc(strlen(key) + strlen(val) + 2);   /* putenv keeps the pointer: never freed */

    sprintf(s, "%s=%s", key, val);
    putenv(s);
}

static void netFail(const char *why)
{
    portConLogf("ge: net: %s\n", why);
    netioShutdown();
    exit(1);
}

/* "Waiting for partner..." in the window title (and the log, once per change). Empty text clears it. */
static void netWaiting(const char *text)
{
    if (strcmp(sWait, text) == 0) {
        return;
    }
    snprintf(sWait, sizeof(sWait), "%s", text);
    if (text[0] != '\0') {
        portConLogf("ge: net: %s\n", text);
    }
    portVideoSetWaitText(text);
}

static int cmpName(const void *a, const void *b)
{
    return strcmp((const char *) a, (const char *) b);
}

/* FNV over the sorted names and bytes of Data/Scripts/*.lua, Data/Scripts/boot/*.lua and Data/Scripts/modes/*.lua (the mode scripts run in
 * co-op since the modes-in-co-op job, so both machines must have the same ones). 0 files hashes to the FNV basis. */
static int sHashFiles;   /* files the last netLuaHash() read (the log line says it) */

static unsigned int netHashDir(unsigned int h, const char *dir, const char *tag)
{
    static char names[64][64];
    int n = 0, i;
    DIR *d = opendir(dir);
    struct dirent *e;

    if (d == NULL) {
        return h;
    }
    while ((e = readdir(d)) != NULL && n < 64) {
        size_t l = strlen(e->d_name);

        if (l > 4 && l < 63 && strcmp(e->d_name + l - 4, ".lua") == 0) {
            strcpy(names[n++], e->d_name);
        }
    }
    closedir(d);
    qsort(names, (size_t) n, 64, cmpName);
    for (i = 0; i < n; i++) {
        char path[1200];
        FILE *f;

        sHashFiles++;
        h = portNetFnv(h, tag, (int) strlen(tag));
        h = portNetFnv(h, names[i], (int) strlen(names[i]));
        snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
        f = fopen(path, "rb");
        if (f != NULL) {
            unsigned char buf[1024];
            size_t got;

            while ((got = fread(buf, 1, sizeof(buf), f)) > 0) {
                h = portNetFnv(h, buf, (int) got);
            }
            fclose(f);
        }
    }
    return h;
}

static unsigned int netLuaHash(void)
{
    char sub[1200];
    unsigned int h = 2166136261u;
    const char *dir = portLuaGameScriptDir();
    const char *forced = getenv("GE_NET_TEST_LUAHASH");   /* test only: this side reports another script-set hash, to prove the session is refused */

    sHashFiles = 0;
    h = netHashDir(h, dir, "s/");
    snprintf(sub, sizeof(sub), "%s/boot", dir);
    h = netHashDir(h, sub, "b/");
    snprintf(sub, sizeof(sub), "%s/modes", dir);
    h = netHashDir(h, sub, "m/");
    if (forced != NULL && forced[0] != '\0') {
        h = (unsigned int) strtoul(forced, NULL, 16);
    }
    portConLogf("ge: net: script set hash %08x (%d files in Data/Scripts, boot/ and modes/)\n", h, sHashFiles);
    return h;
}

/* The modifier values that stand (the ini layer) plus the GE_MODS launch text, as "name=value,..." for
 * every non-default one; the same function runs on both sides. */
static void netModsText(char *out, int cap)
{
    int i, n = 0;
    const char *env = getenv("GE_MODS");

    out[0] = '\0';
    for (i = 0; i < portModifierCount(); i++) {
        const PortModifierInfo *m = portModifierAt(i);
        int v = portModifierGet(i);

        if (portModifierIsSession(i)) {
            continue;   /* the co-op session settings are the HOST's (they travel after SESSION_MARK, not compared) */
        }
        if (m != NULL && v != m->def) {
            n += snprintf(out + n, (size_t) (cap - n), "%s%s=%d", n > 0 ? "," : "", m->name, v);
            if (n >= cap - 1) {
                out[cap - 1] = '\0';
                return;
            }
        }
    }
    if (env != NULL && env[0] != '\0') {
        /* GE_MODS without its co-op session items (those are the host's, whatever each machine's environment says) */
        char kept[PORT_NET_STR_MODS];
        const char *p = env;
        char name[40], val[24];
        int k = 0;

        kept[0] = '\0';
        while (portModListNext(&p, name, sizeof(name), val, sizeof(val))) {
            int idx = portModifierFind(name);

            if (idx >= 0 && portModifierIsSession(idx)) {
                continue;
            }
            k += snprintf(kept + k, sizeof(kept) - (size_t) k, "%s%s%s%s", k > 0 ? "," : "", name, val[0] != '\0' ? "=" : "", val);
            if (k >= (int) sizeof(kept) - 40) {
                break;
            }
        }
        if (kept[0] != '\0') {
            snprintf(out + n, (size_t) (cap - n), "+env:%s", kept);
        }
    }
}

/* SETUP's modifier text = the common text (compared) + SESSION_MARK + the host's co-op session settings (adopted by the guest). */
#define NET_SESSION_MARK "|session:"

/* the part of a SETUP / log modifier text before the mark, copied into out */
static void netModsCommon(const char *full, char *out, int cap)
{
    const char *m = strstr(full, NET_SESSION_MARK);
    int len = m != NULL ? (int) (m - full) : (int) strlen(full);

    if (len > cap - 1) {
        len = cap - 1;
    }
    memcpy(out, full, (size_t) len);
    out[len] = '\0';
}

static int netModsDiffer(const char *hostFull, const char *mine)
{
    char common[PORT_NET_STR_MODS];

    netModsCommon(hostFull, common, sizeof(common));
    return strcmp(common, mine) != 0;
}

/* ---- the game mode of the session (modes-in-co-op job). The HOST chooses it (GE_MODE or its Mods page, portModeChoice); it rides in the
 * session part of SETUP's modifier text as a "mode=<name>" token (stage 1) and in NEXTSTAGE (every later stage). The guest takes it: it
 * sets GE_MODE in its own environment before the stage loads, which portModeStageStart reads first, and which also greys the Mods row.
 * The host's own environment is never written (its Mods row stays usable for the next stage). ---- */
static char sCurMode[PORT_NET_STR_MODE];   /* this stage's mode ("" = none) */

/* rewrites the "mode=" token of the session part of a SETUP / log modifier text (adds the part when there is none) */
static void netModsSetModeToken(char *mods, int cap, const char *mode)
{
    char *mark = strstr(mods, NET_SESSION_MARK);
    char cur[PORT_NET_STR_MODS], out[PORT_NET_STR_MODS];
    const char *p;
    int n = 0;

    if (mark == NULL) {
        mark = mods + strlen(mods);
        cur[0] = '\0';
    } else {
        snprintf(cur, sizeof(cur), "%s", mark + strlen(NET_SESSION_MARK));
    }
    out[0] = '\0';
    for (p = cur; *p != '\0';) {
        char tok[96];
        int k = 0;

        while (*p != '\0' && *p != ',' && k < (int) sizeof(tok) - 1) {
            tok[k++] = *p++;
        }
        tok[k] = '\0';
        if (*p == ',') {
            p++;
        }
        if (k > 0 && strncmp(tok, "mode=", 5) != 0) {
            n += snprintf(out + n, sizeof(out) - (size_t) n, "%s%s", n > 0 ? "," : "", tok);
        }
    }
    if (mode[0] != '\0') {
        n += snprintf(out + n, sizeof(out) - (size_t) n, "%smode=%s", n > 0 ? "," : "", mode);
    }
    snprintf(mark, (size_t) (cap - (int) (mark - mods)), "%s%s", NET_SESSION_MARK, out);
}

/* host: this stage's mode is whatever its environment / Mods page says now; rewrite SETUP's text (it is logged, the guest of a later
 * stage never reads it again: NEXTSTAGE carries the mode) */
static void netHostChooseMode(void)
{
    const char *c = portModeChoice();

    if (sRun > 1u) {   /* test only: GE_NET_TEST_NEXTMODE=<name|0> changes the host's mode for the stages after the first (the Mods page would) */
        const char *t = getenv("GE_NET_TEST_NEXTMODE");

        if (t != NULL && t[0] != '\0') {
            netSetenv("GE_MODE", t);
            c = t;
        }
    }
    sCurMode[0] = '\0';
    if (c != NULL && c[0] != '\0' && strcmp(c, "0") != 0) {
        if (portNetModeValid(c)) {
            snprintf(sCurMode, sizeof(sCurMode), "%s", c);
        } else {
            portConLogf("ge: net: the game mode '%s' is not a legal name: none this stage\n", c);
        }
    }
    netModsSetModeToken(sSetup.mods, (int) sizeof(sSetup.mods), sCurMode);
    portConLogf("ge: net: session game mode: '%s'\n", sCurMode);
}

/* guest (and an offline replay): take the mode, GE_MODE "0" = none, so neither a stray GE_MODE nor this machine's Mods page can differ from the host */
static void netTakeMode(const char *mode)
{
    snprintf(sCurMode, sizeof(sCurMode), "%s", portNetModeValid(mode) ? mode : "");
    netSetenv("GE_MODE", sCurMode[0] != '\0' ? sCurMode : "0");
    portConLogf("ge: net: session game mode: '%s' (the host's)\n", sCurMode);
}

/* hand the session part of a modifier text (or nothing: the defaults) to the co-op rules, and take its game mode */
static void netApplySessionMods(const char *full)
{
    const char *m = strstr(full, NET_SESSION_MARK);
    const char *s = m != NULL ? m + strlen(NET_SESSION_MARK) : "";
    const char *p;
    char mode[PORT_NET_STR_MODE];

    portCoopSessionApplyText(s);
    mode[0] = '\0';
    for (p = s; *p != '\0';) {   /* the token "mode=<name>", at the start or after a comma (not the tail of coop_lives_mode=) */
        if (strncmp(p, "mode=", 5) == 0) {
            int k = 0;

            for (p += 5; *p != '\0' && *p != ',' && k < (int) sizeof(mode) - 1; p++) {
                mode[k++] = *p;
            }
            mode[k] = '\0';
            break;
        }
        while (*p != '\0' && *p != ',') {
            p++;
        }
        if (*p == ',') {
            p++;
        }
    }
    netTakeMode(mode);
}

/* One waiting session-channel message (input-channel packets are dropped before the lock exists). */
static int netPollSession(unsigned char *buf, int cap)
{
    int ch = 0, n;

    for (;;) {
        n = netioRecv(&ch, buf, cap);
        if (n <= 0) {
            return 0;
        }
        if (ch == NETIO_CH_SESSION) {
            return n;
        }
    }
}

static void netSendSession(const unsigned char *buf, int n)
{
    netioSend(NETIO_CH_SESSION, buf, n);
}

/* Lets the queue and ENet flush a final message, then returns (the caller exits right after). */
static void netSendAndLeave(const unsigned char *buf, int n)
{
    int i;

    netSendSession(buf, n);
    for (i = 0; i < 40; i++) {   /* up to 200 ms, enough for the fake lag knob's own 100 ms */
        netioService();
        netioSleepMs(5);
    }
}

/* The launch settings that only the SETUP knows; the stage launch (front.c) reads them after the first menu ticks. */
static void netApplySetup(const PortNetSetup *s)
{
    char v[32];

    netSetenv("GE_STAGE", s->stage);
    snprintf(v, sizeof(v), "%d", s->diff);
    netSetenv("GE_DIFF", v);
    snprintf(v, sizeof(v), "%u", s->seed);
    netSetenv("GE_SEED", v);
    netApplySessionMods(s->mods);
}

static void netHandshakeOk(const char *who)
{
    sActive = 1;
    sPhase = NS_DONE;
    portConLogf("ge: net: handshake ok (%s): stage %s diff %d seed %u delay %d lua %08x mods '%s'\n", who, sSetup.stage,
                sSetup.diff, sSetup.seed, sSetup.delay, sSetup.luaHash, sSetup.mods);
    netWaiting("");
    {
        char a[160], b[160];

        netOptsText(&sOpts[0], a, sizeof(a));
        netOptsText(&sOpts[1], b, sizeof(b));
        portConLogf("ge: net: player settings%s: host (player 0) %s | guest (player 1) %s\n",
                    sOptsOff ? " (NOT applied: GE_NET_OPTS_OFF)" : "", a, b);
    }
}

static void netStep(int phase, const char *waitText)
{
    sPhase = phase;
    sStepStart = netioNowMs();
    netWaiting(waitText);
}

static int netStepOver(int limitMs)
{
    return (int) (netioNowMs() - sStepStart) > limitMs;
}

static void netLostCheck(const char *what)
{
    if (netioState() == NETIO_LOST || netioState() == NETIO_DOWN) {
        char m[96];

        snprintf(m, sizeof(m), "connection lost while waiting for %s", what);
        netFail(m);
    }
}

/* One non-blocking step of the handshake. Refusals and timeouts end the process as in job 1. */
static void netHandshake(void)
{
    unsigned char buf[PORT_NET_BUF_MAX];
    char why[PORT_NET_STR_REASON];
    int n;

    switch (sPhase) {
    case NS_CONNECT:
        if (netioState() == NETIO_CONNECTED) {
            if (sRole == 1) {
                portConLogf("ge: net: guest connected\n");
                netStep(NS_HOST_HELLO, "Waiting for partner (handshake)...");
            } else if (netSettled()) {   /* the pads are enumerated: take the settings block now, not at boot */
                netLocalOpts(&sMyOpts, 0);
                sHello.opts = sMyOpts;
                n = portNetHelloPack(&sHello, buf, sizeof(buf));
                netSendSession(buf, n);
                netStep(NS_GUEST_SETUP, "Waiting for partner (handshake)...");
            }
        } else if (sRole == 2 && netioState() == NETIO_LOST) {
            netFail("could not reach the host");
        } else if ((int) (netioNowMs() - sStepStart) > sWaitMs) {
            netFail(sRole == 1 ? "no guest connected" : "the host did not answer");
        }
        break;
    case NS_HOST_HELLO: {
        PortNetHello hello;

        netLostCheck("HELLO");
        if (!netSettled()) {
            break;   /* the pads are not enumerated yet: the settings block is taken when the HELLO is handled */
        }
        n = netPollSession(buf, sizeof(buf));
        if (n == 0) {
            if (netStepOver(NET_STEP_LIMIT_MS)) {
                netFail("timed out waiting for HELLO");
            }
            break;
        }
        if (portNetHelloUnpack(buf, n, &hello) != 0) {
            netFail("malformed HELLO");
        }
        why[0] = '\0';
        if (hello.proto != PORT_NET_PROTO) {
            snprintf(why, sizeof(why), "protocol mismatch: host %d, guest %d", PORT_NET_PROTO, hello.proto);
        } else if (strcmp(hello.build, sSetup.build) != 0) {
            snprintf(why, sizeof(why), "build mismatch: host '%s', guest '%s'", sSetup.build, hello.build);
        }
        if (why[0] != '\0') {
            n = portNetReasonPack(PORT_NETMSG_REFUSE, why, buf, sizeof(buf));
            netSendAndLeave(buf, n);
            portConLogf("ge: net: refused guest: %s\n", why);
            netioShutdown();
            exit(1);
        }
        netLocalOpts(&sMyOpts, 0);
        portSimCfgCollect(&sSetup.simCfg);   /* job 3c: the ini values the sim reads that no block carries */
        sAgreedCfg = sSetup.simCfg;
        sSetup.hostOpts = sMyOpts;
        sSetup.guestOpts = hello.opts;   /* echoed back: the guest verifies the host understood its settings */
        sOpts[0] = sMyOpts;
        sOpts[1] = hello.opts;
        n = portNetSetupPack(&sSetup, buf, sizeof(buf));
        netSendSession(buf, n);
        netStep(NS_HOST_ACCEPT, "Waiting for partner (handshake)...");
        break;
    }
    case NS_HOST_ACCEPT:
        netLostCheck("the guest's answer");
        n = netPollSession(buf, sizeof(buf));
        if (n == 0) {
            if (netStepOver(NET_STEP_LIMIT_MS)) {
                netFail("timed out waiting for the guest's answer");
            }
            break;
        }
        if (portNetMsgType(buf, n) == PORT_NETMSG_REFUSE) {
            portNetReasonUnpack(buf, n, why, sizeof(why));
            portConLogf("ge: net: guest refused the session: %s\n", why);
            netioShutdown();
            exit(1);
        }
        if (portNetMsgType(buf, n) != PORT_NETMSG_ACCEPT) {
            netFail("unexpected message instead of ACCEPT");
        }
        netApplySetup(&sSetup);
        sOptsKnown = 1;
        netHandshakeOk("host");
        break;
    case NS_GUEST_SETUP: {
        unsigned int luaHash;
        PortNetSetup setup;

        netLostCheck("SETUP");
        n = netPollSession(buf, sizeof(buf));
        if (n == 0) {
            if (netStepOver(NET_STEP_LIMIT_MS)) {
                netFail("timed out waiting for SETUP");
            }
            break;
        }
        if (portNetMsgType(buf, n) == PORT_NETMSG_REFUSE) {
            portNetReasonUnpack(buf, n, why, sizeof(why));
            portConLogf("ge: net: host refused the session: %s\n", why);
            netioShutdown();
            exit(1);
        }
        if (portNetSetupUnpack(buf, n, &setup) != 0) {
            netFail("malformed SETUP");
        }
        luaHash = netLuaHash();
        why[0] = '\0';
        if (setup.proto != PORT_NET_PROTO) {
            snprintf(why, sizeof(why), "protocol mismatch: host %d, guest %d", setup.proto, PORT_NET_PROTO);
        } else if (strcmp(setup.build, sHello.build) != 0) {
            snprintf(why, sizeof(why), "build mismatch: host '%s', guest '%s'", setup.build, sHello.build);
        } else if (netModsDiffer(setup.mods, sMineMods)) {
            char hostCommon[PORT_NET_STR_MODS];

            netModsCommon(setup.mods, hostCommon, sizeof(hostCommon));
            snprintf(why, sizeof(why), "modifier mismatch: host '%s', guest '%s'", hostCommon, sMineMods);
        } else if (setup.luaHash != luaHash) {
            snprintf(why, sizeof(why), "Lua script set mismatch: host %08x, guest %08x", setup.luaHash, luaHash);
        } else if (!portNetOptsEqual(&setup.guestOpts, &sMyOpts)) {
            snprintf(why, sizeof(why), "settings echo mismatch: the host holds other settings for the guest than it was sent");
        } else {
            PortNetSimCfg mine;
            char diff[400];

            portSimCfgCollect(&mine);
            if (portSimCfgDiff(&mine, &setup.simCfg, diff, sizeof(diff)) != 0) {   /* job 3c: an ini value the sim reads differs */
                portConLogf("ge: net: the sim settings differ between the two machines: %s (host's value is 'theirs'; fix midas.ini)\n", diff);
                snprintf(why, sizeof(why), "sim setting differs: %s", diff);
            }
        }
        if (why[0] != '\0') {
            n = portNetReasonPack(PORT_NETMSG_REFUSE, why, buf, sizeof(buf));
            netSendAndLeave(buf, n);
            portConLogf("ge: net: refusing the session: %s\n", why);
            netioShutdown();
            exit(1);
        }
        n = portNetAcceptPack(buf, sizeof(buf));
        netSendSession(buf, n);
        sSetup = setup;
        sAgreedCfg = setup.simCfg;
        sOpts[0] = setup.hostOpts;
        sOpts[1] = sMyOpts;
        sOptsKnown = 1;
        netApplySetup(&sSetup);
        netHandshakeOk("guest");
        break;
    }
    default:
        break;
    }
}

/* ---- session commands (job 5) -------------------------------------------------------------------------------------------
 * A game command (spawn, give) that both machines apply at the SAME sim tick, at the start of the tick before any Bond moves
 * (portCoopDeathTick, boss.c). Either side may issue one; it is applied by the issuer too only at its tick, never at once.
 * Delivery: the CMD message rides the reliable channel; every INPUT packet carries how many commands its sender has issued, and a
 * machine does not start a tick until it holds that many from the partner, so a command can never arrive after its tick (a record for
 * tick T is pushed at least `delay` ticks after the issue, so the count in the packet carrying it already includes every command whose
 * tick is <= T). Order within a tick: host's first, then the guest's, each by sequence number. */
#define NET_CMD_MAX 48
#define NET_CMD_MARGIN 2          /* ticks beyond issue tick + input delay */
#define NET_CMD_AHEAD_MAX 600
static PortNetCmd sCmdQ[NET_CMD_MAX];
static int sCmdN;
extern int g_PortResim;   /* port_snap.c: 1 while a rollback re-simulates a tick */
static PortNetCmd sCmdHist[32];   /* rollback R2: the commands applied at the last ticks (portNetSessionCmds) */
static unsigned int sCmdHistN;
static unsigned int sCmdIssued, sCmdGot, sCmdTheirs;   /* per run: issued here, received, the partner's issued count as last announced */
static PortNetCmd sRepCmd[256];
static unsigned int sRepCmdCount, sRepCmdNext;
extern void portSessionCmdExec(const PortNetCmd *c);   /* port_netcmd.c: the game side of a command */
static void netSendInput(void);

static void netCmdText(const PortNetCmd *c, char *out, int cap)
{
    snprintf(out, (size_t) cap, "#%u from %s: kind %d player %d a %d b %d", c->seq, c->issuer == 0 ? "host" : "guest", c->kind, c->player, c->a, c->b);
}

static void netCmdQueue(const PortNetCmd *c)
{
    if (sCmdN >= NET_CMD_MAX) {
        portConLogf("ge: net: session command queue full, a command was dropped\n");
        return;
    }
    sCmdQ[sCmdN++] = *c;
}

static void netCmdReset(void)
{
    sCmdN = 0;
    sCmdHistN = 0;
    sCmdIssued = sCmdGot = sCmdTheirs = 0;
    sRepCmdNext = 0;
}

static void netOnCmd(const unsigned char *buf, int n)
{
    PortNetCmd c;
    char t[160];

    if (!sLockInit || sReplay) {
        return;
    }
    if (portNetCmdUnpack(buf, n, &c) != 0) {
        portConLogf("ge: net: refused a malformed or out-of-range session command (%d bytes)\n", n);
        return;
    }
    if (c.run != sRun || c.issuer != (sRole == 1 ? 1 : 0) || c.seq != sCmdGot + 1u) {
        portConLogf("ge: net: ignored a session command of the wrong run, issuer or sequence (run %u/%u issuer %d seq %u, wanted %u)\n", c.run, sRun, c.issuer, c.seq, sCmdGot + 1u);
        return;
    }
    if (c.tick > portCoopNetTick() + NET_CMD_AHEAD_MAX) {
        portConLogf("ge: net: refused a session command too far ahead (tick %u, now %u)\n", c.tick, portCoopNetTick());
        return;
    }
    sCmdGot++;
    if (c.tick <= portCoopNetTick()) {
        portConLogf("ge: net: WARNING a session command arrived after its tick (%u, now %u): it is applied late\n", c.tick, portCoopNetTick());
    }
    netCmdText(&c, t, sizeof(t));
    portConLogf("ge: net: session command received %s, for tick %u\n", t, c.tick);
    netCmdQueue(&c);
}

/* Console side: 1 when a session command can be issued now (a live lockstep stage). */
int portNetCmdLive(void)
{
    return sRole != 0 && !sReplay && sActive && !sLost && sLockOn && sLockInit && portStageRunning();
}

/* The player index (0 host, 1 guest) of this machine's Bond, or -1 when not in netplay. */
int portNetCmdSelf(void)
{
    return sRole == 1 ? 0 : (sRole == 2 ? 1 : -1);
}

/* Issues a command; it is applied by both sides at tick now + delay + margin. Returns 0 or -1 (with `why`). */
int portNetCmdIssue(int kind, int player, int a, int b, char *why, int whycap)
{
    PortNetCmd c;
    unsigned char buf[PORT_NET_BUF_MAX];
    char t[160];
    int n;

    if (!portNetCmdLive()) {
        snprintf(why, (size_t) whycap, "netplay is not running a stage right now");
        return -1;
    }
    if (sCmdN >= NET_CMD_MAX - 4) {
        snprintf(why, (size_t) whycap, "too many session commands waiting");
        return -1;
    }
    memset(&c, 0, sizeof(c));
    c.run = sRun;
    c.tick = portCoopNetTick() + (unsigned int) portNetDelay() + NET_CMD_MARGIN;
    c.seq = sCmdIssued + 1u;
    c.issuer = portNetCmdSelf();
    c.kind = kind;
    c.player = player;
    c.a = a;
    c.b = b;
    n = portNetCmdPack(&c, buf, sizeof(buf));
    if (n <= 0) {
        snprintf(why, (size_t) whycap, "the command is out of range");
        return -1;
    }
    sCmdIssued++;
    netCmdQueue(&c);
    netSendSession(buf, n);
    netSendInput();   /* announces the new count at once */
    netCmdText(&c, t, sizeof(t));
    portConLogf("ge: net: session command issued %s, applies at tick %u on both machines\n", t, c.tick);
    return 0;
}

/* Rollback R2: the commands applied at the last ticks, so a re-simulated tick applies the same ones again (the queue above is
 * consumed when a command is applied; a rewound sim would otherwise lose it). */
/* boss.c via portCoopDeathTick, at the start of sim tick `tick`: apply the commands due, in the agreed order. */
void portNetSessionCmds(unsigned int tick)
{
    PortNetCmd todo[NET_CMD_MAX];
    int n = 0, i, j;

    if (g_PortResim) {   /* a re-simulated tick: the commands it applied the first time, from the history */
        unsigned int k;

        for (k = sCmdHistN > 32u ? sCmdHistN - 32u : 0u; k < sCmdHistN; k++) {
            if (sCmdHist[k % 32u].tick == tick && n < NET_CMD_MAX) {
                todo[n++] = sCmdHist[k % 32u];
            }
        }
        for (i = 0; i < n; i++) {
            portSessionCmdExec(&todo[i]);
        }
        return;
    }
    if (sReplay) {
        while (sRepCmdNext < sRepCmdCount && sRepCmd[sRepCmdNext].tick <= tick && n < NET_CMD_MAX) {
            todo[n++] = sRepCmd[sRepCmdNext++];
        }
    } else {
        for (i = 0; i < sCmdN;) {
            if (sCmdQ[i].tick <= tick && n < NET_CMD_MAX) {
                todo[n++] = sCmdQ[i];
                sCmdQ[i] = sCmdQ[--sCmdN];
            } else {
                i++;
            }
        }
    }
    for (i = 1; i < n; i++) {   /* insertion sort by (issuer, seq): host first */
        PortNetCmd k = todo[i];

        for (j = i - 1; j >= 0 && (todo[j].issuer > k.issuer || (todo[j].issuer == k.issuer && todo[j].seq > k.seq)); j--) {
            todo[j + 1] = todo[j];
        }
        todo[j + 1] = k;
    }
    for (i = 0; i < n; i++) {
        char t[160];

        todo[i].tick = tick;
        sCmdHist[sCmdHistN++ % 32u] = todo[i];
        netCmdText(&todo[i], t, sizeof(t));
        portConLogf("ge: net: session command %s applied at tick %u\n", t, tick);
        if (sLog != NULL) {
            fprintf(sLog, "C %u %d %u %d %d %d %d\n", tick, todo[i].issuer, todo[i].seq, todo[i].kind, todo[i].player, todo[i].a, todo[i].b);
            fflush(sLog);
        }
        portSessionCmdExec(&todo[i]);
    }
}

/* ---- lockstep ------------------------------------------------------------------------------ */

static void netSendInput(void)
{
    PortNetInputPkt p;
    unsigned char buf[PORT_NET_BUF_MAX];
    int n;

    portNetLockBuildPkt(&sLock, &p);
    p.run = sRun;
    p.cmds = sCmdIssued;
    n = portNetInputPack(&p, buf, sizeof(buf));
    if (n > 0) {
        netioSend(NETIO_CH_INPUT, buf, n);
    }
    netioService();   /* flush now, do not wait for the next retrace */
    sLastSendMs = netioNowMs();
}

/* The game's own randomSetSeed calls at a stage start take their seed from GE_SEED when it is set (n64_random.c), so the session's seed
 * for a stage must be in the environment too, not only in portCoopReseed. */
static void netSetSeedEnv(unsigned int seed)
{
    char v[16];

    snprintf(v, sizeof(v), "%u", seed);
    netSetenv("GE_SEED", v);
}

/* Guest: launch the host's stage held in sPend* from the menus (front.c portDirectLaunchTick). The host's run number is authoritative: this machine's
 * counter is set to it (a guest that went through a refused / aborted launch of its own must not be a run ahead and then call the host's stage rogue). */
static void netApplyNext(void)
{
    if (sPendRun != sRun + 1u) {
        portConLogf("ge: net: re-synchronising to the host: its next stage is run %u, this machine was at run %u\n", sPendRun, sRun);
        sRun = sPendRun - 1u;
    }
    sPendValid = 0;
    g_PortNetLaunchStage = sPendId;
    sNextGotRun = sPendRun;
    snprintf(sSetup.stage, sizeof(sSetup.stage), "%s", sPendStage);
    sSetup.diff = sPendDiff;
    sSetup.seed = sPendSeed;
    netSetSeedEnv(sPendSeed);
    netTakeMode(sPendMode);                            /* the host's game mode for this stage (GE_MODE here; the stage load reads it) */
    netModsSetModeToken(sSetup.mods, (int) sizeof(sSetup.mods), sCurMode);   /* the session log's M line says it */
    g_PortNetLaunchDiff = sPendDiff;   /* (0..3: the unpacker refused anything else) */
    portConLogf("ge: net: the host chose stage %s, difficulty %d, seed %u for stage run %u: launching it\n", sPendStage, sPendDiff, sPendSeed, sPendRun);
}

/* Guest: the host chose the next mission of the session (it is loading it now). Launch the same one from this machine's menus. */
static void netOnNextStage(const unsigned char *buf, int n)
{
    char st[PORT_NET_STR_STAGE];
    char mode[PORT_NET_STR_MODE];
    unsigned int run = 0, seed = 0;
    int diff = 0;

    mode[0] = '\0';
    if (sRole != 2 || portNetNextStageUnpackM(buf, n, &run, st, sizeof(st), &diff, &seed, mode, sizeof(mode)) != 0) {
        portConLogf("ge: net: ignored a malformed or unexpected NEXTSTAGE\n");
        return;
    }
    if (run <= sNextGotRun || (sPendValid && run == sPendRun)) {   /* a repeat of one already taken (the same stage launch): never launch twice */
        portConLogf("ge: net: ignored a repeated NEXTSTAGE for stage run %u\n", run);
        return;
    }
    {
        int id = -1;

        if (portNetParseStageId(st, portNetStageMaxId(), &id) != 0 || !portNetStageValid(id)) {
            portConLogf("ge: net: ignored NEXTSTAGE with invalid stage '%s'\n", st);
            return;
        }
        if (portStageRunning()) {
            /* mission-end job: the host is already past this stage (it ended there, a few ticks before this machine's sim does, or this machine
             * is stuck). Keep the message; netHold applies it as soon as the old stage is over, and ends the old stage itself after a moment. */
            sPendValid = 1;
            sPendMs = netioNowMs();
            sPendRun = run;
            snprintf(sPendMode, sizeof(sPendMode), "%s", mode);
            sPendSeed = seed;
            sPendId = id;
            sPendDiff = diff;
            snprintf(sPendStage, sizeof(sPendStage), "%s", st);
            portConLogf("ge: net: the host chose stage %s (run %u) while a stage is still running here: it will follow when this one ends\n", st, run);
            return;
        }
        sPendId = id;
    }
    sPendRun = run;
    sPendSeed = seed;
    sPendDiff = diff;
    snprintf(sPendMode, sizeof(sPendMode), "%s", mode);
    snprintf(sPendStage, sizeof(sPendStage), "%s", st);
    netApplyNext();
}

/* The partner is gone (or the stage must end): the way the pause menu's Abort leaves a mission, without touching the save. */
static void netReturnToTitle(void)
{
    set_missionstate(0);
    bossRunTitleStage();
    mission_failed_or_aborted = TRUE;
}

static void netDispatch(void)
{
    unsigned char buf[PORT_NET_BUF_MAX];
    int ch = 0, n;

    for (;;) {
        n = netioRecv(&ch, buf, sizeof(buf));
        if (n <= 0) {
            break;
        }
        if (ch == NETIO_CH_INPUT) {
            PortNetInputPkt pkt;

            if ((sLockInit || sTail) && portNetInputUnpack(buf, n, &pkt) == 0) {
                const unsigned int had = sLock.remoteCount;
                const int stored = portNetLockRecvRun(&sLock, sRun, &pkt);

                if (stored != -1 && pkt.count > 0 && pkt.run == sRun) {   /* job 6 stats: what the packet was worth */
                    if (pkt.firstTick > had + 1u) {
                        sStatOow += (unsigned int) pkt.count;   /* a gap: none of it can be used yet */
                    } else {
                        const unsigned int seen = had + 1u - pkt.firstTick;

                        sStatDups += seen > (unsigned int) pkt.count ? (unsigned int) pkt.count : seen;
                    }
                }
                if (stored != -1 && pkt.cmds > sCmdTheirs) {   /* (a late packet of an earlier stage launch is dropped) */
                    sCmdTheirs = pkt.cmds;
                }
            }
        } else {
            int type = portNetMsgType(buf, n);
            unsigned int run = 0;
            PortNetPlayerOpts oa, ob;

            memset(&oa, 0, sizeof(oa));
            memset(&ob, 0, sizeof(ob));
            if (type == PORT_NETMSG_READY && portNetRunUnpack(buf, n, &type, &run, &oa, &ob) == 0) {
                if (sRole == 2) {
                    /* only the host uses READY (the guest waits for GO) */
                } else if (run > sRun) {   /* a READY for a stage launch this machine has not started: a guest that went its own way. Never act on it. */
                    portConLogf("ge: net: ignored a READY for stage run %u (this machine is at run %u): the partner started a stage the host did not choose\n", run, sRun);
                } else if (run > sPartnerReadyRun) {
                    sPartnerReadyRun = run;
                    sPartnerReadyOpts = oa;   /* the guest's settings as they stand with its stage loaded */
                }
            } else if (type == PORT_NETMSG_GO && portNetRunUnpack(buf, n, &type, &run, &oa, &ob) == 0) {
                if (run > sGoRun) {
                    sGoRun = run;
                    sGoHostOpts = oa;
                    sGoGuestEcho = ob;
                }
            } else if (type == PORT_NETMSG_SIMCFG) {
                PortNetSimCfg sc;

                if (portNetSimCfgUnpack(buf, n, &run, &sc) == 0 && run >= sPartnerSimCfgRun) {
                    sPartnerSimCfgRun = run;
                    sPartnerSimCfg = sc;
                }
            } else if (type == PORT_NETMSG_NEXTSTAGE) {
                netOnNextStage(buf, n);
            } else if (type == PORT_NETMSG_CMD) {
                netOnCmd(buf, n);
            } else if (type == PORT_NETMSG_HASH) {
                PortNetHashMsg hm, mine;

                if (portNetHashUnpack(buf, n, &hm) == 0 && hm.run == sRun) {
                    netHashResult(portNetHashBookAddTheirs(&sBook, &hm, &mine), &mine, &hm);
                }
            }
        }
    }
}

static void netPoll(void)
{
    netioService();
    if (!sActive) {
        netHandshake();
        return;
    }
    netDispatch();
    if (!sLost) {
        const int closed = (netioState() == NETIO_LOST || netioState() == NETIO_DOWN);
        const unsigned int silent = netioSilentMs();
        const int inStage = sLockOn && sLockInit && portStageRunning();
        const char *why = NULL;

        if (closed) {
            why = "the partner closed the game or the link was lost";
        } else if (inStage && silent > NET_SILENT_MS) {
            why = "no packets from the partner for 5 seconds";
        } else if (silent > NET_SILENT_IDLE_MS) {
            why = "no packets from the partner for 30 seconds";
        }
        if (why != NULL) {
            sLost = 1;
            sLostMs = netioNowMs();
            sLostDone = 0;
            snprintf(sLostWhy, sizeof(sLostWhy), "%s", why);
            portConLogf("ge: net: partner disconnected\n");
            portConLogf("ge: net: (%s; stage run %u, sim tick %u%s)\n", why, sRun, portCoopNetTick(), inStage ? ", in a stage: back to the title" : ", in the menus");
            netWaiting(inStage ? "The other player has left - returning to the title..." : "The other player has left the session.");
        }
    }
}

/* Mission-end job: the session cannot go on (the partner is linked but out of step for too long). End it the way a lost partner does (message,
 * back to the title), with this text. Never leave a machine waiting for ever. */
static void netEndSession(const char *why, const char *msg)
{
    if (sLost) {
        return;
    }
    sLost = 1;
    sLostMs = netioNowMs();
    sLostDone = 0;
    snprintf(sLostWhy, sizeof(sLostWhy), "%s", why);
    portConLogf("ge: net: session ended: %s (stage run %u, sim tick %u)\n", why, sRun, portCoopNetTick());
    netWaiting(msg);
}

static void netSendRun(int type, unsigned int run, const PortNetPlayerOpts *a, const PortNetPlayerOpts *b)
{
    unsigned char buf[PORT_NET_BUF_MAX];
    int n = portNetRunPack(type, run, a, b, buf, sizeof(buf));

    if (n > 0) {
        netSendSession(buf, n);
    }
}

/* job 3c: say what the sim-reading ini values are right now (sent just before READY / GO; the receiver compares at GO) */
static void netSendSimCfg(unsigned int run)
{
    unsigned char buf[PORT_NET_BUF_MAX];
    PortNetSimCfg c;
    int n;

    portSimCfgCollect(&c);
    n = portNetSimCfgPack(run, &c, buf, sizeof(buf));
    if (n > 0) {
        netSendSession(buf, n);
    }
}

/* This stage launch is loaded: take the settings block again (job 3b: the pads, the intro's controller claim and any late
 * option change are settled by now; the handshake's block was a snapshot 1.5 s after boot), build the lock (neutral or
 * scripted prefill), say READY with the fresh block. The sim keeps reading the old blocks until GO swaps both in. */
static void netStageReady(void)
{
    unsigned int t;
    PortNetInput in;
    PortNetPlayerOpts before = sMyOpts;

    netLocalOpts(&sMyOpts, 1);
    if (!portNetOptsEqual(&before, &sMyOpts)) {
        char a[200], b[200];

        netOptsText(&before, a, sizeof(a));
        netOptsText(&sMyOpts, b, sizeof(b));
        portConLogf("ge: net: my settings changed since the handshake: was [%s] now [%s]\n", a, b);
    }
    portNetLockInit(&sLock, sSetup.delay);
    for (t = 1; t <= (unsigned int) sLock.delay; t++) {
        portCoopNetPrefill(t, &in);
        portNetLockPushLocal(&sLock, &in);
    }
    sLockInit = 1;
    sLockOn = 0;
    sReadySent = 1;
    sReadyAtMs = sReadyResentMs = netioNowMs();
    sGateStallMs0 = 0;
    sStallRetraces = sStallTotal = sTicksRun = 0;
    netSendSimCfg(sRun);
    netSendRun(PORT_NETMSG_READY, sRun, &sMyOpts, NULL);
    portConLogf("ge: net: stage %u loaded, READY sent (delay %d)\n", sRun, sLock.delay);
}

static void netLogOpen(const char *who);

/* Both sides are ready: swap the refreshed settings blocks in (they apply from tick 1), start the lock. */
static void netGo(void)
{
    char a[200], b[200];
    PortNetSimCfg mineCfg;

    portSimCfgCollect(&mineCfg);   /* job 3c: the sim-reading ini values must still be the partner's (the watch / console can change them) */
    if (sPartnerSimCfgRun != sRun) {
        netFail("the partner sent no sim-settings list for this stage launch (another build?)");
    } else {
        char diff[400];

        if (portSimCfgDiff(&mineCfg, &sPartnerSimCfg, diff, sizeof(diff)) != 0) {
            char why[500];

            snprintf(why, sizeof(why), "the sim settings differ between the two machines at stage start: %s", diff);
            netFail(why);
        }
    }
    sAgreedCfg = (sRole == 1) ? mineCfg : sPartnerSimCfg;
    if (sRole == 1) {
        sOpts[0] = sMyOpts;
        sOpts[1] = sPartnerReadyOpts;
    } else {
        sOpts[0] = sGoHostOpts;
        sOpts[1] = sMyOpts;
    }
    sOptsKnown = 1;
    netOptsText(&sOpts[0], a, sizeof(a));
    netOptsText(&sOpts[1], b, sizeof(b));
    portConLogf("ge: net: player settings at READY (run %u)%s: host (player 0) %s | guest (player 1) %s\n", sRun,
                sOptsOff ? " (NOT applied: GE_NET_OPTS_OFF)" : "", a, b);
    portJoyPortNetReset();
    sLockOn = 1;
    netWaiting("");
    portConLogf("ge: net: GO (run %u)\n", sRun);
    portNetMusicRelease();   /* job 3e: the level's music starts now, not while the stage was loading */
    netLogOpen(sRole == 1 ? "host" : "guest");
    netSendInput();   /* the prefill goes out at once */
}

static int netHold(void)
{
    netPoll();
    if (!sActive) {
        return 1;   /* no tick at all before the handshake: both sides then count the same menu ticks */
    }
    if (sStopTick != 0 && sLockInit && portCoopActive() && portCoopNetTick() >= sStopTick) {
        /* test knob GE_NET_STOP=<tick>: stop after that many sim ticks, but keep answering for a moment so the
         * partner can still collect our last records and acks (also when it has already left) */
        if (sStopAt == 0) {
            sStopAt = netioNowMs();
            portConLogf("ge: net: reached tick %u (GE_NET_STOP), leaving in 1.5 s\n", portCoopNetTick());
        }
        if ((int) (netioNowMs() - sStopAt) > 1500) {
            portConLogf("ge: net: state hash: %u sent, %u compared, %u mismatched\n", sHashSent, sHashCompared, sHashBad);
            portConLogf("ge: net: stopped at tick %u\n", portCoopNetTick());
            exit(0);
        }
        if ((int) (netioNowMs() - sLastSendMs) >= NET_RESEND_MS) {
            netSendInput();
        }
        return 1;
    }
    if (sRogue) {
        if (portStageRunning() && (int) (netioNowMs() - sRogueMs) < NET_LOST_SHOW_MS) {
            return 1;   /* held: the message is on black */
        }
        sRogue = 0;
        netWaiting("");
        if (portStageRunning()) {
            netReturnToTitle();
        }
        sStageOver = 1;   /* no gate, no lock until the next stage the host chooses */
        portConLogf("ge: net: back to the title (the host's session goes on without this stage)\n");
    }
    if (sLost && !sLostDone && !sLostFinish && portStageRunning() && portCoopExitWaiting()) {
        /* CO-OP-EXITS: the partner left while a Bond waits at the level exit: the mission completes (the ending plays for the one who is left),
         * instead of the way back to the title. The usual teardown follows when that stage is over. */
        sLostFinish = 1;
        netWaiting("");
        portCoopPartnerGone();
        portConLogf("ge: net: the partner left while a Bond waits at the exit: the mission completes\n");
    }
    if (sLost && sLostFinish && !sLostDone && !portStageRunning()) {   /* that stage is over: now the session teardown */
        sLostFinish = 0;
        sLostMs = netioNowMs() - NET_LOST_SHOW_MS;
    }
    if (sLost && sLostFinish) {
        return 0;   /* the ending runs on this machine alone */
    }
    if (sLost && !sLostDone) {   /* job 4: say so for a moment, then back to the title (a stage) or just on (the menus) */
        if ((int) (netioNowMs() - sLostMs) < NET_LOST_SHOW_MS) {
            return 1;   /* held: portNetWaitText shows the message on black */
        }
        sLostDone = 1;
        netWaiting("");
        if (portStageRunning()) {
            netReturnToTitle();
            portConLogf("ge: net: back to the title (the session is over; restart the game to play together again)\n");
        }
        /* mission-end job: the session is over, so is co-op. portNetPreinit put GE_COOP=1 in the environment for the whole process and
         * portCoopStageBegin reads it at every stage start: without this a game started from the title was still two Bonds, one of them
         * nobody's. The next stage is plain solo (the fixed 60 Hz tick and the skipped intro stay for the process: restart for a normal boot). */
        netSetenv("GE_COOP", "0");
        netSetenv("GE_COOP_VIEW", "0");
        portConLogf("ge: net: session over: co-op is off, the next game started from the title is solo (restart the game to play together again)\n");
    }
    if (sPendValid && !sLost) {   /* guest: the host's next stage arrived while the old one still ran here */
        if (!portStageRunning()) {
            netApplyNext();
        } else if ((int) (netioNowMs() - sPendMs) > NET_PEND_ABORT_MS) {
            portConLogf("ge: net: the host has left stage run %u and this machine's copy has not ended after %d ms: leaving it to follow the host\n", sRun, NET_PEND_ABORT_MS);
            netReturnToTitle();
            sPendMs = netioNowMs();
        }
    }
    if (sLockOn && !portStageRunning()) {   /* job 3c: the stage ended (menus again): lockstep is over until the next portNetStageBegin */
        sLockOn = 0;
        sLockInit = 0;
        sTail = 1;   /* the lock's records stay until the partner has all of ours (below) */
        sTailMs = netioNowMs();
        sGateStallMs0 = 0;
        sStageOver = 1;
        portConLogf("ge: net: stage %u ended, lockstep off until the next stage starts\n", sRun);
        if (sRole == 2 && getenv("GE_NET_TEST_GUESTPICK") != NULL) {   /* test (guest): try to start a stage from the menus: must be refused ("the host chooses") */
            extern void portNetTestMenuLaunch(s32 id);
            int id = -1;

            if (portNetParseStageId(getenv("GE_NET_TEST_GUESTPICK"), portNetStageMaxId(), &id) == 0 && portNetStageValid(id)) {
                portConLogf("ge: net: TEST: the guest tries to start stage %d from the menus\n", id);
                portNetTestMenuLaunch(id);
            }
        }
        if (sTestNextStage[0] != '\0' && !sTestNextArmed) {   /* test: pick the next mission without menus (GE_NET_TEST_NEXT on the host; GE_NET_TEST_ROGUE on the guest = a launch the host did not choose) */
            char v[16];

            sTestNextArmed = 1;
            netSetenv("GE_STAGE", sTestNextStage);
            snprintf(v, sizeof(v), "%d", sTestNextDiff);
            netSetenv("GE_DIFF", v);
            g_PortDirectLaunchRearm = 1;
            portConLogf("ge: net: TEST: the host will launch stage '%s' (difficulty %d) next\n", sTestNextStage, sTestNextDiff);
        }
    }
    if (sStageOver) {
        netWaiting("");
        if (sTail) {   /* the partner may still be a few ticks behind and wait for our last records: resend until it has them all */
            if ((int) (netioNowMs() - sTailMs) > NET_TAIL_MS || !portNetLockUnacked(&sLock)) {
                sTail = 0;
            } else if ((int) (netioNowMs() - sLastSendMs) >= NET_RESEND_MS * 8) {
                netSendInput();
            }
        }
        return 0;
    }
    if (sLost || !portCoopActive()) {
        netWaiting("");
        return 0;
    }
    if (sRun == 0) {
        return 0;   /* a co-op stage without portNetStageBegin cannot happen; do not hang if it does */
    }
    if (!sReadySent) {
        netStageReady();
    }
    if (!sLockOn) {
        if (sRole == 1 && sPartnerReadyRun >= sRun) {
            netSendSimCfg(sRun);
            netSendRun(PORT_NETMSG_GO, sRun, &sMyOpts, &sPartnerReadyOpts);
            netGo();
        } else if (sRole == 2 && sGoRun >= sRun) {
            if (!portNetOptsEqual(&sGoGuestEcho, &sMyOpts)) {
                netFail("settings echo mismatch at GO: the host holds other settings for the guest than READY sent");
            }
            netGo();
        } else {
            netWaiting("Waiting for partner to load the stage...");
            if (sRole == 2 && (int) (netioNowMs() - sReadyResentMs) > 1000) {   /* the host drops a READY for a run it has not started yet: say it again until GO comes */
                sReadyResentMs = netioNowMs();
                netSendRun(PORT_NETMSG_READY, sRun, &sMyOpts, NULL);
            }
            if ((int) (netioNowMs() - sReadyAtMs) > NET_STAGE_WAIT_MS) {   /* mission-end job: the partner never followed; do not wait for ever */
                netEndSession("the partner did not load the stage in time", sRole == 1 ? "The other player could not join this mission - returning to the title..."
                                                                                    : "The host did not start this mission - returning to the title...");
            }
            return 1;
        }
    }
    if (portNetLockCanRun(&sLock, portCoopNetTick() + 1) && sCmdGot >= sCmdTheirs) {   /* job 5: every session command the partner announced is here */
        sStallRetraces = 0;
        if (sWaitT0 != 0) {   /* job 6: the tick that was held starts now; how long it waited */
            sWaitMsCur = (float) (portProfNow() - sWaitT0) / 1000.0f;
            sWaitT0 = 0;
            sStatWaitedPending = 1;
            sStatLate++;
        }
        netWaiting("");
        return 0;
    }
    if (sWaitT0 == 0) {
        sWaitT0 = portProfNow();
        sGateStallMs0 = netioNowMs();
    }
    if ((int) (netioNowMs() - sGateStallMs0) > NET_GATE_STALL_MS) {   /* linked, but no input for this long: the partner is not in this stage any more */
        netEndSession("no input from the partner for 30 seconds (it is not in this stage)", "The other player is no longer in this mission - returning to the title...");
        return 1;
    }
    sStallRetraces++;
    sStallTotal++;
    if (sStallRetraces > 1) {
        netWaiting("Waiting for partner...");
    }
    if (sStallRetraces == 30) {
        portConLogf("ge: net: no input from the partner for 30 retraces\n");
    }
    if ((int) (netioNowMs() - sLastSendMs) >= NET_RESEND_MS) {
        netSendInput();   /* the last packet may be lost: resend what is unacked, and our ack */
        sStatResends++;
        sStatResendPending = 1;
    }
    return 1;
}

/* gfx_pc.cpp: handle the window's events on this thread (video builds, sync render mode); a no-op otherwise */
__attribute__((weak)) void gfx_hold_poll_events(void)
{
}

/* Heartbeat for the Windows stall watchdog (main.c): it exits when no gfx task is submitted for ~16 s, which a long
 * wait for the partner would otherwise trip. Counts held retraces. */
unsigned int g_PortNetHoldBeat;

int portNetHold(void)
{
    int hold;

    if (sRole == 0) {
        return 0;
    }
    hold = netHold();
    portNetMusicRelease();   /* job 3e: a held level track starts as soon as nothing holds it any more (GO, or the session ended) */
    if (hold) {
        g_PortNetHoldBeat++;
        gfx_hold_poll_events();   /* no frame is rendered for a held tick, so pump the window's events here */
    }
    return hold;
}

/* The line boss.c draws on black while a tick is held, or NULL to keep the last frame (a short stall in play). */
const char *portNetWaitText(void)
{
    if (sRole == 0 || sWait[0] == '\0') {
        return NULL;
    }
    if (sLost && !sLostDone) {
        return sWait;   /* the departure message is always shown (job 4) */
    }
    if (sActive && sLockOn && sStallRetraces < 30) {
        return NULL;
    }
    return sWait;
}

int portNetLockActive(void)
{
    return sLockOn && sLockInit && !sLost && portCoopActive() && (sReplay || portStageRunning());   /* job 3c: not in the menus after a stage */
}

int portNetTickInputs(unsigned int tick, PortNetInput *local, PortNetInput *remote)
{
    const PortNetInput *l;
    const PortNetInput *r;

    if (sReplay) {   /* offline replay: both records come from the log (player index -> local / remote by the replay's view) */
        if (tick < 1 || tick > sRepCount) {
            return 0;
        }
        *local = sRepTick[tick - 1].rec[sReplayView];
        *remote = sRepTick[tick - 1].rec[1 - sReplayView];
        return 1;
    }
    l = portNetLockLocalAt(&sLock, tick);
    r = portNetLockRemoteAt(&sLock, tick);
    if (l == NULL || r == NULL) {
        portConLogf("ge: net: BREACH tick %u ran without its records (local %d remote %d)\n", tick, l != NULL, r != NULL);
        return 0;
    }
    portNetLockConsume(&sLock, tick);
    *local = *l;
    *remote = *r;
    if (g_PortResim) {   /* rollback: a re-simulated tick reads its records again; it is not a new tick */
        return 1;
    }
    sTicksRun++;
    if (sTicksRun % 120u == 0) {
        portConLogf("ge: net: tick %u, held %u retraces so far, rtt %d ms\n", tick, sStallTotal, netioRttMs());
    }
    return 1;
}

void portNetPushLocal(const PortNetInput *in)
{
    if (portNetLockPushLocal(&sLock, in) == 0) {
        portConLogf("ge: net: local record ring full\n");
        return;
    }
    netSendInput();
}

int portNetDelay(void)
{
    return sLockInit ? sLock.delay : portNetLockClampDelay(sSetup.delay);
}

static void netRngTraceDump(const char *why);

static void netDesync(const PortNetHashMsg *mine, const PortNetHashMsg *theirs)
{
    char names[200];
    int i, n = 0;
    FILE *f;
    char name[64];
    unsigned int k;

    names[0] = '\0';
    for (i = 0; i < mine->nfields && i < theirs->nfields; i++) {
        if (mine->fields[i] != theirs->fields[i]) {
            n += snprintf(names + n, sizeof(names) - (size_t) n, "%s%s", n > 0 ? "," : "", portCoopStateFieldName(i));
        }
    }
    portConLogf("ge: net: DESYNC at tick %u (run %u): local hash %08x, partner %08x, differing fields: %s\n", mine->tick,
                mine->run, mine->hash, theirs->hash, names[0] != '\0' ? names : "(none: field counts differ)");
    netRngTraceDump("desync found");   /* GE_NET_RNGTRACE ring: the draws up to now, the first of them is the one to diff */
    if (sDesyncFiles >= 4) {
        return;   /* the first few ticks tell the story; later ones differ too */
    }
    sDesyncFiles++;
    (void) portLogPath("");   /* creates Data/Logs (under the profile) */
    snprintf(name, sizeof(name), "Data/Logs/desync_%u.txt", mine->tick);
    f = fopen(portRootPath(name), "w");
    if (f == NULL) {
        portConLogf("ge: net: cannot write %s\n", name);
        return;
    }
    fprintf(f, "DESYNC at tick %u (stage launch %u of the session)\nrole: %s (player %d)\nlocal hash   %08x\npartner hash %08x\n\n",
            mine->tick, mine->run, sRole == 1 ? "host" : "guest", sRole == 1 ? 0 : 1, mine->hash, theirs->hash);
    fprintf(f, "%-12s %-10s %-10s\n", "field", "local", "partner");
    for (i = 0; i < mine->nfields; i++) {
        const unsigned int o = (i < theirs->nfields) ? theirs->fields[i] : 0u;

        fprintf(f, "%-12s %08x   %08x   %s\n", portCoopStateFieldName(i), mine->fields[i], o, mine->fields[i] != o ? "<-- DIFFERS" : "");
    }
    {
        char a[160];

        netOptsText(&sOpts[0], a, sizeof(a));
        fprintf(f, "\nplayer 0 settings: %s\n", a);
        netOptsText(&sOpts[1], a, sizeof(a));
        fprintf(f, "player 1 settings: %s\n", a);
    }
    fprintf(f, "\nThis side's world at the start of tick %u (the partner's file has its own; diff the two):\n", mine->tick);
    for (k = 0; k < 8; k++) {
        if (sDumps[k].run == mine->run && sDumps[k].tick == mine->tick && sDumps[k].text[0] != '\0') {
            fputs(sDumps[k].text, f);
            break;
        }
    }
    fclose(f);
    portConLogf("ge: net: wrote %s\n", portRootPath(name));
}

static void netHashResult(int verdict, const PortNetHashMsg *mine, const PortNetHashMsg *theirs)
{
    if (verdict == PORT_NETHASH_WAIT) {
        return;
    }
    sHashCompared++;
    sLastHashTick = mine->tick;
    if (sLog != NULL) {   /* the partner's hash for the same tick, and whether it matched ours */
        fprintf(sLog, "X %u %08x %s\n", mine->tick, theirs->hash, verdict == PORT_NETHASH_DIFFER ? "DIFFERENT" : "same");
        fflush(sLog);
    }
    if (verdict == PORT_NETHASH_DIFFER) {
        int fi;

        sHashBad++;
        sLastVerdict = 1;
        sStatHashFlag = NSF_HASH_BAD;
        sDiffMask = 0;
        sDiffTick = mine->tick;
        for (fi = 0; fi < mine->nfields && fi < theirs->nfields && fi < 32; fi++) {
            if (mine->fields[fi] != theirs->fields[fi]) {
                sDiffMask |= 1u << fi;
            }
        }
        netDesync(mine, theirs);
    } else {
        sLastVerdict = 0;
        sStatHashFlag |= NSF_HASH_OK;
    }
    if (verdict != PORT_NETHASH_DIFFER && sHashCompared % 10u == 1u) {
        portConLogf("ge: net: state hash ok at tick %u (%08x), %u compared\n", mine->tick, mine->hash, sHashCompared);
    }
}

/* Every 30th sim tick, before the tick runs: hash the world, tell the partner, compare when both are here. */
void portNetStateCheck(unsigned int tick)
{
    PortNetHashMsg m, other;
    NetDump *d;
    unsigned char buf[PORT_NET_BUF_MAX];
    int n;

    if (sReplay) {
        return;   /* nothing to exchange offline; the per-tick fingerprint (which includes this hash) is checked in portNetSessionTick */
    }
    if (!sActive || sRun == 0) {
        return;
    }
    d = &sDumps[sDumpNext++ & 7u];
    d->run = sRun;
    d->tick = tick;
    d->text[0] = '\0';
    memset(&m, 0, sizeof(m));
    m.tick = tick;
    m.run = sRun;
    portCoopStateHash(&m, d->text, NET_DUMP_CAP);
    if (sLog != NULL) {   /* this side's state hash, as it goes to the partner */
        int k;

        fprintf(sLog, "S %u %08x", tick, m.hash);
        for (k = 0; k < m.nfields; k++) {
            fprintf(sLog, " %08x", m.fields[k]);
        }
        fputc('\n', sLog);
        fflush(sLog);
    }
    n = portNetHashPack(&m, buf, sizeof(buf));
    if (n > 0 && !sLost) {
        netSendSession(buf, n);
        sHashSent++;
    }
    netHashResult(portNetHashBookAddMine(&sBook, &m, &other), &m, &other);
}

/* ---- GE_NET_RNGTRACE=1 (job 3c): every sim RNG draw, with the tick and the caller -----------------------------------------
 * One line per draw into Data/Logs/rngtrace_run<N>_<host|guest|replay>.txt (under the profile):
 *   <tick> <S|C> <n> p<player> <caller offset> <value>
 * S = the sim RNG, C = the chr/obj RNG, n = draw number since the stage reseed, p = the player whose pass is current, the caller
 * offset = return address minus randomGetNext (decimal; the header gives the addresses of this process, tests/net_rngsym.sh
 * turns an offset into a function and line with nm and addr2line). Two sides' traces are compared by tests/net_rngdiff.sh. */
extern s32 g_portRngTrace;
extern u32 g_portSimRandDraws;
extern u32 g_portChrObjRandDraws;
extern u32 randomGetNext(void);
extern u32 chrObjRandomGetNext(void);
static FILE *sRngTrace;   /* the stream file of GE_NET_RNGTRACE=full */

/* netplay job 7: GE_NET_RNGTRACE=1 no longer writes a line per draw for the whole session (about 0.5 MB per 1000 ticks, so over 100 MB an hour, and a stdio call per draw). It keeps the
 * draws of the last GE_NET_RNGTRACE_TICKS sim ticks (default 2000, about half a minute) in a ring in memory (20 bytes a draw, at most RNG_RING_CAP draws: 5 MB) and writes them
 * in the same format when something worth diagnosing happens: the first DESYNC (the ticks before the divergence are in the file: the hash check comes up to 30 ticks after it),
 * the start of the next stage, and a clean exit. GE_NET_RNGTRACE=full is the old unbounded stream. A short run (the tests: a few hundred ticks) dumps everything it drew. */
#define RNG_RING_CAP 262144u
typedef struct {
    unsigned int tick, n, val;
    int off;
    unsigned char kind, pl;
} NetRngRec;
static NetRngRec *sRngRing;
static unsigned int sRngCount;     /* draws recorded since the ring was reset */
static unsigned int sRngWindow = 2000u;
static int sRngMode;               /* 0 off, 1 ring, 2 full stream */
static int sRngDumped;             /* this stage's ring has been written (the first dump is the one that matters: later ones would have lost the early ticks) */
static unsigned int sRngRun;
static char sRngWho[16];

static void netRngTraceDump(const char *why)
{
    char name[96];
    FILE *f;
    unsigned int total, first, i, now = portCoopNetTick();

    if (sRngMode != 1 || sRngRing == NULL || sRngCount == 0 || sRngDumped) {
        return;
    }
    sRngDumped = 1;
    (void) portLogPath("");
    snprintf(name, sizeof(name), "Data/Logs/rngtrace_run%u_%s.txt", sRngRun, sRngWho);
    f = fopen(portRootPath(name), "w");
    if (f == NULL) {
        portConLogf("ge: net: cannot write %s\n", name);
        return;
    }
    fprintf(f, "# sim RNG trace: tick kind n player caller-offset value; offset = return address - randomGetNext\n");
    fprintf(f, "# ref randomGetNext %lx chrObjRandomGetNext %lx\n", (unsigned long) (size_t) &randomGetNext, (unsigned long) (size_t) &chrObjRandomGetNext);
    fprintf(f, "# ring dump (%s) at tick %u: the draws of the last %u ticks (at most %u draws)\n", why, now, sRngWindow, RNG_RING_CAP);
    total = sRngCount < RNG_RING_CAP ? sRngCount : RNG_RING_CAP;
    first = sRngCount - total;
    for (i = first; i < sRngCount; i++) {
        const NetRngRec *r = &sRngRing[i % RNG_RING_CAP];

        if (r->tick + sRngWindow < now) {
            continue;
        }
        fprintf(f, "%u %c %u p%d %ld %08x\n", r->tick, r->kind, r->n, (int) r->pl, (long) r->off, r->val);
    }
    fclose(f);
    portConLogf("ge: net: RNG trace (%s) -> %s\n", why, portRootPath(name));
}

static void netRngTraceAtExit(void)
{
    netRngTraceDump("exit");
}

static void netRngTraceOpen(const char *who)
{
    const char *e = getenv("GE_NET_RNGTRACE");
    char name[96];
    static int atexitSet;

    netRngTraceDump("next stage");
    if (sRngTrace != NULL) {
        fclose(sRngTrace);
        sRngTrace = NULL;
    }
    g_portRngTrace = 0;
    sRngMode = 0;
    if (e == NULL || e[0] == '\0' || e[0] == '0') {
        return;
    }
    sRngMode = (strcmp(e, "full") == 0 || e[0] == '2') ? 2 : 1;
    sRngRun = sRun;
    snprintf(sRngWho, sizeof(sRngWho), "%s", who);
    (void) portLogPath("");
    snprintf(name, sizeof(name), "Data/Logs/rngtrace_run%u_%s.txt", sRun, who);
    if (sRngMode == 1) {
        const char *w = getenv("GE_NET_RNGTRACE_TICKS");

        sRngWindow = (w != NULL && atoi(w) > 0) ? (unsigned int) atoi(w) : 2000u;
        if (sRngRing == NULL) {
            sRngRing = (NetRngRec *) calloc(RNG_RING_CAP, sizeof(NetRngRec));
        }
        if (sRngRing == NULL) {
            sRngMode = 0;
            return;
        }
        sRngCount = 0;
        sRngDumped = 0;
        if (!atexitSet) {
            atexitSet = 1;
            atexit(netRngTraceAtExit);
        }
        g_portRngTrace = 1;
        portConLogf("ge: net: RNG trace: ring of the last %u ticks, written when a desync is found, at the next stage and at exit -> %s\n", sRngWindow, portRootPath(name));
        return;
    }
    sRngTrace = fopen(portRootPath(name), "w");
    if (sRngTrace == NULL) {
        portConLogf("ge: net: cannot write %s\n", name);
        sRngMode = 0;
        return;
    }
    fprintf(sRngTrace, "# sim RNG trace: tick kind n player caller-offset value; offset = return address - randomGetNext\n");
    fprintf(sRngTrace, "# ref randomGetNext %lx chrObjRandomGetNext %lx\n", (unsigned long) (size_t) &randomGetNext, (unsigned long) (size_t) &chrObjRandomGetNext);
    g_portRngTrace = 1;
    portConLogf("ge: net: RNG trace (every draw) -> %s\n", portRootPath(name));
}

/* GE_HASHLOG runs (port_coop.c, solo or local co-op, no netplay): GE_NET_RNGTRACE works there too, as "local" */
void portNetRngTraceOpenLocal(void)
{
    netRngTraceOpen("local");
}

void portNetRngTraceDraw(int kind, void *ret, unsigned int value)
{
    if (sRngMode == 1) {
        NetRngRec *r = &sRngRing[sRngCount % RNG_RING_CAP];

        r->tick = portCoopNetTick();
        r->n = kind == 'S' ? g_portSimRandDraws : g_portChrObjRandDraws;
        r->off = (int) ((size_t) ret - (size_t) &randomGetNext);
        r->val = value;
        r->kind = (unsigned char) kind;
        r->pl = (unsigned char) get_cur_playernum();
        sRngCount++;
        return;
    }
    if (sRngTrace == NULL) {
        return;
    }
    fprintf(sRngTrace, "%u %c %u p%d %ld %08x\n", portCoopNetTick(), kind, kind == 'S' ? g_portSimRandDraws : g_portChrObjRandDraws,
            (int) get_cur_playernum(), (long) ((size_t) ret - (size_t) &randomGetNext), value);
}

static void netRngTraceFlush(void)
{
    if (sRngTrace != NULL) {
        fflush(sRngTrace);   /* the port leaves through _exit, which skips stdio */
    }
}

void portNetStageBegin(int stage)
{
    netCmdReset();
    if (sReplay) {   /* offline replay: no handshake, no READY/GO; the lock is "on" from the stage launch */
        sRun++;
        sLockInit = 1;
        sLockOn = 1;
        sOptsKnown = 1;
        portJoyPortNetReset();
        portCoopReseed(sSetup.seed);
        netRngTraceOpen("replay");
        netLogOpen("replay");   /* GE_NET_LOG with a replay writes the replay's own log, which can be diffed against the original */
        return;
    }
    if (!sActive || sLost) {
        return;
    }
    portNetHashBookInit(&sBook);
    sRun++;
    if (sRole == 1) {
        netHostChooseMode();   /* this stage's game mode: the host's (GE_MODE or its Mods page); the guest takes it from SETUP / NEXTSTAGE */
    }
    sReadySent = 0;
    sLockOn = 0;
    sLockInit = 0;
    sTail = 0;
    sStageOver = 0;
    if (sRun > 1u && sRole == 1) {   /* job 4: a later stage of the session. The host chooses; the guest is told and loads the same one */
        unsigned char buf[PORT_NET_BUF_MAX];
        int n;

        snprintf(sSetup.stage, sizeof(sSetup.stage), "%d", stage);
        sSetup.diff = (int) portSelectedDifficulty();
        sSetup.seed = sSetup.seed * 1664525u + 1013904223u + sRun;   /* a new seed per stage, the same on both sides (it rides in the message) */
        netSetSeedEnv(sSetup.seed);
        n = portNetNextStagePackM(sRun, sSetup.stage, sSetup.diff, sSetup.seed, sCurMode, buf, sizeof(buf));
        if (n > 0) {
            netSendSession(buf, n);
            netioService();   /* out before this machine's stage load blocks the loop for seconds */
        }
        portConLogf("ge: net: next stage %u: stage %s, difficulty %d, seed %u sent to the guest\n", sRun, sSetup.stage, sSetup.diff, sSetup.seed);
    } else if (sRun > 1u && sRole == 2 && sNextGotRun != sRun) {
        /* a guest that got into a stage the host never chose (a modified or older build, or a bug): it must not run alone. It sends nothing
         * (no READY), shows a message and goes back to the title; the host never hears of this stage and is not touched. */
        sRogue = 1;
        sRogueMs = netioNowMs();
        sReadySent = 1;
        portConLogf("ge: net: this machine started a stage the host did not choose (only the host picks the mission): not joining it, back to the title\n");
        netWaiting("The host did not choose this mission - returning to the title...");
        return;
    }
    portCoopReseed(sSetup.seed);   /* the same RNG state on both machines, whatever the menus drew */
    netRngTraceOpen(sRole == 1 ? "host" : "guest");
}

/* ---- session log and offline replay (job 3b) -------------------------------------------------------------------------
 * The log is a text file, one line per fact:  V version/role/run/delay | B build | G stage diff seed lua | M modifier text |
 * O n <hex> <text> settings block of player n | E NAME=VALUE launch environment that changes the sim | T tick rec0 rec1 hash f0..f7
 * (both players' records as applied at that tick, hex of the 20-byte wire record, then this side's state fingerprint at the
 * start of the tick) | S tick hash f0..f7 (the 30-tick state hash as sent to the partner) | X tick partner-hash same/DIFFERENT.
 * GE_NET_REPLAY=<file> loads one and runs it as local co-op: no socket, the records from the log, the same settings blocks,
 * the same lockstep gates, view 0 or 1. Every tick's fingerprint is compared with the log's. */

static const char *const sLogEnvNames[] = { "GE_STAGE_AFTER", "GE_MODS", "GE_COOP_RESPAWN", "GE_COOP_KILL", "GE_COOP_HURT", "GE_COOP_GOD",
                                            "GE_COOP_GIVEKEY", "GE_COOP_SWITCH", "GE_SPAWNXYZ", "GE_CHEATS",
                                            "GE_COOP_GIVEWEAPON", "GE_COOP_MOVETO", "GE_COOP_MOVETOCHR", "GE_COOP_UNSAFEDROP", "GE_COOP_CYCLE",
                                            "GE_COOP_MOVETOPAD", "GE_TEST_ALLOBJ", "GE_COOP_FORCEUNREACH", "GE_COOP_FORCEREACH", "GE_TEST_BOMBOLD", "GE_TEST_ESCORTOLD",
                                            "GE_LUA_TEST", "GE_HOSTAGE_TEST_KILL", NULL };

static void optsToHex(const PortNetPlayerOpts *o, char *out)
{
    unsigned char p[PORT_NET_OPTS_SIZE];
    int i;

    portNetOptsPack(o, p);
    for (i = 0; i < PORT_NET_OPTS_SIZE; i++) {
        snprintf(out + i * 2, 3, "%02x", p[i]);
    }
}

static int optsFromHex(const char *s, PortNetPlayerOpts *o)
{
    unsigned char p[PORT_NET_OPTS_SIZE];
    int i;

    for (i = 0; i < PORT_NET_OPTS_SIZE; i++) {
        unsigned v;

        if (sscanf(s + i * 2, "%2x", &v) != 1) {
            return -1;
        }
        p[i] = (unsigned char) v;
    }
    portNetOptsUnpack(p, o);
    return 0;
}

int portNetReplaying(void)
{
    return sReplay;
}

int portNetLogging(void)
{
    return sLog != NULL;
}

static void netLogOpen(const char *who)
{
    char name[96], hx[PORT_NET_OPTS_SIZE * 2 + 1], text[200];
    int i;

    if (!sLogWanted) {
        return;
    }
    if (sLog != NULL) {
        fclose(sLog);
        sLog = NULL;
    }
    (void) portLogPath("");   /* creates Data/Logs (under the profile) */
    snprintf(name, sizeof(name), "Data/Logs/netlog_run%u_%s.txt", sRun, who);
    sLog = fopen(portRootPath(name), "w");
    if (sLog == NULL) {
        portConLogf("ge: net: cannot write %s\n", name);
        return;
    }
    fprintf(sLog, "# Project Midas netplay session log\n");
    fprintf(sLog, "# replay offline: GE_NET_REPLAY=<this file> [GE_NET_REPLAY_VIEW=0|1], same profile and ini as this session\n");
    fprintf(sLog, "V %d role %s run %u delay %d\n", PORT_NET_PROTO, who, sRun, (int) sSetup.delay);
    fprintf(sLog, "B %s\n", sSetup.build);
    fprintf(sLog, "G stage %s diff %d seed %u lua %08x\n", sSetup.stage, sSetup.diff, sSetup.seed, sSetup.luaHash);
    fprintf(sLog, "M %s\n", sSetup.mods);
    for (i = 0; i < 2; i++) {
        optsToHex(&sOpts[i], hx);
        netOptsText(&sOpts[i], text, sizeof(text));
        fprintf(sLog, "O %d %s %s\n", i, hx, text);
    }
    {   /* job 3c: the sim-config list the session ran with (the host's), so a replay on another ini names the differing keys */
        fprintf(sLog, "K");
        for (i = 0; i < sAgreedCfg.n; i++) {
            fprintf(sLog, " %s=%d", portSimCfgName(i), sAgreedCfg.v[i]);
        }
        fputc('\n', sLog);
    }
    for (i = 0; sLogEnvNames[i] != NULL; i++) {
        const char *v = getenv(sLogEnvNames[i]);

        if (v != NULL && v[0] != '\0') {
            fprintf(sLog, "E %s=%s\n", sLogEnvNames[i], v);
        }
    }
    fflush(sLog);
    portConLogf("ge: net: session log -> %s\n", portRootPath(name));
}

/* the pieces of a tick's fingerprint that differ, by field name */
static void fpDiffNames(const unsigned int *a, const unsigned int *b, char *out, int cap)
{
    int i, n = 0;

    out[0] = '\0';
    for (i = 0; i < PORT_NET_HASH_FIELDS; i++) {
        if (a[i] != b[i]) {
            n += snprintf(out + n, (size_t) (cap - n), "%s%s", n > 0 ? "," : "", portCoopStateFieldName(i));
        }
    }
}

static void netReplayFinish(const char *why)
{
    int ok = sRepBad == 0 && sRepChecked > 0;

    portConLogf("ge: replay: %s: %u per-tick fingerprints compared (%u of them are 30-tick state hashes), %u mismatched\n", why, sRepChecked,
                sRepHashes, sRepBad);
    if (sRepChecked == 0) {
        portConLogf("ge: replay: nothing to compare (the log carries no fingerprints this build can read)\n");
        fflush(stdout);
        exit(3);
    }
    if (sRepBad != 0) {
        portConLogf("ge: replay: first mismatch at tick %u\n", sRepFirstBad);
    }
    portConLogf("ge: replay: %s\n", ok ? "OK: the replay reproduced the logged session tick for tick" : "FAILED: the replay does not match the log");
    fflush(stdout);
    fflush(stderr);
    exit(ok ? 0 : 1);
}

/* port_coop.c portCoopStageChangeNote: the stage was left on sim tick `tick`. A replay of a log that ended with the stage's own exit has no
 * "next tick" to notice the end of the log on: the exit tick is the last one, so finish here (the exit happened where the log says). */
void portNetReplayStageEnd(unsigned int tick)
{
    if (sReplay && sRepCount != 0 && tick >= sRepCount) {
        netReplayFinish("the logged stage ended on its own (the exit tick matches)");
    }
}

/* `net info`: one line each for the role, the link, the stage and the desync check */
#define NET_INFO_LINES 7
static void netInfoLines(char lines[NET_INFO_LINES][200])
{
    const PortNetStatSecond *sec = portNetStatsSecond();
    int ping = 0, jit = 0;

    const char *role = sRole == 1 ? "host (player 0)" : sRole == 2 ? "guest (player 1)" : sReplay ? "offline replay" : "none";
    const char *state = sReplay ? "replaying a session log" : sRole == 0 ? "netplay is off"
                        : sLost ? "the partner disconnected" : !sActive ? "connecting / handshake"
                        : (sLockOn && sLockInit) ? "playing (lockstep on)" : sStageOver ? "in the menus between stages"
                        : sReadySent ? "waiting for the partner to load the stage" : "loading the stage";

    snprintf(lines[0], 200, "net: role %s, %s", role, state);
    snprintf(lines[1], 200, "net: stage run %u, sim tick %u, input delay %d ticks, %u retraces held so far", sRun, portCoopNetTick(),
             sRole != 0 ? portNetDelay() : 0, sStallTotal);
    snprintf(lines[2], 200, "net: ping %d ms, silent for %u ms, ENet packet loss %d%%", sRole != 0 && !sLost ? netioRttMs() : 0,
             sRole != 0 && !sLost ? netioSilentMs() : 0u, sRole != 0 && !sLost ? netioLossPct() : 0);
    snprintf(lines[3], 200, "net: state hash: last compared at tick %u, %u sent, %u compared, %u mismatched; desyncs reported %u (%u files written)",
             sLastHashTick, sHashSent, sHashCompared, sHashBad, sHashBad, sDesyncFiles);
    if (sRole != 0 && !sLost) {
        ping = netioRttMs();
        jit = netioRttVarMs();
    }
    snprintf(lines[2], 200, "net: ping %d ms (smoothed round trip) +/- %d ms jitter, silent for %u ms, ENet packet loss %d%%", ping, jit,
             sRole != 0 && !sLost ? netioSilentMs() : 0u, sRole != 0 && !sLost ? netioLossPct() : 0);
    if (sec->valid) {
        snprintf(lines[4], 200, "net: last second: in %.1f KB/s %.0f pkt/s, out %.1f KB/s %.0f pkt/s (wire bytes incl. IP/UDP headers)", sec->inKBs, sec->inPps,
                 sec->outKBs, sec->outPps);
        snprintf(lines[5], 200, "net: inputs: %u late (ticks held for the partner), %u timer resends, %u duplicate records, %u out-of-window; input wait last second avg %.2f max %.1f ms",
                 sStatLate, sStatResends, sStatDups, sStatOow, sec->waitAvg, sec->waitMax);
        snprintf(lines[6], 200, "net: frame: %.0f fps, sim %.2f ms avg (max %.1f), render build %.2f ms avg (max %.1f)%s", sec->fps, sec->simAvg, sec->simMax,
                 sec->renderAvg, sec->renderMax, "");
    } else {
        snprintf(lines[4], 200, "net: last second: (no full second measured yet)");
        snprintf(lines[5], 200, "net: inputs: %u late (ticks held for the partner), %u timer resends, %u duplicate records, %u out-of-window", sStatLate,
                 sStatResends, sStatDups, sStatOow);
        snprintf(lines[6], 200, "net: frame: (no full second measured yet)");
    }
}

void portNetConsole(const char *arg1)
{
    char lines[NET_INFO_LINES][200];
    int i;

    if (arg1 != NULL && arg1[0] != '\0' && strcmp(arg1, "info") != 0) {
        portConPrintf(0, "usage: net [info]   (see also: net_graph 0|1|2|3)\n");
        return;
    }
    netInfoLines(lines);
    for (i = 0; i < NET_INFO_LINES; i++) {
        portConPrintf(0, "%s\n", lines[i]);
    }
}

/* ---- job 6: feeders for the stats ring and the net_graph overlay (render only) ---- */
void portNetStatsGather(PortNetStatSample *s)
{
    static unsigned int pIn, pOut, pInP, pOutP;
    static int init;
    unsigned int cIn, cOut, cInP, cOutP;

    s->tick = portCoopNetTick();
    if (sRole == 0) {
        return;
    }
    netioCounters(&cIn, &cOut, &cInP, &cOutP);
    if (init) {
        s->inBytes = cIn - pIn;
        s->outBytes = cOut - pOut;
        s->inPkts = cInP - pInP;
        s->outPkts = cOutP - pOutP;
    }
    init = 1;
    pIn = cIn;
    pOut = cOut;
    pInP = cInP;
    pOutP = cOutP;
    if (!sLost) {
        s->pingMs = netioRttMs();
        s->jitterMs = netioRttVarMs();
    }
    s->waitMs = sWaitMsCur;
    sWaitMsCur = 0.0f;
    if (sStatWaitedPending) {
        s->flags |= NSF_WAITED;
    }
    if (sStatResendPending) {
        s->flags |= NSF_RESEND;
    }
    s->flags |= sStatHashFlag;
    sStatWaitedPending = sStatResendPending = 0;
    sStatHashFlag = 0;
}

void portNetGraphInfo(PortNetGraphInfo *o)
{
    int i;

    memset(o, 0, sizeof(*o));
    o->netplay = sRole != 0 || sReplay;
    o->linked = sRole != 0 && !sLost && sLockOn && sLockInit;
    o->role = sRole;
    o->tick = portCoopNetTick();
    o->delay = sRole != 0 ? portNetDelay() : 0;
    if (sRole != 0 && !sLost) {
        o->pingMs = netioRttMs();
        o->jitterMs = netioRttVarMs();
        o->enetLossPct = netioLossPct();
    }
    o->resends = sStatResends;
    o->oow = sStatOow;
    o->dups = sStatDups;
    o->lateTicks = sStatLate;
    o->lastHashTick = sLastHashTick;
    o->lastHashVerdict = sLastVerdict;
    o->hashCompared = sHashCompared;
    o->desyncs = sHashBad;
    o->diffMask = sDiffMask;
    o->diffTick = sDiffTick;
    o->nfields = PORT_NET_HASH_FIELDS < NETGRAPH_FIELDS ? PORT_NET_HASH_FIELDS : NETGRAPH_FIELDS;
    for (i = 0; i < o->nfields; i++) {
        o->fieldName[i] = portCoopStateFieldName(i);
    }
}

/* front.c frontChangeMenu: the guest tried to start a stage from a menu (briefing Start, next mission, retry ...): say so on screen. */
void portNetGuestRefusedNote(void)
{
    portConPrintf(1, "Host chooses the mission - waiting for the host to start it");
}

int portNetMissionLocked(void)
{
    return sRole == 2 && sActive && !sLost;   /* the guest does not choose the mission (job 4): the host does */
}

/* GE_NET_TEST_CMD=<tick>:<h|g>:<console line, '+' for a space>[,...] (test): at the start of that sim tick the host (h) or guest (g)
 * machine types the console line, e.g. 60:g:spawn+guard+4+800,200:g:give+goldeneye_key+p2 */
static void netTestCmds(unsigned int tick)
{
    static const char *spec;
    static int init;
    const char *s;
    extern void portConExec(const char *line);

    if (!init) {
        init = 1;
        spec = getenv("GE_NET_TEST_CMD");
    }
    for (s = spec; s != NULL && *s != '\0';) {
        unsigned int t = 0;
        char who = 0, line[160];
        int used = 0, n = 0;

        if (sscanf(s, "%u:%c:%n", &t, &who, &used) < 2 || used <= 0) {
            break;
        }
        s += used;
        while (*s != '\0' && *s != ',' && n < (int) sizeof(line) - 1) {
            line[n++] = (*s == '+') ? ' ' : *s;
            s++;
        }
        line[n] = '\0';
        if (*s == ',') {
            s++;
        }
        if (t == tick && ((who == 'h' && sRole == 1) || (who == 'g' && sRole == 2))) {
            portConLogf("ge: net: TEST: typing '%s' at tick %u\n", line, tick);
            portConExec(line);
        }
    }
}

static unsigned int netEnvUInt(const char *name)
{
    const char *e = getenv(name);

    return (e != NULL && e[0] != '\0') ? (unsigned int) atol(e) : 0xFFFFFFFFu;
}

void portNetSessionTick(unsigned int tick)
{
    char none[2];

    if (!sReplay) {   /* job 4 test hooks, both deterministic in sim ticks */
        netTestCmds(tick);
        if (sRole == 1 && sRun == 1u && tick == netEnvUInt("GE_NET_TEST_EARLYNEXT")) {   /* test (host): tell the guest the next stage while it is still in this one: it must hold the message and follow when its stage ends */
            unsigned char b2[PORT_NET_BUF_MAX];
            const unsigned int seed2 = sSetup.seed * 1664525u + 1013904223u + 2u;   /* what portNetStageBegin computes for run 2 */
            const int n2 = portNetNextStagePackM(2u, "30", 1, seed2, sCurMode, b2, sizeof(b2));

            if (n2 > 0) {
                netSendSession(b2, n2);
                portConLogf("ge: net: TEST: sent NEXTSTAGE (run 2, depot) to the guest at sim tick %u, while it is still in stage 1\n", tick);
            }
        }
        if (sTestEndTick != 0 && sRun == 1u && tick == sTestEndTick) {
            portConLogf("ge: net: TEST: ending stage run 1 at sim tick %u (back to the title)\n", tick);
            netReturnToTitle();
        }
        if (sTestInfoTick != 0 && tick == sTestInfoTick) {
            char lines[NET_INFO_LINES][200];
            int i;

            netInfoLines(lines);
            for (i = 0; i < NET_INFO_LINES; i++) {
                portConLogf("ge: net: info at tick %u: %s\n", tick, lines[i]);
            }
        }
    }
    netRngTraceFlush();
    if (sLog == NULL && !sReplay) {
        return;
    }
    memset(&sTickFp, 0, sizeof(sTickFp));
    sTickFp.tick = tick;
    sTickFp.run = sRun;
    none[0] = '\0';
    none[1] = '\0';
    portCoopStateHash(&sTickFp, none, 1);   /* cap 1: only the hashes, no dump text */
    if (!sReplay) {
        return;
    }
    if (tick > sRepCount || (sRepStopAt != 0 && tick > sRepStopAt)) {
        netReplayFinish(tick > sRepCount ? "the log has ended" : "GE_NET_REPLAY_STOP reached");
    }
    {
        const NetLogTick *lt = &sRepTick[tick - 1];
        char names[200];

        if (lt->has) {
            sRepChecked++;
            if (tick % 30u == 0) {
                sRepHashes++;
            }
            if (lt->hash != sTickFp.hash || memcmp(lt->fields, sTickFp.fields, sizeof(lt->fields)) != 0) {
                if (sRepBad++ == 0) {
                    sRepFirstBad = tick;
                }
                if (sRepBad <= 8) {
                    fpDiffNames(lt->fields, sTickFp.fields, names, sizeof(names));
                    portConLogf("ge: replay: MISMATCH at tick %u: log %08x, replay %08x, differing fields: %s\n", tick, lt->hash, sTickFp.hash,
                                names[0] != '\0' ? names : "(hash only)");
                }
            }
        }
    }
}

void portNetSessionRecords(unsigned int tick, const PortNetInput *rec0, const PortNetInput *rec1)
{
    char h0[PORT_NET_RECHEX + 1], h1[PORT_NET_RECHEX + 1];
    int i;

    if (sLog == NULL) {
        return;
    }
    portNetRecToHex(rec0, h0);
    portNetRecToHex(rec1, h1);
    fprintf(sLog, "T %u %s %s %08x", tick, h0, h1, sTickFp.hash);
    for (i = 0; i < PORT_NET_HASH_FIELDS; i++) {
        fprintf(sLog, " %08x", sTickFp.fields[i]);
    }
    fputc('\n', sLog);
    fflush(sLog);   /* the port leaves through _exit, which skips stdio */
}

int portNetBlocked(const char *what)
{
    static char seen[8][80];
    static int nseen;
    int i;

    if (sRole == 0 && !sReplay) {
        return 0;
    }
    for (i = 0; i < nseen; i++) {
        if (strcmp(seen[i], what) == 0) {
            return 1;
        }
    }
    if (nseen < 8) {
        snprintf(seen[nseen++], sizeof(seen[0]), "%s", what);
    }
    portConLogf("ge: net: %s is blocked in netplay (it would change the sim on this machine only)\n", what);
    return 1;
}

/* Reads a log into memory. Returns 0 on success; prints why and returns -1 otherwise. */
static int netReplayLoad(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[1024];
    unsigned int cap = 0, lineNo = 0;
    int haveG = 0, haveO[2] = { 0, 0 };

    if (f == NULL) {
        fprintf(stderr, "ge: replay: cannot open %s\n", path);
        return -1;
    }
    memset(&sSetup, 0, sizeof(sSetup));
    sSetup.proto = PORT_NET_PROTO;
    while (fgets(line, sizeof(line), f) != NULL) {
        size_t len = strlen(line);

        lineNo++;
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        if (line[0] == '#' || len < 2) {
            continue;
        }
        if (line[0] == 'V') {
            int proto = 0, delay = 0;
            unsigned int run = 0;
            char role[16];

            if (sscanf(line + 1, " %d role %15s run %u delay %d", &proto, role, &run, &delay) != 4) {
                fprintf(stderr, "ge: replay: %s:%u: bad V line\n", path, lineNo);
                return -1;
            }
            if (proto != PORT_NET_PROTO && proto != 6 && proto != 5 && proto != 4 && proto != 3) {   /* (4: job 3c's logs; job 4 only added flag bits) */
                fprintf(stderr, "ge: replay: the log is protocol %d, this build speaks %d\n", proto, PORT_NET_PROTO);
                return -1;
            }
            if (proto == 3) {   /* job 3b's logs: the records replay (to look at a session), but its 8-field fingerprints cannot be compared */
                fprintf(stderr, "ge: replay: the log is protocol 3 (build before job 3c): it replays, but its fingerprints are not compared\n");
            }
            sSetup.delay = (unsigned char) delay;
        } else if (line[0] == 'B') {
            snprintf(sSetup.build, sizeof(sSetup.build), "%s", line + 2);
        } else if (line[0] == 'G') {
            char stage[PORT_NET_STR_STAGE];
            int diff = 0;
            unsigned int seed = 0, lua = 0;

            if (sscanf(line + 1, " stage %31s diff %d seed %u lua %x", stage, &diff, &seed, &lua) != 4) {
                fprintf(stderr, "ge: replay: %s:%u: bad G line\n", path, lineNo);
                return -1;
            }
            snprintf(sSetup.stage, sizeof(sSetup.stage), "%s", stage);
            sSetup.diff = diff;
            sSetup.seed = seed;
            sSetup.luaHash = lua;
            haveG = 1;
        } else if (line[0] == 'M') {
            snprintf(sSetup.mods, sizeof(sSetup.mods), "%s", line + 2);
            sReplayHeaderMods = 1;
        } else if (line[0] == 'O') {
            int p = 0;
            char hx[64];

            if (sscanf(line + 1, " %d %63s", &p, hx) != 2 || p < 0 || p > 1 || strlen(hx) != PORT_NET_OPTS_SIZE * 2 || optsFromHex(hx, &sOpts[p]) != 0) {
                fprintf(stderr, "ge: replay: %s:%u: bad O line\n", path, lineNo);
                return -1;
            }
            haveO[p] = 1;
        } else if (line[0] == 'K') {   /* job 3c: "K name=value name=value ..." (names hold no spaces) */
            char *tok;

            sAgreedCfg.n = 0;
            for (tok = strtok(line + 1, " "); tok != NULL; tok = strtok(NULL, " ")) {
                char *eq = strchr(tok, '=');
                int k;

                if (eq == NULL) {
                    continue;
                }
                *eq = '\0';
                for (k = 0; k < portSimCfgCount(); k++) {
                    if (strcmp(portSimCfgName(k), tok) == 0) {
                        sAgreedCfg.v[k] = atoi(eq + 1);
                        if (k + 1 > sAgreedCfg.n) {
                            sAgreedCfg.n = k + 1;
                        }
                    }
                }
            }
            sRepHaveCfg = 1;
        } else if (line[0] == 'E') {
            char *eq = strchr(line + 2, '=');

            if (eq != NULL) {
                *eq = '\0';
                netSetenv(line + 2, eq + 1);
            }
        } else if (line[0] == 'C') {   /* job 5: "C tick issuer seq kind player a b": a session command applied at that tick */
            PortNetCmd c;

            memset(&c, 0, sizeof(c));
            if (sscanf(line + 1, " %u %d %u %d %d %d %d", &c.tick, &c.issuer, &c.seq, &c.kind, &c.player, &c.a, &c.b) != 7 || portNetCmdValid(&c) != 0
                || sRepCmdCount >= sizeof(sRepCmd) / sizeof(sRepCmd[0])) {
                fprintf(stderr, "ge: replay: %s:%u: bad or surplus C line\n", path, lineNo);
                return -1;
            }
            sRepCmd[sRepCmdCount++] = c;
        } else if (line[0] == 'T') {
            unsigned int tick = 0;
            int off = 0, k;
            char h0[64], h1[64];
            NetLogTick *lt;

            if (sscanf(line + 1, " %u %63s %63s%n", &tick, h0, h1, &off) < 3 || tick != sRepCount + 1) {
                fprintf(stderr, "ge: replay: %s:%u: bad or out-of-order T line (wanted tick %u)\n", path, lineNo, sRepCount + 1);
                return -1;
            }
            if (sRepCount == cap) {
                cap = cap ? cap * 2 : 4096;
                sRepTick = (NetLogTick *) realloc(sRepTick, (size_t) cap * sizeof(NetLogTick));
                if (sRepTick == NULL) {
                    fprintf(stderr, "ge: replay: out of memory\n");
                    return -1;
                }
            }
            lt = &sRepTick[sRepCount];
            memset(lt, 0, sizeof(*lt));
            if (portNetRecFromHex(h0, &lt->rec[0]) != 0 || portNetRecFromHex(h1, &lt->rec[1]) != 0) {
                fprintf(stderr, "ge: replay: %s:%u: bad record\n", path, lineNo);
                return -1;
            }
            {   /* the fingerprint: hash and PORT_NET_HASH_FIELDS fields (a live side without GE_NET_LOG's fingerprint cannot be) */
                const char *p = line + 1 + off;
                unsigned int v;
                int used;

                if (sscanf(p, " %x%n", &v, &used) == 1) {
                    lt->hash = v;
                    p += used;
                    lt->has = 1;
                    for (k = 0; k < PORT_NET_HASH_FIELDS; k++) {
                        if (sscanf(p, " %x%n", &v, &used) != 1) {
                            lt->has = 0;
                            break;
                        }
                        lt->fields[k] = v;
                        p += used;
                    }
                }
            }
            sRepCount++;
        }
    }
    fclose(f);
    if (!haveG || !haveO[0] || !haveO[1] || sRepCount == 0) {
        fprintf(stderr, "ge: replay: %s is not a complete session log (stage line %d, settings %d/%d, %u ticks)\n", path, haveG, haveO[0], haveO[1], sRepCount);
        return -1;
    }
    return 0;
}

/* First thing in main() (from portNetPreinit): GE_NET_REPLAY=<file>. Loads the log and sets what the stage launch reads. */
static void netReplayStart(const char *path)
{
    const char *v = getenv("GE_NET_REPLAY_VIEW");
    const char *stop = getenv("GE_NET_REPLAY_STOP");
    char num[32];

    if (netReplayLoad(path) != 0) {
        exit(2);
    }
    sReplay = 1;
    sReplayView = (v != NULL && v[0] == '1') ? 1 : 0;
    sRepStopAt = (stop != NULL && stop[0] != '\0') ? (unsigned int) atol(stop) : 0u;
    sOptsKnown = 1;
    netSetenv("GE_COOP", "1");
    netSetenv("GE_FIXEDTICK", "1");
    netSetenv("GE_TICKHZ", "60");
    netSetenv("GE_SKIPINTRO", "1");
    netSetenv("GE_COOP_VIEW", sReplayView ? "1" : "0");
    netSetenv("GE_STAGE", sSetup.stage);
    snprintf(num, sizeof(num), "%d", sSetup.diff);
    netSetenv("GE_DIFF", num);
    snprintf(num, sizeof(num), "%u", sSetup.seed);
    netSetenv("GE_SEED", num);
    sLogWanted = getenv("GE_NET_LOG") != NULL && getenv("GE_NET_LOG")[0] != '\0' && getenv("GE_NET_LOG")[0] != '0';
}

/* ---- boot ---------------------------------------------------------------------------------- */

/* First thing in main(), before the ini and the tick rate are read: what netplay forces on both sides. The stage,
 * difficulty and seed follow with the handshake. The tick rate is resolved from GE_TICKHZ the first time anything
 * asks for it (frametiming.c), so setting it later left a profile's ini value (120 Hz) in force. */
void portNetPreinit(void)
{
    const char *spec = getenv("GE_NET");
    const char *rp = getenv("GE_NET_REPLAY");

    if (rp != NULL && rp[0] != '\0') {
        if (spec != NULL && spec[0] != '\0') {
            fprintf(stderr, "ge: GE_NET and GE_NET_REPLAY are exclusive\n");
            exit(2);
        }
        netReplayStart(rp);
        return;
    }
    if (spec == NULL || spec[0] == '\0') {
        return;
    }
    netSetenv("GE_COOP", "1");
    netSetenv("GE_FIXEDTICK", "1");
    netSetenv("GE_TICKHZ", "60");
    netSetenv("GE_SKIPINTRO", "1");
    netSetenv("GE_COOP_VIEW", strncmp(spec, "join:", 5) == 0 ? "1" : "0");   /* host is player 0, guest is player 1 */
}

void portNetBoot(void)
{
    const char *spec = getenv("GE_NET");
    const char *lag = getenv("GE_NET_LAG");
    const char *w = getenv("GE_NET_WAIT");

    if (sReplay) {   /* offline replay: no socket; check the ini's modifiers are the session's */
        char now[PORT_NET_STR_MODS];

        netModsText(now, sizeof(now));
        if (sReplayHeaderMods && netModsDiffer(sSetup.mods, now)) {
            portConLogf("ge: replay: WARNING the modifiers differ from the session's (log '%s', now '%s'): use the same profile\n", sSetup.mods, now);
        }
        netApplySessionMods(sSetup.mods);   /* the session's co-op settings (exits / respawn / lives) come from the log, not from this machine */
        portConLogf("ge: replay: %u ticks of %s on %s, difficulty %d, seed %u, view %d\n", sRepCount, "the logged session", sSetup.stage, sSetup.diff,
                    sSetup.seed, sReplayView);
        {   /* job 3c: this machine's sim-reading ini values against the session's */
            PortNetSimCfg here;
            char diff[400];

            portSimCfgCollect(&here);
            if (!sRepHaveCfg) {
                sAgreedCfg = here;
                portConLogf("ge: replay: the log has no sim-settings (K) line (an older build's log); using this machine's\n");
            } else if (portSimCfgDiff(&here, &sAgreedCfg, diff, sizeof(diff)) != 0) {
                portConLogf("ge: replay: WARNING the sim settings of this machine differ from the session's: %s (mine = this machine, theirs = the log); the replay will not match\n", diff);
            }
        }
        return;
    }
    if (spec == NULL || spec[0] == '\0') {
        return;   /* netplay off: no socket, no ENet init */
    }
    {
        const char *lg = getenv("GE_NET_LOG");

        sLogWanted = lg != NULL && lg[0] != '\0' && lg[0] != '0';
    }
    if (w != NULL && w[0] != '\0') {
        sWaitMs = atoi(w) * 1000;
    }
    {
        const char *stop = getenv("GE_NET_STOP");

        sStopTick = (stop != NULL && stop[0] != '\0') ? (unsigned int) atol(stop) : 0u;
    }
    {   /* job 4 test knobs */
        const char *et = getenv("GE_NET_TEST_ENDSTAGE");
        const char *nx = getenv("GE_NET_TEST_NEXT");

        if (nx == NULL || nx[0] == '\0') {
            nx = getenv("GE_NET_TEST_ROGUE");
        }
        const char *ti = getenv("GE_NET_TEST_INFO");

        sTestEndTick = (et != NULL && et[0] != '\0') ? (unsigned int) atol(et) : 0u;
        sTestInfoTick = (ti != NULL && ti[0] != '\0') ? (unsigned int) atol(ti) : 0u;
        if (nx != NULL && nx[0] != '\0') {
            const char *colon = strchr(nx, ':');
            const size_t len = colon != NULL ? (size_t) (colon - nx) : strlen(nx);

            if (len > 0 && len < sizeof(sTestNextStage)) {
                memcpy(sTestNextStage, nx, len);
                sTestNextStage[len] = '\0';
                sTestNextDiff = colon != NULL ? atoi(colon + 1) : 0;
            }
        }
    }
    if (lag != NULL && lag[0] != '\0') {
        int ms = 0, jit = 0, loss = 0;

        sscanf(lag, "%d,%d,%d", &ms, &jit, &loss);
        netioSetFake(ms, jit, loss);
        portConLogf("ge: net: fake network: lag %d ms, jitter %d ms, loss %d%%\n", ms, jit, loss);
    }
    netModsText(sMineMods, sizeof(sMineMods));
    sBootMs = netioNowMs();
    {
        const char *td = getenv("GE_NET_TEST_DESYNC");
        const char *off = getenv("GE_NET_OPTS_OFF");

        sTestDesyncTick = (td != NULL && td[0] != '\0') ? (unsigned int) atol(td) : 0u;
        sOptsOff = off != NULL && off[0] != '\0' && off[0] != '0';
        if (sTestDesyncTick != 0) {
            portConLogf("ge: net: TEST: this side will perturb its RNG at tick %u\n", sTestDesyncTick);
        }
    }
    if (strncmp(spec, "host:", 5) == 0) {
        const char *stage = getenv("GE_STAGE"), *diff = getenv("GE_DIFF"), *seed = getenv("GE_SEED"), *dl = getenv("GE_NET_DELAY");

        sRole = 1;
        sPort = atoi(spec + 5);
        if (stage == NULL || stage[0] == '\0') {
            netFail("host needs GE_STAGE (and optionally GE_DIFF, GE_SEED)");
        }
        memset(&sSetup, 0, sizeof(sSetup));
        sSetup.proto = PORT_NET_PROTO;
        snprintf(sSetup.build, sizeof(sSetup.build), "%s", netBuildId());
        snprintf(sSetup.stage, sizeof(sSetup.stage), "%s", stage);
        sSetup.diff = (diff != NULL && diff[0] >= '0' && diff[0] <= '3') ? diff[0] - '0' : 0;   /* 0..3: the guest refuses anything else */
        sSetup.seed = (seed != NULL && seed[0] != '\0') ? (unsigned int) atol(seed) : (unsigned int) netioNowMs() * 2654435761u;
        {   /* the host's co-op session settings ride behind the compared text */
            char sess[96];

            portModifierSessionText(sess, sizeof(sess));
            snprintf(sSetup.mods, sizeof(sSetup.mods), "%s%s%s", sMineMods, NET_SESSION_MARK, sess);
            netHostChooseMode();   /* stage 1's game mode rides in the session part ("mode=<name>") */
        }
        sSetup.luaHash = netLuaHash();
        sSetup.delay = (unsigned char) portNetLockClampDelay((dl != NULL && dl[0] != '\0') ? atoi(dl) : NET_DEFAULT_DELAY);
        if (netioHost(sPort) != 0) {
            netFail("cannot open the host socket");
        }
        portConLogf("ge: net: hosting on UDP %d, waiting up to %d s for the guest\n", sPort, sWaitMs / 1000);
        netStep(NS_CONNECT, "Waiting for partner to join...");
    } else if (strncmp(spec, "join:", 5) == 0) {
        const char *colon = strrchr(spec + 5, ':');

        if (colon == NULL || colon - (spec + 5) >= (int) sizeof(sIp)) {
            netFail("GE_NET=join:<ip>:<port> expected");
        }
        memcpy(sIp, spec + 5, (size_t) (colon - (spec + 5)));
        sIp[colon - (spec + 5)] = '\0';
        sPort = atoi(colon + 1);
        sRole = 2;
        memset(&sHello, 0, sizeof(sHello));
        sHello.proto = PORT_NET_PROTO;
        snprintf(sHello.build, sizeof(sHello.build), "%s", netBuildId());
        if (netioJoin(sIp, sPort) != 0) {
            netFail("cannot start connecting");
        }
        portConLogf("ge: net: joining %s:%d, waiting up to %d s\n", sIp, sPort, sWaitMs / 1000);
        netStep(NS_CONNECT, "Connecting to the host...");
    } else {
        netFail("GE_NET must be host:<port> or join:<ip>:<port>");
    }
    atexit(netioShutdown);   /* a clean close, so the partner logs a disconnect at once */
}

void portNetTick(void)
{
    /* the link is serviced from portNetHold, every retrace; nothing to add per sim tick */
}

/* Netplay job 3e: the level's music is held while a netplay stage is loaded and the lockstep GO has not come (mp_music.c). Not held in a
 * session that is over, an offline replay, or a stage that is not a co-op one. */
extern void portNetMusicRelease(void);   /* mp_music.c */
int portNetMusicHold(void)
{
    return sActive && !sLost && !sReplay && sRole != 0 && portCoopActive() && !sLockOn;
}

int portNetActive(void)
{
    return (sActive && !sLost) || sReplay;   /* (a session whose partner left is over: later stages are plain local co-op) */
}
