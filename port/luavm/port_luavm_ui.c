/* port_luavm_ui.c - the `ui` table: the generic mode menu layer (port/include/port_uimenu.h; midas-notes features/ui-design-system.md
 * "Generic ui layer"; tools/ui_menu.lua is the Lua half). C side: port/src/port_uimenu.c (the hand-off between stages, the named
 * preference files) and port/src/port_uimenu_draw.c (the drawing, presentation). Also the `levelgen` table (port_levelgen_select.c).
 *
 *   ui.launch{mode=, settings=, preset=, label=, lobby=, stage=, difficulty=, env={GE_X="v"}} -> bool   end this stage and start the same
 *                                     stage slot again (stage = "colosseum", "levelgen", "control" ...: that slot instead; the names of GE_STAGE)
 *                                     (no mission report) with that mode (GE_MODE), settings text (a Lua table constructor),
 *                                     difficulty (0..3; nil leaves it) and GE_* variables (up to 8; a variable set for one launch and not
 *                                     the next goes back to what it was). lobby = the lobby's page ("menu", "postgame"; "" = a run)
 *   ui.arm{...}                       the same launch, for when this stage ends by itself (a run: the win, the fail, an abort)
 *   ui.disarm()                       nothing after this stage (the stock report)
 *   ui.quit() -> bool                 end this stage and go to the Files screen
 *   ui.exit() -> bool                 close the game (the window's close box)
 *   ui.current() -> t | nil           what this stage was launched with by the hand-off (the fields above, + seq: launches so far);
 *                                     nil = a plain launch (a launcher, the Mods page)
 *   ui.retry([t]) -> t | nil          the last run's own launch (set by the run, read by the debrief)
 *   ui.result([text]) -> text         the last run's result (a Lua table constructor, set by the run)
 *   ui.prefs(name) -> {key = "value"} a named preference file (Data/Saves/ui/<name>.ini; name: a-z 0-9 _, 1..32); {} in netplay
 *   ui.save_prefs(...) -> false when a key or value was refused (keys A-Z a-z 0-9 _; values printable ASCII, no control characters)
 *   SECURITY (scripts include mods): launch variables are an allowlist with value rules, the mode name is checked (port_uimenu.c)
 *   ui.save_prefs(name, {key = v})    replace it and write the file (values become strings)
 *   ui.view{...}                      the menu as it is now (presentation; port_uimenu_draw.c, tools/ui_menu.lua)
 *   ui.escape_back(on)                Escape is the board's BACK while the lobby holds the controls (not the watch)
 *   ui.hub([on]) -> bool              the session came from the main menu hub (tools/mode_hub.lua); a lobby's EXIT becomes MAIN MENU
 *   ui.opt(key) -> {n, index, text}   one of the port's options for a settings page (allowlist: volumes, mouse sensitivity, FOV ...);
 *   ui.opt_set(key, index) -> bool    index 1-based; applied live and written to midas.ini the way the Options pages do
 *   ui.netplay() -> bool              a netplay session (the menu flow is off there)
 *   ui.state() -> menu_up, gameplay_has_pointer, mouse_captured   (tests)
 *   levelgen.current() -> name        the generated level on the levelgen slot this stage ("bank", "casino")
 *   levelgen.default() -> name        the build's default level's name (GE_LEVELGEN_LEVELS' first)
 *   levelgen.has(name) -> bool        this build carries that level (GE_LEVELGEN=<name> picks it at a stage load)
 *   levelgen.levels() -> { name ... } every level this build carries, the default first
 * MODE tier. Inert outside a mode stage. */
#include <string.h>
#include <stdio.h>
#include "lua.h"
#include "lauxlib.h"
#include "../include/port_uimenu.h"

extern int portNetActive(void);
extern int portModeActive(void);
extern int portUiEnvKeyOk(const char *k);
extern void portRequestQuit(void);   /* video.c */
extern void portConLogf(const char *fmt, ...);

static void optField(lua_State *L, int t, const char *k, char *buf, size_t n)
{
    lua_getfield(L, t, k);
    if (lua_isstring(L, -1)) snprintf(buf, n, "%s", lua_tostring(L, -1));
    else buf[0] = '\0';
    lua_pop(L, 1);
}

static int intField(lua_State *L, int t, const char *k, int def)
{
    int v = def;

    lua_getfield(L, t, k);
    if (lua_isnumber(L, -1)) v = (int) lua_tointeger(L, -1);
    else if (lua_isboolean(L, -1)) v = lua_toboolean(L, -1);
    lua_pop(L, 1);
    return v;
}

static unsigned int rgbaField(lua_State *L, int t, const char *k)
{
    unsigned int v = 0;

    lua_getfield(L, t, k);
    if (lua_isnumber(L, -1)) v = (unsigned int) (lua_tointeger(L, -1) & 0xFFFFFFFF);
    lua_pop(L, 1);
    return v;
}

static void readLaunch(lua_State *L, int t, UiLaunch *out)
{
    luaL_checktype(L, t, LUA_TTABLE);
    memset(out, 0, sizeof(*out));
    optField(L, t, "mode", out->mode, sizeof(out->mode));
    optField(L, t, "settings", out->settings, sizeof(out->settings));
    optField(L, t, "preset", out->preset, sizeof(out->preset));
    optField(L, t, "label", out->label, sizeof(out->label));
    optField(L, t, "lobby", out->lobby, sizeof(out->lobby));
    optField(L, t, "stage", out->stage, sizeof(out->stage));
    out->difficulty = intField(L, t, "difficulty", -1);
    out->seq = intField(L, t, "seq", 0);
    lua_getfield(L, t, "env");
    if (lua_istable(L, -1))
    {   /* (sorted by key, so the order never depends on the table's) */
        const int e = lua_gettop(L);
        char keys[UI_ENV_N][24];
        int n = 0, i, j;

        lua_pushnil(L);
        while (lua_next(L, e) != 0)
        {
            if (lua_type(L, -2) == LUA_TSTRING && portUiEnvKeyOk(lua_tostring(L, -2)) && n < UI_ENV_N)
            {
                snprintf(keys[n], sizeof(keys[n]), "%s", lua_tostring(L, -2));
                n++;
            }
            else
            {   /* (SECURITY: not on the hand-off's allowlist: never set; port_uimenu.c portUiLaunchSanitize checks the values) */
                extern void portConLogf(const char *fmt, ...);

                portConLogf("ge: uimenu: refused the variable %.23s for the next stage (not allowed)\n",
                            lua_type(L, -2) == LUA_TSTRING ? lua_tostring(L, -2) : "?");
            }
            lua_pop(L, 1);
        }
        for (i = 1; i < n; i++)
        {
            char k[24];

            snprintf(k, sizeof(k), "%s", keys[i]);
            for (j = i - 1; j >= 0 && strcmp(keys[j], k) > 0; j--) memcpy(keys[j + 1], keys[j], sizeof(keys[0]));
            memcpy(keys[j + 1], k, sizeof(k));
        }
        for (i = 0; i < n; i++)
        {
            lua_getfield(L, e, keys[i]);
            if (lua_isstring(L, -1) || lua_isboolean(L, -1))
            {
                const char *v = lua_isboolean(L, -1) ? (lua_toboolean(L, -1) ? "1" : "0") : lua_tostring(L, -1);

                snprintf(out->envk[out->nenv], sizeof(out->envk[0]), "%s", keys[i]);
                snprintf(out->envv[out->nenv], sizeof(out->envv[0]), "%s", v);
                out->nenv++;
            }
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
}

static void pushLaunch(lua_State *L, const UiLaunch *l)
{
    int i;

    lua_createtable(L, 0, 9);
    lua_pushstring(L, l->mode); lua_setfield(L, -2, "mode");
    lua_pushstring(L, l->settings); lua_setfield(L, -2, "settings");
    lua_pushstring(L, l->preset); lua_setfield(L, -2, "preset");
    lua_pushstring(L, l->label); lua_setfield(L, -2, "label");
    lua_pushstring(L, l->lobby); lua_setfield(L, -2, "lobby");
    lua_pushstring(L, l->stage); lua_setfield(L, -2, "stage");
    lua_pushinteger(L, l->seq); lua_setfield(L, -2, "seq");
    if (l->difficulty >= 0) { lua_pushinteger(L, l->difficulty); lua_setfield(L, -2, "difficulty"); }
    lua_createtable(L, 0, l->nenv);
    for (i = 0; i < l->nenv; i++) { lua_pushstring(L, l->envv[i]); lua_setfield(L, -2, l->envk[i]); }
    lua_setfield(L, -2, "env");
}

static UiLaunch sTmp;   /* (4 KB: off the C stack) */

static int lLaunch(lua_State *L) { readLaunch(L, 1, &sTmp); lua_pushboolean(L, portUiMenuLaunchNow(&sTmp)); return 1; }
static int lArm(lua_State *L) { readLaunch(L, 1, &sTmp); if (portModeActive()) portUiMenuArm(&sTmp); return 0; }
static int lDisarm(lua_State *L) { (void) L; portUiMenuDisarm(); return 0; }
static int lQuit(lua_State *L) { lua_pushboolean(L, portUiMenuQuitNow()); return 1; }
/* the close box's path (video.c portRequestQuit: SDL_QUIT, then the atexit ini saver) */
static int lExit(lua_State *L)
{
    if (!portModeActive()) { lua_pushboolean(L, 0); return 1; }
    portConLogf("ge: ui: exit the game\n");
    portRequestQuit();
    lua_pushboolean(L, 1);
    return 1;
}
/* ui.hub([on]) -> bool: whether the session came from the main menu hub (tools/mode_hub.lua); a given argument sets it. The lobbies read
 * it to turn their EXIT into MAIN MENU. A process flag: it lives across stage loads, a launcher starts it off */
static int lHub(lua_State *L)
{
    if (lua_gettop(L) >= 1 && portModeActive()) portUiHubSet(lua_toboolean(L, 1));
    lua_pushboolean(L, portUiHub());
    return 1;
}

/* ui.opt(key) -> { n =, index =, text = {...} } | nil, ui.opt_set(key, index) -> bool: the port's own options (port_options.c, the table
 * the Options pages draw: applied live, written to midas.ini in place) for a settings page. Index is 1-based. SECURITY (scripts are mods
 * too): only the keys below, never a path, the ROM, a bind or a netplay switch. */
extern int portOptFind(const char *key);
extern int portOptValueCount(int i);
extern const char *portOptValueTextAt(int i, int idx);
extern int portOptValueIndexNear(int i, int dir);
extern int portOptSetIndex(int i, int idx);

static const char *const kOptAllow[] = { "music_volume", "sfx_volume", "mouse_sens_x", "mouse_sens_y", "look_updown", "hfov_wide",
                                         "always_crosshair", "mission_timer", "show_fps", NULL };

static int optIndexOf(const char *key)
{
    int k;

    for (k = 0; kOptAllow[k] != NULL; k++) if (strcmp(kOptAllow[k], key) == 0) return portOptFind(key);
    return -1;
}

static int lOpt(lua_State *L)
{
    const int i = optIndexOf(luaL_checkstring(L, 1));
    int n, k, cur;

    if (i < 0 || !portModeActive()) return 0;
    n = portOptValueCount(i);
    cur = portOptValueIndexNear(i, 0);
    lua_createtable(L, 0, 3);
    lua_pushinteger(L, n); lua_setfield(L, -2, "n");
    lua_pushinteger(L, cur < 0 ? 1 : (cur >= n ? n : cur + 1)); lua_setfield(L, -2, "index");
    lua_createtable(L, n, 0);
    for (k = 0; k < n; k++)
    {
        const char *t = portOptValueTextAt(i, k);
        size_t len = 0;

        while (t[len] != '\0' && t[len] != '\n') len++;   /* (the table's rows end in a newline) */
        lua_pushlstring(L, t, len);
        lua_rawseti(L, -2, k + 1);
    }
    lua_setfield(L, -2, "text");
    return 1;
}

static int lOptSet(lua_State *L)
{
    const int i = optIndexOf(luaL_checkstring(L, 1));

    if (i < 0 || !portModeActive() || portNetActive()) { lua_pushboolean(L, 0); return 1; }
    lua_pushboolean(L, portOptSetIndex(i, (int) luaL_checkinteger(L, 2) - 1));
    return 1;
}

static int lNetplay(lua_State *L) { lua_pushboolean(L, portNetActive()); return 1; }
static int lEscape(lua_State *L) { portUiMenuEscapeBack(lua_toboolean(L, 1)); return 0; }

static int lCurrent(lua_State *L)
{
    const UiLaunch *l = portUiMenuCurrent();

    if (l == NULL) return 0;
    pushLaunch(L, l);
    return 1;
}

static int lState(lua_State *L)
{
    extern int portGameplayHasPointer(void);   /* bondview2.c */
    extern int portInputMouseLocked(void);     /* input_pc.c */

    lua_pushboolean(L, portUiMenuViewOn());
    lua_pushboolean(L, portGameplayHasPointer());
    lua_pushboolean(L, portInputMouseLocked());
    return 3;
}

static int lRetry(lua_State *L)
{
    const UiLaunch *l;

    if (lua_istable(L, 1))
    {
        readLaunch(L, 1, &sTmp);
        portUiMenuRetrySet(&sTmp);
    }
    l = portUiMenuRetry();
    if (l == NULL) return 0;
    pushLaunch(L, l);
    return 1;
}

static int lResult(lua_State *L)
{
    if (lua_isstring(L, 1)) portUiMenuResultSet(lua_tostring(L, 1));
    lua_pushstring(L, portUiMenuResult());
    return 1;
}

static int lPrefs(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    int i, n;

    if (portNetActive()) { lua_newtable(L); return 1; }
    n = portUiPrefCount(name);
    lua_createtable(L, 0, n);
    for (i = 0; i < n; i++)
    {
        const char *v = NULL, *k = portUiPref(name, i, &v);

        if (k == NULL) break;
        lua_pushstring(L, v);
        lua_setfield(L, -2, k);
    }
    return 1;
}

static int lSavePrefs(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);

    int refused = 0, saved;

    luaL_checktype(L, 2, LUA_TTABLE);
    if (portNetActive()) { lua_pushboolean(L, 0); return 1; }
    portUiPrefClear(name);
    lua_pushnil(L);
    while (lua_next(L, 2) != 0)
    {   /* (a bad key or value is refused and the rest kept; the call then returns false) */
        const char *v = NULL;

        if (lua_isboolean(L, -1)) v = lua_toboolean(L, -1) ? "true" : "false";
        else if (lua_type(L, -1) == LUA_TSTRING || lua_type(L, -1) == LUA_TNUMBER) v = lua_tostring(L, -1);
        if (lua_type(L, -2) != LUA_TSTRING || v == NULL || !portUiPrefPut(name, lua_tostring(L, -2), v)) refused++;
        lua_pop(L, 1);
    }
    saved = portUiPrefSave(name);
    lua_pushboolean(L, saved && refused == 0);
    return 1;
}

/* ---- the view ---- */

static int kindOf(const char *k)
{
    if (strcmp(k, "header") == 0) return UII_HEADER;
    if (strcmp(k, "card") == 0 || strcmp(k, "preset") == 0) return UII_CARD;
    if (strcmp(k, "choice") == 0) return UII_CHOICE;
    if (strcmp(k, "primary") == 0) return UII_PRIMARY;
    return UII_BUTTON;
}

static void seqStr(lua_State *L, int e, int i, char *d, size_t n)
{
    lua_rawgeti(L, e, i);
    snprintf(d, n, "%s", lua_isstring(L, -1) ? lua_tostring(L, -1) : "");
    lua_pop(L, 1);
}

static int seqInt(lua_State *L, int e, int i)
{
    int v = 0;

    lua_rawgeti(L, e, i);
    if (lua_isnumber(L, -1)) v = (int) (lua_tointeger(L, -1) & 0xFFFFFFFF);
    else if (lua_isboolean(L, -1)) v = lua_toboolean(L, -1);
    lua_pop(L, 1);
    return v;
}

/* { label, value, rgba, flag } lists: the info card's lines and the debrief's cells */
static int readInfo(lua_State *L, int t, const char *k, UiInfo *out, int max)
{
    int n = 0, i, len;

    lua_getfield(L, t, k);
    if (lua_istable(L, -1))
    {
        const int s = lua_gettop(L);

        len = (int) lua_rawlen(L, s);
        for (i = 0; i < len && n < max; i++)
        {
            lua_rawgeti(L, s, i + 1);
            if (lua_istable(L, -1))
            {
                const int e = lua_gettop(L);

                seqStr(L, e, 1, out[n].label, sizeof(out[n].label));
                seqStr(L, e, 2, out[n].value, sizeof(out[n].value));
                out[n].rgba = (unsigned int) seqInt(L, e, 3);
                out[n].flag = (signed char) seqInt(L, e, 4);
                n++;
            }
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    return n;
}

static UiView sView;

static int lView(lua_State *L)
{
    UiView *v = &sView;
    char buf[16];
    int i, n;

    luaL_checktype(L, 1, LUA_TTABLE);
    memset(v, 0, sizeof(*v));
    optField(L, 1, "layout", buf, sizeof(buf));
    v->layout = strcmp(buf, "lobby") == 0 ? UIL_LOBBY : (strcmp(buf, "settings") == 0 ? UIL_SETTINGS : (strcmp(buf, "debrief") == 0 ? UIL_DEBRIEF : UIL_OFF));
    v->page = intField(L, 1, "page", v->layout);
    v->cursor = intField(L, 1, "cursor", 0);
    v->medal = intField(L, 1, "medal", -1);
    v->medalNew = intField(L, 1, "medal_new", 0);
    v->bannerRgba = rgbaField(L, 1, "banner_rgba");
    v->medalRgba = rgbaField(L, 1, "medal_rgba");
    optField(L, 1, "title", v->title, sizeof(v->title));
    optField(L, 1, "kicker", v->kicker, sizeof(v->kicker));
    optField(L, 1, "subtitle", v->subtitle, sizeof(v->subtitle));
    optField(L, 1, "desc", v->desc, sizeof(v->desc));
    optField(L, 1, "footer", v->footer, sizeof(v->footer));
    optField(L, 1, "banner", v->banner, sizeof(v->banner));
    optField(L, 1, "news", v->news, sizeof(v->news));
    optField(L, 1, "info_title", v->infoTitle, sizeof(v->infoTitle));
    optField(L, 1, "info_sub", v->infoSub, sizeof(v->infoSub));
    optField(L, 1, "medal_name", v->medalName, sizeof(v->medalName));
    optField(L, 1, "chart", v->chart, sizeof(v->chart));
    optField(L, 1, "legend", v->legend, sizeof(v->legend));
    optField(L, 1, "best_chip", v->bestChip, sizeof(v->bestChip));
    optField(L, 1, "new_medal", v->newMedal, sizeof(v->newMedal));
    optField(L, 1, "checks_title", v->checksTitle, sizeof(v->checksTitle));
    optField(L, 1, "chips_title", v->chipsTitle, sizeof(v->chipsTitle));
    lua_getfield(L, 1, "items");
    if (lua_istable(L, -1))
    {
        const int t = lua_gettop(L);

        n = (int) lua_rawlen(L, t);
        for (i = 0; i < n && v->n < UIV_ITEMS; i++)
        {
            UiItem *it = &v->item[v->n];

            lua_rawgeti(L, t, i + 1);
            if (lua_istable(L, -1))
            {
                const int e = lua_gettop(L);

                optField(L, e, "kind", buf, sizeof(buf));
                it->kind = (signed char) kindOf(buf);
                it->col = (signed char) intField(L, e, "col", 1);
                it->medal = (signed char) intField(L, e, "medal", 0);
                it->chosen = (signed char) intField(L, e, "chosen", 0);
                it->dim = (signed char) intField(L, e, "dim", 0);
                it->tint = rgbaField(L, e, "tint");
                optField(L, e, "label", it->label, sizeof(it->label));
                optField(L, e, "value", it->value, sizeof(it->value));
                optField(L, e, "sub", it->sub, sizeof(it->sub));
                optField(L, e, "right", it->right, sizeof(it->right));
                optField(L, e, "tag", it->tag, sizeof(it->tag));
                v->n++;
            }
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    v->ninfo = readInfo(L, 1, "info", v->info, UIV_INFO);
    v->ncells = readInfo(L, 1, "cells", v->cell, UIV_CELLS);
    lua_getfield(L, 1, "checks");
    if (lua_istable(L, -1))
    {
        const int t = lua_gettop(L);

        n = (int) lua_rawlen(L, t);
        for (i = 0; i < n && v->nchecks < UIV_CHECKS; i++)
        {
            lua_rawgeti(L, t, i + 1);
            if (lua_istable(L, -1))
            {
                UiCheck *k = &v->check[v->nchecks++];

                seqStr(L, lua_gettop(L), 1, k->label, sizeof(k->label));
                k->state = (signed char) seqInt(L, lua_gettop(L), 2);
                seqStr(L, lua_gettop(L), 3, k->value, sizeof(k->value));
            }
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    lua_getfield(L, 1, "chips");
    if (lua_istable(L, -1))
    {
        const int t = lua_gettop(L);

        n = (int) lua_rawlen(L, t);
        for (i = 0; i < n && v->nchips < UIV_CHIPS; i++)
        {
            lua_rawgeti(L, t, i + 1);
            if (lua_istable(L, -1))
            {
                UiChip *k = &v->chip[v->nchips++];

                seqStr(L, lua_gettop(L), 1, k->label, sizeof(k->label));
                k->on = (signed char) seqInt(L, lua_gettop(L), 2);
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
        for (i = 0; i < n && v->nbars < UIV_BARS; i++)
        {
            lua_rawgeti(L, t, i + 1);
            if (lua_istable(L, -1))
            {
                UiBar *b = &v->bar[v->nbars++];

                b->v = seqInt(L, lua_gettop(L), 1);
                b->kind = (signed char) seqInt(L, lua_gettop(L), 2);
                seqStr(L, lua_gettop(L), 3, b->label, sizeof(b->label));
                seqStr(L, lua_gettop(L), 4, b->text, sizeof(b->text));
            }
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    portUiMenuViewSet(v);
    return 0;
}

/* ---- levelgen ---- */

static int lLgCurrent(lua_State *L) { lua_pushstring(L, portLevelgenVariant()); return 1; }
static int lLgDefault(lua_State *L) { lua_pushstring(L, portLevelgenDefaultName()); return 1; }
static int lLgHas(lua_State *L) { lua_pushboolean(L, portLevelgenHas(luaL_checkstring(L, 1))); return 1; }
static int lLgLevels(lua_State *L)
{
    int i, n = portLevelgenCount();

    lua_createtable(L, n, 0);
    for (i = 0; i < n; i++) { lua_pushstring(L, portLevelgenName(i)); lua_rawseti(L, -2, i + 1); }
    return 1;
}

void portLuaUiRegister(lua_State *L)
{
    static const luaL_Reg lib[] = {
        { "launch", lLaunch }, { "arm", lArm }, { "disarm", lDisarm }, { "quit", lQuit }, { "exit", lExit }, { "current", lCurrent }, { "retry", lRetry },
        { "result", lResult }, { "prefs", lPrefs }, { "save_prefs", lSavePrefs }, { "view", lView }, { "escape_back", lEscape },
        { "netplay", lNetplay }, { "state", lState }, { "hub", lHub }, { "opt", lOpt }, { "opt_set", lOptSet }, { NULL, NULL } };
    static const luaL_Reg lg[] = { { "current", lLgCurrent }, { "default", lLgDefault }, { "has", lLgHas }, { "levels", lLgLevels },
                                   { NULL, NULL } };

    luaL_newlib(L, lib);
    lua_setglobal(L, "ui");
    luaL_newlib(L, lg);
    lua_setglobal(L, "levelgen");
}
