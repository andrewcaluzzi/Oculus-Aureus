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
 *   spawn.ko(chr [, p]) / ko.is / ko.count / ko.slaps / ko.wake / ko.waker / ko.search(p) / ko.test_slap(p, chr)
 *   mode.slaps_to_ko([n]) / mode.guards_wake(on)   (knock-outs, job 5)
 *   bond.equip(p, item) / bond.weapon(p) / mission.difficulty()   (job 6; loot is spawn.item "goldbar"/"money", port_luavm_arena.c)
 *   obj.get(i) / obj.test_damage(i, amount [, p])   (any setup object; levelgen P2.2)
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

/* door.one_way(i [, false]) -> bool: is it a stock one-way door (it opens from its free side whatever the key)? With false the one-way
 * bits are cleared first (returns the state before), so only the key opens it from either side. nil = no such door. */
static int doorOneWay(lua_State *L)
{
    int r;

    needStage(L, "door.one_way");
    r = portHeistDoorOneWay((int) luaL_checkinteger(L, 1), !lua_isnoneornil(L, 2) && !lua_toboolean(L, 2));
    if (r < 0) lua_pushnil(L); else lua_pushboolean(L, r);
    return 1;
}

/* door.snap(i, open) -> bool: put the door (and its linked pair) fully open (true) or shut (false) now, even with a chr in the doorway
 * (stock: a blocked door just stops). nil = no such door. */
static int doorSnap(lua_State *L)
{
    int r;

    needStage(L, "door.snap");
    r = portHeistDoorSnap((int) luaL_checkinteger(L, 1), lua_toboolean(L, 2));
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

/* ---- guards and civilians (jobs 3-4) ---- */

/* civilian.state(chr) -> "idle" | "surrendered" | "cowering" | nil (not a civilian) */
static int civState(lua_State *L)
{
    static const char *const names[] = { "idle", "surrendered", "cowering", "knocked_out" };
    int s;

    needStage(L, "civilian.state");
    s = portHeistCivilianState((int) luaL_checkinteger(L, 1));
    if (s < 0 || s > 3) lua_pushnil(L); else lua_pushstring(L, names[s]);
    return 1;
}

static int civKilled(lua_State *L)
{
    needStage(L, "civilian.killed");
    lua_pushinteger(L, portHeistCivilianKilled());
    return 1;
}

/* civilian.tune{range=, aim=, threat=, cower=}: aim distance (units, 800), seconds aimed before it surrenders (0.5), seconds aimed while
 * surrendered before it drops what it carries (2), seconds it cowers after gunfire (4) */
static int civTune(lua_State *L)
{
    float v[4] = { 0, 0, 0, 0 };
    static const char *const keys[4] = { "range", "aim", "threat", "cower" };
    int i;

    luaL_checktype(L, 1, LUA_TTABLE);
    for (i = 0; i < 4; i++)
    {
        lua_getfield(L, 1, keys[i]);
        if (!lua_isnil(L, -1)) v[i] = (float) luaL_checknumber(L, -1);
        lua_pop(L, 1);
    }
    portHeistCivilianTune(v[0], v[1], v[2], v[3]);
    return 0;
}

static int spawnDown(lua_State *L)
{
    needStage(L, "spawn.down");
    lua_pushboolean(L, portHeistChrDown((int) luaL_checkinteger(L, 1)));
    return 1;
}

static int bodiesFound(lua_State *L)
{
    needStage(L, "bodies.found");
    lua_pushinteger(L, portHeistBodiesFound());
    return 1;
}

static int modeBodyCheck(lua_State *L)
{
    portHeistBodyCheck(lua_isnone(L, 1) ? 1 : lua_toboolean(L, 1));
    return 0;
}

/* bond.test_noise(p, noise): test helper, Bond p "fires" a shot of that loudness (alerts every chr within hearingscale * noise * 100 units) */
static int bondTestNoise(lua_State *L)
{
    needStage(L, "bond.test_noise");
    lua_pushboolean(L, portHeistTestNoise((int) luaL_checkinteger(L, 1), (float) luaL_checknumber(L, 2)));
    return 1;
}

/* spawn.heard(chr [, clear]) -> bool: the chr is in the "heard Bond" state (set by a shot within its hearing); nil = no such chr */
static int spawnHeard(lua_State *L)
{
    int r;

    needStage(L, "spawn.heard");
    r = portHeistChrHeard((int) luaL_checkinteger(L, 1), lua_toboolean(L, 2));
    if (r < 0) lua_pushnil(L); else lua_pushboolean(L, r);
    return 1;
}

/* bond.face(p, x, z): test helper, turn Bond p's view onto (x, z) */
static int bondFace(lua_State *L)
{
    needStage(L, "bond.face");
    lua_pushboolean(L, portHeistFaceBond((int) luaL_checkinteger(L, 1), (float) luaL_checknumber(L, 2), (float) luaL_checknumber(L, 3), 0.0f));
    return 1;
}

/* ---- knock-out (job 5) ---- */

/* spawn.ko(chr [, p]): knock a chr out now, as if Bond p's last slap had (p omitted = a script: the event's player is -1) */
static int spawnKo(lua_State *L)
{
    needStage(L, "spawn.ko");
    lua_pushboolean(L, portHeistKnockOut((int) luaL_checkinteger(L, 1), (int) luaL_optinteger(L, 2, -1)));
    return 1;
}

static int koIs(lua_State *L) { needStage(L, "ko.is"); lua_pushboolean(L, portHeistKoIs((int) luaL_checkinteger(L, 1))); return 1; }
static int koSlaps(lua_State *L) { needStage(L, "ko.slaps"); lua_pushinteger(L, portHeistKoSlaps((int) luaL_checkinteger(L, 1))); return 1; }
static int koWake(lua_State *L) { needStage(L, "ko.wake"); lua_pushboolean(L, portHeistKoWake((int) luaL_checkinteger(L, 1))); return 1; }

/* ko.count() -> knocked out now, knock-outs this stage, wakes this stage */
static int koCount(lua_State *L)
{
    int down, total, woken;

    needStage(L, "ko.count");
    portHeistKoCount(&down, &total, &woken);
    lua_pushinteger(L, down);
    lua_pushinteger(L, total);
    lua_pushinteger(L, woken);
    return 3;
}

/* ko.waker(chr) -> the guard walking over to wake it, or nil */
static int koWaker(lua_State *L)
{
    int g;

    needStage(L, "ko.waker");
    g = portHeistKoWaker((int) luaL_checkinteger(L, 1));
    if (g < 0) lua_pushnil(L); else lua_pushinteger(L, g);
    return 1;
}

/* ko.search(p) -> chr | nil: Bond p presses use where he stands (the use key does the same): the nearest knocked-out chr within reach drops what it carries */
static int koSearch(lua_State *L)
{
    int c;

    needStage(L, "ko.search");
    c = portHeistKoSearch((int) luaL_checkinteger(L, 1));
    if (c < 0) lua_pushnil(L); else lua_pushinteger(L, c);
    return 1;
}

/* ko.test_slap(p, chr): test hook, Bond p's slappers hit chr through the stock hit path -> true if the hit landed */
static int koTestSlap(lua_State *L)
{
    needStage(L, "ko.test_slap");
    lua_pushboolean(L, portHeistTestSlap((int) luaL_checkinteger(L, 1), (int) luaL_checkinteger(L, 2)));
    return 1;
}

/* mode.slaps_to_ko([n]) -> n: slaps that knock a chr out (0 = slaps are stock hits). Default 2 in a mode named heist*, 0 otherwise; reset each stage */
static int modeSlapsToKo(lua_State *L)
{
    if (!lua_isnoneornil(L, 1)) portHeistSetSlapsToKo((int) luaL_checkinteger(L, 1));
    lua_pushinteger(L, portHeistSlapsToKo());
    return 1;
}

/* mode.guards_wake(on): a guard who finds a knocked-out chr walks over and wakes it (default true) */
static int modeGuardsWake(lua_State *L)
{
    portHeistSetGuardsWake(lua_isnone(L, 1) ? 1 : lua_toboolean(L, 1));
    return 0;
}

/* mode.hold_fire([on]) -> the setting: guards hold a shot while a bank civilian is near their line of fire (the heist sets it) */
static int modeHoldFire(lua_State *L)
{
    if (!lua_isnone(L, 1)) portHeistSetHoldFire(lua_toboolean(L, 1));
    lua_pushboolean(L, portHeistHoldFireOn());
    return 1;
}

/* spawn.fire_stats(chr) -> fired, held: shots the chr fired and shots it held for a civilian in the line (tests) */
static int spawnFireStats(lua_State *L)
{
    int fired, held;

    portHeistFireStats((int) luaL_checkinteger(L, 1), &fired, &held);
    lua_pushinteger(L, fired);
    lua_pushinteger(L, held);
    return 2;
}

/* ---- job 6: weapon in hand, difficulty ---- */

/* bond.equip(p, item) -> bool: Bond p draws `item` into his right hand (the stock weapon switch; given first if he lacks it).
 * Names as bond.give takes them, plus "fist" / "unarmed". */
static int bondEquip(lua_State *L)
{
    char key[48];
    const char *name = luaL_checkstring(L, 2);
    size_t n = 0;
    int id;

    needStage(L, "bond.equip");
    for (; *name != '\0' && n < sizeof(key) - 1; name++)
    {
        key[n++] = (*name >= 'a' && *name <= 'z') ? (char) (*name - 32) : *name;
    }
    key[n] = '\0';
    id = portLuaGameItemId(key);
    if (id < 0) return luaL_error(L, "bond.equip: unknown item '%s'", luaL_checkstring(L, 2));
    lua_pushboolean(L, portHeistBondEquip((int) luaL_checkinteger(L, 1), id));
    return 1;
}

/* bond.weapon(p) -> ITEM_* id in his right hand (0 unarmed, 1 slappers, 5 PP7 silenced, 38 door decoder ...), nil no such Bond */
static int bondWeapon(lua_State *L)
{
    int w;

    needStage(L, "bond.weapon");
    w = portHeistBondWeapon((int) luaL_checkinteger(L, 1));
    if (w < 0) lua_pushnil(L); else lua_pushinteger(L, w);
    return 1;
}

/* mission.difficulty() -> 0 Agent, 1 Secret Agent, 2 00 Agent, 3 007 */
static int missionDifficulty(lua_State *L)
{
    needStage(L, "mission.difficulty");
    lua_pushinteger(L, portHeistDifficulty());
    return 1;
}

/* ---- obj (any setup object by index; levelgen P2.2) ---- */

/* obj.get(i) -> {idx, type, model, pad, live, room, x, y, z, health, damage, destroyed} | nil (type = PROPDEF_*: 1 door, 3 prop, 4 key,
 * 5 alarm, 6 cctv, 10 monitor, 43 safe ...) */
static int objGet(lua_State *L)
{
    PortHeistObj o;

    needStage(L, "obj.get");
    if (!portHeistObjGet((int) luaL_checkinteger(L, 1), &o))
    {
        lua_pushnil(L);
        return 1;
    }
    lua_createtable(L, 0, 12);
    setInt(L, "idx", o.idx);
    setInt(L, "type", o.type);
    setInt(L, "model", o.model);
    setInt(L, "pad", o.pad);
    setBool(L, "live", o.live);
    setInt(L, "room", o.room);
    setNum(L, "x", o.x); setNum(L, "y", o.y); setNum(L, "z", o.z);
    setNum(L, "health", o.health);
    setNum(L, "damage", o.damage);
    setBool(L, "destroyed", o.destroyed);
    return 1;
}

/* obj.test_damage(i, amount [, p]) -> true | nil: the stock damage path as Bond p's hit (shoot off a padlock, shoot out an alarm box) */
static int objTestDamage(lua_State *L)
{
    int r;

    needStage(L, "obj.test_damage");
    r = portHeistObjDamage((int) luaL_checkinteger(L, 1), (float) luaL_checknumber(L, 2), (int) luaL_optinteger(L, 3, 0));
    if (r < 0) lua_pushnil(L); else lua_pushboolean(L, 1);
    return 1;
}

/* ---- cover: the banker disguise; power; spawn.go (designs/bank-heist.md section 16) ---- */

static const char *const sCoverClass[] = { "public", "staff", "restricted", NULL };
static const char *const sMeterState[] = { "off", "unseen", "watched", "suspicious", "blown", "alert" };

/* cover.on([on]) -> the setting */
static int coverOn(lua_State *L)
{
    if (!lua_isnone(L, 1)) portHeistCoverSetOn(lua_toboolean(L, 1));
    lua_pushboolean(L, portHeistCoverIsOn());
    return 1;
}

/* cover.alert([on]) -> the setting: everyone alert (the alarm, a body found): the disguise protects nobody */
static int coverAlert(lua_State *L)
{
    if (!lua_isnone(L, 1)) portHeistCoverSetAlert(lua_toboolean(L, 1));
    lua_pushboolean(L, portHeistCoverAlert());
    return 1;
}

/* cover.guard(chr [, "security"]) -> bool: this guard judges the disguise */
static int coverGuard(lua_State *L)
{
    const char *role = luaL_optstring(L, 2, "guard");

    needStage(L, "cover.guard");
    lua_pushboolean(L, portHeistCoverGuard((int) luaL_checkinteger(L, 1), strcmp(role, "security") == 0 ? 1 : 0));
    return 1;
}

/* cover.room(room, "public" | "staff" | "restricted"); cover.room(room) -> the class */
static int coverRoom(lua_State *L)
{
    int room = (int) luaL_checkinteger(L, 1);

    if (!lua_isnoneornil(L, 2)) portHeistCoverRoom(room, luaL_checkoption(L, 2, NULL, sCoverClass));
    lua_pushstring(L, sCoverClass[portHeistCoverRoomClass(room)]);
    return 1;
}

/* cover.carrying(p, on): Bond p carries loot (a guard who sees him sees through him) */
static int coverCarrying(lua_State *L)
{
    portHeistCoverCarrying((int) luaL_checkinteger(L, 1), lua_toboolean(L, 2));
    return 0;
}

/* cover.tune{range=, cone=, armed=, restricted=, staff=, run=, run_speed=, run_range=, close=, close_time=, linger=, decay=, radio=, act=} */
static int coverTune(lua_State *L)
{
    static const char *const keys[] = { "range", "cone", "armed", "restricted", "staff", "run", "run_speed", "run_range", "close",
                                        "close_time", "linger", "decay", "radio", "act" };
    float v[14];
    int i;

    luaL_checktype(L, 1, LUA_TTABLE);
    for (i = 0; i < 14; i++)
    {
        lua_getfield(L, 1, keys[i]);
        v[i] = lua_isnil(L, -1) ? -1.0f : (float) luaL_checknumber(L, -1);
        lua_pop(L, 1);
    }
    portHeistCoverTune(v, 14);
    return 0;
}

/* cover.state(chr) -> {susp, witness, reported, sees, role, why} | nil: the guard's meter (masks by player bit) */
static int coverState(lua_State *L)
{
    float susp;
    int w, r, s, role, why;

    needStage(L, "cover.state");
    if (!portHeistCoverState((int) luaL_checkinteger(L, 1), &susp, &w, &r, &s, &role, &why))
    {
        lua_pushnil(L);
        return 1;
    }
    lua_createtable(L, 0, 6);
    setNum(L, "susp", susp);
    setInt(L, "witness", w);
    setInt(L, "reported", r);
    setInt(L, "sees", s);
    lua_pushstring(L, role ? "security" : "guard");
    lua_setfield(L, -2, "role");
    lua_pushstring(L, portHeistCoverWhyName(why));
    lua_setfield(L, -2, "why");
    return 1;
}

/* cover.suspicion(chr, p) -> 0..1 | nil */
static int coverSuspicion(lua_State *L)
{
    float s;

    needStage(L, "cover.suspicion");
    s = portHeistCoverSusp((int) luaL_checkinteger(L, 1), (int) luaL_checkinteger(L, 2));
    if (s < 0.0f) lua_pushnil(L); else lua_pushnumber(L, s);
    return 1;
}

/* cover.meter(p) -> state ("off" "unseen" "watched" "suspicious" "blown" "alert"), level 0..1, guard chr | nil */
static int coverMeter(lua_State *L)
{
    float level;
    int chr, st;

    needStage(L, "cover.meter");
    st = portHeistCoverMeter((int) luaL_checkinteger(L, 1), &level, &chr);
    lua_pushstring(L, sMeterState[st < 0 || st > 5 ? 0 : st]);
    lua_pushnumber(L, level);
    if (chr < 0) lua_pushnil(L); else lua_pushinteger(L, chr);
    return 3;
}

static int coverSeen(lua_State *L) { needStage(L, "cover.seen"); lua_pushboolean(L, portHeistCoverSeen((int) luaL_checkinteger(L, 1))); return 1; }
static int coverCount(lua_State *L) { lua_pushinteger(L, portHeistCoverCount()); return 1; }
static int coverSpeed(lua_State *L) { lua_pushnumber(L, portHeistCoverBondSpeed((int) luaL_checkinteger(L, 1))); return 1; }
static int coverWhyName(lua_State *L) { lua_pushstring(L, portHeistCoverWhyName((int) luaL_checkinteger(L, 1))); return 1; }

/* cover.blow(chr, p): that guard sees through Bond p now (a script); cover.test_act(p): test hook, Bond p slapped / robbed / fired */
static int coverBlow(lua_State *L)
{
    needStage(L, "cover.blow");
    lua_pushboolean(L, portHeistCoverBlow((int) luaL_checkinteger(L, 1), (int) luaL_checkinteger(L, 2)));
    return 1;
}

static int coverTestAct(lua_State *L)
{
    needStage(L, "cover.test_act");
    lua_pushboolean(L, portHeistCoverTestAct((int) luaL_checkinteger(L, 1)));
    return 1;
}

/* cover.radio(chr [, "relay"]): the radio sound at that guard (presentation: volume and pan from this machine's Bond) */
static int coverRadio(lua_State *L)
{
    const char *k = luaL_optstring(L, 2, "call");

    needStage(L, "cover.radio");
    lua_pushboolean(L, portHeistRadioSfx((int) luaL_checkinteger(L, 1), strcmp(k, "relay") == 0));
    return 1;
}

/* roles: a name or an array of names -> bits (registered on first use) */
static unsigned roleBits(lua_State *L, int i)
{
    unsigned bits = 0;
    int b;

    if (lua_type(L, i) == LUA_TSTRING)
    {
        b = portHeistRoleBit(lua_tostring(L, i));
        if (b < 0) luaL_error(L, "too many guard roles (32)");
        return 1u << b;
    }
    luaL_checktype(L, i, LUA_TTABLE);
    for (b = 1; ; b++)
    {
        int bit;

        if (lua_rawgeti(L, i, b) == LUA_TNIL) { lua_pop(L, 1); break; }
        bit = portHeistRoleBit(luaL_checkstring(L, -1));
        lua_pop(L, 1);
        if (bit < 0) luaL_error(L, "too many guard roles (32)");
        bits |= 1u << bit;
    }
    return bits;
}

/* spawn.role(chr, name | {names}): the chr's roles for the guard door rules */
static int spawnRole(lua_State *L)
{
    needStage(L, "spawn.role");
    lua_pushboolean(L, portHeistSetChrRoles((int) luaL_checkinteger(L, 1), roleBits(L, 2)));
    return 1;
}

/* door.guard_access(i, "any" | "never" | name | {names}): who among the guards may open door i (with mode.guard_doors on) */
static int doorGuardAccess(lua_State *L)
{
    int idx = (int) luaL_checkinteger(L, 1);
    int ok;

    needStage(L, "door.guard_access");
    if (lua_type(L, 2) == LUA_TSTRING && strcmp(lua_tostring(L, 2), "any") == 0) ok = portHeistDoorGuardAccess(idx, 0, 0);
    else if (lua_type(L, 2) == LUA_TSTRING && strcmp(lua_tostring(L, 2), "never") == 0) ok = portHeistDoorGuardAccess(idx, 1, 0);
    else ok = portHeistDoorGuardAccess(idx, 1, roleBits(L, 2));
    lua_pushboolean(L, ok);
    return 1;
}

/* door.guard_stats() -> refused, opened: rule checks that kept a guard out / let a role holder open a shut ruled door */
static int doorGuardStats(lua_State *L)
{
    int r, o;

    portHeistDoorStats(&r, &o);
    lua_pushinteger(L, r);
    lua_pushinteger(L, o);
    return 2;
}

/* mode.guard_doors([on]) -> the setting */
static int modeGuardDoors(lua_State *L)
{
    if (!lua_isnone(L, 1)) portHeistSetGuardDoors(lua_toboolean(L, 1));
    lua_pushboolean(L, portHeistGuardDoorsOn());
    return 1;
}

/* power.set(on) / power.on(): the mains (out: no alarm can be raised; the mode darkens the cameras) */
static int powerSet(lua_State *L) { needStage(L, "power.set"); portHeistPowerSet(lua_toboolean(L, 1)); return 0; }
static int powerIsOn(lua_State *L) { lua_pushboolean(L, portHeistPowerOn()); return 1; }

/* spawn.go(chr, pad [, run]) -> bool: the chr walks (runs) to the pad on the stock route; its list keeps running */
static int spawnGo(lua_State *L)
{
    needStage(L, "spawn.go");
    lua_pushboolean(L, portHeistChrGo((int) luaL_checkinteger(L, 1), (int) luaL_checkinteger(L, 2), lua_toboolean(L, 3)));
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
        { "lock", doorLock }, { "unlock", doorUnlock }, { "set_keyflags", doorSetKeyflags }, { "try", doorTry }, { "one_way", doorOneWay }, { "snap", doorSnap }, { NULL, NULL } };
    static const luaL_Reg alarmLib[] = { { "start", alarmStart }, { "stop", alarmStop }, { "active", alarmActive }, { NULL, NULL } };
    static const luaL_Reg cctvLib[] = { { "list", cctvList }, { "get", cctvGet }, { "disable", cctvDisable }, { "enable", cctvEnable },
        { "set_alarm_time", cctvSetAlarmTime }, { "alarm_on_spot", cctvAlarmOnSpot }, { NULL, NULL } };
    static const luaL_Reg spawnAdd[] = { { "key", spawnKey }, { "key_at", spawnKeyAt }, { "key_alive", spawnKeyAlive }, { "down", spawnDown }, { "heard", spawnHeard },
        { "ko", spawnKo }, { "fire_stats", spawnFireStats }, { "go", spawnGo }, { NULL, NULL } };
    static const luaL_Reg bondAdd[] = { { "has_key", bondHasKey }, { "keys", bondKeys }, { "test_gadget", bondTestGadget }, { "face", bondFace }, { "test_noise", bondTestNoise },
        { "equip", bondEquip }, { "weapon", bondWeapon }, { NULL, NULL } };
    static const luaL_Reg missionAdd[] = { { "difficulty", missionDifficulty }, { NULL, NULL } };
    static const luaL_Reg modeAdd[] = { { "per_player_keys", modePerPlayerKeys }, { "body_check", modeBodyCheck }, { "slaps_to_ko", modeSlapsToKo },
        { "guards_wake", modeGuardsWake }, { "hold_fire", modeHoldFire }, { NULL, NULL } };
    static const luaL_Reg koLib[] = { { "is", koIs }, { "count", koCount }, { "slaps", koSlaps }, { "wake", koWake }, { "waker", koWaker },
        { "search", koSearch }, { "test_slap", koTestSlap }, { NULL, NULL } };
    static const luaL_Reg civLib[] = { { "state", civState }, { "killed", civKilled }, { "tune", civTune }, { NULL, NULL } };
    static const luaL_Reg bodiesLib[] = { { "found", bodiesFound }, { NULL, NULL } };
    static const luaL_Reg hudAdd[] = { { "countdown", hudCountdown }, { "countdown_left", hudCountdownLeft }, { NULL, NULL } };
    static const luaL_Reg objLib[] = { { "get", objGet }, { "test_damage", objTestDamage }, { NULL, NULL } };
    static const luaL_Reg coverLib[] = { { "on", coverOn }, { "alert", coverAlert }, { "guard", coverGuard }, { "room", coverRoom },
        { "carrying", coverCarrying }, { "tune", coverTune }, { "state", coverState }, { "suspicion", coverSuspicion }, { "meter", coverMeter },
        { "seen", coverSeen }, { "count", coverCount }, { "speed", coverSpeed }, { "why_name", coverWhyName }, { "blow", coverBlow },
        { "test_act", coverTestAct }, { "radio", coverRadio }, { NULL, NULL } };
    static const luaL_Reg doorAdd[] = { { "guard_access", doorGuardAccess }, { "guard_stats", doorGuardStats }, { NULL, NULL } };
    static const luaL_Reg spawnAdd2[] = { { "role", spawnRole }, { NULL, NULL } };
    static const luaL_Reg modeAdd2[] = { { "guard_doors", modeGuardDoors }, { NULL, NULL } };
    static const luaL_Reg powerLib[] = { { "set", powerSet }, { "on", powerIsOn }, { NULL, NULL } };

    luaL_newlib(L, coverLib);
    lua_setglobal(L, "cover");
    luaL_newlib(L, powerLib);
    lua_setglobal(L, "power");
    luaL_newlib(L, doorLib);
    lua_setglobal(L, "door");
    luaL_newlib(L, objLib);
    lua_setglobal(L, "obj");
    luaL_newlib(L, alarmLib);
    lua_setglobal(L, "alarm");
    luaL_newlib(L, cctvLib);
    lua_setglobal(L, "cctv");
    luaL_newlib(L, civLib);
    lua_setglobal(L, "civilian");
    luaL_newlib(L, bodiesLib);
    lua_setglobal(L, "bodies");
    luaL_newlib(L, koLib);
    lua_setglobal(L, "ko");
    addTo(L, "spawn", spawnAdd);
    addTo(L, "spawn", spawnAdd2);
    addTo(L, "door", doorAdd);
    addTo(L, "mode", modeAdd2);
    addTo(L, "bond", bondAdd);
    addTo(L, "mode", modeAdd);
    addTo(L, "hud", hudAdd);
    addTo(L, "mission", missionAdd);
}
