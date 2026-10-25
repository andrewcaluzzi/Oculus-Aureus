@echo off
rem Walk the Janus bank (the generated heist level, tools/levelgen/janus_bank.json on the levelgen slot): no guards, no heist rules,
rem Bond is invincible and every keyed door opens for him. Needs Data\baserom.u.z64 (see README.txt).
if not exist "%~dp0Data\baserom.u.z64" (
    echo Missing Data\baserom.u.z64 - put your own GoldenEye 007 USA ROM there. See README.txt.
    pause
    exit /b 1
)
set GE_STAGE=levelgen
set GE_SKIPINTRO=1
set GE_MODE=bank-walk
start "" "%~dp0ProjectMidas.exe"
