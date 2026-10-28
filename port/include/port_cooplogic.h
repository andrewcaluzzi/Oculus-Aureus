/* port_cooplogic.h - pure co-op rules (no game types): which Bond a guard targets, "any" / "all dead".
 * Inputs are indexed by player; the answer never depends on the local player or on pass order. */
#ifndef PORT_COOPLOGIC_H
#define PORT_COOPLOGIC_H

#ifdef __cplusplus
extern "C" {
#endif

/* Index of the living player nearest (squared 3D distance) to (tx,ty,tz); ties go to the lower index.
 * Dead players (dead[i] != 0) and players with has[i] == 0 are skipped. If nobody qualifies it returns
 * the lowest index with has[i] != 0, else 0. */
int portCoopPickNearest(int n, const float *x, const float *y, const float *z, const int *dead, const int *has,
                        float tx, float ty, float tz);
/* 1 when every present player (has[i] != 0) is dead, else 0; 0 when nobody is present */
int portCoopAllDeadOf(int n, const int *dead, const int *has);

/* Escort NPCs (Natalya...) follow the nearest LIVING Bond, with hysteresis so they do not flip-flop between two Bonds who are about
 * equally far. dist[i] = the escort's distance to Bond i; cur = the Bond followed so far (-1 none); sinceFields = fields since the
 * last switch. The followed Bond changes only when it is dead / gone (at once, to the nearest living one), or when another living
 * Bond is closer than dist[cur] * ratio (0.7 = 30% closer) AND sinceFields >= minFields (e.g. 180 = 3 s). */
#define PORT_COOP_ESCORT_RATIO 0.70f
#define PORT_COOP_ESCORT_MIN_FIELDS 180
int portCoopEscortPick(int n, const float *dist, const int *dead, const int *has, int cur, int sinceFields, float ratio, int minFields);

/* Job 4: a dead Bond respawns after 10 s of game time, counted in 60 Hz fields (g_ClockTimer), never wall time. */
#define PORT_COOP_FIELDS_PER_SEC 60
#define PORT_COOP_RESPAWN_FIELDS (10 * PORT_COOP_FIELDS_PER_SEC)
#define PORT_COOP_RESPAWN_SPOTS 6

/* The partner a dead Bond respawns next to: the lowest-index living present player other than `self`, or -1. */
int portCoopRespawnPartner(int n, const int *dead, const int *has, int self);
/* Fields left of `total` when death was noted at field `deadSince` and it is now field `now` (never negative). */
int portCoopRespawnFieldsLeft(int deadSince, int now, int total);
/* Whole seconds to show ("Respawning in N"): fields rounded up, 0 when none are left. */
int portCoopRespawnSecs(int fieldsLeft);
/* Respawn now: the timer is out and not everybody is dead (all dead is mission failed). */
int portCoopRespawnDue(int fieldsLeft, int allDead);
/* Candidate spot k (0..5) near a partner at (px,pz) facing (fx,fz): beside it (+right, -right), then behind-right,
 * behind-left, behind, ahead. Fills ox,oz; returns 0 when k is out of range. The caller tests the tile, in order. */
int portCoopRespawnSpot(int k, float px, float pz, float fx, float fz, float *ox, float *oz);

/* Netplay job 3d: death drops. */
#define PORT_COOP_DROP_RING 8
/* Offset of the k-th item dropped at one death spot (k = 0 is the spot itself, then a ring of 6 at 45 units and
 * a ring of 6 at 90, in a fixed order): the same on both machines. Returns 0 when k is out of range (k >= 13). */
int portCoopDropOffset(int k, float *ox, float *oz);
/* Is the death spot a place an item can lie and be reached? Safe only with a stand tile under the spot (hasTile), the
 * spot inside that tile (inTile), the Bond's feet within 150 units of the tile's floor (not in mid-air above a drop),
 * the floor not below the level's lowest room (worldFloor; ignored when worldFloorValid is 0) and the spot not
 * forced unsafe (a test hook). */
int portCoopDropSpotSafe(int hasTile, int inTile, float footY, float floorY, int worldFloorValid, float worldFloor, int forceUnsafe);
/* Rounds a dropped gun carries: the magazine in the Bond's hand when this is the gun in that hand, else a full
 * magazine, never more than the ammo the Bond holds of that type (reserve + the loaded one). `pool` is the ammo of that
 * type still unspent by earlier drops; the result is what to take from it. */
int portCoopDropMagazine(int inHand, int handMag, int magSize, int pool);

/* The arena partner bot's line of fire. 1 when a body at (px,py,pz) is in the way of a shot from the gun at (sx,sy,sz) to a target at
 * (tx,ty,tz): ahead of the gun and nearer than the target along the line, within radius + spread * (distance from the gun) of the line
 * horizontally (spread widens it for a spraying gun), and within `band` of the line's height there. */
int portCoopShotBlocked(float sx, float sy, float sz, float tx, float ty, float tz, float px, float py, float pz, float radius, float spread,
                        float band);
/* The partner bot's trigger: held only when it wants to fire, nobody is in the line of fire, and the gun has rounds in its magazine
 * (usesAmmo = 0 for melee). Holding the trigger on an empty magazine dry-fires again and again and keeps the engine's reload from
 * starting; with the trigger released the reload starts at once, or, with no reserve left, the empty-gun weapon switch. */
int portCoopBotTrigger(int want, int usesAmmo, int mag, int blocked);

#ifdef __cplusplus
}
#endif

#endif
