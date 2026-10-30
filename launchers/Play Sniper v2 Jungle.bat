@echo off
setlocal
rem SNIPER DUEL v2 on the SNIPER JUNGLE (a 408 m valley): the rifle and binoculars, marksmen 250 to 350 m off, a few hits and you are down.
rem Needs a build with the jungle level (-DGE_LEVELGEN_EXTRA=port/levelgen_jungle -DGE_LEVELGEN_EXTRA_NAME=jungle, or as
rem GE_LEVELGEN_EXTRA2 beside the snowfield) and Data\baserom.u.z64 (see README.txt).
if not exist "%~dp0Data\baserom.u.z64" (
    echo Missing Data\baserom.u.z64 - put your own GoldenEye 007 USA ROM there. See README.txt.
    pause
    exit /b 1
)
set GE_STAGE=levelgen
set GE_LEVELGEN=jungle
set GE_NOINTRO=1
set GE_STAGE_AFTER=1
set GE_MODE=sniper-v2
start "" "%~dp0ProjectMidas.exe"
