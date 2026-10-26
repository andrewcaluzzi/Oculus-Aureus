/* port_luavm_planning.c - Lua bindings for the heist's PLANNING phase (designs/bank-heist.md section 27; tools/heist_planning.lua).
 * The C side is port/src/port_planning.c (the hold and the input: sim state) and port/src/port_intel.c's plan mode (the board: presentation).
 *
 *   plan.hold(on)                       hold the start: no Bond moves or fires, his buttons drive the board (sim)
 *   plan.held() -> bool
 *   plan.input(p) -> mask               Bond p's board events since the last call, taken (1 up, 2 down, 4 left, 8 right, 16 select,
 *                                       32 back, 64 start); his own synchronized input, the same on every machine
 *   plan.test_input(p, mask)            tests: events for Bond p as if pressed (sim side)
 *   plan.view(on [, page [, floor [, footer]]])   the board up on this page / floor with this footer (on every machine: the page comes
 *                                       from the plan's sim state), or put away
 *   plan.clear()                        drop the plan's pages (the board's plan layer; intel.clear never touches it)
 *   plan.at(page)                       the next intel.rect / text / marker ... go on the plan layer of that page
 *   plan.count() -> n                   items on the plan layer (tests)
 *   plan.has(p, item) -> bool           tests: Bond p carries that item (a bond.give name) - reads only
 *   plan.strip(p, item) -> bool         take that item out of Bond p's inventory (sim: the stage's own start gun, at the stage start)
 *   plan.pages(first, n)                the plan's own tabs: only they show while planning; after it only the pages before them
 *   plan.tabs() -> first, n             the tabs the board shows now (tests)
 *   plan.hit(page, row, x0, y0, x1, y1) a clickable row of a plan page (board units; cleared by plan.clear): the board's mouse
 *   plan.pointer(p) -> a, b | nil       Bond p's next board click, taken (sim: a session command): a = page * 64 + row (0 = none),
 *                                       b = 1 select (left), 2 back (right), 3 tab, 4 row up, 5 row down (the wheel)
 *   plan.test_mouse(x, y [, button]) -> page, row   tests: the cursor to a board point; button 1 / 2 clicks, 4 / 5 wheel notches
 * The drawing is presentation only: built from the plan's sim state, never read back. */
#include "lua.h"
#include "lauxlib.h"

extern void portPlanningHold(int on);
extern int portPlanningHeld(void);
extern unsigned portPlanningTake(int p);
extern void portPlanningTestInput(int p, unsigned mask);
extern void portIntelPlanView(int on, int page, int floor, const char *footer);
extern void portIntelPlanClear(void);
extern void portIntelAt(int page, int floor, int live);
extern int portIntelItemCount(int live);
extern int portPlanningHasItem(int p, int item);
extern int portPlanningStrip(int p, int item);
extern int portLuaGameItemId(const char *upper);

static int lHold(lua_State *L) { portPlanningHold(lua_toboolean(L, 1)); return 0; }
static int lHeld(lua_State *L) { lua_pushboolean(L, portPlanningHeld()); return 1; }
static int lInput(lua_State *L) { lua_pushinteger(L, (lua_Integer) portPlanningTake((int) luaL_checkinteger(L, 1))); return 1; }

static int lTestInput(lua_State *L)
{
    portPlanningTestInput((int) luaL_checkinteger(L, 1), (unsigned) luaL_checkinteger(L, 2));
    return 0;
}

static int lView(lua_State *L)
{
    portIntelPlanView(lua_toboolean(L, 1), (int) luaL_optinteger(L, 2, 0), (int) luaL_optinteger(L, 3, 0), luaL_optstring(L, 4, ""));
    return 0;
}

static int lClear(lua_State *L) { (void) L; portIntelPlanClear(); return 0; }
static int lAt(lua_State *L) { portIntelAt((int) luaL_checkinteger(L, 1), 0, 2); return 0; }
static int lCount(lua_State *L) { lua_pushinteger(L, portIntelItemCount(2)); return 1; }

static int itemArg(lua_State *L, int idx, const char *fn)
{
    char key[48];
    const char *name = luaL_checkstring(L, idx);
    size_t n = 0;
    int id;

    for (; *name != 0 && n < sizeof(key) - 1; name++) key[n++] = (*name >= 'a' && *name <= 'z') ? (char) (*name - 32) : *name;
    key[n] = 0;
    id = portLuaGameItemId(key);
    if (id < 0) return luaL_error(L, "%s: unknown item '%s'", fn, luaL_checkstring(L, idx));
    return id;
}

extern void portIntelPlanPages(int first, int n);
extern void portIntelTabs(int *first, int *n);
extern void portIntelPlanHit(int page, int row, float x0, float y0, float x1, float y1);
extern void portIntelTestMouse(float x, float y, int button, int *page, int *row);
extern int portPlanningPointerTake(int p, int *a, int *b);

static int lPages(lua_State *L) { portIntelPlanPages((int) luaL_checkinteger(L, 1), (int) luaL_checkinteger(L, 2)); return 0; }

static int lTabs(lua_State *L)
{
    int first = 0, n = 0;

    portIntelTabs(&first, &n);
    lua_pushinteger(L, first);
    lua_pushinteger(L, n);
    return 2;
}

static int lHit(lua_State *L)
{
    portIntelPlanHit((int) luaL_checkinteger(L, 1), (int) luaL_checkinteger(L, 2), (float) luaL_checknumber(L, 3),
                     (float) luaL_checknumber(L, 4), (float) luaL_checknumber(L, 5), (float) luaL_checknumber(L, 6));
    return 0;
}

static int lPointer(lua_State *L)
{
    int a = 0, b = 0;

    if (!portPlanningPointerTake((int) luaL_checkinteger(L, 1), &a, &b)) return 0;
    lua_pushinteger(L, a);
    lua_pushinteger(L, b);
    return 2;
}

static int lTestMouse(lua_State *L)
{
    int page = -1, row = -1;

    portIntelTestMouse((float) luaL_checknumber(L, 1), (float) luaL_checknumber(L, 2), (int) luaL_optinteger(L, 3, 0), &page, &row);
    lua_pushinteger(L, page);
    lua_pushinteger(L, row);
    return 2;
}

static int lStrip(lua_State *L) { lua_pushboolean(L, portPlanningStrip((int) luaL_checkinteger(L, 1), itemArg(L, 2, "plan.strip"))); return 1; }

static int lHas(lua_State *L)
{
    char key[48];
    const char *name = luaL_checkstring(L, 2);
    size_t n = 0;
    int id;

    for (; *name != '\0' && n < sizeof(key) - 1; name++) key[n++] = (*name >= 'a' && *name <= 'z') ? (char) (*name - 32) : *name;
    key[n] = '\0';
    id = portLuaGameItemId(key);
    if (id < 0) return luaL_error(L, "plan.has: unknown item '%s'", luaL_checkstring(L, 2));
    lua_pushboolean(L, portPlanningHasItem((int) luaL_checkinteger(L, 1), id));
    return 1;
}

void portLuaPlanningRegister(lua_State *L)
{
    static const luaL_Reg lib[] = {
        { "hold", lHold }, { "held", lHeld }, { "input", lInput }, { "test_input", lTestInput }, { "view", lView },
        { "clear", lClear }, { "at", lAt }, { "count", lCount }, { "has", lHas }, { "strip", lStrip },
        { "pages", lPages }, { "tabs", lTabs }, { "hit", lHit }, { "pointer", lPointer }, { "test_mouse", lTestMouse }, { NULL, NULL } };

    luaL_newlib(L, lib);
    lua_setglobal(L, "plan");
}
