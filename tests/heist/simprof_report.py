#!/usr/bin/env python3
"""GE_PROFILE_SIM report (designs/bank-heist.md section 28): reads a dump of port/luavm/port_luavm_simprof.c and prints, per phase,
the sim tick CPU (avg / p50 / p95 / max), the hottest functions (self and inclusive % of the phase's sim CPU samples, native frames
resolved with addr2line, inlined frames included), the hottest Lua functions, the counters and the worst ticks with what they ran.
usage (in the build container): python3 simprof_report.py <dump> <binary> [phase,phase|all] [top]"""
import collections
import os
import subprocess
import sys


def load(path):
    phases, protos, counts, ticks, samples = {}, {}, [], [], []
    with open(path, errors="replace") as f:
        for line in f:
            t = line.rstrip("\n")
            if t.startswith("S "):
                head, nat, lua = t.split("|")
                _, tick, ph = head.split()
                samples.append((int(tick), int(ph), [int(x, 16) for x in nat.split()], [int(x) for x in lua.split()]))
            elif t.startswith("T "):
                _, tick, ph, us = t.split()
                ticks.append((int(tick), int(ph), int(us)))
            elif t.startswith("P "):
                _, i, name = t.split(" ", 2)
                protos[int(i)] = name
            elif t.startswith("N "):
                _, i, name = t.split(" ", 2)
                phases[int(i)] = name
            elif t.startswith("C "):
                _, name, ph, n = t.split()
                counts.append((name, int(ph), int(n)))
    return phases, protos, counts, ticks, samples


def resolve(binary, addrs):
    """addr -> list of function names, innermost (inlined) first"""
    addrs = sorted(addrs)
    out = subprocess.run(["addr2line", "-a", "-f", "-i", "-C", "-e", binary], input="\n".join("%x" % a for a in addrs),
                         capture_output=True, text=True).stdout.splitlines()
    res, cur, i = {}, None, 0
    while i < len(out):
        ln = out[i]
        if ln.startswith("0x"):
            cur = int(ln, 16)
            res[cur] = []
            i += 1
            continue
        fn = ln
        loc = out[i + 1] if i + 1 < len(out) else ""
        f = loc.rsplit("/", 1)[-1].split(" ")[0]
        res[cur].append(fn if fn != "??" else "?@%x" % cur)
        if f and fn != "??":
            res[cur][-1] = "%s (%s)" % (fn, f.split(":")[0])
        i += 2
    return res


def pct(n, d):
    return 100.0 * n / d if d else 0.0


def main():
    dump, binary = sys.argv[1], sys.argv[2]
    want = sys.argv[3] if len(sys.argv) > 3 else "all"
    top = int(sys.argv[4]) if len(sys.argv) > 4 else 25
    phases, protos, counts, ticks, samples = load(dump)
    addrs = set()
    for _, _, nat, _ in samples:
        if nat:
            addrs.add(nat[0])
            addrs.update(a - 1 for a in nat[1:])
    names = resolve(binary, addrs)

    def frames(nat):
        fr = list(names.get(nat[0], ["?"])) if nat else []
        for a in nat[1:]:
            fr.extend(names.get(a - 1, ["?"]))
        return fr

    groups = collections.OrderedDict()
    if want == "all":
        for i in sorted(phases):
            groups[phases[i]] = {i}
    else:
        for w in want.split(","):
            groups[w] = {i for i, n in phases.items() if n == w}
    groups["ALL (every phase)"] = set(phases)
    worst_by_tick = {}
    for tick, ph, us in ticks:
        worst_by_tick[tick] = (us, ph)
    for gname, ids in groups.items():
        ss = [s for s in samples if s[1] in ids]
        tk = sorted(us for _, ph, us in ticks if ph in ids)
        if not tk:
            continue
        print("=" * 100)
        print("phase %s: %d sim-only ticks, tick CPU avg %.2f ms, p50 %.2f, p95 %.2f, max %.2f; %d samples" % (
            gname, len(tk), sum(tk) / len(tk) / 1000.0, tk[len(tk) // 2] / 1000.0, tk[int(len(tk) * 0.95)] / 1000.0, tk[-1] / 1000.0,
            len(ss)))
        selfc, incl, luaself, luaincl = collections.Counter(), collections.Counter(), collections.Counter(), collections.Counter()
        n = len(ss)
        for _, _, nat, lua in ss:
            fr = frames(nat)
            if fr:
                selfc[fr[0]] += 1
            for f in set(fr):
                incl[f] += 1
            if lua:
                luaself[protos.get(lua[0], "?")] += 1
                for p in set(lua):
                    luaincl[protos.get(p, "?")] += 1
        print("-- self (native, innermost frame) --")
        for f, c in selfc.most_common(top):
            print("  %5.1f%%  %s" % (pct(c, n), f))
        print("-- inclusive (native) --")
        for f, c in incl.most_common(top + 15):
            print("  %5.1f%%  %s" % (pct(c, n), f))
        print("-- Lua: inclusive / self (samples with a Lua frame: %.1f%%) --" % pct(sum(1 for s in ss if s[3]), n))
        for f, c in luaincl.most_common(top):
            print("  %5.1f%%  %5.1f%%  %s" % (pct(c, n), pct(luaself[f], n), f))
        for target in [t for t in os.environ.get("CALLERS", "").split(",") if t]:
            par = collections.Counter()
            for _, _, nat, _ in ss:
                fr = frames(nat)
                seen = set()
                for i, f in enumerate(fr):
                    if f.startswith(target + " ") and i + 1 < len(fr):
                        chain = " < ".join(fr[i + 1:i + 1 + int(os.environ.get("DEPTH", "2"))])
                        if chain not in seen:
                            seen.add(chain)
                            par[chain] += 1
            print("-- callers of %s --" % target)
            for f, c in par.most_common(12):
                print("  %5.1f%%  %s" % (pct(c, n), f))
        for target in [t for t in os.environ.get("LINES", "").split(",") if t]:
            lc = collections.Counter()
            for _, _, nat, _ in ss:
                if nat and names.get(nat[0], ["?"])[0].startswith(target + " "):
                    lc[nat[0]] += 1
            locs = subprocess.run(["addr2line", "-e", binary], input="\n".join("%x" % a for a in lc), capture_output=True,
                                  text=True).stdout.splitlines()
            bl = collections.Counter()
            for (a, c), loc in zip(list(lc.items()), locs):
                bl[loc.rsplit("/", 1)[-1]] += c
            print("-- self lines of %s --" % target)
            for f, c in bl.most_common(15):
                print("  %5.1f%%  %s" % (pct(c, n), f))
        cs = [(name, c) for name, ph, c in counts if ph in ids]
        if cs:
            agg = collections.Counter()
            for name, c in cs:
                agg[name] += c
            print("-- counters (per tick) --")
            for name, c in agg.most_common():
                print("  %10.1f  %s" % (c / len(tk), name))
    print("=" * 100)
    print("worst ticks:")
    worst = sorted(ticks, key=lambda t: -t[2])[:int(sys.argv[5]) if len(sys.argv) > 5 else 8]
    for tick, ph, us in worst:
        ss = [s for s in samples if s[0] == tick]
        incl = collections.Counter()
        lu = collections.Counter()
        for _, _, nat, lua in ss:
            for f in set(frames(nat)):
                incl[f] += 1
            for p in set(lua):
                lu[protos.get(p, "?")] += 1
        print("tick %d (%s): %.2f ms, %d samples" % (tick, phases.get(ph, "?"), us / 1000.0, len(ss)))
        for f, c in incl.most_common(14):
            print("      %4d  %s" % (c, f))
        for f, c in lu.most_common(6):
            print("      %4d  lua %s" % (c, f))


main()
