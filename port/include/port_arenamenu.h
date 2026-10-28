#ifndef PORT_ARENAMENU_H
#define PORT_ARENAMENU_H
/* port_arenamenu.c (the hand-off between the arena's lobby and its runs, the preferences file) and port_arenamenu_draw.c (the start
 * menu and the post-game, drawn with port_hudstyle.c). modding/arena.md "Start menu and post-game". */

#define AM_SETTINGS_LEN 2048
#define AM_RESULT_LEN   8192

enum { AM_ARM_NONE = 0, AM_ARM_LAUNCH = 1, AM_ARM_QUIT = 2 };

typedef struct AmLaunch
{
    char mode[32];                  /* GE_MODE of that stage: "arena", "arena-chaos", "arena-menu" (the lobby) ... */
    char settings[AM_SETTINGS_LEN]; /* ARENA_SETTINGS overrides as a Lua table constructor ("" = the mode's own) */
    char preset[24];                /* the menu's preset id ("" = none) */
    char label[64];                 /* what the post-game calls the run */
    char lobby[16];                 /* the lobby's page: "menu" or "postgame" ("" = not the lobby) */
    int coop;                       /* GE_COOP for it: 1 / 0, -1 = leave as it is */
    int slots;                      /* GE_COLOSSEUM_SLOTS for it, 0 = leave */
} AmLaunch;

void portArenaMenuLaunchSet(AmLaunch *dst, const char *mode, const char *settings, const char *preset, const char *label,
                            const char *lobby, int coop, int slots);
void portArenaMenuArm(const AmLaunch *next);
void portArenaMenuDisarm(void);
int  portArenaMenuLaunchNow(const AmLaunch *next);
int  portArenaMenuQuitNow(void);
int  portArenaMenuAfterStage(void);           /* front.c */
const AmLaunch *portArenaMenuCurrent(void);
void portArenaMenuRetrySet(const AmLaunch *l);
const AmLaunch *portArenaMenuRetry(void);
void portArenaMenuResultSet(const char *text);
const char *portArenaMenuResult(void);
void portArenaMenuEscapeBack(int on);
int  portArenaMenuEscape(int p);              /* bondview2.c */

int  portArenaMenuPrefCount(void);
const char *portArenaMenuPref(int k, const char **val);
void portArenaMenuPrefClear(void);
int  portArenaMenuPrefPut(const char *key, const char *val);   /* 0 = refused (key A-Z a-z 0-9 _, value printable ASCII) */
int  portArenaMenuPrefSave(void);

/* the view (port_arenamenu_draw.c, presentation) */
enum { AMV_OFF = 0, AMV_MAIN = 1, AMV_ADVANCED = 2, AMV_POSTGAME = 3 };
enum { AMI_HEADER = 0, AMI_PRESET = 1, AMI_CHOICE = 2, AMI_BUTTON = 3, AMI_PRIMARY = 4 };
#define AMV_ITEMS 48
#define AMV_INFO  24
#define AMV_BARS  64

typedef struct { signed char kind, col, medal, chosen, dim; char label[40], value[40], sub[96], right[40]; } AmItem;
typedef struct { char label[32], value[40]; unsigned int rgba; signed char flag; } AmInfo;
typedef struct { short v; signed char kind; } AmBar;

typedef struct AmView
{
    int screen, cursor, n, ninfo, nbars, medal, medalNew;
    AmItem item[AMV_ITEMS];
    AmInfo info[AMV_INFO];
    AmBar bar[AMV_BARS];
    char title[48], kicker[80], subtitle[120], desc[200], footer[200], banner[32], news[120], infoTitle[48], infoSub[80];
    char medalName[40], chart[40], legend[64], bestChip[20], newMedal[16];   /* the post-game's words (the theme table's, from Lua) */
    unsigned int bannerRgba;
} AmView;

void portArenaMenuViewSet(const AmView *v);   /* the Lua binding, from the lobby's tick */
int  portArenaMenuViewOn(void);
void portArenaMenuViewOff(void);
int  portArenaMenuMouse(int mx, int my, int winw, int winh, unsigned int mbuttons, int wheelUp, int wheelDown);   /* port_intel.c */

#endif
