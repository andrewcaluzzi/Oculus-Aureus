@echo off
setlocal
rem SNIPER DUEL on the SNIPER JUNGLE prototype, with the metre-based haze (yay/nay spike): boots build_win_jungle2 straight into the duel in the jungle.
set B=%~dp0build_win_jungle2
if not exist "%B%\Data\baserom.u.z64" (
    echo Missing build_win_jungle2\Data\baserom.u.z64
    pause
    exit /b 1
)
set GE_STAGE=levelgen
set GE_LEVELGEN=jungle
set GE_NOINTRO=1
set GE_STAGE_AFTER=1
set GE_MODE=sniper-jungle
start "" /D "%B%" "%B%\ProjectMidas.exe"
