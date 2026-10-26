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
       PORTLUA_BEH_SPECTATOR, /* unarmed crowd: invincible, no autoaim, hostage faction (nobody targets it), idle list; animated by portLuaGameChrAnim */
       PORTLUA_BEH_CIVILIAN /* heist job 4: unarmed, hostage faction, counts as a civilian casualty; surrenders when aimed at (port_heist.c) */ };
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
    /* heist job 3: hearing = chr->hearingscale (0 = stock 1.0); alarmPad = the pad the stock alarm-raiser list runs to (-1 none);
     * patrol = a stock patrol path id (-1 none) */
    float hearing;
    int alarmPad, patrol;
    /* heist job 6: offset = spawn at (pad + offx, pad + offz) on the stan tile that point lands on (hasoff), so many chrs can share a pad */
    int hasoff;
    float offx, offz;
} PortLuaChrSpec;
int         portLuaGameSpawnChr(int pad, const PortLuaChrSpec *spec);   /* chrnum, -1 failed, -2 bad skin, -3 bad weapon */
int         portLuaGameChrKill(int chr, int head);                        /* test helper: lethal damage to an arena chr (head = count as a headshot); 1 = done */
int         portLuaGameChrHeadshot(int chr);                              /* 1 = an arena enemy's last damaging hit was to the head */
int         portLuaGameChrIdle(int chr);                                  /* 1 idle (standing / one-shot anim run out), 0 animating, -1 gone or dead */
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
/* obj.get / test.obj_damage (levelgen P2.2): any setup object by index. type = PROPDEF_*, model = its obj model; destroyed per the
 * stock damage state. ObjGet: 0 no such record (or no mode). ObjDamage: -1 no such object / no prop, else 1. */
typedef struct PortHeistObj { int idx, type, model, live, room, pad, destroyed; float x, y, z, health, damage; } PortHeistObj;
int         portHeistObjGet(int idx, PortHeistObj *o);
void        portHeistSetHoldFire(int on);                                  /* mode.hold_fire: guards hold shots with a civilian in the line */
int         portHeistHoldFireOn(void);
int         portHeistFireStats(int chrnum, int *fired, int *held);         /* test.fire_stats (tests) */
int         portHeistObjDamage(int idx, float amount, int player);
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
/* guards and civilians (port_heist.c, heist jobs 3-4) */
void        portHeistCivilianAdd(int chrnum);                              /* register a spawned chr as a civilian */
int         portHeistCivilianState(int chrnum);                            /* 0 idle, 1 surrendered, 2 cowering, 3 knocked out, -1 not a civilian / gone */
void        portHeistCivilianTune(float range, float aim_sec, float threat_sec, float cower_sec);   /* <= 0 keeps */
int         portHeistCivilianKilled(void);                                 /* civilians killed this stage */
int         portHeistFaceBond(int player, float x, float z, float y);      /* test helper: turn Bond's view onto a point */
int         portHeistChrDown(int chrnum);                                  /* 1 = dead or knocked out (the body_found notion) */
int         portHeistBodiesFound(void);
int         portHeistTestNoise(int player, float noise);                   /* test helpers */
int         portHeistChrHeard(int chrnum, int clear);
void        portHeistBodyCheck(int on);                                   /* body discovery on/off (default on) */
/* knock-out (port_heist.c, heist job 5) */
int         portHeistKnockOut(int chrnum, int player);                     /* knock a chr out now (player -1 = a script); 1 ok */
int         portHeistKoWake(int chrnum);                                   /* wake a knocked-out chr; 1 ok */
int         portHeistKoIs(int chrnum);
int         portHeistKoSlaps(int chrnum);                                  /* slaps counted toward its knock-out */
void        portHeistKoCount(int *down, int *total, int *woken);           /* out now, knock-outs this stage, wakes this stage */
int         portHeistKoWaker(int chrnum);                                  /* the guard on its way to wake it, -1 none */
void        portHeistSetSlapsToKo(int n);                                  /* 0 = slaps are stock hits */
int         portHeistSlapsToKo(void);
void        portHeistSetGuardsWake(int on);
int         portHeistKoSearch(int player);                                 /* Bond p searches the knocked-out chr next to him: chrnum, -1 none */
int         portHeistTestSlap(int player, int chrnum);                     /* test hook: a slapper hit through the stock hit path */
/* heist job 6 (port_heist.c): loot pickups (spawn.item "goldbar" / "money"), weapon in hand, difficulty */
#define PORT_HEIST_LOOT_HANDLE 0x1000000                                   /* loot handles are >= this (arena item handles stay below) */
int         portHeistLootSpawn(int pad, int kind, float dx, float dz);     /* kind 0 gold bar, 1 money: handle, 0 no room, -2 bad pad */
int         portHeistLootAlive(int handle);
int         portHeistLootRemove(int handle);
int         portHeistLootPos(int handle, float *x, float *z);
int         portHeistBondEquip(int player, int item);                      /* draw that item into the right hand (give it first if missing); 1 ok */
int         portHeistBondWeapon(int player);                               /* ITEM_* in his right hand, -1 no such Bond */
int         portHeistDifficulty(void);                                     /* 0 Agent .. 3 007 */
int         portHeistDoorOneWay(int idx, int clear);                       /* stock one-way bits set? (clear = drop them); -1 no such door */
int         portHeistDoorSnap(int idx, int open);                          /* snap fully open / shut now, blocked or not; -1 no such door */
/* heist CCTV view (port_cctvview.c, designs/bank-heist.md P1 T2) and the HUD panel */
int         portCctvView(int p, int cam, int why);                         /* 1 = Bond p watches camera cam now; why = the event's reason */
int         portCctvViewOff(int p, int why);                               /* 1 = he was watching */
int         portCctvViewing(int p);                                        /* camera setup index, -1 none */
int         portCctvViewTicks(int p);                                      /* ticks on the current camera */
void        portCctvSetName(int cam, const char *name);
const char *portCctvName(int cam);                                         /* NULL = none set */
int         portCctvSeesChr(int cam, int chrnum);                          /* a working camera has the chr in its live cone, clear line */
int         portCctvSeesPoint(int cam, float x, float y, float z);
int         portCctvChrRoom(int chrnum);                                   /* -1 gone */
int         portCctvBondRoom(int p);                                       /* -1 none */
int         portRoomVisDrawn(int pl, int *out, int max);                   /* the rooms view pl drew last (port_roomvis.c) */
int         portCctvRenderInfo(int *cam, int *start, int *rooms, int max, int *geom, int *frames);   /* this machine's last feed pass */
void        portCctvTestPress(int mask);                                   /* test: raw N64 buttons held on pad 0 for 4 sim ticks */
void        portCctvTestUse(int p);                                        /* test: one Port use-action tap in Bond p's next tick */
int         portHeistUseKey(int p);                                        /* inside a use handler: the press is the use action, not B */
void        portHeistModeState(const char *text);                          /* mode.state: the script's sim state, hashed and dumped */
/* the banker disguise, the power, scripted walks (designs/bank-heist.md section 16; port_heist.c "cover") */
int         portHeistCoverGuard(int chrnum, int role);                     /* this guard judges the disguise (role 1 = security staff) */
void        portHeistCoverSetOn(int on);
int         portHeistCoverIsOn(void);
void        portHeistCoverSetAlert(int on);                                /* everyone alert: the disguise protects nobody */
int         portHeistCoverAlert(void);
void        portHeistCoverRoom(int room, int cls);                         /* 0 public, 1 staff only, 2 restricted */
int         portHeistCoverRoomClass(int room);
void        portHeistCoverCarrying(int p, int on);                         /* Bond p carries loot (seen = blown) */
void        portHeistCoverTune(const float *v, int n);                     /* the sCt fields in order; < 0 keeps */
int         portHeistCoverTestAct(int p);
int         portHeistCoverBlow(int chrnum, int p);
int         portHeistCoverState(int chrnum, float *susp, int *witness, int *reported, int *sees, int *role, int *why);
float       portHeistCoverSusp(int chrnum, int p);                         /* -1 not a cover guard */
int         portHeistCoverMeter(int p, float *level, int *chr);            /* 0 off .. 5 alert */
int         portHeistCoverSeen(int p);
int         portHeistCoverCount(void);
float       portHeistCoverBondSpeed(int p);
const char *portHeistCoverWhyName(int why);
void        portHeistPowerSet(int on);
int         portHeistPowerOn(void);
int         portHeistChrGo(int chrnum, int pad, int run);                  /* spawn.go: walk / run to a pad on the stock route */
int         portHeistRadioSfx(int chrnum, int relay);                      /* cover.radio: the radio sound at a guard (presentation) */
int         portHeistRoleBit(const char *name);                            /* guard door rules: a role name's bit, -1 full */
int         portHeistSetChrRoles(int chrnum, unsigned bits);
unsigned    portHeistChrRoles(int chrnum);
int         portHeistDoorGuardAccess(int idx, int rule, unsigned allow);   /* rule 0 = anyone (stock), 1 = these roles only (0 = never) */
void        portHeistSetGuardDoors(int on);
int         portHeistGuardDoorsOn(void);
void        portHeistDoorStats(int *refused, int *opened);
void        portCctvTestShot(void);                                        /* test: a GE_SHOT capture of the next frame */
int         portCctvExtraChrs(void);                                       /* chrs the last feed pass posed and drew itself */
int         portCctvOverlayGet(int *cam, char *name, int namelen, int *index, int *count, int *offline, int *seconds, int *ticks);
int         portCctvButtonMask(const char *name);                          /* "a" "b" "z" "cl" "cr" -> mask, 0 unknown */
const char *portLuaGameHudPanelGet(int line);                              /* this machine's panel line (tests), NULL empty */
void        portLuaGameHudPanel(int player, int line, const char *text);   /* player < 0 = all; NULL text clears the line */
void        portLuaGameHudPanelClear(int player);

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
