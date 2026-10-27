/* port_luavm_arenamenu.c - the `amenu` table: the arena's start menu and post-game (modding/arena.md "Start menu and post-game";
 * tools/mode_arena_menu.lua, the lobby, and tools/arena_flow.lua, in every arena run). C side: port/src/port_arenamenu.c (the hand-off
 * between stages, the preferences file) and port/src/port_arenamenu_draw.c (the drawing, presentation).
 *
 *   amenu.launch{mode=, settings=, preset=, label=, lobby=, coop=, slots=} -> bool   end this stage and start that one (the Colosseum
 *                                     again, no mission report): mode = its GE_MODE, settings = an ARENA_SETTINGS table constructor
 *                                     ("{ waves = 10 }"), lobby = "menu" / "postgame" for the lobby, coop = true / false (GE_COOP; nil
 *                                     leaves it), slots = GE_COLOSSEUM_SLOTS (nil leaves it)
 *   amenu.arm{...}                    the same launch, for when this stage ends by itself (the run: death, the win, an abort)
 *   amenu.disarm()                    nothing after this stage (the stock report)
 *   amenu.quit() -> bool              end this stage and go to the Files screen
 *   amenu.current() -> t | nil        what this stage was launched with by the hand-off (the fields above); nil = a plain launch
 *   amenu.retry([t]) -> t | nil       the last run's own launch (set by the run, read by the post-game)
 *   amenu.result([text]) -> text      the last run's result (a Lua table constructor, set by the run)
 *   amenu.prefs() -> {key = "value"}  the menu's saved choices (Data/Saves/arena_menu.ini)
 *   amenu.save_prefs{key = value}     replace them and write the file (values become strings)
 *   amenu.view{...}                   the menu as it is now (presentation; see port_arenamenu_draw.c and the lobby script)
 *   amenu.escape_back(on)             Escape is the board's BACK while the lobby holds the controls (not the watch)
 *   amenu.netplay() -> bool           a netplay session (the menu flow is off there)
 * MODE tier. Inert outside a mode stage. */
#include <string.h>
#include <stdio.h>
#include "lua.h"
#include "lauxlib.h"
#include "../include/port_arenamenu.h"

extern int portNetActive(void);
extern int portModeActive(void);

static const char *optField(lua_State *L, int t, const char *k, char *buf, size_t n)
{
    lua_getfield(L, t, k);
    if (lua_isstring(L, -1)) snprintf(buf, n, "%s", lua_tostring(L, -1));
    else buf[0] = '\0';
    lua_pop(L, 1);
    return buf;
}

static void readLaunch(lua_State *L, int t, AmLaunch *out)
{
    static char settings[AM_SETTINGS_LEN];
    char mode[32], preset[24], label[64], lobby[16];
    int coop = -1, slots = 0;

    luaL_checktype(L, t, LUA_TTABLE);
    optField(L, t, "mode", mode, sizeof(mode));
    optField(L, t, "settings", settings, sizeof(settings));
    optField(L, t, "preset", preset, sizeof(preset));
    optField(L, t, "label", label, sizeof(label));
    optField(L, t, "lobby", lobby, sizeof(lobby));
    lua_getfield(L, t, "coop");
    if (lua_isboolean(L, -1)) coop = lua_toboolean(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, t, "slots");
    if (lua_isnumber(L, -1)) slots = (int) lua_tointeger(L, -1);
    lua_pop(L, 1);
    portArenaMenuLaunchSet(out, mode, settings, preset, label, lobby, coop, slots);
}

static void pushLaunch(lua_State *L, const AmLaunch *l)
{
    lua_createtable(L, 0, 7);
    lua_pushstring(L, l->mode); lua_setfield(L, -2, "mode");
    lua_pushstring(L, l->settings); lua_setfield(L, -2, "settings");
    lua_pushstring(L, l->preset); lua_setfield(L, -2, "preset");
    lua_pushstring(L, l->label); lua_setfield(L, -2, "label");
    lua_pushstring(L, l->lobby); lua_setfield(L, -2, "lobby");
    if (l->coop >= 0) { lua_pushboolean(L, l->coop); lua_setfield(L, -2, "coop"); }
    if (l->slots > 0) { lua_pushinteger(L, l->slots); lua_setfield(L, -2, "slots"); }
}

static int lLaunch(lua_State *L) { AmLaunch l; readLaunch(L, 1, &l); lua_pushboolean(L, portArenaMenuLaunchNow(&l)); return 1; }
static int lArm(lua_State *L) { AmLaunch l; readLaunch(L, 1, &l); if (portModeActive()) portArenaMenuArm(&l); return 0; }
static int lDisarm(lua_State *L) { (void) L; portArenaMenuDisarm(); return 0; }
static int lQuit(lua_State *L) { lua_pushboolean(L, portArenaMenuQuitNow()); return 1; }
static int lNetplay(lua_State *L) { lua_pushboolean(L, portNetActive()); return 1; }
static int lEscape(lua_State *L) { portArenaMenuEscapeBack(lua_toboolean(L, 1)); return 0; }

static int lCurrent(lua_State *L)
{
    const AmLaunch *l = portArenaMenuCurrent();

    if (l == NULL) return 0;
    pushLaunch(L, l);
    return 1;
}

/* amenu.state() -> menu_up, gameplay_has_pointer, mouse_captured: whether the menu holds the mouse on this machine, whether gameplay
 * owns the pointer (the smart lock's predicate), and whether the pointer is captured now (tests; presentation: never steer the game
 * with it) */
static int lState(lua_State *L)
{
    extern int portGameplayHasPointer(void);   /* bondview2.c */
    extern int portInputMouseLocked(void);     /* input_pc.c */

    lua_pushboolean(L, portArenaMenuViewOn());
    lua_pushboolean(L, portGameplayHasPointer());
    lua_pushboolean(L, portInputMouseLocked());
    return 3;
}

static int lRetry(lua_State *L)
{
    const AmLaunch *l;

    if (lua_istable(L, 1))
    {
        AmLaunch n;

        readLaunch(L, 1, &n);
        portArenaMenuRetrySet(&n);
    }
    l = portArenaMenuRetry();
    if (l == NULL) return 0;
    pushLaunch(L, l);
    return 1;
}

static int lResult(lua_State *L)
{
    if (lua_isstring(L, 1)) portArenaMenuResultSet(lua_tostring(L, 1));
    lua_pushstring(L, portArenaMenuResult());
    return 1;
}

static int lPrefs(lua_State *L)
{
    int i, n;

    if (portNetActive()) { lua_newtable(L); return 1; }
    n = portArenaMenuPrefCount();
    lua_createtable(L, 0, n);
    for (i = 0; i < n; i++)
    {
        const char *v = NULL, *k = portArenaMenuPref(i, &v);

        if (k == NULL) break;
        lua_pushstring(L, v);
        lua_setfield(L, -2, k);
    }
    return 1;
}

static int lSavePrefs(lua_State *L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    if (portNetActive()) { lua_pushboolean(L, 0); return 1; }
    portArenaMenuPrefClear();
    lua_pushnil(L);
    while (lua_next(L, 1) != 0)
    {   /* (the file's order is the table's; the reader does not care) */
        if (lua_type(L, -2) == LUA_TSTRING)
        {
            const char *v = lua_isboolean(L, -1) ? (lua_toboolean(L, -1) ? "true" : "false") : lua_tostring(L, -1);

            if (v != NULL) portArenaMenuPrefPut(lua_tostring(L, -2), v);
        }
        lua_pop(L, 1);
    }
    lua_pushboolean(L, portArenaMenuPrefSave());
    return 1;
}

/* ---- the view ---- */

static void str(lua_State *L, int t, const char *k, char *d, size_t n) { optField(L, t, k, d, n); }

static int intField(lua_State *L, int t, const char *k, int def)
{
    int v = def;

    lua_getfield(L, t, k);
    if (lua_isnumber(L, -1)) v = (int) lua_tointeger(L, -1);
    else if (lua_isboolean(L, -1)) v = lua_toboolean(L, -1);
    lua_pop(L, 1);
    return v;
}

static int kindOf(const char *k)
{
    if (strcmp(k, "header") == 0) return AMI_HEADER;
    if (strcmp(k, "preset") == 0) return AMI_PRESET;
    if (strcmp(k, "choice") == 0) return AMI_CHOICE;
    if (strcmp(k, "primary") == 0) return AMI_PRIMARY;
    return AMI_BUTTON;
}

static int lView(lua_State *L)
{
    static AmView v;
    char buf[16];
    int i, n;

    luaL_checktype(L, 1, LUA_TTABLE);
    memset(&v, 0, sizeof(v));
    str(L, 1, "screen", buf, sizeof(buf));
    v.screen = strcmp(buf, "main") == 0 ? AMV_MAIN : (strcmp(buf, "advanced") == 0 ? AMV_ADVANCED : (strcmp(buf, "postgame") == 0 ? AMV_POSTGAME : AMV_OFF));
    v.cursor = intField(L, 1, "cursor", 0);
    v.medal = intField(L, 1, "medal", -1);
    v.medalNew = intField(L, 1, "medal_new", 0);
    v.bannerRgba = (unsigned int) intField(L, 1, "banner_rgba", 0);
    str(L, 1, "title", v.title, sizeof(v.title));
    str(L, 1, "kicker", v.kicker, sizeof(v.kicker));
    str(L, 1, "subtitle", v.subtitle, sizeof(v.subtitle));
    str(L, 1, "desc", v.desc, sizeof(v.desc));
    str(L, 1, "footer", v.footer, sizeof(v.footer));
    str(L, 1, "banner", v.banner, sizeof(v.banner));
    str(L, 1, "news", v.news, sizeof(v.news));
    str(L, 1, "info_title", v.infoTitle, sizeof(v.infoTitle));
    str(L, 1, "info_sub", v.infoSub, sizeof(v.infoSub));
    str(L, 1, "medal_name", v.medalName, sizeof(v.medalName));
    str(L, 1, "chart", v.chart, sizeof(v.chart));
    str(L, 1, "legend", v.legend, sizeof(v.legend));
    str(L, 1, "best_chip", v.bestChip, sizeof(v.bestChip));
    str(L, 1, "new_medal", v.newMedal, sizeof(v.newMedal));
    lua_getfield(L, 1, "items");
    if (lua_istable(L, -1))
    {
        const int t = lua_gettop(L);

        n = (int) lua_rawlen(L, t);
        for (i = 0; i < n && v.n < AMV_ITEMS; i++)
        {
            AmItem *it = &v.item[v.n];

            lua_rawgeti(L, t, i + 1);
            if (lua_istable(L, -1))
            {
                const int e = lua_gettop(L);

                str(L, e, "kind", buf, sizeof(buf));
                it->kind = (signed char) kindOf(buf);
                it->col = (signed char) intField(L, e, "col", 1);
                it->medal = (signed char) intField(L, e, "medal", 0);
                it->chosen = (signed char) intField(L, e, "chosen", 0);
                it->dim = (signed char) intField(L, e, "dim", 0);
                str(L, e, "label", it->label, sizeof(it->label));
                str(L, e, "value", it->value, sizeof(it->value));
                str(L, e, "sub", it->sub, sizeof(it->sub));
                str(L, e, "right", it->right, sizeof(it->right));
                v.n++;
            }
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    lua_getfield(L, 1, "info");
    if (lua_istable(L, -1))
    {
        const int t = lua_gettop(L);

        n = (int) lua_rawlen(L, t);
        for (i = 0; i < n && v.ninfo < AMV_INFO; i++)
        {
            AmInfo *f = &v.info[v.ninfo];

            lua_rawgeti(L, t, i + 1);
            if (lua_istable(L, -1))
            {
                const int e = lua_gettop(L);

                lua_rawgeti(L, e, 1); snprintf(f->label, sizeof(f->label), "%s", lua_isstring(L, -1) ? lua_tostring(L, -1) : ""); lua_pop(L, 1);
                lua_rawgeti(L, e, 2); snprintf(f->value, sizeof(f->value), "%s", lua_isstring(L, -1) ? lua_tostring(L, -1) : ""); lua_pop(L, 1);
                lua_rawgeti(L, e, 3); f->rgba = lua_isnumber(L, -1) ? (unsigned int) (lua_tointeger(L, -1) & 0xFFFFFFFF) : 0; lua_pop(L, 1);
                lua_rawgeti(L, e, 4); f->flag = (signed char) lua_toboolean(L, -1); lua_pop(L, 1);
                v.ninfo++;
            }
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    lua_getfield(L, 1, "bars");
    if (lua_istable(L, -1))
    {
        const int t = lua_gettop(L);

        n = (int) lua_rawlen(L, t);
        for (i = 0; i < n && v.nbars < AMV_BARS; i++)
        {
            lua_rawgeti(L, t, i + 1);
            if (lua_istable(L, -1))
            {
                lua_rawgeti(L, -1, 1); v.bar[v.nbars].v = (short) lua_tointeger(L, -1); lua_pop(L, 1);
                lua_rawgeti(L, -1, 2); v.bar[v.nbars].kind = (signed char) lua_tointeger(L, -1); lua_pop(L, 1);
                v.nbars++;
            }
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    portArenaMenuViewSet(&v);
    return 0;
}

void portLuaArenaMenuRegister(lua_State *L)
{
    static const luaL_Reg lib[] = {
        { "launch", lLaunch }, { "arm", lArm }, { "disarm", lDisarm }, { "quit", lQuit }, { "current", lCurrent }, { "retry", lRetry },
        { "result", lResult }, { "prefs", lPrefs }, { "save_prefs", lSavePrefs }, { "view", lView }, { "escape_back", lEscape },
        { "netplay", lNetplay }, { "state", lState }, { NULL, NULL } };

    luaL_newlib(L, lib);
    lua_setglobal(L, "amenu");
}
