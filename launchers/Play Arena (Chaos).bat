@echo off
rem Colosseum arena, 20 waves with wave mutators (KNIVES ONLY, PAINTBALL, ONE-HIT KILLS, TURBO, LOW VISIBILITY, BIG HEADS,
rem IRON SKIN, BOTTOMLESS CLIPS; from wave 3) and affix elites (bigger enemies with 1-2 affixes). Needs Data\baserom.u.z64 (see README.txt).
if not exist "%~dp0Data\baserom.u.z64" (
    echo Missing Data\baserom.u.z64 - put your own GoldenEye 007 USA ROM there. See README.txt.
    pause
    exit /b 1
)
set GE_STAGE=colosseum
set GE_SKIPINTRO=1
set GE_MODE=arena-chaos
start "" "%~dp0ProjectMidas.exe"
