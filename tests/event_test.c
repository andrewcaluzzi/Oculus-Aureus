/* Unit test for the C event bus (port/src/port_event.c). */
#include <stdio.h>
#include "port_event.h"

static int fails, hits, last[4];

static int udhits;
static void on(int type, int a, int b, int c, int d, void *ud)
{
    hits++;
    if (ud != NULL) { udhits += *(int *)ud; }
    last[0] = type;
    last[1] = a;
    last[2] = b;
    last[3] = c;
}

static int last4[4];
static void on4(int type, int a, int b, int c, int d, void *ud)
{
    (void) type; (void) ud;
    hits++;
    last4[0] = a;
    last4[1] = b;
    last4[2] = c;
    last4[3] = d;
}

#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

int main(void)
{
    int i;

    portEventEmit(PORT_EV_CHR_DIED, 1, 2, 3, 0);   /* no listener: nothing happens */
    CHECK(hits == 0);
    CHECK(portEventOn(PORT_EV_CHR_DIED, on, NULL) == 0);
    CHECK(portEventOn(PORT_EV_NONE, on, NULL) == -1);
    CHECK(portEventOn(PORT_EV_COUNT, on, NULL) == -1);
    portEventEmit(PORT_EV_ZONE_ENTER, 9, 9, 9, 0);   /* other type: not delivered */
    CHECK(hits == 0);
    portEventEmit(PORT_EV_CHR_DIED, 7, PORT_CHR_BOND, 0, 0);
    CHECK(hits == 1 && last[0] == PORT_EV_CHR_DIED && last[1] == 7 && last[2] == PORT_CHR_BOND);
    portEventOff(PORT_EV_CHR_DIED, on, NULL);
    portEventEmit(PORT_EV_CHR_DIED, 1, 1, 1, 0);
    CHECK(hits == 1);
    for (i = 0; i < 8; i++)
    {
        CHECK(portEventOn(PORT_EV_BOND_DIED, on, NULL) == 0);
    }
    CHECK(portEventOn(PORT_EV_BOND_DIED, on, NULL) == -1);   /* full */
    portEventEmit(PORT_EV_BOND_DIED, 0, 0, 0, 0);
    CHECK(hits == 9);
    portEventClearAll();
    portEventEmit(PORT_EV_BOND_DIED, 0, 0, 0, 0);
    CHECK(hits == 9);
    {   /* user data: two registrations of one fn with different ud, Off removes only the match */
        int one = 1, ten = 10;

        hits = 0;
        CHECK(portEventOn(PORT_EV_ZONE_LEAVE, on, &one) == 0);
        CHECK(portEventOn(PORT_EV_ZONE_LEAVE, on, &ten) == 0);
        portEventEmit(PORT_EV_ZONE_LEAVE, 0, 0, 0, 0);
        CHECK(hits == 2 && udhits == 11);
        portEventOff(PORT_EV_ZONE_LEAVE, on, &one);
        portEventEmit(PORT_EV_ZONE_LEAVE, 0, 0, 0, 0);
        CHECK(hits == 3 && udhits == 21);
        portEventOff(PORT_EV_ZONE_LEAVE, on, NULL);   /* no match: no-op */
        portEventEmit(PORT_EV_ZONE_LEAVE, 0, 0, 0, 0);
        CHECK(hits == 4);
        portEventClearAll();
    }
    {   /* 4th arg, AnyListeners, damage milli-units, attacker */
        hits = 0;
        last[3] = 0;
        CHECK(portEventAnyListeners() == 0);
        portEventEmitDamage(1, 2, 3.0f, 4);   /* no listener: nothing */
        CHECK(hits == 0);
        CHECK(portEventOn(PORT_EV_DAMAGE, on4, NULL) == 0);
        CHECK(portEventAnyListeners() == 1);
        portEventEmitDamage(PORT_CHR_BOND, PORT_CHR_NONE, 1.5f, 7);
        CHECK(hits == 1 && last4[0] == PORT_CHR_BOND && last4[1] == PORT_CHR_NONE && last4[2] == 1500 && last4[3] == 7);
        portEventEmit(PORT_EV_ITEM_PICKUP, 1, 2, 3, 4);   /* other type: not delivered */
        CHECK(hits == 1);
        CHECK(portEventAttacker() == PORT_CHR_NONE);
        portEventSetAttacker(55);
        CHECK(portEventAttacker() == 55);
        portEventSetAttacker(PORT_CHR_NONE);
        portEventClearAll();
        CHECK(portEventAnyListeners() == 0);
    }
    if (fails == 0)
    {
        printf("event_test: all passed\n");
    }
    return fails != 0;
}
