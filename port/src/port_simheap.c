/* port_simheap.c - the rollback-able high heap (rollback job R2, hard part 2 "the portAllocHigh leak").
 *
 * Converted model display lists must live at host addresses >= 0x10000000 (fast3d reads a lower word as a segment reference), so they
 * came from portAllocHigh: a bump arena that is never freed and never rolled back. A snapshot restore rewinds the memp pools, so
 * re-simulating a tick that spawns an object loaded and converted its model again: a second display list buffer (40-70 KB leaked per
 * window, measured in the spike) at a DIFFERENT address, which then differs between two machines' states and keeps the renderer's
 * address-keyed caches growing.
 *
 * This heap is a bump allocator whose offset is ordinary sim state (this file is in the snapshot's static ranges) and whose used part
 * is a snapshot extent (portSnapAddDyn). A restore rewinds the offset and the bytes together, so the re-simulation allocates the
 * same buffer at the same address and writes the same contents: pointer values stay consistent, nothing leaks. Allocations are
 * zeroed (a reused range holds the first run's bytes). When the 24 MB are used up it falls back to portAllocHigh (the old behaviour:
 * never freed, not rolled back) and says so once. */
#ifndef TARGET_N64

#include <string.h>
#include "port_snap.h"

extern void *portAllocHighDedicated(unsigned int size);   /* pcsched.c */
extern void *portAllocHigh(unsigned int size);            /* pcsched.c */
extern void portConLogf(const char *fmt, ...);

#define SIMHEAP_SIZE (24u * 1024u * 1024u)

static unsigned char *sBase;
static unsigned int sUsed;       /* sim state: restored with the snapshot */
static int sTried, sFull;

static int simheapRange(int i, unsigned char **lo, unsigned int *len)
{
    if (i == 0 && sBase != NULL && sUsed != 0)
    {
        *lo = sBase;
        *len = sUsed;
        return 1;
    }
    return 0;
}

void *portAllocSimHigh(unsigned int size)
{
    void *p;

    size = (size + 15u) & ~15u;
    if (!sTried)
    {
        sTried = 1;
        sBase = (unsigned char *) portAllocHighDedicated(SIMHEAP_SIZE);
        if (sBase != NULL)
        {
            portSnapAddDyn("simheap", simheapRange);
            portConLogf("ge: simheap: %u MB at %p (rollback-able high heap for converted model display lists)\n", SIMHEAP_SIZE >> 20, (void *) sBase);
        }
        else
        {
            portConLogf("ge: simheap: no dedicated mapping, converted display lists use the never-freed high arena (not rollback-safe)\n");
        }
    }
    if (sBase != NULL && sUsed + size <= SIMHEAP_SIZE)
    {
        p = sBase + sUsed;
        sUsed += size;
        memset(p, 0, size);
        return p;
    }
    if (sBase != NULL && !sFull)
    {
        sFull = 1;
        portConLogf("ge: simheap: full (%u MB): further display lists use the never-freed high arena\n", sUsed >> 20);
    }
    return portAllocHigh(size);
}

#endif
