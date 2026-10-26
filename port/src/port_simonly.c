/*
 * Rollback job R1: the sim-only tick.
 *
 * A tick is split in two (boss.c, lv.c lvlRender):
 *   - the SIM step: everything that changes game state (lvlTick, deaths, the view move ticks, and every player's
 *     per-player pass: room walk, propsTick, autoaim, use, pickups, the sim owner, the gun);
 *   - the RENDER step: drawing (display list, GL, the console, the gfx task).
 * The sim step can run alone: g_PortSimOnly = 1 makes boss.c skip the render step and lvlRender run each pass into a
 * scratch display list that is rewound, skipping the draw-only calls.
 *
 * GE_SIMONLY_TEST=<start>,<n>[,<every>] (test only, off by default): ticks [start, start+n) of the stage run
 * sim-only (no render step at all), then normal frames resume; with <every> the window repeats every <every> ticks
 * (start+k*every .. +n). Ticks are counted from 1 per stage, the same numbering as the co-op / netplay fingerprint
 * (sPupTick) and, in a demo, `ge: demo tick`. A sim-only stretch must give the fingerprints of a normal run.
 * GE_SIMONLY_STATS=1 logs the cost of the sim-only ticks (avg / max, whole tick without drawing).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern unsigned long long portProfNow(void);

int g_PortSimOnly;   /* 1 while the current tick is sim-only: lvlRender draws into a scratch list and skips draw-only calls */

static int sInit;
static int sOn;
static unsigned sStart, sLen, sEvery;
static unsigned sCount;     /* ticks begun in this stage */
static int sStats;
static unsigned long long sT0;
static unsigned long long sSum, sMax;
static unsigned sNum;
static unsigned sRunFirst, sRunLast;   /* the current stretch */

/* GE_SIMONLY_STATS=1: where the time of a sim-only tick goes (portSimOnlySect marks in boss.c and lv.c) */
#define NSECT 12
static const char *const sSectName[NSECT] = {
    "pass setup (view, z-clear, sky)", "room walk (sim)", "propsTick + onscreen + autoaim + use", "interact / reload / propsTickPlayer",
    "bgLevelRender (world build)", "tracers / sparks / shards / particles", "cheats + ammo", "gun view + watch menu",
    "lvlTick + death tick", "view move ticks", "defrag + swap", "-"
};
static unsigned long long sSect[NSECT];
static int sLastSect = -1;
static unsigned long long sLastT;

void portSimOnlySect(int id)
{
    unsigned long long now;

    if (!sStats || !g_PortSimOnly || id < 0 || id >= NSECT) {
        return;
    }
    now = portProfNow();
    if (sLastSect >= 0) {
        sSect[sLastSect] += now - sLastT;
    }
    sLastSect = id;
    sLastT = now;
}

static void sectEnd(void)   /* closes the open section at the end of a sim-only tick */
{
    if (sLastSect >= 0) {
        sSect[sLastSect] += portProfNow() - sLastT;
        sLastSect = -1;
    }
}

static void sectReport(unsigned ticks)
{
    int i;

    if (!sStats || ticks == 0) {
        return;
    }
    for (i = 0; i < NSECT - 1; i++) {
        if (sSect[i] != 0) {
            printf("ge: simonly:   %-42s avg %.3f ms\n", sSectName[i], (double) sSect[i] / (double) ticks / 1000.0);
        }
    }
    memset(sSect, 0, sizeof(sSect));
}

static void init(void)
{
    const char *e;
    unsigned a = 0, b = 0, c = 0;
    int n;

    sInit = 1;
    sStats = getenv("GE_SIMONLY_STATS") != NULL;
    e = getenv("GE_SIMONLY_TEST");
    if (e == NULL || e[0] == '\0') {
        return;
    }
    n = sscanf(e, "%u,%u,%u", &a, &b, &c);
    if (n < 2 || a < 1 || b < 1) {
        printf("ge: simonly: bad GE_SIMONLY_TEST '%s' (want <start>,<n>[,<every>], start >= 1, n >= 1): off\n", e);
        return;
    }
    sStart = a;
    sLen = b;
    sEvery = (n >= 3) ? c : 0;
    sOn = 1;
    printf("ge: simonly: ticks %u..%u run sim-only%s\n", sStart, sStart + sLen - 1u, sEvery ? " (repeating)" : "");
}

static int inWindow(unsigned tick)
{
    unsigned d;

    if (!sOn || tick < sStart) {
        return 0;
    }
    d = tick - sStart;
    if (sEvery > 0) {
        d %= sEvery;
    }
    return d < sLen;
}

static void flushRun(void)
{
    if (sRunFirst != 0) {
        if (sStats && sNum > 0) {
            printf("ge: simonly: ticks %u..%u ran sim-only: avg %.3f ms, max %.3f ms over %u ticks\n", sRunFirst, sRunLast,
                   (double) sSum / (double) sNum / 1000.0, (double) sMax / 1000.0, sNum);
        } else {
            printf("ge: simonly: ticks %u..%u ran sim-only\n", sRunFirst, sRunLast);
        }
        sectReport(sNum % 300u);
        sRunFirst = sRunLast = 0;
        fflush(stdout);
    }
}

static unsigned sSfxTotal;   /* sound starts the sim asked for during sim-only ticks */

/* snd.c sndPlaySfx: every one-shot sound the sim triggers passes here (rollback R3 will key them by tick + source + sound) */
void portSimOnlySfx(int soundIndex)
{
    (void) soundIndex;
    if (g_PortSimOnly) {
        sSfxTotal++;
    }
}

/* boss.c, after a stage is loaded: ticks are counted from 1 again */
void portSimOnlyStageBegin(void)
{
    extern void portSnapStageBegin(void);   /* port_snap.c: the snapshot ring is per stage */

    if (!sInit) {
        init();
    }
    flushRun();
    sCount = 0;
    portSnapStageBegin();
}

/* ticks begun in this stage (the tick the next portSimOnlyBegin will count is this + 1): port_snap.c's tick numbering */
unsigned int portSimOnlyTickCount(void)
{
    return sCount;
}

/* port_snap.c, a solo savestate put back: the tick count goes back with it (which ticks draw follows the count) */
void portSimOnlySetTickCount(unsigned int n)
{
    sCount = n;
}

/* boss.c retrace gate: will the NEXT tick be sim-only? (a sim-only tick swaps the display buffers without a gfx task, so
 * it must not start while a task of the previous frame is still in flight) */
int portSimOnlyNext(int inStage)
{
    if (!sInit) {
        init();
    }
    return sOn && inStage && inWindow(sCount + 1u);
}

/* boss.c, at the start of an in-stage tick, before lvlTick: counts the tick and returns 1 if it is sim-only */
int portSimOnlyBegin(void)
{
    if (!sInit) {
        init();
    }
    sCount++;
    g_PortSimOnly = 0;
    if (sOn && inWindow(sCount)) {
        g_PortSimOnly = 1;
        sT0 = portProfNow();
        { extern void portSimProfBegin(unsigned tick); portSimProfBegin(sCount); }   /* GE_PROFILE_SIM (port_luavm_simprof.c) */
        if (sRunFirst == 0) {
            printf("ge: simonly: stretch starts at tick %u\n", sCount);
            fflush(stdout);
            sRunFirst = sCount;
            sSum = sMax = 0;
            sNum = 0;
        }
        sRunLast = sCount;
    } else {
        flushRun();
    }
    return g_PortSimOnly;
}

/* boss.c, at the end of the sim step of a sim-only tick */
void portSimOnlyEnd(void)
{
    extern unsigned int g_PortNetHoldBeat;   /* the Windows stall watchdog (main.c) counts it as progress: a sim-only tick submits no gfx task */

    if (g_PortSimOnly) {
        extern void portSimProfEnd(void);   /* GE_PROFILE_SIM (port_luavm_simprof.c) */

        portSimProfEnd();
        g_PortNetHoldBeat++;
    }
    if (g_PortSimOnly && sT0 != 0) {
        unsigned long long d = portProfNow() - sT0;

        sSum += d;
        sNum++;
        if (d > sMax) {
            sMax = d;
        }
        sT0 = 0;
        sectEnd();
        if (sNum % 300u == 0) {   /* proof for the tests that the stretch really ran (a stretch that never ends logs nothing else) */
            printf("ge: simonly: %u sim-only ticks so far in this stretch (now tick %u)", sNum, sCount);
            if (sStats) {
                printf(": avg %.3f ms, max %.3f ms", (double) sSum / (double) sNum / 1000.0, (double) sMax / 1000.0);
            }
            printf("\n");
            sectReport(300u);
            if (sStats) {
                printf("ge: simonly:   sound starts requested by the sim in sim-only ticks so far: %u\n", sSfxTotal);
            }
            fflush(stdout);
        }
    }
    g_PortSimOnly = 0;
}
