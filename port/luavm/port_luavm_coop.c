/* port_luavm_coop.c - Lua bindings for arena co-op (drop A7, modding/arena.md "Drop A7 co-op"): the global `coop` table.
 * Game half: port/src/port_arenacoop.c. Everything is inert outside a mode (nil / false / 0).
 *
 *   coop.rules{ on=true, bleed=30, revive=3, radius=170, revive_health=0.35, grace=2, bleedout="wave"|"run"|"end", debug=false }
 *   coop.active() -> bool                       the rules are on in a two-Bond stage
 *   coop.state(p) -> "up"|"down"|"dead"|"out", secs_left, revive_frac     (secs_left / revive_frac only while down)
 *   coop.release(p) -> bool                     a bled-out Bond ("out") respawns beside his partner now
 *   coop.counts() -> downs, revives, bleedouts
 *   coop.bot(on) -> bool                        the co-op puppet Bond is driven by the arena partner bot (local co-op)
 *   coop.bot_goal(p, x, z) / coop.bot_goal(p)   a point the bot walks to (it forgets it on arrival); no point = clear
 *   coop.bot_info(p) -> { target=, act=, ammo=, weapon=, shots=, dry=, blocked= }   (dry: empty-gun clicks; blocked: ticks it held fire
 *                                                                                    because another Bond was in the line of fire)
 *   test hooks: test.coop_down(p) (put a Bond down now), test.coop_kill(p) (a real death, past the downed rule) */
#include <string.h>
#include "lua.h"
#include "lauxlib.h"
#include "../include/port_lua.h"
#include "../include/port_arenacoop.h"

static void needStage(lua_State *L, const char *api)
{
    if (!portLuaGameStageLive())
    {
        luaL_error(L, "%s needs a live stage (not available in the menus)", api);
    }
}

static float optField(lua_State *L, const char *k, float dflt)
{
    float v = dflt;

    lua_getfield(L, 1, k);
    if (!lua_isnil(L, -1))
    {
        v = (float) luaL_checknumber(L, -1);
    }
    lua_pop(L, 1);
    return v;
}

static int coopRules(lua_State *L)
{
    PortArenaCoopRules r;
    const char *b = "wave";

    needStage(L, "coop.rules");
    luaL_checktype(L, 1, LUA_TTABLE);
    memset(&r, 0, sizeof(r));
    lua_getfield(L, 1, "on");
    r.on = lua_isnil(L, -1) ? 1 : lua_toboolean(L, -1);
    lua_pop(L, 1);
    r.bleed = optField(L, "bleed", 30.0f);
    r.revive = optField(L, "revive", 3.0f);
    r.radius = optField(L, "radius", 170.0f);
    r.reviveHealth = optField(L, "revive_health", 0.35f);
    r.grace = optField(L, "grace", 2.0f);
    lua_getfield(L, 1, "bleedout");
    if (!lua_isnil(L, -1))
    {
        b = luaL_checkstring(L, -1);
    }
    r.bleedout = strcmp(b, "run") == 0 ? PORT_AC_BLEED_RUN : (strcmp(b, "end") == 0 ? PORT_AC_BLEED_END : PORT_AC_BLEED_WAVE);
    if (strcmp(b, "run") != 0 && strcmp(b, "end") != 0 && strcmp(b, "wave") != 0)
    {
        lua_pop(L, 1);
        return luaL_error(L, "coop.rules: bleedout must be \"wave\", \"run\" or \"end\"");
    }
    lua_pop(L, 1);
    lua_getfield(L, 1, "debug");
    r.debug = lua_toboolean(L, -1);
    lua_pop(L, 1);
    portLuaGameCoopRules(&r);
    return 0;
}

static int coopActive(lua_State *L)
{
    needStage(L, "coop.active");
    lua_pushboolean(L, portLuaGameCoopActive());
    return 1;
}

static int coopState(lua_State *L)
{
    float left, frac;
    int s;

    needStage(L, "coop.state");
    s = portLuaGameCoopState((int) luaL_checkinteger(L, 1), &left, &frac);
    switch (s)
    {
        case PORT_AC_UP:   lua_pushstring(L, "up"); return 1;
        case PORT_AC_DEAD: lua_pushstring(L, "dead"); return 1;
        case PORT_AC_OUT:  lua_pushstring(L, "out"); return 1;
        case PORT_AC_DOWN:
            lua_pushstring(L, "down");
            lua_pushnumber(L, left);
            lua_pushnumber(L, frac);
            return 3;
        default:
            lua_pushnil(L);
            return 1;
    }
}

static int coopDown(lua_State *L)
{
    needStage(L, "test.coop_down");
    lua_pushboolean(L, portLuaGameCoopForceDown((int) luaL_checkinteger(L, 1)));
    return 1;
}

static int coopRelease(lua_State *L)
{
    needStage(L, "coop.release");
    lua_pushboolean(L, portLuaGameCoopRelease((int) luaL_checkinteger(L, 1)));
    return 1;
}

static int coopKill(lua_State *L)
{
    needStage(L, "test.coop_kill");
    lua_pushboolean(L, portLuaGameCoopKill((int) luaL_checkinteger(L, 1)));
    return 1;
}

static int coopCounts(lua_State *L)
{
    int d, r, b;

    needStage(L, "coop.counts");
    portLuaGameCoopCounts(&d, &r, &b);
    lua_pushinteger(L, d);
    lua_pushinteger(L, r);
    lua_pushinteger(L, b);
    return 3;
}

static int coopBot(lua_State *L)
{
    needStage(L, "coop.bot");
    lua_pushboolean(L, portLuaGameCoopBot(lua_isnone(L, 1) ? 1 : lua_toboolean(L, 1)));
    return 1;
}

static int coopBotGoal(lua_State *L)
{
    int p = (int) luaL_checkinteger(L, 1);

    needStage(L, "coop.bot_goal");
    if (lua_isnoneornil(L, 2))
    {
        lua_pushboolean(L, portLuaGameCoopBotGoal(p, 0, 0.0f, 0.0f));
    }
    else
    {
        lua_pushboolean(L, portLuaGameCoopBotGoal(p, 1, (float) luaL_checknumber(L, 2), (float) luaL_checknumber(L, 3)));
    }
    return 1;
}

static int coopBotInfo(lua_State *L)
{
    int target, ammo, weapon, shots;
    const char *act;

    needStage(L, "coop.bot_info");
    if (!portLuaGameCoopBotInfo((int) luaL_checkinteger(L, 1), &target, &act, &ammo, &weapon, &shots))
    {
        lua_pushnil(L);
        return 1;
    }
    lua_createtable(L, 0, 5);
    lua_pushinteger(L, target);
    lua_setfield(L, -2, "target");
    lua_pushstring(L, act);
    lua_setfield(L, -2, "act");
    lua_pushinteger(L, ammo);
    lua_setfield(L, -2, "ammo");
    lua_pushinteger(L, weapon);
    lua_setfield(L, -2, "weapon");
    lua_pushinteger(L, shots);
    lua_setfield(L, -2, "shots");
    {
        int dry, blocked;

        if (portLuaGameCoopBotStats((int) luaL_checkinteger(L, 1), &dry, &blocked))
        {
            lua_pushinteger(L, dry);
            lua_setfield(L, -2, "dry");
            lua_pushinteger(L, blocked);
            lua_setfield(L, -2, "blocked");
        }
    }
    return 1;
}

void portLuaCoopRegister(lua_State *L)
{
    extern void portLuaAddTo(lua_State *L, const char *lib, const luaL_Reg *reg);   /* port_luavm.c */
    static const luaL_Reg reg[] = { { "rules", coopRules }, { "active", coopActive }, { "state", coopState },
        { "release", coopRelease }, { "counts", coopCounts }, { "bot", coopBot }, { "bot_goal", coopBotGoal },
        { "bot_info", coopBotInfo }, { NULL, NULL } };
    /* test hooks (modding/lua.md "The test table") */
    static const luaL_Reg testAdd[] = { { "coop_down", coopDown }, { "coop_kill", coopKill }, { NULL, NULL } };

    luaL_newlib(L, reg);
    lua_setglobal(L, "coop");
    portLuaAddTo(L, "test", testAdd);
}
