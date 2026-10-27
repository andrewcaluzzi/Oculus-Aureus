#!/usr/bin/env python3
"""check_no_rom_data.py - check that a built Project Midas exe carries no data taken from the GoldenEye cart.

usage: python3 tools/check_no_rom_data.py <exe> --rom <stock cart> [--build <build dir>] [--nm <nm>] [--assets <dir>]
                                          [--repo <checkout>] [--block 32] [--quiet]

The game's data must come from the player's own cart at runtime (port/src/romdata.c). Data that is already tracked
in this repository's git (the decompilation's oddtextures.c, rarewarelogo.c, font_dl.c, ...) is not the builder's
cart, so it is allowed; anything built from files extracted from the builder's cart is not. Three checks:

1. symbols (needs an nm that reads the exe, --nm or nm / i686-w64-mingw32-nm on PATH): no asset file symbol of
   port/generated/asset_manifest.cmake may be defined in the exe unless its source file is git-tracked.
2. provenance (needs --build): every object the exe links, and every file its depfile says the compiler read, must
   be git-tracked, a system/toolchain file, or a file the build itself generated outside assets/. An untracked
   input (an extracted .bin, a generated Model.c, ...) is a failure.
3. bytes (needs --rom): every 32-byte block of the cart and of every 1172-compressed stream in it (inflated: the
   files of its directory, and the room display lists and point tables packed inside the bg files), in its
   raw, 16-bit-swapped and 32-bit-swapped byte orders (compiled-in data is little-endian), is looked up at every
   offset of the exe. Blocks with fewer than 6 distinct byte values are skipped, so a match means a run of real
   cart data (at least 2 x block - 1 bytes long is always caught). --assets adds the untracked .bin files under a
   folder (the extracted assets) the same way. A run is allowed only when the same bytes are in an object compiled
   from a git-tracked source (needs --build); every other run fails.

Exit 0 when every check that ran passed, 1 when one failed, 2 on a usage error. Without --build, any byte match
fails, because it cannot be told apart from tracked data.
Not covered: the cart's texture bank is in Rare's own image compression, so its images are matched only in their
packed form; texels decoded at build time from the cart would be caught by the provenance check, not the byte scan.
"""
import argparse, bisect, os, re, shutil, struct, subprocess, sys, zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MIN_DISTINCT = 6


def swap16(b):
    n = len(b) & ~1
    a = bytearray(b[:n])
    a[0::2], a[1::2] = b[1:n:2], b[0:n:2]
    return bytes(a)


def swap32(b):
    n = len(b) & ~3
    a = bytearray(b[:n])
    a[0::4], a[1::4], a[2::4], a[3::4] = b[3:n:4], b[2:n:4], b[1:n:4], b[0:n:4]
    return bytes(a)


def git_tracked(repo):
    out = subprocess.run(["git", "-C", repo, "ls-files", "-z"], capture_output=True, check=True).stdout
    return set(p.decode() for p in out.split(b"\0") if p)


def manifest(repo):
    """(source, file symbol) for every compiled-in asset the build knows about: the generated manifest, and any
    port_add_asset_* calls in CMakeLists.txt itself (${VAR} taken from a set(VAR "value" ...) there)"""
    out = []
    top = open(os.path.join(repo, "CMakeLists.txt"), encoding="utf-8").read()
    var = dict(re.findall(r'set\(\s*(\w+)\s+"([^"$]*)"', top))
    for path in (os.path.join(repo, "port", "generated", "asset_manifest.cmake"), os.path.join(repo, "CMakeLists.txt")):
        text = open(path, encoding="utf-8").read()
        for src, sym in re.findall(r"^\s*port_add_asset_(?:seg|direct)\(\s*(\S+)\s+(\S+)\s*\)", text, re.M):
            out.append((re.sub(r"\$\{(\w+)\}", lambda m: var.get(m.group(1), m.group(0)), src), sym))
    return out


# ---- exe layout: file offset -> section, address --------------------------------------------------------------

def sections(exe):
    """[(name, file offset, size, address)] for a PE32 or ELF32 file"""
    out = []
    if exe[:2] == b"MZ":
        pe = struct.unpack_from("<I", exe, 0x3C)[0]
        nsec, = struct.unpack_from("<H", exe, pe + 6)
        symtab, nsym = struct.unpack_from("<II", exe, pe + 12)
        optsize, = struct.unpack_from("<H", exe, pe + 20)
        base, = struct.unpack_from("<I", exe, pe + 24 + 28)
        strtab = symtab + nsym * 18
        for i in range(nsec):
            s = pe + 24 + optsize + i * 40
            name = exe[s:s + 8].rstrip(b"\0").decode("latin-1")
            vsize, va, rawsize, rawptr = struct.unpack_from("<IIII", exe, s + 8)
            if name.startswith("/") and symtab:
                o = strtab + int(name[1:])
                name = exe[o:exe.index(b"\0", o)].decode("latin-1")
            out.append((name, rawptr, rawsize, base + va))
    elif exe[:4] == b"\x7fELF":
        shoff, = struct.unpack_from("<I", exe, 0x20)
        shentsize, shnum, shstrndx = struct.unpack_from("<HHH", exe, 0x2E)
        hdrs = [struct.unpack_from("<IIIIII", exe, shoff + i * shentsize) for i in range(shnum)]
        stroff = hdrs[shstrndx][4]
        for name, typ, flags, addr, off, size in hdrs:
            if typ == 8:   # SHT_NOBITS
                continue
            nm = exe[stroff + name:exe.index(b"\0", stroff + name)].decode("latin-1")
            out.append((nm, off, size, addr))
    return out


def where(secs, off):
    for name, o, size, addr in secs:
        if o <= off < o + size:
            return name, (addr + off - o) if addr else None
    return "(headers)", None


def find_nm(exe, want):
    if want:
        return want
    names = ["i686-w64-mingw32-nm", "nm"] if exe[:2] == b"MZ" else ["nm"]
    for n in names:
        if shutil.which(n):
            return n
    return None


def nm_symbols(nm, path):
    """[(address or None, type, name)] from nm"""
    r = subprocess.run([nm, path], capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError(r.stderr.strip() or "nm failed")
    syms = []
    for line in r.stdout.splitlines():
        parts = line.split()
        if len(parts) == 3:
            syms.append((int(parts[0], 16), parts[1], parts[2]))
        elif len(parts) == 2:
            syms.append((None, parts[0], parts[1]))
    return syms


# ---- check 1: asset file symbols ------------------------------------------------------------------------------

def check_symbols(syms, assets, tracked, pe):
    bad, report = [], []
    defined = {}
    for addr, typ, name in syms:
        if typ in "Uwv" or addr in (None, 0):
            continue
        if pe and name.startswith("_"):
            name = name[1:]
        defined[name] = (addr, typ)
    n = 0
    for src, sym in assets:
        if sym in defined:
            n += 1
            addr, typ = defined[sym]
            line = f"{sym} defined at 0x{addr:08X} ({typ}), from {src}"
            if src in tracked:
                report.append("  allowed (git-tracked source): " + line)
            else:
                bad.append("asset symbol " + line + " (untracked: built from the extracted cart)")
    report.insert(0, f"symbols: {len(assets)} asset file symbols in the manifest, {n} defined in the exe, "
                     f"{len(bad)} from untracked sources")
    return bad, report


# ---- check 2: provenance of every linked object --------------------------------------------------------------

def link_objects(build):
    """the object files of the ge target's link line (CMake Makefiles: link.txt and its @response files)"""
    d = os.path.join(build, "CMakeFiles", "ge.dir")
    text = open(os.path.join(d, "link.txt"), encoding="utf-8").read()
    toks = text.split()
    objs = []
    while toks:
        t = toks.pop(0)
        if t.startswith("@"):
            p = os.path.join(build, t[1:])
            if os.path.exists(p):
                toks = open(p, encoding="utf-8").read().split() + toks
            continue
        if t.endswith((".obj", ".o")):
            objs.append(os.path.normpath(os.path.join(build, t)))
    return objs


def depfile_inputs(path):
    """the prerequisites of a gcc -MD depfile"""
    text = open(path, encoding="utf-8", errors="replace").read().replace("\\\n", " ")
    deps = []
    for line in text.splitlines():
        if ":" not in line or line.startswith("\t"):
            continue
        # target: deps (a Windows drive letter never appears in these Linux-built depfiles)
        _, rest = line.split(": ", 1) if ": " in line else line.split(":", 1)
        rest = rest.replace("\\ ", "\0")
        deps += [t.replace("\0", " ") for t in rest.split()]
    return deps


def rel_to(path, root):
    path = os.path.normpath(os.path.abspath(path))
    root = os.path.normpath(os.path.abspath(root))
    if path == root or path.startswith(root + os.sep):
        return os.path.relpath(path, root).replace(os.sep, "/")
    return None


def check_provenance(build, repo, tracked, assets):
    bad, report, clean = [], [], []
    src_of_asset = {sym: src for src, sym in assets}
    objs = link_objects(build)
    gdir = os.path.join(build, "CMakeFiles", "ge.dir")
    nodep, generated = 0, set()
    for o in objs:
        r = rel_to(o, gdir)
        if r is not None:
            src = re.sub(r"\.(obj|o)$", "", r)
        elif os.path.basename(os.path.dirname(o)) == "asset_obj":
            sym = re.sub(r"\.o$", "", os.path.basename(o))
            src = src_of_asset.get(sym, "?" + sym)
        else:
            src = rel_to(o, repo) or o
        problems = []
        if src not in tracked:
            problems.append(f"source {src} is not git-tracked")
        dep = o + ".d"
        if os.path.exists(dep):
            for p in depfile_inputs(dep):
                rp = rel_to(os.path.join(build, p) if not os.path.isabs(p) else p, repo)
                if rp is None:
                    continue                      # system / toolchain header
                rb = rel_to(os.path.join(repo, rp), build)
                if rb is not None:
                    generated.add(rb)             # generated by this build (buildinfo, ...)
                    continue
                if rp not in tracked:
                    problems.append(f"reads untracked {rp}")
        elif r is not None:
            nodep += 1
        if problems:
            bad += [f"{os.path.relpath(o, build)}: {p}" for p in problems]
        else:
            clean.append(o)
    report.append(f"provenance: {len(objs)} linked objects, {len(clean)} from git-tracked inputs only, "
                  f"{len(objs) - len(clean)} not" + (f" ({nodep} without a depfile: source checked only)" if nodep else ""))
    for g in sorted(generated):
        if g.split("/")[0] == "assets" or g.endswith(".bin"):
            bad.append(f"build-generated input {g} looks like cart data")
        report.append(f"  build-generated input: {g}")
    return bad, report, clean


# ---- check 3: cart bytes --------------------------------------------------------------------------------------

def cart_sources(rom, repo, assets_dir, tracked):
    """[(label, bytes, cart offset or None)] - the cart and its inflated files"""
    srcs = [("cart", rom, 0)]
    rows = []
    fl = os.path.join(repo, "scripts", "filelist.u.csv")
    for line in open(fl, encoding="utf-8"):
        f = line.strip().split(",")
        if len(f) >= 4 and f[0].isdigit():
            rows.append((int(f[0]), int(f[1]), f[2], f[3] == "1"))
    rows.sort()
    inflated = 0
    whole = set()
    for off, size, path, comp in rows:
        blob = rom[off:off + size]
        if comp and blob[:2] == b"\x11\x72":
            try:
                data = zlib.decompressobj(-15).decompress(blob[2:])
            except zlib.error:
                continue
            srcs.append((path + " (inflated)", data, None))
            whole.add(off)
            inflated += 1
    # every other 1172 stream in the cart: the room display lists and point tables packed inside the bg files, ...
    o = rom.find(b"\x11\x72")
    while o >= 0:
        if o not in whole:
            d = zlib.decompressobj(-15)
            try:
                data = d.decompress(rom[o + 2:o + 2 + 0x40000], 0x100000)
            except zlib.error:
                data = b""
            if d.eof and len(data) >= 64:
                i = bisect.bisect_right(rows, (o, 1 << 62)) - 1
                inside = rows[i][2] if i >= 0 and o < rows[i][0] + rows[i][1] else "cart"
                srcs.append((f"{inside} 1172 stream @0x{o:06X} (inflated)", data, None))
                inflated += 1
        o = rom.find(b"\x11\x72", o + 1)
    if assets_dir:
        for d, _, fs in os.walk(assets_dir):
            for f in fs:
                p = os.path.join(d, f)
                rp = rel_to(p, repo)
                if f.endswith(".bin") and (rp is None or rp not in tracked):
                    srcs.append((rp or p, open(p, "rb").read(), None))
    return srcs, rows, inflated


def build_index(srcs, block):
    index, kept = {}, 0
    forms = []
    for sid, (label, data, _) in enumerate(srcs):
        for fid, form in enumerate((data, swap16(data), swap32(data))):
            key = sid * 3 + fid
            forms.append(form)
            for o in range(0, len(form) - block + 1, block):
                b = form[o:o + block]
                if len(set(b)) >= MIN_DISTINCT:
                    index[hash(b)] = (key << 32) | o
                    kept += 1
    return index, forms, kept


def scan(exe, index, forms, block):
    hits = []
    get = index.get
    for o in range(0, len(exe) - block + 1):
        v = get(hash(exe[o:o + block]))
        if v is not None:
            key, so = v >> 32, v & 0xFFFFFFFF
            if forms[key][so:so + block] == exe[o:o + block]:
                hits.append((o, key, so))
    runs = []
    for o, key, so in hits:
        if runs and o <= runs[-1][1]:
            runs[-1][1] = max(runs[-1][1], o + block)
            runs[-1][2].add((key, so))
        else:
            runs.append([o, o + block, {(key, so)}])
    return runs


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("exe")
    ap.add_argument("--rom", help="the stock US cart (z64 order), for the byte scan")
    ap.add_argument("--build", help="the exe's CMake build dir, for provenance and the tracked-data allowance")
    ap.add_argument("--repo", default=ROOT, help="the checkout the exe was built from (default: this one)")
    ap.add_argument("--nm", help="nm that reads the exe (default: i686-w64-mingw32-nm or nm on PATH)")
    ap.add_argument("--assets", help="also scan for the untracked .bin files under this folder (extracted assets)")
    ap.add_argument("--tracked", help="a `git ls-files` listing to use instead of running git (e.g. in a container)")
    ap.add_argument("--block", type=int, default=32)
    ap.add_argument("--quiet", action="store_true", help="summary lines and failures only")
    a = ap.parse_args()

    exe = open(a.exe, "rb").read()
    repo = os.path.abspath(a.repo)
    if a.tracked:
        tracked = set(l.strip() for l in open(a.tracked, encoding="utf-8") if l.strip())
    else:
        tracked = git_tracked(repo)
    assets = manifest(repo)
    pe = exe[:2] == b"MZ"
    secs = sections(exe)
    bad, report, ran = [], [], []
    print(f"exe: {a.exe} ({len(exe)} bytes, {'PE32' if pe else 'ELF32'}, {len(secs)} sections); repo {repo}")

    syms = []
    nm = find_nm(exe, a.nm)
    if nm:
        try:
            syms = nm_symbols(nm, a.exe)
        except RuntimeError as e:
            report.append(f"symbols: not checked ({nm}: {e})")
        else:
            b, r = check_symbols(syms, assets, tracked, pe)
            bad += b; report += r; ran.append("symbols")
    else:
        report.append("symbols: not checked (no nm: pass --nm)")

    clean = []
    if a.build:
        b, r, clean = check_provenance(os.path.abspath(a.build), repo, tracked, assets)
        bad += b; report += r; ran.append("provenance")
    else:
        report.append("provenance: not checked (no --build)")

    if a.rom:
        rom = open(a.rom, "rb").read()
        if rom[:4] != b"\x80\x37\x12\x40":
            sys.exit("--rom: not a z64-order cart image")
        srcs, rows, inflated = cart_sources(rom, repo, a.assets, tracked)
        index, forms, kept = build_index(srcs, a.block)
        runs = scan(exe, index, forms, a.block)
        corpus = b"\0".join(open(o, "rb").read() for o in clean)
        starts = [r[0] for r in rows]
        symaddr = sorted((s[0], s[2]) for s in syms if s[0] and s[1] not in "Uwv")
        allowed = 0
        for start, end, keys in runs:
            run = exe[start:end]
            p = corpus.find(run[:a.block]) if corpus else -1
            ok = p >= 0 and corpus[p:p + len(run)] == run
            if not ok and corpus:
                # data that spans two objects (adjacent in the exe, separate in the corpus): allowed when every byte is
                # covered by a 16-byte window that is in some tracked object (zero bytes are padding: covered)
                covered = bytearray(1 if c == 0 else 0 for c in run)
                for i in range(0, len(run) - 15):
                    if not all(covered[i:i + 16]) and run[i:i + 16] in corpus:
                        covered[i:i + 16] = b"\1" * 16
                if len(run) >= 16 and run[-16:] in corpus:
                    covered[-16:] = b"\1" * 16
                ok = all(covered)
            sec, va = where(secs, start)
            sym = ""
            if va is not None and symaddr:
                i = bisect.bisect_right(symaddr, (va, "￿")) - 1
                if i >= 0:
                    sym = f" {symaddr[i][1]}+0x{va - symaddr[i][0]:X}"
            key, so = sorted(keys)[0]
            label, _, _ = srcs[key // 3]
            form = ("raw", "16-bit swapped", "32-bit swapped")[key % 3]
            if key // 3 == 0:
                i = bisect.bisect_right(starts, so) - 1
                label = f"cart 0x{so:06X}" + (f" ({rows[i][2]})" if i >= 0 and so < rows[i][0] + rows[i][1] else "")
            else:
                label = f"{label} +0x{so:X}"
            line = (f"0x{start:08X}-0x{end:08X} ({end - start} bytes) in {sec}"
                    + (f" @0x{va:08X}" if va is not None else "") + sym + f": {label}, {form}")
            if ok:
                allowed += 1
                if not a.quiet:
                    report.append("  allowed (same bytes in a git-tracked object): " + line)
            else:
                bad.append("cart data " + line)
        report.append(f"bytes: {kept} blocks of {a.block} bytes indexed from the cart and {inflated} inflated 1172 streams"
                      + (f" (+{len(srcs) - 1 - inflated} extracted .bin)" if a.assets else "")
                      + f" in 3 byte orders; {len(runs)} matching runs in the exe, {allowed} allowed (git-tracked data), "
                      f"{len(runs) - allowed} not")
        ran.append("bytes")
    else:
        report.append("bytes: not checked (no --rom)")

    for line in report:
        if not (a.quiet and line.startswith("  ")):
            print(line)
    for b in bad:
        print("FAIL: " + b)
    if not ran:
        print("nothing checked")
        return 2
    print(("FAILED: " if bad else "PASSED: ") + ", ".join(ran) + (f" ({len(bad)} problems)" if bad else ""))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
