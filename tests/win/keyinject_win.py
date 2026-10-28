"""keyinject_win.py <build dir> [mode]: the arena test keys on Windows, hidden mode, with SDL key events (GE_KEYINJECT).

A hidden window never gets the focus, so posted WM_KEYDOWN messages never become SDL keys (tests/win/realkeys_win.py: they do not).
GE_KEYINJECT pushes real SDL_KEYDOWN / SDL_KEYUP events into SDL's queue instead, so everything from SDL's event watch on is the
real path: inputFixedKeyDown -> portLuaTestKey -> the Lua test_key handlers. The bat's environment (Play Arena (Test Keys).bat:
GE_STAGE=colosseum GE_SKIPINTRO=1 GE_MODE=<mode> GE_ARENA_TESTKEYS=1 GE_CHEATS=invuln,guns), plus GE_HIDDEN=1 and GE_KEYLOG=1.
PageDown, Home, End, Insert (and F5 with arena-whacky) are pressed once wave 1 is up. Exit 0 = every key worked."""
import os, subprocess, sys, time

build = sys.argv[1]
mode = sys.argv[2] if len(sys.argv) > 2 else "arena"
exe = os.path.join(build, "ProjectMidas.exe")
out = os.path.join(build, "keyinject")
os.makedirs(out, exist_ok=True)
log = os.path.join(out, mode + ".log")
KEYS = [("PageDown", 78, "TEST next"), ("Home", 74, "TEST callout preview"), ("End", 77, "TEST fog on"), ("Insert", 73, "TEST rain on")]
if mode == "arena-whacky":
    KEYS.append(("F5", 62, "MUTATOR wave"))
START, GAP = 40.0, 4.0   # seconds after the first input poll (the hidden boot reaches wave 1 well before this)
inject = ",".join("%.1f:%d" % (START + i * GAP, k[1]) for i, k in enumerate(KEYS))
if len(sys.argv) > 3:   # diagnosis: a GE_KEYINJECT string of one's own (the checks below then only report)
    inject = sys.argv[3]
    log = os.path.join(out, mode + "-custom.log")
env = dict(os.environ)
# The game must end by itself (GE_FRAMES): its sim thread logs through buffered stdout, and a killed process loses the last buffer
# (the later presses and their TEST lines), which once looked like the sim stopping after the first key.
env.update({"GE_HIDDEN": "1", "GE_STAGE": "colosseum", "GE_SKIPINTRO": "1", "GE_MODE": mode, "GE_ARENA_TESTKEYS": "1",
            "GE_CHEATS": "invuln,guns", "GE_NODIRECTLOG": "1", "GE_KEYLOG": "1", "GE_KEYINJECT": inject, "GE_FRAMES": "9000"})
with open(log, "wb") as fh:
    p = subprocess.Popen([exe], cwd=out, env=env, stdout=fh, stderr=subprocess.STDOUT)
try:
    p.wait(timeout=240)
except subprocess.TimeoutExpired:
    p.kill()
    p.wait()
text = open(log, "rb").read().decode("latin-1")
wave = text.find("ge: arena: wave 1 ")
fails = 0
for name, sc, want in KEYS:
    i = text.find("pressing scancode %d " % sc)
    ok = i >= 0 and text.find(want, i) >= 0
    fails += 0 if ok else 1
    print(("PASS" if ok else "FAIL"), name, "->", want, "" if i >= 0 else "(never injected)")
print("wave 1 seen before the keys:", 0 <= wave < text.find("pressing scancode"))
print("keyinject win (%s): %d failed" % (mode, fails))
sys.exit(1 if fails else 0)
