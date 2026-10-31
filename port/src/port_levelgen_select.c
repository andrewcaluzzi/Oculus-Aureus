/**
 * port_levelgen_select.c - which generated level fills the levelgen slot (LEVELID_LEE), chosen at each stage load instead of at build
 * time (the agreed branch model: modes switch at runtime). midas-notes features/heist.md "Runtime job switch".
 *
 * The build compiles every level of GE_LEVELGEN_LEVELS (CMakeLists.txt: name=dir entries; the bank, the casino, the sniper ridge, the
 * snowfield and the jungle by default). The first is the default and keeps the slot's own symbols (bg_lee_all_p_seg,
 * Tbg_levelgen_all_p_stanZ, UsetuplevelgenZ); every other one is compiled under its own (bg_lee_<name>_all_p_seg,
 * Tbg_levelgen_<name>_all_p_stanZ, Usetuplevelgen<name>Z; objcopy renames them). The configure step writes the table below
 * (levelgen_levels.h in the build dir). The level is picked from GE_LEVELGEN=<name> when the stage loads (bg.c, prop.c ask here): a
 * listed name picks that level, anything else the default. The menus' hand-off sets GE_LEVELGEN for a run (port_uimenu.c), a launcher
 * sets it for the first stage. The Lua layout that goes with it is Data/Scripts/modes/levelgen_layout_<name>.lua (mode scripts include
 * the one levelgen.current() names; the default's is levelgen_layout.lua too).
 *
 * Sim class: SIM in spirit (it picks the stage's data), but it keeps no state of its own: the choice is a pure function of the
 * environment at the load, and the pointers are the linked data. Each level's compiled data is its own, so a bank load after a casino
 * load starts from the bank's own data again (the same as reloading one stage, which the arena's Retry does all the time).
 */
#ifndef TARGET_N64

#include <stdlib.h>
#include <string.h>
#include <ultra64.h>
#include <bondtypes.h>
#include "game/ob.h"

#include "port_uimenu.h"
#include "levelgen_levels.h"   /* (generated: the externs and LEVELGEN_LEVELS_INIT) */

extern void portConLogf(const char *fmt, ...);
extern fileentry file_resource_table[];   /* ob.c */

typedef struct
{
    const char *name;   /* the GE_LEVELGEN value, the layout's suffix */
    u8 *bg;             /* its bg segment (the slot's file table entry is pointed here) */
    u8 *stan;
    stagesetup *setup;
} LevelgenLevel;

static const LevelgenLevel kLevels[] = LEVELGEN_LEVELS_INIT;
#define LEVELGEN_COUNT ((int) (sizeof(kLevels) / sizeof(kLevels[0])))

static const LevelgenLevel *chosen(void)   /* the level GE_LEVELGEN names, else the first (the default) */
{
    const char *e = getenv("GE_LEVELGEN");
    int i;

    if (e != NULL)
    {
        for (i = 0; i < LEVELGEN_COUNT; i++) if (strcmp(e, kLevels[i].name) == 0) return &kLevels[i];
    }
    return &kLevels[0];
}

const char *portLevelgenDefaultName(void) { return kLevels[0].name; }

const char *portLevelgenVariant(void) { return chosen()->name; }

int portLevelgenHas(const char *name)
{
    int i;

    if (name == NULL) return 0;
    for (i = 0; i < LEVELGEN_COUNT; i++) if (strcmp(name, kLevels[i].name) == 0) return 1;
    return 0;
}

/* levelgen.levels(): how many, and the i-th name (0-based); NULL past the end */
int portLevelgenCount(void) { return LEVELGEN_COUNT; }
const char *portLevelgenName(int i) { return (i >= 0 && i < LEVELGEN_COUNT) ? kLevels[i].name : NULL; }

/* bgfog.c, the levelgen slot's environment: the sniper snowfield (tools/levelgen/build_snowfield.py) takes Surface's sky and fog;
 * every other generated level keeps the slot's dark, fogless interior one */
s32 portLevelgenSkyIsSurface(void)
{
    return strcmp(portLevelgenVariant(), "snowfield") == 0;
}

/* the same for the sniper jungle (tools/levelgen/build_jungle.py): Jungle's sky and fog */
s32 portLevelgenSkyIsJungle(void)
{
    return strcmp(portLevelgenVariant(), "jungle") == 0;
}

/* bondview2.c, matrixmath.c, propobj.c: the world's matrices go to the renderer as floats too (gfx_pc.cpp) and the 320 m prop draw cut
 * is lifted. Set by bgfog.c at each stage's environment load: on while the levelgen slot holds a long-range sniper stage (the snowfield,
 * the jungle: bgfog.c's sky_level), whose coordinates pass the fixed-point Mtx range at a narrow zoom or past ~330 m */
static s32 sFloatCamera;

void portLevelgenFloatCameraSet(s32 on) { sFloatCamera = on; }
s32 portLevelgenFloatCamera(void) { return sFloatCamera; }

/* bg.c load_bg_file, the levelgen slot: the stan to use */
u8 *portLevelgenStan(void) { return chosen()->stan; }

/* prop.c, the levelgen slot: the setup to use */
stagesetup *portLevelgenSetup(void)
{
    portConLogf("ge: levelgen: the levelgen slot holds '%s' this stage (%d levels in this build)\n", portLevelgenVariant(), LEVELGEN_COUNT);
    return chosen()->setup;
}

/* bg.c, before it fetches the levelgen slot's bg through the file table: point the slot's entry at the chosen level's bg */
void portLevelgenSelectBg(s32 bgidx)
{
    if (bgidx >= 0) file_resource_table[bgidx].hw_address = chosen()->bg;
}

#endif /* !TARGET_N64 */
