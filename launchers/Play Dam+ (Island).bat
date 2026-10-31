@echo off
rem Dam+ island lookout only (concept 3): stock Dam plus a launch across the lake. On the west quay, walk to the end of the
rem pier that sticks out into the lake and press B at the call box: the launch takes you to Rare's unused island (press B at
rem the call box on the island quay to come back). A steel stair now climbs to the hut's balcony and upper floor, where three
rem snipers keep watch on the dam and a sniper rifle waits by the ladder hole. Look back at the dam from up there.
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
set GE_DAMPLUS=island
rem Cheats: turbo Bond, invincibility, infinite ammo. To play without them, delete the GE_CHEATS line below
rem (or remove any of its words: invuln, turbo, ammo).
set GE_CHEATS=invuln,turbo,ammo
start "" "%~dp0ProjectMidas.exe"
