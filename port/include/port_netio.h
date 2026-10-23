/* port_netio.h - the ENet half of netplay (port/netio/port_netio.c, built into ge_enet with the real libc
 * headers). Plain C types only, so port_net.c (game headers) can include it. */
#ifndef PORT_NETIO_H
#define PORT_NETIO_H

#define NETIO_CH_INPUT   0   /* unreliable sequenced */
#define NETIO_CH_SESSION 1   /* reliable */

enum { NETIO_DOWN = 0, NETIO_LISTENING, NETIO_CONNECTING, NETIO_CONNECTED, NETIO_LOST };

int  netioHost(int port);                   /* 0 = ok; opens the socket and ENet. */
int  netioJoin(const char *ip, int port);   /* 0 = ok; starts connecting. */
void netioShutdown(void);
void netioSetFake(int lagMs, int jitterMs, int lossPct);   /* outgoing packets; loss only hits the input channel */
void netioService(void);                    /* pump the socket and the fake-network queue; non-blocking */
int  netioState(void);
int  netioSend(int channel, const void *data, int len);
int  netioRecv(int *channel, void *buf, int cap);   /* length of one waiting packet, 0 if none */
unsigned int netioNowMs(void);
void netioSleepMs(int ms);
int  netioRttMs(void);
int  netioRttVarMs(void);                   /* ENet's round-trip variance (jitter), ms */
void netioCounters(unsigned int *inBytes, unsigned int *outBytes, unsigned int *inPkts, unsigned int *outPkts);   /* free-running wire totals (UDP datagrams + 28 B IP/UDP header each) */
unsigned int netioSilentMs(void);          /* ms since any datagram (pings included) came from the partner; 0 when not connected */
int  netioLossPct(void);                    /* ENet's packet-loss estimate for the link, percent */
void netioDisconnect(void);                 /* flushes, then closes the link */

#endif /* PORT_NETIO_H */
