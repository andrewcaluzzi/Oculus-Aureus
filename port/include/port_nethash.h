/* port_nethash.h - pure bookkeeping for the netplay state-hash exchange (no game or libc-shadowing includes, so
 * tests/nethash_test.c builds it).
 *
 * Each side hashes the world at the same sim ticks and sends the hash to the partner. The two hashes for a tick
 * may arrive in either order (lockstep lets one side lead by up to the input delay), so each side keeps its own
 * and the partner's hashes until both are present, then compares them. */
#ifndef PORT_NETHASH_H
#define PORT_NETHASH_H

#include "port_netmsg.h"

#define PORT_NETHASH_RING 16   /* pending entries per side; the oldest is dropped when full */

typedef struct PortNetHashBook
{
    PortNetHashMsg mine[PORT_NETHASH_RING];
    PortNetHashMsg theirs[PORT_NETHASH_RING];
    unsigned char  mineUsed[PORT_NETHASH_RING];
    unsigned char  theirsUsed[PORT_NETHASH_RING];
    unsigned int   compared;    /* how many ticks were compared */
    unsigned int   mismatched;  /* ...and how many differed */
} PortNetHashBook;

enum { PORT_NETHASH_WAIT = 0, PORT_NETHASH_MATCH = 1, PORT_NETHASH_DIFFER = 2 };

void portNetHashBookInit(PortNetHashBook *b);

/* Our hash for (run, tick) is known. If the partner's is already here the pair is compared and removed:
 * returns MATCH or DIFFER (and fills *other with the partner's message); else WAIT (ours is kept). */
int portNetHashBookAddMine(PortNetHashBook *b, const PortNetHashMsg *m, PortNetHashMsg *other);

/* The partner's hash arrived: the same, with *other = our message. */
int portNetHashBookAddTheirs(PortNetHashBook *b, const PortNetHashMsg *m, PortNetHashMsg *other);

#endif /* PORT_NETHASH_H */
