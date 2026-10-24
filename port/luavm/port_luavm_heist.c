/* port_luavm_heist.c - Lua bindings for the heist mode (modding/lua.md "Heist bindings"; designs/bank-heist.md P0 job 2).
 * Registered from portLuaState via portLuaHeistRegister; the C side is port/src/port_heist.c.
 *
 *   door.list() -> array of door indices      door.get(i) -> {idx,state,open,keyflags,x,y,z,room,live,autoclose,linked,cell,padlocked} | nil
 *   door.open(i) / door.close(i) -> true started, false nothing to do, nil no such door
 *   door.lock(i, flags) / door.unlock(i [, flags]) / door.set_keyflags(i, flags) -> bool
 *   spawn.key(chr, flags [, model]) -> handle | nil      spawn.key_at(pad, flags [, model]) -> handle | nil      spawn.key_alive(h) -> alive, held, x, z
 *   bond.has_key(p, flags) -> bool      bond.keys(p) -> flags      mode.per_player_keys([on])
 *   alarm.start() / alarm.stop() / alarm.active()
 *   cctv.list() / cctv.get(i) / cctv.disable(i) / cctv.enable(i) / cctv.set_alarm_time(seconds [, i]) / cctv.alarm_on_spot(on)
 *   hud.countdown(seconds | nil) / hud.countdown_left()
 * Door and camera indices are the setup-record indices the probe logs. */
#include <string.h>
#include <stdio.h>
#include "lua.h"
#include "lauxlib.h"
#include "../include/port_lua.h"

static void needStage(lua_State *L, const char *api)
{
    if (!portLuaGameStageLive())
    {
        luaL_error(L, "%s needs a live stage (not available in the menus)", api);
    }
}

static unsigned checkFlags(lua_State *L, int i)
{
    lua_Integer v = luaL_checkinteger(L, i);

    return (unsigned) v;
}

static int pushList(lua_State *L, int (*fn)(int *, int))
{
    static int ids[512];
    int i, n = fn(ids, 512);

    lua_createtable(L, n, 0);
    for (i = 0; i < n; i++)
    {
        lua_pushinteger(L, ids[i]);
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

static void setInt(lua_State *L, const char *k, lua_Integer v) { lua_pushinteger(L, v); lua_setfield(L, -2, k); }
static void setNum(lua_State *L, const char *k, double v) { lua_pushnumber(L, v); lua_setfield(L, -2, k); }
static void setBool(lua_State *L, const char *k, int v) { lua_pushboolean(L, v); lua_setfield(L, -2, k); }

/* ---- door ---- */

static int doorList(lua_State *L) { needStage(L, "door.list"); return pushList(L, portHeistDoorList); }

static int doorGet(lua_State *L)
{
    PortHeistDoor d;
    const char *st;

    needStage(L, "door.get");
    if (!portHeistDoorGet((int) luaL_checkinteger(L, 1), &d))
    {
        lua_pushnil(L);
        return 1;
    }
    /* DOORSTATE: 0 stationary, 1 opening, 2 closing, 3 waiting (open, counting down to close) */
    if (d.state == 1) st = "opening";
    else if (d.state == 2) st = "closing";
    else if (d.state == 3) st = "open";
    else st = d.open > 0.5f ? "open" : "closed";
    lua_createtable(L, 0, 14);
    setInt(L, "idx", d.idx);
    lua_pushstring(L, st);
    lua_setfield(L, -2, "state");
    setNum(L, "open", d.open);
    setInt(L, "keyflags", d.keyflags);
    setNum(L, "x", d.x); setNum(L, "y", d.y); setNum(L, "z", d.z);
    setInt(L, "room", d.room);
    setBool(L, "live", d.live);
    setInt(L, "autoclose", d.autoclose);
    setInt(L, "linked", d.linked);
    setBool(L, "cell", d.cell);
    setBool(L, "padlocked", d.padlocked);
    return 1;
}

static int doorMove(lua_State *L, int open)
{
    int r;

    needStage(L, open ? "door.open" : "door.close");
    r = portHeistDoorMove((int) luaL_checkinteger(L, 1), open);
    if (r < 0) lua_pushnil(L); else lua_pushboolean(L, r);
    return 1;
}

static int doorOpen(lua_State *L) { return doorMove(L, 1); }
static int doorClose(lua_State *L) { return doorMove(L, 0); }

static int doorLock(lua_State *L)
{
    needStage(L, "door.lock");
    lua_pushboolean(L, portHeistDoorKeyflags((int) luaL_checkinteger(L, 1), 0, checkFlags(L, 2)));
    return 1;
}

static int doorUnlock(lua_State *L)
{
    needStage(L, "door.unlock");
    lua_pushboolean(L, portHeistDoorKeyflags((int) luaL_checkinteger(L, 1), 1, lua_isnoneornil(L, 2) ? 0xFFFFFFFFu : checkFlags(L, 2)));
    return 1;
}

static int doorSetKeyflags(lua_State *L)
{
    needStage(L, "door.set_keyflags");
    lua_pushboolean(L, portHeistDoorKeyflags((int) luaL_checkinteger(L, 1), 2, checkFlags(L, 2)));
    return 1;
}

/* door.try(p, i): Bond p uses door i like the use key (stock key check against THAT Bond's keys): true moved, false locked, nil no such door/Bond */
static int doorTry(lua_State *L)
{
    int r;

    needStage(L, "door.try");
    r = portHeistTryDoor((int) luaL_checkinteger(L, 1), (int) luaL_checkinteger(L, 2));
    if (r < 0) lua_pushnil(L); else lua_pushboolean(L, r);
    return 1;
}

/* bond.test_gadget(p, name): test hook, fires gadget_used as if Bond p used that gadget (doordecoder, bombdefuser, datathief, safecracker, lockexploder) */
static int bondTestGadget(lua_State *L)
{
    needStage(L, "bond.test_gadget");
    lua_pushboolean(L, portHeistTestGadget((int) luaL_checkinteger(L, 1), luaL_checkstring(L, 2)));
    return 1;
}

/* ---- keys ---- */

static int spawnKeyCommon(lua_State *L, int isChr, const char *api)
{
    int h;

    needStage(L, api);
    h = portHeistSpawnKey((int) luaL_checkinteger(L, 1), isChr, checkFlags(L, 2), (int) luaL_optinteger(L, 3, 0));
    if (h > 0) lua_pushinteger(L, h); else lua_pushnil(L);
    return 1;
}

static int spawnKey(lua_State *L) { return spawnKeyCommon(L, 1, "spawn.key"); }
static int spawnKeyAt(lua_State *L) { return spawnKeyCommon(L, 0, "spawn.key_at"); }

/* spawn.key_alive(h) -> alive, held_by_a_chr, x, z */
static int spawnKeyAlive(lua_State *L)
{
    float x = 0, z = 0;
    int held = 0;

    needStage(L, "spawn.key_alive");
    if (!portHeistKeyState((int) luaL_checkinteger(L, 1), &x, &z, &held))
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, 1);
    lua_pushboolean(L, held);
    lua_pushnumber(L, x);
    lua_pushnumber(L, z);
    return 4;
}

static int bondHasKey(lua_State *L)
{
    unsigned want = checkFlags(L, 2);

    needStage(L, "bond.has_key");
    lua_pushboolean(L, (portHeistBondKeys((int) luaL_checkinteger(L, 1)) & want) == want);
    return 1;
}

static int bondKeys(lua_State *L)
{
    needStage(L, "bond.keys");
    lua_pushinteger(L, portHeistBondKeys((int) luaL_checkinteger(L, 1)));
    return 1;
}

/* mode.per_player_keys([on = true]): in co-op a key only opens doors for the Bond who picked it up (stock pools every Bond's keys).
 * Call at script top level or in stage_start; reset at each stage start. */
static int modePerPlayerKeys(lua_State *L)
{
    portHeistSetPerPlayerKeys(lua_isnoneornil(L, 1) ? 1 : lua_toboolean(L, 1));
    return 0;
}

/* ---- alarm ---- */

static int alarmStart(lua_State *L) { needStage(L, "alarm.start"); portHeistAlarm(1); return 0; }
static int alarmStop(lua_State *L) { needStage(L, "alarm.stop"); portHeistAlarm(0); return 0; }
static int alarmActive(lua_State *L) { needStage(L, "alarm.active"); lua_pushboolean(L, portHeistAlarmActive()); return 1; }

/* ---- cctv ---- */

static int cctvList(lua_State *L) { needStage(L, "cctv.list"); return pushList(L, portHeistCctvList); }

static int cctvGet(lua_State *L)
{
    PortHeistCctv c;

    needStage(L, "cctv.get");
    if (!portHeistCctvGet((int) luaL_checkinteger(L, 1), &c))
    {
        lua_pushnil(L);
        return 1;
    }
    lua_createtable(L, 0, 12);
    setInt(L, "idx", c.idx);
    setNum(L, "x", c.x); setNum(L, "y", c.y); setNum(L, "z", c.z);
    setInt(L, "room", c.room);
    setNum(L, "face", c.face);
    setNum(L, "timer", c.timer / 60.0);                 /* seconds of unbroken sight so far */
    setNum(L, "alarm_time", c.alarm_ticks > 0 ? c.alarm_ticks / 60.0 : 0.0);   /* 0 = stock (10 s Agent, 5 s above) */
    setBool(L, "disabled", c.disabled);                 /* off, by a script or shot */
    setBool(L, "destroyed", c.destroyed);               /* shot: enable() cannot revive it */
    setBool(L, "live", c.live);
    setInt(L, "lookpad", c.lookpad);
    return 1;
}

static int cctvEnableCommon(lua_State *L, int on)
{
    int r;

    needStage(L, on ? "cctv.enable" : "cctv.disable");
    r = portHeistCctvEnable((int) luaL_checkinteger(L, 1), on);
    if (r < 0) lua_pushnil(L); else lua_pushboolean(L, r);
    return 1;
}

static int cctvDisable(lua_State *L) { return cctvEnableCommon(L, 0); }
static int cctvEnable(lua_State *L) { return cctvEnableCommon(L, 1); }

/* cctv.set_alarm_time(seconds [, camera]): seconds of unbroken sight before a camera fires; 0 or nil restores the stock time */
static int cctvSetAlarmTime(lua_State *L)
{
    int r;

    needStage(L, "cctv.set_alarm_time");
    r = portHeistCctvSetAlarmTime(lua_isnoneornil(L, 2) ? -1 : (int) luaL_checkinteger(L, 2), (float) luaL_optnumber(L, 1, 0.0));
    if (r < 0) lua_pushnil(L); else lua_pushboolean(L, r);
    return 1;
}

/* cctv.alarm_on_spot(on): false = a camera that spots a Bond only fires cctv_spotted, the script decides what happens (default true) */
static int cctvAlarmOnSpot(lua_State *L)
{
    needStage(L, "cctv.alarm_on_spot");
    portHeistCctvAlarmOnSpot(lua_toboolean(L, 1));
    return 0;
}

/* ---- hud ---- */

static int hudCountdown(lua_State *L)
{
    needStage(L, "hud.countdown");
    if (lua_isnoneornil(L, 1) || (lua_isboolean(L, 1) && !lua_toboolean(L, 1)))
    {
        portHeistCountdown(-1.0f);
    }
    else
    {
        float s = (float) luaL_checknumber(L, 1);

        portHeistCountdown(s < 0.0f ? 0.0f : s);
    }
    return 0;
}

static int hudCountdownLeft(lua_State *L)
{
    float s;

    needStage(L, "hud.countdown_left");
    s = portHeistCountdownLeft();
    if (s < 0.0f) lua_pushnil(L); else lua_pushnumber(L, s);
    return 1;
}

static void addTo(lua_State *L, const char *lib, const luaL_Reg *reg)
{
    lua_getglobal(L, lib);
    luaL_setfuncs(L, reg, 0);
    lua_pop(L, 1);
}

void portLuaHeistRegister(lua_State *L)
{
    static const luaL_Reg doorLib[] = { { "list", doorList }, { "get", doorGet }, { "open", doorOpen }, { "close", doorClose },
        { "lock", doorLock }, { "unlock", doorUnlock }, { "set_keyflags", doorSetKeyflags }, { "try", doorTry }, { NULL, NULL } };
    static const luaL_Reg alarmLib[] = { { "start", alarmStart }, { "stop", alarmStop }, { "active", alarmActive }, { NULL, NULL } };
    static const luaL_Reg cctvLib[] = { { "list", cctvList }, { "get", cctvGet }, { "disable", cctvDisable }, { "enable", cctvEnable },
        { "set_alarm_time", cctvSetAlarmTime }, { "alarm_on_spot", cctvAlarmOnSpot }, { NULL, NULL } };
    static const luaL_Reg spawnAdd[] = { { "key", spawnKey }, { "key_at", spawnKeyAt }, { "key_alive", spawnKeyAlive }, { NULL, NULL } };
    static const luaL_Reg bondAdd[] = { { "has_key", bondHasKey }, { "keys", bondKeys }, { "test_gadget", bondTestGadget }, { NULL, NULL } };
    static const luaL_Reg modeAdd[] = { { "per_player_keys", modePerPlayerKeys }, { NULL, NULL } };
    static const luaL_Reg hudAdd[] = { { "countdown", hudCountdown }, { "countdown_left", hudCountdownLeft }, { NULL, NULL } };

    luaL_newlib(L, doorLib);
    lua_setglobal(L, "door");
    luaL_newlib(L, alarmLib);
    lua_setglobal(L, "alarm");
    luaL_newlib(L, cctvLib);
    lua_setglobal(L, "cctv");
    addTo(L, "spawn", spawnAdd);
    addTo(L, "bond", bondAdd);
    addTo(L, "mode", modeAdd);
    addTo(L, "hud", hudAdd);
}
