#!/bin/sh
# The scream crowd (crowd_screams) from build_pc, inside the goldeneye Docker image with Xvfb running on :99
# (Xvfb :99 -screen 0 1280x720x24 +extension GLX). Two runs:
#   P  screams-preview.lua: a kill, a wave clear and a boss kill burst, nothing else happening -> screams_p.raw
#      (SDL's disk driver: the format is in the "ge: audio: opened" line, 22050 Hz s16 stereo). For a clean listen set
#      music_volume=0 in build_pc/midas.ini first.
#   G  crowd-react.lua with GE_CROWD_TEST_SCREAMS=1 GE_CHEATS=invuln: kills, the boss kill and the wave clears screaming over the
#      enemies' gunfire; D  the same with GE_CROWD_TEST_DIE=1: armed wave 3 shoots Bond down (the death laugh). "ge: sfx: player full"
#      counts the presentation sounds that gave up or made way for a game sound, and the game sounds that preempted another
#      game sound (stock behaviour, not caused by the crowd).
# The .raw files are the game's own audio (from the ROM): never commit them or put them in Data/Sounds.
cd /home/dev/project/build_pc || exit 1
cp ../tests/arena_crowd/screams-preview.lua Data/Scripts/modes/screams-preview.lua
cp ../tests/arena_crowd/crowd-react.lua Data/Scripts/modes/crowd-react.lua
export DISPLAY=:99 GE_STAGE=colosseum GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_SOUNDLOG=1 SDL_AUDIODRIVER=disk
GE_MODE=screams-preview GE_FRAMES=1500 SDL_DISKAUDIOFILE=screams_p.raw ./ProjectMidas > screams_p.log 2>&1
echo "P exit $?"
GE_MODE=crowd-react GE_CROWD_TEST_SCREAMS=1 GE_CHEATS=invuln GE_FRAMES=8000 SDL_DISKAUDIOFILE=screams_g.raw ./ProjectMidas > screams_g.log 2>&1
echo "G exit $?"
GE_MODE=crowd-react GE_CROWD_TEST_SCREAMS=1 GE_CROWD_TEST_DIE=1 GE_FRAMES=6000 SDL_DISKAUDIOFILE=screams_d.raw ./ProjectMidas > screams_d.log 2>&1
echo "D exit $?"
for r in p g d; do
  echo "== $r"
  grep -a "ge: audio: opened\|ge: arena: crowd\|fire \|ge: sfx: dropped\|ge: sfx: player full\|rror" screams_$r.log | grep -v ALSA | head -60
done
