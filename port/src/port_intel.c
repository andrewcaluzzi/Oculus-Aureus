/**
 * port_intel.c - the heist INTEL board (designs/bank-heist.md section 19): a crew planning board Bond pulls out mid-heist.
 *
 * The board is PRESENTATION ONLY. Nothing here is sim state, nothing is hashed or snapshotted, and the game never pauses:
 *   - the CONTENT is a list of 2D primitives (rects, frames, lines, triangles, text, markers) that a mode script builds through the
 *     intel.* Lua binding (port/luavm/port_luavm_intel.c; the heist's tools/heist_intel.lua). The script builds it on its own clock
 *     whether or not anyone looks, so opening the board changes nothing the sim runs.
 *   - the VIEW (open, which page, which floor) is this machine's alone. It is driven by the PORTACT_INTEL action, which joy_port.c
 *     takes off the poll before the sim, the input recorder or the netplay window ever see it (portIntelInput).
 * Pages are drawn on a 640x480 board, centred in the console's 2D space (port_console.c portConRender calls portIntelRender). The
 * board draws into its own display lists (two, alternating; portAllocHigh) so a text-heavy page never eats the frame's master list.
 *
 * Env (tests): GE_INTEL_AUTO="<frame>:open|close|page<n>|floor<n>[,...]" drives the view at render frames (the determinism proof);
 * GE_FRAMES (any automated run) skips the start-of-heist briefing. midas.ini [input] intel_hold=1 makes the board show while held.
 */
#ifndef TARGET_N64

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ultra64.h>

#include "game/textrelated.h"
#include "game/dyn.h"
#include "fr.h"

extern void portConLogf(const char *fmt, ...);
extern void *portAllocHigh(u32 size);
extern int portModeActive(void);
extern int portLocalPlayer(void);
extern int portLuaGameBondCount(void);
extern int portLuaGameBondPos(int player, float *x, float *y, float *z);
extern int portLuaGameBondDead(int player);
extern int g_portCoopEpoch;
extern void portTextScale(f32 scale);
extern void portConsoleSpace(s32 *w, s32 *h);

#define IN_PAGES   8
#define IN_FLOORS  10
#define IN_ITEMS   6000          /* per pool */
#define IN_TEXT    (96 * 1024)  /* per pool */
#define IN_BONDS   4
#define IN_DL_GFX  (72 * 1024)  /* Gfx per display list */
#define IN_DL_SAFE 600           /* stop adding items with this much room left */
#define BW 640
#define BH 480

enum { K_RECT, K_FRAME, K_LINE, K_TRI, K_TEXT, K_MARKER };
enum { M_DOT, M_RING, M_X, M_PIN, M_CHECK, M_BOX, M_TRI, M_DIAMOND, M_CAM, M_STAIRS, M_DOOR, M_STAR };

typedef struct {
    u8 kind, page, floor, shape;
    s16 a, b, c, d, e, f;        /* coordinates (board units); K_TEXT: e = wrap width, f = scale x100; K_MARKER: c = size */
    u32 rgba;
    s32 text;                    /* offset in the pool's text, -1 none */
    u8 font;
} IntelItem;

typedef struct {
    IntelItem item[IN_ITEMS];
    s32 n;
    char text[IN_TEXT];
    s32 tn;
} IntelPool;

static IntelPool *sPool[3];      /* 0 static (the map, the fixed notes), 1 live (rebuilt by the script on its clock), 2 the plan (below) */
static s32 sEnabled;
static s32 sEpoch = -1;
static char sTitle[96];
static char sPageTitle[IN_PAGES][32];
static s32 sPages;
static s32 sMapPage = -1;
static char sFloorName[IN_FLOORS][16];
static f32 sFloorX[IN_FLOORS][5];   /* wx1, wz1, scale, bx0, by0: board = (bx0 + (wx1 - x) * s, by0 + (wz1 - z) * s) */
static s32 sFloors;
static s32 sBondFloor[IN_BONDS];
static s32 sAtPage, sAtFloor, sAtLive;

/* the view: this machine's */
static s32 sOpen, sPage, sFloor = -1, sBrief, sHoldMode, sFrame;
/* PLAN MODE (the heist's planning phase, section 27; tools/heist_planning.lua through plan.view): the board is up on every machine, its
 * page and floor follow the plan's sim state (the crew's buttons, port_planning.c), the INTEL key and the page keys are left to the sim,
 * and the footer is the script's. Pool 2 holds the plan's pages (plan.clear / plan.at): intel.clear never touches it. */
static s32 sPlanOn;
static char sPlanFoot[192];
static u8 sPrevIntel, sPrevNav[4];
static s32 sAutoN = -1;
static s32 sAutoFrame[32];
static char sAutoWhat[32][12];

/* the display lists */
static Gfx *sDL[2];
static s32 sDLi;
static Gfx *sDLEnd;

/* ---- content (the Lua binding's side) ---------------------------------------------------------------------------------------- */

static void intelResetAll(void)
{
    s32 i;

    if (sPool[0]) { sPool[0]->n = 0; sPool[0]->tn = 0; }
    if (sPool[1]) { sPool[1]->n = 0; sPool[1]->tn = 0; }
    sTitle[0] = '\0';
    sPages = 0;
    sMapPage = -1;
    sFloors = 0;
    for (i = 0; i < IN_BONDS; i++) sBondFloor[i] = -1;
    sAtPage = 0; sAtFloor = -1; sAtLive = 0;
}

static void intelEpochCheck(void)
{
    if (sEpoch != g_portCoopEpoch)
    {   /* a new stage: forget the last stage's board and close the view */
        sEpoch = g_portCoopEpoch;
        sEnabled = 0;
        sOpen = 0;
        sBrief = 0;
        sPage = 0;
        sFloor = -1;
        sPlanOn = 0;
        intelResetAll();
        if (sPool[2]) { sPool[2]->n = 0; sPool[2]->tn = 0; }
    }
}

static int intelPools(void)
{
    if (sPool[0] == NULL)
    {
        sPool[0] = (IntelPool *) calloc(1, sizeof(IntelPool));
        sPool[1] = (IntelPool *) calloc(1, sizeof(IntelPool));
        sPool[2] = (IntelPool *) calloc(1, sizeof(IntelPool));
    }
    return sPool[0] != NULL && sPool[1] != NULL && sPool[2] != NULL;
}

void portIntelEnable(int on)  { intelEpochCheck(); sEnabled = on ? 1 : 0; }
int portIntelAvailable(void)  { intelEpochCheck(); return sEnabled && portModeActive(); }

void portIntelClear(int liveOnly)
{
    intelEpochCheck();
    if (liveOnly)
    {
        if (sPool[1]) { sPool[1]->n = 0; sPool[1]->tn = 0; }
    }
    else
    {
        intelResetAll();
    }
}

void portIntelTitle(const char *t)
{
    intelEpochCheck();
    snprintf(sTitle, sizeof(sTitle), "%s", t ? t : "");
}

void portIntelPage(int i, const char *title)
{
    intelEpochCheck();
    if (i < 1 || i > IN_PAGES) return;
    snprintf(sPageTitle[i - 1], sizeof(sPageTitle[0]), "%s", title ? title : "");
    if (i > sPages) sPages = i;
}

void portIntelMapPage(int i) { intelEpochCheck(); sMapPage = (i >= 1 && i <= IN_PAGES) ? i - 1 : -1; }

void portIntelFloor(int k, const char *name, float wx1, float wz1, float s, float bx0, float by0)
{
    intelEpochCheck();
    if (k < 1 || k > IN_FLOORS) return;
    snprintf(sFloorName[k - 1], sizeof(sFloorName[0]), "%s", name ? name : "");
    sFloorX[k - 1][0] = wx1; sFloorX[k - 1][1] = wz1; sFloorX[k - 1][2] = s; sFloorX[k - 1][3] = bx0; sFloorX[k - 1][4] = by0;
    if (k > sFloors) sFloors = k;
}

void portIntelBondFloor(int p, int k) { intelEpochCheck(); if (p >= 0 && p < IN_BONDS) sBondFloor[p] = k - 1; }

void portIntelAt(int page, int floor, int live)
{
    intelEpochCheck();
    sAtPage = page - 1;
    sAtFloor = floor - 1;   /* -1: every floor (a page item) */
    sAtLive = live == 2 ? 2 : (live ? 1 : 0);
}

/* plan.clear(): drop the plan's pages (pool 2) */
void portIntelPlanClear(void)
{
    intelEpochCheck();
    if (sPool[2]) { sPool[2]->n = 0; sPool[2]->tn = 0; }
}

/* plan.view(on, page, floor, footer): plan mode on (the board up, at that page and floor) or off (the board put away) */
void portIntelPlanView(int on, int page, int floor, const char *footer)
{
    intelEpochCheck();
    if (on)
    {
        sPlanOn = 1;
        sOpen = 1;
        sBrief = 0;
        if (page >= 1 && page <= IN_PAGES) sPage = page - 1;
        if (floor >= 1 && floor <= IN_FLOORS) sFloor = floor - 1;
        snprintf(sPlanFoot, sizeof(sPlanFoot), "%s", footer ? footer : "");
    }
    else if (sPlanOn)
    {
        sPlanOn = 0;
        sOpen = 0;
        portConLogf("ge: intel: plan mode off (the board is put away)\n");
    }
}

int portIntelPlanOn(void) { return sPlanOn; }

static IntelItem *intelNew(int kind, const char *text)
{
    IntelPool *P;
    IntelItem *it;

    if (!intelPools()) return NULL;
    P = sPool[sAtLive];
    if (P->n >= IN_ITEMS || sAtPage < 0 || sAtPage >= IN_PAGES) return NULL;
    it = &P->item[P->n];
    memset(it, 0, sizeof(*it));
    it->text = -1;
    if (text != NULL)
    {
        const s32 len = (s32) strlen(text);

        if (P->tn + len + 1 > IN_TEXT) return NULL;
        memcpy(P->text + P->tn, text, (size_t) len + 1);
        it->text = P->tn;
        P->tn += len + 1;
    }
    P->n++;
    it->kind = (u8) kind;
    it->page = (u8) sAtPage;
    it->floor = (u8) (sAtFloor < 0 ? 255 : sAtFloor);
    return it;
}

static s16 c16(float v) { return (s16) (v < -30000.0f ? -30000 : (v > 30000.0f ? 30000 : (s32) (v >= 0 ? v + 0.5f : v - 0.5f))); }

void portIntelRect(float x0, float y0, float x1, float y1, unsigned int rgba, int fill, int thick)
{
    IntelItem *it = intelNew(fill ? K_RECT : K_FRAME, NULL);

    if (!it) return;
    it->a = c16(x0); it->b = c16(y0); it->c = c16(x1); it->d = c16(y1); it->e = (s16) (thick < 1 ? 1 : thick); it->rgba = rgba;
}

void portIntelLine(float x0, float y0, float x1, float y1, unsigned int rgba, int thick)
{
    IntelItem *it = intelNew(K_LINE, NULL);

    if (!it) return;
    it->a = c16(x0); it->b = c16(y0); it->c = c16(x1); it->d = c16(y1); it->e = (s16) (thick < 1 ? 1 : thick); it->rgba = rgba;
}

void portIntelTri(float x0, float y0, float x1, float y1, float x2, float y2, unsigned int rgba)
{
    IntelItem *it = intelNew(K_TRI, NULL);

    if (!it) return;
    it->a = c16(x0); it->b = c16(y0); it->c = c16(x1); it->d = c16(y1); it->e = c16(x2); it->f = c16(y2); it->rgba = rgba;
}

void portIntelText(float x, float y, const char *text, unsigned int rgba, float scale, int wrap, int font)
{
    IntelItem *it = intelNew(K_TEXT, text ? text : "");

    if (!it) return;
    it->a = c16(x); it->b = c16(y); it->e = (s16) wrap; it->f = (s16) (scale <= 0.0f ? 100 : (s32) (scale * 100.0f)); it->rgba = rgba;
    it->font = (u8) (font ? 1 : 0);
}

static int intelShape(const char *k)
{
    static const char *const names[] = { "dot", "ring", "x", "pin", "check", "box", "tri", "diamond", "cam", "stairs", "door", "star" };
    int i;

    for (i = 0; i < (int) (sizeof(names) / sizeof(names[0])); i++)
    {
        if (k && strcmp(k, names[i]) == 0) return i;
    }
    return M_DOT;
}

void portIntelMarker(float x, float y, const char *kind, unsigned int rgba, int size, const char *label)
{
    IntelItem *it = intelNew(K_MARKER, (label && label[0]) ? label : NULL);

    if (!it) return;
    it->a = c16(x); it->b = c16(y); it->c = (s16) (size < 2 ? 2 : size); it->shape = (u8) intelShape(kind); it->rgba = rgba;
}

int portIntelItemCount(int live) { return (sPool[0] && sPool[1] && sPool[2]) ? sPool[live == 2 ? 2 : (live ? 1 : 0)]->n : 0; }

/* ---- the view (this machine) --------------------------------------------------------------------------------------------------- */

static int intelBriefAllowed(void)
{
    return getenv("GE_FRAMES") == NULL && getenv("GE_INTEL_NOBRIEF") == NULL;
}

/** intel.briefing(): show the board once at the start, "press INTEL to close". Skipped in automated runs. */
void portIntelBriefing(void)
{
    intelEpochCheck();
    if (!intelBriefAllowed()) return;
    sOpen = 1;
    sBrief = 1;
    sPage = 0;
}

int portIntelIsOpen(void) { return sOpen && portIntelAvailable(); }
int portIntelViewPage(void) { return sPage + 1; }
int portIntelViewFloor(void) { return sFloor + 1; }

/** Tests and screenshots: set the view directly (page / floor 1-based, 0 = leave). */
void portIntelTestShow(int open, int page, int floor)
{
    sOpen = open ? 1 : 0;
    sBrief = 0;
    if (page >= 1 && page <= IN_PAGES) sPage = page - 1;
    if (floor >= 1 && floor <= IN_FLOORS) sFloor = floor - 1;
}

/** [input] intel_hold: 1 = the board shows while INTEL is held, 0 = a press opens it and the next closes it (the default). */
void portIntelSetHold(int on) { sHoldMode = on ? 1 : 0; }
int portIntelHold(void) { return sHoldMode; }

static void intelStepPage(int d)
{
    const int n = sPages > 0 ? sPages : 1;

    sPage = ((sPage + d) % n + n) % n;
}

static void intelStepFloor(int d)
{
    if (sFloors <= 0) return;
    if (sFloor < 0) sFloor = 0;
    sFloor += d;
    if (sFloor < 0) sFloor = 0;
    if (sFloor >= sFloors) sFloor = sFloors - 1;
}

/**
 * joy_port.c, every pad poll, before the sim / recorder / netplay window see the poll: `intel` = the INTEL action is down on some pad,
 * nav = page left, page right, floor up, floor down (watch_prev / prev_weapon, watch_next / next_weapon, watch_up / next_inventory,
 * watch_down / prev_inventory). Returns 1 when the board is open, so the caller swallows the nav presses (the wheel and the bumpers
 * flip pages, not guns; the gadget keys change floors).
 */
int portIntelInput(int intel, int prev, int next, int up, int down)
{
    const u8 nav[4] = { (u8) (prev != 0), (u8) (next != 0), (u8) (up != 0), (u8) (down != 0) };
    int i, open;

    if (!portIntelAvailable() || sPlanOn)
    {   /* (plan mode: the sim takes the page keys as the plan's tabs; the INTEL key does nothing until the plan is confirmed) */
        sPrevIntel = (u8) (intel != 0);
        memcpy(sPrevNav, nav, sizeof(nav));
        return 0;
    }
    if (sHoldMode)
    {
        if (intel && !sPrevIntel) sBrief = 0;
        sOpen = intel || sBrief;
        if (intel && sBrief) sBrief = 0;
    }
    else if (intel && !sPrevIntel)
    {
        sOpen = !sOpen;
        sBrief = 0;
        portConLogf("ge: intel: board %s (the intel key)\n", sOpen ? "opened" : "closed");
    }
    sPrevIntel = (u8) (intel != 0);
    open = sOpen;
    if (open)
    {
        for (i = 0; i < 4; i++)
        {
            if (nav[i] && !sPrevNav[i])
            {
                if (i == 0) intelStepPage(-1);
                else if (i == 1) intelStepPage(1);
                else if (i == 2) intelStepFloor(1);    /* up a floor */
                else intelStepFloor(-1);
            }
        }
    }
    memcpy(sPrevNav, nav, sizeof(nav));
    return open;
}

static void intelAutoParse(void)
{
    const char *e = getenv("GE_INTEL_AUTO");

    sAutoN = 0;
    while (e && *e && sAutoN < 32)
    {
        int n = 0;

        sAutoFrame[sAutoN] = atoi(e);
        while (*e && *e != ':' && *e != ',') e++;
        if (*e == ':')
        {
            e++;
            while (*e && *e != ',' && n < 11) sAutoWhat[sAutoN][n++] = *e++;
        }
        sAutoWhat[sAutoN][n] = '\0';
        sAutoN++;
        while (*e && *e != ',') e++;
        if (*e == ',') e++;
    }
}

static void intelAutoTick(void)
{
    int i;

    if (sAutoN < 0) intelAutoParse();
    for (i = 0; i < sAutoN; i++)
    {
        if (sAutoFrame[i] != sFrame) continue;
        if (strcmp(sAutoWhat[i], "open") == 0) sOpen = 1;
        else if (strcmp(sAutoWhat[i], "close") == 0) sOpen = 0;
        else if (strncmp(sAutoWhat[i], "page", 4) == 0) sPage = atoi(sAutoWhat[i] + 4) - 1;
        else if (strncmp(sAutoWhat[i], "floor", 5) == 0) sFloor = atoi(sAutoWhat[i] + 5) - 1;
        portConLogf("ge: intel: auto %s at frame %d (open %d page %d floor %d)\n", sAutoWhat[i], sFrame, sOpen, sPage + 1, sFloor + 1);
    }
}

/* ---- drawing ------------------------------------------------------------------------------------------------------------------- */

static s32 sW = 640, sH = 480;      /* the console space */
static f32 sSc = 1.0f;
static s32 sOx, sOy;
static u32 sPrim;
static s32 sHavePrim;
static s32 sTexMode;               /* 1 = the text combiner is set */
static s16 sSavedViX, sSavedViY;

#define CON2D(gdl, w, h) do { (gdl)->words.w0 = (u32) 0x4c << 24 /* G_CONSOLE2D_EXT */; \
                              (gdl)->words.w1 = ((u32) (w) << 16) | (u32) (h); (gdl)++; } while (0)

static s32 X(s32 v) { return sOx + (s32) ((f32) v * sSc); }
static s32 Y(s32 v) { return sOy + (s32) ((f32) v * sSc); }
static int roomLeft(Gfx *g) { return g != NULL && (sDLEnd - g) > IN_DL_SAFE; }

static Gfx *modeFill(Gfx *g)
{
    if (sTexMode != 0)
    {
        gDPSetCombineMode(g++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
        sTexMode = 0;
        sHavePrim = 0;
    }
    return g;
}

static Gfx *fill(Gfx *g, s32 x0, s32 y0, s32 x1, s32 y1, u32 rgba)
{
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > sW) x1 = sW;
    if (y1 > sH) y1 = sH;
    if (x1 <= x0 || y1 <= y0 || (rgba & 0xFFu) == 0) return g;
    g = modeFill(g);
    if (!sHavePrim || rgba != sPrim)
    {
        gDPSetPrimColor(g++, 0, 0, (rgba >> 24) & 0xFF, (rgba >> 16) & 0xFF, (rgba >> 8) & 0xFF, rgba & 0xFF);
        sPrim = rgba;
        sHavePrim = 1;
    }
    gDPFillRectangle(g++, x0, y0, x1, y1);
    return g;
}

/* board-unit rect */
static Gfx *bfill(Gfx *g, s32 x0, s32 y0, s32 x1, s32 y1, u32 rgba) { return fill(g, X(x0), Y(y0), X(x1), Y(y1), rgba); }

static Gfx *bframe(Gfx *g, s32 x0, s32 y0, s32 x1, s32 y1, s32 t, u32 rgba)
{
    const s32 ax = X(x0), ay = Y(y0), bx = X(x1), by = Y(y1);

    if (t < 1) t = 1;
    g = fill(g, ax, ay, bx, ay + t, rgba);
    g = fill(g, ax, by - t, bx, by, rgba);
    g = fill(g, ax, ay + t, ax + t, by - t, rgba);
    g = fill(g, bx - t, ay + t, bx, by - t, rgba);
    return g;
}

/* a line as horizontal / vertical runs (screen units) */
static Gfx *sline(Gfx *g, s32 x0, s32 y0, s32 x1, s32 y1, s32 t, u32 rgba)
{
    s32 dx = x1 - x0, dy = y1 - y0, adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy, i, h = t / 2;

    if (adx == 0 && ady == 0) return fill(g, x0 - h, y0 - h, x0 - h + t, y0 - h + t, rgba);
    if (adx >= ady)
    {
        s32 sx = dx < 0 ? -1 : 1, runStart = x0, runY = y0;

        for (i = 0; i <= adx && roomLeft(g); i++)
        {
            const s32 x = x0 + i * sx;
            const s32 y = y0 + (s32) (((f32) dy * (f32) i) / (f32) adx + (dy >= 0 ? 0.5f : -0.5f));

            if (y != runY)
            {
                const s32 xa = sx > 0 ? runStart : x + 1, xb = sx > 0 ? x : runStart + 1;

                g = fill(g, xa, runY - h, xb, runY - h + t, rgba);
                runStart = x;
                runY = y;
            }
        }
        {
            const s32 xa = sx > 0 ? runStart : x1, xb = sx > 0 ? x1 + 1 : runStart + 1;

            g = fill(g, xa, runY - h, xb, runY - h + t, rgba);
        }
    }
    else
    {
        s32 sy = dy < 0 ? -1 : 1, runStart = y0, runX = x0;

        for (i = 0; i <= ady && roomLeft(g); i++)
        {
            const s32 y = y0 + i * sy;
            const s32 x = x0 + (s32) (((f32) dx * (f32) i) / (f32) ady + (dx >= 0 ? 0.5f : -0.5f));

            if (x != runX)
            {
                const s32 ya = sy > 0 ? runStart : y + 1, yb = sy > 0 ? y : runStart + 1;

                g = fill(g, runX - h, ya, runX - h + t, yb, rgba);
                runStart = y;
                runX = x;
            }
        }
        {
            const s32 ya = sy > 0 ? runStart : y1, yb = sy > 0 ? y1 + 1 : runStart + 1;

            g = fill(g, runX - h, ya, runX - h + t, yb, rgba);
        }
    }
    return g;
}

static Gfx *bline(Gfx *g, s32 x0, s32 y0, s32 x1, s32 y1, s32 t, u32 rgba)
{
    return sline(g, X(x0), Y(y0), X(x1), Y(y1), (s32) ((f32) t * sSc + 0.5f) < 1 ? 1 : (s32) ((f32) t * sSc + 0.5f), rgba);
}

/* a filled triangle as 1-unit rows (screen units) */
static Gfx *stri(Gfx *g, s32 x0, s32 y0, s32 x1, s32 y1, s32 x2, s32 y2, u32 rgba)
{
    s32 ymin = y0, ymax = y0, y;
    const s32 xs[3] = { x0, x1, x2 }, ys[3] = { y0, y1, y2 };

    if (y1 < ymin) ymin = y1;
    if (y2 < ymin) ymin = y2;
    if (y1 > ymax) ymax = y1;
    if (y2 > ymax) ymax = y2;
    if (ymin < 0) ymin = 0;
    if (ymax > sH) ymax = sH;
    for (y = ymin; y < ymax && roomLeft(g); y++)
    {
        const f32 fy = (f32) y + 0.5f;
        f32 lo = 1e9f, hi = -1e9f;
        int e;

        for (e = 0; e < 3; e++)
        {
            const s32 ax = xs[e], ay = ys[e], bx = xs[(e + 1) % 3], by = ys[(e + 1) % 3];

            if ((fy >= (f32) ay && fy < (f32) by) || (fy >= (f32) by && fy < (f32) ay))
            {
                const f32 x = (f32) ax + ((fy - (f32) ay) * (f32) (bx - ax)) / (f32) (by - ay);

                if (x < lo) lo = x;
                if (x > hi) hi = x;
            }
        }
        if (hi >= lo) g = fill(g, (s32) (lo + 0.5f), y, (s32) (hi + 0.5f) + 1, y + 1, rgba);
    }
    return g;
}

static Gfx *btri(Gfx *g, s32 x0, s32 y0, s32 x1, s32 y1, s32 x2, s32 y2, u32 rgba)
{
    return stri(g, X(x0), Y(y0), X(x1), Y(y1), X(x2), Y(y2), rgba);
}

/* a disc (screen units) */
static Gfx *sdisc(Gfx *g, s32 cx, s32 cy, s32 r, u32 rgba)
{
    s32 dy;

    for (dy = -r; dy <= r && roomLeft(g); dy++)
    {
        s32 w = 0;

        while ((w + 1) * (w + 1) + dy * dy <= r * r + r) w++;
        g = fill(g, cx - w, cy + dy, cx + w + 1, cy + dy + 1, rgba);
    }
    return g;
}

static Gfx *sring(Gfx *g, s32 cx, s32 cy, s32 r, u32 rgba)
{
    s32 dy;

    for (dy = -r; dy <= r && roomLeft(g); dy++)
    {
        s32 w = 0, wi = 0;
        const s32 ri = r - 2;

        while ((w + 1) * (w + 1) + dy * dy <= r * r + r) w++;
        if (ri > 0 && dy * dy <= ri * ri) { while ((wi + 1) * (wi + 1) + dy * dy <= ri * ri) wi++; }
        else wi = -1;
        if (wi < 0) g = fill(g, cx - w, cy + dy, cx + w + 1, cy + dy + 1, rgba);
        else
        {
            g = fill(g, cx - w, cy + dy, cx - wi, cy + dy + 1, rgba);
            g = fill(g, cx + wi + 1, cy + dy, cx + w + 1, cy + dy + 1, rgba);
        }
    }
    return g;
}

static struct fontchar *fChars(int font) { return (font && ptrFontZurichBoldChars) ? ptrFontZurichBoldChars : ptrFontBankGothicChars; }
static struct font *fFont(int font) { return (font && ptrFontZurichBold) ? ptrFontZurichBold : ptrFontBankGothic; }

static s32 textW(const char *t, int font)
{
    s32 h = 0, w = 0;

    textMeasure(&h, &w, (char *) t, fChars(font), fFont(font), 0);
    return w;
}

static Gfx *modeText(Gfx *g)
{
    if (sTexMode != 1)
    {
        gDPSetCombineLERP(g++, 0, 0, 0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0, 0, 0, 0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0);
        sTexMode = 1;
    }
    return g;
}

/* one line of text at screen (x, y) */
static Gfx *stext(Gfx *g, s32 x, s32 y, const char *t, u32 rgba, f32 scale, int font)
{
    s32 tx = x, ty = y;

    if (t == NULL || t[0] == '\0' || !roomLeft(g)) return g;
    if ((sDLEnd - g) < IN_DL_SAFE + (s32) strlen(t) * 14) return g;
    g = modeText(g);
    portTextScale(scale);
    g = textRender(g, &tx, &ty, (char *) t, fChars(font), fFont(font), rgba, sW, sH, 0, 0);
    portTextScale(1.0f);
    sHavePrim = 0;   /* textRender set its own prim colour */
    return g;
}

/* text at board (x, y), wrapped at `wrap` board units (0 = no wrap); '\n' breaks a line; the line pitch follows the font and scale */
static Gfx *btext(Gfx *g, s32 x, s32 y, const char *t, u32 rgba, f32 scale, s32 wrap, int font)
{
    char line[256];
    const f32 sc = scale * sSc;
    const s32 pitch = (s32) ((font ? 16.0f : 11.0f) * scale + 0.5f);
    s32 by = y;

    portTextScale(sc);
    while (*t && roomLeft(g))
    {
        s32 n = 0, lastFit = -1;
        const char *s = t;

        for (;;)
        {   /* word by word: measured at each word's end only */
            const char c = s[n];

            if (c == ' ' || c == '\n' || c == '\0' || n >= (s32) sizeof(line) - 1)
            {
                line[n] = '\0';
                if (wrap > 0 && lastFit > 0 && (f32) textW(line, font) > (f32) wrap * sSc)
                {
                    n = lastFit;
                    break;
                }
                if (c != ' ' || n >= (s32) sizeof(line) - 2) break;
                lastFit = n;
            }
            line[n] = c;
            n++;
        }
        line[n] = '\0';
        g = stext(g, X(x), Y(by), line, rgba, sc, font);
        portTextScale(sc);
        t = s + n;
        if (*t == ' ' || *t == '\n') t++;
        by += pitch;
    }
    portTextScale(1.0f);
    return g;
}

static Gfx *marker(Gfx *g, const IntelItem *it, const char *label)
{
    const s32 cx = X(it->a), cy = Y(it->b), r = (s32) ((f32) it->c * sSc + 0.5f);
    const u32 col = it->rgba;

    switch (it->shape)
    {
    case M_RING:    g = sring(g, cx, cy, r, col); break;
    case M_X:       g = sline(g, cx - r, cy - r, cx + r, cy + r, 2, col); g = sline(g, cx - r, cy + r, cx + r, cy - r, 2, col); break;
    case M_PIN:     g = sdisc(g, cx + 1, cy + 2, r, 0x00000070u); g = sdisc(g, cx, cy, r, col);
                    g = sdisc(g, cx - r / 3, cy - r / 3, r / 3 > 0 ? r / 3 : 1, 0xFFFFFFA0u); break;
    case M_CHECK:   g = sline(g, cx - r, cy, cx - r / 3, cy + r, 3, col); g = sline(g, cx - r / 3, cy + r, cx + r, cy - r, 3, col); break;
    case M_BOX:     g = fill(g, cx - r, cy - r, cx + r, cy - r + 2, col); g = fill(g, cx - r, cy + r - 2, cx + r, cy + r, col);
                    g = fill(g, cx - r, cy - r, cx - r + 2, cy + r, col); g = fill(g, cx + r - 2, cy - r, cx + r, cy + r, col); break;
    case M_TRI:     g = stri(g, cx, cy - r, cx - r, cy + r, cx + r, cy + r, col); break;
    case M_DIAMOND: g = stri(g, cx, cy - r, cx - r, cy, cx + r, cy, col); g = stri(g, cx - r, cy, cx + r, cy, cx, cy + r, col); break;
    case M_CAM:     g = fill(g, cx - r, cy - r / 2, cx + r / 2, cy + r / 2, col); g = stri(g, cx + r / 2, cy, cx + r, cy - r / 2, cx + r, cy + r / 2, col); break;
    case M_STAIRS:  {
                        s32 k;

                        for (k = 0; k < 3; k++) g = fill(g, cx - r + k * r * 2 / 3, cy + r - (k + 1) * r * 2 / 3, cx + r, cy + r - k * r * 2 / 3, col);
                    }
                    break;
    case M_DOOR:    g = fill(g, cx - r, cy - r, cx + r, cy + r, 0x101010E0u); g = fill(g, cx - r + 1, cy - r + 1, cx + r - 1, cy + r - 1, col); break;
    case M_STAR:    g = stri(g, cx, cy - r, cx - r, cy + r / 2, cx + r, cy + r / 2, col); g = stri(g, cx, cy + r, cx - r, cy - r / 2, cx + r, cy - r / 2, col); break;
    default:        g = sdisc(g, cx, cy, r, col); break;
    }
    if (label)
    {
        g = stext(g, cx + r + 3, cy - 5, label, col | 0xFFu, sSc * 0.9f, 0);
    }
    return g;
}

static Gfx *drawPool(Gfx *g, const IntelPool *P, s32 page, s32 floor)
{
    s32 i;

    for (i = 0; i < P->n && roomLeft(g); i++)
    {
        const IntelItem *it = &P->item[i];
        const char *t = it->text >= 0 ? P->text + it->text : NULL;

        if (it->page != page || (it->floor != 255 && it->floor != floor)) continue;
        switch (it->kind)
        {
        case K_RECT:   g = bfill(g, it->a, it->b, it->c, it->d, it->rgba); break;
        case K_FRAME:  g = bframe(g, it->a, it->b, it->c, it->d, it->e, it->rgba); break;
        case K_LINE:   g = bline(g, it->a, it->b, it->c, it->d, it->e, it->rgba); break;
        case K_TRI:    g = btri(g, it->a, it->b, it->c, it->d, it->e, it->f, it->rgba); break;
        case K_TEXT:   if (t) g = btext(g, it->a, it->b, t, it->rgba, (f32) it->f / 100.0f, it->e, it->font); break;
        case K_MARKER: g = marker(g, it, t); break;
        }
    }
    return g;
}

/* the board's own chrome: the corkboard frame, the blueprint paper and its grid, the tabs, the floor list, the footer */
static Gfx *drawChrome(Gfx *g)
{
    s32 i, x;
    char buf[96];

    g = bfill(g, 6, 6, BW - 6, BH - 6, 0x6B4A2CF4u);                        /* cork */
    for (i = 0; i < 40; i++)
    {   /* cork flecks (fixed, so the board does not shimmer) */
        const s32 fx = 8 + (s32) ((i * 7919u) % (BW - 16)), fy = 8 + (s32) ((i * 104729u) % (BH - 16));

        g = bfill(g, fx, fy, fx + 2, fy + 2, 0x4A3018FFu);
    }
    g = bfill(g, 14, 30, BW - 14, BH - 22, 0x102C58FFu);                    /* the blueprint */
    for (x = 14; x < BW - 14; x += 20) g = bfill(g, x, 30, x + 1, BH - 22, 0x2A4C80A0u);
    for (x = 30; x < BH - 22; x += 20) g = bfill(g, 14, x, BW - 14, x + 1, 0x2A4C80A0u);
    g = bframe(g, 18, 34, BW - 18, BH - 26, 1, 0xD8E4F4C0u);
    /* tabs: index cards along the top */
    {
        const s32 n = sPages > 0 ? sPages : 1;
        const s32 tw = (BW - 40) / n;

        for (i = 0; i < sPages; i++)
        {
            const s32 tx = 20 + i * tw;
            const int cur = (i == sPage);

            g = bfill(g, tx + 1, cur ? 8 : 12, tx + tw - 3, 30, cur ? 0xF2EBD0FFu : 0xB8B098FFu);
            snprintf(buf, sizeof(buf), "%d %s", i + 1, sPageTitle[i]);
            g = stext(g, X(tx + (n > 6 ? 3 : 6)), Y(cur ? 13 : 16), buf, cur ? 0x9A1010FFu : 0x404040FFu, n > 6 ? sSc * 0.72f : sSc, 0);
        }
    }
    /* the floor list on the map page */
    if (sPage == sMapPage && sFloors > 0)
    {
        for (i = 0; i < sFloors; i++)
        {
            const s32 fy = 52 + (sFloors - 1 - i) * 22;
            const int cur = (i == sFloor);

            g = bfill(g, 24, fy, 64, fy + 18, cur ? 0xF2EBD0FFu : 0x1E3E70FFu);
            g = bframe(g, 24, fy, 64, fy + 18, 1, 0xD8E4F4FFu);
            g = stext(g, X(30), Y(fy + 4), sFloorName[i], cur ? 0x9A1010FFu : 0xD8E4F4FFu, sSc, 0);
        }
        g = stext(g, X(24), Y(52 + sFloors * 22), "UP/DOWN", 0x90A8C8FFu, sSc * 0.8f, 0);
    }
    /* footer */
    g = bfill(g, 14, BH - 21, BW - 14, BH - 8, 0x000000A0u);
    if (sPlanOn) g = stext(g, X(20), Y(BH - 19), sPlanFoot, 0xFFE060FFu, sSc * 0.9f, 0);
    else g = stext(g, X(20), Y(BH - 19), sBrief ? "BRIEFING  -  press INTEL (Tab / Back) to close and get to work. It's on that key all heist."
                                           : "INTEL (Tab / Back): close     PAGE: wheel  , .  LB/RB     FLOOR: Up/Down  Q [  gadget buttons",
              sBrief ? 0xFFE060FFu : 0xC8D4E8FFu, sSc, 0);
    if (sTitle[0])
    {
        g = stext(g, X(BW - 16 - (s32) ((f32) textW(sTitle, 0) / 1.0f)), Y(BH - 34), sTitle, 0x90A8C8FFu, sSc, 0);
    }
    return g;
}

/* the crew, live, on the map page (this machine's Bond is YOU) */
static Gfx *drawBonds(Gfx *g)
{
    const int n = portLuaGameBondCount(), me = portLocalPlayer();
    int p;

    if (sPage != sMapPage || sFloor < 0 || sFloor >= sFloors) return g;
    for (p = 0; p < n && p < IN_BONDS; p++)
    {
        float x, y, z;
        const f32 *F = sFloorX[sFloor];

        if (sBondFloor[p] != sFloor || !portLuaGameBondPos(p, &x, &y, &z)) continue;
        {
            const s32 bx = (s32) (F[3] + (F[0] - x) * F[2]), by = (s32) (F[4] + (F[1] - z) * F[2]);
            const s32 cx = X(bx), cy = Y(by);
            const u32 col = p == me ? 0xFFE040FFu : 0x40E0FFFFu;
            const int dead = portLuaGameBondDead(p) == 1;
            char lab[16];

            if (p == me && ((sFrame / 15) & 1)) g = sring(g, cx, cy, 9, 0xFFE04090u);
            g = sdisc(g, cx, cy, 5, 0x000000C0u);
            g = sdisc(g, cx, cy, 4, dead ? 0x808080FFu : col);
            snprintf(lab, sizeof(lab), p == me ? "YOU" : "BOND %d", p + 1);
            g = stext(g, cx + 8, cy - 6, lab, col, sSc, 0);
        }
    }
    return g;
}

/** port_console.c portConRender, every frame in a mode: the board when it is open on this machine. */
Gfx *portIntelRender(Gfx *gdl)
{
    Gfx *g;

    sFrame++;
    intelEpochCheck();
    intelAutoTick();
    if (!sOpen || !portIntelAvailable() || ptrFontBankGothic == NULL || ptrFontBankGothicChars == NULL || !intelPools())
    {
        return gdl;
    }
    if (sDL[0] == NULL)
    {
        sDL[0] = (Gfx *) portAllocHigh(IN_DL_GFX * sizeof(Gfx));
        sDL[1] = (Gfx *) portAllocHigh(IN_DL_GFX * sizeof(Gfx));
        if (sDL[0] == NULL || sDL[1] == NULL)
        {
            portConLogf("ge: intel: no memory for the board's display lists\n");
            sOpen = 0;
            return gdl;
        }
    }
    if (dynGetFreeGfx2(gdl) < 64)
    {
        return gdl;
    }
    if (sPage >= sPages) sPage = 0;
    if (sFloor < 0 && sFloors > 0)
    {   /* first look: the floor this Bond is on */
        const int me = portLocalPlayer();

        sFloor = (me >= 0 && me < IN_BONDS && sBondFloor[me] >= 0) ? sBondFloor[me] : 0;
    }
    portConsoleSpace(&sW, &sH);
    sSc = (f32) sW / (f32) BW;
    if ((f32) sH / (f32) BH < sSc) sSc = (f32) sH / (f32) BH;
    sOx = (sW - (s32) ((f32) BW * sSc)) / 2;
    sOy = (sH - (s32) ((f32) BH * sSc)) / 2;
    sDLi ^= 1;
    g = sDL[sDLi];
    sDLEnd = g + IN_DL_GFX;
    sHavePrim = 0;
    sTexMode = -1;

    /* the console's 2D space, as port_console.c conSpaceBegin (textRender clips against the VI size) */
    sSavedViX = viGetX();
    sSavedViY = viGetY();
    viSetXY((s16) sW, (s16) sH);
    CON2D(gdl, sW, sH);
    gdl = microcode_constructor(gdl);
    gDPSetScissor(gdl++, G_SC_NON_INTERLACE, 0, 0, sW, sH);
    gSPDisplayList(gdl++, g);

    gDPSetRenderMode(g++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    g = drawChrome(g);
    g = drawPool(g, sPool[0], sPage, sFloor);
    g = drawPool(g, sPool[1], sPage, sFloor);
    g = drawPool(g, sPool[2], sPage, sFloor);
    g = drawBonds(g);
    portTextScale(1.0f);
    gSPEndDisplayList(g++);

    gdl = combiner_bayer_lod_perspective(gdl);
    CON2D(gdl, 0, 0);
    viSetXY(sSavedViX, sSavedViY);
    return gdl;
}

#endif /* !TARGET_N64 */
