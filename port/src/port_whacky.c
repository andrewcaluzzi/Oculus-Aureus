/* port_whacky.c - the sim half of the arena's Whacky layer (see port_whacky.h; Lua `whacky`, port/luavm/port_luavm_whacky.c).
 *
 *   minion launcher  whacky.launcher(p, shots, opts) arms Bond p: each of his next `shots` rockets bursts, where it explodes, into
 *                    `count` tiny chrs (model scale ~0.25, collision box scaled with it) that run at turbo animation speed and throw
 *                    real knives (port_chrthrow.c, as the co-op bot does). They are on Bond's side (PORT_FACTION_BOND: guards engage
 *                    them, their knife hits are their own) or, with hostile=true, against him. A minion lives `life` seconds, then fades
 *                    out through the stock fade alpha (chr->fadealpha, the death fade's path) and is removed. They run the idle AI list
 *                    (m_CoopBot) and this file drives them every tick: nearest hostile in sight -> sprint at it, throw when in range.
 *                    They are not arena enemies (own chrnums 28000+, no ArenaInfo): never in the wave's count or kills.
 *   anim rate        the Fast / Slow Animation cheats' global rate (chr.c animation_rate), applied as a ratio to every model so the
 *                    per-chr turbo of the arena's enemies survives it; restored at stage load.
 *   line mode        the Line Mode cheat's flag (boss.c draws it); restored at stage load.
 *   give double      the 2x weapon cheats' inventory pairs, for any Bond (the cheats themselves are solo only).
 *   scale            modelSetScale plus chr->chrwidth / chrheight (movement collision box, hit-height bands).
 *
 * SIM: scripts/port/snap_embed.py SIM_PORT. Every input is sim state (rocket explosions, chr positions, the faction table); no RNG is
 * drawn here (skins and ring offsets come from serial counters). Inert until a script arms something: the hooks return at once. */
#ifndef TARGET_N64

#include <string.h>
#include <math.h>
#include <ultra64.h>
#include <PR/gu.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include <bondaicommands.h>
#include "game/chr.h"
#include "game/chraction.h"
#include "game/player.h"
#include "game/bondinv.h"
#include "game/gun.h"
#include "game/model.h"
#include "game/stan.h"
#include "game/debugmenu_handler.h"
#include "port_spawn.h"
#include "port_mode.h"
#include "port_faction.h"
#include "port_botlogic.h"
#include "port_blocks.h"
#include "port_chrthrow.h"
#include "port_whacky.h"

extern void portConLogf(const char *fmt, ...);
extern s32  portMpCharBodyHead(s32 c, s32 *body, s32 *head, f32 *pov);           /* front.c */
extern int  portHeistOffsetStan(coord3d *pos, StandTile **stan, float dx, float dz);  /* port_heist.c */
extern s32  get_ammo_type_for_weapon(ITEM_IDS weapon);
extern f32  animation_rate;      /* chr.c */
extern ChrRecord *g_ChrSlots;
extern s32  g_NumChrSlots;

#define MINION_MAX      32
#define MINION_CHRNUM0  28000
#define MINION_CHRNUM1  32000
#define BOOM_MAX        8
#define THINK_TICKS     6       /* a minion decides 10 times a second */
#define THROW_RANGE     2600.0f
#define THROW_COOLDOWN  50      /* ticks between a minion's throws */
#define THROW_BUSY      28      /* ticks the throw animation owns the chr */
#define SEEK_RANGE      9000.0f
#define FOLLOW_DIST     700.0f
#define HATCH_TICKS     150     /* invincible this long after the burst (at 60 all three still died in the blast at 1.6 s) */
#define FADE_STEP       6      /* fade alpha per tick when its life is over (255 -> 0 in ~0.7 s) */

typedef struct {
    s32 shots;     /* minion rockets left */
    s32 count;     /* minions per rocket */
    f32 scale, life, health, anim;
    s32 hostile;
} Launcher;

typedef struct {
    s32 chrnum;    /* 0 = free */
    s32 owner;
    s32 born, life, nextThink, nextThrow, busyUntil, nextMove;
    u8  hostile, fading;
} Minion;

typedef struct {
    coord3d pos;
    StandTile *stan;
    s32 owner;
} Boom;

static Launcher sLaunch[4];
static Minion   sMin[MINION_MAX];
static Boom     sBoom[BOOM_MAX];
static s32      sBoomN;
static s32      sTicks;
static s32      sNextChrnum = MINION_CHRNUM0;
static s32      sSerial;          /* skins and ring offsets */
static s32      sCap = 12;        /* most live minions; a new one over the cap fades the oldest */
static s32      sSpawned, sExpired;
static s32      sRateSet, sLineSet;

static const char *const sMinionSkins[] = { "xenia", "natalya", "mayday", "jaws", "oddjob", "baron_samedi", "boris", "valentin" };

void portWhackyReset(void)
{
    if (sRateSet)
    {
        animation_rate = 1.0f;   /* the next stage's chrs take the stock rate (a stage loads its chrs after this) */
    }
    if (sLineSet)
    {
        set_debug_VisCVG_flag(FALSE);
    }
    memset(sLaunch, 0, sizeof(sLaunch));
    memset(sMin, 0, sizeof(sMin));
    sBoomN = 0;
    sTicks = 0;
    sNextChrnum = MINION_CHRNUM0;
    sSerial = 0;
    sCap = 12;
    sSpawned = sExpired = 0;
    sRateSet = sLineSet = 0;
    portChrFxClearAll();   /* the presentation table: no colour survives into the next stage */
}

/* ---- the plain cheats ------------------------------------------------------------------------------------------------------------ */

f32 portWhackyAnimRate(f32 r)
{
    s32 i;
    f32 ratio;

    if (r <= 0.0f || !portModeActive())
    {
        return animation_rate;
    }
    if (r < 0.2f) r = 0.2f;
    if (r > 4.0f) r = 4.0f;
    ratio = (animation_rate > 0.0f) ? r / animation_rate : 1.0f;
    for (i = 0; i < g_NumChrSlots; i++)
    {   /* relative: an arena turbo chr (its own play speed) keeps its edge over the others */
        Model *m = g_ChrSlots[i].model;

        if (m != NULL)
        {
            f32 cur = (m->unkb0 > 0.0f) ? m->animrate : m->playspeed;

            modelSetAnimPlaySpeed(m, cur * ratio, 0.0f);
        }
    }
    animation_rate = r;
    sRateSet = 1;
    portConLogf("ge: whacky: animation rate %.2f\n", (double)r);
    return animation_rate;
}

int portWhackyLineMode(int on)
{
    int was = get_debug_VisCVG_flag() != 0;

    if (on >= 0 && portModeActive())
    {
        set_debug_VisCVG_flag(on ? TRUE : FALSE);
        sLineSet = 1;
    }
    return was;
}

/* which: 0 rockets, 1 grenade launchers, 2 RC-P90s, 3 throwing knives, 4 hunting knives, 5 moonraker lasers */
int portWhackyGiveDouble(int p, int which)
{
    static const ITEM_IDS items[] = { ITEM_ROCKETLAUNCH, ITEM_GRENADELAUNCH, ITEM_FNP90, ITEM_THROWKNIFE, ITEM_KNIFE, ITEM_LASER };
    s32 saved, type;
    ITEM_IDS it;

    if (!portModeActive() || which < 0 || which >= (int)(sizeof(items) / sizeof(items[0])) || p < 0 || p >= portBondCount() ||
        g_playerPointers[p] == NULL)
    {
        return 0;
    }
    it = items[which];
    saved = get_cur_playernum();
    set_cur_player(p);
    bondinvAddInvItem(it);
    bondinvAddDoublesInvItem(it, it);
    type = get_ammo_type_for_weapon(it);
    if (type > 0)
    {
        give_cur_player_ammo(type, get_max_ammo_for_type(type));
    }
    set_cur_player(saved);
    portConLogf("ge: whacky: Bond %d gets 2x %d\n", p, (int)it);
    return 1;
}

static void chrScale(ChrRecord *c, f32 f)
{
    modelSetScale(c->model, c->model->scale * f);
    c->chrwidth *= f;
    c->chrheight *= f;
    if (c->chrwidth < 6.0f) c->chrwidth = 6.0f;
    if (c->chrheight < 30.0f) c->chrheight = 30.0f;
}

int portWhackyChrScale(int chrnum, float f)
{
    ChrRecord *c = portModeActive() ? chrFindByLiteralId(chrnum) : NULL;

    if (c == NULL || c->model == NULL || c->prop == NULL || f < 0.1f || f > 3.0f)
    {
        return 0;
    }
    chrScale(c, f);
    portConLogf("ge: whacky: chr %d scale x%.2f (model %.4f, width %.1f, height %.1f)\n", chrnum, (double)f, (double)c->model->scale,
                (double)c->chrwidth, (double)c->chrheight);
    return 1;
}

/* ---- minions --------------------------------------------------------------------------------------------------------------------- */

int portWhackyLauncher(int p, int shots, int count, float scale, float life, float health, float anim, int hostile, int cap)
{
    Launcher *L;

    if (p < 0 || p >= 4 || !portModeActive())
    {
        return -1;
    }
    L = &sLaunch[p];
    if (shots < 0)
    {
        return L->shots;   /* query */
    }
    L->shots = shots;
    L->count = count < 1 ? 1 : (count > 6 ? 6 : count);
    L->scale = (scale < 0.1f || scale > 1.0f) ? 0.25f : scale;
    L->life = life > 0.0f ? life : 15.0f;
    L->health = health > 0.0f ? health : 0.3f;
    L->anim = (anim < 0.5f || anim > 3.0f) ? 3.0f : anim;
    L->hostile = hostile ? 1 : 0;
    if (cap > 0)
    {
        sCap = cap > MINION_MAX ? MINION_MAX : cap;
    }
    portConLogf("ge: whacky: minion launcher for Bond %d: %d shots, %d a rocket, scale %.2f, life %.0f s, hp x%.2f, anim x%.1f, %s, cap %d\n",
                p, L->shots, L->count, (double)L->scale, (double)L->life, (double)L->health, (double)L->anim,
                L->hostile ? "HOSTILE" : "on Bond's side", (int)sCap);
    return L->shots;
}

static s32 bondOfProp(PropRecord *pp)
{
    s32 p, n = portBondCount();

    for (p = 0; p < n && p < 4; p++)
    {
        if (pp != NULL && g_playerPointers[p] != NULL && g_playerPointers[p]->prop == pp)
        {
            return p;
        }
    }
    return -1;
}

static void queueBoom(const coord3d *pos, StandTile *stan, s32 owner)
{
    if (sBoomN >= BOOM_MAX || stan == NULL)
    {
        return;
    }
    sBoom[sBoomN].pos = *pos;
    sBoom[sBoomN].stan = stan;
    sBoom[sBoomN].owner = owner;
    sBoomN++;
}

void portWhackyRocketBoom(PropRecord *rocket)
{
    ObjectRecord *o;
    PropRecord *at;
    s32 p;

    if (rocket == NULL || (sLaunch[0].shots | sLaunch[1].shots | sLaunch[2].shots | sLaunch[3].shots) == 0 || !portModeActive())
    {
        return;
    }
    o = rocket->obj;
    if (o == NULL || o->projectile == NULL)
    {
        return;
    }
    p = bondOfProp(o->projectile->ownerprop);
    if (p < 0 || sLaunch[p].shots <= 0)
    {
        return;   /* a guard's rocket (Trevelyan), or this Bond's launcher is plain */
    }
    sLaunch[p].shots--;
    at = rocket;
    while (at->parent != NULL)
    {
        at = at->parent;
    }
    queueBoom(rocket->parent != NULL ? &at->pos : &o->runtime_pos, at->stan, p);
    portConLogf("ge: whacky: minion rocket burst at (%.0f, %.0f, %.0f), Bond %d has %d left\n", (double)o->runtime_pos.x,
                (double)o->runtime_pos.y, (double)o->runtime_pos.z, p, sLaunch[p].shots);
}

static s32 liveMinions(void)
{
    s32 i, n = 0;

    for (i = 0; i < MINION_MAX; i++)
    {
        if (sMin[i].chrnum != 0 && !sMin[i].fading)
        {
            n++;
        }
    }
    return n;
}

static Minion *freeMinion(void)
{
    s32 i, old = -1;

    for (i = 0; i < MINION_MAX; i++)
    {
        if (sMin[i].chrnum == 0)
        {
            return &sMin[i];
        }
    }
    for (i = 0; i < MINION_MAX; i++)
    {   /* full: the oldest one goes now (removed, no fade) */
        if (old < 0 || sMin[i].born < sMin[old].born)
        {
            old = i;
        }
    }
    {
        ChrRecord *c = chrFindByLiteralId(sMin[old].chrnum);

        if (c != NULL)
        {
            c->hidden |= CHRHIDDEN_REMOVE;
        }
    }
    memset(&sMin[old], 0, sizeof(sMin[old]));
    return &sMin[old];
}

static ChrRecord *spawnMinion(const coord3d *at, StandTile *stan, s32 owner, const Launcher *L, s32 k)
{
    PortSpawnDesc d;
    coord3d pos = *at;
    StandTile *st = stan;
    PropRecord *anchor = (owner >= 0 && owner < 4 && g_playerPointers[owner] != NULL) ? g_playerPointers[owner]->prop : NULL;
    s32 body = BODY_Natalya_Skirt, head = -1, sk, mp;
    f32 pov, ang, r;
    ChrRecord *c;
    Minion *m;
    const char *skin = sMinionSkins[sSerial % (s32)(sizeof(sMinionSkins) / sizeof(sMinionSkins[0]))];

    if (anchor == NULL)
    {
        return NULL;
    }
    ang = 2.3999632f * (f32)(sSerial + k);   /* the golden angle: a ring that never stacks two on one spot */
    r = 110.0f + 45.0f * (f32)k;
    if (!portHeistOffsetStan(&pos, &st, sinf(ang) * r, cosf(ang) * r))
    {
        pos = *at;
        st = stan;
    }
    pos.y = stanGetPositionYValue(st, pos.x, pos.z);
    sk = portBotSkinFind(skin);
    mp = sk >= 0 ? portBotSkinMp(sk) : -1;
    if (mp < 0 || !portMpCharBodyHead(mp, &body, &head, &pov))
    {
        body = BODY_Natalya_Skirt;
        head = -1;
    }
    while (chrFindByLiteralId(sNextChrnum) != NULL)
    {
        sNextChrnum = (sNextChrnum + 1 >= MINION_CHRNUM1) ? MINION_CHRNUM0 : sNextChrnum + 1;
    }
    memset(&d, 0, sizeof(d));
    d.body = body;
    d.head = head;
    d.nearprop = anchor;
    d.angle = ang;
    d.ailistid = GAILIST_COOP_BOT;   /* idles: this file drives it */
    d.maxdamage = 4.0f * L->health;
    /* Bond's autoaim never locks on to his own minions; and every minion is invincible for its first second: it hatches inside the
     * rocket's blast, which keeps hurting for a while (without this all three died 0.3 s after the burst) */
    d.chrflags_on = (L->hostile ? 0 : CHRFLAG_NO_AUTOAIM) | CHRFLAG_INVINCIBLE;
    d.chrnum = sNextChrnum;
    c = portSpawnChrAt(&d, &pos, st);
    sSerial++;
    if (c == NULL)
    {
        return NULL;
    }
    sNextChrnum = (sNextChrnum + 1 >= MINION_CHRNUM1) ? MINION_CHRNUM0 : sNextChrnum + 1;
    if (c->model == NULL || c->model->obj == NULL)
    {
        c->hidden |= CHRHIDDEN_REMOVE;
        return NULL;
    }
    chrScale(c, L->scale);
    modelSetAnimPlaySpeed(c->model, animation_rate * L->anim, 0.0f);
    portFactionSetChr(c, L->hostile ? PORT_FACTION_HOSTILE : PORT_FACTION_BOND);
    if (liveMinions() >= sCap)
    {   /* over the cap: the oldest live one starts to fade */
        s32 i, old = -1;

        for (i = 0; i < MINION_MAX; i++)
        {
            if (sMin[i].chrnum != 0 && !sMin[i].fading && (old < 0 || sMin[i].born < sMin[old].born))
            {
                old = i;
            }
        }
        if (old >= 0)
        {
            sMin[old].fading = 1;
        }
    }
    m = freeMinion();
    m->chrnum = c->chrnum;
    m->owner = owner;
    m->born = sTicks;
    m->life = (s32)(L->life * 60.0f);
    m->nextThink = sTicks + 1 + k;
    m->nextThrow = sTicks + 20;
    m->busyUntil = 0;
    m->nextMove = 0;
    m->hostile = (u8)L->hostile;
    m->fading = 0;
    sSpawned++;
    portConLogf("ge: whacky: minion chr %d (%s) for Bond %d at (%.0f, %.0f, %.0f): model scale %.4f, width %.1f, height %.1f, hp %.2f, %s\n",
                (int)c->chrnum, skin, owner, (double)pos.x, (double)pos.y, (double)pos.z, (double)c->model->scale, (double)c->chrwidth,
                (double)c->chrheight, (double)c->maxdamage, L->hostile ? "hostile" : "ally");
    return c;
}

/* test hook / crowd gift: n minions for Bond p, `dist` units ahead of him (the launcher's settings, even with no shots left) */
int portWhackyMinionsAt(int p, float x, float z, int n)
{
    PropRecord *bp;
    coord3d pos;
    StandTile *st;
    s32 k, made = 0;
    Launcher L;

    if (!portModeActive() || p < 0 || p >= 4 || g_playerPointers[p] == NULL || (bp = g_playerPointers[p]->prop) == NULL || bp->stan == NULL)
    {
        return 0;
    }
    L = sLaunch[p];
    if (L.count <= 0)
    {   /* never armed: the defaults */
        L.scale = 0.25f;
        L.life = 15.0f;
        L.health = 0.3f;
        L.anim = 3.0f;
        L.hostile = 0;
    }
    pos = bp->pos;
    st = bp->stan;
    if (!portHeistOffsetStan(&pos, &st, x - bp->pos.x, z - bp->pos.z))
    {
        return 0;
    }
    for (k = 0; k < n && k < 6; k++)
    {
        if (spawnMinion(&pos, st, p, &L, k) != NULL)
        {
            made++;
        }
    }
    return made;
}

int portWhackyIsMinion(int chrnum)
{
    s32 i;

    if (chrnum < MINION_CHRNUM0 || chrnum >= MINION_CHRNUM1)
    {
        return 0;
    }
    for (i = 0; i < MINION_MAX; i++)
    {
        if (sMin[i].chrnum == chrnum)
        {
            return 1;
        }
    }
    return 0;
}

int portWhackyMinionList(int *out, int max)
{
    s32 i, n = 0;

    for (i = 0; i < MINION_MAX && n < max; i++)
    {
        if (sMin[i].chrnum != 0)
        {
            out[n++] = sMin[i].chrnum;
        }
    }
    return n;
}

void portWhackyMinionStats(int *spawned, int *expired)
{
    *spawned = sSpawned;
    *expired = sExpired;
}

/* every live minion fades out now (end of the run, a test) */
int portWhackyMinionsFade(void)
{
    s32 i, n = 0;

    for (i = 0; i < MINION_MAX; i++)
    {
        if (sMin[i].chrnum != 0 && !sMin[i].fading)
        {
            sMin[i].fading = 1;
            n++;
        }
    }
    return n;
}

static f32 dist2(const PropRecord *a, const PropRecord *b)
{
    f32 dx = a->pos.x - b->pos.x, dz = a->pos.z - b->pos.z;

    return dx * dx + dz * dz;
}

static void minionThink(Minion *m, ChrRecord *c)
{
    PropRecord *bp = (m->owner >= 0 && m->owner < 4 && g_playerPointers[m->owner] != NULL) ? g_playerPointers[m->owner]->prop : NULL;
    PropRecord *tp = NULL;
    ChrRecord *t = NULL;
    s32 los = 0;

    if (c->prop->stan == NULL || bp == NULL)
    {
        return;
    }
    if (m->hostile)
    {
        tp = bp;   /* chaos: they want Bond */
        los = 1;
    }
    else
    {
        t = portFactionNearestHostile(c, SEEK_RANGE, TRUE);
        los = (t != NULL);
        if (t == NULL)
        {
            t = portFactionNearestHostile(c, SEEK_RANGE, FALSE);
        }
        tp = (t != NULL) ? t->prop : NULL;
    }
    if (tp != NULL && los && sTicks >= m->nextThrow && dist2(c->prop, tp) < THROW_RANGE * THROW_RANGE)
    {
        extern bool chrIsNotDeadOrShot(ChrRecord *self);   /* chraction.c, not in chraction.h */

        if (chrIsNotDeadOrShot(c) && portChrThrowKnife(c, tp))
        {
            m->nextThrow = sTicks + THROW_COOLDOWN;
            m->busyUntil = sTicks + THROW_BUSY;
            m->nextMove = 0;
            return;
        }
    }
    if (sTicks < m->nextMove)
    {
        return;
    }
    m->nextMove = sTicks + 30;
    if (tp != NULL && dist2(c->prop, tp) > 450.0f * 450.0f)
    {
        if (m->hostile || t == NULL) chrGoToBond(c, SPEED_SPRINT);
        else chrGoToChr(c, t->chrnum, SPEED_SPRINT);
    }
    else if (tp == NULL && !m->hostile && dist2(c->prop, bp) > FOLLOW_DIST * FOLLOW_DIST)
    {
        chrGoToBond(c, SPEED_RUN);   /* nothing to fight: tag along */
    }
}

void portWhackyTick(f32 dt)
{
    s32 i, step = (s32)(dt * 60.0f + 0.5f);

    if (sBoomN == 0 && (sLaunch[0].shots | sLaunch[1].shots | sLaunch[2].shots | sLaunch[3].shots) == 0)
    {
        for (i = 0; i < MINION_MAX && sMin[i].chrnum == 0; i++)
        {
        }
        if (i == MINION_MAX)
        {
            return;   /* inert */
        }
    }
    if (!portModeActive() || step <= 0)
    {
        return;
    }
    sTicks += step;
    for (i = 0; i < sBoomN; i++)
    {
        const Launcher *L = &sLaunch[sBoom[i].owner];
        Launcher def;
        s32 k, n;

        if (L->count <= 0)
        {
            memset(&def, 0, sizeof(def));
            def.count = 3; def.scale = 0.25f; def.life = 15.0f; def.health = 0.3f; def.anim = 3.0f;
            L = &def;
        }
        n = L->count;
        for (k = 0; k < n; k++)
        {
            spawnMinion(&sBoom[i].pos, sBoom[i].stan, sBoom[i].owner, L, k);
        }
    }
    sBoomN = 0;
    for (i = 0; i < MINION_MAX; i++)
    {
        Minion *m = &sMin[i];
        ChrRecord *c;

        if (m->chrnum == 0)
        {
            continue;
        }
        c = chrFindByLiteralId(m->chrnum);
        if (c == NULL || c->model == NULL || c->prop == NULL || chrIsDead(c))
        {
            portConLogf("ge: whacky: minion chr %d %s after %.1f s\n", (int)m->chrnum, c == NULL || c->prop == NULL ? "gone" : "killed",
                        (double)(sTicks - m->born) / 60.0);
            memset(m, 0, sizeof(*m));   /* gone, or killed: the stock death fades and removes the body */
            continue;
        }
        if (sTicks - m->born >= HATCH_TICKS && (c->chrflags & CHRFLAG_INVINCIBLE))
        {
            c->chrflags &= ~CHRFLAG_INVINCIBLE;   /* out of the blast: mortal now */
        }
        if (!m->fading && sTicks - m->born >= m->life)
        {
            m->fading = 1;
        }
        if (m->fading)
        {
            s32 a = (s32)c->fadealpha - FADE_STEP * step;

            if (a <= 8)
            {
                c->hidden |= CHRHIDDEN_REMOVE;
                memset(m, 0, sizeof(*m));
                sExpired++;
            }
            else
            {
                c->fadealpha = (u8)a;
            }
            continue;
        }
        if (sTicks < m->nextThink || sTicks < m->busyUntil)
        {
            continue;
        }
        m->nextThink = sTicks + THINK_TICKS;
        minionThink(m, c);
    }
}

#endif /* TARGET_N64 */
