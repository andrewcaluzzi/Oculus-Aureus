/* port_faction.h - co-op bot faction layer: who is on Bond's team, who is hostile.
 * Inert (portFactionActive() == 0) unless a registered non-vanilla chr is alive, so the
 * vanilla game and the attract demos are untouched. */
#ifndef PORT_FACTION_H
#define PORT_FACTION_H

#ifndef TARGET_N64

#include <ultra64.h>
#include <bondtypes.h>
#include "port_botlogic.h"

void       portFactionReset(void);                  /* lvlStageLoad, via portBotReset */
void       portFactionTick(f32 dt);                 /* advances the clock; drops dead engagements */
f32        portFactionNow(void);                    /* seconds since stage load (gated ticks) */
void       portFactionSetChr(ChrRecord *chr, s32 faction);
s32        portFactionOfChr(ChrRecord *chr);        /* resolved: BOND, HOSTILE or NEUTRAL */
s32        portFactionActive(void);                 /* a non-vanilla chr is alive, not demo play/record */
s32        portFactionHostileChrs(ChrRecord *a, ChrRecord *b);
s32        portFactionPropIsBondTeam(PropRecord *p);
ChrRecord *portFactionNearestHostile(ChrRecord *self, f32 maxdist, s32 needLOS);
s32        portFactionLosTo(ChrRecord *self, ChrRecord *cand);   /* the straight stan-ray test the bot targets with */
void       portFactionCountHostile(ChrRecord *self, f32 maxdist, s32 *inrange, s32 *withlos);   /* debug */
PortEngageTable *portFactionEngage(void);           /* guard chrnum -> engaged chr */

/* chraction.c hooks; each returns at once (FALSE) when !portFactionActive(). */
s32  portFactionSightPass(ChrRecord *guard);        /* TRUE = saw a hostile Bond-team chr; engagement set */
void portFactionRedirectAttack(ChrRecord *self, s32 *attacktype, s32 *entityid);
ChrRecord *portFactionMovingAttackTarget(ChrRecord *self);
ChrRecord *portFactionEngagedChr(ChrRecord *self);  /* the live chr this guard is engaged with, else NULL */
void portFactionNoteHit(ChrRecord *victim, PropRecord *attackerprop);
s32  portFactionRetaliate(ChrRecord *self);         /* TRUE = engaged with the last attacker */
s32  portFactionShotDamages(ChrRecord *shooter, ChrRecord *victim);   /* Bond-team victims: hit-chance roll */
void portFactionSetGuardHitChance(f32 chance);      /* 0..1, default 0.35 (an ini key in Task 9) */
f32  portFactionGuardHitChance(void);

#endif /* TARGET_N64 */
#endif /* PORT_FACTION_H */
