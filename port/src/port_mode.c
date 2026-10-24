/* port_mode.c - level reset for game modes (port_mode.h). */
#ifndef TARGET_N64

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ultra64.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include "game/player.h"
#include "port_mode.h"
#include "port_coop.h"
#include "port_blocks.h"

extern s32 getPlayerCount(void);
extern s32 get_is_ramrom_flag(void);
extern s32 get_recording_ramrom_flag(void);
extern s32 get_numguards(void);
extern s32 chrGetNumFree(void);
extern void cheatButtonHandleCheatsTurnedOn(s32 cheat);
extern s32 gamemode;

extern void bossRunTitleStage(void);
extern s32 g_isBondKIA;                       /* front.c */
extern s32 mission_failed_or_aborted;         /* front.c */
extern enum CAMERAMODE g_CameraMode;          /* bondview2.c */

static char sName[32];
static s32 sActive;
static s32 sResult;   /* 0 = running, 1 = won, 2 = failed (kept until the next mission stage loads) */
static char sMenuChoice[32];   /* the Mods menu's Game mode pick; session-only, "" = Off */
static s32 sKeepLocks;         /* a mode script asked for the level's keyed doors to stay locked (mode.keep_locks); reset every stage start */

/* PORT: the Mods menu picks the mode for the next solo stage. GE_MODE in the environment still wins. */
void portModeSetMenuChoice(const char *name)
{
    sMenuChoice[0] = '\0';
    if (name != NULL)
    {
        strncpy(sMenuChoice, name, sizeof(sMenuChoice) - 1);
        sMenuChoice[sizeof(sMenuChoice) - 1] = '\0';
    }
    printf("ge: mode: menu choice '%s'\n", sMenuChoice);
    fflush(stdout);
}

const char *portModeMenuChoice(void)
{
    return sMenuChoice;
}

/* The mode the next stage is meant to run: GE_MODE if set (even "0" = off), else the Mods page's pick; NULL / "" = none. Netplay: the HOST's
 * answer is the session's (port_net.c sends it); a guest's environment is overwritten with it before its stage load. */
const char *portModeChoice(void)
{
    const char *e = getenv("GE_MODE");

    if ((e == NULL || e[0] == '\0') && sMenuChoice[0] != '\0')
    {
        e = sMenuChoice;
    }
    return e;
}

void portModeStageStart(int stage)
{
    const char *e = portModeChoice();

    if (stage != LEVELID_TITLE)
    {
        sResult = 0;   /* the end screen (loaded as the title stage) still reads it */
    }
    sActive = 0;
    sKeepLocks = 0;
    {
        extern void portHeistStageReset(void);   /* port_heist.c: per-player keys, CCTV overrides */
        portHeistStageReset();
    }
    sName[0] = '\0';
    /* Same predicate as the co-op bot: solo, no demo playing or recording. */
    if (stage == LEVELID_TITLE || gamemode == GAMEMODE_MULTI || getPlayerCount() != 1
        || get_is_ramrom_flag() != 0 || get_recording_ramrom_flag() != 0)
    {
        return;
    }
    /* Co-op and netplay run modes too (the mode name is the HOST's: GE_MODE is set from SETUP / NEXTSTAGE on the guest, port_net.c). */
    if (e == NULL || e[0] == '\0' || e[0] == '0')
    {
        portBlocksStageStart(stage);   /* event state is reset on every solo stage; nothing else runs */
        return;
    }
    strncpy(sName, e, sizeof(sName) - 1);
    sName[sizeof(sName) - 1] = '\0';
    sActive = 1;
    portBlocksStageStart(stage);
    printf("ge: mode: '%s' on stage %d, level reset (no chrs, objectives or level AI)\n", sName, stage);
    fflush(stdout);
}

int portModeActive(void)
{
    return sActive;
}

const char *portModeName(void)
{
    return sActive ? sName : "";
}

/* The mode's own end of mission (Lua mission.win/fail). Goes to the title stage directly, so the
 * stock body of bossReturnTitleStage (progress save, briefing) never runs; the end screen is the
 * stock Mission Failed/Report page, which reads portModeEndResult. TRUE = accepted (first call only). */
s32 portModeEndMission(s32 win)
{
    if (!sActive || sResult != 0)
    {
        return FALSE;
    }
    /* Bond dying (death cam) or the stock abort/KIA flow must not be overwritten by a script. In co-op "Bond is dead" means EVERY
     * Bond is down (portBondAllDown: the lives / respawn rules included), never the current or the local player. */
    if (g_isBondKIA || mission_failed_or_aborted || g_CurrentPlayer == NULL || portBondAllDown() || g_CameraMode != CAMERAMODE_FP)
    {
        printf("ge: mode: mission end refused (Bond dead, aborted or not in play)\n");
        fflush(stdout);
        return FALSE;
    }
    sResult = win ? 1 : 2;
    printf("ge: mode: mission %s\n", win ? "won" : "failed");
    fflush(stdout);
    {   /* the player bot (port_playerbot.c): its RESULT line for a mode's own win / fail */
        extern void portPlayerBotMissionEnd(s32 stage, s32 allcomplete);
        portPlayerBotMissionEnd(-1, win ? 1 : 0);
    }
    if (portCoopActive())
    {
        portCoopModeMissionEnd(win ? "mode win" : "mode fail");   /* the shared mission-end: the same sim tick on both machines */
    }
    else
    {
        bossRunTitleStage();
    }
    return TRUE;
}

s32 portModeEndResult(void)
{
    return sResult;
}

void portModeReport(const char *when)
{
    if (sActive)
    {
        printf("ge: mode: %s: chrs=%d (slots=%d)\n", when, get_numguards() - chrGetNumFree(), get_numguards());
        fflush(stdout);
    }
}

void portModeOneSecond(void)
{
    if (sActive)
    {
        /* the level reset opens every door for Bond (no guards carry the keys), unless the script wants the keycard doors (mode.keep_locks) */
        if (!sKeepLocks)
        {
            cheatButtonHandleCheatsTurnedOn(CHEAT_PORT_UNLOCK_DOORS);
        }
        portModeReport("t+1s");
    }
}

/* mode.keep_locks(true): keep the stage's keyed doors and padlocks working (the mode's own key carriers hand out the keys). Call it from the
 * script's top level: the unlock happens one second into the stage. Inert outside a mode. */
void portModeKeepLocks(int on)
{
    if (sActive)
    {
        sKeepLocks = on ? 1 : 0;
    }
}

int portModeLocksKept(void)
{
    return sActive && sKeepLocks;
}

#endif /* TARGET_N64 */
