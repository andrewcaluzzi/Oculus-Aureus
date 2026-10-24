/* port_luavm_cctv.c - Lua bindings for the heist security-camera view and the HUD panel (modding/lua.md "CCTV view";
 * designs/bank-heist.md P1). The C side is port/src/port_cctvview.c; registered after port_luavm_heist.c (which makes the cctv table).
 *
 *   cctv.view(p, cam) -> bool          Bond p watches camera cam (a setup index from cctv.list()); an off camera shows static
 *   cctv.view_off(p) -> bool           true if he was watching
 *   cctv.viewing(p) -> cam | nil
 *   cctv.name(cam [, text]) -> name    the label the feed shows (default "CAMERA n")
 *   cctv.sees(cam, chr) -> bool        a working camera has the chr (or bond.id(p)) in its live cone with a clear line
 *   cctv.render_info() -> table        this machine's last feed pass: {cam, start, rooms = {...}, geom_only, frames} | nil (tests)
 *   hud.panel(lines | nil [, p])       up to 8 lines in a box at the top right (nil or {} clears); p = that Bond only
 *   bond.room(p) -> room | nil         spawn.room(chr) -> room | nil
 * Event: cctv_view(p, cam, why): cam -1 = the view ended; why 0 a script, 1 cycled, 2 the Bond left (B / use), 3 hit, 4 down. */
#include <string.h>
#include <stdio.h>
#include "lua.h"
#include "lauxlib.h"
#include "../include/port_lua.h"

#define PANEL_LINES 8

static void needStage(lua_State *L, const char *api)
{
    if (!portLuaGameStageLive())
    {
        luaL_error(L, "%s needs a live stage (not available in the menus)", api);
    }
}

static int cctvView(lua_State *L)
{
    needStage(L, "cctv.view");
    lua_pushboolean(L, portCctvView((int) luaL_checkinteger(L, 1), (int) luaL_checkinteger(L, 2), 0));
    return 1;
}

static int cctvViewOff(lua_State *L)
{
    needStage(L, "cctv.view_off");
    lua_pushboolean(L, portCctvViewOff((int) luaL_checkinteger(L, 1), 0));
    return 1;
}

static int cctvViewing(lua_State *L)
{
    int c;

    needStage(L, "cctv.viewing");
    c = portCctvViewing((int) luaL_checkinteger(L, 1));
    if (c < 0) lua_pushnil(L); else lua_pushinteger(L, c);
    return 1;
}

static int cctvName(lua_State *L)
{
    int cam = (int) luaL_checkinteger(L, 1);
    const char *n;
    char def[24];

    if (!lua_isnoneornil(L, 2)) portCctvSetName(cam, luaL_checkstring(L, 2));
    n = portCctvName(cam);
    if (n == NULL)
    {
        snprintf(def, sizeof(def), "CAMERA %d", cam);
        n = def;
    }
    lua_pushstring(L, n);
    return 1;
}

static int cctvSees(lua_State *L)
{
    needStage(L, "cctv.sees");
    lua_pushboolean(L, portCctvSeesChr((int) luaL_checkinteger(L, 1), (int) luaL_checkinteger(L, 2)));
    return 1;
}

static int cctvRenderInfo(lua_State *L)
{
    int cam, start, geom, frames, i, n;
    int rooms[48];

    n = portCctvRenderInfo(&cam, &start, rooms, 48, &geom, &frames);
    if (cam < 0 && frames == 0)
    {
        lua_pushnil(L);
        return 1;
    }
    lua_createtable(L, 0, 5);
    lua_pushinteger(L, cam);
    lua_setfield(L, -2, "cam");
    lua_pushinteger(L, start);
    lua_setfield(L, -2, "start");
    lua_pushinteger(L, geom);
    lua_setfield(L, -2, "geom_only");
    lua_pushinteger(L, frames);
    lua_setfield(L, -2, "frames");
    lua_pushinteger(L, portCctvExtraChrs());
    lua_setfield(L, -2, "chrs");
    lua_createtable(L, n, 0);
    for (i = 0; i < n; i++)
    {
        lua_pushinteger(L, rooms[i]);
        lua_rawseti(L, -2, i + 1);
    }
    lua_setfield(L, -2, "rooms");
    {   /* what the overlay shows now */
        int oc, idx, cnt, off, secs, ticks;
        char name[32];

        if (portCctvOverlayGet(&oc, name, (int) sizeof(name), &idx, &cnt, &off, &secs, &ticks))
        {
            lua_pushboolean(L, off);
            lua_setfield(L, -2, "offline");
            lua_pushstring(L, name);
            lua_setfield(L, -2, "label");
            lua_pushinteger(L, idx);
            lua_setfield(L, -2, "index");
        }
    }
    return 1;
}

static int cctvTestShot(lua_State *L)
{
    (void) L;
    portCctvTestShot();
    return 0;
}

/* hud.panel(lines | nil [, p]) */
static int hudPanel(lua_State *L)
{
    int p = (int) luaL_optinteger(L, 2, -1);
    int i;

    needStage(L, "hud.panel");
    portLuaGameHudPanelClear(p);
    if (lua_isnoneornil(L, 1)) return 0;
    luaL_checktype(L, 1, LUA_TTABLE);
    for (i = 0; i < PANEL_LINES; i++)
    {
        lua_rawgeti(L, 1, i + 1);
        if (lua_isstring(L, -1)) portLuaGameHudPanel(p, i, lua_tostring(L, -1));
        lua_pop(L, 1);
    }
    return 0;
}

static int bondRoom(lua_State *L)
{
    int r;

    needStage(L, "bond.room");
    r = portCctvBondRoom((int) luaL_checkinteger(L, 1));
    if (r < 0) lua_pushnil(L); else lua_pushinteger(L, r);
    return 1;
}

static int spawnRoom(lua_State *L)
{
    int r;

    needStage(L, "spawn.room");
    r = portCctvChrRoom((int) luaL_checkinteger(L, 1));
    if (r < 0) lua_pushnil(L); else lua_pushinteger(L, r);
    return 1;
}

/* cctv.test_press(button): test helper, holds raw N64 "a" / "b" / "z" / "cl" / "cr" on pad 0 for a few polls (the real input path) */
static int cctvTestPress(lua_State *L)
{
    int m = portCctvButtonMask(luaL_checkstring(L, 1));

    if (m == 0) return luaL_error(L, "cctv.test_press: unknown button '%s' (a, b, z, cl, cr)", luaL_checkstring(L, 1));
    portCctvTestPress(m);
    return 0;
}

/* cctv.test_use([p]): test helper, one use-action tap (the Port use bind, not B) in Bond p's next tick (default 0) */
static int cctvTestUse(lua_State *L)
{
    portCctvTestUse((int) luaL_optinteger(L, 1, 0));
    return 0;
}

/* bond.use_key(p) -> bool: inside a `use` handler, the press is the use action (the Port use bind), not the B button (B is also reload) */
static int bondUseKey(lua_State *L)
{
    lua_pushboolean(L, portHeistUseKey((int) luaL_checkinteger(L, 1)));
    return 1;
}

/* mode.state(text): the mode's own sim state as a string, set each tick: hashed into the co-op / netplay state hash ("flags") and shown
 * in the desync dump, so a script-state desync is caught and named (the heist: phase, crack, vault, loot, waves) */
static int modeState(lua_State *L)
{
    portHeistModeState(luaL_checkstring(L, 1));
    return 0;
}

/* hud.line(n [, p]) -> the text of HUD line n (1..3) Bond p sees (his own line, else the shared one), "" none (tests) */
static int hudLine(lua_State *L)
{
    extern const char *portModeHudGetFor(int player, int line);
    int n = (int) luaL_checkinteger(L, 1);
    const char *t = (n >= 1 && n <= 3) ? portModeHudGetFor((int) luaL_optinteger(L, 2, 0), n - 1) : NULL;

    lua_pushstring(L, t != NULL ? t : "");
    return 1;
}

/* hud.panel_lines() -> the lines this machine's panel shows (tests) */
static int hudPanelLines(lua_State *L)
{
    int i, n = 0;

    lua_createtable(L, PANEL_LINES, 0);
    for (i = 0; i < PANEL_LINES; i++)
    {
        const char *t = portLuaGameHudPanelGet(i);

        if (t != NULL)
        {
            lua_pushstring(L, t);
            lua_rawseti(L, -2, ++n);
        }
    }
    return 1;
}

static void addTo(lua_State *L, const char *lib, const luaL_Reg *reg)
{
    lua_getglobal(L, lib);
    luaL_setfuncs(L, reg, 0);
    lua_pop(L, 1);
}

void portLuaCctvRegister(lua_State *L)
{
    static const luaL_Reg cctvAdd[] = { { "view", cctvView }, { "view_off", cctvViewOff }, { "viewing", cctvViewing }, { "name", cctvName },
        { "sees", cctvSees }, { "render_info", cctvRenderInfo }, { "test_press", cctvTestPress }, { "test_shot", cctvTestShot },
        { "test_use", cctvTestUse }, { NULL, NULL } };
    static const luaL_Reg hudAdd[] = { { "panel", hudPanel }, { "panel_lines", hudPanelLines }, { "line", hudLine }, { NULL, NULL } };
    static const luaL_Reg bondAdd[] = { { "room", bondRoom }, { "use_key", bondUseKey }, { NULL, NULL } };
    static const luaL_Reg spawnAdd[] = { { "room", spawnRoom }, { NULL, NULL } };
    static const luaL_Reg modeAdd[] = { { "state", modeState }, { NULL, NULL } };

    addTo(L, "mode", modeAdd);
    addTo(L, "cctv", cctvAdd);
    addTo(L, "hud", hudAdd);
    addTo(L, "bond", bondAdd);
    addTo(L, "spawn", spawnAdd);
}
