/**
 * n64_random.c — PC-side C bodies for the three hand-written N64 RNGs.
 *
 * Upstream decomp moved these to hand-written assembly files
 * (src/random.s, src/tlb_random.s, src/game/chrObjRandom.s) and deleted
 * the .c files, so the engine globs no longer provide them on PC. The
 * C bodies below are the port's instruction-traced equivalents that
 * previously lived in those .c files under NONMATCHING (verified
 * EQUIVALENT against the asm during the Phase-0b comparison; the
 * demo replay bit-sync exercises randomGetNext every sim tick).
 *
 * All three generators share one 64-bit xorshift-style mixer, each on
 * its own seed global. Mixer note: the final xor is t ^ ((t >> 20) &
 * 0xfff) — against the MIXED value t, not the old seed ($a0 is
 * clobbered by dsll32 before the last xor).
 */
#include <ultra64.h>
#include "random.h"
#include "tlb_random.h"
#include "game/chrObjRandom.h"

u64 g_randomSeed       = 0xAB8D9F7781280783;
u64 g_tlbRandomSeed    = 0xAB8D9F7781280783;
u64 g_chrObjRandomSeed = 0xAB8D9F7781280783;

static u64 n64RandMix(u64 seed)
{
    u64 t = ((seed << 63) >> 31) | ((seed << 31) >> 32);
    t ^= (seed << 44) >> 32;
    return t ^ ((t >> 20) & 0xfff);
}

/**
 * Iterates the current random seed and returns a 32 bit value.
 * (asm: 7000A450 in src/random.s)
 */
u32 g_portSimRandDraws;     /* PORT: netplay: sim RNG draws since the stage reseed (in the state hash: "rng") */
u32 g_portChrObjRandDraws;  /* PORT: netplay: chr/obj RNG draws since the stage reseed */
s32 g_portRngTrace;         /* GE_NET_RNGTRACE: port_net.c logs every draw (tick, caller, value) */
extern void portNetRngTraceDraw(int kind, void *ret, u32 value);

u32 randomGetNext(void)
{
    g_randomSeed = n64RandMix(g_randomSeed);
    g_portSimRandDraws++;
    if (g_portRngTrace)
    {
        portNetRngTraceDraw('S', __builtin_return_address(0), (u32) g_randomSeed);
    }
    /* Demo-desync hunt instrumentation (ramromreplay.c GE_DEMODIAG):
     * per-tick draw counts diffed against the counts reconstructed from a
     * recording's per-block seed bytes; g_portRandTraceArmed additionally
     * prints each caller for pinpointing a diverging tick. */
    {
        extern s32 g_portRandDrawCount;
        extern s32 g_portRandTraceArmed;
        g_portRandDrawCount++;
        if (g_portRandTraceArmed)
        {
            extern int printf(const char *, ...);
            printf("ge: rng draw %d from %p\n", g_portRandDrawCount, __builtin_return_address(0));
        }
    }
    return (u32) g_randomSeed;
}

/**
 * Sets the global random seed (boss mainloop: randomSetSeed(osGetCount())).
 * On hardware the u32 argument sits in the register sign-extended to 64
 * bits and daddiu operates on that, so mirror it exactly.
 * (asm: 7000A498 in src/random.s)
 */
void randomSetSeed(u32 param_1)
{
#ifndef TARGET_N64
    /* GE_SEED=n: pin the boot seed so two headless runs replay the same
     * random stream (pixel A/B of renderer toggles on e.g. one explosion,
     * whose part positions/rotations are all RNG draws). */
    {
        extern char *getenv(const char *);
        extern long atol(const char *);
        const char *e = getenv("GE_SEED");
        if (e != NULL)
        {
            param_1 = (u32) atol(e);
        }
    }
#endif
    g_randomSeed = (u64) ((s64) (s32) param_1 + 1);
}

/**
 * Iterates the caller's seed with the same mixer and returns the next
 * 32 bit random value. (asm: 7000A4AC in src/random.s)
 */
u32 randomGetNextFrom(u64 *param_1)
{
    *param_1 = n64RandMix(*param_1);
    return (u32) *param_1;
}

/**
 * Random value for TLB random entries. (asm: 70001B10 in src/tlb_random.s)
 */
u32 tlbRandomGetNext(void)
{
    g_tlbRandomSeed = n64RandMix(g_tlbRandomSeed);
    return (u32) g_tlbRandomSeed;
}

/**
 * Same mixer on the chr/obj seed. (asm: 7F09B740 in src/game/chrObjRandom.s)
 */
u32 chrObjRandomGetNext(void)
{
    g_chrObjRandomSeed = n64RandMix(g_chrObjRandomSeed);
    g_portChrObjRandDraws++;
    if (g_portRngTrace)
    {
        portNetRngTraceDraw('C', __builtin_return_address(0), (u32) g_chrObjRandomSeed);
    }
    return (u32) g_chrObjRandomSeed;
}

/**
 * Same sign-extend + 1 as randomSetSeed, on the chr/obj seed.
 * (asm: 7F09B788 in src/game/chrObjRandom.s)
 */
void chrObjRandomSetSeed(u32 param_1)
{
    g_chrObjRandomSeed = (u64) ((s64) (s32) param_1 + 1);
}
