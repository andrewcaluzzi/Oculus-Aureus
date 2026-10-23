/* port_chrthrow.h - a chr throws a real knife projectile (no chr in vanilla can).
 * Modelled on the chr rocket path (chrlvFireWeaponRelated) and the player knife
 * (generate_player_thrown_knife). */
#ifndef PORT_CHRTHROW_H
#define PORT_CHRTHROW_H

#ifndef TARGET_N64

#include <ultra64.h>
#include <bondtypes.h>

/* Faces the target, plays the grenade-throw anim and spawns a PROP_CHRKNIFE projectile
 * from the thrower's right hand at the target's chest. The target is a chr's prop or
 * Bond's viewer prop (solo Bond has no chr in first person). TRUE if a projectile spawned. */
s32 portChrThrowKnife(ChrRecord *thrower, PropRecord *target);

/* propobj.c knife-hit branch, for a Bond-team chr's knife: the hit is the thrower's, not
 * Bond's (no hit stats, no g_AiHealthModifier), it can hit Bond, and a chr victim notes the
 * attack for retaliation. TRUE if the hit landed (the knife then bounces off). */
s32 portChrKnifeHit(PropRecord *victim, s32 hitpart, coord3d *dir, s32 weaponnum, PropRecord *ownerprop);

/* Scale (0..1, default 0.5) on the damage a bot knife does to Bond. */
void portChrThrowSetFriendlyFire(f32 scale);
f32  portChrThrowFriendlyFire(void);

#endif /* TARGET_N64 */
#endif /* PORT_CHRTHROW_H */
