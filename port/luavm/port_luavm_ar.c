/* port_luavm_ar.c - Lua bindings for the AR overlay and the diagnostics layer (port/src/port_arhud.c; designs/bank-heist.md section 20).
 * Presentation only: nothing here changes the sim (test.freeze, registered here, is the one test switch that does; solo only).
 *
 *   ar.on(bool) / ar.enabled() -> bool       the specs (off until a mode turns them on; Shift+F3 / console `ar` toggle them)
 *   ar.layer(name [, bool]) -> bool          suspicion, speech, keys, points, doors, cameras, radar
 *   ar.sound(bool)                           bubbles with or without their blip
 *   ar.config{ range = 1800, cone = 75 }     the sight cone the diagnostics draw (the heist passes cover.tune's)
 *   ar.mark(chr, opts | label)               a tag under the chr: opts = { label = "", rgba = 0xRRGGBBAA, bracket = bool }
 *   ar.say(chr, text [, secs [, kind]])      a speech bubble near the chr's face (kind "hey", "radio", "huh", ... or 0..3)
 *   ar.outline(chr, rgba | nil)              bracket corners and a glow in that colour (nil clears)
 *   ar.clear([chr])                          the chr's mark, outline and bubble (no chr: every one)
 *   ar.point(x, y, z, label [, rgba]) -> id  a waypoint diamond with its distance (the same label moves it); ar.unpoint([label])
 *   ar.noise(x, y, z, radius [, kind])       an expanding ring
 *   ar.reset()                               forget every mark, point, ring and label (a new stage)
 *   diag.on() / diag.off() / diag.enabled()  the diagnostics layer (F3, console `diag`)
 *   diag.door(idx, label [, rgba])           a door's rule label in the diagnostics
 *   diag.room(id, label)                     a room's name and zone in the diagnostics panel
 *   diag.text(n, text)                       panel lines 1..4 for the mode's own numbers
 *   test.freeze(bool) -> ok                  (the test table) no AI list runs (SIM-AFFECTING; refused in co-op and netplay)
 * Colours: an integer 0xRRGGBBAA or a name (red green blue gold white cyan yellow orange). */
#include <string.h>
#include "lua.h"
#include "lauxlib.h"

extern void portArMark(int chrnum, const char *label, unsigned rgba, int bracket);
extern void portArOutline(int chrnum, unsigned rgba);
extern void portArSay(int chrnum, const char *text, float secs, int kind);
extern int portArPoint(float x, float y, float z, const char *label, unsigned rgba);
extern void portArPointClear(const char *label);
extern void portArClear(int chrnum);
extern void portArNoise(float x, float y, float z, float r, int kind);
extern void portArDoorRule(int idx, const char *label, unsigned rgba);
extern void portArRoomLabel(int room, const char *label);
extern void portArDiagText(int line, const char *text);
extern void portArConfig(float coneRange, float coneDeg);
extern void portArSetOn(int on);
extern int portArIsOn(void);
extern void portArSetSound(int on);
extern int portArLayer(const char *name, int on);
extern void portArDiagSet(int on);
extern int portArDiagIsOn(void);
extern int portArFreezeSet(int on);
extern void portArReset(void);

static unsigned colourArg(lua_State *L, int i, unsigned dflt)
{
    static const struct { const char *n; unsigned c; } names[] = {
        { "red", 0xFF3838FFu }, { "green", 0x40FF60FFu }, { "blue", 0x4080FFFFu }, { "gold", 0xFFD040FFu }, { "white", 0xFFFFFFFFu },
        { "cyan", 0x60E8FFFFu }, { "yellow", 0xFFE040FFu }, { "orange", 0xFF9020FFu }, { NULL, 0 } };
    int k;

    if (lua_isnoneornil(L, i)) return dflt;
    if (lua_type(L, i) == LUA_TSTRING)
    {
        const char *s = lua_tostring(L, i);

        for (k = 0; names[k].n != NULL; k++) if (strcmp(names[k].n, s) == 0) return names[k].c;
        return dflt;
    }
    return (unsigned) (lua_Unsigned) luaL_checkinteger(L, i);
}

static int sayKind(lua_State *L, int i)
{
    if (lua_type(L, i) == LUA_TSTRING)
    {
        const char *k = lua_tostring(L, i);

        if (strcmp(k, "hey") == 0 || strcmp(k, "found_body") == 0 || strcmp(k, "backup") == 0 || strcmp(k, "alert") == 0) return 1;
        if (strcmp(k, "radio") == 0) return 2;
        if (strcmp(k, "huh") == 0 || strcmp(k, "investigate") == 0 || strcmp(k, "lost") == 0) return 3;
        return 0;
    }
    return (int) luaL_optinteger(L, i, 0);
}

static int arOn(lua_State *L) { portArSetOn(lua_isnoneornil(L, 1) ? 1 : lua_toboolean(L, 1)); return 0; }
static int arEnabled(lua_State *L) { lua_pushboolean(L, portArIsOn()); return 1; }
static int arSound(lua_State *L) { portArSetSound(lua_toboolean(L, 1)); return 0; }

static int arLayer(lua_State *L)
{
    int r = portArLayer(luaL_checkstring(L, 1), lua_isnoneornil(L, 2) ? -1 : lua_toboolean(L, 2));

    if (r < 0) return luaL_error(L, "ar.layer: no layer '%s' (suspicion speech keys points doors cameras radar)", lua_tostring(L, 1));
    lua_pushboolean(L, r);
    return 1;
}

static int arConfig(lua_State *L)
{
    float range = 0.0f, cone = 0.0f;

    luaL_checktype(L, 1, LUA_TTABLE);
    if (lua_getfield(L, 1, "range") == LUA_TNUMBER) range = (float) lua_tonumber(L, -1);
    if (lua_getfield(L, 1, "cone") == LUA_TNUMBER) cone = (float) lua_tonumber(L, -1);
    lua_pop(L, 2);
    portArConfig(range, cone);
    return 0;
}

static int arMark(lua_State *L)
{
    int chr = (int) luaL_checkinteger(L, 1);
    const char *label = "";
    unsigned rgba = 0xFFFFFFFFu;
    int bracket = 0;

    if (lua_type(L, 2) == LUA_TSTRING) label = lua_tostring(L, 2);
    else if (lua_istable(L, 2))
    {
        if (lua_getfield(L, 2, "label") == LUA_TSTRING) label = lua_tostring(L, -1);
        lua_getfield(L, 2, "rgba");
        rgba = colourArg(L, lua_gettop(L), rgba);
        lua_getfield(L, 2, "bracket");
        bracket = lua_toboolean(L, -1);
        portArMark(chr, label, rgba, bracket);   /* (label stays on the stack until here) */
        lua_pop(L, 3);
        return 0;
    }
    portArMark(chr, label, rgba, bracket);
    return 0;
}

static int arSay(lua_State *L)
{
    portArSay((int) luaL_checkinteger(L, 1), luaL_checkstring(L, 2), (float) luaL_optnumber(L, 3, 3.0), sayKind(L, 4));
    return 0;
}

static int arOutline(lua_State *L)
{
    portArOutline((int) luaL_checkinteger(L, 1), colourArg(L, 2, 0));
    return 0;
}

static int arClear(lua_State *L)
{
    portArClear(lua_isnoneornil(L, 1) ? -1 : (int) luaL_checkinteger(L, 1));
    return 0;
}

static int arPoint(lua_State *L)
{
    int id = portArPoint((float) luaL_checknumber(L, 1), (float) luaL_checknumber(L, 2), (float) luaL_checknumber(L, 3),
                         luaL_optstring(L, 4, ""), colourArg(L, 5, 0));

    if (id < 0) lua_pushnil(L); else lua_pushinteger(L, id);
    return 1;
}

static int arUnpoint(lua_State *L)
{
    portArPointClear(lua_isnoneornil(L, 1) ? NULL : luaL_checkstring(L, 1));
    return 0;
}

static int arNoise(lua_State *L)
{
    int kind = 1;

    if (lua_type(L, 5) == LUA_TSTRING)
    {
        const char *k = lua_tostring(L, 5);

        kind = strcmp(k, "shot") == 0 ? 6 : (strcmp(k, "slap") == 0 ? 0 : (strcmp(k, "thud") == 0 ? 5 : 1));
    }
    else if (lua_isinteger(L, 5)) kind = (int) lua_tointeger(L, 5);
    portArNoise((float) luaL_checknumber(L, 1), (float) luaL_checknumber(L, 2), (float) luaL_checknumber(L, 3), (float) luaL_checknumber(L, 4), kind);
    return 0;
}

static int arReset(lua_State *L) { (void) L; portArReset(); return 0; }

static int diagOn(lua_State *L) { (void) L; portArDiagSet(1); return 0; }
static int diagOff(lua_State *L) { (void) L; portArDiagSet(0); return 0; }
static int diagEnabled(lua_State *L) { lua_pushboolean(L, portArDiagIsOn()); return 1; }
static int diagDoor(lua_State *L) { portArDoorRule((int) luaL_checkinteger(L, 1), luaL_optstring(L, 2, ""), colourArg(L, 3, 0)); return 0; }
static int diagRoom(lua_State *L) { portArRoomLabel((int) luaL_checkinteger(L, 1), luaL_optstring(L, 2, "")); return 0; }
static int diagText(lua_State *L) { portArDiagText((int) luaL_checkinteger(L, 1), luaL_optstring(L, 2, "")); return 0; }
static int diagFreeze(lua_State *L) { lua_pushboolean(L, portArFreezeSet(lua_toboolean(L, 1))); return 1; }

void portLuaArRegister(lua_State *L)
{
    static const luaL_Reg arLib[] = { { "on", arOn }, { "enabled", arEnabled }, { "layer", arLayer }, { "sound", arSound }, { "config", arConfig },
        { "mark", arMark }, { "say", arSay }, { "outline", arOutline }, { "clear", arClear }, { "point", arPoint }, { "unpoint", arUnpoint },
        { "noise", arNoise }, { "reset", arReset }, { NULL, NULL } };
    static const luaL_Reg diagLib[] = { { "on", diagOn }, { "off", diagOff }, { "enabled", diagEnabled }, { "door", diagDoor },
        { "room", diagRoom }, { "text", diagText }, { NULL, NULL } };
    static const luaL_Reg testAdd[] = { { "freeze", diagFreeze }, { NULL, NULL } };
    extern void portLuaAddTo(lua_State *L, const char *lib, const luaL_Reg *reg);

    luaL_newlib(L, arLib);
    lua_setglobal(L, "ar");
    luaL_newlib(L, diagLib);
    lua_setglobal(L, "diag");
    portLuaAddTo(L, "test", testAdd);
}
