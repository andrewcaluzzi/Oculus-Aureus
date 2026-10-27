#!/bin/sh
# the menu test's BMP shots -> JPG in build_tests/amenu/jpg (the order of amenu_drive.lua's "shot" steps), then the BMPs go
S=/home/dev/project/build_tests/amenu/shots
O=/home/dev/project/build_tests/amenu/jpg
mkdir -p $O
rm -f $O/*.jpg
set -- menu-main menu-advanced menu-advanced-changed menu-main-custom postgame-win postgame-fell menu-after
for f in $(ls $S/*.bmp | sort); do
  convert "$f" -quality 85 "$O/$1.jpg"
  shift
done
rm -f $S/*.bmp
ls -la $O
