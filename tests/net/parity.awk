# Netplay job 4: the "pad player against mouse player" input scripts (tests/net_lockstep.sh phase 15).
#   awk -v who=A -f parity.awk > recPA.txt      the host's Bond: a gamepad player
#   awk -v who=B -f parity.awk > recPB.txt      the guest's Bond: a mouse player
# One line per tick, the GE_COOP_LOCAL=replay: format:
#   R tick buttons(hex) stick_x stick_y yaw_milli pitch_milli actions(hex) dig_x dig_y flags(hex)
# A gamepad player (port-driven pad, right stick): the Move stick walks and strafes, the right stick turns and pitches in SMALL, smooth
# steps (the right stick becomes look degrees per tick, a few thousandths of a degree up to ~2.4 degrees), a digital D-pad move, and the
# analog zoom of a trigger carried in the record flags (PORT_NETF_ZIN / ZOUT: bits 2..5 and 6..9) with the zoom action bit.
# A mouse player: sharp one-tick flicks of several degrees in yaw and pitch, no stick turning, no analog zoom.
# Pad bits: R (aim) 0x0010, Z (fire) 0x2000. Port action bits: zoom_in 0x8000, zoom_out 0x10000.
function pulse(t, first, every, len) { return t >= first && ((t - first) % every) < len }
function ipart(x) { return (x < 0) ? -int(-x) : int(x) }
BEGIN {
    for (t = 1; t <= 240; t++) {
        btn = 0; sx = 0; sy = 0; yaw = 0; pit = 0; act = 0; dx = 0; dy = 0; fl = 0
        if (who == "A") {
            if (t <= 90) sy = 70
            if (t >= 20 && t <= 140) { yaw = ipart(2400 * sin(t / 11.0)); pit = ipart(700 * sin(t / 17.0)) }   # smooth right-stick look
            if (t >= 40 && t <= 70) sx = 50                                           # strafe
            if (t >= 95 && t <= 110) { dx = 1; dy = 1 }                               # digital move
            if (t >= 80 && t <= 200) btn += 0x0010                                    # aim (R)
            if (t >= 90 && t <= 119) { act += 0x8000; fl += 5 * 4 }                   # zoom in, trigger a third of the way: level 5/15
            if (t >= 120 && t <= 149) { act += 0x8000; fl += 15 * 4 }                 # zoom in, fully pulled: level 15/15
            if (t >= 150 && t <= 179) { act += 0x10000; fl += 3 * 64 }                # zoom out, level 3/15
            if (t >= 180 && t <= 199) act += 0x8000                                   # zoom in on a key: the action bit alone (full rate)
            if (pulse(t, 100, 30, 3)) btn += 0x2000                                   # fire
        } else {
            if (t <= 60) sy = -70
            if (pulse(t, 25, 9, 1)) { yaw = (int(t / 9) % 2 == 0) ? 9000 : -7000; pit = (int(t / 9) % 3 == 0) ? 6000 : -4500 }   # mouse flicks
            if (pulse(t, 70, 25, 4)) btn += 0x2000                                    # fire
            if (t >= 120 && t <= 150) btn += 0x0010                                   # aim (R)
            if (t >= 61 && t <= 100) sx = -45
        }
        printf "R %d %x %d %d %d %d %x %d %d %x\n", t, btn, sx, sy, yaw, pit, act, dx, dy, fl
    }
}
