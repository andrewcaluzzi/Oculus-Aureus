@echo off
setlocal
rem SNIPER DUEL on the SEVERNAYA SNOWFIELD prototype: a long open rolling snowfield under Surface's sky. Needs a build with the snowfield level
rem (the build_win_snowfield kind: -DGE_LEVELGEN_EXTRA=port/levelgen_snowfield -DGE_LEVELGEN_EXTRA_NAME=snowfield) and Data\baserom.u.z64 (see README.txt).
if not exist "%~dp0Data\baserom.u.z64" (
    echo Missing Data\baserom.u.z64 - put your own GoldenEye 007 USA ROM there. See README.txt.
    pause
    exit /b 1
)
set GE_STAGE=levelgen
set GE_LEVELGEN=snowfield
set GE_NOINTRO=1
set GE_STAGE_AFTER=1
set GE_MODE=sniper-snowfield
start "" "%~dp0ProjectMidas.exe"
