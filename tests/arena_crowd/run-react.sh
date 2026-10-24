#!/bin/sh
# Crowd reactions (crowd-react.lua) from build_pc, inside the goldeneye Docker image with Xvfb running on :99
# (Xvfb :99 -screen 0 1280x720x24 +extension GLX). Three runs:
#   A  GE_CHEATS=invuln: kills, multi-kills, headshots, the boss kill and the wave clears; the arena is won
#   B  GE_CROWD_TEST_DIE=1, no cheat: the enemies shoot Bond down (big hit, low health, the death boo)
#   C  GE_CROWD_TEST_MISSING=1: the kill reaction names a sound file that does not exist (skipped)
# The audio goes to SDL's disk driver (react_a.raw / react_b.raw, 22050 Hz s16 stereo) and GE_SOUNDLOG=1 logs every crowd sound.
# For the sound path, put TEST tones (never recordings, never committed) in Data/Sounds first, e.g. cheer_small1.wav, cheer_big1.wav,
# boo1.wav; without any the reactions still run and log "sound none".
cd /home/dev/project/build_pc || exit 1
cp ../tests/arena_crowd/crowd-react.lua Data/Scripts/modes/crowd-react.lua
export DISPLAY=:99 GE_MODE=crowd-react GE_STAGE=colosseum GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_SOUNDLOG=1 SDL_AUDIODRIVER=disk
GE_CHEATS=invuln GE_FRAMES=14000 SDL_DISKAUDIOFILE=react_a.raw ./ProjectMidas > react_a.log 2>&1
echo "A exit $?"
GE_CROWD_TEST_DIE=1 GE_FRAMES=9000 SDL_DISKAUDIOFILE=react_b.raw ./ProjectMidas > react_b.log 2>&1
echo "B exit $?"
GE_CROWD_TEST_MISSING=1 GE_CHEATS=invuln GE_FRAMES=1500 SDL_DISKAUDIOFILE=react_c.raw ./ProjectMidas > react_c.log 2>&1
echo "C exit $? (the kill reaction names a missing file: it logs sound none)"
for r in a b c; do
  echo "== $r"
  grep -a "ge: arena: crowd\|ge: arena: WON\|ge: arena: OVER\|ge: test:\|ge: sound: play\|ge: sound: voice\|pool\|rror\|WARN" react_$r.log | grep -v ALSA | head -80
done
