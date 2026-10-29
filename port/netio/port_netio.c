/* port_netio.c - ENet transport for netplay. Built into the ge_enet library (real libc headers, see
 * CMakeLists.txt); the game half is port/src/port_net.c. One peer, two channels, a fake-network queue. */
#include <stdlib.h>
#include <string.h>
#include <enet/enet.h>
#ifndef _WIN32
#include <unistd.h>
#endif
#include "port_netio.h"

#define NIO_PKT_MAX 600
#define NIO_RECV_CAP 128
#define NIO_SEND_CAP 256

typedef struct Pkt { int channel, len; unsigned char data[NIO_PKT_MAX]; } Pkt;
typedef struct Delayed { unsigned int due; Pkt p; } Delayed;

static ENetHost *sHost;
static ENetPeer *sPeer;
static int sState = NETIO_DOWN;
static int sInit;
static Pkt sRecv[NIO_RECV_CAP];
static int sRecvHead, sRecvCount;
static Delayed sDelay[NIO_SEND_CAP];
static int sDelayCount;
static unsigned int sLastReliableDue;
static int sHaveReliableDue;   /* sLastReliableDue is set: netioNowMs() is wall-clock ms on Linux (ms since boot on Windows), so 0 is no "before" */
static int sLag, sJitter, sLoss;

static int nioInit(void)
{
    if (!sInit)
    {
        if (enet_initialize() != 0) return -1;
        sInit = 1;
    }
    return 0;
}

int netioHost(int port)
{
    ENetAddress a;
    if (nioInit() != 0) return -1;
    a.host = ENET_HOST_ANY;
    a.port = (enet_uint16) port;
    sHost = enet_host_create(&a, 1, 2, 0, 0);
    if (sHost == NULL) return -1;
    sState = NETIO_LISTENING;
    return 0;
}

int netioJoin(const char *ip, int port)
{
    ENetAddress a;
    if (nioInit() != 0) return -1;
    if (enet_address_set_host(&a, ip) != 0) return -1;
    a.port = (enet_uint16) port;
    sHost = enet_host_create(NULL, 1, 2, 0, 0);
    if (sHost == NULL) return -1;
    sPeer = enet_host_connect(sHost, &a, 2, 0);
    if (sPeer == NULL) return -1;
    sState = NETIO_CONNECTING;
    return 0;
}

void netioSetFake(int lagMs, int jitterMs, int lossPct)
{
    sLag = lagMs < 0 ? 0 : lagMs;
    sJitter = jitterMs < 0 ? 0 : jitterMs;
    sLoss = lossPct < 0 ? 0 : (lossPct > 100 ? 100 : lossPct);
}

unsigned int netioNowMs(void)
{
    return (unsigned int) enet_time_get();
}

void netioSleepMs(int ms)
{
#ifdef _WIN32
    Sleep((DWORD) ms);
#else
    usleep((useconds_t) ms * 1000u);
#endif
}

static void nioSendNow(const Pkt *p)
{
    ENetPacket *pk;
    if (sPeer == NULL || sState != NETIO_CONNECTED) return;
    pk = enet_packet_create(p->data, (size_t) p->len, p->channel == NETIO_CH_SESSION ? ENET_PACKET_FLAG_RELIABLE : 0);
    if (pk != NULL && enet_peer_send(sPeer, (enet_uint8) p->channel, pk) != 0) enet_packet_destroy(pk);
}

int netioSend(int channel, const void *data, int len)
{
    Delayed *d;
    if (len <= 0 || len > NIO_PKT_MAX || (channel != NETIO_CH_INPUT && channel != NETIO_CH_SESSION)) return -1;
    if (channel == NETIO_CH_INPUT && sLoss > 0 && (rand() % 100) < sLoss) return 0;   /* dropped by the knob */
    if (sLag == 0 && sJitter == 0 && sDelayCount == 0)
    {
        Pkt p;
        p.channel = channel; p.len = len; memcpy(p.data, data, (size_t) len);
        nioSendNow(&p);
        return 0;
    }
    if (sDelayCount >= NIO_SEND_CAP) return -1;
    d = &sDelay[sDelayCount++];
    d->due = netioNowMs() + (unsigned int) sLag + (sJitter > 0 ? (unsigned int) (rand() % (sJitter + 1)) : 0u);
    if (channel == NETIO_CH_SESSION)
    {
        /* reliable stays in order. Without the flag the first one was compared with 0: whenever the clock's top bit is set
           (on Linux about 24.9 days of every 49.7) it was moved back to due 0, which never
           comes due either, and the HELLO and every later session message were held forever ("timed out waiting for HELLO") */
        if (sHaveReliableDue && (int) (d->due - sLastReliableDue) < 0) d->due = sLastReliableDue;
        sLastReliableDue = d->due;
        sHaveReliableDue = 1;
    }
    d->p.channel = channel; d->p.len = len; memcpy(d->p.data, data, (size_t) len);
    return 0;
}

static void nioPushRecv(int channel, const void *data, int len)
{
    Pkt *p;
    if (len > NIO_PKT_MAX || sRecvCount >= NIO_RECV_CAP) return;
    p = &sRecv[(sRecvHead + sRecvCount++) % NIO_RECV_CAP];
    p->channel = channel; p->len = len; memcpy(p->data, data, (size_t) len);
}

void netioService(void)
{
    ENetEvent ev;
    unsigned int now;
    int i, keep = 0, heldReliable = 0;

    if (sHost == NULL) return;
    now = netioNowMs();
    for (i = 0; i < sDelayCount; i++)   /* release due packets; a reliable one never passes an earlier held reliable one */
    {
        const int due = (int) (now - sDelay[i].due) >= 0;
        const int rel = sDelay[i].p.channel == NETIO_CH_SESSION;
        if (due && !(rel && heldReliable)) nioSendNow(&sDelay[i].p);
        else
        {
            if (rel) heldReliable = 1;
            sDelay[keep++] = sDelay[i];
        }
    }
    sDelayCount = keep;
    while (enet_host_service(sHost, &ev, 0) > 0)
    {
        switch (ev.type)
        {
            case ENET_EVENT_TYPE_CONNECT:
                if (sPeer != NULL && sPeer != ev.peer && sState == NETIO_CONNECTED)
                {
                    enet_peer_disconnect_now(ev.peer, 0);   /* a third party: only one partner */
                    break;
                }
                sPeer = ev.peer;
                enet_peer_timeout(sPeer, 32, 15000, 60000);   /* a stage load blocks the loop for seconds; do not drop the link */
                sState = NETIO_CONNECTED;
                break;
            case ENET_EVENT_TYPE_DISCONNECT:
                if (ev.peer == sPeer)
                {
                    sPeer = NULL;
                    sState = NETIO_LOST;
                }
                break;
            case ENET_EVENT_TYPE_RECEIVE:
                nioPushRecv((int) ev.channelID, ev.packet->data, (int) ev.packet->dataLength);
                enet_packet_destroy(ev.packet);
                break;
            default:
                break;
        }
    }
    if (sState == NETIO_CONNECTING && sPeer != NULL && sPeer->state == ENET_PEER_STATE_DISCONNECTED) sState = NETIO_LOST;
}

int netioState(void)
{
    return sState;
}

int netioRecv(int *channel, void *buf, int cap)
{
    Pkt *p;
    int n;
    if (sRecvCount == 0) return 0;
    p = &sRecv[sRecvHead];
    n = p->len;
    if (n > cap) n = cap;
    memcpy(buf, p->data, (size_t) n);
    if (channel != NULL) *channel = p->channel;
    sRecvHead = (sRecvHead + 1) % NIO_RECV_CAP;
    sRecvCount--;
    return n;
}

int netioRttMs(void)
{
    /* ENet's smoothed round trip; its acks and pings bypass the fake-network queue, so with GE_NET_LAG the game packets' delay (out and
     * back, plus the mean jitter) is added: the figure then says what the game traffic sees */
    return sPeer != NULL ? (int) sPeer->roundTripTime + 2 * sLag + sJitter : 0;
}

int netioRttVarMs(void)
{
    return sPeer != NULL ? (int) sPeer->roundTripTimeVariance + sJitter / 2 : 0;
}

void netioCounters(unsigned int *inBytes, unsigned int *outBytes, unsigned int *inPkts, unsigned int *outPkts)
{
    /* ENet's own UDP datagram totals (payload of the datagram: its header, commands and acks, pings included), plus the 28 bytes
     * of IPv4 + UDP header each datagram costs on the wire. Free-running 32-bit counters: callers use differences. */
    *inBytes = *outBytes = *inPkts = *outPkts = 0;
    if (sHost != NULL)
    {
        *inBytes = (unsigned int) sHost->totalReceivedData + 28u * (unsigned int) sHost->totalReceivedPackets;
        *outBytes = (unsigned int) sHost->totalSentData + 28u * (unsigned int) sHost->totalSentPackets;
        *inPkts = (unsigned int) sHost->totalReceivedPackets;
        *outPkts = (unsigned int) sHost->totalSentPackets;
    }
}

unsigned int netioSilentMs(void)
{
    /* ENet stamps lastReceiveTime on every datagram from the peer, its keep-alive pings included (every 500 ms), so a live partner
     * is never silent for long even when it sends no game packets */
    if (sPeer == NULL || sState != NETIO_CONNECTED) return 0;
    return (unsigned int) (enet_time_get() - sPeer->lastReceiveTime);
}

int netioLossPct(void)
{
    /* ENet's own packet-loss estimate of the link, 0..100 (packetLoss is scaled to ENET_PEER_PACKET_LOSS_SCALE) */
    return sPeer != NULL ? (int) ((unsigned long) sPeer->packetLoss * 100ul / ENET_PEER_PACKET_LOSS_SCALE) : 0;
}

void netioDisconnect(void)
{
    int i;
    for (i = 0; i < sDelayCount; i++) nioSendNow(&sDelay[i].p);   /* flush the queue, lag or not */
    sDelayCount = 0;
    if (sHost != NULL)
    {
        if (sPeer != NULL) enet_peer_disconnect_now(sPeer, 0);
        enet_host_flush(sHost);
    }
}

void netioShutdown(void)
{
    netioDisconnect();
    if (sHost != NULL) enet_host_destroy(sHost);
    sHost = NULL;
    sPeer = NULL;
    sState = NETIO_DOWN;
    if (sInit) enet_deinitialize();
    sInit = 0;
}
