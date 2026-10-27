@echo off
rem Colosseum arena benchmark, late waves: the state of waves 15-20 (the default crowd: 150 real spectators + 1000 impostors,
rem 150 corpses, 40 live enemies), then 7 phases of 8 s that each leave one kind of thing out of the picture, so the report says
rem what each costs on this PC. About 1.5 minutes, then the game quits by itself. Writes Data\Logs\arena_bench.txt.
rem Needs Data\baserom.u.z64 (see README.txt).
if not exist "%~dp0Data\baserom.u.z64" (
    echo Missing Data\baserom.u.z64 - put your own GoldenEye 007 USA ROM there. See README.txt.
    pause
    exit /b 1
)
set GE_STAGE=colosseum
set GE_SKIPINTRO=1
set GE_MODE=arena-bench
set GE_BENCH_LATE=1
set GE_CHEATS=invuln
echo Running the late-wave arena benchmark (about 1.5 minutes). Leave the mouse and keyboard alone until the window closes.
start "" /wait "%~dp0ProjectMidas.exe"
echo.
echo Finished. The report is here:
echo   %~dp0Data\Logs\arena_bench.txt
echo Please paste its contents back (the summary table at the end is enough). If it is missing or short, send Data\Logs\ge_direct.log too.
pause
