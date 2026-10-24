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
 *   test helpers: spawn.kill(chr), bond.warp(player, pad [, dx, dz]), bond.take_drop(player [, radius])
 *   drop A6: spawn.item(pad, item [, {ammo=, amount=}]) -> handle | nil;  spawn.item_alive(h);  spawn.item_remove(h);  spawn.item_pos(h) -> x, z
 *   test helpers: bond.walk(player, x, z) -> distance (real stick input toward x, z, every tick), bond.walk_stop()
 * behaviour: "chase" (default), "rush", "hold", "snipe", "standard". health is a multiplier (1 = stock). */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
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

static const char *const sBehaviours[] = { "chase", "rush", "hold", "snipe", "standard", "spectator", NULL };

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
    if (lua_istable(L, 2))
    {
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

/* spawn.kill(chr [, "head"]) -> bool: test helper, lethal damage (a normal death: drops, events); "head" counts it as a headshot */
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

/* bond.take_drop(player [, radius]) -> item id (0 = none near), ammo of its type before, after: test helper, the real pickup */
static int arenaBondTakeDrop(lua_State *L)
{
    int before = 0, after = 0, item;

    needStage(L, "bond.take_drop");
    item = portLuaGameTakeDrop((int) luaL_checkinteger(L, 1), (float) luaL_optnumber(L, 2, 400.0), &before, &after);
    lua_pushinteger(L, item);
    lua_pushinteger(L, before);
    lua_pushinteger(L, after);
    return 3;
}

/* bond.warp(player, pad [, dx, dz]): test helper, Bond to a pad (plus an offset from its centre) */
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
    needStage(L, "spawn.item_alive");
    lua_pushboolean(L, portLuaGameItemAlive((int) luaL_checkinteger(L, 1)));
    return 1;
}

/* spawn.item_remove(h) -> bool: take it off the floor */
static int arenaSpawnItemRemove(lua_State *L)
{
    needStage(L, "spawn.item_remove");
    lua_pushboolean(L, portLuaGameItemRemove((int) luaL_checkinteger(L, 1)));
    return 1;
}

/* spawn.item_pos(h) -> x, z (nil when gone) */
static int arenaSpawnItemPos(lua_State *L)
{
    float x, z;

    needStage(L, "spawn.item_pos");
    if (!portLuaGameItemPos((int) luaL_checkinteger(L, 1), &x, &z)) return 0;
    lua_pushnumber(L, x);
    lua_pushnumber(L, z);
    return 2;
}

/* bond.walk(player, x, z) -> distance (test helper): steer Bond toward x, z with the real stick for this tick (call it every tick);
 * bond.walk_stop() releases the stick. Player 0 only. */
static int arenaBondWalk(lua_State *L)
{
    needStage(L, "bond.walk");
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

static void addTo(lua_State *L, const char *lib, const luaL_Reg *reg)
{
    lua_getglobal(L, lib);
    luaL_setfuncs(L, reg, 0);
    lua_pop(L, 1);
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
        { "vitals", arenaBondVitals }, { "warp", arenaBondWarp }, { "give_ammo", arenaBondGiveAmmo }, { "take_drop", arenaBondTakeDrop },
        { "ammo", arenaBondAmmo }, { "walk", arenaBondWalk }, { "walk_stop", arenaBondWalkStop }, { NULL, NULL } };

    static const luaL_Reg benchLib[] = { { "run_start", benchRunStart }, { "note", benchNote }, { "begin", benchBegin },
        { "tag", benchTag }, { "pools", benchPools }, { "finish", benchFinish }, { "look", benchLook }, { "sweep", benchSweep }, { "sweep_off", benchSweepOff }, { "last", benchLast }, { "exit", benchExit },
        { NULL, NULL } };

    static const luaL_Reg crowdLib[] = { { "impostors", crowdImpostors }, { "cheer", crowdCheer }, { NULL, NULL } };
    static const luaL_Reg soundLib[] = { { "play_file", soundPlayFile }, { "play_any", soundPlayAny }, { "variants", soundVariants },
        { "file_exists", soundExists }, { "stop", soundStop }, { "playing", soundPlaying }, { NULL, NULL } };

    luaL_newlib(L, benchLib);
    lua_setglobal(L, "bench");
    luaL_newlib(L, crowdLib);
    lua_setglobal(L, "crowd");
    luaL_newlib(L, soundLib);
    lua_setglobal(L, "sound");
    addTo(L, "spawn", spawnAdd);
    addTo(L, "bond", bondAdd);
}
