/**
 * port_uimenu.c - the generic mode menu layer, C side of the stage hand-off (port_uimenu.h; midas-notes features/ui-design-system.md
 * "Generic ui layer"). The same machinery as the arena's (port_arenamenu.c), with no arena in it:
 *   - THE HAND-OFF. A lobby script (a mode whose stage is the backdrop of a menu) arms what comes after its stage (ui.launch / ui.arm /
 *     ui.quit). When the stage has ended and the front end would open the stock mission report (front.c, after MENU_RUN_STAGE),
 *     portUiMenuAfterStage sets GE_MODE, the difficulty and up to UI_ENV_N GE_* variables for the next stage and front.c launches the
 *     same stage slot again at once: no dossier, no menus. The next stage's scripts read what they were launched with (ui.current).
 *     A variable set for one launch and not set by the next one goes back to what it was before the hand-off first set it.
 *     SECURITY: the scripts are mods too. Only an allowlist of variables with per-key value rules is ever set (portUiLaunchSanitize),
 *     never a path, a script or a test switch; the mode name is checked like the mode loader checks it.
 *   - THE LAST RUN: its result (a Lua table as text) and its launch (Retry / AGAIN) live here between the stages.
 *   - PREFERENCE FILES by name: Data/Saves/ui/<name>.ini (portRootPath, so GE_PROFILE moves them; the layer's own folder, so a name
 *     can never reach another file), key=value lines with checked keys and printable values. GE_UIMENU_DIR=<dir> (read once at process
 *     start) puts them there instead, GE_NOMENUSAVE=1 never writes (tests; the values still live in memory for the session).
 *   - Escape = Back while the lobby holds the controls (bondview2.c asks portUiMenuEscape before it opens the watch).
 *
 * Sim class: SIM (scripts/port/snap_embed.py). The hand-off is an input of the next stage's sim (its settings); it only changes on a
 * script's call, so a rollback restores and re-sets it identically. The preference files feed a lobby's Lua state, so ui.prefs reads
 * nothing in netplay (each machine has its own files): the menu flow is solo and local co-op only (ui.netplay()).
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

#include "port_uimenu.h"
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
extern void set_selected_difficulty(s32 difficulty);   /* front.c (file.h: DIFFICULTY) */

static UiLaunch sNext;          /* what the front end starts when this stage is over */
static s32 sArmed;              /* UI_ARM_* */
static UiLaunch sCur;           /* what the current stage was launched with ... */
static s32 sCurEpoch = -1000;   /* ... valid while g_portCoopEpoch is this */
static UiLaunch sRetry;
static s32 sRetryOk;
static s32 sSeq;
static char sResult[UI_RESULT_LEN];
static s32 sEscOn, sEscEpoch = -1000;
static char sStage[16];         /* the stage slot the hand-off asked for, until front.c takes it */
static s32 sHub;                /* the main menu hub (tools/mode_hub.lua) started this session's lobbies: ui.hub() */

static void cpy(char *d, size_t n, const char *s) { snprintf(d, n, "%s", s != NULL ? s : ""); }

/* ---- the environment the hand-off sets: every key it ever set, with the value it had before ---- */

#define ENV_SEEN 24
static char sEnvK[ENV_SEEN][24];
static char *sEnvOrig[ENV_SEEN];   /* NULL = it was not set */
static s32 sEnvN;

static void envPut(const char *k, const char *v)
{
    char *s = (char *) malloc(strlen(k) + strlen(v) + 2);   /* putenv keeps the pointer: never freed (as port_net.c netSetenv) */

    if (s == NULL) return;
    sprintf(s, "%s=%s", k, v);
    putenv(s);
}

/* SECURITY: mode scripts (third-party mods included) choose these, and the next stage's process environment takes them. Only an
 * explicit ALLOWLIST of keys, each with its own value rule, is ever set: never a path, a script, a data directory or a test switch
 * (GE_UIMENU_DIR, GE_LUA_TEST, GE_ROM*, GE_MODS, GE_PROFILE, GE_NOMENUSAVE ... are all refused). */
enum { EV_NAME, EV_DIGITS, EV_BOOL, EV_PUPPET, EV_LIST };
static const struct { const char *key; s32 rule; } kEnvAllow[] = {
    { "GE_LEVELGEN", EV_NAME },     /* which generated level fills the levelgen slot (port_levelgen_select.c): a level name */
    { "GE_DAMPLUS", EV_LIST },      /* Dam+ on stock Dam (port_damplus.c, read at each stage load): 1, or a comma list of its parts */
    { "GE_SEED", EV_DIGITS },       /* the run's deal (the solo Lua RNG's seed) */
    { "GE_COOP", EV_BOOL },         /* local co-op: a second Bond */
    { "GE_COOP_P2", EV_PUPPET },    /* what drives him: idle / walk / follow (never replay:<file>) */
    { "GE_DIAG", EV_BOOL },         /* the F3 diagnostics up at the start */
};

static int envRule(const char *k)
{
    size_t i;

    if (k == NULL) return -1;
    for (i = 0; i < sizeof(kEnvAllow) / sizeof(kEnvAllow[0]); i++) if (strcmp(k, kEnvAllow[i].key) == 0) return kEnvAllow[i].rule;
    return -1;
}

int portUiEnvKeyOk(const char *k) { return envRule(k) >= 0; }

int portUiEnvValueOk(const char *k, const char *v)
{
    const s32 rule = envRule(k);
    size_t n = 0;
    const char *p;

    if (rule < 0 || v == NULL) return 0;
    switch (rule)
    {
    case EV_BOOL: return strcmp(v, "0") == 0 || strcmp(v, "1") == 0 || v[0] == '\0';
    case EV_PUPPET: return strcmp(v, "idle") == 0 || strcmp(v, "walk") == 0 || strcmp(v, "follow") == 0;
    case EV_DIGITS:
        for (p = v; *p != '\0'; p++, n++) if (n >= 9 || *p < '0' || *p > '9') return 0;
        return 1;
    case EV_LIST:   /* a-z 0-9 _ and commas, 0..23 ("" = unset) */
        for (p = v; *p != '\0'; p++, n++) if (n >= 23 || !((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_' || *p == ',')) return 0;
        return 1;
    default:   /* EV_NAME: a-z 0-9 _, 1..23 */
        for (p = v; *p != '\0'; p++, n++) if (n >= 23 || !((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_')) return 0;
        return n > 0;
    }
}

/* a mode name the hand-off may put in GE_MODE: what port_luavm.c portLuaModeScriptPath accepts (letters, digits, _ and -), 1..31 */
static int modeOk(const char *m)
{
    size_t n = 0;

    if (m == NULL) return 0;
    for (; *m != '\0'; m++, n++)
    {
        if (n >= 31 || !((*m >= 'a' && *m <= 'z') || (*m >= 'A' && *m <= 'Z') || (*m >= '0' && *m <= '9') || *m == '_' || *m == '-')) return 0;
    }
    return n > 0;
}

/* the launch as a script gave it, made safe: a bad mode refuses the whole launch, a variable off the allowlist (or with a bad value)
 * is dropped. 0 = refused */
int portUiLaunchSanitize(UiLaunch *l)
{
    s32 i, j = 0;

    if (!modeOk(l->mode))
    {
        portConLogf("ge: uimenu: launch refused: mode name '%.40s' is not a mode\n", l->mode);
        return 0;
    }
    l->stage[sizeof(l->stage) - 1] = '\0';
    for (i = 0; l->stage[i] != '\0'; i++)
    {   /* a stage name from front.c's direct-launch table (resolved there; an unknown one is ignored): a-z 0-9 only */
        if (!((l->stage[i] >= 'a' && l->stage[i] <= 'z') || (l->stage[i] >= '0' && l->stage[i] <= '9')))
        {
            portConLogf("ge: uimenu: launch refused: stage name '%.16s' is not a stage\n", l->stage);
            return 0;
        }
    }
    if (l->nenv < 0 || l->nenv > UI_ENV_N) l->nenv = 0;
    for (i = 0; i < l->nenv; i++)
    {
        l->envk[i][sizeof(l->envk[i]) - 1] = '\0';
        l->envv[i][sizeof(l->envv[i]) - 1] = '\0';
        if (!portUiEnvValueOk(l->envk[i], l->envv[i]))
        {
            portConLogf("ge: uimenu: refused the variable %.23s for the next stage (not allowed, or a bad value)\n", l->envk[i]);
            continue;
        }
        if (j != i)
        {
            memcpy(l->envk[j], l->envk[i], sizeof(l->envk[0]));
            memcpy(l->envv[j], l->envv[i], sizeof(l->envv[0]));
        }
        j++;
    }
    l->nenv = j;
    if (l->difficulty < -1 || l->difficulty > 3) l->difficulty = -1;
    return 1;
}

static void envSet(const char *k, const char *v)
{
    s32 i;

    for (i = 0; i < sEnvN; i++) if (strcmp(sEnvK[i], k) == 0) break;
    if (i == sEnvN && sEnvN < ENV_SEEN)
    {
        const char *o = getenv(k);

        cpy(sEnvK[i], sizeof(sEnvK[i]), k);
        sEnvOrig[i] = NULL;
        if (o != NULL)
        {
            sEnvOrig[i] = (char *) malloc(strlen(o) + 1);
            if (sEnvOrig[i] != NULL) strcpy(sEnvOrig[i], o);
        }
        sEnvN++;
    }
    envPut(k, v);
}

static void envApply(const UiLaunch *l)
{
    s32 i, j;

    for (i = 0; i < sEnvN; i++)
    {   /* set before and not now: back to what it was */
        int again = 0;

        for (j = 0; j < l->nenv; j++) if (strcmp(l->envk[j], sEnvK[i]) == 0) again = 1;
        if (!again) envPut(sEnvK[i], sEnvOrig[i] != NULL ? sEnvOrig[i] : "");
    }
    for (j = 0; j < l->nenv; j++) if (portUiEnvValueOk(l->envk[j], l->envv[j])) envSet(l->envk[j], l->envv[j]);   /* (checked again) */
}

/* ---- the hand-off ---- */

void portUiMenuArm(const UiLaunch *next)
{
    if (portNetActive()) return;   /* netplay: the host's menus choose the next stage (port_net.c); not supported here */
    sNext = *next;
    if (!portUiLaunchSanitize(&sNext))
    {
        sArmed = UI_ARM_NONE;
        return;
    }
    sArmed = UI_ARM_LAUNCH;
}

void portUiMenuDisarm(void) { sArmed = UI_ARM_NONE; }

int portUiMenuLaunchNow(const UiLaunch *next)
{
    if (portNetActive() || !portModeActive()) return 0;
    portUiMenuArm(next);
    if (sArmed != UI_ARM_LAUNCH) return 0;   /* (refused: a bad mode name) */
    portBenchSweep(0, 0.0f, 0.0f);   /* the lobby's camera turn must not carry into the run */
    portUiMenuViewOff();             /* ... nor the menu's hold on the mouse */
    portUiMenuEscapeBack(0);
    if (!portModeEndMission(0))
    {
        sArmed = UI_ARM_NONE;
        portConLogf("ge: uimenu: launch refused (the stage would not end)\n");
        return 0;
    }
    portConLogf("ge: uimenu: launch armed: mode '%s' preset '%s' lobby '%s' difficulty %d env %d (%d bytes of settings)\n", sNext.mode,
                sNext.preset, sNext.lobby, sNext.difficulty, sNext.nenv, (int) strlen(sNext.settings));
    return 1;
}

int portUiMenuQuitNow(void)
{
    if (!portModeActive()) return 0;
    portBenchSweep(0, 0.0f, 0.0f);
    portUiMenuViewOff();
    portUiMenuEscapeBack(0);
    sArmed = UI_ARM_QUIT;
    if (!portModeEndMission(0))
    {
        sArmed = UI_ARM_NONE;
        return 0;
    }
    portConLogf("ge: uimenu: quit to the title armed\n");
    return 1;
}

/* front.c, a stage is over and the front end would show the mission report. prev = the arena's answer (port_arenamenu.c, asked first):
 * when it took the screen this does nothing. 0 = the report (stock), 1 = launch the stage slot again now, 2 = the Files screen */
int portUiMenuAfterStage(int prev)
{
    const s32 a = sArmed;
    s32 i;

    if (prev != 0)
    {   /* the arena's hand-off took it: its stage slot (the hub hosts the arena's lobby on Control and starts its runs on the Colosseum) */
        if (prev == 1) cpy(sStage, sizeof(sStage), portArenaMenuTakeStage());
        return prev;
    }
    sArmed = UI_ARM_NONE;
    portUiMenuViewOff();
    if (a == UI_ARM_NONE && strcmp(sCur.mode, "0") == 0)
    {   /* a STOCK mission the menus launched (mode "0" = GE_MODE off: the hub's DAM+, tools/mode_damplus_menu.lua) is over: no script
         * ran to arm anything, so the stock report and the Files screen follow, as from GOLDENEYE 007. Its variables (GE_DAMPLUS) go
         * back to what they were, so a mission picked from the Files screen next is the stock one. */
        envApply(&(UiLaunch) { 0 });
        envPut("GE_MODE", "");
        sCur.mode[0] = '\0';
        sHub = 0;
        portConLogf("ge: uimenu: after the stage: a stock mission from the menus is over: its variables are cleared (the stock report next)\n");
        return 0;
    }
    if (a == UI_ARM_NONE || portNetActive()) return 0;
    if (a == UI_ARM_QUIT)
    {
        envApply(&(UiLaunch) { 0 });   /* (nothing of the last launch's variables outlives the menus) */
        envPut("GE_MODE", "");         /* (the missions picked from the Files screen are the stock ones, not the lobby's mode) */
        sHub = 0;
        portConLogf("ge: uimenu: after the stage: to the title menus\n");
        return 2;
    }
    sNext.seq = ++sSeq;
    envApply(&sNext);
    envPut("GE_MODE", sNext.mode);
    cpy(sStage, sizeof(sStage), sNext.stage);
    if (sNext.difficulty >= 0 && sNext.difficulty <= 3) set_selected_difficulty(sNext.difficulty);
    sCur = sNext;
    sCurEpoch = g_portCoopEpoch + 1;   /* the next stage start (portCoopStageBegin counts every one) */
    portConLogf("ge: uimenu: after the stage: launching again: mode '%s' preset '%s' lobby '%s' difficulty %d seq %d", sCur.mode,
                sCur.preset, sCur.lobby, sCur.difficulty, sCur.seq);
    for (i = 0; i < sCur.nenv; i++) portConLogf(" %s=%s", sCur.envk[i], sCur.envv[i]);
    portConLogf("\n");
    return 1;
}

const char *portUiMenuTakeStage(void)
{
    static char t[sizeof(sStage)];

    cpy(t, sizeof(t), sStage);
    sStage[0] = '\0';
    return t;
}

void portUiHubSet(int on) { sHub = on ? 1 : 0; }
int portUiHub(void) { return sHub && !portNetActive(); }

const UiLaunch *portUiMenuCurrent(void) { return (sCurEpoch == g_portCoopEpoch && portModeActive()) ? &sCur : NULL; }

void portUiMenuRetrySet(const UiLaunch *l)
{
    UiLaunch t = *l;

    if (portUiLaunchSanitize(&t)) { sRetry = t; sRetryOk = 1; }
}
const UiLaunch *portUiMenuRetry(void) { return sRetryOk ? &sRetry : NULL; }

void portUiMenuResultSet(const char *text) { cpy(sResult, sizeof(sResult), text); }
const char *portUiMenuResult(void) { return sResult; }

/* ---- Escape = Back in a lobby ---- */

void portUiMenuEscapeBack(int on) { sEscOn = on ? 1 : 0; sEscEpoch = g_portCoopEpoch; }

int portUiMenuEscape(int p)
{
    if (!sEscOn || sEscEpoch != g_portCoopEpoch || !portModeActive() || !portPlanningHeld()) return 0;
    portPlanningTestInput(p, 32u /* PE_BACK */);
    return 1;
}

/* ---- the preference files ---- */

#define PF_FILES 6
#define PF_N     128
#define PF_KEY   40
#define PF_VAL   80

typedef struct
{
    char name[40];
    char k[PF_N][PF_KEY], v[PF_N][PF_VAL];
    s32 n;
} PrefFile;

static PrefFile *sPf[PF_FILES];

/* SECURITY: GE_UIMENU_DIR (tests) is read ONCE, when the process starts, before any script can run; it is never read again (and the
 * hand-off cannot set it: it is not on the allowlist above) */
static char sPfDir[1024];
static int sPfDirSet;

#if defined(__GNUC__)
__attribute__((constructor))
#endif
static void pfDirAtStartup(void)
{
    const char *d = getenv("GE_UIMENU_DIR");

    if (d != NULL && d[0] != '\0' && strlen(d) < sizeof(sPfDir))
    {
        cpy(sPfDir, sizeof(sPfDir), d);
        sPfDirSet = 1;
    }
}

/* a file name: a-z 0-9 _, 1..32 (no separators, no dots: it can only name a file in the layer's own folder) */
static int nameOk(const char *s)
{
    size_t n = 0;

    if (s == NULL || s[0] == '\0') return 0;
    for (; *s != '\0'; s++, n++)
    {
        if (n >= 32 || !((*s >= 'a' && *s <= 'z') || (*s >= '0' && *s <= '9') || *s == '_')) return 0;
    }
    return 1;
}

/* a value: printable ASCII only (no CR, LF or any control character: one value can never become another line), shorter than PF_VAL */
static int valOk(const char *v)
{
    size_t n = 0;

    if (v == NULL) return 0;
    for (; *v != '\0'; v++, n++)
    {
        if (n >= PF_VAL - 1 || (unsigned char) *v < 0x20 || (unsigned char) *v > 0x7E) return 0;
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

        if (n >= PF_KEY - 1 || !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) return 0;
    }
    return 1;
}

static const char *pfPath(const char *name)
{
    static char path[1200];
    char rel[80];

    if (sPfDirSet) snprintf(path, sizeof(path), "%s/%s.ini", sPfDir, name);
    else
    {   /* the layer's own folder: it can never name arena_menu.ini, arena_scores.ini, midas.ini or anything outside Saves/ui */
        snprintf(rel, sizeof(rel), "Data/Saves/ui/%s.ini", name);
        cpy(path, sizeof(path), portRootPath(rel));
    }
    return path;
}

static PrefFile *pfGet(const char *name)
{
    char line[200];
    FILE *f;
    PrefFile *p;
    s32 i;

    if (!nameOk(name)) return NULL;
    for (i = 0; i < PF_FILES && sPf[i] != NULL; i++) if (strcmp(sPf[i]->name, name) == 0) return sPf[i];
    if (i >= PF_FILES) return NULL;
    p = (PrefFile *) calloc(1, sizeof(PrefFile));
    if (p == NULL) return NULL;
    cpy(p->name, sizeof(p->name), name);
    sPf[i] = p;
    f = fopen(pfPath(name), "r");
    if (f == NULL)
    {
        portConLogf("ge: uimenu: no %s preferences yet (%s)\n", name, pfPath(name));
        return p;
    }
    while (fgets(line, sizeof(line), f) != NULL && p->n < PF_N)
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
            cpy(p->k[p->n], PF_KEY, line);
            cpy(p->v[p->n], PF_VAL, eq + 1);
            p->n++;
        }
    }
    fclose(f);
    portConLogf("ge: uimenu: read %d %s preferences from %s\n", (int) p->n, name, pfPath(name));
    return p;
}

int portUiPrefCount(const char *name) { PrefFile *p = pfGet(name); return p != NULL ? p->n : 0; }

const char *portUiPref(const char *name, int k, const char **val)
{
    PrefFile *p = pfGet(name);

    if (p == NULL || k < 0 || k >= p->n) return NULL;
    *val = p->v[k];
    return p->k[k];
}

void portUiPrefClear(const char *name) { PrefFile *p = pfGet(name); if (p != NULL) p->n = 0; }

/* 1 = stored; 0 = refused (a bad file name, key or value: nothing of it is kept) */
int portUiPrefPut(const char *name, const char *key, const char *val)
{
    PrefFile *p = pfGet(name);
    s32 i;

    if (p == NULL || !keyOk(key) || !valOk(val))
    {
        portConLogf("ge: uimenu: refused a preference (file '%.40s'): a bad name, key or value\n", name != NULL ? name : "");
        return 0;
    }
    for (i = 0; i < p->n; i++)
    {
        if (strcmp(p->k[i], key) == 0) { cpy(p->v[i], PF_VAL, val); return 1; }
    }
    if (p->n >= PF_N) return 0;
    cpy(p->k[p->n], PF_KEY, key);
    cpy(p->v[p->n], PF_VAL, val);
    p->n++;
    return 1;
}

int portUiPrefSave(const char *name)
{
    const char *no = getenv("GE_NOMENUSAVE");
    PrefFile *p = pfGet(name);
    FILE *f;
    s32 i;

    if (p == NULL) return 0;
    if (no != NULL && no[0] != '\0' && no[0] != '0')
    {
        portConLogf("ge: uimenu: %s preferences kept in memory only (GE_NOMENUSAVE)\n", name);
        return 0;
    }
    if (!sPfDirSet)
    {
#ifdef _WIN32
        _mkdir(portRootPath("Data"));
        _mkdir(portRootPath("Data/Saves"));
        _mkdir(portRootPath("Data/Saves/ui"));
#else
        mkdir(portRootPath("Data"), 0755);
        mkdir(portRootPath("Data/Saves"), 0755);
        mkdir(portRootPath("Data/Saves/ui"), 0755);
#endif
    }
    f = fopen(pfPath(name), "w");
    if (f == NULL)
    {
        portConLogf("ge: uimenu: cannot write %s\n", pfPath(name));
        return 0;
    }
    fprintf(f, "[%s]\n; a mode menu's saved choices (the generic ui layer). Delete this file for the defaults.\n", name);
    for (i = 0; i < p->n; i++) fprintf(f, "%s=%s\n", p->k[i], p->v[i]);
    fclose(f);
    portConLogf("ge: uimenu: wrote %d %s preferences to %s\n", (int) p->n, name, pfPath(name));
    return 1;
}

#endif /* !TARGET_N64 */
