#include <ultra64.h>
#include <memp.h>
#include "model.h"
#include "initunk_005520.h"
#include "objecthandler.h"
#include "memp.h"

#define MODEL_SPARE_SLOTS          30
#define ANIM_MODEL_SPARE_SLOTS     10

#define MODEL_SPARE_RWDATALEN      0x14
#define ANIM_MODEL_SPARE_RWDATALEN 0x8c


void modelmgrResetSlotCounts(void)
{
    g_MaxAnimModelSlots = 0;
    g_MaxModelSlots = 0;
}


void modelmgrSetLevelResetting(bool resetting) 
{
    g_ModelIsLvResetting = resetting;
}

//this may be a file split

/**
 * NTSC address 0x7F005540.
*/
void modelmgrAllocateModelSlots(s32 numobjs)
{
    s32 i;

    g_MaxModelSlots = numobjs + MODEL_SPARE_SLOTS;
#ifndef TARGET_N64
    {   /* PORT: heist / GE_MODE_SLOTS: spare object model slots for the guards' guns, keys and loot a mode spawns (0 otherwise) */
        extern s32 portHeistExtraObjSlots(void);
        g_MaxModelSlots += portHeistExtraObjSlots();
    }
#endif

    g_ModelSlots = mempAllocBytesInBank(g_MaxModelSlots * sizeof(struct ModelSlot), MEMPOOL_STAGE);

    for (i = 0; i < g_MaxModelSlots; i++)
    {
        g_ModelSlots[i].unk08 = 0;

        if (i < numobjs)
        {
            g_ModelSlots[i].unk10 = NULL;
        }
        else
        {
            g_ModelSlots[i].unk10 = mempAllocBytesInBank(MODEL_SPARE_RWDATALEN * sizeof(u32), MEMPOOL_STAGE);
            g_ModelSlots[i].unk02 = MODEL_SPARE_RWDATALEN;
        }
    }
}


/**
 * NTSC address 0x7F005540.
*/
void modelmgrAllocateAnimModelSlots(s32 numanimated)
{
    s32 temp_t6;
    s32 i;

    g_MaxAnimModelSlots = numanimated + ANIM_MODEL_SPARE_SLOTS;
#ifndef TARGET_N64
    {   /* PORT: arena: spare anim slots for spawned guards (0 when GE_ARENA is off). */
        extern s32 portArenaExtraSlots(void);
        extern s32 portFlatlandExtraSlots(void);
        g_MaxAnimModelSlots += portArenaExtraSlots();
        extern s32 portHeistExtraSlots(void);
        g_MaxAnimModelSlots += portFlatlandExtraSlots(); /* PORT: Flatland has no guards of its own */
        g_MaxAnimModelSlots += portHeistExtraSlots();    /* PORT: heist / GE_MODE_SLOTS: a crowd of guards on a stock stage */
    }
#endif

    // mips2c says: g_AnimModelSlots = mempAllocBytesInBank(temp_t6 * 0xC0, 4);
    // however, the pointer is incremented by 0xbc in the loop below.
    g_AnimModelSlots = mempAllocBytesInBank(g_MaxAnimModelSlots * (4 + sizeof(struct AnimModelSlot)), MEMPOOL_STAGE);

    for (i = 0; i < g_MaxAnimModelSlots; i++)
    {
        g_AnimModelSlots[i].unk08 = 0;

        if (i < numanimated)
        {
            g_AnimModelSlots[i].unk10 = NULL;
        }
        else
        {
            g_AnimModelSlots[i].unk10 = mempAllocBytesInBank(ANIM_MODEL_SPARE_RWDATALEN * sizeof(u32), MEMPOOL_STAGE);
            g_AnimModelSlots[i].unk02 = ANIM_MODEL_SPARE_RWDATALEN;
        }
    }
}
