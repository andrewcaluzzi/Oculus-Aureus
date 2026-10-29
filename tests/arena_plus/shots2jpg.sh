#!/bin/sh
# Turns the plus-shots BMPs (names in the order the scenario took them) into small JPGs in build_tests/plus/jpg, then deletes the BMPs.
S=/home/dev/project/build_tests/plus/shots
J=/home/dev/project/build_tests/plus/jpg
mkdir -p $J
set -- $(ls $S/shot_*.bmp | sort)
for name in hud powerup crate cards; do
  [ -n "$1" ] || break
  convert "$1" -resize 960x -quality 80 $J/$name.jpg
  rm -f "$1"
  shift
done
ls -l $J
