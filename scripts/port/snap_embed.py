#!/usr/bin/env python3
"""Rollback R2: build the sim-statics range table and embed it into the linked exe.

usage: snap_embed.py --exe ProjectMidas[.exe] --map ProjectMidas.map [--root <repo>] [--strict] [--report] [--check-only]

What it does:
  1. reads every .data / .bss / COMMON input section of the link map and classifies its object file:
       SIM      game/engine/port/Lua state that a rollback must save and restore (the snapshot's static ranges)
       PRES     presentation: audio, scheduler, VI, framebuffers, input devices, the renderer glue, the console, the network
                transport, the ROM/asset caches, wall-clock bookkeeping. Never restored (a restore would corrupt it or rewind real time).
       FOREIGN  libc / libgcc / SDL / fast3d / libultra / enet: not game state.
  2. writes the SIM ranges into the fixed-size const table `g_PortSnapTable` (port/src/port_snaptable.c) by patching the exe
     in place, after the link. The table has a fixed capacity, so the layout of the exe is the same before and after: no relink.
  3. the CHECK: a source file under src/ or port/ that owns writable statics but is in no class below is reported (and with
     --strict the exit status is 2). Source files are classified by name; src/*.c and src/game/*.c default to SIM (so a NEW file
     there is snapshotted automatically), a NEW port/ file has no default: it must be added to SIM_PORT or PRES_PORT here.
     tests/snap_table_check.sh runs this in strict mode and has a negative control.
"""
import argparse, os, re, struct, sys

# ---- the classification (edit here when a file is added) ------------------------------------------------------------------------

# port/src files that own simulation state. Everything the sim reads that is not derived from the pools or the game's own statics.
SIM_PORT = {
    'inputrec.c', 'n64_random.c', 'port_arena.c', 'port_blocks.c', 'port_bot.c', 'port_botlogic.c', 'port_chrthrow.c',
    'port_coop.c', 'port_coopdrop.c', 'port_cooplogic.c', 'port_event.c', 'port_faction.c', 'port_flatland.c', 'port_heist.c', 'port_hostage.c',
    'port_lua.c', 'port_mode.c', 'port_modifier.c', 'port_modparse.c', 'port_mpsettings.c', 'port_mpstats.c', 'port_options.c',
    'port_roomvis.c', 'port_spawn.c', 'port_wsets.c', 'ramrom_data.c', 'port_simheap.c', 'port_arenamode.c',
    # the heist CCTV view: who watches which camera (sim); its per-pass render state is rebuilt every pass, so a restore is harmless
    'port_cctvview.c',
    # the heist gadgets: triggers, the piton reel, disguises (sim); the overlay's inputs are rewritten by the script every tick
    'port_gadgets.c',
    # the heist planning phase: the hold and the board's input events (sim; designs/bank-heist.md section 27)
    'port_planning.c',
    'port_arenafx.c',   # arena A7: knife-only flags, the fog record
    'port_arenacoop.c',   # arena co-op: downed / revive timers, the partner bot's memory (drop A7)
    'port_arenacrowd.c',  # arena A8: last Bond hit per chr, blast owners, the held-gun / spectator hit queue (crowd favour)
    'port_arenamenu.c',   # arena start menu / post-game: the hand-off to the next stage (its settings), the menu's preferences
    'port_whacky.c',      # arena Whacky: minion launchers, the minions' brains, the anim-rate / line-mode flags it set
    'port_uimenu.c',      # the generic mode menu layer: the hand-off to the next stage (settings, difficulty, GE_* vars), the named prefs
    'port_levelgen_select.c',   # which generated level fills the levelgen slot at a stage load (no state of its own: the environment)

    # input latches the sim reads (held / edge / zoom state per pad); its live accumulators are excluded by portSnapLive()
    'joy_port.c',
    # the sim's own view of the sounds it starts (tokens, end times in sim fields): modding/snapshot-fidelity.md
    'port_simsnd.c',
    # the player bot: its decisions are state the optimiser's savestates rewind (its scratch buffers are registered live)
    'port_playerbot.c', 'port_playerbotgraph.c',
    # the heist's hearing layer and guard awareness (designs/bank-heist.md section 18)
    'port_noise.c', 'port_ai.c',
    # door interlocks: mantraps, sally ports (designs/bank-heist.md section 29)
    'port_interlock.c',
    # the heist's combat layer: cover, peeks, flanks (designs/bank-heist.md section 24)
    'port_combat.c',
    # hostages and the human shield (designs/bank-heist.md section 31)
    'port_shield.c',
    # guard personalities: the deal, bribes, watchclock skips, what the crew has observed (designs/bank-heist.md section 32)
    'port_traits.c',
}
# port/src files that are presentation or transport: never rewound.
PRES_PORT = {
    'animdata.c', 'romdata.c', 'romfile_model.c', 'romfile_setup.c', 'romfile_stan.c', 'asset_bin_stubs.c', 'audio_pc.c', 'audio_stubs.c', 'headless_stubs.c', 'hitchlog.c', 'input_pc.c',
    'libultra_shim.c', 'main.c', 'model_audit.c', 'motor_stub.c', 'n64_stubs.c', 'pcsched.c', 'port_console.c', 'port_modmenu.c',
    'port_bench.c', 'port_crowd.c', 'port_gamesfx.c', 'port_music.c', 'port_net.c', 'port_netcmd.c', 'port_nethash.c', 'port_netlock.c', 'port_netmsg.c', 'port_netstats.c',
    'port_probe.c', 'port_profiles.c', 'port_simcfg.c', 'port_simonly.c', 'port_snap.c', 'port_snapshot.c', 'port_snaptable.c', 'port_soundfile.c', 'port_textures.c',
    'port_watchpages.c', 'printf_port.c', 'raphnet.c', 'roc_hooks.c', 'romfile_bg.c', 'port_intel.c',
    'segments.c', 'sysstubs.c', 'ucode_dl_stubs.c', 'video.c',
    'port_arenascores.c',   # arena A7: the best-scores file and the report-page text (never read by the sim)
    'port_arenahud.c', 'port_hudstyle.c',   # the arena's wave panel and callouts, the shared HUD drawing helpers (presentation)
}
PRES_PORT |= {'port_arenamenu_draw.c'}   # the arena start menu / post-game drawing, its animation stamps, the mouse: presentation
PRES_PORT |= {'port_uimenu_draw.c'}   # the generic mode menu layer's drawing, its animation stamps, the mouse: presentation
PRES_PORT |= {'port_arhud.c'}   # the AR overlay / diagnostics (marks, bubbles, toggles): presentation, never rewound
PRES_PORT |= {'port_hidden.c'}  # GE_HIDDEN hidden-window switch (port/README-headless.md): process setting, never rewound
PRES_PORT |= {'port_debrief.c'}   # the heist DEBRIEF panel (content, view, input capture): presentation, never rewound
PRES_PORT |= {'port_crosshair.c'}   # crosshair styles, hit-marker timers, the sight's draw list: presentation, never rewound
PRES_PORT |= {'port_chrfx.c'}   # arena Whacky per-chr colour wash / translucency: read only by chrRenderProp, never by the sim
PRES_PORT |= {'port_cctvbody.c'}   # the solo Bond's body on a CCTV feed (its own heap buffers, posed and drawn only in the feed): presentation
# src/*.c and src/game/*.c that are NOT sim state (audio, scheduler, VI, framebuffers, thread stacks, boot, profiling).
PRES_SRC = {
    'audi.c', 'snd.c', 'music.c', 'mp_music.c', 'music_0D2720.c', 'sched.c', 'vi.c', 'pi.c', 'speed_graph.c', 'speedgraphrenderer.c',
    'cfb.c', 'stacks.c', 'c_data_filler.c', 'init.c', 'crash.c', 'rmon.c', 'usb.c', 'motor.c', 'deb.c', 'debugmenu.c', 'fr.c',
    'rsp.c', 'decompress.c',      # the inflate scratch (s_huftWorkspace): transient, rewritten by every decompression
}
# Lua: the VM statics (the VM itself lives in the arena, port_luavm.c) are sim.
SIM_LUA_DIRS = ('port/lua', 'port/luavm')


def stems(root, d):
    p = os.path.join(root, d)
    return {f for f in os.listdir(p) if f.endswith('.c')} if os.path.isdir(p) else set()


def build_classifier(root, assume_port=()):
    game = stems(root, 'src/game')
    eng = stems(root, 'src')
    port = stems(root, 'port/src') | set(assume_port)     # (--assume-port-file: the negative control of tests/snap_table_check.sh)
    lua = stems(root, 'port/lua') | stems(root, 'port/luavm')
    enet = stems(root, 'port/enet') | stems(root, 'port/netio')

    def classify(obj):
        """obj: the object spec from the map. Returns (class, stem) with class SIM/PRES/FOREIGN/UNKNOWN."""
        m = re.search(r'\(([^)]*)\)\s*$', obj)
        path = m.group(1) if m else obj
        base = os.path.basename(path)
        for ext in ('.obj', '.o'):
            if base.endswith(ext):
                base = base[:-len(ext)]
        stem = base                        # foo.c
        norm = path.replace('\\', '/')
        # a direct object path says which directory it came from; an archive member only gives the file name
        d = None
        if '/src/game/' in norm: d = 'game'
        elif '/port/src/' in norm: d = 'port'
        elif '/port/luavm/' in norm or '/port/lua/' in norm: d = 'lua'
        elif '/src/libultra' in norm or '/src/inflate' in norm: d = 'foreign'
        elif '/src/' in norm and norm.rfind('/src/') + 5 == norm.rfind('/') + 1: d = 'eng'
        if stem.endswith('.c') or stem.endswith('.cpp'):
            pass
        else:
            return ('FOREIGN', stem)
        if m is not None and ('libge_lua' in obj or 'liblua' in obj):
            return ('SIM', stem)
        if m is not None and ('libge_enet' in obj):
            return ('FOREIGN', stem)
        if m is not None and ('objects.a' not in obj and 'libge' not in obj):
            return ('FOREIGN', stem)       # libc.a(foo.o), libgcc, libSDL2...
        if d is None:                       # archive member of objects.a: by file name
            if stem in port and stem not in game and stem not in eng: d = 'port'
            elif stem in game and stem not in port: d = 'game'
            elif stem in eng and stem not in port: d = 'eng'
            elif stem in lua: d = 'lua'
            elif stem in port: d = 'port'
            else: d = 'unknown'
        if d == 'foreign' or stem in enet and d == 'unknown':
            return ('FOREIGN', stem)
        if d == 'lua':
            return ('SIM', stem)
        if d in ('game', 'eng'):
            return ('PRES' if stem in PRES_SRC else 'SIM', stem)
        if d == 'port':
            if stem in SIM_PORT: return ('SIM', stem)
            if stem in PRES_PORT: return ('PRES', stem)
            return ('UNKNOWN', stem)
        return ('FOREIGN', stem)           # fast3d, glad, asset objects, ...
    return classify


SEC_RE = re.compile(r'^ (\.data(?:\.rel(?:\.local)?)?|\.data\.(?!rel\.ro)\S+|\.bss|\.bss\.\S+|COMMON|\.data\$\S*|\.bss\$\S*)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S.*)$')
NAME_RE = re.compile(r'^ (\.data(?:\.rel(?:\.local)?)?|\.data\.(?!rel\.ro)\S+|\.bss\.\S+|\.data\$\S*|\.bss\$\S*)\s*$')
CONT_RE = re.compile(r'^\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S.*)$')


def parse_map(path):
    sec = None
    out = []
    table_addr = None
    pend = False
    sym_re = re.compile(r'^\s+0x([0-9a-f]+)\s+_?g_PortSnapTable\s*$')
    for ln in open(path, errors='replace'):
        ln = ln.rstrip('\n')
        if ln and ln[0] not in ' \t' and ln.split()[0].startswith('.'):
            sec = ln.split()[0]
            pend = False
            continue
        m = sym_re.match(ln)
        if m:
            table_addr = int(m.group(1), 16)
            continue
        if sec not in ('.data', '.bss'):
            continue
        if pend:
            pend = False
            m = CONT_RE.match(ln)
            if m and int(m.group(2), 16) != 0:
                out.append((int(m.group(1), 16), int(m.group(2), 16), m.group(3)))
            continue
        if NAME_RE.match(ln):
            pend = True
            continue
        m = SEC_RE.match(ln)
        if not m:
            continue
        addr, size, obj = int(m.group(2), 16), int(m.group(3), 16), m.group(4)
        if size == 0:
            continue
        out.append((addr, size, obj))
    return out, table_addr


def va_to_off(data, va):
    if data[:2] == b'MZ':                                 # PE32
        pe = struct.unpack_from('<I', data, 0x3c)[0]
        nsec = struct.unpack_from('<H', data, pe + 6)[0]
        optsz = struct.unpack_from('<H', data, pe + 20)[0]
        base = struct.unpack_from('<I', data, pe + 24 + 28)[0]
        so = pe + 24 + optsz
        for i in range(nsec):
            name, vsz, vaddr, rsz, rptr = struct.unpack_from('<8sIIII', data, so + 40 * i)
            if base + vaddr <= va < base + vaddr + max(vsz, rsz):
                return rptr + (va - base - vaddr)
        raise SystemExit('snap_embed: address %#x is in no PE section' % va)
    if data[:4] == b'\x7fELF':                            # ELF32
        phoff = struct.unpack_from('<I', data, 28)[0]
        phentsz, phnum = struct.unpack_from('<HH', data, 42)
        for i in range(phnum):
            t, off, vaddr, paddr, filesz, memsz, flags, align = struct.unpack_from('<IIIIIIII', data, phoff + phentsz * i)
            if t == 1 and vaddr <= va < vaddr + filesz:
                return off + (va - vaddr)
        raise SystemExit('snap_embed: address %#x is in no ELF segment' % va)
    raise SystemExit('snap_embed: unknown executable format')


HDR = struct.Struct('<8I')            # magic version capEnt capStr count strUsed reserved reserved
ENT = struct.Struct('<4I')            # addr size nameOff cat
MAGIC = 0x50414E53


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--exe')
    ap.add_argument('--map', required=True)
    ap.add_argument('--root', default=os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..')))
    ap.add_argument('--strict', action='store_true', help='exit 2 when a source file with statics is unclassified')
    ap.add_argument('--report', action='store_true')
    ap.add_argument('--check-only', action='store_true', help='classify and check, do not patch')
    ap.add_argument('--out-list', help='also write the SIM ranges as text (cat addr size name)')
    ap.add_argument('--assume-port-file', action='append', default=[], help='test only: pretend port/src has this file')
    a = ap.parse_args()

    classify = build_classifier(a.root, a.assume_port_file)
    ranges, table_va = parse_map(a.map)
    sim, unknown = [], {}
    tot = {'SIM': 0, 'PRES': 0, 'FOREIGN': 0, 'UNKNOWN': 0}
    per = {}
    for addr, size, obj in ranges:
        c, stem = classify(obj)
        tot[c] += size
        per[(c, stem)] = per.get((c, stem), 0) + size
        if c == 'SIM':
            sim.append((addr, size, stem))
        elif c == 'UNKNOWN':
            unknown[stem] = unknown.get(stem, 0) + size
            sim.append((addr, size, stem))      # unclassified: snapshot it (safe default), and report
    sim.sort()
    # merge adjacent ranges (names: the first one's)
    merged = []
    for addr, size, name in sim:
        if merged and merged[-1][0] + merged[-1][1] == addr:
            merged[-1][1] += size
            if name not in merged[-1][2].split('+'):
                merged[-1][2] += '+' + name if len(merged[-1][2]) < 40 else ''
        else:
            merged.append([addr, size, name])
    print('snap_embed: statics: SIM %d KB in %d ranges, PRES %d KB, FOREIGN %d KB, UNCLASSIFIED %d KB' %
          (tot['SIM'] // 1024 + tot['UNKNOWN'] // 1024, len(merged), tot['PRES'] // 1024, tot['FOREIGN'] // 1024, tot['UNKNOWN'] // 1024))
    if a.report:
        for (c, stem), n in sorted(per.items(), key=lambda t: -t[1])[:40]:
            if c in ('SIM', 'PRES', 'UNKNOWN'):
                print('  %-8s %-26s %9d' % (c, stem, n))
    rc = 0
    if unknown:
        print('snap_embed: WARNING: %d source file(s) own writable statics but are in no class (scripts/port/snap_embed.py): %s' %
              (len(unknown), ', '.join('%s (%d B)' % kv for kv in sorted(unknown.items()))))
        print('snap_embed:   they are snapshotted for now (the safe default); add each to SIM_PORT or PRES_PORT')
        if a.strict:
            rc = 2
    if a.out_list:
        with open(a.out_list, 'w') as f:
            for addr, size, name in merged:
                f.write('S %08x %x %s\n' % (addr, size, name))
    if a.check_only or a.exe is None:
        return rc
    if table_va is None:
        print('snap_embed: g_PortSnapTable is not in the map: nothing embedded')
        return 3
    data = bytearray(open(a.exe, 'rb').read())
    off = va_to_off(data, table_va)
    magic, ver, cap_ent, cap_str, cnt, used, r0, r1 = HDR.unpack_from(data, off)
    if ver != 1 or cap_ent == 0 or cap_str == 0:
        print('snap_embed: the table in the exe is not the placeholder this tool expects (version %d cap %d/%d)' % (ver, cap_ent, cap_str))
        return 3
    if len(merged) > cap_ent:
        print('snap_embed: %d ranges but the table holds %d: raise PORT_SNAP_TABLE_CAP in port_snaptable.c' % (len(merged), cap_ent))
        return 3
    strs = bytearray(b'\0')
    ents = []
    for addr, size, name in merged:
        nm = name.encode()[:39] + b'\0'
        ents.append((addr, size, len(strs), ord('S')))
        strs += nm
    if len(strs) > cap_str:
        print('snap_embed: the name pool needs %d bytes, the table holds %d: raise PORT_SNAP_STR_CAP' % (len(strs), cap_str))
        return 3
    HDR.pack_into(data, off, MAGIC, 1, cap_ent, cap_str, len(ents), len(strs), 0, 0)
    for i, e in enumerate(ents):
        ENT.pack_into(data, off + HDR.size + ENT.size * i, *e)
    for i in range(len(ents), cap_ent):
        ENT.pack_into(data, off + HDR.size + ENT.size * i, 0, 0, 0, 0)
    sbase = off + HDR.size + ENT.size * cap_ent
    data[sbase:sbase + len(strs)] = strs
    open(a.exe, 'wb').write(data)
    print('snap_embed: embedded %d ranges (%d KB of sim statics, %d bytes of names) into %s' %
          (len(ents), sum(e[1] for e in ents) // 1024, len(strs), os.path.basename(a.exe)))
    return rc


if __name__ == '__main__':
    if '--soft' in sys.argv:        # the CMake post-link step: never break a build over the snapshot table (the run says it is missing)
        sys.argv.remove('--soft')
        try:
            rc = main()
        except SystemExit as e:
            rc = e.code if isinstance(e.code, int) else 3
            if not isinstance(e.code, int):
                print(e.code)
        except Exception as e:            # noqa
            print('snap_embed: failed: %r' % (e,))
            rc = 3
        if rc not in (0, None):
            print('snap_embed: WARNING: the range table was NOT embedded (rc %s): GE_SNAP* snapshots will refuse to run in this exe' % rc)
        sys.exit(0)
    sys.exit(main())
