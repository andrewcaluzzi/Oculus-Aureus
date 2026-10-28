@echo off
rem The Janus Job, the casino first: opens the heist's start menu over Le Grand Casino Janus (Monte Carlo), the heist's second job, in
rem the same exe as the bank (the levelgen slot's level is chosen at runtime: GE_LEVELGEN=casino). Croupier, shift manager, director:
rem crack the house vault under the Salons Prives, out by the limousine or the loading dock. The bank is one card away in the menu.
rem Needs Data\baserom.u.z64 (see README.txt).
if not exist "%~dp0Data\baserom.u.z64" (
    echo Missing Data\baserom.u.z64 - put your own GoldenEye 007 USA ROM there. See README.txt.
    pause
    exit /b 1
)
set GE_STAGE=levelgen
set GE_SKIPINTRO=1
set GE_LEVELGEN=casino
set GE_MODE=heist-menu
start "" "%~dp0ProjectMidas.exe"
