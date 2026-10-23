#!/usr/bin/env python3
"""Spike helper (rollback snapshot): derive per-object .data/.bss ranges from an ld map.

usage: snap_ranges.py ProjectMidas.map snap_ranges.txt

Output lines: <cat> <hexaddr> <hexsize> <objname>
  G = src/game/*.c   E = src/*.c (engine)   P = port/src sim-state files (table below)
  R = other port/src (renderer, audio, input, SDL glue, console, ...)
  L = Lua (port/lua, port/luavm)   X = everything else (CRT, libgcc, libstdc++, fast3d, assets)
snapshot code picks categories at run time (GE_SNAPCATS), so the table can be tuned without a relink.
"""
import os, re, sys

root = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
def stems(d):
    return {f for f in os.listdir(os.path.join(root, d)) if f.endswith('.c')}
game = stems('src/game')
engine = stems('src')
port = stems('port/src')
lua = stems('port/lua') | stems('port/luavm')

# port files that own simulation-affecting state (bot, faction, event, mode, modifier, spawn,
# hostage tables, RNG, replay) - everything else in port/src is renderer/audio/input/OS glue.
port_sim = {
    'n64_random.c', 'port_bot.c', 'port_botlogic.c', 'port_chrthrow.c', 'port_event.c',
    'port_faction.c', 'port_hostage.c', 'port_mode.c', 'port_modifier.c', 'port_spawn.c',
    'port_arena.c', 'port_blocks.c', 'port_mpsettings.c', 'port_mpstats.c', 'port_lua.c',
    'port_modparse.c', 'ramrom_data.c', 'inputrec.c', 'roc_hooks.c', 'port_options.c',
    'port_wsets.c',
}

def cat(obj):
    m = re.search(r'\(([^)]*)\)\s*$', obj)
    name = (m.group(1) if m else os.path.basename(obj))
    base = name[:-4] if name.endswith('.obj') else name
    if m is None:
        return 'X', base
    if base in port_sim and base in port:
        return 'P', base
    if base in game:
        return 'G', base
    if base in engine:
        return 'E', base
    if base in port:
        return 'R', base
    if base in lua:
        return 'L', base
    return 'X', base

sec = None
out = []
line_re = re.compile(r'^ (\.data|\.bss|COMMON|\.data\$\S*|\.bss\$\S*)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S.*)$')
for ln in open(sys.argv[1], errors='replace'):
    ln = ln.rstrip('\n')
    if ln and ln[0] not in ' \t' and ln.split()[0].startswith('.'):
        sec = ln.split()[0]
        continue
    if sec not in ('.data', '.bss'):
        continue
    m = line_re.match(ln)
    if not m:
        continue
    addr, size, obj = int(m.group(2), 16), int(m.group(3), 16), m.group(4)
    if size == 0:
        continue
    c, base = cat(obj)
    out.append((c, addr, size, base))

with open(sys.argv[2], 'w') as f:
    for c, a, s, b in sorted(out, key=lambda t: t[1]):
        f.write('%s %08x %x %s\n' % (c, a, s, b))

tot = {}
for c, a, s, b in out:
    tot[c] = tot.get(c, 0) + s
print('ranges: ' + ', '.join('%s=%d KB' % (c, tot[c] // 1024) for c in sorted(tot)))
big = sorted(out, key=lambda t: -t[2])[:25]
for c, a, s, b in big:
    print('  %s %-28s %9d  @%08x' % (c, b, s, a))
