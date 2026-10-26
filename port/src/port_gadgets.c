/* port_gadgets.c - the heist's spycraft gadgets, C side (designs/bank-heist.md section 21; the rules are in tools/heist_gadgets.lua,
 * the Lua glue in port/luavm/port_luavm_gadgets.c).
 *
 * What C does that the script cannot:
 *   - the trigger of a gadget in hand (bondview2.c, each Bond's tick, before MoveBond: his synchronized buttons): with one of the
 *     script's gadget items in his right hand the fire button (Z) is taken here, never reaches the stock gun code (the piton gun's
 *     stock code fires a grenade), and is counted for the script (press edges and the held level, per Bond);
 *   - the reel (piton gun / abseil / vent): Bond is held (no stick, no buttons) and carried along a path of up to 4 points at a fixed
 *     speed, placed before MoveBond every tick (his camera is computed there) (smooth at 60 Hz, not the script's 4 Hz), and set down on the end pad's floor at the end;
 *   - the pickpocket's lift (a chr's keys, not his gun, dropped and picked up by Bond at once);
 *   - the disguise class the cover scan reads (port_heist.c coverScan / coverCamSuspect);
 *   - the overlay (the rope, the aim marker, the progress bar): presentation only, drawn for this machine's Bond.
 * Sim state (the reel, the trigger counts, the disguises) is in the co-op / netplay state hash (portGadgetsHash, port_heist.c) and in
 * the rollback snapshot (this file's statics: scripts/port/snap_embed.py SIM_PORT). The overlay's inputs are presentation (never hashed).
 * Reads only per-Bond sim state; never the local player outside the overlay. PC only. */
#ifndef TARGET_N64

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ultra64.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include <math.h>
#include "game/chr.h"
#include "game/player.h"
#include "game/bondview.h"
#include "game/stan.h"
#include "game/model.h"
#include "game/gun.h"
#include "game/dyn.h"
#include "game/textrelated.h"
#include "fr.h"
#include "port_blocks.h"
#include "port_mode.h"

#define GB_MAX   4
#define PATH_MAX 4
#define ITEMS_MAX 16

extern s32 get_cur_playernum(void);
extern void set_cur_player(s32 p);
extern void portTeleportBondToPad(PropRecord *bp, s32 pad, f32 offx, f32 offz);   /* port_hostage.c */
extern ChrRecord *chrFindByLiteralId(s32 id);
extern void propobjSetDropped(PropRecord *prop, DROPTYPE droptype);
extern TICKOP propPickupByPlayer(PropRecord *prop, bool showstring);
extern void propExecuteTickOperation(PropRecord *prop, TICKOP op);
extern int portHeistOffsetStan(coord3d *pos, StandTile **stan, float dx, float dz);
extern void portConLogf(const char *fmt, ...);
extern int portLocalPlayer(void);
extern s32 g_ClockTimer;
extern unsigned int portSimOnlyTickCount(void);   /* port_simonly.c: ticks this stage (the same on every machine) */

typedef struct
{
    /* the trigger with a gadget in hand */
    s32 edges;            /* fire presses not yet taken by the script */
    s32 held;             /* fire held now (with a gadget in hand) */
    s32 heldTicks;        /* ticks it has been held */
    u16 prevBtn;
    s32 testFire;         /* test.fire: ticks of a held fire still to give */
    s32 testTaps;         /* test.fire(p, 0, n): press edges to give */
    u32 edgeTick;         /* the stage tick of the last press (the safe's dial is timed to the tick) */
    s32 testDelay;        /* ticks before the test taps start */
    /* the reel */
    s32 reel;             /* 0 none, 1 carried */
    f32 pts[PATH_MAX][3]; /* feet positions */
    s32 npts;
    f32 t, total;         /* seconds along it, seconds in all */
    s32 padFrom, padTo;   /* the start and end pads: their floor tiles while carried, and where he is set down */
    s32 done;             /* reels finished (the script reads the count) */
    /* the disguise: 0 none, 1 staff, 2 guard */
    s32 disguise;
} GadgetBond;

static GadgetBond sG[GB_MAX];
static s32 sItems[ITEMS_MAX];
static s32 sItemN;

/* presentation (set by the script every tick for drawing; never hashed, never read by the sim) */
static f32 sBar[GB_MAX] = { -1.0f, -1.0f, -1.0f, -1.0f };
static u32 sBarCol[GB_MAX];
static s32 sMarkOn[GB_MAX];
static f32 sMark[GB_MAX][3];
static s32 sXray[GB_MAX];
static s32 sDialOn[GB_MAX];
static u32 sDialStart[GB_MAX];
static f32 sDialPeriod[GB_MAX], sDialCenter[GB_MAX], sDialWidth[GB_MAX];

void portGadgetsReset(void)
{
    int p;

    memset(sG, 0, sizeof(sG));
    sItemN = 0;
    for (p = 0; p < GB_MAX; p++) { sBar[p] = -1.0f; sMarkOn[p] = 0; sXray[p] = 0; sDialOn[p] = 0; }
}

static int isGadgetItem(s32 item)
{
    int i;

    for (i = 0; i < sItemN; i++) if (sItems[i] == item) return 1;
    return 0;
}

int portGadgetsSetItems(const int *items, int n)
{
    int i;

    sItemN = 0;
    for (i = 0; i < n && i < ITEMS_MAX; i++) sItems[sItemN++] = items[i];
    return sItemN;
}

static StandTile *padStan(s32 pad)
{
    if (pad < 0 || pad >= (s32) portPadCount()) return NULL;
    return g_CurrentSetup.pads[pad].stan;
}

/* Bond's feet at (x, fy, z) on tile st (in the air, outside the floor: the reel's own placement, as testTeleportBond does for a pad) */
static void placeBond(struct player *pl, f32 x, f32 fy, f32 z, StandTile *st)
{
#ifdef VERSION_EU
    const f32 f6c = 0.20039999485f, f3b8 = 0.118799984f;
#else
    const f32 f6c = 0.170000016689f, f3b8 = 0.100000024f;
#endif
    coord3d pos, theta;

    pos.x = x;
    pos.z = z;
    pos.y = fy + pl->eyeheight;
    theta = pl->field_488.theta_transform;
    pl->field_70 = fy;
    pl->field_7C = 0.0f;
    pl->field_6C = fy / f6c;
    change_player_pos_to_target(&pl->field_488, &pos, st);
    pl->field_488.theta_transform = theta;
    pl->prop->pos = pos;
    pl->bondprevpos = pos;
    pl->prop->stan = st;
    pl->field_3B8.x = pl->field_488.pos.x / f3b8;
    pl->field_3B8.y = pl->field_488.pos.y / f3b8;
    pl->field_3B8.z = pl->field_488.pos.z / f3b8;
}

static void reelStep(int p)
{
    GadgetBond *g;
    struct player *pl;
    f32 s, acc;
    int k;

    g = &sG[p];
    pl = g_playerPointers[p];
    if (pl == NULL || pl->prop == NULL || pl->bonddead)
    {
        g->reel = 0;
        return;
    }
    g->t += (f32) g_ClockTimer / 60.0f;
    if (g->t >= g->total || g->npts < 2)
    {
        g->reel = 0;
        g->done++;
        portTeleportBondToPad(pl->prop, g->padTo, 0.0f, 0.0f);
        pl->field_7C = 0.0f;
        return;
    }
    /* constant speed along the polyline */
    {
        f32 len[PATH_MAX], all = 0.0f;

        for (k = 0; k + 1 < g->npts; k++)
        {
            f32 dx = g->pts[k + 1][0] - g->pts[k][0], dy = g->pts[k + 1][1] - g->pts[k][1], dz = g->pts[k + 1][2] - g->pts[k][2];

            len[k] = sqrtf(dx * dx + dy * dy + dz * dz);
            all += len[k];
        }
        s = all * (g->t / g->total);
        acc = 0.0f;
        for (k = 0; k + 1 < g->npts; k++)
        {
            if (s <= acc + len[k] || k + 2 == g->npts)
            {
                f32 f = len[k] > 0.0f ? (s - acc) / len[k] : 1.0f;
                StandTile *st = padStan(k + 2 == g->npts ? g->padTo : g->padFrom);

                if (f > 1.0f) f = 1.0f;
                if (st == NULL) st = pl->prop->stan;
                placeBond(pl,
                          g->pts[k][0] + (g->pts[k + 1][0] - g->pts[k][0]) * f,
                          g->pts[k][1] + (g->pts[k + 1][1] - g->pts[k][1]) * f,
                          g->pts[k][2] + (g->pts[k + 1][2] - g->pts[k][2]) * f, st);
                break;
            }
            acc += len[k];
        }
    }
}

/* gunfire.c: the names a gadget item shows (the watch's two title lines, its short and long names), in the game's text form (a newline
 * ends a string); NULL = the stock text. Only for the script's gadget items, in a mode. */
const char *portGadgetsItemText(int item, int which)
{
    static const struct { s32 item; const char *t[4]; } names[] = {
        { ITEM_PITONGUN, { "Piton\n", "Gun\n", "Piton Gun\n", "Piton Gun\n" } },
        { ITEM_GASKEYRING, { "Lock\n", "Picks\n", "Lockpicks\n", "Lockpicks\n" } },
        { ITEM_CREDITCARD, { "Throwing\n", "Coins\n", "Coins\n", "Throwing Coins\n" } },
        { ITEM_DATATHIEF, { "Tape\n", "Loop\n", "Tape Loop\n", "Camera Tape Loop\n" } },
        { ITEM_BOMBDEFUSER, { "Sleep\n", "Gas\n", "Sleep Gas\n", "Sleeping Gas Canister\n" } },
        { ITEM_CAMERA, { "Thermal\n", "Imager\n", "Thermal Imager\n", "Thermal Imager\n" } },
    };
    int i;

    if (which < 0 || which > 3 || !portModeActive() || !isGadgetItem(item)) return NULL;
    for (i = 0; i < (int) (sizeof(names) / sizeof(names[0])); i++)
    {
        if (names[i].item == item) return names[i].t[which];
    }
    return NULL;
}

/* ---- the per-Bond tick hooks (bondview2.c; the current player is p) ---- */

void portGadgetsBondTick(int p, s8 *sx, s8 *sy, u16 *buttons)
{
    GadgetBond *g;
    s32 fire, item;

    if (p < 0 || p >= GB_MAX || !portModeActive() || sItemN == 0) return;
    g = &sG[p];
    if (g->reel)
    {   /* carried along the rope: placed before his move (so this tick's camera is there), and nothing reaches MoveBond */
        reelStep(p);
        *sx = 0;
        *sy = 0;
        *buttons = 0;
        g->held = 0;
        g->prevBtn = 0;
        return;
    }
    item = (s32) getCurrentPlayerWeaponId(GUNRIGHT);
    if (!isGadgetItem(item))
    {
        g->held = 0;
        g->heldTicks = 0;
        g->prevBtn = *buttons;
        return;
    }
    fire = (*buttons & Z_TRIG) != 0;
    if (g->testFire > 0) { fire = 1; g->testFire--; }
    if (g->testDelay > 0) g->testDelay--;
    else if (g->testTaps > 0 && !(g->prevBtn & Z_TRIG))
    {   /* a test tap: a press this tick, released the next */
        fire = 1;
        g->testTaps--;
    }
    if (fire && !(g->prevBtn & Z_TRIG))
    {
        g->edges++;
        g->edgeTick = portSimOnlyTickCount();
    }
    g->held = fire;
    g->heldTicks = fire ? g->heldTicks + 1 : 0;
    g->prevBtn = (u16) ((*buttons & ~Z_TRIG) | (fire ? Z_TRIG : 0));
    *buttons &= (u16) ~Z_TRIG;   /* the gadget's own trigger: the stock gun code never sees it */
}

/* ---- the script's calls ---- */

int portGadgetsTakeFire(int p, int *held, int *heldTicks)
{
    int n;

    if (p < 0 || p >= GB_MAX) return 0;
    n = sG[p].edges;
    sG[p].edges = 0;
    *held = sG[p].held;
    *heldTicks = sG[p].heldTicks;
    return n;
}

int portGadgetsTestFire(int p, int ticks, int taps, int delay)
{
    if (p < 0 || p >= GB_MAX) return 0;
    sG[p].testFire = ticks;
    sG[p].testTaps = taps;
    sG[p].testDelay = delay;
    return 1;
}

/* Bond p's view: yaw (degrees, the game's vv_theta: forward is (-sin, cos)), pitch (vv_verta), and his eye and feet height */
int portGadgetsAim(int p, float *yaw, float *pitch, float *x, float *y, float *z, float *feet)
{
    struct player *pl;

    if (p < 0 || p >= (int) portBondCount() || !portBondInPlay(p) || (pl = g_playerPointers[p]) == NULL || pl->prop == NULL) return 0;
    *yaw = pl->vv_theta;
    *pitch = pl->vv_verta;
    *x = pl->prop->pos.x;
    *y = pl->prop->pos.y;
    *z = pl->prop->pos.z;
    *feet = pl->field_70;
    return 1;
}

/* carry Bond p along pts (feet positions, n of them, the first one where he stands) in `secs`, set down on pad `to` (from = the pad
 * whose floor tile he is drawn on until the last leg) */
int portGadgetsReel(int p, const float *pts, int n, float secs, int from, int to)
{
    GadgetBond *g;
    int k;

    if (p < 0 || p >= GB_MAX || n < 2 || n > PATH_MAX || secs <= 0.0f || !portModeActive()) return 0;
    if (p >= (int) portBondCount() || !portBondInPlay(p) || padStan(to) == NULL) return 0;
    g = &sG[p];
    for (k = 0; k < n; k++)
    {
        g->pts[k][0] = pts[k * 3];
        g->pts[k][1] = pts[k * 3 + 1];
        g->pts[k][2] = pts[k * 3 + 2];
    }
    g->npts = n;
    g->t = 0.0f;
    g->total = secs;
    g->padFrom = padStan(from) != NULL ? from : to;
    g->padTo = to;
    g->reel = 1;
    g->edges = 0;
    g->held = 0;
    return 1;
}

int portGadgetsReeling(int p, float *frac, int *done)
{
    if (p < 0 || p >= GB_MAX) return 0;
    *frac = sG[p].total > 0.0f ? sG[p].t / sG[p].total : 0.0f;
    *done = sG[p].done;
    return sG[p].reel;
}

/* where a throw from Bond p lands: walked along the floor from him toward his facing (yaw degrees), up to dist; the floor point */
int portGadgetsFloorPoint(int p, float yaw, float dist, float *ox, float *oy, float *oz)
{
    struct player *pl;
    coord3d at;
    StandTile *st;
    f32 fx, fz, d;

    if (p < 0 || p >= (int) portBondCount() || !portBondInPlay(p) || (pl = g_playerPointers[p]) == NULL || pl->prop == NULL
        || pl->prop->stan == NULL) return 0;
    fx = -sinf(yaw * (M_PI_F / 180.0f));
    fz = cosf(yaw * (M_PI_F / 180.0f));
    for (d = dist; d >= 40.0f; d *= 0.7f)
    {
        at = pl->prop->pos;
        at.y = stanGetPositionYValue(pl->prop->stan, at.x, at.z);
        st = pl->prop->stan;
        if (portHeistOffsetStan(&at, &st, fx * d, fz * d))
        {
            *ox = at.x;
            *oy = stanGetPositionYValue(st, at.x, at.z);
            *oz = at.z;
            return 1;
        }
    }
    *ox = pl->prop->pos.x;
    *oy = pl->field_70;
    *oz = pl->prop->pos.z;
    return 1;
}

/* a chr's facing, degrees (0 = +z; atan2(dx, dz) of where it looks) */
int portGadgetsChrYaw(int chrnum, float *yaw, float *y)
{
    ChrRecord *c = chrFindByLiteralId(chrnum);

    if (c == NULL || c->model == NULL || c->prop == NULL) return 0;
    *yaw = getsubroty(c->model) * (180.0f / M_PI_F);
    *y = c->prop->pos.y;
    return 1;
}

/* the pickpocket: chr's keys (PROPDEF_KEY children, not his gun or hat) leave him and Bond p picks them up at once. Returns the key flags
 * lifted (0 = he had none). */
unsigned portGadgetsLiftKey(int p, int chrnum)
{
    ChrRecord *c = chrFindByLiteralId(chrnum);
    PropRecord *child, *next;
    PropRecord *got[8];
    unsigned flags = 0;
    int n = 0, i;
    s32 saved;

    if (c == NULL || c->prop == NULL || p < 0 || p >= (int) portBondCount() || !portBondInPlay(p)) return 0;
    for (child = c->prop->child; child != NULL; child = next)
    {
        next = child->prev;
        if (child == c->handle_positiondata_hat || child == c->weapons_held[GUNLEFT] || child == c->weapons_held[GUNRIGHT]) continue;
        if (child->obj != NULL && child->obj->type == PROPDEF_KEY && n < 8)
        {
            flags |= ((struct KeyRecord *) child->obj)->keyflags;
            got[n++] = child;
        }
    }
    if (n == 0) return 0;
    saved = get_cur_playernum();
    set_cur_player(p);
    for (i = 0; i < n; i++)
    {
        TICKOP op;

        propobjSetDropped(got[i], 1);
        op = propPickupByPlayer(got[i], TRUE);
        propExecuteTickOperation(got[i], op);
    }
    set_cur_player(saved);
    portConLogf("ge: gadgets: Bond %d lifted keys 0x%x from chr %d\n", p, flags, chrnum);
    return flags;
}

int portGadgetsDisguise(int p, int kind)
{
    if (p < 0 || p >= GB_MAX) return 0;
    if (kind >= 0) sG[p].disguise = kind > 2 ? 2 : kind;
    return sG[p].disguise;
}

/* port_heist.c cover: the room class Bond p counts as in, through his disguise (a guard's uniform: the restricted and staff rooms are his
 * own; a staffer's: the staff rooms) */
int portGadgetsDisguiseClass(int p, int cls)
{
    if (p < 0 || p >= GB_MAX || !portModeActive()) return cls;
    if (sG[p].disguise == 2) return 0;
    if (sG[p].disguise == 1 && cls == 1) return 0;
    return cls;
}

/* port_heist.c cover: a disguised Bond is looked at closely: the "close" fill is this many times faster */
float portGadgetsDisguiseCloseScale(int p)
{
    if (p < 0 || p >= GB_MAX || !portModeActive()) return 1.0f;
    return sG[p].disguise == 2 ? 2.0f : 1.0f;
}

unsigned portGadgetsHash(void)
{
    unsigned h = 0x6A09E667u;
    int p, k, i;

    if (!portModeActive()) return 0;
    for (p = 0; p < GB_MAX; p++)
    {
        const GadgetBond *g = &sG[p];

        h = h * 16777619u + (unsigned) g->edges * 3u + (unsigned) g->held * 5u + (unsigned) g->heldTicks * 7u + (unsigned) g->prevBtn * 11u;
        h = h * 16777619u + (unsigned) g->reel * 13u + (unsigned) g->npts * 17u + (unsigned) (g->t * 600.0f) + (unsigned) (g->total * 600.0f) * 19u;
        h = h * 16777619u + (unsigned) g->padFrom * 23u + (unsigned) g->padTo * 29u + (unsigned) g->done * 31u + (unsigned) g->disguise * 37u
            + (unsigned) g->testFire * 41u + (unsigned) g->testTaps * 43u + g->edgeTick * 47u + (unsigned) g->testDelay * 53u;
        for (k = 0; k < g->npts; k++)
        {
            h = h * 16777619u + (unsigned) (s32) g->pts[k][0] + (unsigned) (s32) g->pts[k][1] * 3u + (unsigned) (s32) g->pts[k][2] * 7u;
        }
    }
    for (i = 0; i < sItemN; i++) h = h * 16777619u + (unsigned) sItems[i];
    return h;
}

/* ---- presentation: the overlay (port_console.c portConRender, this machine's Bond only) ---- */

void portGadgetsBar(int p, float frac, unsigned rgba)
{
    if (p < 0 || p >= GB_MAX) return;
    sBar[p] = frac;
    sBarCol[p] = rgba;
}

/* the safe's dial (presentation): a needle sweeping the bar once every `period` ticks from tick `start`, the click window round `center` */
void portGadgetsDial(int p, int on, unsigned start, float period, float center, float width)
{
    if (p < 0 || p >= GB_MAX) return;
    sDialOn[p] = on;
    sDialStart[p] = start;
    sDialPeriod[p] = period > 1.0f ? period : 1.0f;
    sDialCenter[p] = center;
    sDialWidth[p] = width;
}

unsigned portGadgetsNow(int p, unsigned *edgeTick)
{
    *edgeTick = (p >= 0 && p < GB_MAX) ? sG[p].edgeTick : 0;
    return portSimOnlyTickCount();
}

void portGadgetsXray(int p, int on)
{
    if (p >= 0 && p < GB_MAX) sXray[p] = on;
}

void portGadgetsMarker(int p, int on, float x, float y, float z)
{
    if (p < 0 || p >= GB_MAX) return;
    sMarkOn[p] = on;
    sMark[p][0] = x;
    sMark[p][1] = y;
    sMark[p][2] = z;
}

extern Mtxf *camGetWorldToScreenMtxf(void);
extern Mtxf *currentPlayerGetProjectionMatrixF(void);
extern void portConsoleSpace(s32 *w, s32 *h);
extern s16 bondviewGetCurrentPlayerViewportWidth(void);
extern s16 bondviewGetCurrentPlayerViewportHeight(void);
extern s16 bondviewGetCurrentPlayerViewportUly(void);
extern s16 get_curplayer_viewport_ulx(void);
extern s16 getWidth320or440(void);
extern s16 getHeight330or240(void);

static s32 sOvW, sOvH;

/* world -> console space; 0 when behind the camera */
static int project(const f32 *w, f32 *sx, f32 *sy)
{
    Mtxf *cam = camGetWorldToScreenMtxf(), *proj = currentPlayerGetProjectionMatrixF();
    f32 v[4], c[4];
    int i, j;
    f32 vx, vy, vw, vh, fw, fh;

    if (cam == NULL || proj == NULL) return 0;
    for (j = 0; j < 4; j++)
    {
        v[j] = w[0] * cam->m[0][j] + w[1] * cam->m[1][j] + w[2] * cam->m[2][j] + cam->m[3][j];
    }
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
    if (x1 <= x0 || y1 <= y0) return gdl;
    if (rgba != sLastCol)
    {
        gDPSetPrimColor(gdl++, 0, 0, (rgba >> 24) & 0xFF, (rgba >> 16) & 0xFF, (rgba >> 8) & 0xFF, rgba & 0xFF);
        sLastCol = rgba;
    }
    gDPFillRectangle(gdl++, x0, y0, x1, y1);
    return gdl;
}

#define OV_SPACE(gdl, w, h) do { (gdl)->words.w0 = (u32) 0x4c << 24 /* G_CONSOLE2D_EXT */; \
                                 (gdl)->words.w1 = ((u32) (w) << 16) | (u32) (h); (gdl)++; } while (0)

Gfx *portGadgetsOverlay(Gfx *gdl)
{
    int me = portLocalPlayer();
    GadgetBond *g;
    s16 savedX, savedY;
    f32 bx, by;
    int rope = 0, mark = 0, bar = 0, k;

    if (me < 0 || me >= GB_MAX || !portModeActive() || sItemN == 0) return gdl;
    g = &sG[me];
    portConsoleSpace(&sOvW, &sOvH);
    rope = g->reel && g->npts >= 2;
    mark = sMarkOn[me] && !g->reel;
    bar = sBar[me] >= 0.0f;
    if (!rope && !mark && !bar && !sXray[me] && !sDialOn[me]) return gdl;
    if (dynGetFreeGfx2(gdl) < 2400) return gdl;
    savedX = viGetX();
    savedY = viGetY();
    viSetXY((s16) sOvW, (s16) sOvH);
    OV_SPACE(gdl, sOvW, sOvH);
    gdl = microcode_constructor(gdl);
    gDPSetScissor(gdl++, G_SC_NON_INTERLACE, 0, 0, sOvW, sOvH);
    gDPSetRenderMode(gdl++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetCombineMode(gdl++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    sLastCol = 0;
    gdl = rect(gdl, 0, 0, 0, 0, 0);   /* (nothing: the first rect below sets its colour) */
    if (rope && g_playerPointers[me] != NULL && g_playerPointers[me]->prop != NULL)
    {   /* the line: from his hand to the hook (over the top edge going up; where he started going down), sampled in the world so a
         * hook far above the view still draws the line up to the screen's edge */
        struct player *pl = g_playerPointers[me];
        int up = g->pts[g->npts - 1][1] >= g->pts[0][1];
        const f32 *anchor = g->pts[up ? (g->npts >= 3 ? g->npts - 2 : g->npts - 1) : (g->npts >= 3 ? 1 : 0)];
        f32 th = pl->vv_theta * (M_PI_F / 180.0f), hand[3], w[3];

        hand[0] = pl->prop->pos.x - sinf(th) * 30.0f - cosf(th) * 14.0f;
        hand[1] = pl->prop->pos.y - 28.0f;
        hand[2] = pl->prop->pos.z + cosf(th) * 30.0f - sinf(th) * 14.0f;
        for (k = 0; k <= 320; k++)
        {
            f32 f = (f32) k / 320.0f;

            w[0] = hand[0] + (anchor[0] - hand[0]) * f;
            w[1] = hand[1] + (anchor[1] - hand[1]) * f;
            w[2] = hand[2] + (anchor[2] - hand[2]) * f;
            if (project(w, &bx, &by))
            {
                s32 r = k < 120 ? 2 : 1;

                gdl = rect(gdl, (s32) bx - r, (s32) by - r, (s32) bx + r + 1, (s32) by + r + 1, 0xE8E0C8F0u);
            }
        }
        if (project(anchor, &bx, &by)) gdl = rect(gdl, (s32) bx - 4, (s32) by - 4, (s32) bx + 5, (s32) by + 5, 0xFFD040FFu);
    }
    if (sXray[me] && g_ChrSlots != NULL && g_playerPointers[me] != NULL && g_playerPointers[me]->prop != NULL)
    {   /* the thermal imager: every chr within 35 m as a glowing box, through walls (red: armed, white: not) */
        PropRecord *bp = g_playerPointers[me]->prop;
        int i, drawn = 0;

        for (i = 0; i < g_NumChrSlots && drawn < 160; i++)
        {
            ChrRecord *c = &g_ChrSlots[i];
            f32 w[3], dx, dz, d, sz;
            s32 x, y, hw, hh;
            u32 col;

            if (c->model == NULL || c->prop == NULL || (c->hidden & CHRHIDDEN_REMOVE) || c->actiontype == ACT_DEAD) continue;
            dx = c->prop->pos.x - bp->pos.x;
            dz = c->prop->pos.z - bp->pos.z;
            d = sqrtf(dx * dx + dz * dz);
            if (d > 3500.0f || d < 60.0f) continue;
            w[0] = c->prop->pos.x;
            w[1] = c->prop->pos.y + 80.0f;
            w[2] = c->prop->pos.z;
            if (!project(w, &bx, &by)) continue;
            sz = 26000.0f / (d + 1.0f);
            hw = (s32) (sz * 0.4f) + 4;
            hh = (s32) sz + 8;
            x = (s32) bx;
            y = (s32) by;
            col = c->weapons_held[GUNRIGHT] != NULL ? 0xFF3018C0u : 0xF0F0F0B0u;
            if (c->actiontype == ACT_DIE) col = 0x808080A0u;
            gdl = rect(gdl, x - hw, y - hh, x + hw, y - hh + 2, col);
            gdl = rect(gdl, x - hw, y + hh - 2, x + hw, y + hh, col);
            gdl = rect(gdl, x - hw, y - hh, x - hw + 2, y + hh, col);
            gdl = rect(gdl, x + hw - 2, y - hh, x + hw, y + hh, col);
            gdl = rect(gdl, x - 1, y - hh / 2, x + 2, y + hh / 2, col);
            drawn++;
        }
    }
    if (mark && project(sMark[me], &bx, &by))
    {   /* a grapple point in reach: a bracket round it */
        s32 x = (s32) bx, y = (s32) by;

        gdl = rect(gdl, x - 14, y - 14, x - 6, y - 12, 0x60FF60FFu);
        gdl = rect(gdl, x - 14, y - 14, x - 12, y - 6, 0x60FF60FFu);
        gdl = rect(gdl, x + 6, y - 14, x + 14, y - 12, 0x60FF60FFu);
        gdl = rect(gdl, x + 12, y - 14, x + 14, y - 6, 0x60FF60FFu);
        gdl = rect(gdl, x - 14, y + 12, x - 6, y + 14, 0x60FF60FFu);
        gdl = rect(gdl, x - 14, y + 6, x - 12, y + 14, 0x60FF60FFu);
        gdl = rect(gdl, x + 6, y + 12, x + 14, y + 14, 0x60FF60FFu);
        gdl = rect(gdl, x + 12, y + 6, x + 14, y + 14, 0x60FF60FFu);
        gdl = rect(gdl, x - 2, y - 2, x + 3, y + 3, 0x60FF60FFu);
    }
    if (bar)
    {   /* the work in hand (a lock, a uniform): bottom centre */
        s32 w = sOvW / 4, x0 = (sOvW - w) / 2, y0 = (s32) ((f32) sOvH * 0.70f), fill;
        f32 f = sBar[me] > 1.0f ? 1.0f : sBar[me];

        fill = (s32) (f * (f32) w);
        gdl = rect(gdl, x0 - 3, y0 - 3, x0 + w + 3, y0 + 13, 0x000000A0u);
        gdl = rect(gdl, x0, y0, x0 + w, y0 + 10, 0x404040C0u);
        if (fill > 0) gdl = rect(gdl, x0, y0, x0 + fill, y0 + 10, sBarCol[me] ? sBarCol[me] : 0xFFE040FFu);
    }
    if (sDialOn[me])
    {   /* the safe: the dial bar, the click window (green), the needle sweeping it */
        s32 w = sOvW / 3, x0 = (sOvW - w) / 2, y0 = (s32) ((f32) sOvH * 0.66f), wx0, wx1, nx;
        f32 ph = (f32) ((portSimOnlyTickCount() - sDialStart[me]) % (u32) sDialPeriod[me]) / sDialPeriod[me];

        wx0 = x0 + (s32) ((sDialCenter[me] - sDialWidth[me] * 0.5f) * (f32) w);
        wx1 = x0 + (s32) ((sDialCenter[me] + sDialWidth[me] * 0.5f) * (f32) w);
        nx = x0 + (s32) (ph * (f32) w);
        gdl = rect(gdl, x0 - 4, y0 - 6, x0 + w + 4, y0 + 18, 0x000000B0u);
        gdl = rect(gdl, x0, y0, x0 + w, y0 + 12, 0x505050C0u);
        gdl = rect(gdl, wx0, y0, wx1, y0 + 12, (nx >= wx0 && nx <= wx1) ? 0x60FF60FFu : 0x30A030E0u);
        gdl = rect(gdl, nx - 1, y0 - 4, nx + 2, y0 + 16, 0xFFFFFFFFu);
    }
    gdl = combiner_bayer_lod_perspective(gdl);
    OV_SPACE(gdl, 0, 0);
    viSetXY(savedX, savedY);
    return gdl;
}

#endif /* TARGET_N64 */
