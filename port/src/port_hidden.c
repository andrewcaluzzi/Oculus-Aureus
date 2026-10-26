/**
 * Hidden mode: GE_HIDDEN=1 or the --hidden argument (port/README-headless.md).
 *
 * The game runs exactly as usual (GL context, rendering, GE_SHOT screenshots,
 * GE_FRAMES / GE_FIXEDTICK / GE_NOSLEEP), but nothing reaches the desktop or the
 * player sitting at it:
 *  - the window is created hidden and is never shown (gfx_sdl2.cpp skips
 *    SDL_ShowWindow, fullscreen, maximize and the fit-to-display move), so there
 *    is no taskbar entry and no focus steal;
 *  - audio goes to SDL's dummy driver;
 *  - every SDL controller backend is off (HIDAPI, RawInput, WGI, XInput,
 *    DirectInput) and the raphnet raw adapter is never opened, so the game
 *    neither reads nor pokes (rumble, LEDs, suspend) the player's controllers;
 *    keyboard and mouse need the window's focus, which a hidden window never
 *    gets, and the mouse is never captured or warped;
 *  - the window is 1280x720 (GE_WINDOW) and midas.ini is never written;
 *  - error message boxes are suppressed (GE_NOMSGBOX);
 *  - on Windows the process runs at below-normal priority.
 * Each default only applies when the variable is not already set, so a
 * harness can still pick its own window size or audio driver.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif
#ifdef GE_VIDEO
#include <SDL.h>
#endif

#include "port_hidden.h"
#include "port_console.h"

static int s_Hidden = -1;

static int hiddenEnvOn(void)
{
    const char *e = getenv("GE_HIDDEN");

    return e != NULL && e[0] != '\0' && e[0] != '0';
}

int portHidden(void)
{
    if (s_Hidden < 0)
    {
        s_Hidden = hiddenEnvOn();
    }
    return s_Hidden;
}

/* set NAME=VALUE unless NAME is already set (both the CRT and, on Windows,
 * the process environment SDL_getenv reads) */
static void hiddenDefaultEnv(const char *name, const char *value)
{
    const char *cur = getenv(name);

    if (cur != NULL && cur[0] != '\0')
    {
        return;
    }
#ifdef _WIN32
    {
        char kv[256];

        snprintf(kv, sizeof(kv), "%s=%s", name, value);
        _putenv(kv);   /* copies; also updates the Win32 environment block */
        SetEnvironmentVariableA(name, value);
    }
#else
    setenv(name, value, 1);
#endif
}

#ifdef GE_VIDEO
/* OVERRIDE priority: the input code's own SDL_SetHint calls (ini options such
 * as hidapi=1 or background input) cannot turn these back on */
static void hiddenHint(const char *name, const char *value)
{
    SDL_SetHintWithPriority(name, value, SDL_HINT_OVERRIDE);
}
#endif

void portHiddenInit(int argc, char **argv)
{
    int i;

    for (i = 1; i < argc; i++)
    {
        if (argv[i] != NULL && strcmp(argv[i], "--hidden") == 0)
        {
#ifdef _WIN32
            _putenv("GE_HIDDEN=1");
            SetEnvironmentVariableA("GE_HIDDEN", "1");
#else
            setenv("GE_HIDDEN", "1", 1);
#endif
        }
    }
    s_Hidden = hiddenEnvOn();
    if (!s_Hidden)
    {
        return;
    }

    hiddenDefaultEnv("SDL_AUDIODRIVER", "dummy");
    hiddenDefaultEnv("GE_WINDOW", "1280x720");   /* also keeps midas.ini unwritten (video.c) */
    hiddenDefaultEnv("GE_NOINISAVE", "1");
    hiddenDefaultEnv("GE_NOMSGBOX", "1");

#ifdef GE_VIDEO
#ifdef SDL_HINT_AUDIODRIVER
    hiddenHint(SDL_HINT_AUDIODRIVER, getenv("SDL_AUDIODRIVER"));
#endif
    hiddenHint(SDL_HINT_JOYSTICK_HIDAPI, "0");
#ifdef SDL_HINT_JOYSTICK_RAWINPUT
    hiddenHint(SDL_HINT_JOYSTICK_RAWINPUT, "0");
#endif
#ifdef SDL_HINT_JOYSTICK_WGI
    hiddenHint(SDL_HINT_JOYSTICK_WGI, "0");
#endif
    hiddenHint(SDL_HINT_XINPUT_ENABLED, "0");
#ifdef SDL_HINT_DIRECTINPUT_ENABLED
    hiddenHint(SDL_HINT_DIRECTINPUT_ENABLED, "0");
#endif
    hiddenHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "0");
#ifdef SDL_HINT_WINDOW_NO_ACTIVATION_WHEN_SHOWN
    hiddenHint(SDL_HINT_WINDOW_NO_ACTIVATION_WHEN_SHOWN, "1");
#endif
    hiddenHint(SDL_HINT_ALLOW_TOPMOST, "0");
#endif

#ifdef _WIN32
    SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
#endif

    portConLogf("ge: hidden: window never shown, audio %s, no controllers, window %s, ini not written\n",
                getenv("SDL_AUDIODRIVER"), getenv("GE_WINDOW"));
}
