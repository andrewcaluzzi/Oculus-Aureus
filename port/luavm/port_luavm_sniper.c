/* port_luavm_sniper.c - Lua bindings for the sniper duel's C side (tools/mode_sniper.lua; modding/sniper.md). On this branch only the fog.
 *
 *   sniper.fog([start_m [, full_m]]) -> start_m, full_m   the generated level's fog band in metres (no fog nearer than start, full at
 *                                            full; 0 = as far as the band reaches): set and/or read; nothing on a level without one
 *   (presentation)
 * The same function and name as the sniper v2 branch's, which has the rest of the table. */
#include "lua.h"
#include "lauxlib.h"
#include "../include/port_lua.h"

extern void portLevelFogMetres(float start, float full);
extern int portLevelFogGet(float *start, float *full);

static int snFog(lua_State *L)
{
    float a, b;

    if (!lua_isnoneornil(L, 1)) portLevelFogMetres((float) (luaL_checknumber(L, 1) * 100.0), (float) (luaL_optnumber(L, 2, 0) * 100.0));
    if (!portLevelFogGet(&a, &b)) return 0;
    lua_pushnumber(L, a / 100.0);
    lua_pushnumber(L, b / 100.0);
    return 2;
}

void portLuaSniperRegister(lua_State *L)
{
    static const luaL_Reg lib[] = { { "fog", snFog }, { NULL, NULL } };

    luaL_newlib(L, lib);
    lua_setglobal(L, "sniper");
}
