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

/* =========================================================================================================================== */
const DpWing *const g_DpWings[] = {
    &sMotorWing,
    &sCommsWing,
};
const s32 g_DpNumWings = ARRAYCOUNT(g_DpWings);

#endif
