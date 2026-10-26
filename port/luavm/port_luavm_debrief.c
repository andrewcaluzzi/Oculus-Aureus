/* port_luavm_debrief.c - Lua bindings for the heist DEBRIEF panel (designs/bank-heist.md section 26; port/src/port_debrief.c).
 * Presentation only: the panel is this machine's; nothing here is sim state, and the records file's contents never come back to the
 * script (they are one machine's: the Lua arena is sim state in the rollback snapshot).
 *   debrief.set{ kicker =, rating =, rating_rgba =, flavour =, payout =, payout_note =, title =,
 *                rows = { { label, value [, rgba] }, ... }, medals = { { name, desc, earned }, ... } }   replaces the content
 *   debrief.record{ difficulty =, win =, payout =, time =, rating =, result = }   counts the run in Data/Saves/heist_records.json (next to
 *                                the exe; a fixed name), saves it, puts NEW BEST / BEST TIME and the best line on the panel, and logs
 *                                "HEIST RECORD {json}". Call it after debrief.set.
 *   debrief.show([on])           open (default) or close the panel on this machine (it then takes the input until dismissed)
 *   debrief.log(text)            one log line of up to 2000 characters (print() stops at 511)
 * In the test table (modding/lua.md "The test table"; game logic must never read them: one machine's view and file):
 *   test.debrief_open() -> bool
 *   test.debrief_record_info([difficulty]) -> new_best, runs, wins, best_payout, saved
 *   test.debrief_path() -> path */
#include <stdlib.h>
#include <string.h>
#include "lua.h"
#include "lauxlib.h"

extern void portDebriefClear(void);
extern void portDebriefSetText(const char *field, const char *v);
extern void portDebriefSetRatingColour(unsigned int rgba);
extern void portDebriefAddRow(const char *label, const char *value, unsigned int rgba);
extern void portDebriefAddMedal(const char *name, const char *desc, int earned);
extern void portDebriefShow(int on);
extern int portDebriefIsOpen(void);
extern const char *portDebriefRecordsPath(void);
extern void portDebriefRecord(int diff, int win, double payout, double time, const char *rating, const char *result);
extern void portDebriefRecordInfo(int diff, int *newBest, int *runs, int *wins, double *best, int *saved);

static const char *field(lua_State *L, int t, int i)
{
    const char *s;

    lua_rawgeti(L, t, i);
    s = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
    lua_pop(L, 1);   /* (the string stays alive: it is in the table) */
    return s;
}

static int lSet(lua_State *L)
{
    static const char *const texts[] = { "kicker", "rating", "flavour", "payout", "payout_note", "title" };
    size_t k;
    lua_Integer i, n;

    luaL_checktype(L, 1, LUA_TTABLE);
    portDebriefClear();
    for (k = 0; k < sizeof(texts) / sizeof(texts[0]); k++)
    {
        lua_getfield(L, 1, texts[k]);
        if (lua_isstring(L, -1)) portDebriefSetText(texts[k], lua_tostring(L, -1));
        lua_pop(L, 1);
    }
    lua_getfield(L, 1, "rating_rgba");
    if (lua_isinteger(L, -1)) portDebriefSetRatingColour((unsigned int) (lua_tointeger(L, -1) & 0xFFFFFFFF));
    lua_pop(L, 1);
    lua_getfield(L, 1, "rows");
    if (lua_istable(L, -1))
    {
        const int t = lua_gettop(L);

        n = (lua_Integer) lua_rawlen(L, t);
        for (i = 1; i <= n; i++)
        {
            lua_rawgeti(L, t, i);
            if (lua_istable(L, -1))
            {
                const int r = lua_gettop(L);
                unsigned int rgba = 0;

                lua_rawgeti(L, r, 3);
                if (lua_isinteger(L, -1)) rgba = (unsigned int) (lua_tointeger(L, -1) & 0xFFFFFFFF);
                lua_pop(L, 1);
                portDebriefAddRow(field(L, r, 1), field(L, r, 2), rgba);
            }
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    lua_getfield(L, 1, "medals");
    if (lua_istable(L, -1))
    {
        const int t = lua_gettop(L);

        n = (lua_Integer) lua_rawlen(L, t);
        for (i = 1; i <= n; i++)
        {
            lua_rawgeti(L, t, i);
            if (lua_istable(L, -1))
            {
                const int r = lua_gettop(L);
                int earned;

                lua_rawgeti(L, r, 3);
                earned = lua_toboolean(L, -1);
                lua_pop(L, 1);
                portDebriefAddMedal(field(L, r, 1), field(L, r, 2), earned);
            }
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    return 0;
}

static int lRecord(lua_State *L)
{
    int diff, win;
    double payout, t;
    const char *rating, *result;

    luaL_checktype(L, 1, LUA_TTABLE);
    lua_getfield(L, 1, "difficulty"); diff = (int) luaL_optinteger(L, -1, 1); lua_pop(L, 1);
    lua_getfield(L, 1, "win"); win = lua_toboolean(L, -1); lua_pop(L, 1);
    lua_getfield(L, 1, "payout"); payout = luaL_optnumber(L, -1, 0); lua_pop(L, 1);
    lua_getfield(L, 1, "time"); t = luaL_optnumber(L, -1, 0); lua_pop(L, 1);
    lua_getfield(L, 1, "rating"); rating = luaL_optstring(L, -1, "?"); lua_pop(L, 1);
    lua_getfield(L, 1, "result"); result = luaL_optstring(L, -1, win ? "escaped" : "failed"); lua_pop(L, 1);
    portDebriefRecord(diff, win, payout, t, rating, result);   /* (the strings stay alive: they are in the table) */
    return 0;
}

static int lRecordInfo(lua_State *L)
{
    int nb = 0, runs = 0, wins = 0, saved = 0;
    double best = 0;

    portDebriefRecordInfo((int) luaL_optinteger(L, 1, 1), &nb, &runs, &wins, &best, &saved);
    lua_pushboolean(L, nb);
    lua_pushinteger(L, runs);
    lua_pushinteger(L, wins);
    lua_pushnumber(L, best);
    lua_pushboolean(L, saved);
    return 5;
}

/* one log line of up to 2000 characters (print() stops at 511: the HEIST DEBRIEF {json} line is longer) */
static int lLog(lua_State *L)
{
    extern void portConLogf(const char *fmt, ...);

    portConLogf("%.2000s\n", luaL_checkstring(L, 1));
    return 0;
}

static int lShow(lua_State *L) { portDebriefShow(lua_isnoneornil(L, 1) ? 1 : lua_toboolean(L, 1)); return 0; }
static int lIsOpen(lua_State *L) { lua_pushboolean(L, portDebriefIsOpen()); return 1; }
static int lPath(lua_State *L) { lua_pushstring(L, portDebriefRecordsPath()); return 1; }

void portLuaDebriefRegister(lua_State *L)
{
    static const luaL_Reg lib[] = { { "set", lSet }, { "record", lRecord }, { "show", lShow }, { "log", lLog }, { NULL, NULL } };
    static const luaL_Reg testAdd[] = { { "debrief_open", lIsOpen }, { "debrief_record_info", lRecordInfo }, { "debrief_path", lPath },
                                        { NULL, NULL } };
    extern void portLuaAddTo(lua_State *L, const char *lib, const luaL_Reg *reg);

    luaL_newlib(L, lib);
    lua_setglobal(L, "debrief");
    portLuaAddTo(L, "test", testAdd);
}
