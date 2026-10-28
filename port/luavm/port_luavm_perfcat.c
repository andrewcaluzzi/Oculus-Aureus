/* port_luavm_perfcat.c - Lua side of port_perfcat.c (presentation only; registered into the bench and crowd tables by
 * port_luavm_arena.c).
 *   bench.hide({ "spectators", "corpses", ... })   these categories' props are built but not drawn (nil / {} = draw everything);
 *                    names as the report prints them: spectators, corpses, live, objects, fx, impostors. Returns the mask.
 *   bench.summary()  writes the closing table of every finished phase (frame ms avg / p95, gl, sim, draws, ms / draws by category)
 *   crowd.lod([units]) -> units   real spectators farther than this from the camera draw as impostors of their own skin and
 *                    pose (0 = never; GE_CROWD_LOD=<units> overrides). With no argument: the value in force. */
#include <string.h>
#include "lua.h"
#include "lauxlib.h"
#include "../fast3d/gfx_perfcat.h"

extern void portPerfCatHide(unsigned mask);
extern float portPerfCrowdLod(float dist);

int portLuaBenchHide(lua_State *L)
{
    static const struct { const char *name; int cat; } names[] = {
        { "spectators", PERFCAT_SPECT }, { "corpses", PERFCAT_CORPSE }, { "live", PERFCAT_LIVE }, { "objects", PERFCAT_OBJ },
        { "fx", PERFCAT_FX }, { "bond", PERFCAT_BOND }, { "impostors", PERFCAT_IMPOSTOR },
    };
    unsigned mask = 0;
    int i, k, n;

    if (lua_istable(L, 1))
    {
        n = (int)lua_rawlen(L, 1);
        for (i = 1; i <= n; i++)
        {
            const char *s;

            lua_rawgeti(L, 1, i);
            s = luaL_checkstring(L, -1);
            for (k = 0; k < (int)(sizeof(names) / sizeof(names[0])); k++)
            {
                if (strcmp(s, names[k].name) == 0)
                {
                    mask |= 1u << names[k].cat;
                }
            }
            lua_pop(L, 1);
        }
    }
    portPerfCatHide(mask);
    lua_pushinteger(L, (lua_Integer)mask);
    return 1;
}

/* bench.summary(): the table of every finished phase, into the report */
int portLuaBenchSummary(lua_State *L)
{
    extern void portBenchSummary(void);

    (void)L;
    portBenchSummary();
    return 0;
}

/* bench.shot(): one screenshot of the next presented frame (GE_SHOT's writer, gfx_sdl2.cpp); for the look checks */
extern int ge_shot_force __attribute__((weak));
int portLuaBenchShot(lua_State *L)
{
    (void)L;
    if (&ge_shot_force != NULL)
    {
        ge_shot_force = 1;
    }
    return 0;
}

/* crowd.batch([on]) -> on: spectators', corpses' and live chrs' opaque triangles drawn once per draw state (gfx_chrbatch.inl) */
int portLuaCrowdBatch(lua_State *L)
{
    extern int portPerfBatch(int on);

    lua_pushboolean(L, portPerfBatch(lua_isnoneornil(L, 1) ? -1 : (lua_toboolean(L, 1) ? 1 : 0)));
    return 1;
}

/* crowd.bake([on]) -> on: corpses that lie still are captured once and drawn from that (gfx_corpsebake.inl) */
int portLuaCrowdBake(lua_State *L)
{
    extern int portPerfBake(int on);

    lua_pushboolean(L, portPerfBake(lua_isnoneornil(L, 1) ? -1 : (lua_toboolean(L, 1) ? 1 : 0)));
    return 1;
}

int portLuaCrowdLod(lua_State *L)
{
    lua_pushnumber(L, portPerfCrowdLod(lua_isnoneornil(L, 1) ? -1.0f : (float)luaL_checknumber(L, 1)));
    return 1;
}
