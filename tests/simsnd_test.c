/* Unit test for the sim's own view of its sounds (port/src/port_simsnd.c, modding/snapshot-fidelity.md). */
#include <stdio.h>
#include <string.h>
#include "port_simsnd.h"

static int fails;

#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

int main(void)
{
    void *owner = NULL, *owner2 = NULL, *t, *t2, *loop, *tok[PORT_SIMSND_MAX + 1];
    int i;

    portSimSndSetOn(1);
    CHECK(portSimSndOn());
    portSimSndTick(100);
    CHECK(portSimSndNow() == 100);

    /* fields: rounded up, 60 a second */
    CHECK(portSimSndFields(0) == 0);
    CHECK(portSimSndFields(1) == 1);
    CHECK(portSimSndFields(16666) == 1);
    CHECK(portSimSndFields(16667) == 2);
    CHECK(portSimSndFields(1000000) == 60);

    /* a one-shot: starts after 0.5 s, plays 1 s, rings 0.5 s; the owner word holds the token until it ends */
    t = portSimSndStart(42, 500000, 1000000, 500000, PORT_SIMSND_FINAL, &owner);
    CHECK(t != NULL);
    CHECK(portSimSndIsToken(t));
    CHECK(!portSimSndIsToken(&owner));
    CHECK(!portSimSndIsToken(NULL));
    CHECK(!portSimSndIsToken((char *) t + 1));
    owner = t;                                      /* what snd.c does with pendingState->link.next */
    CHECK(portSimSndState(t) == PORT_SIMSND_INIT);  /* before its PLAY event */
    portSimSndTick(130);
    CHECK(portSimSndState(t) == PORT_SIMSND_PLAYING);
    portSimSndTick(219);                            /* starts at 130, ends at 130 + 60 + 30 = 220 */
    CHECK(portSimSndState(t) == PORT_SIMSND_PLAYING);
    CHECK(owner == t);
    portSimSndTick(220);
    CHECK(portSimSndState(t) == PORT_SIMSND_STOPPED);
    CHECK(owner == NULL);                           /* cleared at its end, as the sound player's dispose does */
    CHECK(portSimSndLive() == 0);

    /* a stop: before the voice starts it is gone at once; once playing it rings out its release */
    t = portSimSndStart(1, 100000, 1000000, 200000, PORT_SIMSND_FINAL, &owner);
    owner = t;
    portSimSndStop(t);
    CHECK(portSimSndState(t) == PORT_SIMSND_STOPPED);
    portSimSndTick(221);
    CHECK(owner == NULL);
    t = portSimSndStart(1, 0, 1000000, 200000, PORT_SIMSND_FINAL, &owner);     /* starts at 221, would end at 293 */
    owner = t;
    portSimSndTick(230);
    portSimSndStop(t);
    CHECK(portSimSndState(t) == PORT_SIMSND_STOPPING);
    portSimSndTick(241);                            /* release 12 fields: ends at 242 */
    CHECK(portSimSndState(t) == PORT_SIMSND_STOPPING);
    portSimSndTick(242);
    CHECK(portSimSndState(t) == PORT_SIMSND_STOPPED);
    CHECK(owner == NULL);

    /* looped and retriggered sounds play until stopped; stop-by-flag picks them by their flags */
    loop = portSimSndStart(7, 0, 0, 0, PORT_SIMSND_FINAL | PORT_SIMSND_LOOPED, NULL);
    t2 = portSimSndStart(8, 0, 100000, 0, PORT_SIMSND_FINAL, &owner2);
    owner2 = t2;
    portSimSndTick(100000);
    CHECK(portSimSndState(loop) == PORT_SIMSND_PLAYING);
    CHECK(owner2 == NULL);                          /* the one-shot ended long ago */
    portSimSndStopByFlag(PORT_SIMSND_FINAL | PORT_SIMSND_RETRIGGER);
    CHECK(portSimSndState(loop) == PORT_SIMSND_PLAYING);   /* not retriggered: untouched */
    portSimSndStopByFlag(PORT_SIMSND_FINAL | PORT_SIMSND_LOOPED);
    CHECK(portSimSndState(loop) == PORT_SIMSND_STOPPED);   /* no release: gone */

    /* an owner word that moved on to another sound is left alone */
    t = portSimSndStart(3, 0, 100000, 0, PORT_SIMSND_FINAL, &owner);
    owner = t;
    t2 = portSimSndStart(4, 0, 10000000, 0, PORT_SIMSND_FINAL, &owner);
    owner = t2;
    portSimSndTick(100010);
    CHECK(owner == t2);
    CHECK(portSimSndState(t) == PORT_SIMSND_STOPPED);
    CHECK(portSimSndState(t2) == PORT_SIMSND_PLAYING);

    /* allocation is deterministic (the lowest free slot: the looped sound's, freed) and the generation tells a reused slot
     * from its old sound */
    CHECK(portSimSndSlot(loop) == 0);
    i = portSimSndGen(loop);
    t = portSimSndStart(5, 0, 100000, 0, PORT_SIMSND_FINAL, NULL);
    CHECK(t == loop);
    CHECK(portSimSndGen(t) == i + 1);

    /* stage unload: everything ends, owners cleared */
    portSimSndKillAll();
    CHECK(owner == NULL);
    CHECK(portSimSndLive() == 0);

    /* a full table gives the sim no sound (NULL), the same way every run */
    for (i = 0; i <= PORT_SIMSND_MAX; i++)
    {
        tok[i] = portSimSndStart(9, 0, 0, 0, PORT_SIMSND_FINAL | PORT_SIMSND_LOOPED, NULL);
    }
    CHECK(tok[PORT_SIMSND_MAX - 1] != NULL);
    CHECK(tok[PORT_SIMSND_MAX] == NULL);
    CHECK(portSimSndState(NULL) == PORT_SIMSND_STOPPED);
    portSimSndKillAll();

    if (fails == 0)
    {
        printf("simsnd_test: all passed\n");
    }
    return fails != 0;
}
