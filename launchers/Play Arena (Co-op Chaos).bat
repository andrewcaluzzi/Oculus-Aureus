@echo off
rem Colosseum arena co-op with the bot, plus wave mutators and affix elites. Needs Data\baserom.u.z64 (see README.txt).
if not exist "%~dp0Data\baserom.u.z64" (
    echo Missing Data\baserom.u.z64 - put your own GoldenEye 007 USA ROM there. See README.txt.
    pause
    exit /b 1
)
set GE_COOP=1
set GE_STAGE=colosseum
set GE_SKIPINTRO=1
set GE_MODE=arena-coop-chaos
start "" "%~dp0ProjectMidas.exe"
