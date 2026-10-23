/* port_netlock.h - pure lockstep schedule and ack logic for netplay (no game or libc-shadowing includes, so
 * tests/netlock_test.c builds it).
 *
 * Ticks are numbered from 1. Each side keeps one record per tick for itself (local) and for the partner
 * (remote). The local input sampled during tick t is pushed as the record for tick t + delay, so the first
 * `delay` records are prefilled before tick 1. Tick t may run once the remote record for t is here. */
#ifndef PORT_NETLOCK_H
#define PORT_NETLOCK_H

#include "port_netmsg.h"

#define PORT_NETLOCK_RING      64   /* power of two, larger than 2 * max delay + PORT_NET_MAX_RECS */
#define PORT_NETLOCK_MIN_DELAY 2    /* the sample of tick t is pushed at the start of t + 1 */
#define PORT_NETLOCK_MAX_DELAY 8

typedef struct PortNetLock
{
    int          delay;
    unsigned int localCount;    /* local records held: ticks 1..localCount */
    unsigned int remoteCount;   /* remote records held, contiguous: ticks 1..remoteCount */
    unsigned int theirAck;      /* the partner's next wanted tick: it holds our ticks 1..theirAck-1 */
    unsigned int simTick;       /* highest tick the sim has consumed */
    PortNetInput local[PORT_NETLOCK_RING];
    PortNetInput remote[PORT_NETLOCK_RING];
} PortNetLock;

int  portNetLockClampDelay(int delay);   /* into [MIN_DELAY, MAX_DELAY] */
void portNetLockInit(PortNetLock *l, int delay);

/* Appends the local record for the next tick (localCount + 1). Returns that tick, or 0 if the ring is full. */
unsigned int portNetLockPushLocal(PortNetLock *l, const PortNetInput *in);

/* 1 if tick may run: the remote record is here, the local one is too, and one more local push will not
 * outrun the partner's acks by more than a packet can carry. */
int portNetLockCanRun(const PortNetLock *l, unsigned int tick);

/* The sim consumed `tick`: its ring slots may be reused. NULL from the *At calls means not held. */
void portNetLockConsume(PortNetLock *l, unsigned int tick);
const PortNetInput *portNetLockLocalAt(const PortNetLock *l, unsigned int tick);
const PortNetInput *portNetLockRemoteAt(const PortNetLock *l, unsigned int tick);

/* Takes a partner INPUT packet: stores the records that continue the contiguous run, ignores duplicates
 * and gaps, raises the partner's ack. Returns how many new remote records were stored. */
int portNetLockRecv(PortNetLock *l, const PortNetInputPkt *pkt);

/* Same, for the stage launch `run`: a packet of another run (a late packet of the previous stage) is dropped
 * untouched and -1 is returned. */
int portNetLockRecvRun(PortNetLock *l, unsigned int run, const PortNetInputPkt *pkt);

/* Fills the packet to send: our ack (remoteCount + 1) and every local record the partner has not acked,
 * at most PORT_NET_MAX_RECS (the newest). count 0 is a pure ack. */
void portNetLockBuildPkt(const PortNetLock *l, PortNetInputPkt *out);

/* 1 if there are local records the partner has not acked (worth resending while stalled). */
int portNetLockUnacked(const PortNetLock *l);

#endif /* PORT_NETLOCK_H */
