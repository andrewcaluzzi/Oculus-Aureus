/**
 * port_arenahud.c - the arena's own HUD (AC's playtest, 2026-10-27): the wave panel on the left and the kill callouts at the upper
 * centre, drawn with port_hudstyle.c in the stock fonts. No new art.
 *
 * The mode script feeds it (port_luavm_arena.c): arena.panel{...} with the wave state every half second, arena.callout(text, level,
 * sub) for a chain or streak tier, arena.hud(on). While it is on, port_console.c draws this instead of the plain mode HUD rows; the
 * mode still writes those rows (tests and GE_MODEHUD_LOG read them), and the panel's message strip shows this machine's line 3 (so
 * a co-op Bond's own DOWN / REVIVING text appears there).
 *
 * PRESENTATION ONLY (snap_embed.py: PRES): nothing here is read by the sim. Times are sim ticks (g_GlobalTimer) stamped when Lua
 * sets them, so a rollback re-simulation that sets the same callout on the same tick does not restart it.
 */
#ifndef TARGET_N64

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ultra64.h>

#include "port_hudstyle.h"
#include "port_blocks.h"
#include "port_mode.h"

extern s32 g_GlobalTimer;

#define CALLOUT_LEN 40

typedef struct
{
    s32 wave, waves, kills, left, elites, phase;   /* phase: 0 get ready, 1 fight, 2 breather, 3 over */
    s32 crowd, tier;                               /* crowd 0..100 (-1 = favour off), tier 1..5 */
    s32 dbl;                                       /* seconds of sponsor double damage left */
    char title[40], mutator[32], time[12], tierName[24], toast[48];
    char phaseLabel[20], idleTitle[24], idleSub[32];   /* the theme's words (empty: the built-in ones) */
    s32 credits;                                   /* arena plus: the credits (-1 = none, no row) */
    s32 nchips;                                    /* arena plus: power-up timer chips under the stats */
    struct { char label[24]; s32 secs; u32 rgba; } chip[4];
} Panel;

typedef struct
{
    char text[CALLOUT_LEN], sub[CALLOUT_LEN];
    s32 level, tick;
    f32 life;
    u32 seed;   /* picks the entrance and the exit (a hash of the text and the tick) */
} Callout;

static s32 sOn;
static Panel sP;
static s32 sWaveTick = -100000;
static f32 sCrowdShown = -1.0f;
static Callout sCall;
static s32 sCallSet;

/* ---- Lua side ---- */

extern int g_portCoopEpoch;   /* port_coop.c: counts stage starts */
static s32 sEpoch = -1000;     /* the stage arena.hud(true) was called in: the styled HUD never carries into the next stage */

void portArenaHudOn(int on)
{
    sOn = on ? 1 : 0;
    sEpoch = g_portCoopEpoch;
    if (!sOn)
    {
        sCallSet = 0;
    }
}

int portArenaHudActive(void) { return sOn && sEpoch == g_portCoopEpoch && portModeActive(); }

static void cpy(char *d, size_t n, const char *s) { snprintf(d, n, "%s", s != NULL ? s : ""); }

void portArenaHudPanel(int wave, int waves, const char *title, const char *mutator, int kills, int left, int elites, const char *time,
                       int phase, int crowd, int tier, const char *tierName, const char *toast, int dbl)
{
    if (wave != sP.wave && wave > 0)
    {
        sWaveTick = (s32) g_GlobalTimer;   /* the wave number pops */
    }
    sP.wave = wave;
    sP.waves = waves;
    sP.kills = kills;
    sP.left = left;
    sP.elites = elites;
    sP.phase = phase;
    sP.crowd = crowd;
    sP.tier = tier < 1 ? 1 : (tier > 5 ? 5 : tier);
    sP.dbl = dbl;
    cpy(sP.title, sizeof(sP.title), title);
    cpy(sP.mutator, sizeof(sP.mutator), mutator);
    cpy(sP.time, sizeof(sP.time), time);
    cpy(sP.tierName, sizeof(sP.tierName), tierName);
    cpy(sP.toast, sizeof(sP.toast), toast);
}

/* the theme's words for the panel (arena.panel's phase_label / idle_title / idle_sub) */
void portArenaHudPanelWords(const char *phaseLabel, const char *idleTitle, const char *idleSub)
{
    cpy(sP.phaseLabel, sizeof(sP.phaseLabel), phaseLabel);
    cpy(sP.idleTitle, sizeof(sP.idleTitle), idleTitle);
    cpy(sP.idleSub, sizeof(sP.idleSub), idleSub);
}

/* arena plus (tools/arena_plus.lua): the credits row and the power-up timer chips (up to 4: label, seconds left, colour) */
void portArenaHudExtras(int credits, int n, const char labels[][24], const int *secs, const unsigned *rgba)
{
    int i;

    sP.credits = credits;
    sP.nchips = n < 0 ? 0 : (n > 4 ? 4 : n);
    for (i = 0; i < sP.nchips; i++)
    {
        cpy(sP.chip[i].label, sizeof(sP.chip[i].label), labels[i]);
        sP.chip[i].secs = secs[i];
        sP.chip[i].rgba = rgba[i];
    }
}

/* level 1..6 (colour and size climb with it); life seconds (0 = by level) */
void portArenaHudCallout(const char *text, int level, const char *sub, float life)
{
    const s32 now = (s32) g_GlobalTimer;

    if (text == NULL || text[0] == '\0')
    {
        return;
    }
    if (sCallSet && sCall.tick == now && strncmp(sCall.text, text, CALLOUT_LEN - 1) == 0)
    {
        return;   /* the same call on the same tick: a rollback re-simulation */
    }
    cpy(sCall.text, sizeof(sCall.text), text);
    cpy(sCall.sub, sizeof(sCall.sub), sub);
    sCall.level = level < 1 ? 1 : (level > 6 ? 6 : level);
    sCall.tick = now;
    sCall.life = life > 0.0f ? life : 1.6f + 0.2f * (f32) sCall.level;
    {   /* FNV-1a of the text, mixed with the tick */
        u32 h = 2166136261u;
        const char *s;

        for (s = sCall.text; *s; s++) h = (h ^ (u8) *s) * 16777619u;
        sCall.seed = h ^ ((u32) now * 2654435761u);
    }
    sCallSet = 1;
}

/* ---- drawing ---- */

static const u32 kTierCol[6] = { 0, 0xC03028FFu, 0xD8902CFFu, 0xC8C8B8FFu, 0x58D068FFu, 0xF0C838FFu };   /* HATED .. ADORED */
static const u32 kCallCol[7] = { 0, 0xFFE860FFu, 0xFFB830FFu, 0xFF8020FFu, 0xFF4020FFu, 0xF01838FFu, 0xFF2878FFu };

#define INK    0xF4EEDCFFu   /* parchment white */
#define GOLD   0xF0C040FFu
#define DIM    0xB8B0A0FFu
#define SHADE  0x000000FFu

static f32 secsSince(s32 tick) { return (f32) ((s32) g_GlobalTimer - tick) / 60.0f; }

/* the panel's layout (arena.hud_layout; ARENA_SETTINGS hud_scale / hud_anchor / hud_margin): size against the 640-unit console
 * space, the corner (0 top left, 1 top right) and the gap to the screen edges in console units. The panel is laid out in its own
 * units (PANEL_W wide) and drawn through the canvas transform. */
#define PANEL_W 300
static f32 sScale = 0.65f;
static s32 sAnchor, sMargin = 4;

void portArenaHudLayout(float scale, int anchor, int margin)
{
    sScale = scale < 0.25f ? 0.25f : (scale > 1.5f ? 1.5f : scale);
    sAnchor = anchor == 1 ? 1 : 0;
    sMargin = margin < 0 ? 0 : (margin > 100 ? 100 : margin);
}

/* text sizes inside the panel (panel units; the whole panel is then drawn at hud_scale). AC's pt3 playtest at 2000x1125: the labels were
 * 8-10 px, so the small tier is ~1.5x what it was, the values (kills, left, time) use the title's face, and the hierarchy is kept:
 * the wave digit > the title > the values > the labels. */
#define T_BIG    3.2f    /* the wave number, Zurich */
#define T_TITLE  1.15f   /* the wave name, Zurich */
#define T_VALUE  1.0f    /* kills / left / time, Zurich */
#define T_LABEL  1.15f   /* WAVE / KILLS / LEFT / ELITES, Bank Gothic */
#define T_PHASE  1.2f    /* FIGHT / BREATHER / GET READY */
#define T_TIER   1.25f   /* the crowd's mood */
#define T_TOAST  1.1f
#define T_MUT    1.15f
#define T_MSG    1.2f

static void drawPanel(HudCanvas *c)
{
    const s32 x0 = 0, y0 = 0, w = PANEL_W;
    s32 y = y0, x1 = x0 + w;
    char buf[64];
    const f32 since = secsSince(sWaveTick);
    const f32 flash = since >= 0.0f && since < 1.2f ? 1.0f - since / 1.2f : 0.0f;
    const char *msg = portModeHudGetLocal(2);
    s32 h;

    if (sP.crowd >= 0)
    {
        sCrowdShown = sCrowdShown < 0.0f ? (f32) sP.crowd : sCrowdShown + ((f32) sP.crowd - sCrowdShown) * 0.12f;
    }

    /* the height: the header (wave + title), the mutator tag, the stats row, the crowd block */
    h = 72 + (sP.mutator[0] ? 22 : 0) + 24 + (sP.credits >= 0 ? 22 : 0) + (sP.nchips > 0 ? 22 : 0) + (sP.crowd >= 0 ? 36 : 0) + 2;
    hudSetTransform(c, sAnchor == 1 ? c->w - sMargin - (s32) ((f32) PANEL_W * sScale) : sMargin, sMargin, sScale);

    /* backing: a dark plate fading to the right, a gold rule on the left edge, a frame that flashes when the wave changes */
    hudFill(c, x0 + 2, y0 + 2, x1 + 2, y0 + h + 2, 0x00000060u);
    hudGradientH(c, x0, y0, x1, y0 + h, 0x140E08E0u, 0x140E0870u, 12);
    hudFill(c, x0, y0, x0 + 3, y0 + h, hudLerp(0x8A6A28FFu, GOLD, flash));
    hudFrame(c, x0, y0, x1, y0 + h, 1, hudLerp(0x6A5838A0u, 0xFFE070FFu, flash));

    if (sP.phase == 3)
    {   /* the run is over: the mode's own end lines (medal, best) */
        const char *l1 = portModeHudGetLocal(0), *l2 = portModeHudGetLocal(1);

        hudText(c, x0 + 10, y + 8, l1 ? l1 : "", GOLD, 1.4f, HUD_FONT_ZURICH, HUD_ALIGN_LEFT, HUD_FX_SHADOW, SHADE);
        hudText(c, x0 + 10, y + 38, l2 ? l2 : "", INK, T_VALUE, HUD_FONT_ZURICH, HUD_ALIGN_LEFT, HUD_FX_SHADOW, SHADE);
    }
    else if (sP.wave <= 0)
    {
        hudText(c, x0 + 10, y + 10, sP.idleTitle[0] ? sP.idleTitle : "COLOSSEUM", GOLD, 1.8f, HUD_FONT_ZURICH, HUD_ALIGN_LEFT, HUD_FX_SHADOW, SHADE);
        hudText(c, x0 + 12, y + 44, sP.idleSub[0] ? sP.idleSub : "THE CROWD AWAITS", DIM, T_PHASE, HUD_FONT_BANK, HUD_ALIGN_LEFT, HUD_FX_SHADOW, SHADE);
    }
    else
    {   /* WAVE  12 /20 : the number big, popping in when it changes */
        const f32 pop = since >= 0.0f && since < 0.35f ? 1.0f + 0.5f * (1.0f - hudEaseOut(since / 0.35f)) : 1.0f;
        const f32 big = T_BIG * pop;
        s32 nx;

        hudText(c, x0 + 12, y + 4, "WAVE", DIM, T_LABEL, HUD_FONT_BANK, HUD_ALIGN_LEFT, HUD_FX_SHADOW, SHADE);
        snprintf(buf, sizeof(buf), "%d", (int) sP.wave);
        nx = x0 + 12;
        hudText(c, nx, y + 17 - (s32) ((big - T_BIG) * 8.0f), buf, hudLerp(0xFFD050FFu, 0xFFFFFFFFu, flash), big, HUD_FONT_ZURICH,
                HUD_ALIGN_LEFT, HUD_FX_OUTLINE, SHADE);
        nx += hudTextW(buf, T_BIG, HUD_FONT_ZURICH) + 3;
        if (sP.waves > 0)
        {
            snprintf(buf, sizeof(buf), "/%d", (int) sP.waves);
            hudText(c, nx, y + 40, buf, DIM, T_VALUE, HUD_FONT_ZURICH, HUD_ALIGN_LEFT, HUD_FX_SHADOW, SHADE);
            nx += hudTextW(buf, T_VALUE, HUD_FONT_ZURICH);
        }
        {   /* the wave's name and the phase, right of the number block (a thin divider between); the name shrunk to fit */
            const s32 tx = nx + 14 > x0 + 92 ? nx + 14 : x0 + 92;
            const s32 room = x1 - 8 - tx, tw = hudTextW(sP.title, T_TITLE, HUD_FONT_ZURICH);
            const char *ph = sP.phaseLabel[0] ? sP.phaseLabel : sP.phase == 2 ? "BREATHER" : (sP.phase == 0 ? "GET READY" : "FIGHT");
            f32 ts = (tw > room && tw > 0) ? T_TITLE * (f32) room / (f32) tw : T_TITLE;

            if (ts < 0.6f) ts = 0.6f;
            hudFill(c, tx - 7, y + 10, tx - 6, y + 64, 0x6A583880u);
            hudText(c, tx, y + 14 + (s32) ((T_TITLE - ts) * 6.0f), sP.title, INK, ts, HUD_FONT_ZURICH, HUD_ALIGN_LEFT, HUD_FX_SHADOW, SHADE);
            hudText(c, tx, y + 42, ph, sP.phase == 1 ? 0xF07050FFu : 0x90D0F0FFu, T_PHASE, HUD_FONT_BANK, HUD_ALIGN_LEFT, HUD_FX_SHADOW,
                    SHADE);
            if (sP.elites > 0)
            {   /* live affix elites, after the phase word */
                snprintf(buf, sizeof(buf), "ELITES %d", (int) sP.elites);
                hudText(c, x1 - 8, y + 42, buf, 0xFFB040FFu, T_LABEL, HUD_FONT_BANK, HUD_ALIGN_RIGHT, HUD_FX_SHADOW, SHADE);
            }
        }
    }
    y += 72;
    if (sP.mutator[0])
    {   /* the mutator tag: a purple chip */
        const s32 tw = hudTextW(sP.mutator, T_MUT, HUD_FONT_BANK) + 14;

        hudFill(c, x0 + 10, y - 2, x0 + 10 + tw, y + 16, 0x6A28A0E8u);
        hudFrame(c, x0 + 10, y - 2, x0 + 10 + tw, y + 16, 1, 0xC890F0FFu);
        hudText(c, x0 + 17, y + 1, sP.mutator, 0xF4E8FFFFu, T_MUT, HUD_FONT_BANK, HUD_ALIGN_LEFT, HUD_FX_SHADOW, SHADE);
        y += 22;
    }
    /* the stats row: labels, then the values in the title's face; the clock on the right */
    {
        s32 x = x0 + 12;

        hudFill(c, x0 + 8, y - 2, x1 - 6, y - 1, 0x6A583860u);
        hudText(c, x, y + 3, "KILLS", DIM, T_LABEL, HUD_FONT_BANK, HUD_ALIGN_LEFT, HUD_FX_SHADOW, SHADE);
        x += hudTextW("KILLS", T_LABEL, HUD_FONT_BANK) + 6;
        snprintf(buf, sizeof(buf), "%d", (int) sP.kills);
        hudText(c, x, y, buf, INK, T_VALUE, HUD_FONT_ZURICH, HUD_ALIGN_LEFT, HUD_FX_SHADOW, SHADE);
        x += hudTextW(buf, T_VALUE, HUD_FONT_ZURICH) + 16;
        if (sP.phase == 1 && sP.left >= 0)
        {
            hudText(c, x, y + 3, "LEFT", DIM, T_LABEL, HUD_FONT_BANK, HUD_ALIGN_LEFT, HUD_FX_SHADOW, SHADE);
            x += hudTextW("LEFT", T_LABEL, HUD_FONT_BANK) + 6;
            snprintf(buf, sizeof(buf), "%d", (int) sP.left);
            hudText(c, x, y, buf, 0xFF9868FFu, T_VALUE, HUD_FONT_ZURICH, HUD_ALIGN_LEFT, HUD_FX_SHADOW, SHADE);
        }
        hudText(c, x1 - 8, y, sP.time, INK, T_VALUE, HUD_FONT_ZURICH, HUD_ALIGN_RIGHT, HUD_FX_SHADOW, SHADE);
        y += 24;
    }
    if (sP.credits >= 0)
    {   /* arena plus: the credits, in the stats row's type */
        s32 x = x0 + 12;

        hudText(c, x, y + 3, "CREDITS", DIM, T_LABEL, HUD_FONT_BANK, HUD_ALIGN_LEFT, HUD_FX_SHADOW, SHADE);
        x += hudTextW("CREDITS", T_LABEL, HUD_FONT_BANK) + 6;
        snprintf(buf, sizeof(buf), "%d", (int) sP.credits);
        hudText(c, x, y, buf, GOLD, T_VALUE, HUD_FONT_ZURICH, HUD_ALIGN_LEFT, HUD_FX_SHADOW, SHADE);
        y += 22;
    }
    if (sP.nchips > 0)
    {   /* arena plus: one chip per running power-up, with the seconds left */
        s32 x = x0 + 10, k;

        for (k = 0; k < sP.nchips; k++)
        {
            char txt[40];
            s32 tw;

            snprintf(txt, sizeof(txt), "%s %ds", sP.chip[k].label, (int) sP.chip[k].secs);
            tw = hudTextW(txt, T_MUT, HUD_FONT_BANK) + 12;
            hudFill(c, x, y - 2, x + tw, y + 16, hudLerp(sP.chip[k].rgba, 0x000000FFu, 0.55f));
            hudFrame(c, x, y - 2, x + tw, y + 16, 1, sP.chip[k].rgba);
            hudText(c, x + 6, y + 1, txt, 0xFFFFFFFFu, T_MUT, HUD_FONT_BANK, HUD_ALIGN_LEFT, HUD_FX_SHADOW, SHADE);
            x += tw + 4;
        }
        y += 22;
    }
    if (sP.crowd >= 0)
    {   /* the crowd favour: a thin bar in the tier's colour, the mood to its right, the last toast under it */
        const u32 tc = kTierCol[sP.tier];
        const s32 tierW = hudTextW(sP.tierName, T_TIER, HUD_FONT_BANK);
        const s32 bx0 = x0 + 12, bx1 = x1 - 14 - (tierW > 60 ? tierW : 60), by = y + 4;
        const s32 fillx = bx0 + (s32) ((f32) (bx1 - bx0) * (sCrowdShown < 0.0f ? 0.0f : sCrowdShown) / 100.0f);
        s32 k;

        hudFill(c, bx0, by, bx1, by + 7, 0x00000090u);
        hudGradientH(c, bx0, by, fillx, by + 7, hudLerp(tc, 0x000000FFu, 0.45f), tc, 6);
        for (k = 1; k < 5; k++)
        {   /* tier ticks at 20 / 40 / 60 / 80 */
            const s32 tx = bx0 + (bx1 - bx0) * k / 5;

            hudFill(c, tx, by - 1, tx + 1, by + 8, 0x000000C0u);
        }
        hudFrame(c, bx0 - 1, by - 1, bx1 + 1, by + 8, 1, 0x6A5838C0u);
        hudText(c, x1 - 8, y, sP.tierName, tc, T_TIER, HUD_FONT_BANK, HUD_ALIGN_RIGHT, HUD_FX_SHADOW, SHADE);
        if (sP.toast[0])
        {
            hudText(c, x0 + 12, y + 18, sP.toast, 0xFFE8A0FFu, T_TOAST, HUD_FONT_BANK, HUD_ALIGN_LEFT, HUD_FX_SHADOW, SHADE);
        }
        else if (sP.dbl > 0)
        {
            snprintf(buf, sizeof(buf), "2X DAMAGE %ds", (int) sP.dbl);
            hudText(c, x0 + 12, y + 18, buf, 0xFF6060FFu, T_TOAST, HUD_FONT_BANK, HUD_ALIGN_LEFT, HUD_FX_SHADOW, SHADE);
        }
        y += 36;
    }
    y += 2;
    if (msg != NULL && msg[0] != '\0')
    {   /* this machine's message line (announcements, co-op DOWN / REVIVING): a strip under the panel */
        const s32 tw = hudTextW(msg, T_MSG, HUD_FONT_BANK);
        const s32 sx1 = x0 + (tw + 22 > w ? tw + 22 : w);

        hudGradientH(c, x0, y + 2, sx1, y + 22, 0x5A3810D8u, 0x5A381048u, 10);
        hudFill(c, x0, y + 2, x0 + 3, y + 22, GOLD);
        hudText(c, x0 + 10, y + 6, msg, 0xFFF0C0FFu, T_MSG, HUD_FONT_BANK, HUD_ALIGN_LEFT, HUD_FX_SHADOW, SHADE);
    }
    hudSetTransform(c, 0, 0, 1.0f);
}

/* ---- the callout's show (AC: "go bonkers"): an entrance, a hold, an exit, all escalating with the level (1..6) -------------------
 * Entrances: SLAM (from huge, overshoot and bounce), SPIN (a card flip: the glyphs squash in x by |cos|, turning one to three times),
 * DROP (letters fall in one by one and bounce), ZOOM (through the camera: from 4.5x and transparent), SCRAMBLE (random letters resolving
 * left to right), SPLIT (the halves slide in from the sides and meet with a jolt), ORBIT (the letters circle in from a ring).
 * Hold: a pulse; a wobble from level 2; a shake from level 3; an echo trail and a flare burst behind from level 4; a colour cycle from
 * level 5. Exits: SHRINK, WIPE, FLING (letters thrown off with spin and gravity), SHATTER (scattered outward).
 * Level 5-6: a screen flash and a short freeze (the TEXT holds still; the sim never stops), a vignette; level 6 a gunbarrel of rings
 * behind the text. Each level picks among its own entrances with a presentation-only hash of the callout (the same pick on every
 * machine and after a rollback). Nothing here touches the sim. */
enum { EN_SLAM, EN_SPIN, EN_DROP, EN_ZOOM, EN_SCRAMBLE, EN_SPLIT, EN_ORBIT, EN_N };
enum { EX_SHRINK, EX_WIPE, EX_FLING, EX_SHATTER, EX_N };

static const u8 kEnter[7][5] = { { 0 }, { EN_SLAM, EN_DROP, EN_SPLIT }, { EN_SLAM, EN_SPIN, EN_DROP, EN_SCRAMBLE },
                                 { EN_SPIN, EN_ZOOM, EN_SCRAMBLE, EN_ORBIT, EN_SPLIT }, { EN_SPIN, EN_ZOOM, EN_ORBIT, EN_SCRAMBLE, EN_DROP },
                                 { EN_ZOOM, EN_ORBIT, EN_SPIN }, { EN_ZOOM, EN_ORBIT, EN_SPIN } };
static const u8 kEnterN[7] = { 0, 3, 4, 5, 5, 3, 3 };
static const u8 kExit[7][3] = { { 0 }, { EX_SHRINK, EX_WIPE }, { EX_SHRINK, EX_WIPE }, { EX_FLING, EX_SHATTER, EX_WIPE },
                                { EX_FLING, EX_SHATTER, EX_WIPE }, { EX_FLING, EX_SHATTER }, { EX_FLING, EX_SHATTER } };
static const u8 kExitN[7] = { 0, 2, 2, 3, 3, 2, 2 };

static u32 hash32(u32 x)
{
    x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16;
    return x;
}

static f32 frand(u32 seed, u32 k) { return (f32) (hash32(seed ^ (k * 0x9E3779B9u)) & 0xFFFFu) / 65535.0f; }   /* 0..1 */

static f32 clamp01(f32 v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

typedef struct
{
    f32 t, te, enterSecs, hold, q;   /* time; entrance progress; its length; hold time (after the freeze); exit progress (0 = none) */
    s32 lv, en, ex;
    u32 seed, col;
    f32 alpha, lineScale;
    f32 shakeX, shakeY;
    f32 fit;   /* the safe-area fit (calloutFit): the whole line drawn this much smaller about its centre; 0 or 1 = none */
} CallAnim;

static void letterFx(s32 i, s32 n, HudLetter *l, void *ud)
{
    const CallAnim *A = (const CallAnim *) ud;
    const f32 fi = (f32) i;
    f32 e = A->te;

    /* ---- the entrance ---- */
    switch (A->en)
    {
        case EN_DROP:
        {   /* each letter 0.04 s after the last, from 140 units up, bouncing */
            const f32 p = clamp01((A->t - fi * 0.04f) / 0.38f);

            l->dy -= (1.0f - hudEaseOutBounce(p)) * 140.0f;
            if (p <= 0.0f) l->hide = 1;
            break;
        }
        case EN_SPIN:
        {   /* a card flip about the line's centre: the turns slow down to rest */
            const f32 turns = 1.0f + (f32) (A->lv / 2);
            const f32 ang = (1.0f - hudEaseOut(e)) * turns * 6.2831853f;
            const f32 sx = cosf(ang);

            l->sx = sx < 0.0f ? -sx : sx;
            l->dx += l->ox * (l->sx - 1.0f);
            if (sx < 0.0f) l->rgba = hudLerp(l->rgba, 0x000000FFu, 0.35f);   /* the back of the card is darker */
            break;
        }
        case EN_ZOOM:
        {   /* through the camera: from 4.5x and transparent */
            const f32 s = 1.0f + 3.5f * (1.0f - hudEaseOut(e));

            l->scale *= s;
            l->dx += l->ox * (s - 1.0f);
            l->rgba = hudAlpha(l->rgba, clamp01(e * 1.6f));
            break;
        }
        case EN_SCRAMBLE:
        {   /* random letters until this one resolves (left to right) */
            const f32 p = (A->t - 0.12f - fi * 0.035f);

            if (p < 0.0f)
            {
                static const char kGlyphs[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789#$%&*?!";
                const u32 k = (u32) (A->t * 30.0f) + (u32) i * 7u;

                l->ch = kGlyphs[hash32(A->seed + k) % (sizeof(kGlyphs) - 1)];
                l->rgba = hudLerp(l->rgba, 0x60FF60FFu, 0.6f);   /* a code-breaker green while it scrambles */
            }
            break;
        }
        case EN_SPLIT:
        {   /* the halves meet in the middle */
            l->dx += (l->ox < 0.0f ? -1.0f : 1.0f) * (1.0f - hudEaseOutBack(e, 0.15f)) * 420.0f;
            break;
        }
        case EN_ORBIT:
        {   /* from a spinning ring into place */
            const f32 a0 = 6.2831853f * fi / (f32) (n > 0 ? n : 1) + (1.0f - e) * 9.0f;
            const f32 r = (1.0f - hudEaseOut(e)) * 170.0f;

            l->dx += (cosf(a0) * r - l->ox) * (1.0f - hudEaseOut(e));
            l->dy += sinf(a0) * r * 0.6f;
            break;
        }
        default:   /* EN_SLAM: the line's scale does it */
            break;
    }

    /* ---- the hold ---- */
    if (A->hold > 0.0f)
    {
        if (A->lv >= 2) l->dy += sinf(A->hold * 9.0f + fi * 0.8f) * (0.8f + 0.5f * (f32) A->lv);   /* wobble */
        if (A->lv >= 5) l->rgba = (l->rgba & 0xFFu) | (hudHue(A->hold * 0.9f + fi * 0.06f, 0xFF) & 0xFFFFFF00u);   /* colour cycle */
    }
    l->dx += A->shakeX;
    l->dy += A->shakeY;

    /* ---- the exit ---- */
    if (A->q > 0.0f)
    {
        const f32 q = A->q;

        switch (A->ex)
        {
            case EX_WIPE:
            {
                const f32 edge = q * ((f32) n + 4.0f) - 2.0f;

                if (fi < edge - 2.0f) l->hide = 1;
                else if (fi < edge) l->rgba = hudAlpha(l->rgba, (fi - (edge - 2.0f)) * 0.5f);
                break;
            }
            case EX_FLING:
            {
                const f32 vx = frand(A->seed, (u32) i * 2u) * 2.0f - 1.0f, vy = -frand(A->seed, (u32) i * 2u + 1u);
                const f32 sx = cosf(q * 3.1415927f * (2.0f + 3.0f * frand(A->seed, (u32) i + 99u)));

                l->dx += vx * q * 520.0f;
                l->dy += vy * q * 300.0f + 700.0f * q * q;   /* gravity */
                l->sx *= sx < 0.0f ? -sx : sx;
                l->rgba = hudAlpha(l->rgba, 1.0f - q);
                break;
            }
            case EX_SHATTER:
            {
                const f32 rx = frand(A->seed, (u32) i + 300u) * 2.0f - 1.0f, ry = frand(A->seed, (u32) i + 400u) * 2.0f - 1.0f;

                l->dx += l->ox * q * 1.6f + rx * q * 220.0f;
                l->dy += ry * q * 160.0f;
                l->scale *= 1.0f + q * 0.8f;
                l->rgba = hudAlpha(l->rgba, 1.0f - q);
                break;
            }
            default:   /* EX_SHRINK */
                l->scale *= 1.0f - q;
                l->dx -= l->ox * q;
                l->rgba = hudAlpha(l->rgba, 1.0f - q * 0.5f);
                break;
        }
    }
    l->dx += l->ox * (A->lineScale - 1.0f);   /* the line's own scale (slam, pulse) spreads the letters too */
    l->scale *= A->lineScale;
    l->rgba = hudAlpha(l->rgba, A->alpha);
    if (A->fit > 0.0f && A->fit < 1.0f)
    {   /* the safe-area fit: positions and sizes about the line's centre */
        l->dx = (l->ox + l->dx) * A->fit - l->ox;
        l->scale *= A->fit;
    }
}

/* The safe area (AC's 1280x720 playtest: a level-5 "THE WORLD IS NOT ENOUGH" overran both screen edges in a mid-entrance frame of
 * ZOOM, which starts at 4.5x, and the hold's pulse and shake add a little more). The console space is 480 high and 480 x aspect wide
 * (video.c portConsoleSpace: 853 at 16:9, 640 at 4:3), so a long line that fits a wide screen can still overrun a 4:3 one. The title
 * safe area is the middle 90% of the width. calloutFit measures the letters exactly as letterFx will place them on this frame
 * (hudTextLettersExtent: the same layout and callback, nothing drawn) and returns the factor that brings the wider side inside it,
 * so a zoom or a slam is held at the largest size that fits and the resting line is never touched when it already fits. The exits
 * are measured as the hold (q = 0) so FLING and SHATTER still throw the letters off-screen as they fade, at the hold's size. */
#define CALLOUT_SAFE 0.90f

static f32 calloutSafeHalf(const HudCanvas *c) { return (f32) c->w * CALLOUT_SAFE * 0.5f; }

static f32 calloutFit(HudCanvas *c, s32 cx, s32 cy, f32 base, const CallAnim *A, f32 *needOut)
{
    CallAnim M = *A;
    f32 x0, x1, raw;
    const f32 half = calloutSafeHalf(c), pad = base + 1.0f;   /* pad: the outline (textFx: ~scale units), which the fit does not scale */

    M.q = 0.0f;
    M.fit = 1.0f;
    *needOut = 0.0f;
    if (!hudTextLettersExtent(c, cx, cy, sCall.text, base, HUD_FONT_ZURICH, HUD_ALIGN_CENTRE, letterFx, &M, &x0, &x1))
    {
        return 1.0f;
    }
    raw = (f32) cx - x0 > x1 - (f32) cx ? (f32) cx - x0 : x1 - (f32) cx;
    *needOut = raw + pad;
    /* (one unit spare for the pixel rounding of each letter's position) */
    return (raw + pad > half && raw > 0.0f) ? (half - pad - 1.0f) / raw : 1.0f;
}

/* GE_CALLOUT_FITLOG=1: a line per drawn frame of every callout with the measured span after the fit (tests/arena_a8/callout-fit.sh
 * checks it never leaves the safe area; =2 logs with the fit off, the before); GE_CALLOUT_ENTRANCE=0..6 forces the entrance
 * (SLAM SPIN DROP ZOOM SCRAMBLE SPLIT ORBIT) */
static s32 sFitLog = -1, sForceEn = -2;

static void calloutFitLog(HudCanvas *c, s32 cx, s32 cy, f32 base, const CallAnim *A, f32 need, const char *what)
{
    CallAnim M = *A;
    f32 x0, x1;

    if (A->q > 0.0f && A->ex != EX_SHRINK && A->ex != EX_WIPE)
    {   /* FLING / SHATTER leave on purpose: log the hold-size line they start from */
        M.q = 0.0f;
    }
    if (hudTextLettersExtent(c, cx, cy, sCall.text, base, HUD_FONT_ZURICH, HUD_ALIGN_CENTRE, letterFx, &M, &x0, &x1))
    {
        extern void portConLogf(const char *fmt, ...);
        const f32 half = calloutSafeHalf(c);

        portConLogf("ge: arena: callout fit: %s lv %d en %d ex %d t %.3f q %.2f need %.1f half %.1f k %.3f span %.1f %.1f safe %.1f %.1f "
                    "w %d text %s\n", what, A->lv, A->en, A->ex, A->t, A->q, need, half, A->fit, x0 - (base + 1.0f), x1 + (base + 1.0f),
                    (f32) cx - half, (f32) cx + half, c->w, sCall.text);
    }
}

/* radial flare rays behind the text (level 4+), as rows of small squares, turning slowly */
static void flare(HudCanvas *c, s32 cx, s32 cy, f32 t, f32 r, u32 col, f32 a)
{
    s32 k, j;

    for (k = 0; k < 14; k++)
    {
        const f32 ang = (f32) k * 0.4487989f + t * 0.6f;
        const f32 dx = cosf(ang), dy = sinf(ang) * 0.55f;

        for (j = 3; j < 18; j++)
        {
            const f32 d = r * (f32) j / 18.0f;
            const s32 sz = 3 - j / 7;
            const u32 cc = hudAlpha(col, a * (1.0f - (f32) j / 18.0f) * 0.55f);

            hudFill(c, cx + (s32) (dx * d) - sz, cy + (s32) (dy * d) - sz, cx + (s32) (dx * d) + sz, cy + (s32) (dy * d) + sz, cc);
        }
    }
}

static void drawCallout(HudCanvas *c)
{
    const s32 lv = sCall.level;
    const f32 enterSecs = 0.42f + 0.05f * (f32) lv;
    const f32 freeze = lv >= 5 ? 0.25f : 0.0f;
    const f32 exitSecs = 0.45f;
    const f32 t = secsSince(sCall.tick);
    CallAnim A;
    f32 base, th;
    s32 cx, cy, tw, pad;

    if (!sCallSet || t < 0.0f || t > sCall.life + exitSecs)
    {
        return;
    }
    memset(&A, 0, sizeof(A));
    A.t = t;
    A.lv = lv;
    A.seed = sCall.seed;
    A.en = kEnter[lv][sCall.seed % kEnterN[lv]];
    A.ex = kExit[lv][(sCall.seed >> 8) % kExitN[lv]];
    if (sForceEn == -2)
    {
        const char *e = getenv("GE_CALLOUT_ENTRANCE"), *g = getenv("GE_CALLOUT_FITLOG");

        sForceEn = (e != NULL && e[0] >= '0' && e[0] < '0' + EN_N) ? e[0] - '0' : -1;
        sFitLog = (g != NULL && g[0] != '\0' && g[0] != '0') ? (g[0] == '2' ? 2 : 1) : 0;   /* 2: log with the fit off (the before) */
    }
    if (sForceEn >= 0)
    {
        A.en = sForceEn;
    }
    A.fit = 1.0f;
    A.enterSecs = enterSecs;
    A.te = clamp01(t / enterSecs);
    A.hold = t > enterSecs + freeze ? t - enterSecs - freeze : 0.0f;
    A.q = t > sCall.life ? clamp01((t - sCall.life) / exitSecs) : 0.0f;
    A.alpha = 1.0f;
    A.col = kCallCol[lv];
    base = 1.6f + 0.2f * (f32) (lv - 1);
    A.lineScale = (A.hold > 0.0f) ? hudPulse(A.hold, 3.0f, 0.04f + 0.01f * (f32) lv) : 1.0f;
    if (A.en == EN_SLAM)
    {   /* from 4x, overshooting below 1 and settling */
        A.lineScale *= 1.0f + 3.0f * (1.0f - hudEaseOutBack(A.te, 0.25f));
        A.alpha = clamp01(A.te * 3.0f);
    }
    if (lv >= 3 && A.q <= 0.0f)
    {   /* a shake that grows with the level (strongest just after the entrance) */
        const f32 amp = (f32) (lv - 2) * 1.6f * (A.hold > 0.0f ? (A.hold < 0.6f ? 1.0f - A.hold / 0.6f * 0.6f : 0.4f) : (t >= enterSecs ? 1.0f : 0.3f));
        const u32 fr = (u32) (t * 60.0f);

        A.shakeX = (frand(A.seed, fr * 2u + 7u) * 2.0f - 1.0f) * amp;
        A.shakeY = (frand(A.seed, fr * 2u + 8u) * 2.0f - 1.0f) * amp;
    }
    cx = c->w / 2;
    cy = (s32) ((f32) c->h * 0.28f);
    tw = hudTextW(sCall.text, base, HUD_FONT_ZURICH);
    {   /* a line too wide for the safe area even at rest is set smaller (the bar, flare and gunbarrel follow tw) */
        const f32 room = 2.0f * (calloutSafeHalf(c) - base - 1.0f);

        if ((f32) tw > room && tw > 0 && sFitLog != 2)
        {
            base *= room / (f32) tw;
            tw = hudTextW(sCall.text, base, HUD_FONT_ZURICH);
        }
    }
    th = (f32) hudTextH(base, HUD_FONT_ZURICH);
    pad = 14 + 3 * lv;
    {
        f32 need;

        A.fit = calloutFit(c, cx, cy, base, &A, &need);
        if (sFitLog == 2)
        {
            A.fit = 1.0f;
        }
        if (sFitLog > 0)
        {
            calloutFitLog(c, cx, cy, base, &A, need, "main");
        }
    }

    /* ---- the top tiers' stage: flash, vignette, gunbarrel ---- */
    if (lv >= 5)
    {
        const f32 ft = t - enterSecs;

        if (ft >= 0.0f && ft < 0.3f)
        {
            hudFill(c, 0, 0, c->w, c->h, hudAlpha(0xFFF4D0FFu, 0.45f * (1.0f - ft / 0.3f)));
        }
        if (A.q < 1.0f)
        {   /* a vignette: darker edges */
            const f32 va = 0.32f * (1.0f - A.q) * clamp01(t / 0.3f);
            const s32 vw = c->w / 6, vh = c->h / 6;

            hudGradientH(c, 0, 0, vw, c->h, hudAlpha(0x000000FFu, va), 0x00000000u, 32);
            hudGradientH(c, c->w - vw, 0, c->w, c->h, 0x00000000u, hudAlpha(0x000000FFu, va), 32);
            hudGradientV(c, 0, 0, c->w, vh, hudAlpha(0x000000FFu, va), 0x00000000u, 32);
            hudGradientV(c, 0, c->h - vh, c->w, c->h, 0x00000000u, hudAlpha(0x000000FFu, va), 32);
        }
    }
    if (lv >= 6)
    {   /* the gunbarrel: a dark disc with rings, opening with the entrance and fading out */
        const f32 open = hudEaseOutBack(clamp01(t / (enterSecs * 1.2f)), 0.12f);
        const f32 ga = (1.0f - A.q) * (A.hold > 1.0f ? clamp01(1.6f - A.hold * 0.6f) : 1.0f);
        const s32 R = (s32) ((f32) tw * 0.42f * open);
        const s32 gy = cy + (s32) (th * 0.5f);
        s32 k;

        if (R > 4 && ga > 0.02f)
        {
            hudDisc(c, cx, gy, R, hudAlpha(0x060606E0u, ga));
            for (k = 1; k <= 5; k++)
            {
                const s32 r = R * k / 6;

                hudRing(c, cx, gy, r - 2, r, hudAlpha(k & 1 ? 0xD8D8D8FFu : 0x9A9A9AFFu, ga * 0.75f));
            }
            hudRing(c, cx, gy, R - 3, R, hudAlpha(0xF0C040FFu, ga));
        }
    }
    if (lv >= 4 && A.q < 1.0f)
    {
        flare(c, cx, cy + (s32) (th * 0.5f), t, (f32) tw * 0.8f, lv >= 5 ? hudHue(t * 0.5f, 0xFF) : kCallCol[lv],
              (1.0f - A.q) * clamp01(t / enterSecs));
    }

    /* ---- the backing bar ---- */
    {
        const f32 ba = clamp01(t / (enterSecs * 0.6f)) * (1.0f - A.q);
        const u32 rule = lv >= 5 ? hudHue(t * 0.7f, 0xFF) : A.col;
        const s32 ty = cy, by = cy + (s32) th;

        hudGradientH(c, cx - tw / 2 - pad * 3, ty - 6, cx, by + 6, 0x00000000u, hudAlpha(0x000000B0u, ba), 10);
        hudGradientH(c, cx, ty - 6, cx + tw / 2 + pad * 3, by + 6, hudAlpha(0x000000B0u, ba), 0x00000000u, 10);
        hudGradientH(c, cx - tw / 2 - pad, ty - 7, cx, ty - 5, 0x00000000u, hudAlpha(rule, ba), 8);
        hudGradientH(c, cx, ty - 7, cx + tw / 2 + pad, ty - 5, hudAlpha(rule, ba), 0x00000000u, 8);
        hudGradientH(c, cx - tw / 2 - pad, by + 5, cx, by + 7, 0x00000000u, hudAlpha(rule, ba), 8);
        hudGradientH(c, cx, by + 5, cx + tw / 2 + pad, by + 7, hudAlpha(rule, ba), 0x00000000u, 8);
    }

    /* ---- an echo trail (level 4+): the same show a little earlier, faint ---- */
    if (lv >= 4)
    {
        s32 k;

        for (k = 2; k >= 1; k--)
        {
            CallAnim B = A;

            B.t = t - 0.05f * (f32) k;
            B.te = clamp01(B.t / enterSecs);
            B.alpha = A.alpha * (k == 1 ? 0.3f : 0.15f);
            B.shakeX = A.shakeX * 2.0f;
            if (B.t > 0.0f)
            {   /* (earlier in the entrance, so larger: its own fit) */
                f32 need;

                B.fit = 1.0f;
                B.fit = calloutFit(c, cx, cy, base, &B, &need);
                if (sFitLog == 2)
                {
                    B.fit = 1.0f;
                }
                if (sFitLog > 0)
                {
                    calloutFitLog(c, cx, cy, base, &B, need, "echo");
                }
                hudTextLetters(c, cx, cy, sCall.text, A.col, base, HUD_FONT_ZURICH, HUD_ALIGN_CENTRE, 0, SHADE, letterFx, &B);
            }
        }
    }
    hudTextLetters(c, cx, cy, sCall.text, A.col, base, HUD_FONT_ZURICH, HUD_ALIGN_CENTRE, HUD_FX_OUTLINE, SHADE, letterFx, &A);
    if (sCall.sub[0])
    {
        const f32 sa = clamp01((t - enterSecs * 0.8f) / 0.2f) * (1.0f - A.q);

        hudText(c, cx, cy + (s32) th + 11, sCall.sub, hudAlpha(INK, sa), 0.95f, HUD_FONT_BANK, HUD_ALIGN_CENTRE, HUD_FX_SHADOW, SHADE);
    }
}

/* port_console.c portConRender, in place of the plain mode HUD rows while the arena HUD is on */
Gfx *portArenaHudRender(Gfx *gdl)
{
    HudCanvas c;

    if (!portArenaHudActive())
    {
        return gdl;
    }
    gdl = hudBegin(gdl, &c);
    if (c.ok)
    {
        drawPanel(&c);
        drawCallout(&c);
    }
    return hudEnd(gdl, &c);
}

#endif /* !TARGET_N64 */
