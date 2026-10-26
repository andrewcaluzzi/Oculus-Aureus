/* port_combat.c - the heist's LOUD route: guards who fight like a team (designs/bank-heist.md section 24; owned by the combat stream).
 *
 * The stock attack lists run straight at Bond and stand there shooting. A guard this layer tracks (combat.guard, every armed guard of the
 * heist: posted, raisers once the alarm is up, carriers, squads, waves, police, SWAT) who is in a fight - he has had Bond in sight in the
 * last few seconds, or Bond has shot at him or near him - is taken off his AI list (port_heist.c portHeistChrNoAi asks
 * portCombatOwns) and driven here with GoldenEye's own action primitives:
 *
 *   cover    a point near him that breaks Bond's line of sight (sampled on the stand tiles round him, the stock stan line test with
 *            objects and doors), with a peek point a short step aside that has the line: he runs there and crouches (the stock kneel);
 *   peek     after a hide (`hide_min`..`hide_max` s) he runs out to the peek point and fires a burst (the stock attack, stand or kneel,
 *            TARGET_BOND), then ducks back. Two peeks with no line to Bond: Bond moved, he finds new cover against where Bond is now;
 *   flanked  Bond has the line to him in cover (he moved round it): he moves ("He's flanking us!"), or shoots first if he has nowhere;
 *   flank    two fighting guards near each other pair up: one suppresses from cover (shorter hides, longer bursts: "Covering!"), the
 *            other runs round to a firing point about 80 degrees round Bond from his partner ("Cover me! I'm flanking!");
 *   retreat  hurt past `retreat_hp` of his health, he falls back to cover further from Bond ("I'm hit! Falling back!") and hides longer;
 *   release  no sight of Bond for `lose` s, or no cover and no flank point: he goes back to his own list (it hunts Bond as before),
 *            with Bond's last known position, and is not taken again for `cooldown` s.
 *
 * Barks go through the ai layer's guard_bark event (portAiBark, its ROM grunt / radio sounds and the AR bubble). Every decision reads sim
 * state only, uses the sim RNG (randomGetNext) and lives in portCombatHash (folded into the heist state hash); port_combat.c is a SIM
 * file for the snapshot. PC only, mode only (combat.enable). */
#ifndef TARGET_N64

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <ultra64.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include <bondaicommands.h>
#include "random.h"
#include "game/chrai.h"
#include "game/stan.h"
#include "game/player.h"
#include "game/chr.h"
#include "game/chraction.h"
#include "game/model.h"
#include "game/math_atan2f.h"
#include "port_blocks.h"
#include "port_mode.h"
#include "port_event.h"
#include "port_lua.h"
#include "port_noise.h"
#include "port_traits.h"   /* guard personalities (section 32): a rookie falls back sooner, a veteran later; neutral when off */

extern s32 g_GlobalTimer;
extern s32 g_ClockTimer;
extern bool alarmIsActive(void);
extern s32 plot_course_for_actor(ChrRecord *self, coord3d *arg1, StandTile *stan, SPEED speed);
extern void bondviewUpdateGuardTankFlagsRelated(PropRecord *prop, s32 flag);
extern int portHeistKoIs(int chrnum);
extern int portHeistChrOnGo(const ChrRecord *c);
extern int portNoiseNearDoor(float x, float z, float r);
extern int portNoiseLast(float *x, float *z, float *radius, int *kind, int *heard, int *count);
extern int portAiBark(int chrnum, const char *kind, const char *text);
extern int portAiAwareness(int chrnum, int *state, float *level, float *lastx, float *lastz);
extern s32 chrIsNotDeadOrShot(ChrRecord *self);
extern s32 chrlvCall7F0B0E24WithChrWidthHeight(PropRecord *arg0, coord3d *arg1, coord3d *arg2);

#define CB_MAX   256
#define CB_SCAN  4                 /* ticks between scans (15 Hz) */
#define TEAM_SCAN 60               /* ticks between pairing scans */
#define TAU      6.2831853f
#define LOS_TYPES (CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_PATHBLOCKER | CDTYPE_AIOPAQUE)

enum { CB_IDLE, CB_TOCOVER, CB_HIDE, CB_TOPEEK, CB_SHOOT, CB_FLANK, CB_N };
enum { ROLE_NONE, ROLE_SUPPRESS, ROLE_FLANK };
static const char *const sStName[CB_N] = { "idle", "to_cover", "hide", "to_peek", "shoot", "flank" };
static const char *const sRoleName[3] = { "none", "suppress", "flank" };

#define KIND_RAISER 2              /* combat.guard kinds as tools/heist_ai.lua KIND_CODE (2 = an alarm raiser) */

typedef struct
{
    s32 chrnum;
    u8 used, kind, st, role, retreated, kneel, seen, broken, misses, plots, hurtBark;
    s16 pair;                      /* index of his partner (-1 none) */
    s32 stAt, until, seenAt, hitAt, coolUntil, plotAt, barkAt, engagedAt, searchAt;
    f32 cx, cy, cz, px, pz, lastDamage;
    s32 peeks, covers, flanks;
    ChrRecord *slot;               /* chrFindByLiteralId cache (re-checked by chrnum) */
} Cb;

static Cb sC[CB_MAX];
static int sCN, sOn, sAcc, sTeamAcc, sHooked, sBudget;
static s32 sNow, sStage;
static s32 sCoverN, sPeekN, sFlankN, sRetreatN, sMoveN, sReleaseN, sBarkN, sSearchN, sFailN;
static s32 sShotAt[4];
static f32 sShotX[4], sShotZ[4];

/* combat.tune: seconds unless said */
static struct
{
    f32 engage, react, hideMin, hideMax, burstMin, burstMax, coverMin, coverMax, peekDist, retreatHp, flankChance, flankRange, lose,
        cooldown, kneel, shotNear;
    s32 cover, flank, retreat, searches;
} sT;

static void tuneDefaults(void)
{
    sT.engage = 2600.0f;       /* a Bond this close (and in sight, or shooting at him) is a fight */
    sT.react = 1.5f;           /* in sight this long (or shot at) before he moves to cover */
    sT.hideMin = 1.2f;         /* each hide in cover */
    sT.hideMax = 2.4f;
    sT.burstMin = 1.2f;        /* each burst at the peek point */
    sT.burstMax = 2.2f;
    sT.coverMin = 120.0f;      /* the cover points he considers (units from him) */
    sT.coverMax = 700.0f;
    sT.peekDist = 150.0f;      /* the peek point's step aside from the cover point */
    sT.retreatHp = 0.5f;       /* health left (0..1) at which he falls back */
    sT.flankChance = 0.35f;    /* each pairing scan (1 s), the chance two fighting guards near each other pair up */
    sT.flankRange = 1400.0f;   /* partners this close to each other */
    sT.lose = 8.0f;            /* no sight of Bond this long: back to his own list */
    sT.cooldown = 3.0f;
    sT.kneel = 0.5f;           /* the chance a burst is fired kneeling */
    sT.shotNear = 450.0f;      /* a Bond's shot fired within this of him (with the line) is "under fire" */
    sT.cover = 1;
    sT.flank = 1;
    sT.retreat = 1;
    sT.searches = 3;           /* cover searches per scan (the cost bound) */
}

void portCombatStageReset(void)
{
    memset(sC, 0, sizeof(sC));
    sCN = sOn = sAcc = sTeamAcc = sHooked = sBudget = 0;
    sNow = sStage = 0;
    sCoverN = sPeekN = sFlankN = sRetreatN = sMoveN = sReleaseN = sBarkN = sSearchN = sFailN = 0;
    memset(sShotAt, 0, sizeof(sShotAt));
    memset(sShotX, 0, sizeof(sShotX));
    memset(sShotZ, 0, sizeof(sShotZ));
    tuneDefaults();
}

int portCombatOn(void) { return sOn && portModeActive(); }
int portCombatEnable(int on) { sOn = on ? 1 : 0; return 1; }

static f32 rnd01(void) { return (f32) (randomGetNext() % 10000u) / 10000.0f; }
static f32 rndIn(f32 a, f32 b) { return a + (b - a) * rnd01(); }

static ChrRecord *cChr(Cb *g)
{
    ChrRecord *c = g->slot;

    if (c == NULL || c->chrnum != g->chrnum || c->model == NULL)
    {
        c = chrFindByLiteralId(g->chrnum);
        g->slot = c;
    }
    return c;
}

static Cb *cFind(s32 chrnum)
{
    int i;

    for (i = 0; i < sCN; i++)
    {
        if (sC[i].used && sC[i].chrnum == chrnum) return &sC[i];
    }
    return NULL;
}

static int chrUp(ChrRecord *c)
{
    return c != NULL && c->prop != NULL && c->model != NULL && !(c->hidden & CHRHIDDEN_REMOVE) && !chrIsDead(c) && !portHeistKoIs(c->chrnum)
        && c->actiontype != ACT_DIE && c->actiontype != ACT_DEAD;
}

static int armed(ChrRecord *c)
{
    return chrGetEquippedWeaponPropWithCheck(c, GUNRIGHT) != NULL || chrGetEquippedWeaponPropWithCheck(c, GUNLEFT) != NULL;
}

int portCombatGuardAdd(int chrnum, int kind)
{
    ChrRecord *c = chrFindByLiteralId(chrnum);
    Cb *g = cFind(chrnum);
    int i;

    if (!portModeActive() || c == NULL || c->prop == NULL || c->model == NULL) return 0;
    if (g == NULL)
    {
        for (i = 0; i < sCN && sC[i].used; i++) ;
        if (i == sCN)
        {
            if (sCN >= CB_MAX) return 0;
            sCN++;
        }
        g = &sC[i];
        memset(g, 0, sizeof(*g));
    }
    g->used = 1;
    g->chrnum = chrnum;
    g->kind = (u8) kind;
    g->slot = c;
    g->pair = -1;
    g->lastDamage = c->damage;
    return 1;
}

/* ---- geometry ---- */

static f32 d2(f32 ax, f32 az, f32 bx, f32 bz) { return sqrtf((ax - bx) * (ax - bx) + (az - bz) * (az - bz)); }

static f32 heading(f32 fx, f32 fz, f32 tx, f32 tz)
{
    f32 a = atan2f(tx - fx, tz - fz);

    if (a < 0.0f) a += TAU;
    return a;
}

/* the stand tile under (x, z) at about floor height y (none, or a different floor: NULL) */
static StandTile *tileAt(f32 x, f32 y, f32 z, f32 *fy)
{
    coord3d p;
    f32 h;
    StandTile *t;

    p.x = x; p.y = y + 60.0f; p.z = z;
    t = stanFindTileBelowPos(&p, NULL, &h);
    if (t == NULL || h - y > 70.0f || h - y < -70.0f) return NULL;
    if (fy != NULL) *fy = h;
    return t;
}

/* the heights (above the floor) a line is tested at: a crouched guard's head (cover behind a desk or a counter hides him) and a
 * standing shooter's line */
#define H_CROUCH 90.0f
#define H_STAND  140.0f

/* a clear stan line from (fx, fz) on tile `from` to (tx, tz) at height h that ends on tile `to` (walls, objects, doors in the way: not
 * clear) */
static int lineClear(StandTile *from, f32 fx, f32 fz, f32 tx, f32 tz, StandTile *to, f32 h)
{
    StandTile *st = from;

    if (from == NULL || to == NULL) return 0;
    return stanTestLineUnobstructed(&st, fx, fz, tx, tz, LOS_TYPES, h, h, 0.0f, 1.0f) != 0 && st == to;
}

/* chr c could walk straight from (fx, fz) on tile `from` to (tx, tz) on `to`: the stock side-step test's two lines a body's width
 * apart (chrlvCall7F0B0E24WithChrWidthHeight), through his height band; low furniture blocks it, other chrs do not (they move) */
static int walkClear(ChrRecord *c, StandTile *from, f32 fx, f32 fz, f32 tx, f32 tz, StandTile *to)
{
    f32 dx = tx - fx, dz = tz - fz, d = sqrtf(dx * dx + dz * dz), w = c->chrwidth * 1.2f, ox, oz, top = c->chrheight - 20.0f;
    StandTile *st;
    int s;

    if (from == NULL || to == NULL || d < 1.0f) return from != NULL && from == to;
    ox = dz / d * w; oz = -dx / d * w;
    for (s = -1; s <= 1; s++)
    {
        st = from;
        if (s != 0 && stanTestLineUnobstructed(&st, fx, fz, fx + ox * s, fz + oz * s, CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_PATHBLOCKER,
                                               top, 20.0f, 0.0f, 1.0f) == 0)
            return 0;   /* (as the stock test: out to the side first, then along) */
        if (stanTestLineUnobstructed(&st, fx + ox * s, fz + oz * s, tx + ox * s, tz + oz * s, CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_PATHBLOCKER,
                                     top, 20.0f, 0.0f, 1.0f) == 0)
            return 0;
        if (s == 0 && st != to) return 0;
    }
    return 1;
}

/* Bond's prop sees chr c (Bond's tile to his; at a crouched head's height while he kneels) */
static int bondSees(PropRecord *bp, ChrRecord *c)
{
    int ok;

    if (bp == NULL || bp->stan == NULL || c->prop->stan == NULL) return 0;
    chrSetMoving(c, FALSE);
    bondviewUpdateGuardTankFlagsRelated(bp, 0);
    ok = lineClear(bp->stan, bp->pos.x, bp->pos.z, c->prop->pos.x, c->prop->pos.z, c->prop->stan,
                   c->actiontype == ACT_KNEEL ? H_CROUCH : H_STAND);
    bondviewUpdateGuardTankFlagsRelated(bp, 1);
    chrSetMoving(c, TRUE);
    return ok;
}

/* Bond's prop sees the point (x, z) on tile t at height h */
static int bondSeesPoint(PropRecord *bp, f32 x, f32 z, StandTile *t, f32 h)
{
    int ok;

    if (bp == NULL || bp->stan == NULL || t == NULL) return 0;
    bondviewUpdateGuardTankFlagsRelated(bp, 0);
    ok = lineClear(bp->stan, bp->pos.x, bp->pos.z, x, z, t, h);
    bondviewUpdateGuardTankFlagsRelated(bp, 1);
    return ok;
}

/* the nearest living Bond in play to c within the engage range (about his floor); -1 none */
static int nearestBond(ChrRecord *c, f32 *dist)
{
    int p, n = (int) portBondCount(), best = -1;
    f32 bd = 1e18f;

    for (p = 0; p < n && p < 4; p++)
    {
        struct player *pl = g_playerPointers[p];
        f32 d, dy;

        if (!portBondInPlay(p) || pl == NULL || pl->prop == NULL || pl->bonddead) continue;
        dy = pl->prop->pos.y - c->prop->pos.y;
        if (dy > 350.0f || dy < -350.0f) continue;
        d = d2(pl->prop->pos.x, pl->prop->pos.z, c->prop->pos.x, c->prop->pos.z);
        if (d < bd) { bd = d; best = p; }
    }
    if (dist != NULL) *dist = bd;
    return bd <= sT.engage ? best : -1;
}

static PropRecord *bondProp(int p) { return (p >= 0 && p < 4 && g_playerPointers[p] != NULL) ? g_playerPointers[p]->prop : NULL; }

/* another tracked guard already holds cover near (x, z) */
static int coverTaken(Cb *self, f32 x, f32 z)
{
    int i;

    for (i = 0; i < sCN; i++)
    {
        Cb *g = &sC[i];

        if (g == self || !g->used || g->st == CB_IDLE || g->st == CB_FLANK) continue;
        if (d2(g->cx, g->cz, x, z) < 130.0f) return 1;
    }
    return 0;
}

/* a cover point for c against Bond's prop bp: hidden from Bond, reachable in a straight line from where he stands, off doorways, not
 * held by another guard, with a peek point a step aside that Bond can be shot from. away: further from Bond than now (a retreat).
 * The rings are tried nearest first; in a ring the candidate nearest the middle distance (800 from Bond) wins. */
static int coverFind(Cb *g, ChrRecord *c, PropRecord *bp, int away)
{
    static const f32 ring[] = { 130.0f, 230.0f, 340.0f, 460.0f, 580.0f, 700.0f };
    f32 gx = c->prop->pos.x, gy = c->ground, gz = c->prop->pos.z, bx = bp->pos.x, bz = bp->pos.z, db = d2(gx, gz, bx, bz);
    f32 a0 = (f32) ((u32) g->chrnum * 2654435761u % 6283u) / 1000.0f;   /* each guard starts his ring somewhere else (no RNG draw) */
    int r, k, found = 0;

    sSearchN++;
    for (r = 0; r < (int) (sizeof(ring) / sizeof(ring[0])) && !found; r++)
    {
        f32 bestScore = 1e18f;

        if (ring[r] < sT.coverMin || ring[r] > sT.coverMax) continue;
        for (k = 0; k < 12; k++)
        {
            f32 a = a0 + (f32) k * (TAU / 12.0f), x = gx + sinf(a) * ring[r], z = gz + cosf(a) * ring[r], y, d, ux, uz, s;
            StandTile *t = tileAt(x, gy, z, &y), *pt;
            int side;

            if (t == NULL) continue;
            d = d2(x, z, bx, bz);
            if (d < 300.0f || (away ? d < db + 150.0f : d < db - 250.0f)) continue;
            if (portNoiseNearDoor(x, z, 120.0f) || coverTaken(g, x, z)) continue;
            {   /* the stock side-step test (two lines a body apart, his height band): low furniture in the way refuses it */
                coord3d tp, dv;

                tp.x = x; tp.y = y; tp.z = z;
                dv.x = (x - gx) / ring[r]; dv.y = 0.0f; dv.z = (z - gz) / ring[r];
                s = (f32) chrlvCall7F0B0E24WithChrWidthHeight(c->prop, &tp, &dv);
            }
            if (s == 0.0f) continue;                       /* not straight there (a wall, furniture, a different room) */
            if (bondSeesPoint(bp, x, z, t, H_CROUCH)) continue;   /* Bond sees him crouched there: no cover */
            /* the peek point: a step either side, across Bond's line */
            ux = (bz - z) / d; uz = -(bx - x) / d;
            for (side = 0; side < 4; side++)
            {
                f32 step = sT.peekDist * ((side & 2) ? 0.6f : 1.0f) * ((side & 1) ? -1.0f : 1.0f);
                f32 qx = x + ux * step, qz = z + uz * step, score;

                pt = tileAt(qx, y, qz, NULL);
                if (pt == NULL || !walkClear(c, t, x, z, qx, qz, pt) || !bondSeesPoint(bp, qx, qz, pt, H_STAND)) continue;
                score = fabsf(d - 800.0f) + (f32) side * 40.0f + (away ? -d * 0.5f : 0.0f);
                if (score < bestScore)
                {
                    bestScore = score;
                    g->cx = x; g->cy = y; g->cz = z; g->px = qx; g->pz = qz;
                    found = 1;
                }
                break;
            }
        }
    }
    if (!found) sFailN++;
    return found;
}

/* ---- moving and shooting him (the stock primitives) ---- */

static int moving(ChrRecord *c) { return c->actiontype == ACT_GOPOS || c->actiontype == ACT_RUNPOS || c->actiontype == ACT_PATROL; }

static int runTo(ChrRecord *c, f32 x, f32 y, f32 z)
{
    coord3d p;
    StandTile *t = tileAt(x, y, z, &p.y);

    if (t == NULL) return 0;
    p.x = x; p.z = z;
    return plot_course_for_actor(c, &p, t, SPEED_RUN) ? 1 : 0;
}

static void setSt(Cb *g, int st)
{
    g->st = (u8) st;
    g->stAt = sNow;
    g->plots = 0;
    g->plotAt = sNow;
}

static void cbBark(Cb *g, ChrRecord *c, const char *kind, const char *text, int force)
{
    if (!force && g->barkAt > 0 && sNow - g->barkAt < 180) return;   /* one line per guard per 3 s */
    g->barkAt = sNow;
    sBarkN++;
    printf("ge: combat: chr %d says \"%s\" (%s, %s)\n", (int) c->chrnum, text, sStName[g->st], sRoleName[g->role]);
    portAiBark(c->chrnum, kind, text);
}

static void unpair(Cb *g)
{
    if (g->pair >= 0 && g->pair < sCN && sC[g->pair].pair == (s16) (g - sC))
    {
        sC[g->pair].pair = -1;
        sC[g->pair].role = ROLE_NONE;
    }
    g->pair = -1;
    g->role = ROLE_NONE;
}

static void release(Cb *g, ChrRecord *c, int p, const char *why)
{
    PropRecord *bp = bondProp(p);

    if (g->st == CB_IDLE) return;
    sReleaseN++;
    printf("ge: combat: chr %d back to his own list (%s)\n", (int) g->chrnum, why);
    unpair(g);
    setSt(g, CB_IDLE);
    g->coolUntil = sNow + (s32) (sT.cooldown * 60.0f);
    if (c != NULL && chrUp(c))
    {
        if (bp != NULL) c->lastknowntargetpos = bp->pos;   /* his list hunts where Bond was */
        if (moving(c) || c->actiontype == ACT_KNEEL) chraiStopAnimation(c);
    }
}

/* to a cover point (found now): 1 he is on his way */
static int takeCover(Cb *g, ChrRecord *c, PropRecord *bp, int away)
{
    if (!sT.cover) return 0;
    if (sBudget <= 0 || (g->searchAt > 0 && sNow - g->searchAt < 60)) return -1;   /* not now: the scan budget, one search a second each */
    g->searchAt = sNow;
    sBudget--;
    if (!coverFind(g, c, bp, away)) return 0;
    if (!runTo(c, g->cx, g->cy, g->cz)) return 0;
    setSt(g, CB_TOCOVER);
    g->covers++;
    sCoverN++;
    printf("ge: combat: chr %d to cover %.0f,%.0f (peek %.0f,%.0f), %.0f from Bond%s\n", (int) g->chrnum, g->cx, g->cz, g->px, g->pz,
           d2(g->cx, g->cz, bp->pos.x, bp->pos.z), away ? " (falling back)" : "");
    return 1;
}

static void hide(Cb *g, ChrRecord *c)
{
    f32 t = rndIn(sT.hideMin, sT.hideMax);

    if (g->role == ROLE_SUPPRESS) t *= 0.5f;
    if (g->retreated) t *= 1.6f;
    if (moving(c)) chraiStopAnimation(c);
    check_if_able_to_then_kneel(c);
    setSt(g, CB_HIDE);
    g->until = sNow + (s32) (t * 60.0f);
    g->broken = 0;
}

static void fire(Cb *g, ChrRecord *c)
{
    if (!chrIsNotDeadOrShot(c)) return;
    if (g->kneel) actor_kneel_aim_at_actor(c, TARGET_BOND, 0);
    else actor_aim_at_actor(c, TARGET_BOND, 0);
}

static void shoot(Cb *g, ChrRecord *c)
{
    f32 t = rndIn(sT.burstMin, sT.burstMax);

    if (g->role == ROLE_SUPPRESS) t *= 1.5f;
    if (moving(c)) chraiStopAnimation(c);
    g->kneel = (u8) (rnd01() < sT.kneel || g->retreated);
    setSt(g, CB_SHOOT);
    g->until = sNow + (s32) (t * 60.0f);
    g->peeks++;
    sPeekN++;
    fire(g, c);
}

/* the flanker's firing point: about 80 degrees round Bond from his partner, at the partner's distance (450..900), one Bond can be shot
 * from, on the flanker's floor, a real move from where he is */
static int flankPoint(Cb *g, ChrRecord *c, ChrRecord *pc, PropRecord *bp, f32 *ox, f32 *oy, f32 *oz)
{
    static const f32 da[] = { 0.0f, 0.35f, -0.35f, 0.7f };
    f32 bx = bp->pos.x, bz = bp->pos.z, base = heading(bx, bz, pc->prop->pos.x, pc->prop->pos.z), r = d2(bx, bz, pc->prop->pos.x, pc->prop->pos.z);
    f32 side = (randomGetNext() & 1) ? 1.0f : -1.0f;
    int s, k, m;

    if (r < 450.0f) r = 450.0f;
    if (r > 900.0f) r = 900.0f;
    for (s = 0; s < 2; s++, side = -side)
    {
        for (k = 0; k < 4; k++)
        {
            for (m = 0; m < 2; m++)
            {
                f32 a = base + side * (1.4f + da[k]), rr = r * (m ? 0.75f : 1.0f), x = bx + sinf(a) * rr, z = bz + cosf(a) * rr, y;
                StandTile *t = tileAt(x, c->ground, z, &y);

                if (t == NULL || portNoiseNearDoor(x, z, 120.0f) || d2(x, z, c->prop->pos.x, c->prop->pos.z) < 250.0f) continue;
                if (!bondSeesPoint(bp, x, z, t, H_STAND)) continue;
                *ox = x; *oy = y; *oz = z;
                return 1;
            }
        }
    }
    return 0;
}

static int startFlank(Cb *f, ChrRecord *fc, Cb *s, ChrRecord *sc, PropRecord *bp)
{
    f32 x, y, z;

    if (!flankPoint(f, fc, sc, bp, &x, &y, &z) || !runTo(fc, x, y, z))
    {
        printf("ge: combat: chr %d found no flank point round Bond (partner chr %d)\n", (int) f->chrnum, (int) s->chrnum);
        return 0;
    }
    f->pair = (s16) (s - sC);
    s->pair = (s16) (f - sC);
    f->role = ROLE_FLANK;
    s->role = ROLE_SUPPRESS;
    f->px = x; f->pz = z; f->cy = y;
    setSt(f, CB_FLANK);
    f->flanks++;
    sFlankN++;
    printf("ge: combat: chr %d flanks to %.0f,%.0f while chr %d covers him (%.0f deg round Bond)\n", (int) f->chrnum, x, z, (int) s->chrnum,
           (double) (fabsf(heading(bp->pos.x, bp->pos.z, x, z) - heading(bp->pos.x, bp->pos.z, sc->prop->pos.x, sc->prop->pos.z)) * 57.29578f));
    cbBark(f, fc, "backup", "Cover me! I'm flanking!", 1);
    cbBark(s, sc, "hey", "Covering! Go!", 1);
    return 1;
}

/* ---- the per-guard step ---- */

static void step(Cb *g)
{
    ChrRecord *c = cChr(g);
    PropRecord *bp;
    int p, sees, hit, under, astate = -1;
    f32 db, hp;

    if (c == NULL || c->prop == NULL) { g->used = 0; unpair(g); return; }
    if (!chrUp(c) || !armed(c) || c->actiontype == ACT_SURRENDER || portHeistChrOnGo(c))
    {
        if (g->st != CB_IDLE) { unpair(g); setSt(g, CB_IDLE); }
        g->lastDamage = c->damage;
        return;
    }
    p = nearestBond(c, &db);
    bp = bondProp(p);
    hp = c->maxdamage > 0.0f ? 1.0f - c->damage / c->maxdamage : 1.0f;
    hit = c->damage > g->lastDamage + 0.001f || (g->hitAt > 0 && sNow - g->hitAt <= CB_SCAN);
    g->lastDamage = c->damage;
    if (hit) g->hitAt = sNow;
    if (bp == NULL)
    {
        if (g->st != CB_IDLE) release(g, c, -1, "no Bond near");
        return;
    }
    if (g->st == CB_IDLE)
    {
        int fighting, shot, near;

        if (sNow < g->coolUntil) return;
        if (g->kind == KIND_RAISER && !alarmIsActive()) return;   /* a raiser runs for his panel first */
        portAiAwareness(g->chrnum, &astate, NULL, NULL, NULL);
        fighting = astate == 4 || (c->lastseetarget60 > 0 && g_GlobalTimer - c->lastseetarget60 < 120)
            || c->actiontype == ACT_ATTACK || c->actiontype == ACT_ATTACKWALK || c->actiontype == ACT_ATTACKROLL;
        near = (c->chrflags & CHRFLAG_NEAR_MISS) != 0;
        shot = sShotAt[0] > 0 && sNow - sShotAt[0] < 60 && d2(sShotX[0], sShotZ[0], c->prop->pos.x, c->prop->pos.z) < sT.engage;
        if (!fighting && !hit && !near && !shot) { g->engagedAt = 0; return; }   /* (the line test only for a guard in a fight) */
        sees = bondSees(bp, c);
        if (sees) g->seenAt = sNow;
        under = hit || near || (shot && sees);
        if (!fighting && !under) { g->engagedAt = 0; return; }
        if (g->engagedAt == 0) g->engagedAt = sNow;
        if (!under && sNow - g->engagedAt < (s32) (sT.react * 60.0f)) return;
        if (sT.retreat && hp <= sT.retreatHp * portTraitVal(g->chrnum, TV_RETREAT) && !g->retreated)
        {
            if (takeCover(g, c, bp, 1) > 0)
            {
                g->retreated = 1;
                sRetreatN++;
                cbBark(g, c, "hey", "I'm hit! Falling back!", 1);
            }
            return;
        }
        if (takeCover(g, c, bp, 0) > 0)
        {
            if (under) c->chrflags &= ~CHRFLAG_NEAR_MISS;
            cbBark(g, c, "hey", under ? "Taking fire! Get to cover!" : "Into cover!", 0);
        }
        return;
    }
    sees = bondSees(bp, c);
    if (sees)
    {   /* his list is off: keep the stock sight fields true for when it comes back (and for the ai layer's combat state) */
        g->seenAt = sNow;
        c->lastseetarget60 = g_GlobalTimer;
        c->lastknowntargetpos = bp->pos;
    }
    /* owned: he has not seen Bond for `lose` s */
    if (sNow - g->seenAt > (s32) (sT.lose * 60.0f) && g->st != CB_FLANK)
    {
        cbBark(g, c, "lost", "Where'd he go?", 0);
        release(g, c, p, "lost sight");
        return;
    }
    c->chrflags &= ~CHRFLAG_NEAR_MISS;
    if (sT.retreat && hp <= sT.retreatHp * portTraitVal(g->chrnum, TV_RETREAT) && !g->retreated && g->st != CB_TOCOVER && sBudget > 0)
    {
        unpair(g);
        int r = takeCover(g, c, bp, 1);

        if (r > 0)
        {
            g->retreated = 1;
            sRetreatN++;
            cbBark(g, c, "hey", "I'm hit! Falling back!", 1);
            return;
        }
        if (r == 0) g->retreated = 1;   /* nowhere to fall back to: he fights on */
    }
    if (!chrIsNotDeadOrShot(c)) return;   /* a hit reaction plays out */
    switch (g->st)
    {
        case CB_TOCOVER:
        case CB_TOPEEK:
        case CB_FLANK:
        {
            f32 tx = g->st == CB_TOCOVER ? g->cx : g->px, tz = g->st == CB_TOCOVER ? g->cz : g->pz;
            f32 dd = d2(tx, tz, c->prop->pos.x, c->prop->pos.z);
            int arrived = dd < 60.0f;

            if (!arrived && !moving(c) && sNow - g->plotAt > 30)
            {   /* stopped short: re-plot once, then take it from here */
                if (++g->plots > 1 || !runTo(c, tx, g->cy, tz)) arrived = 1;
                g->plotAt = sNow;
            }
            if (!arrived && sNow - g->stAt > 60 * 8) arrived = 1;
            if (!arrived) break;
            if (g->st == CB_TOCOVER) { if (dd > 110.0f && takeCover(g, c, bp, 0) > 0) break; hide(g, c); }   /* (stuck short of it: cover from here) */
            else if (g->st == CB_TOPEEK) shoot(g, c);
            else
            {   /* at the flank point: fire from there, then find cover round it */
                g->cx = c->prop->pos.x; g->cz = c->prop->pos.z;
                shoot(g, c);
            }
            break;
        }
        case CB_HIDE:
            if (sees)
            {   /* Bond has the line to him in cover: he came round it */
                if (++g->broken >= 2)
                {
                    sMoveN++;
                    unpair(g);
                    cbBark(g, c, "hey", "He's flanking us! Moving!", 0);
                    if (takeCover(g, c, bp, 0) <= 0) shoot(g, c);   /* nowhere to go: shoot it out from here */
                }
                break;
            }
            g->broken = 0;
            if (sNow >= g->until)
            {
                setSt(g, CB_TOPEEK);
                if (!runTo(c, g->px, g->cy, g->pz)) shoot(g, c);
            }
            break;
        case CB_SHOOT:
            if (sNow < g->until)
            {
                if (c->actiontype != ACT_ATTACK) fire(g, c);
                break;
            }
            g->misses = (u8) (sees ? 0 : g->misses + 1);
            if (g->role == ROLE_FLANK) unpair(g);   /* the flank is done: from here he fights from cover like anyone */
            if (g->misses >= 2 || d2(g->cx, g->cz, c->prop->pos.x, c->prop->pos.z) > 400.0f || g->covers == 0 || !sees)
            {   /* Bond moved (or this was a flank point): cover against where he is now */
                if (sBudget > 0)
                {
                    g->misses = 0;
                    int r = takeCover(g, c, bp, 0);

                    if (r < 0) g->until = sNow + 30;   /* not this scan: a little more fire */
                    else if (r == 0)
                    {
                        if (!sees) release(g, c, p, "no cover, no line");
                        else shoot(g, c);
                    }
                }
                break;
            }
            chraiStopAnimation(c);
            setSt(g, CB_TOCOVER);
            if (!runTo(c, g->cx, g->cy, g->cz)) hide(g, c);
            break;
        default:
            break;
    }
}

/* every second: two fighting guards near each other, neither paired, pair up (one suppresses, one flanks) */
static void team(void)
{
    int i, j;

    if (!sT.flank) return;
    for (i = 0; i < sCN; i++)
    {
        Cb *a = &sC[i];
        ChrRecord *ac;

        if (!a->used || a->pair >= 0 || a->st == CB_IDLE || a->st == CB_FLANK || a->retreated) continue;
        ac = cChr(a);
        if (!chrUp(ac)) continue;
        for (j = 0; j < sCN; j++)
        {
            Cb *b = &sC[j];
            ChrRecord *bc;
            int p;
            PropRecord *bp;

            if (j == i || !b->used || b->pair >= 0 || b->st == CB_IDLE || b->st == CB_FLANK || b->retreated) continue;
            bc = cChr(b);
            if (!chrUp(bc) || d2(ac->prop->pos.x, ac->prop->pos.z, bc->prop->pos.x, bc->prop->pos.z) > sT.flankRange) continue;
            if (rnd01() >= sT.flankChance) break;
            p = nearestBond(ac, NULL);
            bp = bondProp(p);
            if (bp == NULL) break;
            /* the healthier one goes round */
            if (bc->damage < ac->damage) { if (startFlank(b, bc, a, ac, bp)) break; }
            else if (startFlank(a, ac, b, bc, bp)) break;
            break;
        }
    }
}

/* a Bond's shot: the ai layer's gunshot noise (only Bonds' shots are noises), read from the hearing layer's last noise each tick. No
 * event listener: a C listener on PORT_EV_NOISE makes port_noise.c emit every noise, and the bus has 8 slots a type. (A hit on a guard
 * is his damage going up, read in his step.) */
static void pollShots(void)
{
    float x, z, r;
    int k, heard, n;

    if (!portNoiseLast(&x, &z, &r, &k, &heard, &n) || n == sHooked) return;
    sHooked = n;
    if (k != NOISE_SHOT) return;
    sShotAt[0] = sNow > 0 ? sNow : 1;
    sShotX[0] = x;
    sShotZ[0] = z;
}

/* port_heist.c portHeistAiTick */
void portCombatTick(void)
{
    int i;

    if (!portCombatOn()) return;
    pollShots();
    sNow += g_ClockTimer;
    sAcc += g_ClockTimer;
    sTeamAcc += g_ClockTimer;
    if (sAcc >= CB_SCAN)
    {
        sAcc = 0;
        sBudget = sT.searches;
        for (i = 0; i < sCN; i++)
        {
            if (sC[i].used) step(&sC[i]);
        }
    }
    if (sTeamAcc >= TEAM_SCAN)
    {
        sTeamAcc = 0;
        team();
    }
}

/* port_heist.c portHeistChrNoAi / portHeistChrWaking: this layer drives him (his AI list is off; the arena route keeps off him too) */
int portCombatOwns(const ChrRecord *c)
{
    Cb *g;

    if (c == NULL || !sOn || sCN == 0 || !portModeActive()) return 0;
    g = cFind(c->chrnum);
    return g != NULL && g->st != CB_IDLE;
}

/* ---- reads and scripts ---- */

const char *portCombatStateName(int st) { return (st >= 0 && st < CB_N) ? sStName[st] : "?"; }

int portCombatInfo(int chrnum, int *st, int *role, float *cx, float *cz, float *px, float *pz, int *pair, int *peeks, int *covers, int *flanks,
                   int *retreated)
{
    Cb *g = cFind(chrnum);

    if (g == NULL) return 0;
    *st = g->st; *role = g->role; *cx = g->cx; *cz = g->cz; *px = g->px; *pz = g->pz;
    *pair = (g->pair >= 0 && g->pair < sCN) ? sC[g->pair].chrnum : -1;
    *peeks = g->peeks; *covers = g->covers; *flanks = g->flanks; *retreated = g->retreated;
    return 1;
}

const char *portCombatRoleName(int r) { return (r >= 0 && r < 3) ? sRoleName[r] : "?"; }

void portCombatCounts(int *covers, int *peeks, int *flanks, int *retreats, int *moves, int *releases, int *barks, int *searches, int *fails)
{
    *covers = sCoverN; *peeks = sPeekN; *flanks = sFlankN; *retreats = sRetreatN; *moves = sMoveN; *releases = sReleaseN; *barks = sBarkN;
    *searches = sSearchN; *fails = sFailN;
}

/* combat.under_fire(chr): as if a Bond's shot had just gone past him (scripts, tests) */
int portCombatUnderFire(int chrnum)
{
    Cb *g = cFind(chrnum);

    if (g == NULL) return 0;
    g->hitAt = sNow > 0 ? sNow : 1;
    return 1;
}

/* combat.los(chr [, p]): Bond p (default the nearest) has the stan line to chr (the line this layer uses) */
int portCombatLos(int chrnum, int p)
{
    ChrRecord *c = chrFindByLiteralId(chrnum);

    if (c == NULL || c->prop == NULL) return 0;
    if (p < 0) p = nearestBond(c, NULL);
    return bondSees(bondProp(p), c);
}

/* combat.pair(flanker, suppressor): pair two tracked fighting guards now (scripts, tests); 1 = the flank started */
int portCombatPair(int fchr, int schr)
{
    Cb *f = cFind(fchr), *s = cFind(schr);
    ChrRecord *fc, *sc;
    PropRecord *bp;

    if (f == NULL || s == NULL || f == s || !chrUp(fc = cChr(f)) || !chrUp(sc = cChr(s))) return 0;
    bp = bondProp(nearestBond(fc, NULL));
    if (bp == NULL) return 0;
    unpair(f);
    unpair(s);
    if (s->st == CB_IDLE) { hide(s, sc); s->cx = sc->prop->pos.x; s->cz = sc->prop->pos.z; s->px = s->cx; s->pz = s->cz; }
    return startFlank(f, fc, s, sc, bp);
}

/* combat.stage(n): the mode's heat stage (0 none, 1 police, 2 SWAT), kept here so it is in the hash */
int portCombatStage(int n)
{
    if (n >= 0) sStage = n;
    return sStage;
}

int portCombatTune(const char *name, float v)
{
    static const struct { const char *n; f32 *p; } t[] = {
        { "engage", &sT.engage }, { "react", &sT.react }, { "hide_min", &sT.hideMin }, { "hide_max", &sT.hideMax },
        { "burst_min", &sT.burstMin }, { "burst_max", &sT.burstMax }, { "cover_min", &sT.coverMin }, { "cover_max", &sT.coverMax },
        { "peek_dist", &sT.peekDist }, { "retreat_hp", &sT.retreatHp }, { "flank_chance", &sT.flankChance }, { "flank_range", &sT.flankRange },
        { "lose", &sT.lose }, { "cooldown", &sT.cooldown }, { "kneel", &sT.kneel }, { "shot_near", &sT.shotNear },
    };
    static const struct { const char *n; s32 *p; } ti[] = {
        { "cover", &sT.cover }, { "flank", &sT.flank }, { "retreat", &sT.retreat }, { "searches", &sT.searches },
    };
    int i;

    if (name == NULL || v < 0.0f) return 0;
    for (i = 0; i < (int) (sizeof(ti) / sizeof(ti[0])); i++)
    {
        if (strcmp(name, ti[i].n) == 0) { *ti[i].p = (s32) v; return 1; }
    }
    for (i = 0; i < (int) (sizeof(t) / sizeof(t[0])); i++)
    {
        if (strcmp(name, t[i].n) == 0) { *t[i].p = v; return 1; }
    }
    return 0;
}

/* folded into the heist state hash (port_heist.c portHeistStateHash) */
unsigned portCombatHash(void)
{
    unsigned h = (unsigned) sOn * 3u + (unsigned) sCN * 5u + (unsigned) sNow * 7u + (unsigned) sAcc * 11u + (unsigned) sTeamAcc * 13u
        + (unsigned) sStage * 17u + (unsigned) sHooked * 19u;
    int i, p;

    if (!sOn) return h;
    h = h * 16777619u + (unsigned) sCoverN + (unsigned) sPeekN * 3u + (unsigned) sFlankN * 5u + (unsigned) sRetreatN * 7u
        + (unsigned) sMoveN * 11u + (unsigned) sReleaseN * 13u + (unsigned) sBarkN * 17u + (unsigned) sSearchN * 19u + (unsigned) sFailN * 23u;
    for (p = 0; p < 4; p++) h = h * 16777619u + (unsigned) sShotAt[p] + (unsigned) (s32) sShotX[p] * 3u + (unsigned) (s32) sShotZ[p] * 5u;
    for (i = 0; i < sCN; i++)
    {
        const Cb *g = &sC[i];

        if (!g->used) continue;
        h = h * 16777619u + (unsigned) g->chrnum + (unsigned) g->st * 3u + (unsigned) g->role * 5u + (unsigned) g->retreated * 7u
            + (unsigned) g->kneel * 11u + (unsigned) g->broken * 13u + (unsigned) g->misses * 17u + (unsigned) g->pair * 19u + g->kind;
        h = h * 16777619u + (unsigned) g->stAt + (unsigned) g->until * 3u + (unsigned) g->seenAt * 5u + (unsigned) g->hitAt * 7u
            + (unsigned) g->coolUntil * 11u + (unsigned) g->plotAt * 13u + (unsigned) g->barkAt * 17u + (unsigned) g->engagedAt * 19u;
        h = h * 16777619u + (unsigned) (s32) g->cx + (unsigned) (s32) g->cz * 3u + (unsigned) (s32) g->px * 5u + (unsigned) (s32) g->pz * 7u
            + (unsigned) (s32) (g->lastDamage * 1000.0f) * 11u + (unsigned) g->peeks * 13u + (unsigned) g->covers * 17u + (unsigned) g->plots + (unsigned) g->searchAt * 23u;
    }
    return h;
}

#endif /* TARGET_N64 */
