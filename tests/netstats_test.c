/* Unit test for port_netstats.c: the sample ring, the one-second window, the frame marks and the net_graph level (netplay job 6). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "port_netstats.h"

static int g_pass, g_fail;
#define CHECK(c) do { if (c) g_pass++; else { g_fail++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static unsigned long long gNow;
unsigned long long portProfNow(void) { return gNow; }
static int gGathers;
void portNetStatsGather(PortNetStatSample *s) { gGathers++; s->pingMs = 42; s->flags |= NSF_WAITED; }

static PortNetStatSample mk(float sim, float ren, unsigned inB, unsigned outB, unsigned flags)
{
    PortNetStatSample s;

    memset(&s, 0, sizeof(s));
    s.simMs = sim;
    s.renderMs = ren;
    s.frameMs = sim + ren;
    s.inBytes = inB;
    s.outBytes = outB;
    s.flags = flags;
    return s;
}

static void test_ring_and_window(void)
{
    PortNetStatSample s;
    const PortNetStatSecond *sec;
    int i;

    portNetStatsReset();
    CHECK(portNetStatsCount() == 0);
    CHECK(portNetStatsBack(0) == NULL);
    CHECK(!portNetStatsSecond()->valid);
    /* 60 frames of 1 KB in / 0.5 KB out, 16.6 ms apart: the window closes at the first sample past one second */
    for (i = 0; i < 61; i++) {
        s = mk(2.0f, 4.0f, 1024, 512, i == 10 ? NSF_WAITED : 0);
        s.tick = (unsigned) i + 1;
        s.waitMs = i == 10 ? 8.0f : 0.0f;
        portNetStatsPush(&s, 1000000ull + (unsigned long long) i * 16667ull);
    }
    sec = portNetStatsSecond();
    CHECK(sec->valid);
    CHECK(portNetStatsCount() == 61);
    CHECK(portNetStatsBack(0)->tick == 61);
    CHECK(portNetStatsBack(60)->tick == 1);
    CHECK(portNetStatsBack(61) == NULL);
    CHECK(sec->frames == 61);
    CHECK(sec->inKBs > 60.5f && sec->inKBs < 61.5f);        /* 61 frames * 1 KB in ~1.0 s */
    CHECK(sec->outKBs > 30.2f && sec->outKBs < 30.8f);
    CHECK(sec->simAvg > 1.99f && sec->simAvg < 2.01f);
    CHECK(sec->renderMax > 3.99f && sec->renderMax < 4.01f);
    CHECK(sec->waited == 1);
    CHECK(sec->waitMax > 7.99f && sec->waitMax < 8.01f);
    CHECK(sec->fps > 60.5f && sec->fps < 61.5f);
    /* the ring keeps NETSTAT_HIST samples, newest first */
    for (i = 0; i < NETSTAT_HIST + 50; i++) {
        s = mk(1.0f, 1.0f, 1, 1, 0);
        s.tick = 1000u + (unsigned) i;
        portNetStatsPush(&s, 3000000ull + (unsigned long long) i * 16667ull);
    }
    CHECK(portNetStatsCount() == NETSTAT_HIST);
    CHECK(portNetStatsBack(0)->tick == 1000u + NETSTAT_HIST + 49);
    CHECK(portNetStatsBack(NETSTAT_HIST - 1)->tick == 1000u + 50);
    CHECK(portNetStatsBack(NETSTAT_HIST) == NULL);
}

static void test_marks(void)
{
    portNetStatsReset();
    gGathers = 0;
    gNow = 5000000ull;
    portNetStatsMark(0);                /* sim begin */
    gNow += 3000ull;
    portNetStatsMark(1);                /* sim end: 3 ms of logic */
    gNow += 5000ull;
    portNetStatsMark(2);                /* frame built: 5 ms of render */
    CHECK(gGathers == 1);
    CHECK(portNetStatsCount() == 1);
    CHECK(portNetStatsBack(0)->simMs > 2.99f && portNetStatsBack(0)->simMs < 3.01f);
    CHECK(portNetStatsBack(0)->renderMs > 4.99f && portNetStatsBack(0)->renderMs < 5.01f);
    CHECK(portNetStatsBack(0)->pingMs == 42);                       /* the net fields come from the gatherer */
    CHECK((portNetStatsBack(0)->flags & NSF_WAITED) != 0);
    portNetStatsMark(2);                /* a stray end mark without a begin pushes nothing */
    CHECK(portNetStatsCount() == 1);
    gNow += 12000ull;
    portNetStatsMark(0);
    gNow += 1000ull;
    portNetStatsMark(1);
    gNow += 2000ull;
    portNetStatsMark(2);
    CHECK(portNetStatsCount() == 2);
    CHECK(portNetStatsBack(0)->frameMs > 14.9f && portNetStatsBack(0)->frameMs < 15.1f);   /* 12 + 1 + 2 ms since the previous frame end */
}

static void test_level(void)
{
    portNetGraphSet(2);
    CHECK(portNetGraphLevel() == 2);
    portNetGraphSet(9);
    CHECK(portNetGraphLevel() == 3);
    portNetGraphSet(-4);
    CHECK(portNetGraphLevel() == 0);
}

int main(void)
{
    test_ring_and_window();
    test_marks();
    test_level();
    printf("netstats: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail != 0;
}
