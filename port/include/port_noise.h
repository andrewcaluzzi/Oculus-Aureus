/* port_noise.h - the heist's hearing layer and guard awareness (designs/bank-heist.md section 18; port/src/port_noise.c, port_ai.c).
 * Owned by the ai stream. Other code may call portNoiseEmit only if present: in C declare it __attribute__((weak)) and test the
 * address; in Lua guard with `if noise and noise.emit then`. Mode only: everything returns at once outside a mode. */
#ifndef PORT_NOISE_H
#define PORT_NOISE_H

/* noise kinds (Lua names in the same order: "slap" "step" "run" "door" "shot" "thud" "glass" "coin" "alarm") */
enum
{
    NOISE_SLAP, NOISE_STEP, NOISE_RUN, NOISE_DOOR, NOISE_SHOT, NOISE_THUD, NOISE_GLASS, NOISE_COIN, NOISE_ALARM, NOISE_KIND_N
};

/* a noise at (x, y, z) heard out to `radius` units at hearing scale 1 in the open (same room). Walls, open portals and closed doors
 * between it and a guard cut that radius (port_noise.c). srcChr: the chr that made it (-2 - p = Bond p, -1 unknown / the world). */
void portNoiseEmit(float x, float y, float z, float radius, int kind, int srcChr);
const char *portNoiseKindName(int kind);
int portNoiseKindFind(const char *name);   /* -1 unknown */

/* guard awareness: 0 unaware, 1 suspicious, 2 searching, 3 alert, 4 combat; level 0..1; the last noise point. 0 = not a tracked guard */
int portAiAwareness(int chrnum, int *state, float *level, float *lastx, float *lastz);

/* port_ai.c, for port_noise.c: guard `i` of the tracked table heard `kind` at loudness `loud` (0..1), from the noise point */
int portAiGuardCount(void);
int portAiGuardChr(int i);
void portAiHear(int i, int kind, float loud, float x, float y, float z, int room, int srcChr);

#endif /* PORT_NOISE_H */
