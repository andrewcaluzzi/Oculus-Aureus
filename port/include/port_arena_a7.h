#ifndef PORT_ARENA_A7_H
#define PORT_ARENA_A7_H
/* Arena drop A7 (modding/arena.md): wave mutators, elite affixes, high scores. Plain types only, so the Lua half
 * (port/luavm/port_luavm_arena.c) can include it. */

/* port_arenafx.c (game state, SIM) */
int   portLuaGameChrKnifeOnly(int chr, int on);                /* knife-only: every shot that is not a thrown knife is dropped */
int   portLuaGameChrScale(int chr, float factor);              /* model scale x factor (0.5 .. 2) */
int   portLuaGameChrHeal(int chr, float frac);                 /* heal wounds by frac of max health (not armour); 1 = healed;
                                                                  frac < 0 = a wound (test hook test.hurt), capped short of death */
int   portLuaGameChrHealth(int chr, float *damage, float *maxdamage);   /* 0 = gone */
int   portLuaGameExplode(int chr, int kind);                   /* an EXPLOSION_DEF explosion at the chr's feet; 1 = made */
int   portLuaGameFog(float farv, int r, int g, int b);          /* fog with this far distance (<= 0: the level's own) */
float portLuaGameFogGet(void);                                 /* the far distance of our fog, 0 = none */

/* port_arenascores.c (files and the debrief text: presentation, PRES) */
int         portArenaScoresEnabled(void);                      /* solo, not netplay */
int         portArenaScoreGet(const char *key, double *out);   /* 1 = found */
int         portArenaScoreSet(const char *key, double v);      /* stores it and rewrites the file (unless GE_NOSCORESAVE); 1 = stored */
int         portArenaScoreCount(void);
const char *portArenaScoreKey(int k, double *v);               /* k-th entry, NULL past the end */
const char *portArenaScoresPath(void);
void        portArenaDebriefSet(int k, const char *text);      /* line k (0..7) of the arena debrief; NULL / "" clears it */
void        portArenaDebriefClear(void);
const char *portArenaDebriefLine(int k);                       /* front.c, the report page on the Colosseum; NULL = no more lines */

#endif
