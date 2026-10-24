/* port_bench.c - the Colosseum arena benchmark (tools/mode_arena_bench.lua, modding/arena.md).
 * hitchlog.c hands every presented frame to portBenchFrame; the Lua `bench` table starts and stops a phase and
 * the report is a plain text file, Data/Logs/arena_bench.txt, to paste back. PC only, no effect on the game. */
#ifndef TARGET_N64

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

extern const char *portLogPath(const char *name);   /* main.c */
char g_PortGlRenderer[160];                          /* set by gfx_opengl.cpp (empty headless) */
char g_PortSysSummary[2048];                         /* the `ge: sys:` lines, appended by sysstubs.c */

#define BENCH_MAXF 60000

static float sFrameMs[BENCH_MAXF], sGlMs[BENCH_MAXF], sSwapMs[BENCH_MAXF], sSimMs[BENCH_MAXF];
static unsigned short sDraws[BENCH_MAXF];
static unsigned char sTag[BENCH_MAXF];
static volatile int sN, sActive, sTagNow;
static float sLastSim, sLastGl;   /* avg sim / gl ms of the phase portBenchEnd wrote last */

void portBenchLast(float *simAvg, float *glAvg)
{
    *simAvg = sLastSim;
    *glAvg = sLastGl;
}

/* hitchlog.c, render thread, once per presented frame */
void portBenchFrame(unsigned long long interval_us, unsigned long long run_us, unsigned long long swap_us,
                    unsigned long long lim_us, unsigned int draws, unsigned long long sim_us)
{
    int n = sN;

    if (!sActive || n >= BENCH_MAXF)
    {
        return;
    }
    sFrameMs[n] = (float)interval_us / 1000.0f;
    sGlMs[n] = (float)(run_us > swap_us + lim_us ? run_us - swap_us - lim_us : 0) / 1000.0f;
    sSwapMs[n] = (float)swap_us / 1000.0f;
    sSimMs[n] = (float)sim_us / 1000.0f;
    sDraws[n] = (unsigned short)(draws > 65535u ? 65535u : draws);
    sTag[n] = (unsigned char)sTagNow;
    sN = n + 1;
}

void portBenchLine(const char *fmt, ...)
{
    FILE *f = fopen(portLogPath("arena_bench.txt"), "a");
    va_list ap;

    if (f == NULL)
    {
        return;
    }
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

void portBenchRunStart(void)
{
    FILE *f = fopen(portLogPath("arena_bench.txt"), "w");

    if (f != NULL)
    {
        fclose(f);
    }
    portBenchLine("Project Midas - Colosseum arena benchmark");
    portBenchLine("GL renderer: %s", g_PortGlRenderer[0] ? g_PortGlRenderer : "(none: headless)");
    portBenchLine("%s", g_PortSysSummary[0] ? g_PortSysSummary : "(no ge: sys: lines)");
}

void portBenchBegin(void)
{
    extern void portDynPeaks(int *gfx, int *gfxCap, int *vtx, int *vtxCap, int reset);
    extern void portModelHitStats(int *used, int *peak, int reset);
    int a, b, c, d;

    portDynPeaks(&a, &b, &c, &d, 1);   /* the display list / vertex / hit entry peaks count from here */
    portModelHitStats(&a, &b, 1);
    sActive = 0;
    sN = 0;
    sTagNow = 0;
    sActive = 1;
}

void portBenchTag(int tag)
{
    sTagNow = tag;
}

static int cmpf(const void *a, const void *b)
{
    float x = *(const float *)a, y = *(const float *)b;

    return (x > y) - (x < y);
}

/* avg p50 p95 p99 max of the frames with tag (-1 = all); returns the count */
static int stats(int tag, float *avg, float *p50, float *p95, float *p99, float *mx)
{
    static float tmp[BENCH_MAXF];
    int i, m = 0, n = sN;
    double sum = 0.0;

    for (i = 0; i < n; i++)
    {
        if (tag < 0 || sTag[i] == tag)
        {
            tmp[m++] = sFrameMs[i];
            sum += sFrameMs[i];
        }
    }
    *avg = *p50 = *p95 = *p99 = *mx = 0.0f;
    if (m == 0)
    {
        return 0;
    }
    qsort(tmp, (size_t)m, sizeof(float), cmpf);
    *avg = (float)(sum / m);
    *p50 = tmp[(m - 1) / 2];
    *p95 = tmp[(int)((m - 1) * 0.95)];
    *p99 = tmp[(int)((m - 1) * 0.99)];
    *mx = tmp[m - 1];
    return m;
}

static void statLine(const char *name, int tag)
{
    float a, p50, p95, p99, mx;
    int m = stats(tag, &a, &p50, &p95, &p99, &mx);

    if (m == 0)
    {
        portBenchLine("  %-7s no frames", name);
        return;
    }
    portBenchLine("  %-7s %5d frames: frame ms avg %.2f  p50 %.2f  p95 %.2f  p99 %.2f  max %.2f  =  %.1f fps (avg), %.1f fps (p95 frame)",
                  name, m, a, p50, p95, p99, mx, 1000.0f / a, p95 > 0.0f ? 1000.0f / p95 : 0.0f);
}

/* pools: chrTotal, chrFree, propTotal, propFree, hitTotal, hitFree, hitPeakUsed, stageFreeBytes, gfxPeak, gfxCap, vtxPeak, vtxCap, simheapUsed */
void portBenchEnd(const char *label, const char *extra, const int *pools)
{
    double gl = 0, sw = 0, sim = 0, dr = 0;
    float glmax = 0, swmax = 0, simmax = 0;
    unsigned drmax = 0;
    int i, n;

    sActive = 0;
    n = sN;
    for (i = 0; i < n; i++)
    {
        gl += sGlMs[i];
        sw += sSwapMs[i];
        sim += sSimMs[i];
        dr += sDraws[i];
        if (sGlMs[i] > glmax) glmax = sGlMs[i];
        if (sSwapMs[i] > swmax) swmax = sSwapMs[i];
        if (sSimMs[i] > simmax) simmax = sSimMs[i];
        if (sDraws[i] > drmax) drmax = sDraws[i];
    }
    sLastSim = n > 0 ? (float)(sim / n) : 0.0f;
    sLastGl = n > 0 ? (float)(gl / n) : 0.0f;
    portBenchLine("");
    portBenchLine("== %s ==", label);
    statLine("ALL", -1);
    statLine("stands", 1);
    statLine("floor", 0);
    if (n > 0)
    {
        portBenchLine("  render: gl avg %.2f max %.2f ms | swap avg %.2f max %.2f ms | sim (longest game-code stretch per frame) avg %.2f max %.2f ms | draws avg %.0f max %u",
                      gl / n, glmax, sw / n, swmax, sim / n, simmax, dr / n, drmax);
    }
    if (extra != NULL && extra[0] != '\0')
    {
        portBenchLine("  %s", extra);
    }
    if (pools != NULL)
    {
        portBenchLine("  pools: chr slots used %d / %d (free %d) | props used %d / %d (free %d) | hit entries in use now %d / %d (peak %d) | stage mempool free %.1f MB",
                      pools[0] - pools[1], pools[0], pools[1], pools[2] - pools[3], pools[2], pools[3],
                      pools[4] - pools[5], pools[4], pools[6], (double)pools[7] / 1048576.0);
        portBenchLine("  frame memory: master display list peak %.0f / %.0f KB | vertex+matrix pool peak %.0f / %.0f KB | converted-model heap %.1f / 24 MB",
                      pools[8] / 1024.0, pools[9] / 1024.0, pools[10] / 1024.0, pools[11] / 1024.0, pools[12] / 1048576.0);
    }
}

#endif
