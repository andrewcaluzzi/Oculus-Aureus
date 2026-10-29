@echo off
setlocal
rem SNIPER DUEL (prototype): the stock Surface stage, a sniper rifle and a PP7, five rounds against named marksmen. When the duel ends the
rem main menu hub comes up on the debrief (its SNIPER DUEL card is the same duel). Needs Data\baserom.u.z64 (see README.txt).
rem GE_NOINTRO: Surface's own intro camera and Bond swirl are skipped. GE_STAGE_AFTER: the boot screens do not wait out their two seconds.
if not exist "%~dp0Data\baserom.u.z64" (
    echo Missing Data\baserom.u.z64 - put your own GoldenEye 007 USA ROM there. See README.txt.
    pause
    exit /b 1
)
set GE_STAGE=surface
set GE_NOINTRO=1
set GE_STAGE_AFTER=1
set GE_MODE=sniper
start "" "%~dp0ProjectMidas.exe"
