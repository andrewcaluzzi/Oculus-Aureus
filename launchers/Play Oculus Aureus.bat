@echo off
setlocal
rem Oculus Aureus, the main menu: Arena, Heist, Sniper Duel (Ridge, Surface, Snowfield, Jungle), Dam+ (the stock Dam mission, expanded),
rem GoldenEye 007 (the original game's Files screen) and Settings, over the Control room's screen wall.
rem Arena, Heist, Sniper Duel and Dam+ open their own start menus right on that screen (their MAIN MENU button comes back); only a run
rem loads its own level. After Dam+ the stock report and the Files screen follow, as from GoldenEye 007.
rem Needs Data\baserom.u.z64 (see README.txt).
rem GE_NOINTRO: Control's own overview camera and Bond swirl are skipped. GE_STAGE_AFTER: the boot screens do not wait out their two seconds.
if not exist "%~dp0Data\baserom.u.z64" (
    echo Missing Data\baserom.u.z64 - put your own GoldenEye 007 USA ROM there. See README.txt.
    pause
    exit /b 1
)
set GE_STAGE=control
set GE_NOINTRO=1
set GE_STAGE_AFTER=1
set GE_MODE=hub
start "" "%~dp0ProjectMidas.exe"
