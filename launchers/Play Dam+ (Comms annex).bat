@echo off
rem Dam+ comms annex only (concept 5): stock Dam plus a doorway in the valley yard's west wall (past the road tunnel, on the
rem left as you enter the yard). Tunnel, radio room, a stair up to a blockhouse and a roof deck with a dish over the yard.
rem While the comms console stands, the alarm calls in reinforcements every 20 s; shoot it and they stop.
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
set GE_DAMPLUS=comms
rem Cheats: turbo Bond, invincibility, infinite ammo. To play without them, delete the GE_CHEATS line below
rem (or remove any of its words: invuln, turbo, ammo).
set GE_CHEATS=invuln,turbo,ammo
start "" "%~dp0ProjectMidas.exe"
