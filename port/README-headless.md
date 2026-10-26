# Hidden mode: running the game with no visible window

`GE_HIDDEN=1` (or the `--hidden` argument) runs Project Midas exactly as usual, but with a window that is never shown.
Agents and scripts can then smoke-test the **Windows exe on a desktop someone is using**: no window, no taskbar
button, no focus steal, no sound, and the player's controllers are left alone. It pairs with the usual harness
variables: `GE_FRAMES`, `GE_FIXEDTICK`, `GE_TICKHZ`, `GE_NOSLEEP`, `GE_SHOT`, `GE_STAGE`, `GE_MODE`, `GE_SEED`.

Code: `port/src/port_hidden.c` (the switch and its defaults), with small `portHidden()` checks in
`port/fast3d/gfx_sdl2.cpp` (window), `port/src/input_pc.c` (mouse) and `port/src/raphnet.c` (raw N64 adapter).

## What it does

| Area | Hidden mode |
|---|---|
| Window | Created with `SDL_WINDOW_HIDDEN | SDL_WINDOW_SKIP_TASKBAR`. `SDL_ShowWindow` is never called. Fullscreen, borderless and maximize requests (from `midas.ini` or Alt+Enter) only record the state: `SDL_MaximizeWindow`/`SDL_RestoreWindow` would show the window through `ShowWindow`. |
| Rendering | Unchanged. The GL context belongs to the hidden window and renders into its back buffer. `GE_SHOT` reads it back with `glReadPixels` before the swap. Vsync is off (swap interval 0), so a hidden run never waits for a display's refresh. |
| Size | `GE_WINDOW=1280x720` unless set. `GE_WINDOW` also stops `midas.ini` being written. |
| Audio | `SDL_AUDIODRIVER=dummy` unless set. The mixer runs normally, but nothing reaches the speakers. |
| Controllers | Every SDL joystick backend is off (HIDAPI, RawInput, WGI, XInput, DirectInput). The hints are set at override priority, so `midas.ini` input options can't turn them back on. The raphnet raw adapter is never opened, polled, suspended or rumbled. The game never reads the player's pads and never pokes them (LEDs, rumble, HIDAPI init packets). |
| Keyboard, mouse | SDL only delivers these to a focused window, and a hidden window never gets the focus. The mouse is never captured, warped or hidden (`portMouseApplyCapture`, the cursor re-assert). |
| Other | `GE_NOINISAVE=1` and `GE_NOMSGBOX=1` unless set. On Windows the process runs at below-normal priority. Each default applies only when the variable isn't set already. |

The log shows it three times:
- `ge: hidden: window never shown, audio dummy, no controllers, window 1280x720, ini not written`
- `SDL: hidden mode: the window is never shown (flags 0x...)`
- `ge: hidden: 0 joysticks visible (every SDL backend off)`

Hidden mode changes no sim state. The dam1 oracle is identical with `GE_HIDDEN=1`, and `port_hidden.c` is classed as
PRES in `scripts/port/snap_embed.py`.

First Windows run (2026-10-26, RTX 2080, driver 617.14): `SMOKE PASS` in 18 s. The pid owned up to 5 top-level
windows and none were visible. 0 joysticks, audio on `dummy`, and the heist rendered normally into the hidden
window's back buffer, so no FBO fallback was needed.

The flags must include `SDL_WINDOW_HIDDEN` (0x8) and must not include `SDL_WINDOW_SHOWN` (0x4).

Why not `SDL_VIDEODRIVER=offscreen`? The bundled SDL2.dll (2.30.11) does include the offscreen driver, but its GL
path needs EGL: a `libEGL.dll` such as ANGLE's, which Windows doesn't ship. It would also swap the WGL context the
real renderer uses for a different GL stack. A hidden WGL window keeps the exact renderer that players run.

## Windows smoke test

```
powershell -ExecutionPolicy Bypass -File tests\win\smoke_hidden.ps1 -BuildDir build_win -Frames 900
```

The script boots `ProjectMidas.exe` hidden into the v3 bank heist (`GE_STAGE=levelgen GE_MODE=heist`, seed 7, fixed
tick), runs `-Frames` frames and takes one screenshot. It fails (exit 1) if any of these happens:
- a visible window appears (it polls every 100 ms; if one appears, it kills the process at once);
- the exe doesn't exit 0 with `ge: frame limit reached`;
- the log has a Lua error, a fatal error, a crash or a GL error;
- the screenshot is missing or blank (one colour).

The logs and `shot_NNNNN.png` go to `<BuildDir>\smoke_hidden\`. Pass `-Stage`/`-Mode` to smoke-test another
stage or mode. The run keeps the real-time pacer, so 900 frames take about 15 s. `-NoSleep` (`GE_NOSLEEP=1`) is
faster, but it renders only about one frame in seven. `GE_SHOT` counts rendered frames, so pass a small `-ShotFrame`
with it.

The build dir needs `Data\baserom.u.z64`, which you copy in yourself. Never commit it.

## Linux (Xvfb)

The same switch works under Xvfb (`DISPLAY=:99 GE_HIDDEN=1 ...`). The window is created but never mapped, and Mesa
still renders into its back buffer. The Linux harness doesn't need it, because Xvfb is invisible anyway. It is
the cheap way to check a hidden-mode change before trying it on Windows.
