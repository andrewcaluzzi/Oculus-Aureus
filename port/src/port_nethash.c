/* port_nethash.c - pure state-hash exchange bookkeeping, unit tested by tests/nethash_test.c. */
#include <string.h>
#include "port_nethash.h"

void portNetHashBookInit(PortNetHashBook *b)
{
    memset(b, 0, sizeof(*b));
}

static int sameKey(const PortNetHashMsg *a, const PortNetHashMsg *b)
{
    return a->run == b->run && a->tick == b->tick;
}

static int findKey(const PortNetHashMsg *arr, const unsigned char *used, const PortNetHashMsg *key)
{
    int i;
    for (i = 0; i < PORT_NETHASH_RING; i++) {
        if (used[i] && sameKey(&arr[i], key)) return i;
    }
    return -1;
}

/* A free slot, else the oldest (lowest run, then tick). */
static int slotFor(const PortNetHashMsg *arr, const unsigned char *used)
{
    int i, oldest = 0;
    for (i = 0; i < PORT_NETHASH_RING; i++) {
        if (!used[i]) return i;
        if (arr[i].run < arr[oldest].run || (arr[i].run == arr[oldest].run && arr[i].tick < arr[oldest].tick)) oldest = i;
    }
    return oldest;
}

static int judge(PortNetHashBook *b, const PortNetHashMsg *mine, const PortNetHashMsg *theirs)
{
    b->compared++;
    if (mine->hash == theirs->hash) return PORT_NETHASH_MATCH;
    b->mismatched++;
    return PORT_NETHASH_DIFFER;
}

int portNetHashBookAddMine(PortNetHashBook *b, const PortNetHashMsg *m, PortNetHashMsg *other)
{
    int k = findKey(b->theirs, b->theirsUsed, m);
    if (k >= 0) {
        *other = b->theirs[k];
        b->theirsUsed[k] = 0;
        return judge(b, m, other);
    }
    k = findKey(b->mine, b->mineUsed, m);   /* the same tick hashed twice: keep the newest */
    if (k < 0) k = slotFor(b->mine, b->mineUsed);
    b->mine[k] = *m;
    b->mineUsed[k] = 1;
    return PORT_NETHASH_WAIT;
}

int portNetHashBookAddTheirs(PortNetHashBook *b, const PortNetHashMsg *m, PortNetHashMsg *other)
{
    int k = findKey(b->mine, b->mineUsed, m);
    if (k >= 0) {
        *other = b->mine[k];
        b->mineUsed[k] = 0;
        return judge(b, other, m);
    }
    k = findKey(b->theirs, b->theirsUsed, m);
    if (k < 0) k = slotFor(b->theirs, b->theirsUsed);
    b->theirs[k] = *m;
    b->theirsUsed[k] = 1;
    return PORT_NETHASH_WAIT;
}
