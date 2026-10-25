/* port_luavm_ai.c - Lua bindings for the hearing layer and guard awareness (designs/bank-heist.md section 18; port/src/port_noise.c,
 * port_ai.c). Registered from portLuaState via portLuaAiRegister. Owned by the ai stream.
 *
 *   noise.emit(x, y, z, radius, kind) -> guards that heard it      kind: "slap" "step" "run" "door" "shot" "thud" "glass" "coin" "alarm"
 *   noise.radius(kind) -> the built-in radius      noise.tune{slap = 700, portal = 0.6, door_factor = 0.25, wall = 0.1, ...}
 *   noise.factor(room_from, room_to) -> the hearing factor between two rooms (1 same room, x portal per open portal, x door per shut door)
 *   noise.last() -> {x, z, radius, kind, heard, count} | nil
 *   ai.enable(on) / ai.on()      ai.guard(chr [, kind])      ai.patrol(chr, {pad, pad, ...})
 *   ai.awareness(chr) -> state, level, lastx, lastz | nil    (state: 0 unaware, 1 suspicious, 2 searching, 3 alert, 4 combat)
 *   ai.state_name(state)      ai.info(chr) -> {state, name, level, sub, x, z, post_x, post_z, looks, down, heard_ago} | nil
 *   ai.tune{turn = 1.2, look = 2.5, search_time = 30, checkin = 25, backup = 2, weight_slap = 1.2, ...}
 *   ai.nearest_free(x, y, z [, range [, exclude]]) -> chr | nil      ai.send(chr, x, y, z [, run [, radio text]]) -> bool
 *   ai.backup(chr) -> answered    (a guard calls backup now, once; key-card carriers hold their posts and never answer)
 *   ai.bark(chr, kind, text)      ai.counts() -> {barks, searches, giveups, checkins, backups, cam_acts, cam_bodies}
 * Events (on(...)): noise(x, y, z, radius, kind, src), guard_bark(chr, kind, text). */
#include <string.h>
#include <stdio.h>
#include "lua.h"
#include "lauxlib.h"
#include "../include/port_lua.h"
#include "../include/port_event.h"

extern int portModeActive(void);
extern void portNoiseEmit(float x, float y, float z, float radius, int kind, int srcChr);
extern int portNoiseKindFind(const char *name);
extern const char *portNoiseKindName(int kind);
extern float portNoiseRadius(int kind);
extern int portNoiseTune(const char *name, float v);
extern float portNoiseRoomFactor(int from, int to);
extern int portNoiseLast(float *x, float *z, float *radius, int *kind, int *heard, int *count);
extern int portNoiseEventArgs(float *x, float *y, float *z, float *radius, int *kind, int *src);
extern int portAiEnable(int on);
extern int portAiOn(void);
extern int portAiGuardAdd(int chrnum, int kind);
extern int portAiPatrol(int chrnum, const int *pads, int n);
extern int portAiAwareness(int chrnum, int *state, float *level, float *lastx, float *lastz);
extern const char *portAiStateName(int st);
extern int portAiInfo(int chrnum, int *sub, const char **subName, float *postx, float *postz, int *looks, int *down, float *heardAgo);
extern int portAiTune(const char *name, float v);
extern float portAiFace(int chrnum);
extern int portAiNearestFree(float x, float y, float z, float range, int exclude);
extern int portAiSend(int chrnum, float x, float y, float z, int run, const char *text);
extern int portAiBark(int chrnum, const char *kind, const char *text);
extern int portAiBackup(int chrnum);
extern int portAiBarkArgs(int *chr, const char **kind, const char **text);
extern void portAiCounts(int *barks, int *searches, int *giveups, int *checkins, int *backups, int *camacts, int *cambodies);

#define EV_NOISE      PORT_EV_NOISE
#define EV_GUARD_BARK PORT_EV_GUARD_BARK

static void needStage(lua_State *L, const char *api)
{
    if (!portLuaGameStageLive()) luaL_error(L, "%s needs a live stage (not available in the menus)", api);
}

static int noiseEmit(lua_State *L)
{
    int kind;

    needStage(L, "noise.emit");
    kind = lua_type(L, 5) == LUA_TNUMBER ? (int) lua_tointeger(L, 5) : portNoiseKindFind(luaL_checkstring(L, 5));
    if (kind < 0) return luaL_error(L, "noise.emit: unknown kind '%s'", lua_tostring(L, 5));
    portNoiseEmit((float) luaL_checknumber(L, 1), (float) luaL_checknumber(L, 2), (float) luaL_checknumber(L, 3), (float) luaL_checknumber(L, 4),
                  kind, (int) luaL_optinteger(L, 6, -1));
    {
        float x, z, r;
        int k, heard, count;

        portNoiseLast(&x, &z, &r, &k, &heard, &count);
        lua_pushinteger(L, heard);
    }
    return 1;
}

static int noiseRadius(lua_State *L)
{
    int k = portNoiseKindFind(luaL_checkstring(L, 1));

    if (k < 0) lua_pushnil(L); else lua_pushnumber(L, portNoiseRadius(k));
    return 1;
}

/* name = value pairs of a table through fn; returns how many were taken */
static int tuneTable(lua_State *L, int (*fn)(const char *, float))
{
    int n = 0;

    luaL_checktype(L, 1, LUA_TTABLE);
    lua_pushnil(L);
    while (lua_next(L, 1) != 0)
    {
        if (lua_type(L, -2) == LUA_TSTRING && lua_isnumber(L, -1)) n += fn(lua_tostring(L, -2), (float) lua_tonumber(L, -1));
        lua_pop(L, 1);
    }
    lua_pushinteger(L, n);
    return 1;
}

static int noiseTune(lua_State *L) { return tuneTable(L, portNoiseTune); }
static int aiTune(lua_State *L) { return tuneTable(L, portAiTune); }

static int noiseFactor(lua_State *L)
{
    needStage(L, "noise.factor");
    lua_pushnumber(L, portNoiseRoomFactor((int) luaL_checkinteger(L, 1), (int) luaL_checkinteger(L, 2)));
    return 1;
}

static int noiseLast(lua_State *L)
{
    float x, z, r;
    int k, heard, count;

    if (!portNoiseLast(&x, &z, &r, &k, &heard, &count)) { lua_pushnil(L); return 1; }
    lua_createtable(L, 0, 6);
    lua_pushnumber(L, x); lua_setfield(L, -2, "x");
    lua_pushnumber(L, z); lua_setfield(L, -2, "z");
    lua_pushnumber(L, r); lua_setfield(L, -2, "radius");
    lua_pushstring(L, portNoiseKindName(k)); lua_setfield(L, -2, "kind");
    lua_pushinteger(L, heard); lua_setfield(L, -2, "heard");
    lua_pushinteger(L, count); lua_setfield(L, -2, "count");
    return 1;
}

static int aiEnable(lua_State *L) { portAiEnable(lua_toboolean(L, 1)); return 0; }
static int aiIsOn(lua_State *L) { lua_pushboolean(L, portAiOn()); return 1; }

static int aiGuard(lua_State *L)
{
    needStage(L, "ai.guard");
    lua_pushboolean(L, portAiGuardAdd((int) luaL_checkinteger(L, 1), (int) luaL_optinteger(L, 2, 0)));
    return 1;
}

static int aiPatrol(lua_State *L)
{
    int pads[16], n = 0, i;

    needStage(L, "ai.patrol");
    luaL_checktype(L, 2, LUA_TTABLE);
    for (i = 1; i <= 16; i++)
    {
        lua_rawgeti(L, 2, i);
        if (lua_isnil(L, -1)) { lua_pop(L, 1); break; }
        pads[n++] = (int) lua_tointeger(L, -1);
        lua_pop(L, 1);
    }
    lua_pushboolean(L, portAiPatrol((int) luaL_checkinteger(L, 1), pads, n));
    return 1;
}

static int aiAwareness(lua_State *L)
{
    int st;
    float lvl, x, z;

    if (!portAiAwareness((int) luaL_checkinteger(L, 1), &st, &lvl, &x, &z)) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, st);
    lua_pushnumber(L, lvl);
    lua_pushnumber(L, x);
    lua_pushnumber(L, z);
    return 4;
}

static int aiStateName(lua_State *L) { lua_pushstring(L, portAiStateName((int) luaL_checkinteger(L, 1))); return 1; }

static int aiInfo(lua_State *L)
{
    int c = (int) luaL_checkinteger(L, 1), st, sub, looks, down;
    float lvl, x, z, px, pz, ago;
    const char *sn;

    if (!portAiAwareness(c, &st, &lvl, &x, &z) || !portAiInfo(c, &sub, &sn, &px, &pz, &looks, &down, &ago)) { lua_pushnil(L); return 1; }
    lua_createtable(L, 0, 12);
    lua_pushinteger(L, st); lua_setfield(L, -2, "state");
    lua_pushstring(L, portAiStateName(st)); lua_setfield(L, -2, "name");
    lua_pushnumber(L, lvl); lua_setfield(L, -2, "level");
    lua_pushstring(L, sn); lua_setfield(L, -2, "sub");
    lua_pushnumber(L, x); lua_setfield(L, -2, "x");
    lua_pushnumber(L, z); lua_setfield(L, -2, "z");
    lua_pushnumber(L, px); lua_setfield(L, -2, "post_x");
    lua_pushnumber(L, pz); lua_setfield(L, -2, "post_z");
    lua_pushinteger(L, looks); lua_setfield(L, -2, "looks");
    lua_pushboolean(L, down); lua_setfield(L, -2, "down");
    lua_pushnumber(L, ago); lua_setfield(L, -2, "heard_ago");
    lua_pushnumber(L, portAiFace(c)); lua_setfield(L, -2, "face");
    return 1;
}

static int aiNearestFree(lua_State *L)
{
    int c;

    needStage(L, "ai.nearest_free");
    c = portAiNearestFree((float) luaL_checknumber(L, 1), (float) luaL_checknumber(L, 2), (float) luaL_checknumber(L, 3),
                          (float) luaL_optnumber(L, 4, 0.0), (int) luaL_optinteger(L, 5, -1));
    if (c < 0) lua_pushnil(L); else lua_pushinteger(L, c);
    return 1;
}

static int aiSend(lua_State *L)
{
    needStage(L, "ai.send");
    lua_pushboolean(L, portAiSend((int) luaL_checkinteger(L, 1), (float) luaL_checknumber(L, 2), (float) luaL_checknumber(L, 3),
                                  (float) luaL_checknumber(L, 4), lua_toboolean(L, 5), luaL_optstring(L, 6, "")));
    return 1;
}

static int aiBackup(lua_State *L)
{
    needStage(L, "ai.backup");
    lua_pushinteger(L, portAiBackup((int) luaL_checkinteger(L, 1)));
    return 1;
}

static int aiBark(lua_State *L)
{
    lua_pushboolean(L, portAiBark((int) luaL_checkinteger(L, 1), luaL_checkstring(L, 2), luaL_optstring(L, 3, "")));
    return 1;
}

static int aiCounts(lua_State *L)
{
    int b, s, g, c, k, a, d;

    portAiCounts(&b, &s, &g, &c, &k, &a, &d);
    lua_createtable(L, 0, 7);
    lua_pushinteger(L, b); lua_setfield(L, -2, "barks");
    lua_pushinteger(L, s); lua_setfield(L, -2, "searches");
    lua_pushinteger(L, g); lua_setfield(L, -2, "giveups");
    lua_pushinteger(L, c); lua_setfield(L, -2, "checkins");
    lua_pushinteger(L, k); lua_setfield(L, -2, "backups");
    lua_pushinteger(L, a); lua_setfield(L, -2, "cam_acts");
    lua_pushinteger(L, d); lua_setfield(L, -2, "cam_bodies");
    return 1;
}

/* port_luavm.c portLuaDispatch: the arguments of the two events whose payload is not four integers; -1 = not one of them */
int portLuaAiPushArgs(lua_State *L, int type)
{
    if (type == EV_NOISE)
    {
        float x, y, z, r;
        int k, src;

        if (!portNoiseEventArgs(&x, &y, &z, &r, &k, &src)) { x = y = z = r = 0.0f; k = 0; src = -1; }
        lua_pushnumber(L, x);
        lua_pushnumber(L, y);
        lua_pushnumber(L, z);
        lua_pushnumber(L, r);
        lua_pushstring(L, portNoiseKindName(k));
        lua_pushinteger(L, src);
        return 6;
    }
    if (type == EV_GUARD_BARK)
    {
        int chr;
        const char *kind, *text;

        if (!portAiBarkArgs(&chr, &kind, &text)) { chr = -1; kind = "huh"; text = ""; }
        lua_pushinteger(L, chr);
        lua_pushstring(L, kind);
        lua_pushstring(L, text);
        return 3;
    }
    return -1;
}

void portLuaAiRegister(lua_State *L)
{
    static const luaL_Reg noiseLib[] = { { "emit", noiseEmit }, { "radius", noiseRadius }, { "tune", noiseTune }, { "factor", noiseFactor },
        { "last", noiseLast }, { NULL, NULL } };
    static const luaL_Reg aiLib[] = { { "enable", aiEnable }, { "on", aiIsOn }, { "guard", aiGuard }, { "patrol", aiPatrol },
        { "awareness", aiAwareness }, { "state_name", aiStateName }, { "info", aiInfo }, { "tune", aiTune }, { "bark", aiBark },
        { "counts", aiCounts }, { "nearest_free", aiNearestFree }, { "send", aiSend }, { "backup", aiBackup }, { NULL, NULL } };

    luaL_newlib(L, noiseLib);
    lua_setglobal(L, "noise");
    luaL_newlib(L, aiLib);
    lua_setglobal(L, "ai");
}
