/* port_luavm_arena.c - Lua bindings added for the arena mode (modding/arena.md, drop A2).
 * Kept apart from port_luavm.c; portLuaArenaRegister adds the functions to the existing `spawn` and
 * `bond` tables (called once from portLuaState).
 *
 *   spawn.chr(pad, {skin=, weapon=, health=, accuracy=, speed=, behaviour=, knives=, face={x,z}}) -> chrnum | nil
 *   spawn.remove(chr) -> bool        spawn.alive(chr) -> bool        spawn.free() -> free chr slots
 *   spawn.skins() -> array of skin names        spawn.weapons() -> array of weapon names
 *   bond.give(player, item [, ammo]) -> bool    bond.heal(player, health, armour)    bond.vitals(player) -> health, armour
 *
 *   spawn.chr also takes body=/head= (name or id), armour= (stock chr armour, x the base 4 damage), anim_speed= (per-chr model play speed), sunglasses=
 *   spawn.bodies() -> body names, head names
 *   drop A5: spawn.chr also takes knife_range=, knife_cooldown= (seconds), sight= (visionrange, units), engage= (effective range, units).
 *   spawn.pools() -> loose, held, free weapon slots, free props;  spawn.trim_drops(max [, keep_radius]);  bond.give_ammo(player, frac)
 *   spawn.kill(chr [, "head"]) (a death through the damage path), bond.warp(player, pad [, dx, dz]);  test.take_drop(player [, radius])
 *   drop A6: spawn.item(pad, item [, {ammo=, amount=}]) -> handle | nil;  spawn.item_alive(h);  spawn.item_remove(h);  spawn.item_pos(h) -> x, z
 *   test hooks: test.walk(player, x, z) -> distance (real stick input toward x, z, every tick), test.walk_stop()
 * behaviour: "chase" (default), "rush", "hold", "snipe", "standard". health is a multiplier (1 = stock). */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "lua.h"
#include "lauxlib.h"
#include "../include/port_lua.h"
#include "../include/port_arena_a8.h"

static void needStage(lua_State *L, const char *api)
{
    if (!portLuaGameStageLive())
    {
        luaL_error(L, "%s needs a live stage (not available in the menus)", api);
    }
}

static const char *const sBehaviours[] = { "chase", "rush", "hold", "snipe", "standard", "spectator", "civilian", NULL };

static const char *const sWeaponNames[] = {
    "ak47", "shotgun", "pp7", "pp7sil", "d5k", "d5ksil", "ar33", "zmg", "dd44", "klobb", "phantom", "rcp90",
    "cougar", "autoshot", "goldengun", "sniper", "rocket", "grenadelauncher", "knife", "laser", "none", NULL };

static int arenaSpawnChr(lua_State *L)
{
    PortLuaChrSpec s;
    int pad = (int) luaL_checkinteger(L, 1), c;

    needStage(L, "spawn.chr");
    memset(&s, 0, sizeof(s));
    s.health = 1.0f;
    s.accuracy = -1;
    s.speed = -1;
    s.body = -1;
    s.head = -2;
    s.alarmPad = -1;
    s.patrol = -1;
    if (lua_istable(L, 2))
    {
        /* heist job 3: alarm_pad (stock alarm raiser), hearing (hearingscale), vision (= sight), post (hold, facing {x,z}), patrol (stock path id) */
        lua_getfield(L, 2, "alarm_pad");
        if (!lua_isnil(L, -1)) s.alarmPad = (int) luaL_checkinteger(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, 2, "hearing");
        if (!lua_isnil(L, -1)) s.hearing = (float) luaL_checknumber(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, 2, "vision");
        if (!lua_isnil(L, -1)) s.sight = (float) luaL_checknumber(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, 2, "patrol");
        if (!lua_isnil(L, -1)) s.patrol = (int) luaL_checkinteger(L, -1);
        lua_pop(L, 1);
        /* heist job 6: offset = {dx, dz} from the pad centre (several chrs on one pad) */
        lua_getfield(L, 2, "offset");
        if (lua_istable(L, -1))
        {
            lua_rawgeti(L, -1, 1);
            s.offx = (float) luaL_checknumber(L, -1);
            lua_pop(L, 1);
            lua_rawgeti(L, -1, 2);
            s.offz = (float) luaL_checknumber(L, -1);
            lua_pop(L, 1);
            s.hasoff = 1;
        }
        lua_pop(L, 1);
        lua_getfield(L, 2, "post");
        if (!lua_isnil(L, -1) && lua_toboolean(L, -1))
        {
            s.behaviour = PORTLUA_BEH_HOLD;   /* a behaviour given below wins */
            if (lua_istable(L, -1))
            {
                lua_rawgeti(L, -1, 1);
                s.fx = (float) luaL_checknumber(L, -1);
                lua_pop(L, 1);
                lua_rawgeti(L, -1, 2);
                s.fz = (float) luaL_checknumber(L, -1);
                lua_pop(L, 1);
                s.hasface = 1;
            }
        }
        lua_pop(L, 1);
        lua_getfield(L, 2, "skin");
        if (!lua_isnil(L, -1)) s.skin = luaL_checkstring(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, 2, "weapon");
        if (!lua_isnil(L, -1)) s.weapon = luaL_checkstring(L, -1);
        lua_pop(L, 1);
        /* body= / head= : a name (see spawn.bodies()) or a raw id; head "random" / "none" / nil = random (bodies with a built-in head ignore it) */
        lua_getfield(L, 2, "body");
        if (lua_type(L, -1) == LUA_TSTRING) s.bodyname = lua_tostring(L, -1);
        else if (!lua_isnil(L, -1)) s.body = (int) luaL_checkinteger(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, 2, "head");
        if (lua_type(L, -1) == LUA_TSTRING) s.headname = lua_tostring(L, -1);
        else if (!lua_isnil(L, -1)) s.head = (int) luaL_checkinteger(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, 2, "armour");
        if (!lua_isnil(L, -1)) s.armour = (float) luaL_checknumber(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, 2, "anim_speed");
        if (!lua_isnil(L, -1)) s.animspeed = (float) luaL_checknumber(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, 2, "sunglasses");
        s.sunglasses = lua_toboolean(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, 2, "knife_range");
        if (!lua_isnil(L, -1)) s.knifeRange = (float) luaL_checknumber(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, 2, "knife_cooldown");
        if (!lua_isnil(L, -1)) s.knifeCooldown = (float) luaL_checknumber(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, 2, "knife_keep");
        if (!lua_isnil(L, -1)) s.knifeKeep = (float) luaL_checknumber(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, 2, "sight");
        if (!lua_isnil(L, -1)) s.sight = (float) luaL_checknumber(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, 2, "engage");
        if (!lua_isnil(L, -1)) s.engage = (float) luaL_checknumber(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, 2, "health");
        if (!lua_isnil(L, -1)) s.health = (float) luaL_checknumber(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, 2, "accuracy");
        if (!lua_isnil(L, -1)) s.accuracy = (int) luaL_checknumber(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, 2, "speed");
        if (!lua_isnil(L, -1)) s.speed = (int) luaL_checknumber(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, 2, "behaviour");
        if (!lua_isnil(L, -1)) s.behaviour = luaL_checkoption(L, -1, "chase", sBehaviours);
        lua_pop(L, 1);
        lua_getfield(L, 2, "knives");
        s.knives = lua_toboolean(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, 2, "face");
        if (lua_istable(L, -1))
        {
            lua_rawgeti(L, -1, 1);
            s.fx = (float) luaL_checknumber(L, -1);
            lua_pop(L, 1);
            lua_rawgeti(L, -1, 2);
            s.fz = (float) luaL_checknumber(L, -1);
            lua_pop(L, 1);
            s.hasface = 1;
        }
        lua_pop(L, 1);
    }
    c = portLuaGameSpawnChr(pad, &s);
    if (c == -2) return luaL_error(L, "unknown skin '%s' (see spawn.skins())", s.skin);
    if (c == -3) return luaL_error(L, "unknown weapon '%s' (see spawn.weapons())", s.weapon);
    if (c == -4) return luaL_error(L, "unknown body '%s' (see spawn.bodies())", s.bodyname ? s.bodyname : "?");
    if (c == -5) return luaL_error(L, "unknown head '%s' (see spawn.bodies())", s.headname ? s.headname : "?");
    if (c < 0) lua_pushnil(L); else lua_pushinteger(L, c);
    return 1;
}

static int arenaSpawnRemove(lua_State *L)
{
    needStage(L, "spawn.remove");
    lua_pushboolean(L, portLuaGameChrRemove((int) luaL_checkinteger(L, 1)));
    return 1;
}

/* spawn.kill(chr [, "head"]) -> bool: lethal damage (a normal death: drops, events); "head" counts it as a headshot */
static int arenaSpawnKill(lua_State *L)
{
    const char *part = luaL_optstring(L, 2, "");

    needStage(L, "spawn.kill");
    lua_pushboolean(L, portLuaGameChrKill((int) luaL_checkinteger(L, 1), strcmp(part, "head") == 0));
    return 1;
}

/* spawn.headshot(chr) -> bool: the arena enemy's last damaging hit was to the head (ask on chr_died) */
static int arenaSpawnHeadshot(lua_State *L)
{
    needStage(L, "spawn.headshot");
    lua_pushboolean(L, portLuaGameChrHeadshot((int) luaL_checkinteger(L, 1)));
    return 1;
}

/* spawn.idle(chr) -> bool | nil: standing, or a one-shot animation has run out and holds the idle pose (nil when gone or dead) */
static int arenaSpawnIdle(lua_State *L)
{
    int r;

    needStage(L, "spawn.idle");
    r = portLuaGameChrIdle((int) luaL_checkinteger(L, 1));
    if (r < 0) return 0;
    lua_pushboolean(L, r);
    return 1;
}

/* spawn.keep_bodies(on): dead arena enemies stay (no stock fade-out and removal) until spawn.remove */
static int arenaSpawnKeepBodies(lua_State *L)
{
    extern void portLuaGameKeepBodies(int on);

    portLuaGameKeepBodies(lua_toboolean(L, 1));
    return 0;
}

/* spawn.pools() -> loose, held, free weapon slots, free props: the 30-slot weapon pool and the 600-prop pool */
static int arenaSpawnPools(lua_State *L)
{
    int loose, held, fw, fp;

    needStage(L, "spawn.pools");
    portLuaGamePools(&loose, &held, &fw, &fp);
    lua_pushinteger(L, loose);
    lua_pushinteger(L, held);
    lua_pushinteger(L, fw);
    lua_pushinteger(L, fp);
    return 4;
}

/* spawn.trim_drops(max [, keep_radius]) -> number freed: drop cap, oldest loose weapons first, none within keep_radius (400) of a Bond */
static int arenaSpawnTrimDrops(lua_State *L)
{
    needStage(L, "spawn.trim_drops");
    lua_pushinteger(L, portLuaGameTrimDrops((int) luaL_checkinteger(L, 1), (float) luaL_optnumber(L, 2, 400.0)));
    return 1;
}

/* bond.give_ammo(player, frac): ammo for the guns he carries (+frac of each type's max), no gun */
static int arenaBondGiveAmmo(lua_State *L)
{
    needStage(L, "bond.give_ammo");
    lua_pushinteger(L, portLuaGameGiveAmmo((int) luaL_checkinteger(L, 1), (float) luaL_checknumber(L, 2)));
    return 1;
}

/* test.take_drop(player [, radius]) -> item id (0 = none near), ammo of its type before, after: test helper, the real pickup */
static int arenaBondTakeDrop(lua_State *L)
{
    int before = 0, after = 0, item;

    needStage(L, "test.take_drop");
    item = portLuaGameTakeDrop((int) luaL_checkinteger(L, 1), (float) luaL_optnumber(L, 2, 400.0), &before, &after);
    lua_pushinteger(L, item);
    lua_pushinteger(L, before);
    lua_pushinteger(L, after);
    return 3;
}

/* bond.warp(player, pad [, dx, dz]) -> bool: Bond to a pad (plus an offset from its centre) */
static int arenaBondWarp(lua_State *L)
{
    needStage(L, "bond.warp");
    lua_pushboolean(L, portLuaGameBondWarp((int) luaL_checkinteger(L, 1), (int) luaL_checkinteger(L, 2), (float) luaL_optnumber(L, 3, 0.0), (float) luaL_optnumber(L, 4, 0.0)));
    return 1;
}

/* spawn.item(pad, item [, {ammo=, amount=}]) -> handle | nil. A real pickup on the floor at the pad, collected by walking over it.
 * item: a weapon name of spawn.weapons() (the pickup carries its ammo; ammo= sets the rounds, default the stock amount) or "armour"
 * (the stock MP body armour; amount= 0..1, default 1). Raises on an unknown item or pad; nil when there was no room. */
static int arenaSpawnItem(lua_State *L)
{
    int pad = (int) luaL_checkinteger(L, 1), ammo = -1, r;
    const char *what = luaL_checkstring(L, 2);
    float amount = 1.0f;
    const char *cnames[16];
    int crounds[16], ncrate = 0;

    needStage(L, "spawn.item");
    if (strcmp(what, "goldbar") == 0 || strcmp(what, "gold") == 0 || strcmp(what, "money") == 0)
    {   /* heist job 6: loot (port_heist.c), its own pool of 160; opts {offset = {dx, dz}} */
        float dx = 0.0f, dz = 0.0f;

        if (lua_istable(L, 3))
        {
            lua_getfield(L, 3, "offset");
            if (lua_istable(L, -1))
            {
                lua_rawgeti(L, -1, 1);
                dx = (float) luaL_checknumber(L, -1);
                lua_pop(L, 1);
                lua_rawgeti(L, -1, 2);
                dz = (float) luaL_checknumber(L, -1);
                lua_pop(L, 1);
            }
            lua_pop(L, 1);
        }
        r = portHeistLootSpawn(pad, what[0] == 'm' ? 1 : 0, dx, dz);
        if (r == -2) return luaL_error(L, "pad %d has no floor", pad);
        if (r <= 0) lua_pushnil(L); else lua_pushinteger(L, r);
        return 1;
    }
    if (lua_istable(L, 3))
    {
        lua_getfield(L, 3, "ammo");
        if (lua_istable(L, -1))
        {   /* an ammo box: {weaponname = rounds, ...} */
            lua_pushnil(L);
            while (lua_next(L, -2) && ncrate < 16)
            {
                if (lua_type(L, -2) == LUA_TSTRING)
                {
                    cnames[ncrate] = lua_tostring(L, -2);   /* the key stays referenced by the table */
                    crounds[ncrate++] = (int) luaL_checkinteger(L, -1);
                }
                lua_pop(L, 1);
            }
        }
        else if (!lua_isnil(L, -1)) ammo = (int) luaL_checkinteger(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, 3, "amount");
        if (!lua_isnil(L, -1)) amount = (float) luaL_checknumber(L, -1);
        lua_pop(L, 1);
    }
    r = portLuaGameItemSpawn(pad, what, ammo, amount, ncrate, cnames, crounds);
    if (r == -1) return luaL_error(L, "unknown item '%s' (a spawn.weapons() name, or \"armour\")", what);
    if (r == -2) return luaL_error(L, "pad %d has no floor", pad);
    if (r <= 0) lua_pushnil(L); else lua_pushinteger(L, r);
    return 1;
}

/* spawn.item_alive(h) -> bool: still lying on the floor (not collected, removed or recycled) */
static int arenaSpawnItemAlive(lua_State *L)
{
    int h = (int) luaL_checkinteger(L, 1);

    needStage(L, "spawn.item_alive");
    lua_pushboolean(L, h >= PORT_HEIST_LOOT_HANDLE ? portHeistLootAlive(h) : portLuaGameItemAlive(h));
    return 1;
}

/* spawn.item_remove(h) -> bool: take it off the floor */
static int arenaSpawnItemRemove(lua_State *L)
{
    int h = (int) luaL_checkinteger(L, 1);

    needStage(L, "spawn.item_remove");
    lua_pushboolean(L, h >= PORT_HEIST_LOOT_HANDLE ? portHeistLootRemove(h) : portLuaGameItemRemove(h));
    return 1;
}

/* drop A7: spawn.markers(on): pickup markers on screen (presentation only); spawn.marker_sync(): republish the marker list (call ~10x/s) */
static float optField(lua_State *L, int t, const char *k, float d)
{
    float v = d;

    lua_getfield(L, t, k);
    if (lua_isboolean(L, -1)) v = lua_toboolean(L, -1) ? 1.0f : 0.0f;
    else if (!lua_isnil(L, -1)) v = (float) luaL_checknumber(L, -1);
    lua_pop(L, 1);
    return v;
}

/* drop mode for markers v2: false / "off" = none, "near" = a dot close up, true / "always" = always a dot */
static int optDropMode(lua_State *L, int t, const char *k, int d)
{
    int v = d;

    lua_getfield(L, t, k);
    if (lua_isboolean(L, -1)) v = lua_toboolean(L, -1) ? 2 : 0;
    else if (lua_type(L, -1) == LUA_TSTRING)
    {
        const char *s = lua_tostring(L, -1);

        v = (strcmp(s, "near") == 0) ? 1 : ((strcmp(s, "always") == 0 || strcmp(s, "on") == 0) ? 2 : 0);
    }
    lua_pop(L, 1);
    return v;
}

/* spawn.markers(on [, {fade=, near=, drops=, knives=, drop_near=, merge=, edge=, max=, legacy=}]): drop A8 markers v2 (port_arenamarkers.h) */
static int arenaSpawnMarkers(lua_State *L)
{
    needStage(L, "spawn.markers");
    portLuaGameMarkers(lua_toboolean(L, 1));
    if (lua_istable(L, 2))
    {
        portLuaGameMarkerStyle(optField(L, 2, "fade", 8.0f), optField(L, 2, "near", 1500.0f), optDropMode(L, 2, "drops", 1),
                               optDropMode(L, 2, "knives", 0), optField(L, 2, "drop_near", 1000.0f), optField(L, 2, "merge", 26.0f),
                               optField(L, 2, "edge", 1.0f) != 0.0f, (int) optField(L, 2, "max", 10.0f), optField(L, 2, "legacy", 0.0f) != 0.0f);
    }
    return 0;
}

/* spawn.item_marker(h, kind [, label]): kind "ammo" | "armour" | "gift" | "item"; the label is at most 7 characters */
static int arenaSpawnItemMarker(lua_State *L)
{
    static const char *const kinds[] = { "ammo", "armour", "gift", "item", NULL };

    needStage(L, "spawn.item_marker");
    lua_pushboolean(L, portLuaGameItemMarker((int) luaL_checkinteger(L, 1), luaL_checkoption(L, 2, NULL, kinds), luaL_optstring(L, 3, NULL)));
    return 1;
}

/* spawn.last_hit(chr) -> part, weapon, player, dist, age, blast  (nil when no Bond ever hit it). See port_arena_a8.h */
static int arenaSpawnLastHit(lua_State *L)
{
    const char *part, *weapon;
    int player, blast;
    float dist, age;

    needStage(L, "spawn.last_hit");
    if (!portLuaGameLastHit((int) luaL_checkinteger(L, 1), &part, &weapon, &player, &dist, &age, &blast)) return 0;
    lua_pushstring(L, part);
    if (weapon) lua_pushstring(L, weapon); else lua_pushnil(L);
    if (player >= 0) lua_pushinteger(L, player); else lua_pushnil(L);
    lua_pushnumber(L, dist);
    lua_pushnumber(L, age);
    lua_pushinteger(L, blast);
    return 6;
}

/* spawn.hit_events() -> { {kind="disarm"|"crowd", chr=, player=}, ... } since the last call (then the queue is empty), lost count */
static int arenaSpawnHitEvents(lua_State *L)
{
    int kind[32], chr[32], player[32], n, i;

    needStage(L, "spawn.hit_events");
    n = portLuaGameHitEvents(kind, chr, player, 32);
    lua_createtable(L, n, 0);
    for (i = 0; i < n; i++)
    {
        lua_createtable(L, 0, 3);
        lua_pushstring(L, kind[i] == 1 ? "disarm" : "crowd");
        lua_setfield(L, -2, "kind");
        lua_pushinteger(L, chr[i]);
        lua_setfield(L, -2, "chr");
        lua_pushinteger(L, player[i]);
        lua_setfield(L, -2, "player");
        lua_rawseti(L, -2, i + 1);
    }
    lua_pushinteger(L, portLuaGameHitEventsLost());
    return 2;
}

/* spawn.owned_guns(chr) -> the names of the guns the arena enemy held (one per hand, kept after a disarm or its death drop), nothing
 * when none is known. The crowd's THEIR OWN GUN compares the killing shot's gun with these. */
static int arenaSpawnOwnedGuns(lua_State *L)
{
    const char *g1, *g2;
    int n;

    needStage(L, "spawn.owned_guns");
    n = portLuaGameOwnedGuns((int) luaL_checkinteger(L, 1), &g1, &g2);
    if (n >= 1) lua_pushstring(L, g1);
    if (n >= 2) lua_pushstring(L, g2);
    return n;
}

static int arenaSpawnDisarm(lua_State *L)
{
    needStage(L, "spawn.disarm");
    lua_pushboolean(L, portLuaGameChrDisarm((int) luaL_checkinteger(L, 1)));
    return 1;
}

/* test.shoot(chr, part, weapon [, player]): test hook, a real shot landing on the chr (handles_shot_actors) fired by that Bond */
static int arenaSpawnShoot(lua_State *L)
{
    int r;

    needStage(L, "test.shoot");
    r = portLuaGameChrShoot((int) luaL_checkinteger(L, 1), luaL_optstring(L, 2, "body"), luaL_optstring(L, 3, "pp7"), (int) luaL_optinteger(L, 4, 0));
    if (r < 0) return luaL_error(L, "test.shoot: unknown weapon '%s'", luaL_optstring(L, 3, "pp7"));
    lua_pushboolean(L, r);
    return 1;
}

/* bond.shots(p) -> shots fired, shots that hit a chr (nil: no such Bond) */
static int arenaBondShots(lua_State *L)
{
    int fired, hits;

    needStage(L, "bond.shots");
    if (!portLuaGameBondShots((int) luaL_checkinteger(L, 1), &fired, &hits)) return 0;
    lua_pushinteger(L, fired);
    lua_pushinteger(L, hits);
    return 2;
}

/* arena.sfx(id [, pitch]) -> bool: a stock sound effect by id (bondconstants.h *_SFX; 160 BING, 54 EVIL_LAUGH ...) */
static int arenaSfx(lua_State *L)
{
    needStage(L, "arena.sfx");
    lua_pushboolean(L, portLuaGameSfx((int) luaL_checkinteger(L, 1), (float) luaL_optnumber(L, 2, 0.0)));
    return 1;
}

/* ---- the arena HUD (port/src/port_arenahud.c, presentation): arena.hud(on), arena.panel{...}, arena.callout(text, level [, sub [, secs]]) */
extern void portArenaHudOn(int on);
extern void portArenaHudPanel(int wave, int waves, const char *title, const char *mutator, int kills, int left, int elites, const char *time,
                              int phase, int crowd, int tier, const char *tierName, const char *toast, int dbl);
extern void portArenaHudCallout(const char *text, int level, const char *sub, float life);
extern void portArenaHudPanelWords(const char *phaseLabel, const char *idleTitle, const char *idleSub);
extern void portArenaHudExtras(int credits, int n, const char labels[][24], const int *secs, const unsigned *rgba);

static int arenaHud(lua_State *L)
{
    portArenaHudOn(lua_toboolean(L, 1));
    return 0;
}

/* arena.hud_layout(scale [, anchor "top_left" | "top_right" [, margin]]): the wave panel's size (1 = the console font's size), corner and
 * gap to the screen edges (console units) */
extern void portArenaHudLayout(float scale, int anchor, int margin);

static int arenaHudLayout(lua_State *L)
{
    static const char *const anchors[] = { "top_left", "top_right", NULL };

    portArenaHudLayout((float) luaL_optnumber(L, 1, 0.65), luaL_checkoption(L, 2, "top_left", anchors), (int) luaL_optinteger(L, 3, 4));
    return 0;
}

static int fieldInt(lua_State *L, const char *k, int d)
{
    int v;

    lua_getfield(L, 1, k);
    v = lua_isnumber(L, -1) ? (int) lua_tointeger(L, -1) : d;
    lua_pop(L, 1);
    return v;
}

/* the strings are copied (bounded) before the next field is read: lua_tostring's pointer lives while the value is on the stack */
static void fieldStr(lua_State *L, const char *k, char *out, size_t n)
{
    lua_getfield(L, 1, k);
    snprintf(out, n, "%s", lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : "");
    lua_pop(L, 1);
}

/* arena.panel{wave, waves, title, mutator, kills, left, elites, time, phase = "delay"|"fight"|"breather"|"over", crowd (0..100, -1 off),
 * tier (1..5), tier_name, toast, double_secs, credits (-1 off), chips = {{label, secs, rgba}, ...}} */
static int arenaPanel(lua_State *L)
{
    static const char *const phases[] = { "delay", "fight", "breather", "over", NULL };
    char title[40], mutator[32], time[12], tierName[24], toast[48], phase[12];
    int ph = 0, i;

    luaL_checktype(L, 1, LUA_TTABLE);
    fieldStr(L, "title", title, sizeof(title));
    fieldStr(L, "mutator", mutator, sizeof(mutator));
    fieldStr(L, "time", time, sizeof(time));
    fieldStr(L, "tier_name", tierName, sizeof(tierName));
    fieldStr(L, "toast", toast, sizeof(toast));
    fieldStr(L, "phase", phase, sizeof(phase));
    for (i = 0; phases[i] != NULL; i++) if (strcmp(phase, phases[i]) == 0) ph = i;
    portArenaHudPanel(fieldInt(L, "wave", 0), fieldInt(L, "waves", 0), title, mutator, fieldInt(L, "kills", 0), fieldInt(L, "left", -1),
                      fieldInt(L, "elites", 0), time, ph, fieldInt(L, "crowd", -1), fieldInt(L, "tier", 3), tierName, toast,
                      fieldInt(L, "double_secs", 0));
    {   /* the theme's words: phase_label, idle_title, idle_sub (empty = the built-in ones) */
        char phaseLabel[20], idleTitle[24], idleSub[32];

        fieldStr(L, "phase_label", phaseLabel, sizeof(phaseLabel));
        fieldStr(L, "idle_title", idleTitle, sizeof(idleTitle));
        fieldStr(L, "idle_sub", idleSub, sizeof(idleSub));
        portArenaHudPanelWords(phaseLabel, idleTitle, idleSub);
    }
    {   /* arena plus (credits and power-up timer chips): credits (-1 = none), chips = { {label =, secs =, rgba =}, ... } (4 at most) */
        char labels[4][24];
        int secs[4], n = 0;
        unsigned rgba[4];

        lua_getfield(L, 1, "chips");
        if (lua_istable(L, -1))
        {
            const int t = lua_gettop(L);
            int k, len = (int) lua_rawlen(L, t);

            for (k = 1; k <= len && n < 4; k++)
            {
                lua_rawgeti(L, t, k);
                if (lua_istable(L, -1))
                {
                    lua_getfield(L, -1, "label");
                    snprintf(labels[n], sizeof(labels[n]), "%s", lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : "");
                    lua_pop(L, 1);
                    lua_getfield(L, -1, "secs");
                    secs[n] = lua_isnumber(L, -1) ? (int) lua_tointeger(L, -1) : 0;
                    lua_pop(L, 1);
                    lua_getfield(L, -1, "rgba");
                    rgba[n] = lua_isnumber(L, -1) ? (unsigned) lua_tointeger(L, -1) : 0xF0C040FFu;
                    lua_pop(L, 1);
                    n++;
                }
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);
        portArenaHudExtras(fieldInt(L, "credits", -1), n, (const char (*)[24]) labels, secs, rgba);
    }
    return 0;
}

static int arenaCallout(lua_State *L)
{
    portArenaHudCallout(luaL_checkstring(L, 1), (int) luaL_optinteger(L, 2, 1), luaL_optstring(L, 3, NULL), (float) luaL_optnumber(L, 4, 0.0));
    return 0;
}

static int arenaSpawnMarkerSync(lua_State *L)
{
    needStage(L, "spawn.marker_sync");
    portLuaGameMarkerSync();
    return 0;
}

/* spawn.item_pos(h) -> x, z (nil when gone) */
static int arenaSpawnItemPos(lua_State *L)
{
    float x, z;
    int h = (int) luaL_checkinteger(L, 1);

    needStage(L, "spawn.item_pos");
    if (!(h >= PORT_HEIST_LOOT_HANDLE ? portHeistLootPos(h, &x, &z) : portLuaGameItemPos(h, &x, &z))) return 0;
    lua_pushnumber(L, x);
    lua_pushnumber(L, z);
    return 2;
}

/* test.walk(player, x, z) -> distance (test helper): steer Bond toward x, z with the real stick for this tick (call it every tick);
 * test.walk_stop() releases the stick. Player 0 only. */
static int arenaBondWalk(lua_State *L)
{
    needStage(L, "test.walk");
    lua_pushnumber(L, portLuaGameBondWalk((int) luaL_checkinteger(L, 1), (float) luaL_checknumber(L, 2), (float) luaL_checknumber(L, 3)));
    return 1;
}

/* bond.ammo(player, weapon) -> carries it (bool), reserve ammo of its type (nil: no such Bond / weapon) */
static int arenaBondAmmo(lua_State *L)
{
    int has, ammo;

    needStage(L, "bond.ammo");
    if (!portLuaGameBondAmmo((int) luaL_checkinteger(L, 1), luaL_checkstring(L, 2), &has, &ammo)) return 0;
    lua_pushboolean(L, has);
    lua_pushinteger(L, ammo);
    return 2;
}

static int arenaBondWalkStop(lua_State *L)
{
    (void) L;
    portLuaGameBondWalkStop();
    return 0;
}

static int arenaSpawnAlive(lua_State *L)
{
    needStage(L, "spawn.alive");
    lua_pushboolean(L, portLuaGameChrAlive((int) luaL_checkinteger(L, 1)));
    return 1;
}

/* spawn.where(chr) -> x, z, actiontype (nil when gone); a test/diagnostic helper */
extern int portLuaGameChrWhere(int chr, float *x, float *z, int *act);
static int arenaSpawnWhere(lua_State *L)
{
    float x, z;
    int act;

    needStage(L, "spawn.where");
    if (!portLuaGameChrWhere((int) luaL_checkinteger(L, 1), &x, &z, &act)) return 0;
    lua_pushnumber(L, x);
    lua_pushnumber(L, z);
    lua_pushinteger(L, act);
    return 3;
}

static int arenaSpawnFree(lua_State *L)
{
    needStage(L, "spawn.free");
    lua_pushinteger(L, portLuaGameChrFree());
    return 1;
}

/* spawn.anim(chr, name [, force]) -> true when started. A fidget (no force) only starts on an idle chr; force plays at once. Unknown names raise.
 * Names: yawn flies scratch_leg scratch_butt adjust sneeze chat listen cough laugh startled surrender dance look fists cross_arms. */
static int arenaSpawnAnim(lua_State *L)
{
    const char *name = luaL_checkstring(L, 2);
    int r;

    needStage(L, "spawn.anim");
    r = portLuaGameChrAnim((int) luaL_checkinteger(L, 1), name, lua_toboolean(L, 3));
    if (r < 0) return luaL_error(L, "unknown animation '%s'", name);
    lua_pushboolean(L, r);
    return 1;
}

static int arenaSpawnSkins(lua_State *L)
{
    int i, n = portLuaGameSkinCount();

    lua_createtable(L, n, 0);
    for (i = 0; i < n; i++)
    {
        lua_pushstring(L, portLuaGameSkinName(i));
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

static int arenaSpawnWeapons(lua_State *L)
{
    int i;

    lua_newtable(L);
    for (i = 0; sWeaponNames[i] != NULL; i++)
    {
        lua_pushstring(L, sWeaponNames[i]);
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

/* spawn.bodies() -> { body names }, { head names } (the names spawn.chr's body= / head= take) */
static int arenaSpawnBodies(lua_State *L)
{
    int h, i;

    for (h = 0; h < 2; h++)
    {
        const char *nm;

        lua_newtable(L);
        for (i = 0; (nm = portLuaGameBodyName(h, i)) != NULL; i++)
        {
            lua_pushstring(L, nm);
            lua_rawseti(L, -2, i + 1);
        }
    }
    return 2;
}

/* bond.give(player, item [, ammo]): the item name as weapon.set takes it (any case, aliases ok) */
static int arenaBondGive(lua_State *L)
{
    char key[48];
    const char *name = luaL_checkstring(L, 2);
    size_t n = 0;

    needStage(L, "bond.give");
    for (; *name != '\0' && n < sizeof(key) - 1; name++)
    {
        key[n++] = (*name >= 'a' && *name <= 'z') ? (char) (*name - 32) : *name;
    }
    key[n] = '\0';
    lua_pushboolean(L, portLuaGameGive((int) luaL_checkinteger(L, 1), key, (int) luaL_optinteger(L, 3, -1)));
    return 1;
}

static int arenaBondHeal(lua_State *L)
{
    needStage(L, "bond.heal");
    portLuaGameHeal((int) luaL_checkinteger(L, 1), (float) luaL_optnumber(L, 2, 0.0), (float) luaL_optnumber(L, 3, 0.0));
    return 0;
}

static int arenaBondVitals(lua_State *L)
{
    float h, a;

    needStage(L, "bond.vitals");
    if (!portLuaGameBondVitals((int) luaL_optinteger(L, 1, 0), &h, &a))
    {
        lua_pushnil(L);
        return 1;
    }
    lua_pushnumber(L, h);
    lua_pushnumber(L, a);
    return 2;
}

/* ---- bench: the arena benchmark (port_bench.c, tools/mode_arena_bench.lua) ----
 *   bench.run_start()  fresh Data/Logs/arena_bench.txt with the machine lines     bench.note(text)  one report line
 *   bench.begin()      start sampling frames     bench.tag(n)  0 = floor view, 1 = stands view for the frames from now on
 *   bench.finish(label, extra)  stop, write the phase block (frame stats, pools)     bench.pools() -> 13 numbers (chr total/free, props total/free,
 *   hit entries total/free/peak used, stage mempool bytes left)     bench.look(player, yaw, pitch)  degrees
 *   bench.sweep(deg_per_sec, pitch)  turn Bond's view every game tick, pitch eased in   bench.sweep_off()     bench.exit()  quit the game */
extern void portBenchRunStart(void);
extern void portBenchLine(const char *fmt, ...);
extern void portBenchBegin(void);
extern void portBenchTag(int tag);
extern void portBenchEnd(const char *label, const char *extra, const int *pools);
extern int portLuaGameBenchPools(int *out);
extern void portBenchSweep(int on, float rate, float pitch);
extern int portLuaGameBondLook(int player, float yaw, float pitch);

static int benchRunStart(lua_State *L) { (void) L; portBenchRunStart(); return 0; }
static int benchNote(lua_State *L) { portBenchLine("%s", luaL_checkstring(L, 1)); return 0; }
static int benchBegin(lua_State *L) { (void) L; portBenchBegin(); return 0; }
static int benchTag(lua_State *L) { portBenchTag((int) luaL_checkinteger(L, 1)); return 0; }

static int benchPools(lua_State *L)
{
    int p[13], i;

    needStage(L, "bench.pools");
    if (!portLuaGameBenchPools(p)) return 0;
    for (i = 0; i < 13; i++) lua_pushinteger(L, p[i]);
    return 13;
}

static int benchFinish(lua_State *L)
{
    int p[13];
    int ok;

    needStage(L, "bench.finish");
    ok = portLuaGameBenchPools(p);
    portBenchEnd(luaL_checkstring(L, 1), luaL_optstring(L, 2, ""), ok ? p : NULL);
    return 0;
}

static int benchLook(lua_State *L)
{
    needStage(L, "bench.look");
    lua_pushboolean(L, portLuaGameBondLook((int) luaL_checkinteger(L, 1), (float) luaL_checknumber(L, 2), (float) luaL_checknumber(L, 3)));
    return 1;
}

static int benchSweep(lua_State *L)
{
    needStage(L, "bench.sweep");
    portBenchSweep(1, (float) luaL_checknumber(L, 1), (float) luaL_checknumber(L, 2));
    return 0;
}

extern void portBenchLast(float *simAvg, float *glAvg);
/* bench.last() -> avg sim ms, avg gl ms of the phase bench.finish wrote last */
static int benchLast(lua_State *L)
{
    float s, g;

    portBenchLast(&s, &g);
    lua_pushnumber(L, s);
    lua_pushnumber(L, g);
    return 2;
}

static int benchSweepOff(lua_State *L) { (void) L; portBenchSweep(0, 0.0f, 0.0f); return 0; }

static int benchExit(lua_State *L)
{
    extern void portConLogf(const char *fmt, ...);

    (void) L;
    portConLogf("ge: bench: done, quitting\n");
    fflush(stdout);
    exit(0);
    return 0;
}

/* ---- crowd: the impostor crowd (port_crowd.c), presentation only ----
 *   crowd.impostors({x, y, z, x, y, z, ...}, {skin names}) -> count   impostors standing at those points ({} removes them)
 *   crowd.cheer(seconds [, fraction])   that share of the impostors (1) raise their arms for about `seconds`, rippling */
extern int portCrowdSet(const float *xyz, int n, const char *const *skins, int nskins);
extern void portCrowdCheer(float secs, float frac);
extern int portLuaBenchHide(lua_State *L);   /* port_luavm_perfcat.c: bench.hide, crowd.lod */
extern int portLuaCrowdLod(lua_State *L);
extern int portLuaBenchSummary(lua_State *L);
extern int portLuaBenchShot(lua_State *L);
extern int portLuaCrowdBatch(lua_State *L);
extern int portLuaCrowdBake(lua_State *L);

static int crowdImpostors(lua_State *L)
{
    float *xyz;
    const char *skins[16];
    int n, i, ns = 0;

    needStage(L, "crowd.impostors");
    luaL_checktype(L, 1, LUA_TTABLE);
    n = (int) lua_rawlen(L, 1) / 3;
    if (n > 4096) n = 4096;
    xyz = lua_newuserdata(L, (size_t) (n > 0 ? n : 1) * 3 * sizeof(float));   /* scratch, collected */
    for (i = 0; i < n * 3; i++)
    {
        lua_rawgeti(L, 1, i + 1);
        xyz[i] = (float) luaL_checknumber(L, -1);
        lua_pop(L, 1);
    }
    if (lua_istable(L, 2))
    {
        for (i = 0; i < 16 && i < (int) lua_rawlen(L, 2); i++)
        {
            lua_rawgeti(L, 2, i + 1);
            skins[ns++] = luaL_checkstring(L, -1);   /* the table keeps the strings alive */
            lua_pop(L, 1);
        }
    }
    lua_pushinteger(L, portCrowdSet(xyz, n, skins, ns));
    return 1;
}

static int crowdCheer(lua_State *L)
{
    portCrowdCheer((float) luaL_checknumber(L, 1), (float) luaL_optnumber(L, 2, 1.0));
    return 0;
}

/* ---- sound: WAV files from Data/Sounds/ mixed into the audio output (port_soundfile.c), presentation only ----
 *   sound.play_file(name [, volume [, duck]]) -> voice id | nil    name = a file in Data/Sounds ("cheer1" or "cheer1.wav")
 *   sound.play_any(base [, volume [, duck]]) -> file name, voice id | nil   a random one of base.wav, base1.wav .. base16.wav that exists
 *   sound.variants(base) -> count   (loads them now, so the first play does not read the disk)
 *   sound.file_exists(name) -> bool;  sound.stop(id [, fade seconds]);  sound.playing() -> voices playing
 * volume 0..1 (x the game's sound effects volume); duck 0..1 = the other playing voices drop to that level for a while.
 * A missing or unreadable file plays nothing. What these return differs between machines: never steer the game with it. */
extern int portSoundFileExists(const char *name);
extern int portSoundFilePlay(const char *name, float vol, float duck);
extern const char *portSoundFilePlayAny(const char *base, float vol, float duck, int *id);
extern int portSoundFileVariants(const char *base);
extern void portSoundFileStop(int id, float fade);
extern int portSoundFilePlaying(void);

static int soundPlayFile(lua_State *L)
{
    int id = portSoundFilePlay(luaL_checkstring(L, 1), (float) luaL_optnumber(L, 2, 1.0), (float) luaL_optnumber(L, 3, 1.0));

    if (id <= 0) return 0;
    lua_pushinteger(L, id);
    return 1;
}

static int soundPlayAny(lua_State *L)
{
    int id = 0;
    const char *n = portSoundFilePlayAny(luaL_checkstring(L, 1), (float) luaL_optnumber(L, 2, 1.0), (float) luaL_optnumber(L, 3, 1.0), &id);

    if (n == NULL) return 0;
    lua_pushstring(L, n);
    lua_pushinteger(L, id);
    return 2;
}

static int soundVariants(lua_State *L) { lua_pushinteger(L, portSoundFileVariants(luaL_checkstring(L, 1))); return 1; }
static int soundExists(lua_State *L) { lua_pushboolean(L, portSoundFileExists(luaL_checkstring(L, 1))); return 1; }
static int soundStop(lua_State *L) { portSoundFileStop((int) luaL_checkinteger(L, 1), (float) luaL_optnumber(L, 2, 0.1)); return 0; }
static int soundPlaying(lua_State *L) { lua_pushinteger(L, portSoundFilePlaying()); return 1; }

/* ---- the game's own sound effects (port_gamesfx.c), presentation only ----
 *   sound.play_sfx(name_or_id [, volume [, pitch [, delay [, pan]]]]) -> handle | nil   name = an SFX_ID without _SFX ("GET_HIT_MALE3"),
 *       any case; volume 0..1 (x the game's sfx volume); pitch multiplier (1 = as recorded); delay seconds; pan -1 (left) .. 1 (right)
 *   sound.sfx_burst(names, count [, opts]) -> sounds queued   count = n or {lo, hi}; opts: vol, pitch = {lo, hi}, gap = {lo, hi}
 *       seconds between starts, delay (first start), pan (spread 0..1). The picks, pitches, pans and gaps use the C side's RNG.
 *   sound.sfx_names() -> list (index = id);  sound.stop_sfx(handle);  sound.sfx_playing() -> playing, queued
 * They play through the game's sound player but never take a game sound's place (snd.c portPresent). Never steer the game with them. */
extern int portGameSfxId(const char *name);
extern const char *portGameSfxName(int id);
extern int portGameSfxCount(void);
extern int portGameSfxPlay(int sfx, float vol, float pitch, float delay, float pan);
extern int portGameSfxBurst(const int *ids, int n, int lo, int hi, float vol, float plo, float phi, float glo, float ghi, float delay,
                            float panSpread);
extern void portGameSfxStop(int id);
extern int portGameSfxPlaying(int *queued);

static int sfxArg(lua_State *L, int idx)
{
    if (lua_type(L, idx) == LUA_TNUMBER)
    {
        int id = (int) lua_tointeger(L, idx);

        return portGameSfxName(id) != NULL ? id : 0;
    }
    return portGameSfxId(luaL_checkstring(L, idx));
}

/* opts[key] as a {lo, hi} pair or one number (both ends) */
static void rangeOpt(lua_State *L, int opts, const char *key, float *lo, float *hi)
{
    if (opts == 0) return;
    lua_getfield(L, opts, key);
    if (lua_isnumber(L, -1)) { *lo = *hi = (float) lua_tonumber(L, -1); }
    else if (lua_istable(L, -1))
    {
        lua_rawgeti(L, -1, 1); *lo = (float) luaL_optnumber(L, -1, *lo); lua_pop(L, 1);
        lua_rawgeti(L, -1, 2); *hi = (float) luaL_optnumber(L, -1, *lo); lua_pop(L, 1);
    }
    lua_pop(L, 1);
}

static float numOpt(lua_State *L, int opts, const char *key, float def)
{
    float v = def;

    if (opts == 0) return def;
    lua_getfield(L, opts, key);
    if (lua_isnumber(L, -1)) v = (float) lua_tonumber(L, -1);
    lua_pop(L, 1);
    return v;
}

static int soundPlaySfx(lua_State *L)
{
    int id = sfxArg(L, 1), h;

    if (id == 0) return 0;
    h = portGameSfxPlay(id, (float) luaL_optnumber(L, 2, 1.0), (float) luaL_optnumber(L, 3, 1.0), (float) luaL_optnumber(L, 4, 0.0),
                        (float) luaL_optnumber(L, 5, 0.0));
    if (h <= 0) return 0;
    lua_pushinteger(L, h);
    return 1;
}

static int soundSfxBurst(lua_State *L)
{
    int ids[64], n = 0, i, lo, hi, opts = lua_istable(L, 3) ? 3 : 0;
    float plo = 0.85f, phi = 1.25f, glo = 0.03f, ghi = 0.15f, flo, fhi;

    luaL_checktype(L, 1, LUA_TTABLE);
    for (i = 1; i <= (int) lua_rawlen(L, 1) && n < 64; i++)
    {
        lua_rawgeti(L, 1, i);
        ids[n] = sfxArg(L, -1);
        if (ids[n] != 0) n++;
        lua_pop(L, 1);
    }
    flo = fhi = 1.0f;
    if (lua_istable(L, 2))
    {
        lua_rawgeti(L, 2, 1); flo = (float) luaL_checknumber(L, -1); lua_pop(L, 1);
        lua_rawgeti(L, 2, 2); fhi = (float) luaL_optnumber(L, -1, flo); lua_pop(L, 1);
    }
    else flo = fhi = (float) luaL_checknumber(L, 2);
    lo = (int) flo;
    hi = (int) fhi;
    rangeOpt(L, opts, "pitch", &plo, &phi);
    rangeOpt(L, opts, "gap", &glo, &ghi);
    lua_pushinteger(L, portGameSfxBurst(ids, n, lo, hi, numOpt(L, opts, "vol", 1.0f), plo, phi, glo, ghi, numOpt(L, opts, "delay", 0.0f),
                                        numOpt(L, opts, "pan", 0.8f)));
    return 1;
}

static int soundSfxNames(lua_State *L)
{
    int i;

    lua_createtable(L, portGameSfxCount(), 0);
    for (i = 1; i < portGameSfxCount(); i++)
    {
        lua_pushstring(L, portGameSfxName(i));
        lua_rawseti(L, -2, i);
    }
    return 1;
}

static int soundStopSfx(lua_State *L) { portGameSfxStop((int) luaL_checkinteger(L, 1)); return 0; }

static int soundSfxPlaying(lua_State *L)
{
    int q = 0, n = portGameSfxPlaying(&q);

    lua_pushinteger(L, n);
    lua_pushinteger(L, q);
    return 2;
}

static void addTo(lua_State *L, const char *lib, const luaL_Reg *reg)
{
    lua_getglobal(L, lib);
    luaL_setfuncs(L, reg, 0);
    lua_pop(L, 1);
}

/* ---- drop A7: mutators, elite affixes, scores (port_arenafx.c, port_arenascores.c) ----
 *   spawn.knife_only(chr [, on]) -> bool     spawn.scale(chr, factor) -> bool     spawn.heal(chr, frac) -> bool   test.hurt(chr, frac)
 *   spawn.health(chr) -> fraction of health left, armour (x the base 4), damage, maxdamage   (nil when gone)
 *   spawn.explode(chr [, kind]) -> bool   kind "small" | "medium" (default) | "grenade" | an EXPLOSION_DEF number
 *   arena.fog(far [, r, g, b]) -> bool (far <= 0: the level's own sky);  arena.fog() -> the current far distance (0 = none)
 *   arena.debrief({ lines }) -> the report page's lines on the Colosseum (8 at most; nil / {} clears)
 *   scores.get(key) -> number | nil;  scores.set(key, v) -> bool;  scores.all() -> { key = v };  scores.enabled();  scores.path() */
#include "../include/port_arena_a7.h"

static int arenaSpawnKnifeOnly(lua_State *L)
{
    needStage(L, "spawn.knife_only");
    lua_pushboolean(L, portLuaGameChrKnifeOnly((int) luaL_checkinteger(L, 1), lua_isnone(L, 2) ? 1 : lua_toboolean(L, 2)));
    return 1;
}

static int arenaSpawnScale(lua_State *L)
{
    needStage(L, "spawn.scale");
    lua_pushboolean(L, portLuaGameChrScale((int) luaL_checkinteger(L, 1), (float) luaL_checknumber(L, 2)));
    return 1;
}

static int arenaSpawnHeal(lua_State *L)
{
    needStage(L, "spawn.heal");
    lua_pushboolean(L, portLuaGameChrHeal((int) luaL_checkinteger(L, 1), (float) luaL_checknumber(L, 2)));
    return 1;
}

/* test.hurt(chr, frac) -> bool: test hook, a wound of frac of full health that never kills (armour is ignored) */
static int arenaSpawnHurt(lua_State *L)
{
    float f;

    needStage(L, "test.hurt");
    f = (float) luaL_checknumber(L, 2);
    lua_pushboolean(L, portLuaGameChrHeal((int) luaL_checkinteger(L, 1), f > 0.0f ? -f : f));
    return 1;
}

static int arenaSpawnHealth(lua_State *L)
{
    float dmg, maxd;

    needStage(L, "spawn.health");
    if (!portLuaGameChrHealth((int) luaL_checkinteger(L, 1), &dmg, &maxd) || maxd <= 0.0f) return 0;
    lua_pushnumber(L, dmg > 0.0f ? 1.0 - (double) dmg / maxd : 1.0);
    lua_pushnumber(L, dmg < 0.0f ? -(double) dmg / 4.0 : 0.0);
    lua_pushnumber(L, dmg);
    lua_pushnumber(L, maxd);
    return 4;
}

static int arenaSpawnExplode(lua_State *L)
{
    int kind = 12;   /* "medium": EXPLOSION_DEF_12, 320 damage range (the grenade is 13: 480) */

    needStage(L, "spawn.explode");
    if (lua_type(L, 2) == LUA_TSTRING)
    {
        static const char *const kinds[] = { "small", "medium", "grenade", NULL };
        static const int ids[] = { 4, 12, 13 };

        kind = ids[luaL_checkoption(L, 2, "medium", kinds)];
    }
    else if (!lua_isnoneornil(L, 2))
    {
        kind = (int) luaL_checkinteger(L, 2);
    }
    portArenaBlastForce(lua_isnoneornil(L, 3) ? -1 : (int) luaL_checkinteger(L, 3));   /* drop A8: credit the blast to that Bond (tests) */
    lua_pushboolean(L, portLuaGameExplode((int) luaL_checkinteger(L, 1), kind));
    portArenaBlastForce(-1);
    return 1;
}

static int arenaFog(lua_State *L)
{
    needStage(L, "arena.fog");
    if (lua_isnoneornil(L, 1))
    {
        lua_pushnumber(L, portLuaGameFogGet());
        return 1;
    }
    lua_pushboolean(L, portLuaGameFog((float) luaL_checknumber(L, 1), (int) luaL_optinteger(L, 2, 0x70), (int) luaL_optinteger(L, 3, 0x70),
                                      (int) luaL_optinteger(L, 4, 0x78)));
    return 1;
}

/* arena.rain(slip [, grip]) -> ok: the RAIN mutator (port_arenarain.c): slip = Bond's slide time constant in seconds at full rain
 * (0 = stop: it rolls out), grip = the grenades' landed-slide friction (0.2 .. 1, default 0.5). arena.rain() -> the slip asked for */
static int arenaRain(lua_State *L)
{
    extern int portLuaGameRain(float slip, float grip);
    extern float portLuaGameRainGet(void);

    needStage(L, "arena.rain");
    if (lua_isnoneornil(L, 1))
    {
        lua_pushnumber(L, portLuaGameRainGet());
        return 1;
    }
    lua_pushboolean(L, portLuaGameRain((float) luaL_checknumber(L, 1), (float) luaL_optnumber(L, 2, 0.5)));
    return 1;
}

/* arena.strike(who, dx, dz [, explosion [, fuse]]) -> x, y, z, serial | nil: the RAIN mutator's lightning (port_arenarain.c).
 * who = a chrnum, or { bond = p } for Bond p; (dx, dz) = the offset from the character's feet (walked along the floor, shorter when a
 * wall or an edge is in the way); explosion = "small" | "medium" | "grenade" (default) | "remote" or an EXPLOSION_DEF number;
 * fuse = seconds before it lands (0.6). The flash and the bolt are drawn when it lands; the explosion hurts whoever stands there. */
static int arenaStrike(lua_State *L)
{
    extern int portLuaGameStrike(int who, float dx, float dz, int kind, float fuseSecs, float *ox, float *oy, float *oz);
    static const char *const kinds[] = { "small", "medium", "grenade", "remote", NULL };
    static const int ids[] = { 4, 12, 13, 19 };
    int who, kind = 13, serial;
    float x, y, z;

    needStage(L, "arena.strike");
    if (lua_istable(L, 1))
    {
        lua_getfield(L, 1, "bond");
        who = -1 - (int) luaL_checkinteger(L, -1);
        lua_pop(L, 1);
    }
    else
    {
        who = (int) luaL_checkinteger(L, 1);
        if (who < 0) return 0;
    }
    if (lua_type(L, 4) == LUA_TSTRING) kind = ids[luaL_checkoption(L, 4, "grenade", kinds)];
    else if (!lua_isnoneornil(L, 4)) kind = (int) luaL_checkinteger(L, 4);
    serial = portLuaGameStrike(who, (float) luaL_checknumber(L, 2), (float) luaL_checknumber(L, 3), kind, (float) luaL_optnumber(L, 5, 0.6),
                               &x, &y, &z);
    if (serial <= 0) return 0;
    lua_pushnumber(L, x);
    lua_pushnumber(L, y);
    lua_pushnumber(L, z);
    lua_pushinteger(L, serial);
    return 4;
}

static int arenaDebrief(lua_State *L)
{
    int k, n = 0;

    portArenaDebriefClear();
    if (lua_istable(L, 1))
    {
        for (k = 1; k <= 8; k++)
        {
            lua_rawgeti(L, 1, k);
            if (lua_type(L, -1) == LUA_TSTRING)
            {
                portArenaDebriefSet(n++, lua_tostring(L, -1));
            }
            lua_pop(L, 1);
        }
    }
    lua_pushinteger(L, n);
    return 1;
}

static int scoresGet(lua_State *L)
{
    double v;

    if (!portArenaScoreGet(luaL_checkstring(L, 1), &v)) return 0;
    lua_pushnumber(L, v);
    return 1;
}

static int scoresSet(lua_State *L)
{
    lua_pushboolean(L, portArenaScoreSet(luaL_checkstring(L, 1), luaL_checknumber(L, 2)));
    return 1;
}

static int scoresAll(lua_State *L)
{
    int k, n = portArenaScoreCount();
    double v;
    const char *key;

    lua_createtable(L, 0, n);
    for (k = 0; (key = portArenaScoreKey(k, &v)) != NULL; k++)
    {
        lua_pushnumber(L, v);
        lua_setfield(L, -2, key);
    }
    return 1;
}

static int scoresEnabled(lua_State *L)
{
    lua_pushboolean(L, portArenaScoresEnabled());
    return 1;
}

static int scoresPath(lua_State *L)
{
    lua_pushstring(L, portArenaScoresPath());
    return 1;
}

static void portLuaArenaRegisterA7(lua_State *L)
{
    static const luaL_Reg spawnA7[] = { { "knife_only", arenaSpawnKnifeOnly }, { "scale", arenaSpawnScale }, { "heal", arenaSpawnHeal },
        { "health", arenaSpawnHealth }, { "explode", arenaSpawnExplode }, { NULL, NULL } };
    static const luaL_Reg testA7[] = { { "hurt", arenaSpawnHurt }, { NULL, NULL } };   /* test hooks (modding/lua.md "The test table") */
    static const luaL_Reg arenaLib[] = { { "fog", arenaFog }, { "rain", arenaRain }, { "strike", arenaStrike }, { "debrief", arenaDebrief }, { NULL, NULL } };
    static const luaL_Reg scoresLib[] = { { "get", scoresGet }, { "set", scoresSet }, { "all", scoresAll }, { "enabled", scoresEnabled },
        { "path", scoresPath }, { NULL, NULL } };

    addTo(L, "spawn", spawnA7);
    addTo(L, "test", testA7);
    luaL_newlib(L, arenaLib);
    lua_setglobal(L, "arena");
    luaL_newlib(L, scoresLib);
    lua_setglobal(L, "scores");
}

void portLuaArenaRegister(lua_State *L)
{
    static const luaL_Reg spawnAdd[] = { { "chr", arenaSpawnChr }, { "remove", arenaSpawnRemove },
        { "alive", arenaSpawnAlive }, { "free", arenaSpawnFree }, { "where", arenaSpawnWhere }, { "skins", arenaSpawnSkins }, { "anim", arenaSpawnAnim },
        { "weapons", arenaSpawnWeapons }, { "bodies", arenaSpawnBodies }, { "kill", arenaSpawnKill }, { "pools", arenaSpawnPools },
        { "trim_drops", arenaSpawnTrimDrops }, { "keep_bodies", arenaSpawnKeepBodies }, { "item", arenaSpawnItem }, { "item_alive", arenaSpawnItemAlive },
        { "item_remove", arenaSpawnItemRemove }, { "item_pos", arenaSpawnItemPos }, { "headshot", arenaSpawnHeadshot }, { "idle", arenaSpawnIdle },
        { NULL, NULL } };
    static const luaL_Reg bondAdd[] = { { "give", arenaBondGive }, { "heal", arenaBondHeal },
        { "vitals", arenaBondVitals }, { "warp", arenaBondWarp }, { "give_ammo", arenaBondGiveAmmo }, { "ammo", arenaBondAmmo }, { NULL, NULL } };
    /* test hooks (modding/lua.md "The test table") */
    static const luaL_Reg testAdd[] = { { "take_drop", arenaBondTakeDrop }, { "walk", arenaBondWalk }, { "walk_stop", arenaBondWalkStop },
        { NULL, NULL } };

    static const luaL_Reg polishAdd[] = { { "markers", arenaSpawnMarkers }, { "marker_sync", arenaSpawnMarkerSync }, { NULL, NULL } };   /* drop A7 */
    static const luaL_Reg spawnA8[] = { { "item_marker", arenaSpawnItemMarker }, { "last_hit", arenaSpawnLastHit },   /* drop A8 */
        { "hit_events", arenaSpawnHitEvents }, { "disarm", arenaSpawnDisarm }, { "owned_guns", arenaSpawnOwnedGuns }, { NULL, NULL } };
    static const luaL_Reg testA8[] = { { "shoot", arenaSpawnShoot }, { NULL, NULL } };   /* test hook */
    static const luaL_Reg bondA8[] = { { "shots", arenaBondShots }, { NULL, NULL } };
    static const luaL_Reg arenaA8[] = { { "sfx", arenaSfx }, { "hud", arenaHud }, { "hud_layout", arenaHudLayout }, { "panel", arenaPanel }, { "callout", arenaCallout },
        { NULL, NULL } };

    static const luaL_Reg benchLib[] = { { "run_start", benchRunStart }, { "note", benchNote }, { "begin", benchBegin },
        { "tag", benchTag }, { "pools", benchPools }, { "finish", benchFinish }, { "look", benchLook }, { "sweep", benchSweep }, { "sweep_off", benchSweepOff }, { "last", benchLast }, { "exit", benchExit },
        { "hide", portLuaBenchHide }, { "summary", portLuaBenchSummary }, { "shot", portLuaBenchShot },   /* port_luavm_perfcat.c */
        { NULL, NULL } };

    static const luaL_Reg crowdLib[] = { { "impostors", crowdImpostors }, { "cheer", crowdCheer }, { "lod", portLuaCrowdLod }, { "batch", portLuaCrowdBatch },
        { "bake", portLuaCrowdBake }, { NULL, NULL } };
    static const luaL_Reg soundLib[] = { { "play_file", soundPlayFile }, { "play_any", soundPlayAny }, { "variants", soundVariants },
        { "file_exists", soundExists }, { "stop", soundStop }, { "playing", soundPlaying }, { "play_sfx", soundPlaySfx },
        { "sfx_burst", soundSfxBurst }, { "sfx_names", soundSfxNames }, { "stop_sfx", soundStopSfx }, { "sfx_playing", soundSfxPlaying },
        { NULL, NULL } };

    luaL_newlib(L, benchLib);
    lua_setglobal(L, "bench");
    luaL_newlib(L, crowdLib);
    lua_setglobal(L, "crowd");
    luaL_newlib(L, soundLib);
    lua_setglobal(L, "sound");
    addTo(L, "spawn", spawnAdd);
    addTo(L, "spawn", polishAdd);
    addTo(L, "spawn", spawnA8);
    addTo(L, "test", testA8);
    addTo(L, "bond", bondAdd);
    addTo(L, "test", testAdd);
    addTo(L, "bond", bondA8);
    portLuaArenaRegisterA7(L);
    addTo(L, "arena", arenaA8);   /* after A7 made the `arena` table */
}
