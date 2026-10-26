/* port_shield.c - hostages and the human shield (designs/bank-heist.md section 31; Lua shield.*, port/luavm/port_luavm_shield.c,
 * tools/heist_hostages.lua).
 *
 *   grab     a Bond with a one-handed gun drawn (a pistol, the Klobb or the ZMG, a knife, the watch laser) presses USE at nothing, beside or
 *            behind a bank civilian who has surrendered (hands up) or cowers, within `reach` (120 units): he takes it as a human shield.
 *            In front of it (inside its front 120 degrees) he is refused: "get behind them".
 *   hold     every tick the hostage is pinned `dist` (45) units in front of him, facing where he faces, its hands up; its AI list is off
 *            (portHeistChrNoAi), it has no collision box and no auto-aim (the knock-out's flags), and the holder's own shots pass
 *            through it (chr.c chrTestHit asks portShieldPassShot). He moves at walk speed (`speed`, 0.5 of full stick: bondview2.c asks
 *            portShieldSpeedCap). He looks wrong to every guard and camera that sees him (the cover's act, every tick).
 *   hold fire  a guard about to fire at a Bond who holds a hostage holds the shot when the hostage is in his line (within `line` units of
 *            the segment from him to the Bond, on his side of the Bond): port_heist.c portHeistHoldFire asks portShieldHoldFire first.
 *            A clear line (from the side, from behind) is allowed. Held guards bark ("Hold your fire!", "Let them go!") and, after
 *            `flank_after` s of held shots, a guard the combat layer tracks runs round for a clear line (portCombatFlankTo).
 *   release  USE at nothing again; the holder hurt (a `drop` chance each hit, sim RNG); a two-handed weapon in hand; the holder down; the
 *            hostage down; or a script (two-handed work: the vault crack, the breaker, a piton line: tools/heist_hostages.lua). A released
 *            hostage cowers (port_heist.c portHeistCivilianCower).
 *
 * Every record (who was taken, by whom, for how long, hurt, the stand-off used) is sim state: it is in portShieldHash (folded into the
 * heist state hash) and port_shield.c is a SIM file for the snapshot (scripts/port/snap_embed.py). Randomness: randomGetNext only. PC only,
 * mode only (shield.enable). */
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
#include "game/gun.h"
#include "game/bondview.h"
#include "game/math_atan2f.h"
#include "port_blocks.h"
#include "port_mode.h"
#include "port_event.h"
#include "port_lua.h"

extern s32 g_ClockTimer;
extern s32 get_cur_playernum(void);
extern int portHeistKoIs(int chrnum);
extern int portHeistCivilianCount(void);
extern int portHeistCivilianChr(int i);
extern int portHeistCivilianState(int chrnum);
extern void portHeistCivilianCower(int chrnum);
extern int portHeistCivilianScare(float x, float z, float r);
extern int portHeistCoverTestAct(int p);
extern int portAiBark(int chrnum, const char *kind, const char *text);
extern void bondviewUpdateGuardTankFlagsRelated(PropRecord *prop, s32 flag);

#define SH_MAX      32               /* hostages taken (records) per stage */
#define SG_MAX      48               /* guards whose shot a hostage held */
#define SH_FLAGS    (CHRFLAG_00010000 | CHRFLAG_NO_AUTOAIM)   /* no collision box, no auto-aim (the knock-out's flags) */
#define SH_LOS      (CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_PATHBLOCKER | CDTYPE_AIOPAQUE)

enum { WHY_USE, WHY_HURT, WHY_TWOHANDS, WHY_WEAPON, WHY_BONDDOWN, WHY_HOSTAGEDOWN, WHY_SCRIPT, WHY_N };
static const char *const sWhy[WHY_N] = { "use", "hurt", "two_hands", "weapon", "bond_down", "hostage_down", "script" };

typedef struct
{
    s32 chrnum, by, grabAt, heldTicks, takes;
    u8 used, held, hurt, standoff, clean, lastWhy;
    u32 added;                        /* the flags this file added (taken off again at the release) */
    f32 dmg0;                         /* the hostage's damage when taken (more = hurt while held) */
} ShRec;

typedef struct { s32 chrnum, firstAt, lastAt, barkAt, flankAt, held; } ShGuard;

static ShRec sR[SH_MAX];
static ShGuard sG[SG_MAX];
static int sRN, sGN, sOn;
static s32 sHold[4];                  /* record index held by Bond p, -1 none */
static f32 sVit[4];                   /* Bond p's health + armour at the last tick (a hit: it went down) */
static s32 sLast[4], sLastSerial[4];  /* the last thing a use press did for Bond p (shield.last) */
static s32 sNow;
static s32 sGrabN, sReleaseN, sRefuseN, sHeldShotN, sClearShotN, sDropN, sFlankN, sBarkN;

static struct { f32 dist, reach, speed, drop, line, flankAfter, front; s32 flank; } sT;

enum { LAST_NONE, LAST_GRAB, LAST_NOGUN, LAST_FRONT, LAST_NOBODY, LAST_RELEASE };

static void tuneDefaults(void)
{
    sT.dist = 45.0f;        /* units in front of the holder */
    sT.reach = 120.0f;      /* a grab reaches this far */
    sT.speed = 0.5f;        /* the holder's stick cap (walk) */
    sT.drop = 0.35f;        /* the chance a hit makes him let go */
    sT.line = 34.0f;        /* a guard's line passing within this of the hostage is blocked */
    sT.flankAfter = 2.0f;   /* seconds of held shots before a tracked guard goes round */
    sT.front = 0.5f;        /* cos of the hostage's front half-angle (60 degrees): a grab from inside it is refused */
    sT.flank = 1;
}

void portShieldReset(void)
{
    int p;

    memset(sR, 0, sizeof(sR));
    memset(sG, 0, sizeof(sG));
    sRN = sGN = sOn = 0;
    for (p = 0; p < 4; p++) { sHold[p] = -1; sVit[p] = -1.0f; sLast[p] = sLastSerial[p] = 0; }
    sNow = 0;
    sGrabN = sReleaseN = sRefuseN = sHeldShotN = sClearShotN = sDropN = sFlankN = sBarkN = 0;
    tuneDefaults();
}

int portShieldEnable(int on) { sOn = on ? 1 : 0; return 1; }
int portShieldOn(void) { return sOn && portModeActive(); }

static f32 d2(f32 ax, f32 az, f32 bx, f32 bz) { return sqrtf((ax - bx) * (ax - bx) + (az - bz) * (az - bz)); }

static struct player *bondPl(int p) { return (p >= 0 && p < 4 && p < (int) portBondCount()) ? g_playerPointers[p] : NULL; }

/* Bond p's facing (the sim's vv_theta): forward (fx, fz) */
static void bondFwd(struct player *pl, f32 *fx, f32 *fz)
{
    f32 th = pl->vv_theta * (3.14159265f / 180.0f);

    *fx = -sinf(th);
    *fz = cosf(th);
}

static int weaponOf(int p)
{
    s32 saved = get_cur_playernum();
    int w;

    set_cur_player(p);
    w = (int) getCurrentPlayerWeaponId(GUNRIGHT);
    set_cur_player(saved);
    return w;
}

/* one hand free: a pistol (PP7, DD44, the Magnum, the golden / silver / gold PP7), a machine pistol (Klobb, ZMG), a knife, the watch laser */
static int oneHanded(int w)
{
    switch (w)
    {
        case ITEM_KNIFE: case ITEM_THROWKNIFE: case ITEM_WPPK: case ITEM_WPPKSIL: case ITEM_TT33: case ITEM_SKORPION: case ITEM_UZI:
        case ITEM_RUGER: case ITEM_GOLDENGUN: case ITEM_SILVERWPPK: case ITEM_GOLDWPPK: case ITEM_WATCHLASER:
            return 1;
        default:
            return 0;
    }
}

static int isGun(int w) { return w >= ITEM_KNIFE && w <= ITEM_ROCKETLAUNCH; }

static ShRec *recOf(s32 chrnum)
{
    int i;

    for (i = 0; i < sRN; i++)
    {
        if (sR[i].used && sR[i].chrnum == chrnum) return &sR[i];
    }
    return NULL;
}

static int chrUp(ChrRecord *c)
{
    return c != NULL && c->prop != NULL && c->model != NULL && !(c->hidden & CHRHIDDEN_REMOVE) && !chrIsDead(c) && !portHeistKoIs(c->chrnum)
        && c->actiontype != ACT_DIE && c->actiontype != ACT_DEAD;
}

/* ---- queries the engine asks (cheap when nobody holds anyone) ---- */

static int anyHeld(void) { return sHold[0] >= 0 || sHold[1] >= 0 || sHold[2] >= 0 || sHold[3] >= 0; }

/* the Bond holding chr, -1 none */
int portShieldHolder(int chrnum)
{
    int p;

    if (!anyHeld()) return -1;
    for (p = 0; p < 4; p++)
    {
        if (sHold[p] >= 0 && sR[sHold[p]].chrnum == chrnum) return p;
    }
    return -1;
}

int portShieldHolds(int chrnum) { return sOn && portShieldHolder(chrnum) >= 0; }

int portShieldHeldBy(int p) { return (p >= 0 && p < 4 && sHold[p] >= 0) ? sR[sHold[p]].chrnum : -1; }

/* port_heist.c portHeistChrNoAi: a held hostage runs no AI list */
int portShieldOwns(const ChrRecord *c) { return c != NULL && sOn && portShieldHolder(c->chrnum) >= 0; }

/* chr.c chrTestHit: the current Bond's shot passes through the hostage he holds himself */
int portShieldPassShot(ChrRecord *chr)
{
    int p;

    if (!sOn || chr == NULL || !anyHeld()) return 0;
    p = (int) get_cur_playernum();
    return p >= 0 && p < 4 && sHold[p] >= 0 && sR[sHold[p]].chrnum == chr->chrnum;
}

/* bondview2.c: the current Bond's stick cap (1 = none) */
float portShieldSpeedCap(void)
{
    int p;

    if (!sOn || !anyHeld()) return 1.0f;
    p = (int) get_cur_playernum();
    return (p >= 0 && p < 4 && sHold[p] >= 0) ? sT.speed : 1.0f;
}

/* ---- grab and release ---- */

static void lastSet(int p, int what)
{
    if (p < 0 || p > 3) return;
    sLast[p] = what;
    sLastSerial[p]++;
}

static void release(int p, int why)
{
    ShRec *r;
    ChrRecord *c;

    if (p < 0 || p > 3 || sHold[p] < 0) return;
    r = &sR[sHold[p]];
    sHold[p] = -1;
    r->held = 0;
    r->lastWhy = (u8) why;
    sReleaseN++;
    if (why == WHY_HURT) sDropN++;
    c = chrFindByLiteralId(r->chrnum);
    if (c != NULL && c->prop != NULL)
    {
        c->chrflags &= ~r->added;
        if (c->damage > r->dmg0 + 0.001f || !chrUp(c)) r->hurt = 1;
        if (chrUp(c))
        {
            portHeistCivilianCower(r->chrnum);   /* it cowers where it stands (then its own list) */
            portLuaGameChrAnim(r->chrnum, "startled", 1);
        }
    }
    else r->hurt = 1;
    r->added = 0;
    r->clean = (u8) (!r->hurt && why != WHY_HOSTAGEDOWN);
    lastSet(p, LAST_RELEASE);
    printf("ge: shield: Bond %d lets hostage chr %d go (%s, %.1f s held%s)\n", p, (int) r->chrnum, sWhy[why], (double) r->heldTicks / 60.0,
           r->hurt ? ", hurt" : "");
}

int portShieldRelease(int p, const char *why)
{
    int w = WHY_SCRIPT, i;

    if (p < 0 || p > 3 || sHold[p] < 0) return 0;
    for (i = 0; why != NULL && i < WHY_N; i++)
    {
        if (strcmp(why, sWhy[i]) == 0) w = i;
    }
    release(p, w);
    return 1;
}

/* the best civilian for Bond p to grab: compliant (hands up or cowering), up, within reach, ahead of him (60 degrees), nearest;
 * *front = the best one was refused only because he stands in front of it */
static int grabCandidate(int p, int *front)
{
    struct player *pl = bondPl(p);
    f32 fx, fz, best = 1e9f;
    int i, n = portHeistCivilianCount(), pick = -1, frontPick = -1;

    *front = 0;
    if (pl == NULL || pl->prop == NULL) return -1;
    bondFwd(pl, &fx, &fz);
    for (i = 0; i < n; i++)
    {
        int chrnum = portHeistCivilianChr(i), st;
        ChrRecord *c;
        f32 dx, dz, dy, d, a, cx, cz;

        if (chrnum < 0 || portShieldHolder(chrnum) >= 0) continue;
        st = portHeistCivilianState(chrnum);
        if (st != 1 && st != 2) continue;               /* hands up or cowering only (not calm, not knocked out) */
        c = chrFindByLiteralId(chrnum);
        if (!chrUp(c)) continue;
        dx = c->prop->pos.x - pl->prop->pos.x;
        dz = c->prop->pos.z - pl->prop->pos.z;
        dy = c->prop->pos.y - pl->prop->pos.y;
        d = sqrtf(dx * dx + dz * dz);
        if (d > sT.reach || dy > 200.0f || dy < -250.0f) continue;
        if (d > 1.0f && (dx * fx + dz * fz) / d < 0.5f) continue;   /* not ahead of him */
        /* where he stands round it: its facing (sin a, cos a) against the line from it to him */
        a = getsubroty(c->model);
        cx = sinf(a); cz = cosf(a);
        if (d > 1.0f && (-dx * cx + -dz * cz) / d > sT.front)
        {
            if (d < best && frontPick < 0) frontPick = chrnum;
            continue;
        }
        if (d < best) { best = d; pick = chrnum; }
    }
    if (pick < 0 && frontPick >= 0) *front = 1;
    return pick;
}

static int grab(int p, int chrnum)
{
    ShRec *r = recOf(chrnum);
    ChrRecord *c = chrFindByLiteralId(chrnum);
    struct player *pl = bondPl(p);

    if (c == NULL || pl == NULL) return 0;
    if (r == NULL)
    {
        if (sRN >= SH_MAX) return 0;
        r = &sR[sRN++];
        memset(r, 0, sizeof(*r));
        r->used = 1;
        r->chrnum = chrnum;
    }
    r->by = p;
    r->held = 1;
    r->grabAt = sNow;
    r->takes++;
    r->dmg0 = c->damage;
    r->added = SH_FLAGS & ~c->chrflags;
    c->chrflags |= r->added;
    sHold[p] = (s32) (r - sR);
    sVit[p] = pl->bondhealth + pl->bondarmour;
    sGrabN++;
    lastSet(p, LAST_GRAB);
    portLuaGameChrAnim(chrnum, "surrender", 1);
    /* the staff round him panic */
    portHeistCivilianScare(c->prop->pos.x, c->prop->pos.z, 700.0f);
    printf("ge: shield: Bond %d takes chr %d as a hostage (take %d)\n", p, (int) chrnum, (int) r->takes);
    return 1;
}

/* port_heist.c portHeistNoteUse: Bond p pressed use; target = a door or an object is in front of him (that press is for it) */
void portShieldUse(int p, int target)
{
    int w, chr, front;

    if (!portShieldOn() || p < 0 || p > 3 || !portBondInPlay(p)) return;
    if (sHold[p] >= 0)
    {
        if (!target) release(p, WHY_USE);   /* (a door opens one-handed: a press at one keeps the hostage) */
        return;
    }
    chr = grabCandidate(p, &front);
    if (chr < 0)
    {
        if (front) { sRefuseN++; lastSet(p, LAST_FRONT); }
        return;   /* (nobody to take: an ordinary press, say nothing) */
    }
    w = weaponOf(p);
    if (!isGun(w) || !oneHanded(w))
    {
        sRefuseN++;
        lastSet(p, LAST_NOGUN);
        return;
    }
    grab(p, chr);
}

/* test.grab(p [, chr]): a test hook, as a use press at nothing (chr: that civilian, if it qualifies) */
int portShieldTestGrab(int p, int chrnum)
{
    int had = sGrabN;

    if (chrnum < 0)
    {
        portShieldUse(p, 0);
        return sGrabN > had;
    }
    if (!portShieldOn() || p < 0 || p > 3 || sHold[p] >= 0 || !portBondInPlay(p)) return 0;
    {
        int front, w = weaponOf(p), cand = grabCandidate(p, &front);

        if (cand != chrnum || !isGun(w) || !oneHanded(w)) return 0;
    }
    return grab(p, chrnum);
}

/* ---- the hold (each tick) ---- */

/* the hostage at dist in front of the holder: a stan walk from his tile (walls and shut doors stop it short) */
static void pin(int p, ShRec *r, ChrRecord *c)
{
    struct player *pl = bondPl(p);
    coord3d bpos, pos;
    f32 fx, fz, dy, yaw;
    static const f32 frac[] = { 1.0f, 0.7f, 0.4f, 0.0f };
    int k;

    if (pl == NULL || pl->prop == NULL || pl->prop->stan == NULL || !portBondPosOf(p, &bpos)) return;
    bondFwd(pl, &fx, &fz);
    dy = c->prop->pos.y - c->ground;
    for (k = 0; k < 4; k++)
    {
        StandTile *st = pl->prop->stan;
        f32 tx = bpos.x + fx * sT.dist * frac[k], tz = bpos.z + fz * sT.dist * frac[k];

        if (k < 3 && stanTestLineUnobstructed(&st, bpos.x, bpos.z, tx, tz, CDTYPE_DOORS | CDTYPE_PATHBLOCKER, 140.0f, 20.0f, 0.0f, 1.0f) == 0) continue;
        pos.x = tx;
        pos.z = tz;
        c->ground = stanGetPositionYValue(st, tx, tz);
        pos.y = c->ground + dy;
        c->prop->pos = pos;
        c->prop->stan = st;
        break;
    }
    yaw = atan2f(fx, fz);
    if (yaw < 0.0f) yaw += 6.2831853f;
    setsubroty(c->model, yaw);
    setsuboffset(c->model, &c->prop->pos);
    chrDetectRooms(c);
    portLuaGameChrAnim(r->chrnum, "surrender", 0);
}

/* a point (x, z) a Bond can shoot from: the flank for a guard whose line the hostage blocks */
static StandTile *tileAt(f32 x, f32 y, f32 z, f32 *fy)
{
    coord3d q;
    f32 h;
    StandTile *t;

    q.x = x; q.y = y + 60.0f; q.z = z;
    t = stanFindTileBelowPos(&q, NULL, &h);
    if (t == NULL || h - y > 70.0f || h - y < -70.0f) return NULL;
    if (fy != NULL) *fy = h;
    return t;
}

static int bondSeesPoint(PropRecord *bp, f32 x, f32 z, StandTile *t)
{
    StandTile *st = bp->stan;
    int ok;

    if (st == NULL || t == NULL) return 0;
    bondviewUpdateGuardTankFlagsRelated(bp, 0);
    ok = stanTestLineUnobstructed(&st, bp->pos.x, bp->pos.z, x, z, SH_LOS, 140.0f, 140.0f, 0.0f, 1.0f) != 0 && st == t;
    bondviewUpdateGuardTankFlagsRelated(bp, 1);
    return ok;
}

/* the hostage held by the Bond whose prop is `target` is in the line from `me` to him */
static int inLine(PropRecord *me, PropRecord *target, int p, f32 *lateral)
{
    ChrRecord *h;
    f32 dx, dz, len2, px, pz, t, ex, ez;

    if (sHold[p] < 0) return 0;
    h = chrFindByLiteralId(sR[sHold[p]].chrnum);
    if (h == NULL || h->prop == NULL) return 0;
    dx = target->pos.x - me->pos.x;
    dz = target->pos.z - me->pos.z;
    len2 = dx * dx + dz * dz;
    if (len2 < 1.0f) return 0;
    px = h->prop->pos.x - me->pos.x;
    pz = h->prop->pos.z - me->pos.z;
    t = (px * dx + pz * dz) / len2;
    ex = px - t * dx;
    ez = pz - t * dz;
    if (lateral != NULL) *lateral = sqrtf(ex * ex + ez * ez);
    if (t <= 0.0f || t >= 1.0f) return 0;           /* behind the shooter, or behind the Bond: not in the way */
    return ex * ex + ez * ez <= sT.line * sT.line;
}

static ShGuard *guardRec(s32 chrnum, int add)
{
    int i;

    for (i = 0; i < sGN; i++)
    {
        if (sG[i].chrnum == chrnum) return &sG[i];
    }
    if (!add || sGN >= SG_MAX) return NULL;
    memset(&sG[sGN], 0, sizeof(sG[0]));
    sG[sGN].chrnum = chrnum;
    return &sG[sGN++];
}

/* port_heist.c portHeistHoldFire (before its civilian test): self is about to fire at the current player. 1 = hold it (the hostage is
 * in the line), 0 = no hostage involved or a clear line */
int portShieldHoldFire(ChrRecord *self)
{
    PropRecord *target;
    int p;

    if (!sOn || !anyHeld() || self == NULL || self->prop == NULL || portShieldHolder(self->chrnum) >= 0) return 0;
    target = getCurrentPlayerProp();
    if (target == NULL) return 0;
    for (p = 0; p < 4; p++)
    {
        struct player *pl = bondPl(p);

        if (pl != NULL && pl->prop == target) break;
    }
    if (p == 4 || sHold[p] < 0) return 0;
    if (inLine(self->prop, target, p, NULL))
    {
        ShGuard *g = guardRec(self->chrnum, 1);

        sHeldShotN++;
        if (g != NULL)
        {
            if (g->firstAt == 0 || sNow - g->lastAt > 90) g->firstAt = sNow > 0 ? sNow : 1;
            g->lastAt = sNow > 0 ? sNow : 1;
            g->held++;
        }
        return 1;
    }
    sClearShotN++;
    return 0;
}

/* a guard held in line for a while: bark, and (tracked by the combat layer) run round for a clear line */
static void guardTick(ShGuard *g)
{
    extern int portCombatFlankTo(int chrnum, float x, float z);
    static const char *const lines[] = { "Hold your fire! He's got a hostage!", "Let them go!", "Hold your fire!", "Let the hostage go - now!" };
    ChrRecord *c;
    int p, bestP = -1;
    f32 bd = 1e9f;

    if (g->lastAt == 0 || sNow - g->lastAt > 60) return;   /* not held in the last second */
    c = chrFindByLiteralId(g->chrnum);
    if (!chrUp(c)) return;
    if (g->barkAt == 0 || sNow - g->barkAt > 240)
    {
        g->barkAt = sNow;
        sBarkN++;
        portAiBark(g->chrnum, "hey", lines[(g->held / 7 + g->chrnum) % 4]);
    }
    if (!sT.flank || sNow - g->firstAt < (s32) (sT.flankAfter * 60.0f) || (g->flankAt > 0 && sNow - g->flankAt < 360)) return;
    for (p = 0; p < 4; p++)
    {
        struct player *pl = bondPl(p);
        f32 d;

        if (sHold[p] < 0 || pl == NULL || pl->prop == NULL) continue;
        d = d2(pl->prop->pos.x, pl->prop->pos.z, c->prop->pos.x, c->prop->pos.z);
        if (d < bd) { bd = d; bestP = p; }
    }
    if (bestP < 0) return;
    g->flankAt = sNow;
    {
        struct player *pl = bondPl(bestP);
        PropRecord *bp = pl->prop;
        f32 fx, fz, r = bd, side, base;
        static const f32 da[] = { 1.25f, 1.5f, 1.05f, 1.8f };   /* radians off Bond's facing: the hostage covers about 50 degrees either side */
        int s, k, m;

        bondFwd(pl, &fx, &fz);
        base = atan2f(fx, fz);
        side = ((c->prop->pos.x - bp->pos.x) * fz - (c->prop->pos.z - bp->pos.z) * fx) >= 0.0f ? 1.0f : -1.0f;   /* his side of Bond's facing */
        if (r < 350.0f) r = 350.0f;
        if (r > 900.0f) r = 900.0f;
        for (s = 0; s < 2; s++, side = -side)
        {
            for (k = 0; k < 4; k++)
            {
                for (m = 0; m < 2; m++)
                {
                    f32 a = base - side * da[k], rr = r * (m ? 0.7f : 1.0f), x = bp->pos.x + sinf(a) * rr, z = bp->pos.z + cosf(a) * rr;
                    StandTile *t = tileAt(x, c->ground, z, NULL);

                    if (t == NULL || d2(x, z, c->prop->pos.x, c->prop->pos.z) < 200.0f || !bondSeesPoint(bp, x, z, t)) continue;
                    if (portCombatFlankTo(g->chrnum, x, z))
                    {
                        sFlankN++;
                        printf("ge: shield: chr %d goes round for a clear shot (%.0f,%.0f), the hostage in his line\n", (int) g->chrnum, x, z);
                        portAiBark(g->chrnum, "backup", "I'll get round him!");
                        return;
                    }
                    printf("ge: shield: chr %d holds his aim (not a free combat guard)\n", (int) g->chrnum);
                    return;   /* not a combat guard (or busy): he holds his aim */
                }
            }
        }
        printf("ge: shield: chr %d finds no clear line round the hostage\n", (int) g->chrnum);
    }
}

/* port_heist.c portHeistAiTick */
void portShieldTick(void)
{
    int p, i;

    if (!portShieldOn()) return;
    sNow += g_ClockTimer;
    for (p = 0; p < 4; p++)
    {
        struct player *pl;
        ShRec *r;
        ChrRecord *c;
        f32 vit;

        if (sHold[p] < 0) continue;
        r = &sR[sHold[p]];
        pl = bondPl(p);
        c = chrFindByLiteralId(r->chrnum);
        if (!chrUp(c)) { r->hurt = 1; release(p, WHY_HOSTAGEDOWN); continue; }
        if (pl == NULL || pl->prop == NULL || pl->bonddead || !portBondInPlay(p)) { release(p, WHY_BONDDOWN); continue; }
        if (!oneHanded(weaponOf(p))) { release(p, WHY_WEAPON); continue; }
        vit = pl->bondhealth + pl->bondarmour;
        if (sVit[p] >= 0.0f && vit < sVit[p] - 0.005f)
        {
            sVit[p] = vit;
            if ((f32) (randomGetNext() % 1000u) < sT.drop * 1000.0f) { release(p, WHY_HURT); continue; }
        }
        sVit[p] = vit;
        if (c->damage > r->dmg0 + 0.001f) r->hurt = 1;
        r->heldTicks += g_ClockTimer;
        pin(p, r, c);
        portHeistCoverTestAct(p);   /* a Bond with a gun to a hostage's head looks wrong to every guard and camera that sees him */
    }
    for (i = 0; i < sGN; i++) guardTick(&sG[i]);
}

/* ---- reads ---- */

int portShieldInfo(int chrnum, int *by, int *held, float *secs, int *hurt, int *standoff, int *clean, int *takes, const char **why)
{
    ShRec *r = recOf(chrnum);

    if (r == NULL) return 0;
    *by = r->by; *held = r->held; *secs = (float) r->heldTicks / 60.0f; *hurt = r->hurt; *standoff = r->standoff; *clean = r->clean;
    *takes = r->takes;
    *why = r->held ? "" : sWhy[r->lastWhy < WHY_N ? r->lastWhy : WHY_SCRIPT];
    return 1;
}

/* test.chr_face(chr): the way a chr faces, degrees (a heading to a point is atan2(x - cx, z - cz)); -1000 none */
float portShieldFace(int chrnum)
{
    ChrRecord *c = chrFindByLiteralId(chrnum);

    return (c != NULL && c->model != NULL) ? getsubroty(c->model) * (180.0f / 3.14159265f) : -1000.0f;
}

int portShieldRecords(int *chrnums, int max)
{
    int i, n = 0;

    for (i = 0; i < sRN && n < max; i++)
    {
        if (sR[i].used) chrnums[n++] = sR[i].chrnum;
    }
    return n;
}

/* shield.standoff(chr): the police stand-off was used for this hostage; 1 = newly marked */
int portShieldStandoff(int chrnum)
{
    ShRec *r = recOf(chrnum);

    if (r == NULL || r->standoff) return 0;
    r->standoff = 1;
    return 1;
}

int portShieldLast(int p, int *serial)
{
    if (p < 0 || p > 3) { *serial = 0; return 0; }
    *serial = sLastSerial[p];
    return sLast[p];
}

void portShieldCounts(int *grabs, int *releases, int *refused, int *held, int *clear, int *drops, int *flanks, int *barks)
{
    *grabs = sGrabN; *releases = sReleaseN; *refused = sRefuseN; *held = sHeldShotN; *clear = sClearShotN; *drops = sDropN; *flanks = sFlankN;
    *barks = sBarkN;
}

int portShieldTune(const char *name, float v)
{
    static const struct { const char *n; f32 *p; } t[] = {
        { "dist", &sT.dist }, { "reach", &sT.reach }, { "speed", &sT.speed }, { "drop", &sT.drop }, { "line", &sT.line },
        { "flank_after", &sT.flankAfter }, { "front", &sT.front },
    };
    int i;

    if (name == NULL) return 0;
    if (strcmp(name, "flank") == 0) { sT.flank = v != 0.0f; return 1; }
    for (i = 0; i < (int) (sizeof(t) / sizeof(t[0])); i++)
    {
        if (strcmp(name, t[i].n) == 0) { *t[i].p = v; return 1; }
    }
    return 0;
}

/* folded into the heist state hash (port_heist.c portHeistStateHash) */
unsigned portShieldHash(void)
{
    unsigned h;
    int i, p;

    /* nothing ever taken or refused: 0, as with the layer off (a run that takes no hostage hashes as one with S.hostages = false) */
    if (!sOn || (sRN == 0 && sGN == 0 && sRefuseN == 0)) return 0;
    h = (unsigned) sRN * 5u + (unsigned) sGN * 7u + (unsigned) sNow * 11u;
    h = h * 16777619u + (unsigned) sGrabN + (unsigned) sReleaseN * 3u + (unsigned) sRefuseN * 5u + (unsigned) sHeldShotN * 7u
        + (unsigned) sClearShotN * 11u + (unsigned) sDropN * 13u + (unsigned) sFlankN * 17u + (unsigned) sBarkN * 19u;
    for (p = 0; p < 4; p++)
        h = h * 16777619u + (unsigned) sHold[p] * 3u + (unsigned) (s32) (sVit[p] * 1000.0f) * 5u + (unsigned) sLast[p] * 7u + (unsigned) sLastSerial[p] * 11u;
    for (i = 0; i < sRN; i++)
    {
        const ShRec *r = &sR[i];

        h = h * 16777619u + (unsigned) r->chrnum + (unsigned) r->by * 3u + (unsigned) r->grabAt * 5u + (unsigned) r->heldTicks * 7u
            + (unsigned) r->takes * 11u + (unsigned) r->held * 13u + (unsigned) r->hurt * 17u + (unsigned) r->standoff * 19u
            + (unsigned) r->clean * 23u + (unsigned) r->lastWhy * 29u + r->added + (unsigned) (s32) (r->dmg0 * 1000.0f) * 31u;
    }
    for (i = 0; i < sGN; i++)
        h = h * 16777619u + (unsigned) sG[i].chrnum + (unsigned) sG[i].firstAt * 3u + (unsigned) sG[i].lastAt * 5u + (unsigned) sG[i].barkAt * 7u
            + (unsigned) sG[i].flankAt * 11u + (unsigned) sG[i].held * 13u;
    return h;
}

#endif /* TARGET_N64 */
