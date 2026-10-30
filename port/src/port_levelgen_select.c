/**
 * port_levelgen_select.c - which generated level fills the levelgen slot (LEVELID_LEE), chosen at each stage load instead of at build
 * time (the agreed branch model: modes switch at runtime). midas-notes features/heist.md "Runtime job switch".
 *
 * The build always compiles the default level (GE_LEVELGEN_DIR, the bank: symbols bg_lee_all_p_seg, Tbg_levelgen_all_p_stanZ,
 * UsetuplevelgenZ) and, when GE_LEVELGEN_EXTRA is set (default port/levelgen_casino), a second one under its own symbols
 * (bg_lee_<name>_all_p_seg, Tbg_levelgen_<name>_all_p_stanZ, Usetuplevelgen<name>Z; CMakeLists.txt renames them with objcopy). The
 * level is picked from GE_LEVELGEN=<name> when the stage loads (bg.c, prop.c ask here): the extra's name picks it, anything else the
 * default. The menus' hand-off sets GE_LEVELGEN for a run (port_uimenu.c), a launcher sets it for the first stage. The Lua layout that
 * goes with it is Data/Scripts/modes/levelgen_layout_<name>.lua (mode scripts include the one levelgen.current() names).
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

extern void portConLogf(const char *fmt, ...);
extern fileentry file_resource_table[];   /* ob.c */

#ifndef GE_LEVELGEN_DEFAULT_NAME
#define GE_LEVELGEN_DEFAULT_NAME "bank"
#endif

extern u8 Tbg_levelgen_all_p_stanZ[];
extern stagesetup UsetuplevelgenZ;
extern u8 bg_lee_all_p_seg[];

#ifdef GE_LEVELGEN_EXTRA_NAME
/* the extra level's symbols: CMakeLists.txt port_add_asset_*_as renames them (GE_LEVELGEN_EXTRA_SYM = its name, e.g. casino) */
#define LG_CAT2(a, b, c) a##b##c
#define LG_CAT(a, b, c) LG_CAT2(a, b, c)
extern u8 LG_CAT(Tbg_levelgen_, GE_LEVELGEN_EXTRA_SYM, _all_p_stanZ)[];
extern stagesetup LG_CAT(Usetuplevelgen, GE_LEVELGEN_EXTRA_SYM, Z);
extern u8 LG_CAT(bg_lee_, GE_LEVELGEN_EXTRA_SYM, _all_p_seg)[];
#endif

static int useExtra(void)
{
#ifdef GE_LEVELGEN_EXTRA_NAME
    const char *e = getenv("GE_LEVELGEN");

    return e != NULL && strcmp(e, GE_LEVELGEN_EXTRA_NAME) == 0;
#else
    return 0;
#endif
}

const char *portLevelgenDefaultName(void) { return GE_LEVELGEN_DEFAULT_NAME; }

const char *portLevelgenVariant(void)
{
#ifdef GE_LEVELGEN_EXTRA_NAME
    if (useExtra()) return GE_LEVELGEN_EXTRA_NAME;
#endif
    return GE_LEVELGEN_DEFAULT_NAME;
}

int portLevelgenHas(const char *name)
{
    if (name == NULL) return 0;
    if (strcmp(name, GE_LEVELGEN_DEFAULT_NAME) == 0) return 1;
#ifdef GE_LEVELGEN_EXTRA_NAME
    if (strcmp(name, GE_LEVELGEN_EXTRA_NAME) == 0) return 1;
#endif
    return 0;
}

/* bgfog.c, the levelgen slot's environment: the sniper snowfield (tools/levelgen/build_snowfield.py) takes Surface's sky and fog;
 * every other generated level keeps the slot's dark, fogless interior one */
s32 portLevelgenSkyIsSurface(void)
{
    return strcmp(portLevelgenVariant(), "snowfield") == 0;
}

/* bondview2.c: the world's camera matrix goes to the renderer as floats too (gfx_pc.cpp), set by bgfog.c at each stage's environment load:
 * on only while the levelgen slot holds the snowfield, whose coordinates pass the fixed-point Mtx range at a narrow zoom */
static s32 sFloatCamera;

void portLevelgenFloatCameraSet(s32 on) { sFloatCamera = on; }
s32 portLevelgenFloatCamera(void) { return sFloatCamera; }

/* bg.c load_bg_file, the levelgen slot: the stan to use */
u8 *portLevelgenStan(void)
{
#ifdef GE_LEVELGEN_EXTRA_NAME
    if (useExtra()) return LG_CAT(Tbg_levelgen_, GE_LEVELGEN_EXTRA_SYM, _all_p_stanZ);
#endif
    return Tbg_levelgen_all_p_stanZ;
}

/* prop.c, the levelgen slot: the setup to use */
stagesetup *portLevelgenSetup(void)
{
    portConLogf("ge: levelgen: the levelgen slot holds '%s' this stage\n", portLevelgenVariant());
#ifdef GE_LEVELGEN_EXTRA_NAME
    if (useExtra()) return &LG_CAT(Usetuplevelgen, GE_LEVELGEN_EXTRA_SYM, Z);
#endif
    return &UsetuplevelgenZ;
}

/* bg.c, before it fetches the levelgen slot's bg through the file table: point the slot's entry at the chosen level's bg */
void portLevelgenSelectBg(s32 bgidx)
{
    u8 *want = bg_lee_all_p_seg;

#ifdef GE_LEVELGEN_EXTRA_NAME
    if (useExtra()) want = LG_CAT(bg_lee_, GE_LEVELGEN_EXTRA_SYM, _all_p_seg);
#endif
    if (bgidx >= 0) file_resource_table[bgidx].hw_address = want;
}

#endif /* !TARGET_N64 */
