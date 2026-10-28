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
 * The enemies' half (slower, stumbling) is Lua: anim_speed on the wave's spawns and spawn.anim slips on hard turns / sudden stops,
 * with the mode's deterministic RNG. Never outside a Lua mode. SIM (scripts/port/snap_embed.py). */
#ifndef TARGET_N64

#include <string.h>
#include <ultra64.h>
#include "game/bgfog.h"
#include "port_mode.h"

extern void portConLogf(const char *fmt, ...);
extern int g_portCoopEpoch;

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

/* every sim tick (port_luavm.c portLuaTick) */
void portArenaRainTick(int clockTicks)
{
    const f32 step = ((f32) clockTicks / 60.0f) / RAIN_FADE_SECS;

    if (!portModeActive() || clockTicks <= 0)
    {
        return;
    }
    epochCheck();
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

#endif /* !TARGET_N64 */
