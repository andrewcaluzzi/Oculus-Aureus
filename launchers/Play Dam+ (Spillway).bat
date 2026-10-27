@echo off
rem Dam+ spillway balcony only (concept 4): stock Dam plus a doorway in the south wall of the big underground chamber under
rem the bungee platform. A stair inside the dam drops about 8 m and a short passage breaks out through the downstream face onto
rem a long balcony on the face: the gorge below, the bungee platform up to the west. Press B at the valve station on the
rem balcony to open the spillway (sound only; no objective changes).
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
set GE_DAMPLUS=spill
rem Cheats: turbo Bond, invincibility, infinite ammo. To play without them, delete the GE_CHEATS line below
rem (or remove any of its words: invuln, turbo, ammo).
set GE_CHEATS=invuln,turbo,ammo
start "" "%~dp0ProjectMidas.exe"
