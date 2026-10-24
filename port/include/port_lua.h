/* port_lua.h - embedded Lua 5.5 for mod scripts (Data/Scripts/*.lua) and the `lua` console
 * command. See modding/lua.md. The Lua state is created lazily, so with no scripts
 * and no `lua` command the game is untouched; it never runs during demos or in multiplayer.
 *
 * Two halves: port/src/port_lua.c (game headers; the portLuaGame* helpers) and
 * port/luavm/port_luavm.c (Lua + real libc headers; the portLua* entry points). This header
 * uses plain C types only, so both can include it. */
#ifndef PORT_LUA_H
#define PORT_LUA_H

#include <stddef.h>

/* entry points (port_luavm.c) */
void portLuaStageStart(void);                            /* lvlStageLoad: stock weapons, run the scripts */
void portLuaConsole(const char *arg1, const char *arg2); /* `lua <code>` (arg1 + " " + arg2) */
void portLuaReload(void);                                /* `lua_reload`: stock weapons, re-run the scripts */

/* game helpers (port_lua.c) */
enum { PORTLUA_S8, PORTLUA_U8, PORTLUA_S16, PORTLUA_U16, PORTLUA_S32, PORTLUA_F32 };

int         portLuaGameAllowed(void);                    /* solo, no demo playing or recording */
const char *portLuaGameScriptDir(void);
int         portLuaGameFieldCount(void);
int         portLuaGameField(int i, const char **name, size_t *off, int *type);
int         portLuaGameItemCount(void);
const char *portLuaGameItemName(int i);                  /* upper case; NULL if the item has no stats */
int         portLuaGameItemFind(const char *upper);      /* -1 if unknown */
void       *portLuaGameStats(int i);                     /* NULL if the item has no stats of its own */
void        portLuaGameSnapshot(void);
void        portLuaGameWeaponReset(void);

/* step 2: mode bridge (port_lua.c). All are inert (0 / -1 / NULL) while no mode is active. */
void        portLuaTick(int clockTicks);                 /* port_luavm.c: from portBlocksTick, 60 ticks = 1 s */
int         portLuaHasTick(void);                        /* nonzero if a script registered a tick handler */
int         portLuaGameStageLive(void);                  /* a stage is loading or running; 0 in the menus */
int         portLuaGameModeActive(void);
const char *portLuaGameModeName(void);
int         portLuaGameItemId(const char *upper);        /* ITEM_* id for a weapon name (aliases ok), -1 unknown */
int         portLuaGamePadCount(void);
int         portLuaGamePadPos(int pad, float *x, float *y, float *z);
int         portLuaGamePadList(float x, float z, float mindist, float maxdist, int wponly, int *out, int max);
/* The Bonds. `player` is the 0-based player index (0 = the host's Bond on both machines in netplay); player < 0 means the
 * no-argument form, which is player 0 (never the local player; GE_LUA_TEST_LEAKLOCAL=1 puts the local read back, a test
 * negative control that must desync). Mode scripts must never see anything that differs between the machines. */
#define PORTLUA_HOSTAGE_MAX 16                           /* = PORT_HOSTAGE_MAX (checked in port_lua.c) */
int         portLuaGameBondCount(void);                  /* 0 outside a mode */
int         portLuaGameBondPos(int player, float *x, float *y, float *z);
int         portLuaGameBondDead(int player);             /* 1 dead, 0 alive, -1 no such Bond */
int         portLuaGameBondNearest(float x, float y, float z, float *dist);   /* player index or -1 */
int         portLuaGameBondAllDown(void);
int         portLuaGameCoop(void);                       /* co-op / netplay stage: two Bonds in the shared sim */
unsigned    portLuaGameRandomSeed(void);                 /* nonzero in co-op: math.random must derive from it; 0 = solo, any seed */
int         portLuaGameSessionMod(const char *name, int *out);   /* co-op: the host's session setting behind a modifier name */
int         portLuaGameSpawnHostage(int pad);            /* chrnum or -1 */
int         portLuaGameSpawnGuard(int pad, int patrol, int item);
int         portLuaGameZoneAdd(const char *name, float x, float y, float z, float r);
void        portLuaGameZoneMove(int zone, float x, float y, float z);
void        portLuaGameZoneRemove(int zone);
int         portLuaGameZoneWatch(int chr);
int         portLuaGameZoneContains(int zone, int chr);
int         portLuaGameZoneBonds(int zone);              /* bit p set = Bond p inside */
const char *portLuaGameZoneName(int zone);
void        portLuaGameHudSet(int player, int line, const char *text);   /* player < 0 = every player */
void        portLuaGameHudClear(int player);
int         portLuaGameHostageList(int *chrs, int *states, int *leaders, int max);
int         portLuaGameHostageState(int chr);
int         portLuaGameHostageSet(int chr, int state, int player);
int         portLuaGameHostageExtract(int chr);
/* Arena mode (port_arenamode.c): a fully specified chr. skin / weapon NULL = default (random head Russian soldier / AK47),
 * health = multiplier of the stock max damage (<= 0 = 1), accuracy / speed 0..100 (-1 = stock), behaviour = PORTLUA_BEH_*,
 * knives = throws knives with its shots, hasface = turn to face (fx, fz) at spawn instead of the pad's look. */
enum { PORTLUA_BEH_CHASE, PORTLUA_BEH_RUSH, PORTLUA_BEH_HOLD, PORTLUA_BEH_SNIPE, PORTLUA_BEH_STANDARD,
       PORTLUA_BEH_SPECTATOR /* unarmed crowd: invincible, no autoaim, hostage faction (nobody targets it), idle list; animated by portLuaGameChrAnim */ };
typedef struct PortLuaChrSpec {
    const char *skin, *weapon;
    float health;
    int accuracy, speed, behaviour, knives, hasface;
    float fx, fz;
    /* drop A4b: explicit body/head (override skin), armour, per-chr animation speed, sunglasses.
     * bodyname/headname win over body/head ints; body -1 / head -2 = not given (head -1 = random / none) */
    const char *bodyname, *headname;
    int body, head, sunglasses;
    float armour;       /* extra stock armour (chr->damage < 0), as a multiple of the base 4 damage; 0 = none */
    float animspeed;    /* model play speed multiplier (the Fast Animation cheat, per chr); 0/1 = normal */
    /* drop A5: knife_range = farthest throw (units, default 4500), knife_cooldown = seconds between throws (default 1.2);
     * sight = chr->visionrange in world units (default 15000); engage = effective firing range in units (0 = by weapon:
     * rifles 6000, SMGs 3500, pistols 2500) */
    float knifeRange, knifeCooldown, sight, engage;
} PortLuaChrSpec;
int         portLuaGameSpawnChr(int pad, const PortLuaChrSpec *spec);   /* chrnum, -1 failed, -2 bad skin, -3 bad weapon */
int         portLuaGameChrKill(int chr);                                  /* test helper: lethal damage to an arena chr; 1 = done */
int         portLuaGamePools(int *loose, int *held, int *freeSlots, int *freeProps);   /* weapon slots (30): loose on the floor / held by chrs / free; free props (600) */
int         portLuaGameTrimDrops(int maxDrops, float keepRadius);         /* drop cap: free the oldest loose weapons beyond maxDrops, never one within keepRadius of a Bond; returns the number freed */
int         portLuaGameBondWarp(int player, int pad, float dx, float dz);                     /* test helper: put a Bond on a pad */
int         portLuaGameTakeDrop(int player, float radius, int *ammoBefore, int *ammoAfter);   /* test helper: pick up the nearest loose weapon via the real pickup; item id or 0 */
int         portLuaGameGiveAmmo(int player, float frac);                  /* +frac of each ammo type's max for the guns he carries; no gun; returns types topped up */
/* drop A6: world pickups. spawn.item: handle > 0, 0 = failed (no room / model), -1 unknown item, -2 bad pad */
int         portLuaGameItemSpawn(int pad, const char *what, int ammo, float amount, int ncrate, const char **cnames, const int *crounds);
int         portLuaGameItemAlive(int handle);
int         portLuaGameItemRemove(int handle);
int         portLuaGameItemPos(int handle, float *x, float *z);
int         portLuaGameBondAmmo(int player, const char *name, int *has, int *ammo);   /* carries the gun?, reserve of its ammo type */
float       portLuaGameBondWalk(int player, float x, float z);            /* test helper: steer Bond toward x, z with the real stick; distance, -1 = no Bond */
void        portLuaGameBondWalkStop(void);
int         portLuaGameChrRemove(int chr);                               /* 1 = flagged for removal */
int         portLuaGameChrAlive(int chr);                                 /* 1 exists and not dead, 0 gone or dead */
int         portLuaGameChrFree(void);                                     /* free chr slots */
int         portLuaGameChrAnim(int chr, const char *name, int force);     /* play a named idle/reaction animation: 1 started, 0 busy/gone, -1 unknown name */
const char *portLuaGameBodyName(int heads, int k);                                         /* body (heads=0) / head (heads=1) names for spawn.bodies(), NULL at the end */
int         portLuaGameSkinCount(void);
const char *portLuaGameSkinName(int k);
int         portLuaGameGive(int player, const char *itemUpper, int ammo); /* 1 given, 0 refused */
void        portLuaGameHeal(int player, float health, float armour);      /* adds, clamped to 0..1 */
int         portLuaGameBondVitals(int player, float *health, float *armour); /* 0 = no such Bond */
int         portLuaGameEndMission(int win);
int         portLuaGameMissionEnded(void);               /* mission.win/fail already happened */
void        portModeKeepLocks(int on);                   /* port_mode.c: mode.keep_locks(bool); the level reset otherwise opens every door for Bond */
/* probe.* (port_probe.c): read-only logging of the stage's setup objects and pads ("ge: probe: ..." lines); inert outside a mode */
void        portProbeLevel(void);
void        portProbePads(void);
void        portProbeCctv(void);                         /* live state of each CCTV (sight timer, sweep, distance to Bond 0) */
int         portProbeDoorsUnlocked(void);                /* the UNLOCK DOORS cheat is on (every door opens for Bond) */
int         portProbeAlarm(void);                    /* alarm siren ticks left (0 = off) */

/* heist bindings (port_heist.c): door.*, spawn.key, bond.has_key, alarm.*, cctv.*, hud.countdown. Door and camera indices are the
 * setup-record indices the probe logs. */
typedef struct PortHeistDoor { int idx, room, state, live, autoclose, linked, cell, padlocked; unsigned keyflags; float x, y, z, open; } PortHeistDoor;
typedef struct PortHeistCctv { int idx, room, timer, disabled, destroyed, live, lookpad, alarm_ticks; float x, y, z, face; } PortHeistCctv;
int         portHeistDoorList(int *out, int max);
int         portHeistDoorGet(int idx, PortHeistDoor *d);
int         portHeistDoorMove(int idx, int open);                          /* -1 no such door, 0 nothing to do, 1 started */
int         portHeistDoorKeyflags(int idx, int mode, unsigned flags);      /* mode 0 lock (|=), 1 unlock (&= ~), 2 set */
int         portHeistTryDoor(int player, int idx);                         /* -1 no such door / Bond, 0 locked, 1 moved */
int         portHeistTestGadget(int player, const char *name);             /* test hook for the gadget_used plumbing */
int         portHeistPerPlayerKeys(void);
void        portHeistSetPerPlayerKeys(int on);
unsigned    portHeistBondKeys(int player);                                 /* key flags that Bond carries */
int         portHeistSpawnKey(int where, int isChr, unsigned flags, int model);   /* handle 1.., 0 failed, -2 bad pad */
int         portHeistKeyState(int handle, float *x, float *z, int *held);
void        portHeistAlarm(int on);
int         portHeistAlarmActive(void);
int         portHeistCctvList(int *out, int max);
int         portHeistCctvGet(int idx, PortHeistCctv *c);
int         portHeistCctvEnable(int idx, int on);
int         portHeistCctvSetAlarmTime(int idx, float seconds);             /* idx < 0 = all; seconds <= 0 = stock */
void        portHeistCctvAlarmOnSpot(int on);
int         portHeistCctvAlarmFrames(const void *rec, int stock);
void        portHeistCountdown(float seconds);                             /* < 0 hides and stops */
float       portHeistCountdownLeft(void);                                  /* seconds, -1 = not running */

/* Mods menu items added by scripts (menu.add_toggle / menu.add_number), port_luavm.c.
 * Values are cached: Refresh calls the scripts' getters (never from a render path). */
int         portLuaMenuCount(void);
const char *portLuaMenuLabel(int i);
int         portLuaMenuIsToggle(int i);
int         portLuaMenuDisabled(int i);
int         portLuaMenuValue(int i);                     /* cached: 0/1 for toggles */
void        portLuaMenuRange(int i, int *mn, int *mx, int *step);
void        portLuaMenuSet(int i, int v);
void        portLuaMenuRefresh(void);
void        portLuaMenuBoot(void);                       /* runs Data/Scripts/boot/*.lua once */

#endif /* PORT_LUA_H */
