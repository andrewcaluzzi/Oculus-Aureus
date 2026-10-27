/* port_damplus.h - Dam+ shared types (port_damplus.c setup, port_damplus_geo.c geometry engine, port_damplus_wings.c data).
 * Nothing here is ROM-derived: wings are described in their own design frames (world units) and placed at run time from
 * stock features found by search (stan edges, a door's bound pad). Feature notes: midas-notes features/dam-expansion.md. */
#ifndef PORT_DAMPLUS_H
#define PORT_DAMPLUS_H

#include <ultra64.h>
#include <bondtypes.h>

/* GE_DAMPLUS parts */
#define DP_NIGHT    0x01   /* slice 1: Night Shift (setup remix) */
#define DP_MOTORPOOL 0x02  /* slice 2: the motor pool behind door 281 */
#define DP_COMMS    0x04   /* concept 5: the comms annex off the valley yard */
#define DP_GALLERY  0x08   /* concept 2: the west service gallery inside the wall */
#define DP_SPILL    0x10   /* concept 4: the spillway balcony */
#define DP_ISLAND   0x20   /* concept 3: the island lookout */
#define DP_ALLPARTS 0x3f

/* our tag ids (stock Dam uses 0-18) */
#define DP_PIERBOX_TAG 22   /* the launch call box on the west pier */
#define DP_ISLEBOX_TAG 23   /* and on the island quay */

/* fixed room ids (a disabled wing's ids hold empty rooms, so every wing keeps its ids whatever else is on) */
#define DP_ROOM_BAY 137
#define DP_ROOM_BARRACKS 138
#define DP_ROOM_COMMS_A 139        /* doorway, tunnel, radio room (and the yard wall's patch) */
#define DP_ROOM_COMMS_B 140        /* stair, blockhouse, roof deck */
#define DP_ROOM_GAL_A 141
#define DP_ROOM_GAL_B 142
#define DP_ROOM_GAL_C 143
#define DP_ROOM_SPILL_A 144
#define DP_ROOM_SPILL_B 145
#define DP_ROOM_ISLAND 146         /* the island hut's outdoor stair (the hut's own floors are stan in stock room 81) */
#define DP_ROOM_LAST 146

/* ---- design model (world units; u along the frame's first axis, v across it, h up from the frame origin's floor) ---- */
/* u codes: a seam point of this wing (its u, v and h then come from the stock point itself), or W - x for the shutter */
#define DP_SEAM(s, i) (0x7000 + (s) * 8 + (i))
#define DP_IS_SEAM(u) ((u) >= 0x7000 && (u) < 0x7000 + 32)
#define DP_W(x) (0x4000 + (x))
#define DP_IS_W(u) ((u) >= 0x4000 - 2000 && (u) <= 0x4000 + 4000)
/* v code: the stock wall plane fitted at run time behind seam s (v = a + b u), for reveals that must meet a stock wall */
#define DP_VW(s) (0x7F00 + (s))
#define DP_IS_VW(v) ((v) >= 0x7F00 && (v) < 0x7F04)

enum { E_WALL = 0, E_OPEN = 1, E_LINTEL = 2, E_RAIL = 3 };   /* per edge: full wall, nothing, wall above lintel, wall below lintel */

/* piece flags */
#define PF_NOSTAN 0x01      /* drawn only */
#define PF_NOGFX 0x02       /* walkable only */
#define PF_STEPS 0x04       /* draw the floor as steps (a 4-gon: edge 0-1 low end, edge 2-3 high end) */
#define PF_NOCEIL 0x08      /* outdoors: no ceiling */
#define PF_SLAB 0x10        /* draw a slab underside 30 below the floor and its open edges' faces */
#define PF_OUTDOOR 0x20     /* lit by the outdoor ambient (stock colours sampled at the cut), not the lamps */
#define PF_NOFLOOR 0x40     /* no floor drawn (a stock floor shows instead) */
#define PF_KERB 0x80        /* hazard-striped kerbs along the full walls (the canyon and road-tunnel look) */

typedef struct
{
    u8 room;
    u8 n, root;
    s16 u[8], v[8];
    u8 edge[8];
    s16 lintel[8];
    s16 height;             /* ceiling above the floor */
    u8 floor, wall, ceil;   /* materials */
    u8 flags;
    s16 fh[8];              /* floor height per vertex (planar) */
} DpPiece;

enum { M_ASPHALT, M_PANEL, M_CEIL, M_HAZARD, M_LAMP, M_UGWALL, M_UGFLOOR, M_UGCEIL, M_UGLAMP, M_CONCRETE, M_CONCFLOOR, M_DAMFACE,
       M_METAL, M_GRATE, M_RAIL, M_COUNT };

typedef struct { s16 u, v, h, r; u8 cr, cg, cb; u8 room; } DpLamp;               /* room 0: every room of the wing */
typedef struct { s16 u, v, len, h; u8 mat; u8 room; } DpFixture;                /* a lamp strip under a ceiling */
typedef struct { u8 room; s16 u0, v0, u1, v1, h0, h1; } DpBox;                 /* a solid box (machinery, a console plinth) */

/* seams: where a wing meets the stock stan */
enum { SEAM_NONE, SEAM_SHUTTER, SEAM_INSERT, SEAM_EDGES };
typedef struct
{
    u8 type;
    u8 room;                /* stock stan room */
    s32 x, z;               /* search point, world (the island is at z -37000: past s16) */
    s32 y;                  /* expected floor height, world */
    s16 width;              /* SEAM_INSERT: doorway width, world */
    s16 hx, hz;             /* +u hint (world direction) */
    s16 radius;             /* SEAM_EDGES: search radius */
} DpSeam;

/* a stock-geometry cut: stock triangles of `room` that reach into the box lose that part (the rest is re-drawn in `patchroom`) */
typedef struct { u8 room; u8 patchroom; s16 u0, u1, v0, v1, h0, h1; u8 maxny100; s8 fitseam; s8 frameseam; } DpCut;
/* frameseam > 0: the box is in that seam's own frame (origin its first point, u towards its last, v away from the stock room,
 * h from the first point's floor) instead of the wing's. Secondary (translucent) stock triangles in the box are dropped, not
 * redrawn. */

/* a portal: a vertical quad from (u0, v0) to (u1, v1), h0..h1, between two rooms (stock or ours) */
typedef struct { u8 r1, r2; s16 u0, v0, u1, v1, h0, h1; } DpPortalSpec;

/* vis: when the camera is in rooms lo..hi, rooms[] are drawn (FORCE: whenever on screen; else clipped to wing portal `portal`) */
#define DP_VIS_FORCE (-1)
typedef struct { u8 lo, hi; s8 portal; u8 n; u8 rooms[64]; } DpVis;

typedef struct
{
    u32 part;
    const char *name;
    u8 room0, nrooms;               /* our rooms room0 .. room0 + nrooms - 1 */
    const DpSeam *seams; s32 nseams;
    const DpPiece *pieces; s32 npieces;
    const DpLamp *lamps; s32 nlamps;
    const DpFixture *fixtures; s32 nfixtures;
    const DpBox *boxes; s32 nboxes;
    const DpCut *cuts; s32 ncuts;
    const DpPortalSpec *portals; s32 nportals;
    const DpVis *vis; s32 nvis;
    f32 ambient[3];
} DpWing;

extern const DpWing *const g_DpWings[];
extern const s32 g_DpNumWings;

/* engine API (port_damplus_geo.c) */
s32 portDamPlusParts(void);
s32 portDamPlusWingReady(u32 part);
void portDamPlusWingToWorld(u32 part, f32 u, f32 v, f32 h, f32 out[3]);
StandTile *portDamPlusWingTileAt(u32 part, f32 u, f32 v, f32 h);
s32 portDamPlusWingSeamRoom(u32 part, s32 seam);
s32 portDamPlusWingPortal(u32 part, s32 k);

#endif
