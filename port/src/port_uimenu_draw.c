/**
 * port_uimenu_draw.c - the generic mode menu layer's renderer (port_uimenu.h): a LOBBY page (a title block, cards and rows on the
 * left, an info card and the primary button on the right), a SETTINGS page (two dense columns of grouped rows) and a DEBRIEF (a banner,
 * a rating line, stat cells that count up, an objective checklist, a bar chart, commendation chips, a row of buttons), drawn over the
 * live stage with the shared HUD helpers (port_hudstyle.c). The arena's look (port_arenamenu_draw.c): the same palette, type and motion.
 * No new art; the fonts are the game's own.
 *
 * PRESENTATION ONLY (snap_embed.py: PRES). The lobby script owns the menu (the cursor, the values, the pages are its sim state, driven
 * by the crew's synchronized buttons through plan.input); every change it pushes the whole view here (ui.view{...}). This file only
 * draws it, animates it (on g_GlobalTimer stamps taken when a push changes something, so a re-simulated push changes nothing) and
 * hit-tests the mouse: a hover or a click becomes a session command (PORT_NETCMD_PLAN_POINT, a = page * 64 + item, b = 1 select,
 * 2 back, 4 / 5 the wheel, 6 hover), which reaches the script through plan.pointer.
 */
#ifndef TARGET_N64

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ultra64.h>

#include "port_hudstyle.h"
#include "port_uimenu.h"
#include "port_netmsg.h"

extern s32 g_GlobalTimer;
extern int portModeActive(void);
extern int portLocalPlayer(void);
extern void portConsoleSpace(s32 *w, s32 *h);
extern int portSessionCmdRun(int kind, int player, int a, int b);
extern int g_portCoopEpoch;

#define INK    UI_INK
#define GOLD   UI_GOLD
#define GOLDDK UI_GOLDDK
#define DIM    UI_DIM
#define SHADE  UI_SHADE
#define RULE   UI_RULE
#define RED    UI_RED
#define GREEN  0x7CC870FFu

static const u32 kMedal[5] = { 0x9A9080FFu, 0xD08A48FFu, 0xD4D8E0FFu, 0xF0C040FFu, 0xA8ECF8FFu };   /* none, bronze .. platinum */

static UiView sV;
static s32 sOn, sEpoch = -1000;
static s32 sScreenTick = -100000, sCursorTick = -100000, sPrevPage = -1, sPrevCursor = -1;
static s32 sValTick[UIV_ITEMS];
static char sPrevVal[UIV_ITEMS][44];

typedef struct { s32 x0, y0, x1, y1, ok; } Rect;
static Rect sHit[UIV_ITEMS];
static s32 sHitPage;
static s32 sHover = -1, sHoverSent = -1;
static f32 sPtrX = -1000.0f, sPtrY = -1000.0f;
static s32 sPtrSeen, sPtrMoveTick = -100000;
static u32 sPrevMB;

/* ---- the view, from the Lua binding ---------------------------------------------------------------------------------------------- */

void portUiMenuViewSet(const UiView *v)
{
    const s32 now = (s32) g_GlobalTimer;
    s32 i;

    if (v->page != sPrevPage || v->layout != sV.layout || sEpoch != g_portCoopEpoch)
    {   /* a new page, or the first push of a new stage */
        sEpoch = g_portCoopEpoch;
        sPtrMoveTick = -100000;
        sPtrSeen = 0;
        sPrevCursor = -1;
        sScreenTick = now;
        sPrevPage = v->page;
        sHover = sHoverSent = -1;
        memset(sPrevVal, 0, sizeof(sPrevVal));
        memset(sHit, 0, sizeof(sHit));
    }
    else
    {
        for (i = 0; i < v->n && i < UIV_ITEMS; i++)
        {
            if (v->item[i].kind == UII_CHOICE && strcmp(sPrevVal[i], v->item[i].value) != 0 && sPrevVal[i][0] != '\0') sValTick[i] = now;
        }
    }
    if (v->cursor != sPrevCursor)
    {
        sCursorTick = now;
        sPrevCursor = v->cursor;
    }
    for (i = 0; i < v->n && i < UIV_ITEMS; i++) snprintf(sPrevVal[i], sizeof(sPrevVal[i]), "%s", v->item[i].value);
    sV = *v;
    if (sV.n > UIV_ITEMS) sV.n = UIV_ITEMS;
    if (sV.ninfo > UIV_INFO) sV.ninfo = UIV_INFO;
    if (sV.ncells > UIV_CELLS) sV.ncells = UIV_CELLS;
    if (sV.nchecks > UIV_CHECKS) sV.nchecks = UIV_CHECKS;
    if (sV.nchips > UIV_CHIPS) sV.nchips = UIV_CHIPS;
    if (sV.nbars > UIV_BARS) sV.nbars = UIV_BARS;
    sOn = sV.layout != UIL_OFF;
}

int portUiMenuViewOn(void) { return sOn && sEpoch == g_portCoopEpoch && portModeActive(); }

void portUiMenuViewOff(void)
{
    sOn = 0;
    sHover = sHoverSent = -1;
    sPrevMB = 0;
}

/* ---- drawing helpers ------------------------------------------------------------------------------------------------------------- */

static f32 sA;          /* the page's fade-in */
static f32 sNow;        /* seconds (sim ticks / 60) */

static f32 since(s32 tick) { return (f32) ((s32) g_GlobalTimer - tick) / 60.0f; }
static u32 A(u32 c) { return hudAlpha(c, sA); }
static f32 clamp01(f32 t) { return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t); }

static void uText(HudCanvas *c, s32 x, s32 y, const char *t, u32 col, f32 sc, s32 font, s32 align, s32 fx)
{
    hudText(c, x, y, t, A(col), sc, font, align, fx, SHADE);
}

static void uTextFit(HudCanvas *c, s32 x, s32 y, const char *t, u32 col, f32 sc, s32 font, s32 align, s32 fx, s32 room, f32 minsc)
{
    const s32 w = hudTextW(t, sc, font);

    if (w > room && w > 0)
    {
        f32 s2 = sc * (f32) room / (f32) w;

        if (s2 < minsc) s2 = minsc;
        y += (s32) ((sc - s2) * (font ? 8.0f : 5.0f));
        sc = s2;
    }
    uText(c, x, y, t, col, sc, font, align, fx);
}

static void plate(HudCanvas *c, s32 x0, s32 y0, s32 x1, s32 y1, u32 left, u32 right) { hudGradientH(c, x0, y0, x1, y1, A(left), A(right), 12); }

/* a value that counts up: an optional prefix ("$"), digits with commas, a short suffix (" BARS"); times ("4:12") and decimals do not */
static const char *countUp(const char *v, f32 k, char *buf, size_t n)
{
    const char *p = v, *d;
    char digits[24];
    s32 nd = 0, i, j, len, commas = 0;
    long val;
    char num[32], out[32];

    if (k >= 1.0f) return v;
    while (*p != '\0' && !(*p >= '0' && *p <= '9') && (p - v) < 3) p++;
    if (!(*p >= '0' && *p <= '9')) return v;
    for (d = p; *d != '\0' && ((*d >= '0' && *d <= '9') || *d == ','); d++)
    {
        if (*d == ',') commas = 1;
        else if (nd < 18) digits[nd++] = *d;
    }
    if (*d == ':' || *d == '.' || strlen(d) > 10 || nd == 0) return v;
    digits[nd] = '\0';
    val = (long) ((f32) atol(digits) * k + 0.5f);
    snprintf(num, sizeof(num), "%ld", val);
    len = (s32) strlen(num);
    for (i = 0, j = 0; i < len && j < (s32) sizeof(out) - 2; i++)
    {   /* the commas back, if it had them */
        if (i > 0 && (len - i) % 3 == 0 && commas) out[j++] = ',';
        out[j++] = num[i];
    }
    out[j] = '\0';
    snprintf(buf, n, "%.*s%s%s", (int) (p - v), v, out, d);
    return buf;
}

static void backdrop(HudCanvas *c)
{
    const s32 w = c->w, h = c->h;

    if (sV.layout == UIL_LOBBY)
    {   /* the left two thirds dark for the lists, the scene showing on the right */
        hudGradientH(c, 0, 0, w * 62 / 100, h, A(0x070503E6u), A(0x07050330u), 64);
        hudFill(c, w * 62 / 100, 0, w, h, A(0x07050330u));
    }
    else if (sV.layout == UIL_SETTINGS)
    {
        hudFill(c, 0, 0, w, h, A(0x070503C8u));
    }
    else
    {
        const u32 tint = ((sV.bannerRgba ? sV.bannerRgba : GOLD) & 0xFFFFFF00u) | 0x50u;

        hudFill(c, 0, 0, w, h, A(0x060403C4u));
        hudGradientV(c, 0, 0, w, h * 35 / 100, A(tint), A(tint & 0xFFFFFF00u), 16);
    }
    hudGradientV(c, 0, 0, w, 26, A(0x000000B0u), A(0x00000000u), 6);
    hudGradientV(c, 0, h - 60, w, h, A(0x00000000u), A(0x000000C8u), 8);
}

static void titleBlock(HudCanvas *c, s32 ox, s32 oy)
{
    const f32 t = clamp01(since(sScreenTick) / 0.35f), e = hudEaseOut(t);
    const s32 dx = (s32) ((1.0f - e) * -24.0f);

    uText(c, ox + 24 + dx, oy + 22, sV.kicker, DIM, 0.8f, HUD_FONT_BANK, HUD_ALIGN_LEFT, HUD_FX_SHADOW);
    uTextFit(c, ox + 22 + dx, oy + 36, sV.title, GOLD, 2.4f, HUD_FONT_ZURICH, HUD_ALIGN_LEFT, HUD_FX_OUTLINE, 580, 1.2f);
    hudGradientH(c, ox + 22, oy + 78, ox + 22 + (s32) (330.0f * e), oy + 80, A(GOLD), A(GOLD & 0xFFFFFF00u), 12);
    if (sV.subtitle[0]) uTextFit(c, ox + 24 + dx, oy + 84, sV.subtitle, INK, 0.8f, HUD_FONT_BANK, HUD_ALIGN_LEFT, HUD_FX_SHADOW, 590, 0.55f);
}

/* ---- the items ------------------------------------------------------------------------------------------------------------------- */

static void drawItem(HudCanvas *c, s32 i, const Rect *r)
{
    const UiItem *it = &sV.item[i];
    const int sel = (i + 1 == sV.cursor), hov = (i == sHover) && !sel;
    const f32 cs = since(sCursorTick), pulse = 0.5f + 0.5f * (hudPulse(sNow, 1.2f, 1.0f) - 1.0f);
    const f32 slide = sel ? hudEaseOut(clamp01(cs / 0.18f)) : 0.0f;
    const f32 stag = clamp01((since(sScreenTick) - 0.06f * (f32) i) / 0.25f);   /* the items stagger in, 60 ms apart */
    const s32 x0 = r->x0 + (s32) (slide * 6.0f) - (s32) ((1.0f - hudEaseOut(stag)) * 14.0f), y0 = r->y0, x1 = r->x1, y1 = r->y1;
    const s32 hh = y1 - y0;
    const f32 saved = sA;

    sA *= (it->dim ? 0.45f : 1.0f) * stag;
    switch (it->kind)
    {
    case UII_HEADER:
        uText(c, x0, y0 + 5, it->label, GOLD, 0.72f, HUD_FONT_BANK, HUD_ALIGN_LEFT, HUD_FX_SHADOW);
        {
            const s32 tw = hudTextW(it->label, 0.72f, HUD_FONT_BANK);

            hudGradientH(c, x0 + tw + 8, y0 + 10, x1, y0 + 11, A(RULE), A(RULE & 0xFFFFFF00u), 8);
        }
        break;
    case UII_CARD:
        {   /* a job / preset card: the name, a line, the best (a medal chip), a tag; tint = its accent */
            const u32 acc = it->tint ? it->tint : GOLD;

            hudFill(c, x0 + 2, y0 + 2, x1 + 2, y1 + 2, A(0x00000050u));
            plate(c, x0, y0, x1, y1, sel ? 0x4A3412F0u : (hov ? 0x2A1E0EE0u : 0x140E08D0u), sel ? 0x4A341270u : 0x140E0858u);
            hudFill(c, x0, y0, x0 + 3, y1, A(sel ? acc : (it->chosen ? hudLerp(acc, 0x000000FFu, 0.4f) : 0x3A3020C0u)));
            if (sel) hudFrame(c, x0, y0, x1, y1, 1, A(hudLerp(0x8A6A28FFu, 0xFFE070FFu, pulse)));
            else hudFrame(c, x0, y0, x1, y1, 1, A(0x3A302080u));
            uTextFit(c, x0 + 12, y0 + 3, it->label, sel ? GOLD : INK, 1.0f, HUD_FONT_ZURICH, HUD_ALIGN_LEFT, HUD_FX_SHADOW, x1 - x0 - 130, 0.65f);
            uTextFit(c, x0 + 12, y0 + 21, it->sub, DIM, 0.62f, HUD_FONT_BANK, HUD_ALIGN_LEFT, HUD_FX_SHADOW, x1 - x0 - 24, 0.5f);
            if (it->value[0]) uTextFit(c, x0 + 12, y0 + hh - 12, it->value, sel ? INK : DIM, 0.62f, HUD_FONT_BANK, HUD_ALIGN_LEFT, HUD_FX_SHADOW,
                                       x1 - x0 - 120, 0.5f);
            if (it->right[0])
            {
                const u32 mc = kMedal[it->medal < 0 || it->medal > 4 ? 0 : it->medal];
                const s32 tw = hudTextW(it->right, 0.66f, HUD_FONT_BANK);

                if (it->medal > 0) hudFill(c, x1 - 16 - tw, y0 + 7, x1 - 11 - tw, y0 + 12, A(mc));
                uText(c, x1 - 8, y0 + 5, it->right, it->medal > 0 ? mc : DIM, 0.66f, HUD_FONT_BANK, HUD_ALIGN_RIGHT, HUD_FX_SHADOW);
            }
            if (it->tag[0]) uText(c, x1 - 8, y0 + hh - 12, it->tag, it->dim ? RED : acc, 0.62f, HUD_FONT_BANK, HUD_ALIGN_RIGHT, HUD_FX_SHADOW);
            if (it->chosen) hudFill(c, x1 - 7, y1 - 4, x1 - 3, y1 - 2, A(acc));
        }
        break;
    case UII_CHOICE:
        {
            const f32 vt = since(sValTick[i]);
            const f32 flash = (vt >= 0.0f && vt < 0.4f) ? 1.0f - vt / 0.4f : 0.0f;
            const s32 ty = y0 + (hh - 10) / 2;

            if (sel) plate(c, x0, y0, x1, y1, 0x4A3412E0u, 0x4A341240u);
            else if (hov) plate(c, x0, y0, x1, y1, 0x2A1E0EC0u, 0x2A1E0E30u);
            if (sel) hudFill(c, x0, y0, x0 + 2, y1, A(GOLD));
            uTextFit(c, x0 + 8, ty, it->label, sel ? INK : DIM, 0.8f, HUD_FONT_BANK, HUD_ALIGN_LEFT, HUD_FX_SHADOW, (x1 - x0) / 2, 0.6f);
            if (sel)
            {
                const s32 vw = hudTextW(it->value, 0.85f, HUD_FONT_BANK);
                const s32 nudge = (s32) (2.0f * (hudPulse(sNow, 2.0f, 1.0f) - 1.0f)) + (s32) (flash * 3.0f);

                uText(c, x1 - 8 + nudge, ty, ">", GOLD, 0.85f, HUD_FONT_BANK, HUD_ALIGN_RIGHT, HUD_FX_SHADOW);
                uText(c, x1 - 20, ty, it->value, hudLerp(GOLD, 0xFFFFFFFFu, flash), 0.85f, HUD_FONT_BANK, HUD_ALIGN_RIGHT, HUD_FX_SHADOW);
                uText(c, x1 - 32 - vw - nudge, ty, "<", GOLD, 0.85f, HUD_FONT_BANK, HUD_ALIGN_RIGHT, HUD_FX_SHADOW);
            }
            else
            {
                uText(c, x1 - 8, ty, it->value, hudLerp(it->tint ? it->tint : INK, GOLD, flash), 0.85f, HUD_FONT_BANK, HUD_ALIGN_RIGHT, HUD_FX_SHADOW);
            }
        }
        break;
    case UII_BUTTON:
        hudFill(c, x0 + 2, y0 + 2, x1 + 2, y1 + 2, A(0x00000050u));
        plate(c, x0, y0, x1, y1, sel ? 0x4A3412F0u : (hov ? 0x2A1E0EE0u : 0x140E08D0u), sel ? 0x4A3412A0u : 0x140E0890u);
        hudFrame(c, x0, y0, x1, y1, 1, A(sel ? hudLerp(0x8A6A28FFu, 0xFFE070FFu, pulse) : RULE));
        uTextFit(c, (x0 + x1) / 2, y0 + (hh - 14) / 2, it->label, sel ? GOLD : INK, 0.9f, HUD_FONT_ZURICH, HUD_ALIGN_CENTRE, HUD_FX_SHADOW,
                 x1 - x0 - 12, 0.6f);
        break;
    case UII_PRIMARY:
        {
            const f32 glow = sel ? pulse : 0.0f;

            hudFill(c, x0 + 3, y0 + 3, x1 + 3, y1 + 3, A(0x00000070u));
            hudGradientV(c, x0, y0, x1, y1, A(hudLerp(0xD8A838FFu, 0xFFE070FFu, glow)), A(hudLerp(0x8A6420FFu, 0xB08A30FFu, glow)), 10);
            hudFrame(c, x0, y0, x1, y1, sel ? 2 : 1, A(sel ? 0xFFF4C0FFu : 0x3A2810FFu));
            uTextFit(c, (x0 + x1) / 2, y0 + (hh - 22) / 2, it->label, 0x1A1006FFu, 1.35f, HUD_FONT_ZURICH, HUD_ALIGN_CENTRE, 0, x1 - x0 - 16, 0.7f);
            if (sel && it->sub[0]) uText(c, (x0 + x1) / 2, y1 + 4, it->sub, DIM, 0.66f, HUD_FONT_BANK, HUD_ALIGN_CENTRE, HUD_FX_SHADOW);
        }
        break;
    }
    if (hov && it->kind != UII_HEADER) hudFrame(c, r->x0 - 1, y0 - 1, x1 + 1, y1 + 1, 1, A(0xFFC02090u));
    sA = saved;
}

static s32 itemH(s32 kind)
{
    if (kind == UII_HEADER) return 18;
    if (kind == UII_CARD) return 46;
    if (kind == UII_CHOICE) return sV.layout == UIL_SETTINGS ? 16 : 22;
    if (kind == UII_PRIMARY) return 36;
    return 22;
}

/* ---- the lobby's info card ------------------------------------------------------------------------------------------------------- */

static s32 infoCard(HudCanvas *c, s32 x0, s32 y0, s32 x1)
{
    s32 y = y0, i;
    const s32 h = 46 + sV.ninfo * 15 + 8;

    hudFill(c, x0 + 3, y0 + 3, x1 + 3, y0 + h + 3, A(0x00000060u));
    plate(c, x0, y0, x1, y0 + h, 0x140E08E0u, 0x140E08A8u);
    hudFill(c, x0, y0, x0 + 3, y0 + h, A(GOLD));
    hudFrame(c, x0, y0, x1, y0 + h, 1, A(RULE));
    uText(c, x0 + 12, y + 6, sV.infoTitle, DIM, 0.72f, HUD_FONT_BANK, HUD_ALIGN_LEFT, HUD_FX_SHADOW);
    uTextFit(c, x0 + 12, y + 18, sV.infoSub, GOLD, 1.3f, HUD_FONT_ZURICH, HUD_ALIGN_LEFT, HUD_FX_OUTLINE, x1 - x0 - 24, 0.5f);
    y += 46;
    for (i = 0; i < sV.ninfo; i++)
    {
        const UiInfo *f = &sV.info[i];

        if (f->label[0] == '\0' && f->value[0] == '\0') { y += 6; continue; }
        uText(c, x0 + 12, y, f->label, DIM, 0.72f, HUD_FONT_BANK, HUD_ALIGN_LEFT, HUD_FX_SHADOW);
        uTextFit(c, x1 - 10, y, f->value, f->rgba ? f->rgba : INK, 0.8f, HUD_FONT_BANK, HUD_ALIGN_RIGHT, HUD_FX_SHADOW, x1 - x0 - 120, 0.55f);
        if (f->flag) uText(c, x1 - 14 - hudTextW(f->value, 0.8f, HUD_FONT_BANK), y, sV.newMedal[0] ? sV.newMedal : "NEW", GOLD, 0.62f, HUD_FONT_BANK,
                           HUD_ALIGN_RIGHT, 0);
        y += 15;
    }
    return y0 + h;
}

/* ---- the debrief's pieces -------------------------------------------------------------------------------------------------------- */

static void statCells(HudCanvas *c, s32 ox, s32 y0)
{
    const s32 per = 4, cw = 146, ch = 50, gap = 6, total = per * cw + (per - 1) * gap;
    const s32 x0 = ox + (640 - total) / 2;
    const f32 t = since(sScreenTick);
    s32 i;

    for (i = 0; i < sV.ncells; i++)
    {
        const UiInfo *f = &sV.cell[i];
        const s32 cx = x0 + (i % per) * (cw + gap), cy = y0 + (i / per) * (ch + gap);
        const f32 k = hudEaseOut(clamp01((t - 0.35f - 0.06f * (f32) i) / 0.7f));
        char buf[48];
        const f32 saved = sA;

        sA *= clamp01((t - 0.25f - 0.06f * (f32) i) / 0.25f);
        hudFill(c, cx + 2, cy + 2, cx + cw + 2, cy + ch + 2, A(0x00000060u));
        hudGradientV(c, cx, cy, cx + cw, cy + ch, A(0x1C140CE8u), A(0x100A06D0u), 6);
        hudFill(c, cx, cy, cx + cw, cy + 2, A(f->flag ? GOLD : 0x6A5838FFu));
        hudFrame(c, cx, cy, cx + cw, cy + ch, 1, A(RULE));
        uTextFit(c, cx + 8, cy + 7, f->label, DIM, 0.68f, HUD_FONT_BANK, HUD_ALIGN_LEFT, HUD_FX_SHADOW, f->flag && k >= 1.0f ? cw - 70 : cw - 14, 0.5f);
        uTextFit(c, cx + 8, cy + 21, countUp(f->value, k, buf, sizeof(buf)), f->rgba ? f->rgba : INK, 1.45f, HUD_FONT_ZURICH, HUD_ALIGN_LEFT,
                 HUD_FX_SHADOW, cw - 16, 0.6f);
        if (f->flag && k >= 1.0f)
        {
            const f32 pp = hudPulse(sNow, 1.5f, 0.12f);

            hudFill(c, cx + cw - 58, cy + 5, cx + cw - 5, cy + 17, A(hudLerp(0xB08A30FFu, 0xFFE070FFu, pp - 0.88f)));
            uText(c, cx + cw - 31, cy + 7, sV.bestChip, 0x1A1006FFu, 0.6f, HUD_FONT_BANK, HUD_ALIGN_CENTRE, 0);
        }
        sA = saved;
    }
}

/* the objective checklist: a box per line (ticked gold, crossed red, empty), ticking in one after another */
static void checklist(HudCanvas *c, s32 x0, s32 y0, s32 x1)
{
    const f32 t = since(sScreenTick);
    s32 i, y = y0 + 16;

    if (sV.nchecks <= 0) return;
    uText(c, x0, y0, sV.checksTitle, DIM, 0.66f, HUD_FONT_BANK, HUD_ALIGN_LEFT, HUD_FX_SHADOW);
    hudGradientH(c, x0 + hudTextW(sV.checksTitle, 0.66f, HUD_FONT_BANK) + 8, y0 + 5, x1, y0 + 6, A(RULE), A(RULE & 0xFFFFFF00u), 8);
    for (i = 0; i < sV.nchecks; i++)
    {
        const UiCheck *k = &sV.check[i];
        const f32 at = 0.9f + 0.12f * (f32) i, e = clamp01((t - at) / 0.2f);
        const f32 saved = sA;
        const u32 col = k->state == 1 ? GOLD : (k->state == 2 ? RED : 0x6A6050FFu);

        sA *= clamp01((t - at + 0.15f) / 0.2f);
        hudFrame(c, x0, y + 1, x0 + 9, y + 10, 1, A(col));
        if (k->state == 1 && e > 0.0f)
        {   /* a tick: two strokes that draw in */
            const s32 a = (s32) (4.0f * clamp01(e * 2.0f)), b = (s32) (6.0f * clamp01(e * 2.0f - 1.0f));
            s32 s;

            for (s = 0; s <= a; s++) hudFill(c, x0 + 2 + s, y + 5 + s, x0 + 4 + s, y + 7 + s, A(GOLD));
            for (s = 0; s <= b; s++) hudFill(c, x0 + 5 + s, y + 8 - s, x0 + 7 + s, y + 10 - s, A(GOLD));
        }
        else if (k->state == 2 && e > 0.0f)
        {
            s32 s;

            for (s = 0; s < 6; s++)
            {
                hudFill(c, x0 + 2 + s, y + 3 + s, x0 + 3 + s, y + 4 + s, A(RED));
                hudFill(c, x0 + 7 - s, y + 3 + s, x0 + 8 - s, y + 4 + s, A(RED));
            }
        }
        uTextFit(c, x0 + 15, y, k->label, k->state == 1 ? INK : DIM, 0.72f, HUD_FONT_BANK, HUD_ALIGN_LEFT, HUD_FX_SHADOW, x1 - x0 - 90, 0.5f);
        if (k->value[0]) uText(c, x1, y, k->value, k->state == 1 ? GOLD : DIM, 0.72f, HUD_FONT_BANK, HUD_ALIGN_RIGHT, HUD_FX_SHADOW);
        sA = saved;
        y += 14;
    }
}

/* the bar chart: labelled bars (kind 0 plain, 1 gold, 2 purple, 3 red) that grow in sequence */
static void bars(HudCanvas *c, s32 x0, s32 y0, s32 x1, s32 y1)
{
    const s32 n = sV.nbars, w = x1 - x0;
    const f32 t = since(sScreenTick);
    s32 i, vmax = 0, bw, step, xs, labelled = 0;

    for (i = 0; i < n; i++)
    {
        const s32 v = sV.bar[i].v < 0 ? -sV.bar[i].v : sV.bar[i].v;

        if (v > vmax) vmax = v;
        if (sV.bar[i].label[0]) labelled = 1;
    }
    if (n <= 0 || vmax <= 0) return;
    bw = w / n;
    if (bw > (labelled ? 64 : 22)) bw = labelled ? 64 : 22;
    step = n > 30 ? 10 : (n > 12 ? 5 : 1);
    xs = x0 + (w - n * bw) / 2;
    uText(c, x0, y0 - 14, sV.chart, DIM, 0.66f, HUD_FONT_BANK, HUD_ALIGN_LEFT, HUD_FX_SHADOW);
    uTextFit(c, x1, y0 - 14, sV.legend, DIM, 0.6f, HUD_FONT_BANK, HUD_ALIGN_RIGHT, HUD_FX_SHADOW, w - hudTextW(sV.chart, 0.66f, HUD_FONT_BANK) - 12, 0.45f);
    hudFill(c, x0 - 4, y1, x1, y1 + 1, A(RULE));
    for (i = 0; i < n; i++)
    {
        const UiBar *b = &sV.bar[i];
        const s32 v = b->v < 0 ? -b->v : b->v;
        const f32 g = hudEaseOut(clamp01((t - 0.8f - (labelled ? 0.12f : 0.025f) * (f32) i) / 0.45f));
        const s32 bh = (s32) ((f32) (y1 - y0 - (labelled ? 10 : 0)) * (f32) v / (f32) vmax * g);   /* (room over a labelled bar for its text) */
        const u32 col = b->kind == 1 ? GOLD : (b->kind == 2 ? 0xA868E8FFu : (b->kind == 3 ? RED : 0xA8A090E0u));
        const s32 bx = xs + i * bw, pad = labelled ? 6 : 1;
        char buf[16];

        hudGradientV(c, bx + pad, y1 - bh, bx + bw - pad, y1, A(col), A(hudLerp(col, 0x000000FFu, 0.5f)), 4);
        if (labelled)
        {
            uTextFit(c, bx + bw / 2, y1 + 3, b->label, DIM, 0.52f, HUD_FONT_BANK, HUD_ALIGN_CENTRE, 0, bw - 2, 0.4f);
            if (b->text[0] && g >= 1.0f) uTextFit(c, bx + bw / 2, y1 - bh - 9, b->text, b->kind == 3 ? RED : INK, 0.52f, HUD_FONT_BANK, HUD_ALIGN_CENTRE,
                                                  HUD_FX_SHADOW, bw - 2, 0.4f);
        }
        else if ((i + 1) % step == 0 || i == 0 || i == n - 1)
        {
            snprintf(buf, sizeof(buf), "%d", i + 1);
            uText(c, bx + bw / 2, y1 + 3, buf, DIM, 0.55f, HUD_FONT_BANK, HUD_ALIGN_CENTRE, 0);
        }
    }
}

/* the commendation chips: earned ones gold, the rest dark; they wrap */
static void chips(HudCanvas *c, s32 x0, s32 y0, s32 x1)
{
    const f32 t = since(sScreenTick);
    s32 i, x = x0, y = y0 + 15;

    if (sV.nchips <= 0) return;
    uText(c, x0, y0, sV.chipsTitle, DIM, 0.66f, HUD_FONT_BANK, HUD_ALIGN_LEFT, HUD_FX_SHADOW);
    for (i = 0; i < sV.nchips; i++)
    {
        const UiChip *k = &sV.chip[i];
        const s32 tw = hudTextW(k->label, 0.56f, HUD_FONT_BANK), cw = tw + 12;
        const f32 at = 1.2f + 0.05f * (f32) i, pop = clamp01((t - at) / 0.25f);
        const f32 saved = sA;

        if (x + cw > x1) { x = x0; y += 15; }
        if (y > y0 + 15 + 2 * 15) break;   /* three rows at most (the earned ones come first) */
        sA *= pop;
        if (k->on)
        {
            hudFill(c, x, y, x + cw, y + 12, A(hudLerp(0xFFF4C0FFu, 0xC89A38FFu, hudEaseOutBack(pop, 1.7f))));
            uText(c, x + 6, y + 2, k->label, 0x1A1006FFu, 0.56f, HUD_FONT_BANK, HUD_ALIGN_LEFT, 0);
        }
        else
        {
            hudFill(c, x, y, x + cw, y + 12, A(0x140E08A0u));
            hudFrame(c, x, y, x + cw, y + 12, 1, A(0x3A302080u));
            uText(c, x + 6, y + 2, k->label, 0x6A6050FFu, 0.56f, HUD_FONT_BANK, HUD_ALIGN_LEFT, 0);
        }
        sA = saved;
        x += cw + 4;
    }
}

/* ---- layout ---------------------------------------------------------------------------------------------------------------------- */

static void layout(s32 ox, s32 oy, s32 colTop2)
{
    s32 i, y1 = oy + 112, y2 = colTop2;

    if (sV.layout == UIL_SETTINGS) y1 = y2 = oy + 102;
    if (sV.layout == UIL_DEBRIEF)
    {   /* the buttons: one centred row */
        s32 tw = 0, x;

        for (i = 0; i < sV.n; i++) tw += (sV.item[i].kind == UII_PRIMARY ? 176 : 160) + (i > 0 ? 12 : 0);
        x = ox + (640 - tw) / 2;
        for (i = 0; i < sV.n; i++)
        {
            const s32 bw = sV.item[i].kind == UII_PRIMARY ? 176 : 160;

            sHit[i].x0 = x; sHit[i].x1 = x + bw;
            sHit[i].y0 = oy + 400; sHit[i].y1 = oy + 400 + 34;
            sHit[i].ok = 1;
            x += bw + 12;
        }
        return;
    }
    for (i = 0; i < sV.n; i++)
    {
        const UiItem *it = &sV.item[i];
        const s32 h = itemH(it->kind);
        const int right = it->col == 2;
        s32 *y = right ? &y2 : &y1;
        const s32 gap = it->kind == UII_CARD ? 4 : (sV.layout == UIL_SETTINGS ? 1 : 4);

        if (it->kind == UII_HEADER && *y > (sV.layout == UIL_SETTINGS ? oy + 102 : oy + 112)) *y += 4;
        if (it->kind == UII_PRIMARY || it->kind == UII_BUTTON) *y += (sV.layout == UIL_SETTINGS ? 4 : 2);
        sHit[i].x0 = right ? ox + 330 : ox + 22;
        sHit[i].x1 = right ? ox + 618 : (sV.layout == UIL_SETTINGS ? ox + 312 : ox + 304);
        sHit[i].y0 = *y;
        sHit[i].y1 = *y + h;
        sHit[i].ok = it->kind != UII_HEADER;
        *y += h + gap;
        if (it->kind == UII_PRIMARY && it->sub[0]) *y += 12;
    }
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

static void debriefTop(HudCanvas *c, s32 ox, s32 oy)
{
    const f32 t = since(sScreenTick);
    const f32 pop = t < 0.35f ? 1.0f + 0.7f * (1.0f - hudEaseOut(t / 0.35f)) : 1.0f;   /* the banner pops in with a 1.7x overshoot */
    const f32 bsc = 3.3f * pop;
    const s32 cx = c->w / 2;

    (void) ox;
    uText(c, cx, oy + 18, sV.kicker, DIM, 0.8f, HUD_FONT_BANK, HUD_ALIGN_CENTRE, HUD_FX_SHADOW);
    uTextFit(c, cx, oy + 30 - (s32) ((bsc - 3.3f) * 8.0f), sV.banner, sV.bannerRgba ? sV.bannerRgba : GOLD, bsc, HUD_FONT_ZURICH, HUD_ALIGN_CENTRE,
             HUD_FX_OUTLINE, (s32) (600.0f * pop), 1.4f);
    hudGradientH(c, cx - 220, oy + 86, cx, oy + 88, A(0xFFFFFF00u & GOLD), A(GOLD), 12);
    hudGradientH(c, cx, oy + 86, cx + 220, oy + 88, A(GOLD), A(0xFFFFFF00u & GOLD), 12);
    uTextFit(c, cx, oy + 94, sV.subtitle, INK, 0.85f, HUD_FONT_BANK, HUD_ALIGN_CENTRE, HUD_FX_SHADOW, 600, 0.55f);
    if (sV.medal >= 0 && sV.medalName[0])
    {   /* the rating / medal line */
        const u32 mc = sV.medalRgba ? sV.medalRgba : kMedal[sV.medal > 4 ? 4 : sV.medal];
        const f32 mt = clamp01((t - 0.3f) / 0.3f);
        const f32 saved = sA;
        const s32 tw = hudTextW(sV.medalName, 1.1f, HUD_FONT_ZURICH);

        sA *= mt;
        hudFill(c, cx - tw / 2 - 18, oy + 112, cx - tw / 2 - 8, oy + 122, A(mc));
        uText(c, cx, oy + 109, sV.medalName, mc, 1.1f, HUD_FONT_ZURICH, HUD_ALIGN_CENTRE, HUD_FX_OUTLINE);
        if (sV.medalNew) uText(c, cx + tw / 2 + 10, oy + 112, sV.newMedal, GOLD, 0.8f, HUD_FONT_BANK, HUD_ALIGN_LEFT, HUD_FX_SHADOW);
        sA = saved;
    }
    if (sV.news[0])
    {
        const f32 pp = hudPulse(sNow, 1.2f, 1.0f) - 1.0f;

        uTextFit(c, cx, oy + 132, sV.news, hudLerp(GOLD, 0xFFF4C0FFu, 0.5f + 0.5f * pp), 0.85f, HUD_FONT_BANK, HUD_ALIGN_CENTRE, HUD_FX_SHADOW, 600, 0.55f);
    }
}

/* port_console.c portConRender, in a mode, over the HUD */
Gfx *portUiMenuRender(Gfx *gdl)
{
    HudCanvas c;
    s32 ox, oy, i, colTop2;

    if (!portUiMenuViewOn()) return gdl;
    gdl = hudBegin(gdl, &c);
    if (!c.ok) return gdl;
    sNow = (f32) g_GlobalTimer / 60.0f;
    sA = hudEaseOut(clamp01(since(sScreenTick) / 0.28f));
    ox = (c.w - 640) / 2;
    oy = (c.h - 480) / 2;
    backdrop(&c);
    colTop2 = oy + 112;
    if (sV.layout == UIL_DEBRIEF)
    {
        const s32 rows = (sV.ncells + 3) / 4;
        const s32 low = oy + 150 + rows * 56 + 8;   /* under the cells: the checklist left, the chart and the chips right */

        debriefTop(&c, ox, oy);
        statCells(&c, ox, oy + 150);
        checklist(&c, ox + 28, low, ox + 300);
        if (sV.nbars > 0)
        {
            bars(&c, ox + 330, low + 16, ox + 612, low + 46);
            chips(&c, ox + 330, low + 64, ox + 612);
        }
        else chips(&c, ox + 330, low, ox + 612);
    }
    else
    {
        titleBlock(&c, ox, oy);
        if (sV.layout == UIL_LOBBY && (sV.ninfo > 0 || sV.infoSub[0])) colTop2 = infoCard(&c, ox + 330, oy + 112, ox + 618) + 10;
    }
    layout(ox, oy, colTop2);
    sHitPage = sV.page;
    for (i = 0; i < sV.n; i++) drawItem(&c, i, &sHit[i]);
    if (sV.desc[0])
    {   /* the strip at the bottom: what the selected thing does */
        const s32 y = oy + 442;

        hudGradientH(&c, ox + 22, y, ox + 618, y + 18, A(0x3A2810D0u), A(0x3A281030u), 12);
        hudFill(&c, ox + 22, y, ox + 25, y + 18, A(GOLD));
        uTextFit(&c, ox + 32, y + 4, sV.desc, 0xFFF0C0FFu, 0.8f, HUD_FONT_BANK, HUD_ALIGN_LEFT, HUD_FX_SHADOW, 580, 0.5f);
    }
    if (sV.footer[0]) uTextFit(&c, c.w / 2, c.h - 14, sV.footer, DIM, 0.7f, HUD_FONT_BANK, HUD_ALIGN_CENTRE, HUD_FX_SHADOW, c.w - 40, 0.45f);
    cursorArrow(&c);
    {
        static s32 warned;

        if (!warned && (c.end - c.g) < 1200)
        {
            warned = 1;
            fprintf(stdout, "ge: uimenu: the HUD display list is nearly full (%d left)\n", (int) (c.end - c.g));
        }
    }
    return hudEnd(gdl, &c);
}

/* ---- the mouse ------------------------------------------------------------------------------------------------------------------- */

static void click(s32 item, s32 act)
{
    const int me = portLocalPlayer();

    portSessionCmdRun(PORT_NETCMD_PLAN_POINT, me < 0 ? 0 : me, sHitPage * 64 + item, act);
}

/* port_intel.c portIntelMouse, every poll: while the menu is up the mouse is its pointer (1 = taken: no bind sees the buttons) */
int portUiMenuMouse(int mx, int my, int winw, int winh, unsigned int mbuttons, int wheelUp, int wheelDown)
{
    static s32 lastx = -99999, lasty = -99999;
    s32 w, h, i, over = -1;
    u32 edge;

    if (!portUiMenuViewOn() || winw <= 0 || winh <= 0)
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
    if (sHitPage == sV.page && since(sPtrMoveTick) < 6.0f)
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
