/**
 * port_hudstyle.c - shared HUD drawing helpers (port_hudstyle.h): boxes, gradients, frames and scaled stock-font text with a shadow or
 * an outline, in the console's 2D space, written into the helper's own double-buffered display list (as port_debrief.c does) and
 * called from it. The fonts are the game's own (Bank Gothic, Zurich Bold); no new art. Presentation only (snap_embed.py: PRES).
 */
#ifndef TARGET_N64

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <ultra64.h>

#include "game/textrelated.h"
#include "game/dyn.h"
#include "fr.h"
#include "port_hudstyle.h"

extern void *portAllocHigh(u32 size);
extern void portTextScale(f32 scale);
extern void portConsoleSpace(s32 *w, s32 *h);
extern void portConLogf(const char *fmt, ...);

#define HUD_DL_GFX  (64 * 1024)   /* (the arena's start menu / debrief, port_arenamenu_draw.c, needs over 24K on a busy page) */
#define HUD_DL_SAFE 300

#define CON2D(gdl, w, h) do { (gdl)->words.w0 = (u32) 0x4c << 24 /* G_CONSOLE2D_EXT */; \
                              (gdl)->words.w1 = ((u32) (w) << 16) | (u32) (h); (gdl)++; } while (0)

static Gfx *sDL[2];
static s32 sDLi, sNoMem;

Gfx *hudBegin(Gfx *gdl, HudCanvas *c)
{
    memset(c, 0, sizeof(*c));
    if (sNoMem || ptrFontBankGothic == NULL || ptrFontBankGothicChars == NULL || dynGetFreeGfx2(gdl) < 64)
    {
        return gdl;
    }
    if (sDL[0] == NULL)
    {
        sDL[0] = (Gfx *) portAllocHigh(HUD_DL_GFX * sizeof(Gfx));
        sDL[1] = (Gfx *) portAllocHigh(HUD_DL_GFX * sizeof(Gfx));
        if (sDL[0] == NULL || sDL[1] == NULL)
        {
            portConLogf("ge: hudstyle: no memory for the HUD display lists\n");
            sNoMem = 1;
            return gdl;
        }
    }
    portConsoleSpace(&c->w, &c->h);
    sDLi ^= 1;
    c->g = sDL[sDLi];
    c->end = c->g + HUD_DL_GFX;
    c->texMode = -1;
    c->ok = 1;
    c->s = 1.0f;
    c->vx = viGetX();
    c->vy = viGetY();
    viSetXY((s16) c->w, (s16) c->h);
    CON2D(gdl, c->w, c->h);
    gdl = microcode_constructor(gdl);
    gDPSetScissor(gdl++, G_SC_NON_INTERLACE, 0, 0, c->w, c->h);
    gSPDisplayList(gdl++, c->g);
    gDPSetRenderMode(c->g++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    return gdl;
}

Gfx *hudEnd(Gfx *gdl, HudCanvas *c)
{
    if (!c->ok)
    {
        return gdl;
    }
    portTextScale(1.0f);
    gSPEndDisplayList(c->g++);
    gdl = combiner_bayer_lod_perspective(gdl);
    CON2D(gdl, 0, 0);
    viSetXY(c->vx, c->vy);
    c->ok = 0;
    return gdl;
}

static s32 room(const HudCanvas *c, s32 need) { return c->ok && (c->end - c->g) > HUD_DL_SAFE + need; }

void hudSetTransform(HudCanvas *c, s32 ox, s32 oy, f32 s)
{
    c->ox = ox;
    c->oy = oy;
    c->s = s > 0.0f ? s : 1.0f;
}

static s32 TX(const HudCanvas *c, s32 v) { return c->ox + (s32) ((f32) v * c->s + (v >= 0 ? 0.5f : -0.5f)); }
static s32 TY(const HudCanvas *c, s32 v) { return c->oy + (s32) ((f32) v * c->s + (v >= 0 ? 0.5f : -0.5f)); }

/* a box in console units (after the transform) */
static void rawFill(HudCanvas *c, s32 x0, s32 y0, s32 x1, s32 y1, u32 rgba)
{
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > c->w) x1 = c->w;
    if (y1 > c->h) y1 = c->h;
    if (x1 <= x0 || y1 <= y0 || (rgba & 0xFFu) == 0 || !room(c, 8))
    {
        return;
    }
    if (c->texMode != 0)
    {
        gDPSetCombineMode(c->g++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
        c->texMode = 0;
        c->havePrim = 0;
    }
    if (!c->havePrim || rgba != c->prim)
    {
        gDPSetPrimColor(c->g++, 0, 0, (rgba >> 24) & 0xFF, (rgba >> 16) & 0xFF, (rgba >> 8) & 0xFF, rgba & 0xFF);
        c->prim = rgba;
        c->havePrim = 1;
    }
    gDPFillRectangle(c->g++, x0, y0, x1, y1);
}

void hudFill(HudCanvas *c, s32 x0, s32 y0, s32 x1, s32 y1, u32 rgba)
{
    rawFill(c, TX(c, x0), TY(c, y0), TX(c, x1), TY(c, y1), rgba);
}

/* the frame's thickness is scaled too, but never under one console unit, so a scaled-down frame stays visible */
void hudFrame(HudCanvas *c, s32 x0, s32 y0, s32 x1, s32 y1, s32 t, u32 rgba)
{
    const s32 ax = TX(c, x0), ay = TY(c, y0), bx = TX(c, x1), by = TY(c, y1);
    s32 ct = (s32) ((f32) t * c->s + 0.5f);

    if (ct < 1) ct = 1;
    rawFill(c, ax, ay, bx, ay + ct, rgba);
    rawFill(c, ax, by - ct, bx, by, rgba);
    rawFill(c, ax, ay + ct, ax + ct, by - ct, rgba);
    rawFill(c, bx - ct, ay + ct, bx, by - ct, rgba);
}

void hudGradientV(HudCanvas *c, s32 x0, s32 y0, s32 x1, s32 y1, u32 top, u32 bottom, s32 steps)
{
    s32 i, h = y1 - y0;

    if (steps < 1) steps = 1;
    if (steps > h) steps = h > 0 ? h : 1;
    for (i = 0; i < steps; i++)
    {
        hudFill(c, x0, y0 + h * i / steps, x1, y0 + h * (i + 1) / steps, hudLerp(top, bottom, steps > 1 ? (f32) i / (f32) (steps - 1) : 0.0f));
    }
}

void hudGradientH(HudCanvas *c, s32 x0, s32 y0, s32 x1, s32 y1, u32 left, u32 right, s32 steps)
{
    s32 i, w = x1 - x0;

    if (steps < 1) steps = 1;
    if (steps > w) steps = w > 0 ? w : 1;
    for (i = 0; i < steps; i++)
    {
        hudFill(c, x0 + w * i / steps, y0, x0 + w * (i + 1) / steps, y1, hudLerp(left, right, steps > 1 ? (f32) i / (f32) (steps - 1) : 0.0f));
    }
}

static struct fontchar *fChars(s32 font) { return (font && ptrFontZurichBoldChars) ? ptrFontZurichBoldChars : ptrFontBankGothicChars; }
static struct font *fFont(s32 font) { return (font && ptrFontZurichBold) ? ptrFontZurichBold : ptrFontBankGothic; }

s32 hudTextW(const char *t, f32 scale, s32 font)
{
    s32 h = 0, w = 0;

    if (t == NULL || t[0] == '\0' || ptrFontBankGothic == NULL)
    {
        return 0;
    }
    portTextScale(scale);
    textMeasure(&h, &w, (char *) t, fChars(font), fFont(font), 0);
    portTextScale(1.0f);
    return w;
}

s32 hudTextH(f32 scale, s32 font)
{
    return (s32) ((font ? 16.0f : 11.0f) * scale + 0.5f);
}

extern void portTextSquashX(f32 sx);   /* textrelated.c (PC): an extra x factor on the glyphs */

static void textAt(HudCanvas *c, s32 x, s32 y, const char *t, u32 rgba, f32 scale, f32 sx, s32 font)
{
    s32 tx = x, ty = y;

    if (!room(c, (s32) strlen(t) * 14 + 16) || (rgba & 0xFFu) == 0 || scale < 0.05f)
    {
        return;
    }
    if (c->texMode != 1)
    {
        gDPSetCombineLERP(c->g++, 0, 0, 0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0, 0, 0, 0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0);
        c->texMode = 1;
    }
    portTextScale(scale);
    portTextSquashX(sx);
    c->g = textRender(c->g, &tx, &ty, (char *) t, fChars(font), fFont(font), rgba, c->w, c->h, 0, 0);
    portTextSquashX(1.0f);
    portTextScale(1.0f);
    c->havePrim = 0;
}

/* text in console units (after the transform), squashed in x by sx, with its shadow / outline */
static void textFx(HudCanvas *c, s32 x, s32 y, const char *t, u32 rgba, f32 scale, f32 sx, s32 font, s32 fx, u32 dark)
{
    s32 o = (s32) (scale + 0.5f);

    if (o < 1) o = 1;
    dark = hudAlpha(dark, (f32) (rgba & 0xFFu) / 255.0f);
    if (fx & HUD_FX_OUTLINE)
    {
        static const s8 d[8][2] = { { -1, -1 }, { 0, -1 }, { 1, -1 }, { -1, 0 }, { 1, 0 }, { -1, 1 }, { 0, 1 }, { 1, 1 } };
        s32 i;

        for (i = 0; i < 8; i++)
        {
            textAt(c, x + d[i][0] * o, y + d[i][1] * o, t, dark, scale, sx, font);
        }
    }
    else if (fx & HUD_FX_SHADOW)
    {
        textAt(c, x + o, y + o, t, dark, scale, sx, font);
    }
    textAt(c, x, y, t, rgba, scale, sx, font);
}

void hudTextSq(HudCanvas *c, s32 x, s32 y, const char *t, u32 rgba, f32 scale, f32 sx, s32 font, s32 align, s32 fx, u32 dark)
{
    if (!c->ok || t == NULL || t[0] == '\0')
    {
        return;
    }
    if (align != HUD_ALIGN_LEFT)
    {   /* (the width at `scale` is in the block's units; a squash narrows it about the same anchor) */
        const s32 w = (s32) ((f32) hudTextW(t, scale, font) * sx);

        x -= align == HUD_ALIGN_CENTRE ? w / 2 : w;
    }
    textFx(c, TX(c, x), TY(c, y), t, rgba, scale * c->s, sx, font, fx, dark);
}

void hudText(HudCanvas *c, s32 x, s32 y, const char *t, u32 rgba, f32 scale, s32 font, s32 align, s32 fx, u32 dark)
{
    hudTextSq(c, x, y, t, rgba, scale, 1.0f, font, align, fx, dark);
}

/* Each letter of t drawn on its own: the line is laid out at `scale` (left / centre / right on x, top at y), then fn may move, scale,
 * squash, recolour or hide every letter (around its own centre). Spaces keep their place and are never passed to fn. */
void hudTextLetters(HudCanvas *c, s32 x, s32 y, const char *t, u32 rgba, f32 scale, s32 font, s32 align, s32 fx, u32 dark,
                    HudLetterFn fn, void *ud)
{
    char pre[64], one[2] = { 0, 0 };
    s32 i, n, total, h;

    if (!c->ok || t == NULL || t[0] == '\0')
    {
        return;
    }
    n = (s32) strlen(t);
    if (n > (s32) sizeof(pre) - 1) n = (s32) sizeof(pre) - 1;
    total = hudTextW(t, scale, font);
    if (align == HUD_ALIGN_CENTRE) x -= total / 2;
    else if (align == HUD_ALIGN_RIGHT) x -= total;
    h = hudTextH(scale, font);
    for (i = 0; i < n; i++)
    {
        HudLetter l;
        s32 lx, lw;

        if (t[i] == ' ')
        {
            continue;
        }
        memcpy(pre, t, (size_t) i);
        pre[i] = '\0';
        one[0] = t[i];
        lx = x + (i > 0 ? hudTextW(pre, scale, font) : 0);
        lw = hudTextW(one, scale, font);
        l.dx = l.dy = 0.0f;
        l.scale = 1.0f;
        l.sx = 1.0f;
        l.rgba = rgba;
        l.hide = 0;
        l.ch = t[i];
        l.ox = (f32) lx + (f32) lw * 0.5f - ((f32) x + (f32) total * 0.5f);
        if (fn != NULL)
        {
            fn(i, n, &l, ud);
        }
        if (l.hide || (l.rgba & 0xFFu) == 0 || l.scale <= 0.02f)
        {
            continue;
        }
        one[0] = l.ch;
        {   /* scale about the letter's centre */
            const f32 cx = (f32) lx + (f32) lw * 0.5f + l.dx, cy = (f32) y + (f32) h * 0.5f + l.dy;
            const f32 s = scale * l.scale;
            const f32 w2 = (f32) lw * l.scale * l.sx * 0.5f, h2 = (f32) h * l.scale * 0.5f;

            textFx(c, TX(c, (s32) (cx - w2)), TY(c, (s32) (cy - h2)), one, l.rgba, s * c->s, l.sx, font, fx, dark);
        }
    }
}

u32 hudAlpha(u32 rgba, f32 a)
{
    s32 al;

    if (a < 0.0f) a = 0.0f;
    if (a > 1.0f) a = 1.0f;
    al = (s32) ((f32) (rgba & 0xFFu) * a + 0.5f);
    return (rgba & 0xFFFFFF00u) | (u32) al;
}

u32 hudLerp(u32 a, u32 b, f32 t)
{
    u32 out = 0;
    s32 sh;

    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    for (sh = 0; sh < 32; sh += 8)
    {
        const f32 ca = (f32) ((a >> sh) & 0xFFu), cb = (f32) ((b >> sh) & 0xFFu);

        out |= ((u32) (ca + (cb - ca) * t + 0.5f) & 0xFFu) << sh;
    }
    return out;
}

f32 hudPulse(f32 secs, f32 hz, f32 amp)
{
    return 1.0f + amp * sinf(6.2831853f * hz * secs);
}

void hudDisc(HudCanvas *c, s32 cx, s32 cy, s32 r, u32 rgba)
{
    hudRing(c, cx, cy, 0, r, rgba);
}

/* r0 inner (0 = a disc), r1 outer; one or two spans a row */
void hudRing(HudCanvas *c, s32 cx, s32 cy, s32 r0, s32 r1, u32 rgba)
{
    s32 dy;

    if (r1 <= 0 || r1 <= r0)
    {
        return;
    }
    for (dy = -r1; dy < r1 && room(c, 16); dy++)
    {
        const f32 fy = (f32) dy + 0.5f;
        const s32 xo = (s32) sqrtf((f32) r1 * (f32) r1 - fy * fy);
        const s32 xi = (r0 > 0 && fy * fy < (f32) r0 * (f32) r0) ? (s32) sqrtf((f32) r0 * (f32) r0 - fy * fy) : -1;

        if (xi < 0)
        {
            hudFill(c, cx - xo, cy + dy, cx + xo, cy + dy + 1, rgba);
        }
        else
        {
            hudFill(c, cx - xo, cy + dy, cx - xi, cy + dy + 1, rgba);
            hudFill(c, cx + xi, cy + dy, cx + xo, cy + dy + 1, rgba);
        }
    }
}

u32 hudHue(f32 h, u32 alpha)
{
    f32 r, g, b, f;
    s32 i;

    h -= (f32) (s32) h;
    if (h < 0.0f) h += 1.0f;
    i = (s32) (h * 6.0f);
    f = h * 6.0f - (f32) i;
    switch (i % 6)
    {
        case 0: r = 1; g = f; b = 0; break;
        case 1: r = 1 - f; g = 1; b = 0; break;
        case 2: r = 0; g = 1; b = f; break;
        case 3: r = 0; g = 1 - f; b = 1; break;
        case 4: r = f; g = 0; b = 1; break;
        default: r = 1; g = 0; b = 1 - f; break;
    }
    /* keep it bright: mix a little white in */
    r = 0.25f + 0.75f * r; g = 0.25f + 0.75f * g; b = 0.25f + 0.75f * b;
    return ((u32) (r * 255.0f) << 24) | ((u32) (g * 255.0f) << 16) | ((u32) (b * 255.0f) << 8) | (alpha & 0xFFu);
}

f32 hudEaseOutBack(f32 t, f32 overshoot)
{
    const f32 s = 1.70158f * (overshoot / 0.1f);
    f32 u;

    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    u = t - 1.0f;
    return u * u * ((s + 1.0f) * u + s) + 1.0f;
}

f32 hudEaseOutBounce(f32 t)
{
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    if (t < 1.0f / 2.75f) return 7.5625f * t * t;
    if (t < 2.0f / 2.75f) { t -= 1.5f / 2.75f; return 7.5625f * t * t + 0.75f; }
    if (t < 2.5f / 2.75f) { t -= 2.25f / 2.75f; return 7.5625f * t * t + 0.9375f; }
    t -= 2.625f / 2.75f;
    return 7.5625f * t * t + 0.984375f;
}

f32 hudEaseOut(f32 t)
{
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
}

#endif /* !TARGET_N64 */
