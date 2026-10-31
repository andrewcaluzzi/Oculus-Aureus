#!/bin/sh
# Dam+ shots at the stock walls redrawn around our doorways: a bullet must stop on the wall (a bg hit, which is what places the
# impact decal and the spark: chrprop.c, after the "bghit:" dump), not pass into the void behind it.
# The player bot plays tests/damplus/shots.plan (warp to each spot, aim, four single shots with Z on the pad) with GE_DUMP_BOND=1.
# Agent, invulnerable, no replay (the plan warps). $1 = GE_DAMPLUS (default 1; 0 = stock Dam: the same shots at the intact walls,
# targets A-F only).
# Each shot is classed by where its ray crosses the wall's plane: "wall" = the hit is on the plane (within 30), "doorway" = it
# went on through the doorway's opening (lateral and height inside it: legal, the spread is a few degrees), "front" = it hit
# something in front of the wall, "THROUGH" = it went on through the wall outside the opening, "VOID" = no bg hit at all.
# PASS: no THROUGH and no VOID shot, and every target has at least one "wall" shot.
# Usage (container, Xvfb :99): sh tests/damplus/shots.sh [1|0]
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
DP=${1:-1}
export TAG=shots$DP MAXSECS=${MAXSECS:-60} REPLAY=0 \
       EXTRA="GE_PLAYERBOT_PLAN=$ROOT/tests/damplus/shots.plan GE_DAMPLUS=$DP GE_CHEATS=invuln,ammo GE_DUMP_BOND=1 $EXTRA"
OUT=${BOTOUT:-$ROOT/build_tests/bot}/dam.d0
sh "$ROOT/tests/bot/level.sh" dam 0 > /dev/null
LOG=$OUT/shots$DP.log
awk -v dp=$DP '
  function setw(k, cx, cz, ux, uz, w, top) { WX[k] = cx; WZ[k] = cz; UX[k] = ux; UZ[k] = uz; NX[k] = uz; NZ[k] = -ux; SPAN[k] = w; TOP[k] = top }
  BEGIN {
    split("A B C D E F G H", name, " ");
    # wall planes: a point C on the doorway edge, the unit vector u along the wall (C -> D), the opening span and its top y
    setw(1, 15785, 10931, -0.607, 0.796, 260, 193); setw(2, 15785, 10931, -0.607, 0.796, 260, 193);
    setw(3, -5885, -2615, 0.34, 0.94, 150, -457);   setw(4, -5885, -2615, 0.34, 0.94, 150, -457);
    setw(5, 98, -3424, 1, 0, 160, -457);            setw(6, 98, -3424, 1, 0, 160, -457);
    # the face: through the balcony inner edge line, 10 out (frame (900, 255) -> (2100, 435)); the passage at 657..819 along it
    setw(7, 998, -3169, 0.989, 0.148, 0, -1192);    setw(8, 998, -3169, 0.989, 0.148, 0, -1192);
    P0[7] = 657; P0[8] = 657; SPAN[7] = 819; SPAN[8] = 819;
    want = (dp == 0) ? 6 : 8;
  }
  /playerbot: warp: no pad/ { k++; BX = "" }
  /^bg/ && BX == "" { next }
  /playerbot: warp to pad/ { k++; s = $0; sub(/.* pos /, "", s); split(s, b, ","); BX = b[1]; BY = b[2]; BZ = b[3] }
  /^bgmiss:/ && k >= 1 && k <= want { void[k]++; printf "DPSHOTS %s: shot into the VOID (%s)\n", name[k], $0 }
  /^bghit:/ && k >= 1 && k <= want {
    s = $0; sub(/.*world=\(/, "", s); sub(/\).*/, "", s); split(s, h, ","); rm = $0; sub(/.*room=/, "", rm);
    # signed distance from the wall plane (normal: either sign, taken towards Bond)
    nx = NX[k]; nz = NZ[k];
    if ((BX - WX[k]) * nx + (BZ - WZ[k]) * nz < 0) { nx = -nx; nz = -nz }
    dh = (h[1] - WX[k]) * nx + (h[3] - WZ[k]) * nz;
    db = (BX - WX[k]) * nx + (BZ - WZ[k]) * nz;
    if (dh > 30) cls = "front";
    else if (dh >= -30) { cls = "wall"; onwall[k]++ }
    else {
      t = db / (db - dh); cx = BX + t * (h[1] - BX); cy = BY + t * (h[2] - BY); cz = BZ + t * (h[3] - BZ);
      u = (cx - WX[k]) * UX[k] + (cz - WZ[k]) * UZ[k];
      lo = (k >= 7) ? P0[k] : 0;
      if (u >= lo - 5 && u <= SPAN[k] + 5 && cy <= TOP[k] + 5) cls = "doorway"; else { cls = "THROUGH"; thru[k]++ }
    }
    printf "DPSHOTS %s: hit %s room %s, %.0f %s the wall plane: %s\n", name[k], s, rm, dh < 0 ? -dh : dh,
           dh < 0 ? "behind" : "in front of", cls;
  }
  END {
    bad = 0;
    for (i = 1; i <= want; i++) {
      if (void[i] || thru[i]) { printf "DPSHOTS %s: FAIL (%d through the wall, %d into the void)\n", name[i], thru[i], void[i]; bad++ }
      else if (!onwall[i]) { printf "DPSHOTS %s: FAIL (no shot stopped on the wall)\n", name[i]; bad++ }
    }
    print "DPSHOTS RESULT " (bad == 0 ? "pass" : "fail");
  }' "$LOG"
