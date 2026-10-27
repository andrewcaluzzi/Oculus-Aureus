/* port_chrfx.c - per-chr colour wash and translucency for the arena's Whacky effects (see port_whacky.h).
 *
 * How it draws (chrRenderProp, chr.c): a chr's materials are blended in the RDP blender towards the "stan colour" by the fog
 * colour's alpha (G_RM_FOG_PRIM_A, G_BL_CLR_FOG / G_BL_A_FOG; chr->shadecol: white = 0,0,0,0, black = 0,0,0,128, red = 255,0,0,128,
 * bondtypes.h). The wash composes one more colour into that same register:
 *     A = 1 - (1 - sA)(1 - t)      RGB = (S sA (1 - t) + T t) / A
 * (S, sA the stan colour, T, t the wash), before the distance fog is composed on top (lerp_rgba_s32_with_rgba_f32), so a washed chr
 * still fogs like any other. Translucency multiplies the chr's fade alpha before chrRenderProp picks its pass: below 255 the chr moves
 * to the translucent pass and its alpha rides the env colour, exactly the stock death fade-out. Nothing new reaches Fast3D: the same
 * DL commands are emitted with other values, so no render state can leak into the next draw.
 *
 * PRESENTATION ONLY (scripts/port/snap_embed.py PRES_PORT): the sim never reads this table. A chr's ModelHitEntries are built and freed
 * the same way whichever pass draws it (the alpha is kept above 0, so drawjointlist always runs). Animations run on g_GlobalTimer and a
 * hash of the chrnum: no RNG, no allocation. Keyed by chrnum like port_arenafx.c (arena chrnums are consecutive). */
#ifndef TARGET_N64

#include <string.h>
#include <ultra64.h>
#include <PR/gu.h>   /* sinf */
#include <bondtypes.h>
#include "port_mode.h"
#include "port_whacky.h"

extern s32 g_GlobalTimer;

#define FX_N 1024

typedef struct {
    s32 chrnum;       /* chrnum + 1; 0 = empty */
    PortChrFx fx;
} FxSlot;

static FxSlot sFx[FX_N];
static s32 sCount;   /* live entries: both hooks return at once at 0 */

static FxSlot *slotFor(int chrnum)
{
    return &sFx[(u32)chrnum & (FX_N - 1)];
}

int portChrFxSet(int chrnum, const PortChrFx *fx)
{
    FxSlot *s = slotFor(chrnum);

    if (fx == NULL)
    {
        return 0;
    }
    if (s->chrnum == 0)
    {
        sCount++;
    }
    s->chrnum = chrnum + 1;   /* a newer chr on the same slot simply takes it */
    s->fx = *fx;
    if (s->fx.period == 0)
    {
        s->fx.period = 60;
    }
    return 1;
}

/* the Lua glue (port/luavm, built without the game headers) passes plain ints */
int portChrFxSetRaw(int chrnum, int r, int g, int b, int amount, int r2, int g2, int b2, int alpha, int anim, int lo, int period)
{
    PortChrFx fx;

#define CL8(v) ((u8)((v) < 0 ? 0 : ((v) > 255 ? 255 : (v))))
    fx.r = CL8(r); fx.g = CL8(g); fx.b = CL8(b); fx.amount = CL8(amount);
    fx.r2 = CL8(r2); fx.g2 = CL8(g2); fx.b2 = CL8(b2);
    fx.alpha = CL8(alpha);
    fx.anim = (u8)(anim < 0 || anim > CHRFX_ANIM_PULSE2 ? 0 : anim);
    fx.lo = CL8(lo);
    fx.period = (u16)(period < 2 ? 2 : (period > 6000 ? 6000 : period));
#undef CL8
    return portChrFxSet(chrnum, &fx);
}

int portChrFxClear(int chrnum)
{
    FxSlot *s = slotFor(chrnum);

    if (s->chrnum != chrnum + 1)
    {
        return 0;
    }
    s->chrnum = 0;
    sCount--;
    return 1;
}

void portChrFxClearAll(void)
{
    memset(sFx, 0, sizeof(sFx));
    sCount = 0;
}

int portChrFxCount(void)
{
    return sCount;
}

int portChrFxGet(int chrnum, PortChrFx *out)
{
    FxSlot *s = slotFor(chrnum);

    if (s->chrnum != chrnum + 1)
    {
        return 0;
    }
    *out = s->fx;
    return 1;
}

static const PortChrFx *fxOf(const ChrRecord *chr)
{
    FxSlot *s;

    if (sCount <= 0 || chr == NULL || !portModeActive())
    {
        return NULL;
    }
    s = slotFor(chr->chrnum);
    return (s->chrnum == (s32)chr->chrnum + 1) ? &s->fx : NULL;
}

static u32 fxHash(u32 x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

/* 0..1 sine over the period, phased per chr so a wave does not pulse in lockstep */
static f32 fxWave(const PortChrFx *fx, s32 chrnum, f32 speedmul)
{
    f32 period = (f32)fx->period / speedmul;
    u32 t = (u32)g_GlobalTimer + (u32)chrnum * 37u;

    if (period < 2.0f)
    {
        period = 2.0f;
    }
    return 0.5f + 0.5f * sinf(6.2831853f * (f32)(t % (u32)(period * 16.0f)) / period);
}

static void hueToRgb(f32 h, u8 *r, u8 *g, u8 *b)   /* h 0..6, full saturation and value */
{
    s32 i = (s32)h;
    f32 f = h - (f32)i;
    f32 rr = 0.0f, gg = 0.0f, bb = 0.0f;

    switch (i % 6)
    {
        case 0: rr = 1.0f; gg = f; break;
        case 1: rr = 1.0f - f; gg = 1.0f; break;
        case 2: gg = 1.0f; bb = f; break;
        case 3: gg = 1.0f - f; bb = 1.0f; break;
        case 4: rr = f; bb = 1.0f; break;
        default: rr = 1.0f; bb = 1.0f - f; break;
    }
    *r = (u8)(rr * 255.0f);
    *g = (u8)(gg * 255.0f);
    *b = (u8)(bb * 255.0f);
}

/* this frame's wash colour, wash amount (0..255) and alpha (0..255) */
static void fxEval(const PortChrFx *fx, s32 chrnum, s32 *r, s32 *g, s32 *b, s32 *amt, s32 *alpha)
{
    *r = fx->r;
    *g = fx->g;
    *b = fx->b;
    *amt = fx->amount;
    *alpha = fx->alpha;
    switch (fx->anim)
    {
        case CHRFX_ANIM_CYCLE:
        {
            u32 t = (u32)g_GlobalTimer + (u32)chrnum * 23u;
            u8 cr, cg, cb;

            hueToRgb(6.0f * (f32)(t % fx->period) / (f32)fx->period, &cr, &cg, &cb);
            *r = cr;
            *g = cg;
            *b = cb;
            break;
        }
        case CHRFX_ANIM_PULSE:
        {
            f32 w = fxWave(fx, chrnum, 1.0f);
            f32 lo = (f32)fx->amount * (f32)fx->lo / 255.0f;

            *amt = (s32)(lo + ((f32)fx->amount - lo) * w);
            break;
        }
        case CHRFX_ANIM_PULSE2:
        {
            f32 w = fxWave(fx, chrnum, 1.0f);

            *r = (s32)((f32)fx->r + ((f32)fx->r2 - (f32)fx->r) * w);
            *g = (s32)((f32)fx->g + ((f32)fx->g2 - (f32)fx->g) * w);
            *b = (s32)((f32)fx->b + ((f32)fx->b2 - (f32)fx->b) * w);
            break;
        }
        case CHRFX_ANIM_FLICKER:
        {   /* a ghost: a slow sway in alpha, and now and then (a hash of the chr and the 4-tick step) it all but vanishes */
            u32 h = fxHash(((u32)g_GlobalTimer >> 2) * 2654435761u ^ (u32)chrnum * 40503u);
            f32 w = fxWave(fx, chrnum, 1.0f);

            *alpha = (s32)((f32)fx->alpha * (0.7f + 0.3f * w));
            if ((h & 0xFF) < 40)
            {
                *alpha = fx->lo;
            }
            else if ((h & 0xFF) < 52)
            {
                *alpha = fx->lo / 3;
            }
            break;
        }
        case CHRFX_ANIM_SHIMMER:
        {   /* glass: a fast alpha shimmer, and the wash catches the light */
            f32 w = fxWave(fx, chrnum, 1.0f);
            f32 w2 = fxWave(fx, chrnum * 3 + 11, 2.7f);

            *alpha = (s32)((f32)fx->lo + ((f32)fx->alpha - (f32)fx->lo) * w);
            *amt = (s32)((f32)fx->amount * (0.6f + 0.4f * w2));
            break;
        }
        default:
            break;
    }
}

s32 portChrFxAlpha(ChrRecord *chr, s32 alpha)
{
    const PortChrFx *fx = fxOf(chr);
    s32 r, g, b, amt, a, out;

    if (fx == NULL || alpha <= 0)
    {
        return alpha;
    }
    fxEval(fx, chr->chrnum, &r, &g, &b, &amt, &a);
    if (a >= 255)
    {
        return alpha;   /* opaque: the chr stays in the opaque pass */
    }
    if (a < 0)
    {
        a = 0;
    }
    out = (alpha * a) / 255;
    return out < 1 ? 1 : out;   /* never 0: drawjointlist must still run (hit entries, held weapons) */
}

void portChrFxShade(ChrRecord *chr, rgba_s32 *shade)
{
    const PortChrFx *fx = fxOf(chr);
    s32 r, g, b, amt, a;
    f32 t, sa, A, k;

    if (fx == NULL || shade == NULL)
    {
        return;
    }
    fxEval(fx, chr->chrnum, &r, &g, &b, &amt, &a);
    if (amt <= 0)
    {
        return;
    }
    if (amt > 255)
    {
        amt = 255;
    }
    t = (f32)amt / 255.0f;
    sa = (f32)shade->a / 255.0f;
    A = 1.0f - (1.0f - sa) * (1.0f - t);
    if (A <= 0.0f)
    {
        return;
    }
    k = sa * (1.0f - t);
    shade->r = (s32)(((f32)shade->r * k + (f32)r * t) / A);
    shade->g = (s32)(((f32)shade->g * k + (f32)g * t) / A);
    shade->b = (s32)(((f32)shade->b * k + (f32)b * t) / A);
    shade->a = (s32)(A * 255.0f);
    if (shade->r > 255) shade->r = 255;
    if (shade->g > 255) shade->g = 255;
    if (shade->b > 255) shade->b = 255;
    if (shade->a > 255) shade->a = 255;
}

#endif /* TARGET_N64 */
