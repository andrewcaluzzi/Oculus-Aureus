@echo off
setlocal
rem SNIPER DUEL on the SNIPER JUNGLE prototype: a big wide jungle valley under Jungle's sky. Needs a build with the jungle level
rem (the build_win_jungle kind: -DGE_LEVELGEN_EXTRA=port/levelgen_jungle -DGE_LEVELGEN_EXTRA_NAME=jungle) and Data\baserom.u.z64 (see README.txt).
if not exist "%~dp0Data\baserom.u.z64" (
    echo Missing Data\baserom.u.z64 - put your own GoldenEye 007 USA ROM there. See README.txt.
    pause
    exit /b 1
)
set GE_STAGE=levelgen
set GE_LEVELGEN=jungle
set GE_NOINTRO=1
set GE_STAGE_AFTER=1
set GE_MODE=sniper-jungle
start "" "%~dp0ProjectMidas.exe"
