/* port_mode.h - game modes' "level reset".
 * GE_MODE=<name> makes the next solo stage load wiped to geometry: no guards, objectives or level
 * AI scripts, doors unlocked (the script can keep them locked: mode.keep_locks), and the stock mission-complete suppressed. Off unless the variable is
 * set; never active in demos, recording or multiplayer. */
#ifndef PORT_MODE_H
#define PORT_MODE_H

void        portModeStageStart(int stage);   /* lvlStageLoad: decide, before proplvreset2 */
int         portModeActive(void);            /* 1 while the loaded stage is a reset stage */
const char *portModeName(void);              /* GE_MODE value, "" when not active */
void        portModeReport(const char *when); /* logs live chr count (load, then ~1s in) */
void        portModeOneSecond(void);         /* lv.c GE_CHEATS point: unlock doors (unless mode.keep_locks), report */
void        portModeKeepLocks(int on);       /* Lua mode.keep_locks: the level's keyed doors / padlocks stay locked (call at script load) */
int         portModeLocksKept(void);
/* The mode's own win/fail: shows the stock end screen (the stock trigger stays suppressed). TRUE = first call. */
int         portModeEndMission(int win);
/* Mods menu: the mode for the next solo stage ("" = off; GE_MODE in the environment wins). Session-only. */
void        portModeSetMenuChoice(const char *name);
const char *portModeMenuChoice(void);
const char *portModeChoice(void);            /* GE_MODE if set (even "0"), else the Mods page's pick; NULL or "" = none. In netplay the HOST's choice is the session's */
int         portModeEndResult(void);         /* 0 running, 1 won, 2 failed; read by the end screen (front.c) */
/* building blocks (events, pads, zones, HUD): port_event.h, port_blocks.h */

#endif /* PORT_MODE_H */
