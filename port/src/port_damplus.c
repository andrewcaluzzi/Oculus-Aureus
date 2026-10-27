/* port_damplus.c - Dam+: an expanded Dam behind an opt-in switch (features: midas-notes features/dam-expansion.md).
 *
 * Stock Dam still comes from the user's ROM. This file only APPENDS to it at load time:
 *   slice 1 "Night Shift": extra pads (anchored to stock pads by id plus an offset, floor tile found by walking the stock stan
 *   from the anchor's tile), extra guards cloned from a stock guard record read at run time (with that guard's own weapon
 *   records), new patrol paths over the stock waypoints, a few floor pickups cloned from stock records.
 * Nothing stock is renumbered: new pads, propdefs, AI lists and patrol paths go after the stock ones, so every objective,
 * tag, flag, the intro/outro and the bungee keep their ids.
 *
 *   slice 2 "Motor pool": the start shutter (door 281) opens onto two generated rooms (port_damplus_geo.c); here: its key on
 *   the truck guard, the door culling them, pads on the new floor, guards, props, pickups, waypoints and a patrol.
 *
 * Switch: GE_DAMPLUS (unset/0 = off; 1 = everything; "night" = slice 1 only; "motor" = slice 2 only). Off in attract demos, while recording, in
 * multiplayer and on every stage but Dam. Tests and the dam1 oracle never set it. */
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

extern s32 getPlayerCount(void);
extern s32 get_is_ramrom_flag(void);
extern s32 get_recording_ramrom_flag(void);
extern s32 gamemode;
extern const char *portModeChoice(void);
extern s32 sizepropdef(PropDefHeaderRecord *pdef);
extern stagesetup g_CurrentSetup;
extern ChrRecord *chrFindByLiteralId(s32 id);

#define DP_NIGHT 1      /* slice 1: Night Shift (setup remix) */
#define DP_MOTORPOOL 2  /* slice 2: the motor pool behind door 281 */

static s32 sParts;      /* parts on for the stage being loaded (0 = stock Dam); set once per stage load */

/* What GE_DAMPLUS asks for, whatever the stage. */
static s32 portDamPlusWanted(void)
{
    const char *e = getenv("GE_DAMPLUS");

    if (e == NULL || e[0] == '\0' || e[0] == '0')
    {
        return 0;
    }
    if (strcmp(e, "night") == 0)
    {
        return DP_NIGHT;
    }
    if (strcmp(e, "motor") == 0)
    {
        return DP_MOTORPOOL;
    }
    return DP_NIGHT | DP_MOTORPOOL;
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
#define DP_END_PATH (-1)

typedef struct
{
    s16 anchor;     /* stock pad id, or DP_NEW(n) */
    s16 dx, dz;     /* offset from the anchor, world units */
    s16 yaw;        /* facing, degrees (0 = +z) */
} DpPad;

typedef struct
{
    s16 pad;        /* stock pad id, or DP_NEW(n) */
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
 * Slice 2 data: the motor pool behind door 281 (geometry: port_damplus_geo.c). Pads here are DESIGN points in the door-local
 * frame (world units: u along the shutter, v into the rock), DP_MP(n) names them. */

#define DP_MP(n) (0x4400 + (n))

typedef struct
{
    s16 u, v;
    s16 yaw;        /* facing in the frame, degrees: 0 = +v (deeper in), 90 = +u */
} DpDesignPad;

static const DpDesignPad sMotorPads[] = {
    /* 0-6: waypoints, the shutter to the barracks */
    { 450, 140, 0 }, { 520, 590, 270 }, { -150, 590, 270 }, { -800, 1200, 0 }, { -900, 1860, 0 }, { -900, 2200, 0 },
    { -900, 2600, 180 },
    /* 7-10: guards */
    { -380, 1050, 120 }, { -1450, 1600, 150 }, { -1250, 2700, 160 }, { -500, 2450, 220 },
    /* 11-13: pickups in the barracks */
    { -1400, 2820, 0 }, { -420, 2820, 0 }, { -420, 2120, 0 },
    /* 14-25: props: the second truck, drums, crates, lockers, a table */
    { -1050, 1250, 0 }, { -1610, 560, 0 }, { -1610, 680, 0 }, { -1500, 600, 0 }, { -1620, 1900, 90 }, { -250, 1920, 90 },
    { -1430, 2850, 180 }, { -1270, 2850, 180 }, { -1110, 2850, 180 }, { -900, 2420, 0 }, { -780, 2440, 270 }, { -1020, 2440, 90 },
};

/* waypoints: pads DP_MP(0..6), a chain; W0 joins the nearest stock waypoint in the shutter's room */
#define DP_NMWP 7

static const DpGuard sMotorGuards[] = {
    /* the bay: two idle guards by the truck and at the back, one walking between the bay and the barracks */
    { DP_MP(7), 0x0002, 7, 120 },
    { DP_MP(8), 0x0002, 7, 121 },
    { DP_MP(2), 0x0484, 7, 122 },
    /* the barracks: an armoured sergeant (Dam's own ai_11: more health, armour, kneels to fire) and a guard */
    { DP_MP(9), 0x040c, 14, 123 },
    { DP_MP(10), 0x0002, 22, 124 },
};

static const DpPickup sMotorPickups[] = {
    { DP_PICK_ARMOUR, DP_MP(11), 0 },  /* the locker's prize: body armour */
    { DP_PICK_WEAPON, DP_MP(12), 7 },  /* and two KF7s (ammo) */
    { DP_PICK_WEAPON, DP_MP(13), 7 },
};

typedef struct
{
    s16 pad;
    s16 model;
} DpProp;

static const DpProp sMotorProps[] = {
    { DP_MP(14), PROP_MILTRUCK },
    { DP_MP(15), PROP_OIL_DRUM1 }, { DP_MP(16), PROP_OIL_DRUM2 }, { DP_MP(17), PROP_OIL_DRUM7 },
    { DP_MP(18), PROP_METAL_CRATE1 }, { DP_MP(19), PROP_AMMO_CRATE1 },
    { DP_MP(20), PROP_LOCKER3 }, { DP_MP(21), PROP_LOCKER4 }, { DP_MP(22), PROP_LOCKER3 },
    { DP_MP(23), PROP_WOODEN_TABLE1 }, { DP_MP(24), PROP_METAL_CHAIR1 }, { DP_MP(25), PROP_METAL_CHAIR1 },
};

#define DP_DOOR 281         /* the start shutter */
#define DP_DOOR_KEY 0x80    /* its stock key bits (no Dam object carries them) */
#define DP_KEY_CHR 6        /* the stock guard by the truck at the start gets the key */

static u8 sAiPatrol12[] = { DP_AI_PATROL(12) };

extern s32 portDamPlusMotorPoolReady(void);
extern void portDamPlusDesignToWorld(f32 u, f32 v, f32 h, f32 out[3]);
extern StandTile *portDamPlusTileAt(f32 u, f32 v);

/* --------------------------------------------------------------------------------------------------------------------------- */

static s32 sStockPads;   /* stock pad count of the stage being loaded */
static u32 *sOursFrom, *sOursTo;       /* our appended propdef records (this stage load), for portDamPlusVerify */
static ObjectRecord *sKeyRec;
static DoorRecord *sDoorRec;
static s32 sNightPadsOn; /* how many night pads were appended (0 when slice 1 is off) */

static s32 dpPadIndex(s16 p)
{
    if (p >= DP_MP(0))
    {
        return sStockPads + sNightPadsOn + (p - DP_MP(0));
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

static char sPadNames[64][8];
static u8 sPadOk[64];

static void dpPadBasics(PadRecord *out, s32 i, f32 yawx, f32 yawz)
{
    memset(out, 0, sizeof(*out));
    out->up.f[1] = 1.0f;
    out->look.f[0] = yawx;
    out->look.f[2] = yawz;
    snprintf(sPadNames[i & 63], sizeof(sPadNames[0]), "qdp%d", i);
    out->plink = sPadNames[i & 63];
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

/* A motor-pool pad: a design point, its tile is ours (looked up in the design frame). */
static s32 dpMakeDesignPad(PadRecord *out, const DpDesignPad *spec, s32 i)
{
    f32 w[3], w2[3];
    f32 yaw = spec->yaw * (M_PI / 180.0f);
    f32 dx, dz, l;

    portDamPlusDesignToWorld(spec->u, spec->v, 51.0f, w);   /* stock pads sit ~51 units above their floor */
    portDamPlusDesignToWorld(spec->u + 100.0f * sinf(yaw), spec->v + 100.0f * cosf(yaw), 51.0f, w2);
    dx = w2[0] - w[0];
    dz = w2[2] - w[2];
    l = sqrtf(dx * dx + dz * dz);
    dpPadBasics(out, i, dx / l, dz / l);
    out->pos.f[0] = w[0];
    out->pos.f[1] = w[1];
    out->pos.f[2] = w[2];
    out->stan = portDamPlusTileAt(spec->u, spec->v);
    if (out->stan == NULL)
    {
        printf("ge: dam+: motor-pool pad %d (%d, %d) is on no tile\n", i, spec->u, spec->v);
    }
    return out->stan != NULL;
}

static s32 dpPadOk(s16 p)
{
    if (p >= DP_MP(0))
    {
        return sPadOk[sNightPadsOn + (p - DP_MP(0))];
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
    s32 n = 0;
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
    memcpy(al + n, add, 0);
    {
        s32 i;
        for (i = 0; i < nadd; i++)
        {
            al[n + i].ailist = (AIRecord *) add[i].list;
            al[n + i].ID = add[i].id;
        }
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

/* the motor pool's waypoints (a chain over DP_MP(0..6)) and its own group, joined to the nearest stock waypoint in the
 * shutter's room; returns the first new waypoint id, -1 if there is no graph to join */
static s32 dpAddMotorWaypoints(s32 seamroom)
{
    s32 nw = 0, ng = 0, i, best = -1;
    f32 bestd = 1e30f;
    waypoint *wp;
    waygroup *wg;
    s32 *members, *gnb;
    PadRecord *w0 = &g_CurrentSetup.pads[dpPadIndex(DP_MP(0))];

    if (g_CurrentSetup.pathwaypoints == NULL || g_CurrentSetup.waypointgroups == NULL)
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
    wp = mempAllocBytesInBank((nw + DP_NMWP + 1) * sizeof(waypoint), MEMPOOL_STAGE);
    memcpy(wp, g_CurrentSetup.pathwaypoints, nw * sizeof(waypoint));
    wg = mempAllocBytesInBank((ng + 2) * sizeof(waygroup), MEMPOOL_STAGE);
    memcpy(wg, g_CurrentSetup.waypointgroups, ng * sizeof(waygroup));
    members = mempAllocBytesInBank((DP_NMWP + 1) * sizeof(s32), MEMPOOL_STAGE);
    for (i = 0; i < DP_NMWP; i++)
    {
        s32 *nb = mempAllocBytesInBank(4 * sizeof(s32), MEMPOOL_STAGE);
        s32 k = 0;

        if (i == 0) nb[k++] = best;
        if (i > 0) nb[k++] = nw + i - 1;
        if (i < DP_NMWP - 1) nb[k++] = nw + i + 1;
        nb[k] = -1;
        wp[nw + i].padID = dpPadIndex(DP_MP(i));
        wp[nw + i].neighbours = nb;
        wp[nw + i].groupNum = ng;
        wp[nw + i].dist = 0;
        members[i] = nw + i;
    }
    members[DP_NMWP] = -1;
    memset(&wp[nw + DP_NMWP], 0, sizeof(waypoint));
    wp[nw + DP_NMWP].padID = -1;
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
    g_CurrentSetup.pathwaypoints = wp;
    g_CurrentSetup.waypointgroups = wg;
    printf("ge: dam+: motor pool: waypoints %d-%d (group %d) joined to stock waypoint %d (pad %d)\n", nw, nw + DP_NMWP - 1, ng, best,
           wp[best].padID);
    return nw;
}

/* The setup hook: proplvreset2 at "PD rejoins here" (prop.c), after every stock pointer is promoted and the pads are scaled
 * and resolved, before guards and objects are counted and created. */
void portDamPlusSetup(s32 stageId)
{
    PadRecord *pads;
    s32 npads, nnew, nmotor, i, ok;
    PropDefHeaderRecord *defs = g_CurrentSetup.propDefs;
    PropDefHeaderRecord *p;
    u32 stockwords, extrawords;
    u32 *newdefs, *w;
    s32 nguards = 0, npick = 0, nprops = 0, motor;
    s32 night;

    night = (sParts & DP_NIGHT) != 0;
    motor = (sParts & DP_MOTORPOOL) && portDamPlusMotorPoolReady();
    if (stageId != LEVELID_DAM || (!night && !motor) || defs == NULL || g_CurrentSetup.pads == NULL)
    {
        sOursFrom = sOursTo = NULL;
        return;
    }

    /* pads: stock, then night pads, then motor-pool pads */
    sStockPads = npads = dpCountPads(g_CurrentSetup.pads);
    nnew = night ? ARRAYCOUNT(sNightPads) : 0;
    nmotor = motor ? ARRAYCOUNT(sMotorPads) : 0;
    sNightPadsOn = nnew;
    pads = mempAllocBytesInBank((npads + nnew + nmotor + 1) * sizeof(PadRecord), MEMPOOL_STAGE);
    memcpy(pads, g_CurrentSetup.pads, npads * sizeof(PadRecord));
    for (i = 0; i < nnew; i++)
    {
        sPadOk[i] = dpMakePad(&pads[npads + i], pads, &sNightPads[i], i);
    }
    for (i = 0; i < nmotor; i++)
    {
        sPadOk[nnew + i] = dpMakeDesignPad(&pads[npads + nnew + i], &sMotorPads[i], nnew + i);
    }
    memset(&pads[npads + nnew + nmotor], 0, sizeof(PadRecord));
    g_CurrentSetup.pads = pads;

    /* AI lists and patrol paths */
    if (night)
    {
        dpAddAiLists(sNightAi, ARRAYCOUNT(sNightAi));
        dpAddPaths(sNightPaths, ARRAYCOUNT(sNightPaths));
    }
    if (motor)
    {
        extern s32 portDamPlusSeamRoom(void);
        s32 w0 = dpAddMotorWaypoints(portDamPlusSeamRoom());
        if (w0 >= 0)
        {
            static DpAiList ai12[1];
            static DpPath path12[1];
            s32 *wps = mempAllocBytesInBank(9 * sizeof(s32), MEMPOOL_STAGE);
            s32 seq[8] = { 2, 3, 4, 5, 6, 5, 4, 3 };

            for (i = 0; i < 8; i++)
            {
                wps[i] = w0 + seq[i];
            }
            wps[8] = -1;
            ai12[0].list = sAiPatrol12;
            ai12[0].id = 0x0484;
            path12[0].waypoints = wps;
            path12[0].id = 12;
            dpAddAiLists(ai12, 1);
            dpAddPaths(path12, 1);
        }
    }

    /* propdefs: stock records unchanged and in place, ours appended before the END record */
    stockwords = 0;
    for (p = defs; p->type != PROPDEF_END; p = DP_NEXT(p))
    {
        stockwords += sizepropdef(p);
    }
    extrawords = (ARRAYCOUNT(sNightGuards) + ARRAYCOUNT(sMotorGuards)) * (0x22 * 4 + 16)
               + (ARRAYCOUNT(sNightPickups) + ARRAYCOUNT(sMotorPickups) + ARRAYCOUNT(sMotorProps) + 2) * 0x40;
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
    if (motor)
    {
        PropDefHeaderRecord *door = defs, *tprop = dpFindType(defs, PROPDEF_PROP);

        nguards += dpEmitGuards(&w, defs, sMotorGuards, ARRAYCOUNT(sMotorGuards));
        npick += dpEmitPickups(&w, defs, sMotorPickups, ARRAYCOUNT(sMotorPickups));
        for (i = 0; tprop != NULL && i < (s32) ARRAYCOUNT(sMotorProps); i++)
        {
            ObjectRecord *o;

            if (!dpPadOk(sMotorProps[i].pad))
            {
                continue;
            }
            o = (ObjectRecord *) dpEmit(&w, tprop);
            o->extrascale = 256;
            o->obj = sMotorProps[i].model;
            o->pad = dpPadIndex(sMotorProps[i].pad);
            o->flags = 0;
            o->flags2 = 0;
            o->prop = NULL;
            o->model = NULL;
            nprops++;
        }
        /* the shutter: culls the motor pool while it is shut (it used to leave its area always visible), and its key is on the
         * guard by the truck */
        for (i = 0; i < DP_DOOR && door->type != PROPDEF_END; i++)
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
            ko->pad = DP_KEY_CHR;
            ko->flags = PROPFLAG_ASSIGNEDTOCHR;
            *(s32 *) &ko->damage = 1000 << 16;   /* the raw setup form: domakedefaultobj converts it */
            key->keyflags = ((DoorRecord *) door)->keyflags ? ((DoorRecord *) door)->keyflags : DP_DOOR_KEY;
            w += sizepropdef((PropDefHeaderRecord *) key);
            sKeyRec = ko;
            sDoorRec = (DoorRecord *) k;
            printf("ge: dam+: door %d: culls the motor pool when shut; its key (0x%x) is on chr %d\n", DP_DOOR, key->keyflags, DP_KEY_CHR);
        }
    }

    /* the END record */
    sOursTo = w;
    *w++ = ((u32 *) defs)[stockwords];
    g_CurrentSetup.propDefs = (PropDefHeaderRecord *) newdefs;

    ok = (u32) (w - newdefs) <= stockwords + extrawords + 1;
    printf("ge: dam+: setup: %d pads, %d guards, %d pickups, %d props appended (%u + %u propdef words)%s\n",
           nnew + nmotor, nguards, npick, nprops, stockwords, (u32) (w - newdefs) - stockwords - 1, ok ? "" : " OVERFLOW");
    fflush(stdout);
}

/* proplvreset2's end: did every appended record become a live object? (guards spawned, objects have props, the key sits on its
 * chr, the shutter found its portal) */
void portDamPlusVerify(void)
{
    u32 *w;
    s32 guards = 0, guardsLive = 0, objs = 0, objsLive = 0;

    if (sOursFrom == NULL || sOursTo == NULL || !(sParts & (DP_NIGHT | DP_MOTORPOOL)))
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
        else
        {
            objs++;
            objsLive += ((ObjectRecord *) p)->prop != NULL;
        }
    }
    printf("ge: dam+: verify: guards %d/%d spawned, objects %d/%d live", guardsLive, guards, objsLive, objs);
    if (sKeyRec != NULL)
    {
        ChrRecord *c = chrFindByLiteralId(DP_KEY_CHR);

        printf(", shutter key %s chr %d", (sKeyRec->prop != NULL && c != NULL && c->prop != NULL && sKeyRec->prop->parent == c->prop)
               ? "held by" : "NOT on", DP_KEY_CHR);
    }
    if (sDoorRec != NULL)
    {
        printf(", shutter portal %d", sDoorRec->portalNumber);
    }
    printf("\n");
    fflush(stdout);
    sOursFrom = sOursTo = NULL;
}

#endif
