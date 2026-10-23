#!/usr/bin/env python3
"""Netplay soak fuzzer: from a seed, write two input record files and a scenario environment.

  net_soak_gen.py <seed> <stage> <ticks> <outdir>

Writes into <outdir>:
  recA.txt   the host's Bond (player 0), recB.txt the guest's Bond (player 1), in the GE_COOP_LOCAL=replay: format
             R tick buttons(hex) stick_x stick_y yaw_milli pitch_milli actions(hex) dig_x dig_y flags(hex)
  env.txt    one KEY=VALUE per line (no spaces in values): GE_MODS, the kills, the gives, the session commands,
             the teleports / door toggles, the respawn delay ...  net_soak.sh passes these to both instances
  prof.txt   "<side> <width> <height> <widescreen> <hfov> <auto_aim> <look_ahead>" for the host and the guest: the windows
  plan.txt   a human-readable list of what the scenario does (for the report and for reproducing by hand)

Everything is derived from the seed with python's random.Random, so a seed on a stage always gives the same files.
Inputs are random but plausible: walking with turning, strafing, backing up, door rushes (forward + use pulses),
mouse-style flicks and smooth pad-style turning, bursts of fire, weapon cycling (the slappers are in the ring), reload,
crouch (toggle and hold), aim with zoom, the watch opened and closed three ways (START, Escape, watch_objectives),
a Return-only START, and a rare detonate.

Record bits (tests/net/everything.awk): pad A 0x8000 B 0x4000 Z(fire) 0x2000 START 0x1000 D-right 0x0100 L 0x20 R(aim) 0x10;
port actions reload 0x1 use 0x2 prev_weapon 0x20 next_weapon 0x40 crouch_toggle 0x200 crouch_hold 0x400 watch_next 0x1000
watch_objectives 0x2000 detonate 0x4000 zoom_in 0x8000 zoom_out 0x10000; flags 1 = Escape key, 2 = START from Return alone.
"""
import math
import os
import random
import sys

# Dam waypoints (x y z) from net_roomwalk.sh: the start tunnel, the gate end, the dam road, the plant and the tunnels under it.
DAM_WPS = [
    (20108, 160, 16898), (19500, 160, 16900), (18892, 101, 17660), (17223, 250, 19560), (16222, 101, 20309),
    (13756, 101, 17454), (15729, 101, 5085), (14779, 102, 1905),
    (13598, -434, 2114), (8440, -440, -1117), (1712, -434, -3343),
]

# weapon names `give` knows (netplay.md); all give ammo with the item
GUNS = ["kf7", "pp7", "klobb", "dd44", "zmg", "d5k", "phantom", "ar33", "rcp90", "sniper", "cougar", "wppk", "knife", "goldengun"]

STAGES = {
    # spawn: most guards one command may ask for / the live arena guards to keep under (spare chr slots of the stage)
    "dam": {"spawn_max": 3, "arena_cap": 6, "key": False},
    "depot": {"spawn_max": 4, "arena_cap": 8, "key": True},
    "surface": {"spawn_max": 3, "arena_cap": 6, "key": False},
    "flatland": {"spawn_max": 10, "arena_cap": 36, "key": False},
}

# windows: small, to keep llvmpipe cheap (width height widescreen hfov_wide)
WINDOWS = [(320, 240, 0, 90), (320, 240, 0, 110), (400, 225, 1, 70), (400, 225, 1, 90), (352, 198, 1, 100), (320, 180, 1, 120),
           (384, 288, 0, 60), (480, 200, 1, 100)]


def clamp(v, lo, hi):
    return lo if v < lo else hi if v > hi else v


class Rec:
    """One player's per-tick record, built by overlaying segments."""

    def __init__(self, n):
        self.n = n
        self.btn = [0] * (n + 2)
        self.sx = [0] * (n + 2)
        self.sy = [0] * (n + 2)
        self.yaw = [0] * (n + 2)
        self.pit = [0] * (n + 2)
        self.act = [0] * (n + 2)
        self.dx = [0] * (n + 2)
        self.dy = [0] * (n + 2)
        self.fl = [0] * (n + 2)

    def lines(self):
        out = []
        for t in range(1, self.n + 1):
            out.append("R %d %x %d %d %d %d %x %d %d %x" % (t, self.btn[t], self.sx[t], self.sy[t], self.yaw[t], self.pit[t],
                                                            self.act[t], self.dx[t], self.dy[t], self.fl[t]))
        return out


def gen_player(rng, n, who):
    r = Rec(n)
    t = 1
    # a short walk out of the start spot, like the recorded sessions
    stat = {"seg": 0}
    while t <= n:
        kind = rng.choices(["walk", "sweepfire", "doorrush", "flick", "backpedal", "idle", "strafe", "spin"],
                           weights=[22, 18, 20, 10, 8, 5, 10, 7])[0]
        ln = rng.randint(20, 110)
        e = min(n, t + ln - 1)
        stat["seg"] += 1
        turn = rng.choice([0, 0, 1, -1]) * rng.randint(150, 2400)       # smooth (pad-style) turn per tick, milli-degrees
        wob = rng.random() < 0.5
        for k in range(t, e + 1):
            if kind == "walk":
                r.sy[k] = rng.choice([70, 70, 55, 40]) if k == t else r.sy[k - 1]
                r.yaw[k] = int(turn * (1.0 + 0.4 * math.sin(k / 9.0))) if wob else turn
            elif kind == "sweepfire":
                r.sy[k] = rng.choice([0, 0, 30, -30]) if k == t else r.sy[k - 1]
                r.yaw[k] = int(1300 * math.sin((k - t) / 7.0)) + turn // 3
                r.pit[k] = int(500 * math.sin((k - t) / 11.0))
            elif kind == "doorrush":
                r.sy[k] = 70
                r.yaw[k] = rng.randint(-250, 250)
                if (k - t) % rng.randint(9, 14) == 0:
                    r.act[k] |= 0x2                                       # use: open the door in front
                    if k + 1 <= n:
                        r.act[k + 1] |= 0x2
            elif kind == "flick":
                r.sy[k] = rng.choice([0, 50, 70]) if k == t else r.sy[k - 1]
                if (k - t) % rng.randint(6, 15) == 0:
                    r.yaw[k] = rng.choice([-1, 1]) * rng.randint(2000, 11000)
                    r.pit[k] = rng.choice([-1, 1]) * rng.randint(0, 6000)
            elif kind == "backpedal":
                r.sy[k] = -rng.choice([40, 70]) if k == t else r.sy[k - 1]
                r.yaw[k] = turn // 2
            elif kind == "strafe":
                r.sx[k] = rng.choice([-1, 1]) * rng.choice([40, 60, 70]) if k == t else r.sx[k - 1]
                r.sy[k] = rng.choice([0, 40, 70]) if k == t else r.sy[k - 1]
                r.yaw[k] = turn // 2
            elif kind == "spin":
                r.yaw[k] = rng.choice([-1, 1]) * 2400 if k == t else r.yaw[k - 1]
            # idle: nothing
        # digital move (the keyboard path) now and then
        if rng.random() < 0.12:
            for k in range(t, min(e, t + 15) + 1):
                r.dx[k] = 1 if rng.random() < 0.5 else -1
                r.dy[k] = rng.choice([-1, 0, 1])
        t = e + 1

    # ---- overlays ----
    # bursts of fire, more often while moving toward things
    k = rng.randint(5, 60)
    while k <= n:
        blen = rng.choice([1, 2, 3, 5, 8, 12, 20, 40])
        for j in range(k, min(n, k + blen - 1) + 1):
            r.btn[j] |= 0x2000
        k += blen + rng.randint(15, 160)
    # weapon cycling (next / prev), reload, crouch, aim + zoom
    k = rng.randint(30, 120)
    while k <= n:
        r.act[k] |= rng.choice([0x40, 0x40, 0x20])
        if k + 1 <= n:
            r.act[k + 1] |= 0
        k += rng.randint(25, 240)
    k = rng.randint(80, 250)
    while k <= n:
        r.act[k] |= 0x1
        k += rng.randint(120, 500)
    k = rng.randint(60, 300)
    while k <= n:
        if rng.random() < 0.5:
            r.act[k] |= 0x200                                            # crouch toggle (stand again later)
            if k + rng.randint(20, 60) <= n:
                r.act[k + rng.randint(20, 60)] |= 0x200
        else:
            for j in range(k, min(n, k + rng.randint(15, 70)) + 1):      # crouch hold
                r.act[j] |= 0x400
        k += rng.randint(150, 600)
    k = rng.randint(40, 200)
    while k <= n:
        a = k
        b = min(n, k + rng.randint(25, 120))
        for j in range(a, b + 1):
            r.btn[j] |= 0x0010                                           # aim (R)
        if rng.random() < 0.7:
            zi = rng.randint(a, b)
            for j in range(zi, min(b, zi + rng.randint(5, 30)) + 1):
                r.act[j] |= 0x8000
            for j in range(min(b, zi + 30) + 1, min(b, zi + 40) + 1):
                r.act[j] |= 0x10000
        k = b + rng.randint(80, 400)
    # the watch: opened and closed three ways while fire may be held; a Return-only START that must not open it; Escape taps
    wins = []   # ticks in which the watch may be open: no fire / A / B / Return-START there (Z on the Abort page really aborts the mission)
    k = rng.randint(250, 700)
    while k <= n - 220:
        # open with Escape (a START pulse was sometimes ignored, and the later close pulse then OPENED the watch under input meant for the
        # game: Z / use on the Abort page ended the stage); close with Escape or watch_objectives. No page turns (a page turn while the watch
        # is still opening reached the Abort page too). The protected window is long: nothing but the close pulse is sent inside it.
        r.fl[k] |= 1
        close = k + rng.randint(160, 220)
        if rng.random() < 0.5:
            r.fl[close] |= 1
        else:
            r.act[close] |= 0x2000
            r.act[close + 1] |= 0x2000
        wins.append((k + 2, close + 200))
        k = close + rng.randint(400, 1100)
    # (no stray Return-only START: it starts the watch's opening animation, which flips the parity of the later Escape pulses)
    k = rng.randint(200, 600)
    while k <= n:
        r.act[k] |= 0x4000                                               # detonate (nothing planted: a no-op)
        k += rng.randint(600, 2000)
    for (a, b) in wins:
        for j in range(a, min(b, n) + 1):
            r.btn[j] &= ~(0x2000 | 0x8000 | 0x4000)
            if not os.environ.get('SOAK_KEEP_DPAD_IN_WATCH'):
                r.dx[j] = 0
                r.dy[j] = 0
            r.act[j] &= ~0x2   # use = the A button: selects the page entry (Abort mission) while the watch is open
            if r.fl[j] & 2:
                r.btn[j] &= ~0x1000
                r.fl[j] &= ~2
    return r


def main():
    seed = int(sys.argv[1])
    stage = sys.argv[2]
    n = int(sys.argv[3])
    outdir = sys.argv[4]
    os.makedirs(outdir, exist_ok=True)
    cfg = STAGES.get(stage, STAGES["dam"])
    rng = random.Random("soak-%d-%s" % (seed, stage))

    ra = gen_player(random.Random("soakA-%d-%s" % (seed, stage)), n, 0)
    rb = gen_player(random.Random("soakB-%d-%s" % (seed, stage)), n, 1)
    open(os.path.join(outdir, "recA.txt"), "w").write("\n".join(ra.lines()) + "\n")
    open(os.path.join(outdir, "recB.txt"), "w").write("\n".join(rb.lines()) + "\n")

    env = {}
    plan = []
    mortal = rng.random() < 0.5   # invincible protects against GE_COOP_KILL, so deaths need invincible=0 (enemy_damage low: the guards rarely kill)
    hp = rng.choice([100, 400, 400, 1000])
    if mortal:
        env["GE_MODS"] = "invincible=0,enemy_damage=%d,enemy_health=%d" % (rng.choice([2, 5, 10, 25]), hp)
    else:
        env["GE_MODS"] = "invincible=1,enemy_health=%d" % hp
    plan.append("mods %s" % env["GE_MODS"])
    respawn = rng.choice([60, 90, 120, 180])
    env["GE_COOP_RESPAWN"] = str(respawn)

    cmds = []      # (tick, side h|g, text with + for spaces)
    kills = []     # (tick, player)
    hurts = []
    gives = []     # (tick, player, item id, ammo): GE_COOP_GIVEWEAPON
    moveto = []    # (tick, player, other, 1)
    movechr = []   # (tick, player, chr)
    tps = []       # (tick, player, x, y, z, yaw)
    doors = []     # (tick, o|c)

    def side():
        return rng.choice("hg")

    # opening kit: guns with plenty of ammo for both Bonds, so the fire bursts have something to shoot
    for pl in (1, 2):
        for g in rng.sample(GUNS, 3):
            cmds.append((rng.randint(8, 40), side(), "give+%s+p%d+%d" % (g, pl, rng.choice([60, 200, 999]))))

    # guard waves
    t = rng.randint(30, 80)
    live = 0
    while t < n - 60:
        if live > cfg["arena_cap"] - cfg["spawn_max"] or rng.random() < 0.15:
            cmds.append((t, side(), "spawn+clear"))
            plan.append("t%d spawn clear" % t)
            live = 0
        else:
            c = rng.randint(1, cfg["spawn_max"])
            d = rng.choice([300, 500, 800, 1200, 2000])
            cmds.append((t, side(), "spawn+guard+%d+%d" % (c, d)))
            plan.append("t%d spawn guard %d %d" % (t, c, d))
            live += c
        t += rng.randint(120, 400)

    # random gives (guns, quest items) outside the kill sequences
    t = rng.randint(100, 250)
    while t < n - 40:
        pl = rng.choice([1, 2])
        if rng.random() < 0.6:
            cmds.append((t, side(), "give+%s+p%d+%d" % (rng.choice(GUNS), pl, rng.choice([30, 120, 500]))))
        else:
            cmds.append((t, side(), "give+%s+p%d" % (rng.choice(["goldeneye_key", "dat_tape", "goldeneye_key"]), pl)))
        t += rng.randint(150, 500)

    # deliberate deaths: give a quest item (so there is something to drop), kill, move the partner onto the corpse to take the drop.
    # Never two Bonds dead together (that would end the mission), so the death windows of the two players are kept apart.
    windows = {0: [], 1: []}
    t = rng.randint(250, 500)
    # (GE_COOP_KILL under invincible=1 does not kill but leaves bondhealth at 0, which ends the mission: kills only in mortal runs)
    while mortal and t < n - respawn - 140:
        pl = rng.choice([0, 1])
        oth = 1 - pl
        lo, hi = t - 60, t + respawn + 140
        clash = any(not (hi < a or lo > b) for (a, b) in windows[oth]) or any(not (hi < a or lo > b) for (a, b) in windows[pl])
        if not clash:
            windows[pl].append((lo, hi))
            what = rng.choice(["goldeneye_key", "dat_tape", "key"]) if cfg["key"] else rng.choice(["goldeneye_key", "dat_tape", "none"])
            if what != "none":
                cmds.append((t - 40, side(), "give+%s+p%d" % (what, pl + 1)))
            if rng.random() < 0.6:
                cmds.append((t - 30, side(), "give+%s+p%d+%d" % (rng.choice(GUNS), pl + 1, rng.choice([30, 200]))))
            if rng.random() < 0.5:
                hurts.append((t, pl, 10))
                plan.append("t%d hurt p%d (certain kill; blocked while invincible)" % (t, pl))
            kills.append((t, pl))
            plan.append("t%d kill p%d, item %s, partner picks up" % (t, pl, what))
            moveto.append((t + rng.randint(12, 40), oth, pl, 1))
            if rng.random() < 0.5:
                moveto.append((t + rng.randint(60, 100), oth, pl, 1))
        t += rng.randint(300, 900)

    # movement bias: teleports along the Dam waypoints (start tunnel, gate end, road, tunnels), or next-to-guard moves elsewhere
    t = rng.randint(120, 260)
    while t < n - 40:
        pl = rng.choice([0, 1])
        dead = any(a <= t <= b for (a, b) in windows[pl])
        if not dead:
            if stage == "dam" and rng.random() < 0.7:
                x, y, z = rng.choice(DAM_WPS)
                tps.append((t, pl, x, y, z, rng.choice([0, 90, 180, 270])))
            elif rng.random() < 0.8:
                movechr.append((t, pl, rng.randint(0, 40)))
            else:
                movechr.append((t, pl, -1))
        t += rng.randint(150, 450)
    # all doors open / close (the door code, the room-visibility code)
    t = rng.randint(60, 200)
    state = "o"
    while t < n - 20:
        doors.append((t, state))
        state = "c" if state == "o" else "o"
        t += rng.randint(60, 300)

    # a rare one-sided mid-session look: a spawn clear by the OTHER side right after a wave, same tick, to cross commands
    if rng.random() < 0.5:
        tt = rng.randint(100, max(101, n - 100))
        cmds.append((tt, "h", "spawn+guard+1+400"))
        cmds.append((tt, "g", "spawn+guard+1+600"))
        plan.append("t%d simultaneous spawns from both sides" % tt)

    cmds.sort()
    env["GE_NET_TEST_CMD"] = ",".join("%d:%s:%s" % c for c in cmds if c[0] >= 1)
    if kills:
        env["GE_COOP_KILL"] = ",".join("%d:%d" % k for k in sorted(kills))
    if hurts:
        env["GE_COOP_HURT"] = ",".join("%d:%d:%d" % h for h in sorted(hurts))
    if moveto:
        env["GE_COOP_MOVETO"] = ",".join("%d:%d:%d:%d" % m for m in sorted(moveto))
    mc = sorted(movechr)
    if mc:
        env["GE_COOP_MOVETOCHR"] = ",".join("%d:%d:%d" % m for m in mc)
    if tps and stage == "dam":
        env["GE_ROOMTEST_TP"] = ",".join("%d:%d:%d:%d:%d:%d" % tp for tp in sorted(tps))
    if doors:
        env["GE_ROOMTEST_DOORS"] = ",".join("%d:%s" % d for d in doors)
    with open(os.path.join(outdir, "env.txt"), "w") as f:
        for k in sorted(env):
            f.write("%s=%s\n" % (k, env[k]))

    wa = rng.choice(WINDOWS)
    wb = rng.choice(WINDOWS)
    with open(os.path.join(outdir, "prof.txt"), "w") as f:
        f.write("h %d %d %d %d %d %d\n" % (wa + (rng.choice([0, 1]), rng.choice([0, 1]))))
        f.write("g %d %d %d %d %d %d\n" % (wb + (rng.choice([0, 1]), rng.choice([0, 1]))))
    with open(os.path.join(outdir, "plan.txt"), "w") as f:
        f.write("seed %d stage %s ticks %d windows host %s guest %s\n" % (seed, stage, n, wa, wb))
        f.write("\n".join(plan) + "\n")
        f.write("commands: %d, kills: %d, teleports: %d, movechr: %d, door toggles: %d\n" % (len(cmds), len(kills), len(tps), len(mc), len(doors)))


if __name__ == "__main__":
    main()
