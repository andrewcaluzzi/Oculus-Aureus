/* port_luavm_intel.c - Lua bindings for the heist INTEL board (designs/bank-heist.md section 19; modding/lua.md "INTEL board").
 * The C side is port/src/port_intel.c. The board is presentation only: these calls fill a list of 2D primitives that this machine
 * draws when its player opens the board; nothing here is sim state. A script builds its content on its own clock (never "when the board
 * is open", which is one machine's input), so the sim runs the same code whether anyone looks or not.
 *
 * Board units: 640 x 480 (x right, y down), centred on the screen. Colours: 0xRRGGBBAA.
 *   intel.enable(on)                     this mode has a board (the INTEL key opens it; Tab stops being Classic Mode here)
 *   intel.clear([live])                  live = true: drop the live layer only; else everything (pages, floors, both layers)
 *   intel.title(text)                    the small caption at the bottom right
 *   intel.page(i, title)                 page i (1..8): its tab
 *   intel.map_page(i)                    the page whose items are per floor (up / down picks the floor; the crew is drawn live there)
 *   intel.floor(k, name, wx1, wz1, s, bx0, by0)   floor k (1..10) of the map page: world (x, z) -> board (bx0 + (wx1 - x) s, by0 + (wz1 - z) s)
 *   intel.bond(p, k)                     Bond p is on floor k (0 = none): the map draws him there, live, every frame
 *   intel.at(page, floor, live)          where the next items go: page, floor (0 = every floor), the live layer (true) or static
 *   intel.rect(x0, y0, x1, y1, rgba)     filled          intel.frame(x0, y0, x1, y1, rgba [, thick])   outline
 *   intel.line(x0, y0, x1, y1, rgba [, thick])           intel.tri(x0, y0, x1, y1, x2, y2, rgba)      filled
 *   intel.poly({x1, y1, x2, y2, ...}, rgba)              filled convex polygon (a fan of triangles)
 *   intel.text(x, y, text, rgba [, scale [, wrap [, font]]])   wrap = width in board units (0 none); font 1 = the big face; "\n" breaks
 *   intel.marker(x, y, kind, rgba [, size [, label]])    kind: dot ring x pin check box tri diamond cam stairs door star
 *   intel.briefing()                     open the board once on this machine as the start-of-heist briefing (skipped in automated runs)
 *   intel.count([live]) -> n             items in a layer (tests)
 *   intel.test_show(open, page, floor)   tests and screenshots: set this machine's view (never call it from game logic)
 *   intel.view() -> open, page, floor    this machine's view (tests only: game logic must never read it) */
#include <string.h>
#include "lua.h"
#include "lauxlib.h"

extern void portIntelEnable(int on);
extern void portIntelClear(int liveOnly);
extern void portIntelTitle(const char *t);
extern void portIntelPage(int i, const char *title);
extern void portIntelMapPage(int i);
extern void portIntelFloor(int k, const char *name, float wx1, float wz1, float s, float bx0, float by0);
extern void portIntelBondFloor(int p, int k);
extern void portIntelAt(int page, int floor, int live);
extern void portIntelRect(float x0, float y0, float x1, float y1, unsigned int rgba, int fill, int thick);
extern void portIntelLine(float x0, float y0, float x1, float y1, unsigned int rgba, int thick);
extern void portIntelTri(float x0, float y0, float x1, float y1, float x2, float y2, unsigned int rgba);
extern void portIntelText(float x, float y, const char *text, unsigned int rgba, float scale, int wrap, int font);
extern void portIntelMarker(float x, float y, const char *kind, unsigned int rgba, int size, const char *label);
extern void portIntelBriefing(void);
extern int portIntelItemCount(int live);
extern void portIntelTestShow(int open, int page, int floor);
extern int portIntelIsOpen(void);
extern int portIntelViewPage(void);
extern int portIntelViewFloor(void);

static float num(lua_State *L, int i) { return (float) luaL_checknumber(L, i); }
static unsigned int col(lua_State *L, int i) { return (unsigned int) (luaL_optinteger(L, i, 0xFFFFFFFF) & 0xFFFFFFFF); }

static int lEnable(lua_State *L) { portIntelEnable(lua_toboolean(L, 1)); return 0; }
static int lClear(lua_State *L) { portIntelClear(lua_toboolean(L, 1)); return 0; }
static int lTitle(lua_State *L) { portIntelTitle(luaL_checkstring(L, 1)); return 0; }
static int lPage(lua_State *L) { portIntelPage((int) luaL_checkinteger(L, 1), luaL_checkstring(L, 2)); return 0; }
static int lMapPage(lua_State *L) { portIntelMapPage((int) luaL_checkinteger(L, 1)); return 0; }
static int lBond(lua_State *L) { portIntelBondFloor((int) luaL_checkinteger(L, 1), (int) luaL_optinteger(L, 2, 0)); return 0; }
static int lBriefing(lua_State *L) { (void) L; portIntelBriefing(); return 0; }

static int lFloor(lua_State *L)
{
    portIntelFloor((int) luaL_checkinteger(L, 1), luaL_checkstring(L, 2), num(L, 3), num(L, 4), num(L, 5), num(L, 6), num(L, 7));
    return 0;
}

static int lAt(lua_State *L)
{
    portIntelAt((int) luaL_checkinteger(L, 1), (int) luaL_optinteger(L, 2, 0), lua_toboolean(L, 3));
    return 0;
}

static int lRect(lua_State *L) { portIntelRect(num(L, 1), num(L, 2), num(L, 3), num(L, 4), col(L, 5), 1, 1); return 0; }
static int lFrame(lua_State *L)
{
    portIntelRect(num(L, 1), num(L, 2), num(L, 3), num(L, 4), col(L, 5), 0, (int) luaL_optinteger(L, 6, 1));
    return 0;
}
static int lLine(lua_State *L)
{
    portIntelLine(num(L, 1), num(L, 2), num(L, 3), num(L, 4), col(L, 5), (int) luaL_optinteger(L, 6, 1));
    return 0;
}
static int lTri(lua_State *L) { portIntelTri(num(L, 1), num(L, 2), num(L, 3), num(L, 4), num(L, 5), num(L, 6), col(L, 7)); return 0; }

static int lPoly(lua_State *L)
{
    float v[64];
    int n, i;

    luaL_checktype(L, 1, LUA_TTABLE);
    n = (int) lua_rawlen(L, 1);
    if (n > 64) n = 64;
    for (i = 0; i < n; i++)
    {
        lua_rawgeti(L, 1, i + 1);
        v[i] = (float) lua_tonumber(L, -1);
        lua_pop(L, 1);
    }
    for (i = 2; i < n / 2; i++)
    {
        portIntelTri(v[0], v[1], v[2 * (i - 1)], v[2 * (i - 1) + 1], v[2 * i], v[2 * i + 1], col(L, 2));
    }
    return 0;
}

static int lText(lua_State *L)
{
    portIntelText(num(L, 1), num(L, 2), luaL_checkstring(L, 3), col(L, 4), (float) luaL_optnumber(L, 5, 1.0),
                  (int) luaL_optinteger(L, 6, 0), (int) luaL_optinteger(L, 7, 0));
    return 0;
}

static int lMarker(lua_State *L)
{
    portIntelMarker(num(L, 1), num(L, 2), luaL_checkstring(L, 3), col(L, 4), (int) luaL_optinteger(L, 5, 5), luaL_optstring(L, 6, NULL));
    return 0;
}

static int lCount(lua_State *L) { lua_pushinteger(L, portIntelItemCount(lua_toboolean(L, 1))); return 1; }

static int lTestShow(lua_State *L)
{
    portIntelTestShow(lua_toboolean(L, 1), (int) luaL_optinteger(L, 2, 0), (int) luaL_optinteger(L, 3, 0));
    return 0;
}

static int lView(lua_State *L)
{
    lua_pushboolean(L, portIntelIsOpen());
    lua_pushinteger(L, portIntelViewPage());
    lua_pushinteger(L, portIntelViewFloor());
    return 3;
}

void portLuaIntelRegister(lua_State *L)
{
    static const luaL_Reg lib[] = {
        { "enable", lEnable }, { "clear", lClear }, { "title", lTitle }, { "page", lPage }, { "map_page", lMapPage }, { "floor", lFloor },
        { "bond", lBond }, { "at", lAt }, { "rect", lRect }, { "frame", lFrame }, { "line", lLine }, { "tri", lTri }, { "poly", lPoly },
        { "text", lText }, { "marker", lMarker }, { "briefing", lBriefing }, { "count", lCount }, { "test_show", lTestShow },
        { "view", lView }, { NULL, NULL } };

    luaL_newlib(L, lib);
    lua_setglobal(L, "intel");
}
