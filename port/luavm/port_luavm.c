/* port_luavm.c - the Lua half of the scripting layer (see port_lua.h and
 * modding/lua.md). Built in the ge_lua library against the real libc headers.
 *
 * One lua_State, made on first need. Sandbox: only base/string/table/math/utf8/coroutine
 * libs, no dofile/loadfile/collectgarbage, a 16 MB allocator cap and a per-call instruction
 * budget. Every entry goes through lua_pcall, so a script error is logged, never fatal.
 * The `weapon` table edits the WeaponStats structs in place through the portLuaGame* helpers. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <time.h>
#include <dirent.h>
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "../include/port_lua.h"
#include "../include/port_event.h"
#include "../include/port_modifier.h"
#include "../include/port_music.h"
#define LUA_CORE
#include "lstate.h"           /* global_State: only for the snapshot compare's benign range (portLuaState) */
#undef G

extern void portConLogf(const char *fmt, ...);   /* port_console.c */

/* Private ASCII ctype functions behind the macros in ge_lua_ctype.h. */
int ge_lua_isalpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
int ge_lua_isspace(int c) { return c == ' ' || (c >= '\t' && c <= '\r'); }
int ge_lua_toupper(int c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }

#define LUA_MEM_CAP     (16u * 1024u * 1024u)
#define LUA_INSN_LIMIT   50000000
#define LUA_HOOK_STEP    1000
#define LUA_HOOK_GRACE   200000     /* instructions left for the error handler after a trip */
#define LUA_MAX_SCRIPTS  64

static lua_State *sL;
static size_t sMemUsed;
static long sBudget;

/* ---- state ------------------------------------------------------------- */

/* ---- the Lua arena (rollback R2) ----------------------------------------------------------------------------------------------------
 * The whole Lua VM (the lua_State, every table, string and closure) lives in ONE fixed block, allocated by this deterministic
 * size-class allocator, so a rollback snapshot can copy the VM with a memcpy of the used part and every pointer inside it stays valid:
 * the block never moves, and a restore puts the allocator's own bookkeeping (the header below, in the block) back with it. The 16 MB
 * cap of the old malloc allocator is unchanged (sMemUsed). Allocation is deterministic (same calls, same addresses), so the order
 * of a pairs() walk over pointer-keyed tables is also the same on every machine that gets the same arena address (the block is
 * requested through the same hint ladder in the same order on both machines).
 * Block sizes: multiples of 16 up to 1024, then 8 steps per power of two; freed blocks go on a LIFO list per class (their first word
 * links the list); the arena never coalesces and never gives memory back (a size class is reused, not split). */
#define LUA_ARENA_SIZE   (LUA_MEM_CAP + 4u * 1024u * 1024u)    /* the cap, plus the rounding slack and the header */
#define LUA_ARENA_NCLS   192
#define LUA_ARENA_HDR    1024u

typedef struct {
    unsigned int bump;                     /* next free offset from the arena start */
    unsigned int blocks;                   /* live blocks (a sanity figure) */
    unsigned int freeList[LUA_ARENA_NCLS]; /* arena offsets (0 = empty) */
} LuaArenaHdr;

static unsigned char *sArena;
static int sArenaTried;

extern void *portAllocHighDedicated(unsigned int size);   /* pcsched.c */
extern void portSnapAddDyn(const char *name, int (*fn)(int, unsigned char **, unsigned int *));   /* port_snap.c */

static int portLuaArenaRange(int i, unsigned char **lo, unsigned int *len)
{
    if (i == 0 && sArena != NULL)
    {
        *lo = sArena;
        *len = ((LuaArenaHdr *) sArena)->bump;
        return 1;
    }
    return 0;
}

static unsigned int arenaClass(size_t n, unsigned int *size)
{
    unsigned int e, sub;

    if (n <= 1024u)
    {
        unsigned int c = ((unsigned int) n + 15u) >> 4;

        *size = c * 16u;
        return c;
    }
    e = 31u - (unsigned int) __builtin_clz((unsigned int) (n - 1u));
    sub = (unsigned int) (((n - 1u) >> (e - 3u)) & 7u);
    *size = (9u + sub) << (e - 3u);
    return 64u + (e - 10u) * 8u + sub + 1u;
}

static void *arenaAlloc(size_t n)
{
    LuaArenaHdr *h = (LuaArenaHdr *) sArena;
    unsigned int size, c = arenaClass(n, &size);
    void *p;

    if (c >= LUA_ARENA_NCLS)
    {
        return NULL;
    }
    if (h->freeList[c] != 0)
    {
        p = sArena + h->freeList[c];
        h->freeList[c] = *(unsigned int *) p;
    }
    else
    {
        if (h->bump + size > LUA_ARENA_SIZE)
        {
            return NULL;
        }
        p = sArena + h->bump;
        h->bump += size;
        memset(p, 0, size);   /* fresh memory arrives zeroed; after a restore the tail beyond `bump` still holds the first run's bytes */
    }
    h->blocks++;
    return p;
}

static void arenaFree(void *p, size_t osize)
{
    LuaArenaHdr *h = (LuaArenaHdr *) sArena;
    unsigned int size, c = arenaClass(osize, &size);

    *(unsigned int *) p = h->freeList[c];
    h->freeList[c] = (unsigned int) ((unsigned char *) p - sArena);
    h->blocks--;
}

static int arenaInit(void)
{
    if (!sArenaTried)
    {
        sArenaTried = 1;
        sArena = (unsigned char *) portAllocHighDedicated(LUA_ARENA_SIZE);   /* zeroed */
        if (sArena != NULL)
        {
            ((LuaArenaHdr *) sArena)->bump = LUA_ARENA_HDR;
            portSnapAddDyn("lua", portLuaArenaRange);
            portConLogf("ge: lua: VM arena %u MB at %p (snapshottable)\n", LUA_ARENA_SIZE >> 20, (void *) sArena);
        }
        else
        {
            portConLogf("ge: lua: no fixed arena for the VM: it uses the C heap (not rollback-safe)\n");
        }
    }
    return sArena != NULL;
}

static void *portLuaAlloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
    void *p;

    (void) ud;
    if (ptr == NULL)
    {
        osize = 0;
    }
    if (nsize == 0)
    {
        if (ptr != NULL)
        {
            if (arenaInit())
            {
                arenaFree(ptr, osize);
            }
            else
            {
                free(ptr);
            }
        }
        sMemUsed -= osize;
        return NULL;
    }
    if (nsize > osize && nsize - osize > LUA_MEM_CAP - sMemUsed)
    {
        return NULL;   /* Lua raises a memory error */
    }
    if (arenaInit())
    {
        unsigned int s0, s1;

        if (ptr != NULL && arenaClass(osize, &s0) == arenaClass(nsize, &s1))
        {
            p = ptr;   /* same size class: the block already fits */
        }
        else
        {
            p = arenaAlloc(nsize);
            if (p == NULL)
            {
                return (ptr != NULL && nsize < osize) ? ptr : NULL;   /* shrinking never fails */
            }
            if (ptr != NULL)
            {
                memcpy(p, ptr, osize < nsize ? osize : nsize);
                arenaFree(ptr, osize);
            }
        }
    }
    else
    {
        p = realloc(ptr, nsize);
    }
    if (p != NULL)
    {
        sMemUsed = sMemUsed - osize + nsize;
    }
    return p;
}

static void portLuaHook(lua_State *L, lua_Debug *ar)
{
    (void) ar;
    sBudget += LUA_HOOK_STEP;
    if (sBudget > LUA_INSN_LIMIT)
    {
        sBudget = LUA_INSN_LIMIT - LUA_HOOK_GRACE;
        luaL_error(L, "script ran too long (over %d million instructions)", LUA_INSN_LIMIT / 1000000);
    }
}

static int portLuaPrint(lua_State *L)
{
    char line[512];
    size_t len = 0;
    int i, n = lua_gettop(L);

    line[0] = '\0';
    for (i = 1; i <= n; i++)
    {
        size_t l;
        const char *s = luaL_tolstring(L, i, &l);

        if (i > 1 && len < sizeof(line) - 1)
        {
            line[len++] = '\t';
        }
        if (l > sizeof(line) - 1 - len)
        {
            l = sizeof(line) - 1 - len;
        }
        memcpy(line + len, s, l);
        len += l;
        line[len] = '\0';
        lua_pop(L, 1);
    }
    portConLogf("ge: lua: %s\n", line);
    return 0;
}

static int portLuaMsgh(lua_State *L)
{
    const char *msg = lua_tostring(L, 1);

    if (msg == NULL)
    {
        msg = lua_pushfstring(L, "(error object is a %s value)", luaL_typename(L, 1));
    }
    luaL_traceback(L, L, msg, 1);
    return 1;
}

/* Calls the function under its nargs arguments with the budget reset. Logs and returns
 * non-zero on error; on success the results (nres, or all for LUA_MULTRET) are on the stack. */
static int portLuaCall(lua_State *L, int nargs, int nres)
{
    int base = lua_gettop(L) - nargs;   /* the function's index */
    int rc;
    long saved = sBudget;   /* a nested call (a handler's API call emitting an event) must not reset the outer budget */

    lua_pushcfunction(L, portLuaMsgh);
    lua_insert(L, base);
    sBudget = 0;
    rc = lua_pcall(L, nargs, nres, base);
    sBudget = saved;
    lua_remove(L, base);
    if (rc != LUA_OK)
    {
        portConLogf("ge: lua: error: %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
        return rc;
    }
    return 0;
}

/* ---- weapon API -------------------------------------------------------- */

/* Upper-cases name into out, dropping a leading "item_"; the item list is upper case. */
static void portLuaItemKey(const char *name, char *out, size_t cap)
{
    size_t n = 0;

    if ((name[0] | 32) == 'i' && (name[1] | 32) == 't' && (name[2] | 32) == 'e' &&
        (name[3] | 32) == 'm' && name[4] == '_')
    {
        name += 5;
    }
    for (; *name != '\0' && n < cap - 1; name++)
    {
        out[n++] = (*name >= 'a' && *name <= 'z') ? (char) (*name - 32) : *name;
    }
    out[n] = '\0';
}

static char *portLuaCheckItem(lua_State *L, int idx)
{
    const char *name = luaL_checkstring(L, idx);
    char key[48];
    void *ws;
    int i;

    portLuaItemKey(name, key, sizeof(key));
    i = portLuaGameItemFind(key);
    if (i < 0)
    {
        luaL_error(L, "unknown item '%s' (see weapon.items())", name);
    }
    ws = portLuaGameStats(i);
    if (ws == NULL)
    {
        luaL_error(L, "item '%s' has no weapon stats of its own", name);
    }
    portLuaGameSnapshot();
    return (char *) ws;
}

static void portLuaCheckField(lua_State *L, int idx, size_t *off, int *type)
{
    const char *name = luaL_checkstring(L, idx);
    const char *fname;
    int i;

    for (i = 0; portLuaGameField(i, &fname, off, type); i++)
    {
        if (strcmp(fname, name) == 0)
        {
            return;
        }
    }
    luaL_error(L, "unknown field '%s' (see weapon.fields())", name);
}

static int portLuaWeaponGet(lua_State *L)
{
    char *p = portLuaCheckItem(L, 1);
    size_t off;
    int type;

    portLuaCheckField(L, 2, &off, &type);
    p += off;
    switch (type)
    {
    case PORTLUA_S8:  lua_pushinteger(L, *(int8_t *) p); break;
    case PORTLUA_U8:  lua_pushinteger(L, *(uint8_t *) p); break;
    case PORTLUA_S16: lua_pushinteger(L, *(int16_t *) p); break;
    case PORTLUA_U16: lua_pushinteger(L, *(uint16_t *) p); break;
    case PORTLUA_S32: lua_pushinteger(L, *(int32_t *) p); break;
    default:          lua_pushnumber(L, *(float *) p); break;
    }
    return 1;
}

static int portLuaWeaponSet(lua_State *L)
{
    char *p = portLuaCheckItem(L, 1);
    const char *fname = luaL_checkstring(L, 2);
    size_t off;
    int type;

    portLuaCheckField(L, 2, &off, &type);
    p += off;
    if (type == PORTLUA_F32)
    {
        lua_Number n = luaL_checknumber(L, 3);

        if (n != n || n > 3.0e38 || n < -3.0e38)
        {
            return luaL_error(L, "field '%s': value out of range for a float", fname);
        }
        *(float *) p = (float) n;
    }
    else
    {
        lua_Integer v, lo, hi;
        int ok;

        v = lua_tointegerx(L, 3, &ok);
        if (!ok)
        {
            return luaL_error(L, "field '%s' needs a whole number", fname);
        }
        switch (type)
        {
        case PORTLUA_S8:  lo = INT8_MIN;  hi = INT8_MAX; break;
        case PORTLUA_U8:  lo = 0;         hi = UINT8_MAX; break;
        case PORTLUA_S16: lo = INT16_MIN; hi = INT16_MAX; break;
        case PORTLUA_U16: lo = 0;         hi = UINT16_MAX; break;
        default:          lo = INT32_MIN; hi = INT32_MAX; break;
        }
        if (v < lo || v > hi)
        {
            return luaL_error(L, "field '%s': %I out of range %I..%I", fname, v, lo, hi);
        }
        switch (type)
        {
        case PORTLUA_S8:  *(int8_t *) p = (int8_t) v; break;
        case PORTLUA_U8:  *(uint8_t *) p = (uint8_t) v; break;
        case PORTLUA_S16: *(int16_t *) p = (int16_t) v; break;
        case PORTLUA_U16: *(uint16_t *) p = (uint16_t) v; break;
        default:          *(int32_t *) p = (int32_t) v; break;
        }
    }
    lua_pushboolean(L, 1);
    return 1;
}

static int portLuaWeaponResetL(lua_State *L)
{
    (void) L;
    portLuaGameSnapshot();
    portLuaGameWeaponReset();
    return 0;
}

static int portLuaWeaponItems(lua_State *L)
{
    int i, n = 0;

    lua_newtable(L);
    for (i = 0; i < portLuaGameItemCount(); i++)
    {
        const char *name = portLuaGameItemName(i);
        char low[48];
        size_t k;

        if (name == NULL) continue;
        for (k = 0; name[k] != '\0' && k < sizeof(low) - 1; k++)
        {
            low[k] = (name[k] >= 'A' && name[k] <= 'Z') ? (char) (name[k] + 32) : name[k];
        }
        low[k] = '\0';
        lua_pushstring(L, low);
        lua_rawseti(L, -2, ++n);
    }
    return 1;
}

static int portLuaWeaponFields(lua_State *L)
{
    const char *name;
    size_t off;
    int i, type;

    lua_newtable(L);
    for (i = 0; portLuaGameField(i, &name, &off, &type); i++)
    {
        lua_pushstring(L, name);
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

static const luaL_Reg sWeaponLib[] = {
    { "set", portLuaWeaponSet },
    { "get", portLuaWeaponGet },
    { "reset", portLuaWeaponResetL },
    { "items", portLuaWeaponItems },
    { "fields", portLuaWeaponFields },
    { NULL, NULL }
};

/* ---- mode API (step 2) -------------------------------------------------- */

#define LUA_EV_TICK       100         /* not on the C bus: driven by portLuaTick */
#define LUA_EV_TESTKEY    101         /* likewise: queued by portLuaTestKey, delivered on the next game tick */
#define LUA_TICK_PERIOD   15          /* clock ticks (60 = 1 s): the tick event fires every 0.25 s */
#define LUA_MAX_HANDLERS  16          /* per event */
#define LUA_HANDLERS_KEY  "midas_handlers"

static const struct { const char *name; int type; int nargs; } sEvents[] = {
    { "chr_died", PORT_EV_CHR_DIED, 2 },         /* victim, killer (CHR_BOND / CHR_NONE) */
    { "zone_enter", PORT_EV_ZONE_ENTER, 2 },     /* zone id, chr (bond.id(p) = CHR_BOND - p for Bond p) */
    { "zone_leave", PORT_EV_ZONE_LEAVE, 2 },
    { "stage_start", PORT_EV_STAGE_START, 1 },   /* stage id */
    { "bond_died", PORT_EV_BOND_DIED, 2 },       /* killer (always CHR_NONE for now), player index of the Bond that died */
    { "hostage_follow", PORT_EV_HOSTAGE_FOLLOW, 2 },   /* hostage chr, player index of the Bond that leads it */
    { "hostage_wait", PORT_EV_HOSTAGE_WAIT, 2 },       /* hostage chr, player index of the Bond that told it to wait */
    { "damage", PORT_EV_DAMAGE, 4 },             /* victim, attacker, amount (float), weapon (-1 unknown) */
    { "chr_spawned", PORT_EV_CHR_SPAWNED, 1 },   /* chrnum (port spawns only) */
    { "item_pickup", PORT_EV_ITEM_PICKUP, 4 },   /* objtype (PROPDEF_*), id (weapon / ammo type / key flags / -1), qty, player */
    { "alarm", PORT_EV_ALARM, 1 },               /* 1 started, 0 stopped */
    { "cctv_spotted", PORT_EV_CCTV_SPOTTED, 2 }, /* camera setup index, player */
    { "use", PORT_EV_USE, 4 },                   /* player, PROP_TYPE_* of the target, target setup index (-1), PROPDEF_* type */
    { "gadget_used", PORT_EV_GADGET_USED, 4 },   /* player, ITEM_* id, target setup index (-1), PROPDEF_* type */
    { "body_found", PORT_EV_BODY_FOUND, 3 },     /* guard chr that found it, body chr, 1 knocked out / 0 dead */
    { "chr_knocked_out", PORT_EV_CHR_KNOCKED_OUT, 2 },   /* chr, player index of the Bond who slapped it out (-1 script) */
    { "chr_woken", PORT_EV_CHR_WOKEN, 2 },       /* chr, the guard chr that woke it (-1 script) */
    { "chr_searched", PORT_EV_CHR_SEARCHED, 2 }, /* knocked-out chr, player index; it dropped what it carried */
    { "civilian_killed", PORT_EV_CIVILIAN_KILLED, 2 },   /* civilian chr, killer player index (-1 unknown) */
    { "civilian_surrendered", PORT_EV_CIVILIAN_SURRENDERED, 2 },   /* civilian chr, player index that aimed at it */
    { "civilian_robbed", PORT_EV_CIVILIAN_ROBBED, 2 },   /* civilian chr, player index; it dropped its key / concealed items */
    { "objective_changed", PORT_EV_OBJECTIVE_CHANGED, 2 },   /* index, status (0 incomplete, 1 complete, 2 failed) */
    { "tick", LUA_EV_TICK, 1 },                  /* seconds since the last tick event */
    { "test_key", LUA_EV_TESTKEY, 1 },           /* name of a test hotkey: "next" "back" "next+" "back+" (PageDown, PageUp, with Shift = +) */
};
#define LUA_EV_N ((int) (sizeof(sEvents) / sizeof(sEvents[0])))

static unsigned char sHooked[PORT_EV_COUNT];
static int sTickOn;
static int sTickAccum;
static int sTestKeyOn;
static char sTestKeyQ[8][8];
static int sTestKeyN;
static const char *sTestKeyName;

static void portLuaDispatch(int type, int a, int b, int c, int d, double dt)
{
    lua_State *L = sL;
    int top, n, i, k, nargs = 0;

    if (L == NULL || portLuaGameMissionEnded())
    {
        return;   /* the mission is over: no more spawns or HUD changes */
    }
    for (k = 0; k < LUA_EV_N; k++)
    {
        if (sEvents[k].type == type) { nargs = sEvents[k].nargs; break; }
    }
    top = lua_gettop(L);
    lua_getfield(L, LUA_REGISTRYINDEX, LUA_HANDLERS_KEY);
    lua_rawgeti(L, -1, type);
    if (!lua_istable(L, -1))
    {
        lua_settop(L, top);
        return;
    }
    n = (int) lua_rawlen(L, -1);
    for (i = 1; i <= n; i++)
    {
        lua_rawgeti(L, -1, i);
        if (!lua_isfunction(L, -1))
        {
            lua_pop(L, 1);   /* a disabled handler */
            continue;
        }
        if (type == LUA_EV_TICK)
        {
            lua_pushnumber(L, dt);
        }
        else if (type == LUA_EV_TESTKEY)
        {
            lua_pushstring(L, sTestKeyName);
        }
        else
        {
            if (nargs >= 1) lua_pushinteger(L, a);
            if (nargs >= 2) lua_pushinteger(L, b);
            if (nargs >= 3)
            {
                if (type == PORT_EV_DAMAGE) lua_pushnumber(L, c / 1000.0);   /* milli-units on the bus */
                else lua_pushinteger(L, c);
            }
            if (nargs >= 4) lua_pushinteger(L, d);
        }
        if (portLuaCall(L, nargs, 0) != 0)
        {
            lua_pushboolean(L, 0);
            lua_rawseti(L, -2, i);   /* disable it: an erroring handler must not spam every tick */
            portConLogf("ge: lua: handler %d of '%s' disabled after an error\n", i, sEvents[k < LUA_EV_N ? k : 0].name);
        }
    }
    lua_settop(L, top);
}

static void portLuaTramp(int type, int a, int b, int c, int d, void *ud)
{
    (void) ud;
    portLuaDispatch(type, a, b, c, d, 0.0);
}

int portLuaHasTick(void)
{
    return sTickOn && sL != NULL;
}

/* A test hotkey (input_pc.c) or the `arena` console command; it reaches Lua's test_key handlers on the next game tick. */
void portLuaTestKey(const char *name)
{
    if (!sTestKeyOn || sL == NULL || name == NULL || sTestKeyN >= 8)
    {
        return;
    }
    snprintf(sTestKeyQ[sTestKeyN++], sizeof(sTestKeyQ[0]), "%s", name);
}

void portLuaTick(int clockTicks)
{
    int i;
    extern void portBenchSweepTick(int clockTicks);   /* port_arenamode.c: the benchmark's camera sweep */

    portBenchSweepTick(clockTicks);

    if (sL != NULL && sTestKeyN > 0)
    {
        int n = sTestKeyN;
        char q[8][8];

        memcpy(q, sTestKeyQ, sizeof(q));
        sTestKeyN = 0;
        for (i = 0; i < n; i++)
        {
            sTestKeyName = q[i];
            portLuaDispatch(LUA_EV_TESTKEY, 0, 0, 0, 0, 0.0);
        }
    }
    if (!sTickOn || sL == NULL)
    {
        return;
    }
    sTickAccum += clockTicks;
    if (sTickAccum >= LUA_TICK_PERIOD)
    {
        double dt = sTickAccum / 60.0;

        sTickAccum = 0;
        portLuaDispatch(LUA_EV_TICK, 0, 0, 0, 0, dt);
    }
}

/* Unhooks every Lua-owned bus listener and drops the handler tables (stage start). */
static void portLuaListenersClear(void)
{
    int t;

    for (t = 1; t < PORT_EV_COUNT; t++)
    {
        if (sHooked[t])
        {
            portEventOff(t, portLuaTramp, NULL);
            sHooked[t] = 0;
        }
    }
    sTickOn = 0;
    sTickAccum = 0;
    sTestKeyOn = 0;
    sTestKeyN = 0;
    if (sL != NULL)
    {
        lua_newtable(sL);
        lua_setfield(sL, LUA_REGISTRYINDEX, LUA_HANDLERS_KEY);
    }
}

static int portLuaOn(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    int k, type, n;

    luaL_checktype(L, 2, LUA_TFUNCTION);
    for (k = 0; k < LUA_EV_N; k++)
    {
        if (strcmp(sEvents[k].name, name) == 0) break;
    }
    if (k == LUA_EV_N)
    {
        return luaL_error(L, "unknown event '%s' (chr_died zone_enter zone_leave stage_start bond_died "
                             "hostage_follow hostage_wait damage chr_spawned item_pickup objective_changed alarm cctv_spotted use gadget_used body_found civilian_killed civilian_surrendered civilian_robbed tick test_key)", name);
    }
    type = sEvents[k].type;
    lua_getfield(L, LUA_REGISTRYINDEX, LUA_HANDLERS_KEY);
    if (lua_rawgeti(L, -1, type) != LUA_TTABLE)
    {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_rawseti(L, -3, type);
    }
    n = (int) lua_rawlen(L, -1);
    if (n >= LUA_MAX_HANDLERS)
    {
        return luaL_error(L, "too many '%s' handlers (max %d)", name, LUA_MAX_HANDLERS);
    }
    lua_pushvalue(L, 2);
    lua_rawseti(L, -2, n + 1);
    if (type == LUA_EV_TICK)
    {
        sTickOn = 1;
    }
    else if (type == LUA_EV_TESTKEY)
    {
        sTestKeyOn = 1;
    }
    else if (!sHooked[type])
    {
        if (portEventOn(type, portLuaTramp, NULL) != 0)
        {
            return luaL_error(L, "event bus is full for '%s'", name);
        }
        sHooked[type] = 1;
    }
    return 0;
}

/* The bindings below touch stage, player and prop state: in the menus (no stage live) they raise a Lua error
 * (logged by pcall) instead of reading state that does not exist. Outside a mode in a live stage they stay inert. */
static void portLuaNeedStage(lua_State *L, const char *api)
{
    if (!portLuaGameStageLive())
    {
        luaL_error(L, "%s needs a live stage (not available in the menus)", api);
    }
}

static int portLuaModeName(lua_State *L)
{
    lua_pushstring(L, portLuaGameModeName());
    return 1;
}

static int portLuaPadsCount(lua_State *L)
{
    portLuaNeedStage(L, "pads.count");
    lua_pushinteger(L, portLuaGamePadCount());
    return 1;
}

static int portLuaPadsPos(lua_State *L)
{
    portLuaNeedStage(L, "pads.pos");
    float x, y, z;

    if (!portLuaGamePadPos((int) luaL_checkinteger(L, 1), &x, &y, &z))
    {
        lua_pushnil(L);
        return 1;
    }
    lua_pushnumber(L, x);
    lua_pushnumber(L, y);
    lua_pushnumber(L, z);
    return 3;
}

/* pads.far_from(x, z, mindist [, maxdist [, waypoint_only]]) -> array of pad ids (XZ distance) */
static int portLuaPadsFarFrom(lua_State *L)
{
    portLuaNeedStage(L, "pads.far_from");
    static int ids[2048];
    float x = (float) luaL_checknumber(L, 1), z = (float) luaL_checknumber(L, 2);
    float mind = (float) luaL_checknumber(L, 3);
    float maxd = (float) luaL_optnumber(L, 4, 1.0e9);
    int wp = lua_toboolean(L, 5);
    int i, n = portLuaGamePadList(x, z, mind, maxd, wp, ids, 2048);

    lua_createtable(L, n, 0);
    for (i = 0; i < n; i++)
    {
        lua_pushinteger(L, ids[i]);
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

static int portLuaSpawnHostage(lua_State *L)
{
    portLuaNeedStage(L, "spawn.hostage");
    int c = portLuaGameSpawnHostage((int) luaL_checkinteger(L, 1));

    if (c < 0) lua_pushnil(L); else lua_pushinteger(L, c);
    return 1;
}

/* spawn.guard(pad [, {patrol = path_id, weapon = "ak47"}]) -> chrnum or nil */
static int portLuaSpawnGuard(lua_State *L)
{
    portLuaNeedStage(L, "spawn.guard");
    int pad = (int) luaL_checkinteger(L, 1), patrol = -1, item = -1, c;

    if (lua_istable(L, 2))
    {
        lua_getfield(L, 2, "patrol");
        if (!lua_isnil(L, -1)) patrol = (int) luaL_checkinteger(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, 2, "weapon");
        if (!lua_isnil(L, -1))
        {
            char key[48];
            const char *w = luaL_checkstring(L, -1);

            portLuaItemKey(w, key, sizeof(key));
            item = portLuaGameItemId(key);
            if (item < 0)
            {
                return luaL_error(L, "unknown weapon '%s' (see weapon.items())", w);
            }
        }
        lua_pop(L, 1);
    }
    c = portLuaGameSpawnGuard(pad, patrol, item);
    if (c < 0) lua_pushnil(L); else lua_pushinteger(L, c);
    return 1;
}

static int portLuaZoneAdd(lua_State *L)
{
    portLuaNeedStage(L, "zone.add");
    int z = portLuaGameZoneAdd(luaL_checkstring(L, 1), (float) luaL_checknumber(L, 2), (float) luaL_checknumber(L, 3),
                               (float) luaL_checknumber(L, 4), (float) luaL_checknumber(L, 5));

    if (z < 0) lua_pushnil(L); else lua_pushinteger(L, z);
    return 1;
}

static int portLuaZoneMove(lua_State *L)
{
    portLuaNeedStage(L, "zone.move");
    portLuaGameZoneMove((int) luaL_checkinteger(L, 1), (float) luaL_checknumber(L, 2), (float) luaL_checknumber(L, 3),
                        (float) luaL_checknumber(L, 4));
    return 0;
}

static int portLuaZoneRemove(lua_State *L)
{
    portLuaNeedStage(L, "zone.remove");
    portLuaGameZoneRemove((int) luaL_checkinteger(L, 1));
    return 0;
}

static int portLuaZoneWatch(lua_State *L)
{
    portLuaNeedStage(L, "zone.watch");
    lua_pushboolean(L, portLuaGameZoneWatch((int) luaL_checkinteger(L, 1)));
    return 1;
}

static int portLuaZoneContains(lua_State *L)
{
    portLuaNeedStage(L, "zone.contains");
    lua_pushboolean(L, portLuaGameZoneContains((int) luaL_checkinteger(L, 1), (int) luaL_checkinteger(L, 2)));
    return 1;
}

/* zone.bonds(id) -> array of the player indices of the Bonds inside (state of the last tick) */
static int portLuaZoneBonds(lua_State *L)
{
    portLuaNeedStage(L, "zone.bonds");
    int mask = portLuaGameZoneBonds((int) luaL_checkinteger(L, 1)), p, n = 0;

    lua_createtable(L, 2, 0);
    for (p = 0; p < 4; p++)
    {
        if ((mask >> p) & 1)
        {
            lua_pushinteger(L, p);
            lua_rawseti(L, -2, ++n);
        }
    }
    return 1;
}

static int portLuaZoneName(lua_State *L)
{
    portLuaNeedStage(L, "zone.name");
    const char *n = portLuaGameZoneName((int) luaL_checkinteger(L, 1));

    if (n == NULL) lua_pushnil(L); else lua_pushstring(L, n);
    return 1;
}

/* An optional player index argument: nil / absent = -1 (every player for the HUD, player 0 for the Bond calls), else 0..3. */
static int portLuaOptPlayer(lua_State *L, int arg)
{
    lua_Integer p;

    if (lua_isnoneornil(L, arg))
    {
        return -1;
    }
    p = luaL_checkinteger(L, arg);
    if (p < 0 || p > 3)
    {
        return luaL_error(L, "player index must be 0..3 (0 = the host's Bond), not %d", (int) p);
    }
    return (int) p;
}

/* hud.set(line, text [, player]): player nil = every player's HUD, else only that Bond's (each machine draws its own Bond's lines) */
static int portLuaHudSet(lua_State *L)
{
    portLuaNeedStage(L, "hud.set");
    lua_Integer line = luaL_checkinteger(L, 1);
    int player = portLuaOptPlayer(L, 3);

    if (line < 1 || line > 3)
    {
        return luaL_error(L, "hud line must be 1..3");
    }
    portLuaGameHudSet(player, (int) line - 1, lua_isnil(L, 2) ? NULL : luaL_checkstring(L, 2));
    return 0;
}

/* hud.clear([player]): all three lines of every player, or of one */
static int portLuaHudClear(lua_State *L)
{
    portLuaNeedStage(L, "hud.clear");
    portLuaGameHudClear(portLuaOptPlayer(L, 1));
    return 0;
}

/* bond.pos([p]) -> x, y, z or nil. No argument = player 0 on every machine (NOT the local player). */
static int portLuaBondPos(lua_State *L)
{
    portLuaNeedStage(L, "bond.pos");
    float x, y, z;

    if (!portLuaGameBondPos(portLuaOptPlayer(L, 1), &x, &y, &z))
    {
        lua_pushnil(L);
        return 1;
    }
    lua_pushnumber(L, x);
    lua_pushnumber(L, y);
    lua_pushnumber(L, z);
    return 3;
}

/* bond.count() -> number of Bonds in this stage (1 solo, 2 co-op), 0 outside a mode */
static int portLuaBondCount(lua_State *L)
{
    portLuaNeedStage(L, "bond.count");
    lua_pushinteger(L, portLuaGameBondCount());
    return 1;
}

/* bond.dead([p]) -> true / false, nil when there is no such Bond */
static int portLuaBondDead(lua_State *L)
{
    portLuaNeedStage(L, "bond.dead");
    int d = portLuaGameBondDead(portLuaOptPlayer(L, 1));

    if (d < 0) lua_pushnil(L); else lua_pushboolean(L, d);
    return 1;
}

/* bond.nearest(x, y, z) -> player index, distance of the nearest Bond in play, or nil */
static int portLuaBondNearest(lua_State *L)
{
    portLuaNeedStage(L, "bond.nearest");
    float d = 0.0f;
    int p = portLuaGameBondNearest((float) luaL_checknumber(L, 1), (float) luaL_checknumber(L, 2), (float) luaL_checknumber(L, 3), &d);

    if (p < 0)
    {
        lua_pushnil(L);
        return 1;
    }
    lua_pushinteger(L, p);
    lua_pushnumber(L, d);
    return 2;
}

/* bond.all_down() -> true when every Bond is dead (co-op: nobody left who can respawn) */
static int portLuaBondAllDown(lua_State *L)
{
    portLuaNeedStage(L, "bond.all_down");
    lua_pushboolean(L, portLuaGameBondAllDown());
    return 1;
}

/* bond.id(p) -> the chr id of Bond p in events and zone calls (CHR_BOND - p: -2, -3); bond.player(chr) -> its player index or nil */
static int portLuaBondId(lua_State *L)
{
    lua_Integer p = luaL_checkinteger(L, 1);

    if (p < 0 || p > 3)
    {
        return luaL_error(L, "player index must be 0..3");
    }
    lua_pushinteger(L, PORT_CHR_BONDP((int) p));
    return 1;
}

static int portLuaBondPlayer(lua_State *L)
{
    lua_Integer id = luaL_checkinteger(L, 1);

    if (id > 2147483647 || id < -2147483647 || !PORT_CHR_IS_BOND((int) id))
    {
        lua_pushnil(L);
    }
    else
    {
        lua_pushinteger(L, PORT_CHR_BOND_PLAYER((int) id));
    }
    return 1;
}

static const char *portLuaHostageStateName(int s)
{
    static const char *const names[] = { "wait", "follow", "extracted", "dead" };

    return (s >= 0 && s <= 3) ? names[s] : NULL;
}

static int portLuaHostagesList(lua_State *L)
{
    portLuaNeedStage(L, "hostages.list");
    int chrs[PORTLUA_HOSTAGE_MAX], states[PORTLUA_HOSTAGE_MAX], leaders[PORTLUA_HOSTAGE_MAX];
    int i, n = portLuaGameHostageList(chrs, states, leaders, PORTLUA_HOSTAGE_MAX);

    lua_createtable(L, n, 0);
    for (i = 0; i < n; i++)
    {
        lua_createtable(L, 0, 3);
        lua_pushinteger(L, chrs[i]);
        lua_setfield(L, -2, "chr");
        lua_pushstring(L, portLuaHostageStateName(states[i]));
        lua_setfield(L, -2, "state");
        if (leaders[i] >= 0)   /* the Bond a following hostage trails (player index) */
        {
            lua_pushinteger(L, leaders[i]);
            lua_setfield(L, -2, "leader");
        }
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

static int portLuaHostagesState(lua_State *L)
{
    portLuaNeedStage(L, "hostages.state");
    const char *s = portLuaHostageStateName(portLuaGameHostageState((int) luaL_checkinteger(L, 1)));

    if (s == NULL) lua_pushnil(L); else lua_pushstring(L, s);
    return 1;
}

static int portLuaHostagesSetState(lua_State *L)
{
    portLuaNeedStage(L, "hostages.set_state");
    const char *s = luaL_checkstring(L, 2);
    int state;

    if (strcmp(s, "wait") == 0) state = 0;
    else if (strcmp(s, "follow") == 0) state = 1;
    else return luaL_error(L, "state must be 'wait' or 'follow'");
    /* hostages.set_state(chr, "follow" [, player]): the Bond that leads it, default player 0 */
    lua_pushboolean(L, portLuaGameHostageSet((int) luaL_checkinteger(L, 1), state, portLuaOptPlayer(L, 3)));
    return 1;
}

static int portLuaHostagesExtract(lua_State *L)
{
    portLuaNeedStage(L, "hostages.extract");
    lua_pushboolean(L, portLuaGameHostageExtract((int) luaL_checkinteger(L, 1)));
    return 1;
}

static int portLuaMissionEnd(lua_State *L, int win)
{
    portLuaNeedStage(L, win ? "mission.win" : "mission.fail");
    const char *msg = luaL_optstring(L, 1, "");

    if (portLuaGameEndMission(win))
    {
        portConLogf("ge: lua: mission %s: %s\n", win ? "win" : "fail", msg);
        lua_pushboolean(L, 1);
    }
    else
    {
        lua_pushboolean(L, 0);   /* no mode, or the mission already ended */
    }
    return 1;
}

static int portLuaMissionWin(lua_State *L) { return portLuaMissionEnd(L, 1); }
static int portLuaMissionFail(lua_State *L) { return portLuaMissionEnd(L, 0); }

/* ---- mods: the modifier registry (port_modifier.c) ---- */

static int portLuaModsIndex(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    int i = portModifierFind(name);

    if (i < 0)
    {
        return luaL_error(L, "no modifier '%s' (see mods.list())", name);
    }
    return i;
}

static int portLuaModsGet(lua_State *L)
{
    int i = portLuaModsIndex(L);
    const PortModifierInfo *m = portModifierAt(i);
    int sv;

    /* the co-op session settings (coop_exits / coop_respawn / coop_lives / coop_lives_mode) are the HOST's in netplay: a guest's own value
     * is not the session's, so a script (which runs on both machines) must read the session's */
    if (portModifierIsSession(i) && portLuaGameSessionMod(m->name, &sv))
    {
        lua_pushinteger(L, sv);
        return 1;
    }
    if (m->kind == PORT_MOD_TOGGLE) lua_pushboolean(L, portModifierGet(i));
    else lua_pushinteger(L, portModifierGet(i));
    return 1;
}

static int portLuaModsSet(lua_State *L)
{
    int i = portLuaModsIndex(L);
    int v = lua_isboolean(L, 2) ? lua_toboolean(L, 2) : (int) luaL_checkinteger(L, 2);

    lua_pushboolean(L, portModifierSetQuiet(i, v));   /* a script's change is not written to midas.ini */
    return 1;
}

static int portLuaModsList(lua_State *L)
{
    int i, n = portModifierCount();

    lua_createtable(L, n, 0);
    for (i = 0; i < n; i++)
    {
        const PortModifierInfo *m = portModifierAt(i);

        lua_createtable(L, 0, 9);
        lua_pushstring(L, m->name);
        lua_setfield(L, -2, "name");
        lua_pushstring(L, m->label);
        lua_setfield(L, -2, "label");
        lua_pushstring(L, m->kind == PORT_MOD_TOGGLE ? "toggle" : "int");
        lua_setfield(L, -2, "kind");
        {
            int sv;

            if (portModifierIsSession(i) && portLuaGameSessionMod(m->name, &sv)) lua_pushinteger(L, sv);   /* the host's, in co-op */
            else if (m->kind == PORT_MOD_TOGGLE) lua_pushboolean(L, portModifierGet(i));
            else lua_pushinteger(L, portModifierGet(i));
        }
        lua_setfield(L, -2, "value");
        lua_pushinteger(L, m->min);
        lua_setfield(L, -2, "min");
        lua_pushinteger(L, m->max);
        lua_setfield(L, -2, "max");
        lua_pushinteger(L, m->step);
        lua_setfield(L, -2, "step");
        lua_pushinteger(L, m->def);
        lua_setfield(L, -2, "default");
        lua_pushstring(L, m->desc);
        lua_setfield(L, -2, "desc");
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

/* ---- music: level music override (port_music.c) ---- */

static int portLuaMusicId(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    int id = portMusicFind(name);

    if (id < 0)
    {
        return luaL_error(L, "no music track '%s' (see music.list())", name);
    }
    return id;
}

/* music.set(name): the main track of the current/next stage (cleared at the next stage start). */
static int portLuaMusicSet(lua_State *L)
{
    int id = portLuaMusicId(L);

    lua_pushboolean(L, portLuaGameAllowed() && portMusicSetMain(id));
    return 1;
}

/* music.play(name): the main channel now; false when no stage is live. */
static int portLuaMusicPlay(lua_State *L)
{
    int id = portLuaMusicId(L);

    lua_pushboolean(L, portLuaGameAllowed() && portMusicPlayNow(id, 1));
    return 1;
}

static int portLuaMusicList(lua_State *L)
{
    int id, n = portMusicTrackCount(), k = 0;

    lua_createtable(L, n, 0);
    for (id = 1; id <= n; id++)
    {
        const char *s = portMusicName(id);

        if (s != NULL)
        {
            lua_pushstring(L, s);
            lua_rawseti(L, -2, ++k);
        }
    }
    return 1;
}

static int portLuaMusicCurrent(lua_State *L)
{
    const char *s = portMusicName(portMusicCurrent());

    if (s == NULL)
    {
        lua_pushnil(L);
    }
    else
    {
        lua_pushstring(L, s);
    }
    return 1;
}

/* ---- menu API: items for the Mods page ---------------------------------- */

#define LUA_MENU_MAX 24
static struct
{
    char label[40];
    int toggle, refGet, refSet, mn, mx, step, disabled, boot, value;
} sMenu[LUA_MENU_MAX];
static int sMenuN;
static int sMenuBooting;
static int sMenuBooted;

static void portLuaMenuDrop(int i)
{
    if (sL != NULL)
    {
        luaL_unref(sL, LUA_REGISTRYINDEX, sMenu[i].refGet);
        luaL_unref(sL, LUA_REGISTRYINDEX, sMenu[i].refSet);
    }
}

/* Called at each stage start: items from the last stage's scripts go, boot items stay. */
static void portLuaMenuStageClear(void)
{
    int i, k = 0;

    for (i = 0; i < sMenuN; i++)
    {
        if (sMenu[i].boot)
        {
            sMenu[k++] = sMenu[i];
        }
        else
        {
            portLuaMenuDrop(i);
        }
    }
    sMenuN = k;
}

static int portLuaMenuAdd(lua_State *L, int toggle)
{
    const char *label = luaL_checkstring(L, 1);
    int a = toggle ? 2 : 5;   /* index of the getter */
    int i;

    if (!toggle)
    {
        luaL_checkinteger(L, 2);
        luaL_checkinteger(L, 3);
        luaL_checkinteger(L, 4);
    }
    luaL_checktype(L, a, LUA_TFUNCTION);
    luaL_checktype(L, a + 1, LUA_TFUNCTION);
    if (sMenuN >= LUA_MENU_MAX)
    {
        return luaL_error(L, "too many menu items (max %d)", LUA_MENU_MAX);
    }
    i = sMenuN++;
    memset(&sMenu[i], 0, sizeof(sMenu[i]));
    snprintf(sMenu[i].label, sizeof(sMenu[i].label), "%s", label);
    sMenu[i].toggle = toggle;
    if (!toggle)
    {
        sMenu[i].mn = (int) lua_tointeger(L, 2);
        sMenu[i].mx = (int) lua_tointeger(L, 3);
        sMenu[i].step = (int) lua_tointeger(L, 4);
        if (sMenu[i].step < 1) sMenu[i].step = 1;
        if (sMenu[i].mx < sMenu[i].mn) sMenu[i].mx = sMenu[i].mn;
    }
    lua_pushvalue(L, a + 1);
    sMenu[i].refSet = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_pushvalue(L, a);
    sMenu[i].refGet = luaL_ref(L, LUA_REGISTRYINDEX);
    sMenu[i].boot = sMenuBooting;
    portConLogf("ge: lua: menu item '%s' added (%s)\n", sMenu[i].label, toggle ? "toggle" : "number");
    return 0;
}

static int portLuaMenuAddToggle(lua_State *L) { return portLuaMenuAdd(L, 1); }
static int portLuaMenuAddNumber(lua_State *L) { return portLuaMenuAdd(L, 0); }

int portLuaMenuCount(void) { return sMenuN; }
const char *portLuaMenuLabel(int i) { return (i >= 0 && i < sMenuN) ? sMenu[i].label : ""; }
int portLuaMenuIsToggle(int i) { return i >= 0 && i < sMenuN && sMenu[i].toggle; }
int portLuaMenuDisabled(int i) { return i < 0 || i >= sMenuN || sMenu[i].disabled; }
int portLuaMenuValue(int i) { return (i >= 0 && i < sMenuN) ? sMenu[i].value : 0; }

void portLuaMenuRange(int i, int *mn, int *mx, int *step)
{
    if (i >= 0 && i < sMenuN)
    {
        *mn = sMenu[i].mn;
        *mx = sMenu[i].mx;
        *step = sMenu[i].step;
    }
}

/* Calls the item's getter or setter under pcall; an error is logged and disables the item. */
static void portLuaMenuCall(int i, int isSet, int v)
{
    lua_State *L = sL;
    int top;

    if (L == NULL || sMenu[i].disabled)
    {
        return;
    }
    top = lua_gettop(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, isSet ? sMenu[i].refSet : sMenu[i].refGet);
    if (isSet)
    {
        if (sMenu[i].toggle) lua_pushboolean(L, v);
        else lua_pushinteger(L, v);
    }
    if (portLuaCall(L, isSet ? 1 : 0, isSet ? 0 : 1) != 0)
    {
        sMenu[i].disabled = 1;
        portConLogf("ge: lua: menu item '%s' disabled after an error\n", sMenu[i].label);
    }
    else if (!isSet)
    {
        if (sMenu[i].toggle) sMenu[i].value = lua_toboolean(L, -1) ? 1 : 0;
        else sMenu[i].value = (int) lua_tointeger(L, -1);
    }
    lua_settop(L, top);
}

void portLuaMenuRefresh(void)
{
    int i;

    for (i = 0; i < sMenuN; i++)
    {
        portLuaMenuCall(i, 0, 0);
    }
}

void portLuaMenuSet(int i, int v)
{
    if (i < 0 || i >= sMenuN)
    {
        return;
    }
    if (!sMenu[i].toggle)
    {
        if (v < sMenu[i].mn) v = sMenu[i].mn;
        if (v > sMenu[i].mx) v = sMenu[i].mx;
    }
    portLuaMenuCall(i, 1, v);
    portLuaMenuCall(i, 0, 0);
}

static const luaL_Reg sMenuLib[] = { { "add_toggle", portLuaMenuAddToggle }, { "add_number", portLuaMenuAddNumber },
                                     { NULL, NULL } };

static const luaL_Reg sModsLib[] = { { "get", portLuaModsGet }, { "set", portLuaModsSet }, { "list", portLuaModsList },
                                     { NULL, NULL } };
static const luaL_Reg sMusicLib[] = { { "set", portLuaMusicSet }, { "play", portLuaMusicPlay }, { "list", portLuaMusicList },
                                      { "current", portLuaMusicCurrent }, { NULL, NULL } };
/* mode.env_flag("GE_...") -> true when that environment variable is set and not "0" (Lua has no os library) */
static int portLuaModeEnvFlag(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    const char *v = (strncmp(name, "GE_", 3) == 0) ? getenv(name) : NULL;

    lua_pushboolean(L, v != NULL && v[0] != '\0' && strcmp(v, "0") != 0);
    return 1;
}

/* mode.keep_locks([on = true]): keep the stage's keyed doors and padlocks working. The level reset otherwise turns the UNLOCK DOORS cheat on one
 * second into the stage, so no key door ever stops Bond. Call it from the script's top level (before that second is up). Inert outside a mode. */
static int portLuaModeKeepLocks(lua_State *L)
{
    portModeKeepLocks(lua_isnone(L, 1) ? 1 : lua_toboolean(L, 1));
    return 0;
}

static const luaL_Reg sModeLib[] ={ { "name", portLuaModeName }, { "env_flag", portLuaModeEnvFlag }, { "keep_locks", portLuaModeKeepLocks },
                                    { NULL, NULL } };
static const luaL_Reg sPadsLib[] = { { "count", portLuaPadsCount }, { "pos", portLuaPadsPos },
                                     { "far_from", portLuaPadsFarFrom }, { NULL, NULL } };
static const luaL_Reg sSpawnLib[] = { { "hostage", portLuaSpawnHostage }, { "guard", portLuaSpawnGuard }, { NULL, NULL } };
static const luaL_Reg sZoneLib[] = { { "add", portLuaZoneAdd }, { "move", portLuaZoneMove }, { "remove", portLuaZoneRemove },
                                     { "watch", portLuaZoneWatch }, { "contains", portLuaZoneContains },
                                     { "bonds", portLuaZoneBonds }, { "name", portLuaZoneName }, { NULL, NULL } };
static const luaL_Reg sHudLib[] = { { "set", portLuaHudSet }, { "clear", portLuaHudClear }, { NULL, NULL } };
static const luaL_Reg sBondLib[] = { { "pos", portLuaBondPos }, { "count", portLuaBondCount }, { "dead", portLuaBondDead },
                                     { "nearest", portLuaBondNearest }, { "all_down", portLuaBondAllDown },
                                     { "id", portLuaBondId }, { "player", portLuaBondPlayer }, { NULL, NULL } };
static const luaL_Reg sHostagesLib[] = { { "list", portLuaHostagesList }, { "state", portLuaHostagesState },
                                         { "set_state", portLuaHostagesSetState }, { "extract", portLuaHostagesExtract },
                                         { NULL, NULL } };
static const luaL_Reg sMissionLib[] = { { "win", portLuaMissionWin }, { "fail", portLuaMissionFail }, { NULL, NULL } };

/* probe.level() / probe.pads(): log the stage's setup objects (doors, keys, CCTV, alarms, monitors, safes, guard records) and the
 * pad table with rooms. Read-only; the output goes to stdout as "ge: probe: ..." lines. */
static int portLuaProbeLevel(lua_State *L)
{
    portLuaNeedStage(L, "probe.level");
    portProbeLevel();
    return 0;
}

static int portLuaProbePads(lua_State *L)
{
    portLuaNeedStage(L, "probe.pads");
    portProbePads();
    return 0;
}

/* probe.alarm() -> ticks of siren left (0 = no alarm). The stock CCTV raises it after 300 ticks of unbroken sight. */
static int portLuaProbeAlarm(lua_State *L)
{
    portLuaNeedStage(L, "probe.alarm");
    lua_pushinteger(L, portLuaGameModeActive() ? portProbeAlarm() : 0);
    return 1;
}

/* probe.doors_unlocked() -> true while the UNLOCK DOORS cheat is on for Bond (the mode default; mode.keep_locks() prevents it) */
static int portLuaProbeDoorsUnlocked(lua_State *L)
{
    portLuaNeedStage(L, "probe.doors_unlocked");
    lua_pushboolean(L, portLuaGameModeActive() && portProbeDoorsUnlocked());
    return 1;
}

static int portLuaProbeCctv(lua_State *L)
{
    portLuaNeedStage(L, "probe.cctv");
    portProbeCctv();
    return 0;
}

static const luaL_Reg sProbeLib[] = { { "level", portLuaProbeLevel }, { "pads", portLuaProbePads }, { "alarm", portLuaProbeAlarm },
                                      { "cctv", portLuaProbeCctv }, { "doors_unlocked", portLuaProbeDoorsUnlocked }, { NULL, NULL } };

static void portLuaRegisterLib(lua_State *L, const char *name, const luaL_Reg *reg)
{
    luaL_newlib(L, reg);
    lua_setglobal(L, name);
}

/* ---- deterministic math.random ------------------------------------------------------------------------------------------------
 * Lua's own math.random is seeded from the clock and an address when the state is made, so two machines of a netplay session draw different
 * numbers: a mode script that placed spawns with it would desync. This generator (SplitMix64, 64-bit state) replaces math.random and
 * math.randomseed. Re-seeded at every stage start: in co-op from the session seed (the host's, the same on both machines), in solo from the
 * clock as before. math.randomseed(n) with an explicit number is deterministic too; without an argument it is ignored in co-op. */
static uint64_t sRandState;

static uint64_t portLuaRandNext(void)
{
    uint64_t z;

    sRandState += 0x9E3779B97F4A7C15ULL;
    z = sRandState;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

static void portLuaRandReseed(void)
{
    unsigned seed = portLuaGameRandomSeed();

    if (seed != 0u)
    {
        sRandState = (uint64_t) seed * 0x2545F4914F6CDD1DULL + 0x1234567ULL;
    }
    else
    {
        int local;

        sRandState = (uint64_t) time(NULL) * 0x9E3779B97F4A7C15ULL ^ (uint64_t) (uintptr_t) &local;
    }
}

/* the value in [0, n] with no modulo bias (Lua's own "project") */
static uint64_t portLuaRandProject(uint64_t ran, uint64_t n)
{
    if ((n & (n + 1)) == 0)
    {
        return ran & n;
    }
    else
    {
        uint64_t lim = n;

        lim |= (lim >> 1);
        lim |= (lim >> 2);
        lim |= (lim >> 4);
        lim |= (lim >> 8);
        lim |= (lim >> 16);
        lim |= (lim >> 32);
        while ((ran &= lim) > n)
        {
            ran = portLuaRandNext();
        }
        return ran;
    }
}

static int portLuaMathRandom(lua_State *L)
{
    lua_Integer low, up;
    uint64_t rv = portLuaRandNext();

    switch (lua_gettop(L))
    {
        case 0:
            lua_pushnumber(L, (lua_Number) (rv >> 11) * (0.5 / ((uint64_t) 1 << 52)));
            return 1;
        case 1:
            low = 1;
            up = luaL_checkinteger(L, 1);
            if (up == 0)
            {   /* math.random(0): a random integer, all bits */
                lua_pushinteger(L, (lua_Integer) rv);
                return 1;
            }
            break;
        case 2:
            low = luaL_checkinteger(L, 1);
            up = luaL_checkinteger(L, 2);
            break;
        default:
            return luaL_error(L, "wrong number of arguments");
    }
    luaL_argcheck(L, low <= up, lua_gettop(L), "interval is empty");
    lua_pushinteger(L, (lua_Integer) (portLuaRandProject(rv, (uint64_t) up - (uint64_t) low) + (uint64_t) low));
    return 1;
}

static int portLuaMathRandomseed(lua_State *L)
{
    if (lua_isnone(L, 1))
    {
        if (portLuaGameRandomSeed() == 0u)
        {
            portLuaRandReseed();   /* solo: a fresh clock seed, as the stock call */
        }
        return 0;   /* co-op: ignored (the seed is the session's) */
    }
    sRandState = (uint64_t) luaL_checkinteger(L, 1) * 0x9E3779B97F4A7C15ULL + (uint64_t) luaL_optinteger(L, 2, 0);
    return 0;
}

/* ---- deterministic table.sort -------------------------------------------------------------------------------------------------------
 * Lua's table.sort picks its pivot with clock() and time() for arrays of more than 100 elements (l_randomizePivot), so a comparator that
 * leaves ties open can order them differently on two machines. This replacement is a bottom-up MERGE sort: stable, no clock, the same result
 * on every machine for any comparator (a comparator that is not a strict order simply gives some order; it raises no "invalid order function"). */
static const char sSortSource[] =
    "local floor = math.floor\n"
    "table.sort = function(t, lt)\n"
    "  local n = #t\n"
    "  if n < 2 then return end\n"
    "  if lt == nil then lt = function(a, b) return a < b end end\n"
    "  local a, b = {}, {}\n"
    "  for i = 1, n do a[i] = t[i] end\n"
    "  local width = 1\n"
    "  while width < n do\n"
    "    local i = 1\n"
    "    while i <= n do\n"
    "      local mid = i + width; if mid > n + 1 then mid = n + 1 end\n"
    "      local hi = i + 2 * width; if hi > n + 1 then hi = n + 1 end\n"
    "      local l, r, k = i, mid, i\n"
    "      while l < mid and r < hi do\n"
    "        if lt(a[r], a[l]) then b[k] = a[r]; r = r + 1 else b[k] = a[l]; l = l + 1 end\n"
    "        k = k + 1\n"
    "      end\n"
    "      while l < mid do b[k] = a[l]; l = l + 1; k = k + 1 end\n"
    "      while r < hi do b[k] = a[r]; r = r + 1; k = k + 1 end\n"
    "      i = i + 2 * width\n"
    "    end\n"
    "    a, b = b, a\n"
    "    width = width * 2\n"
    "  end\n"
    "  for i = 1, n do t[i] = a[i] end\n"
    "end\n";

/* ---- include(name): run another mode script (Data/Scripts/modes/<name>.lua) from a mode script. The modes folder is hashed with the
 * other scripts at the netplay handshake, so both machines run the same text. Used by preset modes (hostage-flatland.lua sets its
 * HOSTAGE_SETTINGS and includes "hostage"). Only while a mode script is running; depth 4. ---- */
static int sIncludeDepth;
static int sInMode;

static int portLuaInclude(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    const char *c;
    char path[600];

    if (!sInMode)
    {
        return luaL_error(L, "include() works only inside a mode script");
    }
    if (name[0] == '\0' || strlen(name) > 40)
    {
        return luaL_error(L, "bad script name");
    }
    for (c = name; *c != '\0'; c++)
    {
        if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '_' || *c == '-'))
        {
            return luaL_error(L, "script name '%s': only letters, digits, '_' and '-'", name);
        }
    }
    if (sIncludeDepth >= 4)
    {
        return luaL_error(L, "include() nested too deep");
    }
    snprintf(path, sizeof(path), "%s/modes/%s.lua", portLuaGameScriptDir(), name);
    if (luaL_loadfilex(L, path, "t") != LUA_OK)
    {
        return lua_error(L);
    }
    sIncludeDepth++;
    lua_call(L, 0, 0);   /* an error unwinds through the caller's pcall; the depth is reset when the mode script starts */
    sIncludeDepth--;
    return 0;
}

/* ---- state creation ---------------------------------------------------- */

static lua_State *portLuaState(void)
{
    if (sL != NULL)
    {
        return sL;
    }
    /* A FIXED string-hash seed (Lua 5.5 would take luaL_makeseed: time and addresses): pairs() over a table with string keys walks the nodes in
     * hash order, which must be the same on both machines of a netplay session. */
    sL = lua_newstate(portLuaAlloc, NULL, 0x4D696461u);
    if (sL == NULL)
    {
        portConLogf("ge: lua: could not create the Lua state\n");
        return NULL;
    }
    {   /* Lua's string cache is indexed by the ADDRESS of the C string passed to lua_pushstring, so a stack buffer puts the same string
         * in another slot at another call depth. It is a lookup cache with no meaning; the snapshot compare ignores it. */
        extern void portSnapBenign(const char *name, const void *p, unsigned int n);
        global_State *gs = sL->l_G;

        portSnapBenign("lua.strcache", gs->strcache, sizeof(gs->strcache));
    }
    luaL_openselectedlibs(sL, LUA_GLIBK | LUA_STRLIBK | LUA_TABLIBK | LUA_MATHLIBK |
                              LUA_UTF8LIBK | LUA_COLIBK, 0);
    lua_pushnil(sL);
    lua_setglobal(sL, "dofile");
    lua_pushnil(sL);
    lua_setglobal(sL, "loadfile");
    lua_pushnil(sL);
    lua_setglobal(sL, "collectgarbage");
    lua_pushcfunction(sL, portLuaPrint);
    lua_setglobal(sL, "print");
    lua_getglobal(sL, "math");   /* the deterministic generator (above) replaces the clock-seeded one */
    lua_pushcfunction(sL, portLuaMathRandom);
    lua_setfield(sL, -2, "random");
    lua_pushcfunction(sL, portLuaMathRandomseed);
    lua_setfield(sL, -2, "randomseed");
    lua_pop(sL, 1);
    if (luaL_dostring(sL, sSortSource) != LUA_OK)   /* (before the instruction hook is set) */
    {
        portConLogf("ge: lua: table.sort replacement failed: %s\n", lua_tostring(sL, -1));
        lua_pop(sL, 1);
    }
    luaL_newlib(sL, sWeaponLib);
    lua_setglobal(sL, "weapon");
    portLuaRegisterLib(sL, "mods", sModsLib);
    portLuaRegisterLib(sL, "menu", sMenuLib);
    portLuaRegisterLib(sL, "music", sMusicLib);
    portLuaRegisterLib(sL, "mode", sModeLib);
    portLuaRegisterLib(sL, "pads", sPadsLib);
    portLuaRegisterLib(sL, "spawn", sSpawnLib);
    portLuaRegisterLib(sL, "zone", sZoneLib);
    portLuaRegisterLib(sL, "hud", sHudLib);
    portLuaRegisterLib(sL, "bond", sBondLib);
    portLuaRegisterLib(sL, "hostages", sHostagesLib);
    portLuaRegisterLib(sL, "mission", sMissionLib);
    portLuaRegisterLib(sL, "probe", sProbeLib);
    {   /* arena mode additions to spawn and bond (port_luavm_arena.c) */
        extern void portLuaArenaRegister(lua_State *L);

        portLuaArenaRegister(sL);
    }
    {   /* heist bindings: door, alarm, cctv, spawn.key, bond.has_key, mode.per_player_keys, hud.countdown (port_luavm_heist.c) */
        extern void portLuaHeistRegister(lua_State *L);

        portLuaHeistRegister(sL);
    }
    lua_pushcfunction(sL, portLuaOn);
    lua_setglobal(sL, "on");
    lua_pushcfunction(sL, portLuaInclude);
    lua_setglobal(sL, "include");
    lua_pushinteger(sL, PORT_CHR_BOND);
    lua_setglobal(sL, "CHR_BOND");
    lua_pushinteger(sL, PORT_CHR_NONE);
    lua_setglobal(sL, "CHR_NONE");
    lua_newtable(sL);
    lua_setfield(sL, LUA_REGISTRYINDEX, LUA_HANDLERS_KEY);
    lua_sethook(sL, portLuaHook, LUA_MASKCOUNT, LUA_HOOK_STEP);
    portConLogf("ge: lua: %s ready\n", LUA_RELEASE);
    return sL;
}

/* ---- scripts and console ----------------------------------------------- */

static int portLuaCmpName(const void *a, const void *b)
{
    return strcmp((const char *) a, (const char *) b);
}

/* Data/Scripts/modes/<GE_MODE>.lua when a mode is active and the file exists (name limited to
 * letters, digits, '_' and '-'). */
static int portLuaModeScriptPath(char *out, size_t cap)
{
    const char *name = portLuaGameModeName();
    const char *p;
    FILE *f;

    if (!portLuaGameModeActive() || name[0] == '\0')
    {
        return 0;
    }
    for (p = name; *p != '\0'; p++)
    {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_' || *p == '-'))
        {
            return 0;
        }
    }
    snprintf(out, cap, "%s/modes/%s.lua", portLuaGameScriptDir(), name);
    f = fopen(out, "rb");
    if (f == NULL)
    {
        return 0;
    }
    fclose(f);
    return 1;
}

static void portLuaRunScripts(void)
{
    static char names[LUA_MAX_SCRIPTS][64];
    const char *dir = portLuaGameScriptDir();
    DIR *d = opendir(dir);
    struct dirent *e;
    char modePath[512];
    int i, n = 0, modeScript;
    const char *testScript;

    if (d == NULL)
    {
        return;
    }
    while ((e = readdir(d)) != NULL && n < LUA_MAX_SCRIPTS)
    {
        size_t l = strlen(e->d_name);

        if (l > 4 && l < sizeof(names[0]) && strcmp(e->d_name + l - 4, ".lua") == 0)
        {
            memcpy(names[n++], e->d_name, l + 1);
        }
    }
    closedir(d);
    modeScript = portLuaModeScriptPath(modePath, sizeof(modePath));
    testScript = getenv("GE_LUA_TEST");   /* test harness: one extra script file, not shipped */
    if (testScript != NULL && testScript[0] == '\0')
    {
        testScript = NULL;
    }
    if (n == 0 && !modeScript && testScript == NULL)
    {
        return;   /* no scripts: the state is never created */
    }
    qsort(names, n, sizeof(names[0]), portLuaCmpName);
    if (portLuaState() == NULL)
    {
        return;
    }
    portLuaGameSnapshot();
    portLuaGameWeaponReset();
    for (i = 0; i < n; i++)
    {
        char path[512];

        snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
        portConLogf("ge: lua: running %s\n", names[i]);
        if (luaL_loadfilex(sL, path, "t") != LUA_OK)
        {
            portConLogf("ge: lua: error: %s\n", lua_tostring(sL, -1));
            lua_pop(sL, 1);
            continue;
        }
        portLuaCall(sL, 0, 0);
    }
    if (modeScript)
    {   /* the mode's own script runs last, after the level reset and the regular scripts */
        portConLogf("ge: lua: running mode script modes/%s.lua\n", portLuaGameModeName());
        if (luaL_loadfilex(sL, modePath, "t") != LUA_OK)
        {
            portConLogf("ge: lua: error: %s\n", lua_tostring(sL, -1));
            lua_pop(sL, 1);
        }
        else
        {
            sInMode = 1;
            sIncludeDepth = 0;
            portLuaCall(sL, 0, 0);
            sInMode = 0;
        }
    }
    if (testScript != NULL)
    {
        portConLogf("ge: lua: running test script %s\n", testScript);
        if (luaL_loadfilex(sL, testScript, "t") != LUA_OK)
        {
            portConLogf("ge: lua: error: %s\n", lua_tostring(sL, -1));
            lua_pop(sL, 1);
        }
        else
        {
            portLuaCall(sL, 0, 0);
        }
    }
}

/* Data/Scripts/boot/*.lua, once per run, the first time the Mods page or a stage needs them: they
 * exist to call menu.add_*, so the items are there before any stage. on() handlers they register are
 * dropped at the next stage start like any other. */
void portLuaMenuBoot(void)
{
    static char names[LUA_MAX_SCRIPTS][64];
    char dir[400];
    DIR *d;
    struct dirent *e;
    int i, n = 0;

    if (sMenuBooted)
    {
        return;
    }
    if (!portLuaGameAllowed())
    {
        return;
    }
    sMenuBooted = 1;
    snprintf(dir, sizeof(dir), "%s/boot", portLuaGameScriptDir());
    d = opendir(dir);
    if (d == NULL)
    {
        return;
    }
    while ((e = readdir(d)) != NULL && n < LUA_MAX_SCRIPTS)
    {
        size_t l = strlen(e->d_name);

        if (l > 4 && l < sizeof(names[0]) && strcmp(e->d_name + l - 4, ".lua") == 0)
        {
            memcpy(names[n++], e->d_name, l + 1);
        }
    }
    closedir(d);
    if (n == 0 || portLuaState() == NULL)
    {
        return;
    }
    qsort(names, n, sizeof(names[0]), portLuaCmpName);
    sMenuBooting = 1;
    for (i = 0; i < n; i++)
    {
        char path[512];

        snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
        portConLogf("ge: lua: running boot/%s\n", names[i]);
        if (luaL_loadfilex(sL, path, "t") != LUA_OK)
        {
            portConLogf("ge: lua: error: %s\n", lua_tostring(sL, -1));
            lua_pop(sL, 1);
            continue;
        }
        portLuaCall(sL, 0, 0);
    }
    sMenuBooting = 0;
    portLuaMenuRefresh();
}

void portLuaStageStart(void)
{
    portLuaMenuBoot();
    portLuaMenuStageClear();   /* items from the last stage's scripts go; boot items stay */
    portLuaListenersClear();   /* nothing from the last stage's scripts may dangle */
    portLuaRandReseed();       /* math.random: the session seed in co-op (same draws on both machines), the clock in solo */
    if (!portLuaGameAllowed())
    {
        portLuaGameWeaponReset();   /* a demo must see stock weapons */
        return;
    }
    portLuaRunScripts();
}

void portLuaReload(void)
{
    if (!portLuaGameAllowed())
    {
        portConLogf("ge: lua: off during demos and multiplayer\n");
        return;
    }
    portLuaGameWeaponReset();
    portLuaMenuStageClear();
    portLuaRunScripts();
}

void portLuaConsole(const char *arg1, const char *arg2)
{
    char code[600];
    lua_State *L;
    int top, n, i;

    if (arg1 == NULL)
    {
        portConLogf("ge: lua: usage: lua <code>\n");
        return;
    }
    if (!portLuaGameAllowed())
    {
        portConLogf("ge: lua: off during demos and multiplayer\n");
        return;
    }
    L = portLuaState();
    if (L == NULL)
    {
        return;
    }
    top = lua_gettop(L);
    snprintf(code, sizeof(code), "return %s%s%s", arg1, arg2 != NULL ? " " : "", arg2 != NULL ? arg2 : "");
    if (luaL_loadbufferx(L, code, strlen(code), "=lua", "t") != LUA_OK)
    {   /* not an expression: run it as a statement chunk */
        lua_pop(L, 1);
        snprintf(code, sizeof(code), "%s%s%s", arg1, arg2 != NULL ? " " : "", arg2 != NULL ? arg2 : "");
        if (luaL_loadbufferx(L, code, strlen(code), "=lua", "t") != LUA_OK)
        {
            portConLogf("ge: lua: error: %s\n", lua_tostring(L, -1));
            lua_settop(L, top);
            return;
        }
    }
    if (portLuaCall(L, 0, LUA_MULTRET) == 0)
    {
        n = lua_gettop(L) - top;
        for (i = 1; i <= n; i++)
        {
            portConLogf("ge: lua: => %s\n", luaL_tolstring(L, top + i, NULL));
            lua_pop(L, 1);
        }
    }
    lua_settop(L, top);
}
