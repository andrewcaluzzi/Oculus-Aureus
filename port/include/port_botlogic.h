/* port_botlogic.h - pure co-op bot logic: factions, combatant rule, engagement and attack tables.
 * No game headers or globals, so it is unit-testable outside the game. */
#ifndef PORT_BOTLOGIC_H
#define PORT_BOTLOGIC_H

#include <stddef.h>

enum { PORT_FACTION_VANILLA = 0, PORT_FACTION_BOND = 1, PORT_FACTION_HOSTILE = 2, PORT_FACTION_NEUTRAL = 3,
       PORT_FACTION_HOSTAGE = 4 /* never hostile to anyone; any shot that physically hits one damages it */ };

int portFactionResolveVanilla(int isCombatant);          /* -> HOSTILE or NEUTRAL */
int portFactionRelHostile(int fa, int fb);               /* resolved factions; 1 = hostile */
int portBotIsCombatant(int holdsWeapon, int civilianFlag, int isAllyBody);

#define PORT_ENGAGE_MAX 64
typedef struct { int guard, target; float lastSeen; } PortEngageEntry;
typedef struct { PortEngageEntry e[PORT_ENGAGE_MAX]; int count; } PortEngageTable;
void portEngageReset(PortEngageTable *t);
void portEngageSet(PortEngageTable *t, int guard, int target, float now);
void portEngageSeen(PortEngageTable *t, int guard, float now);
int  portEngageTarget(PortEngageTable *t, int guard, float now, float timeout); /* -1 = none */
void portEngageDropTarget(PortEngageTable *t, int target);
void portEngageDropGuard(PortEngageTable *t, int guard);

#define PORT_ATTACK_MAX 64
typedef struct { int victim, attacker; float when; } PortAttackEntry;
typedef struct { PortAttackEntry e[PORT_ATTACK_MAX]; int count; } PortAttackTable;
void portAttackReset(PortAttackTable *t);
void portAttackNote(PortAttackTable *t, int victim, int attacker, float now);
int  portAttackLast(const PortAttackTable *t, int victim, float now, float window); /* -1 = none */

typedef enum {
    PORT_BOTACT_NONE, PORT_BOTACT_IDLE, PORT_BOTACT_RUN_TO_BOND, PORT_BOTACT_TAKE_FLANK,
    PORT_BOTACT_FACE_TARGET, PORT_BOTACT_THROW, PORT_BOTACT_TELEPORT, PORT_BOTACT_RESPAWN,
    PORT_BOTACT_LOITER   /* Bond has been still: stroll around him (the brain runs the walk/pause cycle) */
} PortBotAct;
/* followDist (coop_bot_follow_dist): personal space. Inside it she makes no move; beyond followDist +
 * followSlack she follows, and keeps following until she is back inside followDist (no stutter at the edge).
 * loiter: when Bond has moved less than loiterMove for loiterAfter seconds and she is not fighting, she
 * strolls between followDist and 2 x followDist from him, pausing loiterPauseMin..Max seconds. */
typedef struct { float leash, knifeRange, knifeCooldown, catchupDist, catchupAfter, respawnAfter,
                 followDist, followSlack, loiterAfter, loiterMove, loiterPauseMin, loiterPauseMax;
                 int loiter; } PortBotTuning;
typedef struct { int alive; float dt, distToBond; int bondSeesBot, navFailed;
                 int hasTarget; float targetDist; int targetVisible;
                 float bondX, bondZ; /* Bond's position, for the stillness test */ } PortBotInputs;
typedef struct { float sinceThrow, farFor, deadFor, stillFor, refX, refZ; int following, haveRef; } PortBotMemory;
void       portBotTuningDefaults(PortBotTuning *t);
void       portBotTuningSetFollowDist(PortBotTuning *t, float followDist);   /* also moves the sprint distance (+150) */
void       portBotMemoryReset(PortBotMemory *m);
PortBotAct portBotDecide(const PortBotInputs *in, const PortBotTuning *t, PortBotMemory *m);

/* Private PRNG (xorshift32) for the bot's own choices (loiter points and pauses, random skin). Never the
 * game's randomGetNext, so the bot never shifts the game's sequence and a run stays reproducible. */
void         portBotRngSeed(unsigned int *s, unsigned int stage);
unsigned int portBotRngNext(unsigned int *s);
float        portBotRngFloat(unsigned int *s);                   /* [0, 1) */
/* A loiter point as a bearing (fraction of a turn, [0,1)) and a distance in [followDist, 2 x followDist] from Bond */
void         portBotLoiterPick(unsigned int *s, float followDist, float *turn, float *dist);
float        portBotLoiterPause(unsigned int *s, const PortBotTuning *t);   /* seconds, in [loiterPauseMin, loiterPauseMax] */

/* Skins: the multiplayer character roster (front.c mp_chr_setup) under friendly lowercase names. A skin is
 * an index k into the usable list; portBotSkinMp(k) is its mp_chr_setup index (body + head). */
#define PORT_BOT_SKIN_COUNT 70
int         portBotSkinCount(void);
const char *portBotSkinName(int k);                /* NULL out of range */
int         portBotSkinMp(int k);                  /* mp_chr_setup index, -1 out of range */
int         portBotSkinFind(const char *name);     /* k, or -1; case-insensitive */
int         portBotSkinFindMp(int mp);             /* k for an mp_chr_setup index, or -1 (excluded) */

#endif
