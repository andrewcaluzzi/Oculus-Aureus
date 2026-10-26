/**
 * port_debrief.c - the heist DEBRIEF (designs/bank-heist.md section 26): the end-of-heist report, drawn full screen in the INTEL
 * board's style (port_intel.c: the cork frame, the blueprint paper, the stock Bank Gothic / Zurich faces). No new art.
 *
 * PRESENTATION ONLY. The content (a rating stamp, the payout, a stats table, the commendations) is set by the mode script
 * (debrief.set{...}, port/luavm/port_luavm_debrief.c; the heist's tools/heist_debrief.lua) once, when the heist ends. The panel is this
 * machine's: it opens on debrief.show() and stays up over whatever comes next (the stage's last frames, the death cam, the stock
 * mission screen) until this machine's player dismisses it. While it is up it takes the input: joy_port.c clears the poll's actions
 * (portDebriefInput) and libultra_shim.c blanks the pads after the commit (portDebriefPadFilter), so the press that dismisses it does
 * not also skip the stock mission screen; the pads stay blank until every key is let go. Dismiss: use, INTEL (Tab / Back), A, B, Start,
 * menu accept / back (after 0.75 s, so a held key does not skip it). In an automated run (GE_FRAMES) it draws but never takes the input
 * (GE_DEBRIEF_CAPTURE=1 forces it). Nothing here is sim state (snap_embed.py: PRES).
 *
 * The records file: Data/Saves/heist_records.json next to the exe (portRootPath: GE_PROFILE moves it with the saves). The name is
 * fixed here and the file is read and written only here: a script can never write anywhere else, and the file's contents never reach
 * the Lua arena (debrief.record{...} below).
 */
#ifndef TARGET_N64

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif
#include <ultra64.h>

#include "game/textrelated.h"
#include "game/dyn.h"
#include "fr.h"

extern void portConLogf(const char *fmt, ...);
extern void *portAllocHigh(u32 size);
extern void portTextScale(f32 scale);
extern void portConsoleSpace(s32 *w, s32 *h);
extern const char *portRootPath(const char *name);
extern FILE *portRootFopen(const char *name, const char *mode);

#define DB_ROWS    22
#define DB_MEDALS  18   /* 15, and 3 more with the guard personalities (section 32): six rows, a little tighter */
#define DB_DL_GFX  (40 * 1024)
#define DB_DL_SAFE 400
#define DB_RECORDS "Data/Saves/heist_records.json"
#define DB_RECMAX  (16 * 1024)
#define BW 640
#define BH 480

typedef struct { char label[40]; char value[56]; u32 rgba; } DbRow;
typedef struct { char name[28]; char desc[72]; s32 earned; } DbMedal;

static struct {
    char kicker[48], rating[24], flavour[200], payout[40], payoutNote[120], best[32], bestNote[120], title[80];
    u32 ratingRgba;
    DbRow row[DB_ROWS];
    s32 rows;
    DbMedal medal[DB_MEDALS];
    s32 medals;
} sDb;

/* the view: 0 closed, 1 open, 2 dismissed and waiting for every key to be let go */
static s32 sState, sPolls, sPrevAny, sActDown, sCapture = -1, sFrame;

static Gfx *sDL[2];
static s32 sDLi;
static Gfx *sDLEnd;

/* ---- content and the view (the Lua binding's side) ----------------------------------------------------------------------------- */

static void cpy(char *d, size_t n, const char *s) { snprintf(d, n, "%s", s ? s : ""); }

void portDebriefClear(void) { memset(&sDb, 0, sizeof(sDb)); }

void portDebriefSetText(const char *field, const char *v)
{
    if (strcmp(field, "kicker") == 0) cpy(sDb.kicker, sizeof(sDb.kicker), v);
    else if (strcmp(field, "rating") == 0) cpy(sDb.rating, sizeof(sDb.rating), v);
    else if (strcmp(field, "flavour") == 0) cpy(sDb.flavour, sizeof(sDb.flavour), v);
    else if (strcmp(field, "payout") == 0) cpy(sDb.payout, sizeof(sDb.payout), v);
    else if (strcmp(field, "payout_note") == 0) cpy(sDb.payoutNote, sizeof(sDb.payoutNote), v);
    else if (strcmp(field, "best") == 0) cpy(sDb.best, sizeof(sDb.best), v);
    else if (strcmp(field, "best_note") == 0) cpy(sDb.bestNote, sizeof(sDb.bestNote), v);
    else if (strcmp(field, "title") == 0) cpy(sDb.title, sizeof(sDb.title), v);
}

void portDebriefSetRatingColour(unsigned int rgba) { sDb.ratingRgba = rgba; }

void portDebriefAddRow(const char *label, const char *value, unsigned int rgba)
{
    if (sDb.rows >= DB_ROWS) return;
    cpy(sDb.row[sDb.rows].label, sizeof(sDb.row[0].label), label);
    cpy(sDb.row[sDb.rows].value, sizeof(sDb.row[0].value), value);
    sDb.row[sDb.rows].rgba = rgba;
    sDb.rows++;
}

void portDebriefAddMedal(const char *name, const char *desc, int earned)
{
    if (sDb.medals >= DB_MEDALS) return;
    cpy(sDb.medal[sDb.medals].name, sizeof(sDb.medal[0].name), name);
    cpy(sDb.medal[sDb.medals].desc, sizeof(sDb.medal[0].desc), desc);
    sDb.medal[sDb.medals].earned = earned ? 1 : 0;
    sDb.medals++;
}

static int dbCapture(void)
{
    if (sCapture < 0)
    {
        const char *c = getenv("GE_DEBRIEF_CAPTURE");

        sCapture = (c != NULL && c[0] == '1') ? 1 : (getenv("GE_FRAMES") == NULL);
    }
    return sCapture;
}

void portDebriefShow(int on)
{
    sState = on ? 1 : 0;
    sPolls = 0;
    sPrevAny = 1;   /* a key already down when it opens must be let go (and pressed again) to dismiss */
    portConLogf("ge: debrief: panel %s\n", on ? "shown" : "hidden");
}

int portDebriefIsOpen(void) { return sState == 1; }

/** joy_port.c portJoyPortPollCommit, every poll: `down` = a dismiss action (use, INTEL, menu accept / back) is down on some pad.
 * Returns 1 while the panel takes the input (the caller then clears every pending action). */
int portDebriefInput(int down)
{
    sActDown = down ? 1 : 0;
    return sState != 0 && dbCapture();
}

/** libultra_shim.c osContGetReadData, after the commit: the raw pads (A, B, Start dismiss too); blanked while the panel is up. */
void portDebriefPadFilter(OSContPad *data, int n)
{
    int i, any = sActDown;

    if (sState == 0 || !dbCapture()) return;
    for (i = 0; i < n; i++)
    {
        if (data[i].button & (A_BUTTON | B_BUTTON | START_BUTTON)) any = 1;
    }
    if (sState == 1)
    {
        sPolls++;
        if (any && !sPrevAny && sPolls > 45)
        {
            sState = 2;
            portConLogf("ge: debrief: dismissed\n");
        }
    }
    else if (!any)
    {
        int held = 0;

        for (i = 0; i < n; i++) held |= data[i].button;
        if (!held) sState = 0;
    }
    sPrevAny = any;
    for (i = 0; i < n; i++)
    {
        data[i].button = 0;
        data[i].stick_x = 0;
        data[i].stick_y = 0;
    }
}

/* ---- the records file ------------------------------------------------------------------------------------------------------------
 * The best result per difficulty, kept here in C and never handed to the script: the file is this machine's, and its contents must not
 * reach the Lua arena (sim state in the rollback snapshot; co-op machines would hold different text). The script only reports the run
 * (debrief.record{...}); this fills the panel's NEW BEST stamp and its line, writes the file, and logs "HEIST RECORD {json}". */

typedef struct {
    s32 runs, wins, hasBest, hasTime;
    double bestPayout, bestTime;
    char bestRating[16];
    char lastResult[12], lastRating[16];
    double lastPayout, lastTime;
} DbRec;

static DbRec sRec[4];
static s32 sRecLoaded;
static s32 sRecNewBest, sRecBestTime, sRecSaved, sRecDone;
static char sRecBest[32], sRecNote[120];

static const char *const sDiffName[4] = { "Agent", "Secret Agent", "00 Agent", "007" };

static int rank(const char *r)
{
    static const char *const order[] = { "BUSTED", "CHAOS", "COWBOY", "PROFESSIONAL", "GHOST" };
    int i;

    for (i = 0; i < 5; i++) if (strcmp(r, order[i]) == 0) return i;
    return 0;
}

static void money(char *out, size_t n, double v)
{
    char d[32], o[48];
    int len, i, j = 0;

    snprintf(d, sizeof(d), "%.0f", v < 0 ? 0.0 : v);
    len = (int) strlen(d);
    o[j++] = '$';
    for (i = 0; i < len; i++)
    {
        if (i > 0 && (len - i) % 3 == 0) o[j++] = ',';
        o[j++] = d[i];
    }
    o[j] = '\0';
    snprintf(out, n, "%s", o);
}

static void clockStr(char *out, size_t n, double t) { const int s = (int) t; snprintf(out, n, "%d:%02d", s / 60, s % 60); }

/* a number or a string after "key": within [p, end) */
static int recNum(const char *p, const char *end, const char *key, double *v)
{
    char k[40];
    const char *q;

    snprintf(k, sizeof(k), "\"%s\":", key);
    q = strstr(p, k);
    if (q == NULL || q >= end) return 0;
    *v = atof(q + strlen(k));
    return 1;
}

static void recStr(const char *p, const char *end, const char *key, char *out, size_t n)
{
    char k[40];
    const char *q;
    size_t i = 0;

    snprintf(k, sizeof(k), "\"%s\":\"", key);
    q = strstr(p, k);
    if (q == NULL || q >= end) return;
    q += strlen(k);
    while (*q && *q != '"' && i + 1 < n) out[i++] = *q++;
    out[i] = '\0';
}

static void recLoad(void)
{
    FILE *f;
    char *buf;
    size_t n;
    int d;

    if (sRecLoaded) return;
    sRecLoaded = 1;
    memset(sRec, 0, sizeof(sRec));
    f = portRootFopen(DB_RECORDS, "rb");
    if (f == NULL) return;
    buf = (char *) malloc(DB_RECMAX + 1);
    if (buf == NULL) { fclose(f); return; }
    n = fread(buf, 1, DB_RECMAX, f);
    fclose(f);
    buf[n] = '\0';
    for (d = 0; d < 4; d++)
    {
        char k[8];
        const char *p, *end, *last;
        double v;
        DbRec *r = &sRec[d];

        snprintf(k, sizeof(k), "\"%d\":{", d);
        p = strstr(buf, k);
        if (p == NULL) continue;
        end = p + 1;
        {   /* this difficulty's object runs to the next difficulty's key, or the end */
            int e;

            end = buf + n;
            for (e = 0; e < 4; e++)
            {
                char k2[8];
                const char *q;

                snprintf(k2, sizeof(k2), "\"%d\":{", e);
                q = strstr(p + 1, k2);
                if (q != NULL && q < end) end = q;
            }
        }
        last = strstr(p, "\"last\":");
        if (last == NULL || last > end) last = end;
        if (recNum(p, last, "runs", &v)) r->runs = (s32) v;
        if (recNum(p, last, "wins", &v)) r->wins = (s32) v;
        if (recNum(p, last, "best_payout", &v)) { r->bestPayout = v; r->hasBest = 1; }
        if (recNum(p, last, "best_time", &v)) { r->bestTime = v; r->hasTime = 1; }
        recStr(p, last, "best_rating", r->bestRating, sizeof(r->bestRating));
        recStr(last, end, "result", r->lastResult, sizeof(r->lastResult));
        recStr(last, end, "rating", r->lastRating, sizeof(r->lastRating));
        if (recNum(last, end, "payout", &v)) r->lastPayout = v;
        if (recNum(last, end, "time", &v)) r->lastTime = v;
    }
    free(buf);
}

static int recSave(void)
{
    static const char *const dirs[] = { "Data", "Data/Saves" };
    char text[4096];
    size_t len = 0;
    struct stat st;
    FILE *f;
    int i, d, first = 1, ok;

    len += (size_t) snprintf(text + len, sizeof(text) - len, "{\"version\":1,\"difficulty\":{");
    for (d = 0; d < 4; d++)
    {
        const DbRec *r = &sRec[d];

        if (r->runs <= 0) continue;
        len += (size_t) snprintf(text + len, sizeof(text) - len, "%s\n \"%d\":{\"runs\":%d,\"wins\":%d", first ? "" : ",", d, (int) r->runs,
                                 (int) r->wins);
        if (r->hasBest) len += (size_t) snprintf(text + len, sizeof(text) - len, ",\"best_payout\":%.0f", r->bestPayout);
        if (r->hasTime) len += (size_t) snprintf(text + len, sizeof(text) - len, ",\"best_time\":%.2f", r->bestTime);
        if (r->bestRating[0]) len += (size_t) snprintf(text + len, sizeof(text) - len, ",\"best_rating\":\"%s\"", r->bestRating);
        len += (size_t) snprintf(text + len, sizeof(text) - len, ",\"last\":{\"result\":\"%s\",\"rating\":\"%s\",\"payout\":%.0f,\"time\":%.2f}}",
                                 r->lastResult, r->lastRating, r->lastPayout, r->lastTime);
        first = 0;
    }
    len += (size_t) snprintf(text + len, sizeof(text) - len, "\n}}\n");
    if (len >= sizeof(text)) return 0;
    for (i = 0; i < 2; i++)
    {
        char path[1200];

        snprintf(path, sizeof(path), "%s", portRootPath(dirs[i]));
        if (stat(path, &st) != 0)
        {
#ifdef _WIN32
            _mkdir(path);
#else
            mkdir(path, 0755);
#endif
        }
    }
    f = portRootFopen(DB_RECORDS, "wb");
    if (f == NULL) { portConLogf("ge: debrief: could not write %s\n", portRootPath(DB_RECORDS)); return 0; }
    ok = fwrite(text, 1, len, f) == len;
    fclose(f);
    portConLogf("ge: debrief: records saved to %s (%u bytes)\n", portRootPath(DB_RECORDS), (unsigned) len);
    return ok;
}

/** debrief.record{...}: the run's result at the end of a heist. Counts it in the records, saves the file, fills the panel's stamp. */
void portDebriefRecord(int diff, int win, double payout, double time, const char *rating, const char *result)
{
    extern int g_PortResim;
    DbRec *r, prev;
    const char *dn;
    char a[48], b[48], c[16], e[16];

    if (diff < 0 || diff > 3) diff = 1;
    dn = sDiffName[diff];
    if (g_PortResim && sRecDone)
    {   /* a rollback re-simulating the last tick runs the script's end again: counted once, the stamp put back */
        cpy(sDb.best, sizeof(sDb.best), sRecBest);
        cpy(sDb.bestNote, sizeof(sDb.bestNote), sRecNote);
        return;
    }
    recLoad();
    r = &sRec[diff];
    prev = *r;
    sRecNewBest = sRecBestTime = 0;
    r->runs++;
    if (win)
    {
        r->wins++;
        if (!r->hasBest || payout > r->bestPayout) { r->bestPayout = payout; r->hasBest = 1; sRecNewBest = 1; }
        if (!r->hasTime || time < r->bestTime) { r->bestTime = time; r->hasTime = 1; sRecBestTime = 1; }
        if (!r->bestRating[0] || rank(rating) > rank(r->bestRating)) cpy(r->bestRating, sizeof(r->bestRating), rating);
    }
    cpy(r->lastResult, sizeof(r->lastResult), result);
    cpy(r->lastRating, sizeof(r->lastRating), rating);
    r->lastPayout = payout;
    r->lastTime = time;
    sRecSaved = recSave();
    sRecDone = 1;

    /* the stamp and its line */
    cpy(sRecBest, sizeof(sRecBest), sRecNewBest ? "NEW BEST" : (sRecBestTime ? "BEST TIME" : ""));
    money(a, sizeof(a), r->bestPayout);
    money(b, sizeof(b), prev.bestPayout);
    if (!win) snprintf(sRecNote, sizeof(sRecNote), "Best on %s: %s", dn, r->hasBest ? a : "none yet");
    else if (sRecNewBest && !prev.hasBest) snprintf(sRecNote, sizeof(sRecNote), "Your first clean getaway on %s. The record book is open.", dn);
    else if (sRecNewBest) snprintf(sRecNote, sizeof(sRecNote), "Beats the old best on %s: %s", dn, b);
    else
    {
        clockStr(c, sizeof(c), r->bestTime);
        snprintf(sRecNote, sizeof(sRecNote), "Best on %s: %s (%s, %s)", dn, a, r->bestRating, c);
    }
    if (sRecBestTime && prev.hasTime)
    {
        const size_t l = strlen(sRecNote);

        clockStr(c, sizeof(c), time);
        clockStr(e, sizeof(e), prev.bestTime);
        snprintf(sRecNote + l, sizeof(sRecNote) - l, "  Best time %s (was %s)", c, e);
    }
    cpy(sDb.best, sizeof(sDb.best), sRecBest);
    cpy(sDb.bestNote, sizeof(sDb.bestNote), sRecNote);
    portConLogf("HEIST RECORD {\"difficulty\":%d,\"new_best\":%s,\"best_time\":%s,\"runs\":%d,\"wins\":%d,\"best_payout\":%.0f,\"best_rating\":\"%s\","
                "\"saved\":%s,\"path\":\"%s\"}\n", diff, sRecNewBest ? "true" : "false", sRecBestTime ? "true" : "false", (int) r->runs,
                (int) r->wins, r->hasBest ? r->bestPayout : 0.0, r->bestRating, sRecSaved ? "true" : "false", portRootPath(DB_RECORDS));
}

/** Tests only (one machine's file: game logic must never read it): the last record's outcome. */
void portDebriefRecordInfo(int diff, int *newBest, int *runs, int *wins, double *best, int *saved)
{
    recLoad();
    if (diff < 0 || diff > 3) diff = 1;
    *newBest = sRecNewBest;
    *runs = sRec[diff].runs;
    *wins = sRec[diff].wins;
    *best = sRec[diff].hasBest ? sRec[diff].bestPayout : 0.0;
    *saved = sRecSaved;
}

const char *portDebriefRecordsPath(void) { return portRootPath(DB_RECORDS); }
/* ---- drawing (the INTEL board's primitives, port_intel.c) ------------------------------------------------------------------------ */

static s32 sW = 640, sH = 480;
static f32 sSc = 1.0f;
static s32 sOx, sOy;
static u32 sPrim;
static s32 sHavePrim, sTexMode;

#define CON2D(gdl, w, h) do { (gdl)->words.w0 = (u32) 0x4c << 24 /* G_CONSOLE2D_EXT */; \
                              (gdl)->words.w1 = ((u32) (w) << 16) | (u32) (h); (gdl)++; } while (0)

static s32 X(s32 v) { return sOx + (s32) ((f32) v * sSc); }
static s32 Y(s32 v) { return sOy + (s32) ((f32) v * sSc); }
static int roomLeft(Gfx *g) { return g != NULL && (sDLEnd - g) > DB_DL_SAFE; }

static Gfx *fill(Gfx *g, s32 x0, s32 y0, s32 x1, s32 y1, u32 rgba)
{
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > sW) x1 = sW;
    if (y1 > sH) y1 = sH;
    if (x1 <= x0 || y1 <= y0 || (rgba & 0xFFu) == 0 || !roomLeft(g)) return g;
    if (sTexMode != 0)
    {
        gDPSetCombineMode(g++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
        sTexMode = 0;
        sHavePrim = 0;
    }
    if (!sHavePrim || rgba != sPrim)
    {
        gDPSetPrimColor(g++, 0, 0, (rgba >> 24) & 0xFF, (rgba >> 16) & 0xFF, (rgba >> 8) & 0xFF, rgba & 0xFF);
        sPrim = rgba;
        sHavePrim = 1;
    }
    gDPFillRectangle(g++, x0, y0, x1, y1);
    return g;
}

static Gfx *bfill(Gfx *g, s32 x0, s32 y0, s32 x1, s32 y1, u32 rgba) { return fill(g, X(x0), Y(y0), X(x1), Y(y1), rgba); }

static Gfx *bframe(Gfx *g, s32 x0, s32 y0, s32 x1, s32 y1, s32 t, u32 rgba)
{
    const s32 ax = X(x0), ay = Y(y0), bx = X(x1), by = Y(y1);

    t = (s32) ((f32) t * sSc + 0.5f);
    if (t < 1) t = 1;
    g = fill(g, ax, ay, bx, ay + t, rgba);
    g = fill(g, ax, by - t, bx, by, rgba);
    g = fill(g, ax, ay + t, ax + t, by - t, rgba);
    g = fill(g, bx - t, ay + t, bx, by - t, rgba);
    return g;
}

static Gfx *stri(Gfx *g, s32 x0, s32 y0, s32 x1, s32 y1, s32 x2, s32 y2, u32 rgba)
{
    s32 ymin = y0, ymax = y0, y;
    const s32 xs[3] = { x0, x1, x2 }, ys[3] = { y0, y1, y2 };

    if (y1 < ymin) ymin = y1;
    if (y2 < ymin) ymin = y2;
    if (y1 > ymax) ymax = y1;
    if (y2 > ymax) ymax = y2;
    for (y = ymin; y < ymax && roomLeft(g); y++)
    {
        const f32 fy = (f32) y + 0.5f;
        f32 lo = 1e9f, hi = -1e9f;
        int e;

        for (e = 0; e < 3; e++)
        {
            const s32 ax = xs[e], ay = ys[e], bx = xs[(e + 1) % 3], by = ys[(e + 1) % 3];

            if ((fy >= (f32) ay && fy < (f32) by) || (fy >= (f32) by && fy < (f32) ay))
            {
                const f32 x = (f32) ax + ((fy - (f32) ay) * (f32) (bx - ax)) / (f32) (by - ay);

                if (x < lo) lo = x;
                if (x > hi) hi = x;
            }
        }
        if (hi >= lo) g = fill(g, (s32) (lo + 0.5f), y, (s32) (hi + 0.5f) + 1, y + 1, rgba);
    }
    return g;
}

/* a five-point medal star at board (cx, cy), radius r board units: two triangles, as the board's star marker */
static Gfx *star(Gfx *g, s32 bx, s32 by, s32 br, u32 rgba)
{
    const s32 cx = X(bx), cy = Y(by), r = (s32) ((f32) br * sSc + 0.5f);

    g = stri(g, cx, cy - r, cx - r, cy + r / 2, cx + r, cy + r / 2, rgba);
    g = stri(g, cx, cy + r, cx - r, cy - r / 2, cx + r, cy - r / 2, rgba);
    return g;
}

static struct fontchar *fChars(int font) { return (font && ptrFontZurichBoldChars) ? ptrFontZurichBoldChars : ptrFontBankGothicChars; }
static struct font *fFont(int font) { return (font && ptrFontZurichBold) ? ptrFontZurichBold : ptrFontBankGothic; }

/* the width of `t` in screen units at board scale `scale` */
static s32 textW(const char *t, f32 scale, int font)
{
    s32 h = 0, w = 0;

    portTextScale(scale * sSc);
    textMeasure(&h, &w, (char *) t, fChars(font), fFont(font), 0);
    portTextScale(1.0f);
    return w;
}

static Gfx *stext(Gfx *g, s32 x, s32 y, const char *t, u32 rgba, f32 scale, int font)
{
    s32 tx = x, ty = y;

    if (t == NULL || t[0] == '\0' || (sDLEnd - g) < DB_DL_SAFE + (s32) strlen(t) * 14) return g;
    if (sTexMode != 1)
    {
        gDPSetCombineLERP(g++, 0, 0, 0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0, 0, 0, 0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0);
        sTexMode = 1;
    }
    portTextScale(scale * sSc);
    g = textRender(g, &tx, &ty, (char *) t, fChars(font), fFont(font), rgba, sW, sH, 0, 0);
    portTextScale(1.0f);
    sHavePrim = 0;
    return g;
}

/* board-unit text: left (align 0), centred on x (1) or right-aligned to x (2) */
static Gfx *btext(Gfx *g, s32 x, s32 y, const char *t, u32 rgba, f32 scale, int font, int align)
{
    s32 sx = X(x);

    if (align) sx -= align == 1 ? textW(t, scale, font) / 2 : textW(t, scale, font);
    return stext(g, sx, Y(y), t, rgba, scale, font);
}

/* wrapped board text (word by word, at `wrap` board units); returns the y under the last line */
static Gfx *wtext(Gfx *g, s32 x, s32 y, const char *t, u32 rgba, f32 scale, s32 wrap, int font, s32 *yEnd)
{
    char line[256];
    const s32 pitch = (s32) ((font ? 16.0f : 11.0f) * scale + 0.5f);

    while (t && *t && roomLeft(g))
    {
        s32 n = 0, lastFit = -1;

        for (;;)
        {
            const char c = t[n];

            if (c == ' ' || c == '\n' || c == '\0' || n >= (s32) sizeof(line) - 2)
            {
                line[n] = '\0';
                if (lastFit > 0 && (f32) textW(line, scale, font) > (f32) wrap * sSc) { n = lastFit; break; }
                if (c != ' ') break;
                lastFit = n;
            }
            line[n] = c;
            n++;
        }
        line[n] = '\0';
        g = stext(g, X(x), Y(y), line, rgba, scale, font);
        t += n;
        if (*t == ' ' || *t == '\n') t++;
        y += pitch;
    }
    if (yEnd) *yEnd = y;
    return g;
}

static Gfx *drawPanel(Gfx *g)
{
    s32 i, x, y;
    char buf[96];
    const u32 ink = 0x2A2418FFu, rc = sDb.ratingRgba ? sDb.ratingRgba : 0x9A1010FFu;

    /* the board: cork, the blueprint and its grid (port_intel.c drawChrome) */
    g = bfill(g, 6, 6, BW - 6, BH - 6, 0x6B4A2CF8u);
    for (i = 0; i < 40; i++)
    {
        const s32 fx = 8 + (s32) ((i * 7919u) % (BW - 16)), fy = 8 + (s32) ((i * 104729u) % (BH - 16));

        g = bfill(g, fx, fy, fx + 2, fy + 2, 0x4A3018FFu);
    }
    g = bfill(g, 14, 30, BW - 14, BH - 22, 0x102C58FFu);
    for (x = 14; x < BW - 14; x += 20) g = bfill(g, x, 30, x + 1, BH - 22, 0x2A4C80A0u);
    for (x = 30; x < BH - 22; x += 20) g = bfill(g, 14, x, BW - 14, x + 1, 0x2A4C80A0u);
    g = bframe(g, 18, 34, BW - 18, BH - 26, 1, 0xD8E4F4C0u);
    /* the index-card tab, the title */
    g = bfill(g, 21, 8, 150, 30, 0xF2EBD0FFu);
    g = btext(g, 28, 13, "DEBRIEF", 0x9A1010FFu, 1.0f, 0, 0);
    g = btext(g, BW - 20, 14, sDb.title, 0xF2EBD0FFu, 1.0f, 0, 2);

    /* the case file: the verdict */
    g = bfill(g, 27, 43, 321, 303, 0x00000060u);   /* (its shadow) */
    g = bfill(g, 24, 40, 318, 300, 0xE8DDB8FFu);
    g = bframe(g, 24, 40, 318, 300, 1, 0x8A7A58FFu);
    g = bfill(g, 286, 40, 298, 300, 0xD8CCA4FFu);   /* a paper clip's shadow strip */
    g = btext(g, 34, 48, sDb.kicker, ink, 1.0f, 0, 0);
    g = bframe(g, 36, 64, 306, 116, 3, rc);
    g = bframe(g, 41, 69, 301, 111, 1, rc);
    g = btext(g, 171, 74, sDb.rating, rc, 2.1f, 1, 1);
    g = wtext(g, 36, 124, sDb.flavour, ink, 0.9f, 268, 0, &y);
    y = y < 176 ? 176 : y + 4;
    g = btext(g, 36, y, "CREW PAYOUT", 0x5A4A30FFu, 0.9f, 0, 0);
    g = btext(g, 36, y + 12, sDb.payout, 0x1E5A1EFFu, 1.6f, 1, 0);
    if (sDb.best[0])
    {   /* the NEW BEST stamp */
        const s32 w = textW(sDb.best, 1.0f, 1) / (sSc > 0.0f ? sSc : 1.0f) + 14;

        g = bframe(g, 304 - w, y + 8, 304, y + 34, 2, 0xC01818FFu);
        g = btext(g, 304 - w / 2, y + 13, sDb.best, 0xC01818FFu, 1.0f, 1, 1);
    }
    g = wtext(g, 36, y + 42, sDb.payoutNote, 0x5A4A30FFu, 0.8f, 268, 0, &y);
    g = wtext(g, 36, y + 2, sDb.bestNote, 0x7A2A10FFu, 0.8f, 268, 0, &y);

    /* the job in numbers */
    g = btext(g, 336, 44, "THE JOB IN NUMBERS", 0xF2EBD0FFu, 1.0f, 0, 0);
    g = bfill(g, 336, 57, 614, 58, 0xD8E4F4A0u);
    for (i = 0; i < sDb.rows; i++)
    {
        const s32 ry = 62 + i * 11;

        if (i & 1) g = bfill(g, 334, ry - 1, 616, ry + 10, 0xFFFFFF10u);
        g = btext(g, 338, ry, sDb.row[i].label, 0xA8B8D4FFu, 0.85f, 0, 0);
        g = btext(g, 612, ry, sDb.row[i].value, sDb.row[i].rgba ? sDb.row[i].rgba : 0xFFFFFFFFu, 0.85f, 0, 2);
    }

    /* the commendations: three columns of medals, earned ones lit */
    {
        s32 earned = 0;

        for (i = 0; i < sDb.medals; i++) earned += sDb.medal[i].earned;
        snprintf(buf, sizeof(buf), "COMMENDATIONS  %d OF %d", (int) earned, (int) sDb.medals);
        g = btext(g, 30, 310, buf, 0xF2EBD0FFu, 1.0f, 0, 0);
        g = bfill(g, 28, 323, 612, 324, 0xD8E4F4A0u);
    }
    for (i = 0; i < sDb.medals; i++)
    {
        const s32 cx = 30 + (i % 3) * 196, cy = 328 + (i / 3) * (sDb.medals > 15 ? 21 : 26);
        const DbMedal *m = &sDb.medal[i];

        if (cy > 438) break;
        if (m->earned)
        {
            g = bfill(g, cx - 2, cy - 2, cx + 190, cy + 23, 0xF2C84028u);
            g = star(g, cx + 9, cy + 9, 8, 0x5A3A00FFu);
            g = star(g, cx + 9, cy + 9, 7, 0xF2C840FFu);
        }
        else
        {
            g = star(g, cx + 9, cy + 9, 7, 0x4A5A78FFu);
        }
        g = btext(g, cx + 22, cy, m->name, m->earned ? 0xFFF0C0FFu : 0x7088A8FFu, 0.9f, 0, 0);
        g = btext(g, cx + 22, cy + 11, m->desc, m->earned ? 0xC8D4E8FFu : 0x5A6E8CFFu, 0.7f, 0, 0);
    }

    /* footer */
    g = bfill(g, 14, BH - 21, BW - 14, BH - 8, 0x000000A0u);
    g = btext(g, 20, BH - 19, sState == 1 ? "USE  or  INTEL (Tab / Back)  or  A / Start: continue" : "", 0xFFE060FFu, 1.0f, 0, 0);
    return g;
}

/** port_console.c portConRender, every frame on every screen: the panel while it is up on this machine. */
Gfx *portDebriefRender(Gfx *gdl)
{
    Gfx *g;
    s16 vx, vy;

    sFrame++;
    if (sState == 0 || ptrFontBankGothic == NULL || ptrFontBankGothicChars == NULL) return gdl;
    if (sDL[0] == NULL)
    {
        sDL[0] = (Gfx *) portAllocHigh(DB_DL_GFX * sizeof(Gfx));
        sDL[1] = (Gfx *) portAllocHigh(DB_DL_GFX * sizeof(Gfx));
        if (sDL[0] == NULL || sDL[1] == NULL)
        {
            portConLogf("ge: debrief: no memory for the panel's display lists\n");
            sState = 0;
            return gdl;
        }
    }
    if (dynGetFreeGfx2(gdl) < 64) return gdl;
    portConsoleSpace(&sW, &sH);
    sSc = (f32) sW / (f32) BW;
    if ((f32) sH / (f32) BH < sSc) sSc = (f32) sH / (f32) BH;
    sOx = (sW - (s32) ((f32) BW * sSc)) / 2;
    sOy = (sH - (s32) ((f32) BH * sSc)) / 2;
    sDLi ^= 1;
    g = sDL[sDLi];
    sDLEnd = g + DB_DL_GFX;
    sHavePrim = 0;
    sTexMode = -1;

    vx = viGetX();
    vy = viGetY();
    viSetXY((s16) sW, (s16) sH);
    CON2D(gdl, sW, sH);
    gdl = microcode_constructor(gdl);
    gDPSetScissor(gdl++, G_SC_NON_INTERLACE, 0, 0, sW, sH);
    gSPDisplayList(gdl++, g);

    gDPSetRenderMode(g++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    g = bfill(g, -40, -40, BW + 40, BH + 40, 0x000000C0u);   /* dim whatever is under the board (the pillarbox too) */
    g = fill(g, 0, 0, sW, sH, 0x00000060u);
    g = drawPanel(g);
    portTextScale(1.0f);
    gSPEndDisplayList(g++);

    gdl = combiner_bayer_lod_perspective(gdl);
    CON2D(gdl, 0, 0);
    viSetXY(vx, vy);
    return gdl;
}

#endif /* !TARGET_N64 */
