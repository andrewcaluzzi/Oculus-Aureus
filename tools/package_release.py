#!/usr/bin/env python3
"""package_release.py - the Oculus Aureus hub release package: a folder and an archive, with no cart image, cart data or cheats in it.

usage: py -3 tools/package_release.py <build dir> <out dir> --version v0.4 --rom <stock cart (z64), to prove that neither the
       package nor the exe holds its data> [--platform linux --sdl2 <libSDL2-2.0.so.0>] [--nm <nm>] [--date "YYYY-MM-DD HH:MM"]
       [--commit <sha>]

Makes <out>/OculusAureus-<version>/ and <out>/OculusAureus-<version>-windows.zip (or, with --platform linux, run on Windows or
Linux from a Linux build dir, <out>/OculusAureus-<version>-linux/ and OculusAureus-<version>-linux.tar.gz: ProjectMidas instead of
the exe, lib/libSDL2-2.0.so.0 from --sdl2, and each launcher as a .sh made by package_arena.bat_to_sh()).
A new release is a re-run with a new --version; nothing else in this file names a version.

The version comes from ONE place: the build's GE_RELEASE_NAME (CMake cache: the window title, the legal screen's build line and
the credits show it, port/src/sysstubs.c portPlatformName). The package is refused unless the build's GE_RELEASE_NAME ends with
" <version>" and that string is in the exe.

Contents (exact lists, below): the exe, SDL2.dll (Windows), the controller files, midas_ini_help.txt, LAUNCHERS (from the repo's
launchers/ folder, never the build dir), README.txt (tools/release_README.txt for the platform), README-FORK.md, LICENSE.txt,
THIRD_PARTY_NOTICES.txt, Data/README.txt, Data/shaders_base.txt, Data/Sounds/README.txt, Data/Scripts/modes/ = MODES (the
scripts the hub's shipped tiles reach: the hub, the Arena, the Heist on bank and casino, the Sniper Duel v1 and v2, the generated
levels' layouts), empty Data/Textures/Base and Options, Mods/README.txt.
Never copied: a cart image, Data/Saves, Data/Devices, Data/Logs, midas.ini, the bot plans, the test / probe / bench / hostage /
bank-walk / heist-bot modes, the Dam+ card (its hub tile ships hidden), the direct-launch prototype wrappers.

Checks (the package is refused when one fails): the launchers and the mode scripts are exactly the lists; no packaged .bat / .sh /
.lua sets a cheat (no allowlist: no launcher may set GE_CHEATS); no file a run writes (saves, logs, devices, midas.ini, test
drivers); no file with a cart-image extension or an N64 cart header; none of 512 sampled 4 KB cart blocks in any file; and
tools/check_no_rom_data.py passes on the packaged exe (cart bytes, the build's provenance, the asset symbols with an nm)."""
import argparse, datetime, os, re, shutil, subprocess, sys, tarfile, zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import package_arena as pa   # copy, cheat_scan, bat_to_sh, rom_blocks, the cart checks

pa.CHEATS_OK.clear()         # no cheat allowlist in a hub release: every GE_CHEATS line refuses the package

ROOT = pa.ROOT
LAUNCHERS = ["Play Oculus Aureus.bat"]
TOP = ["ProjectMidas.exe", "SDL2.dll", "gamecontrollerdb.txt", "gamecontrollerdefault.txt", "midas_ini_help.txt"]
TOP_LINUX = ["ProjectMidas", "gamecontrollerdb.txt", "gamecontrollerdefault.txt", "midas_ini_help.txt"]
MODES = [
    # the hub (GE_MODE=hub on Control) and the menu layer every lobby is built on
    "hub.lua", "_hub_defs.lua", "_ui_menu.lua",
    # ARENA: the lobby, the run and its presets (the lobby launches them), its modules
    "arena-menu.lua", "arena.lua", "arena-endless.lua", "arena-chaos.lua", "arena-chaos-endless.lua", "arena-coop.lua",
    "arena-coop-chaos.lua", "arena-whacky.lua", "_arena_coop.lua", "_arena_crowd.lua", "_arena_elites.lua", "_arena_flow.lua",
    "_arena_maps.lua", "_arena_menu_defs.lua", "_arena_mutators.lua", "_arena_plus.lua", "_arena_polish.lua", "_arena_scores.lua",
    "_arena_theme.lua", "_arena_whacky.lua",
    # HEIST: the lobby, the run, its modules, the two jobs
    "heist-menu.lua", "heist.lua", "_heist_flow.lua", "_heist_menu_defs.lua", "_heist_theme.lua", "heist_lib.lua", "heist_ai.lua",
    "heist_ar.lua", "heist_bank_map.lua", "heist_debrief.lua", "heist_gadgets.lua", "heist_guide.lua", "heist_hostages.lua",
    "heist_intel.lua", "heist_interlock.lua", "heist_planning.lua", "heist_randomiser.lua", "heist_traits.lua", "job_bank.lua",
    "job_casino.lua",
    # SNIPER DUEL: the card, the v1 duel (ridge, surface), the v2 duel (snowfield, jungle)
    "sniper-menu.lua", "sniper.lua", "_sniper_defs.lua", "sniper-v2.lua", "_sniper_v2_defs.lua",
    # the generated levels' layouts (bank, the default, twice: levelgen_layout.lua is the default's)
    "levelgen_layout.lua", "levelgen_layout_bank.lua", "levelgen_layout_casino.lua", "levelgen_layout_ridge.lua",
    "levelgen_layout_snowfield.lua", "levelgen_layout_jungle.lua",
]
FROM_REPO = pa.FROM_REPO
# files a run writes, or a test leaves behind: never in a package
RUN_FILES = re.compile(r"(^|/)(Data/(Saves|Logs|Devices|Profiles|alltest_ui|BotPlans|BotRoutes)/|midas\.ini$|ge\.ini$|zz_[^/]*$|"
                       r"[^/]*\.(log|gedevice|geprofile|gestats|bin|bak|tmp)$|gamecontrolleruser\.txt$)", re.I)


def readme(platform, version, commit, date):
    """tools/release_README.txt for one platform: @IF <platform>@ ... @END@ (a whole-line pair, or inline) kept or dropped,
    @SEP@ the path separator, @VERSION@ @COMMIT@ @DATE@ filled in"""
    out, keep = [], [True]
    for line in open(os.path.join(ROOT, "tools", "release_README.txt"), encoding="utf-8").read().splitlines():
        if m := re.fullmatch(r"@IF (\w+)@", line.strip()):
            keep.append(keep[-1] and m.group(1) == platform)
        elif line.strip() == "@END@":
            keep.pop()
        elif keep[-1]:
            out.append(line)
    text = "\n".join(out) + "\n"
    text = re.sub(r"@IF (\w+)@(.*?)@END@", lambda m: m.group(2) if m.group(1) == platform else "", text)
    if "@IF" in text or "@END@" in text:
        raise SystemExit("release_README.txt: an @IF@ without its @END@")
    return (text.replace("@SEP@", "/" if platform == "linux" else "\\").replace("@VERSION@", version)
            .replace("@COMMIT@", commit).replace("@DATE@", date))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("build")
    ap.add_argument("out")
    ap.add_argument("--version", required=True, help="the release's version, e.g. v0.4 (the package and archive names carry it)")
    ap.add_argument("--rom", required=True, help="the stock US cart (z64): the block scan and check_no_rom_data.py need it")
    ap.add_argument("--nm", help="an nm that reads the exe, for check_no_rom_data.py's symbol check (default: one on PATH)")
    ap.add_argument("--date", default=datetime.datetime.now().strftime("%Y-%m-%d %H:%M"))
    ap.add_argument("--commit")
    ap.add_argument("--platform", choices=("windows", "linux"), default="windows")
    ap.add_argument("--sdl2", help="linux: the 32-bit libSDL2-2.0.so.0 to bundle (the build container's)")
    a = ap.parse_args()
    linux = a.platform == "linux"
    if not re.fullmatch(r"v\d+\.\d+(\.\d+)?(-[a-z0-9]+)?", a.version):
        sys.exit(f"--version {a.version}: want v<major>.<minor>[.<patch>][-<label>], e.g. v0.4")
    if linux and not a.sdl2:
        sys.exit("--platform linux needs --sdl2")
    commit = a.commit or subprocess.run(["git", "-C", ROOT, "rev-parse", "--short", "HEAD"], capture_output=True, text=True).stdout.strip()
    bad, report = [], []

    # ---- the version: the build's GE_RELEASE_NAME, the one place it is set ----
    exe_name = TOP_LINUX[0] if linux else TOP[0]
    cache = {}
    for line in open(os.path.join(a.build, "CMakeCache.txt"), encoding="utf-8", errors="replace"):
        if m := re.match(r"^(GE_RELEASE_NAME|GE_LEVELGEN_LEVELS|GE_LEVELGEN_DIR|CMAKE_BUILD_TYPE|GE_ROMDATA_FALLBACK):\w+=(.*)$", line.rstrip("\r\n")):
            cache[m.group(1)] = m.group(2)
    rel = cache.get("GE_RELEASE_NAME", "")
    exe_bytes = open(os.path.join(a.build, exe_name), "rb").read()
    if not rel.endswith(" " + a.version):
        bad.append(f"the build's GE_RELEASE_NAME is '{rel}', not '<name> {a.version}': configure it with -DGE_RELEASE_NAME=\"Oculus Aureus {a.version}\"")
    elif rel.encode() + b"\0" not in exe_bytes:
        bad.append(f"the exe does not carry '{rel}' (rebuild after configuring GE_RELEASE_NAME)")
    if cache.get("GE_ROMDATA_FALLBACK", "OFF").upper() not in ("OFF", "0", "FALSE", "NO", ""):
        bad.append("the build has GE_ROMDATA_FALLBACK on (compiled-in cart data)")
    if cache.get("GE_LEVELGEN_DIR"):
        bad.append(f"the build is a one-level GE_LEVELGEN_DIR build ({cache['GE_LEVELGEN_DIR']}): the hub needs every level")
    report.append(f"build: {a.build}; GE_RELEASE_NAME '{rel}', CMAKE_BUILD_TYPE {cache.get('CMAKE_BUILD_TYPE')}, "
                  f"GE_LEVELGEN_LEVELS {cache.get('GE_LEVELGEN_LEVELS', '(default)')}")
    if bad:
        sys.exit("refused: " + "; ".join(bad))

    name = "OculusAureus-" + a.version + ("-linux" if linux else "")
    pkg = os.path.join(a.out, name)
    if os.path.exists(pkg):
        sys.exit(f"{pkg} exists: remove it first")
    for f in TOP_LINUX if linux else TOP:
        pa.copy(os.path.join(a.build, f), os.path.join(pkg, f))
    launchers = [f[:-4] + ".sh" for f in LAUNCHERS] if linux else LAUNCHERS
    for f in LAUNCHERS:   # from the repo's sources, never from a build dir someone has played in (and edited)
        if linux:
            with open(os.path.join(pkg, f[:-4] + ".sh"), "w", encoding="utf-8", newline="\n") as fh:
                fh.write(pa.bat_to_sh(os.path.join(ROOT, "launchers", f)).replace("tools/package_arena.py", "tools/package_release.py"))
        else:
            pa.copy(os.path.join(ROOT, "launchers", f), os.path.join(pkg, f))
    if linux:
        pa.copy(a.sdl2, os.path.join(pkg, "lib", "libSDL2-2.0.so.0"))
    pa.copy(os.path.join(a.build, "Data", "shaders_base.txt"), os.path.join(pkg, "Data", "shaders_base.txt"))
    for f in MODES:
        pa.copy(os.path.join(a.build, "Data", "Scripts", "modes", f), os.path.join(pkg, "Data", "Scripts", "modes", f))
    for src, dst in FROM_REPO.items():
        pa.copy(os.path.join(ROOT, src), os.path.join(pkg, dst))
    for d in ("Base", "Options"):
        os.makedirs(os.path.join(pkg, "Data", "Textures", d))
    with open(os.path.join(pkg, "README.txt"), "w", encoding="utf-8", newline="\n" if linux else "\r\n") as fh:
        fh.write(readme(a.platform, a.version, commit, a.date))

    # ---- no cheats: every packaged .bat / .sh / .lua (no allowlist) ----
    files = [os.path.join(d, f) for d, _, fs in os.walk(pkg) for f in fs]
    b, r = pa.cheat_scan(files)
    bad += b
    report += r

    # ---- exactly the launchers and the mode scripts; nothing a run writes ----
    rels = sorted(os.path.relpath(f, pkg).replace(os.sep, "/") for f in files)
    bats = [x for x in rels if x.lower().endswith((".bat", ".sh"))]
    if bats != sorted(launchers):
        bad.append(f"launchers are not exactly {sorted(launchers)}: {bats}")
    luas = [x for x in rels if x.lower().endswith(".lua")]
    want = sorted("Data/Scripts/modes/" + m for m in MODES)
    if luas != want:
        bad.append(f"the mode scripts are not exactly MODES: extra {sorted(set(luas) - set(want))}, missing {sorted(set(want) - set(luas))}")
    runf = [x for x in rels if RUN_FILES.search(x)]
    if runf:
        bad.append(f"files a run writes or a test leaves: {runf}")
    report.append("launchers (" + str(len(bats)) + "): " + "; ".join(bats))
    report.append(f"mode scripts: {len(luas)} (exactly MODES)" if luas == want else f"mode scripts: {len(luas)} (NOT MODES)")
    for line in report:
        print(line)
    for x in bad:
        print("  " + x)

    # ---- no cart image ----
    bad += [f for f in files if f.lower().endswith(pa.CART_EXT)]
    for f in files:
        with open(f, "rb") as fh:
            if fh.read(4) in pa.CART_MAGIC:
                bad.append(f)
    blocks = pa.rom_blocks(a.rom)
    for f in files:
        data = open(f, "rb").read()
        hits = sum(1 for blk in blocks if blk in data)
        if hits:
            bad.append(f"{f} ({hits} of {len(blocks)} sampled cart blocks)")
    report.append(f"cart check: no cart extension, no N64 header, none of {len(blocks)} sampled 4 KB blocks of the cart in any file"
                  if not bad else "cart check: see the failures")
    print(report[-1])

    # ---- no cart data in the exe: tools/check_no_rom_data.py on the packaged exe (its exit code decides) ----
    cmd = [sys.executable, os.path.join(ROOT, "tools", "check_no_rom_data.py"), os.path.join(pkg, exe_name),
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
        zpath = os.path.join(a.out, "OculusAureus-" + a.version + "-linux.tar.gz")
        def mode(t):   # modes set here, not read from disk: the package dir may sit on a drive that keeps no exec bits
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
        zpath = os.path.join(a.out, "OculusAureus-" + a.version + "-windows.zip")
        with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
            for d, dirs, fs in sorted(os.walk(pkg)):
                rel_d = os.path.relpath(d, a.out).replace(os.sep, "/")
                if not fs and not dirs:
                    z.writestr(rel_d + "/", "")   # the empty Data/Textures folders
                for f in sorted(fs):
                    z.write(os.path.join(d, f), rel_d + "/" + f)
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
