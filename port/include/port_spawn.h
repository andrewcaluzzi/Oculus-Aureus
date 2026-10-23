/* port_spawn.h - runtime chr spawning for port features (co-op bot, later modes / Lua).
 * Wraps chrSpawnAtCoord with a slot gate and applies the descriptor after a successful
 * spawn. Call from the sim update only (chrlvAllChrTick), never from the render path.
 */
#ifndef PORT_SPAWN_H
#define PORT_SPAWN_H

#ifndef TARGET_N64

#include <ultra64.h>
#include <bondtypes.h>

typedef struct PortSpawnDesc {
    s32 body, head;            /* head < 0 = random, as chrSpawnAtCoord */
    PropRecord *nearprop;      /* spawn at this prop's pos/stan (Bond); chrAdjustPosForSpawn steps off */
    f32 angle;                 /* radians, chr convention: facing (sin a, cos a) */
    s32 ailistid;              /* global list id, resolved with ailistFindById */
    f32 maxdamage;             /* unscaled, as chrSetMaxDamage (vanilla default 4); 0 = keep default */
    u32 chrflags_on;
    s32 chrnum;                /* < 0 = keep the auto-assigned 5000+ */
    s32 nofront;               /* angle points away from the anchor's facing: refuse the front half-plane */
} PortSpawnDesc;

/* Spawns stay this many slots clear of full: vanilla clones/spawns need >= 3 free,
 * the port keeps one more so the level's own scripts never lose theirs. */
#define PORT_SPAWN_MIN_FREE 4

ChrRecord *portSpawnChr(const PortSpawnDesc *d);        /* NULL + one log line on failure */
ChrRecord *portSpawnChrAt(const PortSpawnDesc *d, const coord3d *pos, StandTile *stan);   /* explicit spot; nearprop still gates */
/* Spawn on pad `pad` facing the pad's look; d->nearprop is just the anchor gate (Bond's prop), d->angle is
 * ignored. Armed with (weaponprop, weaponitem) such as PROP_CHRKALASH/ITEM_AK47; weaponprop < 0 = unarmed. */
ChrRecord *portSpawnChrAtPad(s32 pad, const PortSpawnDesc *d, s32 weaponprop, s32 weaponitem);
s32  portSpawnRingPoint(PropRecord *anchor, f32 angle, f32 dist, coord3d *out, StandTile **outstan);   /* TRUE = clear */
s32  portSpawnTeleportNear(ChrRecord *chr, PropRecord *nearprop); /* TRUE on success */
s32  portSpawnTeleportNearAt(ChrRecord *chr, PropRecord *nearprop, f32 stepangle); /* step off along stepangle */
void portSpawnReset(void);                             /* lvlStageLoad: forget per-stage state */

#endif /* TARGET_N64 */
#endif /* PORT_SPAWN_H */
