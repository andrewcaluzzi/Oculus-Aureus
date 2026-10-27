/* port_damplus_geo.c - Dam+ geometry engine: generated wings bolted onto stock Dam at load time.
 *
 * Nothing here is copied from the ROM. Each wing (port_damplus_wings.c) is described in its own design frame (world units:
 * u along a stock edge, v away from the stock room, h up) and placed at run time from stock features found by search:
 *   - seams: SEAM_SHUTTER (door 281's open stan edges, the motor pool), SEAM_INSERT (a doorway on a long open stock stan edge:
 *     two points are inserted into that stock tile's edge in our copy of the stan, so the doorway can sit anywhere on a wall),
 *     SEAM_EDGES (every open stock edge near a point, used as found);
 *   - the stan: a STAGE copy of the packed stock stan with seam points inserted, every stock link remapped, our tiles appended
 *     (and stitched to the seams both ways). The ROM image is never written;
 *   - cuts: stock triangles that reach into a wing's cut box are dropped from that stock room's display list when it loads
 *     (made degenerate in the loaded copy) and their parts outside the box are re-drawn in one of our rooms with the stock
 *     triangle's own texture number, interpolated texture coordinates and vertex colours (read from the bg file at run time);
 *   - rooms: host-pointer room data (vertices + one display list per room, in a 1172 container of stored deflate blocks), Dam
 *     texture NUMBERS only, baked vertex lighting;
 *   - portals and appended global-vis blocks (which stock rooms draw from ours and ours from stock rooms).
 * load_bg_file -> portDamPlusStan() (stan), portDamPlusBgRooms() (rooms, portals, vis, cuts); bgLoadRoomModelData ->
 * portDamPlusRoomLoaded() (the cuts in loaded stock rooms). The setup side is in port_damplus.c. */
#ifndef TARGET_N64

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ultra64.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include "memp.h"
#include "game/bg.h"
#include "game/stan.h"
#include "port_damplus.h"

/* libm calls the project's math.h does not declare (an undeclared float function is int f() and returns garbage) */
extern long lroundf(float);
extern float floorf(float);
extern float ceilf(float);
extern float hypotf(float, float);

extern void *_fileNameLoadToBank(char *filename, s32 loadMethod, s32 size, u8 bank);
extern s32 fileGetIndex(u8 *name);
extern s32 romdataFileServed(s32 index);
extern char *setup_text_pointers[];
extern s32 sizepropdef(PropDefHeaderRecord *pdef);
extern u8 list_of_tilesizes[];
extern void stanDetermineEOF(void *file, s32 origBase, u8 *newBase);
extern void stanLoadFile(void *file);
extern void obLoadBGFileBytesAtOffset(u8 *bgname, u8 *target, s32 offset, s32 len);
extern s32 *dword_CODE_bss_8007FF90;    /* the global vis script (bg.c) */
extern struct levelentry levelinfotable[];
extern s32 levelentry_index;

static char *portDamPlusBgName(void)
{
    return (char *) levelinfotable[levelentry_index].bg_seg_filename;
}

#define DP_DOOR 281            /* the stock rusty shutter beside Bond's start (propdef index) */
#define DP_MAXROOMS 16         /* our rooms: 137 .. 152 at most */
#define DP_MAXWINGS 8
#define DP_MAXTILES 1024
#define DP_TESTROOM_DEPTH 400

struct DpPrefix { s32 stanfile; StandTile *first; };

/* materials: Dam texture numbers, the C0 command word stock rooms use with them, st units per FILE unit (measured off stock
 * rooms' own mapping density), and a base colour */
static const struct { u16 tex; u32 c0; f32 st; u8 r, g, b; } sMats[M_COUNT] = {
    { 1125, 0xC0080002, 21.5f, 150, 150, 150 },   /* road-tunnel asphalt */
    { 1132, 0xC0080003, 19.0f, 120, 120, 120 },   /* road-tunnel rock */
    { 292,  0xC0080002, 27.3f, 135, 135, 135 },   /* road-tunnel ceiling */
    { 1127, 0xC0080002, 54.6f, 170, 170, 170 },   /* hazard stripes */
    { 1383, 0xC0080002, 73.0f, 255, 255, 255 },   /* tunnel light strip */
    { 441,  0xC0080002, 82.0f, 150, 160, 158 },   /* underground wall */
    { 442,  0xC0080002, 70.0f, 140, 150, 148 },   /* underground floor */
    { 1234, 0xC0080002, 100.0f, 120, 132, 128 },  /* underground ceiling */
    { 428,  0xC0080002, 80.0f, 255, 255, 255 },   /* underground lamp */
    { 286,  0xC0080002, 30.6f, 140, 140, 140 },   /* concrete (the valley walls, the quay) */
    { 22,   0xC0080002, 25.0f, 175, 175, 175 },   /* gravel floor (the yard, the crest) */
    { 291,  0xC0080002, 74.0f, 160, 160, 160 },   /* dam face concrete */
    { 299,  0xC0080002, 100.0f, 140, 140, 140 },  /* metal (machinery, steelwork) */
    { 240,  0xC0080002, 140.0f, 160, 165, 165 },  /* floor grating */
    { 286,  0xC0080002, 30.6f, 150, 150, 150 },   /* parapet: concrete (Dam's railing texture 152 is cut out with alpha, which our opaque mode draws black) */
};

static s32 dpFullBright(s32 mat)
{
    return mat == M_LAMP || mat == M_UGLAMP;
}

/* --------------------------------------------------------------------------------------------------------------------------- */
typedef struct
{
    s32 ok;
    const DpWing *w;
    f32 ox, oy, oz;             /* frame origin, file units */
    f32 ux, uz, vx, vz;         /* frame axes (xz) */
    /* seams */
    s32 nseam;
    s16 sp[4][8][3];            /* seam points as read (file units) */
    s32 nsp[4];
    s32 seamRoom[4];
    StandTile *insTile[4];      /* SEAM_INSERT: the stock tile (in the stock block) and the edge the doorway is cut into */
    s32 insEdge[4];
    s32 insFlip[4];             /* the doorway's C (u = 0 end) is at the tile's later point */
    StandTile *lnkTile[4][6];   /* SHUTTER / EDGES: stock tiles + edges our tiles link to */
    s32 lnkEdge[4][6];
    s32 nlnk[4];
    /* shutter */
    f32 W, bu, doorH;
    /* run-time fits from the cuts */
    f32 wallV[4];
    s32 wallN[4];
    f32 outdoor[3];
    s32 outdoorN;
    /* portals: global indices */
    s32 portalIdx[8];
    /* stan */
    s32 tile0, ntiles;          /* our tiles in sTiles[] */
} DpFrame;

static DpFrame sF[DP_MAXWINGS];
static s32 sNF;

/* the motor pool's work pieces (it may grow test rooms) */
static DpPiece sMotorWork[16];
static DpWing sMotorWing;
static s32 sTestRooms;

/* all our stan tiles (any wing): design corners for pad lookups */
typedef struct { s16 p[3][3]; u8 room; s8 wing; u8 outdoor; f32 uvh[3][3]; } DpTri;
static DpTri sTiles[DP_MAXTILES];
static s32 sNTiles;
static StandTile *sTilePtr[DP_MAXTILES];   /* where each of ours ended up in the new block */

static s32 sStanOk;             /* the extended stan is live */
static s32 sMaxRoom;            /* highest room id we add (0 = none) */

/* cuts: per stock room, which of its primary-DL triangles (by ordinal) are dropped when it loads */
typedef struct { u8 room; s32 ntris[2]; u8 *drop[2]; s32 ndrop; } DpRoomCut;   /* [0] primary list, [1] secondary */
static DpRoomCut sCuts[8];
static s32 sNCuts;

/* patch triangles (the stock surfaces around a cut), per our room */
typedef struct { f32 x[3], y[3], z[3], s[3], t[3]; u8 r[3], g[3], b[3]; u32 c0; u16 tex; u8 room; } DpPatch;
static DpPatch *sPatch;
static s32 sNPatch, sMaxPatch;

/* our rooms' data */
static u32 *sVtx[DP_MAXROOMS], *sGdl[DP_MAXROOMS];
static s32 sVtxBytes[DP_MAXROOMS], sGdlBytes[DP_MAXROOMS];
static f32 sRoomPos[DP_MAXROOMS][3];

/* --------------------------------------------------------------------------------------------------------------------------- */
static f32 sLs;   /* levelscale: world = file / ls */

static DpFrame *dpFrameOf(u32 part)
{
    s32 i;
    for (i = 0; i < sNF; i++)
    {
        if (sF[i].w->part == part)
        {
            return &sF[i];
        }
    }
    return NULL;
}

s32 portDamPlusWingReady(u32 part)
{
    DpFrame *f = dpFrameOf(part);
    return f != NULL && f->ok && sStanOk;
}

/* kept for port_damplus.c's motor-pool code */
s32 portDamPlusMotorPoolReady(void)
{
    return portDamPlusWingReady(DP_MOTORPOOL);
}

s32 portDamPlusWingSeamRoom(u32 part, s32 seam)
{
    DpFrame *f = dpFrameOf(part);
    return (f != NULL && seam < f->nseam) ? f->seamRoom[seam] : -1;
}

s32 portDamPlusSeamRoom(void)
{
    return portDamPlusWingSeamRoom(DP_MOTORPOOL, 0);
}

s32 portDamPlusWingPortal(u32 part, s32 k)
{
    DpFrame *f = dpFrameOf(part);
    return (f != NULL && k < 8) ? f->portalIdx[k] : -1;
}

/* design (u, v, h) -> file units (absolute) */
static void dpFile(const DpFrame *f, f32 u, f32 v, f32 h, f32 out[3])
{
    out[0] = f->ox + (u * f->ux + v * f->vx) * sLs;
    out[1] = f->oy + h * sLs;
    out[2] = f->oz + (u * f->uz + v * f->vz) * sLs;
}

/* file units -> design */
static void dpDesign(const DpFrame *f, f32 x, f32 y, f32 z, f32 out[3])
{
    f32 dx = x - f->ox, dz = z - f->oz;

    out[0] = (dx * f->ux + dz * f->uz) / sLs;
    out[1] = (dx * f->vx + dz * f->vz) / sLs;
    out[2] = (y - f->oy) / sLs;
}

void portDamPlusWingToWorld(u32 part, f32 u, f32 v, f32 h, f32 out[3])
{
    DpFrame *f = dpFrameOf(part);

    out[0] = out[1] = out[2] = 0.0f;
    if (f == NULL)
    {
        return;
    }
    dpFile(f, u, v, h, out);
    out[0] /= sLs;
    out[1] /= sLs;
    out[2] /= sLs;
}

void portDamPlusDesignToWorld(f32 u, f32 v, f32 h, f32 out[3])
{
    portDamPlusWingToWorld(DP_MOTORPOOL, u, v, h, out);
}

/* a piece vertex in design coords (seam codes resolve to the stock point) */
static void dpVert(const DpFrame *f, const DpPiece *p, s32 k, f32 *u, f32 *v, f32 *h)
{
    s16 cu = p->u[k];

    if (DP_IS_SEAM(cu))
    {
        s32 s = (cu - 0x7000) / 8, i = (cu - 0x7000) % 8;
        f32 d[3];

        dpDesign(f, f->sp[s][i][0], f->sp[s][i][1], f->sp[s][i][2], d);
        *u = d[0];
        *v = d[1];
        *h = d[2];
        return;
    }
    *u = DP_IS_W(cu) ? f->W - (f32) (cu - 0x4000) : (f32) cu;
    if (DP_IS_VW(p->v[k]))
    {
        s32 s = p->v[k] - 0x7F00;
        *v = (f->wallN[s] > 0) ? f->wallV[s] : 0.0f;
    }
    else
    {
        *v = p->v[k];
    }
    *h = p->fh[k];
}

/* the stan point of a piece vertex: the stock seam point itself, or our own design vertex rounded */
static void dpStanPoint(const DpFrame *f, const DpPiece *p, s32 k, s16 out[3])
{
    s16 cu = p->u[k];

    if (DP_IS_SEAM(cu))
    {
        s32 s = (cu - 0x7000) / 8, i = (cu - 0x7000) % 8;
        out[0] = f->sp[s][i][0];
        out[1] = f->sp[s][i][1];
        out[2] = f->sp[s][i][2];
    }
    else
    {
        f32 u, v, h, fl[3];
        dpVert(f, p, k, &u, &v, &h);
        dpFile(f, u, v, h, fl);
        out[0] = (s16) lroundf(fl[0]);
        out[1] = (s16) lroundf(fl[1]);
        out[2] = (s16) lroundf(fl[2]);
    }
}

static StandTile *dpNextTile(StandTile *t)
{
    return (StandTile *) (((u8 *) t) + list_of_tilesizes[t->tail.hdrTail.pointCount & 0xf]);
}

/* ---- seams ------------------------------------------------------------------------------------------------------------------ */
/* door 281's bound pad, read from the setup file before the setup loads */
static s32 dpDoorPad(f32 pos[3], f32 up[3], f32 *lo, f32 *hi, f32 *height)
{
    char name[64];
    stagesetup *st;
    s32 served;
    u8 *base;
    PropDefHeaderRecord *p;
    BoundPadRecord *bp;
    s32 i;

    name[0] = setup_text_pointers[LEVELID_DAM][0];
    name[1] = '\0';
    strcat(name, setup_text_pointers[LEVELID_DAM] + 1);
    st = _fileNameLoadToBank(name, 1, 256, MEMPOOL_STAGE);
    if (st == NULL)
    {
        return 0;
    }
    served = romdataFileServed(fileGetIndex((u8 *) name));
    base = (u8 *) st;
    p = (PropDefHeaderRecord *) (served ? base + (u32) st->propDefs : (u8 *) st->propDefs);
    for (i = 0; i < DP_DOOR && p->type != PROPDEF_END; i++)
    {
        p = (PropDefHeaderRecord *) (((u32 *) p) + sizepropdef(p));
    }
    if (p->type != PROPDEF_DOOR)
    {
        printf("ge: dam+: propdef %d is not a door - motor pool off\n", DP_DOOR);
        return 0;
    }
    bp = (BoundPadRecord *) (served ? base + (u32) st->boundpads : (u8 *) st->boundpads);
    bp += ((ObjectRecord *) p)->pad;
    pos[0] = bp->pos.f[0];
    pos[1] = bp->pos.f[1];
    pos[2] = bp->pos.f[2];
    up[0] = bp->up.f[0];
    up[1] = bp->up.f[1];
    up[2] = bp->up.f[2];
    *lo = bp->bbox.ymin;
    *hi = bp->bbox.ymax;
    *height = fabsf(bp->bbox.zmax - bp->bbox.zmin);
    return 1;
}

/* the frame from a first point, a +u direction (xz, unit) and the stock tile whose interior v points away from */
static void dpSetFrame(DpFrame *f, const s16 o[3], f32 ux, f32 uz, StandTile *t)
{
    s32 c = t->tail.hdrTail.pointCount & 0xf, k;
    f32 cx = 0.0f, cz = 0.0f;

    for (k = 0; k < c; k++)
    {
        cx += t->points[k].x / (f32) c;
        cz += t->points[k].z / (f32) c;
    }
    f->ox = o[0];
    f->oy = o[1];
    f->oz = o[2];
    f->ux = ux;
    f->uz = uz;
    f->vx = -uz;
    f->vz = ux;
    if ((cx - f->ox) * f->vx + (cz - f->oz) * f->vz > 0.0f)
    {
        f->vx = -f->vx;
        f->vz = -f->vz;
    }
}

static s32 dpSeamShutter(DpFrame *f, StandTile *first)
{
    f32 P[3], U[3], ulo, uhi, height, ul, N[2];
    StandTile *t;
    s32 n = 0, np = 0, i, k;
    s16 pts[4][3];

    if (!dpDoorPad(P, U, &ulo, &uhi, &height))
    {
        return 0;
    }
    U[1] = 0.0f;
    ul = sqrtf(U[0] * U[0] + U[2] * U[2]);
    if (ul < 0.5f)
    {
        printf("ge: dam+: door %d is not upright - motor pool off\n", DP_DOOR);
        return 0;
    }
    U[0] /= ul;
    U[2] /= ul;
    N[0] = U[2];
    N[1] = -U[0];
    for (t = first; *(u32 *) t != 0; t = dpNextTile(t))
    {
        s32 c = t->tail.hdrTail.pointCount & 0xf;

        for (k = 0; k < c; k++)
        {
            StandTilePoint *a = &t->points[k], *b = &t->points[(k + 1) % c];
            f32 da, db, ua, ub;

            if (a->link >= 16)
            {
                continue;
            }
            da = (a->x - P[0]) * N[0] + (a->z - P[2]) * N[1];
            db = (b->x - P[0]) * N[0] + (b->z - P[2]) * N[1];
            ua = (a->x - P[0]) * U[0] + (a->z - P[2]) * U[2];
            ub = (b->x - P[0]) * U[0] + (b->z - P[2]) * U[2];
            if (fabsf(da) > 12.0f || fabsf(db) > 12.0f || ua < ulo - 12.0f || ua > uhi + 12.0f || ub < ulo - 12.0f || ub > uhi + 12.0f
                || fabsf(a->y - P[1]) > 40.0f)
            {
                continue;
            }
            if (n >= 2)
            {
                printf("ge: dam+: more than two open edges under the shutter - motor pool off\n");
                return 0;
            }
            f->lnkTile[0][n] = t;
            f->lnkEdge[0][n] = k;
            f->seamRoom[0] = t->room;
            n++;
            {
                StandTilePoint *e2[2] = { a, b };
                s32 m;

                for (m = 0; m < 2; m++)
                {
                    s32 dup = 0;
                    for (i = 0; i < np; i++)
                    {
                        if (pts[i][0] == e2[m]->x && pts[i][2] == e2[m]->z) dup = 1;
                    }
                    if (!dup && np < 4)
                    {
                        pts[np][0] = e2[m]->x;
                        pts[np][1] = e2[m]->y;
                        pts[np][2] = e2[m]->z;
                        np++;
                    }
                }
            }
        }
    }
    if (n != 2 || np != 3)
    {
        printf("ge: dam+: found %d open edges / %d points under the shutter (want 2 / 3) - motor pool off\n", n, np);
        return 0;
    }
    f->nlnk[0] = 2;
    /* order the three points along U: A (lowest), B, E (highest) */
    {
        f32 pr[3];
        s32 order[3] = { 0, 1, 2 }, a, b;

        for (i = 0; i < 3; i++)
        {
            pr[i] = (pts[i][0] - P[0]) * U[0] + (pts[i][2] - P[2]) * U[2];
        }
        for (a = 0; a < 3; a++)
        {
            for (b = a + 1; b < 3; b++)
            {
                if (pr[order[b]] < pr[order[a]])
                {
                    s32 tmp = order[a];
                    order[a] = order[b];
                    order[b] = tmp;
                }
            }
        }
        for (i = 0; i < 3; i++)
        {
            memcpy(f->sp[0][i], pts[order[i]], sizeof(f->sp[0][i]));
        }
        f->nsp[0] = 3;
    }
    {
        f32 dx = f->sp[0][2][0] - f->sp[0][0][0], dz = f->sp[0][2][2] - f->sp[0][0][2];
        f32 len = sqrtf(dx * dx + dz * dz);
        StandTile *t0 = f->lnkTile[0][0];

        dpSetFrame(f, f->sp[0][0], dx / len, dz / len, t0);
        f->W = len / sLs;
        f->bu = ((f->sp[0][1][0] - f->ox) * f->ux + (f->sp[0][1][2] - f->oz) * f->uz) / sLs;
        f->doorH = height / sLs;
    }
    printf("ge: dam+: shutter seam: room %d, %.0f world units wide (B at %.0f), %.0f high\n", f->seamRoom[0], f->W, f->bu, f->doorH);
    if (f->W < 600.0f || f->W > 1300.0f)
    {
        printf("ge: dam+: the shutter is not the expected size - motor pool off\n");
        return 0;
    }
    return 1;
}

/* a doorway on the open stock edge of `room` nearest the search point: two points C (u = 0) and D (u = width) inserted into it */
static s32 dpSeamInsert(DpFrame *f, s32 si, const DpSeam *s, StandTile *first)
{
    StandTile *t, *bt = NULL;
    s32 bk = -1, k;
    f32 bd = 1e30f, sx = s->x * sLs, sz = s->z * sLs, sy = s->y * sLs;

    for (t = first; *(u32 *) t != 0; t = dpNextTile(t))
    {
        s32 c = t->tail.hdrTail.pointCount & 0xf;

        if (t->room != s->room)
        {
            continue;
        }
        for (k = 0; k < c; k++)
        {
            StandTilePoint *a = &t->points[k], *b = &t->points[(k + 1) % c];
            f32 dx = b->x - a->x, dz = b->z - a->z, l2 = dx * dx + dz * dz, tt, px, pz, d;

            if (a->link >= 16 || l2 < 1.0f || fabsf(a->y - sy) > 60.0f)
            {
                continue;
            }
            tt = ((sx - a->x) * dx + (sz - a->z) * dz) / l2;
            tt = fmaxf(0.0f, fminf(1.0f, tt));
            px = a->x + dx * tt;
            pz = a->z + dz * tt;
            d = hypotf(px - sx, pz - sz);
            if (d < bd)
            {
                bd = d;
                bt = t;
                bk = k;
            }
        }
    }
    if (bt == NULL || bd > 200.0f * sLs)
    {
        printf("ge: dam+: %s: no open stan edge in room %d near %d,%d - wing off\n", f->w->name, s->room, s->x, s->z);
        return 0;
    }
    {
        s32 c = bt->tail.hdrTail.pointCount & 0xf;
        StandTilePoint *a = &bt->points[bk], *b = &bt->points[(bk + 1) % c];
        f32 dx = b->x - a->x, dz = b->z - a->z, dy = b->y - a->y, len = sqrtf(dx * dx + dz * dz);
        f32 ex = dx / len, ez = dz / len, w = s->width * sLs, mid, c0, c1;
        s32 flip = (ex * s->hx + ez * s->hz) < 0.0f;

        if (c + 2 > 10)
        {
            printf("ge: dam+: %s: stock tile has %d points, no room for the doorway - wing off\n", f->w->name, c);
            return 0;
        }
        mid = ((sx - a->x) * ex + (sz - a->z) * ez);
        c0 = mid - w / 2;   /* along A->B */
        c1 = mid + w / 2;
        if (c0 < 2.0f || c1 > len - 2.0f)
        {
            printf("ge: dam+: %s: the doorway does not fit its stock edge (%.0f..%.0f of %.0f) - wing off\n", f->w->name, c0, c1, len);
            return 0;
        }
        /* point 0 = the u = 0 end (C), point 1 = the u = width end (D) */
        {
            f32 ta = flip ? c1 : c0, tb = flip ? c0 : c1;

            f->sp[si][0][0] = (s16) lroundf(a->x + ex * ta);
            f->sp[si][0][1] = (s16) lroundf(a->y + dy * ta / len);
            f->sp[si][0][2] = (s16) lroundf(a->z + ez * ta);
            f->sp[si][1][0] = (s16) lroundf(a->x + ex * tb);
            f->sp[si][1][1] = (s16) lroundf(a->y + dy * tb / len);
            f->sp[si][1][2] = (s16) lroundf(a->z + ez * tb);
            f->nsp[si] = 2;
        }
        f->insTile[si] = bt;
        f->insEdge[si] = bk;
        f->insFlip[si] = flip;
        f->seamRoom[si] = s->room;
        if (si == 0)
        {
            f32 ux = f->sp[0][1][0] - f->sp[0][0][0], uz = f->sp[0][1][2] - f->sp[0][0][2], l = sqrtf(ux * ux + uz * uz);

            dpSetFrame(f, f->sp[0][0], ux / l, uz / l, bt);
        }
        printf("ge: dam+: %s: seam %d: doorway %d wide in room %d's edge (%.0f long), at %d %d %d\n", f->w->name, si, (s32) s->width,
               (s32) s->room, len / sLs, f->sp[si][0][0], f->sp[si][0][1], f->sp[si][0][2]);
    }
    return 1;
}

/* every open stock edge of `room` within the radius: their points, ordered along the hint */
static s32 dpSeamEdges(DpFrame *f, s32 si, const DpSeam *s, StandTile *first)
{
    StandTile *t;
    s32 k, n = 0, np = 0, i;
    f32 sx = s->x * sLs, sz = s->z * sLs, sy = s->y * sLs, r = s->radius * sLs;
    s16 pts[8][3];

    for (t = first; *(u32 *) t != 0; t = dpNextTile(t))
    {
        s32 c = t->tail.hdrTail.pointCount & 0xf;

        if (t->room != s->room)
        {
            continue;
        }
        for (k = 0; k < c; k++)
        {
            StandTilePoint *a = &t->points[k], *b = &t->points[(k + 1) % c];
            StandTilePoint *e2[2];
            s32 m;

            if (a->link >= 16 || hypotf(a->x - sx, a->z - sz) > r || hypotf(b->x - sx, b->z - sz) > r || fabsf(a->y - sy) > 60.0f)
            {
                continue;
            }
            if (n >= 6)
            {
                break;
            }
            f->lnkTile[si][n] = t;
            f->lnkEdge[si][n] = k;
            n++;
            e2[0] = a;
            e2[1] = b;
            for (m = 0; m < 2; m++)
            {
                s32 dup = 0;
                for (i = 0; i < np; i++)
                {
                    if (pts[i][0] == e2[m]->x && pts[i][2] == e2[m]->z) dup = 1;
                }
                if (!dup && np < 8)
                {
                    pts[np][0] = e2[m]->x;
                    pts[np][1] = e2[m]->y;
                    pts[np][2] = e2[m]->z;
                    np++;
                }
            }
        }
    }
    if (n == 0)
    {
        printf("ge: dam+: %s: no open stan edges in room %d near %d,%d - wing off\n", f->w->name, s->room, s->x, s->z);
        return 0;
    }
    /* order along the hint */
    {
        s32 a, b;
        for (a = 0; a < np; a++)
        {
            for (b = a + 1; b < np; b++)
            {
                f32 pa = pts[a][0] * s->hx + pts[a][2] * s->hz, pb = pts[b][0] * s->hx + pts[b][2] * s->hz;
                if (pb < pa)
                {
                    s16 tmp[3];
                    memcpy(tmp, pts[a], sizeof(tmp));
                    memcpy(pts[a], pts[b], sizeof(tmp));
                    memcpy(pts[b], tmp, sizeof(tmp));
                }
            }
        }
    }
    memcpy(f->sp[si], pts, sizeof(pts));
    f->nsp[si] = np;
    f->nlnk[si] = n;
    f->seamRoom[si] = s->room;
    if (si == 0)
    {
        f32 ux = pts[np - 1][0] - pts[0][0], uz = pts[np - 1][2] - pts[0][2], l = sqrtf(ux * ux + uz * uz);
        dpSetFrame(f, pts[0], ux / l, uz / l, f->lnkTile[0][0]);
    }
    printf("ge: dam+: %s: seam %d: %d open edges, %d points in room %d\n", f->w->name, si, n, np, s->room);
    for (i = 0; i < np; i++)
    {
        printf("ge: dam+: %s:   point %d: %d %d %d\n", f->w->name, i, pts[i][0], pts[i][1], pts[i][2]);
    }
    return 1;
}

/* ---- our stan --------------------------------------------------------------------------------------------------------------- */
static s32 dpSameXYZ(const s16 *a, const s16 *b)
{
    return a[0] == b[0] && a[2] == b[2] && abs(a[1] - b[1]) <= 3;
}

static s32 dpSameXZ(const s16 *a, const s16 *b)
{
    return a[0] == b[0] && a[2] == b[2];
}

static void dpBuildWingTris(DpFrame *f)
{
    const DpWing *w = f->w;
    s32 pi;

    f->tile0 = sNTiles;
    for (pi = 0; pi < w->npieces; pi++)
    {
        const DpPiece *p = &w->pieces[pi];
        s32 k;
        f32 cu = 0.0f, cv = 0.0f, ch = 0.0f, cf[3];
        s16 cpt[3];

        if (p->flags & PF_NOSTAN)
        {
            continue;
        }
        /* fan from the polygon's centroid (our own point, inside a convex piece): every polygon edge is then the edge of a
         * triangle with area, even with several vertices along one side (doorways, openings) */
        for (k = 0; k < p->n; k++)
        {
            f32 u, v, h;
            dpVert(f, p, k, &u, &v, &h);
            cu += u / p->n;
            cv += v / p->n;
            ch += h / p->n;
        }
        dpFile(f, cu, cv, ch, cf);
        cpt[0] = (s16) lroundf(cf[0]);
        cpt[1] = (s16) lroundf(cf[1]);
        cpt[2] = (s16) lroundf(cf[2]);
        for (k = 0; k < p->n; k++)
        {
            s32 a = k, b = (k + 1) % p->n;
            DpTri *o;
            f32 cr, au, av, ah, bu, bv, bh;

            if (sNTiles >= DP_MAXTILES)
            {
                return;
            }
            o = &sTiles[sNTiles];
            memcpy(o->p[0], cpt, sizeof(cpt));
            o->uvh[0][0] = cu;
            o->uvh[0][1] = cv;
            o->uvh[0][2] = ch;
            dpStanPoint(f, p, a, o->p[1]);
            dpVert(f, p, a, &au, &av, &ah);
            o->uvh[1][0] = au;
            o->uvh[1][1] = av;
            o->uvh[1][2] = ah;
            dpStanPoint(f, p, b, o->p[2]);
            dpVert(f, p, b, &bu, &bv, &bh);
            o->uvh[2][0] = bu;
            o->uvh[2][1] = bv;
            o->uvh[2][2] = bh;
            o->room = p->room;
            o->wing = (s8) (f - sF);
            o->outdoor = (p->flags & PF_OUTDOOR) != 0;
            /* stock tiles wind clockwise seen from above (negative cross in x,z): match it */
            cr = (f32) (o->p[1][0] - o->p[0][0]) * (o->p[2][2] - o->p[0][2]) - (f32) (o->p[1][2] - o->p[0][2]) * (o->p[2][0] - o->p[0][0]);
            if (cr == 0.0f)
            {
                continue;   /* degenerate: never emit a zero-area tile */
            }
            if (cr > 0.0f)
            {
                s16 t3[3];
                f32 tuv[3];

                memcpy(t3, o->p[1], sizeof(t3));
                memcpy(o->p[1], o->p[2], sizeof(t3));
                memcpy(o->p[2], t3, sizeof(t3));
                memcpy(tuv, o->uvh[1], sizeof(tuv));
                memcpy(o->uvh[1], o->uvh[2], sizeof(tuv));
                memcpy(o->uvh[2], tuv, sizeof(tuv));
            }
            sNTiles++;
        }
    }
    f->ntiles = sNTiles - f->tile0;
}

/* the tile under a design point of a wing (the one whose floor is nearest h where floors overlap), or NULL */
StandTile *portDamPlusWingTileAt(u32 part, f32 u, f32 v, f32 h)
{
    DpFrame *f = dpFrameOf(part);
    StandTile *best = NULL;
    f32 bestd = 1e30f;
    s32 i;

    if (f == NULL || !sStanOk)
    {
        return NULL;
    }
    for (i = f->tile0; i < f->tile0 + f->ntiles; i++)
    {
        f32 (*q)[3] = sTiles[i].uvh;
        f32 d1 = (u - q[1][0]) * (q[0][1] - q[1][1]) - (q[0][0] - q[1][0]) * (v - q[1][1]);
        f32 d2 = (u - q[2][0]) * (q[1][1] - q[2][1]) - (q[1][0] - q[2][0]) * (v - q[2][1]);
        f32 d3 = (u - q[0][0]) * (q[2][1] - q[0][1]) - (q[2][0] - q[0][0]) * (v - q[0][1]);
        s32 neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
        s32 pos = (d1 > 0) || (d2 > 0) || (d3 > 0);

        if (!(neg && pos))
        {
            f32 th = (q[0][2] + q[1][2] + q[2][2]) / 3.0f, d = fabsf(th - h);
            if (d < bestd)
            {
                bestd = d;
                best = sTilePtr[i];
            }
        }
    }
    return best;
}

StandTile *portDamPlusTileAt(f32 u, f32 v)
{
    return portDamPlusWingTileAt(DP_MOTORPOOL, u, v, 0.0f);
}

/* The new stan block: the stock tiles (seam points inserted, links remapped), our tiles for stock rooms right after that
 * room's run, then our rooms' tiles; then every link of ours, and the seams both ways. */
typedef struct { StandTile *tile; s32 edge; s16 p1[3], p2[3]; s32 wing, seam; } DpIns;

static s32 dpTileBytes(s32 npts)
{
    return list_of_tilesizes[npts];
}

static s32 dpBuildStan(struct DpPrefix *prefix)
{
    StandTile *t, *first;
    s32 nstock = 0, i, j, k, nslots = 0, maxnum = 0, nins = 0;
    u32 stockbytes, total;
    u32 *oldOff, *newOff;
    u8 *blk, *w;
    u32 *hdr;
    u32 *oldslots = (u32 *) prefix + 2;
    DpIns ins[8];
    s32 f;

    if (sNTiles <= 0)
    {
        return 0;
    }
    for (t = prefix->first; *(u32 *) t != 0; t = dpNextTile(t))
    {
        s32 num = (t->id >> 8) & 0x7fff;
        if (num > maxnum) maxnum = num;
        nstock++;
    }
    stockbytes = (u32) ((u8 *) t - (u8 *) prefix->first);
    while (oldslots[nslots] != 0)
    {
        nslots++;
    }
    if (maxnum + 100 + sNTiles > 0x7fff)
    {
        return 0;
    }
    for (f = 0; f < sNF; f++)
    {
        s32 s;
        for (s = 0; s < sF[f].nseam; s++)
        {
            if (sF[f].ok && sF[f].insTile[s] != NULL && nins < 8)
            {
                DpIns *in = &ins[nins++];
                in->tile = sF[f].insTile[s];
                in->edge = sF[f].insEdge[s];
                /* in the tile's own A->B order */
                memcpy(in->p1, sF[f].sp[s][sF[f].insFlip[s] ? 1 : 0], sizeof(in->p1));
                memcpy(in->p2, sF[f].sp[s][sF[f].insFlip[s] ? 0 : 1], sizeof(in->p2));
                in->wing = f;
                in->seam = s;
            }
        }
    }
    oldOff = calloc(nstock + 1, sizeof(u32));
    newOff = calloc(nstock + 1, sizeof(u32));
    total = 8 + (nslots + 1) * 4 + stockbytes + nins * 16 + sNTiles * 0x20 + 16;
    blk = mempAllocBytesInBank((total + 15) & ~15, MEMPOOL_STAGE);
    memset(blk, 0, (total + 15) & ~15);
    hdr = (u32 *) blk;
    first = (StandTile *) (blk + 8 + (nslots + 1) * 4);
    w = (u8 *) first;

    /* tiles: stock (with insertions), ours per stock room after its run, then ours per new room */
    i = 0;
    for (t = prefix->first; *(u32 *) t != 0; i++)
    {
        StandTile *next = dpNextTile(t);
        s32 c = t->tail.hdrTail.pointCount & 0xf, insk = -1;
        StandTile *o = (StandTile *) w;

        oldOff[i] = (u32) ((u8 *) t - (u8 *) prefix->first);
        newOff[i] = (u32) (w - (u8 *) first);
        for (k = 0; k < nins; k++)
        {
            if (ins[k].tile == t)
            {
                insk = k;
            }
        }
        if (insk < 0)
        {
            memcpy(o, t, dpTileBytes(c));
            w += dpTileBytes(c);
        }
        else
        {
            s32 e = ins[insk].edge, m = 0;

            memcpy(o, t, 8);   /* header: id, room, colour, count (fixed below) */
            o->tail.hdrTail.pointCount = (o->tail.hdrTail.pointCount & ~0xf) | (c + 2);
            for (k = 0; k < c; k++)
            {
                o->points[m++] = t->points[k];
                if (k == e)
                {
                    StandTilePoint *pc = &o->points[m++], *pd = &o->points[m++];

                    pc->x = ins[insk].p1[0];
                    pc->y = ins[insk].p1[1];
                    pc->z = ins[insk].p1[2];
                    pc->link = 0;                       /* the doorway: linked to ours below */
                    pd->x = ins[insk].p2[0];
                    pd->y = ins[insk].p2[1];
                    pd->z = ins[insk].p2[2];
                    pd->link = t->points[k].link;       /* D -> B: the rest of the stock wall, as it was */
                }
            }
            ins[insk].tile = o;     /* from here on: the copy, edge e + 1 is the doorway */
            ins[insk].edge = e + 1;
            w += dpTileBytes(c + 2);
        }
        /* our tiles for this stock room, after its last tile */
        if (*(u32 *) next == 0 || next->room != t->room)
        {
            for (j = 0; j < sNTiles; j++)
            {
                if (sTiles[j].room == t->room)
                {
                    sTilePtr[j] = (StandTile *) w;
                    w += 0x20;
                }
            }
        }
        t = next;
    }
    oldOff[nstock] = stockbytes;
    newOff[nstock] = (u32) (w - (u8 *) first);
    {
        s32 r;
        for (r = DP_ROOM_BAY; r <= DP_ROOM_BAY + DP_MAXROOMS; r++)
        {
            for (j = 0; j < sNTiles; j++)
            {
                if (sTiles[j].room == r && sTilePtr[j] == NULL)
                {
                    sTilePtr[j] = (StandTile *) w;
                    w += 0x20;
                }
            }
        }
    }
    *(u32 *) w = 0;
    if ((u32) (w + 4 - blk) > total)
    {
        printf("ge: dam+: stan block overflow - Dam+ geometry off\n");
        free(oldOff);
        free(newOff);
        return 0;
    }
#define DP_OLD2NEW(off, out) do { s32 lo_ = 0, hi_ = nstock; (out) = -1; \
        while (lo_ <= hi_) { s32 mid_ = (lo_ + hi_) / 2; if (oldOff[mid_] == (off)) { (out) = newOff[mid_]; break; } \
        if (oldOff[mid_] < (off)) lo_ = mid_ + 1; else hi_ = mid_ - 1; } } while (0)
#define DP_LINK(tile) ((u16) ((0x80 + ((u8 *) (tile) - (u8 *) first)) >> 3))
    /* our tiles (written first: the walk below steps over them) */
    for (i = 0; i < sNTiles; i++)
    {
        StandTile *o = sTilePtr[i];
        DpFrame *fr = &sF[sTiles[i].wing];
        StandTile *ref = fr->insTile[0] != NULL ? fr->insTile[0] : (fr->nlnk[0] > 0 ? fr->lnkTile[0][0] : NULL);

        memset(o, 0, 0x20);
        o->id = (u32) (maxnum + 100 + i) << 8;     /* 'p' tiles numbered above every stock tile */
        o->room = sTiles[i].room;
        /* the chr shade colour: the seam's stock tile outdoors (and the motor pool's bay, as before), else a cool grey */
        if (ref != NULL && (sTiles[i].outdoor || (fr->w->part == DP_MOTORPOOL && sTiles[i].room == fr->w->room0)))
        {
            o->mid.half = ref->mid.half;
        }
        else
        {
            o->mid.headerMid.r = 6;
            o->mid.headerMid.g = 7;
            o->mid.headerMid.b = 7;
        }
        o->mid.headerMid.special = 0;
        o->tail.hdrTail.pointCount = 3;
        o->tail.hdrTail.headerC = 0;
        o->tail.hdrTail.headerD = 1;
        o->tail.hdrTail.headerE = 2;
        for (j = 0; j < 3; j++)
        {
            o->points[j].x = sTiles[i].p[j][0];
            o->points[j].y = sTiles[i].p[j][1];
            o->points[j].z = sTiles[i].p[j][2];
            o->points[j].link = 0;
        }
    }
    /* remap every stock link (our copies sit at new offsets) */
    for (t = first; *(u32 *) t != 0; t = dpNextTile(t))
    {
        s32 c = t->tail.hdrTail.pointCount & 0xf, isours = 0;

        for (j = 0; j < sNTiles; j++)
        {
            if (sTilePtr[j] == t) isours = 1;
        }
        if (isours)
        {
            continue;
        }
        for (k = 0; k < c; k++)
        {
            u16 L = t->points[k].link;

            if (L >= 16)
            {
                s32 nw;
                u32 off = ((u32) L << 3) - 0x80;

                DP_OLD2NEW(off, nw);
                if (nw < 0)
                {
                    printf("ge: dam+: stan link %u to offset %u matches no stock tile\n", L, off);
                    t->points[k].link = 0;
                }
                else
                {
                    t->points[k].link = (u16) ((0x80 + nw) >> 3);
                }
            }
        }
    }
    for (i = 0; i < sNTiles; i++)
    {
        StandTile *o = sTilePtr[i];
        DpFrame *fr = &sF[sTiles[i].wing];

        for (k = 0; k < 3; k++)
        {
            s16 *a = sTiles[i].p[k], *b = sTiles[i].p[(k + 1) % 3];
            s32 m, s;

            for (j = 0; j < sNTiles && o->points[k].link == 0; j++)
            {
                if (j == i) continue;
                for (m = 0; m < 3; m++)
                {
                    if (dpSameXYZ(sTiles[j].p[m], b) && dpSameXYZ(sTiles[j].p[(m + 1) % 3], a))
                    {
                        o->points[k].link = DP_LINK(sTilePtr[j]);
                        break;
                    }
                }
            }
            /* the seams of this tile's wing */
            for (s = 0; s < fr->nseam && o->points[k].link == 0; s++)
            {
                for (m = 0; m < nins && o->points[k].link == 0; m++)
                {
                    if (ins[m].wing == sTiles[i].wing && ins[m].seam == s)
                    {
                        if ((dpSameXZ(ins[m].p1, a) && dpSameXZ(ins[m].p2, b)) || (dpSameXZ(ins[m].p1, b) && dpSameXZ(ins[m].p2, a)))
                        {
                            o->points[k].link = DP_LINK(ins[m].tile);
                            ins[m].tile->points[ins[m].edge].link = DP_LINK(o);
                        }
                    }
                }
                for (m = 0; m < fr->nlnk[s] && o->points[k].link == 0; m++)
                {
                    s32 off;
                    StandTile *st;
                    s32 c, e;
                    s16 sa[3], sb[3];

                    DP_OLD2NEW((u32) ((u8 *) fr->lnkTile[s][m] - (u8 *) prefix->first), off);
                    if (off < 0) continue;
                    st = (StandTile *) ((u8 *) first + off);
                    c = st->tail.hdrTail.pointCount & 0xf;
                    e = fr->lnkEdge[s][m];
                    sa[0] = st->points[e].x; sa[1] = st->points[e].y; sa[2] = st->points[e].z;
                    sb[0] = st->points[(e + 1) % c].x; sb[1] = st->points[(e + 1) % c].y; sb[2] = st->points[(e + 1) % c].z;
                    if ((dpSameXZ(sa, a) && dpSameXZ(sb, b)) || (dpSameXZ(sa, b) && dpSameXZ(sb, a)))
                    {
                        o->points[k].link = DP_LINK(st);    /* ours -> stock */
                        st->points[e].link = DP_LINK(o);    /* stock -> ours (in our copy only) */
                    }
                }
            }
        }
    }
    /* header: the room slots point at tiles of the new block */
    hdr[0] = 0;
    hdr[1] = (u32) first;
    for (i = 0; i < nslots; i++)
    {
        s32 nw;
        DP_OLD2NEW(oldslots[i] - (u32) prefix->first, nw);
        hdr[2 + i] = (u32) first + (nw >= 0 ? (u32) nw : oldslots[i] - (u32) prefix->first);
    }
    hdr[2 + nslots] = 0;
#undef DP_LINK
#undef DP_OLD2NEW
    free(oldOff);
    free(newOff);
    stanDetermineEOF(blk, 0, blk);
    stanLoadFile(blk);
    printf("ge: dam+: stan: %d tiles appended, %d doorways cut into stock edges, %u bytes of stock tiles, numbered from p%d\n", sNTiles,
           nins, stockbytes, maxnum + 100);
    for (i = 0; i < nins; i++)
    {
        printf("ge: dam+: stan: doorway %d (%s seam %d) %s\n", i, sF[ins[i].wing].w->name, ins[i].seam,
               ins[i].tile->points[ins[i].edge].link >= 16 ? "linked both ways" : "NOT LINKED");
    }
    return 1;
}

/* ---- the motor pool's working copy (it can grow test rooms) ----------------------------------------------------------------- */
static void dpMotorWork(const DpWing *base, s32 ntest)
{
    s32 i, n = base->npieces;

    sMotorWing = *base;
    memcpy(sMotorWork, base->pieces, n * sizeof(DpPiece));
    sTestRooms = 0;
    if (ntest > 0)
    {
        static const DpPiece open = { DP_ROOM_BARRACKS, 8, 4, { -1500, -1000, -800, -300, -300, -800, -1000, -1500 },
            { 2040, 2040, 2040, 2040, 2900, 2900, 2900, 2900 }, { E_WALL, E_OPEN, E_WALL, E_WALL, E_WALL, E_OPEN, E_WALL, E_WALL }, { 0 },
            330, M_UGFLOOR, M_UGWALL, M_UGCEIL, 0, { 0 } };

        if (ntest > DP_MAXROOMS - 2)
        {
            ntest = DP_MAXROOMS - 2;
        }
        sMotorWork[n - 1] = open;
        for (i = 0; i < ntest && n < 16; i++)
        {
            DpPiece *p = &sMotorWork[n++];
            s16 v0 = 2900 + DP_TESTROOM_DEPTH * i, v1 = v0 + DP_TESTROOM_DEPTH;

            memset(p, 0, sizeof(*p));
            p->room = DP_ROOM_BARRACKS + 1 + i;
            p->n = 4;
            p->u[0] = -1000; p->v[0] = v0;
            p->u[1] = -800;  p->v[1] = v0;
            p->u[2] = -800;  p->v[2] = v1;
            p->u[3] = -1000; p->v[3] = v1;
            p->edge[0] = E_OPEN;
            p->edge[1] = E_WALL;
            p->edge[2] = (i < ntest - 1) ? E_OPEN : E_WALL;
            p->edge[3] = E_WALL;
            p->height = 230;
            p->floor = M_UGFLOOR;
            p->wall = M_UGWALL;
            p->ceil = M_UGCEIL;
        }
        sTestRooms = ntest;
        sMotorWing.nrooms = 2 + ntest;
    }
    sMotorWing.pieces = sMotorWork;
    sMotorWing.npieces = n;
    {
        static DpPortalSpec ports[16];
        s32 np = base->nportals;

        memcpy(ports, base->portals, np * sizeof(DpPortalSpec));
        for (i = 0; i < sTestRooms && np < 16; i++)
        {
            DpPortalSpec *ps = &ports[np++];
            ps->r1 = DP_ROOM_BARRACKS + i;
            ps->r2 = DP_ROOM_BARRACKS + 1 + i;
            ps->u0 = -1000;
            ps->u1 = -800;
            ps->v0 = ps->v1 = 2900 + DP_TESTROOM_DEPTH * i;
            ps->h0 = 0;
            ps->h1 = 230;
        }
        sMotorWing.portals = ports;
        sMotorWing.nportals = np;
    }
}

/* load_bg_file, right after the stock stan is loaded. levelscale: this stage's. */
void portDamPlusStan(f32 levelscale)
{
    extern struct DpPrefix *stan_prefix;
    struct DpPrefix *prefix = stan_prefix;
    s32 parts = portDamPlusParts(), i;
    const char *e = getenv("GE_DAMPLUS_TESTROOMS");
    s32 ntest = e != NULL ? atoi(e) : 0;

    memset(sF, 0, sizeof(sF));
    sNF = 0;
    sNTiles = 0;
    memset(sTilePtr, 0, sizeof(sTilePtr));
    sStanOk = 0;
    sMaxRoom = 0;
    sNCuts = 0;
    sNPatch = 0;
    sLs = levelscale;
    if (prefix == NULL || prefix->first == NULL)
    {
        return;
    }
    for (i = 0; i < g_DpNumWings && sNF < DP_MAXWINGS; i++)
    {
        const DpWing *w = g_DpWings[i];
        DpFrame *f;
        s32 s, ok = 1;

        if (!(parts & w->part))
        {
            continue;
        }
        if (ntest > 0 && w->part != DP_MOTORPOOL)
        {
            printf("ge: dam+: %s off while GE_DAMPLUS_TESTROOMS uses its room ids\n", w->name);
            continue;
        }
        f = &sF[sNF];
        if (w->part == DP_MOTORPOOL)
        {
            dpMotorWork(w, ntest);
            w = &sMotorWing;
        }
        f->w = w;
        f->nseam = w->nseams;
        for (s = 0; s < w->nseams && ok; s++)
        {
            switch (w->seams[s].type)
            {
            case SEAM_SHUTTER: ok = dpSeamShutter(f, prefix->first); break;
            case SEAM_INSERT: ok = dpSeamInsert(f, s, &w->seams[s], prefix->first); break;
            case SEAM_EDGES: ok = dpSeamEdges(f, s, &w->seams[s], prefix->first); break;
            default: ok = 0; break;
            }
        }
        if (!ok)
        {
            memset(f, 0, sizeof(*f));
            continue;
        }
        f->ok = 1;
        sNF++;
    }
    if (sNF == 0)
    {
        return;
    }
    for (i = 0; i < sNF; i++)
    {
        dpBuildWingTris(&sF[i]);
        if (sF[i].w->room0 + sF[i].w->nrooms - 1 > sMaxRoom)
        {
            sMaxRoom = sF[i].w->room0 + sF[i].w->nrooms - 1;
        }
    }
    if (!dpBuildStan(prefix))
    {
        sNF = 0;
        sMaxRoom = 0;
        return;
    }
    sStanOk = 1;
}

/* ---- stock room data (read from the bg file at run time, never stored) ------------------------------------------------------ */
typedef struct { f32 x, y, z; s16 s, t; u8 r, g, b; } DpSV;
typedef struct { s32 v[3]; u32 c0; u16 tex; u8 key; s32 ord; } DpST;   /* key 1 primary, 2 secondary; ord within its list */

static s32 dpParseDl(const u8 *dl, s32 dlen, s32 nvtx, u8 key, DpST **T, s32 *n, s32 *cap)
{
    s32 vbuf[32], i, ord = 0;
    u32 c0 = 0xC0080002;
    u16 tex = 0;

    for (i = 0; i < 32; i++) vbuf[i] = -1;
    for (i = 0; i + 8 <= dlen; i += 8)
    {
        const u8 *b = dl + i;
        u32 w0 = ((u32) b[0] << 24) | ((u32) b[1] << 16) | ((u32) b[2] << 8) | b[3];
        u32 w1 = ((u32) b[4] << 24) | ((u32) b[5] << 16) | ((u32) b[6] << 8) | b[7];
        u32 op = w0 >> 24;
        s32 tri[4][3], nt = 0, q;

        if (op == 0x04)
        {
            s32 cnt = ((w0 >> 20) & 0xF) + 1, v0 = (w0 >> 16) & 0xF, base = (w1 & 0xFFFFFF) / 16, k;
            for (k = 0; k < cnt && v0 + k < 32; k++)
            {
                vbuf[v0 + k] = (base + k < nvtx) ? base + k : -1;
            }
        }
        else if (op == 0xC0)
        {
            c0 = w0;
            tex = (u16) w1;
        }
        else if (op == 0xB1)
        {
            for (q = 0; q < 4; q++)
            {
                s32 aa = (w1 >> (8 * q)) & 0xF, bb = (w1 >> (8 * q + 4)) & 0xF, cc = (w0 >> (4 * q)) & 0xF;
                if (aa == 0 && bb == 0 && cc == 0) continue;
                tri[nt][0] = vbuf[aa]; tri[nt][1] = vbuf[bb]; tri[nt][2] = vbuf[cc];
                nt++;
            }
        }
        else if (op == 0xBF)
        {
            tri[0][0] = vbuf[((w1 >> 16) & 0xFF) / 10]; tri[0][1] = vbuf[((w1 >> 8) & 0xFF) / 10]; tri[0][2] = vbuf[(w1 & 0xFF) / 10];
            nt = 1;
        }
        else if (op == 0xB8)
        {
            break;
        }
        for (q = 0; q < nt; q++)
        {
            if (*n >= *cap) { *cap *= 2; *T = realloc(*T, *cap * sizeof(DpST)); }
            memcpy((*T)[*n].v, tri[q], sizeof(tri[q]));
            (*T)[*n].c0 = c0;
            (*T)[*n].tex = tex;
            (*T)[*n].key = key;
            (*T)[*n].ord = ord++;
            (*n)++;
        }
    }
    return ord;
}

/* a stock room's vertices (file units) and the triangles of both display lists, read from the bg file (scratch only) */
static s32 dpReadStockRoom(s32 room, DpSV **vout, s32 *nv, DpST **tout, s32 *nt, s32 *n1, s32 *n2)
{
    s32 csz = g_BgRoomInfo[room].csize_point_index_binary, dsz = g_BgRoomInfo[room].csize_primary_DL_binary;
    s32 ssz = g_BgRoomInfo[room].csize_secondary_DL_binary;
    u8 *cbuf, *vraw, *draw;
    s32 vlen, dlen, i, n = 0, cap = 4096, big;
    DpSV *V;
    DpST *T;
    f32 *pos = ptr_bgdata_room_fileposition_list[room].pos.f;

    if (csz <= 0 || dsz <= 0)
    {
        return 0;
    }
    big = csz > dsz ? csz : dsz;
    big = big > ssz ? big : ssz;
    cbuf = malloc((big + 0x40) & ~0xf);
    vraw = malloc(0x40000);
    draw = malloc(0x40000);
    obLoadBGFileBytesAtOffset((u8 *) portDamPlusBgName(), cbuf,
                              (s32) ((u32) (uintptr_t) ptr_bgdata_room_fileposition_list[room].pPointTableBin + 0xf1000000u), (csz + 0xf) & ~0xf);
    vlen = bgDecompress(cbuf, vraw);
    V = malloc((vlen / 16 + 1) * sizeof(DpSV));
    for (i = 0; i < vlen / 16; i++)
    {
        u8 *b = vraw + i * 16;
        V[i].x = (s16) ((b[0] << 8) | b[1]) + pos[0];
        V[i].y = (s16) ((b[2] << 8) | b[3]) + pos[1];
        V[i].z = (s16) ((b[4] << 8) | b[5]) + pos[2];
        V[i].s = (s16) ((b[8] << 8) | b[9]);
        V[i].t = (s16) ((b[10] << 8) | b[11]);
        V[i].r = b[12];
        V[i].g = b[13];
        V[i].b = b[14];
    }
    T = malloc(cap * sizeof(DpST));
    obLoadBGFileBytesAtOffset((u8 *) portDamPlusBgName(), cbuf,
                              (s32) ((u32) (uintptr_t) ptr_bgdata_room_fileposition_list[room].pPriMappingBin + 0xf1000000u), (dsz + 0xf) & ~0xf);
    dlen = bgDecompress(cbuf, draw);
    *n1 = dpParseDl(draw, dlen, vlen / 16, 1, &T, &n, &cap);
    *n2 = 0;
    if (ssz > 0 && ptr_bgdata_room_fileposition_list[room].pSecMappingBin != NULL)
    {
        obLoadBGFileBytesAtOffset((u8 *) portDamPlusBgName(), cbuf,
                                  (s32) ((u32) (uintptr_t) ptr_bgdata_room_fileposition_list[room].pSecMappingBin + 0xf1000000u), (ssz + 0xf) & ~0xf);
        dlen = bgDecompress(cbuf, draw);
        *n2 = dpParseDl(draw, dlen, vlen / 16, 2, &T, &n, &cap);
    }
    free(cbuf);
    free(vraw);
    free(draw);
    *vout = V;
    *nv = vlen / 16;
    *tout = T;
    *nt = n;
    return 1;
}

/* Sutherland-Hodgman: keep the part of poly where sign * (d . (u,v,h) - c) >= 0. Points carry 9 attributes (u v h s t r g b x). */
#define DP_NA 8
static s32 dpClip(f32 in[][DP_NA], s32 n, s32 axis, f32 c, s32 keepAbove, f32 out[][DP_NA])
{
    s32 i, m = 0;

    for (i = 0; i < n; i++)
    {
        f32 *a = in[i], *b = in[(i + 1) % n];
        f32 da = keepAbove ? a[axis] - c : c - a[axis], db = keepAbove ? b[axis] - c : c - b[axis];

        if (da >= 0.0f)
        {
            memcpy(out[m++], a, sizeof(f32) * DP_NA);
        }
        if ((da >= 0.0f) != (db >= 0.0f))
        {
            f32 t = da / (da - db);
            s32 k;
            for (k = 0; k < DP_NA; k++)
            {
                out[m][k] = a[k] + (b[k] - a[k]) * t;
            }
            m++;
        }
    }
    return m;
}

static f32 dpPolyArea(f32 p[][DP_NA], s32 n)
{
    /* area of a 3D polygon (in design units) */
    f32 sx = 0, sy = 0, sz = 0;
    s32 i;

    for (i = 0; i < n; i++)
    {
        f32 *a = p[i], *b = p[(i + 1) % n];
        sx += a[1] * b[2] - a[2] * b[1];
        sy += a[2] * b[0] - a[0] * b[2];
        sz += a[0] * b[1] - a[1] * b[0];
    }
    return 0.5f * sqrtf(sx * sx + sy * sy + sz * sz);
}

static void dpEmitPatchPoly(const DpFrame *f, f32 p[][DP_NA], s32 n, u32 c0, u16 tex, u8 room)
{
    s32 i, j;

    if (n < 3 || dpPolyArea(p, n) < 0.5f)
    {
        return;
    }
    for (i = 1; i + 1 < n; i++)
    {
        s32 idx[3] = { 0, i, i + 1 };
        DpPatch *q;

        if (sNPatch >= sMaxPatch)
        {
            sMaxPatch = sMaxPatch ? sMaxPatch * 2 : 512;
            sPatch = realloc(sPatch, sMaxPatch * sizeof(DpPatch));
        }
        q = &sPatch[sNPatch++];
        for (j = 0; j < 3; j++)
        {
            f32 fl[3], *a = p[idx[j]];
            dpFile(f, a[0], a[1], a[2], fl);
            q->x[j] = fl[0];
            q->y[j] = fl[1];
            q->z[j] = fl[2];
            q->s[j] = a[3];
            q->t[j] = a[4];
            q->r[j] = (u8) fminf(255.0f, fmaxf(0.0f, a[5] + 0.5f));
            q->g[j] = (u8) fminf(255.0f, fmaxf(0.0f, a[6] + 0.5f));
            q->b[j] = (u8) fminf(255.0f, fmaxf(0.0f, a[7] + 0.5f));
        }
        q->c0 = c0;
        q->tex = tex;
        q->room = room;
    }
}

static DpRoomCut *dpCutFor(s32 room, s32 n1, s32 n2)
{
    s32 i, k;
    for (i = 0; i < sNCuts; i++)
    {
        if (sCuts[i].room == room) return &sCuts[i];
    }
    if (sNCuts >= 8) return NULL;
    sCuts[sNCuts].room = room;
    sCuts[sNCuts].ntris[0] = n1;
    sCuts[sNCuts].ntris[1] = n2;
    for (k = 0; k < 2; k++)
    {
        s32 n = k ? n2 : n1;
        sCuts[sNCuts].drop[k] = mempAllocBytesInBank((n + 16) & ~15, MEMPOOL_STAGE);
        memset(sCuts[sNCuts].drop[k], 0, (n + 16) & ~15);
    }
    sCuts[sNCuts].ndrop = 0;
    return &sCuts[sNCuts++];
}

static void dpDoCuts(DpFrame *f)
{
    s32 c;

    for (c = 0; c < f->w->ncuts; c++)
    {
        const DpCut *cut = &f->w->cuts[c];
        DpSV *V;
        DpST *T;
        s32 nv, nt, n1, n2, i, removed = 0, pieces0 = sNPatch;
        DpRoomCut *rc;
        f32 box[3][2] = { { cut->u0, cut->u1 }, { cut->v0, cut->v1 }, { cut->h0, cut->h1 } };
        DpFrame cf = *f;     /* the frame the box is in: the wing's, or a seam's own */

        if (cut->frameseam > 0 && cut->frameseam < f->nseam && f->nsp[(s32) cut->frameseam] >= 2)
        {
            s32 fs = cut->frameseam, last = f->nsp[fs] - 1;
            f32 ux = f->sp[fs][last][0] - f->sp[fs][0][0], uz = f->sp[fs][last][2] - f->sp[fs][0][2], l = sqrtf(ux * ux + uz * uz);
            StandTile *ref = f->insTile[fs] != NULL ? f->insTile[fs] : f->lnkTile[fs][0];

            dpSetFrame(&cf, f->sp[fs][0], ux / l, uz / l, ref);
        }
        if (!dpReadStockRoom(cut->room, &V, &nv, &T, &nt, &n1, &n2))
        {
            printf("ge: dam+: %s: cut %d: room %d has no data\n", f->w->name, c, cut->room);
            continue;
        }
        rc = dpCutFor(cut->room, n1, n2);
        for (i = 0; i < nt && rc != NULL; i++)
        {
            f32 P[3][DP_NA], A[12][DP_NA], B[12][DP_NA];
            s32 j, n, ax;
            f32 e1[3], e2[3], nn[3], ln;
            s32 inside = 1;

            if (T[i].v[0] < 0 || T[i].v[1] < 0 || T[i].v[2] < 0)
            {
                continue;
            }
            for (j = 0; j < 3; j++)
            {
                DpSV *s = &V[T[i].v[j]];
                f32 d[3];
                dpDesign(&cf, s->x, s->y, s->z, d);
                P[j][0] = d[0]; P[j][1] = d[1]; P[j][2] = d[2];
                P[j][3] = s->s; P[j][4] = s->t;
                P[j][5] = s->r; P[j][6] = s->g; P[j][7] = s->b;
            }
            for (j = 0; j < 3; j++)
            {
                e1[j] = P[1][j] - P[0][j];
                e2[j] = P[2][j] - P[0][j];
            }
            nn[0] = e1[1] * e2[2] - e1[2] * e2[1];
            nn[1] = e1[2] * e2[0] - e1[0] * e2[2];
            nn[2] = e1[0] * e2[1] - e1[1] * e2[0];
            ln = sqrtf(nn[0] * nn[0] + nn[1] * nn[1] + nn[2] * nn[2]);
            if (ln < 1e-3f || fabsf(nn[2]) / ln > cut->maxny100 / 100.0f)
            {
                continue;   /* too flat for this cut (a floor) */
            }
            /* does it reach into the box at all? clip it to the box */
            memcpy(A, P, sizeof(P));
            n = 3;
            for (ax = 0; ax < 3 && n >= 3; ax++)
            {
                n = dpClip(A, n, ax, box[ax][0], 1, B);
                n = dpClip(B, n, ax, box[ax][1], 0, A);
            }
            if (n < 3 || dpPolyArea(A, n) < 1.0f)
            {
                continue;
            }
            /* yes: it is dropped, and what lies outside the box is drawn again (pieces: outside plane 1, inside 1 outside 2, ...) */
            rc->drop[T[i].key - 1][T[i].ord] = 1;
            rc->ndrop++;
            removed++;
            if (T[i].key != 1)
            {
                continue;   /* a translucent (secondary-list) triangle: dropped, not redrawn */
            }
            for (j = 0; j < 3; j++)
            {
                f->outdoor[0] += P[j][5];
                f->outdoor[1] += P[j][6];
                f->outdoor[2] += P[j][7];
                f->outdoorN++;
            }
            if (cut->fitseam >= 0 && cut->fitseam < 4 && fabsf(nn[2]) / ln < 0.3f)
            {
                for (j = 0; j < 3; j++)
                {
                    f->wallV[(s32) cut->fitseam] += P[j][1];
                    f->wallN[(s32) cut->fitseam]++;
                }
            }
            memcpy(A, P, sizeof(P));
            n = 3;
            for (ax = 0; ax < 3 && n >= 3; ax++)
            {
                s32 side;
                for (side = 0; side < 2 && n >= 3; side++)
                {
                    f32 Cpart[12][DP_NA];
                    s32 m;

                    /* the part beyond this face: kept */
                    m = dpClip(A, n, ax, box[ax][side], side, Cpart);
                    dpEmitPatchPoly(&cf, Cpart, m, T[i].c0, T[i].tex, cut->patchroom);
                    /* the rest continues to the next face */
                    m = dpClip(A, n, ax, box[ax][side], !side, B);
                    memcpy(A, B, sizeof(f32) * DP_NA * m);
                    n = m;
                }
            }
            (void) inside;
        }
        printf("ge: dam+: %s: cut %d: room %d: %d of %d+%d triangles cut, %d patch triangles\n", f->w->name, c, cut->room, removed, n1,
               n2, sNPatch - pieces0);
        free(V);
        free(T);
    }
    {
        s32 s;
        for (s = 0; s < 4; s++)
        {
            if (f->wallN[s] > 0)
            {
                f->wallV[s] /= f->wallN[s];
                f->wallN[s] = 1;
                printf("ge: dam+: %s: stock wall behind seam %d at v %.1f\n", f->w->name, s, f->wallV[s]);
            }
        }
        if (f->outdoorN > 0)
        {
            f->outdoor[0] /= f->outdoorN;
            f->outdoor[1] /= f->outdoorN;
            f->outdoor[2] /= f->outdoorN;
        }
    }
}

/* ---- our bg rooms ------------------------------------------------------------------------------------------------------------ */
typedef struct { f32 x, y, z; f32 s, t; u8 r, g, b; } DpV;
typedef struct { DpV v[3]; u32 c0; u16 tex; } DpGTri;

static DpGTri *sGT;
static s32 sNGT, sMaxGT;
static const DpFrame *sCurF;
static u8 sCurRoom;

static void dpLight(f32 u, f32 v, f32 h, s32 mat, s32 outdoor, u8 *r, u8 *g, u8 *b)
{
    const DpWing *w = sCurF->w;
    f32 acc[3];
    s32 i;

    if (dpFullBright(mat))
    {
        *r = *g = *b = 255;   /* light fixtures stay full bright */
        return;
    }
    if (outdoor)
    {
        f32 o[3] = { 150.0f, 160.0f, 185.0f };   /* dusk blue, unless the stock surfaces at the cut say otherwise */
        if (sCurF->outdoorN > 0)
        {
            o[0] = sCurF->outdoor[0];
            o[1] = sCurF->outdoor[1];
            o[2] = sCurF->outdoor[2];
        }
        *r = (u8) fminf(255.0f, o[0] * sMats[mat].r / 160.0f);
        *g = (u8) fminf(255.0f, o[1] * sMats[mat].g / 160.0f);
        *b = (u8) fminf(255.0f, o[2] * sMats[mat].b / 160.0f);
        return;
    }
    acc[0] = w->ambient[0];
    acc[1] = w->ambient[1];
    acc[2] = w->ambient[2];
    for (i = 0; i < w->nlamps; i++)
    {
        const DpLamp *L = &w->lamps[i];
        f32 du = u - L->u, dv = v - L->v, dh = h - L->h;
        f32 d2 = du * du + dv * dv + dh * dh;
        f32 k = 1.0f / (1.0f + d2 / ((f32) L->r * L->r));

        if (L->room != 0 && L->room != sCurRoom)
        {
            k *= 0.15f;   /* a room's lamps barely reach the next room */
        }
        acc[0] += k * L->cr / 255.0f;
        acc[1] += k * L->cg / 255.0f;
        acc[2] += k * L->cb / 255.0f;
    }
    *r = (u8) fminf(255.0f, sMats[mat].r * acc[0] * 1.25f);
    *g = (u8) fminf(255.0f, sMats[mat].g * acc[1] * 1.25f);
    *b = (u8) fminf(255.0f, sMats[mat].b * acc[2] * 1.25f);
}

/* one textured triangle in design coords; (s, t) given in world units along the surface */
static void dpTri(s32 mat, s32 outdoor, const f32 dsg[3][3], const f32 st[3][2])
{
    DpGTri *g;
    s32 j;

    if (sNGT >= sMaxGT)
    {
        sMaxGT = sMaxGT ? sMaxGT * 2 : 2048;
        sGT = realloc(sGT, sMaxGT * sizeof(DpGTri));
    }
    g = &sGT[sNGT++];
    g->c0 = sMats[mat].c0;
    g->tex = sMats[mat].tex;
    for (j = 0; j < 3; j++)
    {
        f32 f[3];

        dpFile(sCurF, dsg[j][0], dsg[j][1], dsg[j][2], f);
        g->v[j].x = f[0];
        g->v[j].y = f[1];
        g->v[j].z = f[2];
        g->v[j].s = st[j][0] * sLs * sMats[mat].st;
        g->v[j].t = st[j][1] * sLs * sMats[mat].st;
        dpLight(dsg[j][0], dsg[j][1], dsg[j][2], mat, outdoor, &g->v[j].r, &g->v[j].g, &g->v[j].b);
    }
    /* keep st small: shift by whole 4096 steps (every texture up to 128 texels tiles seamlessly across it) */
    {
        f32 ms = fminf(g->v[0].s, fminf(g->v[1].s, g->v[2].s));
        f32 mt = fminf(g->v[0].t, fminf(g->v[1].t, g->v[2].t));
        f32 ds = floorf(ms / 4096.0f) * 4096.0f, dt = floorf(mt / 4096.0f) * 4096.0f;

        for (j = 0; j < 3; j++)
        {
            g->v[j].s -= ds;
            g->v[j].t -= dt;
        }
    }
}

/* a planar quad a b c d (design coords), split into a grid of at most `cell` world units, st from the two edge directions */
static void dpQuad(s32 mat, s32 outdoor, const f32 a[3], const f32 b[3], const f32 d[3], f32 cell)
{
    f32 e1[3], e2[3], l1, l2;
    s32 n1, n2, i, j, k;

    for (k = 0; k < 3; k++)
    {
        e1[k] = b[k] - a[k];
        e2[k] = d[k] - a[k];
    }
    l1 = sqrtf(e1[0] * e1[0] + e1[1] * e1[1] + e1[2] * e1[2]);
    l2 = sqrtf(e2[0] * e2[0] + e2[1] * e2[1] + e2[2] * e2[2]);
    if (l1 < 0.5f || l2 < 0.5f)
    {
        return;
    }
    n1 = (s32) ceilf(l1 / cell);
    n2 = (s32) ceilf(l2 / cell);
    if (n1 < 1) n1 = 1;
    if (n2 < 1) n2 = 1;
    for (i = 0; i < n1; i++)
    {
        for (j = 0; j < n2; j++)
        {
            f32 P[4][3], T[4][2];
            s32 c;
            f32 s0 = (f32) i / n1, s1 = (f32) (i + 1) / n1, t0 = (f32) j / n2, t1 = (f32) (j + 1) / n2;
            f32 ss[4] = { s0, s1, s1, s0 }, tt[4] = { t0, t0, t1, t1 };

            for (c = 0; c < 4; c++)
            {
                for (k = 0; k < 3; k++)
                {
                    P[c][k] = a[k] + e1[k] * ss[c] + e2[k] * tt[c];
                }
                T[c][0] = ss[c] * l1;
                T[c][1] = tt[c] * l2;
            }
            {
                f32 A1[3][3], B1[3][3], TA[3][2], TB[3][2];
                s32 ia[3] = { 0, 1, 2 }, ib[3] = { 0, 2, 3 };

                for (c = 0; c < 3; c++)
                {
                    memcpy(A1[c], P[ia[c]], sizeof(A1[c]));
                    memcpy(B1[c], P[ib[c]], sizeof(B1[c]));
                    memcpy(TA[c], T[ia[c]], sizeof(TA[c]));
                    memcpy(TB[c], T[ib[c]], sizeof(TB[c]));
                }
                dpTri(mat, outdoor, A1, TA);
                dpTri(mat, outdoor, B1, TB);
            }
        }
    }
}

/* a wall between two floor points, from their floor heights + h0 to + h1 */
static void dpWall(s32 mat, s32 outdoor, f32 ua, f32 va, f32 ha, f32 ub, f32 vb, f32 hb, f32 h0, f32 h1)
{
    f32 A[3] = { ua, va, ha + h1 }, B[3] = { ub, vb, hb + h1 }, D[3] = { ua, va, ha + h0 };

    if (fabsf(hb - ha) < 0.5f)
    {
        dpQuad(mat, outdoor, A, B, D, 320.0f);
    }
    else
    {   /* a sloped run (beside a stair): two triangles with st from u-v distance and height */
        f32 L = hypotf(ub - ua, vb - va);
        f32 P[3][3], T[3][2];

        P[0][0] = ua; P[0][1] = va; P[0][2] = ha + h0; T[0][0] = 0; T[0][1] = ha + h0;
        P[1][0] = ub; P[1][1] = vb; P[1][2] = hb + h0; T[1][0] = L; T[1][1] = hb + h0;
        P[2][0] = ub; P[2][1] = vb; P[2][2] = hb + h1; T[2][0] = L; T[2][1] = hb + h1;
        dpTri(mat, outdoor, P, T);
        P[1][0] = ub; P[1][1] = vb; P[1][2] = hb + h1; T[1][0] = L; T[1][1] = hb + h1;
        P[2][0] = ua; P[2][1] = va; P[2][2] = ha + h1; T[2][0] = 0; T[2][1] = ha + h1;
        dpTri(mat, outdoor, P, T);
    }
}

/* a polygon (floor or ceiling) as a fan of triangles, each subdivided by midpoints until short enough; st = (u, v) */
static void dpFanSub(s32 mat, s32 outdoor, const f32 a[3], const f32 b[3], const f32 c[3], s32 depth)
{
    f32 lab = hypotf(b[0] - a[0], b[1] - a[1]), lbc = hypotf(c[0] - b[0], c[1] - b[1]), lca = hypotf(a[0] - c[0], a[1] - c[1]);

    if (depth < 6 && fmaxf(lab, fmaxf(lbc, lca)) > 420.0f)
    {
        f32 m[3];
        const f32 *p0, *p1, *p2;
        s32 k;

        if (lab >= lbc && lab >= lca) { p0 = a; p1 = b; p2 = c; }
        else if (lbc >= lca) { p0 = b; p1 = c; p2 = a; }
        else { p0 = c; p1 = a; p2 = b; }
        for (k = 0; k < 3; k++)
        {
            m[k] = (p0[k] + p1[k]) * 0.5f;
        }
        dpFanSub(mat, outdoor, p0, m, p2, depth + 1);
        dpFanSub(mat, outdoor, m, p1, p2, depth + 1);
        return;
    }
    {
        f32 P[3][3], T[3][2];
        const f32 *q[3] = { a, b, c };
        s32 j;

        for (j = 0; j < 3; j++)
        {
            P[j][0] = q[j][0];
            P[j][1] = q[j][1];
            P[j][2] = q[j][2];
            T[j][0] = q[j][0];
            T[j][1] = q[j][1];
        }
        dpTri(mat, outdoor, P, T);
    }
}

static void dpPieceGeo(const DpFrame *f, const DpPiece *p)
{
    s32 k, outdoor = (p->flags & PF_OUTDOOR) != 0;
    f32 U[8], V[8], H[8], cu = 0.0f, cv = 0.0f, ch = 0.0f;

    for (k = 0; k < p->n; k++)
    {
        dpVert(f, p, k, &U[k], &V[k], &H[k]);
        cu += U[k] / p->n;
        cv += V[k] / p->n;
        ch += H[k] / p->n;
    }
    /* floor and ceiling: fans from the centroid */
    for (k = 0; k < p->n; k++)
    {
        s32 b = (k + 1) % p->n;
        f32 C[3] = { cu, cv, ch }, A[3] = { U[k], V[k], H[k] }, B[3] = { U[b], V[b], H[b] };

        if (!(p->flags & (PF_STEPS | PF_NOFLOOR)))
        {
            dpFanSub(p->floor, outdoor, C, A, B, 0);
        }
        if (!(p->flags & PF_NOCEIL))
        {
            C[2] += p->height;
            A[2] += p->height;
            B[2] += p->height;
            dpFanSub(p->ceil, outdoor, C, B, A, 0);
        }
        if (p->flags & PF_SLAB)
        {
            f32 S0[3] = { cu, cv, ch - 30.0f }, S1[3] = { U[k], V[k], H[k] - 30.0f }, S2[3] = { U[b], V[b], H[b] - 30.0f };
            dpFanSub(p->wall, outdoor, S0, S2, S1, 0);
            if (p->edge[k] != E_WALL && p->edge[k] != E_LINTEL)
            {
                dpWall(p->wall, outdoor, U[k], V[k], H[k], U[b], V[b], H[b], -30.0f, 0.0f);
            }
        }
    }
    /* steps: a 4-gon, edge 0-1 at the low end, edge 2-3 at the high end */
    if ((p->flags & PF_STEPS) && p->n == 4)
    {
        f32 rise = ((H[2] + H[3]) - (H[0] + H[1])) * 0.5f;
        s32 ns = (s32) ceilf(fabsf(rise) / 24.0f), i;

        if (ns < 1) ns = 1;
        for (i = 0; i < ns; i++)
        {
            f32 t0 = (f32) i / ns, t1 = (f32) (i + 1) / ns;
            f32 a0[3], b0[3], a1[3], b1[3], htread;

            /* points along the 0->3 and 1->2 sides */
            a0[0] = U[0] + (U[3] - U[0]) * t0; a0[1] = V[0] + (V[3] - V[0]) * t0;
            b0[0] = U[1] + (U[2] - U[1]) * t0; b0[1] = V[1] + (V[2] - V[1]) * t0;
            a1[0] = U[0] + (U[3] - U[0]) * t1; a1[1] = V[0] + (V[3] - V[0]) * t1;
            b1[0] = U[1] + (U[2] - U[1]) * t1; b1[1] = V[1] + (V[2] - V[1]) * t1;
            htread = H[0] + (H[3] - H[0]) * (rise > 0 ? t1 : t0);
            {
                f32 A[3] = { a0[0], a0[1], htread }, B[3] = { b0[0], b0[1], htread }, D[3] = { a1[0], a1[1], htread };
                dpQuad(p->floor, outdoor, A, B, D, 400.0f);
            }
            {   /* the riser at the step's low edge */
                f32 hlo = H[0] + (H[3] - H[0]) * t0, hhi = H[0] + (H[3] - H[0]) * t1;
                f32 rlo = fminf(hlo, hhi), rhi = fmaxf(hlo, hhi);
                f32 *ea = rise > 0 ? a0 : a1, *eb = rise > 0 ? b0 : b1;
                f32 A[3] = { ea[0], ea[1], rhi }, B[3] = { eb[0], eb[1], rhi }, D[3] = { ea[0], ea[1], rlo };
                dpQuad(p->wall, outdoor, A, B, D, 400.0f);
            }
        }
    }
    /* walls */
    for (k = 0; k < p->n; k++)
    {
        s32 b = (k + 1) % p->n;

        switch (p->edge[k])
        {
        case E_OPEN:
            break;
        case E_WALL:
            if (!(p->flags & PF_NOCEIL))
            {
                dpWall(p->wall, outdoor, U[k], V[k], H[k], U[b], V[b], H[b], (p->flags & PF_STEPS) ? -40.0f : 0.0f, p->height);
            }
            if (p->flags & PF_KERB)
            {   /* a hazard-striped kerb 8 units in front of the wall (no coplanar faces), 45 high */
                f32 eu = U[b] - U[k], ev = V[b] - V[k], el = sqrtf(eu * eu + ev * ev);
                f32 nu = -ev / el, nv = eu / el;
                f32 A[3], B[3], D[3];

                if ((cu - U[k]) * nu + (cv - V[k]) * nv < 0.0f)
                {
                    nu = -nu;
                    nv = -nv;
                }
                A[0] = U[k] + nu * 8; A[1] = V[k] + nv * 8; A[2] = H[k] + 45;
                B[0] = U[b] + nu * 8; B[1] = V[b] + nv * 8; B[2] = H[b] + 45;
                D[0] = U[k] + nu * 8; D[1] = V[k] + nv * 8; D[2] = H[k];
                dpQuad(M_HAZARD, outdoor, A, B, D, 400.0f);
            }
            else
            {
                dpWall(p->wall, outdoor, U[k], V[k], H[k], U[b], V[b], H[b], 0.0f, p->lintel[k] ? p->lintel[k] : 110.0f);
            }
            break;
        case E_LINTEL:
            dpWall(p->wall, outdoor, U[k], V[k], H[k], U[b], V[b], H[b], p->lintel[k], p->height);
            break;
        case E_RAIL:
            dpWall(M_RAIL, outdoor, U[k], V[k], H[k], U[b], V[b], H[b], 0.0f, p->lintel[k] ? p->lintel[k] : 100.0f);
            break;
        }
    }
}

static void dpBox(const DpBox *bx)
{
    f32 u0 = bx->u0, u1 = bx->u1, v0 = bx->v0, v1 = bx->v1, h0 = bx->h0, h1 = bx->h1;
    f32 A[3], B[3], D[3];

    /* top */
    A[0] = u0; A[1] = v0; A[2] = h1; B[0] = u1; B[1] = v0; B[2] = h1; D[0] = u0; D[1] = v1; D[2] = h1;
    dpQuad(M_METAL, 0, A, B, D, 300.0f);
    /* four sides */
    A[0] = u0; A[1] = v0; A[2] = h1; B[0] = u1; B[1] = v0; B[2] = h1; D[0] = u0; D[1] = v0; D[2] = h0;
    dpQuad(M_METAL, 0, A, B, D, 300.0f);
    A[0] = u0; A[1] = v1; A[2] = h1; B[0] = u1; B[1] = v1; B[2] = h1; D[0] = u0; D[1] = v1; D[2] = h0;
    dpQuad(M_METAL, 0, A, B, D, 300.0f);
    A[0] = u0; A[1] = v0; A[2] = h1; B[0] = u0; B[1] = v1; B[2] = h1; D[0] = u0; D[1] = v0; D[2] = h0;
    dpQuad(M_METAL, 0, A, B, D, 300.0f);
    A[0] = u1; A[1] = v0; A[2] = h1; B[0] = u1; B[1] = v1; B[2] = h1; D[0] = u1; D[1] = v0; D[2] = h0;
    dpQuad(M_METAL, 0, A, B, D, 300.0f);
}

/* big-endian byte writer */
typedef struct { u8 *b; s32 n, cap; } DpBuf;

static void dpPut(DpBuf *o, u32 v, s32 bytes)
{
    s32 i;
    for (i = bytes - 1; i >= 0; i--)
    {
        if (o->n < o->cap) o->b[o->n] = (u8) (v >> (8 * i));
        o->n++;
    }
}

static void dpGfx(DpBuf *o, u32 w0, u32 w1)
{
    dpPut(o, w0, 4);
    dpPut(o, w1, 4);
}

/* Rare's 1172 container (0x11 0x72 + raw deflate) holding stored blocks, as host u32 words of the big-endian image */
static u32 *dpPack1172(const u8 *data, s32 len, s32 *outBytes)
{
    s32 nblk = (len + 65534) / 65535, total, i, o = 0;
    u8 *tmp;
    u32 *words;

    if (nblk < 1) nblk = 1;
    total = 2 + nblk * 5 + len;
    total = (total + 15) & ~15;
    tmp = mempAllocBytesInBank(total, MEMPOOL_STAGE);
    memset(tmp, 0, total);
    tmp[o++] = 0x11;
    tmp[o++] = 0x72;
    for (i = 0; i < nblk; i++)
    {
        s32 chunk = len - i * 65535;
        if (chunk > 65535) chunk = 65535;
        if (chunk < 0) chunk = 0;
        tmp[o++] = (i == nblk - 1) ? 1 : 0;   /* BFINAL, BTYPE 00 (stored); the rest of the byte is padding */
        tmp[o++] = (u8) chunk;
        tmp[o++] = (u8) (chunk >> 8);
        tmp[o++] = (u8) ~chunk;
        tmp[o++] = (u8) (~chunk >> 8);
        memcpy(tmp + o, data + i * 65535, chunk);
        o += chunk;
    }
    words = (u32 *) tmp;   /* reuse in place: rebuild as host words of the big-endian image */
    for (i = 0; i < total / 4; i++)
    {
        u32 w = ((u32) tmp[i * 4] << 24) | ((u32) tmp[i * 4 + 1] << 16) | ((u32) tmp[i * 4 + 2] << 8) | tmp[i * 4 + 3];
        words[i] = w;
    }
    *outBytes = total;
    return words;
}

/* the gathered triangles (sGT) as room data at origin (ox, oy, oz) */
static s32 dpEmitRoomData(s32 ri, s32 room, f32 ox, f32 oy, f32 oz)
{
    DpBuf vb, db;
    s32 i, nv = 0, first = 1, gi;
    s32 ngroups = 0;
    struct { u32 c0; u16 tex; } groups[64];

    if (sNGT == 0)
    {   /* an empty room (a wing that is off): one zero-area triangle, so every room in the table has data */
        f32 z[3][3] = { { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 } }, st[3][2] = { { 0, 0 }, { 0, 0 }, { 0, 0 } };
        dpTri(M_CONCRETE, 0, z, st);
    }
    for (i = 0; i < sNGT; i++)
    {
        s32 found = 0;
        for (gi = 0; gi < ngroups; gi++)
        {
            if (groups[gi].c0 == sGT[i].c0 && groups[gi].tex == sGT[i].tex) found = 1;
        }
        if (!found && ngroups < 64)
        {
            groups[ngroups].c0 = sGT[i].c0;
            groups[ngroups].tex = sGT[i].tex;
            ngroups++;
        }
    }
    vb.cap = sNGT * 3 * 16;
    vb.b = mempAllocBytesInBank(vb.cap, MEMPOOL_STAGE);
    vb.n = 0;
    db.cap = 64 * 8 + (sNGT / 5 + ngroups + 2) * 3 * 8 + sNGT * 4;
    db.b = mempAllocBytesInBank(db.cap, MEMPOOL_STAGE);
    db.n = 0;
    for (gi = 0; gi < ngroups; gi++)
    {
        s32 batch[5], nb = 0;

        if (first)
        {   /* the state prefix stock rooms open with (as gen_flatland.py), culling off */
            dpGfx(&db, 0xE7000000, 0);
            dpGfx(&db, 0xBA001402, 0x00100000);
            dpGfx(&db, 0xB900031D, 0x0C182078);
            dpGfx(&db, 0xFC26A004, 0x1F1093FF);
            dpGfx(&db, 0xBA001001, 0x00010000);
            dpGfx(&db, 0xBB002801, 0xFFFFFFFF);
            dpGfx(&db, groups[gi].c0, groups[gi].tex);
            dpGfx(&db, 0xBA001102, 0);
            dpGfx(&db, 0xBA000C02, 0x00002000);
            dpGfx(&db, 0xB6000000, 0x00003000);
            dpGfx(&db, 0xFB000000, 0x000000FF);
            first = 0;
        }
        else
        {
            dpGfx(&db, groups[gi].c0, groups[gi].tex);
        }
        for (i = 0; i <= sNGT; i++)
        {
            if (i < sNGT && sGT[i].c0 == groups[gi].c0 && sGT[i].tex == groups[gi].tex)
            {
                batch[nb++] = i;
            }
            if (nb == 5 || (i == sNGT && nb > 0))
            {
                s32 b, j, nvb = nb * 3;

                for (b = 0; b < nb; b++)
                {
                    for (j = 0; j < 3; j++)
                    {
                        DpV *v = &sGT[batch[b]].v[j];

                        dpPut(&vb, (u16) (s16) lroundf(v->x - ox), 2);
                        dpPut(&vb, (u16) (s16) lroundf(v->y - oy), 2);
                        dpPut(&vb, (u16) (s16) lroundf(v->z - oz), 2);
                        dpPut(&vb, 0, 2);
                        dpPut(&vb, (u16) (s16) lroundf(v->s), 2);
                        dpPut(&vb, (u16) (s16) lroundf(v->t), 2);
                        dpPut(&vb, v->r, 1);
                        dpPut(&vb, v->g, 1);
                        dpPut(&vb, v->b, 1);
                        dpPut(&vb, 0xFF, 1);
                    }
                }
                dpGfx(&db, 0x04000000 | ((nvb - 1) << 20) | (nvb * 16), 0x0E000000 + nv * 16);
                nv += nvb;
                /* G_TRI4: up to four triangles per command, vertex nibbles of this batch */
                for (b = 0; b < nb; b += 4)
                {
                    u32 w0 = 0xB1000000, w1 = 0;
                    s32 q;

                    for (q = 0; q < 4 && b + q < nb; q++)
                    {
                        s32 base = (b + q) * 3;
                        w0 |= (u32) ((base + 2) & 0xF) << (4 * q);
                        w1 |= (u32) ((base + 0) & 0xF) << (8 * q);
                        w1 |= (u32) ((base + 1) & 0xF) << (8 * q + 4);
                    }
                    dpGfx(&db, w0, w1);
                }
                nb = 0;
            }
        }
    }
    dpGfx(&db, 0xB8000000, 0);
    if (vb.n > vb.cap || db.n > db.cap)
    {
        printf("ge: dam+: room %d buffer overflow\n", room);
        return 0;
    }
    sVtx[ri] = dpPack1172(vb.b, vb.n, &sVtxBytes[ri]);
    sGdl[ri] = dpPack1172(db.b, db.n, &sGdlBytes[ri]);
    sRoomPos[ri][0] = ox;
    sRoomPos[ri][1] = oy;
    sRoomPos[ri][2] = oz;
    printf("ge: dam+: room %d: %d triangles, %d vertices, %d DL bytes\n", room, sNGT, nv, db.n);
    return 1;
}

static s32 dpBuildRoom(s32 room)
{
    s32 f, pi, i, ri = room - DP_ROOM_BAY;
    const DpFrame *owner = NULL;

    sNGT = 0;
    for (f = 0; f < sNF; f++)
    {
        const DpWing *w = sF[f].w;

        if (room < w->room0 || room >= w->room0 + w->nrooms)
        {
            continue;
        }
        owner = &sF[f];
        sCurF = &sF[f];
        sCurRoom = (u8) room;
        for (pi = 0; pi < w->npieces; pi++)
        {
            if (w->pieces[pi].room == room && !(w->pieces[pi].flags & PF_NOGFX))
            {
                dpPieceGeo(&sF[f], &w->pieces[pi]);
            }
        }
        for (i = 0; i < w->nfixtures; i++)
        {
            const DpFixture *fx = &w->fixtures[i];
            if (fx->room == room)
            {
                f32 h = fx->h - 6;
                f32 A[3] = { fx->u, fx->v - 18, h }, B[3] = { fx->u + fx->len, fx->v - 18, h }, D[3] = { fx->u, fx->v + 18, h };
                dpQuad(fx->mat, 0, A, B, D, 400.0f);
            }
        }
        for (i = 0; i < w->nboxes; i++)
        {
            if (w->boxes[i].room == room)
            {
                dpBox(&w->boxes[i]);
            }
        }
    }
    /* patch triangles of stock surfaces around the cuts */
    for (i = 0; i < sNPatch; i++)
    {
        DpGTri *g;
        s32 j;

        if (sPatch[i].room != room)
        {
            continue;
        }
        if (sNGT >= sMaxGT)
        {
            sMaxGT = sMaxGT ? sMaxGT * 2 : 2048;
            sGT = realloc(sGT, sMaxGT * sizeof(DpGTri));
        }
        g = &sGT[sNGT++];
        g->c0 = sPatch[i].c0;
        g->tex = sPatch[i].tex;
        for (j = 0; j < 3; j++)
        {
            g->v[j].x = sPatch[i].x[j];
            g->v[j].y = sPatch[i].y[j];
            g->v[j].z = sPatch[i].z[j];
            g->v[j].s = sPatch[i].s[j];
            g->v[j].t = sPatch[i].t[j];
            g->v[j].r = sPatch[i].r[j];
            g->v[j].g = sPatch[i].g[j];
            g->v[j].b = sPatch[i].b[j];
        }
    }
    if (owner == NULL)
    {
        owner = &sF[0];
        sCurF = owner;
        sCurRoom = (u8) room;
    }
    return dpEmitRoomData(ri, room, owner->ox, owner->oy, owner->oz);
}

static bg_portal_entry *dpPortalQuad(const DpFrame *f, const DpPortalSpec *ps)
{
    bg_portal_entry *p = mempAllocBytesInBank(sizeof(bg_portal_entry) + 4 * sizeof(coord3d), MEMPOOL_STAGE);
    coord3d *pt = &p->point;
    f32 q[4][3];
    s32 i;

    f32 u0 = DP_IS_W(ps->u0) ? f->W - (ps->u0 - 0x4000) : ps->u0, u1 = DP_IS_W(ps->u1) ? f->W - (ps->u1 - 0x4000) : ps->u1;

    memset(p, 0, sizeof(bg_portal_entry) + 4 * sizeof(coord3d));
    p->numPoints = 4;
    dpFile(f, u0, ps->v0, ps->h0, q[0]);
    dpFile(f, u1, ps->v1, ps->h0, q[1]);
    dpFile(f, u1, ps->v1, ps->h1, q[2]);
    dpFile(f, u0, ps->v0, ps->h1, q[3]);
    for (i = 0; i < 4; i++)
    {
        pt[i].f[0] = q[i][0];
        pt[i].f[1] = q[i][1];
        pt[i].f[2] = q[i][2];
    }
    return p;
}

/* the vis script: copy the stock commands, append ours before the END */
typedef struct { u8 type; u8 length; u8 pad[2]; s32 arg; } DpVisCmd;

static void dpAppendVis(void)
{
    DpVisCmd *stock = (DpVisCmd *) dword_CODE_bss_8007FF90, *out;
    s32 n = 0, extra = 0, f, i, o;

    if (stock == NULL)
    {
        return;
    }
    while (stock[n].type != 0)
    {
        n += stock[n].length ? stock[n].length : 1;
    }
    for (f = 0; f < sNF; f++)
    {
        for (i = 0; i < sF[f].w->nvis; i++)
        {
            extra += 8 + sF[f].w->vis[i].n * 2;
        }
    }
    if (extra == 0)
    {
        return;
    }
    out = mempAllocBytesInBank((n + extra + 2) * sizeof(DpVisCmd), MEMPOOL_STAGE);
    memset(out, 0, (n + extra + 2) * sizeof(DpVisCmd));
    memcpy(out, stock, n * sizeof(DpVisCmd));
    o = n;
#define DP_CMD(t, len, a) do { out[o].type = (t); out[o].length = (len); out[o].arg = (a); o++; } while (0)
    for (f = 0; f < sNF; f++)
    {
        for (i = 0; i < sF[f].w->nvis; i++)
        {
            const DpVis *vs = &sF[f].w->vis[i];
            s32 r;

            DP_CMD(0x21, 1, 0);                     /* REMOVE_VIS: start from "nothing seen" */
            DP_CMD(0x14, 3, 0);                     /* PUSH_IF_ROOM_IN_RANGE lo hi */
            DP_CMD(0x65, 0, vs->lo);
            DP_CMD(0x65, 0, vs->hi);
            DP_CMD(0x5a, 1, 0);                     /* IF (pull) */
            if (vs->portal == DP_VIS_FORCE || sF[f].portalIdx[(s32) vs->portal] < 0)
            {
                DP_CMD(0x1e, 1, 0);                 /* FORCE_VIS */
            }
            else
            {
                DP_CMD(0x1f, 2, 0);                 /* MATCH_PORTAL_VIS portal */
                DP_CMD(0x64, 0, sF[f].portalIdx[(s32) vs->portal]);
            }
            for (r = 0; r < vs->n; r++)
            {
                if (vs->rooms[r] >= g_MaxNumRooms) continue;
                DP_CMD(0x20, 2, 0);                 /* ADD_VISIBLE_ROOM r */
                DP_CMD(0x65, 0, vs->rooms[r]);
            }
            DP_CMD(0x5c, 1, 0);                     /* ENDIF */
        }
    }
    DP_CMD(0x00, 1, 0);                             /* END */
#undef DP_CMD
    dword_CODE_bss_8007FF90 = (s32 *) out;
    printf("ge: dam+: vis: %d stock commands + %d appended\n", n, o - n - 1);
}

/* load_bg_file, after the stock room-size loop and before initializeRoomData: cuts, rooms, the room/portal tables, vis */
void portDamPlusBgRooms(void)
{
    s32 nstock = g_MaxNumRooms;        /* rooms 1..N-1 and the end entry N */
    s32 nours, i, f, np, k;
    bg_room_data *rt;
    bg_portal_data_entry *pt;

    if (!sStanOk || sMaxRoom == 0)
    {
        return;
    }
    nours = sMaxRoom - DP_ROOM_BAY + 1;
    if (nstock != DP_ROOM_BAY || nstock + nours >= MAXROOMCOUNT || nours > DP_MAXROOMS)
    {
        printf("ge: dam+: the stock bg has %d room entries (want %d), %d more do not fit MAXROOMCOUNT %d - Dam+ rooms off "
               "(their stan is already in: those rooms have no gfx)\n", nstock, DP_ROOM_BAY, nours, MAXROOMCOUNT);
        sNF = 0;
        return;
    }
    for (np = 0; g_BgPortals[np].offset_portal != NULL; np++)
    {
    }
    /* cuts first (they read stock rooms through the stock table, and fit walls / outdoor light the rooms use) */
    for (f = 0; f < sNF; f++)
    {
        dpDoCuts(&sF[f]);
    }
    for (i = 0; i < nours; i++)
    {
        if (!dpBuildRoom(DP_ROOM_BAY + i))
        {
            sNF = 0;
            return;
        }
    }
    rt = mempAllocBytesInBank((nstock + nours + 2) * sizeof(bg_room_data), MEMPOOL_STAGE);
    memset(rt, 0, (nstock + nours + 2) * sizeof(bg_room_data));
    memcpy(rt, ptr_bgdata_room_fileposition_list, nstock * sizeof(bg_room_data));
    rt[nstock + nours] = ptr_bgdata_room_fileposition_list[nstock];   /* the end entry moves past ours */
    for (i = 0; i < nours; i++)
    {
        s32 r = DP_ROOM_BAY + i;

        rt[r].pPointTableBin = sVtx[i];
        rt[r].pPriMappingBin = sGdl[i];
        rt[r].pSecMappingBin = NULL;
        rt[r].pos.f[0] = sRoomPos[i][0];
        rt[r].pos.f[1] = sRoomPos[i][1];
        rt[r].pos.f[2] = sRoomPos[i][2];
        g_BgRoomInfo[r].model_bin_loaded = 0;
        g_BgRoomInfo[r].field_35 = 0;
        g_BgRoomInfo[r].csize_point_index_binary = sVtxBytes[i];
        g_BgRoomInfo[r].csize_primary_DL_binary = sGdlBytes[i];
        g_BgRoomInfo[r].csize_secondary_DL_binary = 0;
        g_BgRoomInfo[r].cur_room_totalsize = -1;
    }
    ptr_bgdata_room_fileposition_list = rt;
    g_MaxNumRooms = nstock + nours;

    /* portals */
    {
        s32 nadd = 0;

        for (f = 0; f < sNF; f++)
        {
            nadd += sF[f].w->nportals;
        }
        if (np + nadd >= PORTMAX)
        {
            printf("ge: dam+: %d stock portals + %d do not fit PORTMAX %d - no portals added\n", np, nadd, PORTMAX);
            nadd = 0;
        }
        pt = mempAllocBytesInBank((np + nadd + 1) * sizeof(bg_portal_data_entry), MEMPOOL_STAGE);
        memset(pt, 0, (np + nadd + 1) * sizeof(bg_portal_data_entry));
        memcpy(pt, g_BgPortals, np * sizeof(bg_portal_data_entry));
        i = np;
        for (f = 0; f < sNF && nadd > 0; f++)
        {
            memset(sF[f].portalIdx, 0xff, sizeof(sF[f].portalIdx));
            for (k = 0; k < sF[f].w->nportals; k++)
            {
                const DpPortalSpec *ps = &sF[f].w->portals[k];

                pt[i].offset_portal = dpPortalQuad(&sF[f], ps);
                pt[i].connectedRoom1 = ps->r1 ? ps->r1 : (u8) sF[f].seamRoom[0];
                pt[i].connectedRoom2 = ps->r2;
                if (k < 8) sF[f].portalIdx[k] = i;
                i++;
            }
            printf("ge: dam+: %s: portals %d-%d\n", sF[f].w->name, i - sF[f].w->nportals, i - 1);
        }
        g_BgPortals = pt;
    }
    dpAppendVis();
    printf("ge: dam+: bg: rooms %d-%d added (%d room entries), %d portals\n", DP_ROOM_BAY, sMaxRoom, g_MaxNumRooms, i);
    fflush(stdout);
}

/* one loaded display list: zero the marked triangles (TRI4 slot nibbles / TRI1 word), after checking its triangle count */
static s32 dpDropInDl(u32 *dl, s32 n, const u8 *drop, s32 ntris, s32 room, s32 key)
{
    s32 i, count = 0, dropped = 0, pass;

    for (pass = 0; pass < 2; pass++)
    {
        count = 0;
        for (i = 0; i < n; i++)
        {
            u32 w0 = dl[i * 2], w1 = dl[i * 2 + 1], op = w0 >> 24;
            if (op == 0xB1)
            {
                s32 q;
                for (q = 0; q < 4; q++)
                {
                    if (((w1 >> (8 * q)) & 0xFF) || ((w0 >> (4 * q)) & 0xF))
                    {
                        if (pass == 1 && drop[count])
                        {
                            w1 &= ~(0xFFu << (8 * q));
                            w0 &= ~(0xFu << (4 * q));
                            dropped++;
                        }
                        count++;
                    }
                }
                if (pass == 1)
                {
                    dl[i * 2] = w0;
                    dl[i * 2 + 1] = w1;
                }
            }
            else if (op == 0xBF)
            {
                if (pass == 1 && drop[count])
                {
                    dl[i * 2 + 1] = 0;
                    dropped++;
                }
                count++;
            }
            else if (op == 0xB8)
            {
                break;
            }
        }
        if (pass == 0 && count != ntris)
        {
            static s32 warned;
            if (!warned++)
            {
                printf("ge: dam+: room %d list %d loaded with %d triangles, the bg file has %d - cut not applied\n", room, key, count, ntris);
            }
            return 0;
        }
    }
    return dropped;
}

/* bgLoadRoomModelData, a stock room just loaded: drop the triangles our cuts replaced (in the loaded copy) */
void portDamPlusRoomLoaded(s32 room)
{
    DpRoomCut *rc = NULL;
    s32 i, dropped = 0;

    for (i = 0; i < sNCuts; i++)
    {
        if (sCuts[i].room == room) rc = &sCuts[i];
    }
    if (rc == NULL || rc->ndrop == 0)
    {
        return;
    }
    if (g_BgRoomInfo[room].ptr_expanded_mapping_info != NULL)
    {
        dropped += dpDropInDl((u32 *) g_BgRoomInfo[room].ptr_expanded_mapping_info, g_BgRoomInfo[room].usize_primary_DL_binary / 8,
                              rc->drop[0], rc->ntris[0], room, 1);
    }
    if (g_BgRoomInfo[room].ptr_secondary_expanded_mapping_info != NULL && rc->ntris[1] > 0)
    {
        dropped += dpDropInDl((u32 *) g_BgRoomInfo[room].ptr_secondary_expanded_mapping_info,
                              g_BgRoomInfo[room].usize_secondary_DL_binary / 8, rc->drop[1], rc->ntris[1], room, 2);
    }
    {
        static u32 logged;
        if (!(logged & (1u << (rc - sCuts))))
        {
            logged |= 1u << (rc - sCuts);
            printf("ge: dam+: room %d loaded: %d triangles cut\n", room, dropped);
        }
    }
}

/* the test rooms (GE_DAMPLUS_TESTROOMS) use the motor pool's working copy */
s32 portDamPlusTestRooms(void)
{
    return sTestRooms;
}

#endif
