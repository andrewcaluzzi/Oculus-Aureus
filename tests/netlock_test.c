/* netlock_test.c - unit tests for the pure lockstep schedule and ack logic in port_netlock.c. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "port_netlock.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond)                                              \
    do                                                           \
    {                                                            \
        if (cond)                                                \
        {                                                        \
            g_pass++;                                            \
        }                                                        \
        else                                                     \
        {                                                        \
            g_fail++;                                            \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                        \
    } while (0)

/* the input a player "samples" at a tick: unique per player and tick */
static PortNetInput mk(int player, unsigned int t)
{
    PortNetInput in;
    memset(&in, 0, sizeof(in));
    in.buttons = (unsigned short) (t * 7u + (unsigned) player);
    in.stickX = (signed char) ((int) (t % 100u) - 50);
    in.stickY = (signed char) player;
    in.actions = t * 2654435761u + (unsigned) player;
    in.flags = (unsigned short) ((t + (unsigned) player) & PORT_NETF_KNOWN);
    in.yawMilli = (int) t * 13 - player;
    in.pitchMilli = player ? -(int) t : (int) t;
    return in;
}

static int same(const PortNetInput *a, const PortNetInput *b)
{
    return a != NULL && b != NULL && a->buttons == b->buttons && a->stickX == b->stickX && a->stickY == b->stickY
        && a->actions == b->actions && a->digX == b->digX && a->digY == b->digY && a->flags == b->flags && a->yawMilli == b->yawMilli
        && a->pitchMilli == b->pitchMilli;
}

static void test_clamp(void)
{
    CHECK(portNetLockClampDelay(0) == PORT_NETLOCK_MIN_DELAY);
    CHECK(portNetLockClampDelay(1) == PORT_NETLOCK_MIN_DELAY);
    CHECK(portNetLockClampDelay(3) == 3);
    CHECK(portNetLockClampDelay(99) == PORT_NETLOCK_MAX_DELAY);
}

static void test_schedule(void)
{
    PortNetLock l;
    PortNetInput z, got;
    unsigned int t;
    memset(&z, 0, sizeof(z));
    portNetLockInit(&l, 3);
    CHECK(l.delay == 3 && l.theirAck == 1 && l.localCount == 0);
    CHECK(!portNetLockCanRun(&l, 1));                     /* nothing held yet */
    for (t = 1; t <= 3; t++) CHECK(portNetLockPushLocal(&l, &z) == t);   /* prefill ticks 1..delay */
    CHECK(l.localCount == 3);
    CHECK(!portNetLockCanRun(&l, 1));                     /* still no remote record */
    got = mk(1, 1);
    {
        PortNetInputPkt p;
        memset(&p, 0, sizeof(p));
        p.firstTick = 1;
        p.ack = 1;
        p.count = 1;
        p.rec[0] = got;
        CHECK(portNetLockRecv(&l, &p) == 1);
    }
    CHECK(portNetLockCanRun(&l, 1));
    CHECK(!portNetLockCanRun(&l, 2));                     /* remote 2 missing */
    CHECK(!portNetLockCanRun(&l, 0));
    CHECK(same(portNetLockRemoteAt(&l, 1), &got));
    CHECK(portNetLockRemoteAt(&l, 2) == NULL);
    /* the sample of tick 1 becomes the record for tick 1 + delay = 4 */
    got = mk(0, 1);
    CHECK(portNetLockPushLocal(&l, &got) == 4);
    CHECK(same(portNetLockLocalAt(&l, 4), &got));
    CHECK(portNetLockLocalAt(&l, 5) == NULL);
}

static void test_recv_rules(void)
{
    PortNetLock l;
    PortNetInputPkt p;
    PortNetInput z;
    int i;
    memset(&z, 0, sizeof(z));
    portNetLockInit(&l, 3);
    for (i = 0; i < 3; i++) portNetLockPushLocal(&l, &z);
    memset(&p, 0, sizeof(p));
    p.firstTick = 1;
    p.ack = 3;
    p.count = 4;
    for (i = 0; i < 4; i++) p.rec[i] = mk(1, 1u + (unsigned) i);
    CHECK(portNetLockRecv(&l, &p) == 4 && l.remoteCount == 4);
    CHECK(l.theirAck == 3);
    CHECK(portNetLockRecv(&l, &p) == 0 && l.remoteCount == 4);        /* duplicate packet */
    p.firstTick = 7;                                                  /* a gap: 5, 6 missing */
    CHECK(portNetLockRecv(&l, &p) == 0 && l.remoteCount == 4);
    p.firstTick = 3;                                                  /* overlap, then 2 new (5, 6) */
    for (i = 0; i < 4; i++) p.rec[i] = mk(1, 3u + (unsigned) i);
    CHECK(portNetLockRecv(&l, &p) == 2 && l.remoteCount == 6);
    CHECK(same(portNetLockRemoteAt(&l, 6), &p.rec[3]));
    p.count = 0;
    p.ack = 1;                                                        /* an old ack never lowers ours */
    portNetLockRecv(&l, &p);
    CHECK(l.theirAck == 3);
    p.ack = 99;                                                       /* a lying ack is clamped to what we sent */
    portNetLockRecv(&l, &p);
    CHECK(l.theirAck == 4);
}

/* A late INPUT packet of an earlier stage launch must not feed the next one (stage-run id in INPUT). */
static void test_run_filter(void)
{
    PortNetLock l;
    PortNetInputPkt p;
    PortNetInput z;
    unsigned char wire[PORT_NET_BUF_MAX];
    PortNetInputPkt q;
    int i, n;
    memset(&z, 0, sizeof(z));
    portNetLockInit(&l, 3);
    for (i = 0; i < 3; i++) portNetLockPushLocal(&l, &z);
    memset(&p, 0, sizeof(p));
    p.run = 1;                               /* the old stage */
    p.firstTick = 1;
    p.ack = 3;
    p.count = 4;
    for (i = 0; i < 4; i++) p.rec[i] = mk(1, 1u + (unsigned) i);
    CHECK(portNetLockRecvRun(&l, 2, &p) == -1);          /* we are in run 2: dropped */
    CHECK(l.remoteCount == 0 && l.theirAck == 1);        /* nothing stored, the ack stays at its initial value */
    p.run = 3;                                           /* a future run is no better */
    CHECK(portNetLockRecvRun(&l, 2, &p) == -1 && l.remoteCount == 0);
    p.run = 2;
    CHECK(portNetLockRecvRun(&l, 2, &p) == 4 && l.remoteCount == 4 && l.theirAck == 3);
    p.run = 1;                                           /* the late old packet again, now with records we lack */
    p.firstTick = 5;
    p.count = 2;
    p.rec[0] = mk(9, 5);
    p.rec[1] = mk(9, 6);
    CHECK(portNetLockRecvRun(&l, 2, &p) == -1 && l.remoteCount == 4);
    /* and the run survives the wire */
    p.run = 77;
    n = portNetInputPack(&p, wire, sizeof(wire));
    CHECK(n > 0 && portNetInputUnpack(wire, n, &q) == 0 && q.run == 77);
    CHECK(portNetLockRecvRun(&l, 77, &q) == 2 && l.remoteCount == 6);
}

static void test_build_pkt(void)
{
    PortNetLock l;
    PortNetInputPkt p, q;
    PortNetInput z;
    unsigned int t;
    memset(&z, 0, sizeof(z));
    portNetLockInit(&l, 3);
    portNetLockBuildPkt(&l, &p);
    CHECK(p.count == 0 && p.ack == 1 && p.firstTick == 1);
    CHECK(!portNetLockUnacked(&l));
    for (t = 1; t <= 5; t++)
    {
        PortNetInput r = mk(0, t);
        portNetLockPushLocal(&l, &r);
    }
    portNetLockBuildPkt(&l, &p);
    CHECK(p.firstTick == 1 && p.count == 5 && p.ack == 1);
    {
        PortNetInput e1 = mk(0, 1), e5 = mk(0, 5);
        CHECK(same(&p.rec[0], &e1) && same(&p.rec[4], &e5));
    }
    CHECK(portNetLockUnacked(&l));
    memset(&q, 0, sizeof(q));
    q.firstTick = 1;
    q.ack = 4;
    q.count = 2;
    q.rec[0] = mk(1, 1);
    q.rec[1] = mk(1, 2);
    portNetLockRecv(&l, &q);
    portNetLockBuildPkt(&l, &p);
    CHECK(p.firstTick == 4 && p.count == 2 && p.ack == 3);            /* only ticks 4 and 5 are unacked */
    {
        PortNetInput e4 = mk(0, 4);
        CHECK(same(&p.rec[0], &e4));
    }
    q.count = 0;
    q.ack = 6;
    portNetLockRecv(&l, &q);
    portNetLockBuildPkt(&l, &p);
    CHECK(p.count == 0 && !portNetLockUnacked(&l));                   /* all acked: a pure ack packet */
}

static void test_flow_control(void)
{
    PortNetLock l;
    PortNetInputPkt p;
    PortNetInput z;
    unsigned int t;
    memset(&z, 0, sizeof(z));
    memset(&p, 0, sizeof(p));
    portNetLockInit(&l, 3);
    for (t = 1; t <= 3; t++) portNetLockPushLocal(&l, &z);
    p.firstTick = 1;
    p.count = PORT_NET_MAX_RECS;
    p.ack = 1;
    for (t = 0; t < PORT_NET_MAX_RECS; t++) p.rec[t] = mk(1, 1 + t);
    portNetLockRecv(&l, &p);                                         /* remote 1..16, partner never acks us */
    for (t = 1; t <= 12; t++)                                        /* run ticks, pushing a sample per tick */
    {
        CHECK(portNetLockCanRun(&l, t));
        portNetLockConsume(&l, t);
        CHECK(portNetLockPushLocal(&l, &z) == t + 3);
    }
    CHECK(l.localCount == 15 && portNetLockCanRun(&l, 13));          /* 15 unacked: one more push still fits a packet */
    portNetLockConsume(&l, 13);
    CHECK(portNetLockPushLocal(&l, &z) == 16);
    CHECK(!portNetLockCanRun(&l, 14));                               /* 16 unacked: the packet is full, wait for an ack */
    p.count = 0;
    p.ack = 4;                                                       /* the partner acks 1..3 */
    portNetLockRecv(&l, &p);
    CHECK(portNetLockCanRun(&l, 14));
    portNetLockBuildPkt(&l, &p);
    CHECK(p.count <= PORT_NET_MAX_RECS && p.firstTick + (unsigned) p.count - 1 == l.localCount);
}

/* two sides over a lossy, laggy, reordering fake link: every tick must see the partner's sample from
 * `delay` ticks earlier on both sides, and both must reach the end */
typedef struct Wire { PortNetInputPkt pkt; int due; } Wire;

static void run_pair(int delay, int lagTicks, int lossPct, unsigned seed, int ticks)
{
    PortNetLock lk[2];
    unsigned int simTick[2] = { 0, 0 };
    Wire q[2][256];
    int qn[2] = { 0, 0 };
    PortNetInput z, applied[2][2][400];
    int now, side, done = 0, i, ok = 1;
    unsigned int t;
    memset(&z, 0, sizeof(z));
    srand(seed);
    for (side = 0; side < 2; side++)
    {
        portNetLockInit(&lk[side], delay);
        for (t = 1; t <= (unsigned) delay; t++) portNetLockPushLocal(&lk[side], &z);   /* neutral prefill */
    }
    for (now = 0; now < 4000 && !done; now++)
    {
        for (side = 0; side < 2; side++)
        {
            int o = 1 - side, k = 0;
            for (i = 0; i < qn[side]; i++)                              /* deliver due packets, shuffled order inside a tick */
            {
                if (q[side][i].due <= now) portNetLockRecv(&lk[side], &q[side][i].pkt);
                else q[side][k++] = q[side][i];
            }
            qn[side] = k;
            if (simTick[side] < (unsigned) ticks && portNetLockCanRun(&lk[side], simTick[side] + 1))
            {
                t = ++simTick[side];
                portNetLockConsume(&lk[side], t);
                applied[side][0][t] = *portNetLockLocalAt(&lk[side], t);
                applied[side][1][t] = *portNetLockRemoteAt(&lk[side], t);
                {   /* this tick's sample goes in as tick t + delay */
                    PortNetInput s = mk(side, t);
                    CHECK(portNetLockPushLocal(&lk[side], &s) == t + (unsigned) delay);
                }
            }
            /* every iteration: send (resend while stalled), subject to loss */
            if (rand() % 100 >= lossPct && qn[o] < 250)
            {
                portNetLockBuildPkt(&lk[side], &q[o][qn[o]].pkt);
                q[o][qn[o]].due = now + lagTicks + (rand() % 3);
                qn[o]++;
            }
        }
        done = simTick[0] >= (unsigned) ticks && simTick[1] >= (unsigned) ticks;
    }
    CHECK(done);
    for (t = 1; t <= (unsigned) ticks && done; t++)
    {
        PortNetInput expect0 = (t <= (unsigned) delay) ? z : mk(0, t - (unsigned) delay);
        PortNetInput expect1 = (t <= (unsigned) delay) ? z : mk(1, t - (unsigned) delay);
        if (!same(&applied[0][0][t], &expect0) || !same(&applied[0][1][t], &expect1)
            || !same(&applied[1][0][t], &expect1) || !same(&applied[1][1][t], &expect0))
        {
            ok = 0;
        }
    }
    CHECK(ok);
}

int main(void)
{
    test_clamp();
    test_schedule();
    test_recv_rules();
    test_run_filter();
    test_build_pkt();
    test_flow_control();
    run_pair(3, 0, 0, 1, 300);     /* clean, zero lag */
    run_pair(3, 6, 0, 2, 300);     /* lag beyond the delay: stalls, still exact */
    run_pair(3, 2, 20, 3, 300);    /* 20% loss */
    run_pair(2, 4, 40, 4, 300);    /* min delay, 40% loss */
    run_pair(8, 10, 30, 5, 300);   /* max delay */
    printf("netlock: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail != 0;
}
