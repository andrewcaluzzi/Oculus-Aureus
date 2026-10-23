/* port_netstats.h - live network and frame-time statistics (netplay job 6): `net info`, and the data behind the `net_graph` overlay
 * (drawn by port_console.c). The ring and the one-second window are pure (tests/netstats_test.c); the feeders are in port_net.c
 * and boss.c. Render only: nothing here is read by the sim. */
#ifndef PORT_NETSTATS_H
#define PORT_NETSTATS_H

#define NETSTAT_HIST 300          /* samples kept (one per rendered frame): 5 s at 60 Hz */

#define NSF_WAITED   1u           /* the tick was held waiting for the partner's input */
#define NSF_HASH_OK  2u           /* a state hash was compared at this tick and matched */
#define NSF_HASH_BAD 4u           /* ... and differed (DESYNC) */
#define NSF_RESEND   8u           /* the unacked records were sent again by the stall timer */

typedef struct PortNetStatSample
{
    float simMs, renderMs, frameMs;   /* the tick's logic, the frame build (lvlRender), the time since the previous frame */
    float waitMs;                     /* how long the tick was held for the partner (0 = not at all) */
    int pingMs, jitterMs;             /* ENet round trip (smoothed) and its variance; 0 without a link */
    unsigned int inBytes, outBytes;   /* wire bytes since the previous sample (UDP datagrams + 28 B header each) */
    unsigned int inPkts, outPkts;     /* datagrams since the previous sample */
    unsigned int flags;               /* NSF_* */
    unsigned int tick;                /* the sim tick (0 outside a stage) */
} PortNetStatSample;

typedef struct PortNetStatSecond      /* the last completed one-second window */
{
    float inKBs, outKBs, inPps, outPps;
    float simAvg, simMax, renderAvg, renderMax, fps;
    float waitAvg, waitMax;
    unsigned int frames, waited, resends;
    int valid;
} PortNetStatSecond;

void portNetStatsReset(void);
/* adds one frame; nowUs = a monotonic microsecond clock (the window rotates every 1,000,000) */
void portNetStatsPush(const PortNetStatSample *s, unsigned long long nowUs);
int portNetStatsCount(void);                                   /* samples held, 0..NETSTAT_HIST */
const PortNetStatSample *portNetStatsBack(int back);           /* 0 = newest; NULL out of range */
const PortNetStatSecond *portNetStatsSecond(void);
/* marks called by boss.c around the frame: 0 = sim begin, 1 = sim end, 2 = frame built (pushes the sample) */
void portNetStatsMark(int what);
/* port_net.c: fills the net fields of the sample being pushed (zeros when netplay is off) and clears what it consumed */
void portNetStatsGather(PortNetStatSample *s);

/* net_graph level 0..3: the console command, [display] net_graph, or GE_NET_GRAPH (in that priority: env wins at start-up) */
int portNetGraphLevel(void);
void portNetGraphSet(int level);

/* what port_net.c knows for the overlay's number block */
#define NETGRAPH_FIELDS 12
typedef struct PortNetGraphInfo
{
    int netplay;                  /* 1 = a netplay session (or a replay) is the source of the numbers */
    int linked;                   /* the lockstep gate is on */
    int role;                     /* 0 off, 1 host, 2 guest */
    unsigned int tick;
    int delay;
    int pingMs, jitterMs, enetLossPct;
    unsigned int resends, oow, dups, lateTicks;   /* session totals: timer resends, records ignored (gap / ring), duplicate records, ticks that waited */
    unsigned int lastHashTick;
    int lastHashVerdict;          /* -1 none yet, 0 ok, 1 DESYNC */
    unsigned int hashCompared, desyncs;
    unsigned int diffMask;        /* bit i = sub-hash field i differed in the last failed check */
    unsigned int diffTick;        /* the tick of that failed check */
    int nfields;
    const char *fieldName[NETGRAPH_FIELDS];
} PortNetGraphInfo;
void portNetGraphInfo(PortNetGraphInfo *out);   /* port_net.c */

#endif /* PORT_NETSTATS_H */
