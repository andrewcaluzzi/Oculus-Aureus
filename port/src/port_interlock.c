/* port_interlock.c - door interlocks: mantraps and sally ports (designs/bank-heist.md section 29; Lua interlock.*, port_luavm_interlock.c).
 *
 * A group is a list of up to 4 doors of which only one may stand open at a time (the research's section 3.1: "only one door of a pair
 * may be open at a time"). Every open order goes through doorActivate (propobj.c), and doorActivate asks portInterlockGate first. So the
 * rule binds every opener the same way: a Bond's use press (propdoorInteract), a guard's door finder (chraction.c), a guard's AI list
 * (chrai.c), a script's door.open. A refused order is queued. The group opens that door itself once it is free: every door shut, the
 * verification hold over, no lockdown (one request waits at a time: the first). So a staffer who badges the inner door while the outer one is still closing walks on when it is
 * his turn, the way a real mantrap's reader turns from red to green. Each door closes itself `close` ticks after it opened (the doors-
 * closing timer). After the last door shuts the group holds every door for `hold` ticks: the verification window the script fills (a
 * camera, a staffer in the security room). Then the queued door opens.
 *
 * Guards: an interlocked door carries the stock "AI cannot operate" bit (flags2 0x20000000) while it is in a group, as a ruled door does
 * (port_heist.c doorRuleLock). Without it a guard who was told to wait would walk straight through the shut door (an on-screen chr's
 * move test ignores ordinary doors). portHeistChrMayOpenDoor asks portInterlockChrGate: 0 = wait (he walks up to the shut door and
 * stands there; his door finder asks again every 10 ticks and the queue opens it for him), 2 = he may open it in spite of the bit.
 *
 * Power: each group has a failure mode. Fail-safe (0, the staff mantrap): with the power out the interlock drops and every door opens
 * by hand. The mode also clears the badge readers' keys. This is the life-safety egress rule for an exit route: the maglocks release.
 * Fail-secure (1, the sally port): with the power out no door of the group moves at all. The roller shutters have no motor power, and
 * the auto close stops too.
 *
 * Bypasses: interlock.override(door, secs) lets one door ignore its group (a picked mechanical override cylinder).
 * interlock.lockdown(g, secs) holds every door shut (the verify step caught someone: the occupant is held for security).
 *
 * Sim state: all of it is in portInterlockHash (the co-op / netplay state hash) and in this file's statics (the rollback snapshot:
 * scripts/port/snap_embed.py SIM_PORT). Ticks come from g_ClockTimer. Mode only: with no group defined every door is stock. */
#ifndef TARGET_N64

#include <stdio.h>
#include <string.h>
#include <ultra64.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include "port_mode.h"

#define IL_GROUPS 8
#define IL_DOORS  4

extern void doorActivate(DoorRecord *door, DOORSTATE state);
extern s32 g_ClockTimer;
extern int portHeistSetupIndex(const void *rec);
extern void *portHeistDoorRecord(int idx);
extern int portHeistPowerOn(void);

typedef struct
{
    s32 used;
    s32 n;
    s32 door[IL_DOORS];        /* setup indices */
    u8  hadAiLock[IL_DOORS];   /* the layout's own "AI cannot operate" bit, restored on clear */
    s32 holdTicks, closeTicks;
    s32 fail;                  /* 0 fail-safe, 1 fail-secure */
    s32 lockUntil;             /* lockdown: nothing opens before this tick */
    s32 holdUntil;             /* the verification hold: nothing opens before this tick */
    s32 holdFrom;              /* the tick the current hold started (-1 none) */
    s32 busy;                  /* some door was not shut at the last tick */
    s32 pending, pendingUntil; /* the queued door's slot (-1 none) and when the request lapses */
    s32 pendingBy;             /* who queued it (as cycleBy) */
    s32 grace;                 /* doors a fail-safe outage left open together: not a violation until one shuts */
    s32 openSince[IL_DOORS];   /* the tick each door was last seen start to open (-1 shut) */
    s32 override[IL_DOORS];    /* that door ignores the group until this tick (0 none) */
    s32 lastOpen;              /* the slot that opened last (-1 none) */
    s32 cycleBy;               /* who opened the current cycle: 1000 + chrnum for a chr, -2 someone else (a Bond, a script), -1 none */
    s32 cycleAt;
    s32 cycles, refused, violations, queuedOpens;
} IlGroup;

static IlGroup sIl[IL_GROUPS];
static s32 sIlNow;
static s32 sIlReqChr = -1, sIlReqDoor = -1, sIlReqAt = -1;   /* the chr whose door finder asked last (attributes the open order) */
static s32 sIlInQueue;                                        /* the queue's own open order (passes the gate it was queued at) */
static s32 sIlQueueBy;                                        /* ... and who queued it */

void portInterlockReset(void)
{
    memset(sIl, 0, sizeof(sIl));
    sIlNow = 0;
    sIlReqChr = sIlReqDoor = sIlReqAt = -1;
    sIlInQueue = sIlQueueBy = 0;
}

static DoorRecord *ilDoor(int idx) { return (DoorRecord *) portHeistDoorRecord(idx); }

/* a door and its linked leaves are shut (a double door is one door here) */
static int ilShut(DoorRecord *d)
{
    DoorRecord *e = d;
    int n = 0;

    if (d == NULL) return 1;
    do
    {
        if (!((e->openstate == DOORSTATE_STATIONARY || e->openstate == DOORSTATE_WAITING) && e->openPosition <= 0.0f)) return 0;
        e = e->linkedDoor;
    } while (e != NULL && e != d && ++n < 8);
    return 1;
}

/* the group and slot of a door record (or of one of its linked leaves); -1 none */
static int ilFind(const DoorRecord *rec, int *slot)
{
    int g, s;

    for (g = 0; g < IL_GROUPS; g++)
    {
        if (!sIl[g].used) continue;
        for (s = 0; s < sIl[g].n; s++)
        {
            DoorRecord *d = ilDoor(sIl[g].door[s]), *e = d;
            int k = 0;

            while (e != NULL && k++ < 8)
            {
                if (e == rec) { if (slot) *slot = s; return g; }
                e = e->linkedDoor;
                if (e == d) break;
            }
        }
    }
    return -1;
}

static void ilAiLock(IlGroup *G, int s, int on)
{
    DoorRecord *d = ilDoor(G->door[s]), *e = d;
    int k = 0;

    while (e != NULL && k++ < 8)
    {
        if (on) e->flags2 |= PROPFLAG2_20000000;
        else if (!G->hadAiLock[s]) e->flags2 &= ~PROPFLAG2_20000000;
        e = e->linkedDoor;
        if (e == d) break;
    }
}

/* why door slot `s` of group G may not open now: 0 it may, 1 another door is open, 2 the hold, 3 lockdown, 4 no power (fail-secure) */
static int ilWhy(const IlGroup *G, int s)
{
    int k;

    if (G->override[s] > sIlNow) return 0;
    if (!portHeistPowerOn()) return G->fail ? 4 : 0;
    if (G->lockUntil > sIlNow) return 3;
    for (k = 0; k < G->n; k++)
    {
        if (k != s && !ilShut(ilDoor(G->door[k]))) return 1;
    }
    if (G->holdUntil > sIlNow) return 2;
    return 0;
}

/* propobj.c doorActivate: 1 = let the order through, 0 = drop it (an open order the group refuses; a fail-secure group without power
 * drops a close order too: its shutters have no motor) */
int portInterlockGate(DoorRecord *door, s32 state)
{
    IlGroup *G;
    int g, s = 0, why;

    if (door == NULL || !portModeActive()) return 1;
    g = ilFind(door, &s);
    if (g < 0) return 1;
    G = &sIl[g];
    if (state == DOORSTATE_CLOSING) return !(G->fail && !portHeistPowerOn() && G->override[s] <= sIlNow);
    if (state != DOORSTATE_OPENING) return 1;
    if (!ilShut(ilDoor(G->door[s]))) return 1;     /* it is moving or open already: a reversal of its own move, not a new cycle */
    why = ilWhy(G, s);
    if (why != 0)
    {
        G->refused++;
        if (why <= 2 && !sIlInQueue && (G->pending < 0 || G->pending == s))
        {   /* queue it: the group opens it when it is free (first come: a later request for another door waits for the next turn) */
            if (G->pending < 0) G->pendingBy = -2;
            G->pending = s;
            G->pendingUntil = sIlNow + G->holdTicks + G->closeTicks + 6 * 60;
        }
        return 0;
    }
    G->lastOpen = s;
    if (sIlInQueue) G->cycleBy = sIlQueueBy;
    else G->cycleBy = (sIlReqAt == sIlNow && sIlReqDoor == G->door[s]) ? 1000 + sIlReqChr : -2;
    G->cycleAt = sIlNow;
    G->cycles++;
    if (G->pending == s) G->pending = -1;
    return 1;
}

/* port_heist.c portHeistChrMayOpenDoor (after the role rule let him): -1 the door is in no group, 0 he waits (the request is queued),
 * 2 he may open it (its AI-lock bit is the group's, not the layout's) */
int portInterlockChrGate(void *chrp, void *doorp)
{
    ChrRecord *self = (ChrRecord *) chrp;
    DoorRecord *door = (DoorRecord *) doorp;
    IlGroup *G;
    int g, s = 0, why;

    if (door == NULL || self == NULL || !portModeActive()) return -1;
    g = ilFind(door, &s);
    if (g < 0) return -1;
    G = &sIl[g];
    if (!ilShut(ilDoor(G->door[s]))) return 2;
    why = ilWhy(G, s);
    if (why != 0)
    {
        if (why <= 2 && (G->pending < 0 || G->pending == s))
        {
            if (G->pending < 0) { G->refused++; G->pendingBy = 1000 + self->chrnum; }
            G->pending = s;
            G->pendingUntil = sIlNow + G->holdTicks + G->closeTicks + 6 * 60;
        }
        return 0;
    }
    sIlReqChr = self->chrnum;
    sIlReqDoor = G->door[s];
    sIlReqAt = sIlNow;
    return G->hadAiLock[s] ? -1 : 2;   /* (a door the layout locks to AI stays the rule's business) */
}

/* every sim tick while a mode runs (port_heist.c portHeistAiTick) */
void portInterlockTick(void)
{
    int g, s;

    sIlNow += g_ClockTimer;
    for (g = 0; g < IL_GROUPS; g++)
    {
        IlGroup *G = &sIl[g];
        int open = 0, frozen;

        if (!G->used) continue;
        frozen = G->fail && !portHeistPowerOn();
        for (s = 0; s < G->n; s++)
        {
            DoorRecord *d = ilDoor(G->door[s]);

            if (d == NULL) continue;
            if (ilShut(d)) { G->openSince[s] = -1; continue; }
            open++;
            if (G->openSince[s] < 0) G->openSince[s] = sIlNow;
            /* the doors-closing timer: an open door (not one already closing) closes itself; retried every half second while a body in
             * the doorway stops it */
            if (!frozen && G->closeTicks > 0 && sIlNow - G->openSince[s] >= G->closeTicks && d->openstate != DOORSTATE_CLOSING)
            {
                doorActivate(d, DOORSTATE_CLOSING);
                G->openSince[s] = sIlNow - G->closeTicks + 30;
            }
        }
        if (!portHeistPowerOn() && !G->fail) G->grace = 1;
        if (open <= 1) G->grace = 0;
        if (open >= 2 && portHeistPowerOn() && !G->grace)
        {   /* two doors open at once: only an override (a picked cylinder) or a fail-safe outage may cause it; counted for the tests */
            int ov = 0;

            for (s = 0; s < G->n; s++) if (G->override[s] > sIlNow) ov = 1;
            if (!ov && G->violations < 0x7FFFFFF0) G->violations++;
        }
        if (G->busy && open == 0 && G->holdTicks > 0)
        {   /* the last door shut: the verification hold */
            G->holdUntil = sIlNow + G->holdTicks;
            G->holdFrom = sIlNow;
        }
        if (G->holdUntil <= sIlNow) G->holdFrom = -1;
        G->busy = open > 0;
        if (G->pending >= 0)
        {
            if (sIlNow > G->pendingUntil) G->pending = -1;
            else if (ilWhy(G, G->pending) == 0)
            {
                DoorRecord *d = ilDoor(G->door[G->pending]);

                G->pending = -1;
                if (d != NULL && ((ObjectRecord *) d)->prop != NULL)
                {
                    sIlInQueue = 1;
                    sIlQueueBy = G->pendingBy;
                    doorActivate(d, DOORSTATE_OPENING);
                    sIlInQueue = 0;
                    G->queuedOpens++;
                }
            }
        }
    }
}

/* ---- the script side (port_luavm_interlock.c) ---- */

/* define a group: doors (setup indices), hold and close in ticks, fail 0 safe / 1 secure. The group id, or -1 (full, or a door that is
 * no door or already in a group) */
int portInterlockDefine(const int *doors, int n, int holdTicks, int closeTicks, int fail)
{
    int g, s;

    if (n < 2 || n > IL_DOORS) return -1;
    for (s = 0; s < n; s++)
    {
        DoorRecord *d = ilDoor(doors[s]);

        if (d == NULL || ilFind(d, NULL) >= 0) return -1;
    }
    for (g = 0; g < IL_GROUPS && sIl[g].used; g++) {}
    if (g == IL_GROUPS) return -1;
    memset(&sIl[g], 0, sizeof(sIl[g]));
    sIl[g].used = 1;
    sIl[g].n = n;
    sIl[g].holdTicks = holdTicks > 0 ? holdTicks : 0;
    sIl[g].closeTicks = closeTicks > 0 ? closeTicks : 0;
    sIl[g].fail = fail ? 1 : 0;
    sIl[g].pending = sIl[g].lastOpen = sIl[g].cycleBy = sIl[g].holdFrom = -1;
    for (s = 0; s < n; s++)
    {
        DoorRecord *d = ilDoor(doors[s]);

        sIl[g].door[s] = doors[s];
        sIl[g].hadAiLock[s] = (d->flags2 & PROPFLAG2_20000000) ? 1 : 0;
        sIl[g].openSince[s] = -1;
        ilAiLock(&sIl[g], s, 1);
    }
    return g;
}

int portInterlockClear(int g)
{
    int s;

    if (g < 0 || g >= IL_GROUPS || !sIl[g].used) return 0;
    for (s = 0; s < sIl[g].n; s++) ilAiLock(&sIl[g], s, 0);
    memset(&sIl[g], 0, sizeof(sIl[g]));
    return 1;
}

int portInterlockOf(int idx, int *slot)
{
    DoorRecord *d = ilDoor(idx);

    return d != NULL ? ilFind(d, slot) : -1;
}

int portInterlockLockdown(int g, int ticks)
{
    if (g < 0 || g >= IL_GROUPS || !sIl[g].used) return 0;
    sIl[g].lockUntil = ticks > 0 ? sIlNow + ticks : 0;
    if (ticks > 0) sIl[g].pending = -1;
    return 1;
}

int portInterlockOverride(int idx, int ticks)
{
    int s = 0, g = portInterlockOf(idx, &s);

    if (g < 0) return 0;
    sIl[g].override[s] = ticks > 0 ? sIlNow + ticks : 0;
    return 1;
}

typedef struct
{
    int n, door[IL_DOORS], open[IL_DOORS], fail, phase, why, pending, lastOpen, cycleBy, cycles, refused, violations, queuedOpens;
    float holdLeft, lockLeft, holdLen;
} PortInterlockInfo;

/* phase: 0 ready (every door shut), 1 cycle (a door is open or moving), 2 verifying (the hold), 3 lockdown, 4 no power (fail-secure),
 * 5 released (fail-safe, no power) */
int portInterlockGet(int g, PortInterlockInfo *o)
{
    const IlGroup *G;
    int s, open = 0;

    if (g < 0 || g >= IL_GROUPS || !sIl[g].used || o == NULL) return 0;
    G = &sIl[g];
    memset(o, 0, sizeof(*o));
    o->n = G->n;
    for (s = 0; s < G->n; s++)
    {
        o->door[s] = G->door[s];
        o->open[s] = !ilShut(ilDoor(G->door[s]));
        open += o->open[s];
    }
    o->fail = G->fail;
    if (!portHeistPowerOn()) o->phase = G->fail ? 4 : 5;
    else if (G->lockUntil > sIlNow) o->phase = 3;
    else if (open) o->phase = 1;
    else if (G->holdUntil > sIlNow) o->phase = 2;
    else o->phase = 0;
    o->pending = G->pending >= 0 ? G->door[G->pending] : -1;
    o->lastOpen = G->lastOpen >= 0 ? G->door[G->lastOpen] : -1;
    o->cycleBy = G->cycleBy;
    o->cycles = G->cycles; o->refused = G->refused; o->violations = G->violations; o->queuedOpens = G->queuedOpens;
    o->holdLeft = G->holdUntil > sIlNow ? (G->holdUntil - sIlNow) / 60.0f : 0.0f;
    o->lockLeft = G->lockUntil > sIlNow ? (G->lockUntil - sIlNow) / 60.0f : 0.0f;
    o->holdLen = G->holdTicks / 60.0f;
    return 1;
}

/* the AR specs' word for a door (port_arhud.c drawDoors; presentation, reads only): NULL for a door in no group */
const char *portInterlockLabel(int idx, unsigned *rgba)
{
    int s = 0, g = portInterlockOf(idx, &s), why;
    const IlGroup *G;

    if (g < 0) return NULL;
    G = &sIl[g];
    if (G->override[s] > sIlNow) { *rgba = 0xFFA040FFu; return "OVERRIDE"; }
    if (!portHeistPowerOn()) { *rgba = G->fail ? 0xFF4040FFu : 0xFFC040FFu; return G->fail ? "NO POWER" : "RELEASED"; }
    if (!ilShut(ilDoor(G->door[s]))) { *rgba = 0x60FF90FFu; return "CYCLE"; }
    why = ilWhy(G, s);
    if (why == 3) { *rgba = 0xFF3030FFu; return "LOCKDOWN"; }
    if (why == 2) { *rgba = 0x60E0FFFFu; return G->pending == s ? "VERIFYING > NEXT" : "VERIFYING"; }
    if (why == 1) { *rgba = 0xFFC040FFu; return G->pending == s ? "WAIT (QUEUED)" : "WAIT"; }
    *rgba = 0x80C0FFFFu;
    return "INTERLOCK";
}

unsigned portInterlockHash(void)
{
    unsigned h = (unsigned) sIlNow * 3u + (unsigned) (sIlReqChr + 1) * 5u + (unsigned) (sIlReqAt + 1) * 7u;
    int g, s;

    for (g = 0; g < IL_GROUPS; g++)
    {
        const IlGroup *G = &sIl[g];

        if (!G->used) continue;
        h = h * 16777619u + (unsigned) g + (unsigned) G->n * 3u + (unsigned) G->fail * 5u + (unsigned) G->holdTicks * 7u
            + (unsigned) G->closeTicks * 11u + (unsigned) G->lockUntil * 13u + (unsigned) G->holdUntil * 17u + (unsigned) G->busy * 19u;
        h = h * 16777619u + (unsigned) (G->pending + 1) + (unsigned) G->pendingUntil * 3u + (unsigned) (G->lastOpen + 1) * 5u
            + (unsigned) G->cycleBy * 7u + (unsigned) G->cycleAt * 11u + (unsigned) G->cycles * 13u + (unsigned) G->refused * 17u
            + (unsigned) G->violations * 19u + (unsigned) G->queuedOpens * 23u + (unsigned) G->holdFrom * 29u + (unsigned) G->pendingBy * 31u + (unsigned) G->grace * 37u;
        for (s = 0; s < G->n; s++)
        {
            h = h * 16777619u + (unsigned) G->door[s] + (unsigned) G->openSince[s] * 3u + (unsigned) G->override[s] * 5u
                + (unsigned) G->hadAiLock[s] * 7u;
        }
    }
    return h;
}

#endif /* TARGET_N64 */
