/* port_cctvview.c - the heist security-camera view (designs/bank-heist.md P1 T2, AC's answer 5: "real video from the start").
 *
 * A Bond using a monitor or the camera console watches a CCTV camera: his screen shows the world from the camera, he cycles the cameras
 * and leaves the view with his own buttons, and taking damage throws him out. Two halves:
 *
 *   SIM (deterministic, the same on every machine): which camera each Bond watches (sView), set by Lua (cctv.view / cctv.view_off) or by
 *   the Bond's input in his own tick (portCctvViewTick, bondview2.c, before MoveBond). While watching, his stick and buttons never reach
 *   MoveBond, so he stands still at the console; mouse look and the Port binds are off (bondview2.c portMouseLookPermitted). The state is
 *   in the heist hash.
 *
 *   PRESENTATION (this machine's Bond only): the world draw of his pass comes from the camera. The sim keeps Bond's canonical camera
 *   (two-worlds rule): every matrix and list the sim reads is Bond's. Only three render-side things change in that pass:
 *     1. room visibility: bg.c runs a portal walk from the camera (its room, eye, matrix, fov) before the sim walk, and draws its list
 *        (rooms the sim walk did not reach draw geometry only, as in the co-op margin);
 *     2. the two projection matrices the display list loads (props: projmatrix, bg: field_10E0) are rewritten, after the sim built and
 *        used them, so that Bond-camera space maps to the camera's (P_cam * V_cam * V_bond^-1, and P_cam * V_cam relative to the room
 *        origin): every model and room keeps the matrices the sim computed, the GPU just sees them from elsewhere;
 *     3. the gun and crosshair display lists are dropped, the per-room scissors are off.
 *   A camera that is off (shot, or switched off by a script) is drawn under full-screen static (port_console.c overlay).
 *
 * Controls while watching (GoldenEye's own): A or C-right = next camera, C-left = previous (also the stick pushed left/right), B = leave.
 * Port binds: next / previous weapon or inventory = next / previous camera, use or menu back = leave.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <ultra64.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include "game/bondview.h"
#include "game/player.h"
#include "game/chr.h"
#include "game/stan.h"
#include "game/matrixmath.h"
#include "game/math_atan2f.h"
#include "port_event.h"
#include "port_mode.h"
#include "port_coop.h"
#include "port_lua.h"

#define CV_PI        3.14159265f
#define CV_TAU       6.2831853f
#define CV_FOVY      62.0f      /* a security camera's lens: a little wider than Bond's 60 */
#define CV_EYE_AHEAD 28.0f      /* the eye sits this far in front of the camera head, outside its model */
#define CV_NAMES     32

extern void *portHeistCctvRecord(int idx);
extern int portHeistCctvList(int *out, int max);
extern int portHeistPadRaw(int pad, coord3d *pos, StandTile **stan, float *angle);
extern s32 get_cur_playernum(void);
extern s32 get_is_ramrom_flag(void);
extern s32 mission_timer;
extern void viCctvPerspectiveF(f32 mf[4][4], f32 fovy);
extern s32 portJoyPortCctvTake(void);
extern u16 joyGetButtonsPressedThisFrame(s8 contpadnum, u16 mask);

/* ---- sim state ---- */
static s32 sView[4] = { -1, -1, -1, -1 };   /* the camera setup index each Bond watches, -1 none */
static f32 sViewHp[4];                       /* his health + armour: lower = he was hit, he leaves the view */
static u16 sPrevBtn[4];                      /* buttons held last tick (edges) */
static u16 sMaskBtn[4];                      /* buttons held as the view ended: ignored until released (B must not use the console again) */
static s8 sPrevStick[4];
static s32 sViewTicks[4];                    /* ticks into the current view */
static struct { s32 cam; char name[24]; } sNames[CV_NAMES];
static s32 sNamesN;

/* ---- presentation state (this machine's pass) ---- */
static s32 sPassCam = -1;     /* the camera this pass draws from, -1 none */
static s32 sPassPl = -1;
static s32 sPassRoom = -1;
static coord3d sEye, sLook, sUp;
static Mtxf sW2S;             /* the camera's world-to-screen (lookat) matrix: the walk's field_10CC */
static struct { s32 cam, start, n, geom, frames; s32 rooms[48]; } sInfo = { -1, -1, 0, 0, 0 };

static CCTVRecord *camRec(s32 cam) { return (CCTVRecord *) portHeistCctvRecord(cam); }

static struct player *bondOf(s32 p)
{
    if (p < 0 || p >= 4 || p >= portAllPlayerCount()) return NULL;
    return g_playerPointers[p];
}

static f32 vitals(struct player *pl) { return pl->bondhealth + pl->bondarmour; }

static void viewEmit(s32 p, s32 cam, s32 why)
{
    if (portEventListenerCount(PORT_EV_CCTV_VIEW) > 0)
    {
        portEventEmit(PORT_EV_CCTV_VIEW, p, cam, why, 0);
    }
}

void portCctvViewReset(void)
{
    s32 i;

    for (i = 0; i < 4; i++)
    {
        sView[i] = -1;
        sPrevBtn[i] = 0;
        sMaskBtn[i] = 0;
        sPrevStick[i] = 0;
        sViewTicks[i] = 0;
    }
    sNamesN = 0;
    sPassCam = -1;
    sInfo.cam = -1;
    sInfo.frames = 0;
}

/* 1 = Bond p now watches camera cam (why: CCTV_WHY_*), 0 = refused (no such camera or Bond, or he is down) */
int portCctvView(int p, int cam, int why)
{
    struct player *pl = bondOf(p);

    if (!portModeActive() || pl == NULL || pl->bonddead || camRec(cam) == NULL) return 0;
    if (sView[p] < 0) sMaskBtn[p] = 0;
    sViewHp[p] = vitals(pl);
    if (sView[p] != cam)
    {
        sView[p] = cam;
        sViewTicks[p] = 0;
        viewEmit(p, cam, why);
    }
    return 1;
}

/* 1 = Bond p was watching and now is not */
int portCctvViewOff(int p, int why)
{
    if (p < 0 || p >= 4 || sView[p] < 0) return 0;
    sView[p] = -1;
    sMaskBtn[p] = sPrevBtn[p];
    viewEmit(p, -1, why);
    return 1;
}

int portCctvViewing(int p)
{
    return (p >= 0 && p < 4 && portModeActive()) ? sView[p] : -1;
}

int portCctvViewTicks(int p)
{
    return (p >= 0 && p < 4 && sView[p] >= 0) ? sViewTicks[p] : 0;
}

static s32 cycle(s32 cam, s32 step)
{
    s32 ids[32];
    s32 n = portHeistCctvList(ids, 32), i;

    if (n <= 0) return cam;
    for (i = 0; i < n && ids[i] != cam; i++)
    {
    }
    if (i == n) return ids[0];
    return ids[(i + step + n) % n];
}

/* bondview2.c, Bond p's tick, before MoveBond: the feed's controls; while he watches, nothing reaches MoveBond. 1 = held. */
int portCctvViewTick(int p, s8 *sx, s8 *sy, u16 *buttons)
{
    struct player *pl;
    u16 raw, pressed;
    s32 acts, step = 0, leave = 0;
    s8 rawx;

    if (p < 0 || p >= 4) return 0;
    raw = *buttons;
    rawx = *sx;
    if (sView[p] < 0 || !portModeActive())
    {
        if (sMaskBtn[p] != 0)
        {
            sMaskBtn[p] &= raw;   /* until released */
            *buttons &= (u16) ~sMaskBtn[p];
        }
        sPrevBtn[p] = raw;
        sPrevStick[p] = rawx;
        return 0;
    }
    pl = bondOf(p);
    pressed = (u16) ((raw & ~sPrevBtn[p]) | joyGetButtonsPressedThisFrame((s8) p, 0xFFFF));   /* (a tap between two ticks too) */
    acts = portJoyPortCctvTake();
    sPrevBtn[p] = raw;
    if (pl == NULL || pl->bonddead)
    {
        portCctvViewOff(p, 4);
    }
    else if (vitals(pl) < sViewHp[p] - 0.0001f)
    {
        portCctvViewOff(p, 3);   /* hit: he looks up from the screens */
    }
    else
    {
        sViewHp[p] = vitals(pl);   /* healed / armoured up: the new level is the baseline */
        sViewTicks[p]++;
        if ((pressed & (A_BUTTON | R_CBUTTONS)) || (acts & 1) || (rawx > 50 && sPrevStick[p] <= 50)) step = 1;
        if ((pressed & L_CBUTTONS) || (acts & 2) || (rawx < -50 && sPrevStick[p] >= -50)) step = -1;
        if ((pressed & B_BUTTON) || (acts & 4)) leave = 1;
        if (leave)
        {
            portCctvViewOff(p, 2);
        }
        else if (step != 0)
        {
            s32 next = cycle(sView[p], step);

            if (next != sView[p])
            {
                sView[p] = next;
                sViewTicks[p] = 0;
                viewEmit(p, next, 1);
            }
        }
    }
    sPrevStick[p] = rawx;
    *sx = 0;
    *sy = 0;
    *buttons = 0;
    if (sView[p] < 0)
    {
        sMaskBtn[p] = raw;
    }
    return 1;
}

unsigned portCctvViewHash(void)
{
    unsigned h = 0;
    s32 i;

    for (i = 0; i < 4; i++)
    {
        union { f32 f; unsigned u; } hp;

        hp.f = sViewHp[i];
        h = h * 16777619u + (unsigned) (sView[i] + 1) * 7u + (unsigned) sMaskBtn[i];
        h = h * 16777619u + (unsigned) sPrevBtn[i] * 3u + (unsigned) (u8) sPrevStick[i] * 5u + (unsigned) sViewTicks[i] * 11u
            + (sView[i] >= 0 ? hp.u : 0u);
    }
    return h;
}

/* ---- names ---- */

void portCctvSetName(int cam, const char *name)
{
    s32 i;

    for (i = 0; i < sNamesN && sNames[i].cam != cam; i++)
    {
    }
    if (i == sNamesN)
    {
        if (sNamesN >= CV_NAMES) return;
        sNamesN++;
    }
    sNames[i].cam = cam;
    snprintf(sNames[i].name, sizeof(sNames[i].name), "%s", name != NULL ? name : "");
}

const char *portCctvName(int cam)
{
    s32 i;

    for (i = 0; i < sNamesN; i++)
    {
        if (sNames[i].cam == cam && sNames[i].name[0] != '\0') return sNames[i].name;
    }
    return NULL;
}

/* ---- the camera's eye ---- */

static f32 wrapAngle(f32 a)
{
    while (a >= CV_TAU) a -= CV_TAU;
    while (a < 0.0f) a += CV_TAU;
    return a;
}

/* the live facing of a camera head (the stock sight test: atan2(dx, dz) against unkC4 + unkC8 + pi) */
static f32 camYaw(CCTVRecord *cam)
{
    return wrapAngle(wrapAngle(cam->unkC8) + cam->unkC4 + CV_PI);
}

static int camEye(CCTVRecord *cam, coord3d *eye, coord3d *look, s32 *room)
{
    ObjectRecord *o = (ObjectRecord *) cam;
    PropRecord *prop = o->prop;
    coord3d pos, pad;
    StandTile *st;
    f32 yaw = camYaw(cam), pitch = -0.45f;

    if (prop == NULL) return 0;
    pos = prop->pos;
    if (portHeistPadRaw(cam->lookpad, &pad, &st, NULL))
    {   /* tilt: onto the pad it watches, at chest height */
        f32 dx = pad.x - pos.x, dz = pad.z - pos.z;
        f32 dh = sqrtf(dx * dx + dz * dz);
        f32 dy = (pad.y + 40.0f) - pos.y;

        if (dh > 1.0f)
        {
            pitch = atan2f(dy, dh);   /* the game's atan2f answers in [0, 2pi) */
            if (pitch > CV_PI) pitch -= CV_TAU;
        }
    }
    if (pitch < -1.2f) pitch = -1.2f;
    if (pitch > 0.2f) pitch = 0.2f;
    look->x = sinf(yaw) * cosf(pitch);
    look->y = sinf(pitch);
    look->z = cosf(yaw) * cosf(pitch);
    eye->x = pos.x + look->x * CV_EYE_AHEAD;
    eye->y = pos.y + look->y * CV_EYE_AHEAD;
    eye->z = pos.z + look->z * CV_EYE_AHEAD;
    *room = (prop->rooms[0] != 0xFF) ? (s32) prop->rooms[0] : -1;
    return *room >= 0;
}

/* ---- presentation: the pass ---- */

/* bg.c bgRoomVisibilityRelated, at the start of every pass: 1 = this pass draws a camera feed (the camera's walk runs next) */
s32 portCctvPassBegin(void)
{
    s32 pl = get_cur_playernum();
    CCTVRecord *cam;
    static s32 norender = -1;

    if (norender < 0)
    {   /* GE_CCTV_NORENDER=1 (test): the view's sim side only, Bond's own picture (the A/B check that the feed is presentation) */
        const char *e = getenv("GE_CCTV_NORENDER");

        norender = (e != NULL && e[0] != '0');
    }
    sPassCam = -1;
    if (!portModeActive() || pl != portLocalPlayer() || pl < 0 || pl >= 4 || sView[pl] < 0 || get_is_ramrom_flag() || norender)
    {
        if (pl == portLocalPlayer()) sInfo.cam = -1;
        return 0;
    }
    cam = camRec(sView[pl]);
    if (cam == NULL || !camEye(cam, &sEye, &sLook, &sPassRoom))
    {
        sInfo.cam = sView[pl];
        sInfo.n = 0;
        return 0;
    }
    sUp.x = 0.0f;
    sUp.y = 1.0f;
    sUp.z = 0.0f;
    matrix_4x4_set_lookat(&sW2S, sEye.x, sEye.y, sEye.z, sLook.x, sLook.y, sLook.z, sUp.x, sUp.y, sUp.z);
    sPassCam = sView[pl];
    sPassPl = pl;
    return 1;
}

s32 portCctvPassActive(void)
{
    return sPassCam >= 0 && get_cur_playernum() == sPassPl;
}

Mtxf *portCctvWorldToScreen(void) { return &sW2S; }
f32 portCctvFovY(void) { return CV_FOVY; }
s32 portCctvRoom(void) { return sPassRoom; }
coord3d *portCctvEye(void) { return &sEye; }

static u8 sRoomOn[256];   /* the rooms the feed draws (this pass) */

/* bg.c: what the camera's walk drew (tests, cctv.render_info) */
void portCctvNoteRooms(s32 start, const s32 *rooms, s32 n, s32 geomonly)
{
    s32 i;

    memset(sRoomOn, 0, sizeof(sRoomOn));
    for (i = 0; i < n; i++)
    {
        if (rooms[i] >= 0 && rooms[i] < 256) sRoomOn[rooms[i]] = 1;
    }
    sInfo.cam = sPassCam;
    sInfo.start = start;
    sInfo.n = n;
    sInfo.geom = geomonly;
    sInfo.frames++;
    for (i = 0; i < n && i < 48; i++) sInfo.rooms[i] = rooms[i];
}

static void clampMtx(Mtxf *m)
{
    s32 i, j;

    for (i = 0; i < 4; i++)
    {
        for (j = 0; j < 4; j++)
        {
            if (m->m[i][j] > 32000.0f) m->m[i][j] = 32000.0f;
            else if (m->m[i][j] < -32000.0f) m->m[i][j] = -32000.0f;
        }
    }
}

/* lv.c, right before bgLevelRender: rewrite the two projection matrices this pass's display list loads. The sim built and used them
 * already (as floats); the fixed-point copies are read only by the renderer, when the display list runs. */
void portCctvPassMatrices(void)
{
    Mtxf proj, c, out, rel;
    Mtx *pm;
    Mtx *bgm;

    if (!portCctvPassActive() || g_CurrentPlayer == NULL || g_CurrentPlayer->viewtoworldmtxf == NULL) return;
    pm = currentPlayerGetProjectionMatrix();
    bgm = (Mtx *) (uintptr_t) get_BONDdata_field_10E0();
    if (pm == NULL || bgm == NULL) return;
    viCctvPerspectiveF(proj.m, CV_FOVY);
    /* props: modelview is Bond-camera space (basemtx = field_10CC): back to the world (Bond's view-to-world), into the camera's */
    matrix_4x4_multiply(&sW2S, g_CurrentPlayer->viewtoworldmtxf, &c);
    matrix_4x4_multiply(&proj, &c, &out);
    clampMtx(&out);
    guMtxF2L(out.m, pm);
    /* bg: field_10E0 is P * lookat(eye - current_model_pos): the same for the camera */
    matrix_4x4_set_lookat(&rel, sEye.x - g_CurrentPlayer->current_model_pos.x, sEye.y - g_CurrentPlayer->current_model_pos.y,
                          sEye.z - g_CurrentPlayer->current_model_pos.z, sLook.x, sLook.y, sLook.z, sUp.x, sUp.y, sUp.z);
    matrix_4x4_multiply(&proj, &rel, &out);
    clampMtx(&out);
    guMtxF2L(out.m, bgm);
}

/* ---- the people on the feed ---- */

/* The world draw only draws the props the sim made onscreen (Bond's view): a guard in a room only the camera sees has no matrices this
 * frame. lv.c calls this right after the world draw of a feed pass: each such chr (the camera's rooms, in front of it, not onscreen for
 * Bond) is posed from its current animation into fresh matrices (Bond-camera space, so the feed's projection places it), drawn with the
 * gun in its right hand, and its Model struct (render_pos, the loaded animation frames) put back byte for byte, so nothing the sim reads
 * changes (render_pos is only read for onscreen chrs anyway). Left behind: the model's LOD / BSP node choices (its rwdata), which only a
 * render reads and the sim's own next render of that chr recomputes.
 * Bonds too (PROP_TYPE_VIEWER with a body: co-op and netplay keep one per Bond, posed every tick): the watching Bond's own body is never
 * onscreen in his own view (he sees through his eyes), so a camera that covers him shows him only through this draw; his partner is
 * drawn here when Bond's own view does not have him. A solo Bond has no body (bondviewRemovePlayerBody), so he is not on the feed. */
extern ModelRenderData D_8002CCBC;
extern void subcalcmatrices(ModelRenderData *arg0, Model *arg1);
extern void instcalcmatrices(ModelRenderData *arg0, Model *arg1);
extern void subdraw(ModelRenderData *mrData, Model *mdl);
extern Mtxf *modelFindNodeMtx(Model *model, ModelNode *node, s32 arg2);
extern void *dynAllocate(s32 size);
extern void modelSetDistanceDisabled(s32 param_1);
extern PropRecord *chrpropGetActiveTail(void);
extern Gfx *bgScissorCurrentPlayerViewDefault(Gfx *gdl);
static s32 g_PortCctvExtraChrs;   /* chrs the last feed pass drew this way (tests) */
static s32 g_PortCctvBonds;       /* ... of them Bonds (their co-op bodies: the watching Bond himself, his partner) */

int portCctvExtraChrs(void)
{
    return g_PortCctvExtraChrs;
}

int portCctvExtraBonds(void)
{
    return g_PortCctvBonds;
}

#define CV_CHR_MAX   40
#define CV_CHR_RANGE 6000.0f

static Gfx *cctvDrawChrsLoop(Gfx *gdl, s32 *pn, u8 *top);
#define CV_POOL_TOP  (512 * 1024)   /* the feed's own corner at the top of the frame's pool half (dyn.c adds it in a heist) */
#define CV_POOL_CHR  (24 * 1024)    /* room one more chr (matrices, skinned vertices, its gun) needs at most */

/* dyn.c, the vertex / matrix pool size: the feed's corner, in a mode that has the feeds (a heist mode) */
s32 portCctvPoolExtra(void)
{
    extern s32 portHeistExtraSlots(void);

    return portHeistExtraSlots() > 0 ? CV_POOL_TOP : 0;
}

Gfx *portCctvDrawChrs(Gfx *gdl)
{
    s32 n = 0;

    static s32 off = -1;

    if (off < 0)
    {   /* GE_CCTV_NOCHRS=1 (diagnostic): the feed shows only what the world draw drew */
        const char *e = getenv("GE_CCTV_NOCHRS");

        off = (e != NULL && e[0] != '0');
    }
    if (!portCctvPassActive() || off)
    {
        return gdl;
    }
    g_PortCctvBonds = 0;
    gdl = bgScissorCurrentPlayerViewDefault(gdl);
    modelSetDistanceDisabled(1);   /* full detail: the LOD distance is Bond's, not the camera's */
    /* The model draw takes numbers from the SIM RNG (model.c: a gun's muzzle flash, its size and spin), and this draw happens only on the
     * watching Bond's machine: put both generators and their draw counts back afterwards, or a co-op / netplay partner desyncs the moment
     * a guard fires on the feed (heist P3 determinism test, view 0 vs view 1). */
    {
        extern u64 g_randomSeed;
        extern u64 g_chrObjRandomSeed;
        extern u32 g_portSimRandDraws;
        extern u32 g_portChrObjRandDraws;
        extern s32 g_portRandDrawCount;
        extern s32 g_portRngTrace;
        const u64 seed = g_randomSeed, cseed = g_chrObjRandomSeed;
        const u32 draws = g_portSimRandDraws, cdraws = g_portChrObjRandDraws;
        const s32 dcount = g_portRandDrawCount, trace = g_portRngTrace;

        /* and the frame's matrix / vertex pool (dyn.c, one linear pointer per frame half): the posed chrs' matrices and vertices go to the
         * top of the half, so every allocation the sim's own draw makes after this lands where it lands on a machine with no feed (the
         * layout of that pool is read back through stale pointers two frames later: a view-dependent autoaim, heist P3) */
        extern u8 *g_VtxBuffers[3];
        extern u8 *g_GfxMemPos;
        extern u8 g_GfxActiveBufferIndex;
        u8 *mem = g_GfxMemPos;
        u8 *top = g_VtxBuffers[g_GfxActiveBufferIndex + 1];

        g_portRngTrace = 0;
        if (top != NULL && top - CV_POOL_TOP > mem)
        {
            g_GfxMemPos = top - CV_POOL_TOP;
            gdl = cctvDrawChrsLoop(gdl, &n, top);
        }
        g_GfxMemPos = mem;
        g_portRngTrace = trace;
        g_randomSeed = seed;
        g_chrObjRandomSeed = cseed;
        g_portSimRandDraws = draws;
        g_portChrObjRandDraws = cdraws;
        g_portRandDrawCount = dcount;
    }
    modelSetDistanceDisabled(0);
    g_PortCctvExtraChrs = n;
    return gdl;
}

/* a model's rw data (Model.datas: numRecords words), saved before the feed poses it and put back after: the matrix calc writes the root
 * node's runtime position and angles there, which the sim's own next render of that chr reads (a view-dependent autoaim, heist P3) */
#define CV_RW_MAX 8192
static u8 *sRwChr, *sRwGun;   /* (heap, CV_RW_MAX each: presentation scratch, kept out of the snapshot's statics) */

static void rwSave(Model *m, u8 *buf, s32 *len)
{
    s32 n = (m->datas != NULL && m->obj != NULL) ? m->obj->numRecords : 0;

    if (m->rwdatalen > 0 && n > m->rwdatalen) n = m->rwdatalen;
    n *= 4;
    if (buf == NULL || n < 0 || n > CV_RW_MAX) n = 0;
    if (n > 0) memcpy(buf, m->datas, (size_t) n);
    *len = n;
}

static void rwRestore(Model *m, const u8 *buf, s32 len)
{
    if (len > 0 && m->datas != NULL) memcpy(m->datas, buf, (size_t) len);
}

static Gfx *cctvDrawChrsLoop(Gfx *gdl, s32 *pn, u8 *top)
{
    extern u8 *g_GfxMemPos;
    PropRecord *prop;
    s32 n = 0, rwChr = 0, rwGun = 0;

    if (sRwChr == NULL)
    {
        sRwChr = (u8 *) malloc(CV_RW_MAX);
        sRwGun = (u8 *) malloc(CV_RW_MAX);
    }

    for (prop = chrpropGetActiveTail(); prop != NULL && n < CV_CHR_MAX; prop = prop->prev)
    {
        ChrRecord *chr;
        Model *model, *wmodel = NULL;
        RenderPosView *saved, *wsaved = NULL;
        Model msave, wmsave;
        ModelRenderData rd;
        PropRecord *w;
        f32 dx, dy, dz;

        if ((prop->type != PROP_TYPE_CHR && prop->type != PROP_TYPE_VIEWER) || prop->rooms[0] == 0xFF || !sRoomOn[prop->rooms[0]])
        {
            continue;
        }
        if ((prop->flags & PROPFLAG_ONSCREEN) && !(prop->type == PROP_TYPE_VIEWER && getPlayerPointerIndex(prop) == sPassPl))
        {
            continue;   /* the world draw drew it (Bond's own body never is: he sees through his eyes) */
        }
        chr = prop->chr;
        if (chr == NULL || (model = chr->model) == NULL || model->obj == NULL || (chr->chrflags & CHRFLAG_HIDDEN) || chr->fadealpha <= 0)
        {
            continue;
        }
        dx = prop->pos.x - sEye.x;
        dy = prop->pos.y - sEye.y;
        dz = prop->pos.z - sEye.z;
        if (dx * dx + dy * dy + dz * dz > CV_CHR_RANGE * CV_CHR_RANGE || dx * sLook.x + dy * sLook.y + dz * sLook.z < 0.0f)
        {
            continue;
        }
        if (top - g_GfxMemPos < CV_POOL_CHR)
        {
            break;   /* the feed's corner of the pool is full: the rest are not drawn this frame */
        }
        msave = *model;   /* the whole model goes back afterwards: render_pos and the animation frames subcalcmatrices loads */
        saved = model->render_pos;
        rwSave(model, sRwChr, &rwChr);   /* and its rw data (the root node's position / angle, LOD, toggles): the matrix calc writes it */
        rd = D_8002CCBC;
        rd.basemtx = camGetWorldToScreenMtxf();
        rd.mtxlist = (Mtxf *) dynAllocate(model->obj->numMatrices * (s32) sizeof(Mtxf));
        subcalcmatrices(&rd, model);
        w = chr->weapons_held[GUNRIGHT];
        if (w != NULL && w->obj != NULL && (wmodel = w->obj->model) != NULL && wmodel->obj != NULL && wmodel->attachedto_objinst != NULL
            && wmodel->attachedto_objinst != model->obj->Switches[5])
        {
            wmsave = *wmodel;
            wsaved = wmodel->render_pos;
            rwSave(wmodel, sRwGun, &rwGun);
            rd.basemtx = modelFindNodeMtx(model, wmodel->attachedto_objinst, 0);
            rd.mtxlist = (Mtxf *) dynAllocate(wmodel->obj->numMatrices * (s32) sizeof(Mtxf));
            instcalcmatrices(&rd, wmodel);
        }
        else
        {
            wmodel = NULL;
        }
        rd.zbufferenabled = TRUE;
        rd.gdl = gdl;
        rd.flags = 3;
        rd.PropType = 7;
        rd.envcolour.word = 0;   /* (chrRenderProp: the blood colour, alpha 0 = no fade) */
        rd.fogcolour.word = ((u32) chr->shadecol.r << 24) | ((u32) chr->shadecol.g << 16) | ((u32) chr->shadecol.b << 8) | (u32) chr->shadecol.a;
        subdraw(&rd, model);
        /* the display list reads the matrices as the RSP's fixed point when it runs: convert them in place, as chrRenderProp does after
         * its last pass (bondviewTransformManyPosToViewMatrix) */
        bondviewTransformManyPosToViewMatrix(model->render_pos, model->obj->numMatrices);
        if (wmodel != NULL)
        {
            subdraw(&rd, wmodel);
            bondviewTransformManyPosToViewMatrix(wmodel->render_pos, wmodel->obj->numMatrices);
            *wmodel = wmsave;
            wmodel->render_pos = wsaved;
            rwRestore(wmodel, sRwGun, rwGun);
        }
        gdl = rd.gdl;
        *model = msave;
        model->render_pos = saved;
        rwRestore(model, sRwChr, rwChr);
        n++;
        if (prop->type == PROP_TYPE_VIEWER)
        {
            g_PortCctvBonds++;
        }
    }
    *pn = n;
    return gdl;
}

/* ---- the overlay (port_console.c draws it; this machine's Bond) ---- */

/* 1 = this machine's Bond watches a camera: its name, its place in the list, whether it is off, the clock */
int portCctvOverlayGet(int *cam, char *name, int namelen, int *index, int *count, int *offline, int *seconds, int *ticks)
{
    s32 p = portLocalPlayer();
    s32 ids[32], n, i;
    CCTVRecord *rec;
    const char *nm;

    if (!portModeActive() || p < 0 || p >= 4 || sView[p] < 0) return 0;
    rec = camRec(sView[p]);
    *cam = sView[p];
    n = portHeistCctvList(ids, 32);
    for (i = 0; i < n && ids[i] != sView[p]; i++)
    {
    }
    *index = (i < n) ? i + 1 : 0;
    *count = n;
    *offline = (rec == NULL || ((ObjectRecord *) rec)->prop == NULL || (((ObjectRecord *) rec)->flags & PROPFLAG_CCTV_DISABLED) != 0);
    nm = portCctvName(sView[p]);
    if (nm != NULL) snprintf(name, (size_t) namelen, "%s", nm);
    else snprintf(name, (size_t) namelen, "CAMERA %d", (int) *index);
    *seconds = mission_timer / 60;
    *ticks = sViewTicks[p];
    return 1;
}

/* cctv.render_info(): what this machine's last camera-feed pass drew */
int portCctvRenderInfo(int *cam, int *start, int *rooms, int max, int *geom, int *frames)
{
    s32 i, n = sInfo.n < max ? sInfo.n : max;

    *cam = sInfo.cam;
    *start = sInfo.start;
    *geom = sInfo.geom;
    *frames = sInfo.frames;
    for (i = 0; i < n; i++) rooms[i] = sInfo.rooms[i];
    return n;
}

/* ---- test helper: cctv.test_press(button) holds raw N64 buttons on pad 0 for a few sim ticks (input_pc.c), the real input path ---- */
u16 g_PortTestButtons;
s32 g_PortTestButtonPolls;   /* sim ticks left (input_pc.c counts them down once per tick; it was 6 polls: a wall-clock-dependent hold) */
s32 g_PortTestUse[4];        /* cctv.test_use(p): one Port use-action tap in Bond p's next tick (joy_port.c portJoyPortUseTap) */

void portCctvTestPress(int mask)
{
    g_PortTestButtons = (u16) mask;
    g_PortTestButtonPolls = 4;
}

void portCctvTestUse(int p)
{
    if (p >= 0 && p < 4) g_PortTestUse[p] = 1;
}

/* cctv.test_shot(): a GE_SHOT capture of the next presented frame (gfx_sdl2.cpp; none in a headless build) */
extern int ge_shot_force __attribute__((weak));

void portCctvTestShot(void)
{
    if (&ge_shot_force != NULL)
    {
        ge_shot_force = 1;
    }
}

int portCctvButtonMask(const char *name)
{
    if (strcmp(name, "a") == 0) return A_BUTTON;
    if (strcmp(name, "b") == 0) return B_BUTTON;
    if (strcmp(name, "z") == 0) return Z_TRIG;
    if (strcmp(name, "cl") == 0) return L_CBUTTONS;
    if (strcmp(name, "cr") == 0) return R_CBUTTONS;
    return 0;
}

/* ---- coverage: does a working camera see a point (intel) ---- */

/* 1 = camera cam is working and has (x, z) inside its live cone (+-45 degrees about where its head points now, the stock test) with a
 * clear stan line. Sim-safe: it reads the camera and the stan, writes nothing. */
static PropRecord *sSeeTarget;   /* the chr / Bond being tested: its own collision must not block the line */

int portCctvSeesPoint(int camidx, float x, float y, float z)
{
    CCTVRecord *cam = camRec(camidx);
    ObjectRecord *o = (ObjectRecord *) cam;
    PropRecord *prop;
    StandTile *st;
    f32 dx, dz, d, a;

    (void) y;
    if (cam == NULL || (prop = o->prop) == NULL || (o->flags & PROPFLAG_CCTV_DISABLED)) return 0;
    dx = x - prop->pos.x;
    dz = z - prop->pos.z;
    if (cam->unkE8 > 0.0f && dx * dx + dz * dz > cam->unkE8 * cam->unkE8) return 0;
    a = atan2f(dx, dz);
    d = a - camYaw(cam);
    while (d > CV_PI) d -= CV_TAU;
    while (d < -CV_PI) d += CV_TAU;
    if (d > 0.7853982f || d < -0.7853982f) return 0;
    st = prop->stan;
    if (st == NULL) return 1;
    {   /* as the stock camera test does for Bond: the target's own collision is lifted for the line test (and put back) */
        ChrRecord *tc = sSeeTarget != NULL ? sSeeTarget->chr : NULL;
        u32 old = tc != NULL ? (u32) tc->hidden : 0;
        s32 r;

        if (tc != NULL) tc->hidden |= CHRHIDDEN_MOVING;   /* chrSetMoving(chr, 0); restored bit-exact below, whatever it was */
        r = stanTestLineUnobstructed(&st, prop->pos.x, prop->pos.z, x, z, 0x1B, 100.0f, 100.0f, 0.0f, 1.0f) != 0;
        if (tc != NULL) tc->hidden = (tc->hidden & ~CHRHIDDEN_MOVING) | (old & CHRHIDDEN_MOVING);
        /* the floor: the line walks the stan from the camera's own floor, so it ends on the target's tile (or at least in his room) only
         * when he stands on that floor. A chr straight above or below the cone (x/z inside it) is not seen. */
        if (r && sSeeTarget != NULL && sSeeTarget->stan != NULL && st != sSeeTarget->stan
            && (st == NULL || (st->room & 0xFF) != (sSeeTarget->stan->room & 0xFF))) r = 0;
        return r;
    }
}

/* the same for a chr (chrnum) or a Bond (who < 0: Bond -1 - who) */
int portCctvSeesChr(int camidx, int chrnum)
{
    ChrRecord *c;

    if (PORT_CHR_IS_BOND(chrnum))
    {
        struct player *pl = bondOf(PORT_CHR_BOND_PLAYER(chrnum));

        int r;

        if (pl == NULL || pl->prop == NULL || pl->bonddead) return 0;
        sSeeTarget = pl->prop;
        r = portCctvSeesPoint(camidx, pl->prop->pos.x, pl->prop->pos.y, pl->prop->pos.z);
        sSeeTarget = NULL;
        return r;
    }
    c = chrFindByLiteralId(chrnum);
    if (c == NULL || c->prop == NULL || c->model == NULL) return 0;
    {
        int r;

        sSeeTarget = c->prop;
        r = portCctvSeesPoint(camidx, c->prop->pos.x, c->prop->pos.y, c->prop->pos.z);
        sSeeTarget = NULL;
        return r;
    }
}

/* the room a chr stands in (its first room), -1 gone */
int portCctvChrRoom(int chrnum)
{
    ChrRecord *c = chrFindByLiteralId(chrnum);

    if (c == NULL || c->prop == NULL || c->prop->rooms[0] == 0xFF) return -1;
    return (int) c->prop->rooms[0];
}

/* the room Bond p stands in (his floor tile's), -1 none */
int portCctvBondRoom(int p)
{
    struct player *pl = bondOf(p);

    if (pl == NULL || pl->prop == NULL || pl->prop->stan == NULL) return -1;
    return (int) pl->prop->stan->room;
}
