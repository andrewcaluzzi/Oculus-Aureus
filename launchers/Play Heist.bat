@echo off
rem Bank heist on Bunker 2: the Janus bank, St Petersburg branch. Needs Data\baserom.u.z64 (see README.txt).
if not exist "%~dp0Data\baserom.u.z64" (
    echo Missing Data\baserom.u.z64 - put your own GoldenEye 007 USA ROM there. See README.txt.
    pause
    exit /b 1
)
set GE_STAGE=bunker2
set GE_SKIPINTRO=1
set GE_MODE=heist
start "" "%~dp0ProjectMidas.exe"
