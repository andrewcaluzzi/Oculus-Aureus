/* port_flatland.c - Flatland, the generated flat test level.
 *
 * The level itself is compiled-in data (port/flatland, tools/gen_flatland.py) that takes over the
 * cut MP stage slot LEVELID_SHO. This file holds the one runtime question the hooks ask: is the
 * stage being loaded Flatland. It also reserves spare chr and anim slots, because the level has
 * no guards of its own and the modes, the bot and `spawn` need room to create their own. */
#ifndef TARGET_N64

#include <ultra64.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include <stdlib.h>

extern s32 bossGetStageNum(void);

#define FLATLAND_EXTRA_SLOTS 96   /* 16 hostages + 40 guards + the bot / two Bonds' bodies, with headroom (was 48: 8 + 24 + bot) */

s32 portFlatlandStage(void)
{
    return bossGetStageNum() == LEVELID_SHO;
}

/* The Colosseum arena (port/colosseum, modding/arena.md) takes the cut stage LEVELID_EAR. It has
 * no guards either; waves plus a crowd need many more slots than Flatland. */
#define COLOSSEUM_EXTRA_SLOTS 240   /* waves (max_alive ~14 + corpses) + a crowd of up to ~150 */

s32 portColosseumStage(void)
{
    return bossGetStageNum() == LEVELID_EAR;
}

s32 portFlatlandExtraSlots(void)
{
    if (portColosseumStage())
    {
        /* GE_COLOSSEUM_SLOTS raises it (benchmark: ~1400) */
        const char *e = getenv("GE_COLOSSEUM_SLOTS");
        s32 n = e ? atoi(e) : 0;
        return (n >= COLOSSEUM_EXTRA_SLOTS && n <= 4000) ? n : COLOSSEUM_EXTRA_SLOTS;
    }
    return portFlatlandStage() ? FLATLAND_EXTRA_SLOTS : 0;
}

/* slots beyond the default (0 unless the Colosseum runs with GE_COLOSSEUM_SLOTS): dyn.c grows the per-frame display list and vertex pools by it */
s32 portColosseumBigSlots(void)
{
    return portColosseumStage() ? portFlatlandExtraSlots() - COLOSSEUM_EXTRA_SLOTS : 0;
}

#endif
