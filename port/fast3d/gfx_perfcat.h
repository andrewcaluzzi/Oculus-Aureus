/* gfx_perfcat.h - render cost by category (the arena's late-wave benchmark, tools/mode_arena_bench.lua "late" preset).
 * The sim thread tags the display list per prop (port/src/port_perfcat.c, a G_NOOP marker: w1 = PERFCAT_MAGIC << 24 | cat);
 * the render thread (gfx_perfcat.inl, included by gfx_pc.cpp) charges every triangle, draw batch and microsecond of gfx_run
 * to the category open at the time. Presentation only: nothing here is sim state, and no marker is written unless asked
 * (bench.begin / GE_PERFCAT=1). Shared by C and C++. */
#ifndef GFX_PERFCAT_H
#define GFX_PERFCAT_H

#define PERFCAT_MAGIC 0x5Au   /* G_NOOP w1 >> 24 (gfx_pc.cpp's other G_NOOP tags: 0x46 0x47 0x53 0x58 0x59) */

enum {
    PERFCAT_LEVEL = 0,    /* rooms, sky, everything the world pass draws that is not a tagged prop */
    PERFCAT_SPECT,        /* real spectators (arena crowd chrs) */
    PERFCAT_CORPSE,       /* dead chrs (kept bodies) */
    PERFCAT_LIVE,         /* every other chr: live enemies, co-op bots, minions */
    PERFCAT_OBJ,          /* objects, loose weapons, doors, pickups */
    PERFCAT_FX,           /* explosions and smoke props */
    PERFCAT_BOND,         /* Bond bodies as props (the other player in co-op) */
    PERFCAT_IMPOSTOR,     /* the impostor crowd (one GL draw per CROWD_DRAW) */
    PERFCAT_XLU,          /* the deferred sorted-translucent replay (shadows, glass, explosions, room XLU) */
    PERFCAT_GUNHUD,       /* after the world: Bond's gun, the HUD, menus, 2D */
    PERFCAT_FRAME,        /* gfx_run outside the display list: frame start, clears, prewarm, present */
    PERFCAT_BATCH,        /* the chr batch replay (gfx_chrbatch.inl): the batched chrs' opaque triangles, one draw per draw state */
    PERFCAT_N
};

#define PERFCAT_NAMES { "level", "spectators", "corpses", "live chrs", "objects", "fx", "bond", "impostors", "sorted xlu", "gun+hud", "frame", "chr batch" }

/* marker w1 bit 8: the opaque triangles of this prop go to the chr batch (gfx_chrbatch.inl) instead of drawing in place */
#define PERFCAT_FLAG_BATCH 0x100u

/* one presented frame, as gfx_run left it */
typedef struct {
    unsigned int draws[PERFCAT_N];   /* draw batches (gfx_flush) whose triangles were of this category */
    unsigned int tris[PERFCAT_N];    /* triangles drawn (immediate) or deferred to the sorted pass */
    float ms[PERFCAT_N];             /* render-thread time inside gfx_run (DL walk, transforms, state, GL calls) */
    float drawms[PERFCAT_N];         /* of which inside the GL draw call itself (gfx_rapi->draw_triangles) */
    unsigned int frame;              /* gfx_run count */
} PerfCatFrame;

#ifdef __cplusplus
extern "C" {
#endif
extern volatile int g_PerfCatOn;                  /* port_perfcat.c: markers written; render side times when set */
void gfx_perfcat_last(PerfCatFrame *out);         /* gfx_perfcat.inl: the last finished frame (render thread) */
#ifdef __cplusplus
}
#endif

#endif
