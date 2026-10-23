# Netplay job 3b: the "everything" input script for the lockstep tests (tests/net_lockstep.sh phase 6).
#   awk -v who=A -f everything.awk > recEA.txt      the host's Bond (player 0)
#   awk -v who=B -f everything.awk > recEB.txt      the guest's Bond (player 1)
# One line per tick, the GE_COOP_LOCAL=replay: format with the job 3b columns:
#   R tick buttons(hex) stick_x stick_y yaw_milli pitch_milli actions(hex) dig_x dig_y flags(hex)
# Port action bits: reload 0x1, use 0x2, prev_weapon 0x20, next_weapon 0x40, crouch_toggle 0x200, crouch_hold 0x400,
# watch_next 0x1000, watch_objectives 0x2000, detonate 0x4000, zoom_in 0x8000, zoom_out 0x10000.
# Pad bits: A 0x8000, B 0x4000, Z 0x2000, START 0x1000, D-pad right 0x0100, L 0x0020, R (aim) 0x0010.
# Record flags: 1 = Escape key, 2 = START came from the Return key alone.
#
# The Bonds do everything the record can carry: walk, strafe and turn; cycle the weapon four times (slappers are in the ring);
# reload; crouch (toggle / hold); aim with zoom; fire; use; detonate (no mines: dropped); open and close the watch three ways
# (START, Escape, watch_objectives) while fire is HELD; and a START from the Return key alone that must not open the watch.
# The watch needs about 125 ticks to open and 35 to close, so the fire and the hits at tick 160 land with both watches open
# (state 5). The harness (net_lockstep.sh phase 6) adds the deaths with
#   GE_COOP_HURT=160:0:0.5,160:1:0.5,215:1:10,350:0:10,410:1:10,430:0:10,460:1:10  GE_COOP_RESPAWN=40
#   tick 160   both Bonds hit with the watch open: nothing happens (the watch gate), no shot is fired either
#   host's Bond:  killed at 350, respawns at 390, killed again at 430
#   guest's Bond: killed at 215, respawns at 255, killed at 410, respawns at 450, killed a third time at 460
function pulse(t, first, every, len) { return t >= first && ((t - first) % every) < len }
BEGIN {
    for (t = 1; t <= 440; t++) {
        btn = 0; sx = 0; sy = 0; yaw = 0; pit = 0; act = 0; dx = 0; dy = 0; fl = 0
        if (who == "A") {
            if (t <= 10) sy = 70
            if (t >= 12 && t <= 13) btn += 0x1000                                 # START: opens the watch
            if (t >= 20 && t <= 172) btn += 0x2000                                # try to fire with the watch open
            if (t >= 150 && t <= 151) act += 0x1000                               # watch_next page
            if (t >= 160 && t <= 161) act += 0x1000
            if (t >= 165 && t <= 166) btn += 0x0100                               # D-pad right in the watch
            if (t == 175) fl += 1                                                 # Escape: closes the watch
            if (t >= 215 && t <= 216) act += 0x1                                  # reload
            if (t >= 225 && t <= 226) act += 0x200                                # crouch
            if (t >= 245 && t <= 246) act += 0x200                                # stand
            if (t >= 215 && t <= 330) { sx = -40; yaw = 1500 }                    # turn while strafing
            if (t >= 260 && t <= 333 && pulse(t, 260, 24, 2)) act += 0x40         # next weapon x4
            if (t >= 335 && t <= 349) btn += 0x2000                               # attack with whatever is in hand (slappers)
            if (t >= 395 && t <= 396) act += 0x1                                  # reload
            if (t >= 400 && t <= 436) btn += 0x0010                               # aim (R)
            if (t >= 405 && t <= 415) act += 0x8000                               # zoom in
            if (t >= 416 && t <= 424) act += 0x10000                              # zoom out
            if (t >= 420 && t <= 430) btn += 0x2000                               # fire
            if (t >= 434 && t <= 435) act += 0x2                                  # use
        } else {
            if (t <= 10) sy = -70
            if (t == 15) fl += 1                                                  # Escape: opens the watch
            if (t >= 25 && t <= 168) btn += 0x2000                                # try to fire with the watch open
            if (t >= 150 && t <= 151) act += 0x1000                               # watch_next page
            if (t >= 160 && t <= 161) act += 0x1000
            if (t >= 170 && t <= 171) act += 0x2000                               # watch_objectives: closes it
            if (t >= 265 && t <= 266) { btn += 0x1000; fl += 2 }                  # START from Return alone: the watch stays shut
            if (t >= 270 && t <= 271) act += 0x4000                               # detonate (nothing to detonate)
            if (t >= 300 && t <= 386 && pulse(t, 300, 28, 2)) act += 0x20         # prev weapon x4
            if (t >= 330 && t <= 350) act += 0x400                                # crouch hold
            if (t >= 300 && t <= 340) { sx = 40; yaw = -1500 }
            if (t >= 395 && t <= 396) act += 0x1                                  # reload
            if (t >= 388 && t <= 408) btn += 0x2000                               # attack with whatever is in hand (slappers)
            if (t >= 415 && t <= 425) { btn += 0x2000; dy = 1; dx = 1 }           # fire, with a digital move
        }
        printf "R %d %x %d %d %d %d %x %d %d %x\n", t, btn, sx, sy, yaw, pit, act, dx, dy, fl
    }
}
