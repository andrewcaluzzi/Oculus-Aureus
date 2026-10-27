/* port_damplus_geo.c - Dam+ slice 2: the motor pool behind the start shutter (door 281), built at load time.
 *
 * Nothing here is copied from the ROM. The rooms are described below in a door-local frame (world units: u along the shutter,
 * v into the rock behind it, h up) and turned into Dam's frame at run time from the stock stan edges under the shutter:
 * load_bg_file -> portDamPlusStan() (after the stock stan is loaded) finds door 281's bound pad in the setup file, the open stan
 * edges under it (A..E), builds the frame from them, then generates
 *   - the stan: our triangles appended after the stock tiles in a STAGE copy of the packed stan, stitched both ways to the stock
 *     edges A-B-E (their points are used as read, so the seam is exact),
 *   - the bg: two rooms (137 vehicle bay, 138 barracks) as host-pointer room data (vertices + one display list per room, in a
 *     1172 container made of stored deflate blocks), Dam texture NUMBERS only, baked vertex lighting,
 *   - two portals (the shutter: 135<->137, the barracks doorway: 137<->138).
 * load_bg_file -> portDamPlusBgRooms() then swaps in the extended room and portal tables just before initializeRoomData, after
 * the stock code computed every stock room's sizes from the stock table (so room 136's size is right).
 * The setup side (door, key, guards, pads in the new rooms, waypoints) is in port_damplus.c. */
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

/* libm calls the project's math.h does not declare (an undeclared float function is int f() and returns garbage) */
extern long lroundf(float);
extern float floorf(float);
extern float ceilf(float);
extern float hypotf(float, float);

extern s32 portDamPlusParts(void);
extern void *_fileNameLoadToBank(char *filename, s32 loadMethod, s32 size, u8 bank);
extern s32 fileGetIndex(u8 *name);
extern s32 romdataFileServed(s32 index);
extern char *setup_text_pointers[];
extern s32 sizepropdef(PropDefHeaderRecord *pdef);
extern u8 list_of_tilesizes[];
extern void stanDetermineEOF(void *file, s32 origBase, u8 *newBase);
extern void stanLoadFile(void *file);

#define DP_MOTORPOOL 2
#define DP_DOOR 281            /* the stock rusty shutter beside Bond's start (propdef index) */
#define DP_ROOM_BAY 137        /* our bg/stan room ids: right after Dam's last room (136) */
#define DP_ROOM_BARRACKS 138

struct DpPrefix { s32 stanfile; StandTile *first; };

/* ---------------------------------------------------------------------------------------------------------------------------
 * The layout (door-local frame, world units). u: along the shutter from its A end (0) to its E end (W, read at run time);
 * v: away from room 135, into the rock; h: up from the stan floor at A. Codes: DP_W(x) = W - x; DP_SEAM_A/B/E = the stock seam
 * points themselves (used as read). Polygons are convex, listed in order; fan-triangulated from `root`. */
#define DP_W(x) (0x4000 + (x))
#define DP_SEAM_A 0x7001
#define DP_SEAM_B 0x7002
#define DP_SEAM_E 0x7003

enum { E_WALL = 0, E_OPEN = 1, E_LINTEL = 2 };   /* per edge: full wall, no wall (shared / portal), wall above `lintel` */
enum { M_ASPHALT, M_PANEL, M_CEIL, M_HAZARD, M_LAMP, M_UGWALL, M_UGFLOOR, M_UGCEIL, M_UGLAMP, M_CONCRETE, M_COUNT };

typedef struct
{
    u8 room;
    u8 n, root;
    s16 u[8], v[8];
    u8 edge[8];
    s16 lintel[8];
    s16 height;
    u8 floor, wall, ceil;
} DpPiece;

static const DpPiece sPieces[] = {
    /* room 137: the apron under the shutter (A, B and E are the stock edge), the throat behind it (inside the stock rock faces
     * there: a jamb wall at u ~140, a slab at v ~780, overhangs from h ~440 up; the shutter is ~402 high), the vehicle bay off its
     * left side. The bay is cut into the rock: the road tunnel's rock (1132) for walls, its asphalt and ceiling */
    { DP_ROOM_BAY, 5, 4, { DP_SEAM_A, DP_SEAM_B, DP_SEAM_E, DP_W(20), 160 }, { 0, 0, 0, 40, 40 },
      { E_OPEN, E_OPEN, E_WALL, E_OPEN, E_WALL }, { 0 }, 415, M_ASPHALT, M_PANEL, M_CEIL },
    { DP_ROOM_BAY, 6, 1, { 160, DP_W(20), DP_W(20), 160, 160, 160 }, { 40, 40, 720, 720, 720, 460 },
      { E_OPEN, E_WALL, E_WALL, E_OPEN, E_OPEN, E_WALL }, { 0 }, 415, M_ASPHALT, M_PANEL, M_CEIL },
    { DP_ROOM_BAY, 7, 0, { -1700, 160, 160, 160, -800, -1000, -1700 }, { 460, 460, 720, 2000, 2000, 2000, 2000 },
      { E_WALL, E_LINTEL, E_WALL, E_WALL, E_LINTEL, E_WALL, E_WALL }, { 0, 415, 0, 0, 230, 0, 0 }, 700, M_ASPHALT, M_PANEL, M_CEIL },
    /* room 138: a short doorway passage and the barracks */
    { DP_ROOM_BARRACKS, 4, 0, { -1000, -800, -800, -1000 }, { 2000, 2000, 2040, 2040 },
      { E_OPEN, E_WALL, E_OPEN, E_WALL }, { 0 }, 230, M_UGFLOOR, M_UGWALL, M_UGCEIL },
    { DP_ROOM_BARRACKS, 6, 4, { -1500, -1000, -800, -300, -300, -1500 }, { 2040, 2040, 2040, 2040, 2900, 2900 },
      { E_WALL, E_OPEN, E_WALL, E_WALL, E_WALL, E_WALL }, { 0 }, 330, M_UGFLOOR, M_UGWALL, M_UGCEIL },
};

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
    { 286,  0xC0080002, 30.6f, 120, 120, 120 },   /* concrete */
};

/* lamps per room: u, v, h, radius (world), colour */
typedef struct { s16 u, v, h, r; u8 cr, cg, cb; } DpLamp;
static const DpLamp sLamps[] = {
    { 480, 120, 405, 520, 150, 165, 200 },      /* dusk-blue spill at the shutter */
    { 520, 560, 405, 520, 255, 190, 110 },      /* sodium lamps: throat, bay */
    { -400, 900, 690, 900, 255, 185, 100 },
    { -1200, 900, 690, 900, 255, 185, 100 },
    { -400, 1650, 690, 900, 255, 185, 100 },
    { -1200, 1650, 690, 900, 255, 185, 100 },
    { -900, 2450, 320, 700, 200, 230, 210 },    /* barracks: cold strip light */
};
/* ceiling light fixtures (quads just under the ceiling): u, v, length along u, room height */
static const struct { s16 u, v, len, h; u8 mat; u8 room; } sFixtures[] = {
    { 380, 300, 260, 415, M_LAMP, DP_ROOM_BAY }, { 380, 600, 260, 415, M_LAMP, DP_ROOM_BAY },
    { -560, 900, 320, 700, M_LAMP, DP_ROOM_BAY }, { -1360, 900, 320, 700, M_LAMP, DP_ROOM_BAY },
    { -560, 1650, 320, 700, M_LAMP, DP_ROOM_BAY }, { -1360, 1650, 320, 700, M_LAMP, DP_ROOM_BAY },
    { -1000, 2450, 200, 330, M_UGLAMP, DP_ROOM_BARRACKS },
};

/* --------------------------------------------------------------------------------------------------------------------------- */

static struct
{
    s32 ok;                     /* the motor pool is built for this stage load */
    f32 ls;                     /* levelscale (world = file / ls) */
    f32 ox, oy, oz;             /* A, file units */
    f32 ux, uz, vx, vz;         /* frame axes (xz) */
    f32 W;                      /* opening width, world */
    f32 bu;                     /* B's u, world */
    s16 seam[3][3];             /* A, B, E as read (file units) */
    StandTile *seamTile[2];     /* the stock tiles owning A-B and B-E */
    s32 seamEdge[2];            /* their edge index (point k -> k+1) */
    s32 seamRoom;
    /* our stan */
    u8 *stanBlock;
    StandTile *first;           /* the block's first tile */
    StandTile *ourTiles;        /* our first tile */
    s32 nTiles;
    u8 tileRoom[256];
    f32 tileUV[256][3][2];      /* each tile's corners in the design frame (world), for pad -> tile lookups */
    /* our bg */
    u32 *vtx[2], *gdl[2];
    s32 vtxBytes[2], gdlBytes[2];
    bg_portal_entry *portal[2];
    f32 doorH;                  /* shutter opening height (world) */
} S;

s32 portDamPlusMotorPoolReady(void)
{
    return S.ok;
}

s32 portDamPlusSeamRoom(void)
{
    return S.seamRoom;
}

/* design (u, v, h) -> file units (absolute) */
static f32 dpU(s16 u)
{
    if (u == DP_SEAM_A) return 0.0f;
    if (u == DP_SEAM_B) return S.bu;
    if (u == DP_SEAM_E) return S.W;
    if (u >= 0x4000 - 2000 && u <= 0x4000 + 4000) return S.W - (f32) (u - 0x4000);
    return (f32) u;
}

static void dpFile(f32 u, f32 v, f32 h, f32 out[3])
{
    out[0] = S.ox + (u * S.ux + v * S.vx) * S.ls;
    out[1] = S.oy + h * S.ls;
    out[2] = S.oz + (u * S.uz + v * S.vz) * S.ls;
}

/* world position of a design point (for pads): file / ls */
void portDamPlusDesignToWorld(f32 u, f32 v, f32 h, f32 out[3])
{
    dpFile(u, v, h, out);
    out[0] /= S.ls;
    out[1] /= S.ls;
    out[2] /= S.ls;
}

/* the stock seam points as s16, or our own design vertex rounded the one way every piece rounds it */
static void dpStanPoint(const DpPiece *p, s32 k, s16 out[3])
{
    s16 u = p->u[k];

    if (u == DP_SEAM_A || u == DP_SEAM_B || u == DP_SEAM_E)
    {
        s32 i = (u == DP_SEAM_A) ? 0 : (u == DP_SEAM_B) ? 1 : 2;
        out[0] = S.seam[i][0];
        out[1] = S.seam[i][1];
        out[2] = S.seam[i][2];
    }
    else
    {
        f32 f[3];
        dpFile(dpU(u), p->v[k], 0.0f, f);
        out[0] = (s16) lroundf(f[0]);
        out[1] = S.seam[0][1];
        out[2] = (s16) lroundf(f[2]);
    }
}

/* ---- setup peek: door 281's bound pad, read from the setup file before the setup loads ------------------------------------ */
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

static StandTile *dpNextTile(StandTile *t)
{
    return (StandTile *) (((u8 *) t) + list_of_tilesizes[t->tail.hdrTail.pointCount & 0xf]);
}

/* the open stock edges under the shutter: both ends within a few units of the door's line and inside its width */
static s32 dpFindSeam(StandTile *first, const f32 P[3], const f32 U[3], f32 ulo, f32 uhi)
{
    f32 N[2] = { U[2], -U[0] };
    StandTile *t;
    s32 n = 0;
    f32 lo = 1e9f, hi = -1e9f;
    s16 pts[4][3];
    s32 np = 0;
    s32 i, k;

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
            S.seamTile[n] = t;
            S.seamEdge[n] = k;
            S.seamRoom = t->room;
            n++;
            /* collect the distinct points */
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
            lo = fminf(lo, fminf(ua, ub));
            hi = fmaxf(hi, fmaxf(ua, ub));
        }
    }
    if (n != 2 || np != 3)
    {
        printf("ge: dam+: found %d open edges / %d points under the shutter (want 2 / 3) - motor pool off\n", n, np);
        return 0;
    }
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
            memcpy(S.seam[i], pts[order[i]], sizeof(S.seam[i]));
        }
    }
    return 1;
}

/* ---- our stan --------------------------------------------------------------------------------------------------------------- */
typedef struct { s16 p[3][3]; u8 room; f32 uv[3][2]; } DpTri;

static s32 dpSameXZ(const s16 *a, const s16 *b)
{
    return a[0] == b[0] && a[2] == b[2];
}

static s32 dpBuildTris(DpTri *out, s32 max)
{
    s32 n = 0, pi;

    for (pi = 0; pi < (s32) ARRAYCOUNT(sPieces); pi++)
    {
        const DpPiece *p = &sPieces[pi];
        s32 k;

        for (k = 0; k < p->n; k++)
        {
            s32 a = (p->root + 1 + k) % p->n, b = (p->root + 2 + k) % p->n;
            s32 idx[3] = { p->root, a, b };
            s32 j;
            f32 cr;

            if (a == p->root || b == p->root)
            {
                continue;
            }
            if (n >= max)
            {
                return n;
            }
            for (j = 0; j < 3; j++)
            {
                dpStanPoint(p, idx[j], out[n].p[j]);
                out[n].uv[j][0] = dpU(p->u[idx[j]]);
                out[n].uv[j][1] = p->v[idx[j]];
            }
            out[n].room = p->room;
            /* stock tiles wind clockwise seen from above (negative cross in x,z): match it */
            cr = (f32) (out[n].p[1][0] - out[n].p[0][0]) * (out[n].p[2][2] - out[n].p[0][2])
               - (f32) (out[n].p[1][2] - out[n].p[0][2]) * (out[n].p[2][0] - out[n].p[0][0]);
            if (cr == 0.0f)
            {
                continue;   /* degenerate: never emit a zero-area tile */
            }
            if (cr > 0.0f)
            {
                s16 t3[3];
                f32 tuv[2];

                memcpy(t3, out[n].p[1], sizeof(t3));
                memcpy(out[n].p[1], out[n].p[2], sizeof(t3));
                memcpy(out[n].p[2], t3, sizeof(t3));
                memcpy(tuv, out[n].uv[1], sizeof(tuv));
                memcpy(out[n].uv[1], out[n].uv[2], sizeof(tuv));
                memcpy(out[n].uv[2], tuv, sizeof(tuv));
            }
            n++;
        }
    }
    return n;
}

static s32 dpBuildStan(void *prefixv)
{
    struct DpPrefix *prefix = prefixv;
    DpTri *tris = calloc(256, sizeof(DpTri));
    s32 ntri, i, j, k, nslots = 0, maxnum = 0;
    u32 stockbytes;
    u8 *blk;
    u32 *hdr;
    StandTile *t, *first;
    u32 *oldslots = (u32 *) prefix + 2;

    ntri = (tris != NULL) ? dpBuildTris(tris, 256) : 0;
    if (ntri <= 0)
    {
        free(tris);
        return 0;
    }
    /* the stock tiles: size, highest tile number (ours go above it) */
    for (t = prefix->first; *(u32 *) t != 0; t = dpNextTile(t))
    {
        s32 num = (t->id >> 8) & 0x7fff;
        if (num > maxnum) maxnum = num;
    }
    stockbytes = (u32) ((u8 *) t - (u8 *) prefix->first);
    while (oldslots[nslots] != 0)
    {
        nslots++;
    }
    if (maxnum + 100 + ntri > 0x7fff)
    {
        free(tris);
        return 0;
    }
    /* [0][first][room slots..][0] [stock tiles][our tiles][0] */
    blk = mempAllocBytesInBank(((8 + (nslots + 1) * 4 + stockbytes + ntri * 0x20 + 16) + 15) & ~15, MEMPOOL_STAGE);
    hdr = (u32 *) blk;
    first = (StandTile *) (blk + 8 + (nslots + 1) * 4);
    memcpy(first, prefix->first, stockbytes);
    hdr[0] = 0;
    hdr[1] = (u32) first;
    for (i = 0; i < nslots; i++)
    {
        hdr[2 + i] = (u32) first + (oldslots[i] - (u32) prefix->first);
    }
    hdr[2 + nslots] = 0;
    S.ourTiles = (StandTile *) ((u8 *) first + stockbytes);
    memset(S.ourTiles, 0, ntri * 0x20 + 16);
    for (i = 0; i < ntri; i++)
    {
        StandTile *o = (StandTile *) ((u8 *) S.ourTiles + i * 0x20);
        StandTile *sm = (StandTile *) ((u8 *) first + ((u8 *) S.seamTile[0] - (u8 *) prefix->first));

        o->id = (u32) (maxnum + 100 + i) << 8;     /* 'p' tiles numbered above every stock tile */
        o->room = tris[i].room;
        o->mid.half = (tris[i].room == DP_ROOM_BAY) ? sm->mid.half : 0;
        if (tris[i].room != DP_ROOM_BAY)
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
            o->points[j].x = tris[i].p[j][0];
            o->points[j].y = tris[i].p[j][1];
            o->points[j].z = tris[i].p[j][2];
            o->points[j].link = 0;
        }
        S.tileRoom[i] = tris[i].room;
        memcpy(S.tileUV[i], tris[i].uv, sizeof(tris[i].uv));
    }
    /* links: our shared edges, then the seam both ways */
#define DP_LINK(tile) ((u16) ((0x80 + ((u8 *) (tile) - (u8 *) first)) >> 3))
    for (i = 0; i < ntri; i++)
    {
        StandTile *o = (StandTile *) ((u8 *) S.ourTiles + i * 0x20);

        for (k = 0; k < 3; k++)
        {
            s16 *a = tris[i].p[k], *b = tris[i].p[(k + 1) % 3];
            s32 m;

            for (j = 0; j < ntri && o->points[k].link == 0; j++)
            {
                if (j == i) continue;
                for (m = 0; m < 3; m++)
                {
                    if (dpSameXZ(tris[j].p[m], b) && dpSameXZ(tris[j].p[(m + 1) % 3], a))
                    {
                        o->points[k].link = DP_LINK((u8 *) S.ourTiles + j * 0x20);
                        break;
                    }
                }
            }
            for (m = 0; m < 2 && o->points[k].link == 0; m++)
            {
                StandTile *st = (StandTile *) ((u8 *) first + ((u8 *) S.seamTile[m] - (u8 *) prefix->first));
                s32 c = st->tail.hdrTail.pointCount & 0xf, e = S.seamEdge[m];
                s16 sa[3] = { st->points[e].x, st->points[e].y, st->points[e].z };
                s16 sb[3] = { st->points[(e + 1) % c].x, st->points[(e + 1) % c].y, st->points[(e + 1) % c].z };

                if ((dpSameXZ(sa, a) && dpSameXZ(sb, b)) || (dpSameXZ(sa, b) && dpSameXZ(sb, a)))
                {
                    o->points[k].link = DP_LINK(st);  /* ours -> stock */
                    st->points[e].link = DP_LINK(o); /* stock -> ours (in our copy only) */
                }
            }
        }
    }
#undef DP_LINK
    free(tris);
    S.stanBlock = blk;
    S.first = first;
    S.nTiles = ntri;
    printf("ge: dam+: stan: %d tiles appended to %u bytes of stock tiles (rooms %d, %d), numbered from p%d\n", ntri, stockbytes,
           DP_ROOM_BAY, DP_ROOM_BARRACKS, maxnum + 100);
    return 1;
}

/* the tile under a design point (world u, v), or NULL */
StandTile *portDamPlusTileAt(f32 u, f32 v)
{
    s32 i;

    for (i = 0; S.ok && i < S.nTiles; i++)
    {
        f32 (*q)[2] = S.tileUV[i];
        f32 d1 = (u - q[1][0]) * (q[0][1] - q[1][1]) - (q[0][0] - q[1][0]) * (v - q[1][1]);
        f32 d2 = (u - q[2][0]) * (q[1][1] - q[2][1]) - (q[1][0] - q[2][0]) * (v - q[2][1]);
        f32 d3 = (u - q[0][0]) * (q[2][1] - q[0][1]) - (q[2][0] - q[0][0]) * (v - q[0][1]);
        s32 neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
        s32 pos = (d1 > 0) || (d2 > 0) || (d3 > 0);

        if (!(neg && pos))
        {
            return (StandTile *) ((u8 *) S.ourTiles + i * 0x20);
        }
    }
    return NULL;
}

/* ---- our bg rooms ------------------------------------------------------------------------------------------------------------ */
typedef struct { f32 x, y, z; f32 s, t; u8 r, g, b; } DpV;
typedef struct { DpV v[3]; u8 mat; } DpGTri;

static DpGTri *sGT;
static s32 sNGT, sMaxGT;

static void dpLight(f32 u, f32 v, f32 h, u8 room, u8 mat, u8 *r, u8 *g, u8 *b)
{
    f32 acc[3] = { 0.18f, 0.18f, 0.22f };
    s32 i;

    if (sMats[mat].r == 255 && sMats[mat].g == 255)
    {
        *r = *g = *b = 255;   /* light fixtures stay full bright */
        return;
    }
    for (i = 0; i < (s32) ARRAYCOUNT(sLamps); i++)
    {
        const DpLamp *L = &sLamps[i];
        f32 du = u - L->u, dv = v - L->v, dh = h - L->h;
        f32 d2 = du * du + dv * dv + dh * dh;
        f32 k = 1.0f / (1.0f + d2 / ((f32) L->r * L->r));

        if ((room == DP_ROOM_BARRACKS) != (L->v > 2000))
        {
            k *= 0.15f;   /* a room's lamps barely reach the other room */
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
static void dpTri(u8 room, u8 mat, const f32 dsg[3][3], const f32 st[3][2])
{
    DpGTri *g;
    s32 j;

    if (sNGT >= sMaxGT)
    {
        return;
    }
    g = &sGT[sNGT++];
    g->mat = mat;
    for (j = 0; j < 3; j++)
    {
        f32 f[3];

        dpFile(dsg[j][0], dsg[j][1], dsg[j][2], f);
        g->v[j].x = f[0];
        g->v[j].y = f[1];
        g->v[j].z = f[2];
        g->v[j].s = st[j][0] * S.ls * sMats[mat].st;
        g->v[j].t = st[j][1] * S.ls * sMats[mat].st;
        dpLight(dsg[j][0], dsg[j][1], dsg[j][2], room, mat, &g->v[j].r, &g->v[j].g, &g->v[j].b);
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
static void dpQuad(u8 room, u8 mat, const f32 a[3], const f32 b[3], const f32 d[3], f32 cell, s32 flip)
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

                if (flip)
                {
                    ia[1] = 2; ia[2] = 1;
                    ib[1] = 3; ib[2] = 2;
                }
                for (c = 0; c < 3; c++)
                {
                    memcpy(A1[c], P[ia[c]], sizeof(A1[c]));
                    memcpy(B1[c], P[ib[c]], sizeof(B1[c]));
                    memcpy(TA[c], T[ia[c]], sizeof(TA[c]));
                    memcpy(TB[c], T[ib[c]], sizeof(TB[c]));
                }
                dpTri(room, mat, A1, TA);
                dpTri(room, mat, B1, TB);
            }
        }
    }
}

/* a polygon (floor or ceiling) as a fan of triangles, each subdivided by midpoints until short enough */
static void dpFanSub(u8 room, u8 mat, const f32 a[3], const f32 b[3], const f32 c[3], s32 depth)
{
    f32 lab = hypotf(b[0] - a[0], b[1] - a[1]), lbc = hypotf(c[0] - b[0], c[1] - b[1]), lca = hypotf(a[0] - c[0], a[1] - c[1]);

    if (depth < 5 && fmaxf(lab, fmaxf(lbc, lca)) > 420.0f)
    {
        f32 m[3], n[3];
        const f32 *p0, *p1, *p2;
        s32 k;

        /* split the longest edge */
        if (lab >= lbc && lab >= lca) { p0 = a; p1 = b; p2 = c; }
        else if (lbc >= lca) { p0 = b; p1 = c; p2 = a; }
        else { p0 = c; p1 = a; p2 = b; }
        for (k = 0; k < 3; k++)
        {
            m[k] = (p0[k] + p1[k]) * 0.5f;
        }
        (void) n;
        dpFanSub(room, mat, p0, m, p2, depth + 1);
        dpFanSub(room, mat, m, p1, p2, depth + 1);
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
        dpTri(room, mat, P, T);
    }
}

static void dpBuildRoomGeo(u8 room)
{
    s32 pi, k, f;
    f32 cu, cv;

    for (pi = 0; pi < (s32) ARRAYCOUNT(sPieces); pi++)
    {
        const DpPiece *p = &sPieces[pi];

        if (p->room != room)
        {
            continue;
        }
        /* floor and ceiling: (u, v, h) */
        for (k = 1; k + 1 < p->n; k++)
        {
            s32 a = p->root, b = (p->root + k) % p->n, c = (p->root + k + 1) % p->n;
            f32 A[3] = { dpU(p->u[a]), p->v[a], 0 }, B[3] = { dpU(p->u[b]), p->v[b], 0 }, C[3] = { dpU(p->u[c]), p->v[c], 0 };

            dpFanSub(room, p->floor, A, B, C, 0);
            A[2] = B[2] = C[2] = p->height;
            dpFanSub(room, p->ceil, A, C, B, 0);
        }
        /* walls */
        cu = cv = 0.0f;
        for (k = 0; k < p->n; k++)
        {
            cu += dpU(p->u[k]) / p->n;
            cv += (f32) p->v[k] / p->n;
        }
        for (k = 0; k < p->n; k++)
        {
            s32 b = (k + 1) % p->n;
            f32 h0;
            f32 ua = dpU(p->u[k]), va = p->v[k], ub = dpU(p->u[b]), vb = p->v[b];

            if (p->edge[k] == E_OPEN)
            {
                continue;
            }
            h0 = (p->edge[k] == E_LINTEL) ? p->lintel[k] : 0.0f;
            {
                f32 A[3] = { ua, va, p->height }, B[3] = { ub, vb, p->height }, D[3] = { ua, va, h0 };

                dpQuad(room, p->wall, A, B, D, 320.0f, 0);
            }
            if (h0 == 0.0f && room == DP_ROOM_BAY)
            {   /* a hazard-striped kerb along the bay's walls, like the canyon and the road tunnel */
                f32 eu = ub - ua, ev = vb - va, el = sqrtf(eu * eu + ev * ev);
                f32 nu = -ev / el, nv = eu / el;
                f32 A[3], B[3], D[3];

                if ((cu - ua) * nu + (cv - va) * nv < 0.0f)
                {
                    nu = -nu;
                    nv = -nv;
                }
                nu *= 8.0f;   /* 8 units in front of the wall: no coplanar faces */
                nv *= 8.0f;
                A[0] = ua + nu; A[1] = va + nv; A[2] = 45;
                B[0] = ub + nu; B[1] = vb + nv; B[2] = 45;
                D[0] = ua + nu; D[1] = va + nv; D[2] = 0;
                dpQuad(room, M_HAZARD, A, B, D, 400.0f, 0);
            }
        }
    }
    for (f = 0; f < (s32) ARRAYCOUNT(sFixtures); f++)
    {
        if (sFixtures[f].room == room)
        {
            f32 h = sFixtures[f].h - 6;
            f32 A[3] = { sFixtures[f].u, sFixtures[f].v - 18, h }, B[3] = { sFixtures[f].u + sFixtures[f].len, sFixtures[f].v - 18, h };
            f32 D[3] = { sFixtures[f].u, sFixtures[f].v + 18, h };

            dpQuad(room, sFixtures[f].mat, A, B, D, 400.0f, 0);
        }
    }
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

static s32 dpBuildRoomData(s32 ri, u8 room)
{
    DpGTri *gt = malloc(2600 * sizeof(DpGTri));
    DpBuf vb, db;
    s32 i, m, nv = 0, first = 1;

    if (gt == NULL)
    {
        return 0;
    }
    sGT = gt;
    sNGT = 0;
    sMaxGT = 2600;
    dpBuildRoomGeo(room);
    if (sNGT == 0 || sNGT >= sMaxGT)
    {
        free(gt);
        printf("ge: dam+: room %d: %d triangles (limit %d) - motor pool off\n", room, sNGT, sMaxGT);
        return 0;
    }
    vb.cap = sNGT * 3 * 16;
    vb.b = mempAllocBytesInBank(vb.cap, MEMPOOL_STAGE);
    vb.n = 0;
    db.cap = 64 * 8 + (sNGT / 5 + M_COUNT + 2) * 3 * 8 + sNGT * 4;
    db.b = mempAllocBytesInBank(db.cap, MEMPOOL_STAGE);
    db.n = 0;
    for (m = 0; m < M_COUNT; m++)
    {
        s32 cnt = 0, batch[15], nb = 0;

        for (i = 0; i < sNGT; i++)
        {
            if (gt[i].mat == m) cnt++;
        }
        if (cnt == 0)
        {
            continue;
        }
        if (first)
        {   /* the state prefix stock rooms open with (as gen_flatland.py), culling off */
            dpGfx(&db, 0xE7000000, 0);
            dpGfx(&db, 0xBA001402, 0x00100000);
            dpGfx(&db, 0xB900031D, 0x0C182078);
            dpGfx(&db, 0xFC26A004, 0x1F1093FF);
            dpGfx(&db, 0xBA001001, 0x00010000);
            dpGfx(&db, 0xBB002801, 0xFFFFFFFF);
            dpGfx(&db, sMats[m].c0, sMats[m].tex);
            dpGfx(&db, 0xBA001102, 0);
            dpGfx(&db, 0xBA000C02, 0x00002000);
            dpGfx(&db, 0xB6000000, 0x00003000);
            dpGfx(&db, 0xFB000000, 0x000000FF);
            first = 0;
        }
        else
        {
            dpGfx(&db, sMats[m].c0, sMats[m].tex);
        }
        for (i = 0; i <= sNGT; i++)
        {
            if (i < sNGT && gt[i].mat == m)
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
                        DpV *v = &gt[batch[b]].v[j];
                        f32 rx = v->x - S.ox, ry = v->y - S.oy, rz = v->z - S.oz;

                        dpPut(&vb, (u16) (s16) lroundf(rx), 2);
                        dpPut(&vb, (u16) (s16) lroundf(ry), 2);
                        dpPut(&vb, (u16) (s16) lroundf(rz), 2);
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
    free(gt);
    sGT = NULL;
    if (vb.n > vb.cap || db.n > db.cap)
    {
        printf("ge: dam+: room %d buffer overflow - motor pool off\n", room);
        return 0;
    }
    S.vtx[ri] = dpPack1172(vb.b, vb.n, &S.vtxBytes[ri]);
    S.gdl[ri] = dpPack1172(db.b, db.n, &S.gdlBytes[ri]);
    printf("ge: dam+: room %d: %d triangles, %d vertices, %d DL bytes\n", room, sNGT, nv, db.n);
    return 1;
}

static bg_portal_entry *dpPortal(f32 u0, f32 u1, f32 v, f32 h)
{
    bg_portal_entry *p = mempAllocBytesInBank(sizeof(bg_portal_entry) + 4 * sizeof(coord3d), MEMPOOL_STAGE);
    coord3d *pt = &p->point;
    f32 f[3];

    memset(p, 0, sizeof(bg_portal_entry) + 4 * sizeof(coord3d));
    p->numPoints = 4;
    dpFile(u0, v, 0, f);   pt[0].f[0] = f[0]; pt[0].f[1] = f[1]; pt[0].f[2] = f[2];
    dpFile(u1, v, 0, f);   pt[1].f[0] = f[0]; pt[1].f[1] = f[1]; pt[1].f[2] = f[2];
    dpFile(u1, v, h, f);   pt[2].f[0] = f[0]; pt[2].f[1] = f[1]; pt[2].f[2] = f[2];
    dpFile(u0, v, h, f);   pt[3].f[0] = f[0]; pt[3].f[1] = f[1]; pt[3].f[2] = f[2];
    return p;
}

/* load_bg_file, right after the stock stan is loaded. levelscale: this stage's. */
void portDamPlusStan(f32 levelscale)
{
    extern struct DpPrefix *stan_prefix;
    struct DpPrefix *prefix = stan_prefix;
    f32 P[3], Up[3], ulo, uhi, height, ul;

    memset(&S, 0, sizeof(S));
    if (!(portDamPlusParts() & DP_MOTORPOOL) || prefix == NULL || prefix->first == NULL)
    {
        return;
    }
    S.ls = levelscale;
    if (!dpDoorPad(P, Up, &ulo, &uhi, &height))
    {
        return;
    }
    Up[1] = 0.0f;
    ul = sqrtf(Up[0] * Up[0] + Up[2] * Up[2]);
    if (ul < 0.5f)
    {
        printf("ge: dam+: door %d is not upright - motor pool off\n", DP_DOOR);
        return;
    }
    Up[0] /= ul;
    Up[2] /= ul;
    if (!dpFindSeam(prefix->first, P, Up, ulo, uhi))
    {
        return;
    }
    /* the frame: origin A, u towards E, v away from the stock tile's interior */
    {
        f32 dx = S.seam[2][0] - S.seam[0][0], dz = S.seam[2][2] - S.seam[0][2];
        f32 len = sqrtf(dx * dx + dz * dz);
        StandTile *t = S.seamTile[0];
        s32 c = t->tail.hdrTail.pointCount & 0xf, e = (S.seamEdge[0] + 2) % c;
        f32 side;

        S.ox = S.seam[0][0];
        S.oy = S.seam[0][1];
        S.oz = S.seam[0][2];
        S.ux = dx / len;
        S.uz = dz / len;
        S.vx = -S.uz;
        S.vz = S.ux;
        side = (t->points[e].x - S.ox) * S.vx + (t->points[e].z - S.oz) * S.vz;
        if (side > 0.0f)
        {
            S.vx = -S.vx;
            S.vz = -S.vz;
        }
        S.W = len / S.ls;
        S.bu = ((S.seam[1][0] - S.ox) * S.ux + (S.seam[1][2] - S.oz) * S.uz) / S.ls;
        S.doorH = height / S.ls;
    }
    printf("ge: dam+: shutter seam: room %d, %.0f world units wide (B at %.0f), %.0f high\n", S.seamRoom, S.W, S.bu, S.doorH);
    if (S.W < 600.0f || S.W > 1300.0f)
    {
        printf("ge: dam+: the shutter is not the expected size - motor pool off\n");
        return;
    }
    if (!dpBuildStan(prefix))
    {
        return;
    }
    if (!dpBuildRoomData(0, DP_ROOM_BAY) || !dpBuildRoomData(1, DP_ROOM_BARRACKS))
    {
        return;
    }
    S.portal[0] = dpPortal(0.0f, S.W, 0.0f, fmaxf(S.doorH, 300.0f));
    S.portal[1] = dpPortal(-1000.0f, -800.0f, 2000.0f, 230.0f);
    /* the extended stan goes live: the stock loader's own two steps on our block */
    stanDetermineEOF(S.stanBlock, 0, S.stanBlock);
    stanLoadFile(S.stanBlock);
    S.ok = 1;
}

/* load_bg_file, after the stock room-size loop and before initializeRoomData: the extended room and portal tables */
void portDamPlusBgRooms(void)
{
    s32 nstock = g_MaxNumRooms;        /* rooms 1..N-1 and the end entry N */
    bg_room_data *rt;
    bg_portal_data_entry *pt;
    s32 i, np;
    s32 ids[2] = { DP_ROOM_BAY, DP_ROOM_BARRACKS };

    if (!S.ok)
    {
        return;
    }
    if (nstock != DP_ROOM_BAY || DP_ROOM_BARRACKS + 2 >= MAXROOMCOUNT)
    {
        printf("ge: dam+: the stock bg has %d room entries, not %d - motor pool off (stan already extended: rooms 137/138 have no "
               "gfx)\n", nstock, DP_ROOM_BAY);
        S.ok = 0;
        return;
    }
    rt = mempAllocBytesInBank((nstock + 4) * sizeof(bg_room_data), MEMPOOL_STAGE);
    memset(rt, 0, (nstock + 4) * sizeof(bg_room_data));
    memcpy(rt, ptr_bgdata_room_fileposition_list, nstock * sizeof(bg_room_data));
    rt[nstock + 2] = ptr_bgdata_room_fileposition_list[nstock];   /* the end entry moves past ours */
    for (i = 0; i < 2; i++)
    {
        s32 r = ids[i];

        rt[r].pPointTableBin = S.vtx[i];
        rt[r].pPriMappingBin = S.gdl[i];
        rt[r].pSecMappingBin = NULL;
        rt[r].pos.f[0] = S.ox;
        rt[r].pos.f[1] = S.oy;
        rt[r].pos.f[2] = S.oz;
        g_BgRoomInfo[r].model_bin_loaded = 0;
        g_BgRoomInfo[r].field_35 = 0;
        g_BgRoomInfo[r].csize_point_index_binary = S.vtxBytes[i];
        g_BgRoomInfo[r].csize_primary_DL_binary = S.gdlBytes[i];
        g_BgRoomInfo[r].csize_secondary_DL_binary = 0;
        g_BgRoomInfo[r].cur_room_totalsize = -1;
    }
    ptr_bgdata_room_fileposition_list = rt;
    g_MaxNumRooms = nstock + 2;

    for (np = 0; g_BgPortals[np].offset_portal != NULL; np++)
    {
    }
    if (np + 2 >= PORTMAX)
    {
        printf("ge: dam+: %d stock portals: no room for 2 more under PORTMAX %d\n", np, PORTMAX);
    }
    pt = mempAllocBytesInBank((np + 3) * sizeof(bg_portal_data_entry), MEMPOOL_STAGE);
    memset(pt, 0, (np + 3) * sizeof(bg_portal_data_entry));
    memcpy(pt, g_BgPortals, np * sizeof(bg_portal_data_entry));
    pt[np].offset_portal = S.portal[0];
    pt[np].connectedRoom1 = (u8) S.seamRoom;
    pt[np].connectedRoom2 = DP_ROOM_BAY;
    pt[np + 1].offset_portal = S.portal[1];
    pt[np + 1].connectedRoom1 = DP_ROOM_BAY;
    pt[np + 1].connectedRoom2 = DP_ROOM_BARRACKS;
    g_BgPortals = pt;
    printf("ge: dam+: bg: rooms %d, %d added (%d room entries), portals %d (%d<->%d) and %d (%d<->%d)\n", DP_ROOM_BAY,
           DP_ROOM_BARRACKS, g_MaxNumRooms, np, S.seamRoom, DP_ROOM_BAY, np + 1, DP_ROOM_BAY, DP_ROOM_BARRACKS);
    fflush(stdout);
}

#endif
