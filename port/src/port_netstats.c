/* port_netstats.c - the frame/network sample ring and its one-second window (netplay job 6). Pure apart from the clock in
 * portNetStatsMark and the ini/env read in portNetGraphLevel. Render only: the sim never reads any of it. */
#include <stdlib.h>
#include <string.h>
#include "port_netstats.h"

extern unsigned long long portProfNow(void);                                   /* pcsched.c: monotonic microseconds */
__attribute__((weak)) int portVideoNetGraphCfg(void) { return 0; }             /* video.c: [display] net_graph */

static PortNetStatSample sRing[NETSTAT_HIST];
static unsigned int sHead;        /* total samples pushed; the newest is sRing[(sHead - 1) % NETSTAT_HIST] */
static PortNetStatSecond sSec;

/* the window being accumulated */
static unsigned long long sWinStart;
static double sInB, sOutB, sInP, sOutP, sSimSum, sRenSum, sWaitSum;
static float sSimMax, sRenMax, sWaitMax;
static unsigned int sWinFrames, sWinWaited, sWinResends;

void portNetStatsReset(void)
{
    memset(sRing, 0, sizeof(sRing));
    memset(&sSec, 0, sizeof(sSec));
    sHead = 0;
    sWinStart = 0;
    sInB = sOutB = sInP = sOutP = sSimSum = sRenSum = sWaitSum = 0.0;
    sSimMax = sRenMax = sWaitMax = 0.0f;
    sWinFrames = sWinWaited = sWinResends = 0;
}

void portNetStatsPush(const PortNetStatSample *s, unsigned long long nowUs)
{
    sRing[sHead % NETSTAT_HIST] = *s;
    sHead++;
    if (sWinStart == 0)
    {
        sWinStart = nowUs;
    }
    sInB += s->inBytes;
    sOutB += s->outBytes;
    sInP += s->inPkts;
    sOutP += s->outPkts;
    sSimSum += s->simMs;
    sRenSum += s->renderMs;
    sWaitSum += s->waitMs;
    if (s->simMs > sSimMax) sSimMax = s->simMs;
    if (s->renderMs > sRenMax) sRenMax = s->renderMs;
    if (s->waitMs > sWaitMax) sWaitMax = s->waitMs;
    sWinFrames++;
    if (s->flags & NSF_WAITED) sWinWaited++;
    if (s->flags & NSF_RESEND) sWinResends++;
    if (nowUs - sWinStart >= 1000000ull)
    {
        const double secs = (double) (nowUs - sWinStart) / 1000000.0;

        sSec.inKBs = (float) (sInB / 1024.0 / secs);
        sSec.outKBs = (float) (sOutB / 1024.0 / secs);
        sSec.inPps = (float) (sInP / secs);
        sSec.outPps = (float) (sOutP / secs);
        sSec.fps = (float) (sWinFrames / secs);
        sSec.simAvg = (float) (sSimSum / sWinFrames);
        sSec.renderAvg = (float) (sRenSum / sWinFrames);
        sSec.simMax = sSimMax;
        sSec.renderMax = sRenMax;
        sSec.waitAvg = (float) (sWaitSum / sWinFrames);
        sSec.waitMax = sWaitMax;
        sSec.frames = sWinFrames;
        sSec.waited = sWinWaited;
        sSec.resends = sWinResends;
        sSec.valid = 1;
        sWinStart = nowUs;
        sInB = sOutB = sInP = sOutP = sSimSum = sRenSum = sWaitSum = 0.0;
        sSimMax = sRenMax = sWaitMax = 0.0f;
        sWinFrames = sWinWaited = sWinResends = 0;
    }
}

int portNetStatsCount(void)
{
    return sHead < NETSTAT_HIST ? (int) sHead : NETSTAT_HIST;
}

const PortNetStatSample *portNetStatsBack(int back)
{
    if (back < 0 || back >= portNetStatsCount())
    {
        return NULL;
    }
    return &sRing[(sHead - 1u - (unsigned int) back) % NETSTAT_HIST];
}

const PortNetStatSecond *portNetStatsSecond(void)
{
    return &sSec;
}

/* ---- the frame marks (boss.c) ---- */
static unsigned long long sT0, sT1, sTPrev;

void portNetStatsMark(int what)
{
    const unsigned long long now = portProfNow();

    if (what == 0)
    {
        sT0 = now;
        sT1 = 0;
    }
    else if (what == 1)
    {
        sT1 = now;
    }
    else if (what == 2 && sT0 != 0)
    {
        PortNetStatSample s;

        memset(&s, 0, sizeof(s));
        if (sT1 == 0 || sT1 < sT0)
        {
            sT1 = sT0;
        }
        s.simMs = (float) (sT1 - sT0) / 1000.0f;
        s.renderMs = (float) (now - sT1) / 1000.0f;
        s.frameMs = sTPrev != 0 ? (float) (now - sTPrev) / 1000.0f : s.simMs + s.renderMs;
        sTPrev = now;
        portNetStatsGather(&s);
        portNetStatsPush(&s, now);
        sT0 = 0;
    }
}

/* ---- the level ---- */
static int sLevel = -1;

int portNetGraphLevel(void)
{
    if (sLevel < 0)
    {
        const char *e = getenv("GE_NET_GRAPH");

        sLevel = (e != NULL && e[0] != '\0') ? atoi(e) : portVideoNetGraphCfg();
        if (sLevel < 0 || sLevel > 3)
        {
            sLevel = 0;
        }
    }
    return sLevel;
}

void portNetGraphSet(int level)
{
    sLevel = level < 0 ? 0 : level > 3 ? 3 : level;
}
