/* port_crosshair.c - crosshair styles: the game half (port/include/port_crosshair.h, modding/lua.md "Crosshair styles").
 *
 * gunfire.c gunDrawSight asks portXhairDraw first. With no style, no hide and no draw hook it returns 0 at once and the game's
 * own sight (and the PD-style always_crosshair target) draws exactly as before. Otherwise the style's shape, its outline, a
 * centre dot, a hit marker and whatever the Lua draw hooks asked for go into a small draw list, which is rasterised here into
 * G_FILLRECT spans in the sight's own space (the pinned 2D frame, the wide-aim span mapping and the HUD Scale about the aim
 * point, as gunfire.c portDrawTargetPlus does). Rect corners carry their sub-pixel fractions (bondwalk2.c portRectPreciseTag).
 *
 * Shapes are rasterised as horizontal spans a quarter HUD pixel high (one rect per span), so translucent colours never
 * overlap themselves: a thick diagonal line is a convex quad cut by each row, a ring is one or two spans per row.
 *
 * Dynamic spread reads the gun's real spread (WeaponStats Inaccuracy, scaled the way gunfire.c bullet_path_from_screen_center
 * scales it, with the first-shot bonus while not firing), Bond's walk speed and his shot counter; the hit marker reads his hit
 * counters (player_data shot_count, the registers the mission stats use). All reads. PRESENTATION ONLY: nothing here is read
 * by the sim, and nothing here draws a random number or allocates from the frame pools (only display-list words). */
#ifndef TARGET_N64

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ultra64.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include "game/player.h"
#include "game/bondview.h"
#include "game/gun.h"
#include "game/lv.h"
#include "game/dyn.h"
#include "game/options.h"
#include "fr.h"
#include "../include/port_crosshair.h"

extern void portConLogf(const char *fmt, ...);
extern int portNetActive(void);
extern int portLuaGameAllowed(void);
extern int portLuaGameItemId(const char *upper);   /* port_lua.c */
extern Gfx *portRectPreciseTag(Gfx *gdl, f32 ulx, f32 uly, f32 lrx, f32 lry, s32 iulx, s32 iuly, s32 ilrx, s32 ilry);   /* bondwalk2.c */
extern f32 portAimReachX(void);            /* bondview2.c */
extern f32 g_bondviewSceneWideFactor;

/* ---- state ---------------------------------------------------------------------------------------------------------------------- */

static PortXhairStyle sBase[XHAIR_LAYERS];
static int sSet[XHAIR_LAYERS];
static int sNW[XHAIR_LAYERS];
static int sWItem[XHAIR_LAYERS][XHAIR_MAX_WEAPON];
static PortXhairStyle sW[XHAIR_LAYERS][XHAIR_MAX_WEAPON];
static int sHidden;
static int sDrawn;
static float sLastSpread;

/* per player: the hit / shot counters last seen, and when they last went up (g_GlobalTimer fields) */
static int sTrackInit[4], sHitsSeen[4], sHeadSeen[4], sShotsSeen[4];
static s32 sHitAt[4], sShotAt[4];
static int sHitHead[4], sHitEver[4];
static float sKick[4];
static int sTestHit = -1;

void portXhairDefaults(PortXhairStyle *s)
{
    memset(s, 0, sizeof(*s));
    s->shape = XHAIR_CROSS;
    s->size = 4.0f;
    s->gap = 2.0f;
    s->thick = 0.75f;
    s->color = 0xFFFFFFE6u;
    s->dotsize = 1.0f;
    s->ocolor = 0x000000A0u;
    s->othick = 0.4f;
    s->spreadk = 1.0f;
    s->movek = 3.0f;
    s->firek = 1.5f;
    s->hitcolor = 0xFFFFFFFFu;
    s->headcolor = 0xFF3030FFu;
    s->hittime = 0.25f;
    s->hitsize = 3.0f;
    s->always = 1;
}

void portXhairSet(int layer, const PortXhairStyle *base, int nweap, const int *items, const PortXhairStyle *perweapon)
{
    int i;

    if (layer < 0 || layer >= XHAIR_LAYERS || base == NULL) return;
    sBase[layer] = *base;
    if (nweap > XHAIR_MAX_WEAPON) nweap = XHAIR_MAX_WEAPON;
    sNW[layer] = nweap < 0 ? 0 : nweap;
    for (i = 0; i < sNW[layer]; i++)
    {
        sWItem[layer][i] = items[i];
        sW[layer][i] = perweapon[i];
    }
    sSet[layer] = 1;
}

void portXhairClear(int layer)
{
    if (layer >= 0 && layer < XHAIR_LAYERS)
    {
        sSet[layer] = 0;
        sNW[layer] = 0;
    }
}

int portXhairIsSet(int layer) { return layer >= 0 && layer < XHAIR_LAYERS && sSet[layer]; }
int portXhairShapeOf(int layer) { return portXhairIsSet(layer) ? sBase[layer].shape : -1; }
void portXhairHide(int on) { sHidden = on ? 1 : 0; }
int portXhairHidden(void) { return sHidden; }
float portXhairSpreadNow(void) { return sLastSpread; }
int portXhairDrawCount(void) { return sDrawn; }
void portXhairTestHit(int head) { sTestHit = head ? 1 : 0; }

void portXhairStageReset(void)
{
    int p;

    portXhairClear(XHAIR_LAYER_SCRIPT);
    sHidden = 0;
    for (p = 0; p < 4; p++) sTrackInit[p] = 0;
}

int portXhairItemId(const char *upper) { return portLuaGameItemId(upper); }

/* ---- the draw list ---------------------------------------------------------------------------------------------------------------- */

enum { CMD_RECT, CMD_LINE, CMD_CIRCLE };
typedef struct { u8 kind; f32 a, b, c, d, e; u32 rgba; } XCmd;
#define XHAIR_MAX_CMDS 256
static XCmd sCmd[XHAIR_MAX_CMDS];
static int sCmdN;
static int sCollecting;

static void cmdPush(int kind, f32 a, f32 b, f32 c, f32 d, f32 e, u32 rgba)
{
    if (!sCollecting || sCmdN >= XHAIR_MAX_CMDS || (rgba & 0xFF) == 0) return;
    if (!(a == a) || !(b == b) || !(c == c) || !(d == d) || !(e == e)) return;   /* NaN */
    sCmd[sCmdN].kind = (u8) kind;
    sCmd[sCmdN].a = a; sCmd[sCmdN].b = b; sCmd[sCmdN].c = c; sCmd[sCmdN].d = d; sCmd[sCmdN].e = e;
    sCmd[sCmdN].rgba = rgba;
    sCmdN++;
}

void portXhairCmdLine(float x0, float y0, float x1, float y1, float thick, unsigned rgba) { cmdPush(CMD_LINE, x0, y0, x1, y1, thick, rgba); }
void portXhairCmdRect(float x0, float y0, float x1, float y1, unsigned rgba) { cmdPush(CMD_RECT, x0, y0, x1, y1, 0.0f, rgba); }
void portXhairCmdCircle(float x, float y, float r, float thick, unsigned rgba) { cmdPush(CMD_CIRCLE, x, y, r, thick, 0.0f, rgba); }

/* ---- the rasteriser --------------------------------------------------------------------------------------------------------------- */

#define XHAIR_ROW 0.25f          /* span height, HUD px */
#define XHAIR_MAX_RECTS 1600
static Gfx *sG;
static Gfx *sGEnd;               /* stop before this (room for the tail) */
static int sRects;
static u32 sCol;
static f32 sAx, sAy, sDx, sXs; /* aim point in list space, its drawn x, the x scale (wide-aim span mapping) */

static void emitColour(u32 c)
{
    if (c == sCol) return;
    sCol = c;
    gDPPipeSync(sG++);
    gDPSetPrimColor(sG++, 0, 0, (c >> 24) & 0xFF, (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
}

/* a rect in list space (x is mapped through the aim point's x scale) */
static void emitRect(f32 x0, f32 y0, f32 x1, f32 y1, u32 c)
{
    f32 qa, qb, qc, qd;
    s32 ia, ib, ic, id;

    if (sRects >= XHAIR_MAX_RECTS || sG + 6 >= sGEnd) return;
    x0 = sDx + (x0 - sAx) * sXs;
    x1 = sDx + (x1 - sAx) * sXs;
    if (x1 <= x0 || y1 <= y0) return;
    qa = x0 * 4.0f; qb = y0 * 4.0f; qc = x1 * 4.0f; qd = y1 * 4.0f;
    if (qc <= 0.0f || qd <= 0.0f || qa > 4000.0f || qb > 4000.0f) return;
    if (qa < 0.0f) qa = 0.0f;
    if (qb < 0.0f) qb = 0.0f;
    ia = (s32) qa; ib = (s32) qb; ic = (s32) qc; id = (s32) qd;
    if (ic <= ia) ic = ia + 1;
    if (id <= ib) id = ib + 1;
    emitColour(c);
    sG = portRectPreciseTag(sG, qa, qb, qc, qd, ia, ib, ic, id);
    sG->words.w0 = ((u32) G_FILLRECT << 24) | (((u32) ic & 0xFFF) << 12) | ((u32) id & 0xFFF);
    sG->words.w1 = (((u32) ia & 0xFFF) << 12) | ((u32) ib & 0xFFF);
    sG++;
    sRects++;
}

/* a convex polygon, row by row */
static void rasterPoly(const f32 *px, const f32 *py, int n, u32 c)
{
    f32 ymin = py[0], ymax = py[0], y;
    int i;

    for (i = 1; i < n; i++)
    {
        if (py[i] < ymin) ymin = py[i];
        if (py[i] > ymax) ymax = py[i];
    }
    if (ymax - ymin > 200.0f) return;
    y = (f32) (s32) (ymin / XHAIR_ROW) * XHAIR_ROW;
    for (; y < ymax; y += XHAIR_ROW)
    {
        f32 yc = y + XHAIR_ROW * 0.5f, lo = 1.0e9f, hi = -1.0e9f;

        for (i = 0; i < n; i++)
        {
            int j = (i + 1) % n;
            f32 ya = py[i], yb = py[j];

            if ((yc >= ya && yc < yb) || (yc >= yb && yc < ya))
            {
                f32 x = px[i] + (px[j] - px[i]) * (yc - ya) / (yb - ya);

                if (x < lo) lo = x;
                if (x > hi) hi = x;
            }
        }
        if (hi > lo) emitRect(lo, y, hi, y + XHAIR_ROW, c);
    }
}

static void rasterLine(f32 x0, f32 y0, f32 x1, f32 y1, f32 th, u32 c)
{
    f32 dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy), nx, ny, px[4], py[4];

    if (th <= 0.0f) th = 0.5f;
    if (l < 0.001f) return;
    if (dy == 0.0f || dx == 0.0f)
    {   /* axis-aligned: one rect */
        f32 h = th * 0.5f;

        if (dy == 0.0f) emitRect(x0 < x1 ? x0 : x1, y0 - h, x0 < x1 ? x1 : x0, y0 + h, c);
        else emitRect(x0 - h, y0 < y1 ? y0 : y1, x0 + h, y0 < y1 ? y1 : y0, c);
        return;
    }
    nx = -dy / l * th * 0.5f;
    ny = dx / l * th * 0.5f;
    px[0] = x0 + nx; py[0] = y0 + ny;
    px[1] = x1 + nx; py[1] = y1 + ny;
    px[2] = x1 - nx; py[2] = y1 - ny;
    px[3] = x0 - nx; py[3] = y0 - ny;
    rasterPoly(px, py, 4, c);
}

static void rasterCircle(f32 cx, f32 cy, f32 r, f32 th, u32 c)
{
    f32 ro, ri, y;

    if (r <= 0.0f || r > 100.0f) return;
    if (th <= 0.0f || th * 0.5f >= r) { ro = r; ri = 0.0f; }   /* a disc */
    else { ro = r + th * 0.5f; ri = r - th * 0.5f; }
    for (y = cy - ro; y < cy + ro; y += XHAIR_ROW)
    {
        f32 yc = y + XHAIR_ROW * 0.5f - cy, xo, xi;

        if (yc * yc >= ro * ro) continue;
        xo = sqrtf(ro * ro - yc * yc);
        if (ri > 0.0f && yc * yc < ri * ri)
        {
            xi = sqrtf(ri * ri - yc * yc);
            emitRect(cx - xo, y, cx - xi, y + XHAIR_ROW, c);
            emitRect(cx + xi, y, cx + xo, y + XHAIR_ROW, c);
        }
        else
        {
            emitRect(cx - xo, y, cx + xo, y + XHAIR_ROW, c);
        }
    }
}

/* ---- shapes ----------------------------------------------------------------------------------------------------------------------- */

static void armRect(f32 x0, f32 y0, f32 x1, f32 y1, f32 e, u32 c) { portXhairCmdRect(x0 - e, y0 - e, x1 + e, y1 + e, c); }

/* a line extended by e at both ends and e thicker each side (the outline pass) */
static void lineE(f32 x0, f32 y0, f32 x1, f32 y1, f32 th, f32 e, u32 c)
{
    f32 dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy);

    if (l > 0.001f && e > 0.0f)
    {
        x0 -= dx / l * e; y0 -= dy / l * e;
        x1 += dx / l * e; y1 += dy / l * e;
    }
    portXhairCmdLine(x0, y0, x1, y1, th + 2.0f * e, c);
}

static void shapeBuild(const PortXhairStyle *s, f32 x, f32 y, f32 g, f32 e, u32 c)
{
    const f32 n = s->size, t = s->thick, h = t * 0.5f, k = 0.70710678f;

    switch (s->shape)
    {
    case XHAIR_DOT:
        portXhairCmdCircle(x, y, n * 0.5f + e, 0.0f, c);
        break;
    case XHAIR_CROSS:
    case XHAIR_T:
        armRect(x - g - n, y - h, x - g, y + h, e, c);
        armRect(x + g, y - h, x + g + n, y + h, e, c);
        armRect(x - h, y + g, x + h, y + g + n, e, c);
        if (s->shape == XHAIR_CROSS) armRect(x - h, y - g - n, x + h, y - g, e, c);
        break;
    case XHAIR_X:
        lineE(x - g * k, y - g * k, x - (g + n) * k, y - (g + n) * k, t, e, c);
        lineE(x + g * k, y - g * k, x + (g + n) * k, y - (g + n) * k, t, e, c);
        lineE(x - g * k, y + g * k, x - (g + n) * k, y + (g + n) * k, t, e, c);
        lineE(x + g * k, y + g * k, x + (g + n) * k, y + (g + n) * k, t, e, c);
        break;
    case XHAIR_CIRCLE:
        portXhairCmdCircle(x, y, n + (g - s->gap), t + 2.0f * e, c);
        break;
    case XHAIR_CHEVRON:
        lineE(x, y + g, x - n, y + g + n * 0.8f, t, e, c);
        lineE(x, y + g, x + n, y + g + n * 0.8f, t, e, c);
        break;
    case XHAIR_BRACKETS:
    {
        f32 r = g + n, a = n * 0.6f;
        int sx, sy;

        for (sx = -1; sx <= 1; sx += 2)
        {
            for (sy = -1; sy <= 1; sy += 2)
            {
                f32 cx = x + sx * r, cy = y + sy * r;

                armRect(sx < 0 ? cx : cx - a, cy - h, sx < 0 ? cx + a : cx, cy + h, e, c);
                armRect(cx - h, sy < 0 ? cy : cy - a, cx + h, sy < 0 ? cy + a : cy, e, c);
            }
        }
        break;
    }
    default:
        break;
    }
    if (s->dot && s->shape != XHAIR_DOT)
    {
        portXhairCmdCircle(x, y, s->dotsize * 0.5f + e, 0.0f, c);
    }
}

static u32 fade(u32 c, f32 a)
{
    s32 al = (s32) ((f32) (c & 0xFF) * (a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a)));

    return (c & 0xFFFFFF00u) | (u32) al;
}

static void hitBuild(const PortXhairStyle *s, f32 x, f32 y, f32 g, f32 since, int head)
{
    f32 a = 1.0f - since / (s->hittime > 0.01f ? s->hittime : 0.01f), r0 = (g > 2.0f ? g : 2.0f) + 1.0f, r1 = r0 + s->hitsize;
    f32 k = 0.70710678f, t = s->thick > 0.5f ? s->thick : 0.5f;
    u32 c = fade(head ? s->headcolor : s->hitcolor, a);
    int pass, sx, sy;

    if (a <= 0.0f) return;
    for (pass = s->outline ? 0 : 1; pass < 2; pass++)
    {
        f32 e = pass == 0 ? s->othick : 0.0f;

        for (sx = -1; sx <= 1; sx += 2)
        {
            for (sy = -1; sy <= 1; sy += 2)
            {
                lineE(x + sx * r0 * k, y + sy * r0 * k, x + sx * r1 * k, y + sy * r1 * k, t, e, pass == 0 ? fade(s->ocolor, a) : c);
            }
        }
    }
}

/* ---- per-frame reads -------------------------------------------------------------------------------------------------------------- */

static void track(int p)
{
    struct player_data *pd = &g_playerPlayerData[p];
    int hits = pd->shot_count[SHOT_REGISTER_HEAD] + pd->shot_count[SHOT_REGISTER_BODY] + pd->shot_count[SHOT_REGISTER_LIMB]
               + pd->shot_count[SHOT_REGISTER_GUN] + pd->shot_count[SHOT_REGISTER_HAT];
    int head = pd->shot_count[SHOT_REGISTER_HEAD], shots = pd->shot_count[SHOT_REGISTER_TOTAL];

    if (!sTrackInit[p] || hits < sHitsSeen[p] || shots < sShotsSeen[p])
    {   /* first look, or a new stage reset the counters */
        sTrackInit[p] = 1;
        sHitsSeen[p] = hits;
        sHeadSeen[p] = head;
        sShotsSeen[p] = shots;
        sHitEver[p] = 0;
        sKick[p] = 0.0f;
        sShotAt[p] = g_GlobalTimer - 600;
        return;
    }
    if (hits > sHitsSeen[p])
    {
        sHitAt[p] = g_GlobalTimer;
        sHitHead[p] = head > sHeadSeen[p];
        sHitEver[p] = 1;
    }
    if (sTestHit >= 0)
    {
        sHitAt[p] = g_GlobalTimer;
        sHitHead[p] = sTestHit;
        sHitEver[p] = 1;
        sTestHit = -1;
    }
    if (shots > sShotsSeen[p])
    {
        sShotAt[p] = g_GlobalTimer;
    }
    sHitsSeen[p] = hits;
    sHeadSeen[p] = head;
    sShotsSeen[p] = shots;
}

/* the gun's spread at the aim point, HUD px (gunfire.c bullet_path_from_screen_center: the shot lands within half of it) */
static f32 gunSpread(int weapon, f32 sinceShot)
{
    WeaponStats *ws = get_ptr_item_statistics((ITEM_IDS) weapon);
    f32 inacc = ws != NULL ? ws->Inaccuracy : 0.0f, fov = viGetFovY();

    if (bondwalkItemCheckBitflags((ITEM_IDS) weapon, WEAPONSTATBITFLAG_FIRST_SHOT_ACCURACY) && sinceShot > 0.3f)
    {
        inacc *= 0.25f;   /* the first shot of a volley is four times more accurate */
    }
    if (fov < 1.0f) fov = 60.0f;
    return 0.5f * (120.0f * inacc / fov) * getPlayer_c_screenheight() / 240.0f;
}

static const PortXhairStyle *pickStyle(int layer, int weapon)
{
    int i;

    for (i = 0; i < sNW[layer]; i++)
    {
        if (sWItem[layer][i] == weapon) return &sW[layer][i];
    }
    return &sBase[layer];
}

/* ---- the sight ---------------------------------------------------------------------------------------------------------------- */

/* gunfire.c gunDrawSight, first thing. 1 = handled (the game's sight is not drawn), 0 = draw the game's sight as usual. */
int portXhairDraw(s32 *gdlp)
{
    struct player *pl = g_CurrentPlayer;
    const int layer = sSet[XHAIR_LAYER_SCRIPT] ? XHAIR_LAYER_SCRIPT : (sSet[XHAIR_LAYER_USER] ? XHAIR_LAYER_USER : -1);
    const int luaOk = portLuaXhairHooks() && portLuaGameAllowed() && !portNetActive();
    const PortXhairStyle *s;
    PortXhairStyle classicExtras;
    int p, weapon, aiming, shapeOn, hudOn, classic, i;
    f32 now, sinceShot, sinceHit, speed, dyn, g, reachx, fx, fy;
    PortXhairFrame fr;
    Gfx *g0;

    if (sHidden) return 1;
    if (layer < 0 && !luaOk) return 0;   /* the vanilla path, untouched */
    if (pl == NULL) return 0;
    p = get_cur_playernum();
    if (p < 0 || p > 3) return 0;
    track(p);
    if (pl->mpmenuon || pl->bonddead) return layer >= 0 && portXhairShapeOf(layer) != XHAIR_CLASSIC;

    weapon = (int) getCurrentPlayerWeaponId(GUNRIGHT);
    if (layer >= 0) s = pickStyle(layer, weapon);
    else
    {   /* only draw hooks: the game's sight stays, the hooks draw over it */
        portXhairDefaults(&classicExtras);
        classicExtras.shape = XHAIR_CLASSIC;
        classicExtras.hit = 0;
        classicExtras.dynamic = 1;
        s = &classicExtras;
    }
    classic = s->shape == XHAIR_CLASSIC;
    aiming = pl->gunsightmode == 0;
    hudOn = (pl->gunsightmode & ~GUNSIGHTREASON_NOTAIMING) == 0;   /* no damage flash / cutscene / option hide */
    shapeOn = !classic && (aiming || (hudOn && s->always));

    now = (f32) g_GlobalTimer / 60.0f;
    sinceShot = (f32) (g_GlobalTimer - sShotAt[p]) / 60.0f;
    sinceHit = sHitEver[p] ? (f32) (g_GlobalTimer - sHitAt[p]) / 60.0f : -1.0f;
    (void) now;
    speed = sqrtf(pl->speedforwards * pl->speedforwards + pl->speedsideways * pl->speedsideways);
    if (speed > 1.0f) speed = 1.0f;
    dyn = 0.0f;
    if (s->dynamic)
    {
        f32 kick = sinceShot < 0.25f ? s->firek * (1.0f - sinceShot / 0.25f) : 0.0f;

        dyn = s->spreadk * gunSpread(weapon, sinceShot) + s->movek * speed + kick;
        if (dyn > 40.0f) dyn = 40.0f;
    }
    sLastSpread = dyn;
    g = s->gap + dyn;

    fx = pl->crosshair_angle.f[0];
    fy = pl->crosshair_angle.f[1];
    sCmdN = 0;
    sCollecting = 1;
    if (shapeOn && s->shape != XHAIR_NONE)
    {
        if (s->outline) shapeBuild(s, fx, fy, g, s->othick, s->ocolor);
        shapeBuild(s, fx, fy, g, 0.0f, s->color);
    }
    if (s->hit && hudOn && sinceHit >= 0.0f && sinceHit < s->hittime)
    {
        hitBuild(s, fx, fy, g, sinceHit, sHitHead[p]);
    }
    if (luaOk && hudOn)
    {
        fr.x = fx; fr.y = fy; fr.spread = dyn; fr.sincehit = sinceHit; fr.headshot = sHitHead[p];
        fr.aiming = aiming; fr.weapon = weapon; fr.player = p;
        fr.w = getPlayer_c_screenwidth() + getPlayer_c_screenleft() * 2.0f;
        fr.h = getPlayer_c_screenheight() + getPlayer_c_screentop() * 2.0f;
        portLuaXhairRun(&fr, (layer >= 0 && s == &sBase[layer] && s->hasdraw) ? layer : -1);
    }
    sCollecting = 0;

    if (sCmdN > 0)
    {
        static int logged;

        g0 = (Gfx *) *gdlp;
        sG = g0;
        sGEnd = g0 + (dynGetFreeGfx2(g0) - 64);
        if (sGEnd - sG < 200) { sCmdN = 0; return !classic; }   /* no room: draw nothing rather than overrun */
        if (sGEnd - sG > XHAIR_MAX_RECTS * 4) sGEnd = sG + XHAIR_MAX_RECTS * 4;
        reachx = portAimReachX();
        sAx = fx; sAy = fy; sDx = fx; sXs = 1.0f;
        if (reachx > 1.001f)
        {   /* wide mouse aim: the window mapping at the unpinned x (as portDrawTargetPlus) */
            PORT_HUD_ANCHOR(sG, PORT_HUD_ANCHOR_SPAN);
            sDx = bondviewAimUnpinX(fx);
            sXs = 1.0f / g_bondviewSceneWideFactor;
        }
        sG = portHudScaleBegin(sG, sDx, fy);
        gDPPipeSync(sG++);
        gDPSetCycleType(sG++, G_CYC_1CYCLE);
        gDPSetRenderMode(sG++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
        gDPSetCombineMode(sG++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
        sCol = 0;
        sRects = 0;
        sCol = ~sCmd[0].rgba;   /* force the first colour */
        for (i = 0; i < sCmdN; i++)
        {
            const XCmd *c = &sCmd[i];

            if (c->kind == CMD_RECT) emitRect(c->a < c->c ? c->a : c->c, c->b < c->d ? c->b : c->d, c->a < c->c ? c->c : c->a, c->b < c->d ? c->d : c->b, c->rgba);
            else if (c->kind == CMD_LINE) rasterLine(c->a, c->b, c->c, c->d, c->e, c->rgba);
            else rasterCircle(c->a, c->b, c->c, c->d, c->rgba);
        }
        gDPPipeSync(sG++);
        sG = portHudScaleEnd(sG);
        if (reachx > 1.001f)
        {
            PORT_HUD_ANCHOR_OFF(sG);
        }
        *gdlp = (s32) sG;
        sDrawn++;
        if (!logged)
        {
            logged = 1;
            portConLogf("ge: crosshair: custom sight drawing (shape %d, %d rects, tick %d)\n", s->shape, sRects, (int) g_GlobalTimer);
        }
    }
    return !classic;
}

#endif /* TARGET_N64 */
