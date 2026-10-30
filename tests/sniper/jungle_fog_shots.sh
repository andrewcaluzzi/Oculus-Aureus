#!/bin/sh
# Hidden Windows run of jungle_fog_shots.lua (Git Bash; no window: GE_HIDDEN=1). usage: jungle_fog_shots.sh <build dir> <out dir> <label> [start_m,full_m] [old]
#   old = GE_JUNGLE_OLDFOG=1 (the first prototype's flat haze). BMPs land in <out dir>/<label>/ (convert them in the container: see the report).
B=${1:?build dir}
OUT=${2:?out dir}
LBL=${3:?label}
FOG=$4
OLD=$5
M=$B/Data/Scripts/modes
mkdir -p "$OUT/$LBL"
rm -f "$OUT/$LBL"/*.bmp
if [ -n "$FOG" ]; then echo "JFOG = { ${FOG%%,*}, ${FOG##*,} }" > "$M/sniper-jfshots.lua"; else : > "$M/sniper-jfshots.lua"; fi
cat "$(dirname "$0")/jungle_fog_shots.lua" >> "$M/sniper-jfshots.lua"
export GE_HIDDEN=1 GE_MODE=sniper-jfshots GE_STAGE=levelgen GE_LEVELGEN=jungle GE_NOINTRO=1 GE_STAGE_AFTER=1 GE_NOINISAVE=1 GE_SEED=7
export GE_FRAMES=${FRAMES:-6000} GE_CHEATS=invuln GE_NOMENUSAVE=1 GE_NOMSGBOX=1
if [ -n "$WIN" ]; then export GE_WINDOW=$WIN; fi   # (WIN=2560x1440 for the scope crop)
if [ -n "$OLD" ]; then export GE_JUNGLE_OLDFOG=1; fi
cd "$OUT/$LBL"
"$B/ProjectMidas.exe" > "$OUT/$LBL.log" 2>&1
echo "exit $?"
rm -f "$M/sniper-jfshots.lua"
grep -a "JFSHOT\|ge: fog\|lua: .*error\|stats\|fps" "$OUT/$LBL.log" | cut -c1-200 | head -40
