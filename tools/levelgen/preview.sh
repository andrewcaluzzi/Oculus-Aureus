#!/bin/sh
# Preview an edited layout in the game (tools/levelgen/editor/README.md): generate it, build it into build_preview/ (its own build dir,
# so port/levelgen/ and build_pc/ are untouched), boot the game headless in Docker on it (GE_STAGE=levelgen) and save a walk-through:
# Bond's start, then one shot per room (the rooms you name, or the layout's largest).
#
#   tools/levelgen/preview.sh <layout.json> [room ...]          from a Linux shell with Docker (on Windows: WSL, in the repo)
#
# Output: build_preview/shots/<NN>_<name>.png (and the generator's and the game's logs next to them), build_preview/footprints.json
# (drop it on the editor to check prop footprints there too). The first run configures and builds build_preview (minutes); later
# runs rebuild only the level. Needs the Docker image (README.md "Building", default `goldeneye`; IMAGE=... to change), the extracted
# assets and baserom.u.z64 in the repo root. JOBS (default 2) sets the build's -j. The container is removed at the end.
set -e
[ $# -ge 1 ] || { sed -n '2,12p' "$0"; exit 2; }
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
IMAGE=${IMAGE:-goldeneye}
JOBS=${JOBS:-2}
CONTAINER=${PREVIEW_CONTAINER:-levelgen-preview-$$}
P=/home/dev/project
OUT=$ROOT/build_preview
LAYOUT=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
shift
mkdir -p "$OUT/shots"
cp "$LAYOUT" "$OUT/layout.json"
dock() { docker run --rm -v "$ROOT":$P "$IMAGE" "$@"; }

echo "== generate $(basename "$LAYOUT")"
if ! dock python3 $P/tools/gen_level.py $P/build_preview/layout.json --out $P/build_preview/levelgen > "$OUT/gen_level.log" 2>&1; then
    tail -20 "$OUT/gen_level.log"; echo "preview: gen_level refused the layout (full log: build_preview/gen_level.log)"; exit 1
fi
tail -2 "$OUT/gen_level.log"
dock python3 $P/tools/levelgen/editor/footprints.py $P/build_preview/layout.json $P/build_preview/footprints.json || true

echo "== build build_preview"
if [ ! -f "$OUT/CMakeCache.txt" ]; then
    dock cmake -S $P -B $P/build_preview -DCMAKE_BUILD_TYPE=Debug -DGE_VIDEO=ON -DSDL2_LINUX_ROOT=/home/dev/sdl2-linux-i686 \
        -DGE_LEVELGEN_DIR=build_preview/levelgen > "$OUT/configure.log"
fi
dock cmake --build $P/build_preview -j "$JOBS" > "$OUT/build.log" 2>&1 || { tail -30 "$OUT/build.log"; exit 1; }
[ -f "$OUT/Data/baserom.u.z64" ] || cp "$ROOT/baserom.u.z64" "$OUT/Data/baserom.u.z64"
cp "$ROOT/tests/levelgen/levelgen_preview_shots.lua" "$OUT/Data/Scripts/modes/levelgen_preview_shots.lua"
{
    echo "-- written by tools/levelgen/preview.sh"
    printf 'PREVIEW = { n = %s, rooms = {' "${SHOTS:-5}"
    for r in "$@"; do printf ' "%s",' "$r"; done
    echo ' } }'
} > "$OUT/Data/Scripts/modes/levelgen_preview_cfg.lua"

echo "== run (headless, Xvfb in container $CONTAINER)"
docker run -d --name "$CONTAINER" -v "$ROOT":$P "$IMAGE" sleep 3600 > /dev/null
trap 'docker rm -f "$CONTAINER" > /dev/null 2>&1 || true' EXIT
docker exec "$CONTAINER" sh -c 'command -v Xvfb' > /dev/null 2>&1 || \
    docker exec -u root "$CONTAINER" sh -c 'apt-get update -qq > /dev/null && apt-get install -y -qq xvfb > /dev/null'
docker exec -d "$CONTAINER" Xvfb :99 -screen 0 1280x720x24 +extension GLX
sleep 2
rm -f "$OUT"/shots/*.png "$OUT"/shots/*.bmp
docker exec -w $P/build_preview/shots -e DISPLAY=:99 -e GE_MODE=levelgen_preview_shots -e GE_STAGE=levelgen -e GE_SKIPINTRO=1 \
    -e GE_NOINISAVE=1 -e GE_SEED=7 -e GE_FULLHEIGHT=0 -e GE_EDGEINSET=1 -e GE_FRAMES=${GE_FRAMES:-9000} "$CONTAINER" \
    timeout 900 ../ProjectMidas > "$OUT/shots/game.log" 2>&1 || true
grep -E "LEVELGEN|ge: lua: .*error" "$OUT/shots/game.log" || true

echo "== screenshots"
names=$(grep -o "LEVELGEN SHOT [^ ]*" "$OUT/shots/game.log" | awk '{print $3}')
k=0
for b in $(ls "$OUT"/shots/shot_*.bmp 2>/dev/null | sort); do
    k=$((k + 1))
    n=$(echo "$names" | sed -n "${k}p")
    png=$(printf '%s/shots/%02d_%s.png' "$OUT" "$k" "${n:-shot}")
    dock python3 $P/tests/bmp2png.py "$P/build_preview/shots/$(basename "$b")" "$P/build_preview/shots/$(basename "$png")" && rm -f "$b"
    echo "$png"
done
[ $k -gt 0 ] || { echo "preview: no screenshots (see build_preview/shots/game.log)"; exit 1; }
