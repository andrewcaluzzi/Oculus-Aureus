@echo off
rem Whacky arena with test keys and cheats (invincible, all guns). F5 / F6 = next / previous Whacky effect NOW on the live enemies,
rem F7 = effect off, F8 = the minion launcher (+ rockets: fire it, each rocket hatches tiny knife throwers on your side).
rem PageDown / PageUp = next / back wave (Shift = stack on the live enemies). Needs Data\baserom.u.z64 (see README.txt).
if not exist "%~dp0Data\baserom.u.z64" (
    echo Missing Data\baserom.u.z64 - put your own GoldenEye 007 USA ROM there. See README.txt.
    pause
    exit /b 1
)
set GE_STAGE=colosseum
set GE_SKIPINTRO=1
set GE_MODE=arena-whacky
set GE_ARENA_TESTKEYS=1
set GE_CHEATS=invuln,guns
start "" "%~dp0ProjectMidas.exe"
