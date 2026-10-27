#ifndef PORT_ARENA_A8_H
#define PORT_ARENA_A8_H
/* Arena drop A8 (modding/arena.md "Drop A8"): crowd favour hooks and markers v2. Plain types only, so the Lua half
 * (port/luavm/port_luavm_arena.c) can include it. Game side: port/src/port_arenacrowd.c (SIM) and port_arenamode.c. */

/* spawn.last_hit(chr): the last Bond shot / blast that hurt the chr. part "head" | "body" | "limb" | "gun" | "hat" | "blast";
 * weapon a spawn.weapons() name ("fist", "knife", "other"; NULL for a blast); player -1 = nobody; age in seconds; blast serial (0 = a shot) */
int   portLuaGameLastHit(int chr, const char **part, const char **weapon, int *player, float *dist, float *age, int *blast);
int   portLuaGameHitEvents(int *kind, int *chr, int *player, int max);   /* the queued held-gun (1) and spectator (2) hits; empties the queue */
int   portLuaGameHitEventsLost(void);
int   portLuaGameChrDisarm(int chr);                                     /* knock the held guns out of the hand; 1 = done */
int   portLuaGameOwnedGuns(int chr, const char **g1, const char **g2);   /* the guns it held (kept after a drop): how many, names */
int   portLuaGameBondShots(int player, int *fired, int *hits);
int   portLuaGameChrShoot(int chr, const char *part, const char *weapon, int player);   /* test helper: a real shot through the damage code */
void  portArenaBlastForce(int player);                                    /* the next explosion's owner (-1 = the normal rule) */
int   portLuaGameSfx(int id, float pitch);

/* markers v2 (port_arenamode.c) */
void  portLuaGameMarkerStyle(float fade, float nearD, int drops, int knives, float dropNear, float merge, int edge, int maxGroups, int legacy);
int   portLuaGameItemMarker(int handle, int kind, const char *label);

#endif
