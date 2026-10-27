@echo off
rem Colosseum arena: opens the arena's start menu (presets, difficulty, every setting; your last choices are kept in
rem Data\Saves\arena_menu.ini). A run ends on the post-game: Retry, Change settings, Quit. The other "Play Arena (...)" launchers
rem skip the menu and start their preset at once (they end on the post-game too). Needs Data\baserom.u.z64 (see README.txt).
if not exist "%~dp0Data\baserom.u.z64" (
    echo Missing Data\baserom.u.z64 - put your own GoldenEye 007 USA ROM there. See README.txt.
    pause
    exit /b 1
)
set GE_STAGE=colosseum
set GE_SKIPINTRO=1
set GE_MODE=arena-menu
start "" "%~dp0ProjectMidas.exe"
