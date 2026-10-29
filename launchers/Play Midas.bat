@echo off
setlocal
rem The main menu: Arena, Heist, GoldenEye 007 (the original game's Files screen) and Settings, over the Control room's screen wall.
rem Arena and Heist open their own start menus from here (and their MAIN MENU button comes back). Needs Data\baserom.u.z64 (see README.txt).
if not exist "%~dp0Data\baserom.u.z64" (
    echo Missing Data\baserom.u.z64 - put your own GoldenEye 007 USA ROM there. See README.txt.
    pause
    exit /b 1
)
set GE_STAGE=control
set GE_SKIPINTRO=1
set GE_MODE=hub
start "" "%~dp0ProjectMidas.exe"
