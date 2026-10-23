/* port_netlock.c - pure lockstep schedule and ack logic, unit tested by tests/netlock_test.c. */
#include <string.h>
#include "port_netlock.h"

#define SLOT(t) ((t) & (PORT_NETLOCK_RING - 1))

int portNetLockClampDelay(int delay)
{
    if (delay < PORT_NETLOCK_MIN_DELAY) return PORT_NETLOCK_MIN_DELAY;
    if (delay > PORT_NETLOCK_MAX_DELAY) return PORT_NETLOCK_MAX_DELAY;
    return delay;
}

void portNetLockInit(PortNetLock *l, int delay)
{
    memset(l, 0, sizeof(*l));
    l->delay = portNetLockClampDelay(delay);
    l->theirAck = 1;
}

unsigned int portNetLockPushLocal(PortNetLock *l, const PortNetInput *in)
{
    unsigned int next = l->localCount + 1;
    /* the oldest record the partner may still need is theirAck (or later); never overwrite it */
    if (next - l->theirAck >= PORT_NETLOCK_RING) return 0;
    l->local[SLOT(next)] = *in;
    l->localCount = next;
    return next;
}

int portNetLockCanRun(const PortNetLock *l, unsigned int tick)
{
    if (tick == 0 || tick > l->remoteCount || tick > l->localCount) return 0;
    return (l->localCount + 1 - l->theirAck) < PORT_NET_MAX_RECS;   /* unacked records before the next push */
}

void portNetLockConsume(PortNetLock *l, unsigned int tick)
{
    if (tick > l->simTick) l->simTick = tick;
}

const PortNetInput *portNetLockLocalAt(const PortNetLock *l, unsigned int tick)
{
    if (tick == 0 || tick > l->localCount || l->localCount - tick >= PORT_NETLOCK_RING) return 0;
    return &l->local[SLOT(tick)];
}

const PortNetInput *portNetLockRemoteAt(const PortNetLock *l, unsigned int tick)
{
    if (tick == 0 || tick > l->remoteCount || l->remoteCount - tick >= PORT_NETLOCK_RING) return 0;
    return &l->remote[SLOT(tick)];
}

int portNetLockRecv(PortNetLock *l, const PortNetInputPkt *pkt)
{
    int i, stored = 0;
    for (i = 0; i < pkt->count && i < PORT_NET_MAX_RECS; i++)
    {
        unsigned int tick = pkt->firstTick + (unsigned int) i;
        if (tick != l->remoteCount + 1) continue;                   /* duplicate, or a gap we cannot fill yet */
        if (tick - l->simTick > PORT_NETLOCK_RING - 1) break;       /* would overwrite a record the sim may still read */
        l->remote[SLOT(tick)] = pkt->rec[i];
        l->remoteCount = tick;
        stored++;
    }
    {
        unsigned int ack = pkt->ack;
        if (ack > l->localCount + 1) ack = l->localCount + 1;       /* cannot ack what was never sent */
        if (ack > l->theirAck) l->theirAck = ack;
    }
    return stored;
}

void portNetLockBuildPkt(const PortNetLock *l, PortNetInputPkt *out)
{
    unsigned int first = l->theirAck;
    int i;
    memset(out, 0, sizeof(*out));
    out->ack = l->remoteCount + 1;
    if (l->localCount + 1 > first + PORT_NET_MAX_RECS) first = l->localCount + 1 - PORT_NET_MAX_RECS;
    if (first < 1) first = 1;
    out->firstTick = first;
    out->count = (l->localCount >= first) ? (int) (l->localCount - first + 1) : 0;
    for (i = 0; i < out->count; i++) out->rec[i] = l->local[SLOT(first + (unsigned int) i)];
}

int portNetLockUnacked(const PortNetLock *l)
{
    return l->localCount >= l->theirAck;
}

int portNetLockRecvRun(PortNetLock *l, unsigned int run, const PortNetInputPkt *pkt)
{
    if (pkt->run != run) return -1;   /* a late packet of another stage launch must not feed this one */
    return portNetLockRecv(l, pkt);
}
