/* port_luavm_gadgets.c - Lua bindings for the heist's gadgets (tools/heist_gadgets.lua; the C side is port/src/port_gadgets.c;
 * designs/bank-heist.md section 21, modding/lua.md "Gadgets").
 *
 *   gadget.items({item, ...})                 the ITEM_* ids that are gadgets: in hand, their fire button is the script's, not the gun's
 *   gadget.fire(p) -> presses, held, ticks     fire presses since the last call (taken), whether it is held now, for how many ticks
 *   gadget.test_fire(p, ticks [, taps])       test hook: hold fire for `ticks` sim ticks, or give `taps` presses (one a tick)
 *   gadget.aim(p) -> yaw, pitch, x, y, z, feet Bond p's view (degrees: yaw is vv_theta, forward = (-sin, cos)) and eye / feet height
 *   gadget.reel(p, {{x,y,z}, ...}, secs, from_pad, to_pad) -> bool   carry Bond p along the feet points, set him down on to_pad
 *   gadget.reeling(p) -> bool, frac, done     on the line now, how far, reels finished this stage
 *   gadget.floor_point(p, yaw, dist) -> x, y, z where a throw along `yaw` lands (the floor walked from Bond, short of walls)
 *   gadget.chr_yaw(chr) -> degrees, y         where a chr faces (atan2(dx, dz)), and its height
 *   gadget.lift_key(p, chr) -> flags          the pickpocket: chr's keys go to Bond p (0: none)
 *   gadget.disguise(p [, kind])               0 none, 1 staff, 2 guard (the cover reads it)
 *   gadget.bar(p, frac [, rgba]) / gadget.marker(p, x, y, z | nil)   presentation: the work bar, the grapple point bracket
 *   gadget.xray(p, on)                        presentation: the thermal imager's boxes round every chr near, through walls */
#include <string.h>
#include <stdio.h>
#include "lua.h"
#include "lauxlib.h"
#include "../include/port_lua.h"

extern int portGadgetsSetItems(const int *items, int n);
extern int portGadgetsTakeFire(int p, int *held, int *heldTicks);
extern int portGadgetsTestFire(int p, int ticks, int taps);
extern int portGadgetsAim(int p, float *yaw, float *pitch, float *x, float *y, float *z, float *feet);
extern int portGadgetsReel(int p, const float *pts, int n, float secs, int from, int to);
extern int portGadgetsReeling(int p, float *frac, int *done);
extern int portGadgetsFloorPoint(int p, float yaw, float dist, float *ox, float *oy, float *oz);
extern int portGadgetsChrYaw(int chrnum, float *yaw, float *y);
extern unsigned portGadgetsLiftKey(int p, int chrnum);
extern int portGadgetsDisguise(int p, int kind);
extern void portGadgetsBar(int p, float frac, unsigned rgba);
extern void portGadgetsMarker(int p, int on, float x, float y, float z);
extern void portGadgetsXray(int p, int on);

static void needStage(lua_State *L, const char *api)
{
    if (!portLuaGameStageLive())
    {
        luaL_error(L, "%s needs a live stage (not available in the menus)", api);
    }
}

static int gItems(lua_State *L)
{
    int ids[16], n = 0, i;

    needStage(L, "gadget.items");
    luaL_checktype(L, 1, LUA_TTABLE);
    for (i = 1; i <= 16; i++)
    {
        lua_rawgeti(L, 1, i);
        if (lua_isnil(L, -1)) { lua_pop(L, 1); break; }
        ids[n++] = (int) luaL_checkinteger(L, -1);
        lua_pop(L, 1);
    }
    lua_pushinteger(L, portGadgetsSetItems(ids, n));
    return 1;
}

static int gFire(lua_State *L)
{
    int held = 0, ticks = 0, n;

    needStage(L, "gadget.fire");
    n = portGadgetsTakeFire((int) luaL_checkinteger(L, 1), &held, &ticks);
    lua_pushinteger(L, n);
    lua_pushboolean(L, held);
    lua_pushinteger(L, ticks);
    return 3;
}

static int gTestFire(lua_State *L)
{
    needStage(L, "gadget.test_fire");
    lua_pushboolean(L, portGadgetsTestFire((int) luaL_checkinteger(L, 1), (int) luaL_optinteger(L, 2, 0), (int) luaL_optinteger(L, 3, 0)));
    return 1;
}

static int gAim(lua_State *L)
{
    float yaw, pitch, x, y, z, feet;

    needStage(L, "gadget.aim");
    if (!portGadgetsAim((int) luaL_checkinteger(L, 1), &yaw, &pitch, &x, &y, &z, &feet)) return 0;
    lua_pushnumber(L, yaw);
    lua_pushnumber(L, pitch);
    lua_pushnumber(L, x);
    lua_pushnumber(L, y);
    lua_pushnumber(L, z);
    lua_pushnumber(L, feet);
    return 6;
}

static int gReel(lua_State *L)
{
    float pts[4 * 3];
    int n = 0, i, k;

    needStage(L, "gadget.reel");
    luaL_checktype(L, 2, LUA_TTABLE);
    for (i = 1; i <= 4; i++)
    {
        lua_rawgeti(L, 2, i);
        if (!lua_istable(L, -1)) { lua_pop(L, 1); break; }
        for (k = 0; k < 3; k++)
        {
            lua_rawgeti(L, -1, k + 1);
            pts[n * 3 + k] = (float) luaL_checknumber(L, -1);
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
        n++;
    }
    lua_pushboolean(L, portGadgetsReel((int) luaL_checkinteger(L, 1), pts, n, (float) luaL_checknumber(L, 3), (int) luaL_checkinteger(L, 4),
                                       (int) luaL_checkinteger(L, 5)));
    return 1;
}

static int gReeling(lua_State *L)
{
    float frac = 0.0f;
    int done = 0, on;

    needStage(L, "gadget.reeling");
    on = portGadgetsReeling((int) luaL_checkinteger(L, 1), &frac, &done);
    lua_pushboolean(L, on);
    lua_pushnumber(L, frac);
    lua_pushinteger(L, done);
    return 3;
}

static int gFloorPoint(lua_State *L)
{
    float x, y, z;

    needStage(L, "gadget.floor_point");
    if (!portGadgetsFloorPoint((int) luaL_checkinteger(L, 1), (float) luaL_checknumber(L, 2), (float) luaL_checknumber(L, 3), &x, &y, &z)) return 0;
    lua_pushnumber(L, x);
    lua_pushnumber(L, y);
    lua_pushnumber(L, z);
    return 3;
}

static int gChrYaw(lua_State *L)
{
    float yaw, y;

    needStage(L, "gadget.chr_yaw");
    if (!portGadgetsChrYaw((int) luaL_checkinteger(L, 1), &yaw, &y)) return 0;
    lua_pushnumber(L, yaw);
    lua_pushnumber(L, y);
    return 2;
}

static int gLiftKey(lua_State *L)
{
    needStage(L, "gadget.lift_key");
    lua_pushinteger(L, (lua_Integer) portGadgetsLiftKey((int) luaL_checkinteger(L, 1), (int) luaL_checkinteger(L, 2)));
    return 1;
}

static int gDisguise(lua_State *L)
{
    needStage(L, "gadget.disguise");
    lua_pushinteger(L, portGadgetsDisguise((int) luaL_checkinteger(L, 1), (int) luaL_optinteger(L, 2, -1)));
    return 1;
}

static int gBar(lua_State *L)
{
    portGadgetsBar((int) luaL_checkinteger(L, 1), lua_isnoneornil(L, 2) ? -1.0f : (float) luaL_checknumber(L, 2),
                   (unsigned) luaL_optinteger(L, 3, 0));
    return 0;
}

static int gMarker(lua_State *L)
{
    int p = (int) luaL_checkinteger(L, 1);

    if (lua_isnoneornil(L, 2)) portGadgetsMarker(p, 0, 0.0f, 0.0f, 0.0f);
    else portGadgetsMarker(p, 1, (float) luaL_checknumber(L, 2), (float) luaL_checknumber(L, 3), (float) luaL_checknumber(L, 4));
    return 0;
}

static int gXray(lua_State *L)
{
    portGadgetsXray((int) luaL_checkinteger(L, 1), lua_toboolean(L, 2));
    return 0;
}

void portLuaGadgetsRegister(lua_State *L)
{
    static const luaL_Reg lib[] = { { "items", gItems }, { "fire", gFire }, { "test_fire", gTestFire }, { "aim", gAim }, { "reel", gReel },
        { "reeling", gReeling }, { "floor_point", gFloorPoint }, { "chr_yaw", gChrYaw }, { "lift_key", gLiftKey }, { "disguise", gDisguise },
        { "bar", gBar }, { "marker", gMarker }, { "xray", gXray }, { NULL, NULL } };

    luaL_newlib(L, lib);
    lua_setglobal(L, "gadget");
}
