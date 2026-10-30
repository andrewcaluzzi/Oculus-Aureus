/* port_luavm_sniper.c - Lua bindings for the sniper duel's C side (port/src/port_sniper.c; tools/mode_sniper.lua; modding/sniper.md).
 *
 *   sniper.zoom_limit(item, fov) -> bool     the narrowest field of view (degrees) "sniperrifle" (the scope) or "camera" (the duel's
 *                                            binoculars) zooms to; stock 7; until the next stage start
 *   sniper.view(p) -> aiming, item, fov, stance   aim mode on, the ITEM_* id in his right hand, the field of view now, the stance
 *                                            (0 stand, 1 crouch, 2 squat, 3 prone); nothing when there is no such Bond
 *   sniper.prone(p [, on]) -> bool           PRONE (a third stance under the squat: the eye lower, slow, steady): set and return
 *   sniper.prone_tune(eye, speed, sway)      the eye offset (-160..-100 units; the squat is -100), the move-speed scale, the sway scale
 *   sniper.point(id, kind, x, y, z [, alpha [, size]])   an overlay point, id 0..23: kind "glint" (a white flare), "marker" (a quiet
 *                                            diamond), "tag" (brackets), "off"; alpha 0..1 (0 hides it), size in HUD pixels
 *   sniper.clear()                           every overlay point off
 *   sniper.binoculars(on)                    the lens frame while the camera is raised in aim mode
 *   sniper.thermal(on [, dim])               the thermal view's dim (0..0.9)
 *   sniper.no_unarmed(on)                    the weapon cycle steps over the bare hands (they stay as the empty-guns fallback)
 *   sniper.bond_hit(frac)                    an AI shot on Bond takes exactly frac of a health bar (0 = the stock damage)
 *   sniper.fog([start_m [, full_m]]) -> start_m, full_m   the generated level's fog band in metres (no fog nearer than start, full at
 *                                            full; 0 = as far as the band reaches): set and/or read; nothing on a level without one
 *   (presentation: point, clear, binoculars, thermal; sim: zoom_limit, prone, no_unarmed)
 * In the test table:
 *   test.aim(p, on [, fov])                  aim mode forced on (the real scope or binoculars) at that zoom (0 keeps the zoom) */
#include <string.h>
#include "lua.h"
#include "lauxlib.h"
#include "../include/port_lua.h"

extern int portSniperSetMinFov(const char *item, float fov);
extern int portSniperView(int p, int *aiming, int *item, float *fov, int *stance);
extern void portSniperSetProne(int p, int on);
extern int portSniperProne(int p);
extern void portSniperProneTune(float eye, float speed, float sway);
extern int portSniperPoint(int id, int kind, float x, float y, float z, float alpha, float size);
extern void portSniperClearPoints(void);
extern void portSniperBinoculars(int on);
extern void portSniperThermal(int on, float dim);
extern void portSniperTestAim(int p, int on, float fov);
extern void portSniperSetNoUnarmed(int on);
extern void portSniperSetBondHit(float frac);
extern void portLevelFogMetres(float start, float full);
extern int portLevelFogGet(float *start, float *full);

static int snZoomLimit(lua_State *L)
{
    const char *item = luaL_checkstring(L, 1);
    float fov = (float) luaL_checknumber(L, 2);

    if (!portSniperSetMinFov(item, fov)) return luaL_error(L, "sniper.zoom_limit: '%s' does not zoom (sniperrifle, camera)", item);
    lua_pushboolean(L, 1);
    return 1;
}

static int snView(lua_State *L)
{
    int aiming, item, stance;
    float fov;

    if (!portSniperView((int) luaL_checkinteger(L, 1), &aiming, &item, &fov, &stance)) return 0;
    lua_pushboolean(L, aiming);
    lua_pushinteger(L, item);
    lua_pushnumber(L, fov);
    lua_pushinteger(L, stance);
    return 4;
}

static int snProne(lua_State *L)
{
    int p = (int) luaL_checkinteger(L, 1);

    if (!lua_isnoneornil(L, 2)) portSniperSetProne(p, lua_toboolean(L, 2));
    lua_pushboolean(L, portSniperProne(p));
    return 1;
}

static int snProneTune(lua_State *L)
{
    portSniperProneTune((float) luaL_checknumber(L, 1), (float) luaL_checknumber(L, 2), (float) luaL_checknumber(L, 3));
    return 0;
}

static int snPoint(lua_State *L)
{
    int id = (int) luaL_checkinteger(L, 1);
    const char *k = luaL_checkstring(L, 2);
    int kind = strcmp(k, "glint") == 0 ? 1 : strcmp(k, "marker") == 0 ? 2 : strcmp(k, "tag") == 0 ? 3 : strcmp(k, "off") == 0 ? 0 : -1;

    if (kind < 0) return luaL_error(L, "sniper.point: unknown kind '%s' (glint, marker, tag, off)", k);
    if (id < 0 || id > 23) return luaL_error(L, "sniper.point: id %d out of 0..23", id);
    portSniperPoint(id, kind, (float) luaL_optnumber(L, 3, 0), (float) luaL_optnumber(L, 4, 0), (float) luaL_optnumber(L, 5, 0),
                    (float) luaL_optnumber(L, 6, 1.0), (float) luaL_optnumber(L, 7, 0));
    return 0;
}

static int snNoUnarmed(lua_State *L) { portSniperSetNoUnarmed(lua_toboolean(L, 1)); return 0; }
static int snBondHit(lua_State *L) { portSniperSetBondHit((float) luaL_checknumber(L, 1)); return 0; }

static int snFog(lua_State *L)
{
    float a, b;

    if (!lua_isnoneornil(L, 1)) portLevelFogMetres((float) (luaL_checknumber(L, 1) * 100.0), (float) (luaL_optnumber(L, 2, 0) * 100.0));
    if (!portLevelFogGet(&a, &b)) return 0;
    lua_pushnumber(L, a / 100.0);
    lua_pushnumber(L, b / 100.0);
    return 2;
}
static int snClear(lua_State *L) { (void) L; portSniperClearPoints(); return 0; }
static int snBinoc(lua_State *L) { portSniperBinoculars(lua_toboolean(L, 1)); return 0; }
static int snThermal(lua_State *L) { portSniperThermal(lua_toboolean(L, 1), (float) luaL_optnumber(L, 2, 0.55)); return 0; }

static int snTestAim(lua_State *L)
{
    portSniperTestAim((int) luaL_checkinteger(L, 1), lua_toboolean(L, 2), (float) luaL_optnumber(L, 3, 0));
    return 0;
}

void portLuaSniperRegister(lua_State *L)
{
    static const luaL_Reg lib[] = { { "zoom_limit", snZoomLimit }, { "view", snView }, { "prone", snProne }, { "prone_tune", snProneTune },
        { "point", snPoint }, { "clear", snClear }, { "binoculars", snBinoc }, { "thermal", snThermal }, { "no_unarmed", snNoUnarmed },
        { "bond_hit", snBondHit }, { "fog", snFog }, { NULL, NULL } };
    static const luaL_Reg testAdd[] = { { "aim", snTestAim }, { NULL, NULL } };
    extern void portLuaAddTo(lua_State *L, const char *lib, const luaL_Reg *reg);

    luaL_newlib(L, lib);
    lua_setglobal(L, "sniper");
    portLuaAddTo(L, "test", testAdd);
}
