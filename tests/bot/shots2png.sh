#!/bin/sh
# Convert the GE_SHOT BMPs in a directory to PNG (all, or the last N): sh tests/bot/shots2png.sh <dir> [N]
HERE=$(cd "$(dirname "$0")" && pwd)
DIR=$1
N=${2:-1000}
for f in $(ls "$DIR"/*.bmp 2>/dev/null | tail -n "$N"); do
    python3 "$HERE/../bmp2png.py" "$f" "${f%.bmp}.png" && rm -f "$f"
done
ls "$DIR"/*.png 2>/dev/null | tail -n "$N"
