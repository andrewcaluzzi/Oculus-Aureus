/* port_damplus.c - Dam+: an expanded Dam behind an opt-in switch (features: midas-notes features/dam-expansion.md).
 *
 * Stock Dam still comes from the user's ROM. This file only APPENDS to it at load time:
 *   slice 1 "Night Shift": extra pads (anchored to stock pads by id plus an offset, floor tile found by walking the stock stan
 *   from the anchor's tile), extra guards cloned from a stock guard record read at run time (with that guard's own weapon
 *   records), new patrol paths over the stock waypoints, a few floor pickups cloned from stock records.
 *   the wings (port_damplus_geo.c builds them, port_damplus_wings.c describes them): per wing, pads on its floors (design
 *   points), a waypoint chain joined to the stock graph, guards, props, pickups, a patrol. The motor pool also gives the start
 *   shutter (door 281) a key on the truck guard; the comms annex has a console that, once destroyed, stops the alarm
 *   reinforcements (our replacement for the background list 0x1002, stock ai_22, which spawns at pad 116 while the alarm sounds).
 * Nothing stock is renumbered: new pads, propdefs, tags, AI lists and patrol paths go after the stock ones, so every objective,
 * tag, flag, the intro/outro and the bungee keep their ids.
 *
 * Switch: GE_DAMPLUS (unset/0 = off; 1 = everything; or a comma list of night, motor, comms, gallery, spill, island). Off in
 * attract demos, while recording, in multiplayer and on every stage but Dam. Tests and the dam1 oracle never set it. */
#ifndef TARGET_N64

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ultra64.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include "memp.h"
#include "game/stan.h"
#include "game/chr.h"
#include "game/propobj.h"
#include "port_damplus.h"

extern s32 getPlayerCount(void);
extern s32 get_is_ramrom_flag(void);
extern s32 get_recording_ramrom_flag(void);
extern s32 gamemode;
extern const char *portModeChoice(void);
extern s32 sizepropdef(PropDefHeaderRecord *pdef);
extern stagesetup g_CurrentSetup;
extern ChrRecord *chrFindByLiteralId(s32 id);
extern s32 g_ClockTimer;
extern s32 g_GlobalTimer;

static s32 sParts;      /* parts on for the stage being loaded (0 = stock Dam); set once per stage load */

/* What GE_DAMPLUS asks for, whatever the stage. */
static s32 portDamPlusWanted(void)
{
    static const struct { const char *name; s32 part; } names[] = {
        { "night", DP_NIGHT }, { "motor", DP_MOTORPOOL }, { "comms", DP_COMMS }, { "gallery", DP_GALLERY },
        { "spill", DP_SPILL }, { "island", DP_ISLAND },
    };
    const char *e = getenv("GE_DAMPLUS");
    s32 parts = 0;

    if (e == NULL || e[0] == '\0' || e[0] == '0')
    {
        return 0;
    }
    if (strcmp(e, "1") == 0 || strcmp(e, "all") == 0)
    {
        return DP_ALLPARTS;
    }
    while (*e)
    {
        s32 i, n = (s32) strcspn(e, ",");

        for (i = 0; i < (s32) ARRAYCOUNT(names); i++)
        {
            if ((s32) strlen(names[i].name) == n && strncmp(e, names[i].name, n) == 0)
            {
                parts |= names[i].part;
            }
        }
        e += n;
        if (*e == ',') e++;
    }
    return parts;
}

/* Called once at the start of every stage load (load_bg_file), before any Dam+ hook. */
void portDamPlusStageBegin(s32 stage)
{
    const char *mode = portModeChoice();

    sParts = 0;
    if (stage != LEVELID_DAM)
    {
        return;
    }
    if (gamemode == GAMEMODE_MULTI || getPlayerCount() != 1 || get_is_ramrom_flag() != 0 || get_recording_ramrom_flag() != 0)
    {
        return;
    }
    (void) mode;   /* a Lua mode on Dam (it removes the guards) still gets the extra space when GE_DAMPLUS asks for it */
    sParts = portDamPlusWanted();
    if (sParts != 0)
    {
        printf("ge: dam+: on (parts 0x%x)\n", sParts);
        fflush(stdout);
    }
}

s32 portDamPlusParts(void)
{
    return sParts;
}

/* ---------------------------------------------------------------------------------------------------------------------------
 * Slice 1 data. Pads are our own: a stock anchor pad (by id) plus an offset in world units; DP_NEW(n) names an earlier new pad.
 * Waypoint ids in the patrol paths are stock Dam waypoints (the approach and dam graphs are untouched). */

#define DP_NEW(n) (0x4000 + (n))
#define DP_LOCAL(n) (0x5000 + (n))     /* pad n of the wing being set up */
#define DP_END_PATH (-1)

typedef struct
{
    s16 anchor;     /* stock pad id, or DP_NEW(n) */
    s16 dx, dz;     /* offset from the anchor, world units */
    s16 yaw;        /* facing, degrees (0 = +z) */
} DpPad;

typedef struct
{
    s16 pad;        /* stock pad id, DP_NEW(n), or DP_LOCAL(n) */
    u16 ailist;     /* AI list id (stock global/stage list, or one of ours below) */
    s16 tmplchr;    /* stock guard whose record (body, stats) and weapons are cloned */
    s16 chrnum;     /* our chr number (unused by stock Dam: 0-47 are taken) */
} DpGuard;

enum { DP_PICK_ARMOUR, DP_PICK_WEAPON };

typedef struct
{
    u8 kind;
    s16 pad;
    s16 tmplchr;    /* DP_PICK_WEAPON: the weapon is a copy of this stock guard's first gun, left on the floor */
} DpPickup;

/* -- pads ------------------------------------------------------------------------------------------------------------------ */
static const DpPad sNightPads[] = {
    /* 0-1: the road tunnel's empty segments (129-131 towards the valley, 124-126 towards the canyon), at the road's edge
     * like stock chrs 4 and 5 in segment 127, so the truck passes them */
    { 285, 209, -292, 140 },        /* 0: segment 129, on the road (a stepping stone for the walk) */
    { DP_NEW(0), 660, -260, 320 },  /* 1: segment 130/131, the valley side, east wall, facing the canyon */
    { 9, 980, 640, 235 },           /* 2: segment 125/126, north wall, facing the valley */
    /* 3-4: the dead-end tunnel stub at the west end of the crest (room 55): a sentry, and the far end */
    { 283, -880, 700, 130 },
    { 283, -1090, 913, 130 },
};

/* -- AI lists: patrol starters (start the path, then the stock guard brain, global list 7, as stock ai_0-6 do) ---------------- */
#define DP_AI_PATROL(path) 0x20, (path), 0x05, 0xfd, 0x00, 0x07, 0x04
static u8 sAiPatrol8[] = { DP_AI_PATROL(8) };
static u8 sAiPatrol9[] = { DP_AI_PATROL(9) };
static u8 sAiPatrol10[] = { DP_AI_PATROL(10) };
static u8 sAiPatrol11[] = { DP_AI_PATROL(11) };

typedef struct
{
    u8 *list;
    u16 id;
} DpAiList;

static const DpAiList sNightAi[] = {
    { sAiPatrol8, 0x0480 },
    { sAiPatrol9, 0x0481 },
    { sAiPatrol10, 0x0482 },
    { sAiPatrol11, 0x0483 },
};

/* -- patrol paths over stock waypoints --------------------------------------------------------------------------------------- */
/* 8: the crest, west stub (room 55) to the east head (53) and back; 9: the same round from the east end */
static s32 sPath8[] = { 46, 47, 48, 49, 54, 53, 50, 55, 57, 56, 58, 60, 59, 60, 58, 56, 57, 55, 50, 53, 54, 49, 48, 47, DP_END_PATH };
static s32 sPath9[] = { 59, 60, 58, 56, 57, 55, 50, 53, 54, 49, 48, 47, 46, 47, 48, 49, 54, 53, 50, 55, 57, 56, 58, 60, DP_END_PATH };
/* 10: the lake-side quay, centre (71) to the west end (69) and back */
static s32 sPath10[] = { 83, 82, 80, 86, 87, 88, 89, 93, 92, 91, 94, 91, 92, 93, 89, 88, 87, 86, 80, 82, DP_END_PATH };
/* 11: the road tunnel, valley (111) to the canyon (132) and back */
static s32 sPath11[] = { 45, 44, 43, 42, 43, 44, DP_END_PATH };

typedef struct
{
    s32 *waypoints;
    u8 id;
} DpPath;

static const DpPath sNightPaths[] = {
    { sPath8, 8 },
    { sPath9, 9 },
    { sPath10, 10 },
    { sPath11, 11 },
};

/* -- guards ------------------------------------------------------------------------------------------------------------------ */
/* Templates: stock chr 7 (an approach guard with a KF7, idle brain 0x0002) and chr 41 (tower guard). */
static const DpGuard sNightGuards[] = {
    /* the crest: two patrols walking the whole crest in opposite directions, a sentry by the bungee platform, one in the stub */
    { 283, 0x0480, 7, 100 },
    { 270, 0x0481, 7, 101 },
    { 277, 0x0002, 7, 102 },
    { DP_NEW(3), 0x0002, 7, 103 },
    /* a second sentry on each tower top (72, 75, 78) */
    { 186, 0x0002, 41, 104 },
    { 250, 0x0002, 41, 105 },
    { 216, 0x0002, 41, 106 },
    /* the quay: a patrol plus a sentry at each end */
    { 236, 0x0482, 7, 107 },
    { 238, 0x0002, 7, 108 },
    { 224, 0x0002, 7, 109 },
    /* the road tunnel: a patrol between the valley and the canyon, and a sentry in each empty stretch */
    { 284, 0x0483, 7, 110 },
    { DP_NEW(1), 0x0002, 7, 111 },
    { DP_NEW(2), 0x0002, 7, 112 },
};

static const DpPickup sNightPickups[] = {
    { DP_PICK_ARMOUR, DP_NEW(4), 0 },  /* body armour at the end of the west stub, past its sentry */
    { DP_PICK_ARMOUR, 231, 0 },        /* body armour on the quay's west half */
    { DP_PICK_WEAPON, 237, 7 },        /* a KF7 (ammo) on the quay */
    { DP_PICK_WEAPON, 285, 7 },        /* a KF7 at the tunnel bend */
};

/* ---------------------------------------------------------------------------------------------------------------------------
 * Wing setups. Pads are DESIGN points in the wing's frame (world units; u, v, the floor height h of the spot, a facing in the
 * frame: 0 = +v, 90 = +u); DP_LOCAL(n) names them. Pads 0 .. nwp-1 are the wing's waypoints, a chain whose first joins the
 * nearest stock waypoint in the wing's seam room. */

typedef struct { s16 u, v, h, yaw; } DpWPad;

enum { DP_PROP_PLAIN, DP_PROP_CONSOLE };
typedef struct { s16 pad; s16 model; u8 kind; s16 scale; } DpProp;   /* scale: 256 = 1 (0 = 256) */

typedef struct
{
    u32 part;
    const DpWPad *pads; s32 npads;
    s32 nwp;
    const DpGuard *guards; s32 nguards;
    const DpPickup *pickups; s32 npickups;
    const DpProp *props; s32 nprops;
    u8 patrolPath; u16 patrolAi; u8 *patrolAiBytes; const s8 *patrolSeq; s32 npatrol;
    u8 joinLast;     /* the last waypoint also joins the nearest stock waypoint in the wing's seam-1 room */
} DpWingSetup;

/* -- the motor pool (slice 2) -- */
static const DpWPad sMotorPads[] = {
    /* 0-6: waypoints, the shutter to the barracks */
    { 450, 140, 0, 0 }, { 520, 590, 0, 270 }, { -150, 590, 0, 270 }, { -800, 1200, 0, 0 }, { -900, 1860, 0, 0 }, { -900, 2200, 0, 0 },
    { -900, 2600, 0, 180 },
    /* 7-10: guards */
    { -380, 1050, 0, 120 }, { -1450, 1600, 0, 150 }, { -1250, 2700, 0, 160 }, { -500, 2450, 0, 220 },
    /* 11-13: pickups in the barracks */
    { -1400, 2820, 0, 0 }, { -420, 2820, 0, 0 }, { -420, 2120, 0, 0 },
    /* 14-25: props: the second truck, drums, crates, lockers, a table */
    { -1050, 1250, 0, 0 }, { -1610, 560, 0, 0 }, { -1610, 680, 0, 0 }, { -1500, 600, 0, 0 }, { -1620, 1900, 0, 90 }, { -250, 1920, 0, 90 },
    { -1430, 2850, 0, 180 }, { -1270, 2850, 0, 180 }, { -1110, 2850, 0, 180 }, { -900, 2420, 0, 0 }, { -780, 2440, 0, 270 }, { -1020, 2440, 0, 90 },
};

static const DpGuard sMotorGuards[] = {
    /* the bay: two idle guards by the truck and at the back, one walking between the bay and the barracks */
    { DP_LOCAL(7), 0x0002, 7, 120 },
    { DP_LOCAL(8), 0x0002, 7, 121 },
    { DP_LOCAL(2), 0x0484, 7, 122 },
    /* the barracks: an armoured sergeant (Dam's own ai_11: more health, armour, kneels to fire) and a guard */
    { DP_LOCAL(9), 0x040c, 14, 123 },
    { DP_LOCAL(10), 0x0002, 22, 124 },
};

static const DpPickup sMotorPickups[] = {
    { DP_PICK_ARMOUR, DP_LOCAL(11), 0 },  /* the locker's prize: body armour */
    { DP_PICK_WEAPON, DP_LOCAL(12), 7 },  /* and two KF7s (ammo) */
    { DP_PICK_WEAPON, DP_LOCAL(13), 7 },
};

static const DpProp sMotorProps[] = {
    { DP_LOCAL(14), PROP_MILTRUCK },
    { DP_LOCAL(15), PROP_OIL_DRUM1 }, { DP_LOCAL(16), PROP_OIL_DRUM2 }, { DP_LOCAL(17), PROP_OIL_DRUM7 },
    { DP_LOCAL(18), PROP_METAL_CRATE1 }, { DP_LOCAL(19), PROP_AMMO_CRATE1 },
    { DP_LOCAL(20), PROP_LOCKER3 }, { DP_LOCAL(21), PROP_LOCKER4 }, { DP_LOCAL(22), PROP_LOCKER3 },
    { DP_LOCAL(23), PROP_WOODEN_TABLE1 }, { DP_LOCAL(24), PROP_METAL_CHAIR1 }, { DP_LOCAL(25), PROP_METAL_CHAIR1 },
};

static u8 sAiPatrol12[] = { DP_AI_PATROL(12) };
static const s8 sMotorPatrol[] = { 2, 3, 4, 5, 6, 5, 4, 3 };

static const DpWingSetup sMotorSetup = {
    DP_MOTORPOOL, sMotorPads, ARRAYCOUNT(sMotorPads), 7, sMotorGuards, ARRAYCOUNT(sMotorGuards),
    sMotorPickups, ARRAYCOUNT(sMotorPickups), sMotorProps, ARRAYCOUNT(sMotorProps),
    12, 0x0484, sAiPatrol12, sMotorPatrol, ARRAYCOUNT(sMotorPatrol),
};

/* -- concept 5: the comms annex (frame: port_damplus_wings.c; u along the yard's west wall, v into the cliff) -- */
static const DpWPad sCommsPads[] = {
    /* 0-5: waypoints: the tunnel, the radio room, the radio room by the stair opening, the stair foot, the blockhouse, the deck */
    { 130, 200, 0, 0 }, { 150, 820, 0, 0 }, { 470, 1030, 0, 90 }, { 690, 1030, 0, 180 }, { 690, 270, 440, 180 }, { 660, -100, 440, 180 },
    /* 6-9: guards: the operator at the console, a guard in the radio room, the deck sentry, one in the tunnel */
    { -120, 960, 0, 0 }, { 420, 700, 0, 230 }, { 480, -170, 440, 180 }, { 60, 430, 0, 180 },
    /* 10: the comms console against the radio room's back wall; 11-13: radio units and a tuning console; 14: the dish on the deck */
    { -150, 1070, 0, 180 }, { 80, 1080, 0, 180 }, { 260, 1080, 0, 180 }, { 450, 600, 0, 200 }, { 820, -120, 440, 200 },
    /* 15-16: pickups: body armour on the deck, a KF7 in the radio room */
    { 420, 90, 440, 0 }, { -200, 600, 0, 0 },
};

static const DpGuard sCommsGuards[] = {
    { DP_LOCAL(6), 0x0002, 7, 130 },
    { DP_LOCAL(7), 0x0002, 22, 131 },
    { DP_LOCAL(8), 0x0002, 41, 132 },
    { DP_LOCAL(9), 0x0002, 7, 133 },
    { DP_LOCAL(1), 0x0485, 7, 134 },      /* walks the radio room - stair - deck round */
};

static const DpProp sCommsProps[] = {
    { DP_LOCAL(10), PROP_CONSOLE1, DP_PROP_CONSOLE },
    { DP_LOCAL(11), PROP_RADIO_UNIT2 }, { DP_LOCAL(12), PROP_RADIO_UNIT3 }, { DP_LOCAL(13), PROP_TUNING_CONSOLE1 },
    { DP_LOCAL(14), PROP_SATDISH, DP_PROP_PLAIN, 64 },
};

static const DpPickup sCommsPickups[] = {
    { DP_PICK_ARMOUR, DP_LOCAL(15), 0 },
    { DP_PICK_WEAPON, DP_LOCAL(16), 7 },
};

static u8 sAiPatrol13[] = { DP_AI_PATROL(13) };
static const s8 sCommsPatrol[] = { 1, 2, 3, 4, 5, 4, 3, 2 };

static const DpWingSetup sCommsSetup = {
    DP_COMMS, sCommsPads, ARRAYCOUNT(sCommsPads), 6, sCommsGuards, ARRAYCOUNT(sCommsGuards),
    sCommsPickups, ARRAYCOUNT(sCommsPickups), sCommsProps, ARRAYCOUNT(sCommsProps),
    13, 0x0485, sAiPatrol13, sCommsPatrol, ARRAYCOUNT(sCommsPatrol),
};

/* -- concept 2: the west service gallery (frame: port_damplus_wings.c; u across, v along the gallery from room 84's hatch) -- */
static const DpWPad sGalleryPads[] = {
    /* 0-13: waypoints: the hatch, the gallery, the hall entry, on to the stub's axis, the landing, the stair, the vestibule */
    { 75, 150, 0, 0 }, { 75, 300, 0, 0 }, { 190, 1500, 0, 0 }, { 400, 2800, 0, 0 }, { 680, 4100, 0, 0 }, { 1080, 5300, 0, 0 },
    { 1330, 6450, 0, 0 }, { 1663, 7943, 0, 0 }, { 2248, 9876, 0, 0 }, { 2280, 9982, 0, 0 }, { 2529, 9906, 0, 0 },
    { 2367, 9370, 267, 0 }, { 2219, 8882, 535, 0 }, { 1970, 8958, 535, 0 },
    /* 14-17: guards: by the hatch, the hall walkway's two ends, the vestibule */
    { 140, 900, 0, 180 }, { 340, 2900, 0, 20 }, { 590, 3400, 0, 200 }, { 2100, 8940, 535, 200 },
    /* 18-19: pickups: body armour on the walkway, a KF7 on the landing */
    { 460, 3150, 0, 0 }, { 2420, 10010, 0, 0 },
    /* 20-22: props: drums on the landing, a crate in the gallery */
    { 2500, 9950, 0, 0 }, { 2440, 9985, 0, 0 }, { 1150, 5550, 0, 0 },
};

static const DpGuard sGalleryGuards[] = {
    { DP_LOCAL(14), 0x0002, 22, 140 },
    { DP_LOCAL(15), 0x0002, 7, 141 },
    { DP_LOCAL(16), 0x0002, 22, 142 },
    { DP_LOCAL(17), 0x0002, 7, 143 },
    { DP_LOCAL(5), 0x0486, 7, 144 },      /* walks the gallery, hatch to landing */
};

static const DpPickup sGalleryPickups[] = {
    { DP_PICK_ARMOUR, DP_LOCAL(18), 0 },
    { DP_PICK_WEAPON, DP_LOCAL(19), 7 },
};

static const DpProp sGalleryProps[] = {
    { DP_LOCAL(20), PROP_OIL_DRUM1 }, { DP_LOCAL(21), PROP_OIL_DRUM2 }, { DP_LOCAL(22), PROP_METAL_CRATE1 },
};

static u8 sAiPatrol14[] = { DP_AI_PATROL(14) };
static const s8 sGalleryPatrol[] = { 1, 2, 3, 4, 5, 6, 7, 8, 7, 6, 5, 4, 3, 2 };

static const DpWingSetup sGallerySetup = {
    DP_GALLERY, sGalleryPads, ARRAYCOUNT(sGalleryPads), 14, sGalleryGuards, ARRAYCOUNT(sGalleryGuards),
    sGalleryPickups, ARRAYCOUNT(sGalleryPickups), sGalleryProps, ARRAYCOUNT(sGalleryProps),
    14, 0x0486, sAiPatrol14, sGalleryPatrol, ARRAYCOUNT(sGalleryPatrol), 1,
};

static const DpWingSetup *const sWingSetups[] = { &sMotorSetup, &sCommsSetup, &sGallerySetup };

/* -- the comms console's reward ------------------------------------------------------------------------------------------- */
#define DP_COMMS_TAG 20               /* our tag id for the console (stock tags are 0-18) */
#define DP_COMMS_CALL_FLAG0 0x00010000 /* stage flag set by reinforcement call k: 0x10000 << k (stock Dam uses 0x100-0x2000) */
#define DP_COMMS_CALLS 6
#define DP_COMMS_GAP60 1200           /* 20 s between calls */

/* Our replacement for background list 0x1002 (stock ai_22: once the alarm sounds, two tries at spawning a guard at pad 116 on
 * list 0x040f, then idle). Ours: while the comms console stands and the alarm sounds, a call every 20 s, six calls; each call
 * spawns at pad 116 like ai_22 does (the first call twice, as ai_22 does) and sets its own stage flag. Once the console is
 * destroyed, no more calls. Bytes: big-endian arguments, as the ROM's lists are. */
#define DPB_SPAWN116(lbl) 0xBD, 0x25, 0xff, 0x00, 0x74, 0x04, 0x0f, 0x00, 0x00, 0x00, 0x00, (lbl)
#define DPB_FLAG(f) 0x9A, (u8) ((f) >> 24), (u8) ((f) >> 16), (u8) ((f) >> 8), (u8) (f)
#define DPB_CONSOLE_OK(lbl) 0x5B, DP_COMMS_TAG, (lbl), 0x00, 0x7f, 0x02, (lbl)   /* if standing goto lbl; else goto_next idle */
#define DPB_CALL(k, L) \
    DPB_CONSOLE_OK(L + 0), \
    0xF3, 0x7f,                                         /* the bungee is under way: idle */ \
    DPB_SPAWN116(L + 1), 0x02, L + 1, \
    DPB_FLAG(DP_COMMS_CALL_FLAG0 << (k)), \
    0xAE,                                               /* local timer: reset and start */ \
    0x02, L + 2, 0x03,                                  /* wait: sleep */ \
    DPB_CONSOLE_OK(L + 3), \
    0xB4, 0x00, (u8) (DP_COMMS_GAP60 >> 8), (u8) DP_COMMS_GAP60, L + 4, 0x01, L + 2, \
    0x02, L + 4, \
    0x02, L + 5, 0x37, L + 6, 0x03, 0x01, L + 5, 0x02, L + 6   /* then wait for the alarm (again) */

static u8 sAiComms[] = {
    0x02, 0x00, 0x03, 0x37, 0x01, 0x01, 0x00, 0x02, 0x01,      /* label 0: sleep until the alarm sounds */
    DPB_CONSOLE_OK(0x08), 0xF3, 0x7f, DPB_SPAWN116(0x09), 0x02, 0x09, 0x03,   /* the first call's extra spawn, as ai_22's pair */
    DPB_CALL(0, 0x10), DPB_CALL(1, 0x18), DPB_CALL(2, 0x20), DPB_CALL(3, 0x28), DPB_CALL(4, 0x30), DPB_CALL(5, 0x38),
    0x02, 0x7f, 0x03, 0x01, 0x7f, 0x04,                         /* idle for good */
};

/* --------------------------------------------------------------------------------------------------------------------------- */

static s32 sStockPads;   /* stock pad count of the stage being loaded */
static u32 *sOursFrom, *sOursTo;       /* our appended propdef records (this stage load), for portDamPlusVerify */
static ObjectRecord *sKeyRec;
static DoorRecord *sDoorRec;
static ObjectRecord *sConsoleRec;
static s32 sConsoleIdx;
static s32 sNightPadsOn; /* how many night pads were appended (0 when slice 1 is off) */
static s32 sWingPadBase[8];
static s32 sWingOn[8];
static s32 sCurWing;     /* the wing whose DP_LOCAL pads are meant */
static u32 sCallsSeen;   /* comms reinforcement calls already logged */

static s32 dpPadIndex(s16 p)
{
    if (p >= DP_LOCAL(0))
    {
        return sWingPadBase[sCurWing] + (p - DP_LOCAL(0));
    }
    return (p >= DP_NEW(0)) ? sStockPads + (p - DP_NEW(0)) : p;
}

static s32 dpCountPads(PadRecord *pads)
{
    s32 n = 0;

    while (pads[n].plink != NULL)
    {
        n++;
    }
    return n;
}

static char sPadNames[128][8];
static u8 sPadOk[128];

static void dpPadBasics(PadRecord *out, s32 i, f32 yawx, f32 yawz)
{
    memset(out, 0, sizeof(*out));
    out->up.f[1] = 1.0f;
    out->look.f[0] = yawx;
    out->look.f[2] = yawz;
    snprintf(sPadNames[i & 127], sizeof(sPadNames[0]), "qdp%d", i);
    out->plink = sPadNames[i & 127];
}

/* New pad i at anchor + offset: same height as the anchor, floor tile found by walking the stan from the anchor's tile. */
static s32 dpMakePad(PadRecord *out, PadRecord *all, const DpPad *spec, s32 i)
{
    PadRecord *a = &all[dpPadIndex(spec->anchor)];
    StandTile *tile = (StandTile *) a->stan;
    f32 tx = a->pos.f[0] + spec->dx;
    f32 tz = a->pos.f[2] + spec->dz;
    f32 yaw = spec->yaw * (M_PI / 180.0f);
    s32 ok;

    dpPadBasics(out, i, sinf(yaw), cosf(yaw));
    out->pos.f[0] = tx;
    out->pos.f[1] = a->pos.f[1];
    out->pos.f[2] = tz;

    ok = (tile != NULL) && walkTilesBetweenPoints_NoCallback(&tile, a->pos.f[0], a->pos.f[2], tx, tz) && tile != NULL
         && isPointInsideTriStandTileUnscaled_Maybe(tile, tx, tz);
    if (!ok)
    {
        coord3d p = out->pos;
        tile = sub_GAME_7F0AFB78(&p.f[0], &p.f[1], &p.f[2], 0);
    }
    out->stan = tile;
    printf("ge: dam+: pad %d (new %d) at %.0f %.0f %.0f room %d%s\n", sStockPads + i, i, tx, out->pos.f[1], tz,
           tile ? getTileRoom(tile) : -1, ok ? "" : " (nearest tile, the walk from the anchor failed)");
    return tile != NULL;
}

/* A wing pad: a design point; its tile is ours (looked up in the design frame). */
static s32 dpMakeWingPad(PadRecord *out, u32 part, const DpWPad *spec, s32 i)
{
    f32 w[3], w2[3];
    f32 yaw = spec->yaw * (M_PI / 180.0f);
    f32 dx, dz, l;

    portDamPlusWingToWorld(part, spec->u, spec->v, spec->h + 51.0f, w);   /* stock pads sit ~51 units above their floor */
    portDamPlusWingToWorld(part, spec->u + 100.0f * sinf(yaw), spec->v + 100.0f * cosf(yaw), spec->h + 51.0f, w2);
    dx = w2[0] - w[0];
    dz = w2[2] - w[2];
    l = sqrtf(dx * dx + dz * dz);
    dpPadBasics(out, i, dx / l, dz / l);
    out->pos.f[0] = w[0];
    out->pos.f[1] = w[1];
    out->pos.f[2] = w[2];
    out->stan = portDamPlusWingTileAt(part, spec->u, spec->v, spec->h);
    if (out->stan == NULL)
    {
        printf("ge: dam+: wing 0x%x pad %d (%d, %d, %d) is on no tile\n", part, i, spec->u, spec->v, spec->h);
    }
    return out->stan != NULL;
}

static s32 dpPadOk(s16 p)
{
    if (p >= DP_LOCAL(0))
    {
        return sPadOk[sWingPadBase[sCurWing] - sStockPads + (p - DP_LOCAL(0))];
    }
    if (p >= DP_NEW(0))
    {
        return sPadOk[p - DP_NEW(0)];
    }
    return 1;
}

#define DP_NEXT(p) ((PropDefHeaderRecord *) (((u32 *) (p)) + sizepropdef(p)))

static PropDefHeaderRecord *dpFindGuard(PropDefHeaderRecord *defs, s32 chrnum)
{
    PropDefHeaderRecord *p;

    for (p = defs; p->type != PROPDEF_END; p = DP_NEXT(p))
    {
        if (p->type == PROPDEF_GUARD && ((GuardRecord *) p)->chrnum == chrnum)
        {
            return p;
        }
    }
    return NULL;
}

static PropDefHeaderRecord *dpFindType(PropDefHeaderRecord *defs, s32 type)
{
    PropDefHeaderRecord *p;

    for (p = defs; p->type != PROPDEF_END; p = DP_NEXT(p))
    {
        if (p->type == type)
        {
            return p;
        }
    }
    return NULL;
}

static PropDefHeaderRecord *dpFindGun(PropDefHeaderRecord *defs, s32 chrnum)
{
    PropDefHeaderRecord *p;

    for (p = defs; p->type != PROPDEF_END; p = DP_NEXT(p))
    {
        if (p->type == PROPDEF_COLLECTABLE && (((ObjectRecord *) p)->flags & PROPFLAG_ASSIGNEDTOCHR)
            && ((ObjectRecord *) p)->pad == chrnum)
        {
            return p;
        }
    }
    return NULL;
}

/* the object a stock tag (by id) points at, or NULL */
static PropDefHeaderRecord *dpFindTagged(PropDefHeaderRecord *defs, s32 tagid)
{
    PropDefHeaderRecord *p;
    s32 idx = 0;

    for (p = defs; p->type != PROPDEF_END; p = DP_NEXT(p), idx++)
    {
        if (p->type == PROPDEF_TAG && ((TagObjectRecord *) p)->ID == tagid)
        {
            s32 target = idx + ((TagObjectRecord *) p)->OffsetToObj, j = 0;
            PropDefHeaderRecord *q;

            for (q = defs; q->type != PROPDEF_END; q = DP_NEXT(q), j++)
            {
                if (j == target) return q;
            }
        }
    }
    return NULL;
}

/* Appends a copy of rec at *w; returns the copy. */
static u32 *dpEmit(u32 **w, PropDefHeaderRecord *rec)
{
    s32 n = sizepropdef(rec);
    u32 *dst = *w;

    memcpy(dst, rec, n * 4);
    *w += n;
    return dst;
}

static s32 dpEmitGuards(u32 **w, PropDefHeaderRecord *defs, const DpGuard *g, s32 n)
{
    s32 i, made = 0;
    PropDefHeaderRecord *p;

    for (i = 0; i < n; i++, g++)
    {
        PropDefHeaderRecord *t = dpFindGuard(defs, g->tmplchr);
        GuardRecord *gr;
        s32 padi = dpPadIndex(g->pad);

        if (t == NULL || !dpPadOk(g->pad))
        {
            printf("ge: dam+: guard %d skipped (no template chr %d or no pad)\n", g->chrnum, g->tmplchr);
            continue;
        }
        gr = (GuardRecord *) dpEmit(w, t);
        gr->chrnum = g->chrnum;
        gr->PadID = padi;
        gr->AIListID = g->ailist;
        gr->Preset = padi;
        gr->chrpreset1 = 0xffff;
        gr->Data = NULL;
        made++;

        /* the template's own guns, given to the clone */
        for (p = defs; p->type != PROPDEF_END; p = DP_NEXT(p))
        {
            if (p->type == PROPDEF_COLLECTABLE && (((ObjectRecord *) p)->flags & PROPFLAG_ASSIGNEDTOCHR)
                && ((ObjectRecord *) p)->pad == g->tmplchr)
            {
                ObjectRecord *o = (ObjectRecord *) dpEmit(w, p);
                o->pad = g->chrnum;
                o->prop = NULL;
                o->model = NULL;
            }
        }
    }
    return made;
}

static s32 dpEmitPickups(u32 **w, PropDefHeaderRecord *defs, const DpPickup *k, s32 n)
{
    s32 i, made = 0;

    for (i = 0; i < n; i++, k++)
    {
        PropDefHeaderRecord *t = (k->kind == DP_PICK_ARMOUR) ? dpFindType(defs, PROPDEF_ARMOUR) : dpFindGun(defs, k->tmplchr);
        ObjectRecord *o;

        if (t == NULL || !dpPadOk(k->pad))
        {
            continue;
        }
        o = (ObjectRecord *) dpEmit(w, t);
        o->pad = dpPadIndex(k->pad);
        o->flags &= ~PROPFLAG_ASSIGNEDTOCHR;
        o->flags2 &= ~0x000000F0;   /* the stock armours are "don't load" on harder difficulties: ours are on every one */
        o->prop = NULL;
        o->model = NULL;
        made++;
    }
    return made;
}

/* AI lists, patrol paths, waypoints: copies of the stock tables with ours after them */
static void dpAddAiLists(const DpAiList *add, s32 nadd)
{
    s32 n = 0, i;
    AIListRecord *al;

    while (g_CurrentSetup.ailists != NULL && g_CurrentSetup.ailists[n].ailist != NULL)
    {
        n++;
    }
    al = mempAllocBytesInBank((n + nadd + 1) * sizeof(AIListRecord), MEMPOOL_STAGE);
    if (n > 0)
    {
        memcpy(al, g_CurrentSetup.ailists, n * sizeof(AIListRecord));
    }
    for (i = 0; i < nadd; i++)
    {
        al[n + i].ailist = (AIRecord *) add[i].list;
        al[n + i].ID = add[i].id;
    }
    memset(&al[n + nadd], 0, sizeof(AIListRecord));
    g_CurrentSetup.ailists = al;
}

static void dpAddPaths(const DpPath *add, s32 nadd)
{
    s32 n = 0, i;
    PathRecord *pp;

    while (g_CurrentSetup.patrolpaths != NULL && g_CurrentSetup.patrolpaths[n].waypoints != NULL)
    {
        n++;
    }
    pp = mempAllocBytesInBank((n + nadd + 1) * sizeof(PathRecord), MEMPOOL_STAGE);
    if (n > 0)
    {
        memcpy(pp, g_CurrentSetup.patrolpaths, n * sizeof(PathRecord));
    }
    for (i = 0; i < nadd; i++)
    {
        s32 len = 0;

        while (add[i].waypoints[len] >= 0)
        {
            len++;
        }
        pp[n + i].waypoints = add[i].waypoints;
        pp[n + i].ID = add[i].id;
        pp[n + i].isLoop = 1;
        pp[n + i].len = len;
    }
    memset(&pp[n + nadd], 0, sizeof(PathRecord));
    g_CurrentSetup.patrolpaths = pp;
}

static s32 *dpListPlus(const s32 *stock, s32 extra)
{
    s32 n = 0;
    s32 *out;

    while (stock != NULL && stock[n] >= 0)
    {
        n++;
    }
    out = mempAllocBytesInBank((n + 2) * sizeof(s32), MEMPOOL_STAGE);
    if (n > 0)
    {
        memcpy(out, stock, n * sizeof(s32));
    }
    out[n] = extra;
    out[n + 1] = -1;
    return out;
}

/* a wing's waypoints (a chain over its first nwp pads) and its own group, joined to the nearest stock waypoint in the seam
 * room; returns the first new waypoint id, -1 if there is no graph to join */
static s32 dpAddWingWaypoints(const DpWingSetup *ws, s32 seamroom)
{
    s32 nw = 0, ng = 0, i, best = -1, nwp = ws->nwp;
    f32 bestd = 1e30f;
    waypoint *wp;
    waygroup *wg;
    s32 *members, *gnb;
    PadRecord *w0 = &g_CurrentSetup.pads[dpPadIndex(DP_LOCAL(0))];

    if (g_CurrentSetup.pathwaypoints == NULL || g_CurrentSetup.waypointgroups == NULL || nwp <= 0)
    {
        return -1;
    }
    while (g_CurrentSetup.pathwaypoints[nw].padID >= 0)
    {
        PadRecord *pd = &g_CurrentSetup.pads[g_CurrentSetup.pathwaypoints[nw].padID];

        if (pd->stan != NULL && getTileRoom((StandTile *) pd->stan) == seamroom)
        {
            f32 dx = pd->pos.f[0] - w0->pos.f[0], dz = pd->pos.f[2] - w0->pos.f[2];
            f32 d = dx * dx + dz * dz;

            if (d < bestd)
            {
                bestd = d;
                best = nw;
            }
        }
        nw++;
    }
    while (g_CurrentSetup.waypointgroups[ng].neighbours != NULL)
    {
        ng++;
    }
    if (best < 0)
    {
        return -1;
    }
    wp = mempAllocBytesInBank((nw + nwp + 1) * sizeof(waypoint), MEMPOOL_STAGE);
    memcpy(wp, g_CurrentSetup.pathwaypoints, nw * sizeof(waypoint));
    wg = mempAllocBytesInBank((ng + 2) * sizeof(waygroup), MEMPOOL_STAGE);
    memcpy(wg, g_CurrentSetup.waypointgroups, ng * sizeof(waygroup));
    members = mempAllocBytesInBank((nwp + 1) * sizeof(s32), MEMPOOL_STAGE);
    for (i = 0; i < nwp; i++)
    {
        s32 *nb = mempAllocBytesInBank(4 * sizeof(s32), MEMPOOL_STAGE);
        s32 k = 0;

        if (i == 0) nb[k++] = best;
        if (i > 0) nb[k++] = nw + i - 1;
        if (i < nwp - 1) nb[k++] = nw + i + 1;
        nb[k] = -1;
        wp[nw + i].padID = dpPadIndex(DP_LOCAL(i));
        wp[nw + i].neighbours = nb;
        wp[nw + i].groupNum = ng;
        wp[nw + i].dist = 0;
        members[i] = nw + i;
    }
    members[nwp] = -1;
    memset(&wp[nw + nwp], 0, sizeof(waypoint));
    wp[nw + nwp].padID = -1;
    /* the stock waypoint and its group learn about ours (new arrays; the stock ones are untouched) */
    wp[best].neighbours = dpListPlus(wp[best].neighbours, nw);
    gnb = mempAllocBytesInBank(2 * sizeof(s32), MEMPOOL_STAGE);
    gnb[0] = wp[best].groupNum;
    gnb[1] = -1;
    wg[wp[best].groupNum].neighbours = dpListPlus(wg[wp[best].groupNum].neighbours, ng);
    wg[ng].neighbours = gnb;
    wg[ng].waypoints = members;
    wg[ng].dist = 0;
    memset(&wg[ng + 1], 0, sizeof(waygroup));
    if (ws->joinLast)
    {   /* the far end: the last waypoint joins the nearest stock waypoint in the seam-1 room too */
        s32 room1 = portDamPlusWingSeamRoom(ws->part, 1), best2 = -1, j;
        f32 bd2 = 1e30f;
        PadRecord *wl = &g_CurrentSetup.pads[dpPadIndex(DP_LOCAL(nwp - 1))];

        for (j = 0; j < nw; j++)
        {
            PadRecord *pd = &g_CurrentSetup.pads[wp[j].padID];

            if (pd->stan != NULL && getTileRoom((StandTile *) pd->stan) == room1)
            {
                f32 dx = pd->pos.f[0] - wl->pos.f[0], dz = pd->pos.f[2] - wl->pos.f[2];
                if (dx * dx + dz * dz < bd2)
                {
                    bd2 = dx * dx + dz * dz;
                    best2 = j;
                }
            }
        }
        if (best2 >= 0)
        {
            s32 *nb = mempAllocBytesInBank(5 * sizeof(s32), MEMPOOL_STAGE), k = 0;

            if (nwp > 1) nb[k++] = nw + nwp - 2;
            nb[k++] = best2;
            nb[k] = -1;
            if (nwp > 1) wp[nw + nwp - 1].neighbours = nb;
            else wp[nw].neighbours = dpListPlus(wp[nw].neighbours, best2);
            wp[best2].neighbours = dpListPlus(wp[best2].neighbours, nw + nwp - 1);
            if (wp[best2].groupNum != wp[best].groupNum)
            {
                wg[ng].neighbours = dpListPlus(wg[ng].neighbours, wp[best2].groupNum);
                wg[wp[best2].groupNum].neighbours = dpListPlus(wg[wp[best2].groupNum].neighbours, ng);
            }
            else
            {
                /* already a neighbour group: no change */
            }
            printf("ge: dam+: wing 0x%x: last waypoint %d joined to stock waypoint %d (pad %d)\n", ws->part, nw + nwp - 1, best2,
                   wp[best2].padID);
        }
    }
    g_CurrentSetup.pathwaypoints = wp;
    g_CurrentSetup.waypointgroups = wg;
    printf("ge: dam+: wing 0x%x: waypoints %d-%d (group %d) joined to stock waypoint %d (pad %d)\n", ws->part, nw, nw + nwp - 1, ng,
           best, wp[best].padID);
    return nw;
}

/* our list in place of a stock one (by id): in the AI-list table, and in the background chr already running it */
static void dpReplaceAiList(u16 id, u8 *list)
{
    s32 i;
    AIRecord *old = NULL;

    for (i = 0; g_CurrentSetup.ailists != NULL && g_CurrentSetup.ailists[i].ailist != NULL; i++)
    {
        if (g_CurrentSetup.ailists[i].ID == id)
        {
            old = g_CurrentSetup.ailists[i].ailist;
            g_CurrentSetup.ailists[i].ailist = (AIRecord *) list;
        }
    }
    for (i = 0; old != NULL && g_ActiveChrs != NULL && i < g_ActiveChrsCount; i++)
    {
        if (g_ActiveChrs[i].ailist == old)
        {
            g_ActiveChrs[i].ailist = (AIRecord *) list;
            g_ActiveChrs[i].aioffset = 0;
        }
    }
    printf("ge: dam+: list 0x%04x replaced%s\n", id, old ? "" : " (NOT FOUND)");
}

/* The setup hook: proplvreset2 at "PD rejoins here" (prop.c), after every stock pointer is promoted and the pads are scaled
 * and resolved, before guards and objects are counted and created. */
void portDamPlusSetup(s32 stageId)
{
    PadRecord *pads;
    s32 npads, nnew, nwingpads = 0, i, ok, wi;
    PropDefHeaderRecord *defs = g_CurrentSetup.propDefs;
    PropDefHeaderRecord *p;
    u32 stockwords, extrawords;
    u32 *newdefs, *w;
    s32 nguards = 0, npick = 0, nprops = 0, anywing = 0;
    s32 night;

    night = (sParts & DP_NIGHT) != 0;
    for (wi = 0; wi < (s32) ARRAYCOUNT(sWingSetups); wi++)
    {
        sWingOn[wi] = (sParts & sWingSetups[wi]->part) && portDamPlusWingReady(sWingSetups[wi]->part);
        anywing |= sWingOn[wi];
    }
    sConsoleRec = NULL;
    sConsoleIdx = -1;
    sCallsSeen = 0;
    if (stageId != LEVELID_DAM || (!night && !anywing) || defs == NULL || g_CurrentSetup.pads == NULL)
    {
        sOursFrom = sOursTo = NULL;
        return;
    }

    /* pads: stock, then night pads, then each wing's pads */
    sStockPads = npads = dpCountPads(g_CurrentSetup.pads);
    nnew = night ? ARRAYCOUNT(sNightPads) : 0;
    sNightPadsOn = nnew;
    for (wi = 0; wi < (s32) ARRAYCOUNT(sWingSetups); wi++)
    {
        sWingPadBase[wi] = npads + nnew + nwingpads;
        if (sWingOn[wi])
        {
            nwingpads += sWingSetups[wi]->npads;
        }
    }
    pads = mempAllocBytesInBank((npads + nnew + nwingpads + 1) * sizeof(PadRecord), MEMPOOL_STAGE);
    memcpy(pads, g_CurrentSetup.pads, npads * sizeof(PadRecord));
    for (i = 0; i < nnew; i++)
    {
        sPadOk[i] = dpMakePad(&pads[npads + i], pads, &sNightPads[i], i);
    }
    for (wi = 0; wi < (s32) ARRAYCOUNT(sWingSetups); wi++)
    {
        const DpWingSetup *ws = sWingSetups[wi];

        if (!sWingOn[wi]) continue;
        for (i = 0; i < ws->npads; i++)
        {
            s32 k = sWingPadBase[wi] - npads + i;
            sPadOk[k & 127] = dpMakeWingPad(&pads[npads + k], ws->part, &ws->pads[i], k);
        }
    }
    memset(&pads[npads + nnew + nwingpads], 0, sizeof(PadRecord));
    g_CurrentSetup.pads = pads;

    /* AI lists, patrol paths, waypoints */
    if (night)
    {
        dpAddAiLists(sNightAi, ARRAYCOUNT(sNightAi));
        dpAddPaths(sNightPaths, ARRAYCOUNT(sNightPaths));
    }
    for (wi = 0; wi < (s32) ARRAYCOUNT(sWingSetups); wi++)
    {
        const DpWingSetup *ws = sWingSetups[wi];
        s32 w0;

        if (!sWingOn[wi]) continue;
        sCurWing = wi;
        w0 = dpAddWingWaypoints(ws, portDamPlusWingSeamRoom(ws->part, 0));
        if (w0 >= 0 && ws->npatrol > 0)
        {
            DpAiList *al = mempAllocBytesInBank(sizeof(DpAiList), MEMPOOL_STAGE);
            DpPath *pth = mempAllocBytesInBank(sizeof(DpPath), MEMPOOL_STAGE);
            s32 *wps = mempAllocBytesInBank((ws->npatrol + 1) * sizeof(s32), MEMPOOL_STAGE);

            for (i = 0; i < ws->npatrol; i++)
            {
                wps[i] = w0 + ws->patrolSeq[i];
            }
            wps[ws->npatrol] = -1;
            al->list = ws->patrolAiBytes;
            al->id = ws->patrolAi;
            pth->waypoints = wps;
            pth->id = ws->patrolPath;
            dpAddAiLists(al, 1);
            dpAddPaths(pth, 1);
        }
    }

    /* propdefs: stock records unchanged and in place, ours appended before the END record */
    stockwords = 0;
    for (p = defs; p->type != PROPDEF_END; p = DP_NEXT(p))
    {
        stockwords += sizepropdef(p);
    }
    extrawords = (ARRAYCOUNT(sNightGuards) + 16) * (0x22 * 4 + 16) + (ARRAYCOUNT(sNightPickups) + 64) * 0x40;
    for (wi = 0; wi < (s32) ARRAYCOUNT(sWingSetups); wi++)
    {
        extrawords += sWingSetups[wi]->nguards * (0x22 * 4 + 16) + (sWingSetups[wi]->npickups + sWingSetups[wi]->nprops + 4) * 0x40;
    }
    newdefs = mempAllocBytesInBank((stockwords + extrawords + 1) * 4, MEMPOOL_STAGE);
    memcpy(newdefs, defs, stockwords * 4);
    w = newdefs + stockwords;
    sOursFrom = w;
    sKeyRec = NULL;
    sDoorRec = NULL;

    if (night)
    {
        nguards += dpEmitGuards(&w, defs, sNightGuards, ARRAYCOUNT(sNightGuards));
        npick += dpEmitPickups(&w, defs, sNightPickups, ARRAYCOUNT(sNightPickups));
    }
    for (wi = 0; wi < (s32) ARRAYCOUNT(sWingSetups); wi++)
    {
        const DpWingSetup *ws = sWingSetups[wi];
        PropDefHeaderRecord *tprop = dpFindType(defs, PROPDEF_PROP);

        if (!sWingOn[wi]) continue;
        sCurWing = wi;
        nguards += dpEmitGuards(&w, defs, ws->guards, ws->nguards);
        npick += dpEmitPickups(&w, defs, ws->pickups, ws->npickups);
        for (i = 0; tprop != NULL && i < ws->nprops; i++)
        {
            const DpProp *pr = &ws->props[i];
            ObjectRecord *o;

            if (!dpPadOk(pr->pad))
            {
                continue;
            }
            if (pr->kind == DP_PROP_CONSOLE)
            {
                /* a destructible console: the record of the data-backup mainframe (stock tag 6) with our model, tagged 20 */
                PropDefHeaderRecord *tm = dpFindTagged(defs, 6);
                TagObjectRecord *tag;

                if (tm == NULL)
                {
                    printf("ge: dam+: no stock tag 6 object to make the console from\n");
                    continue;
                }
                tag = (TagObjectRecord *) dpEmit(&w, dpFindType(defs, PROPDEF_TAG));
                tag->ID = DP_COMMS_TAG;
                tag->OffsetToObj = 1;               /* the record right after it */
                tag->NextTag = NULL;
                tag->TaggedObject = NULL;
                o = (ObjectRecord *) dpEmit(&w, tm);
                o->obj = pr->model;
                o->pad = dpPadIndex(pr->pad);
                o->prop = NULL;
                o->model = NULL;
                sConsoleRec = o;
                {
                    u32 *q;
                    s32 idx = 0;
                    for (q = newdefs; q < (u32 *) o; q += sizepropdef((PropDefHeaderRecord *) q)) idx++;
                    sConsoleIdx = idx;
                }
                printf("ge: dam+: comms console: object %d (model %d), tag %d\n", sConsoleIdx, pr->model, DP_COMMS_TAG);
            }
            else
            {
                o = (ObjectRecord *) dpEmit(&w, tprop);
                o->extrascale = pr->scale ? pr->scale : 256;
                o->obj = pr->model;
                o->pad = dpPadIndex(pr->pad);
                o->flags = 0;
                o->flags2 = 0;
                o->prop = NULL;
                o->model = NULL;
            }
            nprops++;
        }
        if (ws->part == DP_MOTORPOOL)
        {
            /* the shutter: culls the motor pool while it is shut (it used to leave its area always visible), and its key is on
             * the guard by the truck */
            PropDefHeaderRecord *door = defs;

            for (i = 0; i < 281 && door->type != PROPDEF_END; i++)
            {
                door = DP_NEXT(door);
            }
            if (door->type == PROPDEF_DOOR)
            {
                ObjectRecord *d = (ObjectRecord *) door;
                ObjectRecord *k = (ObjectRecord *) ((u32 *) newdefs + ((u32 *) door - (u32 *) defs));
                KeyRecord *key = (KeyRecord *) w;
                ObjectRecord *ko = (ObjectRecord *) key;

                k->flags = (d->flags & ~PROPFLAG_NO_PORTAL_CLOSE) | PROPFLAG_CULL_BEHIND_DOOR;
                memset(key, 0, sizeof(KeyRecord));
                ko->extrascale = 256;
                ko->type = PROPDEF_KEY;
                ko->obj = PROP_SWIPE_CARD2;
                ko->pad = 6;
                ko->flags = PROPFLAG_ASSIGNEDTOCHR;
                *(s32 *) &ko->damage = 1000 << 16;   /* the raw setup form: domakedefaultobj converts it */
                key->keyflags = ((DoorRecord *) door)->keyflags ? ((DoorRecord *) door)->keyflags : 0x80;
                w += sizepropdef((PropDefHeaderRecord *) key);
                sKeyRec = ko;
                sDoorRec = (DoorRecord *) k;
                printf("ge: dam+: door 281: culls the motor pool when shut; its key (0x%x) is on chr 6\n", key->keyflags);
            }
        }
        if (ws->part == DP_COMMS && sConsoleRec != NULL)
        {
            dpReplaceAiList(0x1002, sAiComms);
        }
    }

    /* the END record */
    sOursTo = w;
    *w++ = ((u32 *) defs)[stockwords];
    g_CurrentSetup.propDefs = (PropDefHeaderRecord *) newdefs;

    ok = (u32) (w - newdefs) <= stockwords + extrawords + 1;
    printf("ge: dam+: setup: %d pads, %d guards, %d pickups, %d props appended (%u + %u propdef words)%s\n",
           nnew + nwingpads, nguards, npick, nprops, stockwords, (u32) (w - newdefs) - stockwords - 1, ok ? "" : " OVERFLOW");
    fflush(stdout);
}

/* proplvreset2's end: did every appended record become a live object? (guards spawned, objects have props, the key sits on its
 * chr, the shutter found its portal) */
void portDamPlusVerify(void)
{
    u32 *w;
    s32 guards = 0, guardsLive = 0, objs = 0, objsLive = 0;

    if (sOursFrom == NULL || sOursTo == NULL || sParts == 0)
    {
        return;
    }
    for (w = sOursFrom; w < sOursTo; w += sizepropdef((PropDefHeaderRecord *) w))
    {
        PropDefHeaderRecord *p = (PropDefHeaderRecord *) w;

        if (p->type == PROPDEF_GUARD)
        {
            guards++;
            guardsLive += ((GuardRecord *) p)->Data != NULL;
        }
        else if (p->type != PROPDEF_TAG)
        {
            objs++;
            objsLive += ((ObjectRecord *) p)->prop != NULL;
        }
    }
    printf("ge: dam+: verify: guards %d/%d spawned, objects %d/%d live", guardsLive, guards, objsLive, objs);
    if (sKeyRec != NULL)
    {
        ChrRecord *c = chrFindByLiteralId(6);

        printf(", shutter key %s chr %d", (sKeyRec->prop != NULL && c != NULL && c->prop != NULL && sKeyRec->prop->parent == c->prop)
               ? "held by" : "NOT on", 6);
    }
    if (sDoorRec != NULL)
    {
        printf(", shutter portal %d", sDoorRec->portalNumber);
    }
    if (sConsoleRec != NULL)
    {
        printf(", comms console %s (tagged %d)", sConsoleRec->prop != NULL ? "live" : "NOT LIVE",
               (sConsoleRec->runtime_bitflags & RUNTIMEBITFLAG_TAGGED) != 0);
    }
    printf("\n");
    fflush(stdout);
    sOursFrom = sOursTo = NULL;
}

/* ---- per tick (lvlTick, after chrpropTick) ------------------------------------------------------------------------------------ */
/* GE_DAMPLUS_TEST=comms[:alarm_s:destroy_s] (tests only): sound the alarm at alarm_s (and every 10 s after, so it keeps sounding)
 * and shoot the comms console at destroy_s through the stock damage path; the calls are logged either way. */
static void dpCommsTick(void)
{
    static const char *test;
    static s32 alarmAt = 5, destroyAt = 60, parsed;
    f32 t = g_GlobalTimer / 60.0f;
    s32 k;

    if (!parsed)
    {
        parsed = 1;
        test = getenv("GE_DAMPLUS_TEST");
        if (test != NULL && strncmp(test, "comms", 5) == 0)
        {
            sscanf(test + 5, ":%d:%d", &alarmAt, &destroyAt);
        }
        else
        {
            test = NULL;
        }
    }
    for (k = 0; k < DP_COMMS_CALLS; k++)
    {
        u32 f = (u32) DP_COMMS_CALL_FLAG0 << k;
        if ((objectiveregisters1 & f) && !(sCallsSeen & f))
        {
            sCallsSeen |= f;
            printf("ge: dam+: comms: reinforcement call %d at %.1f s (alarm %s, console %s)\n", k + 1, t, alarmIsActive() ? "on" : "off",
                   (sConsoleRec != NULL && sConsoleRec->prop != NULL && objIsHealthy(sConsoleRec)) ? "standing" : "destroyed");
            fflush(stdout);
        }
    }
    if (test != NULL && sConsoleRec != NULL)
    {
        static s32 lastAlarm = -100, destroyed;

        if (t >= alarmAt && t - lastAlarm >= 10.0f)
        {
            lastAlarm = (s32) t;
            if (!alarmIsActive())
            {
                alarmActivate();
                printf("ge: dam+: test: alarm on at %.1f s\n", t);
            }
        }
        if (!destroyed && t >= destroyAt && sConsoleRec->prop != NULL)
        {
            coord3d pos = sConsoleRec->prop->pos;

            destroyed = 1;
            objApplyDamage(sConsoleRec, 1000.0f, &pos, ITEM_WPPK, 0);
            printf("ge: dam+: test: console shot at %.1f s: %s\n", t, objIsHealthy(sConsoleRec) ? "still standing" : "destroyed");
            fflush(stdout);
        }
    }
}

void portDamPlusTick(void)
{
    if (sParts == 0 || g_ClockTimer <= 0)
    {
        return;
    }
    if (sConsoleRec != NULL)
    {
        dpCommsTick();
    }
}

#endif
