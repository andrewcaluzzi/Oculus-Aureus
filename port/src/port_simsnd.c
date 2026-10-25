/* port_simsnd.c - the sim's own view of the sounds it starts (modding/snapshot-fidelity.md).
 *
 * The game asks the sound player whether a sound it started still plays (chrai.c: a free AI sound slot, a slot's sim-visible
 * volume; chr.c / chraction.c: a guard still talking; gun.c / gunfire.c: a free impact or casing slot; doors, trucks, alarms),
 * and the sound player clears the sim's owner word when the sound ends (snd.c sndUnlinkClearSound). Both run on the audio's
 * clock: audio frames that the pump renders by wall time (GE_CLOCKSCALE, no device: SDL_GetTicks; with a device: the queue
 * level), and the audio engine is outside every snapshot. So a sim that reads them depends on how fast the machine ran, and a
 * restore (rollback, the bot's savestates) hands the sim the abandoned future's voices and handle values.
 *
 * Here the sim gets a TOKEN for each sound it starts (a pointer into the table below, which is sim state: this file is in
 * snap_embed.py's SIM_PORT) instead of the engine's ALSoundState pointer. A token knows when its sound ends in sim time
 * (fields), from the same envelope arithmetic the sound player uses (snd.c portSimSndPlan), and "is it playing" and the
 * owner-word clear are answered from that. The engine keeps playing the real voice: snd.c maps a token to it through a
 * presentation-side table that a restore never touches (a token whose allocation the restore undid simply has no voice).
 *
 * GE_SIMSND=0 restores the engine-driven behaviour (A/B). Pure logic, no engine calls: tests/simsnd_test.c. */
#include "port_simsnd.h"

#include <stdlib.h>

typedef struct {
    int used;
    int gen;            /* bumped per allocation; snd.c's voice map matches it */
    int flags;          /* PORT_SIMSND_* */
    int start;          /* field the tracked sound's voice starts (its PLAY event) */
    int end;            /* field from which it no longer plays (PORT_SIMSND_FOREVER: until stopped) */
    int release;        /* fields of its release (a stop lets it ring this long) */
    int stopping;       /* stopped by the sim: in its release */
    void **owner;       /* the sim's owner word (sndPlaySfx's pendingState->link.next), cleared at the end */
    int sound;
} SimSnd;

static SimSnd sSnd[PORT_SIMSND_MAX];
static int sNow;            /* sim fields (portSimSndTick) */
static int sOn = -1;

int portSimSndOn(void)
{
    if (sOn < 0)
    {
        const char *e = getenv("GE_SIMSND");

        sOn = !(e != NULL && e[0] == '0');
    }
    return sOn;
}

void portSimSndSetOn(int on)
{
    sOn = on ? 1 : 0;
}

int portSimSndIsToken(const void *p)
{
    const char *c = (const char *) p;

    return c >= (const char *) &sSnd[0] && c < (const char *) &sSnd[PORT_SIMSND_MAX]
           && ((size_t) (c - (const char *) &sSnd[0])) % sizeof(SimSnd) == 0;
}

int portSimSndSlot(const void *tok)
{
    return portSimSndIsToken(tok) ? (int) (((const SimSnd *) tok) - sSnd) : -1;
}

int portSimSndGen(const void *tok)
{
    return portSimSndIsToken(tok) ? ((const SimSnd *) tok)->gen : -1;
}

int portSimSndNow(void)
{
    return sNow;
}

/* microseconds of the audio clock -> sim fields (60 a second), rounded up: a sound is still playing in the field it ends in */
int portSimSndFields(long us)
{
    if (us <= 0)
    {
        return 0;
    }
    return (int) ((us * 60L + 999999L) / 1000000L);
}

void *portSimSndStart(int sound, long startUs, long playUs, long releaseUs, int flags, void **owner)
{
    int i;

    for (i = 0; i < PORT_SIMSND_MAX; i++)
    {
        SimSnd *s = &sSnd[i];

        if (!s->used)
        {
            s->used = 1;
            s->gen++;
            s->flags = flags;
            s->sound = sound;
            s->start = sNow + portSimSndFields(startUs);
            s->release = portSimSndFields(releaseUs);
            s->end = (flags & (PORT_SIMSND_LOOPED | PORT_SIMSND_RETRIGGER)) ? PORT_SIMSND_FOREVER
                                                                           : s->start + portSimSndFields(playUs) + s->release;
            s->stopping = 0;
            s->owner = owner;
            return s;
        }
    }
    return NULL;    /* as full a table as the engine's own state pool can be: the sim gets no sound, deterministically */
}

int portSimSndState(const void *tok)
{
    const SimSnd *s;

    if (!portSimSndIsToken(tok))
    {
        return PORT_SIMSND_STOPPED;
    }
    s = (const SimSnd *) tok;
    if (!s->used || sNow >= s->end)
    {
        return PORT_SIMSND_STOPPED;
    }
    if (sNow < s->start)
    {
        return PORT_SIMSND_INIT;
    }
    return s->stopping ? PORT_SIMSND_STOPPING : PORT_SIMSND_PLAYING;
}

/* the sound player's deactivate: a sound not yet started is gone at once, a playing one rings out its release */
void portSimSndStop(const void *tok)
{
    SimSnd *s;

    if (!portSimSndIsToken(tok))
    {
        return;
    }
    s = (SimSnd *) tok;
    if (!s->used || sNow >= s->end)
    {
        return;
    }
    if (sNow < s->start)
    {
        s->end = sNow;
    }
    else if (!s->stopping)
    {
        const int e = sNow + s->release;

        s->stopping = 1;
        if (e < s->end)
        {
            s->end = e;
        }
    }
}

/* sndDeactivateAllSfxByFlag: every sound whose flags hold all of `flag` (1 = every game sound, 3 = the looped ones, 0x11 = the
 * retriggered ones) */
void portSimSndStopByFlag(int flag)
{
    int i;

    for (i = 0; i < PORT_SIMSND_MAX; i++)
    {
        if (sSnd[i].used && (sSnd[i].flags & flag) == flag)
        {
            portSimSndStop(&sSnd[i]);
        }
    }
}

static void endOne(SimSnd *s)
{
    if (s->owner != NULL && *s->owner == (void *) s)
    {
        *s->owner = NULL;   /* what sndUnlinkClearSound does when the sound player disposes it */
    }
    s->used = 0;
    s->owner = NULL;
}

/* once per sim tick, before the sim step: the sim clock, and the sounds that ended give their owner words back */
void portSimSndTick(int fields)
{
    int i;

    sNow = fields;
    for (i = 0; i < PORT_SIMSND_MAX; i++)
    {
        if (sSnd[i].used && sNow >= sSnd[i].end)
        {
            endOne(&sSnd[i]);
        }
    }
}

/* stage unload (snd.c portSndKillAllSfx): every sound is gone, owner words cleared */
void portSimSndKillAll(void)
{
    int i;

    for (i = 0; i < PORT_SIMSND_MAX; i++)
    {
        if (sSnd[i].used)
        {
            endOne(&sSnd[i]);
        }
    }
}

int portSimSndLive(void)
{
    int i, n = 0;

    for (i = 0; i < PORT_SIMSND_MAX; i++)
    {
        n += sSnd[i].used && sNow < sSnd[i].end;
    }
    return n;
}
