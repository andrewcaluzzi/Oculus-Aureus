/* port_arena.h - bot test arena (GE_ARENA=1): invincible Bond, extra chr slots, `spawn guard`,
 * scoreboard and the headless benchmark.
 * Off (GE_ARENA unset) nothing here runs and the game is vanilla. */
#ifndef PORT_ARENA_H
#define PORT_ARENA_H

#ifndef TARGET_N64

#include <ultra64.h>
#include <bondtypes.h>

#define PORT_ARENA_EXTRA_SLOTS 24

s32  portArenaEnabled(void);                      /* GE_ARENA=1, cached */
s32  portArenaExtraSlots(void);                   /* chr and anim spare slots to add at stage load (0 when off) */
void portArenaReset(void);                        /* lvlStageLoad, via portBotReset: counters and guards */
void portArenaTick(f32 dt, PropRecord *bondprop); /* once per gated sim tick: queued spawns, bench */
void portArenaConsole(const char *sub, const char *rest);   /* `spawn guard [n] [dist]` | `spawn clear` */
void portArenaNetSpawn(s32 issuer, s32 n, f32 dist);   /* netplay session command: guards around Bond `issuer` */
void portArenaNetClear(void);
void portArenaStatsPrint(void);                   /* `bot stats` */

/* Scoreboard feeds from the bot; stage-relative time comes from portFactionNow. */
void portArenaNoteThrow(f32 targetDist);
void portArenaNoteHit(s32 killed);
void portArenaNoteDeath(void);
void portArenaTotals(s32 *throws, s32 *hits, s32 *kills, s32 *deaths);   /* telemetry columns */

#endif /* TARGET_N64 */
#endif /* PORT_ARENA_H */
