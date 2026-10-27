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

static EnvironmentRecord sFogEnv;   /* the fogLoadCurrentEnvironment record must outlive the call */
static f32 sFogFar;

int portLuaGameFog(float farv, int r, int g, int b)
{
    if (!portModeActive())
    {
        return 0;
    }
    if (farv <= 0.0f)
    {
        if (sFogFar > 0.0f)
        {
            fogLoadLevelEnvironment(bossGetStageNum(), 0);   /* the stage's own (the Colosseum: fogless, blue sky) */
        }
        sFogFar = 0.0f;
        return 1;
    }
    if (farv < 1000.0f) farv = 1000.0f;
    if (farv > 16000.0f) farv = 16000.0f;
    memset(&sFogEnv, 0, sizeof(sFogEnv));
    sFogEnv.Id = (u32)bossGetStageNum();
    sFogEnv.Visibility.BlendMultiplier = 10.0f;   /* the near plane, as most stock fog rows */
    sFogEnv.Visibility.FarFog = farv;             /* far plane and fog end: nothing beyond it is drawn */
    sFogEnv.Fog.DifferenceFromFarIntensity = 0x3DE;   /* fog starts this far into the depth range (Facility's 990/1000: a thick fog) */
    sFogEnv.Fog.FarIntensity = 0x3E8;
    sFogEnv.Sky.Red = (u8)r;
    sFogEnv.Sky.Green = (u8)g;
    sFogEnv.Sky.Blue = (u8)b;
    sFogEnv.Sky.Clouds = 0;
    sFogEnv.Sky.IsWater = 0;
    fogLoadCurrentEnvironment(&sFogEnv);   /* Nfd 0 = no chr fade (g_NearFogValuesP NULL); sets g_FogSkyIsEnabled */
    sFogFar = farv;
    portConLogf("ge: arena: fog far %.0f colour %d,%d,%d\n", (double)farv, r, g, b);
    return 1;
}

float portLuaGameFogGet(void)
{
    return (portModeActive() && g_FogSkyIsEnabled) ? sFogFar : 0.0f;
}

#endif /* TARGET_N64 */
