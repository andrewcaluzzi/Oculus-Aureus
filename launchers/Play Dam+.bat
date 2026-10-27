@echo off
rem Dam+: stock Dam (from your ROM) with everything built so far:
rem   Night Shift: more guards, patrols and pickups.
rem   Motor pool: the rusty shutter beside the start opens onto a vehicle bay and a barracks. Its key is on the guard by the truck.
rem   Comms annex: a doorway in the valley yard's west wall leads to a radio room and up to a roof deck with a dish over the yard.
rem     Shoot the comms console and the alarm stops calling in reinforcements.
rem   West gallery: a hatch in the westernmost underground room leads through a long gallery and a turbine hall inside the
rem     wall up to the crest's dead-end stub, whose hazard-striped end now opens.
rem   Spillway: a doorway in the underground chamber under the bungee platform leads down inside the dam to a balcony on the
rem     downstream face. Press B at its valve station to open the spillway (sound only).
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
rem Cheats: turbo Bond, invincibility, infinite ammo. To play without them, delete the GE_CHEATS line below
rem (or remove any of its words: invuln, turbo, ammo).
set GE_CHEATS=invuln,turbo,ammo
start "" "%~dp0ProjectMidas.exe"
