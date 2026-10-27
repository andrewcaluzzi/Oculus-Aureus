/**
 * port_arenamenu_draw.c - the arena's START MENU and POST-GAME, drawn over the live Colosseum (the lobby, tools/mode_arena_menu.lua)
 * with the shared HUD helpers (port_hudstyle.c: the arena HUD's plates, gold rules, Bank Gothic / Zurich Bold with shadows and
 * outlines). No new art; the fonts are the game's own.
 *
 * PRESENTATION ONLY (snap_embed.py: PRES). The lobby script owns the menu (the cursor, the values, the pages are its sim state, driven
 * by the crew's synchronized buttons through plan.input); every change it pushes the whole view here (amenu.view{...}). This file only
 * draws it, animates it (on g_GlobalTimer stamps taken when a push changes something, so a re-simulated push changes nothing) and
 * hit-tests the mouse: a hover or a click becomes a session command (PORT_NETCMD_PLAN_POINT, a = screen * 64 + item, b = 1 select,
 * 2 back, 4 / 5 the wheel, 6 hover), which reaches the script through plan.pointer like the heist planning board's clicks.
 */
#ifndef TARGET_N64

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ultra64.h>

#include "port_hudstyle.h"
#include "port_arenamenu.h"
#include "port_netmsg.h"

extern s32 g_GlobalTimer;
extern int portModeActive(void);
extern int portLocalPlayer(void);
extern void portConsoleSpace(s32 *w, s32 *h);
extern int portSessionCmdRun(int kind, int player, int a, int b);

#define INK    0xF4EEDCFFu   /* parchment white (the arena HUD's) */
#define GOLD   0xF0C040FFu
#define GOLDDK 0x8A6A28FFu
#define DIM    0xB8B0A0FFu
#define SHADE  0x000000FFu
#define RULE   0x6A5838A0u
#define RED    0xE04A30FFu

static const u32 kMedal[5] = { 0x9A9080FFu, 0xD08A48FFu, 0xD4D8E0FFu, 0xF0C040FFu, 0xA8ECF8FFu };   /* none, bronze .. platinum */

extern int g_portCoopEpoch;   /* port_coop.c: counts stage starts */

static AmView sV;
static s32 sOn, sEpoch = -1000;
static s32 sScreenTick = -100000, sCursorTick = -100000, sPrevScreen = -1, sPrevCursor = -1;
static s32 sValTick[AMV_ITEMS];
static char sPrevVal[AMV_ITEMS][40];

/* the mouse (this machine's) */
typedef struct { s32 x0, y0, x1, y1, ok; } Rect;
static Rect sHit[AMV_ITEMS];
static s32 sHitScreen;
static s32 sHover = -1, sHoverSent = -1;
static f32 sPtrX = -1000.0f, sPtrY = -1000.0f;
static s32 sPtrSeen, sPtrMoveTick = -100000;
static u32 sPrevMB;

/* ---- the view, from the Lua binding ------------------------------------------------------------------------------------------- */

void portArenaMenuViewSet(const AmView *v)
{
    const s32 now = (s32) g_GlobalTimer;
    s32 i;

    if (v->screen != sPrevScreen || sEpoch != g_portCoopEpoch)
    {   /* a new page, or the first push of a new stage (the clock and the last stage's view mean nothing now) */
        sEpoch = g_portCoopEpoch;
        sPtrMoveTick = -100000;
        sPtrSeen = 0;
        sPrevCursor = -1;
        sScreenTick = now;
        sPrevScreen = v->screen;
        sHover = sHoverSent = -1;
        memset(sPrevVal, 0, sizeof(sPrevVal));
        memset(sHit, 0, sizeof(sHit));
    }
    else
    {
        for (i = 0; i < v->n && i < AMV_ITEMS; i++)
        {
            if (v->item[i].kind == AMI_CHOICE && strcmp(sPrevVal[i], v->item[i].value) != 0 && sPrevVal[i][0] != '\0') sValTick[i] = now;
        }
    }
    if (v->cursor != sPrevCursor)
    {
        sCursorTick = now;
        sPrevCursor = v->cursor;
    }
    for (i = 0; i < v->n && i < AMV_ITEMS; i++) snprintf(sPrevVal[i], sizeof(sPrevVal[i]), "%s", v->item[i].value);
    sV = *v;
    if (sV.n > AMV_ITEMS) sV.n = AMV_ITEMS;
    if (sV.ninfo > AMV_INFO) sV.ninfo = AMV_INFO;
    if (sV.nbars > AMV_BARS) sV.nbars = AMV_BARS;
    sOn = sV.screen != AMV_OFF;
}

/* the menu is up: only in the stage whose script pushed it (AC's playtest: the lobby's view outlived the stage load into the run, where it
 * kept the mouse - a cursor, clicks eaten, no fire - while drawing invisibly, the new stage's clock being behind its stamps) */
int portArenaMenuViewOn(void) { return sOn && sEpoch == g_portCoopEpoch && portModeActive(); }

/* amenu.view{screen = "off"} and every stage end: the menu lets go of the mouse at once */
void portArenaMenuViewOff(void)
{
    sOn = 0;
    sHover = sHoverSent = -1;
    sPrevMB = 0;
}

/* ---- drawing ------------------------------------------------------------------------------------------------------------------- */

static f32 sA;          /* the screen's fade-in */
static f32 sNow;        /* seconds (sim ticks / 60) */

static f32 since(s32 tick) { return (f32) ((s32) g_GlobalTimer - tick) / 60.0f; }
static u32 A(u32 c) { return hudAlpha(c, sA); }
static f32 clamp01(f32 t) { return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t); }

static void amText(HudCanvas *c, s32 x, s32 y, const char *t, u32 col, f32 sc, s32 font, s32 align, s32 fx)
{
    hudText(c, x, y, t, A(col), sc, font, align, fx, SHADE);
}

/* text shrunk to fit `room` (never under `minsc`) */
static void amTextFit(HudCanvas *c, s32 x, s32 y, const char *t, u32 col, f32 sc, s32 font, s32 align, s32 fx, s32 room, f32 minsc)
{
    const s32 w = hudTextW(t, sc, font);

    if (w > room && w > 0)
    {
        f32 s2 = sc * (f32) room / (f32) w;

        if (s2 < minsc) s2 = minsc;
        y += (s32) ((sc - s2) * (font ? 8.0f : 5.0f));
        sc = s2;
    }
    amText(c, x, y, t, col, sc, font, align, fx);
}

static void amPlate(HudCanvas *c, s32 x0, s32 y0, s32 x1, s32 y1, u32 left, u32 right)
{
    hudGradientH(c, x0, y0, x1, y1, A(left), A(right), 12);
}

static void backdrop(HudCanvas *c, s32 screen)
{
    const s32 w = c->w, h = c->h;

    if (screen == AMV_MAIN)
    {   /* the left two thirds dark for the lists, the stands showing on the right */
        hudGradientH(c, 0, 0, w * 62 / 100, h, A(0x070503E6u), A(0x07050330u), 64);
        hudFill(c, w * 62 / 100, 0, w, h, A(0x07050330u));
    }
    else if (screen == AMV_ADVANCED)
    {
        hudFill(c, 0, 0, w, h, A(0x070503C8u));
    }
    else
    {
        const u32 tint = (sV.bannerRgba & 0xFFFFFF00u) | 0x50u;

        hudFill(c, 0, 0, w, h, A(0x060403B8u));
        hudGradientV(c, 0, 0, w, h * 35 / 100, A(tint), A(tint & 0xFFFFFF00u), 16);
    }
    hudGradientV(c, 0, 0, w, 26, A(0x000000B0u), A(0x00000000u), 6);
    hudGradientV(c, 0, h - 60, w, h, A(0x00000000u), A(0x000000C8u), 8);
}

static void amTitle(HudCanvas *c, s32 ox, s32 oy)
{
    const f32 t = clamp01(since(sScreenTick) / 0.35f), e = hudEaseOut(t);
    const s32 dx = (s32) ((1.0f - e) * -24.0f);

    amText(c, ox + 24 + dx, oy + 22, sV.kicker, DIM, 0.8f, HUD_FONT_BANK, HUD_ALIGN_LEFT, HUD_FX_SHADOW);
    amText(c, ox + 22 + dx, oy + 36, sV.title, GOLD, 2.4f, HUD_FONT_ZURICH, HUD_ALIGN_LEFT, HUD_FX_OUTLINE);
    hudGradientH(c, ox + 22, oy + 78, ox + 22 + (s32) (330.0f * e), oy + 80, A(GOLD), A(GOLD & 0xFFFFFF00u), 12);
    if (sV.subtitle[0]) amText(c, ox + 24 + dx, oy + 84, sV.subtitle, INK, 0.8f, HUD_FONT_BANK, HUD_ALIGN_LEFT, HUD_FX_SHADOW);
}

/* one item in its rect */
static void drawItem(HudCanvas *c, s32 i, const Rect *r)
{
    const AmItem *it = &sV.item[i];
    const int sel = (i + 1 == sV.cursor), hov = (i == sHover) && !sel;
    const f32 cs = since(sCursorTick), pulse = 0.5f + 0.5f * (hudPulse(sNow, 1.2f, 1.0f) - 1.0f);
    const f32 slide = sel ? hudEaseOut(clamp01(cs / 0.18f)) : 0.0f;
    const f32 dimA = it->dim ? 0.45f : 1.0f;
    const s32 x0 = r->x0 + (s32) (slide * 6.0f), y0 = r->y0, x1 = r->x1, y1 = r->y1, hh = y1 - y0;
    const f32 saved = sA;

    sA *= dimA;
    switch (it->kind)
    {
    case AMI_HEADER:
        amText(c, x0, y0 + 5, it->label, GOLD, 0.72f, HUD_FONT_BANK, HUD_ALIGN_LEFT, HUD_FX_SHADOW);
        {
            const s32 tw = hudTextW(it->label, 0.72f, HUD_FONT_BANK);

            hudGradientH(c, x0 + tw + 8, y0 + 10, x1, y0 + 11, A(RULE), A(RULE & 0xFFFFFF00u), 8);
        }
        break;
    case AMI_PRESET:
        hudFill(c, x0 + 2, y0 + 2, x1 + 2, y1 + 2, A(0x00000050u));
        amPlate(c, x0, y0, x1, y1, sel ? 0x4A3412F0u : (hov ? 0x2A1E0EE0u : 0x140E08D0u), sel ? 0x4A341270u : 0x140E0858u);
        hudFill(c, x0, y0, x0 + 3, y1, A(sel ? GOLD : (it->chosen ? GOLDDK : 0x3A3020C0u)));
        if (sel) hudFrame(c, x0, y0, x1, y1, 1, A(hudLerp(0x8A6A28FFu, 0xFFE070FFu, pulse)));
        else hudFrame(c, x0, y0, x1, y1, 1, A(0x3A302080u));
        amTextFit(c, x0 + 12, y0 + 2, it->label, sel ? GOLD : INK, 0.95f, HUD_FONT_ZURICH, HUD_ALIGN_LEFT, HUD_FX_SHADOW, x1 - x0 - 120, 0.7f);
        amTextFit(c, x0 + 12, y0 + hh - 11, it->sub, DIM, 0.62f, HUD_FONT_BANK, HUD_ALIGN_LEFT, HUD_FX_SHADOW, x1 - x0 - 20, 0.5f);
        if (it->right[0])
        {   /* the best: a medal-coloured chip and the line */
            const u32 mc = kMedal[it->medal < 0 || it->medal > 4 ? 0 : it->medal];
            const s32 tw = hudTextW(it->right, 0.7f, HUD_FONT_BANK);

            if (it->medal > 0) hudFill(c, x1 - 16 - tw, y0 + 7, x1 - 11 - tw, y0 + 12, A(mc));
            amText(c, x1 - 8, y0 + 5, it->right, it->medal > 0 ? mc : DIM, 0.7f, HUD_FONT_BANK, HUD_ALIGN_RIGHT, HUD_FX_SHADOW);
        }
        if (it->chosen)
        {   /* the preset in force: a gold pip at the right edge */
            hudFill(c, x1 - 7, y1 - 9, x1 - 3, y1 - 5, A(GOLD));
        }
        break;
    case AMI_CHOICE:
        {
            const f32 vt = since(sValTick[i]);
            const f32 flash = (vt >= 0.0f && vt < 0.4f) ? 1.0f - vt / 0.4f : 0.0f;
            const s32 ty = y0 + (hh - 10) / 2;

            if (sel) amPlate(c, x0, y0, x1, y1, 0x4A3412E0u, 0x4A341240u);
            else if (hov) amPlate(c, x0, y0, x1, y1, 0x2A1E0EC0u, 0x2A1E0E30u);
            if (sel) hudFill(c, x0, y0, x0 + 2, y1, A(GOLD));
            amText(c, x0 + 8, ty, it->label, sel ? INK : DIM, 0.8f, HUD_FONT_BANK, HUD_ALIGN_LEFT, HUD_FX_SHADOW);
            if (sel)
            {
                const s32 vw = hudTextW(it->value, 0.85f, HUD_FONT_BANK);
                const s32 nudge = (s32) (2.0f * (hudPulse(sNow, 2.0f, 1.0f) - 1.0f));

                amText(c, x1 - 8 + nudge, ty, ">", GOLD, 0.85f, HUD_FONT_BANK, HUD_ALIGN_RIGHT, HUD_FX_SHADOW);
                amText(c, x1 - 20, ty, it->value, hudLerp(GOLD, 0xFFFFFFFFu, flash), 0.85f, HUD_FONT_BANK, HUD_ALIGN_RIGHT, HUD_FX_SHADOW);
                amText(c, x1 - 32 - vw - nudge, ty, "<", GOLD, 0.85f, HUD_FONT_BANK, HUD_ALIGN_RIGHT, HUD_FX_SHADOW);
            }
            else
            {
                amText(c, x1 - 8, ty, it->value, hudLerp(INK, GOLD, flash), 0.85f, HUD_FONT_BANK, HUD_ALIGN_RIGHT, HUD_FX_SHADOW);
            }
        }
        break;
    case AMI_BUTTON:
        hudFill(c, x0 + 2, y0 + 2, x1 + 2, y1 + 2, A(0x00000050u));
        amPlate(c, x0, y0, x1, y1, sel ? 0x4A3412F0u : (hov ? 0x2A1E0EE0u : 0x140E08D0u), sel ? 0x4A3412A0u : 0x140E0890u);
        hudFrame(c, x0, y0, x1, y1, 1, A(sel ? hudLerp(0x8A6A28FFu, 0xFFE070FFu, pulse) : RULE));
        amTextFit(c, (x0 + x1) / 2, y0 + (hh - 14) / 2, it->label, sel ? GOLD : INK, 0.9f, HUD_FONT_ZURICH, HUD_ALIGN_CENTRE, HUD_FX_SHADOW,
                x1 - x0 - 12, 0.6f);
        break;
    case AMI_PRIMARY:
        {
            const f32 glow = sel ? pulse : 0.0f;

            hudFill(c, x0 + 3, y0 + 3, x1 + 3, y1 + 3, A(0x00000070u));
            hudGradientV(c, x0, y0, x1, y1, A(hudLerp(0xD8A838FFu, 0xFFE070FFu, glow)), A(hudLerp(0x8A6420FFu, 0xB08A30FFu, glow)), 10);
            hudFrame(c, x0, y0, x1, y1, sel ? 2 : 1, A(sel ? 0xFFF4C0FFu : 0x3A2810FFu));
            amTextFit(c, (x0 + x1) / 2, y0 + (hh - 22) / 2, it->label, 0x1A1006FFu, 1.35f, HUD_FONT_ZURICH, HUD_ALIGN_CENTRE, 0,
                    x1 - x0 - 16, 0.7f);
            if (sel && it->sub[0]) amText(c, (x0 + x1) / 2, y1 + 4, it->sub, DIM, 0.66f, HUD_FONT_BANK, HUD_ALIGN_CENTRE, HUD_FX_SHADOW);
        }
        break;
    }
    if (hov && it->kind != AMI_HEADER) hudFrame(c, r->x0 - 1, y0 - 1, x1 + 1, y1 + 1, 1, A(0xFFC02090u));
    sA = saved;
}

static s32 itemH(s32 screen, s32 kind)
{
    if (kind == AMI_HEADER) return 18;
    if (kind == AMI_PRESET) return 30;
    if (kind == AMI_CHOICE) return screen == AMV_ADVANCED ? 16 : 22;
    if (kind == AMI_PRIMARY) return 36;
    return 22;
}

/* the info card (main: the run's settings and bests) */
static s32 infoCard(HudCanvas *c, s32 x0, s32 y0, s32 x1)
{
    s32 y = y0, i;
    const s32 h = 46 + sV.ninfo * 15 + 8;

    hudFill(c, x0 + 3, y0 + 3, x1 + 3, y0 + h + 3, A(0x00000060u));
    amPlate(c, x0, y0, x1, y0 + h, 0x140E08E0u, 0x140E08A8u);
    hudFill(c, x0, y0, x0 + 3, y0 + h, A(GOLD));
    hudFrame(c, x0, y0, x1, y0 + h, 1, A(RULE));
    amText(c, x0 + 12, y + 6, sV.infoTitle, DIM, 0.72f, HUD_FONT_BANK, HUD_ALIGN_LEFT, HUD_FX_SHADOW);
    amTextFit(c, x0 + 12, y + 18, sV.infoSub, GOLD, 1.3f, HUD_FONT_ZURICH, HUD_ALIGN_LEFT, HUD_FX_OUTLINE, x1 - x0 - 24, 0.5f);
    y += 46;
    for (i = 0; i < sV.ninfo; i++)
    {
        const AmInfo *f = &sV.info[i];

        if (f->label[0] == '\0' && f->value[0] == '\0') { y += 6; continue; }
        amText(c, x0 + 12, y, f->label, DIM, 0.72f, HUD_FONT_BANK, HUD_ALIGN_LEFT, HUD_FX_SHADOW);
        amTextFit(c, x1 - 10, y, f->value, f->rgba ? f->rgba : INK, 0.8f, HUD_FONT_BANK, HUD_ALIGN_RIGHT, HUD_FX_SHADOW, x1 - x0 - 120, 0.55f);
        if (f->flag) amText(c, x1 - 14 - hudTextW(f->value, 0.8f, HUD_FONT_BANK), y, "NEW", GOLD, 0.62f, HUD_FONT_BANK, HUD_ALIGN_RIGHT, 0);
        y += 15;
    }
    return y0 + h;
}

/* the post-game's stat cells: label, a value that counts up, a NEW BEST chip */
static void statCells(HudCanvas *c, s32 ox, s32 y0)
{
    const s32 per = 4, cw = 146, ch = 50, gap = 6, total = per * cw + (per - 1) * gap;
    const s32 x0 = ox + (640 - total) / 2;
    const f32 t = since(sScreenTick);
    s32 i;

    for (i = 0; i < sV.ninfo; i++)
    {
        const AmInfo *f = &sV.info[i];
        const s32 cx = x0 + (i % per) * (cw + gap), cy = y0 + (i / per) * (ch + gap);
        const f32 k = hudEaseOut(clamp01((t - 0.35f - 0.06f * (f32) i) / 0.7f));
        char buf[48];
        const char *v = f->value;
        const f32 saved = sA;

        sA *= clamp01((t - 0.25f - 0.06f * (f32) i) / 0.25f);
        hudFill(c, cx + 2, cy + 2, cx + cw + 2, cy + ch + 2, A(0x00000060u));
        hudGradientV(c, cx, cy, cx + cw, cy + ch, A(0x1C140CE8u), A(0x100A06D0u), 6);
        hudFill(c, cx, cy, cx + cw, cy + 2, A(f->flag ? GOLD : 0x6A5838FFu));
        hudFrame(c, cx, cy, cx + cw, cy + ch, 1, A(RULE));
        amText(c, cx + 8, cy + 7, f->label, DIM, 0.68f, HUD_FONT_BANK, HUD_ALIGN_LEFT, HUD_FX_SHADOW);
        {   /* a plain integer (with a short suffix) counts up */
            char *end = NULL;
            const long n = strtol(v, &end, 10);

            if (end != v && end != NULL && *end != ':' && *end != '.' && strlen(end) <= 6 && k < 1.0f)
            {
                snprintf(buf, sizeof(buf), "%ld%s", (long) ((f32) n * k + 0.5f), end);
                v = buf;
            }
        }
        amTextFit(c, cx + 8, cy + 21, v, f->rgba ? f->rgba : INK, 1.45f, HUD_FONT_ZURICH, HUD_ALIGN_LEFT, HUD_FX_SHADOW, cw - 16, 0.7f);
        if (f->flag && k >= 1.0f)
        {
            const f32 pp = hudPulse(sNow, 1.5f, 0.12f);

            hudFill(c, cx + cw - 58, cy + 5, cx + cw - 5, cy + 17, A(hudLerp(0xB08A30FFu, 0xFFE070FFu, pp - 0.88f)));
            amText(c, cx + cw - 31, cy + 7, sV.bestChip, 0x1A1006FFu, 0.6f, HUD_FONT_BANK, HUD_ALIGN_CENTRE, 0);
        }
        sA = saved;
    }
}

static void amBars(HudCanvas *c, s32 ox, s32 y0, s32 y1)
{
    const s32 n = sV.nbars, w = 560, xl = ox + 40;
    s32 x0;
    const f32 t = since(sScreenTick);
    s32 i, vmax = 0, bw, step;

    for (i = 0; i < n; i++) if (sV.bar[i].v > vmax) vmax = sV.bar[i].v;
    if (n <= 0 || vmax <= 0) return;   /* (no kills: no chart) */
    bw = w / n;
    if (bw > 22) bw = 22;
    step = n > 30 ? 10 : (n > 12 ? 5 : 1);
    x0 = xl + (w - n * bw) / 2;   /* (a short run's few bars sit in the middle) */
    amText(c, xl, y0 - 14, sV.chart, DIM, 0.66f, HUD_FONT_BANK, HUD_ALIGN_LEFT, HUD_FX_SHADOW);
    amText(c, xl + w, y0 - 14, sV.legend, DIM, 0.6f, HUD_FONT_BANK, HUD_ALIGN_RIGHT, HUD_FX_SHADOW);
    hudFill(c, xl - 4, y1, xl + w, y1 + 1, A(RULE));
    for (i = 0; i < n; i++)
    {
        const AmBar *b = &sV.bar[i];
        const f32 g = hudEaseOut(clamp01((t - 0.8f - 0.025f * (f32) i) / 0.45f));
        const s32 bh = (s32) ((f32) (y1 - y0) * (f32) b->v / (f32) vmax * g);
        const u32 col = b->kind == 1 ? GOLD : (b->kind == 2 ? 0xA868E8FFu : (b->kind == 3 ? RED : 0xA8A090E0u));
        const s32 bx = x0 + i * bw;
        char buf[8];

        hudGradientV(c, bx + 1, y1 - bh, bx + bw - 1, y1, A(col), A(hudLerp(col, 0x000000FFu, 0.5f)), 4);
        if ((i + 1) % step == 0 || i == 0 || i == n - 1)
        {
            snprintf(buf, sizeof(buf), "%d", i + 1);
            amText(c, bx + bw / 2, y1 + 3, buf, DIM, 0.55f, HUD_FONT_BANK, HUD_ALIGN_CENTRE, 0);
        }
    }
}

static void layout(HudCanvas *c, s32 ox, s32 oy, s32 colTop2)
{
    s32 i, y1 = oy + 112, y2 = colTop2;

    if (sV.screen == AMV_ADVANCED) y1 = y2 = oy + 102;
    if (sV.screen == AMV_POSTGAME)
    {   /* the buttons: one centred row */
        s32 tw = 0, x;

        for (i = 0; i < sV.n; i++) tw += (sV.item[i].kind == AMI_PRIMARY ? 176 : 160) + (i > 0 ? 12 : 0);
        x = ox + (640 - tw) / 2;
        for (i = 0; i < sV.n; i++)
        {
            const s32 bw = sV.item[i].kind == AMI_PRIMARY ? 176 : 160;

            sHit[i].x0 = x; sHit[i].x1 = x + bw;
            sHit[i].y0 = oy + 398; sHit[i].y1 = oy + 398 + 34;
            sHit[i].ok = 1;
            x += bw + 12;
        }
        return;
    }
    for (i = 0; i < sV.n; i++)
    {
        const AmItem *it = &sV.item[i];
        const s32 h = itemH(sV.screen, it->kind);
        const int right = it->col == 2;
        s32 *y = right ? &y2 : &y1;
        const s32 gap = it->kind == AMI_PRESET ? 3 : (sV.screen == AMV_ADVANCED ? 1 : 4);

        if (it->kind == AMI_HEADER && *y > (sV.screen == AMV_ADVANCED ? oy + 102 : oy + 112)) *y += 4;
        if (it->kind == AMI_PRIMARY || it->kind == AMI_BUTTON) *y += (sV.screen == AMV_ADVANCED ? 4 : 2);
        sHit[i].x0 = right ? ox + 330 : ox + 22;
        sHit[i].x1 = right ? ox + 618 : (sV.screen == AMV_ADVANCED ? ox + 312 : ox + 304);
        sHit[i].y0 = *y;
        sHit[i].y1 = *y + h;
        sHit[i].ok = it->kind != AMI_HEADER;
        *y += h + gap;
        if (it->kind == AMI_PRIMARY && it->sub[0]) *y += 12;
    }
    (void) c;
}

static void cursorArrow(HudCanvas *c)
{
    const s32 px = (s32) sPtrX, py = (s32) sPtrY;
    s32 k;

    if (!sPtrSeen || px < 0 || py < 0 || px > c->w || py > c->h || since(sPtrMoveTick) > 6.0f) return;
    for (k = 0; k < 16; k++)
    {
        const s32 w = k < 12 ? k * 2 / 3 + 1 : 12 - k + 8;

        hudFill(c, px - 1, py + k - 1, px + w + 2, py + k + 1, 0x000000FFu);
    }
    for (k = 1; k < 14; k++)
    {
        const s32 w = k < 11 ? k * 2 / 3 : 11 - k + 6;

        hudFill(c, px, py + k, px + w + 1, py + k + 1, 0xFFFFFFFFu);
    }
}

/* port_console.c portConRender, in a mode, over the HUD */
Gfx *portArenaMenuRender(Gfx *gdl)
{
    HudCanvas c;
    s32 ox, oy, i, colTop2;

    if (!portArenaMenuViewOn())
    {
        return gdl;
    }
    gdl = hudBegin(gdl, &c);
    if (!c.ok)
    {
        return gdl;
    }
    sNow = (f32) g_GlobalTimer / 60.0f;
    sA = hudEaseOut(clamp01(since(sScreenTick) / 0.28f));
    ox = (c.w - 640) / 2;
    oy = (c.h - 480) / 2;
    backdrop(&c, sV.screen);
    colTop2 = oy + 112;
    if (sV.screen == AMV_POSTGAME)
    {
        const f32 t = since(sScreenTick);
        const f32 pop = t < 0.35f ? 1.0f + 0.7f * (1.0f - hudEaseOut(t / 0.35f)) : 1.0f;
        const f32 bsc = 3.3f * pop;
        const s32 cx = c.w / 2;

        amText(&c, cx, oy + 18, sV.kicker, DIM, 0.8f, HUD_FONT_BANK, HUD_ALIGN_CENTRE, HUD_FX_SHADOW);
        amTextFit(&c, cx, oy + 30 - (s32) ((bsc - 3.3f) * 8.0f), sV.banner, sV.bannerRgba ? sV.bannerRgba : GOLD, bsc, HUD_FONT_ZURICH,
                  HUD_ALIGN_CENTRE, HUD_FX_OUTLINE, (s32) (600.0f * pop), 1.4f);
        hudGradientH(&c, cx - 220, oy + 86, cx, oy + 88, A(0xFFFFFF00u & GOLD), A(GOLD), 12);
        hudGradientH(&c, cx, oy + 86, cx + 220, oy + 88, A(GOLD), A(0xFFFFFF00u & GOLD), 12);
        amText(&c, cx, oy + 94, sV.subtitle, INK, 0.9f, HUD_FONT_BANK, HUD_ALIGN_CENTRE, HUD_FX_SHADOW);
        if (sV.medal >= 0)
        {
            char buf[48];
            const u32 mc = kMedal[sV.medal > 4 ? 4 : sV.medal];
            const f32 mt = clamp01((t - 0.3f) / 0.3f);
            const f32 saved = sA;

            sA *= mt;
            snprintf(buf, sizeof(buf), "%s", sV.medalName);
            {
                const s32 tw = hudTextW(buf, 1.1f, HUD_FONT_ZURICH);

                hudFill(&c, cx - tw / 2 - 18, oy + 112, cx - tw / 2 - 8, oy + 122, A(mc));
                amText(&c, cx + 0, oy + 109, buf, mc, 1.1f, HUD_FONT_ZURICH, HUD_ALIGN_CENTRE, HUD_FX_OUTLINE);
                if (sV.medalNew) amText(&c, cx + tw / 2 + 10, oy + 112, sV.newMedal, GOLD, 0.8f, HUD_FONT_BANK, HUD_ALIGN_LEFT, HUD_FX_SHADOW);
            }
            sA = saved;
        }
        if (sV.news[0])
        {
            const f32 pp = hudPulse(sNow, 1.2f, 1.0f) - 1.0f;

            amText(&c, cx, oy + 132, sV.news, hudLerp(GOLD, 0xFFF4C0FFu, 0.5f + 0.5f * pp), 0.85f, HUD_FONT_BANK, HUD_ALIGN_CENTRE, HUD_FX_SHADOW);
        }
        statCells(&c, ox, oy + 150);
        amBars(&c, ox, oy + 336, oy + 384);
    }
    else
    {
        amTitle(&c, ox, oy);
        if (sV.screen == AMV_MAIN && (sV.ninfo > 0 || sV.infoSub[0])) colTop2 = infoCard(&c, ox + 330, oy + 112, ox + 618) + 10;
    }
    layout(&c, ox, oy, colTop2);
    sHitScreen = sV.screen;
    for (i = 0; i < sV.n; i++) drawItem(&c, i, &sHit[i]);
    if (sV.desc[0])
    {   /* the strip at the bottom: what the selected thing does */
        const s32 y = oy + 440;

        hudGradientH(&c, ox + 22, y, ox + 618, y + 18, A(0x3A2810D0u), A(0x3A281030u), 12);
        hudFill(&c, ox + 22, y, ox + 25, y + 18, A(GOLD));
        amTextFit(&c, ox + 32, y + 4, sV.desc, 0xFFF0C0FFu, 0.8f, HUD_FONT_BANK, HUD_ALIGN_LEFT, HUD_FX_SHADOW, 580, 0.55f);
    }
    if (sV.footer[0]) amTextFit(&c, c.w / 2, c.h - 16, sV.footer, DIM, 0.7f, HUD_FONT_BANK, HUD_ALIGN_CENTRE, HUD_FX_SHADOW, c.w - 40, 0.5f);
    cursorArrow(&c);
    {
        static s32 warned;

        if (!warned && (c.end - c.g) < 1200)
        {
            warned = 1;
            fprintf(stdout, "ge: arenamenu: the HUD display list is nearly full (%d left)\n", (int) (c.end - c.g));
        }
    }
    return hudEnd(gdl, &c);
}

/* ---- the mouse ----------------------------------------------------------------------------------------------------------------- */

static void click(s32 item, s32 act)
{
    const int me = portLocalPlayer();

    portSessionCmdRun(PORT_NETCMD_PLAN_POINT, me < 0 ? 0 : me, sHitScreen * 64 + item, act);
}

/* port_intel.c portIntelMouse, every poll: while the menu is up the mouse is its pointer (1 = taken: no bind sees the buttons) */
int portArenaMenuMouse(int mx, int my, int winw, int winh, unsigned int mbuttons, int wheelUp, int wheelDown)
{
    static s32 lastx = -99999, lasty = -99999;
    s32 w, h, i, over = -1;
    u32 edge;

    if (!portArenaMenuViewOn() || winw <= 0 || winh <= 0)
    {
        sPrevMB = mbuttons;
        return 0;
    }
    portConsoleSpace(&w, &h);
    if (mx != lastx || my != lasty)
    {
        sPtrX = (f32) mx * (f32) w / (f32) winw;
        sPtrY = (f32) my * (f32) h / (f32) winh;
        if (lastx != -99999) sPtrMoveTick = (s32) g_GlobalTimer;
        sPtrSeen = 1;
        lastx = mx;
        lasty = my;
    }
    if (sHitScreen == sV.screen && since(sPtrMoveTick) < 6.0f)
    {
        for (i = 0; i < sV.n; i++)
        {
            const Rect *r = &sHit[i];

            if (r->ok && sPtrX >= (f32) r->x0 && sPtrX < (f32) r->x1 && sPtrY >= (f32) r->y0 && sPtrY < (f32) r->y1) { over = i; break; }
        }
    }
    sHover = over;
    if (over >= 0 && over != sHoverSent && since(sPtrMoveTick) < 0.2f)
    {   /* the cursor follows the pointer (the script moves it: sim input) */
        sHoverSent = over;
        click(over + 1, 6);
    }
    edge = mbuttons & ~sPrevMB;
    sPrevMB = mbuttons;
    if ((edge & 1u) && over >= 0) click(over + 1, 1);
    if (edge & 4u) click(over >= 0 ? over + 1 : 0, 2);
    if (wheelUp) click(0, 4);
    if (wheelDown) click(0, 5);
    return 1;
}

#endif /* !TARGET_N64 */
