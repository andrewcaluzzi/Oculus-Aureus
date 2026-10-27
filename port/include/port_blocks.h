/* port_blocks.h - game-mode building blocks: pads, zones, mode HUD text, the per-tick watcher that
 * emits CHR_DIED / ZONE_* / STAGE_START / BOND_DIED (port_event.h). All of it is inert unless
 * portModeActive(); the tick is called from chrlvAllChrTick. */
#ifndef PORT_BLOCKS_H
#define PORT_BLOCKS_H

#ifndef TARGET_N64

#include <ultra64.h>
#include <bondtypes.h>

/* ---- pads (g_CurrentSetup.pads; the table has no stored count, so it is derived) ---- */
s32  portPadCount(void);                                   /* 0 if the stage has no pads */
s32  portPadPos(s32 pad, coord3d *pos);                    /* TRUE = valid pad (finite, has a stan) */
s32  portPadStan(s32 pad, coord3d *pos, StandTile **stan, f32 *angle); /* walkable spot (getposstan) + facing, TRUE = ok */
f32  portPadDist(s32 a, s32 b);                            /* 3D distance, < 0 if either pad is invalid */
/* Pad ids whose distance from `from` is in [mindist, maxdist], in pad order. wponly = only pads on
 * the AI waypoint graph (known walkable). Returns the count written (<= max). */
s32  portPadList(const coord3d *from, f32 mindist, f32 maxdist, s32 wponly, s32 *out, s32 max);
s32  portBondPos(coord3d *pos);                            /* player 0's Bond (NEVER the local player: modes run in the shared sim) */

/* ---- the Bonds (co-op: 2; solo: 1). Player indices are the same on every machine (0 = the host's Bond). Nothing here reads
 * portLocalPlayer() or the current pass, so a mode script sees identical values on both machines. ---- */
s32  portBondCount(void);                                  /* Bonds in this stage (0 before the players exist) */
s32  portBondInPlay(s32 p);                                /* Bond p exists, is alive, and the stage is in first-person play (no demo, no cutscene) */
s32  portBondDeadOf(s32 p);                                /* TRUE if Bond p exists and is dead (waiting to respawn, or out of lives) */
s32  portBondPosOf(s32 p, coord3d *pos);                   /* floor position of Bond p; FALSE unless portBondInPlay(p) */
s32  portBondNearest(f32 x, f32 y, f32 z, f32 *dist);      /* index of the nearest Bond in play (3D, ties to the lower index), -1 if none; dist may be NULL */
s32  portBondAllDown(void);                                /* every Bond is dead (co-op: the shared mission-lost test, lives/respawn included) */

/* ---- zones: XZ circle of `radius`, |dy| <= PORT_ZONE_HALF_HEIGHT. Bond always counts; other chrs
 * only after portZoneWatchChr. ZONE_ENTER/LEAVE fire on the edge, from the tick. ---- */
#define PORT_ZONE_MAX         32
#define PORT_ZONE_HALF_HEIGHT 250.0f
s32  portZoneAdd(const char *name, f32 x, f32 y, f32 z, f32 radius);   /* zone id, or -1 when full */
void portZoneMove(s32 zone, f32 x, f32 y, f32 z);
void portZoneRemove(s32 zone);                            /* silent: no LEAVE events */
s32  portZoneWatchChr(s32 chrnum);                        /* up to 32 chrs; FALSE when full */
void portZoneUnwatchChr(s32 chrnum);
s32  portZoneContains(s32 zone, s32 chrnum);              /* chrnum PORT_CHR_BONDP(p) for Bond p (PORT_CHR_BOND = player 0); state as of the last tick */
s32  portZoneBondMask(s32 zone);                          /* bit p set = Bond p is inside (state as of the last tick) */
const char *portZoneName(s32 zone);                       /* NULL if unused */

/* ---- mode HUD: up to 3 lines of <= 63 chars, drawn by the console overlay only while portModeActive() ---- */
#define PORT_HUD_LINES 4   /* drop A8: a 4th line (the arena crowd favour) */
/* Each line has an "all players" text and an optional per-player text (player 0..3). A machine draws the per-player text of ITS
 * Bond when there is one, else the all-players text. The HUD is never read by the sim. */
void        portModeHudSet(s32 line, const char *text);   /* all players; NULL or "" clears the line */
void        portModeHudSetFor(s32 player, s32 line, const char *text);   /* player < 0 = all players */
void        portModeHudClearAll(void);                    /* every line, every player */
void        portModeHudClearFor(s32 player);              /* player < 0 = everything (as portModeHudClearAll) */
const char *portModeHudGet(s32 line);                     /* the all-players text, NULL when empty or no mode */
const char *portModeHudGetFor(s32 player, s32 line);      /* the text player draws: its own, else the all-players one */
const char *portModeHudGetOwn(s32 player, s32 line);      /* only the per-player text (NULL when that player has none) */
const char *portModeHudGetLocal(s32 line);                /* RENDER ONLY (port_console.c): the text of this machine's Bond */
/* The HUD panel (hud.panel): up to 8 more lines in a box at the top right (the heist's control-room intel). Same rules: per player or
 * for every player, presentation only, cleared with the HUD. */
#define PORT_HUD_PANEL_LINES 8
void        portModeHudPanelSet(s32 player, s32 line, const char *text);   /* player < 0 = all players; NULL clears the line */
void        portModeHudPanelClear(s32 player);                               /* player < 0 = the all-players panel */
const char *portModeHudPanelGetLocal(s32 line);                              /* RENDER ONLY: this machine's Bond's panel (else the all-players one) */

/* Limits: 32 zones, 32 watched chrs, 3 HUD lines of 63 chars, a 64-entry last-attacker ring
 * (portModeNoteHit overwrites the oldest), 512 chr slots polled for deaths. Deaths and BOND_DIED are
 * polled before the first-person gate, so they fire in the death cam; zones and STAGE_START need the gate. */

/* ---- lifecycle ---- */
void portBlocksStageStart(int stage);                     /* from portModeStageStart: forget zones, HUD, death state */
void portBlocksTick(void);                                /* chrlvAllChrTick: no-op unless portModeActive() */
void portModeNoteHit(s32 victimchrnum, s32 attackerchrnum); /* last attacker for CHR_DIED; PORT_CHR_BOND for Bond */

#endif /* TARGET_N64 */
#endif /* PORT_BLOCKS_H */
