/* port_arenacrowd.c - arena drop A8 (modding/arena.md "Drop A8"): what the crowd-favour script needs to know about a kill that the
 * stock events do not say: where the last Bond shot hit a chr (head, gun, ...), with what, from how far and by which Bond, and which
 * explosion (and whose) hurt it last; shots that hit a spectator or a held gun; a Bond's shots fired / hits. Plus spawn.disarm (the gun
 * a Bond shot is knocked out of the hand: the stock game only chips the gun model) and arena.sfx (a stock sound by id).
 *
 * Hooks (all #ifndef TARGET_N64, no-ops outside a game mode or for chrs the arena did not spawn):
 *   chraction.c handles_shot_actors   portArenaShotHit (after the damage), portArenaCrowdShot (the invincible branch)
 *   explosion.c explosionCreate       portArenaBlastCreated (the owner: a Bond whose rocket / grenade it was)
 *   explosion.c explosionInflictDamage portArenaBlastHit (before chrlvExplosionDamage)
 * Everything is keyed by chrnum / player index / explosion slot and written only from the sim, so both netplay machines agree. The tables
 * are sim state: SIM in scripts/port/snap_embed.py. */
#ifndef TARGET_N64

#include <string.h>
#include <math.h>
#include <ultra64.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include <bondaicommands.h>
#include "game/chr.h"
#include "game/chrai.h"
#include "game/chraction.h"
#include "game/explosion.h"
#include "game/player.h"
#include "game/propobj.h"
#include "game/lv.h"
#include "port_mode.h"
#include "port_spawn.h"
#include "port_arena_a8.h"

extern void portConLogf(const char *fmt, ...);
extern s32 portArenaChrIsEnemy(const ChrRecord *chr);
extern s32 portArenaChrSpectator(const ChrRecord *chr);
extern const char *portArenaWeaponName(s32 item);
extern s32 portArenaWeaponItem(const char *name);
extern s32 portBondCount(void);
extern void *portSndPlaySfxPitched(void *bank, s16 id, f32 pitch);   /* snd.c */
extern void *g_musicSfxBufferPtr;

#define HIT_N    1024   /* direct-mapped by chrnum (arena chrnums are consecutive) */
#define BLAST_N  128    /* >= the explosion buffer (portExplosionCap: 100) */
#define EV_N     32
#define PART_BLAST 1000

typedef struct
{
    s32 chrnum;     /* owner of the entry, 0 = empty */
    s32 part;       /* HIT_* of the last Bond shot, PART_BLAST for a blast */
    s32 weapon;     /* ITEM_IDS of that shot, -1 for a blast */
    s32 player;     /* the Bond who fired it / owns the blast, -1 = none (an enemy's rocket, a corpse explosion) */
    f32 dist;       /* that Bond to the chr when it landed */
    s32 tick;       /* g_GlobalTimer */
    s32 blast;      /* serial of the blast (0 = the last hit was a bullet) */
} HitRec;

static HitRec sHit[HIT_N];

/* the guns an arena enemy held (ITEM_IDS, both hands; 0 = none), kept after they leave its hands (disarm, the death drop), so the crowd
 * can tell a kill with the victim's own gun (THEIR OWN GUN). Updated at spawn, on every hit and at a disarm, only while it holds one. */
typedef struct { s32 chrnum; s32 item[2]; } OwnedRec;
static OwnedRec sOwned[HIT_N];

void portArenaNoteHeld(const ChrRecord *chr)
{
    OwnedRec *o;
    s32 i, item[2] = { 0, 0 }, any = 0;

    if (chr == NULL || !portModeActive() || !portArenaChrIsEnemy(chr))
    {
        return;
    }
    for (i = 0; i < 2; i++)
    {
        PropRecord *w = chr->weapons_held[i];

        if (w != NULL && w->weapon != NULL && w->weapon->weaponnum > 0)
        {
            item[i] = (s32)w->weapon->weaponnum;
            any = 1;
        }
    }
    o = &sOwned[(u32)chr->chrnum & (HIT_N - 1)];
    if (o->chrnum != (s32)chr->chrnum)
    {   /* a new chr in this slot: forget the old one's guns */
        o->chrnum = (s32)chr->chrnum;
        o->item[0] = o->item[1] = 0;
    }
    if (any)
    {
        o->item[0] = item[0];
        o->item[1] = item[1];
    }
}

/* spawn.owned_guns(chr) -> the names of the guns it held (0, 1 or 2; nothing when none is known) */
int portLuaGameOwnedGuns(int chrnum, const char **g1, const char **g2)
{
    const OwnedRec *o = &sOwned[(u32)chrnum & (HIT_N - 1)];
    const char *names[2] = { NULL, NULL };
    int i, n = 0;

    *g1 = *g2 = NULL;
    if (!portModeActive() || chrnum == 0 || o->chrnum != chrnum)
    {
        return 0;
    }
    for (i = 0; i < 2; i++)
    {
        const char *nm = o->item[i] > 0 ? portArenaWeaponName(o->item[i]) : NULL;

        if (nm != NULL)
        {
            names[n++] = nm;
        }
    }
    *g1 = names[0];
    *g2 = names[1];
    return n;
}
static s32 sBlastOwner[BLAST_N], sBlastSerial[BLAST_N], sBlastNext, sBlastForce = -1;

typedef struct { s32 kind, chr, player, tick; } HitEv;   /* kind 1 = a held gun hit (disarm), 2 = a spectator hit */
static HitEv sEv[EV_N];
static s32 sEvN, sEvLost;

static void evPush(s32 kind, s32 chr, s32 player)
{
    if (sEvN >= EV_N)
    {
        sEvLost++;
        return;   /* the script polls every 0.25 s; a full ring keeps the oldest (any order works, both machines drop the same) */
    }
    sEv[sEvN].kind = kind;
    sEv[sEvN].chr = chr;
    sEv[sEvN].player = player;
    sEv[sEvN].tick = (s32)g_GlobalTimer;
    sEvN++;
}

static f32 bondDist(s32 player, const ChrRecord *chr)
{
    PropRecord *bp;
    f32 dx, dy, dz;

    if (player < 0 || player >= 4 || g_playerPointers[player] == NULL || (bp = g_playerPointers[player]->prop) == NULL || chr->prop == NULL)
    {
        return 0.0f;
    }
    dx = bp->pos.x - chr->prop->pos.x;
    dy = bp->pos.y - chr->prop->pos.y;
    dz = bp->pos.z - chr->prop->pos.z;
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

/* handles_shot_actors, after the damage of a shot that landed on a live chr (isPlayer: a Bond fired it; the current player is that Bond) */
void portArenaShotHit(ChrRecord *chr, s32 hitpart, s32 weaponid, s32 isPlayer)
{
    HitRec *h;
    s32 p;

    if (!isPlayer || chr == NULL || !portModeActive() || !portArenaChrIsEnemy(chr))
    {
        return;
    }
    portArenaNoteHeld(chr);
    p = get_cur_playernum();
    h = &sHit[(u32)chr->chrnum & (HIT_N - 1)];
    h->chrnum = (s32)chr->chrnum;
    h->part = hitpart;
    h->weapon = weaponid;
    h->player = p;
    h->dist = bondDist(p, chr);
    h->tick = (s32)g_GlobalTimer;
    h->blast = 0;
    if (hitpart == HIT_GUN)
    {
        evPush(1, (s32)chr->chrnum, p);
    }
}

/* handles_shot_actors, the invincible branch: a Bond shot a spectator */
void portArenaCrowdShot(ChrRecord *chr, s32 isPlayer)
{
    if (isPlayer && chr != NULL && portArenaChrSpectator(chr))
    {
        evPush(2, (s32)chr->chrnum, get_cur_playernum());
    }
}

/* explosionCreate: remember whose explosion this slot holds. A Bond's own rocket / grenade explodes with its projectile as the source; the
 * projectile's owner is his prop. Anything else (an enemy's rocket, a barrel, an explosive corpse) has no owner. */
void portArenaBlastCreated(s32 slot, PropRecord *source)
{
    s32 owner = -1;

    if (slot < 0 || slot >= BLAST_N || !portModeActive())
    {
        return;
    }
    if (sBlastForce >= 0)
    {   /* spawn.explode(chr, kind, player): a test blast credited to that Bond */
        owner = sBlastForce;
    }
    else if (source != NULL && (source->type == PROP_TYPE_WEAPON || source->type == PROP_TYPE_OBJ) && source->obj != NULL &&
             source->obj->projectile != NULL && source->obj->projectile->ownerprop != NULL &&
             source->obj->projectile->ownerprop->type == PROP_TYPE_VIEWER)
    {
        owner = getPlayerPointerIndex(source->obj->projectile->ownerprop);
    }
    sBlastOwner[slot] = owner;
    sBlastSerial[slot] = ++sBlastNext;
}

/* explosionInflictDamage, before the blast damages a chr */
void portArenaBlastHit(ChrRecord *chr, s32 slot)
{
    HitRec *h;

    if (chr == NULL || slot < 0 || slot >= BLAST_N || !portModeActive() || !portArenaChrIsEnemy(chr) ||
        chr->actiontype == ACT_DIE || chr->actiontype == ACT_DEAD)
    {
        return;
    }
    portArenaNoteHeld(chr);
    h = &sHit[(u32)chr->chrnum & (HIT_N - 1)];
    h->chrnum = (s32)chr->chrnum;
    h->part = PART_BLAST;
    h->weapon = -1;
    h->player = sBlastOwner[slot];
    h->dist = bondDist(sBlastOwner[slot], chr);
    h->tick = (s32)g_GlobalTimer;
    h->blast = sBlastSerial[slot];
}

/* ---- Lua side ---- */

static const char *partName(s32 part)
{
    switch (part)
    {
        case HIT_HEAD:   return "head";
        case HIT_GUN:    return "gun";
        case HIT_HAT:    return "hat";
        case HIT_CHEST: case HIT_PELVIS: return "body";
        case PART_BLAST: return "blast";
        default:         return "limb";
    }
}

int portLuaGameLastHit(int chrnum, const char **part, const char **weapon, int *player, float *dist, float *age, int *blast)
{
    const HitRec *h = &sHit[(u32)chrnum & (HIT_N - 1)];

    if (!portModeActive() || h->chrnum != chrnum || chrnum == 0)
    {
        return 0;
    }
    *part = partName(h->part);
    *weapon = h->weapon >= 0 ? portArenaWeaponName(h->weapon) : NULL;
    if (h->weapon >= 0 && *weapon == NULL)
    {
        *weapon = "other";
    }
    *player = h->player;
    *dist = h->dist;
    *age = (f32)((s32)g_GlobalTimer - h->tick) / 60.0f;
    *blast = h->blast;
    return 1;
}

int portLuaGameHitEvents(int *kind, int *chr, int *player, int max)
{
    s32 i, n = sEvN < max ? sEvN : max;

    for (i = 0; i < n; i++)
    {
        kind[i] = sEv[i].kind;
        chr[i] = sEv[i].chr;
        player[i] = sEv[i].player;
    }
    sEvN = 0;
    return n;
}

int portLuaGameHitEventsLost(void)
{
    s32 n = sEvLost;

    sEvLost = 0;
    return n;
}

/* spawn.disarm(chr): the held guns fall to the floor, as the stock death drop does (propobjSetDropped), and the chr goes on with the
 * arena's melee chase list (an unarmed chr in a firing list would stand and "fire" nothing). 1 = something was dropped. */
int portLuaGameChrDisarm(int chrnum)
{
    ChrRecord *c = portModeActive() ? chrFindByLiteralId(chrnum) : NULL;
    s32 i, n = 0;

    if (c == NULL || c->prop == NULL || c->model == NULL || chrIsDead(c) || !portArenaChrIsEnemy(c))
    {
        return 0;
    }
    portArenaNoteHeld(c);   /* the guns it drops stay "its own" (a kill with one counts as THEIR OWN GUN) */
    for (i = 0; i < 2; i++)
    {
        PropRecord *w = c->weapons_held[i];

        if (w != NULL && w->obj != NULL && (w->obj->flags & PROPFLAG_AIUNDROPPABLE) == 0)
        {
            propobjSetDropped(w, 1);
            c->hidden |= CHRHIDDEN_DROP_HELD_ITEMS;
            n++;
        }
    }
    if (n > 0)
    {
        c->ailist = ailistFindById(GAILIST_ARENA_CHASE);
        c->aioffset = 0;
        c->sleep = 0;
        portConLogf("ge: arena: DISARM chr %d: %d gun(s) knocked out of the hand\n", chrnum, (int)n);
    }
    return n > 0;
}

/* bond.shots(p): shots fired and shots that hit a chr (the stock per-player stat counters), 0 = no such Bond */
int portLuaGameBondShots(int player, int *fired, int *hits)
{
    const s32 *sc;

    if (!portModeActive() || player < 0 || player >= 4 || player >= portBondCount())
    {
        return 0;
    }
    sc = g_playerPlayerData[player].shot_count;
    *fired = sc[SHOT_REGISTER_TOTAL];
    *hits = sc[SHOT_REGISTER_HEAD] + sc[SHOT_REGISTER_BODY] + sc[SHOT_REGISTER_LIMB] + sc[SHOT_REGISTER_GUN] + sc[SHOT_REGISTER_HAT];
    return 1;
}

/* test.shoot(chr, part, weapon, player): test helper, a real shot landing on the chr through handles_shot_actors (the damage code a
 * bullet takes, with its hit part) as Bond `player` fired it from where he stands */
int portLuaGameChrShoot(int chrnum, const char *part, const char *weapon, int player)
{
    ChrRecord *c = portModeActive() ? chrFindByLiteralId(chrnum) : NULL;
    s32 hp = HIT_CHEST, item, saved;
    coord3d dir;
    PropRecord *bp;
    f32 d;

    if (c == NULL || c->prop == NULL || c->model == NULL || player < 0 || player >= portBondCount() || g_playerPointers[player] == NULL ||
        (bp = g_playerPointers[player]->prop) == NULL)
    {
        return 0;
    }
    if (part != NULL)
    {
        if (strcmp(part, "head") == 0) hp = HIT_HEAD;
        else if (strcmp(part, "gun") == 0) hp = HIT_GUN;
        else if (strcmp(part, "limb") == 0) hp = HIT_LEFT_ARM;
        else if (strcmp(part, "hat") == 0) hp = HIT_HAT;
    }
    item = (weapon != NULL && strcmp(weapon, "fist") == 0) ? ITEM_FIST : portArenaWeaponItem(weapon != NULL ? weapon : "pp7");
    if (item < 0)
    {
        return -1;
    }
    dir.x = c->prop->pos.x - bp->pos.x;
    dir.y = 0.0f;
    dir.z = c->prop->pos.z - bp->pos.z;
    d = sqrtf(dir.x * dir.x + dir.z * dir.z);
    if (d > 0.0f)
    {
        dir.x /= d;
        dir.z /= d;
    }
    saved = get_cur_playernum();
    set_cur_player(player);
    handles_shot_actors(c, hp, &dir, item, TRUE);
    set_cur_player(saved);
    return 1;
}

/* spawn.explode(chr, kind, player): the next explosion is credited to that Bond (-1 = nobody); port_arenafx.c calls this around its create */
void portArenaBlastForce(int player)
{
    sBlastForce = player;
}

/* arena.sfx(id [, pitch]): a stock sound effect by its id (bondconstants.h, the *_SFX list), no position. 1 = it plays. */
int portLuaGameSfx(int id, float pitch)
{
    if (!portModeActive() || id <= 0 || id > 261 || g_musicSfxBufferPtr == NULL)
    {
        return 0;
    }
    return portSndPlaySfxPitched(g_musicSfxBufferPtr, (s16)id, pitch) != NULL;
}

#endif /* TARGET_N64 */
