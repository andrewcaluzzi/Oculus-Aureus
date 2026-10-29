#!/bin/sh
# Smoke of the arena on one stock multiplayer map in the build container (Xvfb on :99, build_pc): a short run (tests/arena_maps/maps-smoke.lua),
# fast-forward, Bond invulnerable. usage: run.sh <map: temple|complex|caves|library|stack|basement|facility> [game frames] ["GE_X=y ..."]
# Log: build_tests/maps/<map>.log. Prints the numbers the report wants: per wave the enemies the wave asked for and the ones that appeared,
# the stuck enemies, the end, the frame time.
MAP=${1:-temple}
P=/home/dev/project
B=$P/build_pc
OUT=$P/build_tests/maps
M=$B/Data/Scripts/modes
mkdir -p $OUT/shots
for f in mode_arena:arena arena_flow:_arena_flow arena_menu_defs:_arena_menu_defs mode_arena_scores:_arena_scores mode_arena_crowd:_arena_crowd \
         mode_arena_mutators:_arena_mutators mode_arena_elites:_arena_elites arena_polish:_arena_polish arena_theme:_arena_theme \
         arena_coop:_arena_coop arena_plus:_arena_plus ui_menu:_ui_menu arena_maps:_arena_maps arena_whacky:_arena_whacky; do
  cp "$P/tools/${f%%:*}.lua" "$M/${f##*:}.lua"
done
T=maps-smoke-$MAP
{ echo "MAPSMOKE_MAP = \"$MAP\""; cat $P/tests/arena_maps/maps-smoke.lua; } > $M/$T.lua
rm -f $B/Data/Saves/eeprom.bin
export DISPLAY=:99 GE_MODE=$T GE_STAGE=mp$MAP GE_NOINTRO=1 GE_STAGE_AFTER=1 GE_NOINISAVE=1 GE_FIXEDTICK=1 GE_TICKHZ=60 GE_SEED=7 GE_NOMSGBOX=1
export GE_FRAMES=${2:-30000} GE_FULLHEIGHT=0 GE_EDGEINSET=1 GE_CHEATS=invuln GE_NOSCORESAVE=1 GE_SCOREFILE=$OUT/scores.ini GE_UIMENU_DIR=$OUT GE_NOMENUSAVE=1
export GE_ARENA_TESTKEYS=0
if [ "${FAST:-1}" = 1 ]; then export GE_NOSLEEP=1 GE_CLOCKSCALE=20; fi
for kv in $3; do export "$kv"; done
L=$OUT/$MAP.log
cd $OUT/shots
S=$(date +%s)
timeout ${TIMEOUT:-1500} $B/ProjectMidas > $L 2>&1
echo "exit $?" >> $L
E=$(date +%s)
rm -f $M/$T.lua
echo "== $MAP: $((E - S)) s wall for $GE_FRAMES frames = $(( (E - S) * 1000 / GE_FRAMES )) ms a frame (Xvfb, software GL, drawing on)"
grep -a "mpnav: .*waypoints\|ge: arena: MAP " $L | head -4
awk '/ge: arena: wave [0-9]+ .*: [0-9]+ enemies/ { if (w != "") printf "wave %s asked %s, appeared %d\n", w, ask, n; s=$0; sub(/.*ge: arena: wave /, "", s); w=s; sub(/ .*/, "", w); ask=$0; sub(/ enemies.*/, "", ask); sub(/.*: /, "", ask); n=0 } /MAPSMOKE spawn/ { n++ } END { if (w != "") printf "wave %s asked %s, appeared %d\n", w, ask, n }' $L
grep -a "MAPSMOKE STUCK\|MAPSMOKE LATE\|ge: arena: STUCK" $L | head -12
grep -a "ge: arena: wave [0-9]* cleared" $L | sed 's/; economy.*//' | head -12
grep -a "MAPSMOKE summary" $L | tail -1
grep -a "ge: arena: WON\|ge: arena: OVER\|ge: arena: MAP .*stuck\|bond died\|^exit " $L
if grep -aq "ge: lua: .*error\|A7 error\|PLUS error\|stuck check error\|setup error" $L; then echo "LUA ERROR:"; grep -a "ge: lua: .*error\|A7 error\|PLUS error\|stuck check error\|setup error" $L | head -5; fi
