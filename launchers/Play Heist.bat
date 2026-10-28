@echo off
rem The Janus Job: opens the heist's start menu over the Janus bank (St Petersburg branch, the generated level on the levelgen slot):
rem pick the job (the bank, or the casino: the menu moves there), the difficulty and every setting in Q BRANCH (your last choices are
rem kept in Data\Saves\heist_menu.ini, the bests in Data\Saves\heist_bests.ini), then BEGIN THE JOB: the planning board, then in.
rem The job ends on the debrief: AGAIN, NEW JOB, RETURN TO MI6. Needs Data\baserom.u.z64 (see README.txt).
if not exist "%~dp0Data\baserom.u.z64" (
    echo Missing Data\baserom.u.z64 - put your own GoldenEye 007 USA ROM there. See README.txt.
    pause
    exit /b 1
)
set GE_STAGE=levelgen
set GE_SKIPINTRO=1
set GE_LEVELGEN=bank
set GE_MODE=heist-menu
start "" "%~dp0ProjectMidas.exe"
