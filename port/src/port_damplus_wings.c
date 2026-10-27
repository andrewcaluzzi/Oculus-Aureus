/* port_damplus_wings.c - Dam+ wing data: generated rooms, described in their own design frames (world units; see
 * port_damplus.h). Our own layouts; every stock coordinate they need (seam points, wall planes, the faces a cut meets) is
 * found at run time. Engine: port_damplus_geo.c; setup (pads, guards, props): port_damplus.c. */
#ifndef TARGET_N64

#include "port_damplus.h"

#define O E_OPEN
#define W E_WALL
#define L E_LINTEL
#define R E_RAIL

/* ===========================================================================================================================
 * Slice 2: the motor pool behind the start shutter (door 281). Frame: origin A (the shutter's first stan point), u along the
 * shutter to E (W = its width, read at run time), v away from room 135 into the rock. Room 137: an apron under the shutter
 * (A, B, E are the stock points themselves), a throat behind it (inside the stray stock rock faces there), the vehicle bay off
 * its left side. Room 138: a doorway passage and the barracks. */
static const DpSeam sMotorSeams[] = { { SEAM_SHUTTER } };

static const DpPiece sMotorPieces[] = {
    { DP_ROOM_BAY, 5, 4, { DP_SEAM(0, 0), DP_SEAM(0, 1), DP_SEAM(0, 2), DP_W(20), 160 }, { 0, 0, 0, 40, 40 },
      { O, O, W, O, W }, { 0 }, 415, M_ASPHALT, M_PANEL, M_CEIL, PF_KERB },
    { DP_ROOM_BAY, 6, 1, { 160, DP_W(20), DP_W(20), 160, 160, 160 }, { 40, 40, 720, 720, 720, 460 },
      { O, W, W, O, O, W }, { 0 }, 415, M_ASPHALT, M_PANEL, M_CEIL, PF_KERB },
    { DP_ROOM_BAY, 7, 0, { -1700, 160, 160, 160, -800, -1000, -1700 }, { 460, 460, 720, 2000, 2000, 2000, 2000 },
      { W, L, W, W, L, W, W }, { 0, 415, 0, 0, 230, 0, 0 }, 700, M_ASPHALT, M_PANEL, M_CEIL, PF_KERB },
    { DP_ROOM_BARRACKS, 4, 0, { -1000, -800, -800, -1000 }, { 2000, 2000, 2040, 2040 },
      { O, W, O, W }, { 0 }, 230, M_UGFLOOR, M_UGWALL, M_UGCEIL },
    { DP_ROOM_BARRACKS, 6, 4, { -1500, -1000, -800, -300, -300, -1500 }, { 2040, 2040, 2040, 2040, 2900, 2900 },
      { W, O, W, W, W, W }, { 0 }, 330, M_UGFLOOR, M_UGWALL, M_UGCEIL },
};

static const DpLamp sMotorLamps[] = {
    { 480, 120, 405, 520, 150, 165, 200, DP_ROOM_BAY },       /* dusk-blue spill at the shutter */
    { 520, 560, 405, 520, 255, 190, 110, DP_ROOM_BAY },       /* sodium lamps: throat, bay */
    { -400, 900, 690, 900, 255, 185, 100, DP_ROOM_BAY },
    { -1200, 900, 690, 900, 255, 185, 100, DP_ROOM_BAY },
    { -400, 1650, 690, 900, 255, 185, 100, DP_ROOM_BAY },
    { -1200, 1650, 690, 900, 255, 185, 100, DP_ROOM_BAY },
    { -900, 2450, 320, 700, 200, 230, 210, DP_ROOM_BARRACKS }, /* barracks: cold strip light */
};

static const DpFixture sMotorFixtures[] = {
    { 380, 300, 260, 415, M_LAMP, DP_ROOM_BAY }, { 380, 600, 260, 415, M_LAMP, DP_ROOM_BAY },
    { -560, 900, 320, 700, M_LAMP, DP_ROOM_BAY }, { -1360, 900, 320, 700, M_LAMP, DP_ROOM_BAY },
    { -560, 1650, 320, 700, M_LAMP, DP_ROOM_BAY }, { -1360, 1650, 320, 700, M_LAMP, DP_ROOM_BAY },
    { -1000, 2450, 200, 330, M_UGLAMP, DP_ROOM_BARRACKS },
};

static const DpPortalSpec sMotorPortals[] = {
    { 0, DP_ROOM_BAY, 0, 0, DP_W(0), 0, 0, 402 },                       /* the shutter: 135 <-> 137 */
    { DP_ROOM_BAY, DP_ROOM_BARRACKS, -1000, 2000, -800, 2000, 0, 230 }, /* the barracks doorway */
};

static const DpWing sMotorWing = {
    DP_MOTORPOOL, "motor pool", DP_ROOM_BAY, 2,
    sMotorSeams, 1, sMotorPieces, ARRAYCOUNT(sMotorPieces), sMotorLamps, ARRAYCOUNT(sMotorLamps),
    sMotorFixtures, ARRAYCOUNT(sMotorFixtures), NULL, 0, NULL, 0, sMotorPortals, ARRAYCOUNT(sMotorPortals), NULL, 0,
    { 0.18f, 0.18f, 0.22f },
};

/* ===========================================================================================================================
 * Concept 5: the comms annex off the valley yard (room 111). A doorway in the middle of the yard's west retaining wall (one
 * 1713-unit stock stan edge; two points are inserted into it at run time). Frame: origin C (the doorway's first point), u along
 * the wall (towards the valley's south end), v into the cliff, h up from the yard floor. The wall is 428 high with a flat cap;
 * the cliff rises from v ~130-200 at about 2:1.
 *   room 139: the doorway and a tunnel (v 0..520), the radio room (u -260..560, v 520..1120) with the comms console;
 *             also the redrawn yard wall around the doorway (the cut's patch).
 *   room 140: a stair foot off the radio room's east side, a stair rising 440 back towards the yard, a concrete blockhouse
 *             breaking out of the cliff just above the wall's top, and its roof deck with the dish, overhanging the yard by
 *             260 behind a railing. */
static const DpSeam sCommsSeams[] = {
    { SEAM_INSERT, 111, 15705, 11036, -107, 260, -597, 802, 0 },
};

#define CA DP_ROOM_COMMS_A
#define CB DP_ROOM_COMMS_B
static const DpPiece sCommsPieces[] = {
    /* the tunnel: the doorway (the inserted stock points) to the radio room */
    { CA, 4, 0, { DP_SEAM(0, 0), DP_SEAM(0, 1), 260, 0 }, { 0, 0, 520, 520 }, { O, W, O, W }, { 0 }, 300,
      M_CONCFLOOR, M_CONCRETE, M_CEIL, 0 },
    /* the radio room */
    { CA, 7, 0, { -260, 0, 260, 560, 560, 560, -260 }, { 520, 520, 520, 520, 940, 1120, 1120 }, { W, L, W, W, L, W, W },
      { 0, 300, 0, 0, 300, 0, 0 }, 340, M_UGFLOOR, M_UGWALL, M_UGCEIL, 0 },
    /* the stair foot (its west side opens onto the radio room) */
    { CB, 5, 0, { 560, 580, 800, 800, 560 }, { 940, 940, 940, 1120, 1120 }, { W, O, W, W, O }, { 0 }, 300,
      M_UGFLOOR, M_UGWALL, M_UGCEIL, 0 },
    /* the stair: low end v 940 (h 0), high end v 400 (h 440) */
    { CB, 4, 0, { 800, 580, 580, 800 }, { 940, 940, 400, 400 }, { O, W, O, W }, { 0 }, 300, M_UGFLOOR, M_UGWALL, M_UGCEIL,
      PF_STEPS, { 0, 0, 440, 440 } },
    /* the blockhouse: front wall at v 140 with the deck doorway (u 580..800), the stair behind */
    { CB, 8, 0, { 380, 580, 800, 940, 940, 800, 580, 380 }, { 140, 140, 140, 140, 400, 400, 400, 400 },
      { W, L, W, W, W, O, W, W }, { 0, 260, 0, 0, 0, 0, 0, 0 }, 300, M_UGFLOOR, M_CONCRETE, M_UGCEIL, 0,
      { 440, 440, 440, 440, 440, 440, 440, 440 } },
    /* the roof deck: over the wall's cap, 260 out over the yard, a railing on three sides */
    { CB, 6, 0, { 380, 940, 940, 800, 580, 380 }, { -260, -260, 140, 140, 140, 140 }, { R, R, O, O, O, R }, { 100, 100, 0, 0, 0, 100 },
      0, M_DAMFACE, M_CONCRETE, M_CONCRETE, PF_NOCEIL | PF_OUTDOOR | PF_SLAB, { 440, 440, 440, 440, 440, 440 } },
};

static const DpLamp sCommsLamps[] = {
    { 130, 300, 290, 380, 210, 220, 200, CA },     /* tunnel */
    { 50, 820, 330, 550, 190, 230, 215, CA },      /* radio room: cold fluorescents */
    { 380, 820, 330, 550, 190, 230, 215, CA },
    { 690, 700, 560, 420, 200, 210, 190, CB },     /* stair */
    { 660, 270, 730, 420, 200, 220, 210, CB },     /* blockhouse */
};

static const DpFixture sCommsFixtures[] = {
    { 60, 300, 140, 300, M_LAMP, CA },
    { -60, 820, 220, 340, M_UGLAMP, CA }, { 300, 820, 200, 340, M_UGLAMP, CA },
    { 600, 270, 160, 740, M_UGLAMP, CB },
};

static const DpCut sCommsCuts[] = {
    /* the yard's retaining wall, for the doorway (drawn again around it in room 139) */
    { 111, CA, 0, 260, -40, 40, -30, 300, 50, 0 },
    /* the cliff where the blockhouse breaks out of it (drawn again around it in room 140) */
    { 119, CB, 380, 940, 140, 420, 430, 740, 95, -1 },
};

static const DpPortalSpec sCommsPortals[] = {
    { 111, CA, 0, 0, 260, 0, 0, 300 },          /* 0: the doorway */
    { CA, CB, 560, 940, 560, 1120, 0, 300 },    /* 1: radio room <-> stair foot */
};

static const DpVis sCommsVis[] = {
    /* the valley and the road tunnel draw the annex (the patched wall, the blockhouse and the deck) whenever it is on screen */
    { 110, 131, DP_VIS_FORCE, 2, { CA, CB } },
    /* the tunnel and radio room see the valley's cliffs through the doorway */
    { CA, CA, 0, 2, { 118, 119 } },
    /* the deck sees the whole valley */
    { CB, CB, DP_VIS_FORCE, 10, { 110, 111, 112, 113, 118, 119, 121, 122, 123, CA } },
};

static const DpWing sCommsWing = {
    DP_COMMS, "comms annex", DP_ROOM_COMMS_A, 2,
    sCommsSeams, ARRAYCOUNT(sCommsSeams), sCommsPieces, ARRAYCOUNT(sCommsPieces), sCommsLamps, ARRAYCOUNT(sCommsLamps),
    sCommsFixtures, ARRAYCOUNT(sCommsFixtures), NULL, 0, sCommsCuts, ARRAYCOUNT(sCommsCuts),
    sCommsPortals, ARRAYCOUNT(sCommsPortals), sCommsVis, ARRAYCOUNT(sCommsVis),
    { 0.16f, 0.18f, 0.18f },
};
#undef CA
#undef CB

/* ===========================================================================================================================
 * Concept 2: the west service gallery inside the wall. Frame: a 150 x 185 hatch in room 84's west wall (one 187-long stock edge,
 * the wall there is only 188 high), u across the gallery (towards the downstream face), v along it (west), h up from room 84's
 * floor (y -642). The gallery curves with the crest above it, 220 wide and 280 high, between the upstream and downstream faces
 * (the wing is ~800 thick at this depth), through a turbine hall (room 142: a grated walkway over a 300-deep pit with two
 * turbine housings), then runs along the axis of the crest's dead-end stub, under its floor, past its end; a landing, a stair
 * back up beside the gallery, and a vestibule entering the stub's hazard-striped end wall (seam 1: the stub end's open stan edge,
 * used as found; its two stock wall triangles and the stripes over them are cut, in that seam's own frame).
 * Layout generated by a local design aid (gen_c2.py) from the frame and the stub end's position in it. */
static const DpSeam sGallerySeams[] = {
    { SEAM_INSERT, 84, -5857, -2545, -642, 150, 64, 175, 0 },
    { SEAM_EDGES, 55, -13492, 2380, -107, 0, 321, 428, 300 },
};

#define GA DP_ROOM_GAL_A
#define GB DP_ROOM_GAL_B
#define GC DP_ROOM_GAL_C
static const DpPiece sGalleryPieces[] = {
    /* room 143: the hatch (the inserted stock points), a widening, the gallery to the hall */
    { GC, 4, 0, { DP_SEAM(0, 0), DP_SEAM(0, 1), 150, 0 }, { 0, 0, 40, 40 }, { O, W, O, W }, { 0, 0, 0, 0 }, 185, M_UGFLOOR, M_UGWALL, M_UGCEIL, 0 },
    { GC, 4, 0, { 0, 150, 185, -35 }, { 40, 40, 294, 306 }, { L, W, O, W }, { 185, 0, 0, 0 }, 280, M_UGFLOOR, M_UGWALL, M_UGCEIL, 0 },
    { GC, 4, 0, { -35, 185, 219, 1 }, { 306, 294, 890, 910 }, { O, W, O, W }, { 0 }, 280, M_UGFLOOR, M_UGWALL, M_UGCEIL, 0 },
    { GC, 4, 0, { 1, 219, 299, 81 }, { 910, 890, 1484, 1516 }, { O, W, O, W }, { 0 }, 280, M_UGFLOOR, M_UGWALL, M_UGCEIL, 0 },
    { GC, 4, 0, { 81, 299, 399, 181 }, { 1516, 1484, 2082, 2118 }, { O, W, O, W }, { 0 }, 280, M_UGFLOOR, M_UGWALL, M_UGCEIL, 0 },
    { GC, 4, 0, { 181, 399, 509, 291 }, { 2118, 2082, 2782, 2818 }, { O, W, O, W }, { 0 }, 280, M_UGFLOOR, M_UGWALL, M_UGCEIL, 0 },
    /* room 142: the turbine hall: the walkway (walkable), the pit (to h -300) and the upper hall (to +500), drawn only */
    { GB, 4, 0, { 291, 509, 628, 412 }, { 2818, 2782, 3477, 3523 }, { O, R, O, R }, { 0, 100, 0, 100 }, 0, M_GRATE, M_METAL, M_METAL,
      PF_NOCEIL | PF_SLAB },
    { GB, 4, 0, { 200, 760, 760, 200 }, { 2780, 2780, 3520, 3520 }, { W, W, W, W }, { 300, 300, 300, 300 }, 0, M_CONCRETE, M_UGWALL,
      M_UGCEIL, PF_NOSTAN | PF_NOCEIL, { -300, -300, -300, -300 } },
    { GB, 8, 0, { 200, 291, 509, 760, 760, 628, 412, 200 }, { 2780, 2818, 2782, 2780, 3520, 3477, 3523, 3520 },
      { W, L, W, W, W, L, W, W }, { 0, 280, 0, 0, 0, 280, 0, 0 }, 500, M_UGFLOOR, M_UGWALL, M_UGCEIL, PF_NOSTAN | PF_NOFLOOR },
    /* room 141: the gallery on to the stub's axis, under the stub, past its end */
    { GA, 4, 0, { 412, 628, 786, 574 }, { 3523, 3477, 4069, 4131 }, { O, W, O, W }, { 0 }, 280, M_UGFLOOR, M_UGWALL, M_UGCEIL, 0 },
    { GA, 4, 0, { 574, 786, 974, 766 }, { 4131, 4069, 4665, 4735 }, { O, W, O, W }, { 0 }, 280, M_UGFLOOR, M_UGWALL, M_UGCEIL, 0 },
    { GA, 4, 0, { 766, 974, 1185, 975 }, { 4735, 4665, 5267, 5333 }, { O, W, O, W }, { 0 }, 280, M_UGFLOOR, M_UGWALL, M_UGCEIL, 0 },
    { GA, 4, 0, { 975, 1185, 1357, 1143 }, { 5333, 5267, 5877, 5923 }, { O, W, O, W }, { 0 }, 280, M_UGFLOOR, M_UGWALL, M_UGCEIL, 0 },
    { GA, 4, 0, { 1143, 1357, 1439, 1221 }, { 5923, 5877, 6438, 6462 }, { O, W, O, W }, { 0 }, 280, M_UGFLOOR, M_UGWALL, M_UGCEIL, 0 },
    { GA, 4, 0, { 1221, 1439, 1478, 1268 }, { 6462, 6438, 6954, 7018 }, { O, W, O, W }, { 0 }, 280, M_UGFLOOR, M_UGWALL, M_UGCEIL, 0 },
    { GA, 4, 0, { 1268, 1478, 1768, 1558 }, { 7018, 6954, 7911, 7975 }, { O, W, O, W }, { 0 }, 280, M_UGFLOOR, M_UGWALL, M_UGCEIL, 0 },
    { GA, 4, 0, { 1558, 1768, 2058, 1847 }, { 7975, 7911, 8868, 8932 }, { O, W, O, W }, { 0 }, 280, M_UGFLOOR, M_UGWALL, M_UGCEIL, 0 },
    { GA, 4, 0, { 1847, 2058, 2353, 2143 }, { 8932, 8868, 9845, 9908 }, { O, W, O, W }, { 0 }, 280, M_UGFLOOR, M_UGWALL, M_UGCEIL, 0 },
    /* the landing beyond the stub end, the stair back up beside the gallery, the vestibule at the stub's end wall */
    { GA, 6, 0, { 2143, 2353, 2392, 2602, 2666, 2207 }, { 9908, 9845, 9833, 9769, 9980, 10119 }, { O, W, O, W, W, W }, { 0 }, 280,
      M_UGFLOOR, M_UGWALL, M_UGCEIL, 0 },
    { GA, 4, 0, { 2602, 2392, 2131, 2341 }, { 9769, 9833, 8972, 8908 }, { O, W, O, W }, { 0 }, 280, M_UGFLOOR, M_UGWALL, M_UGCEIL,
      PF_STEPS, { 0, 0, 535, 535 } },
    { GA, 6, 0, { DP_SEAM(1, 0), DP_SEAM(1, 1), 2368, 2341, 2131, 1856 }, { 0, 0, 8900, 8908, 8972, 9055 }, { O, W, W, L, W, W },
      { 0, 0, 0, 280, 0, 0 }, 428, M_CONCFLOOR, M_CONCRETE, M_CEIL, 0, { 535, 535, 535, 535, 535, 535 } },
};

static const DpLamp sGalleryLamps[] = {
    { 75, 300, 260, 520, 190, 225, 205, GC }, { 190, 1500, 260, 520, 190, 225, 205, GC }, { 400, 2600, 260, 520, 190, 225, 205, GC },
    { 680, 4100, 260, 520, 190, 225, 205, GA }, { 1080, 5300, 260, 520, 190, 225, 205, GA }, { 1330, 6450, 260, 520, 190, 225, 205, GA },
    { 1663, 7943, 260, 520, 190, 225, 205, GA }, { 2248, 9876, 260, 520, 190, 225, 205, GA },
    { 480, 3000, 480, 700, 255, 200, 140, GB }, { 480, 3300, 480, 700, 255, 200, 140, GB },   /* the hall: sodium */
    { 2404, 9944, 500, 600, 200, 220, 210, GA }, { 2094, 8920, 800, 600, 200, 220, 210, GA },
};

static const DpFixture sGalleryFixtures[] = {
    { 20, 300, 110, 280, M_UGLAMP, GC }, { 135, 1500, 110, 280, M_UGLAMP, GC }, { 345, 2600, 110, 280, M_UGLAMP, GC },
    { 625, 4100, 110, 280, M_UGLAMP, GA }, { 1025, 5300, 110, 280, M_UGLAMP, GA }, { 1275, 6450, 110, 280, M_UGLAMP, GA },
    { 1608, 7943, 110, 280, M_UGLAMP, GA }, { 2193, 9876, 110, 280, M_UGLAMP, GA },
    { 330, 3000, 300, 500, M_LAMP, GB }, { 330, 3300, 300, 500, M_LAMP, GB },
    { 2000, 8940, 200, 963, M_LAMP, GA },
};

static const DpBox sGalleryBoxes[] = {
    /* two turbine housings and a valve column in the pit */
    { GB, 250, 2880, 420, 3120, -300, -70 }, { GB, 540, 3180, 710, 3420, -300, -70 }, { GB, 690, 2820, 740, 2870, -300, 120 },
};

static const DpCut sGalleryCuts[] = {
    /* room 84's west wall, for the hatch (redrawn around it in room 143) */
    { 84, GC, 0, 150, -20, 20, -20, 185, 50, -1, 0 },
    /* the stub's end wall and its hazard stripes, in the stub end's own frame (nothing of it is redrawn: the cut spans it) */
    { 55, GA, 2, 533, -20, 20, -20, 440, 50, -1, 1 },
};

static const DpPortalSpec sGalleryPortals[] = {
    { 84, GC, 0, 0, 150, 0, 0, 185 },               /* 0: the hatch */
    { GC, GB, 291, 2818, 509, 2782, 0, 280 },       /* 1: gallery <-> hall */
    { GB, GA, 412, 3523, 628, 3477, 0, 280 },       /* 2: hall <-> gallery */
    { GA, 55, 1821, 8940, 2333, 8785, 535, 963 },   /* 3: the vestibule <-> the stub */
};

static const DpVis sGalleryVis[] = {
    /* the west underground draws room 143 (it carries room 84's redrawn wall) */
    { 82, 87, DP_VIS_FORCE, 1, { GC } },
};

static const DpWing sGalleryWing = {
    DP_GALLERY, "west gallery", DP_ROOM_GAL_A, 3,
    sGallerySeams, ARRAYCOUNT(sGallerySeams), sGalleryPieces, ARRAYCOUNT(sGalleryPieces), sGalleryLamps, ARRAYCOUNT(sGalleryLamps),
    sGalleryFixtures, ARRAYCOUNT(sGalleryFixtures), sGalleryBoxes, ARRAYCOUNT(sGalleryBoxes), sGalleryCuts, ARRAYCOUNT(sGalleryCuts),
    sGalleryPortals, ARRAYCOUNT(sGalleryPortals), sGalleryVis, ARRAYCOUNT(sGalleryVis),
    { 0.14f, 0.17f, 0.16f },
};
#undef GA
#undef GB
#undef GC

/* =========================================================================================================================== */
const DpWing *const g_DpWings[] = {
    &sMotorWing,
    &sCommsWing,
    &sGalleryWing,
};
const s32 g_DpNumWings = ARRAYCOUNT(g_DpWings);

#endif
