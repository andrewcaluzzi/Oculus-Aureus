@echo off
setlocal
rem SNIPER DUEL on SEVERNAYA RIDGE (prototype): the duel's own level, two ridges across a valley. Every build carries the ridge level
rem (GE_LEVELGEN_LEVELS). Needs Data\baserom.u.z64 (see README.txt). When the duel ends the main menu hub
rem comes up on the debrief.
if not exist "%~dp0Data\baserom.u.z64" (
    echo Missing Data\baserom.u.z64 - put your own GoldenEye 007 USA ROM there. See README.txt.
    pause
    exit /b 1
)
set GE_STAGE=levelgen
set GE_LEVELGEN=ridge
set GE_NOINTRO=1
set GE_STAGE_AFTER=1
set GE_MODE=sniper-ridge
start "" "%~dp0ProjectMidas.exe"
