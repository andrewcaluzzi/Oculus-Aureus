/* port_luavm_combat.c - Lua bindings for the heist's combat layer (designs/bank-heist.md section 24; port/src/port_combat.c). Registered
 * from portLuaState via portLuaCombatRegister. Owned by the combat stream.
 *
 *   combat.enable(on) / combat.on()      combat.guard(chr [, kind])      combat.tune{react = 1.5, hide_min = 1.2, flank = 1, ...}
 *   combat.info(chr) -> {state, name, role, cover_x, cover_z, peek_x, peek_z, pair, peeks, covers, flanks, retreated} | nil
 *   combat.counts() -> {covers, peeks, flanks, retreats, moves, releases, barks, searches, fails}
 *   combat.los(chr [, p]) -> Bond p (default the nearest) has the line to chr      combat.under_fire(chr)      combat.pair(flanker, partner)
 *   combat.stage([n]) -> the heat stage (0 none, 1 police, 2 SWAT; the mode sets it, the hash keeps it) */
#include <string.h>
#include <stdio.h>
#include "lua.h"
#include "lauxlib.h"
#include "../include/port_lua.h"

extern int portCombatEnable(int on);
extern int portCombatOn(void);
extern int portCombatGuardAdd(int chrnum, int kind);
extern int portCombatTune(const char *name, float v);
extern const char *portCombatStateName(int st);
extern const char *portCombatRoleName(int r);
extern int portCombatInfo(int chrnum, int *st, int *role, float *cx, float *cz, float *px, float *pz, int *pair, int *peeks, int *covers,
                          int *flanks, int *retreated);
extern void portCombatCounts(int *covers, int *peeks, int *flanks, int *retreats, int *moves, int *releases, int *barks, int *searches,
                             int *fails);
extern int portCombatUnderFire(int chrnum);
extern int portCombatLos(int chrnum, int p);
extern int portCombatPair(int fchr, int schr);
extern int portCombatStage(int n);

static void needStage(lua_State *L, const char *api)
{
    if (!portLuaGameStageLive()) luaL_error(L, "%s needs a live stage (not available in the menus)", api);
}

static int cbEnable(lua_State *L) { lua_pushboolean(L, portCombatEnable(lua_toboolean(L, 1))); return 1; }
static int cbOn(lua_State *L) { lua_pushboolean(L, portCombatOn()); return 1; }

static int cbGuard(lua_State *L)
{
    needStage(L, "combat.guard");
    lua_pushboolean(L, portCombatGuardAdd((int) luaL_checkinteger(L, 1), (int) luaL_optinteger(L, 2, 0)));
    return 1;
}

static int cbTune(lua_State *L)
{
    int n = 0;

    luaL_checktype(L, 1, LUA_TTABLE);
    lua_pushnil(L);
    while (lua_next(L, 1) != 0)
    {
        if (lua_type(L, -2) == LUA_TSTRING)
        {
            if (lua_type(L, -1) == LUA_TBOOLEAN) n += portCombatTune(lua_tostring(L, -2), lua_toboolean(L, -1) ? 1.0f : 0.0f);
            else if (lua_isnumber(L, -1)) n += portCombatTune(lua_tostring(L, -2), (float) lua_tonumber(L, -1));
        }
        lua_pop(L, 1);
    }
    lua_pushinteger(L, n);
    return 1;
}

static int cbInfo(lua_State *L)
{
    int st, role, pair, peeks, covers, flanks, ret;
    float cx, cz, px, pz;

    if (!portCombatInfo((int) luaL_checkinteger(L, 1), &st, &role, &cx, &cz, &px, &pz, &pair, &peeks, &covers, &flanks, &ret))
    {
        lua_pushnil(L);
        return 1;
    }
    lua_createtable(L, 0, 12);
    lua_pushinteger(L, st); lua_setfield(L, -2, "state");
    lua_pushstring(L, portCombatStateName(st)); lua_setfield(L, -2, "name");
    lua_pushstring(L, portCombatRoleName(role)); lua_setfield(L, -2, "role");
    lua_pushnumber(L, cx); lua_setfield(L, -2, "cover_x");
    lua_pushnumber(L, cz); lua_setfield(L, -2, "cover_z");
    lua_pushnumber(L, px); lua_setfield(L, -2, "peek_x");
    lua_pushnumber(L, pz); lua_setfield(L, -2, "peek_z");
    if (pair >= 0) lua_pushinteger(L, pair); else lua_pushnil(L);
    lua_setfield(L, -2, "pair");
    lua_pushinteger(L, peeks); lua_setfield(L, -2, "peeks");
    lua_pushinteger(L, covers); lua_setfield(L, -2, "covers");
    lua_pushinteger(L, flanks); lua_setfield(L, -2, "flanks");
    lua_pushboolean(L, ret); lua_setfield(L, -2, "retreated");
    return 1;
}

static int cbCounts(lua_State *L)
{
    int v[9];
    static const char *const k[9] = { "covers", "peeks", "flanks", "retreats", "moves", "releases", "barks", "searches", "fails" };
    int i;

    portCombatCounts(&v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8]);
    lua_createtable(L, 0, 9);
    for (i = 0; i < 9; i++) { lua_pushinteger(L, v[i]); lua_setfield(L, -2, k[i]); }
    return 1;
}

static int cbLos(lua_State *L)
{
    needStage(L, "combat.los");
    lua_pushboolean(L, portCombatLos((int) luaL_checkinteger(L, 1), (int) luaL_optinteger(L, 2, -1)));
    return 1;
}

static int cbUnderFire(lua_State *L) { lua_pushboolean(L, portCombatUnderFire((int) luaL_checkinteger(L, 1))); return 1; }

static int cbPair(lua_State *L)
{
    needStage(L, "combat.pair");
    lua_pushboolean(L, portCombatPair((int) luaL_checkinteger(L, 1), (int) luaL_checkinteger(L, 2)));
    return 1;
}

static int cbStage(lua_State *L) { lua_pushinteger(L, portCombatStage((int) luaL_optinteger(L, 1, -1))); return 1; }

void portLuaCombatRegister(lua_State *L)
{
    static const luaL_Reg lib[] = { { "enable", cbEnable }, { "on", cbOn }, { "guard", cbGuard }, { "tune", cbTune }, { "info", cbInfo },
        { "counts", cbCounts }, { "los", cbLos }, { "under_fire", cbUnderFire }, { "pair", cbPair }, { "stage", cbStage }, { NULL, NULL } };

    luaL_newlib(L, lib);
    lua_setglobal(L, "combat");
}
