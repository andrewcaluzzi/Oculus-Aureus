/* port_arenacoop.h - arena co-op (drop A7, modding/arena.md "Drop A7 co-op"): downed / revive rules for two Bonds and the
 * arena partner bot (a Bond driven through the co-op puppet pad). Plain types only: the Lua half (port/luavm/port_luavm_coop.c)
 * includes it too. Game half: port/src/port_arenacoop.c. Everything is inert unless a mode is active, co-op is on and the
 * mode script turned the rules on (coop.rules), so solo play and the demos never reach it. */
#ifndef PORT_ARENACOOP_H
#define PORT_ARENACOOP_H

/* life states (coop.state) */
enum { PORT_AC_UP = 0, PORT_AC_DOWN = 1, PORT_AC_DEAD = 2, PORT_AC_OUT = 3, PORT_AC_NONE = -1 };
/* what a bleed-out does (coop.rules bleedout=) */
enum { PORT_AC_BLEED_WAVE = 0, PORT_AC_BLEED_RUN = 1, PORT_AC_BLEED_END = 2 };

typedef struct {
    int   on;            /* the downed rules are on */
    float bleed;         /* seconds a downed Bond lasts */
    float revive;        /* seconds a partner must stand close */
    float radius;        /* how close (units, XZ) */
    float reviveHealth;  /* health after a revive, 0..1 */
    float grace;         /* seconds of no damage after a revive */
    int   bleedout;      /* PORT_AC_BLEED_* */
    int   debug;         /* bot / rule debug log */
    /* the partner bot's skill (port_cooplogic.h PortCoopBotSkill): a step 1..5 (5 = the bot as built), and its knobs; a negative
     * knob takes the step's value. The game half clamps them. */
    int   botSkill;
    float botReaction;   /* seconds before it fires at a newly acquired target */
    float botAimError;   /* degrees of aim wobble and extra fire cone */
    float botTurn;       /* degrees a second it turns at most */
    float botRange;      /* units it engages from */
} PortArenaCoopRules;

/* engine hooks (port_coop.c, bondview2.c) */
void     portArenaCoopReset(void);                 /* stage start (co-op deathReset) */
void     portArenaCoopTick(void);                  /* once per sim tick, after the co-op death tick */
int      portArenaCoopDownInstead(int player);     /* bondviewKillCurrentPlayer: 1 = the Bond went down instead of dying */
int      portArenaCoopNoDamage(int player);        /* record_damage_kills: 1 = no damage (downed, or the grace after a revive) */
int      portArenaCoopDowned(int player);          /* 1 while down (no input, enemies ignore him) */
int      portArenaCoopHeld(int player);            /* 1 while a bled-out Bond waits for the wave to end (no respawn) */
unsigned portArenaCoopHash(char *dump, int cap);   /* the netplay state hash part (players field) and a dump line */
/* the partner bot: one pad + look for Bond `me` (the co-op puppet), partner `partner` */
void     portArenaBotPad(int me, int partner, unsigned short *buttons, signed char *sx, signed char *sy, int *yawMilli, int *pitchMilli);

/* Lua game half */
void portLuaGameCoopRules(const PortArenaCoopRules *r);
int  portLuaGameCoopActive(void);
int  portLuaGameCoopState(int player, float *secsLeft, float *reviveFrac);   /* PORT_AC_*; secsLeft: bleed-out timer, reviveFrac 0..1 */
int  portLuaGameCoopForceDown(int player);                                   /* test: put a Bond down now */
int  portLuaGameCoopRelease(int player);                                     /* a held (bled-out) Bond respawns at the next death tick */
int  portLuaGameCoopKill(int player);                                        /* test: a real death, past the downed rule */
int  portLuaGameCoopBot(int on);                                             /* the puppet Bond is driven by the arena bot */
int  portLuaGameCoopBotGoal(int player, int set, float x, float z);         /* a point the bot walks to (ammo, armour); set 0 clears */
int  portLuaGameCoopBotInfo(int player, int *target, const char **act, int *ammo, int *weapon, int *shots);
/* the bot's test counters: empty-gun clicks of that Bond's gun, ticks it held fire with another Bond in the line of fire */
int  portLuaGameCoopBotStats(int player, int *dry, int *blocked);
void portLuaGameCoopCounts(int *downs, int *revives, int *bleedouts);

#endif
