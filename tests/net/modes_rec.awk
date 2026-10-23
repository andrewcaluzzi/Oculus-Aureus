# Writes a GE_COOP_LOCAL / GE_COOP_P2 record file for the modes-in-co-op tests (net_lockstep.sh phases 40-43): neutral input for every tick,
# with the B button (0x4000: the raw N64 B tap, which bondview2.c turns into the Bond's own "use" press) down on the ticks in `taps` and a
# one-tick turn of `yaw` milli-degrees at the ticks in `yaws` ("tick:millidegrees,...").
#   awk -v n=1500 -v taps=110,420 -v yaws=590:180000 -f modes_rec.awk > recA.txt
# Record line: R tick buttons(hex) stickx sticky yaw pitch
BEGIN {
    nt = split(taps, tv, ",")
    for (i = 1; i <= nt; i++) if (tv[i] != "") tap[tv[i]] = 1
    ny = split(yaws, yv, ",")
    for (i = 1; i <= ny; i++) {
        if (yv[i] == "") continue
        split(yv[i], yp, ":")
        yaw[yp[1]] = yp[2]
    }
    for (t = 1; t <= n; t++) printf "R %d %s 0 0 %d 0\n", t, (t in tap) ? "4000" : "0", (t in yaw) ? yaw[t] : 0
}
