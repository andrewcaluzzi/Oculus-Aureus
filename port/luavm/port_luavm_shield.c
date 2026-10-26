/* port_luavm_shield.c - Lua bindings for hostages and the human shield (port/src/port_shield.c; tools/heist_hostages.lua; designs/bank-heist.md
 * section 31). Any mode may use them.
 *
 *   shield.enable(on), shield.on() -> bool
 *   shield.tune{ dist =, reach =, speed =, drop =, line =, flank_after =, front =, flank = 0|1 }
 *   shield.held(p) -> chr | nil              the hostage Bond p holds
 *   shield.holder(chr) -> p | nil
 *   shield.grab(p [, chr]) -> bool           test hook: as a use press at nothing (chr: only that civilian)
 *   shield.release(p [, why]) -> bool        why: "use" "hurt" "two_hands" "weapon" "script" (default)
 *   shield.last(p) -> what, serial           what the last use press did: "none" "grab" "nogun" "front" "release"
 *   shield.info(chr) -> {by, held, secs, hurt, standoff, clean, takes, why} | nil   (a chr ever taken this stage)
 *   shield.list() -> { chr, ... }            every hostage taken this stage
 *   shield.standoff(chr) -> bool             mark the police stand-off used for it (true = newly)
 *   shield.face(chr) -> degrees              the way a chr faces (tests: stand behind one)
 *   shield.counts() -> {grabs, releases, refused, held_shots, clear_shots, drops, flanks, barks} */
#include <string.h>
#include "lua.h"
#include "lauxlib.h"
#include "../include/port_lua.h"

extern int portShieldEnable(int on);
extern int portShieldOn(void);
extern int portShieldTune(const char *name, float v);
extern int portShieldHeldBy(int p);
extern int portShieldHolder(int chrnum);
extern int portShieldTestGrab(int p, int chrnum);
extern int portShieldRelease(int p, const char *why);
extern int portShieldLast(int p, int *serial);
extern int portShieldInfo(int chrnum, int *by, int *held, float *secs, int *hurt, int *standoff, int *clean, int *takes, const char **why);
extern int portShieldRecords(int *chrnums, int max);
extern int portShieldStandoff(int chrnum);
extern void portShieldCounts(int *grabs, int *releases, int *refused, int *held, int *clear, int *drops, int *flanks, int *barks);

static int shEnable(lua_State *L) { lua_pushboolean(L, portShieldEnable(lua_toboolean(L, 1))); return 1; }
static int shOn(lua_State *L) { lua_pushboolean(L, portShieldOn()); return 1; }

static int shTune(lua_State *L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_pushnil(L);
    while (lua_next(L, 1) != 0)
    {
        if (lua_type(L, -2) == LUA_TSTRING && lua_isnumber(L, -1))
        {
            if (!portShieldTune(lua_tostring(L, -2), (float) lua_tonumber(L, -1)))
                luaL_error(L, "shield.tune: unknown key '%s'", lua_tostring(L, -2));
        }
        lua_pop(L, 1);
    }
    return 0;
}

static int shHeld(lua_State *L)
{
    int c = portShieldHeldBy((int) luaL_checkinteger(L, 1));

    if (c < 0) lua_pushnil(L); else lua_pushinteger(L, c);
    return 1;
}

static int shHolder(lua_State *L)
{
    int p = portShieldHolder((int) luaL_checkinteger(L, 1));

    if (p < 0) lua_pushnil(L); else lua_pushinteger(L, p);
    return 1;
}

static int shGrab(lua_State *L)
{
    lua_pushboolean(L, portShieldTestGrab((int) luaL_checkinteger(L, 1), (int) luaL_optinteger(L, 2, -1)));
    return 1;
}

static int shRelease(lua_State *L)
{
    lua_pushboolean(L, portShieldRelease((int) luaL_checkinteger(L, 1), luaL_optstring(L, 2, "script")));
    return 1;
}

static int shLast(lua_State *L)
{
    static const char *const kWhat[] = { "none", "grab", "nogun", "front", "nobody", "release" };
    int serial = 0, w = portShieldLast((int) luaL_checkinteger(L, 1), &serial);

    lua_pushstring(L, kWhat[w >= 0 && w <= 5 ? w : 0]);
    lua_pushinteger(L, serial);
    return 2;
}

static int shInfo(lua_State *L)
{
    int by, held, hurt, standoff, clean, takes;
    float secs;
    const char *why;

    if (!portShieldInfo((int) luaL_checkinteger(L, 1), &by, &held, &secs, &hurt, &standoff, &clean, &takes, &why)) { lua_pushnil(L); return 1; }
    lua_newtable(L);
    lua_pushinteger(L, by); lua_setfield(L, -2, "by");
    lua_pushboolean(L, held); lua_setfield(L, -2, "held");
    lua_pushnumber(L, secs); lua_setfield(L, -2, "secs");
    lua_pushboolean(L, hurt); lua_setfield(L, -2, "hurt");
    lua_pushboolean(L, standoff); lua_setfield(L, -2, "standoff");
    lua_pushboolean(L, clean); lua_setfield(L, -2, "clean");
    lua_pushinteger(L, takes); lua_setfield(L, -2, "takes");
    lua_pushstring(L, why); lua_setfield(L, -2, "why");
    return 1;
}

static int shList(lua_State *L)
{
    int c[32], n = portShieldRecords(c, 32), i;

    lua_newtable(L);
    for (i = 0; i < n; i++) { lua_pushinteger(L, c[i]); lua_rawseti(L, -2, i + 1); }
    return 1;
}

extern float portShieldFace(int chrnum);
static int shFace(lua_State *L) { lua_pushnumber(L, portShieldFace((int) luaL_checkinteger(L, 1))); return 1; }

static int shStandoff(lua_State *L) { lua_pushboolean(L, portShieldStandoff((int) luaL_checkinteger(L, 1))); return 1; }

static int shCounts(lua_State *L)
{
    int g, r, f, h, c, d, fl, b;

    portShieldCounts(&g, &r, &f, &h, &c, &d, &fl, &b);
    lua_newtable(L);
    lua_pushinteger(L, g); lua_setfield(L, -2, "grabs");
    lua_pushinteger(L, r); lua_setfield(L, -2, "releases");
    lua_pushinteger(L, f); lua_setfield(L, -2, "refused");
    lua_pushinteger(L, h); lua_setfield(L, -2, "held_shots");
    lua_pushinteger(L, c); lua_setfield(L, -2, "clear_shots");
    lua_pushinteger(L, d); lua_setfield(L, -2, "drops");
    lua_pushinteger(L, fl); lua_setfield(L, -2, "flanks");
    lua_pushinteger(L, b); lua_setfield(L, -2, "barks");
    return 1;
}

void portLuaShieldRegister(lua_State *L)
{
    static const luaL_Reg lib[] = { { "enable", shEnable }, { "on", shOn }, { "tune", shTune }, { "held", shHeld }, { "holder", shHolder },
        { "grab", shGrab }, { "release", shRelease }, { "last", shLast }, { "info", shInfo }, { "list", shList }, { "standoff", shStandoff },
        { "counts", shCounts }, { "face", shFace }, { NULL, NULL } };

    luaL_newlib(L, lib);
    lua_setglobal(L, "shield");
}
