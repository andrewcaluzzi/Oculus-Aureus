/* port_sniper.c - the sniper duel's C side (tools/mode_sniper.lua; the Lua glue in port/luavm/port_luavm_sniper.c; modding/sniper.md).
 *
 * What C does that the script cannot:
 *   - the zoom limits: the narrowest field of view the sniper rifle's scope and the camera (the duel's binoculars) zoom to (stock 7 degrees
 *     for both; gun.c camera_sniper_zoom_in asks here), so the scope's strength is a tunable in the mode's defs;
 *   - the view state a detection rule needs (aiming, the item in hand, the field of view, the stance);
 *   - PRONE: a third, lower stance below the stock squat (bondview2.c asks for the eye offset, the move-speed scale and the sway scale);
 *   - the overlay (presentation only, this machine's Bond): small world points (a glint: a white flare at a marksman's scope; a marker: a
 *     quiet fading diamond where he fired from; a tag: brackets on a marksman the binoculars pinned), the binoculars' lens frame (a dark
 *     mask with two round lenses while the camera is raised), and the thermal tint;
 *   - a test hook: aim mode forced on with a set zoom (test.aim), so a hidden run can look through the real scope.
 * Sim state (the zoom limits, prone) is per Bond and reset at every stage start (portSniperReset from portHeistStageReset). The overlay's
 * inputs are presentation (never hashed). PC only. */
#ifndef TARGET_N64

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ultra64.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include <math.h>
#include "game/chr.h"
#include "game/player.h"
#include "game/bondview.h"
#include "game/gun.h"
#include "game/dyn.h"
#include "game/textrelated.h"
#include "fr.h"
#include "port_mode.h"

#define SN_MAX_P   4
#define SN_POINTS 24

extern int portLocalPlayer(void);
extern Mtxf *camGetWorldToScreenMtxf(void);
extern Mtxf *currentPlayerGetProjectionMatrixF(void);
extern void portConsoleSpace(s32 *w, s32 *h);
extern s16 bondviewGetCurrentPlayerViewportWidth(void);
extern s16 bondviewGetCurrentPlayerViewportHeight(void);
extern s16 bondviewGetCurrentPlayerViewportUly(void);
extern s16 get_curplayer_viewport_ulx(void);
extern s16 getWidth320or440(void);
extern s16 getHeight330or240(void);
extern f32 viGetFovY(void);

/* ---------------------------------------------------------------- sim ---- */
static f32 sMinFovRifle = 7.0f, sMinFovCamera = 7.0f;
static s32 sForceAim[SN_MAX_P];
static f32 sForceFov[SN_MAX_P];
static s32 sProne[SN_MAX_P];
static f32 sProneEye = -135.0f, sProneSpeed = 0.25f, sProneSway = 0.25f;
static s32 sNoUnarmed;
static s32 sAiHit;
static f32 sHitFrac;

/* chraction.c: an AI shot is landing on a Bond (around its record_damage_kills call) */
void portSniperAiHit(s32 on) { sAiHit = on; }

/* bondview2.c record_damage_kills: the duel's exact hit (sniper.bond_hit): an AI shot on Bond takes `frac` of a health bar, whatever
 * part it struck (the stock damage varies with the body part and more), so N hits always kill and N - 1 never do */
f32 portSniperBondHitDamage(f32 dmg, f32 actual_health)
{
    return (sAiHit && sHitFrac > 0.0f && dmg > 0.0f) ? sHitFrac * actual_health : dmg;
}

void portSniperSetBondHit(float frac) { sHitFrac = frac < 0.0f ? 0.0f : frac; }

/* gun.c advance / backstep_through_inventory: the weapon cycle steps over the bare hands (the duel's loadout is the rifle and the
 * binoculars; the hands stay in the inventory as the stock fallback when every gun is empty) */
s32 portSniperNoUnarmed(void) { return sNoUnarmed; }
void portSniperSetNoUnarmed(int on) { sNoUnarmed = on; }

/* overlay (presentation) */
typedef struct { s32 on, kind; f32 x, y, z, alpha, size; } SnPoint;   /* kind 1 glint, 2 marker, 3 tag */
static SnPoint sPts[SN_POINTS];
static s32 sBinoc, sThermal;
static f32 sThermalDim = 0.55f;

void portSniperReset(void)
{
    sMinFovRifle = 7.0f;
    sMinFovCamera = 7.0f;
    memset(sForceAim, 0, sizeof(sForceAim));
    memset(sForceFov, 0, sizeof(sForceFov));
    memset(sProne, 0, sizeof(sProne));
    sNoUnarmed = 0;
    sAiHit = 0;
    sHitFrac = 0.0f;
    memset(sPts, 0, sizeof(sPts));
    sBinoc = 0;
    sThermal = 0;
}

/* gun.c camera_sniper_zoom_in: the narrowest field of view the item zooms to */
f32 portSniperMinFov(s32 item)
{
    return item == ITEM_CAMERA ? sMinFovCamera : sMinFovRifle;
}

int portSniperSetMinFov(const char *item, float fov)
{
    if (fov < 0.5f) fov = 0.5f;
    if (fov > 60.0f) fov = 60.0f;
    if (strcmp(item, "sniperrifle") == 0) { sMinFovRifle = fov; return 1; }
    if (strcmp(item, "camera") == 0) { sMinFovCamera = fov; return 1; }
    return 0;
}

/* test.aim(p, on, fov): bondview2.c, each Bond's tick after his input: aim mode on, the zoom item's zoom set (0 = off) */
void portSniperTestAim(int p, int on, float fov)
{
    if (p < 0 || p >= SN_MAX_P) return;
    sForceAim[p] = on;
    sForceFov[p] = fov;
}

/* bondview2.c: returns 1 (and the field of view to force, 0 = keep) when aim mode is forced for the current Bond */
s32 portSniperForcedAim(s32 p, f32 *fov)
{
    if (p < 0 || p >= SN_MAX_P || !sForceAim[p]) return 0;
    *fov = sForceFov[p];
    return 1;
}

/* the view: aiming, the item in his right hand, the field of view now, the stance (0 stand, 1 crouch, 2 squat, 3 prone) */
int portSniperView(int p, int *aiming, int *item, float *fov, int *stance)
{
    struct player *pl;

    if (p < 0 || p >= SN_MAX_P || g_playerPointers[p] == NULL) return 0;
    pl = g_playerPointers[p];
    *aiming = pl->insightaimmode != 0;
    *item = pl->hands[GUNRIGHT].weaponnum;
    *fov = pl->insightaimmode ? (*item == ITEM_CAMERA ? pl->camera_zoom : (*item == ITEM_SNIPERRIFLE ? pl->sniper_zoom : 60.0f)) : 60.0f;
    *stance = sProne[p] ? 3 : (pl->crouchpos == CROUCH_SQUAT ? 2 : (pl->crouchpos == CROUCH_HALF ? 1 : 0));
    return 1;
}

/* prone: on / off, and the numbers (eye offset in units below standing, move-speed and sway scales) */
void portSniperSetProne(int p, int on)
{
    if (p >= 0 && p < SN_MAX_P) sProne[p] = on;
}

int portSniperProne(int p)
{
    return (p >= 0 && p < SN_MAX_P) ? sProne[p] : 0;
}

void portSniperProneTune(float eye, float speed, float sway)
{
    if (eye < -160.0f) eye = -160.0f;
    if (eye > -100.0f) eye = -100.0f;
    sProneEye = eye;
    sProneSpeed = speed < 0.05f ? 0.05f : (speed > 1.0f ? 1.0f : speed);
    sProneSway = sway < 0.0f ? 0.0f : (sway > 1.0f ? 1.0f : sway);
}

/* bondview2.c: the current Bond's crouch target offset (prone overrides the squat's), his move scale, his sway scale */
f32 portSniperEyeOffset(s32 p, f32 stock)
{
    return (p >= 0 && p < SN_MAX_P && sProne[p]) ? sProneEye : stock;
}

f32 portSniperMoveScale(s32 p)
{
    return (p >= 0 && p < SN_MAX_P && sProne[p]) ? sProneSpeed : 1.0f;
}

f32 portSniperSwayScale(s32 p)
{
    return (p >= 0 && p < SN_MAX_P && sProne[p]) ? sProneSway : 1.0f;
}

/* -------------------------------------------------------- the overlay ---- */
int portSniperPoint(int id, int kind, float x, float y, float z, float alpha, float size)
{
    if (id < 0 || id >= SN_POINTS) return 0;
    sPts[id].on = kind > 0 && alpha > 0.0f;
    sPts[id].kind = kind;
    sPts[id].x = x;
    sPts[id].y = y;
    sPts[id].z = z;
    sPts[id].alpha = alpha > 1.0f ? 1.0f : alpha;
    sPts[id].size = size;
    return 1;
}

void portSniperClearPoints(void) { memset(sPts, 0, sizeof(sPts)); }
void portSniperBinoculars(int on) { sBinoc = on; }
void portSniperThermal(int on, float dim) { sThermal = on; sThermalDim = dim < 0.0f ? 0.0f : (dim > 0.9f ? 0.9f : dim); }

static s32 sOvW, sOvH;

static int project(const f32 *w, f32 *sx, f32 *sy)
{
    Mtxf *cam = camGetWorldToScreenMtxf(), *proj = currentPlayerGetProjectionMatrixF();
    f32 v[4], c[4];
    int i, j;
    f32 vx, vy, vw, vh, fw, fh;

    if (cam == NULL || proj == NULL) return 0;
    for (j = 0; j < 4; j++) v[j] = w[0] * cam->m[0][j] + w[1] * cam->m[1][j] + w[2] * cam->m[2][j] + cam->m[3][j];
    for (j = 0; j < 4; j++)
    {
        c[j] = 0.0f;
        for (i = 0; i < 4; i++) c[j] += v[i] * proj->m[i][j];
    }
    if (c[3] <= 1.0f) return 0;
    fw = (f32) getWidth320or440();
    fh = (f32) getHeight330or240();
    vx = (f32) get_curplayer_viewport_ulx() / fw * (f32) sOvW;
    vy = (f32) bondviewGetCurrentPlayerViewportUly() / fh * (f32) sOvH;
    vw = (f32) bondviewGetCurrentPlayerViewportWidth() / fw * (f32) sOvW;
    vh = (f32) bondviewGetCurrentPlayerViewportHeight() / fh * (f32) sOvH;
    *sx = vx + (c[0] / c[3] + 1.0f) * 0.5f * vw;
    *sy = vy + (1.0f - c[1] / c[3]) * 0.5f * vh;
    return 1;
}

static u32 sLastCol;

static Gfx *rect(Gfx *gdl, s32 x0, s32 y0, s32 x1, s32 y1, u32 rgba)
{
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > sOvW) x1 = sOvW;
    if (y1 > sOvH) y1 = sOvH;
    if (x1 <= x0 || y1 <= y0 || (rgba & 0xFF) == 0) return gdl;
    if (rgba != sLastCol)
    {
        gDPSetPrimColor(gdl++, 0, 0, (rgba >> 24) & 0xFF, (rgba >> 16) & 0xFF, (rgba >> 8) & 0xFF, rgba & 0xFF);
        sLastCol = rgba;
    }
    gDPFillRectangle(gdl++, x0, y0, x1, y1);
    return gdl;
}

static u32 withAlpha(u32 rgb, f32 a)
{
    s32 v = (s32) (a * 255.0f);

    if (v < 0) v = 0;
    if (v > 255) v = 255;
    return (rgb & 0xFFFFFF00u) | (u32) v;
}

#define OV_SPACE(gdl, w, h) do { (gdl)->words.w0 = (u32) 0x4c << 24 /* G_CONSOLE2D_EXT */; \
                                 (gdl)->words.w1 = ((u32) (w) << 16) | (u32) (h); (gdl)++; } while (0)

/* the binoculars' lens frame: two round lenses side by side, everything else dark (scanline bands) */
static Gfx *binocMask(Gfx *gdl)
{
    s32 cy = sOvH / 2, r = (s32) ((f32) sOvH * 0.40f), gap = r / 6;
    s32 cxl = sOvW / 2 - r + gap, cxr = sOvW / 2 + r - gap;
    s32 y, band = 2;
    u32 col = 0x050608F4u;

    if (cxl - r < 0) { r = sOvW / 4; cxl = sOvW / 2 - r + gap; cxr = sOvW / 2 + r - gap; }
    gdl = rect(gdl, 0, 0, sOvW, cy - r, col);
    gdl = rect(gdl, 0, cy + r, sOvW, sOvH, col);
    for (y = cy - r; y < cy + r; y += band)
    {
        f32 dy = (f32) (y + band / 2 - cy);
        f32 h = (f32) (r * r) - dy * dy;
        s32 hw = h > 0.0f ? (s32) sqrtf(h) : 0;
        s32 l0 = cxl - hw, l1 = cxl + hw, r0 = cxr - hw, r1 = cxr + hw;

        gdl = rect(gdl, 0, y, l0, y + band, col);
        if (r0 > l1) gdl = rect(gdl, l1, y, r0, y + band, col);
        else l1 = r0;
        gdl = rect(gdl, r1, y, sOvW, y + band, col);
        /* a thin rim on the lens edge */
        if (hw > 2)
        {
            gdl = rect(gdl, l0, y, l0 + 2, y + band, 0x202428E0u);
            gdl = rect(gdl, r1 - 2, y, r1, y + band, 0x202428E0u);
        }
    }
    /* a faint reticle: the centre tick marks */
    gdl = rect(gdl, sOvW / 2 - 10, cy, sOvW / 2 - 3, cy + 1, 0xC8D0D060u);
    gdl = rect(gdl, sOvW / 2 + 3, cy, sOvW / 2 + 10, cy + 1, 0xC8D0D060u);
    gdl = rect(gdl, sOvW / 2, cy - 10, sOvW / 2 + 1, cy - 3, 0xC8D0D060u);
    gdl = rect(gdl, sOvW / 2, cy + 3, sOvW / 2 + 1, cy + 10, 0xC8D0D060u);
    return gdl;
}

static Gfx *drawPoint(Gfx *gdl, const SnPoint *p, s32 x, s32 y)
{
    f32 a = p->alpha;
    s32 s = (s32) (p->size > 0.0f ? p->size : 3.0f), k;

    if (p->kind == 1)
    {   /* a glint: a white core, a soft halo and four short rays */
        s32 ray = s * 3;

        gdl = rect(gdl, x - s - 2, y - s - 2, x + s + 3, y + s + 3, withAlpha(0xFFFFF0FFu, a * 0.25f));
        gdl = rect(gdl, x - s, y - s, x + s + 1, y + s + 1, withAlpha(0xFFFFFFFFu, a));
        gdl = rect(gdl, x - ray, y, x + ray + 1, y + 1, withAlpha(0xFFFFFFFFu, a * 0.7f));
        gdl = rect(gdl, x, y - ray, x + 1, y + ray + 1, withAlpha(0xFFFFFFFFu, a * 0.7f));
    }
    else if (p->kind == 2)
    {   /* a marker: a small hollow diamond, quiet grey-white */
        for (k = 0; k <= s + 2; k++)
        {
            s32 d = s + 2 - k;

            gdl = rect(gdl, x - d, y - k, x - d + 1, y - k + 1, withAlpha(0xE8ECF0FFu, a));
            gdl = rect(gdl, x + d, y - k, x + d + 1, y - k + 1, withAlpha(0xE8ECF0FFu, a));
            gdl = rect(gdl, x - d, y + k, x - d + 1, y + k + 1, withAlpha(0xE8ECF0FFu, a));
            gdl = rect(gdl, x + d, y + k, x + d + 1, y + k + 1, withAlpha(0xE8ECF0FFu, a));
        }
    }
    else if (p->kind == 3)
    {   /* a tag: four bracket corners, pale amber */
        s32 b = s + 6, l = 4;
        u32 c = withAlpha(0xF0D890FFu, a);

        gdl = rect(gdl, x - b, y - b, x - b + l, y - b + 1, c);
        gdl = rect(gdl, x - b, y - b, x - b + 1, y - b + l, c);
        gdl = rect(gdl, x + b - l + 1, y - b, x + b + 1, y - b + 1, c);
        gdl = rect(gdl, x + b, y - b, x + b + 1, y - b + l, c);
        gdl = rect(gdl, x - b, y + b, x - b + l, y + b + 1, c);
        gdl = rect(gdl, x - b, y + b - l + 1, x - b + 1, y + b + 1, c);
        gdl = rect(gdl, x + b - l + 1, y + b, x + b + 1, y + b + 1, c);
        gdl = rect(gdl, x + b, y + b - l + 1, x + b + 1, y + b + 1, c);
    }
    return gdl;
}

/* port_console.c portConRender, in a mode: under the HUD lines */
Gfx *portSniperOverlay(Gfx *gdl)
{
    int me = portLocalPlayer(), i, any = 0, binoc = 0;
    struct player *pl;
    s16 savedX, savedY;

    if (me < 0 || me >= SN_MAX_P || !portModeActive() || g_playerPointers[me] == NULL) return gdl;
    pl = g_playerPointers[me];
    for (i = 0; i < SN_POINTS; i++) any |= sPts[i].on;
    binoc = sBinoc && pl->insightaimmode && pl->hands[GUNRIGHT].weaponnum == ITEM_CAMERA && !pl->bonddead;
    if (!any && !binoc && !sThermal) return gdl;
    if (dynGetFreeGfx2(gdl) < 3000) return gdl;
    portConsoleSpace(&sOvW, &sOvH);
    savedX = viGetX();
    savedY = viGetY();
    viSetXY((s16) sOvW, (s16) sOvH);
    OV_SPACE(gdl, sOvW, sOvH);
    gdl = microcode_constructor(gdl);
    gDPSetScissor(gdl++, G_SC_NON_INTERLACE, 0, 0, sOvW, sOvH);
    gDPSetRenderMode(gdl++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetCombineMode(gdl++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    sLastCol = 0;
    if (sThermal) gdl = rect(gdl, 0, 0, sOvW, sOvH, withAlpha(0x06101CFFu, sThermalDim));
    for (i = 0; i < SN_POINTS; i++)
    {
        f32 w[3], sx, sy;

        if (!sPts[i].on) continue;
        w[0] = sPts[i].x;
        w[1] = sPts[i].y;
        w[2] = sPts[i].z;
        if (project(w, &sx, &sy)) gdl = drawPoint(gdl, &sPts[i], (s32) sx, (s32) sy);
    }
    if (binoc) gdl = binocMask(gdl);
    gdl = combiner_bayer_lod_perspective(gdl);
    OV_SPACE(gdl, 0, 0);
    viSetXY(savedX, savedY);
    return gdl;
}

#endif /* !TARGET_N64 */
