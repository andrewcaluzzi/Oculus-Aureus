/**
 * port_arenamenu.c - the arena's start menu and post-game, C side of the stage hand-off (modding/arena.md "Start menu and post-game").
 *
 * The arena's menus are a LOBBY: the Colosseum itself, loaded with the mode `arena-menu` (tools/mode_arena_menu.lua). Bond is held
 * (plan.hold, port_planning.c), the crowd fills the stands, the camera turns slowly, and the start menu or the post-game is drawn over
 * it (port_arenamenu_draw.c). A run is a fresh stage load of the Colosseum with the run's mode and settings, exactly as a launcher
 * starts one. What this file does:
 *   - THE HAND-OFF. Lua arms what comes after this stage (amenu.launch / amenu.arm / amenu.quit). When the stage has ended and the
 *     front end would open the stock mission report (front.c, after MENU_RUN_STAGE), portArenaMenuAfterStage sets GE_MODE / GE_COOP /
 *     GE_COLOSSEUM_SLOTS for the next stage and front.c launches the Colosseum again at once: no dossier, no menus. The next stage's
 *     scripts read what they were launched with (amenu.current: the mode, the ARENA_SETTINGS text, the preset, the lobby page).
 *   - THE LAST RUN: its result (a Lua table as text) and its launch (for Retry) live here between the stages.
 *   - THE PREFERENCES FILE: Data/Saves/arena_menu.ini (portRootPath, so GE_PROFILE moves it with the saves), key=value lines of the
 *     menu's last choices. GE_ARENAMENU_FILE=<path> uses another file, GE_NOMENUSAVE=1 never writes it (tests).
 *   - Escape = Back while the lobby holds the controls (bondview2.c asks portArenaMenuEscape before it opens the watch).
 *
 * Sim class: SIM (scripts/port/snap_embed.py). The hand-off is an input of the next stage's sim (its settings); it only changes on a
 * script's call, so a rollback restores and re-sets it identically. The preferences file feeds the lobby's Lua state, so the lobby
 * never reads it in netplay (each machine has its own file): the menu flow is solo and local co-op only (amenu.netplay()).
 */
#ifndef TARGET_N64

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif
#include <ultra64.h>

#include "port_arenamenu.h"

extern void portConLogf(const char *fmt, ...);
extern const char *portRootPath(const char *name);
extern int portNetActive(void);
extern int portModeActive(void);
extern s32 portModeEndMission(s32 win);
extern int g_portCoopEpoch;
extern int portPlanningHeld(void);
extern void portPlanningTestInput(int p, unsigned mask);
extern void portBenchSweep(int on, float rate, float pitch);

static AmLaunch sNext;          /* what the front end starts when this stage is over */
static s32 sArmed;              /* AM_ARM_NONE / AM_ARM_LAUNCH / AM_ARM_QUIT */
static AmLaunch sCur;           /* what the current stage was launched with ... */
static s32 sCurEpoch = -1000;   /* ... valid while g_portCoopEpoch is this */
static AmLaunch sRetry;         /* the last run's own launch */
static s32 sRetryOk;
static char sResult[AM_RESULT_LEN];
static s32 sEscOn, sEscEpoch = -1000;

static void cpy(char *d, size_t n, const char *s) { snprintf(d, n, "%s", s != NULL ? s : ""); }

static void amSetenv(const char *k, const char *v)
{
    char *s = (char *) malloc(strlen(k) + strlen(v) + 2);   /* putenv keeps the pointer: never freed (as port_net.c netSetenv) */

    if (s == NULL) return;
    sprintf(s, "%s=%s", k, v);
    putenv(s);
}

void portArenaMenuLaunchSet(AmLaunch *dst, const char *mode, const char *settings, const char *preset, const char *label,
                            const char *lobby, int coop, int slots)
{
    memset(dst, 0, sizeof(*dst));
    cpy(dst->mode, sizeof(dst->mode), mode);
    cpy(dst->settings, sizeof(dst->settings), settings);
    cpy(dst->preset, sizeof(dst->preset), preset);
    cpy(dst->label, sizeof(dst->label), label);
    cpy(dst->lobby, sizeof(dst->lobby), lobby);
    dst->coop = coop;
    dst->slots = slots;
}

/* SECURITY: mode scripts (third-party mods too) give the launch; its mode becomes the next stage's GE_MODE. It must be what the mode
 * loader accepts (port_luavm.c portLuaModeScriptPath: letters, digits, _ and -), 1..31 characters, or empty (= the lobby). The
 * other fields are clamped. 0 = refused (the launch is not armed) */
static int modeOk(const char *m)
{
    size_t n = 0;

    for (; *m != '\0'; m++, n++)
    {
        if (n >= 31 || !((*m >= 'a' && *m <= 'z') || (*m >= 'A' && *m <= 'Z') || (*m >= '0' && *m <= '9') || *m == '_' || *m == '-')) return 0;
    }
    return 1;
}

static int launchSanitize(AmLaunch *l)
{
    l->mode[sizeof(l->mode) - 1] = '\0';
    if (!modeOk(l->mode))
    {
        portConLogf("ge: arenamenu: launch refused: mode name '%.40s' is not a mode\n", l->mode);
        return 0;
    }
    if (l->coop < -1 || l->coop > 1) l->coop = -1;
    if (l->slots < 0 || l->slots > 4000) l->slots = 0;   /* (GE_COLOSSEUM_SLOTS: the stage takes 240..4000) */
    return 1;
}

/* amenu.arm: what to start when this stage ends (the run arms the post-game at its start and again at its end) */
void portArenaMenuArm(const AmLaunch *next)
{
    AmLaunch t;

    if (portNetActive()) return;   /* netplay: the host's menus choose the next stage (port_net.c); not supported here yet */
    t = *next;
    if (!launchSanitize(&t))
    {
        return;   /* (whatever was armed before stays) */
    }
    sNext = t;
    sArmed = AM_ARM_LAUNCH;
}

void portArenaMenuDisarm(void) { sArmed = AM_ARM_NONE; }

/* amenu.launch: arm it and end this stage now (the shared mission end: solo or local co-op). 1 = the stage is ending */
int portArenaMenuLaunchNow(const AmLaunch *next)
{
    AmLaunch t;

    if (portNetActive() || !portModeActive()) return 0;
    t = *next;
    if (!launchSanitize(&t))
    {
        return 0;   /* a bad mode name: nothing is armed, the stage goes on */
    }
    portArenaMenuArm(&t);
    portBenchSweep(0, 0.0f, 0.0f);   /* the lobby's camera turn must not carry into the run */
    portArenaMenuViewOff();          /* ... nor the menu's hold on the mouse */
    portArenaMenuEscapeBack(0);
    if (!portModeEndMission(0))
    {
        sArmed = AM_ARM_NONE;
        portConLogf("ge: arenamenu: launch refused (the stage would not end)\n");
        return 0;
    }
    portConLogf("ge: arenamenu: launch armed: mode '%s' preset '%s' lobby '%s' coop %d slots %d (%d bytes of settings)\n",
                sNext.mode, sNext.preset, sNext.lobby, sNext.coop, sNext.slots, (int) strlen(sNext.settings));
    return 1;
}

/* amenu.quit: end this stage and go to the Files screen (the title menus), not the mission report */
int portArenaMenuQuitNow(void)
{
    if (!portModeActive()) return 0;
    portBenchSweep(0, 0.0f, 0.0f);
    portArenaMenuViewOff();
    portArenaMenuEscapeBack(0);
    sArmed = AM_ARM_QUIT;
    if (!portModeEndMission(0))
    {
        sArmed = AM_ARM_NONE;
        return 0;
    }
    portConLogf("ge: arenamenu: quit to the title armed\n");
    return 1;
}

/* front.c, a stage is over and the front end would show the mission report: 0 = do that (stock), 1 = launch the stage again now
 * (selected_stage is still the Colosseum), 2 = go to the Files screen */
s32 portArenaMenuAfterStage(void)
{
    const s32 a = sArmed;
    char v[16];

    sArmed = AM_ARM_NONE;
    portArenaMenuViewOff();   /* (whatever comes next, the menu does not hold the mouse) */
    if (a == AM_ARM_NONE || portNetActive())
    {
        return 0;
    }
    if (a == AM_ARM_QUIT)
    {
        portConLogf("ge: arenamenu: after the stage: to the title menus\n");
        return 2;
    }
    amSetenv("GE_MODE", sNext.mode[0] ? sNext.mode : "arena-menu");
    if (sNext.coop >= 0) amSetenv("GE_COOP", sNext.coop ? "1" : "0");
    if (sNext.slots > 0)
    {
        snprintf(v, sizeof(v), "%d", (int) sNext.slots);
        amSetenv("GE_COLOSSEUM_SLOTS", v);
    }
    sCur = sNext;
    sCurEpoch = g_portCoopEpoch + 1;   /* the next stage start (portCoopStageBegin counts every one) */
    portConLogf("ge: arenamenu: after the stage: launching the Colosseum again: mode '%s' preset '%s' lobby '%s'\n", sCur.mode, sCur.preset,
                sCur.lobby);
    return 1;
}

/* amenu.current: what this stage was launched with by the hand-off; NULL = a plain launch (a launcher, the Mods page) */
const AmLaunch *portArenaMenuCurrent(void)
{
    return (sCurEpoch == g_portCoopEpoch && portModeActive()) ? &sCur : NULL;
}

void portArenaMenuRetrySet(const AmLaunch *l)
{
    AmLaunch t = *l;

    if (launchSanitize(&t)) { sRetry = t; sRetryOk = 1; }
}
const AmLaunch *portArenaMenuRetry(void) { return sRetryOk ? &sRetry : NULL; }

void portArenaMenuResultSet(const char *text) { cpy(sResult, sizeof(sResult), text); }
const char *portArenaMenuResult(void) { return sResult; }

/* ---- Escape = Back in the lobby ---- */

void portArenaMenuEscapeBack(int on) { sEscOn = on ? 1 : 0; sEscEpoch = g_portCoopEpoch; }

/* bondview2.c, the solo Escape key, before it opens the watch: 1 = taken (a BACK event for the lobby's board) */
int portArenaMenuEscape(int p)
{
    if (!sEscOn || sEscEpoch != g_portCoopEpoch || !portModeActive() || !portPlanningHeld())
    {
        return 0;
    }
    portPlanningTestInput(p, 32u /* PE_BACK */);
    return 1;
}

/* ---- the preferences file ---- */

#define PREF_N   64
#define PREF_KEY 32
#define PREF_VAL 64

static char sPrefK[PREF_N][PREF_KEY], sPrefV[PREF_N][PREF_VAL];
static s32 sPrefN, sPrefLoaded;
static char sPrefPath[1200];

/* SECURITY: GE_ARENAMENU_FILE (tests) is read ONCE, when the process starts, before any script can run, and never again; nothing a
 * script can do (the hand-off sets only GE_MODE / GE_COOP / GE_COLOSSEUM_SLOTS) can point the file somewhere else */
static char sPrefEnv[1024];

#if defined(__GNUC__)
__attribute__((constructor))
#endif
static void prefEnvAtStartup(void)
{
    const char *e = getenv("GE_ARENAMENU_FILE");

    if (e != NULL && e[0] != '\0' && strlen(e) < sizeof(sPrefEnv))
    {
        cpy(sPrefEnv, sizeof(sPrefEnv), e);
    }
}

static const char *prefPath(void)
{
    if (sPrefPath[0] == '\0')
    {
        cpy(sPrefPath, sizeof(sPrefPath), sPrefEnv[0] != '\0' ? sPrefEnv : portRootPath("Data/Saves/arena_menu.ini"));
    }
    return sPrefPath;
}

/* SECURITY: a value is printable ASCII only (no CR, LF or any control character, so one value can never become another line of the
 * file), shorter than PREF_VAL (a longer one would be cut, not stored as given) */
static int valOk(const char *v)
{
    size_t n = 0;

    if (v == NULL) return 0;
    for (; *v != '\0'; v++, n++)
    {
        if (n >= PREF_VAL - 1 || (unsigned char) *v < 0x20 || (unsigned char) *v > 0x7E) return 0;
    }
    return 1;
}

static int keyOk(const char *k)
{
    size_t n = 0;

    if (k == NULL || k[0] == '\0') return 0;
    for (; *k != '\0'; k++, n++)
    {
        const char c = *k;

        if (n >= PREF_KEY - 1 || !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) return 0;
    }
    return 1;
}

static void prefLoad(void)
{
    char line[200];
    FILE *f;

    if (sPrefLoaded) return;
    sPrefLoaded = 1;
    sPrefN = 0;
    f = fopen(prefPath(), "r");
    if (f == NULL)
    {
        portConLogf("ge: arenamenu: no preferences yet (%s)\n", prefPath());
        return;
    }
    while (fgets(line, sizeof(line), f) != NULL && sPrefN < PREF_N)
    {
        char *eq = strchr(line, '='), *nl;

        if (eq == NULL || line[0] == ';' || line[0] == '[' || line[0] == '#') continue;
        *eq = '\0';
        for (nl = eq + 1; *nl != '\0'; nl++)
        {
            if (*nl == '\r' || *nl == '\n') { *nl = '\0'; break; }
        }
        if (keyOk(line) && valOk(eq + 1))
        {
            cpy(sPrefK[sPrefN], PREF_KEY, line);
            cpy(sPrefV[sPrefN], PREF_VAL, eq + 1);
            sPrefN++;
        }
    }
    fclose(f);
    portConLogf("ge: arenamenu: read %d preferences from %s\n", (int) sPrefN, prefPath());
}

int portArenaMenuPrefCount(void) { prefLoad(); return sPrefN; }

const char *portArenaMenuPref(int k, const char **val)
{
    prefLoad();
    if (k < 0 || k >= sPrefN) return NULL;
    *val = sPrefV[k];
    return sPrefK[k];
}

void portArenaMenuPrefClear(void) { prefLoad(); sPrefN = 0; }

/* 1 = stored; 0 = refused (a bad key or value: nothing of it is kept) */
int portArenaMenuPrefPut(const char *key, const char *val)
{
    s32 i;

    prefLoad();
    if (!keyOk(key) || !valOk(val))
    {
        portConLogf("ge: arenamenu: refused a preference: a bad key or value (key '%.31s')\n", key != NULL ? key : "");
        return 0;
    }
    for (i = 0; i < sPrefN; i++)
    {
        if (strcmp(sPrefK[i], key) == 0) { cpy(sPrefV[i], PREF_VAL, val); return 1; }
    }
    if (sPrefN >= PREF_N) return 0;
    cpy(sPrefK[sPrefN], PREF_KEY, key);
    cpy(sPrefV[sPrefN], PREF_VAL, val);
    sPrefN++;
    return 1;
}

int portArenaMenuPrefSave(void)
{
    const char *no = getenv("GE_NOMENUSAVE");
    FILE *f;
    s32 i;

    if (no != NULL && no[0] != '\0' && no[0] != '0')
    {
        portConLogf("ge: arenamenu: preferences not written (GE_NOMENUSAVE)\n");
        return 0;
    }
#ifdef _WIN32
    _mkdir(portRootPath("Data"));
    _mkdir(portRootPath("Data/Saves"));
#else
    mkdir(portRootPath("Data"), 0755);
    mkdir(portRootPath("Data/Saves"), 0755);
#endif
    f = fopen(prefPath(), "w");
    if (f == NULL)
    {
        portConLogf("ge: arenamenu: cannot write %s\n", prefPath());
        return 0;
    }
    fprintf(f, "[arena_menu]\n; the arena start menu's last choices (Play Arena.bat). Delete this file for the defaults.\n");
    for (i = 0; i < sPrefN; i++) fprintf(f, "%s=%s\n", sPrefK[i], sPrefV[i]);
    fclose(f);
    portConLogf("ge: arenamenu: wrote %d preferences to %s\n", (int) sPrefN, prefPath());
    return 1;
}

#endif /* !TARGET_N64 */
