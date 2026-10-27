@echo off
rem Dam+: stock Dam (from your ROM) with Night Shift (more guards, patrols and pickups) and the motor pool: the rusty
rem shutter beside the start now opens onto a vehicle bay and a barracks. Its key is on the guard by the truck.
rem Same mission, briefing and objectives.
rem Difficulty: GE_DIFF 0 Agent .. 3 007 (00 Agent = all four objectives). Edit the line below to change it.
rem Needs Data\baserom.u.z64 (see README.txt).
if not exist "%~dp0Data\baserom.u.z64" (
    echo Missing Data\baserom.u.z64 - put your own GoldenEye 007 USA ROM there. See README.txt.
    pause
    exit /b 1
)
set GE_STAGE=dam
set GE_SKIPINTRO=1
set GE_DIFF=2
set GE_DAMPLUS=1
start "" "%~dp0ProjectMidas.exe"
