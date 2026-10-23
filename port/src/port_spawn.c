/* port_spawn.c - runtime chr spawning (see port_spawn.h).
 *
 * Everything goes through chrSpawnAtCoord, never chrAllocate (which takes the first free
 * slot with no NULL check). The slot gate here is one stricter than vanilla's, so a level's
 * own clone/spawn scripts keep their margin. Failures log once per stage per reason, since
 * callers retry every second. */
#ifndef TARGET_N64

#include <math.h>
#include <ultra64.h>
#include <bondtypes.h>
#include "game/chr.h"
#include "game/chrai.h"
#include "game/chraction.h"
#include "game/model.h"
#include "game/stan.h"
#include "game/objecthandler.h"   /* chrGiveWeapon */
#include "port_spawn.h"
#include "port_blocks.h"
#include "port_event.h"

extern void portConLogf(const char *fmt, ...);   /* port_console.c */
extern s32 chrGetNumFree(void);                  /* chr.c, not in chr.h */
/* chraction.c: non-static, only forward-declared there (missing from chraction.h) */
extern PropRecord *chrSpawnAtCoord(s32 bodynum, s32 headnum, coord3d *pos, StandTile *stan,
                                   f32 angle, AIListRecord *ailist, s32 spawnflags);
extern s32 chrIsPosOffScreen(coord3d *pos, StandTile *tile);

#define SPAWN_ALLOW_ONSCREEN 0x10   /* SPAWN_IGNORE_PAD_SIGHT_CHECK */
s32 gPortSpawnExtraFlags;           /* extra chrSpawnAtCoord flags for the next spawn only (the arena mode sets SPAWN_SUNGLASSES = 1 around its call) */
#define SPAWN_STEP           60.0f  /* chrAdjustPosForSpawn's ring radius */
#define SPAWN_REAR_STEPS     5      /* behind, +-45, +-90: never in front of the anchor */

enum { LOGGED_NOSLOT = 1, LOGGED_NOLIST = 2, LOGGED_NOSPOT = 4, LOGGED_NOPROP = 8, LOGGED_CHRNUM = 16 };
static u32 sLogged;    /* failure reasons already logged this stage */
static s32 sSpawned;   /* chrs spawned this stage (all freed by the stage reset) */

void portSpawnReset(void)
{
    sLogged = 0;
    sSpawned = 0;
}

static s32 spawnLogOnce(u32 reason)
{
    if (sLogged & reason)
    {
        return FALSE;
    }
    sLogged |= reason;
    return TRUE;
}

/* A clear spot one step off the anchor prop, trying up to nsteps (8 = all) of the 8
 * directions from `angle`, nearest first (5 = never the front half-plane when `angle` is behind).
 * chrAdjustPosForSpawn would accept the anchor's own spot when it is the current
 * player: its CDTYPE_PLAYERS volume test doesn't see Bond (verified: the bot spawned
 * at Bond's exact x/z), so start from the ring instead. Same tests as its ring loop. */
static s32 spawnStepOff(PropRecord *anchor, f32 angle, s32 nsteps, s32 offscreen, coord3d *out, StandTile **outstan)
{
    /* offsets from `angle` in 45 degree steps, nearest first: 0, +1, -1, +2, -2, +3, -3, 4 */
    static const s32 order[8] = { 0, 1, 7, 2, 6, 3, 5, 4 };
    const f32 base = angle;
    s32 i;

    for (i = 0; i < nsteps; i++)
    {
        StandTile *s = anchor->stan;
        coord3d test;

        angle = base + (f32)order[i] * (M_TAU_F / 8.0f);
        if (angle >= M_TAU_F)
        {
            angle -= M_TAU_F;
        }

        test.x = anchor->pos.x + sinf(angle) * SPAWN_STEP;
        test.y = anchor->pos.y;
        test.z = anchor->pos.z + cosf(angle) * SPAWN_STEP;

        if (stanTestLineUnobstructed(&s, anchor->pos.x, anchor->pos.z, test.x, test.z,
                                     CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_PATHBLOCKER, 0.0f, 1.0f, 0.0f, 1.0f) &&
            stanTestVolume(&s, test.x, test.z, 20.0f,
                           CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_PLAYERS | CDTYPE_CHRS | CDTYPE_PATHBLOCKER,
                           0.0f, 1.0f) < 0 &&
            (!offscreen || chrIsPosOffScreen(&test, s)))
        {
            *out = test;
            *outstan = s;
            return TRUE;
        }
    }
    return FALSE;
}

/* A clear spot `dist` from the anchor along `angle` (arena ring), same tests as the step-off. */
s32 portSpawnRingPoint(PropRecord *anchor, f32 angle, f32 dist, coord3d *out, StandTile **outstan)
{
    StandTile *s = anchor->stan;
    coord3d test;

    test.x = anchor->pos.x + sinf(angle) * dist;
    test.y = anchor->pos.y;
    test.z = anchor->pos.z + cosf(angle) * dist;
    if (stanTestLineUnobstructed(&s, anchor->pos.x, anchor->pos.z, test.x, test.z,
                                 CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_PATHBLOCKER, 0.0f, 1.0f, 0.0f, 1.0f) &&
        stanTestVolume(&s, test.x, test.z, 20.0f,
                       CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_PLAYERS | CDTYPE_CHRS | CDTYPE_PATHBLOCKER,
                       0.0f, 1.0f) < 0)
    {
        *out = test;
        *outstan = s;
        return TRUE;
    }
    return FALSE;
}

static ChrRecord *spawnChrImpl(const PortSpawnDesc *d, const coord3d *atpos, StandTile *atstan)
{
    AIRecord *ailist;
    PropRecord *prop;
    ChrRecord *chr;
    coord3d pos;
    StandTile *stan;
    s32 nfree;

    if (d == NULL || d->nearprop == NULL || d->nearprop->stan == NULL)
    {
        if (spawnLogOnce(LOGGED_NOPROP))
        {
            portConLogf("ge: bot: spawn failed: no anchor prop/stan\n");
        }
        return NULL;
    }
    if (d->chrnum >= 0 && chrFindByLiteralId(d->chrnum) != NULL)
    {
        if (spawnLogOnce(LOGGED_CHRNUM))
        {
            portConLogf("ge: bot: spawn refused: chrnum %d is still held\n", (int)d->chrnum);
        }
        return NULL;   /* e.g. dismiss then summon in one tick: the caller retries */
    }
    nfree = chrGetNumFree();
    if (nfree < PORT_SPAWN_MIN_FREE)
    {
        if (spawnLogOnce(LOGGED_NOSLOT))
        {
            portConLogf("ge: bot: no free chr slot (%d free)\n", (int)nfree);
        }
        return NULL;
    }
    ailist = ailistFindById(d->ailistid);
    if (ailist == NULL)
    {
        if (spawnLogOnce(LOGGED_NOLIST))
        {
            portConLogf("ge: bot: spawn failed: no AI list %d\n", (int)d->ailistid);
        }
        return NULL;
    }

    pos = d->nearprop->pos;
    stan = d->nearprop->stan;
    if (atpos != NULL)
    {
        pos = *atpos;
        stan = atstan;
    }
    else if (!spawnStepOff(d->nearprop, d->angle, d->nofront ? SPAWN_REAR_STEPS : 8, FALSE, &pos, &stan) &&
             d->nofront)
    {
        if (spawnLogOnce(LOGGED_NOSPOT))
        {
            portConLogf("ge: bot: spawn deferred: no clear spot behind Bond (%.0f %.0f %.0f)\n",
                        d->nearprop->pos.x, d->nearprop->pos.y, d->nearprop->pos.z);
        }
        return NULL;
    }
    /* else chrAdjustPosForSpawn's search */

    /* the AIListRecord* parameter really carries the list itself (chrAllocate casts it) */
    prop = chrSpawnAtCoord(d->body, d->head, &pos, stan, d->angle,
                           (AIListRecord *)ailist, SPAWN_ALLOW_ONSCREEN | gPortSpawnExtraFlags);
    if (prop == NULL || prop->chr == NULL)
    {
        if (spawnLogOnce(LOGGED_NOSPOT))
        {
            portConLogf("ge: bot: spawn failed: body %d, no clear spot near (%.0f %.0f %.0f) or no model\n",
                        (int)d->body, d->nearprop->pos.x, d->nearprop->pos.y, d->nearprop->pos.z);
        }
        return NULL;
    }

    chr = prop->chr;
    if (d->maxdamage > 0.0f)
    {
        chrSetMaxDamage(chr, d->maxdamage);
    }
    chr->chrflags |= d->chrflags_on;
    if (d->chrnum >= 0)
    {
        chr->chrnum = (s16)d->chrnum;
    }
    sSpawned++;
    portEventEmit(PORT_EV_CHR_SPAWNED, chr->chrnum, 0, 0, 0);
    return chr;
}

ChrRecord *portSpawnChr(const PortSpawnDesc *d)
{
    return spawnChrImpl(d, NULL, NULL);
}

ChrRecord *portSpawnChrAt(const PortSpawnDesc *d, const coord3d *pos, StandTile *stan)
{
    return spawnChrImpl(d, pos, stan);
}

/* Spawn on a pad (position, stan and facing from the pad). d->nearprop is only the anchor gate
 * (use Bond's prop); d->angle is replaced by the pad's facing. weaponprop < 0 = unarmed. */
ChrRecord *portSpawnChrAtPad(s32 pad, const PortSpawnDesc *d, s32 weaponprop, s32 weaponitem)
{
    PortSpawnDesc dd;
    coord3d pos;
    StandTile *stan = NULL;
    f32 angle = 0.0f;
    ChrRecord *chr;

    if (d == NULL || !portPadStan(pad, &pos, &stan, &angle))
    {
        portConLogf("ge: mode: spawn at pad %d refused: invalid pad or no floor\n", (int)pad);
        return NULL;
    }
    dd = *d;
    dd.angle = angle;
    chr = spawnChrImpl(&dd, &pos, stan);
    if (chr != NULL && weaponprop >= 0)
    {
        chrGiveWeapon(chr, weaponprop, (ITEM_IDS)weaponitem, 0);
    }
    if (chr != NULL)
    {
        portConLogf("ge: mode: spawned chr %d at pad %d (%.0f %.0f %.0f)\n", (int)chr->chrnum, (int)pad, chr->prop->pos.x,
                    chr->prop->pos.y, chr->prop->pos.z);
    }
    return chr;
}

static s32 spawnTeleport(ChrRecord *chr, PropRecord *nearprop, f32 stepangle, s32 nsteps);

/* Move a live chr next to nearprop, off screen when possible; same steps as the
 * AI_TRYTeleportingChrToPad command (chrai.c). */
s32 portSpawnTeleportNear(ChrRecord *chr, PropRecord *nearprop)
{
    if (chr == NULL || chr->model == NULL)
    {
        return FALSE;
    }
    return spawnTeleport(chr, nearprop, getsubroty(chr->model), 8);
}

/* stepangle points away from the anchor's facing: never lands in front of it. */
s32 portSpawnTeleportNearAt(ChrRecord *chr, PropRecord *nearprop, f32 stepangle)
{
    return spawnTeleport(chr, nearprop, stepangle, SPAWN_REAR_STEPS);
}

static s32 spawnTeleport(ChrRecord *chr, PropRecord *nearprop, f32 stepangle, s32 nsteps)
{
    coord3d pos;
    StandTile *stan;
    f32 facing;
    s32 ok;

    if (chr == NULL || chr->prop == NULL || chr->model == NULL ||
        nearprop == NULL || nearprop->stan == NULL)
    {
        return FALSE;
    }

    facing = getsubroty(chr->model);
    sub_GAME_7F03D058(chr->prop, FALSE);   /* our own collision must not block the test */

    ok = spawnStepOff(nearprop, stepangle, nsteps, TRUE, &pos, &stan) ||
         spawnStepOff(nearprop, stepangle, nsteps, FALSE, &pos, &stan);
    if (ok)
    {
        /* nearprop's y is no floor height (Bond's is eye level): keep the chr's own height above the new floor */
        f32 floor = stanGetPositionYValue(stan, pos.x, pos.z);

        pos.y = floor + (chr->prop->pos.y - chr->ground);
        chr->ground = floor;
        chr->prop->pos = pos;
        chr->prop->stan = stan;
        chr->chrflags |= CHRFLAG_INIT;
        setsubroty(chr->model, facing);
        setsuboffset(chr->model, &pos);
        chrDetectRooms(chr);
    }

    sub_GAME_7F03D058(chr->prop, TRUE);
    return ok ? TRUE : FALSE;
}

#endif /* TARGET_N64 */
