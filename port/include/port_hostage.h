/* port_hostage.h - hostages and guarding guards for game modes.
 * Hostages are unarmed chrs in PORT_FACTION_HOSTAGE: nobody targets them, but any shot or knife that
 * physically hits one damages it (portFactionShotDamages). Their brain runs from chrlvActionTick in
 * place of ai(). Everything is inert unless portModeActive() (the tick, the use hook and the brain all
 * need a hostage to exist, and hostages are only made through this API). Chr ids are chrnums. */
#ifndef PORT_HOSTAGE_H
#define PORT_HOSTAGE_H

#ifndef TARGET_N64

#include <ultra64.h>
#include <bondtypes.h>

enum { PORT_HOSTAGE_NONE = -1, PORT_HOSTAGE_WAIT = 0, PORT_HOSTAGE_FOLLOW, PORT_HOSTAGE_EXTRACTED, PORT_HOSTAGE_DEAD };

#define PORT_HOSTAGE_MAX 16    /* hostages per stage (was 8; the co-op Flatland test uses 10) */
#define PORT_GUARD_MAX   40    /* guards spawned through portGuardSpawn per stage (was 24; the co-op Flatland test uses 20) */

/* Spawn an unarmed hostage on pad `pad` (state WAIT). Returns its chrnum (7200+n), or -1 (bad pad, no
 * slot, table full, no first-person mission). Needs Bond in play: call from a STAGE_START listener or later. */
s32  portHostageSpawn(s32 pad);
s32  portHostageState(s32 chrnum);                  /* PORT_HOSTAGE_*, NONE for a chr that is no hostage */
s32  portHostageList(s32 *chrnums, s32 *states, s32 max);   /* spawn order; either array may be NULL; returns the count written */
/* WAIT or FOLLOW only, for a live hostage; emits PORT_EV_HOSTAGE_FOLLOW/WAIT (a = chr, b = player) on a change. TRUE = changed.
 * A FOLLOW names the Bond that leads it (`player`; portHostageSetState = player 0). FOLLOW on a hostage that already follows
 * ANOTHER Bond hands it over to `player` (still a change). */
s32  portHostageSetState(s32 chrnum, s32 state);
s32  portHostageSetStateBy(s32 chrnum, s32 state, s32 player);
s32  portHostageLeader(s32 chrnum);                 /* the player index a FOLLOWing hostage trails, else -1 */
s32  portHostageExtract(s32 chrnum);                /* state EXTRACTED and the chr is removed; TRUE = done */

/* The use press of the CURRENT player's Bond (the pass that processes that Bond's own input record, so under netplay it is
 * that Bond's press on both machines, never a local read): toggles the hostage within ~150 units that he faces (+-60 deg),
 * nearest first. WAIT -> follows him; following HIM -> waits; following the OTHER Bond -> follows him from now on.
 * TRUE = a hostage took it, so the caller must swallow the press (doors etc. get nothing). */
s32  portHostageUse(void);

/* Spawn an armed guard on pad `pad`, holding position (GAILIST_SIMPLE_GUARD: stands until it sees or
 * hears Bond, then attacks like a stock guard). patrolPath >= 0 = a patrol path id of the stage setup
 * (applied a tick after spawn); weaponItem: ITEM_AK47, ITEM_SHOTGUN, ITEM_WPPK, ITEM_MP5K, ITEM_M16, ITEM_UZI,
 * ITEM_TT33; anything else falls back to ITEM_AK47. Returns the chrnum (7300+n) or -1. */
s32  portGuardSpawn(s32 pad, s32 patrolPath, s32 weaponItem);

/* lifecycle (port_blocks.c calls these) */
void portHostageStageStart(int stage);              /* forget hostages and guards, read the test env */
void portHostageTick(void);                         /* gated sim tick, only while portModeActive() */
void portHostageDeadSweep(void);                    /* before the first-person gate: marks killed hostages DEAD */
s32  portHostageIsHostage(ChrRecord *chr);
s32  portHostageNotAGuard(ChrRecord *chr);          /* TRUE for a hostage: chr searches with no team check (chraction.c) skip it */
void portHostageChrTick(ChrRecord *self);           /* the brain: chrlvActionTick runs it instead of ai() */

#endif /* TARGET_N64 */
#endif /* PORT_HOSTAGE_H */
