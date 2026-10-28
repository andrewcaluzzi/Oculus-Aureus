/* port_arenafx.c - arena drop A7 (modding/arena.md "Drop A7"): the small engine pieces behind the wave mutators and the elite
 * affixes. Lua bindings in port/luavm/port_luavm_arena.c; declarations in port/include/port_arena_a7.h.
 *
 *   knife-only chrs  a side table by chrnum; portModEnemyKnife (port_modifier.c) drops every shot of a flagged chr that is not a knife
 *   chr scale        modelSetScale on one chr (an elite is drawn a little bigger)
 *   chr heal/health  chr->damage (wounds > 0, armour < 0) against chr->maxdamage
 *   explode          explosionCreate at a chr's feet (the explosive-death affix): a stock explosion, it hurts whoever stands there
 *   fog              a fog environment for the arena stage (the LOW VISIBILITY mutator); 0 = back to the level's own
 * All sim state here is classified SIM in scripts/port/snap_embed.py. */
#ifndef TARGET_N64

#include <string.h>
#include <ultra64.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include "boss.h"
#include "game/chr.h"
#include "game/chrai.h"
#include "game/chraction.h"   /* chrIsDead (1.0.2b moved it here) */
#include "game/model.h"
#include "game/explosion.h"
#include "game/bgfog.h"
#include "port_mode.h"
#include "port_arena_a7.h"

extern void portConLogf(const char *fmt, ...);
extern void fogLoadCurrentEnvironment(EnvironmentRecord *arg0);   /* bgfog.c (not in its header) */

#define FX_N 1024   /* direct-mapped by chrnum, like port_arenamode.c's table (arena chrnums are consecutive) */

static s32 sKnifeOnly[FX_N];   /* chrnum + 1 of a knife-only chr in its slot, 0 = none */

static ChrRecord *liveChr(int chrnum)
{
    ChrRecord *c;

    if (!portModeActive())
    {
        return NULL;
    }
    c = chrFindByLiteralId(chrnum);
    return (c != NULL && c->prop != NULL && c->model != NULL) ? c : NULL;
}

s32 portArenaChrKnifeOnly(const ChrRecord *chr)
{
    return chr != NULL && portModeActive() && sKnifeOnly[(u32)chr->chrnum & (FX_N - 1)] == (s32)chr->chrnum + 1;
}

int portLuaGameChrKnifeOnly(int chrnum, int on)
{
    if (liveChr(chrnum) == NULL)
    {
        return 0;
    }
    sKnifeOnly[(u32)chrnum & (FX_N - 1)] = on ? chrnum + 1 : 0;
    return 1;
}

int portLuaGameChrScale(int chrnum, float factor)
{
    ChrRecord *c = liveChr(chrnum);

    if (c == NULL || factor < 0.5f || factor > 2.0f)
    {
        return 0;
    }
    /* As the Tiny Bond / DK Mode cheats do (cheat.c): the drawn size is proportional (tests/arena_a7/arena7-look.lua: x1 / x1.15 /
     * x0.87 measured 105 / 120 / 91 px tall; mind that +x is on the screen's LEFT when Bond faces +z). */
    portConLogf("ge: arena: chr %d model scale %.4f -> %.4f\n", chrnum, (double)c->model->scale, (double)(c->model->scale * factor));
    modelSetScale(c->model, c->model->scale * factor);
    return 1;
}

int portLuaGameChrHeal(int chrnum, float frac)
{
    ChrRecord *c = liveChr(chrnum);

    if (c != NULL && !chrIsDead(c) && frac < 0.0f)
    {   /* test hook (test.hurt): a wound that never kills (blasts always kill a chr, so a test cannot hurt one partially) */
        c->damage -= frac * c->maxdamage;
        if (c->damage > c->maxdamage * 0.95f)
        {
            c->damage = c->maxdamage * 0.95f;
        }
        return 1;
    }
    if (c == NULL || chrIsDead(c) || frac <= 0.0f || c->damage <= 0.0f)
    {
        return 0;   /* nothing to heal: dead, unhurt, or still in its armour (damage < 0) */
    }
    c->damage -= frac * c->maxdamage;
    if (c->damage < 0.0f)
    {
        c->damage = 0.0f;
    }
    return 1;
}

int portLuaGameChrHealth(int chrnum, float *damage, float *maxdamage)
{
    ChrRecord *c = liveChr(chrnum);

    if (c == NULL)
    {
        return 0;
    }
    *damage = c->damage;
    *maxdamage = c->maxdamage;
    return 1;
}

/* kind comes from Lua (spawn.explode, the explosive affix): bounded by the real table size. EXPLOSION_DEF_21 is 21 but g_ExplosionTypes
 * has 21 entries (0..20), so the old "kind > EXPLOSION_DEF_21" test let type 21 read past the table. */
int portLuaGameExplode(int chrnum, int kind)
{
    extern s32 portExplosionTypeCount(void);   /* explosion.c */
    ChrRecord *c = liveChr(chrnum);
    coord3d pos;

    if (c == NULL || c->prop->stan == NULL || kind < 1 || kind >= portExplosionTypeCount())
    {
        return 0;
    }
    pos = c->prop->pos;
    explosionCreate(NULL, &pos, c->prop->stan, (s16)kind, 1, 0, c->prop->rooms, 0);
    portConLogf("ge: arena: explosion type %d at chr %d (%.0f, %.0f, %.0f)\n", kind, chrnum, (double)pos.x, (double)pos.y, (double)pos.z);
    return 1;
}

/* ---- the arena fog (the LOW VISIBILITY mutator) ----
 * v0.1 loaded a fog record with its far plane AT the fog's far distance (2600) and full fog only at that plane, on a stage whose rooms
 * had all been patched fogless at load time: AC's playtest (v0.2, wave 11) saw the level unfogged, hard-cut at 2600 against the grey,
 * the crowd floating unfogged, and the switch was a pop. Now (with port_roomfog.c re-patching the rooms, and the crowd impostors and
 * the markers fogged through bgfog.c portFogCurve):
 *   - the curve is stock Jungle's: Jungle (GE_FOG_AUDIT) is near 10, far 2500, fog from 996 to 1000 thousandths of the depth range,
 *     i.e. from ~1250 to full at 2500 (vertex fog is linear in the depth buffer's value, ~1/distance, so it thickens quickly and then
 *     flattens). Here the band is the same 4 thousandths wide and ends at the mutator's distance: 2600 -> fog from ~1275 to full at 2600;
 *   - the far plane is FOG_FAR, past the whole Colosseum: nothing is clipped while the fog is on, everything beyond the full-fog
 *     distance is drawn in the fog colour (and whole rooms out there are culled by the fog distance, as on stock fog stages);
 *   - it fades in and out over FOG_FADE_SECS by sliding the band from past the far plane (no fog anywhere) to its place: the fog
 *     comes in from the distance like a wall closing in. The positions are fractions of a thousandth there, so they go to bgfog.c as
 *     floats (portFogSetPositionF). The sky (and the clouds) go from the Colosseum's own colour to the fog's at the same time. The fog
 *     factor is an s16 128000 / (max - min), which is why the band cannot be wider apart in distance at near 10 than Jungle's;
 *   - g_ScaledFarFogIntensity, which the sim reads (chraction.c: a guard's notice roll only within the fog), is set to the full-fog
 *     distance (no limit while the band is still past the far plane), so at full fog the gameplay distance is exactly v0.1's 2600.
 * Driven per sim tick (portArenaFogTick from portLuaTick): every value is a function of ticks, the same on every machine, and the
 * state is in this SIM file. */
#define FOG_NEAR      10.0f
#define FOG_FAR       16000.0f  /* the far plane while the fog is on: past the whole Colosseum (the pit is 9000 across) */
#define FOG_SPAN      4.0f      /* thousandths of the depth range from the fog's start to full (Jungle's 996..1000) */
#define FOG_OUT       (1000.5f + FOG_SPAN)   /* the band's full end at the fade's ends: past the far plane, so no fog at all */
#define FOG_FADE_SECS 1.5f

extern f32 g_ScaledFarFogIntensity;   /* bgfog.c: the fog's far distance the sim reads */
extern int g_portCoopEpoch;           /* port_coop.c: counts stage starts */
extern f32 bgGetLevelVisibilityScale(void);

static EnvironmentRecord sFogEnv;   /* the fogLoadCurrentEnvironment record must outlive the call */
static f32 sFogFar;                 /* what arena.fog asked for (0 = off): arena.fog() reads it back */
static f32 sFogTarget;              /* the full-fog distance the fade runs to / from */
static f32 sFogAmt;                 /* 0 .. 1: how far the fade has got */
static s32 sFogLoaded;              /* this file's fog record is in force (the fade may be running out) */
static s32 sFogEpoch = -1000;
static u8 sFogRgb[3];
static SkyBoxRecord sFogSky;        /* the stage's own sky, taken when the fog starts */

static u8 lerpU8(u8 a, u8 b, f32 t) { return (u8) ((f32) a + ((f32) b - (f32) a) * t + 0.5f); }

/* thousandths of the depth range at view distance w, and back (the depth buffer's value: F / (F - n) * (1 - n / w)) */
static f32 depthAt(f32 w) { return FOG_FAR / (FOG_FAR - FOG_NEAR) * (1.0f - FOG_NEAR / w) * 1000.0f; }

static f32 distAt(f32 frac)
{
    const f32 d = 1.0f - frac / 1000.0f * (FOG_FAR - FOG_NEAR) / FOG_FAR;

    return d > FOG_NEAR / 1.0e9f ? FOG_NEAR / d : 1.0e9f;
}

static void fogApply(void)
{
    extern void portFogSetPositionF(s32 on, f32 mn, f32 mx);   /* bgfog.c */
    const f32 s = sFogAmt * sFogAmt * (3.0f - 2.0f * sFogAmt);   /* smoothstep */
    const f32 mxT = depthAt(sFogTarget);
    const f32 mx = FOG_OUT + (mxT - FOG_OUT) * s;
    const f32 mn = mx - FOG_SPAN;
    f32 vs = bgGetLevelVisibilityScale();

    memset(&sFogEnv, 0, sizeof(sFogEnv));
    sFogEnv.Id = (u32) bossGetStageNum();
    sFogEnv.Visibility.BlendMultiplier = FOG_NEAR;   /* the near plane, as Jungle and most stock fog rows */
    sFogEnv.Visibility.FarFog = FOG_FAR;
    sFogEnv.Fog.DifferenceFromFarIntensity = (s32) mn;   /* (the floats below are what is drawn) */
    sFogEnv.Fog.FarIntensity = (s32) mx;
    portFogSetPositionF(1, mn, mx);
    sFogEnv.Sky = sFogSky;
    sFogEnv.Sky.Red = lerpU8(sFogSky.Red, sFogRgb[0], s);
    sFogEnv.Sky.Green = lerpU8(sFogSky.Green, sFogRgb[1], s);
    sFogEnv.Sky.Blue = lerpU8(sFogSky.Blue, sFogRgb[2], s);
    sFogEnv.Sky.CloudRed = sFogSky.CloudRed + ((f32) sFogRgb[0] - sFogSky.CloudRed) * s;
    sFogEnv.Sky.CloudGreen = sFogSky.CloudGreen + ((f32) sFogRgb[1] - sFogSky.CloudGreen) * s;
    sFogEnv.Sky.CloudBlue = sFogSky.CloudBlue + ((f32) sFogRgb[2] - sFogSky.CloudBlue) * s;
    if (s >= 0.999f)
    {   /* at full fog the sky is the flat fog colour, as on stock fog stages: the cloud layer's shading is lighter than the fog, and
         * fully fogged figures on the top tier stood out against it */
        sFogEnv.Sky.Clouds = 0;
    }
    fogLoadCurrentEnvironment(&sFogEnv);   /* Nfd 0 = no chr fade (g_NearFogValuesP NULL); sets g_FogSkyIsEnabled */
    g_ScaledFarFogIntensity = (mx < 1000.0f ? distAt(mx) : 1.0e9f) / (vs > 0.0f ? vs : 1.0f);
}

static void fogEpochCheck(void)
{
    if (sFogEpoch != g_portCoopEpoch)
    {   /* a new stage: its own environment is already loaded; nothing of the last stage's fog carries over */
        sFogEpoch = g_portCoopEpoch;
        sFogFar = sFogTarget = sFogAmt = 0.0f;
        sFogLoaded = 0;
    }
}

int portLuaGameFog(float farv, int r, int g, int b)
{
    if (!portModeActive())
    {
        return 0;
    }
    fogEpochCheck();
    if (farv <= 0.0f)
    {
        if (sFogFar > 0.0f)
        {
            portConLogf("ge: arena: fog off (fading out over %.1f s)\n", (double) FOG_FADE_SECS);
        }
        sFogFar = 0.0f;   /* the tick fades it out, then loads the stage's own environment again */
        return 1;
    }
    if (farv < 1000.0f) farv = 1000.0f;
    if (farv > 8000.0f) farv = 8000.0f;   /* (below FOG_CLEAR) */
    if (!sFogLoaded)
    {   /* the stage's own sky, to fade from */
        const struct CurrentEnvironmentRecord *c = fogGetCurrentEnvironmentp();

        memset(&sFogSky, 0, sizeof(sFogSky));
        if (c != NULL)
        {
            sFogSky.Red = c->Red;
            sFogSky.Green = c->Green;
            sFogSky.Blue = c->Blue;
            sFogSky.Clouds = c->Clouds;
            sFogSky.CloudRepeat = c->CloudRepeat;
            sFogSky.SkyImageId = c->SkyImageId;
            sFogSky.CloudRed = c->CloudRed;
            sFogSky.CloudGreen = c->CloudGreen;
            sFogSky.CloudBlue = c->CloudBlue;
            sFogSky.IsWater = c->IsWater;
            sFogSky.WaterRepeat = c->WaterRepeat;
            sFogSky.WaterImageId = c->WaterImageId;
            sFogSky.WaterRed = c->WaterRed;
            sFogSky.WaterGreen = c->WaterGreen;
            sFogSky.WaterBlue = c->WaterBlue;
            sFogSky.WaterConcavity = c->WaterConcavity;
        }
        sFogAmt = 0.0f;
    }
    sFogFar = sFogTarget = farv;
    sFogRgb[0] = (u8) r;
    sFogRgb[1] = (u8) g;
    sFogRgb[2] = (u8) b;
    sFogLoaded = 1;
    fogApply();
    portConLogf("ge: arena: fog far %.0f colour %d,%d,%d (fading in over %.1f s; at full: fog from %.0f, far plane %.0f)\n", (double) farv,
                r, g, b, (double) FOG_FADE_SECS, (double) distAt(depthAt(farv) - FOG_SPAN), (double) FOG_FAR);
    return 1;
}

/* every sim tick (port_luavm.c portLuaTick): the fade */
void portArenaFogTick(int clockTicks)
{
    f32 step;

    if (!portModeActive() || clockTicks <= 0)
    {
        return;
    }
    fogEpochCheck();
    if (!sFogLoaded)
    {
        return;
    }
    step = ((f32) clockTicks / 60.0f) / FOG_FADE_SECS;
    if (sFogFar > 0.0f)
    {
        if (sFogAmt >= 1.0f)
        {
            return;
        }
        sFogAmt = sFogAmt + step > 1.0f ? 1.0f : sFogAmt + step;
        fogApply();
        if (sFogAmt >= 1.0f)
        {
            portConLogf("ge: arena: fog in: from %.0f to full at %.0f, far plane %.0f\n", (double) distAt(depthAt(sFogTarget) - FOG_SPAN),
                        (double) sFogTarget, (double) FOG_FAR);
        }
        return;
    }
    sFogAmt -= step;
    if (sFogAmt > 0.0f)
    {
        fogApply();
        return;
    }
    sFogAmt = 0.0f;
    sFogLoaded = 0;
    fogLoadLevelEnvironment(bossGetStageNum(), 0);   /* the stage's own (the Colosseum: fogless, blue sky) */
    portConLogf("ge: arena: fog out: the stage's own environment again\n");
}

/* the fog is drawn at all (fading in, full or fading out): the corpse bake waits (port_perfcat.c) */
int portArenaFogDrawn(void)
{
    return portModeActive() && sFogLoaded && sFogEpoch == g_portCoopEpoch;
}

float portLuaGameFogGet(void)
{
    return (portModeActive() && sFogLoaded && sFogEpoch == g_portCoopEpoch) ? sFogFar : 0.0f;
}

#endif /* TARGET_N64 */
