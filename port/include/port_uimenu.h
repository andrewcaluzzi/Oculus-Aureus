#ifndef PORT_UIMENU_H
#define PORT_UIMENU_H
/* The generic mode menu layer (midas-notes features/ui-design-system.md "Generic ui layer"; fork modding/lua.md "The ui table").
 * A mode-agnostic copy of the arena's start menu / post-game machinery (port_arenamenu*.c), so any mode can have a lobby, a settings
 * page and a debrief in the same look with no new art:
 *   port_uimenu.c       SIM: the hand-off between stages (what the next stage is launched with: its mode, settings text, difficulty,
 *                       a few GE_* variables), the last run's result and launch (Retry), Escape = Back, the named preference files
 *   port_uimenu_draw.c  PRES: draws the view a script pushes (ui.view{...}) with port_hudstyle.c, animates it on stamps, the mouse
 *   port/luavm/port_luavm_ui.c   the `ui` table (and `levelgen`, the runtime choice of the generated level, port_levelgen_select.c)
 * The arena keeps its own copy (amenu) until it migrates onto this. */

#define UI_SETTINGS_LEN 4096
#define UI_RESULT_LEN   8192
#define UI_ENV_N        8

enum { UI_ARM_NONE = 0, UI_ARM_LAUNCH = 1, UI_ARM_QUIT = 2 };

typedef struct UiLaunch
{
    char mode[32];                  /* GE_MODE of that stage */
    char settings[UI_SETTINGS_LEN]; /* the mode's settings as a Lua table constructor ("" = the mode's own) */
    char preset[24];                /* the menu's preset / job id */
    char label[64];                 /* what the debrief calls the run */
    char lobby[16];                 /* the lobby's page ("menu", "postgame"; "" = a run, not the lobby) */
    char stage[16];                 /* the stage slot to load ("colosseum", "levelgen", "control"; "" = the same slot again) */
    int difficulty;                 /* the stage's difficulty 0..3 (-1 = leave the front end's) */
    int seq;                        /* set by the hand-off: counts launches this session (tests tell runs apart) */
    int nenv;
    char envk[UI_ENV_N][24];        /* GE_* variables for that stage (restored after it unless the next launch sets them again) */
    char envv[UI_ENV_N][64];
} UiLaunch;

void portUiMenuArm(const UiLaunch *next);
void portUiMenuDisarm(void);
int  portUiMenuLaunchNow(const UiLaunch *next);
int  portUiMenuQuitNow(void);
int  portUiMenuAfterStage(int prev);          /* front.c: chained after the arena's (prev = its answer) */
const char *portUiMenuTakeStage(void);        /* front.c: the stage slot the hand-off asked for ("" = none), once */
int  portUiHub(void);                         /* 1 = this session came from the main menu hub (a lobby's EXIT becomes MAIN MENU) */
void portUiHubSet(int on);
const UiLaunch *portUiMenuCurrent(void);
void portUiMenuRetrySet(const UiLaunch *l);
const UiLaunch *portUiMenuRetry(void);
void portUiMenuResultSet(const char *text);
const char *portUiMenuResult(void);
void portUiMenuEscapeBack(int on);
int  portUiMenuEscape(int p);                 /* bondview2.c */

/* what a script may hand to the next stage (SECURITY: mode scripts include third-party mods): an allowlist of GE_* keys, each with a
 * value rule (GE_LEVELGEN a level name, GE_SEED digits, GE_COOP / GE_DIAG 0 or 1, GE_COOP_P2 idle / walk / follow), and a mode name
 * of letters, digits, _ and -. portUiLaunchSanitize drops the rest (0 = the launch itself is refused: a bad mode name) */
int  portUiEnvKeyOk(const char *k);
int  portUiEnvValueOk(const char *k, const char *v);
int  portUiLaunchSanitize(UiLaunch *l);

/* named preference files: Data/Saves/ui/<name>.ini (name: a-z 0-9 _, 1..32; keys A-Z a-z 0-9 _; values printable ASCII, no control
 * characters). GE_UIMENU_DIR (read once at process start) moves the folder for tests. */
int  portUiPrefCount(const char *name);
const char *portUiPref(const char *name, int k, const char **val);
void portUiPrefClear(const char *name);
int  portUiPrefPut(const char *name, const char *key, const char *val);   /* 0 = refused */
int  portUiPrefSave(const char *name);

/* ---- the view (port_uimenu_draw.c, presentation) ---- */
enum { UIL_OFF = 0, UIL_LOBBY = 1, UIL_SETTINGS = 2, UIL_DEBRIEF = 3 };
enum { UII_HEADER = 0, UII_CARD = 1, UII_CHOICE = 2, UII_BUTTON = 3, UII_PRIMARY = 4 };
#define UIV_ITEMS  48
#define UIV_INFO   24
#define UIV_CELLS  12
#define UIV_CHECKS 12
#define UIV_CHIPS  24
#define UIV_BARS   32

/* the palette (the arena's): parchment ink, gold, dim, rules, alert red; medals none, bronze, silver, gold, platinum */
#define UI_INK    0xF4EEDCFFu
#define UI_GOLD   0xF0C040FFu
#define UI_GOLDDK 0x8A6A28FFu
#define UI_DIM    0xB8B0A0FFu
#define UI_SHADE  0x000000FFu
#define UI_RULE   0x6A5838A0u
#define UI_RED    0xE04A30FFu

typedef struct { signed char kind, col, medal, chosen, dim; char label[44], value[44], sub[100], right[48], tag[32]; unsigned int tint; } UiItem;
typedef struct { char label[32], value[44]; unsigned int rgba; signed char flag; } UiInfo;     /* an info-card line or a stat cell */
typedef struct { char label[48], value[32]; signed char state; } UiCheck;                  /* 0 not done, 1 done, 2 failed */
typedef struct { char label[28]; signed char on; } UiChip;
typedef struct { int v; signed char kind; char label[16], text[16]; } UiBar;   /* text: drawn over the bar ("" = none) */

typedef struct UiView
{
    int layout, page, cursor, n, ninfo, ncells, nchecks, nchips, nbars, medal, medalNew;
    UiItem item[UIV_ITEMS];
    UiInfo info[UIV_INFO];
    UiInfo cell[UIV_CELLS];
    UiCheck check[UIV_CHECKS];
    UiChip chip[UIV_CHIPS];
    UiBar bar[UIV_BARS];
    char title[48], kicker[100], subtitle[160], desc[200], footer[200], banner[40], news[120], infoTitle[48], infoSub[80];
    char medalName[40], chart[40], legend[80], bestChip[20], newMedal[16], checksTitle[40], chipsTitle[48];
    unsigned int bannerRgba, medalRgba;
} UiView;

void portUiMenuViewSet(const UiView *v);
int  portUiMenuViewOn(void);
void portUiMenuViewOff(void);
int  portUiMenuMouse(int mx, int my, int winw, int winh, unsigned int mbuttons, int wheelUp, int wheelDown);   /* port_intel.c */

/* ---- the generated level, chosen at stage load (port_levelgen_select.c) ---- */
const char *portLevelgenVariant(void);        /* the name of the level the levelgen slot holds now ("bank", "casino", "jungle" ...) */
int  portLevelgenHas(const char *name);       /* 1 = this build carries that level */
const char *portLevelgenDefaultName(void);    /* the default level's name: GE_LEVELGEN_LEVELS' first ("bank") */
int  portLevelgenCount(void);                 /* how many levels the build carries (GE_LEVELGEN_LEVELS) */
const char *portLevelgenName(int i);          /* the i-th one's name (0-based, the default first), NULL past the end */

#endif
