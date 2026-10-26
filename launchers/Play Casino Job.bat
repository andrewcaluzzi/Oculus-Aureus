@echo off
rem The heist's second job: Le Grand Casino Janus, Monte Carlo (designs/job2-casino.md). Needs a build with the casino on the levelgen
rem slot (a build dir configured with "-DGE_LEVELGEN_DIR=port/levelgen_casino", e.g. build_win_casino): copy this file next to its
rem ProjectMidas.exe. Walk in from the place, climb the keycard ladder (croupier, shift manager, director), crack the house vault
rem under the Salons Prives, out by the limousine or the loading dock. Needs Data\baserom.u.z64 (see README.txt).
if not exist "%~dp0Data\baserom.u.z64" (
    echo Missing Data\baserom.u.z64 - put your own GoldenEye 007 USA ROM there. See README.txt.
    pause
    exit /b 1
)
set GE_STAGE=levelgen
set GE_SKIPINTRO=1
set GE_MODE=heist
start "" "%~dp0ProjectMidas.exe"
