@echo off
rem Colosseum arena co-op: 20 waves for two Bonds. You are Bond 1; Bond 2 is the arena bot (it fights, loots guns, picks up the
rem centre ammo and armour, and revives you). A Bond who runs out of health goes DOWN: stand next to him for 3 s to revive him; he
rem bleeds out after 30 s (back when the wave is cleared). Waves are 1.5x bigger and 1.2x tougher. No best scores in co-op.
rem Needs Data\baserom.u.z64 (see README.txt).
if not exist "%~dp0Data\baserom.u.z64" (
    echo Missing Data\baserom.u.z64 - put your own GoldenEye 007 USA ROM there. See README.txt.
    pause
    exit /b 1
)
set GE_COOP=1
set GE_STAGE=colosseum
set GE_SKIPINTRO=1
set GE_MODE=arena-coop
start "" "%~dp0ProjectMidas.exe"
