@echo off
rem Bank heist: the Janus bank, St Petersburg branch (the generated level, tools/levelgen/janus_bank.json on the levelgen slot).
rem Start in the street, climb the keycard ladder, crack the vault a floor down, out by the van or the loading dock.
rem Needs Data\baserom.u.z64 (see README.txt).
if not exist "%~dp0Data\baserom.u.z64" (
    echo Missing Data\baserom.u.z64 - put your own GoldenEye 007 USA ROM there. See README.txt.
    pause
    exit /b 1
)
set GE_STAGE=levelgen
set GE_SKIPINTRO=1
set GE_MODE=heist
start "" "%~dp0ProjectMidas.exe"
