/* port_arhud.c - the augmented interface ("Q-Branch specs") and the diagnostics layer (designs/bank-heist.md section 20).
 *
 * World points are projected through the local Bond's camera (the same world-to-screen x projection product port_crowd.c builds) into
 * the console's 2D space (480 lines by the window's width, port_console.c), and drawn there as rects and text after the frame:
 *   AR (on by default in the heist, `ar` console command / Shift+F3 / ar.on):
 *     - above each cover guard: a "?" that fills with his suspicion meter (cover.state; ai.awareness when that exists), a red "!" once
 *       he has seen through you or everyone is on alert; faded with distance, hidden when the chr was not drawn this frame
 *     - speech bubbles near the face (ar.say, the guard_bark event, the mode's own barks), with a short ROM sound
 *     - key holders: a coloured key tag and spy-style bracket corners around the chr's projected bounds (and any ar.outline colour)
 *     - waypoint diamonds with distances (ar.point), doors your keys open, the cameras (red while timing you, a faint floor cone)
 *     - off-screen points: an arrow on the screen edge
 *   Diagnostics (F3, console `diag`, diag.on): per chr number, action, AI list, awareness, suspicion, the go-to target, the sight cone and
 *     the hearing ring; noise rings; door key and rule colours; the room and zone you stand in; tick, sim hash, FPS and frame ms, chr
 *     count, alarm / power / cover; a radar. `diag freeze` stops every AI list (solo only, sim-affecting, off by default).
 *
 * PRESENTATION ONLY. Everything here reads sim state and writes only its own statics (marks, bubbles, rings, toggles), which nothing in
 * the sim reads - except the one test switch `diag freeze` (portArFreezeAi, read by port_heist.c portHeistChrNoAi), refused in co-op
 * and netplay. Lua calls made while a rollback re-simulates a tick are ignored (no double bubbles or sounds). */
#ifndef TARGET_N64

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ultra64.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include "game/chr.h"
#include "game/chraction.h"
#include "game/chrai.h"
#include "game/model.h"
#include "game/player.h"
#include "game/bondview.h"
#include "game/lv.h"
#include "game/textrelated.h"
#include "game/dyn.h"
#include "game/math_atan2f.h"
#include "fr.h"
#include "port_mode.h"
#include "port_lua.h"
#include "port_netmsg.h"

extern void portConPrintf(s32 showmsg, const char *fmt, ...);
extern int portLocalPlayer(void);
extern int portCoopActive(void);
extern int portNetActive(void);
extern int g_PortResim;
extern s32 portPillarboxFrame(void);
extern void portConsoleSpace(s32 *w, s32 *h);
extern int portCctvOverlayGet(int *cam, char *name, int namelen, int *index, int *count, int *offline, int *seconds, int *ticks);
extern int portHeistCoverState(int chrnum, float *susp, int *witness, int *reported, int *sees, int *role, int *why);
extern int portHeistCoverMeter(int p, float *level, int *chr);
extern int portHeistCoverAlert(void);
extern int portHeistCoverIsOn(void);
extern int portHeistCctvBondTimer(int idx, int p);
extern int portHeistPowerOn(void);
extern int portHeistChrDown(int chrnum);
extern int portHeistKoIs(int chrnum);
extern int portHeistCivilianState(int chrnum);
extern int portHeistPadRaw(int pad, coord3d *pos, StandTile **stan, float *angle);
extern int portGameSfxPlay(int sfx, float vol, float pitch, float delay, float pan);
extern int portCoopStateHash(PortNetHashMsg *m, char *dump, int cap);
extern s32 chraiGetAIListID(AIRecord *AIList, bool *isGlobalAIList);
extern s32 portStageRunning(void);
/* the ai stream's guard awareness (OVERNIGHT interfaces): used when it is linked in, else the cover meter alone */
extern int portAiAwareness(int chrnum, int *state, float *level, float *lastx, float *lastz) __attribute__((weak));

#define AR_DEG2RAD 0.017453292f

/* ---- toggles ---------------------------------------------------------------------------------------------------------------------- */

enum { ARL_SUSPECT = 0, ARL_SPEECH, ARL_KEYS, ARL_POINTS, ARL_DOORS, ARL_CAMERAS, ARL_RADAR, ARL_N };
static const char *const sLayerName[ARL_N] = { "suspicion", "speech", "keys", "points", "doors", "cameras", "radar" };
static u8 sLayer[ARL_N] = { 1, 1, 1, 1, 1, 1, 0 };
static int sArOn;            /* the specs (a mode turns them on: ar.on) */
static int sDiagOn;          /* the diagnostics layer */
static int sFreezeAi;        /* diag freeze: sim-affecting, solo only */
static int sSilent;          /* ar.sound(false): bubbles make no sound */

/* the sight cone the diagnostics draw (cover.tune's range and cone: the heist passes them, ar.config) */
static f32 sConeRange = 1800.0f, sConeDeg = 75.0f;

/* ---- marks, bubbles, points, rings, labels (written from Lua, read by the renderer) ------------------------------------------------ */

#define AR_MARKS   96
#define AR_SAYS    24
#define AR_POINTS  32
#define AR_RINGS   24
#define AR_DOORS   96
#define AR_ROOMS   256
#define AR_TEXTLEN 48

typedef struct { s32 chrnum; u32 outline; u32 rgba; char label[24]; u8 used, bracket; } ArMark;
typedef struct { s32 chrnum; s32 until60, start60; u32 rgba; char text[AR_TEXTLEN]; u8 used; } ArSay;
typedef struct { f32 x, y, z; u32 rgba; char label[24]; u8 used; } ArPoint;
typedef struct { f32 x, y, z, r; s32 start60; u32 rgba; u8 used; } ArRing;
typedef struct { s32 idx; u32 rgba; char label[16]; } ArDoorRule;

static ArMark sMark[AR_MARKS];
static ArSay sSay[AR_SAYS];
static ArPoint sPoint[AR_POINTS];
static ArRing sRing[AR_RINGS];
static ArDoorRule sDoorRule[AR_DOORS];
static int sDoorRuleN;
static char sRoomLabel[AR_ROOMS][28];
static char sDiagText[4][64];

static s32 arNow60(void) { return g_GlobalTimer; }

static ArMark *markFind(s32 chrnum, int make)
{
    int i, freei = -1;

    for (i = 0; i < AR_MARKS; i++)
    {
        if (sMark[i].used && sMark[i].chrnum == chrnum) return &sMark[i];
        if (!sMark[i].used && freei < 0) freei = i;
    }
    if (!make || freei < 0) return NULL;
    memset(&sMark[freei], 0, sizeof(ArMark));
    sMark[freei].used = 1;
    sMark[freei].chrnum = chrnum;
    return &sMark[freei];
}

static int arLuaOk(void) { return !g_PortResim; }

void portArMark(int chrnum, const char *label, unsigned rgba, int bracket)
{
    ArMark *m;

    if (!arLuaOk() || (m = markFind(chrnum, 1)) == NULL) return;
    snprintf(m->label, sizeof(m->label), "%s", label != NULL ? label : "");
    m->rgba = rgba != 0 ? rgba : 0xFFFFFFFFu;
    m->bracket = (u8) (bracket != 0);
}

void portArOutline(int chrnum, unsigned rgba)
{
    ArMark *m;

    if (!arLuaOk() || (m = markFind(chrnum, rgba != 0)) == NULL) return;
    m->outline = rgba;
    if (rgba == 0 && m->label[0] == '\0' && !m->bracket) m->used = 0;
}

/* a speech bubble near chrnum's face for secs; kind: 0 talk, 1 hey (red), 2 radio (cyan), 3 huh (yellow). The sound is a stock ROM SFX
 * by id, panned from the chr's position relative to this machine's Bond, through port_gamesfx.c (presentation slots). */
void portArSay(int chrnum, const char *text, float secs, int kind)
{
    static const u32 col[4] = { 0xFFFFFFFFu, 0xFF5040FFu, 0x60E8FFFFu, 0xFFE060FFu };
    ArSay *s = NULL;
    int i, oldest = 0;
    ChrRecord *c;

    if (!arLuaOk() || text == NULL || text[0] == '\0') return;
    if (kind < 0 || kind > 3) kind = 0;
    for (i = 0; i < AR_SAYS; i++)
    {
        if (sSay[i].used && sSay[i].chrnum == chrnum) { s = &sSay[i]; break; }   /* one bubble per chr: the newest line */
    }
    for (i = 0; s == NULL && i < AR_SAYS; i++)
    {
        if (!sSay[i].used) s = &sSay[i];
        else if (sSay[i].until60 < sSay[oldest].until60) oldest = i;
    }
    if (s == NULL) s = &sSay[oldest];
    s->used = 1;
    s->chrnum = chrnum;
    s->start60 = arNow60();
    s->until60 = arNow60() + (s32) ((secs > 0.0f ? secs : 3.0f) * 60.0f);
    s->rgba = col[kind];
    snprintf(s->text, sizeof(s->text), "%s", text);

    c = chrFindByLiteralId(chrnum);
    if (sArOn && sLayer[ARL_SPEECH] && !sSilent && c != NULL && c->prop != NULL)
    {
        int lp = portLocalPlayer();
        struct player *pl = (lp >= 0 && lp < 4) ? g_playerPointers[lp] : NULL;

        if (pl != NULL && pl->prop != NULL)
        {
            f32 dx = c->prop->pos.x - pl->prop->pos.x, dz = c->prop->pos.z - pl->prop->pos.z, d = sqrtf(dx * dx + dz * dz), th, rx, vol, pan;

            if (d < 2500.0f)
            {
                vol = d < 300.0f ? 0.7f : 0.7f - 0.5f * (d - 300.0f) / 2200.0f;
                th = pl->vv_theta * AR_DEG2RAD;
                rx = d > 1.0f ? (dx * cosf(th) + dz * sinf(th)) / d : 0.0f;
                pan = rx < -1.0f ? -1.0f : (rx > 1.0f ? 1.0f : rx);
                /* radio: the squelch; hey: the camera's alert beep; talk / huh: a quiet beep */
                portGameSfxPlay(kind == 2 ? RADIO_SFX : (kind == 1 ? CAMERA_BEEP1_SFX : BEEP_QUIET_SFX), vol, kind == 3 ? 1.25f : 1.0f, 0.0f, pan);
            }
        }
    }
}

int portArPoint(float x, float y, float z, const char *label, unsigned rgba)
{
    int i, freei = -1;

    if (!arLuaOk()) return -1;
    for (i = 0; i < AR_POINTS; i++)
    {
        if (sPoint[i].used && label != NULL && label[0] != '\0' && strcmp(sPoint[i].label, label) == 0) { freei = i; break; }
        if (!sPoint[i].used && freei < 0) freei = i;
    }
    if (freei < 0) return -1;
    sPoint[freei].used = 1;
    sPoint[freei].x = x; sPoint[freei].y = y; sPoint[freei].z = z;
    sPoint[freei].rgba = rgba != 0 ? rgba : 0x50FFA0FFu;
    snprintf(sPoint[freei].label, sizeof(sPoint[freei].label), "%s", label != NULL ? label : "");
    return freei + 1;
}

void portArPointClear(const char *label)
{
    int i;

    for (i = 0; i < AR_POINTS; i++)
    {
        if (sPoint[i].used && (label == NULL || strcmp(sPoint[i].label, label) == 0)) sPoint[i].used = 0;
    }
}

void portArClear(int chrnum)   /* chrnum < 0: every mark and bubble */
{
    int i;

    for (i = 0; i < AR_MARKS; i++) if (chrnum < 0 || sMark[i].chrnum == chrnum) sMark[i].used = 0;
    for (i = 0; i < AR_SAYS; i++) if (chrnum < 0 || sSay[i].chrnum == chrnum) sSay[i].used = 0;
}

void portArNoise(float x, float y, float z, float r, int kind)
{
    int i, best = 0;

    if (!arLuaOk()) return;
    /* a Bond's footsteps (and anything else made where he stands) come at his prop, which is at eye height: put the ring at his feet,
     * or it is a circle round the camera itself and jumps about as he moves */
    for (i = 0; i < 4; i++)
    {
        struct player *pl = g_playerPointers[i];

        if (pl == NULL || pl->prop == NULL) continue;
        if (fabsf(x - pl->prop->pos.x) < 80.0f && fabsf(z - pl->prop->pos.z) < 80.0f && fabsf(y - pl->prop->pos.y) < 60.0f)
        {
            y = pl->prop->pos.y - pl->eyeheight;
            break;
        }
    }
    for (i = 0; i < AR_RINGS; i++)
    {
        if (!sRing[i].used) { best = i; break; }
        if (sRing[i].start60 < sRing[best].start60) best = i;
    }
    sRing[best].used = 1;
    sRing[best].x = x; sRing[best].y = y; sRing[best].z = z;
    sRing[best].r = r > 20.0f ? r : 20.0f;
    sRing[best].start60 = arNow60();
    sRing[best].rgba = kind == 6 /* shot */ ? 0xFF6040FFu : (kind == 0 /* slap */ || kind == 5 /* thud */ ? 0xFFC040FFu : 0x80C0FFFFu);
}

void portArDoorRule(int idx, const char *label, unsigned rgba)
{
    int i;

    for (i = 0; i < sDoorRuleN; i++) if (sDoorRule[i].idx == idx) break;
    if (i == sDoorRuleN)
    {
        if (sDoorRuleN >= AR_DOORS) return;
        sDoorRuleN++;
    }
    sDoorRule[i].idx = idx;
    sDoorRule[i].rgba = rgba;
    snprintf(sDoorRule[i].label, sizeof(sDoorRule[i].label), "%s", label != NULL ? label : "");
}

void portArRoomLabel(int room, const char *label)
{
    if (room >= 0 && room < AR_ROOMS) snprintf(sRoomLabel[room], sizeof(sRoomLabel[room]), "%s", label != NULL ? label : "");
}

void portArDiagText(int line, const char *text)
{
    if (line >= 1 && line <= 4) snprintf(sDiagText[line - 1], sizeof(sDiagText[0]), "%s", text != NULL ? text : "");
}

void portArConfig(float coneRange, float coneDeg)
{
    if (coneRange > 0.0f) sConeRange = coneRange;
    if (coneDeg > 0.0f) sConeDeg = coneDeg;
}

void portArSetOn(int on) { sArOn = on ? 1 : 0; }
int portArIsOn(void) { return sArOn; }
void portArSetSound(int on) { sSilent = on ? 0 : 1; }
int portArLayer(const char *name, int on)   /* on < 0: query. -1 = no such layer */
{
    int i;

    for (i = 0; i < ARL_N; i++)
    {
        if (name != NULL && strcmp(name, sLayerName[i]) == 0)
        {
            if (on >= 0) sLayer[i] = (u8) (on != 0);
            return sLayer[i];
        }
    }
    return -1;
}

void portArDiagSet(int on) { sDiagOn = on ? 1 : 0; }
int portArDiagIsOn(void) { return sDiagOn; }

/* port_heist.c portHeistChrNoAi: 1 = no chr runs its AI list (diag freeze; solo test tool) */
int portArFreezeAi(void) { return sFreezeAi; }

static int arFreezeSet(int on)
{
    if (on && (portCoopActive() || portNetActive()))
    {
        portConPrintf(1, "diag freeze: solo only (it changes the sim)\n");
        return 0;
    }
    sFreezeAi = on ? 1 : 0;
    portConPrintf(1, "diag freeze %s: %s\n", sFreezeAi ? "ON" : "off", sFreezeAi ? "no AI list runs (the chrs finish their current action)" : "AI runs");
    return 1;
}
int portArFreezeSet(int on) { return arFreezeSet(on); }

/* a new stage: forget the marks of the old one (port_mode / the Lua stage_start: ar.reset) */
void portArReset(void)
{
    memset(sMark, 0, sizeof(sMark));
    memset(sSay, 0, sizeof(sSay));
    memset(sPoint, 0, sizeof(sPoint));
    memset(sRing, 0, sizeof(sRing));
    memset(sRoomLabel, 0, sizeof(sRoomLabel));
    memset(sDiagText, 0, sizeof(sDiagText));
    sDoorRuleN = 0;
    sFreezeAi = 0;
}

/* raw keys (input_pc.c): F3 the diagnostics, Shift+F3 the specs */
void portArKey(int shift)
{
    if (shift)
    {
        sArOn = !sArOn;
        portConPrintf(1, "AR specs %s\n", sArOn ? "on" : "off");
        portGameSfxPlay(sArOn ? WATCH_ON_SFX : WATCH_OFF_SFX, 0.6f, 1.0f, 0.0f, 0.0f);
    }
    else
    {
        sDiagOn = !sDiagOn;
        portConPrintf(1, "diag %s\n", sDiagOn ? "on" : "off");
    }
}

/* console: `diag [on|off|freeze [on|off]]`, `ar [on|off|<layer> on|off|layers]` */
void portArConsole(const char *cmd, const char *a1, const char *a2)
{
    const int isDiag = cmd != NULL && (cmd[0] == 'd' || cmd[0] == 'D');
    int i;

    if (isDiag)
    {
        if (a1 == NULL) sDiagOn = !sDiagOn;
        else if (strcmp(a1, "on") == 0 || strcmp(a1, "1") == 0) sDiagOn = 1;
        else if (strcmp(a1, "off") == 0 || strcmp(a1, "0") == 0) sDiagOn = 0;
        else if (strcmp(a1, "freeze") == 0) { arFreezeSet(a2 == NULL ? !sFreezeAi : (strcmp(a2, "on") == 0 || strcmp(a2, "1") == 0)); return; }
        else { portConPrintf(0, "usage: diag [on|off]   diag freeze [on|off] (solo: stops every AI list)\n"); return; }
        portConPrintf(1, "diag %s\n", sDiagOn ? "on" : "off");
        return;
    }
    if (a1 == NULL) sArOn = !sArOn;
    else if (strcmp(a1, "on") == 0 || strcmp(a1, "1") == 0) sArOn = 1;
    else if (strcmp(a1, "off") == 0 || strcmp(a1, "0") == 0) sArOn = 0;
    else if (strcmp(a1, "layers") == 0)
    {
        for (i = 0; i < ARL_N; i++) portConPrintf(0, "  %s %s\n", sLayerName[i], sLayer[i] ? "on" : "off");
        return;
    }
    else if (portArLayer(a1, a2 == NULL ? -1 : (strcmp(a2, "on") == 0 || strcmp(a2, "1") == 0)) >= 0)
    {
        if (a2 == NULL) portArLayer(a1, !portArLayer(a1, -1));
        portConPrintf(1, "ar %s %s\n", a1, portArLayer(a1, -1) ? "on" : "off");
        return;
    }
    else { portConPrintf(0, "usage: ar [on|off]   ar layers   ar <suspicion|speech|keys|points|doors|cameras|radar> [on|off]\n"); return; }
    portConPrintf(1, "AR specs %s\n", sArOn ? "on" : "off");
}

/* ---- the draw lists ---------------------------------------------------------------------------------------------------------------
 * Rects first, then text (the two need different combiner state, as in port_console.c). Coordinates are the console's 2D space. */

#define AR_MAXRECT 2400
#define AR_MAXTEXT 140
typedef struct { s16 x0, y0, x1, y1; u32 c; } ArRect;
typedef struct { s16 x, y; u32 c; char t[AR_TEXTLEN + 16]; } ArText;
static ArRect sR[AR_MAXRECT];
static int sRN;
static ArText sT[AR_MAXTEXT];
static int sTN;
static s32 sW = 640, sH = 480;

static void rect(s32 x0, s32 y0, s32 x1, s32 y1, u32 c)
{
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > sW) x1 = sW;
    if (y1 > sH) y1 = sH;
    if (x1 <= x0 || y1 <= y0 || (c & 0xFF) == 0 || sRN >= AR_MAXRECT) return;
    sR[sRN].x0 = (s16) x0; sR[sRN].y0 = (s16) y0; sR[sRN].x1 = (s16) x1; sR[sRN].y1 = (s16) y1; sR[sRN].c = c;
    sRN++;
}

static s32 textW(const char *t)
{
    s32 h = 0, w = 0;

    textMeasure(&h, &w, (char *) t, ptrFontBankGothicChars, ptrFontBankGothic, 0);
    return w;
}

static void text(s32 x, s32 y, const char *t, u32 c)
{
    if (t == NULL || t[0] == '\0' || sTN >= AR_MAXTEXT || (c & 0xFF) == 0) return;
    if (x < 2) x = 2;
    if (y < 2) y = 2;
    if (y > sH - 14) y = sH - 14;
    sT[sTN].x = (s16) x; sT[sTN].y = (s16) y; sT[sTN].c = c;
    snprintf(sT[sTN].t, sizeof(sT[sTN].t), "%s", t);
    sTN++;
}

static void textC(s32 cx, s32 y, const char *t, u32 c) { text(cx - textW(t) / 2, y, t, c); }

static u32 withAlpha(u32 c, f32 a)
{
    s32 al = (s32) ((f32) (c & 0xFF) * (a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a)));

    return (c & 0xFFFFFF00u) | (u32) al;
}

/* a 2D line as runs of thin rects (one per step of the minor axis, at most 40) */
static void line2(f32 x0, f32 y0, f32 x1, f32 y1, u32 c, s32 th)
{
    f32 dx = x1 - x0, dy = y1 - y0, adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
    s32 n, i;

    if (adx < 1.0f && ady < 1.0f) return;
    if (adx > 5000.0f || ady > 5000.0f) return;
    n = (s32) ((adx < ady ? adx : ady) / 2.0f) + 1;
    if (n > 40) n = 40;
    for (i = 0; i < n; i++)
    {
        f32 t0 = (f32) i / (f32) n, t1 = (f32) (i + 1) / (f32) n;
        f32 ax = x0 + dx * t0, ay = y0 + dy * t0, bx = x0 + dx * t1, by = y0 + dy * t1;

        if (adx >= ady)
        {
            f32 my = (ay + by) * 0.5f;
            rect((s32) (ax < bx ? ax : bx), (s32) my - th / 2, (s32) (ax < bx ? bx : ax) + 1, (s32) my - th / 2 + th, c);
        }
        else
        {
            f32 mx = (ax + bx) * 0.5f;
            rect((s32) mx - th / 2, (s32) (ay < by ? ay : by), (s32) mx - th / 2 + th, (s32) (ay < by ? by : ay) + 1, c);
        }
    }
}

/* ---- the view ---------------------------------------------------------------------------------------------------------------------- */

static f32 sMvp[4][4];
static f32 sCam[4][4];
static f32 sSl, sSt, sSw, sSh, sXs, sXo;
static coord3d sEye;

static int viewSetup(struct player *pl)
{
    Mtxf *cam, *proj;
    s32 i, j;

    cam = pl->field_10CC;
    proj = pl->projmatrixf;
    if (cam == NULL || proj == NULL || pl->c_screenwidth <= 0.0f || pl->c_screenheight <= 0.0f) return 0;
    for (i = 0; i < 4; i++)
    {
        for (j = 0; j < 4; j++)
        {
            sCam[i][j] = cam->m[i][j];
            sMvp[i][j] = cam->m[i][0] * proj->m[0][j] + cam->m[i][1] * proj->m[1][j] + cam->m[i][2] * proj->m[2][j] + cam->m[i][3] * proj->m[3][j];
        }
    }
    sSl = pl->c_screenleft;
    sSt = pl->c_screentop;
    sSw = pl->c_screenwidth;
    sSh = pl->c_screenheight;
    if (portPillarboxFrame())
    {   /* the 3D sits in a centred 4:3 band */
        sXs = (f32) sH * (4.0f / 3.0f) / 320.0f;
        sXo = ((f32) sW - (f32) sH * (4.0f / 3.0f)) * 0.5f;
    }
    else
    {   /* wide (the projection is widened to the window) or stretched: the 320-unit frame spans the window */
        sXs = (f32) sW / 320.0f;
        sXo = 0.0f;
    }
    sEye = pl->prop != NULL ? pl->prop->pos : sEye;
    return 1;
}

/* world -> console space. 1 = in front of the camera (sx, sy set, maybe off screen); 0 = behind */
static int proj(f32 x, f32 y, f32 z, f32 *sx, f32 *sy)
{
    f32 cx = x * sMvp[0][0] + y * sMvp[1][0] + z * sMvp[2][0] + sMvp[3][0];
    f32 cy = x * sMvp[0][1] + y * sMvp[1][1] + z * sMvp[2][1] + sMvp[3][1];
    f32 cw = x * sMvp[0][3] + y * sMvp[1][3] + z * sMvp[2][3] + sMvp[3][3];
    f32 px, py;

    if (cw < 1.0f) return 0;
    px = sSl + (cx / cw + 1.0f) * 0.5f * sSw;
    py = sSt + (1.0f - cy / cw) * 0.5f * sSh;
    *sx = sXo + px * sXs;
    *sy = py * ((f32) sH / 240.0f);
    return 1;
}

static int onScreen(f32 sx, f32 sy, f32 m) { return sx >= -m && sx <= (f32) sW + m && sy >= -m && sy <= (f32) sH + m; }

/* view-space direction of a world point (for the edge arrows): +x right, +y up of the camera */
static void viewDir(f32 x, f32 y, f32 z, f32 *vx, f32 *vy, f32 *vz)
{
    *vx = x * sCam[0][0] + y * sCam[1][0] + z * sCam[2][0] + sCam[3][0];
    *vy = x * sCam[0][1] + y * sCam[1][1] + z * sCam[2][1] + sCam[3][1];
    *vz = x * sCam[0][2] + y * sCam[1][2] + z * sCam[2][2] + sCam[3][2];
}

/* a 3D segment, clipped to the near side of the camera (the w > 1 half-space) */
static void line3(f32 ax, f32 ay, f32 az, f32 bx, f32 by, f32 bz, u32 c, s32 th)
{
    f32 wa = ax * sMvp[0][3] + ay * sMvp[1][3] + az * sMvp[2][3] + sMvp[3][3];
    f32 wb = bx * sMvp[0][3] + by * sMvp[1][3] + bz * sMvp[2][3] + sMvp[3][3];
    f32 x0, y0, x1, y1;

    if (wa < 1.0f && wb < 1.0f) return;
    if (wa < 1.0f || wb < 1.0f)
    {
        f32 t = (1.0f - wa) / (wb - wa), mx = ax + (bx - ax) * t, my = ay + (by - ay) * t, mz = az + (bz - az) * t;

        if (wa < 1.0f) { ax = mx; ay = my; az = mz; } else { bx = mx; by = my; bz = mz; }
    }
    if (!proj(ax, ay, az, &x0, &y0) || !proj(bx, by, bz, &x1, &y1)) return;
    if ((x0 < 0 && x1 < 0) || (y0 < 0 && y1 < 0) || (x0 > sW && x1 > sW) || (y0 > sH && y1 > sH)) return;
    if (x0 < -2000.0f || x0 > sW + 2000.0f || x1 < -2000.0f || x1 > sW + 2000.0f || y0 < -2000.0f || y0 > sH + 2000.0f || y1 < -2000.0f || y1 > sH + 2000.0f) return;
    line2(x0, y0, x1, y1, c, th);
}

/* a horizontal circle on the floor (or an arc from a0 to a1 degrees) */
static void ring3(f32 x, f32 y, f32 z, f32 r, u32 c, s32 segs, f32 a0, f32 a1)
{
    s32 i;
    f32 px = 0, pz = 0;

    for (i = 0; i <= segs; i++)
    {
        f32 a = (a0 + (a1 - a0) * (f32) i / (f32) segs) * AR_DEG2RAD, qx = x + sinf(a) * r, qz = z + cosf(a) * r;

        if (i > 0) line3(px, y, pz, qx, y, qz, c, 2);
        px = qx;
        pz = qz;
    }
}

/* ---- icons ------------------------------------------------------------------------------------------------------------------------- */

/* 5x7 pixel glyphs, top row first, bit 4 = left */
static const u8 kQuestion[7] = { 0x0E, 0x11, 0x01, 0x06, 0x04, 0x00, 0x04 };
static const u8 kBang[7]     = { 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04 };
static const u8 kKey[7]      = { 0x0E, 0x11, 0x0E, 0x04, 0x06, 0x04, 0x06 };
static const u8 kCam[7]      = { 0x00, 0x1E, 0x13, 0x1F, 0x1E, 0x08, 0x08 };

/* glyph centred on (cx, bottom y), pixel size s; rows below `fill` (0..1 from the bottom) in `on`, the rest in `off`, a dark backing */
static void glyph(const u8 *g, f32 cx, f32 by, s32 s, u32 on, u32 off, f32 fill)
{
    s32 r, b, x0 = (s32) cx - (5 * s) / 2, y0 = (s32) by - 7 * s;
    s32 lit = (s32) (fill * 7.0f + 0.5f);

    rect(x0 - s / 2 - 1, y0 - s / 2 - 1, x0 + 5 * s + s / 2 + 1, y0 + 7 * s + s / 2 + 1, withAlpha(0x00000080u, (f32) (on & 0xFF) / 255.0f));
    for (r = 0; r < 7; r++)
    {
        const u32 c = (6 - r) < lit ? on : off;

        for (b = 0; b < 5; b++)
        {
            if (g[r] & (0x10 >> b)) rect(x0 + b * s, y0 + r * s, x0 + b * s + s, y0 + r * s + s, c);
        }
    }
}

/* spy-style corner brackets around a screen box */
static void brackets(s32 x0, s32 y0, s32 x1, s32 y1, u32 c)
{
    s32 lx = (x1 - x0) / 4, ly = (y1 - y0) / 5;

    if (lx < 4) lx = 4;
    if (ly < 4) ly = 4;
    rect(x0, y0, x0 + lx, y0 + 2, c); rect(x0, y0, x0 + 2, y0 + ly, c);
    rect(x1 - lx, y0, x1, y0 + 2, c); rect(x1 - 2, y0, x1, y0 + ly, c);
    rect(x0, y1 - 2, x0 + lx, y1, c); rect(x0, y1 - ly, x0 + 2, y1, c);
    rect(x1 - lx, y1 - 2, x1, y1, c); rect(x1 - 2, y1 - ly, x1, y1, c);
}

static void diamond(f32 cx, f32 cy, s32 r, u32 c)
{
    s32 i;

    for (i = -r; i <= r; i += 2)
    {
        s32 w = r - (i < 0 ? -i : i);

        rect((s32) cx - w, (s32) cy + i, (s32) cx + w + 1, (s32) cy + i + 2, c);
    }
}

/* an arrow on the screen edge toward a point that is off screen or behind the camera */
static void edgeArrow(f32 x, f32 y, f32 z, const char *label, u32 c)
{
    f32 vx, vy, vz, l, ex, ey, hw = (f32) sW * 0.5f - 24.0f, hh = (f32) sH * 0.5f - 24.0f, k;

    viewDir(x, y, z, &vx, &vy, &vz);
    (void) vz;
    l = sqrtf(vx * vx + vy * vy);
    if (l < 0.001f) { vx = 0.0f; vy = -1.0f; l = 1.0f; }
    vx /= l;
    vy /= l;
    k = (vx * vx * hh * hh > vy * vy * hw * hw) ? hw / (vx < 0 ? -vx : vx) : hh / (vy < 0 ? -vy : vy);
    ex = (f32) sW * 0.5f + vx * k;
    ey = (f32) sH * 0.5f - vy * k;
    diamond(ex, ey, 6, withAlpha(c, 0.8f));
    rect((s32) (ex + vx * 10.0f) - 2, (s32) (ey - vy * 10.0f) - 2, (s32) (ex + vx * 10.0f) + 3, (s32) (ey - vy * 10.0f) + 3, c);
    if (label != NULL && label[0] != '\0') textC((s32) (ex - vx * 30.0f), (s32) (ey + vy * 20.0f) - 4, label, withAlpha(c, 0.85f));
}

/* ---- chrs ---------------------------------------------------------------------------------------------------------------------------- */

static int chrUp(ChrRecord *c)
{
    return c != NULL && c->prop != NULL && c->model != NULL && !(c->hidden & CHRHIDDEN_REMOVE) && c->actiontype != ACT_DIE
        && c->actiontype != ACT_DEAD && !chrIsDead(c);
}

static int chrIsBond(ChrRecord *c)
{
    int p;

    for (p = 0; p < 4; p++)
    {
        if (g_playerPointers[p] != NULL && g_playerPointers[p]->prop == c->prop) return 1;
    }
    return c->prop->type != PROP_TYPE_CHR;
}

/* the key flags a chr carries (its child props that are keys) */
static u32 chrKeys(ChrRecord *c)
{
    PropRecord *p;
    u32 k = 0;
    int n = 0;

    for (p = c->prop->child; p != NULL && n < 32; p = p->next, n++)
    {
        if (p->type == PROP_TYPE_OBJ && p->obj != NULL && p->obj->type == PROPDEF_KEY) k |= ((KeyRecord *) p->obj)->keyflags;
    }
    return k;
}

static u32 keyColour(u32 k)
{
    if (k & 0x8) return 0xFF3838FFu;    /* RED */
    if (k & 0x4) return 0x4080FFFFu;    /* BLUE */
    if (k & 0x2) return 0x40FF60FFu;    /* GREEN */
    if (k & 0x10) return 0xFFD040FFu;   /* GOLD */
    return 0xE0E0E0FFu;
}

static const char *keyName(u32 k)
{
    if (k & 0x8) return "RED";
    if (k & 0x4) return "BLUE";
    if (k & 0x2) return "GREEN";
    if (k & 0x10) return "GOLD";
    return "KEY";
}

static f32 distFade(f32 d, f32 near, f32 far)
{
    if (d <= near) return 1.0f;
    if (d >= far) return 0.0f;
    return 1.0f - (d - near) / (far - near);
}

/* suspicion of chr toward Bond lp: -1 = not watched by anything; level 0..1; *alert = red "!" */
static f32 chrSuspicion(ChrRecord *c, int lp, int *alert)
{
    f32 s = -1.0f, susp;
    int witness, reported, sees, role, why;

    *alert = 0;
    if (portHeistCoverState(c->chrnum, &susp, &witness, &reported, &sees, &role, &why))
    {
        if (portHeistCoverAlert() || (witness & (1 << lp))) { *alert = 1; s = 1.0f; }
        else
        {
            extern float portHeistCoverSusp(int chrnum, int p);
            f32 mine = portHeistCoverSusp(c->chrnum, lp);

            s = mine >= 0.0f ? mine : susp;
        }
    }
    if (portAiAwareness != NULL)
    {
        int st = 0;
        f32 lvl = 0.0f, lx, lz;

        if (portAiAwareness(c->chrnum, &st, &lvl, &lx, &lz))
        {
            if (st >= 3) { *alert = 1; s = 1.0f; }
            else if (st >= 1 && lvl > s) s = lvl;
        }
    }
    return s;
}

static const char *const kActName[] = { "init", "stand", "kneel", "anim", "die", "dead", "argh", "preargh", "attack", "attackwalk",
    "attackroll", "sidestep", "jumpout", "runpos", "patrol", "gopos", "surrender", "lookat", "surprised", "startalarm", "grenade",
    "turndir", "test", "bondintro", "bonddie", "bondmulti" };

static void drawChr(ChrRecord *c, int lp, struct player *pl)
{
    PropRecord *pp = c->prop;
    f32 dx = pp->pos.x - pl->prop->pos.x, dz = pp->pos.z - pl->prop->pos.z, dy = pp->pos.y - pl->prop->pos.y;
    f32 d = sqrtf(dx * dx + dz * dz), hgt = c->chrheight > 40.0f ? c->chrheight : 180.0f, gy = c->ground;
    f32 hx, hy, fade, x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    int visible = (pp->flags & PROPFLAG_ONSCREEN) != 0, headIn, i, alert;
    ArMark *m = markFind(c->chrnum, 0);
    u32 keys = chrKeys(c);
    f32 susp;

    if (gy < pp->pos.y - 400.0f || gy > pp->pos.y + 50.0f) gy = pp->pos.y - hgt * 0.5f;   /* a bad ground: half a chr under the prop */
    headIn = proj(pp->pos.x, gy + hgt + 8.0f, pp->pos.z, &hx, &hy);

    /* the chr's projected bounds (8 corners of its box) */
    for (i = 0; i < 8; i++)
    {
        f32 w = c->chrwidth > 5.0f ? c->chrwidth : 25.0f, sx, sy;

        if (proj(pp->pos.x + ((i & 1) ? w : -w), gy + ((i & 2) ? hgt : 0.0f), pp->pos.z + ((i & 4) ? w : -w), &sx, &sy))
        {
            if (sx < x0) x0 = sx;
            if (sx > x1) x1 = sx;
            if (sy < y0) y0 = sy;
            if (sy > y1) y1 = sy;
        }
    }

    if (sArOn && visible && headIn && onScreen(hx, hy, 40.0f) && dy > -600.0f && dy < 600.0f)
    {
        /* key holders: brackets in the key's colour, a key tag (a script's ar.outline colour wins) */
        if (sLayer[ARL_KEYS] && (keys != 0 || (m != NULL && (m->outline != 0 || m->bracket))) && x1 > x0)
        {
            u32 col = (m != NULL && m->outline != 0) ? m->outline : (keys != 0 ? keyColour(keys) : (m != NULL ? m->rgba : 0xFFFFFFFFu));
            f32 pulse = 0.75f + 0.25f * sinf((f32) (arNow60() % 60) * (6.2831853f / 60.0f));

            fade = distFade(d, 1500.0f, 4500.0f);
            brackets((s32) x0 - 4, (s32) y0 - 4, (s32) x1 + 4, (s32) y1 + 4, withAlpha(col, fade * pulse));
            rect((s32) x0 - 2, (s32) y0 - 2, (s32) x1 + 2, (s32) y1 + 2, withAlpha(col, fade * 0.12f));   /* the glow: a faint tint */
            if (keys != 0)
            {
                glyph(kKey, x1 + 10.0f, y0 + 16.0f, 2, withAlpha(col, fade), withAlpha(col, fade), 1.0f);
                text((s32) x1 + 16, (s32) y0 + 4, keyName(keys), withAlpha(col, fade));
            }
        }
        if (m != NULL && m->label[0] != '\0')
        {
            textC((s32) hx, (s32) y1 + 6, m->label, withAlpha(m->rgba, distFade(d, 1500.0f, 4500.0f)));
        }
        /* suspicion: "?" filling, red "!" */
        if (sLayer[ARL_SUSPECT] && (susp = chrSuspicion(c, lp, &alert)) >= 0.0f && (alert || susp > 0.02f))
        {
            fade = distFade(d, 1200.0f, 3200.0f);
            if (fade > 0.0f)
            {
                s32 px = d < 900.0f ? 5 : (d < 1800.0f ? 4 : 3);

                if (alert)
                {
                    const int blink = (arNow60() / 10) & 1;

                    glyph(kBang, hx, hy, px, withAlpha(blink ? 0xFF2020FFu : 0xFF6050FFu, fade), 0, 1.0f);
                }
                else
                {
                    u32 on = susp < 0.5f ? 0xFFE040FFu : 0xFF9020FFu;

                    s32 bx0 = (s32) hx + (5 * px) / 2 + px + 2, bh = 7 * px, fillh = (s32) ((f32) bh * (susp > 1.0f ? 1.0f : susp));

                    glyph(kQuestion, hx, hy, px, withAlpha(on, fade), withAlpha(0xC0C0C0FFu, fade * 0.55f), susp);
                    /* the meter beside it: a thin bar filling up */
                    rect(bx0, (s32) hy - bh, bx0 + 3, (s32) hy, withAlpha(0x404040C0u, fade));
                    rect(bx0, (s32) hy - fillh, bx0 + 3, (s32) hy, withAlpha(on, fade));
                }
            }
        }
    }

    if (sDiagOn && visible && d < 2200.0f && dy > -600.0f && dy < 600.0f)
    {
        bool glob = 0;
        s32 lid = c->ailist != NULL ? chraiGetAIListID(c->ailist, &glob) : -1;
        int st = -1, w, rep, sees, role, why;
        f32 lvl = 0.0f, lx, lz, sus = -1.0f, yaw = getsubroty(c->model), eye = gy + hgt - 20.0f;
        char ln[96];
        const char *act = c->actiontype < (u8) (sizeof(kActName) / sizeof(kActName[0])) ? kActName[c->actiontype] : "?";

        if (portAiAwareness != NULL) portAiAwareness(c->chrnum, &st, &lvl, &lx, &lz);
        if (!portHeistCoverState(c->chrnum, &sus, &w, &rep, &sees, &role, &why)) sus = -1.0f;
        if (headIn && onScreen(hx, hy, 0.0f))
        {
            s32 ty = (s32) hy - 46;

            snprintf(ln, sizeof(ln), "#%d %s %s%04x+%d", (int) c->chrnum, act, glob ? "G" : "L", (unsigned) (lid & 0xFFFF), (int) c->aioffset);
            textC((s32) hx, ty, ln, 0xA0FFA0FFu);
            snprintf(ln, sizeof(ln), "aw %d %.2f  sus %.2f  al %d  %dm", st, lvl, sus, (int) c->alertness, (int) (d / 100.0f));
            textC((s32) hx, ty + 12, ln, 0xD0D0D0FFu);
            if (portHeistKoIs(c->chrnum)) textC((s32) hx, ty - 12, "KO", 0xFFC040FFu);
            else if (portHeistCivilianState(c->chrnum) >= 0) textC((s32) hx, ty - 12, "civ", 0x80C0FFFFu);
        }
        if (d < 1600.0f)
        {   /* the sight cone as a fan on the floor (its edges at full range, the arc at 700 units so it stays readable), the hearing
             * ring on the floor (hearingscale x 100 per noise unit: x5, about a silenced shot) */
            f32 ca = sConeDeg * AR_DEG2RAD, r = sConeRange, ra = r < 700.0f ? r : 700.0f, fy = gy + 6.0f;
            u32 cc = sus >= 0.0f && (w & (1 << lp)) ? 0xFF404080u : (sus > 0.02f ? 0xFFB04070u : 0xFFFF6060u);

            (void) eye;
            line3(pp->pos.x, fy, pp->pos.z, pp->pos.x + sinf(yaw - ca) * r, fy, pp->pos.z + cosf(yaw - ca) * r, withAlpha(cc, 0.6f), 2);
            line3(pp->pos.x, fy, pp->pos.z, pp->pos.x + sinf(yaw + ca) * r, fy, pp->pos.z + cosf(yaw + ca) * r, withAlpha(cc, 0.6f), 2);
            line3(pp->pos.x, fy, pp->pos.z, pp->pos.x + sinf(yaw) * ra, fy, pp->pos.z + cosf(yaw) * ra, cc, 2);
            ring3(pp->pos.x, fy, pp->pos.z, ra, cc, 10, (yaw - ca) / AR_DEG2RAD, (yaw + ca) / AR_DEG2RAD);
            ring3(pp->pos.x, gy + 4.0f, pp->pos.z, c->hearingscale * 500.0f, 0x60A0FF50u, 16, 0.0f, 360.0f);
            if (c->actiontype == ACT_GOPOS || c->actiontype == ACT_RUNPOS)
            {
                coord3d *t = &c->act_gopos.targetpos;

                line3(pp->pos.x, gy + 10.0f, pp->pos.z, t->x, gy + 10.0f, t->z, 0x40FFFFA0u, 2);
            }
        }
    }
}

/* ---- the per-frame pass ---------------------------------------------------------------------------------------------------------- */

static void drawSays(int lp, struct player *pl)
{
    int i;
    const s32 now = arNow60();

    for (i = 0; i < AR_SAYS; i++)
    {
        ArSay *s = &sSay[i];
        ChrRecord *c;
        f32 hx, hy, d, a;
        s32 w, bx, by;

        if (!s->used) continue;
        if (now >= s->until60 || now < s->start60 - 60) { s->used = 0; continue; }
        c = chrFindByLiteralId(s->chrnum);
        if (c == NULL || c->prop == NULL) { s->used = 0; continue; }
        if (!(c->prop->flags & PROPFLAG_ONSCREEN)) continue;
        d = sqrtf((c->prop->pos.x - pl->prop->pos.x) * (c->prop->pos.x - pl->prop->pos.x) + (c->prop->pos.z - pl->prop->pos.z) * (c->prop->pos.z - pl->prop->pos.z));
        a = distFade(d, 1500.0f, 3500.0f);
        if (s->until60 - now < 20) a *= (f32) (s->until60 - now) / 20.0f;   /* fade out */
        if (now - s->start60 < 6) a *= (f32) (now - s->start60 + 1) / 6.0f; /* pop in */
        if (a <= 0.02f || !proj(c->prop->pos.x, (c->ground > c->prop->pos.y - 400.0f ? c->ground : c->prop->pos.y - 90.0f) + (c->chrheight > 40.0f ? c->chrheight : 180.0f), c->prop->pos.z, &hx, &hy)) continue;
        if (!onScreen(hx, hy, 60.0f)) continue;
        w = textW(s->text) + 12;
        bx = (s32) hx + 14;
        by = (s32) hy - 46;
        if (bx + w > sW - 4) bx = sW - 4 - w;
        rect(bx, by, bx + w, by + 18, withAlpha(0x000000C8u, a));
        rect(bx, by, bx + w, by + 2, withAlpha(s->rgba, a));
        rect(bx, by + 16, bx + w, by + 18, withAlpha(s->rgba, a * 0.6f));
        rect(bx, by, bx + 2, by + 18, withAlpha(s->rgba, a * 0.8f));
        rect(bx + w - 2, by, bx + w, by + 18, withAlpha(s->rgba, a * 0.8f));
        /* the tail: a step of rects down to the face */
        rect((s32) hx + 16, by + 18, (s32) hx + 24, by + 22, withAlpha(0x000000C8u, a));
        rect((s32) hx + 14, by + 22, (s32) hx + 19, by + 26, withAlpha(0x000000C8u, a));
        rect((s32) hx + 12, by + 26, (s32) hx + 15, by + 29, withAlpha(0x000000C8u, a));
        text(bx + 6, by + 3, s->text, withAlpha(s->rgba, a));
        (void) lp;
    }
}

static void drawPoints(struct player *pl)
{
    int i;

    for (i = 0; i < AR_POINTS; i++)
    {
        ArPoint *p = &sPoint[i];
        f32 sx, sy, dx, dz, dy, d;
        char ln[48];

        if (!p->used) continue;
        dx = p->x - pl->prop->pos.x; dz = p->z - pl->prop->pos.z; dy = p->y - pl->prop->pos.y;
        d = sqrtf(dx * dx + dz * dz + dy * dy);
        snprintf(ln, sizeof(ln), "%s %dm", p->label, (int) (d / 100.0f));
        if (proj(p->x, p->y + 60.0f, p->z, &sx, &sy) && onScreen(sx, sy, -16.0f))
        {
            f32 a = d < 300.0f ? 0.35f + 0.65f * d / 300.0f : 1.0f;   /* fade when you are on it */

            diamond(sx, sy, 7, withAlpha(0x000000A0u, a));
            diamond(sx, sy, 5, withAlpha(p->rgba, a));
            textC((s32) sx, (s32) sy + 10, ln, withAlpha(p->rgba, a));
        }
        else
        {
            edgeArrow(p->x, p->y, p->z, ln, p->rgba);
        }
    }
}

static void drawDoors(int lp, struct player *pl)
{
    int list[256], n, i, k;
    u32 have = portHeistBondKeys(lp);

    n = portHeistDoorList(list, 256);
    for (i = 0; i < n; i++)
    {
        PortHeistDoor dr;
        f32 sx, sy, dx, dz, dy, d;
        const ArDoorRule *rule = NULL;

        if (!portHeistDoorGet(list[i], &dr) || !dr.live) continue;
        dx = dr.x - pl->prop->pos.x; dz = dr.z - pl->prop->pos.z; dy = dr.y - pl->prop->pos.y;
        d = sqrtf(dx * dx + dz * dz);
        if (d > 1800.0f || dy < -400.0f || dy > 400.0f) continue;
        if (!proj(dr.x, dr.y + 40.0f, dr.z, &sx, &sy) || !onScreen(sx, sy, 0.0f)) continue;
        for (k = 0; k < sDoorRuleN; k++) if (sDoorRule[k].idx == dr.idx) { rule = &sDoorRule[k]; break; }
        if (sArOn && sLayer[ARL_DOORS] && dr.keyflags != 0 && dr.open < 0.5f)
        {   /* a keyed door: green tick when your keys open it, a dim lock in its colour when they do not */
            const int opens = (have & dr.keyflags) == dr.keyflags;
            const u32 col = keyColour(dr.keyflags);
            const f32 a = distFade(d, 600.0f, 1800.0f);

            if (opens)
            {
                brackets((s32) sx - 18, (s32) sy - 18, (s32) sx + 18, (s32) sy + 18, withAlpha(col, a));
                textC((s32) sx, (s32) sy - 5, "OPEN", withAlpha(col, a));
            }
            else
            {
                glyph(kKey, sx, sy + 7.0f, 2, withAlpha(col, a * 0.6f), 0, 1.0f);
            }
        }
        if (sDiagOn)
        {
            char ln[48];

            snprintf(ln, sizeof(ln), "D%d k%x %s%s", dr.idx, dr.keyflags, rule != NULL ? rule->label : "", dr.open > 0.5f ? " open" : "");
            textC((s32) sx, (s32) sy + 12, ln, rule != NULL && rule->rgba != 0 ? rule->rgba : 0xC0C0C0FFu);
        }
    }
}

static void drawCameras(int lp, struct player *pl)
{
    int list[64], n, i;
    const int power = portHeistPowerOn();

    if (!power) return;   /* no power: the cameras are dead (they show static), so the specs show none */
    n = portHeistCctvList(list, 64);
    for (i = 0; i < n; i++)
    {
        PortHeistCctv cv;
        f32 sx, sy, dx, dz, dy, d, a, yaw;
        coord3d lpos;
        StandTile *st = NULL;
        f32 ang = 0.0f, floorY, reach;
        int timing;
        u32 col;

        if (!portHeistCctvGet(list[i], &cv) || !cv.live || cv.disabled) continue;
        dx = cv.x - pl->prop->pos.x; dz = cv.z - pl->prop->pos.z; dy = cv.y - pl->prop->pos.y;
        d = sqrtf(dx * dx + dz * dz);
        if (d > 3000.0f || dy < -500.0f || dy > 700.0f) continue;
        timing = portHeistCctvBondTimer(cv.idx, lp) > 0;
        col = timing ? 0xFF3030FFu : 0xFFFFFFFFu;
        a = distFade(d, 1200.0f, 3000.0f);
        /* the floor cone: from under the camera along its current heading, to past its look pad */
        floorY = pl->prop->pos.y - 150.0f;
        reach = 900.0f;
        if (cv.lookpad >= 0 && portHeistPadRaw(cv.lookpad, &lpos, &st, &ang))
        {
            f32 lx = lpos.x - cv.x, lz = lpos.z - cv.z;

            floorY = lpos.y + 2.0f;
            reach = sqrtf(lx * lx + lz * lz) * 1.4f;
            if (reach < 400.0f) reach = 400.0f;
            if (reach > 2000.0f) reach = 2000.0f;
        }
        yaw = (cv.face + 180.0f) * AR_DEG2RAD;   /* PortHeistCctv.face is the camera's yaw + 180 (port_heist.c) */
        if (pl->prop->stan != NULL && cv.room == (pl->prop->stan->room & 0xFF))   /* the cone only in your room: no occlusion test */
        {
            const f32 h = 22.0f * AR_DEG2RAD;
            const u32 cc = withAlpha(timing ? 0xFF303060u : 0xFFFFFF30u, a);

            line3(cv.x, floorY, cv.z, cv.x + sinf(yaw - h) * reach, floorY, cv.z + cosf(yaw - h) * reach, cc, 2);
            line3(cv.x, floorY, cv.z, cv.x + sinf(yaw + h) * reach, floorY, cv.z + cosf(yaw + h) * reach, cc, 2);
            ring3(cv.x, floorY, cv.z, reach, cc, 6, (yaw - h) / AR_DEG2RAD, (yaw + h) / AR_DEG2RAD);
        }
        if (proj(cv.x, cv.y + 30.0f, cv.z, &sx, &sy) && onScreen(sx, sy, 0.0f))
        {
            glyph(kCam, sx, sy, 2, withAlpha(col, a), 0, 1.0f);
            if (timing && ((arNow60() / 8) & 1)) textC((s32) sx, (s32) sy + 4, "REC", withAlpha(col, a));
            if (sDiagOn)
            {
                char ln[32];

                snprintf(ln, sizeof(ln), "cam%d t%d/%d", cv.idx, portHeistCctvBondTimer(cv.idx, lp), cv.alarm_ticks);
                textC((s32) sx, (s32) sy + 16, ln, 0xC0C0C0FFu);
            }
        }
    }
}

static void drawRings(void)
{
    int i;
    const s32 now = arNow60();

    for (i = 0; i < AR_RINGS; i++)
    {
        ArRing *r = &sRing[i];
        f32 t;

        if (!r->used) continue;
        t = (f32) (now - r->start60) / 60.0f;   /* one second: grow to the radius and fade */
        if (t < 0.0f || t > 1.0f) { r->used = 0; continue; }
        ring3(r->x, r->y + 4.0f, r->z, r->r * (0.2f + 0.8f * t), withAlpha(r->rgba, 1.0f - t), 20, 0.0f, 360.0f);
    }
}

/* the radar: a top-down disc at the bottom right, your heading up; guards, staff, cameras, points */
static void drawRadar(int lp, struct player *pl)
{
    const s32 R = 64, cx = sW - R - 12, cy = sH - R - 40;
    const f32 range = 2500.0f, th = pl->vv_theta * AR_DEG2RAD, cs = cosf(th), sn = sinf(th);
    s32 i;

    (void) lp;
    rect(cx - R, cy - R, cx + R, cy + R, 0x00000078u);
    rect(cx - R, cy - R, cx + R, cy - R + 1, 0x50FFA060u);
    rect(cx - R, cy + R - 1, cx + R, cy + R, 0x50FFA060u);
    rect(cx - R, cy - R, cx - R + 1, cy + R, 0x50FFA060u);
    rect(cx + R - 1, cy - R, cx + R, cy + R, 0x50FFA060u);
    rect(cx - 2, cy - 3, cx + 2, cy + 3, 0xFFFFFFFFu);
    for (i = 0; i < g_NumChrSlots; i++)
    {
        ChrRecord *c = &g_ChrSlots[i];
        f32 dx, dz, rx, ry;
        int alert;
        f32 s;
        u32 col;

        if (!chrUp(c) || chrIsBond(c)) continue;
        dx = c->prop->pos.x - pl->prop->pos.x;
        dz = c->prop->pos.z - pl->prop->pos.z;
        if (c->prop->pos.y - pl->prop->pos.y > 300.0f || c->prop->pos.y - pl->prop->pos.y < -300.0f) continue;
        /* the view looks along (-sin th, cos th) in GE's xz (port_heist.c radioSfx): right = (cos th, sin th) */
        rx = (dx * cs + dz * sn) / range * (f32) R;
        ry = (dx * -sn + dz * cs) / range * (f32) R;
        if (rx < -R + 3 || rx > R - 3 || ry < -R + 3 || ry > R - 3) continue;
        s = chrSuspicion(c, lp, &alert);
        col = portHeistCivilianState(c->chrnum) >= 0 ? 0x80C0FFFFu : (alert ? 0xFF3030FFu : (s > 0.02f ? 0xFFC040FFu : 0xE0E0E0FFu));
        if (chrKeys(c) != 0) col = keyColour(chrKeys(c));
        if (portHeistKoIs(c->chrnum)) col = 0x707070FFu;
        rect(cx + (s32) rx - 2, cy - (s32) ry - 2, cx + (s32) rx + 2, cy - (s32) ry + 2, col);
    }
    for (i = 0; i < AR_POINTS; i++)
    {
        f32 dx, dz, rx, ry;

        if (!sPoint[i].used) continue;
        dx = sPoint[i].x - pl->prop->pos.x;
        dz = sPoint[i].z - pl->prop->pos.z;
        rx = (dx * cs + dz * sn) / range * (f32) R;
        ry = (dx * -sn + dz * cs) / range * (f32) R;
        if (rx < -R + 4) rx = -R + 4;
        if (rx > R - 4) rx = R - 4;
        if (ry < -R + 4) ry = -R + 4;
        if (ry > R - 4) ry = R - 4;
        diamond(cx + rx, cy - ry, 3, sPoint[i].rgba);
    }
    textC(cx, cy + R + 2, "25m", 0x50FFA0C0u);
}

/* the on-screen diagnostics panel: tick, hash, FPS / frame ms, chrs, alarm / power / cover, the room you are in */
static void drawDiagPanel(int lp, struct player *pl)
{
    static u32 hashShown;
    static s32 hashAt = -1000;
    static OSTime last;
    static f32 ms = 16.7f;
    OSTime now = osGetTime();
    char ln[96];
    int i, up = 0, n = 0, room = -1;
    f32 level;
    int chr, meter;
    static const char *const mname[] = { "off", "unseen", "watched", "SUSPICIOUS", "BLOWN", "ALERT" };
    s32 y = 140, x = 10;

    if (last != 0 && now > last)
    {
        f32 f = (f32) ((now - last) / 46875ull);   /* osGetTime: 46875 counts per ms */

        if (f < 1000.0f) ms = ms * 0.9f + f * 0.1f;
    }
    last = now;
    if (g_GlobalTimer - hashAt >= 30 || g_GlobalTimer < hashAt)
    {   /* the sim state hash, read every half second (the co-op hash; read-only) */
        PortNetHashMsg hm;
        char dump[4];

        memset(&hm, 0, sizeof(hm));
        hashShown = portCoopStateHash(&hm, dump, 1) == 0 ? hm.hash : 0;
        hashAt = g_GlobalTimer;
    }
    for (i = 0; i < g_NumChrSlots; i++)
    {
        if (g_ChrSlots[i].model != NULL) { n++; if (chrUp(&g_ChrSlots[i])) up++; }
    }
    if (pl->prop->stan != NULL) room = pl->prop->stan->room & 0xFF;
    meter = portHeistCoverMeter(lp, &level, &chr);
    rect(x - 4, y - 4, x + 250, y + 12 * 7 + 4, 0x00000090u);
    snprintf(ln, sizeof(ln), "DIAG  tick %d  hash %08x", (int) g_GlobalTimer, hashShown);
    text(x, y, ln, 0x60FF60FFu);
    snprintf(ln, sizeof(ln), "%.1f fps  %.1f ms  chrs %d up / %d", ms > 0.0f ? 1000.0f / ms : 0.0f, ms, up, n);
    text(x, y + 12, ln, 0xFFFFFFFFu);
    snprintf(ln, sizeof(ln), "alarm %s  power %s  cover %s %.2f", portHeistAlarmActive() ? "ON" : "off", portHeistPowerOn() ? "on" : "OUT",
             meter >= 0 && meter <= 5 ? mname[meter] : "?", level);
    text(x, y + 24, ln, portHeistAlarmActive() ? 0xFF6060FFu : 0xFFFFFFFFu);
    snprintf(ln, sizeof(ln), "room %d %s  pos %d %d %d", room, room >= 0 && room < AR_ROOMS ? sRoomLabel[room] : "",
             (int) pl->prop->pos.x, (int) pl->prop->pos.y, (int) pl->prop->pos.z);
    text(x, y + 36, ln, 0xFFE060FFu);
    snprintf(ln, sizeof(ln), "%s%s", sFreezeAi ? "AI FROZEN  " : "", portAiAwareness != NULL ? "ai.awareness linked" : "cover meter only");
    text(x, y + 48, ln, sFreezeAi ? 0xFF4040FFu : 0x909090FFu);
    for (i = 0; i < 4; i++) if (sDiagText[i][0] != '\0') text(x, y + 60 + 12 * i, sDiagText[i], 0xC0E0FFFFu);
}

/* the frame's pass (port_console.c portConRender, after the cover eye; mode only) */
Gfx *portArHudRender(Gfx *gdl)
{
    int lp = portLocalPlayer(), cam, idx, cnt, off, secs, ticks, i;
    struct player *pl = (lp >= 0 && lp < 4) ? g_playerPointers[lp] : NULL;
    char name[8];
    s16 savedX, savedY;
    u32 lastc = 0;
    s32 need;

    if ((!sArOn && !sDiagOn) || pl == NULL || pl->prop == NULL || g_ChrSlots == NULL || !portStageRunning()) return gdl;
    if (ptrFontBankGothic == NULL || ptrFontBankGothicChars == NULL) return gdl;
    if (portCctvOverlayGet(&cam, name, (int) sizeof(name), &idx, &cnt, &off, &secs, &ticks)) return gdl;   /* on a camera feed */
    portConsoleSpace(&sW, &sH);
    if (!viewSetup(pl)) return gdl;
    sRN = sTN = 0;

    if (sArOn && sLayer[ARL_CAMERAS]) drawCameras(lp, pl);
    if (sDiagOn) drawRings();
    else if (sArOn) drawRings();
    if ((sArOn && sLayer[ARL_DOORS]) || sDiagOn) drawDoors(lp, pl);
    for (i = 0; i < g_NumChrSlots; i++)
    {
        ChrRecord *c = &g_ChrSlots[i];

        if (chrUp(c) && !chrIsBond(c)) drawChr(c, lp, pl);
    }
    if (sArOn && sLayer[ARL_POINTS]) drawPoints(pl);
    if (sArOn && sLayer[ARL_SPEECH]) drawSays(lp, pl);
    if ((sArOn && sLayer[ARL_RADAR]) || sDiagOn) drawRadar(lp, pl);
    if (sDiagOn)
    {
        static int logged;

        if (!logged)
        {   /* once per run: tests (run_ar.sh proof) check the layer really drew */
            extern void portConLogf(const char *fmt, ...);

            portConLogf("ge: arhud: diagnostics layer drawing (tick %d)\n", (int) g_GlobalTimer);
            logged = 1;
        }
        drawDiagPanel(lp, pl);
    }
    if (sArOn && !sDiagOn) text(sW - 74, sH - 16, "Q-SPECS", 0x50FFA090u);

    /* emit: as many rects and texts as the master list has room for */
    need = 64;
    if (dynGetFreeGfx2(gdl) < need + 64) return gdl;
    savedX = viGetX();
    savedY = viGetY();
    viSetXY((s16) sW, (s16) sH);
    gdl->words.w0 = (u32) 0x4c << 24;   /* G_CONSOLE2D_EXT, as port_console.c CON_SPACE */
    gdl->words.w1 = ((u32) sW << 16) | (u32) sH;
    gdl++;
    gdl = microcode_constructor(gdl);
    gDPSetScissor(gdl++, G_SC_NON_INTERLACE, 0, 0, sW, sH);
    gDPSetRenderMode(gdl++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetCombineMode(gdl++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    for (i = 0; i < sRN; i++)
    {
        if (dynGetFreeGfx2(gdl) < 3 + 64 + 2000) break;   /* keep room for the text and the tail */
        if (i == 0 || sR[i].c != lastc)
        {
            gDPSetPrimColor(gdl++, 0, 0, (sR[i].c >> 24) & 0xFF, (sR[i].c >> 16) & 0xFF, (sR[i].c >> 8) & 0xFF, sR[i].c & 0xFF);
            lastc = sR[i].c;
        }
        gDPFillRectangle(gdl++, sR[i].x0, sR[i].y0, sR[i].x1, sR[i].y1);
    }
    gDPSetCombineLERP(gdl++, 0, 0, 0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0, 0, 0, 0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0);
    for (i = 0; i < sTN; i++)
    {
        s32 tx = sT[i].x, ty = sT[i].y;

        if (dynGetFreeGfx2(gdl) < 3 + (s32) strlen(sT[i].t) * 12 + 32) break;
        gdl = textRender(gdl, &tx, &ty, sT[i].t, ptrFontBankGothicChars, ptrFontBankGothic, sT[i].c, sW, sH, 0, 0);
    }
    gdl = combiner_bayer_lod_perspective(gdl);
    gdl->words.w0 = (u32) 0x4c << 24;
    gdl->words.w1 = 0;
    gdl++;
    viSetXY(savedX, savedY);
    return gdl;
}

#endif /* TARGET_N64 */
