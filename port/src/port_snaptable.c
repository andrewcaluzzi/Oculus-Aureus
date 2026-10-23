/* port_snaptable.c - the sim-statics range table of the rollback snapshots, embedded at link time (rollback R2).
 *
 * This is a FIXED-SIZE const table that is all zero but its header. scripts/port/snap_embed.py (a post-link step of the `ge`
 * target) reads the linker map, classifies every .data / .bss / COMMON object (game + engine + sim-owning port files + Lua = SIM)
 * and patches the ranges into this array in the exe itself. The size never changes, so the exe's layout is identical before and
 * after the patch: no relink. There is no map file at run time. A build without python (or a link that skipped the step) leaves
 * count 0: port_snap.c then reports "no embedded table" and rollback snapshots stay off.
 *
 * Layout (little endian, scripts/port/snap_embed.py HDR / ENT):
 *   header { magic version capEnt capStr count strUsed 0 0 } , capEnt x { addr size nameOff cat } , capStr bytes of names. */
#ifndef TARGET_N64

#include "port_snap.h"

const PortSnapTable g_PortSnapTable = {
    .magic = 0,                       /* PORT_SNAP_MAGIC once embedded */
    .version = 1,
    .capEnt = PORT_SNAP_TABLE_CAP,
    .capStr = PORT_SNAP_STR_CAP,
};

#endif
