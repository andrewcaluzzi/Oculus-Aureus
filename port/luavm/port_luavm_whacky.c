/* port_luavm_whacky.c - Lua bindings for the arena's Whacky layer (port/include/port_whacky.h; modding/arena.md "Whacky").
 * Kept in its own file (like port_luavm_arena.c); portLuaWhackyRegister is called once from portLuaState.
 *
 * chrfx (PRESENTATION: may differ per machine; never read by the sim)
 *   chrfx.set(chr, {tint = {r, g, b} | "gold" ..., amount = 0..1, alpha = 0..1, anim = "cycle" | "pulse" | "pulse2" | "flicker" |
 *             "shimmer" | "none", speed = cycles a second, lo = 0..1, tint2 = {r, g, b} | name}) -> true
 *   chrfx.clear(chr) -> bool    chrfx.clear_all()    chrfx.count() -> n
 *   colour names: gold black shadow white ghost glass red orange yellow green cyan blue purple pink steel
 * whacky (SIM: call it from deterministic mode code only)
 *   whacky.anim_rate([r]) -> rate        the Fast / Slow Animation cheats' global rate (0.2 .. 4; relative per chr)
 *   whacky.linemode([on]) -> was on      the Line Mode cheat
 *   whacky.give_double(p, kind) -> bool  kind "rocket" | "grenade" | "rcp90" | "throwing_knife" | "knife" | "laser": a 2x cheat pair
 *   whacky.scale(chr, f) -> bool         model and collision size x f (0.1 .. 3)
 *   whacky.launcher(p, shots [, {count=3, scale=0.25, life=15, health=0.3, anim=3, hostile=false, cap=12}]) -> shots
 *   whacky.launcher_shots(p) -> shots left
 *   whacky.minions_at(p, x, z [, n]) -> made   n minions at a floor point Bond p can walk to (crowd gifts, tests)
 *   whacky.minions() -> array of chrnums    whacky.is_minion(chr) -> bool    whacky.minion_stats() -> spawned, expired
 *   whacky.minions_fade() -> n             every live minion fades out now */
#include <string.h>
#include "lua.h"
#include "lauxlib.h"

extern int   portLuaGameStageLive(void);
extern int   portChrFxSetRaw(int chrnum, int r, int g, int b, int amount, int r2, int g2, int b2, int alpha, int anim, int lo, int period);
extern int   portChrFxClear(int chrnum);
extern void  portChrFxClearAll(void);
extern int   portChrFxCount(void);
extern float portWhackyAnimRate(float r);
extern int   portWhackyLineMode(int on);
extern int   portWhackyGiveDouble(int p, int which);
extern int   portWhackyChrScale(int chrnum, float f);
extern int   portWhackyLauncher(int p, int shots, int count, float scale, float life, float health, float anim, int hostile, int cap);
extern int   portWhackyMinionsAt(int p, float x, float z, int n);
extern int   portWhackyIsMinion(int chrnum);
extern int   portWhackyMinionList(int *out, int max);
extern void  portWhackyMinionStats(int *spawned, int *expired);
extern int   portWhackyMinionsFade(void);

static void needStage(lua_State *L, const char *api)
{
    if (!portLuaGameStageLive())
    {
        luaL_error(L, "%s needs a live stage (not available in the menus)", api);
    }
}

/* ---- chrfx ---- */

static const struct { const char *name; int r, g, b; } sColours[] = {
    { "gold", 255, 196, 40 }, { "black", 0, 0, 0 }, { "shadow", 12, 6, 24 }, { "white", 255, 255, 255 }, { "ghost", 170, 220, 255 },
    { "glass", 150, 235, 255 }, { "red", 255, 30, 20 }, { "orange", 255, 120, 0 }, { "yellow", 255, 235, 40 }, { "green", 40, 255, 60 },
    { "cyan", 0, 230, 255 }, { "blue", 40, 90, 255 }, { "purple", 170, 40, 255 }, { "pink", 255, 60, 190 }, { "steel", 120, 160, 210 },
};

static void colourArg(lua_State *L, int idx, int *r, int *g, int *b)
{
    if (lua_type(L, idx) == LUA_TSTRING)
    {
        const char *s = lua_tostring(L, idx);
        int i;

        for (i = 0; i < (int)(sizeof(sColours) / sizeof(sColours[0])); i++)
        {
            if (strcmp(s, sColours[i].name) == 0)
            {
                *r = sColours[i].r;
                *g = sColours[i].g;
                *b = sColours[i].b;
                return;
            }
        }
        luaL_error(L, "chrfx: unknown colour '%s'", s);
    }
    else if (lua_istable(L, idx))
    {
        lua_rawgeti(L, idx, 1);
        *r = (int)luaL_checkinteger(L, -1);
        lua_rawgeti(L, idx, 2);
        *g = (int)luaL_checkinteger(L, -1);
        lua_rawgeti(L, idx, 3);
        *b = (int)luaL_checkinteger(L, -1);
        lua_pop(L, 3);
    }
}

static float numField(lua_State *L, int t, const char *k, float d)
{
    float v = d;

    lua_getfield(L, t, k);
    if (!lua_isnil(L, -1)) v = (float)luaL_checknumber(L, -1);
    lua_pop(L, 1);
    return v;
}

static int fxSet(lua_State *L)
{
    static const char *const anims[] = { "none", "cycle", "pulse", "flicker", "shimmer", "pulse2", NULL };
    int chr = (int)luaL_checkinteger(L, 1);
    int r = 255, g = 255, b = 255, r2 = 255, g2 = 255, b2 = 255, anim = 0;
    float amount, alpha, speed, lo;

    luaL_checktype(L, 2, LUA_TTABLE);
    lua_getfield(L, 2, "tint");
    colourArg(L, lua_gettop(L), &r, &g, &b);
    amount = lua_isnil(L, -1) ? 0.0f : 0.5f;   /* a tint with no amount: half */
    lua_pop(L, 1);
    lua_getfield(L, 2, "tint2");
    colourArg(L, lua_gettop(L), &r2, &g2, &b2);
    lua_pop(L, 1);
    amount = numField(L, 2, "amount", amount);
    alpha = numField(L, 2, "alpha", 1.0f);
    speed = numField(L, 2, "speed", 1.0f);
    lo = numField(L, 2, "lo", 0.3f);
    lua_getfield(L, 2, "anim");
    if (!lua_isnil(L, -1)) anim = luaL_checkoption(L, -1, "none", anims);
    lua_pop(L, 1);
    if (speed < 0.01f) speed = 0.01f;
    lua_pushboolean(L, portChrFxSetRaw(chr, r, g, b, (int)(amount * 255.0f + 0.5f), r2, g2, b2, (int)(alpha * 255.0f + 0.5f), anim,
                                       (int)(lo * 255.0f + 0.5f), (int)(60.0f / speed + 0.5f)));
    return 1;
}

static int fxClear(lua_State *L)
{
    lua_pushboolean(L, portChrFxClear((int)luaL_checkinteger(L, 1)));
    return 1;
}

static int fxClearAll(lua_State *L)
{
    (void)L;
    portChrFxClearAll();
    return 0;
}

static int fxCount(lua_State *L)
{
    lua_pushinteger(L, portChrFxCount());
    return 1;
}

/* ---- whacky ---- */

static int wAnimRate(lua_State *L)
{
    needStage(L, "whacky.anim_rate");
    lua_pushnumber(L, portWhackyAnimRate((float)luaL_optnumber(L, 1, 0.0)));
    return 1;
}

static int wLineMode(lua_State *L)
{
    needStage(L, "whacky.linemode");
    lua_pushboolean(L, portWhackyLineMode(lua_isnoneornil(L, 1) ? -1 : lua_toboolean(L, 1)));
    return 1;
}

static int wGiveDouble(lua_State *L)
{
    static const char *const kinds[] = { "rocket", "grenade", "rcp90", "throwing_knife", "knife", "laser", NULL };
    int p = (int)luaL_checkinteger(L, 1);
    int k = luaL_checkoption(L, 2, NULL, kinds);

    needStage(L, "whacky.give_double");
    lua_pushboolean(L, portWhackyGiveDouble(p, k));
    return 1;
}

static int wScale(lua_State *L)
{
    needStage(L, "whacky.scale");
    lua_pushboolean(L, portWhackyChrScale((int)luaL_checkinteger(L, 1), (float)luaL_checknumber(L, 2)));
    return 1;
}

static int wLauncher(lua_State *L)
{
    int p = (int)luaL_checkinteger(L, 1), shots = (int)luaL_checkinteger(L, 2), count = 3, hostile = 0, cap = 0;
    float scale = 0.25f, life = 15.0f, health = 0.3f, anim = 3.0f;

    needStage(L, "whacky.launcher");
    if (lua_istable(L, 3))
    {
        count = (int)numField(L, 3, "count", 3.0f);
        scale = numField(L, 3, "scale", scale);
        life = numField(L, 3, "life", life);
        health = numField(L, 3, "health", health);
        anim = numField(L, 3, "anim", anim);
        cap = (int)numField(L, 3, "cap", 0.0f);
        lua_getfield(L, 3, "hostile");
        hostile = lua_toboolean(L, -1);
        lua_pop(L, 1);
    }
    if (shots < 0) shots = 0;
    lua_pushinteger(L, portWhackyLauncher(p, shots, count, scale, life, health, anim, hostile, cap));
    return 1;
}

static int wLauncherShots(lua_State *L)
{
    needStage(L, "whacky.launcher_shots");
    lua_pushinteger(L, portWhackyLauncher((int)luaL_checkinteger(L, 1), -1, 0, 0, 0, 0, 0, 0, 0));
    return 1;
}

static int wMinionsAt(lua_State *L)
{
    needStage(L, "whacky.minions_at");
    lua_pushinteger(L, portWhackyMinionsAt((int)luaL_checkinteger(L, 1), (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3),
                                           (int)luaL_optinteger(L, 4, 3)));
    return 1;
}

static int wMinions(lua_State *L)
{
    int list[64], n, i;

    n = portWhackyMinionList(list, 64);
    lua_createtable(L, n, 0);
    for (i = 0; i < n; i++)
    {
        lua_pushinteger(L, list[i]);
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

static int wIsMinion(lua_State *L)
{
    lua_pushboolean(L, portWhackyIsMinion((int)luaL_checkinteger(L, 1)));
    return 1;
}

static int wMinionStats(lua_State *L)
{
    int s, e;

    portWhackyMinionStats(&s, &e);
    lua_pushinteger(L, s);
    lua_pushinteger(L, e);
    return 2;
}

static int wMinionsFade(lua_State *L)
{
    lua_pushinteger(L, portWhackyMinionsFade());
    return 1;
}

void portLuaWhackyRegister(lua_State *L)
{
    static const luaL_Reg fxLib[] = { { "set", fxSet }, { "clear", fxClear }, { "clear_all", fxClearAll }, { "count", fxCount },
                                      { NULL, NULL } };
    static const luaL_Reg wLib[] = { { "anim_rate", wAnimRate }, { "linemode", wLineMode }, { "give_double", wGiveDouble },
                                     { "scale", wScale }, { "launcher", wLauncher }, { "launcher_shots", wLauncherShots },
                                     { "minions_at", wMinionsAt }, { "minions", wMinions }, { "is_minion", wIsMinion },
                                     { "minion_stats", wMinionStats }, { "minions_fade", wMinionsFade }, { NULL, NULL } };

    luaL_newlib(L, fxLib);
    lua_setglobal(L, "chrfx");
    luaL_newlib(L, wLib);
    lua_setglobal(L, "whacky");
}
