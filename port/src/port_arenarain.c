/* port_arenarain.c - the RAIN mutator's sim side (THUNDERBALL; AC: "everyone slips"). Lua: arena.rain(slip [, grip]) / arena.rain(0)
 * (port_luavm_arena.c); the mutator itself is tools/mode_arena_mutators.lua (rain), the drawing port_arenarain_draw.c (presentation).
 *
 *   - it rolls in and out over RAIN_FADE_SECS: an amount 0..1 that scales everything below, driven per sim tick (portArenaRainTick
 *     from portLuaTick), so it is the same on every machine and in a rollback (the state is in this SIM file);
 *   - BOND SLIPS (and the co-op partner: every player): his walk is a velocity that follows what the walk animation and the stick ask
 *     for with a time constant (rain_slip seconds at full rain) instead of at once, so he overshoots, takes longer to stop and swings
 *     wide in turns (bondview2.c, around bondviewCalcUpdatePlayerCollision: portArenaRainSlip before, portArenaRainSlipAfter after,
 *     so a wall stops the slide instead of banking it);
 *   - GRENADES SLIDE FURTHER: the landed-slide friction (propobj.c) is multiplied by the grip (rain_grip at full rain; 1 = dry, which
 *     leaves stock's arithmetic untouched);
 *   - THE SKY goes overcast: the Colosseum's own sky and cloud colours fade to a dark grey (when no fog record is in force).
 *   - LIGHTNING (arena.strike, the end of this file): a strike lands on the floor near a character (an enemy or a Bond) after a
 *     short fuse and is a stock explosion there (it hurts whoever stands in it, Bond too); the flash and the bolt are drawn by
 *     port_arenarain_draw.c from the record kept here.
 * The enemies' half (slower, stumbling) is Lua: anim_speed on the wave's spawns and spawn.anim slips on hard turns / sudden stops,
 * with the mode's deterministic RNG; so are the lightning's timing, its target and its offset. Never outside a Lua mode. SIM
 * (scripts/port/snap_embed.py). */
#ifndef TARGET_N64

#include <math.h>   /* (sqrtf: only for the log line) */
#include <string.h>
#include <ultra64.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include "game/bgfog.h"
#include "game/chr.h"
#include "game/player.h"
#include "game/stan.h"
#include "game/explosion.h"
#include "port_blocks.h"
#include "port_mode.h"

extern void portConLogf(const char *fmt, ...);
extern int g_portCoopEpoch;
extern ChrRecord *chrFindByLiteralId(s32 id);
extern int portHeistOffsetStan(coord3d *pos, StandTile **stan, float dx, float dz);   /* port_heist.c */
static void strikeReset(void);

#define RAIN_FADE_SECS 1.5f
#define RAIN_PLAYERS   4

static f32 sSlip;          /* rain_slip asked for (seconds at full rain; 0 = dry / fading out) */
static f32 sSlipLast;      /* the last one asked for (the fade-out keeps it) */
static f32 sGripAsk = 1.0f;
static f32 sAmt;           /* 0..1 */
static s32 sLive;          /* fading in, on, or fading out */
static s32 sEpoch = -1000;
static s32 sSkyTaken;
static u8 sSkyOwn[3];
static f32 sCloudOwn[3];
static f32 sVel[RAIN_PLAYERS][2];
static s32 sVelOk[RAIN_PLAYERS];

static const u8 kSkyWet[3] = { 0x3A, 0x40, 0x4A };     /* overcast */
static const f32 kCloudWet[3] = { 104.0f, 108.0f, 118.0f };

static void epochCheck(void)
{
    if (sEpoch != g_portCoopEpoch)
    {   /* a new stage: dry, its own sky already loaded */
        sEpoch = g_portCoopEpoch;
        sSlip = sSlipLast = 0.0f;
        sGripAsk = 1.0f;
        sAmt = 0.0f;
        sLive = sSkyTaken = 0;
        memset(sVelOk, 0, sizeof(sVelOk));
        strikeReset();
    }
}

static f32 smooth(void) { return sAmt * sAmt * (3.0f - 2.0f * sAmt); }

static void skyApply(void)
{
    struct CurrentEnvironmentRecord *c = fogGetCurrentEnvironmentp();
    const f32 s = smooth();
    s32 i;
    u8 *rgb;
    f32 *cl;

    if (c == NULL || g_FogSkyIsEnabled || !sSkyTaken)
    {
        return;   /* (a fog record owns the sky: port_arenafx.c) */
    }
    rgb = &c->Red;
    cl = &c->CloudRed;
    for (i = 0; i < 3; i++)
    {
        rgb[i] = (u8) ((f32) sSkyOwn[i] + ((f32) kSkyWet[i] - (f32) sSkyOwn[i]) * s + 0.5f);
        cl[i] = sCloudOwn[i] + (kCloudWet[i] - sCloudOwn[i]) * s;
    }
}

/* arena.rain(slip [, grip]): slip > 0 = rain with that slip (seconds at full rain), 0 = stop (it rolls out); grip = the grenades'
 * landed-slide friction at full rain (0.2 .. 1, default 0.5) */
int portLuaGameRain(float slip, float grip)
{
    if (!portModeActive())
    {
        return 0;
    }
    epochCheck();
    if (slip <= 0.0f)
    {
        if (sSlip > 0.0f)
        {
            portConLogf("ge: arena: rain off (rolling out over %.1f s)\n", (double) RAIN_FADE_SECS);
        }
        sSlip = 0.0f;
        return 1;
    }
    if (slip > 2.0f) slip = 2.0f;
    if (grip < 0.2f) grip = 0.2f;
    if (grip > 1.0f) grip = 1.0f;
    if (!sLive && !g_FogSkyIsEnabled)
    {   /* the stage's own sky, to fade from and back to */
        const struct CurrentEnvironmentRecord *c = fogGetCurrentEnvironmentp();

        sSkyOwn[0] = c->Red;
        sSkyOwn[1] = c->Green;
        sSkyOwn[2] = c->Blue;
        sCloudOwn[0] = c->CloudRed;
        sCloudOwn[1] = c->CloudGreen;
        sCloudOwn[2] = c->CloudBlue;
        sSkyTaken = 1;
    }
    sSlip = sSlipLast = slip;
    sGripAsk = grip;
    sLive = 1;
    portConLogf("ge: arena: rain on: slip %.2f s, grenade grip %.2f (rolling in over %.1f s)\n", (double) slip, (double) grip,
                (double) RAIN_FADE_SECS);
    return 1;
}

float portLuaGameRainGet(void) { epochCheck(); return portModeActive() ? sSlip : 0.0f; }

static void strikeTick(int clockTicks);

/* every sim tick (port_luavm.c portLuaTick) */
void portArenaRainTick(int clockTicks)
{
    const f32 step = ((f32) clockTicks / 60.0f) / RAIN_FADE_SECS;

    if (!portModeActive() || clockTicks <= 0)
    {
        return;
    }
    epochCheck();
    strikeTick(clockTicks);   /* (a strike on its fuse still lands after the rain has stopped) */
    if (!sLive)
    {
        return;
    }
    if (sSlip > 0.0f)
    {
        if (sAmt < 1.0f)
        {
            sAmt = sAmt + step > 1.0f ? 1.0f : sAmt + step;
            skyApply();
        }
        return;
    }
    sAmt -= step;
    if (sAmt > 0.0f)
    {
        skyApply();
        return;
    }
    sAmt = 0.0f;
    skyApply();   /* (back to the stage's own colours exactly) */
    sLive = sSkyTaken = 0;
    memset(sVelOk, 0, sizeof(sVelOk));
    portConLogf("ge: arena: rain out: dry again\n");
}

/* how much rain there is now, 0..1 (the drawing; the crowd) */
float portArenaRainAmount(void)
{
    return (portModeActive() && sLive && sEpoch == g_portCoopEpoch) ? smooth() : 0.0f;
}

/* propobj.c, a landed projectile's slide: the friction multiplier (exactly 1 when dry) */
f32 portArenaRainGrip(void)
{
    if (!portModeActive() || !sLive || sEpoch != g_portCoopEpoch)
    {
        return 1.0f;
    }
    return 1.0f - (1.0f - sGripAsk) * smooth();
}

/* bondview2.c, before the player's collision: the walk's offset this tick (dx, dz) becomes a slipping velocity's */
void portArenaRainSlip(s32 p, f32 *dx, f32 *dz, f32 delta)
{
    f32 tau, k;

    if (p < 0 || p >= RAIN_PLAYERS || delta <= 0.0f || !portModeActive() || !sLive || sEpoch != g_portCoopEpoch)
    {
        if (p >= 0 && p < RAIN_PLAYERS) sVelOk[p] = 0;
        return;
    }
    tau = sSlipLast * smooth() * 60.0f;   /* in ticks */
    if (tau < 0.5f || !sVelOk[p])
    {   /* (no slip yet: the velocity starts as what was asked) */
        sVel[p][0] = *dx / delta;
        sVel[p][1] = *dz / delta;
        sVelOk[p] = 1;
        return;
    }
    k = delta / (tau + delta);   /* (a first-order follow without libm: the same on every platform) */
    sVel[p][0] += (*dx / delta - sVel[p][0]) * k;
    sVel[p][1] += (*dz / delta - sVel[p][1]) * k;
    *dx = sVel[p][0] * delta;
    *dz = sVel[p][1] * delta;
}

/* bondview2.c, after the collision: the velocity is what the player really moved (a wall or a chr stops the slide) */
void portArenaRainSlipAfter(s32 p, f32 movedx, f32 movedz, f32 delta)
{
    if (p < 0 || p >= RAIN_PLAYERS || delta <= 0.0f || !sVelOk[p] || !sLive)
    {
        return;
    }
    sVel[p][0] = movedx / delta;
    sVel[p][1] = movedz / delta;
}

/* ---- LIGHTNING (AC: "a massive flash of light, a zap to the ground and an explosion. random spot in the map but close to a random
 * character (inc Bond) so you have to keep moving") ----
 * arena.strike(who, dx, dz, kind, fuse): the mutator (Lua, the mode's seeded RNG) picks when, at whom and the offset. Here the point
 * is walked along the floor from the character's feet (dx, dz) away (the stan walk: no wall, ledge or gap in between; shorter when
 * that is refused, the character's own spot at worst), so it is always on walkable ground in the arena. It is held there for the
 * fuse, then a stock explosion of that kind goes off on it with no owner (it hurts whoever stands in it, Bond included, like a
 * grenade). The fuse is what makes standing still the danger: the point is where the character WAS. The draw side reads the
 * records (portArenaStrikePending / portArenaStrikeLanded) for the crackle on the fuse, the flash and the bolt. */
#define STRIKE_MAX    8
#define STRIKE_LANDED 4

typedef struct
{
    coord3d pos;      /* the floor point */
    StandTile *stan;
    s32 fuse;         /* ticks to go; 0 = free */
    s32 fuse0;
    s32 kind;
    u32 serial;
} Strike;

typedef struct { coord3d pos; u32 serial; } StrikeLanded;   /* serial 0 = none */

static Strike sStrike[STRIKE_MAX];
static StrikeLanded sLanded[STRIKE_LANDED];
static s32 sLandedI;
static u32 sStrikeSerial;   /* (never reset: the draw side keys on it) */

static void strikeReset(void)
{
    memset(sStrike, 0, sizeof(sStrike));
    memset(sLanded, 0, sizeof(sLanded));
    sLandedI = 0;
}

/* the anchor's floor point and tile: a live chr by chrnum (who >= 0) or Bond p (who = -1 - p) */
static int strikeAnchor(int who, coord3d *pos, StandTile **stan)
{
    PropRecord *pr;

    if (who < 0)
    {
        const s32 p = -1 - who;

        if (p >= portBondCount() || !portBondInPlay(p) || g_playerPointers[p] == NULL || (pr = g_playerPointers[p]->prop) == NULL)
        {
            return 0;
        }
    }
    else
    {
        ChrRecord *c = chrFindByLiteralId(who);

        if (c == NULL || c->model == NULL || (pr = c->prop) == NULL)
        {
            return 0;
        }
    }
    if (pr->stan == NULL)
    {
        return 0;
    }
    *pos = pr->pos;
    *stan = pr->stan;
    pos->y = stanGetPositionYValue(*stan, pos->x, pos->z);   /* (a Bond's prop is at eye level) */
    return 1;
}

/* arena.strike: 0 = refused (no such character, a bad kind, every slot on a fuse), else the strike's serial; *o = the floor point */
int portLuaGameStrike(int who, float dx, float dz, int kind, float fuseSecs, float *ox, float *oy, float *oz)
{
    extern s32 portExplosionTypeCount(void);   /* explosion.c */
    coord3d pos, at;
    StandTile *st, *sat;
    Strike *s = NULL;
    f32 k, got = 0.0f;
    s32 i;

    if (!portModeActive() || kind < 1 || kind >= portExplosionTypeCount())
    {
        return 0;
    }
    epochCheck();
    if (!strikeAnchor(who, &pos, &st))
    {
        return 0;
    }
    for (i = 0; i < STRIKE_MAX && s == NULL; i++)
    {
        if (sStrike[i].fuse <= 0) s = &sStrike[i];
    }
    if (s == NULL)
    {
        return 0;
    }
    for (k = 1.0f; k > 0.15f; k *= 0.6f)
    {   /* the whole offset, else shorter (a wall or the pit's edge in the way) */
        at = pos;
        sat = st;
        if (portHeistOffsetStan(&at, &sat, dx * k, dz * k))
        {
            pos = at;
            st = sat;
            got = k;
            break;
        }
    }
    pos.y = stanGetPositionYValue(st, pos.x, pos.z);
    if (fuseSecs < 0.0f) fuseSecs = 0.0f;
    if (fuseSecs > 5.0f) fuseSecs = 5.0f;
    s->pos = pos;
    s->stan = st;
    s->kind = kind;
    s->fuse = s->fuse0 = 1 + (s32) (fuseSecs * 60.0f + 0.5f);
    s->serial = ++sStrikeSerial;
    *ox = pos.x;
    *oy = pos.y;
    *oz = pos.z;
    portConLogf("ge: arena: LIGHTNING %u aimed at %s %d: (%.0f, %.0f, %.0f), offset %.0f of %.0f, fuse %d ticks, explosion type %d\n",
                (unsigned) s->serial, who < 0 ? "bond" : "chr", who < 0 ? -1 - who : who, (double) pos.x, (double) pos.y, (double) pos.z,
                (double) (got * sqrtf(dx * dx + dz * dz)), (double) sqrtf(dx * dx + dz * dz), (int) s->fuse, (int) kind);
    return (int) s->serial;
}

static void strikeTick(int clockTicks)
{
    s32 i;

    for (i = 0; i < STRIKE_MAX; i++)
    {
        Strike *s = &sStrike[i];
        u8 rooms[2];

        if (s->fuse <= 0 || (s->fuse -= clockTicks) > 0)
        {
            continue;
        }
        s->fuse = 0;
        rooms[0] = (u8) getTileRoom(s->stan);
        rooms[1] = 0xFF;
        explosionCreate(NULL, &s->pos, s->stan, (s16) s->kind, 1, 0, rooms, 0);
        sLanded[sLandedI].pos = s->pos;
        sLanded[sLandedI].serial = s->serial;
        sLandedI = (sLandedI + 1) % STRIKE_LANDED;
        portConLogf("ge: arena: LIGHTNING %u lands at (%.0f, %.0f, %.0f): explosion type %d\n", (unsigned) s->serial, (double) s->pos.x,
                    (double) s->pos.y, (double) s->pos.z, (int) s->kind);
    }
}

/* the draw side (presentation): strike slot i on its fuse -> 1, its point and how much of the fuse has gone (0..1) */
int portArenaStrikePending(int i, float *x, float *y, float *z, float *gone)
{
    const Strike *s;

    if (i < 0 || i >= STRIKE_MAX || !portModeActive() || sEpoch != g_portCoopEpoch || (s = &sStrike[i])->fuse <= 0)
    {
        return 0;
    }
    *x = s->pos.x;
    *y = s->pos.y;
    *z = s->pos.z;
    *gone = s->fuse0 > 0 ? 1.0f - (f32) s->fuse / (f32) s->fuse0 : 1.0f;
    return 1;
}

/* ... the last landed strikes (i 0..STRIKE_LANDED-1): the serial (0 = none) and the point */
unsigned portArenaStrikeLanded(int i, float *x, float *y, float *z)
{
    if (i < 0 || i >= STRIKE_LANDED || !portModeActive() || sEpoch != g_portCoopEpoch || sLanded[i].serial == 0)
    {
        return 0;
    }
    *x = sLanded[i].pos.x;
    *y = sLanded[i].pos.y;
    *z = sLanded[i].pos.z;
    return sLanded[i].serial;
}

#endif /* !TARGET_N64 */
