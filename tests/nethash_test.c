/* nethash_test.c - unit tests for the pure state-hash exchange bookkeeping in port_nethash.c. */
#include <stdio.h>
#include <string.h>
#include "port_nethash.h"

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

static PortNetHashMsg mk(unsigned run, unsigned tick, unsigned hash)
{
    PortNetHashMsg m;
    memset(&m, 0, sizeof(m));
    m.run = run;
    m.tick = tick;
    m.hash = hash;
    m.nfields = 2;
    m.fields[0] = hash ^ 1u;
    m.fields[1] = hash ^ 2u;
    return m;
}

static void test_orders(void)
{
    PortNetHashBook b;
    PortNetHashMsg o, a, c;
    portNetHashBookInit(&b);
    a = mk(1, 30, 0xAAu);
    CHECK(portNetHashBookAddMine(&b, &a, &o) == PORT_NETHASH_WAIT);          /* ours first */
    c = mk(1, 30, 0xAAu);
    CHECK(portNetHashBookAddTheirs(&b, &c, &o) == PORT_NETHASH_MATCH && o.hash == 0xAAu);
    a = mk(1, 60, 0xBBu);
    c = mk(1, 60, 0xBBu);
    CHECK(portNetHashBookAddTheirs(&b, &c, &o) == PORT_NETHASH_WAIT);        /* the partner's first */
    CHECK(portNetHashBookAddMine(&b, &a, &o) == PORT_NETHASH_MATCH);
    CHECK(b.compared == 2 && b.mismatched == 0);
    a = mk(1, 90, 0x11u);
    c = mk(1, 90, 0x22u);
    CHECK(portNetHashBookAddMine(&b, &a, &o) == PORT_NETHASH_WAIT);
    CHECK(portNetHashBookAddTheirs(&b, &c, &o) == PORT_NETHASH_DIFFER && o.hash == 0x11u && o.fields[0] == (0x11u ^ 1u));
    CHECK(b.compared == 3 && b.mismatched == 1);
    /* a compared pair is gone: a repeat waits again */
    CHECK(portNetHashBookAddTheirs(&b, &c, &o) == PORT_NETHASH_WAIT);
}

static void test_run_and_ring(void)
{
    PortNetHashBook b;
    PortNetHashMsg o, a, c;
    int i;
    portNetHashBookInit(&b);
    a = mk(1, 30, 0x5u);
    c = mk(2, 30, 0x5u);                                                     /* same tick, another stage launch */
    CHECK(portNetHashBookAddMine(&b, &a, &o) == PORT_NETHASH_WAIT);
    CHECK(portNetHashBookAddTheirs(&b, &c, &o) == PORT_NETHASH_WAIT);        /* not paired across runs */
    CHECK(b.compared == 0);
    c = mk(1, 30, 0x5u);
    CHECK(portNetHashBookAddTheirs(&b, &c, &o) == PORT_NETHASH_MATCH);
    /* never paired entries do not grow without bound; the oldest goes */
    portNetHashBookInit(&b);
    for (i = 1; i <= PORT_NETHASH_RING + 4; i++) {
        a = mk(1, 30u * (unsigned) i, (unsigned) i);
        CHECK(portNetHashBookAddMine(&b, &a, &o) == PORT_NETHASH_WAIT);
    }
    c = mk(1, 30, 1);                                                        /* tick 30 was pushed out */
    CHECK(portNetHashBookAddTheirs(&b, &c, &o) == PORT_NETHASH_WAIT);
    c = mk(1, 30u * (PORT_NETHASH_RING + 4), PORT_NETHASH_RING + 4);         /* the newest is still there */
    CHECK(portNetHashBookAddTheirs(&b, &c, &o) == PORT_NETHASH_MATCH);
}

int main(void)
{
    test_orders();
    test_run_and_ring();
    printf("nethash: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail != 0;
}
