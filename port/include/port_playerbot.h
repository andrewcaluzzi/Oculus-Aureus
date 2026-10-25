/* port_playerbot.h - the player bot: an AI that plays solo missions as Bond through player 1's controller.
 *
 * It only produces INPUT (pad buttons and sticks in osContGetReadData, direct-mode look degrees in the mouse
 * sample), so a run it plays is an ordinary input stream: record it with GE_INPUTREC and GE_INPUTPLAY replays the
 * same mission without the bot. It never writes sim state.
 *
 *   GE_PLAYERBOT=1        drive Bond (also the console command `playerbot on`)
 *   GE_PLAYERBOT=watch    drive nothing, only report (used when replaying a bot run)
 * Everything else (plans, timeouts, logs, the result line) is in modding/bot.md.
 *
 * Off by default: with GE_PLAYERBOT unset every hook returns at once and the game is unchanged. */
#ifndef PORT_PLAYERBOT_H
#define PORT_PLAYERBOT_H

#ifndef TARGET_N64

#include <ultra64.h>

s32  portPlayerBotMode(void);        /* 0 off, 1 drive, 2 watch */
/* inputrec.c hooks, called with player 1's input just before it is recorded / replayed */
void portPlayerBotPad(OSContPad *pad);
void portPlayerBotLook(f32 *dx, f32 *dy, s32 *direct, f32 *yawdeg, f32 *pitchdeg);
void portPlayerBotDigMove(s32 *dx, s32 *dy);
void portPlayerBotActions(u8 *down, s32 count);
/* file.c portMissionEndLog: the stage is ending */
void portPlayerBotMissionEnd(s32 stage, s32 allcomplete);
/* console: `playerbot [on|off|watch|info|plan <file>|step]` */
void portPlayerBotConsole(const char *arg, const char *arg2);

/* Plug-in point for game modes (Bank Heist etc.): a planner that hands the bot its next step instead of the plan
 * file. Return 1 with *step filled, 0 when there is nothing to do right now (the bot fights / idles), -1 when the
 * mode is finished. NULL = plan files (the default). Steps use the plan language of port_playerbotlogic.h. */
struct PblStep;
typedef s32 (*PortPlayerBotPlanner)(struct PblStep *step, s32 stageTick);
void portPlayerBotSetPlanner(PortPlayerBotPlanner fn);

#endif /* TARGET_N64 */
#endif /* PORT_PLAYERBOT_H */
