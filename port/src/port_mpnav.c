/* port_mpnav.c - a path graph for a stock multiplayer map run with the arena (tools/arena_maps.lua; modding/arena.md "Maps").
 * The MP setups (Ump_setup*Z) ship no waypoints: GoldenEye has no bots, so nothing on an MP map ever walks a route. The arena's
 * enemies chase Bond by plot_course_for_actor, which takes a straight line when one is clear (portArenaDirect, chraction.c) and a
 * waypoint route otherwise, so without a graph they would stand still behind the first wall. The map's own pads are too few and too
 * far apart to chain (a library of 62 pads links 8 of them), so the graph is made from the FLOOR (the stan tiles):
 *   1. nodes: every pad that has a floor tile under it, then the centre of every floor tile and a grid across the big ones (a tile can
 *      be a whole room), each a little way in from the walls, on a spot clear of props (see spotFree). The new nodes are appended to the
 *      stage's pad table as pads of their own (named "mpnav"; the map's pads keep their numbers, pads.base() says how many there are),
 *      so a spawn can use them too. Ladders and crawl spaces are not walkable;
 *   2. links: nodes on one tile are linked, and so are the nodes of two tiles the floor joins (the nearest two, or through a node on the
 *      edge the tiles share when a straight walk is not clear; the step between them may be no more than MP_NAV_STEP); then, for a short
 *      route, a longer link between two nodes that a chr-wide straight walk joins clear both ways (the stan tiles and the props). No link
 *      is longer than MP_NAV_MAXLINK (see there);
 *   3. the largest connected piece becomes the stage's waypoint table, ONE waypoint per group (as tools/gen_colosseum.py does: the
 *      group-level route is then the whole route and a chr never zig-zags on a group border). Pads outside that piece are not
 *      waypoints, so pads.far_from(.., true) lists only pads a chr can walk to from the others.
 * portMpNavNearest picks the waypoint a route starts from or heads for (chraction.c chrlvStanPathRelated).
 * portMpNavBuild runs once at the end of a stage load (lv.c, after the props exist) for a stage loaded by an MP-setup name
 * (g_PortMpSetup, front.c). Deterministic (tile order, no RNG), identical on every machine; the tables live in the stage pool like a
 * ROM setup's. PC only. */
#ifndef TARGET_N64

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ultra64.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include "memp.h"
#include "game/chrai.h"
#include "game/stan.h"

#define MP_NAV_GRID    260.0f    /* across a big tile, a node is tried every this far */
#define MP_NAV_INSET   55.0f     /* a node is at least this far in from its tile's edges (a tile with no such point gets its centre) */
#define MP_NAV_SAME    240.0f    /* two nodes on one tile are not closer than this (the tile's centre and the pads count) */
#define MP_NAV_ONTILE  460.0f    /* nodes on one tile are linked when this near */
#define MP_NAV_SHORT   620.0f    /* longest straight-walk link */
/* the longest link of any kind: an offscreen chr hops along a link at about 330 units a second and the game restarts a chr's course every
 * 3 s (182 ticks), so a link must be walked in well under that or the chr never arrives (a 1300-unit link held every chaser that took it) */
#define MP_NAV_MAXLINK 640.0f
#define MP_NAV_STEP    60.0f     /* highest step at the edge between two joined tiles */
#define MP_NAV_RISE    140.0f    /* highest rise of a straight-walk link */
#define MP_NAV_DEG     20        /* most links a node has */
#define MP_NAV_MAX     5200      /* most nodes */
#define MP_NAV_WIDTH   40.0f     /* the corridor tested either side of a straight-walk link (a chr is 20 wide and runs wide on a corner) */

extern int printf(const char *, ...);
extern s32 ptr_firstroom_0;      /* stan.c: the tile file */
extern f32 inv_level_scale;
extern u8 list_of_tilesizes[];
extern StandTile *standTileStart;

typedef struct { float x, y, z; StandTile *tile; int pad; int ti; } NavNode;   /* pad: the map's pad number, or -1 for a floor node; ti: the tile's number */

int g_PortMpNavBase = 0;         /* the map's own pads (the nodes made from tiles come after them); 0 = no graph was built */
const void *g_PortMpNavPads = NULL;   /* the pad table this made (port_blocks.c portPadCount: it is not followed by the bound pads) */
int g_PortMpNavCount = 0;        /* its length, the terminator included (as portPadCount counts) */

#define MP_NAV_CD (CDTYPE_OBJS | CDTYPE_PATHBLOCKER)   /* what blocks a straight-walk link besides the floor's edge */

static StandTile *tileNext(StandTile *t)
{
    return (StandTile *) ((u8 *) t + list_of_tilesizes[t->tail.hdrTail.pointCount & 0xf]);
}

static int tileCount(const StandTile *t) { return t->tail.hdrTail.pointCount & 0xf; }

static StandTile *tileLink(const StandTile *t, int i)
{
    u16 link = t->points[i].link;

    return (link >> 4) != 0 ? (StandTile *) ((u8 *) standTileStart + ((u32) link << 3)) : NULL;
}

/* the height of the edge of tile a that leads to tile b, and of b's edge back: 1 when a chr can step between them */
static int stepOk(const StandTile *a, int k, const StandTile *b)
{
    int ca = tileCount(a), cb = tileCount(b), q;
    float ya = ((float) a->points[k].y + (float) a->points[(k + 1) % ca].y) * 0.5f * inv_level_scale, yb = ya;

    for (q = 0; q < cb; q++)
    {
        if (tileLink(b, q) == a)
        {
            yb = ((float) b->points[q].y + (float) b->points[(q + 1) % cb].y) * 0.5f * inv_level_scale;
            break;
        }
    }
    return fabsf(ya - yb) <= MP_NAV_STEP;
}

static int lineOk(StandTile *from, float x1, float z1, float x2, float z2, StandTile **end)
{
    StandTile *t = from;
    int ok = stanTestLineUnobstructed(&t, x1, z1, x2, z2, MP_NAV_CD, 165.0f, 20.0f, 0.0f, 1.0f);

    if (end != NULL) *end = t;
    return ok;
}

/* a chr-wide straight walk from node a to node b: 0 = clear, else why not (1 = the floor ends or a prop is in the way of the
 * centre line, 2 = it does not end on b's tile, 3 = a side line is blocked) */
static int navClear(const NavNode *a, const NavNode *b)
{
    StandTile *end;
    float dx = b->x - a->x, dz = b->z - a->z;
    float len = sqrtf(dx * dx + dz * dz), ox, oz;

    if (len < 1.0f) return 0;
    ox = MP_NAV_WIDTH * dz / len;    /* the offset to the side: the line's normal */
    oz = -MP_NAV_WIDTH * dx / len;
    if (!lineOk(a->tile, a->x, a->z, b->x, b->z, &end)) return 1;
    if (end == NULL || !isPointInsideTriStandTileUnscaled_Maybe(end, b->x, b->z)) return 2;
    if (!lineOk(a->tile, a->x, a->z, a->x + ox, a->z + oz, NULL) || !lineOk(a->tile, a->x + ox, a->z + oz, b->x + ox, b->z + oz, NULL)) return 3;
    if (!lineOk(a->tile, a->x, a->z, a->x - ox, a->z - oz, NULL) || !lineOk(a->tile, a->x - ox, a->z - oz, b->x - ox, b->z - oz, NULL)) return 3;
    return 0;
}

static int cmpNodeX(const void *a, const void *b)
{
    const NavNode *p = (const NavNode *) a, *q = (const NavNode *) b;

    return (p->x > q->x) - (p->x < q->x);
}

typedef struct { NavNode *nd; int nn; int *nb; unsigned char *dg; } NavGraph;

static int spotFree(StandTile *t, float x, float y, float z);

/* a node on the edge k of tile ti (the edge it shares with a tile it is joined to), a few units in on ti's side; made once per edge
 * (portal[ti * 16 + k]); -1 when there is no room or the point is not on the tile */
static int portalNode(NavGraph *g, int *portal, StandTile **tiles, int ti, int k)
{
    StandTile *t = tiles[ti];
    int c = tileCount(t), q;
    float ax = (float) t->points[k].x * inv_level_scale, az = (float) t->points[k].z * inv_level_scale;
    float bx = (float) t->points[(k + 1) % c].x * inv_level_scale, bz = (float) t->points[(k + 1) % c].z * inv_level_scale;
    float cx = 0.0f, cz = 0.0f, mx = (ax + bx) * 0.5f, mz = (az + bz) * 0.5f, dx, dz, len;

    if (portal[ti * 16 + k] >= 0) return portal[ti * 16 + k];
    if (g->nn >= MP_NAV_MAX) return -1;
    for (q = 0; q < c; q++) { cx += (float) t->points[q].x * inv_level_scale; cz += (float) t->points[q].z * inv_level_scale; }
    dx = cx / (float) c - mx; dz = cz / (float) c - mz;
    len = sqrtf(dx * dx + dz * dz);
    if (len > 1.0f) { mx += dx / len * 8.0f; mz += dz / len * 8.0f; }
    if (!isPointInsideTriStandTileUnscaled_Maybe(t, mx, mz) || !spotFree(t, mx, stanGetPositionYValue(t, mx, mz), mz)) return -1;
    g->nd[g->nn].x = mx; g->nd[g->nn].y = stanGetPositionYValue(t, mx, mz); g->nd[g->nn].z = mz;
    g->nd[g->nn].tile = t; g->nd[g->nn].pad = -1; g->nd[g->nn].ti = -1;
    portal[ti * 16 + k] = g->nn;
    return g->nn++;
}

static int hasEdge(const NavGraph *g, int u, int v)
{
    int k;

    for (k = 0; k < g->dg[u]; k++) if (g->nb[u * MP_NAV_DEG + k] == v) return 1;
    return 0;
}

static void addEdge(NavGraph *g, int u, int v)
{
    float dx = g->nd[v].x - g->nd[u].x, dz = g->nd[v].z - g->nd[u].z;

    if (u == v || dx * dx + dz * dz > MP_NAV_MAXLINK * MP_NAV_MAXLINK || hasEdge(g, u, v) || g->dg[u] >= MP_NAV_DEG || g->dg[v] >= MP_NAV_DEG) return;
    g->nb[u * MP_NAV_DEG + g->dg[u]++] = v;
    g->nb[v * MP_NAV_DEG + g->dg[v]++] = u;
}

extern u8 g_StanTileSpecialFlags[];   /* stan.c */

/* a tile a chr cannot use: a ladder, or a crawl space (it would have to crouch) */
static int tileUnwalkable(const StandTile *t)
{
    return (g_StanTileSpecialFlags[STAN_MID_NIB(t->mid.half, 12)] & (STANTILEFLAG_LADDER | STANTILEFLAG_FORCECROUCH)) != 0;
}

extern bool chrlvIsPosClearOfObjectBounds(coord3d *pos, StandTile *stan);   /* chraction.c */

/* a spot a chr can stand on: the volume of a chr there is clear of props, and the point is outside every object's footprint. An offscreen
 * chr hops from waypoint to waypoint (chraction.c chrlvTravelTickMagic) and refuses to land on a spot that fails these tests, so a node
 * in a statue, a crate or a weapon on its pad would hold every chr that routes through it for good. */
static int spotFree(StandTile *t, float x, float y, float z)
{
    StandTile *tt = t;
    coord3d p;

    p.f[0] = x; p.f[1] = y; p.f[2] = z;
    return stanTestVolume(&tt, x, z, 28.0f, CDTYPE_OBJS | CDTYPE_PATHBLOCKER, 0.0f, 1.0f) < 0 && chrlvIsPosClearOfObjectBounds(&p, t);
}

/* the point is on tile t and at least r from every edge of it that is a WALL (an edge the floor does not continue over: no link). The edges
 * shared with another tile do not count: a long room is two triangles, and its middle is the diagonal. */
static int insetOk(StandTile *t, float x, float z, float r)
{
    int c = tileCount(t), k;

    if (!isPointInsideTriStandTileUnscaled_Maybe(t, x, z)) return 0;
    if (r <= 0.0f) return 1;
    for (k = 0; k < c; k++)
    {
        float ax, az, bx, bz, dx, dz, len2, u, qx, qz;

        if (tileLink(t, k) != NULL) continue;
        ax = (float) t->points[k].x * inv_level_scale; az = (float) t->points[k].z * inv_level_scale;
        bx = (float) t->points[(k + 1) % c].x * inv_level_scale; bz = (float) t->points[(k + 1) % c].z * inv_level_scale;
        dx = bx - ax; dz = bz - az;
        len2 = dx * dx + dz * dz;
        u = (len2 > 0.0f) ? ((x - ax) * dx + (z - az) * dz) / len2 : 0.0f;
        u = u < 0.0f ? 0.0f : (u > 1.0f ? 1.0f : u);
        qx = ax + u * dx - x; qz = az + u * dz - z;
        if (qx * qx + qz * qz < r * r) return 0;
    }
    return 1;
}

/* a floor node at (x, z) on tile t (number ti) if the point is a little way in from the tile's edges and no node of the tile is near it;
 * `centre`: the tile's centre, which takes a smaller margin (then none) when the tile has no room for MP_NAV_INSET, so every tile has a node */
static void navTry(NavGraph *g, StandTile *t, int ti, float x, float z, int centre)
{
    NavNode *nd = g->nd;
    float y;
    int j;

    if (g->nn >= MP_NAV_MAX) return;
    if (!insetOk(t, x, z, MP_NAV_INSET) && !(centre && (insetOk(t, x, z, MP_NAV_INSET * 0.4f) || insetOk(t, x, z, 0.0f)))) return;
    y = stanGetPositionYValue(t, x, z);
    for (j = 0; j < g->nn; j++)
    {
        float dx, dz;

        if (nd[j].tile != t) continue;
        dx = nd[j].x - x; dz = nd[j].z - z;
        if (dx * dx + dz * dz < MP_NAV_SAME * MP_NAV_SAME) return;
    }
    if (!spotFree(t, x, y, z)) return;
    nd[g->nn].x = x; nd[g->nn].y = y; nd[g->nn].z = z; nd[g->nn].tile = t; nd[g->nn].pad = -1; nd[g->nn].ti = ti;
    g->nn++;
}

/* chraction.c chrlvStanPathRelated, the waypoint a route starts from or heads for, for a position: on a map this file built the graph for, the nearest
 * few waypoints, the first with a clear straight walk from the position (else the nearest). The stock pick (the first waypoint on the tile the
 * position floods out to, then the LAST neighbour of it that is nearer) sent a chr back to the node it had just left: the chasers ping-ponged
 * between two nodes. NULL when this stage has no such graph (the stock pick then runs). */
waypoint *portMpNavNearest(coord3d *pos, StandTile *stan)
{
    waypoint *w = g_CurrentSetup.pathwaypoints;
    const PadRecord *pads = g_CurrentSetup.pads;
    struct { float d; waypoint *w; } best[8];
    int nb = 0, i, j;

    if (w == NULL || pads == NULL || (const void *) pads != g_PortMpNavPads)
    {
        return NULL;
    }
    for (i = 0; w[i].padID >= 0; i++)
    {
        const PadRecord *p = &pads[w[i].padID];
        float dx = p->pos.f[0] - pos->f[0], dz = p->pos.f[2] - pos->f[2], d = dx * dx + dz * dz;

        if (fabsf(p->pos.f[1] - pos->f[1]) > 420.0f) d += 1.0e9f;   /* (another storey: only when nothing else is near) */
        if (nb == 8 && d >= best[7].d) continue;
        j = (nb < 8) ? nb++ : 7;
        while (j > 0 && best[j - 1].d > d) { best[j] = best[j - 1]; j--; }
        best[j].d = d; best[j].w = &w[i];
    }
    if (nb == 0)
    {
        return NULL;
    }
    for (i = 0; stan != NULL && i < nb; i++)
    {
        const PadRecord *p = &pads[best[i].w->padID];
        StandTile *t = stan;

        if (stanTestLineUnobstructed(&t, pos->f[0], pos->f[2], p->pos.f[0], p->pos.f[2], CDTYPE_OBJS | CDTYPE_PATHBLOCKER, 165.0f, 20.0f, 0.0f, 1.0f))
        {
            return best[i].w;
        }
    }
    return best[0].w;
}

/* Builds the waypoint table of the loaded MP map; returns the number of waypoints (0 = none built, the setup is left as it was). */
int portMpNavBuild(void)
{
    static const char kName[] = "mpnav";
    PadRecord *pads = g_CurrentSetup.pads, *np;
    NavGraph G;
    NavNode *nd;
    StandTile **tiles = NULL, *t;
    int *tStart = NULL, *tOrder = NULL, *portal = NULL, portals = 0, nodes0;
    int *comp = NULL, *map = NULL, *stack = NULL;
    waypoint *wp;
    waygroup *grp;
    s32 *lists, *members;
    int n = 0, nt = 0, i, j, k, q, comps = 0, best = -1, bestn = 0, m, e, extra, kept = 0, shortOk = 0, shortTried = 0, adjLinks = 0, padNodes = 0;
    int refused[4] = { 0, 0, 0, 0 };

    g_PortMpNavBase = 0;
    g_PortMpNavPads = NULL;
    g_PortMpNavCount = 0;
    if (pads == NULL || ptr_firstroom_0 == 0 || (g_CurrentSetup.pathwaypoints != NULL && g_CurrentSetup.pathwaypoints[0].padID >= 0))
    {
        return 0;   /* no pads or floor, or the setup has a graph of its own */
    }
    while (pads[n].plink != NULL) n++;
    for (t = (StandTile *) ptr_firstroom_0; *(u32 *) t != 0; t = tileNext(t)) nt++;
    G.nd = nd = (NavNode *) malloc(sizeof(NavNode) * MP_NAV_MAX);
    G.nb = (int *) malloc(sizeof(int) * MP_NAV_MAX * MP_NAV_DEG);
    G.dg = (unsigned char *) calloc(MP_NAV_MAX, 1);
    G.nn = 0;
    tiles = (StandTile **) malloc(sizeof(StandTile *) * (size_t) (nt + 1));
    tStart = (int *) calloc((size_t) nt + 2, sizeof(int));
    comp = (int *) malloc(sizeof(int) * MP_NAV_MAX);
    map = (int *) malloc(sizeof(int) * MP_NAV_MAX);
    stack = (int *) malloc(sizeof(int) * MP_NAV_MAX);
    tOrder = (int *) malloc(sizeof(int) * MP_NAV_MAX);
#define NAV_FREE() do { free(nd); free(G.nb); free(G.dg); free(tiles); free(tStart); free(tOrder); free(comp); free(map); free(stack); free(portal); } while (0)
    if (!nd || !G.nb || !G.dg || !tiles || !tStart || !comp || !map || !stack || !tOrder || nt == 0)
    {
        NAV_FREE();
        return 0;
    }
    for (t = (StandTile *) ptr_firstroom_0, i = 0; *(u32 *) t != 0; t = tileNext(t)) tiles[i++] = t;   /* (ascending addresses) */
    /* 1. nodes: the map's pads that have a floor, then each tile's centre and a grid across it */
    for (i = 0; i < n && G.nn < MP_NAV_MAX; i++)
    {
        StandTile *s = pads[i].stan;

        if (s != NULL && s->id != 0 && !tileUnwalkable(s) && isPointInsideTriStandTileUnscaled_Maybe(s, pads[i].pos.f[0], pads[i].pos.f[2])
            && spotFree(s, pads[i].pos.f[0], pads[i].pos.f[1], pads[i].pos.f[2]))
        {
            nd[G.nn].x = pads[i].pos.f[0]; nd[G.nn].y = pads[i].pos.f[1]; nd[G.nn].z = pads[i].pos.f[2]; nd[G.nn].tile = s; nd[G.nn].pad = i;
            nd[G.nn].ti = -1;
            G.nn++;
        }
    }
    padNodes = G.nn;
    for (i = 0; i < nt && G.nn < MP_NAV_MAX; i++)
    {
        int c = tileCount(tiles[i]);
        float cx = 0.0f, cz = 0.0f, minx = 1e9f, maxx = -1e9f, minz = 1e9f, maxz = -1e9f;
        int ix, iz;

        t = tiles[i];
        if (c < 3 || tileUnwalkable(t)) continue;
        for (k = 0; k < c; k++)
        {
            float px = (float) t->points[k].x * inv_level_scale, pz = (float) t->points[k].z * inv_level_scale;

            cx += px; cz += pz;
            if (px < minx) minx = px;
            if (px > maxx) maxx = px;
            if (pz < minz) minz = pz;
            if (pz > maxz) maxz = pz;
        }
        navTry(&G, t, i, cx / (float) c, cz / (float) c, 1);
        if (maxx - minx > MP_NAV_GRID || maxz - minz > MP_NAV_GRID)
        {
            for (ix = (int) (minx / MP_NAV_GRID) - 1; ix <= (int) (maxx / MP_NAV_GRID) + 1; ix++)
            {
                for (iz = (int) (minz / MP_NAV_GRID) - 1; iz <= (int) (maxz / MP_NAV_GRID) + 1; iz++)
                {
                    navTry(&G, t, i, (float) ix * MP_NAV_GRID, (float) iz * MP_NAV_GRID, 0);
                }
            }
        }
    }
    for (i = 0; i < padNodes; i++)   /* the pad nodes' tile numbers (binary search over the ascending tile addresses) */
    {
        int lo = 0, hi = nt - 1;

        while (lo <= hi)
        {
            int mid = (lo + hi) / 2;

            if (tiles[mid] == nd[i].tile) { nd[i].ti = mid; break; }
            if ((u8 *) tiles[mid] < (u8 *) nd[i].tile) lo = mid + 1; else hi = mid - 1;
        }
    }
    /* nodes by tile: tOrder lists the node numbers tile by tile, tStart[tile] is where a tile's begin */
    for (i = 0; i < G.nn; i++) if (nd[i].ti >= 0) tStart[nd[i].ti + 1]++;
    for (i = 0; i < nt; i++) tStart[i + 1] += tStart[i];
    {
        int *fill = (int *) calloc((size_t) nt + 1, sizeof(int));

        if (fill == NULL) { NAV_FREE(); return 0; }
        for (i = 0; i < G.nn; i++) if (nd[i].ti >= 0) tOrder[tStart[nd[i].ti] + fill[nd[i].ti]++] = i;
        free(fill);
    }
    /* 2a. the floor's own links: the nodes of one tile, and the nearest two nodes of each tile the floor joins */
    portal = (int *) malloc(sizeof(int) * (size_t) nt * 16);
    if (portal == NULL) { NAV_FREE(); return 0; }
    for (i = 0; i < nt * 16; i++) portal[i] = -1;
    nodes0 = G.nn;
    for (i = 0; i < nodes0; i++)
    {
        int ti = nd[i].ti, c;

        if (ti < 0) continue;
        c = tileCount(tiles[ti]);
        for (j = tStart[ti]; j < tStart[ti + 1]; j++)
        {
            int v = tOrder[j];
            float dx = nd[v].x - nd[i].x, dz = nd[v].z - nd[i].z;

            if (v > i && dx * dx + dz * dz <= MP_NAV_ONTILE * MP_NAV_ONTILE) { addEdge(&G, i, v); adjLinks++; }
        }
        for (k = 0; k < c; k++)
        {
            StandTile *lt = tileLink(tiles[ti], k);
            int lti = -1, lo = 0, hi = nt - 1, b1 = -1, b2 = -1;
            float d1 = 1e30f, d2 = 1e30f;

            if (lt == NULL) continue;
            while (lo <= hi)
            {
                int mid = (lo + hi) / 2;

                if (tiles[mid] == lt) { lti = mid; break; }
                if ((u8 *) tiles[mid] < (u8 *) lt) lo = mid + 1; else hi = mid - 1;
            }
            if (lti < 0 || tileUnwalkable(lt) || !stepOk(tiles[ti], k, lt)) continue;
            for (j = tStart[lti]; j < tStart[lti + 1]; j++)
            {
                int v = tOrder[j];
                float dx = nd[v].x - nd[i].x, dz = nd[v].z - nd[i].z, d = dx * dx + dz * dz;

                if (d < d1) { d2 = d1; b2 = b1; d1 = d; b1 = v; }
                else if (d < d2) { d2 = d; b2 = v; }
            }
            /* the nearest node of the next tile: a straight walk if one is clear; else through the middle of the edge the two tiles share (a
             * tile is convex, so a node to a point on its own edge is always a clear walk: the floor really joins them there) */
            if (b1 >= 0)
            {
                if (navClear(&nd[i], &nd[b1]) == 0 && navClear(&nd[b1], &nd[i]) == 0) addEdge(&G, i, b1);
                else
                {
                    int p = portalNode(&G, portal, tiles, ti, k);

                    if (p >= 0) { addEdge(&G, i, p); addEdge(&G, p, b1); portals++; }
                }
                adjLinks++;
            }
            if (b2 >= 0 && navClear(&nd[i], &nd[b2]) == 0 && navClear(&nd[b2], &nd[i]) == 0) { addEdge(&G, i, b2); adjLinks++; }
        }
    }
    /* 2b. straight-walk links for shorter routes: sweep the nodes by x */
    {
        int *byx = (int *) malloc(sizeof(int) * (size_t) G.nn);
        NavNode *sorted = (NavNode *) malloc(sizeof(NavNode) * (size_t) G.nn);

        if (byx == NULL || sorted == NULL) { free(byx); free(sorted); NAV_FREE(); return 0; }
        for (i = 0; i < G.nn; i++) { sorted[i] = nd[i]; sorted[i].pad = i; }   /* (pad: the node's number while sorted) */
        qsort(sorted, (size_t) G.nn, sizeof(NavNode), cmpNodeX);
        for (i = 0; i < G.nn; i++) byx[i] = sorted[i].pad;
        free(sorted);
        for (i = 0; i < G.nn; i++)
        {
            for (k = i + 1; k < G.nn; k++)
            {
                int u = byx[i], v = byx[k], why;
                float dx, dz;

                dx = nd[v].x - nd[u].x;
                if (dx > MP_NAV_SHORT) break;
                dz = nd[v].z - nd[u].z;
                if (dx * dx + dz * dz > MP_NAV_SHORT * MP_NAV_SHORT || fabsf(nd[v].y - nd[u].y) > MP_NAV_RISE) continue;
                if ((nd[u].ti >= 0 && nd[u].ti == nd[v].ti) || hasEdge(&G, u, v) || G.dg[u] >= MP_NAV_DEG || G.dg[v] >= MP_NAV_DEG) continue;
                shortTried++;
                why = navClear(&nd[u], &nd[v]);
                if (why == 0) why = navClear(&nd[v], &nd[u]);
                if (why == 0) { addEdge(&G, u, v); shortOk++; }
                else refused[why]++;
            }
        }
        free(byx);
    }
    for (i = 0; i < G.nn; i++) comp[i] = -1;
    for (i = 0; i < G.nn; i++)   /* connected pieces; the largest wins (ties: the one met first) */
    {
        int sp = 0, size = 0;

        if (comp[i] >= 0) continue;
        comp[i] = comps;
        stack[sp++] = i;
        while (sp > 0)
        {
            int u = stack[--sp];

            size++;
            for (k = 0; k < G.dg[u]; k++)
            {
                int v = G.nb[u * MP_NAV_DEG + k];

                if (comp[v] < 0) { comp[v] = comps; stack[sp++] = v; }
            }
        }
        if (size > bestn) { bestn = size; best = comps; }
        comps++;
    }
    m = 0;
    extra = 0;
    for (i = 0; i < G.nn; i++)
    {
        map[i] = (comp[i] == best) ? m++ : -1;
        if (map[i] >= 0 && nd[i].pad < 0) extra++;
    }
    printf("ge: mpnav: %d tiles, %d nodes (%d from the map's %d pads, %d on tile edges); links: %d on the floor, %d straight-walk of %d tried (refused: %d floor/prop, %d tile, %d side)\n",
           nt, G.nn, padNodes, n, portals, adjLinks, shortOk, shortTried, refused[1], refused[2], refused[3]);
    if (m < 2)
    {
        printf("ge: mpnav: no usable graph\n");
        NAV_FREE();
        return 0;
    }
    for (i = 0; i < G.nn; i++) if (map[i] >= 0) kept += G.dg[i];
    kept /= 2;
    np = (PadRecord *) mempAllocBytesInBank((u32) (sizeof(PadRecord) * (size_t) (n + extra + 1)), MEMPOOL_STAGE);
    wp = (waypoint *) mempAllocBytesInBank((u32) (sizeof(waypoint) * (size_t) (m + 1)), MEMPOOL_STAGE);
    grp = (waygroup *) mempAllocBytesInBank((u32) (sizeof(waygroup) * (size_t) (m + 1)), MEMPOOL_STAGE);
    lists = (s32 *) mempAllocBytesInBank((u32) (sizeof(s32) * (size_t) (kept * 2 + m)), MEMPOOL_STAGE);
    members = (s32 *) mempAllocBytesInBank((u32) (sizeof(s32) * 2 * (size_t) m), MEMPOOL_STAGE);
    if (np == NULL || wp == NULL || grp == NULL || lists == NULL || members == NULL)
    {
        NAV_FREE();
        return 0;
    }
    memcpy(np, pads, sizeof(PadRecord) * (size_t) (n + 1));   /* the map's pads, their terminator included, then the floor nodes over it */
    k = n;
    for (i = 0; i < G.nn; i++)
    {
        if (map[i] >= 0 && nd[i].pad < 0)
        {
            np[k].pos.f[0] = nd[i].x; np[k].pos.f[1] = nd[i].y; np[k].pos.f[2] = nd[i].z;
            np[k].up.f[0] = 0.0f; np[k].up.f[1] = 1.0f; np[k].up.f[2] = 0.0f;
            np[k].look.f[0] = 0.0f; np[k].look.f[1] = 0.0f; np[k].look.f[2] = 1.0f;
            np[k].plink = (char *) kName;
            np[k].stan = nd[i].tile;
            nd[i].pad = k++;
        }
    }
    np[k] = pads[n];   /* the terminator */
    e = 0;
    for (i = 0; i < G.nn; i++)
    {
        int w = map[i];

        if (w < 0) continue;
        wp[w].padID = nd[i].pad;
        wp[w].neighbours = &lists[e];
        for (q = 0; q < G.dg[i]; q++) lists[e++] = map[G.nb[i * MP_NAV_DEG + q]];
        lists[e++] = -1;
        wp[w].groupNum = w;
        wp[w].dist = 0;
        members[2 * w] = w;
        members[2 * w + 1] = -1;
        grp[w].neighbours = wp[w].neighbours;   /* a group per waypoint: its neighbour groups are its neighbour waypoints */
        grp[w].waypoints = &members[2 * w];
        grp[w].dist = 0;
    }
    wp[m].padID = -1; wp[m].neighbours = NULL; wp[m].groupNum = 0; wp[m].dist = 0;
    grp[m].neighbours = NULL; grp[m].waypoints = NULL; grp[m].dist = 0;
    g_CurrentSetup.pads = np;
    g_CurrentSetup.pathwaypoints = wp;
    g_CurrentSetup.waypointgroups = grp;
    g_PortMpNavBase = n;
    g_PortMpNavPads = np;
    g_PortMpNavCount = k + 1;
    printf("ge: mpnav: %d pieces; the largest: %d waypoints (%d of the map's pads, %d floor nodes added after pad %d), %d links\n",
           comps, m, m - extra, extra, n - 1, kept);
    NAV_FREE();
    return m;
}

#endif
