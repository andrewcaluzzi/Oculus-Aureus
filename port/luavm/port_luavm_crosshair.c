/* port_luavm_crosshair.c - Lua bindings for crosshair styles (port/src/port_crosshair.c, modding/lua.md "Crosshair styles").
 * Presentation only: nothing here changes the sim.
 *
 *   crosshair.style(opts | nil)       a style for this stage (a script's style; dropped at the next stage start). opts: shape, size,
 *                                     gap, thickness, color, dot, dot_size, outline, outline_color, outline_thickness, dynamic,
 *                                     spread_scale, move_bloom, fire_kick, hit_marker, hit_color, headshot_color, hit_time, hit_size,
 *                                     always, draw = function(x, y, ctx), weapons = { <weapon> = { ...overrides } }
 *   crosshair.hide([bool])            no sight at all (true by default); crosshair.hidden()
 *   crosshair.reset()                 drop the script's style and the hide
 *   crosshair.use(name)               the player's own style from the pack (Data/Scripts/mods/crosshair_pack.lua; "off" = classic);
 *                                     lasts for the session (console `crosshair <name>`, GE_CROSSHAIR=<name>)
 *   crosshair.list() -> names         the pack's styles; crosshair.shapes() -> the shape names
 *   crosshair.current() -> shape, who the shape drawn now ("classic" when none) and whose it is ("script" / "player" / nil)
 *   crosshair.spread() -> px          the dynamic gap last drawn (HUD px)
 *   test.crosshair_draws() -> n, test.crosshair_hit([head])   the test table: frames drawn by a custom sight; a hit marker now
 *   on("draw_crosshair", function(x, y, ctx) ... end)   every frame the sight could show (not in netplay); ctx.line(x0, y0, x1, y1
 *                                     [, thickness [, color]]), ctx.rect(x0, y0, x1, y1 [, color]), ctx.circle(x, y, r [, thickness
 *                                     [, color]]) (thickness 0: filled), ctx.dot(x, y [, size [, color]]); ctx.spread, ctx.since_hit
 *                                     (nil before the first hit), ctx.headshot, ctx.aiming, ctx.weapon, ctx.player, ctx.w, ctx.h
 * Colours: 0xRRGGBBAA, a name (white red green blue cyan yellow orange gold magenta black), or {r, g, b [, a]} (0..255).
 * Units: HUD pixels, the game's 240-line frame (one is 3 window pixels at 720p, before the HUD Scale). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lua.h"
#include "lauxlib.h"
#include "../include/port_crosshair.h"

extern lua_State *portLuaVmState(int create);              /* port_luavm.c */
extern int portLuaVmCall(lua_State *L, int nargs, int nres);
extern int portLuaGameAllowed(void);
extern const char *portLuaGameScriptDir(void);
extern int portNetActive(void);
extern void portConLogf(const char *fmt, ...);
extern void portConPrintf(int showmsg, const char *fmt, ...);

#define XHAIR_EVENT        102                 /* port_luavm.c LUA_EV_XHAIR: the on() handler slot */
#define XHAIR_HANDLERS_KEY "midas_handlers"
#define XHAIR_DRAW_KEY     "midas_xhair_draw"  /* [1] the script style's draw function, [2] the player's */
#define XHAIR_PACK_KEY     "midas_xhair_pack"
#define XHAIR_CTX_KEY      "midas_xhair_ctx"

static int sOnHooked;
static int sHasDraw[XHAIR_LAYERS];
static char sUserName[40];

static const char *const kShapes[XHAIR_SHAPE_COUNT] = { "classic", "none", "dot", "cross", "t", "x", "circle", "chevron", "brackets" };

/* ---- arguments -------------------------------------------------------------------------------------------------------------------- */

static unsigned colourAt(lua_State *L, int i, unsigned dflt)
{
    static const struct { const char *n; unsigned c; } names[] = {
        { "white", 0xFFFFFFFFu }, { "red", 0xFF3030FFu }, { "green", 0x40FF60FFu }, { "blue", 0x4080FFFFu }, { "cyan", 0x40F0FFFFu },
        { "yellow", 0xFFE040FFu }, { "orange", 0xFF9020FFu }, { "gold", 0xFFD040FFu }, { "magenta", 0xFF40E0FFu },
        { "black", 0x000000FFu }, { NULL, 0 } };
    int k;

    if (lua_isnoneornil(L, i)) return dflt;
    if (lua_type(L, i) == LUA_TSTRING)
    {
        const char *s = lua_tostring(L, i);

        for (k = 0; names[k].n != NULL; k++) if (strcmp(names[k].n, s) == 0) return names[k].c;
        return (unsigned) luaL_error(L, "unknown colour '%s'", s);
    }
    if (lua_istable(L, i))
    {
        unsigned c[4] = { 255, 255, 255, 255 };

        for (k = 0; k < 4; k++)
        {
            lua_rawgeti(L, i, k + 1);
            if (lua_isnumber(L, -1))
            {
                lua_Integer v = lua_tointeger(L, -1);

                c[k] = (unsigned) (v < 0 ? 0 : (v > 255 ? 255 : v));
            }
            lua_pop(L, 1);
        }
        return (c[0] << 24) | (c[1] << 16) | (c[2] << 8) | c[3];
    }
    return (unsigned) (lua_Unsigned) luaL_checkinteger(L, i);
}

static int shapeFind(const char *s)
{
    int k;

    if (strcmp(s, "plus") == 0) return XHAIR_CROSS;
    if (strcmp(s, "ring") == 0) return XHAIR_CIRCLE;
    if (strcmp(s, "vanilla") == 0 || strcmp(s, "default") == 0) return XHAIR_CLASSIC;
    for (k = 0; k < XHAIR_SHAPE_COUNT; k++) if (strcmp(kShapes[k], s) == 0) return k;
    return -1;
}

static float numField(lua_State *L, int t, const char *name, float dflt, float lo, float hi)
{
    float v = dflt;

    if (lua_getfield(L, t, name) != LUA_TNIL)
    {
        if (!lua_isnumber(L, -1)) luaL_error(L, "crosshair: '%s' must be a number", name);
        v = (float) lua_tonumber(L, -1);
        if (!(v == v)) v = dflt;
        if (v < lo) v = lo;
        if (v > hi) v = hi;
    }
    lua_pop(L, 1);
    return v;
}

static int boolField(lua_State *L, int t, const char *name, int dflt)
{
    int v = dflt;

    if (lua_getfield(L, t, name) != LUA_TNIL) v = lua_toboolean(L, -1);
    lua_pop(L, 1);
    return v;
}

static unsigned colourField(lua_State *L, int t, const char *name, unsigned dflt)
{
    unsigned v = dflt;

    if (lua_getfield(L, t, name) != LUA_TNIL) v = colourAt(L, lua_gettop(L), dflt);
    lua_pop(L, 1);
    return v;
}

/* the fields present in the table at t, over what s already holds */
static void parseStyle(lua_State *L, int t, PortXhairStyle *s)
{
    if (lua_getfield(L, t, "shape") != LUA_TNIL)
    {
        const char *n = luaL_checkstring(L, -1);
        int k = shapeFind(n);

        if (k < 0) luaL_error(L, "crosshair: unknown shape '%s' (classic none dot cross t x circle chevron brackets)", n);
        s->shape = k;
    }
    lua_pop(L, 1);
    s->size = numField(L, t, "size", s->size, 0.0f, 60.0f);
    s->gap = numField(L, t, "gap", s->gap, 0.0f, 60.0f);
    s->thick = numField(L, t, "thick", s->thick, 0.1f, 20.0f);
    s->thick = numField(L, t, "thickness", s->thick, 0.1f, 20.0f);
    s->color = colourField(L, t, "colour", s->color);
    s->color = colourField(L, t, "color", s->color);
    if (lua_getfield(L, t, "dot") != LUA_TNIL)
    {
        if (lua_isnumber(L, -1)) { s->dot = 1; s->dotsize = (float) lua_tonumber(L, -1); }
        else s->dot = lua_toboolean(L, -1);
    }
    lua_pop(L, 1);
    s->dotsize = numField(L, t, "dot_size", s->dotsize, 0.1f, 30.0f);
    if (lua_getfield(L, t, "outline") != LUA_TNIL)
    {
        if (lua_type(L, -1) == LUA_TBOOLEAN) s->outline = lua_toboolean(L, -1);
        else { s->outline = 1; s->ocolor = colourAt(L, lua_gettop(L), s->ocolor); }
    }
    lua_pop(L, 1);
    s->ocolor = colourField(L, t, "outline_color", s->ocolor);
    s->othick = numField(L, t, "outline_thickness", s->othick, 0.1f, 10.0f);
    if (lua_getfield(L, t, "dynamic") != LUA_TNIL)
    {
        if (lua_isnumber(L, -1)) { s->dynamic = 1; s->spreadk = (float) lua_tonumber(L, -1); }
        else s->dynamic = lua_toboolean(L, -1);
    }
    lua_pop(L, 1);
    s->spreadk = numField(L, t, "spread_scale", s->spreadk, 0.0f, 20.0f);
    s->movek = numField(L, t, "move_bloom", s->movek, 0.0f, 40.0f);
    s->firek = numField(L, t, "fire_kick", s->firek, 0.0f, 40.0f);
    s->hit = boolField(L, t, "hit_marker", s->hit);
    s->hitcolor = colourField(L, t, "hit_color", s->hitcolor);
    s->headcolor = colourField(L, t, "headshot_color", s->headcolor);
    s->hittime = numField(L, t, "hit_time", s->hittime, 0.02f, 5.0f);
    s->hitsize = numField(L, t, "hit_size", s->hitsize, 0.5f, 30.0f);
    s->always = boolField(L, t, "always", s->always);
}

/* registry[XHAIR_DRAW_KEY][layer + 1] = fn (or nil) */
static void drawSlotSet(lua_State *L, int layer, int fnIdx)
{
    if (lua_getfield(L, LUA_REGISTRYINDEX, XHAIR_DRAW_KEY) != LUA_TTABLE)
    {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, LUA_REGISTRYINDEX, XHAIR_DRAW_KEY);
    }
    if (fnIdx > 0) lua_pushvalue(L, fnIdx);
    else lua_pushnil(L);
    lua_rawseti(L, -2, layer + 1);
    lua_pop(L, 1);
    sHasDraw[layer] = fnIdx > 0;
}

/* the style table at t into a layer (base, then each weapons entry over a copy of the base) */
static void applyStyle(lua_State *L, int t, int layer)
{
    PortXhairStyle base, per[XHAIR_MAX_WEAPON];
    int items[XHAIR_MAX_WEAPON], nw = 0;

    t = lua_absindex(L, t);
    portXhairDefaults(&base);
    parseStyle(L, t, &base);
    if (lua_getfield(L, t, "draw") == LUA_TFUNCTION)
    {
        base.hasdraw = 1;
        drawSlotSet(L, layer, lua_gettop(L));
    }
    else
    {
        drawSlotSet(L, layer, 0);
    }
    lua_pop(L, 1);
    if (lua_getfield(L, t, "weapons") == LUA_TTABLE)
    {
        int wt = lua_gettop(L);

        lua_pushnil(L);
        while (lua_next(L, wt) != 0)
        {
            if (lua_type(L, -2) == LUA_TSTRING && lua_istable(L, -1) && nw < XHAIR_MAX_WEAPON)
            {
                char up[40];
                const char *n = lua_tostring(L, -2);
                size_t i;
                int id;

                if (strncmp(n, "item_", 5) == 0 || strncmp(n, "ITEM_", 5) == 0) n += 5;
                for (i = 0; n[i] != '\0' && i < sizeof(up) - 1; i++) up[i] = (char) ((n[i] >= 'a' && n[i] <= 'z') ? n[i] - 32 : n[i]);
                up[i] = '\0';
                id = portXhairItemId(up);
                if (id < 0) luaL_error(L, "crosshair: unknown weapon '%s' (names as weapon.items())", lua_tostring(L, -2));
                per[nw] = base;
                per[nw].hasdraw = 0;
                parseStyle(L, lua_gettop(L), &per[nw]);
                items[nw] = id;
                nw++;
            }
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    portXhairSet(layer, &base, nw, items, per);
}

/* ---- the pack ------------------------------------------------------------------------------------------------------------------ */

/* pushes the pack table (loading Data/Scripts/mods/crosshair_pack.lua the first time, or again with reload) or nil */
static int packPush(lua_State *L, int reload)
{
    char path[600];
    int top;

    if (!reload && lua_getfield(L, LUA_REGISTRYINDEX, XHAIR_PACK_KEY) == LUA_TTABLE) return 1;
    if (!reload) lua_pop(L, 1);
    top = lua_gettop(L);
    snprintf(path, sizeof(path), "%s/mods/crosshair_pack.lua", portLuaGameScriptDir());
    if (luaL_loadfilex(L, path, "t") != LUA_OK)
    {
        portConLogf("ge: crosshair: %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
        lua_pushnil(L);
        return 0;
    }
    if (portLuaVmCall(L, 0, 1) != 0 || !lua_istable(L, -1))
    {
        lua_settop(L, top);
        portConLogf("ge: crosshair: the pack did not return a table of styles\n");
        lua_pushnil(L);
        return 0;
    }
    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_REGISTRYINDEX, XHAIR_PACK_KEY);
    return 1;
}

/* the pack style called name onto the stack (nil when there is none); the pack is an array of { name = , ...style } */
static int packFind(lua_State *L, const char *name)
{
    int n, i;

    if (!packPush(L, 0)) return 0;
    n = (int) lua_rawlen(L, -1);
    for (i = 1; i <= n; i++)
    {
        lua_rawgeti(L, -1, i);
        if (lua_istable(L, -1))
        {
            lua_getfield(L, -1, "name");
            if (lua_type(L, -1) == LUA_TSTRING && strcmp(lua_tostring(L, -1), name) == 0)
            {
                lua_pop(L, 1);
                lua_remove(L, -2);
                return 1;
            }
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    lua_pushnil(L);
    return 0;
}

/* the player's style by pack name ("off" / "classic" / "" = none). 1 = done. Runs under the caller's pcall or a fresh one. */
static int useName(lua_State *L, const char *name)
{
    int top = lua_gettop(L);

    if (name == NULL || name[0] == '\0' || strcmp(name, "off") == 0 || strcmp(name, "classic") == 0)
    {
        portXhairClear(XHAIR_LAYER_USER);
        drawSlotSet(L, XHAIR_LAYER_USER, 0);
        sUserName[0] = '\0';
        return 1;
    }
    if (!packFind(L, name))
    {
        lua_settop(L, top);
        return 0;
    }
    applyStyle(L, -1, XHAIR_LAYER_USER);
    snprintf(sUserName, sizeof(sUserName), "%s", name);
    lua_settop(L, top);
    return 1;
}

static int pUseProtected(lua_State *L)
{
    lua_pushboolean(L, useName(L, lua_tostring(L, 1)));
    return 1;
}

/* from C (the console, GE_CROSSHAIR): a protected useName */
static int useFromC(const char *name)
{
    lua_State *L;
    int ok = 0;

    if (portNetActive())
    {
        portConPrintf(0, "crosshair: pick a style before netplay starts (it loads a script)\n");
        return 0;
    }
    if (!portLuaGameAllowed()) return 0;
    L = portLuaVmState(1);
    if (L == NULL) return 0;
    lua_pushcfunction(L, pUseProtected);
    lua_pushstring(L, name);
    if (portLuaVmCall(L, 1, 1) == 0)
    {
        ok = lua_toboolean(L, -1);
        lua_pop(L, 1);
    }
    return ok;
}

/* ---- crosshair.* -------------------------------------------------------------------------------------------------------------- */

static int xStyle(lua_State *L)
{
    if (lua_isnoneornil(L, 1) || (lua_type(L, 1) == LUA_TBOOLEAN && !lua_toboolean(L, 1)))
    {
        portXhairClear(XHAIR_LAYER_SCRIPT);
        drawSlotSet(L, XHAIR_LAYER_SCRIPT, 0);
        return 0;
    }
    if (lua_type(L, 1) == LUA_TSTRING)
    {   /* a pack style by name, as the script's style */
        const char *n = lua_tostring(L, 1);

        if (!packFind(L, n)) return luaL_error(L, "crosshair: no style '%s' in the pack", n);
        applyStyle(L, -1, XHAIR_LAYER_SCRIPT);
        return 0;
    }
    luaL_checktype(L, 1, LUA_TTABLE);
    applyStyle(L, 1, XHAIR_LAYER_SCRIPT);
    return 0;
}

static int xHide(lua_State *L) { portXhairHide(lua_isnoneornil(L, 1) ? 1 : lua_toboolean(L, 1)); return 0; }
static int xHidden(lua_State *L) { lua_pushboolean(L, portXhairHidden()); return 1; }

static int xReset(lua_State *L)
{
    portXhairClear(XHAIR_LAYER_SCRIPT);
    drawSlotSet(L, XHAIR_LAYER_SCRIPT, 0);
    portXhairHide(0);
    return 0;
}

static int xUse(lua_State *L)
{
    lua_pushboolean(L, useName(L, luaL_optstring(L, 1, "off")));
    return 1;
}

static int xList(lua_State *L)
{
    int n, i, k = 0;

    lua_newtable(L);
    if (!packPush(L, 0)) { lua_pop(L, 1); return 1; }
    n = (int) lua_rawlen(L, -1);
    for (i = 1; i <= n; i++)
    {
        if (lua_rawgeti(L, -1, i) == LUA_TTABLE)
        {
            if (lua_getfield(L, -1, "name") == LUA_TSTRING) lua_rawseti(L, -4, ++k);
            else lua_pop(L, 1);
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    return 1;
}

static int xShapes(lua_State *L)
{
    int k;

    lua_newtable(L);
    for (k = 0; k < XHAIR_SHAPE_COUNT; k++)
    {
        lua_pushstring(L, kShapes[k]);
        lua_rawseti(L, -2, k + 1);
    }
    return 1;
}

static int xCurrent(lua_State *L)
{
    int layer = portXhairIsSet(XHAIR_LAYER_SCRIPT) ? XHAIR_LAYER_SCRIPT : (portXhairIsSet(XHAIR_LAYER_USER) ? XHAIR_LAYER_USER : -1);
    int sh = layer < 0 ? XHAIR_CLASSIC : portXhairShapeOf(layer);

    lua_pushstring(L, (sh >= 0 && sh < XHAIR_SHAPE_COUNT) ? kShapes[sh] : "classic");
    if (layer < 0) lua_pushnil(L);
    else lua_pushstring(L, layer == XHAIR_LAYER_SCRIPT ? "script" : "player");
    return 2;
}

static int xSpread(lua_State *L) { lua_pushnumber(L, portXhairSpreadNow()); return 1; }
static int xDraws(lua_State *L) { lua_pushinteger(L, portXhairDrawCount()); return 1; }
static int xTestHit(lua_State *L) { portXhairTestHit(lua_toboolean(L, 1)); return 0; }

/* ---- the draw context ----------------------------------------------------------------------------------------------------------- */

static int argBase(lua_State *L) { return lua_istable(L, 1) ? 2 : 1; }   /* ctx.line(...) and ctx:line(...) */

static int cLine(lua_State *L)
{
    int b = argBase(L);

    portXhairCmdLine((float) luaL_checknumber(L, b), (float) luaL_checknumber(L, b + 1), (float) luaL_checknumber(L, b + 2),
                     (float) luaL_checknumber(L, b + 3), (float) luaL_optnumber(L, b + 4, 0.75), colourAt(L, b + 5, 0xFFFFFFE6u));
    return 0;
}

static int cRect(lua_State *L)
{
    int b = argBase(L);

    portXhairCmdRect((float) luaL_checknumber(L, b), (float) luaL_checknumber(L, b + 1), (float) luaL_checknumber(L, b + 2),
                     (float) luaL_checknumber(L, b + 3), colourAt(L, b + 4, 0xFFFFFFE6u));
    return 0;
}

static int cCircle(lua_State *L)
{
    int b = argBase(L);

    portXhairCmdCircle((float) luaL_checknumber(L, b), (float) luaL_checknumber(L, b + 1), (float) luaL_checknumber(L, b + 2),
                       (float) luaL_optnumber(L, b + 3, 0.75), colourAt(L, b + 4, 0xFFFFFFE6u));
    return 0;
}

static int cDot(lua_State *L)
{
    int b = argBase(L);

    portXhairCmdCircle((float) luaL_checknumber(L, b), (float) luaL_checknumber(L, b + 1), (float) luaL_optnumber(L, b + 2, 1.0) * 0.5f,
                       0.0f, colourAt(L, b + 3, 0xFFFFFFE6u));
    return 0;
}

static void ctxPush(lua_State *L)
{
    if (lua_getfield(L, LUA_REGISTRYINDEX, XHAIR_CTX_KEY) == LUA_TTABLE) return;
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushcfunction(L, cLine); lua_setfield(L, -2, "line");
    lua_pushcfunction(L, cRect); lua_setfield(L, -2, "rect");
    lua_pushcfunction(L, cCircle); lua_setfield(L, -2, "circle");
    lua_pushcfunction(L, cDot); lua_setfield(L, -2, "dot");
    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_REGISTRYINDEX, XHAIR_CTX_KEY);
}

int portLuaXhairHooks(void)
{
    return portLuaVmState(0) != NULL && (sOnHooked || sHasDraw[0] || sHasDraw[1]);
}

/* calls fn (at -1) with x, y, ctx (at ctx); an error clears *slot's function (handlers table at tidx, index i) */
static int runOne(lua_State *L, const PortXhairFrame *f, int ctx)
{
    lua_pushnumber(L, f->x);
    lua_pushnumber(L, f->y);
    lua_pushvalue(L, ctx);
    return portLuaVmCall(L, 3, 0);
}

void portLuaXhairRun(const PortXhairFrame *f, int layer)
{
    lua_State *L = portLuaVmState(0);
    int top, ctx, n, i;

    if (L == NULL) return;
    top = lua_gettop(L);
    ctxPush(L);
    ctx = lua_gettop(L);
    lua_pushnumber(L, f->spread); lua_setfield(L, ctx, "spread");
    if (f->sincehit >= 0.0f) lua_pushnumber(L, f->sincehit);
    else lua_pushnil(L);
    lua_setfield(L, ctx, "since_hit");
    lua_pushboolean(L, f->headshot); lua_setfield(L, ctx, "headshot");
    lua_pushboolean(L, f->aiming); lua_setfield(L, ctx, "aiming");
    lua_pushinteger(L, f->weapon); lua_setfield(L, ctx, "weapon");
    lua_pushinteger(L, f->player); lua_setfield(L, ctx, "player");
    lua_pushnumber(L, f->w); lua_setfield(L, ctx, "w");
    lua_pushnumber(L, f->h); lua_setfield(L, ctx, "h");

    if (layer >= 0 && layer < XHAIR_LAYERS && sHasDraw[layer])
    {
        lua_getfield(L, LUA_REGISTRYINDEX, XHAIR_DRAW_KEY);
        if (lua_istable(L, -1) && lua_rawgeti(L, -1, layer + 1) == LUA_TFUNCTION)
        {
            if (runOne(L, f, ctx) != 0)
            {
                portConLogf("ge: crosshair: the style's draw function was dropped after an error\n");
                drawSlotSet(L, layer, 0);
            }
        }
        lua_settop(L, ctx);
    }
    if (sOnHooked)
    {
        lua_getfield(L, LUA_REGISTRYINDEX, XHAIR_HANDLERS_KEY);
        if (lua_istable(L, -1) && lua_rawgeti(L, -1, XHAIR_EVENT) == LUA_TTABLE)
        {
            int ht = lua_gettop(L);

            n = (int) lua_rawlen(L, ht);
            for (i = 1; i <= n; i++)
            {
                if (lua_rawgeti(L, ht, i) != LUA_TFUNCTION) { lua_pop(L, 1); continue; }
                if (runOne(L, f, ctx) != 0)
                {
                    lua_pushboolean(L, 0);
                    lua_rawseti(L, ht, i);
                    portConLogf("ge: lua: handler %d of 'draw_crosshair' disabled after an error\n", i);
                }
            }
        }
    }
    lua_settop(L, top);
}

/* port_luavm.c portLuaOn: an on("draw_crosshair", fn) */
void portLuaXhairHooked(void) { sOnHooked = 1; }

/* port_luavm.c portLuaStageStart: the script's style and hide go; the player's stay. GE_CROSSHAIR=<name> picks the player's once. */
void portLuaXhairStageReset(void)
{
    static int envDone;
    lua_State *L = portLuaVmState(0);

    portXhairStageReset();
    sOnHooked = 0;
    sHasDraw[XHAIR_LAYER_SCRIPT] = 0;
    if (L != NULL) drawSlotSet(L, XHAIR_LAYER_SCRIPT, 0);
    if (!envDone && portLuaGameAllowed())
    {
        const char *e = getenv("GE_CROSSHAIR");

        envDone = 1;
        if (e != NULL && e[0] != '\0' && !useFromC(e)) portConLogf("ge: crosshair: GE_CROSSHAIR=%s: no such style in the pack\n", e);
    }
}

/* the console: crosshair [<name> | off | list | hide | show | reload] */
void portLuaXhairConsole(const char *a1, const char *a2)
{
    lua_State *L;
    int n, i;

    (void) a2;
    if (a1 != NULL && strcmp(a1, "hide") == 0) { portXhairHide(1); portConPrintf(0, "crosshair hidden\n"); return; }
    if (a1 != NULL && strcmp(a1, "show") == 0) { portXhairHide(0); portConPrintf(0, "crosshair shown\n"); return; }
    if (a1 != NULL && strcmp(a1, "list") != 0 && strcmp(a1, "reload") != 0)
    {
        if (useFromC(a1)) portConPrintf(0, "crosshair: %s\n", a1[0] != '\0' ? a1 : "off");
        else portConPrintf(0, "crosshair: no style '%s' (crosshair list)\n", a1);
        return;
    }
    if (portNetActive() || !portLuaGameAllowed() || (L = portLuaVmState(1)) == NULL)
    {
        portConPrintf(0, "crosshair: %s\n", sUserName[0] != '\0' ? sUserName : "classic");
        return;
    }
    if (!packPush(L, a1 != NULL && strcmp(a1, "reload") == 0))
    {
        lua_pop(L, 1);
        portConPrintf(0, "crosshair: no pack (Data/Scripts/mods/crosshair_pack.lua)\n");
        return;
    }
    n = (int) lua_rawlen(L, -1);
    portConPrintf(0, "crosshair: %s   styles:", sUserName[0] != '\0' ? sUserName : "classic");
    for (i = 1; i <= n; i++)
    {
        if (lua_rawgeti(L, -1, i) == LUA_TTABLE)
        {
            if (lua_getfield(L, -1, "name") == LUA_TSTRING) portConPrintf(0, " %s", lua_tostring(L, -1));
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
    }
    portConPrintf(0, "   (crosshair <name> | off | hide | show | reload)\n");
    lua_pop(L, 1);
}

static const luaL_Reg kXhairLib[] = {
    { "style", xStyle }, { "hide", xHide }, { "hidden", xHidden }, { "reset", xReset }, { "use", xUse }, { "list", xList },
    { "shapes", xShapes }, { "current", xCurrent }, { "spread", xSpread }, { NULL, NULL }
};
static const luaL_Reg kXhairTest[] = { { "crosshair_draws", xDraws }, { "crosshair_hit", xTestHit }, { NULL, NULL } };

void portLuaXhairRegister(lua_State *L)
{
    extern void portLuaAddTo(lua_State *L, const char *lib, const luaL_Reg *reg);

    luaL_newlib(L, kXhairLib);
    lua_setglobal(L, "crosshair");
    portLuaAddTo(L, "test", kXhairTest);
}
