@echo off
rem Dam+ west service gallery only (concept 2): stock Dam plus a low maintenance hatch in the west wall of the westernmost
rem underground room (the square room with the first gate pair, at the bottom of the west tower's stair). A long curved
rem gallery inside the wall, a turbine hall with a walkway over the machinery, and a stair up that comes out through the
rem hazard-striped dead end of the tunnel stub at the crest's west end. A flanking route between the crest and the underground.
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
set GE_DAMPLUS=gallery
rem Cheats: turbo Bond, invincibility, infinite ammo. To play without them, delete the GE_CHEATS line below
rem (or remove any of its words: invuln, turbo, ammo).
set GE_CHEATS=invuln,turbo,ammo
start "" "%~dp0ProjectMidas.exe"
