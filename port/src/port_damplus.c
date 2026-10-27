/* port_damplus.c - Dam+: an expanded Dam behind an opt-in switch (features: midas-notes features/dam-expansion.md).
 *
 * Stock Dam still comes from the user's ROM. This file only APPENDS to it at load time:
 *   slice 1 "Night Shift": extra pads (anchored to stock pads by id plus an offset, floor tile found by walking the stock stan
 *   from the anchor's tile), extra guards cloned from a stock guard record read at run time (with that guard's own weapon
 *   records), new patrol paths over the stock waypoints, a few floor pickups cloned from stock records.
 * Nothing stock is renumbered: new pads, propdefs, AI lists and patrol paths go after the stock ones, so every objective,
 * tag, flag, the intro/outro and the bungee keep their ids.
 *
 * Switch: GE_DAMPLUS (unset/0 = off; 1 = everything; "night" = slice 1 only). Off in attract demos, while recording, in
 * multiplayer, with a Lua mode, and on every stage but Dam. Tests and the dam1 oracle never set it. */
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
    if (mode != NULL && mode[0] != '\0' && mode[0] != '0')
    {
        return;
    }
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

/* --------------------------------------------------------------------------------------------------------------------------- */

static s32 sStockPads;   /* stock pad count of the stage being loaded */

static s32 dpPadIndex(s16 p)
{
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

/* New pad i at anchor + offset: same height as the anchor, floor tile found by walking the stan from the anchor's tile. */
static s32 dpMakePad(PadRecord *out, PadRecord *all, const DpPad *spec, s32 i)
{
    PadRecord *a = &all[dpPadIndex(spec->anchor)];
    StandTile *tile = (StandTile *) a->stan;
    f32 tx = a->pos.f[0] + spec->dx;
    f32 tz = a->pos.f[2] + spec->dz;
    f32 yaw = spec->yaw * (M_PI / 180.0f);
    s32 ok;

    memset(out, 0, sizeof(*out));
    out->pos.f[0] = tx;
    out->pos.f[1] = a->pos.f[1];
    out->pos.f[2] = tz;
    out->up.f[1] = 1.0f;
    out->look.f[0] = sinf(yaw);
    out->look.f[2] = cosf(yaw);
    snprintf(sPadNames[i & 63], sizeof(sPadNames[0]), "qdp%d", i);
    out->plink = sPadNames[i & 63];

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

static PropDefHeaderRecord *dpFindGuard(PropDefHeaderRecord *defs, s32 chrnum)
{
    PropDefHeaderRecord *p;

    for (p = defs; p->type != PROPDEF_END; p = (PropDefHeaderRecord *) (((u32 *) p) + sizepropdef(p)))
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

    for (p = defs; p->type != PROPDEF_END; p = (PropDefHeaderRecord *) (((u32 *) p) + sizepropdef(p)))
    {
        if (p->type == type)
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

/* The setup hook: proplvreset2 at "PD rejoins here" (prop.c), after every stock pointer is promoted and the pads are scaled
 * and resolved, before guards and objects are counted and created. */
void portDamPlusSetup(s32 stageId)
{
    PadRecord *pads;
    s32 npads, nnew, i, ok;
    PropDefHeaderRecord *defs = g_CurrentSetup.propDefs;
    PropDefHeaderRecord *p;
    u32 stockwords, extrawords;
    u32 *newdefs, *w;
    s32 nguards = 0, npick = 0;
    static u8 padok[64];

    if (stageId != LEVELID_DAM || !(sParts & DP_NIGHT) || defs == NULL || g_CurrentSetup.pads == NULL)
    {
        return;
    }

    /* pads */
    sStockPads = npads = dpCountPads(g_CurrentSetup.pads);
    nnew = ARRAYCOUNT(sNightPads);
    pads = mempAllocBytesInBank((npads + nnew + 1) * sizeof(PadRecord), MEMPOOL_STAGE);
    memcpy(pads, g_CurrentSetup.pads, npads * sizeof(PadRecord));
    for (i = 0; i < nnew; i++)
    {
        padok[i] = dpMakePad(&pads[npads + i], pads, &sNightPads[i], i);
    }
    memset(&pads[npads + nnew], 0, sizeof(PadRecord));
    g_CurrentSetup.pads = pads;

    /* AI lists */
    {
        s32 n = 0;
        AIListRecord *al;

        while (g_CurrentSetup.ailists != NULL && g_CurrentSetup.ailists[n].ailist != NULL)
        {
            n++;
        }
        al = mempAllocBytesInBank((n + ARRAYCOUNT(sNightAi) + 1) * sizeof(AIListRecord), MEMPOOL_STAGE);
        if (n > 0)
        {
            memcpy(al, g_CurrentSetup.ailists, n * sizeof(AIListRecord));
        }
        for (i = 0; i < (s32) ARRAYCOUNT(sNightAi); i++)
        {
            al[n + i].ailist = (AIRecord *) sNightAi[i].list;
            al[n + i].ID = sNightAi[i].id;
        }
        memset(&al[n + ARRAYCOUNT(sNightAi)], 0, sizeof(AIListRecord));
        g_CurrentSetup.ailists = al;
    }

    /* patrol paths */
    {
        s32 n = 0;
        PathRecord *pp;

        while (g_CurrentSetup.patrolpaths != NULL && g_CurrentSetup.patrolpaths[n].waypoints != NULL)
        {
            n++;
        }
        pp = mempAllocBytesInBank((n + ARRAYCOUNT(sNightPaths) + 1) * sizeof(PathRecord), MEMPOOL_STAGE);
        if (n > 0)
        {
            memcpy(pp, g_CurrentSetup.patrolpaths, n * sizeof(PathRecord));
        }
        for (i = 0; i < (s32) ARRAYCOUNT(sNightPaths); i++)
        {
            s32 len = 0;

            while (sNightPaths[i].waypoints[len] >= 0)
            {
                len++;
            }
            pp[n + i].waypoints = sNightPaths[i].waypoints;
            pp[n + i].ID = sNightPaths[i].id;
            pp[n + i].isLoop = 1;
            pp[n + i].len = len;
        }
        memset(&pp[n + ARRAYCOUNT(sNightPaths)], 0, sizeof(PathRecord));
        g_CurrentSetup.patrolpaths = pp;
    }

    /* propdefs: stock records unchanged and in place, ours appended before the END record */
    stockwords = 0;
    for (p = defs; p->type != PROPDEF_END; p = (PropDefHeaderRecord *) (((u32 *) p) + sizepropdef(p)))
    {
        stockwords += sizepropdef(p);
    }
    extrawords = 0;
    for (i = 0; i < (s32) ARRAYCOUNT(sNightGuards); i++)
    {
        extrawords += 0x22 * 4 + 16;   /* a guard and up to a few weapons: generous */
    }
    extrawords += ARRAYCOUNT(sNightPickups) * 0x40;
    newdefs = mempAllocBytesInBank((stockwords + extrawords + 1) * 4, MEMPOOL_STAGE);
    memcpy(newdefs, defs, stockwords * 4);
    w = newdefs + stockwords;

    for (i = 0; i < (s32) ARRAYCOUNT(sNightGuards); i++)
    {
        const DpGuard *g = &sNightGuards[i];
        PropDefHeaderRecord *t = dpFindGuard(defs, g->tmplchr);
        GuardRecord *gr;
        s32 padi = dpPadIndex(g->pad);

        if (t == NULL || (g->pad >= DP_NEW(0) && !padok[g->pad - DP_NEW(0)]))
        {
            printf("ge: dam+: guard %d skipped (no template chr %d or no pad)\n", g->chrnum, g->tmplchr);
            continue;
        }
        gr = (GuardRecord *) dpEmit(&w, t);
        gr->chrnum = g->chrnum;
        gr->PadID = padi;
        gr->AIListID = g->ailist;
        gr->Preset = padi;
        gr->chrpreset1 = 0xffff;
        gr->Data = NULL;
        nguards++;

        /* the template's own guns, given to the clone */
        for (p = defs; p->type != PROPDEF_END; p = (PropDefHeaderRecord *) (((u32 *) p) + sizepropdef(p)))
        {
            if (p->type == PROPDEF_COLLECTABLE && (((ObjectRecord *) p)->flags & PROPFLAG_ASSIGNEDTOCHR)
                && ((ObjectRecord *) p)->pad == g->tmplchr)
            {
                ObjectRecord *o = (ObjectRecord *) dpEmit(&w, p);
                o->pad = g->chrnum;
                o->prop = NULL;
                o->model = NULL;
            }
        }
    }

    for (i = 0; i < (s32) ARRAYCOUNT(sNightPickups); i++)
    {
        const DpPickup *k = &sNightPickups[i];
        PropDefHeaderRecord *t = NULL;
        ObjectRecord *o;

        if (k->pad >= DP_NEW(0) && !padok[k->pad - DP_NEW(0)])
        {
            continue;
        }
        if (k->kind == DP_PICK_ARMOUR)
        {
            t = dpFindType(defs, PROPDEF_ARMOUR);
        }
        else
        {
            for (p = defs; p->type != PROPDEF_END; p = (PropDefHeaderRecord *) (((u32 *) p) + sizepropdef(p)))
            {
                if (p->type == PROPDEF_COLLECTABLE && (((ObjectRecord *) p)->flags & PROPFLAG_ASSIGNEDTOCHR)
                    && ((ObjectRecord *) p)->pad == k->tmplchr)
                {
                    t = p;
                    break;
                }
            }
        }
        if (t == NULL)
        {
            continue;
        }
        o = (ObjectRecord *) dpEmit(&w, t);
        o->pad = dpPadIndex(k->pad);
        o->flags &= ~PROPFLAG_ASSIGNEDTOCHR;
        o->prop = NULL;
        o->model = NULL;
        npick++;
    }

    /* the END record */
    *w++ = ((u32 *) defs)[stockwords];
    g_CurrentSetup.propDefs = (PropDefHeaderRecord *) newdefs;

    ok = (u32) (w - newdefs) <= stockwords + extrawords + 1;
    printf("ge: dam+: night shift: %d pads, %d guards, %d pickups, %d patrol paths, %d AI lists appended (%u + %u propdef words)%s\n",
           nnew, nguards, npick, (s32) ARRAYCOUNT(sNightPaths), (s32) ARRAYCOUNT(sNightAi), stockwords,
           (u32) (w - newdefs) - stockwords - 1, ok ? "" : " OVERFLOW");
    fflush(stdout);
}

#endif
