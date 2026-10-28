/* port_roomfog.c - room display lists re-patched when the fog is turned on or off in the middle of a stage (the arena's LOW VISIBILITY
 * mutator, port_arenafx.c).
 *
 * A room's display list is patched once, when it loads (bg.c bgLoadRoomModelData): with fog on the primary list gets
 * DL_LUT_PRIMARY_ADDFOG (render modes G_RM_PASS -> G_RM_FOG_SHADE_A) and the secondary DL_LUT_SECONDARY_ADDFOG; with fog off they get
 * DL_LUT_PRIMARY / DL_LUT_SECONDARY (shade alpha -> environment alpha in the combiners). A stock stage never changes its fog, so that
 * is enough there; the arena turns fog on with every room already loaded fogless, and AC's playtest (v0.2, wave 11, 2000x1125) showed
 * the result: the level and the stands drawn with no fog at all, hard-cut by the fog's far plane against the grey.
 *
 * portRoomFogSync (bg.c, before a room's list is drawn) brings a loaded room to the current fog state: the other state's table is
 * undone (each replacement back to its original command) and this state's applied. Both steps are idempotent, so a room already in
 * the right state is unchanged. The retained GPU copy (room_cache) is dropped so the next draw records the new commands. Only while
 * a Lua mode is active: stock stages never get here with a mismatch, and are never touched.
 *
 * PRESENTATION (snap_embed.py PRES): the per-room flags only skip work; a wrong flag costs one more idempotent pass. */
#ifndef TARGET_N64

#include <ultra64.h>
#include <PR/gbi.h>
#include <bondconstants.h>
#include "game/bg.h"
#include "game/bgfog.h"
#include "port_mode.h"

extern void portRoomCacheRoomFreed(s32 room);   /* roc_hooks.c: drop the room's retained copy */
extern void portConLogf(const char *fmt, ...);

#define RF_ROOMS 1024
static s8 sForm[RF_ROOMS];   /* the fog state the room's lists were last patched for: 0 off, 1 on (bgLoadRoomModelData sets it) */
static s32 sConverted;

/* each replacement of a LUT back to the command it replaced (the first match only, so a pair is never undone twice) */
static void lutUndo(Gfx *start, Gfx *end, const Gfx *lut)
{
    Gfx *g;
    const Gfx *p;

    for (g = start; g < end; g++)
    {
        for (p = lut; p->words.w0 != 0; p += 2)
        {
            if (g->words.w0 == p[1].words.w0 && g->words.w1 == p[1].words.w1)
            {
                *g = p[0];
                break;
            }
        }
    }
}

static void patch(void *dl, s32 size, enum CCRMLUT undo, enum CCRMLUT apply)
{
    Gfx *start = (Gfx *) dl, *end = (Gfx *) ((u8 *) dl + size);

    if (dl == NULL || size <= 0)
    {
        return;
    }
    lutUndo(start, end, ptrDynamic_CC_RM_LUT[(s32) undo]);
    bgApplyDynamicCCRMLUT(start, end, apply);
}

void portRoomFogLoaded(s32 room, s32 fog)
{
    if (room >= 0 && room < RF_ROOMS)
    {
        sForm[room] = fog ? 1 : 0;
    }
}

void portRoomFogSync(s32 room)
{
    const s8 want = g_FogSkyIsEnabled ? 1 : 0;
    s_room_info *r;

    if (room < 0 || room >= RF_ROOMS || sForm[room] == want || !portModeActive())
    {
        return;
    }
    r = &g_BgRoomInfo[room];
    if (want)
    {   /* fogless -> fog: as bgLoadRoomModelData does with g_FogSkyIsEnabled set (indices: bg.c ptrDynamic_CC_RM_LUT) */
        patch(r->ptr_expanded_mapping_info, r->usize_primary_DL_binary, 6 /* PRIMARY */, 1 /* PRIMARY_ADDFOG */);
        patch(r->ptr_secondary_expanded_mapping_info, r->usize_secondary_DL_binary, 7 /* SECONDARY */, 5 /* SECONDARY_ADDFOG */);
    }
    else
    {
        patch(r->ptr_expanded_mapping_info, r->usize_primary_DL_binary, 1, 6);
        patch(r->ptr_secondary_expanded_mapping_info, r->usize_secondary_DL_binary, 5, 7);
    }
    portRoomCacheRoomFreed(room);
    sForm[room] = want;
    if (++sConverted <= 64)
    {
        portConLogf("ge: roomfog: room %d lists patched for fog %s\n", (int) room, want ? "on" : "off");
    }
}

#endif /* !TARGET_N64 */
