/* port_coop.h - two Bonds in one solo stage.
 *
 * getPlayerCount() keeps returning the MODE value (1 in co-op); portAllPlayerCount() is the real
 * number of player structs and is used only where the code iterates or sizes per player. With
 * co-op off every function here returns the vanilla value. */
#ifndef PORT_COOP_H
#define PORT_COOP_H

#ifdef __cplusplus
extern "C" {
#endif

/* nonzero while the loaded solo stage holds two players (GE_COOP=1) */
int portCoopActive(void);
/* real number of player structs (== getPlayerCount() unless co-op is active) */
int portAllPlayerCount(void);
/* the player this machine renders and controls: 0 unless co-op moves it */
int portLocalPlayer(void);
void portCoopSetLocalPlayer(int p);
/* nonzero when this player's third-person body must stay (every player in co-op) */
int portCoopKeepBody(int playernum);
/* nonzero for P2 in co-op (the player placed beside P1) */
int portCoopIsGuest(int playernum);
/* swaps pad/device index 0 with the local player's (identity unless co-op local != 0), so the
 * keyboard, mouse and pad 0 reach whichever Bond this machine plays */
int portCoopPadSwap(int i);
/* input_pc.c, once per sim tick: advances the puppet source */
void portCoopTick(void);
/* libultra_shim.c osContGetReadData, after the stick gate and input recording: moves the local pad
 * to slot portLocalPlayer() and writes the puppet pad into the other slot (data = OSContPad[4]) */
void portCoopPadRoute(void *data);
/* console: coop view [0|1] / coop info / coop p2 <idle|walk|follow> */
void portCoopConsole(const char *arg1, const char *arg2);
/* boss.c, once per stage load: returns the player count to allocate (2 for a co-op solo stage) */
int portCoopStageBegin(int stage, int players);

/* Game modes in co-op:
 *   portCoopSessionSeed()        the stage's session seed (the host's in netplay): math.random of a mode script derives from it
 *   portCoopModeMissionEnd(why)  mission.win / mission.fail: the shared mission end, once, logged with the sim tick
 *   portCoopSessionGet(name,&v)  the host's co-op session setting behind a modifier name (1 = known) */
unsigned int portCoopSessionSeed(void);
void portCoopModeMissionEnd(const char *why);
int portCoopSessionGet(const char *name, int *out);

/* Job 3 (guards and shared rules) lives in port_coop.c; game files declare what they call with a local
 * extern, like the other hooks. None of it depends on portLocalPlayer() or on pass order. */

/* Job 4: death and respawn. Game files declare what they call with a local extern.
 *   portCoopDeathTick()          boss.c, after lvlTick: notes deaths, respawns a Bond after 10 s of game time
 *   portCoopRespawnPartnerOf(i)  the living Bond a dead Bond i respawns next to (-1: none)
 *   portCoopRespawnSecsFor(i)    "Respawning in N" seconds for Bond i (0: show nothing)
 *   portCoopFailOnce()           1 the first time it is called per stage (all dead: start the failed flow once)
 *   portCoopAllDeathsDone()      1 when every Bond is dead and its death animation is over */

#ifdef __cplusplus
}
#endif

#endif
