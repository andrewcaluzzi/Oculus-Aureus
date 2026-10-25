/* port_noise.c - the heist's hearing layer (designs/bank-heist.md section 18; owned by the ai stream).
 * A noise is a point, a radius (how far it carries in the open, at hearing scale 1) and a kind. A guard hears it when his straight-line
 * distance is inside radius x his hearing scale x the room factor between the noise's room and his: the same room 1, each open portal on
 * the way x `portal`, each closed door x `door` (the best path, up to 4 portals deep), and never less than `wall` (a loud noise carries
 * through walls). Guards hear whatever way they face. Who heard what is decided here from sim state only (positions, the BG portal list,
 * the door records' open positions), so both machines agree; the Lua `noise` event and the awareness (port_ai.c) follow.
 * Sources: noise.emit (Lua: the gadgets), and port_ai.c (slaps, knock-outs and deaths, Bond's steps and runs, gunshots, doors).
 * PC only, mode only. */
#ifndef TARGET_N64

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <ultra64.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include "game/chr.h"
#include "game/chraction.h"
#include "game/stan.h"
#include "game/bg.h"
#include "game/loadobjectmodel.h"
#include "game/player.h"
#include "port_mode.h"
#include "port_event.h"
#include "port_noise.h"

extern s32 g_GlobalTimer;

static const char *const sKindName[NOISE_KIND_N] = { "slap", "step", "run", "door", "shot", "thud", "glass", "coin", "alarm" };

/* tunables (noise.tune): the factors and the radius each built-in source uses (units, at hearing scale 1) */
static struct { f32 portal, door, wall, radius[NOISE_KIND_N], doorForced; } sNt;
static int sNtSet;
static s32 sNoiseN, sHeardN;   /* counters (hashed) */
static struct { f32 x, y, z, radius; s32 kind, src, heard, at; } sLast;

/* the door in each portal (setup records; cached per stage, re-read when the setup list changes) */
#define NOISE_DOORS 256
static DoorRecord *sDoorRec[NOISE_DOORS];
static s16 sDoorPortal[NOISE_DOORS];
static int sDoorN = -1;
static const void *sDoorSetup;
static f32 sAtt[256];
static u8 sDoorPrev[NOISE_DOORS];   /* each door's state at the last scan (portNoiseDoorsTick) */

static void tuneDefaults(void)
{
    static const f32 r[NOISE_KIND_N] = {
        700.0f,     /* slap: a hand across a face, a punch */
        60.0f,      /* step: walking (Left Shift / half stick): only right behind him */
        330.0f,     /* run: full stick */
        420.0f,     /* door: a door opening */
        0.0f,       /* shot: the gun's own noise x 100 (gunTickNoise), as the stock hearing */
        650.0f,     /* thud: a body hitting the floor (knocked out or dead) */
        900.0f,     /* glass: glass or a prop breaking */
        450.0f,     /* coin: a thrown distraction */
        3500.0f,    /* alarm: the siren */
    };
    memcpy(sNt.radius, r, sizeof(r));
    sNt.portal = 0.6f;
    sNt.door = 0.25f;
    sNt.wall = 0.1f;
    sNt.doorForced = 1100.0f;
    sNtSet = 1;
}

void portNoiseStageReset(void)
{
    tuneDefaults();
    sNoiseN = sHeardN = 0;
    memset(&sLast, 0, sizeof(sLast));
    sLast.kind = -1;
    sDoorN = -1;
    sDoorSetup = NULL;
    memset(sDoorPrev, 0, sizeof(sDoorPrev));
}

const char *portNoiseKindName(int kind) { return (kind >= 0 && kind < NOISE_KIND_N) ? sKindName[kind] : "?"; }

int portNoiseKindFind(const char *name)
{
    int k;

    if (name == NULL) return -1;
    for (k = 0; k < NOISE_KIND_N; k++)
    {
        if (strcmp(name, sKindName[k]) == 0) return k;
    }
    return -1;
}

float portNoiseRadius(int kind)
{
    if (!sNtSet) tuneDefaults();
    return (kind >= 0 && kind < NOISE_KIND_N) ? sNt.radius[kind] : 0.0f;
}

float portNoiseForcedDoorRadius(void) { if (!sNtSet) tuneDefaults(); return sNt.doorForced; }

/* noise.tune: names as the kinds plus portal, door_factor, wall, door_forced; < 0 keeps */
int portNoiseTune(const char *name, float v)
{
    int k;

    if (!sNtSet) tuneDefaults();
    if (v < 0.0f || name == NULL) return 0;
    if (strcmp(name, "portal") == 0) { sNt.portal = v > 1.0f ? 1.0f : v; return 1; }
    if (strcmp(name, "door_factor") == 0) { sNt.door = v > 1.0f ? 1.0f : v; return 1; }
    if (strcmp(name, "wall") == 0) { sNt.wall = v > 1.0f ? 1.0f : v; return 1; }
    if (strcmp(name, "door_forced") == 0) { sNt.doorForced = v; return 1; }
    k = portNoiseKindFind(name);
    if (k < 0) return 0;
    sNt.radius[k] = v;
    return 1;
}

void portNoiseFactors(float *portal, float *door, float *wall)
{
    if (!sNtSet) tuneDefaults();
    *portal = sNt.portal; *door = sNt.door; *wall = sNt.wall;
}

static void doorScan(void)
{
    PropDefHeaderRecord *ph;

    if (sDoorN >= 0 && sDoorSetup == g_CurrentSetup.propDefs) return;
    sDoorN = 0;
    sDoorSetup = g_CurrentSetup.propDefs;
    if (g_CurrentSetup.propDefs == NULL) return;
    for (ph = g_CurrentSetup.propDefs; ph->type != PROPDEF_END && sDoorN < NOISE_DOORS; ph = (PropDefHeaderRecord *) (((u32 *) ph) + sizepropdef(ph)))
    {
        if (ph->type == PROPDEF_DOOR)
        {
            DoorRecord *d = (DoorRecord *) ph;

            if (d->portalNumber >= 0 && d->portalNumber < PORTMAX)
            {
                sDoorRec[sDoorN] = d;
                sDoorPortal[sDoorN] = (s16) d->portalNumber;
                sDoorN++;
            }
        }
    }
}

/* how much a sound loses through portal k: open (or an opening) `portal`, a door mostly shut `door`. A door that is gone (shot off,
 * blown) counts as open. */
static f32 portalFactor(int k)
{
    int i;

    for (i = 0; i < sDoorN; i++)
    {
        if (sDoorPortal[i] == k)
        {
            DoorRecord *d = sDoorRec[i];

            if (((ObjectRecord *) d)->prop != NULL && d->openPosition < 0.3f) return sNt.door;
            return sNt.portal;
        }
    }
    return sNt.portal;
}

/* sAtt[room] = the best factor from `room` to every room (4 portals deep), floored by the wall factor */
static void roomFactors(int room)
{
    int it, k, r;

    for (r = 0; r < 256; r++) sAtt[r] = 0.0f;
    if (room < 0 || room > 255) return;
    sAtt[room] = 1.0f;
    doorScan();
    if (g_BgPortals != NULL)
    {
        for (it = 0; it < 4; it++)
        {
            int changed = 0;

            for (k = 0; k < PORTMAX && g_BgPortals[k].offset_portal != NULL; k++)
            {
                int a = g_BgPortals[k].connectedRoom1, b = g_BgPortals[k].connectedRoom2;
                f32 f;

                if (sAtt[a] <= 0.0f && sAtt[b] <= 0.0f) continue;
                f = portalFactor(k);
                if (sAtt[a] * f > sAtt[b] + 1e-6f) { sAtt[b] = sAtt[a] * f; changed = 1; }
                if (sAtt[b] * f > sAtt[a] + 1e-6f) { sAtt[a] = sAtt[b] * f; changed = 1; }
            }
            if (!changed) break;
        }
    }
    for (r = 0; r < 256; r++)
    {
        if (sAtt[r] < sNt.wall) sAtt[r] = sNt.wall;
    }
}

/* the factor between two rooms (tests, ai.hear_factor) */
float portNoiseRoomFactor(int from, int to)
{
    if (!sNtSet) tuneDefaults();
    roomFactors(from);
    return (to >= 0 && to < 256) ? sAtt[to] : 0.0f;
}

static int roomAt(f32 x, f32 y, f32 z)
{
    coord3d p;
    f32 h;
    StandTile *t;

    p.x = x; p.y = y + 50.0f; p.z = z;
    t = stanFindTileBelowPos(&p, NULL, &h);
    return t != NULL ? (int) (t->room & 0xFF) : -1;
}

/* the Lua side reads the event's arguments here (port_luavm_ai.c) */
typedef struct { f32 x, y, z, radius; s32 kind, src; } NoiseArgs;
static NoiseArgs sArgs[4];
static int sArgsN;

int portNoiseEventArgs(float *x, float *y, float *z, float *radius, int *kind, int *src)
{
    const NoiseArgs *a;

    if (sArgsN <= 0) return 0;
    a = &sArgs[sArgsN - 1];
    *x = a->x; *y = a->y; *z = a->z; *radius = a->radius; *kind = a->kind; *src = a->src;
    return 1;
}

static int sRoomHint = -1;   /* the caller knows the room (a chr's or Bond's tile): no tile search */

void portNoiseEmitRoom(float x, float y, float z, float radius, int kind, int srcChr, int room)
{
    sRoomHint = room;
    portNoiseEmit(x, y, z, radius, kind, srcChr);
    sRoomHint = -1;
}

void portNoiseEmit(float x, float y, float z, float radius, int kind, int srcChr)
{
    int room, i, n, heard = 0;

    if (!portModeActive() || kind < 0 || kind >= NOISE_KIND_N || radius <= 0.0f) return;
    if (!sNtSet) tuneDefaults();
    room = sRoomHint >= 0 ? sRoomHint : roomAt(x, y, z);
    sRoomHint = -1;
    sNoiseN++;
    roomFactors(room);
    n = portAiGuardCount();
    for (i = 0; i < n; i++)
    {
        ChrRecord *c;
        int cn = portAiGuardChr(i), gr;
        f32 dx, dy, dz, d, eff;

        if (cn < 0 || cn == srcChr) continue;
        c = chrFindByLiteralId(cn);
        if (c == NULL || c->prop == NULL || c->model == NULL || c->prop->stan == NULL || chrIsDead(c)) continue;
        gr = c->prop->stan->room & 0xFF;
        eff = radius * (c->hearingscale > 0.0f ? c->hearingscale : 1.0f) * (room >= 0 ? sAtt[gr] : sNt.wall);
        dx = c->prop->pos.x - x;
        dy = (c->prop->pos.y - y) * 1.5f;   /* a floor between them muffles it more than the same distance across a room */
        dz = c->prop->pos.z - z;
        d = sqrtf(dx * dx + dy * dy + dz * dz);
        if (d >= eff) continue;
        heard++;
        portAiHear(i, kind, 1.0f - d / eff, x, y, z, room, srcChr);
    }
    sHeardN += heard;
    sLast.x = x; sLast.y = y; sLast.z = z; sLast.radius = radius; sLast.kind = kind; sLast.src = srcChr; sLast.heard = heard;
    sLast.at = g_GlobalTimer;
    if (portEventListenerCount(PORT_EV_NOISE) > 0 && sArgsN < 4)
    {
        NoiseArgs *a = &sArgs[sArgsN++];

        a->x = x; a->y = y; a->z = z; a->radius = radius; a->kind = kind; a->src = srcChr;
        portEventEmit(PORT_EV_NOISE, kind, srcChr, heard, room);
        sArgsN--;
    }
}

/* port_ai.c, every scan: a door a Bond starts opening (one within 250 units of it) is a noise there. Guards open doors all the time on
 * their routes: those are not news to the others. */
void portNoiseDoorsTick(void)
{
    extern s32 portBondCount(void);
    extern s32 portBondInPlay(s32 p);
    int i, p, n = (int) portBondCount();

    doorScan();
    for (i = 0; i < sDoorN; i++)
    {
        DoorRecord *d = sDoorRec[i];
        PropRecord *dp = ((ObjectRecord *) d)->prop;
        u8 st = (u8) d->openstate;

        if (st == DOORSTATE_OPENING && sDoorPrev[i] != DOORSTATE_OPENING && dp != NULL)
        {
            for (p = 0; p < n && p < 4; p++)
            {
                struct player *pl = g_playerPointers[p];
                f32 dx, dz;

                if (!portBondInPlay(p) || pl == NULL || pl->prop == NULL) continue;
                dx = pl->prop->pos.x - dp->pos.x;
                dz = pl->prop->pos.z - dp->pos.z;
                if (dx * dx + dz * dz < 250.0f * 250.0f)
                {
                    portNoiseEmit(dp->pos.x, dp->pos.y, dp->pos.z, sNt.radius[NOISE_DOOR], NOISE_DOOR, -2 - p);
                    break;
                }
            }
        }
        sDoorPrev[i] = st;
    }
}

/* noise.last(): the last noise and how many guards heard it (tests) */
int portNoiseLast(float *x, float *z, float *radius, int *kind, int *heard, int *count)
{
    *x = sLast.x; *z = sLast.z; *radius = sLast.radius; *kind = sLast.kind; *heard = sLast.heard; *count = sNoiseN;
    return sLast.kind >= 0;
}

unsigned portNoiseHash(void)
{
    unsigned h = (unsigned) sNoiseN * 3u + (unsigned) sHeardN * 7u;
    int k;

    h = h * 16777619u + (unsigned) (s32) (sNt.portal * 1000.0f) + (unsigned) (s32) (sNt.door * 1000.0f) * 3u + (unsigned) (s32) (sNt.wall * 1000.0f) * 5u;
    for (k = 0; k < NOISE_KIND_N; k++) h = h * 16777619u + (unsigned) (s32) sNt.radius[k];
    for (k = 0; k < NOISE_DOORS; k++) h = h * 31u + sDoorPrev[k];
    h = h * 16777619u + (unsigned) (s32) sLast.x + (unsigned) (s32) sLast.z * 3u + (unsigned) sLast.kind * 5u + (unsigned) sLast.heard * 7u;
    return h;
}

#endif /* TARGET_N64 */
