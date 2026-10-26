/* port_luavm_interlock.c - Lua bindings for door interlocks (port/src/port_interlock.c; tools/heist_interlock.lua; designs/bank-heist.md
 * section 29, modding/lua.md "Door interlocks"). Any mode may use them.
 *
 *   interlock.define{doors = {i, ...}, hold = secs, close = secs, fail = "safe" | "secure"} -> g | nil   2..4 doors, one open at a time
 *   interlock.clear(g)
 *   interlock.of(door) -> g, slot | nil
 *   interlock.get(g) -> {phase, doors, open, pending, last, by, hold, hold_len, lock, cycles, refused, violations, queued, fail} | nil
 *       phase: "ready" "cycle" "verifying" "lockdown" "nopower" (fail-secure, no power) "released" (fail-safe, no power);
 *       by: the chr that opened the cycle, or -2 someone else (a Bond, a script), -1 none
 *   interlock.lockdown(g, secs)              hold every door shut for secs (0 releases)
 *   interlock.override(door, secs)           that door ignores its group for secs (a picked override cylinder)
 *   interlock.label(door) -> text, rgba | nil   the AR word: INTERLOCK, WAIT, VERIFYING, LOCKDOWN, CYCLE, NO POWER, RELEASED, OVERRIDE */
#include <string.h>
#include "lua.h"
#include "lauxlib.h"
#include "../include/port_lua.h"

#define IL_DOORS 4
typedef struct
{
    int n, door[IL_DOORS], open[IL_DOORS], fail, phase, why, pending, lastOpen, cycleBy, cycles, refused, violations, queuedOpens;
    float holdLeft, lockLeft, holdLen;
} PortInterlockInfo;

extern int portInterlockDefine(const int *doors, int n, int holdTicks, int closeTicks, int fail);
extern int portInterlockClear(int g);
extern int portInterlockOf(int idx, int *slot);
extern int portInterlockLockdown(int g, int ticks);
extern int portInterlockOverride(int idx, int ticks);
extern int portInterlockGet(int g, PortInterlockInfo *o);
extern const char *portInterlockLabel(int idx, unsigned *rgba);

static void needStage(lua_State *L, const char *api)
{
    if (!portLuaGameStageLive()) luaL_error(L, "%s needs a live stage (not available in the menus)", api);
}

static int ticks(lua_Number secs) { return secs > 0 ? (int) (secs * 60.0 + 0.5) : 0; }

static int ilDefine(lua_State *L)
{
    int doors[IL_DOORS], n = 0, i, g;
    lua_Number hold, close;
    const char *fail;

    needStage(L, "interlock.define");
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_getfield(L, 1, "doors");
    luaL_checktype(L, -1, LUA_TTABLE);
    for (i = 1; i <= IL_DOORS + 1; i++)
    {
        lua_rawgeti(L, -1, i);
        if (lua_isnil(L, -1)) { lua_pop(L, 1); break; }
        if (n >= IL_DOORS) luaL_error(L, "interlock.define: at most %d doors", IL_DOORS);
        doors[n++] = (int) luaL_checkinteger(L, -1);
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    lua_getfield(L, 1, "hold"); hold = luaL_optnumber(L, -1, 3.0); lua_pop(L, 1);
    lua_getfield(L, 1, "close"); close = luaL_optnumber(L, -1, 4.0); lua_pop(L, 1);
    lua_getfield(L, 1, "fail"); fail = luaL_optstring(L, -1, "safe"); lua_pop(L, 1);
    g = portInterlockDefine(doors, n, ticks(hold), ticks(close), strcmp(fail, "secure") == 0);
    if (g < 0) lua_pushnil(L); else lua_pushinteger(L, g);
    return 1;
}

static int ilClear(lua_State *L) { lua_pushboolean(L, portInterlockClear((int) luaL_checkinteger(L, 1))); return 1; }

static int ilOf(lua_State *L)
{
    int slot = 0, g = portInterlockOf((int) luaL_checkinteger(L, 1), &slot);

    if (g < 0) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, g);
    lua_pushinteger(L, slot + 1);
    return 2;
}

static int ilLockdown(lua_State *L)
{
    lua_pushboolean(L, portInterlockLockdown((int) luaL_checkinteger(L, 1), ticks(luaL_optnumber(L, 2, 10.0))));
    return 1;
}

static int ilOverride(lua_State *L)
{
    lua_pushboolean(L, portInterlockOverride((int) luaL_checkinteger(L, 1), ticks(luaL_optnumber(L, 2, 10.0))));
    return 1;
}

static int ilGet(lua_State *L)
{
    static const char *const kPhase[] = { "ready", "cycle", "verifying", "lockdown", "nopower", "released" };
    PortInterlockInfo o;
    int i;

    if (!portInterlockGet((int) luaL_checkinteger(L, 1), &o)) { lua_pushnil(L); return 1; }
    lua_newtable(L);
    lua_pushstring(L, kPhase[o.phase >= 0 && o.phase <= 5 ? o.phase : 0]); lua_setfield(L, -2, "phase");
    lua_newtable(L);
    for (i = 0; i < o.n; i++) { lua_pushinteger(L, o.door[i]); lua_rawseti(L, -2, i + 1); }
    lua_setfield(L, -2, "doors");
    lua_newtable(L);
    for (i = 0; i < o.n; i++) { lua_pushboolean(L, o.open[i]); lua_rawseti(L, -2, i + 1); }
    lua_setfield(L, -2, "open");
    lua_pushinteger(L, o.pending); lua_setfield(L, -2, "pending");
    lua_pushinteger(L, o.lastOpen); lua_setfield(L, -2, "last");
    lua_pushinteger(L, o.cycleBy >= 1000 ? o.cycleBy - 1000 : o.cycleBy); lua_setfield(L, -2, "by");
    lua_pushnumber(L, o.holdLeft); lua_setfield(L, -2, "hold");
    lua_pushnumber(L, o.holdLen); lua_setfield(L, -2, "hold_len");
    lua_pushnumber(L, o.lockLeft); lua_setfield(L, -2, "lock");
    lua_pushinteger(L, o.cycles); lua_setfield(L, -2, "cycles");
    lua_pushinteger(L, o.refused); lua_setfield(L, -2, "refused");
    lua_pushinteger(L, o.violations); lua_setfield(L, -2, "violations");
    lua_pushinteger(L, o.queuedOpens); lua_setfield(L, -2, "queued");
    lua_pushstring(L, o.fail ? "secure" : "safe"); lua_setfield(L, -2, "fail");
    return 1;
}

static int ilLabel(lua_State *L)
{
    unsigned rgba = 0;
    const char *t = portInterlockLabel((int) luaL_checkinteger(L, 1), &rgba);

    if (t == NULL) { lua_pushnil(L); return 1; }
    lua_pushstring(L, t);
    lua_pushinteger(L, (lua_Integer) rgba);
    return 2;
}

void portLuaInterlockRegister(lua_State *L)
{
    static const luaL_Reg lib[] = { { "define", ilDefine }, { "clear", ilClear }, { "of", ilOf }, { "get", ilGet },
        { "lockdown", ilLockdown }, { "override", ilOverride }, { "label", ilLabel }, { NULL, NULL } };

    luaL_newlib(L, lib);
    lua_setglobal(L, "interlock");
}
