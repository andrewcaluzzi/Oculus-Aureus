/* port_luavm_traits.c - Lua bindings for the guard personalities (designs/bank-heist.md section 32; port/src/port_traits.c), added to
 * the ai table after port_luavm_ai.c registers it (portLuaTraitsRegister). Owned by the personalities stream.
 *
 *   ai.trait_enable(on) / ai.trait_on()      the layer (tools/heist_traits.lua turns it on with S.personalities)
 *   ai.trait(chr [, name]) -> name           a guard's trait ("none", "rookie", "veteran", "book", "bribable"); with a name: set it
 *   ai.traits() -> { {chr, trait, observed, seen_by, bribed, skips, clocked}, ... }   every traited guard, in deal order
 *   ai.trait_val(chr, field) -> number       hear, susp, turn, checkin, retreat, surrender, pads, backup, full (neutral when off)
 *   ai.trait_stations({pads})                the watchclock stations (a veteran may skip them)
 *   ai.trait_observe(chr, p)                 Bond p has had a look at him (a camera feed; direct sight is scanned in C)
 *   ai.trait_bribe(chr, p, cost) -> "ok" | "refused" | "no" | "already"
 *   ai.trait_stats() -> {skips, clocked, bribes, bribe_cost, radios, observed, rookie, veteran, book, bribable, none}
 *   ai.trait_tune{ skip = 0.5, radio_cooldown = 20, radio_range = 1600, acc_rookie = -40, acc_veteran = 10 } */
#include <string.h>
#include "lua.h"
#include "lauxlib.h"
#include "../include/port_lua.h"
#include "../include/port_traits.h"

extern int portTraitEnable(int on);
extern int portTraitFind(const char *name);
extern const char *portTraitName(int t);
extern int portTraitSet(int chrnum, int trait);
extern int portTraitCount(void);
extern int portTraitEntry(int i, int *chrnum, int *trait, int *obs, int *bribed, int *skips, int *clocked, int *skipPad);
extern void portTraitStats(int *skips, int *clocked, int *bribes, int *cost, int *radios, int *observed, int *dealt);
extern int portTraitStations(const int *pads, int n);
extern int portTraitObserve(int chrnum, int p);
extern int portTraitBribe(int chrnum, int p, int cost);
extern int portTraitTune(const char *name, float v);

static int trEnable(lua_State *L) { lua_pushboolean(L, portTraitEnable(lua_toboolean(L, 1))); return 1; }
static int trOn(lua_State *L) { lua_pushboolean(L, portTraitOn()); return 1; }

static int trTrait(lua_State *L)
{
    int chr = (int) luaL_checkinteger(L, 1);

    if (lua_isstring(L, 2))
    {
        int t = portTraitFind(lua_tostring(L, 2));

        if (t < 0) return luaL_error(L, "ai.trait: no trait '%s' (none, rookie, veteran, book, bribable)", lua_tostring(L, 2));
        if (!portLuaGameStageLive()) return luaL_error(L, "ai.trait needs a live stage");
        lua_pushboolean(L, portTraitSet(chr, t));
        return 1;
    }
    lua_pushstring(L, portTraitName(portTraitOf(chr)));
    return 1;
}

static int trTraits(lua_State *L)
{
    int i, n = portTraitCount(), k = 0;

    lua_createtable(L, n, 0);
    for (i = 0; i < n; i++)
    {
        int chr, t, obs, br, sk, cl, sp;

        if (!portTraitEntry(i, &chr, &t, &obs, &br, &sk, &cl, &sp)) continue;
        lua_createtable(L, 0, 8);
        if (sp >= 0) { lua_pushinteger(L, sp); lua_setfield(L, -2, "skip_pad"); }   /* a veteran's open gap (the station he skipped) */
        lua_pushinteger(L, chr); lua_setfield(L, -2, "chr");
        lua_pushstring(L, portTraitName(t)); lua_setfield(L, -2, "trait");
        lua_pushboolean(L, obs != 0); lua_setfield(L, -2, "observed");
        lua_pushinteger(L, obs); lua_setfield(L, -2, "seen_by");
        lua_pushboolean(L, br); lua_setfield(L, -2, "bribed");
        lua_pushinteger(L, sk); lua_setfield(L, -2, "skips");
        lua_pushinteger(L, cl); lua_setfield(L, -2, "clocked");
        lua_rawseti(L, -2, ++k);
    }
    return 1;
}

static int trVal(lua_State *L)
{
    static const char *const f[TV_N] = { "hear", "susp", "turn", "checkin", "retreat", "surrender", "pads", "backup", "full" };
    const char *name = luaL_checkstring(L, 2);
    int k;

    for (k = 0; k < TV_N; k++)
    {
        if (strcmp(name, f[k]) == 0) { lua_pushnumber(L, portTraitVal((int) luaL_checkinteger(L, 1), k)); return 1; }
    }
    return luaL_error(L, "ai.trait_val: no field '%s'", name);
}

static int trStations(lua_State *L)
{
    int pads[32], n = 0, i, len;

    luaL_checktype(L, 1, LUA_TTABLE);
    len = (int) lua_rawlen(L, 1);
    for (i = 1; i <= len && n < 32; i++)
    {
        lua_rawgeti(L, 1, i);
        if (lua_isinteger(L, -1)) pads[n++] = (int) lua_tointeger(L, -1);
        lua_pop(L, 1);
    }
    lua_pushinteger(L, portTraitStations(pads, n));
    return 1;
}

static int trObserve(lua_State *L)
{
    lua_pushboolean(L, portTraitObserve((int) luaL_checkinteger(L, 1), (int) luaL_optinteger(L, 2, 0)));
    return 1;
}

static int trBribe(lua_State *L)
{
    int r;

    if (!portLuaGameStageLive()) return luaL_error(L, "ai.trait_bribe needs a live stage");
    r = portTraitBribe((int) luaL_checkinteger(L, 1), (int) luaL_checkinteger(L, 2), (int) luaL_optinteger(L, 3, 0));
    lua_pushstring(L, r == 1 ? "ok" : (r == -1 ? "refused" : (r == 2 ? "already" : "no")));
    return 1;
}

static int trStats(lua_State *L)
{
    int sk, cl, br, cost, ra, ob, dealt[TRAIT_N], k;

    portTraitStats(&sk, &cl, &br, &cost, &ra, &ob, dealt);
    lua_createtable(L, 0, 12);
    lua_pushinteger(L, sk); lua_setfield(L, -2, "skips");
    lua_pushinteger(L, cl); lua_setfield(L, -2, "clocked");
    lua_pushinteger(L, br); lua_setfield(L, -2, "bribes");
    lua_pushinteger(L, cost); lua_setfield(L, -2, "bribe_cost");
    lua_pushinteger(L, ra); lua_setfield(L, -2, "radios");
    lua_pushinteger(L, ob); lua_setfield(L, -2, "observed");
    for (k = 0; k < TRAIT_N; k++) { lua_pushinteger(L, dealt[k]); lua_setfield(L, -2, portTraitName(k)); }
    return 1;
}

static int trTune(lua_State *L)
{
    int n = 0;

    luaL_checktype(L, 1, LUA_TTABLE);
    lua_pushnil(L);
    while (lua_next(L, 1) != 0)
    {
        if (lua_type(L, -2) == LUA_TSTRING && lua_isnumber(L, -1)) n += portTraitTune(lua_tostring(L, -2), (float) lua_tonumber(L, -1));
        lua_pop(L, 1);
    }
    lua_pushinteger(L, n);
    return 1;
}

void portLuaTraitsRegister(lua_State *L)
{
    static const luaL_Reg lib[] = { { "trait_enable", trEnable }, { "trait_on", trOn }, { "trait", trTrait }, { "traits", trTraits },
        { "trait_val", trVal }, { "trait_stations", trStations }, { "trait_observe", trObserve }, { "trait_bribe", trBribe },
        { "trait_stats", trStats }, { "trait_tune", trTune }, { NULL, NULL } };

    lua_getglobal(L, "ai");
    if (!lua_istable(L, -1))
    {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setglobal(L, "ai");
    }
    luaL_setfuncs(L, lib, 0);
    lua_pop(L, 1);
}
