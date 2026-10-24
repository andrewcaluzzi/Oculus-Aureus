@echo off
rem Colosseum arena benchmark, live enemies: no crowd, no corpses; 0 / 25 / 50 / 100 / 200 live soldiers fighting an invulnerable Bond.
rem Five phases of 30 s plus set-up, about 4 minutes, then the game quits by itself. Writes Data\Logs\arena_bench.txt. Needs Data\baserom.u.z64.
if not exist "%~dp0Data\baserom.u.z64" (
    echo Missing Data\baserom.u.z64 - put your own GoldenEye 007 USA ROM there. See README.txt.
    pause
    exit /b 1
)
set GE_STAGE=colosseum
set GE_SKIPINTRO=1
set GE_MODE=arena-bench-live
set GE_CHEATS=invuln
set GE_COLOSSEUM_SLOTS=600
echo Running the live-enemy arena benchmark (about 4 minutes). Leave the mouse and keyboard alone until the window closes.
start "" /wait "%~dp0ProjectMidas.exe"
echo.
echo Finished. The report is here:
echo   %~dp0Data\Logs\arena_bench.txt
echo Please paste its contents back. If it is missing or short, send Data\Logs\ge_direct.log too.
pause
