#!/usr/bin/env python3
"""package_arena.py - an Arena release package: a folder and a zip, with no cart image or cart data in it.

usage: py -3 tools/package_arena.py <windows build dir> <out dir> --tag arena-v0.1 --rom <stock cart (z64), to prove that
       neither the package nor the exe holds its data> [--nm <nm for the exe>] [--date "YYYY-MM-DD HH:MM"] [--commit <sha>]

Makes <out>/ProjectMidas-<tag>/ and <out>/OculusAureus-<tag>-windows.zip from a built Windows build dir:
  ProjectMidas.exe, SDL2.dll, gamecontrollerdb.txt, gamecontrollerdefault.txt, midas_ini_help.txt, the arena launchers
  (Play Arena = the start menu, the preset modes, Whacky, Test Keys, Whacky Test Keys, Benchmark, Benchmark Live), README.txt
  (tools/arena_release_README.txt), README-FORK.md, LICENSE.txt, THIRD_PARTY_NOTICES.txt, Data/README.txt, Data/shaders_base.txt,
  Data/Sounds/README.txt, Data/Scripts/modes/ (the arena modes, the start menu, their _arena_* modules and the benchmarks), empty
  Data/Textures/Base and Options, Mods/README.txt.
Never copied: a cart image, Data/Saves, Data/Devices, Data/Logs, midas.ini (the game writes it on the first run), the heist,
hostage, probe and test modes.
The launchers come from the repo's launchers/ folder, never from the build dir (a play dir may hold edited ones).
Checks (the package is refused when one fails): no launcher sets GE_CHEATS except the test and benchmark launchers listed in
CHEATS_OK, each with exactly its line; no file with a cart-image extension; no file that starts with an N64 cart header (any byte
order); none of 512 evenly spaced 4 KB blocks of the cart (blocks of one repeated byte skipped) appears in any packaged file; and
tools/check_no_rom_data.py passes on the packaged exe (cart bytes in 3 byte orders, including the inflated 1172 streams, the build's
provenance, and the asset symbols when an nm is found or given).
The exe carries no cart-derived data: the only cart-derived assets an exe used to link were the three menu models (the legal page,
the Nintendo and GoldenEye logos), and they now load from the player's cart at run time like everything else (PORT_ASSET_KEEP_SYMS
keeps only the fork's own generated levels). oddtextures.c / rarewarelogo.c / font_dl.c are git-tracked upstream sources.

--platform linux (run it on Linux, e.g. in the build container): the same package from a Linux build dir, as
<out>/OculusAureus-<tag>-linux.tar.gz (a tar keeps the exec bits): ProjectMidas instead of the exe, lib/libSDL2-2.0.so.0 (--sdl2,
used only when the system has no 32-bit libSDL2), README.txt from tools/arena_release_README_linux.txt, and a .sh launcher made
from each .bat by bat_to_sh() (the same checks run on them)."""
import argparse, datetime, os, re, shutil, subprocess, sys, tarfile, zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LAUNCHERS = ["Play Arena.bat", "Play Arena (Endless).bat", "Play Arena (Chaos).bat", "Play Arena (Chaos Endless).bat",
             "Play Arena (Co-op).bat", "Play Arena (Co-op Chaos).bat", "Play Arena (Whacky).bat", "Play Arena (Test Keys).bat",
             "Play Arena (Whacky Test Keys).bat", "Arena Benchmark.bat", "Arena Benchmark (Live).bat", "Arena Benchmark (Late Waves).bat"]
TOP = ["ProjectMidas.exe", "SDL2.dll", "gamecontrollerdb.txt", "gamecontrollerdefault.txt", "midas_ini_help.txt"]
TOP_LINUX = ["ProjectMidas", "gamecontrollerdb.txt", "gamecontrollerdefault.txt", "midas_ini_help.txt"]
MODES = ["arena.lua", "arena-endless.lua", "arena-chaos.lua", "arena-chaos-endless.lua", "arena-coop.lua", "arena-coop-chaos.lua",
         "_arena_polish.lua", "_arena_mutators.lua", "_arena_elites.lua", "_arena_scores.lua", "_arena_coop.lua", "_arena_crowd.lua",
         "_arena_theme.lua", "arena-menu.lua", "_arena_menu_defs.lua", "_arena_flow.lua", "arena-whacky.lua", "_arena_whacky.lua",
         "arena-bench.lua", "arena-bench-live.lua", "_arena_plus.lua", "_ui_menu.lua", "_arena_maps.lua"]
FROM_REPO = {"README-FORK.md": "README-FORK.md", "LICENSE": "LICENSE.txt", "tools/THIRD_PARTY_NOTICES.txt": "THIRD_PARTY_NOTICES.txt",
             "tools/Data_README.txt": "Data/README.txt", "tools/Mods_README.txt": "Mods/README.txt",
             "tools/Sounds_README.txt": "Data/Sounds/README.txt"}
# Cheats never ship in a play launcher. The allowlist is exactly these five launchers with exactly these lines: the two test-keys
# launchers (invuln + guns) and the three benchmarks (invuln, so a measurement run cannot end early). Anything else, including one of
# these five with another cheat line, refuses the package.
# (Keyed by launcher name without the extension, so the Linux .sh made from each .bat is held to the same line.)
CHEATS_OK = {"Play Arena (Test Keys)": "GE_CHEATS=invuln,guns",
             "Play Arena (Whacky Test Keys)": "GE_CHEATS=invuln,guns",
             "Arena Benchmark": "GE_CHEATS=invuln",
             "Arena Benchmark (Live)": "GE_CHEATS=invuln",
             "Arena Benchmark (Late Waves)": "GE_CHEATS=invuln"}
# cheat-like settings: the environment switches, and Lua settings defaults that would make Bond unkillable or hand out everything
CHEAT_ENV = re.compile(r"\b(GE_CHEATS|GE_COOP_GOD|GE_ARENA_GOD|GE_GOD|GE_INVULN)\b", re.I)
CHEAT_LUA = re.compile(r"\b(invuln|invulnerable|invincible|god|godmode|god_mode|cheats|all_guns|infinite_health|no_damage)\s*=\s*"
                       r"(true|[1-9]|\"[^\"]+\"|'[^']+'|\{)", re.I)
CART_EXT = (".z64", ".v64", ".n64", ".rom")
CART_MAGIC = (b"\x80\x37\x12\x40", b"\x37\x80\x40\x12", b"\x40\x12\x37\x80")   # z64, v64, n64 byte orders


def copy(src, dst):
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copy2(src, dst)


def cheat_scan(files):
    """Every packaged .bat and .lua: a launcher that sets GE_CHEATS (or another cheat switch) outside the allowlist, or a Lua line
    (comments stripped) that names a cheat switch or defaults a cheat-like setting on. Returns (problems, report lines)."""
    bad, report = [], []
    for f in sorted(files):
        base = os.path.basename(f)
        low = f.lower()
        if low.endswith((".bat", ".sh")):
            for n, line in enumerate(open(f, encoding="utf-8", errors="replace"), 1):
                s = line.strip()
                if s.lower().startswith("rem") or s.startswith(("::", "#")) or not CHEAT_ENV.search(s):
                    continue
                if CHEATS_OK.get(os.path.splitext(base)[0]) == re.sub(r"^(set|export)\s+", "", s, flags=re.I):
                    report.append(f"allowed (allowlist): {base}:{n}: {s}")
                else:
                    bad.append(f"{base}:{n} sets a cheat: {s}")
        elif low.endswith(".lua"):
            for n, line in enumerate(open(f, encoding="utf-8", errors="replace"), 1):
                code = line.split("--", 1)[0]
                if CHEAT_ENV.search(code) or CHEAT_LUA.search(code):
                    bad.append(f"{base}:{n} has a cheat-like setting: {line.strip()}")
    report.insert(0, "cheat allowlist: " + "; ".join(f"{k} = {v}" for k, v in CHEATS_OK.items()))
    report.append("cheat scan: " + (f"FAILED, {len(bad)} problem(s)" if bad else
                  f"clean ({sum(1 for f in files if f.lower().endswith(('.bat', '.sh', '.lua')))} launcher / .lua files scanned)"))
    return bad, report


SH_HEAD = """#!/bin/sh
# Made from launchers/{bat} by tools/package_arena.py.
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR" || exit 1
# The system's 32-bit libSDL2 when it has one (PipeWire / PulseAudio, Wayland), else the bundled one in lib/ (X11, ALSA).
if ! {{ /sbin/ldconfig -p 2>/dev/null || ldconfig -p 2>/dev/null; }} | grep -q 'libSDL2-2\\.0\\.so\\.0 (libc6)'; then
    export LD_LIBRARY_PATH="$DIR/lib${{LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}}"
fi
"""


def bat_to_sh(path):
    """A launcher .bat as a POSIX sh script. Knows only the lines the arena launchers use; anything else refuses the package, so a
    new kind of line gets a translation here instead of a broken script."""
    out = [SH_HEAD.format(bat=os.path.basename(path))]
    text = lambda s: s.replace("%~dp0", "$DIR/").replace("\\", "/").replace(".bat", ".sh")
    quote = lambda s: '"' + re.sub(r'(["$`\\])', r"\\\1", s).replace("\\$DIR/", "$DIR/") + '"'
    for line in open(path, encoding="utf-8").read().splitlines():
        ind, s = re.match(r"(\s*)(.*)", line).groups()
        low = s.lower()
        if low in ("@echo off", "setlocal", ""):
            continue
        if low.startswith("rem"):
            out.append(ind + "#" + text(s[3:]))
        elif m := re.fullmatch(r'if not exist "%~dp0([^"]+)" \(', s, re.I):
            out.append(ind + f'if [ ! -f "$DIR/{text(m.group(1))}" ]; then')
        elif s == ")":
            out.append(ind + "fi")
        elif low == "exit /b 1":
            out.append(ind + "exit 1")
        elif low == "pause":
            out.append(ind + "printf 'Press Enter to close. '; read -r _")
        elif low == "echo.":
            out.append(ind + "echo")
        elif low.startswith("echo "):
            out.append(ind + "echo " + quote(text(s[5:])))
        elif m := re.fullmatch(r"set ([A-Z0-9_]+)=(\S*)", s, re.I):
            out.append(ind + f"export {m.group(1)}={m.group(2)}")
        elif s == 'start "" "%~dp0ProjectMidas.exe"':
            out.append(ind + 'exec ./ProjectMidas "$@"')
        elif s == 'start "" /wait "%~dp0ProjectMidas.exe"':
            out.append(ind + './ProjectMidas "$@"')
        else:
            raise SystemExit(f"bat_to_sh: no translation for {os.path.basename(path)}: {line}")
    return "\n".join(out) + "\n"


def rom_blocks(rom, n=512, size=4096):
    data = open(rom, "rb").read()
    step = max(size, (len(data) - size) // n)
    return [data[o:o + size] for o in range(0, len(data) - size, step) if data[o:o + size].count(data[o:o + 1]) != size]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("build")
    ap.add_argument("out")
    ap.add_argument("--tag", required=True)
    ap.add_argument("--rom", required=True, help="the stock US cart (z64): the block scan and check_no_rom_data.py need it")
    ap.add_argument("--nm", help="an nm that reads the exe, for check_no_rom_data.py's symbol check (default: one on PATH)")
    ap.add_argument("--date", default=datetime.datetime.now().strftime("%Y-%m-%d %H:%M"))
    ap.add_argument("--commit")
    ap.add_argument("--platform", choices=("windows", "linux"), default="windows")
    ap.add_argument("--sdl2", help="linux: the 32-bit libSDL2-2.0.so.0 to bundle (the build container's)")
    a = ap.parse_args()
    linux = a.platform == "linux"
    if linux and not a.sdl2:
        sys.exit("--platform linux needs --sdl2")
    commit = a.commit or subprocess.run(["git", "-C", ROOT, "rev-parse", "--short", "HEAD"], capture_output=True, text=True).stdout.strip()

    name = "ProjectMidas-" + a.tag + ("-linux" if linux else "")
    pkg = os.path.join(a.out, name)
    if os.path.exists(pkg):
        sys.exit(f"{pkg} exists: remove it first")
    for f in TOP_LINUX if linux else TOP:
        copy(os.path.join(a.build, f), os.path.join(pkg, f))
    launchers = [f[:-4] + ".sh" for f in LAUNCHERS] if linux else LAUNCHERS
    for f in LAUNCHERS:   # from the repo's sources, never from a build dir someone has played in (and edited)
        if linux:
            with open(os.path.join(pkg, f[:-4] + ".sh"), "w", encoding="utf-8", newline="\n") as fh:
                fh.write(bat_to_sh(os.path.join(ROOT, "launchers", f)))
        else:
            copy(os.path.join(ROOT, "launchers", f), os.path.join(pkg, f))
    if linux:
        copy(a.sdl2, os.path.join(pkg, "lib", "libSDL2-2.0.so.0"))
    copy(os.path.join(a.build, "Data", "shaders_base.txt"), os.path.join(pkg, "Data", "shaders_base.txt"))
    for f in MODES:
        copy(os.path.join(a.build, "Data", "Scripts", "modes", f), os.path.join(pkg, "Data", "Scripts", "modes", f))
    for src, dst in FROM_REPO.items():
        copy(os.path.join(ROOT, src), os.path.join(pkg, dst))
    for d in ("Base", "Options"):
        os.makedirs(os.path.join(pkg, "Data", "Textures", d))
    readme = open(os.path.join(ROOT, "tools", "arena_release_README" + ("_linux" if linux else "") + ".txt"), encoding="utf-8").read()
    readme = readme.replace("@TAG@", a.tag).replace("@COMMIT@", commit).replace("@DATE@", a.date)
    with open(os.path.join(pkg, "README.txt"), "w", encoding="utf-8", newline="\n" if linux else "\r\n") as fh:
        fh.write(readme)

    # ---- no cheats: every packaged .bat and .lua ----
    files = [os.path.join(d, f) for d, _, fs in os.walk(pkg) for f in fs]
    bad, report = cheat_scan(files)

    # ---- Arena only: exactly the arena launchers, and only arena mode scripts ----
    bats = sorted(os.path.relpath(f, pkg) for f in files if f.lower().endswith((".bat", ".sh")))
    if bats != sorted(launchers) or any(not (b.startswith("Play Arena") or b.startswith("Arena Benchmark")) for b in bats):
        bad.append(f"launchers are not exactly the arena set: {bats}")
    luas = sorted(os.path.relpath(f, pkg).replace(os.sep, "/") for f in files if f.lower().endswith(".lua"))
    if any(not re.match(r"Data/Scripts/modes/(arena(-[a-z-]+)?|_arena_[a-z_]+)\.lua$", l) for l in luas):
        bad.append(f"a non-arena script is packaged: {luas}")
    report.append("launchers (" + str(len(bats)) + "): " + "; ".join(bats))
    print(report[-1])
    for line in report:
        print(line)
    for b in bad:
        print("  " + b)

    # ---- no cart image ----
    bad += [f for f in files if f.lower().endswith(CART_EXT)]
    for f in files:
        with open(f, "rb") as fh:
            if fh.read(4) in CART_MAGIC:
                bad.append(f)
    if a.rom:
        blocks = rom_blocks(a.rom)
        for f in files:
            data = open(f, "rb").read()
            hits = sum(1 for b in blocks if b in data)
            if hits:
                bad.append(f"{f} ({hits} of {len(blocks)} sampled cart blocks)")
        if not bad:
            report.append(f"cart check: no cart extension, no N64 header, none of {len(blocks)} sampled 4 KB blocks of the cart in any file")
    else:
        report.append("cart check: no cart extension, no N64 header (no --rom: the block sample was not run)")
    print(report[-1])

    # ---- no cart data in the exe: tools/check_no_rom_data.py on the packaged exe (its exit code decides) ----
    cmd = [sys.executable, os.path.join(ROOT, "tools", "check_no_rom_data.py"), os.path.join(pkg, TOP_LINUX[0] if linux else TOP[0]),
           "--rom", a.rom, "--build", a.build, "--repo", ROOT, "--quiet"] + (["--nm", a.nm] if a.nm else [])
    print("check_no_rom_data.py: running (a few minutes)")
    r = subprocess.run(cmd, capture_output=True, text=True)
    out = (r.stdout + r.stderr).strip().splitlines()
    for line in out:
        print("  " + line)
    report.append(f"check_no_rom_data.py (exit {r.returncode}):")
    report += ["  " + line for line in out]
    if r.returncode != 0:
        bad.append(f"check_no_rom_data.py failed (exit {r.returncode})")

    if bad:
        shutil.rmtree(pkg)
        sys.exit("refused: " + "; ".join(bad))

    total = sum(os.path.getsize(f) for f in files)
    report.append(f"package: {pkg} ({len(files)} files, {total} bytes)")
    if linux:
        # modes set here, not read from disk: the package dir may sit on a drive that keeps no exec bits
        zpath = os.path.join(a.out, "OculusAureus-" + a.tag + "-linux.tar.gz")
        def mode(t):
            t.uid = t.gid = 0
            t.uname = t.gname = ""
            t.mode = 0o755 if t.isdir() or t.name.endswith((".sh", "/ProjectMidas", ".so.0")) else 0o644
            return t
        with tarfile.open(zpath, "w:gz", compresslevel=9) as z:
            z.add(pkg, name, filter=mode)
        report.append(f"tar: {zpath} ({os.path.getsize(zpath)} bytes), commit {commit}")
        with tarfile.open(zpath) as z:
            report.append("tar listing:")
            report += [f"  {oct(i.mode)} {i.size:>10}  {i.name}" for i in z.getmembers()]
    else:
        zpath = os.path.join(a.out, "OculusAureus-" + a.tag + "-windows.zip")
        with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
            for d, dirs, fs in sorted(os.walk(pkg)):
                rel = os.path.relpath(d, a.out).replace(os.sep, "/")
                if not fs and not dirs:
                    z.writestr(rel + "/", "")   # the empty Data/Textures folders, as in arena-v0.2
                for f in sorted(fs):
                    z.write(os.path.join(d, f), rel + "/" + f)
        report.append(f"zip: {zpath} ({os.path.getsize(zpath)} bytes), commit {commit}")
        with zipfile.ZipFile(zpath) as z:
            report.append("zip listing:")
            report += [f"  {i.file_size:>10}  {i.filename}" for i in z.infolist()]
    with open(os.path.join(a.out, name + "-package-report.txt"), "w", encoding="utf-8") as fh:
        fh.write("\n".join(report) + "\n")
    print(f"package: {pkg} ({len(files)} files, {total} bytes)")
    print(f"archive: {zpath} ({os.path.getsize(zpath)} bytes), commit {commit}")
    print(f"report: {os.path.join(a.out, name + '-package-report.txt')}")


if __name__ == "__main__":
    main()
