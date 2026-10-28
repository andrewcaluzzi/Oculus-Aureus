/**
 * port_uimenu.c - the generic mode menu layer, C side of the stage hand-off (port_uimenu.h; midas-notes features/ui-design-system.md
 * "Generic ui layer"). The same machinery as the arena's (port_arenamenu.c), with no arena in it:
 *   - THE HAND-OFF. A lobby script (a mode whose stage is the backdrop of a menu) arms what comes after its stage (ui.launch / ui.arm /
 *     ui.quit). When the stage has ended and the front end would open the stock mission report (front.c, after MENU_RUN_STAGE),
 *     portUiMenuAfterStage sets GE_MODE, the difficulty and up to UI_ENV_N GE_* variables for the next stage and front.c launches the
 *     same stage slot again at once: no dossier, no menus. The next stage's scripts read what they were launched with (ui.current).
 *     A variable set for one launch and not set by the next one goes back to what it was before the hand-off first set it.
 *   - THE LAST RUN: its result (a Lua table as text) and its launch (Retry / AGAIN) live here between the stages.
 *   - PREFERENCE FILES by name: Data/Saves/<name>.ini (portRootPath, so GE_PROFILE moves them), key=value lines. GE_UIMENU_DIR=<dir>
 *     puts them there instead, GE_NOMENUSAVE=1 never writes (tests; the values still live in memory for the session).
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

int portUiEnvKeyOk(const char *k)
{
    size_t n = 0;

    if (k == NULL || strncmp(k, "GE_", 3) != 0) return 0;
    for (; *k != '\0'; k++, n++)
    {
        if (n >= 23 || !((*k >= 'A' && *k <= 'Z') || (*k >= '0' && *k <= '9') || *k == '_')) return 0;
    }
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
    for (j = 0; j < l->nenv; j++) if (portUiEnvKeyOk(l->envk[j])) envSet(l->envk[j], l->envv[j]);
}

/* ---- the hand-off ---- */

void portUiMenuArm(const UiLaunch *next)
{
    if (portNetActive()) return;   /* netplay: the host's menus choose the next stage (port_net.c); not supported here */
    sNext = *next;
    sArmed = UI_ARM_LAUNCH;
}

void portUiMenuDisarm(void) { sArmed = UI_ARM_NONE; }

int portUiMenuLaunchNow(const UiLaunch *next)
{
    if (portNetActive() || !portModeActive()) return 0;
    portUiMenuArm(next);
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

    if (prev != 0) return prev;
    sArmed = UI_ARM_NONE;
    portUiMenuViewOff();
    if (a == UI_ARM_NONE || portNetActive()) return 0;
    if (a == UI_ARM_QUIT)
    {
        envApply(&(UiLaunch) { 0 });   /* (nothing of the last launch's variables outlives the menus) */
        portConLogf("ge: uimenu: after the stage: to the title menus\n");
        return 2;
    }
    sNext.seq = ++sSeq;
    envApply(&sNext);
    envPut("GE_MODE", sNext.mode);
    if (sNext.difficulty >= 0 && sNext.difficulty <= 3) set_selected_difficulty(sNext.difficulty);
    sCur = sNext;
    sCurEpoch = g_portCoopEpoch + 1;   /* the next stage start (portCoopStageBegin counts every one) */
    portConLogf("ge: uimenu: after the stage: launching again: mode '%s' preset '%s' lobby '%s' difficulty %d seq %d", sCur.mode,
                sCur.preset, sCur.lobby, sCur.difficulty, sCur.seq);
    for (i = 0; i < sCur.nenv; i++) portConLogf(" %s=%s", sCur.envk[i], sCur.envv[i]);
    portConLogf("\n");
    return 1;
}

const UiLaunch *portUiMenuCurrent(void) { return (sCurEpoch == g_portCoopEpoch && portModeActive()) ? &sCur : NULL; }

void portUiMenuRetrySet(const UiLaunch *l) { sRetry = *l; sRetryOk = 1; }
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
    char name[24];
    char k[PF_N][PF_KEY], v[PF_N][PF_VAL];
    s32 n;
} PrefFile;

static PrefFile *sPf[PF_FILES];

static int nameOk(const char *s)
{
    size_t n = 0;

    if (s == NULL || s[0] == '\0') return 0;
    for (; *s != '\0'; s++, n++)
    {
        if (n >= 23 || !((*s >= 'a' && *s <= 'z') || (*s >= '0' && *s <= '9') || *s == '_')) return 0;
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
    char rel[64];
    const char *d = getenv("GE_UIMENU_DIR");

    if (d != NULL && d[0] != '\0') snprintf(path, sizeof(path), "%s/%s.ini", d, name);
    else
    {
        snprintf(rel, sizeof(rel), "Data/Saves/%s.ini", name);
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
        if (keyOk(line))
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

void portUiPrefPut(const char *name, const char *key, const char *val)
{
    PrefFile *p = pfGet(name);
    s32 i;

    if (p == NULL || !keyOk(key)) return;
    for (i = 0; i < p->n; i++)
    {
        if (strcmp(p->k[i], key) == 0) { cpy(p->v[i], PF_VAL, val); return; }
    }
    if (p->n >= PF_N) return;
    cpy(p->k[p->n], PF_KEY, key);
    cpy(p->v[p->n], PF_VAL, val);
    p->n++;
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
    if (getenv("GE_UIMENU_DIR") == NULL)
    {
#ifdef _WIN32
        _mkdir(portRootPath("Data"));
        _mkdir(portRootPath("Data/Saves"));
#else
        mkdir(portRootPath("Data"), 0755);
        mkdir(portRootPath("Data/Saves"), 0755);
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
