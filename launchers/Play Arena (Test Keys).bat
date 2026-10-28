@echo off
setlocal
rem Colosseum arena with the test keys and cheats: PageDown/PageUp jump waves (Shift = stack), Home / Shift+Home preview every kill callout, End turns the LOW VISIBILITY fog on / off, Insert the THUNDERBALL rain. Needs Data\baserom.u.z64 (see README.txt).
if not exist "%~dp0Data\baserom.u.z64" (
    echo Missing Data\baserom.u.z64 - put your own GoldenEye 007 USA ROM there. See README.txt.
    pause
    exit /b 1
)
set GE_STAGE=colosseum
set GE_SKIPINTRO=1
set GE_MODE=arena
set GE_ARENA_TESTKEYS=1
set GE_CHEATS=invuln,guns
start "" "%~dp0ProjectMidas.exe"
