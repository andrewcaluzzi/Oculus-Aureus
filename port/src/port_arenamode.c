/* port_arenamode.c - the game half of the arena mode's Lua API (modding/arena.md, drop A2):
 * a fully specified enemy (skin, weapon, health, accuracy, speed, behaviour, knives), removal and
 * liveness queries, per-Bond give and heal. Everything is keyed by explicit player indexes or chrnums,
 * never the local player, so it is safe in co-op. Called from the Lua bindings (port_luavm_arena.c). */
#ifndef TARGET_N64

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ultra64.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include <bondaicommands.h>
#include "game/chr.h"
#include "game/chrai.h"
#include "game/chraction.h"
#include "game/player.h"
#include "game/objecthandler.h"
#include "game/propobj.h"
#include "game/bondview.h"
#include "game/bondinv.h"
#include "game/gun.h"
#include "game/model.h"
#include "game/math_atan2f.h"
#include <memp.h>
#include "port_lua.h"
#include "port_spawn.h"
#include "port_blocks.h"
#include "port_mode.h"
#include "port_faction.h"
#include "port_botlogic.h"

extern void portConLogf(const char *fmt, ...);
extern s32 chrGetNumFree(void);
extern s32 portMpCharBodyHead(s32 c, s32 *body, s32 *head, f32 *pov);   /* front.c: the MP roster entry */
extern void portGiveItemTo(int player, int id, int ammo);               /* port_netcmd.c */
extern int portLuaGameItemId(const char *upper);                        /* port_lua.c */

extern s32 get_ammo_type_for_weapon(ITEM_IDS weapon);
extern s32 check_cur_player_ammo_amount_in_inventory(AMMOTYPE type);
extern s32 get_max_ammo_for_type(s32 type);

#define ARENA_CHRNUM0   8000
#define INFO_N          4096  /* direct-mapped by chrnum (arena chrnums are consecutive, so a window of INFO_N never collides) */

#define KNIFE_RANGE_DEFAULT     4500.0f   /* A5: farthest knife throw */
#define KNIFE_COOLDOWN_DEFAULT  1.2f      /* A5: seconds between a thrower's throws (was ~2.5) */
#define SIGHT_DEFAULT           15000.0f  /* A5: chr->visionrange in world units (the Colosseum is 9000 across) */

/* per arena chr: its knife numbers and whether it has fired yet (first-shot range log) */
typedef struct { s32 chrnum; u8 enemy, knifer, fired, spectator, stockroute, headshot; f32 knifeRange, knifeCooldown, engage; } ArenaInfo;
static ArenaInfo sInfo[INFO_N];

static ArenaInfo *infoGet(s32 chrnum)
{
    ArenaInfo *a = &sInfo[chrnum & (INFO_N - 1)];

    return (a->chrnum == chrnum) ? a : NULL;
}

typedef struct { const char *name; s32 prop, item; } ArenaWeapon;

static const ArenaWeapon sWeapons[] = {
    { "ak47", PROP_CHRKALASH, ITEM_AK47 },           { "kf7", PROP_CHRKALASH, ITEM_AK47 },
    { "shotgun", PROP_CHRSHOTGUN, ITEM_SHOTGUN },
    { "pp7", PROP_CHRWPPK, ITEM_WPPK },              { "wppk", PROP_CHRWPPK, ITEM_WPPK },
    { "pp7sil", PROP_CHRWPPKSIL, ITEM_WPPKSIL },     { "wppksil", PROP_CHRWPPKSIL, ITEM_WPPKSIL },
    { "d5k", PROP_CHRMP5K, ITEM_MP5K },              { "mp5k", PROP_CHRMP5K, ITEM_MP5K },
    { "d5ksil", PROP_CHRMP5KSIL, ITEM_MP5KSIL },     { "mp5ksil", PROP_CHRMP5KSIL, ITEM_MP5KSIL },
    { "ar33", PROP_CHRM16, ITEM_M16 },               { "m16", PROP_CHRM16, ITEM_M16 },
    { "zmg", PROP_CHRUZI, ITEM_UZI },                { "uzi", PROP_CHRUZI, ITEM_UZI },
    { "dd44", PROP_CHRTT33, ITEM_TT33 },             { "tt33", PROP_CHRTT33, ITEM_TT33 },
    { "klobb", PROP_CHRSKORPION, ITEM_SKORPION },    { "skorpion", PROP_CHRSKORPION, ITEM_SKORPION },
    { "phantom", PROP_CHRSPECTRE, ITEM_SPECTRE },    { "spectre", PROP_CHRSPECTRE, ITEM_SPECTRE },
    { "rcp90", PROP_CHRFNP90, ITEM_FNP90 },          { "fnp90", PROP_CHRFNP90, ITEM_FNP90 },
    { "cougar", PROP_CHRRUGER, ITEM_RUGER },         { "ruger", PROP_CHRRUGER, ITEM_RUGER },
    { "autoshot", PROP_CHRAUTOSHOT, ITEM_AUTOSHOT },
    { "goldengun", PROP_CHRGOLDEN, ITEM_GOLDENGUN }, { "golden", PROP_CHRGOLDEN, ITEM_GOLDENGUN },
    { "sniper", PROP_CHRSNIPERRIFLE, ITEM_SNIPERRIFLE }, { "sniperrifle", PROP_CHRSNIPERRIFLE, ITEM_SNIPERRIFLE },
    { "rocket", PROP_CHRROCKETLAUNCH, ITEM_ROCKETLAUNCH }, { "rocketlauncher", PROP_CHRROCKETLAUNCH, ITEM_ROCKETLAUNCH },
    { "grenadelauncher", PROP_CHRGRENADELAUNCH, ITEM_GRENADELAUNCH },
    { "knife", PROP_CHRKNIFE, ITEM_KNIFE },
    { "laser", PROP_CHRLASER, ITEM_LASER },
};

/* Explicit bodies / heads for spawn.chr{body=, head=}: the stock BODIES / HEADS enums (a body index is also the
 * c_item_entries index the level setups use: Bunker 2's alarm reinforcements are bodies 21 and 38, the Caverns guards 4). */
typedef struct { const char *name; s32 id; } ArenaNamed;
#define NB(n) { #n, BODY_##n }
#define NH(n) { #n, HEAD_##n }
static const ArenaNamed sBodies[] = {
    NB(Jungle_Commando), NB(St_Petersburg_Guard), NB(Russian_Soldier), NB(Russian_Infantry), NB(Janus_Special_Forces),
    NB(Boris), NB(Ourumov), NB(Trevelyan_Janus), NB(Trevelyan_006), NB(Valentin_), NB(Xenia), NB(Baron_Samedi), NB(Jaws),
    NB(Mayday), NB(Oddjob), NB(Natalya_Skirt), NB(Janus_Marine), NB(Russian_Commandant), NB(Siberian_Guard_1_Mishkin),
    NB(Naval_Officer), NB(Siberian_Special_Forces), NB(Special_Operations_Uniform), NB(Formal_Wear), NB(Jungle_Fatigues),
    NB(Parka), NB(Rosika), NB(Scientist_2_Female), NB(Civilian_1_Female), NB(Civilian_4), NB(Civilian_2), NB(Civilian_3),
    NB(Scientist_1_Male), NB(Helicopter_Pilot), NB(Siberian_Guard_2), NB(Arctic_Commando), NB(Moonraker_Elite_1_Male),
    NB(Moonraker_Elite_2_Female),
    { "Spetsnaz", BODY_Siberian_Special_Forces }, { "Arctic", BODY_Arctic_Commando }, { "Janus_SF", BODY_Janus_Special_Forces },
    { NULL, 0 }
};
static const ArenaNamed sHeads[] = {
    NH(Male_Karl), NH(Male_Alan), NH(Male_Pete), NH(Male_Martin), NH(Male_Mark), NH(Male_Duncan), NH(Male_Shaun), NH(Male_Dwayne),
    NH(Male_B), NH(Male_Dave_Dr_Doak), NH(Male_Grant), NH(Male_Des), NH(Male_Chris), NH(Male_Lee), NH(Male_Neil), NH(Male_Jim),
    NH(Male_Robin), NH(Male_Steve_H), NH(Male_Terrorist), NH(Male_Biker), NH(Male_Graeme), NH(Male_Steve_Ellis), NH(Male_Joel),
    NH(Male_Scott), NH(Male_Joe_Altered), NH(Male_Ken), NH(Male_Joe), NH(Male_Mishkin), NH(Female_Sally), NH(Female_Marion_Rosika),
    NH(Female_Mandy), NH(Female_Vivien),
    { NULL, 0 }
};

const char *portLuaGameBodyName(int heads, int k)
{
    const ArenaNamed *t = heads ? sHeads : sBodies;
    int i;

    for (i = 0; i <= k; i++)
    {
        if (t[i].name == NULL)
        {
            return NULL;
        }
    }
    return t[k].name;
}

static s32 nameEq(const char *a, const char *b);

static s32 namedFind(const ArenaNamed *t, const char *name, s32 *out)
{
    s32 i;

    for (i = 0; t[i].name != NULL; i++)
    {
        if (nameEq(t[i].name, name))
        {
            *out = t[i].id;
            return TRUE;
        }
    }
    return FALSE;
}

extern s32 gPortSpawnExtraFlags;   /* port_spawn.c */

static s32 sNextNum = ARENA_CHRNUM0;

static char lc(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

/* Names compare case-insensitively with '_', '-' and ' ' ignored. */
static s32 nameEq(const char *a, const char *b)
{
    for (;;)
    {
        while (*a == '_' || *a == '-' || *a == ' ') a++;
        while (*b == '_' || *b == '-' || *b == ' ') b++;
        if (*a == '\0' || *b == '\0')
        {
            return *a == *b;
        }
        if (lc(*a) != lc(*b))
        {
            return FALSE;
        }
        a++;
        b++;
    }
}

static const ArenaWeapon *findWeapon(const char *name)
{
    s32 i;

    for (i = 0; i < (s32)(sizeof(sWeapons) / sizeof(sWeapons[0])); i++)
    {
        if (nameEq(sWeapons[i].name, name))
        {
            return &sWeapons[i];
        }
    }
    return NULL;
}

/* The sim's anchor Bond: the lowest index in play. */
static PropRecord *anchorProp(void)
{
    s32 p, n = portBondCount();

    for (p = 0; p < n; p++)
    {
        if (portBondInPlay(p) && g_playerPointers[p] != NULL)
        {
            return g_playerPointers[p]->prop;
        }
    }
    return NULL;
}

s32 portArenaChrKnifer(const ChrRecord *chr)
{
    ArenaInfo *a = (chr != NULL) ? infoGet((s32)chr->chrnum) : NULL;

    return a != NULL && a->knifer;
}

/* an enemy the arena mode spawned (not the crowd): the engine uses this for the direct-route and the first-shot log */
/* spawn.keep_bodies(on): dead arena enemies are not faded out and removed by the stock chrlvTickDead (90 ticks); the mode removes them */
static s32 sKeepBodies;

void portLuaGameKeepBodies(int on)
{
    sKeepBodies = on;
}

s32 portArenaKeepBody(const ChrRecord *chr)
{
    ArenaInfo *a = (sKeepBodies && chr != NULL) ? infoGet((s32)chr->chrnum) : NULL;

    return a != NULL && a->enemy;
}

s32 portArenaChrIsEnemy(const ChrRecord *chr)
{
    ArenaInfo *a = (chr != NULL) ? infoGet((s32)chr->chrnum) : NULL;

    return a != NULL && a->enemy;
}

/* a spectator, or an arena enemy that is dead (its body lies still): the engine skips its prop collision (chr.c) */
s32 portArenaChrDormant(const ChrRecord *chr)
{
    ArenaInfo *a;

    if (chr == NULL || chr->chrnum < ARENA_CHRNUM0 || !portModeActive())
    {
        return FALSE;
    }
    a = infoGet((s32)chr->chrnum);
    return a != NULL && (a->spectator || (a->enemy && chr->actiontype == ACT_DEAD));
}

/* chraction.c, on every hit that lands on a chr: remembers whether an arena enemy's last damaging hit was to the head (the crowd
 * cheers a headshot kill; spawn.headshot). Port-side bookkeeping only, read by the mode on chr_died. */
void portArenaNoteHitPart(const ChrRecord *chr, s32 head)
{
    ArenaInfo *a = (chr != NULL && chr->chrnum >= ARENA_CHRNUM0) ? infoGet((s32)chr->chrnum) : NULL;

    if (a != NULL && a->enemy)
    {
        a->headshot = head ? 1 : 0;
    }
}

int portLuaGameChrHeadshot(int chrnum)
{
    ArenaInfo *a = (chrnum >= ARENA_CHRNUM0) ? infoGet((s32)chrnum) : NULL;

    return a != NULL && a->headshot;
}

/* GE_ARENA_OLDAI=1: the drop A4 enemy AI (run to 9 m, then fight; waypoint routes only), for before/after measurements on the same binary */
static s32 legacyAI(void)
{
    static s32 v = -1;

    if (v < 0)
    {
        const char *e = getenv("GE_ARENA_OLDAI");

        v = (e != NULL && e[0] != '\0' && e[0] != '0') ? 1 : 0;
    }
    return v;
}

/* an arena enemy that takes the A5 straight route / follows Bond (chraction.c plot_course_for_actor, chrlvTickGoPos) */
s32 portArenaDirect(const ChrRecord *chr)
{
    ArenaInfo *a = (chr != NULL) ? infoGet((s32)chr->chrnum) : NULL;

    /* heist: an alarm raiser or a patroller keeps the stock routes (it must reach ITS pad, not walk to Bond); so does a guard walking over
     * to wake a knocked-out chr (port_heist.c) */
    extern int portHeistChrWaking(const ChrRecord *c);

    return !legacyAI() && portArenaChrIsEnemy(chr) && !(a != NULL && a->stockroute) && !portHeistChrWaking(chr);
}

/* range = farthest throw (units), cooldownTicks = ticks between throws (60 Hz); the arena defaults for a chr without an entry */
s32 portArenaKnifeParams(const ChrRecord *chr, f32 *range, f32 *cooldownTicks)
{
    ArenaInfo *a = (chr != NULL) ? infoGet((s32)chr->chrnum) : NULL;

    *range = (a != NULL && a->knifeRange > 0.0f) ? a->knifeRange : KNIFE_RANGE_DEFAULT;
    *cooldownTicks = 60.0f * ((a != NULL && a->knifeCooldown > 0.0f) ? a->knifeCooldown : KNIFE_COOLDOWN_DEFAULT);
    return a != NULL;
}

/* Called for every queued shot (chrlvTriggerFireWeapon): logs how far from Bond an arena enemy first opens fire. */
void portArenaNoteShot(ChrRecord *chr)
{
    ArenaInfo *a = infoGet((s32)chr->chrnum);
    PropRecord *bp;
    f32 dx, dz;

    if (a == NULL || !a->enemy || a->fired)
    {
        return;
    }
    bp = getCurrentPlayerProp();
    if (bp == NULL || chr->prop == NULL)
    {
        return;
    }
    a->fired = 1;
    dx = bp->pos.x - chr->prop->pos.x;
    dz = bp->pos.z - chr->prop->pos.z;
    portConLogf("ge: arena: FIRST SHOT chr %d at distance %.0f (engage range %.0f)\n", (int)chr->chrnum, sqrtf(dx * dx + dz * dz), a->engage);
}

/* Effective firing range class of a weapon: 0 = LONG (rifles, 60 m), 1 = MID (SMGs, 35 m), 2 = SHORT (pistols, 25 m), -1 = melee or unarmed */
static s32 rangeClass(const ArenaWeapon *w, f32 engage)
{
    if (engage > 0.0f)
    {
        return (engage >= 5000.0f) ? 0 : (engage >= 3000.0f ? 1 : 2);
    }
    if (w == NULL)
    {
        return -1;
    }
    switch (w->item)
    {
        case ITEM_AK47: case ITEM_M16: case ITEM_SNIPERRIFLE: case ITEM_ROCKETLAUNCH: case ITEM_GRENADELAUNCH: case ITEM_AUTOSHOT:
        case ITEM_GOLDENGUN: case ITEM_LASER:
            return 0;
        case ITEM_MP5K: case ITEM_MP5KSIL: case ITEM_UZI: case ITEM_SPECTRE: case ITEM_FNP90: case ITEM_SHOTGUN:
            return 1;
        case ITEM_KNIFE:
            return -1;
        default:   /* pistols, Klobb */
            return 2;
    }
}

static f32 rangeOf(s32 cls)
{
    return cls == 0 ? 6000.0f : (cls == 1 ? 3500.0f : (cls == 2 ? 2500.0f : 0.0f));
}

static s32 behaviourList(s32 b, s32 cls)
{
    if (legacyAI())
    {
        cls = -1;   /* the A4 lists: GAILIST_ARENA_CHASE for chase and rush */
    }
    switch (b)
    {   /* chase / rush use the A5 engage lists for an armed chr: fire from the weapon's effective range, advance between bursts.
         * (The older m_ArenaChase, which ran to 9 m before it fought, stays for melee and unarmed chrs.) */
        case PORTLUA_BEH_RUSH:
            return cls < 0 ? GAILIST_ARENA_CHASE : (cls == 0 ? GAILIST_ARENA_ENGAGE_LONG_RUSH : (cls == 1 ? GAILIST_ARENA_ENGAGE_MID_RUSH : GAILIST_ARENA_ENGAGE_SHORT_RUSH));
        case PORTLUA_BEH_HOLD:     return GAILIST_SIMPLE_GUARD;
        case PORTLUA_BEH_SNIPE:    return GAILIST_ARENA_SNIPE;   /* stock AIM_AT_BOND is aim-only: never fires */
        case PORTLUA_BEH_STANDARD: return GAILIST_STANDARD_GUARD;
        case PORTLUA_BEH_SPECTATOR: return GAILIST_COOP_BOT;   /* idles forever */
        case PORTLUA_BEH_CIVILIAN:  return GAILIST_COOP_BOT;   /* idles at its post; port_heist.c drives surrender and cowering */
        default:
            return cls < 0 ? GAILIST_ARENA_CHASE : (cls == 0 ? GAILIST_ARENA_ENGAGE_LONG_CHASE : (cls == 1 ? GAILIST_ARENA_ENGAGE_MID_CHASE : GAILIST_ARENA_ENGAGE_SHORT_CHASE));
    }
}

int portLuaGameSpawnChr(int pad, const PortLuaChrSpec *s)
{
    PortSpawnDesc d;
    coord3d pos;
    StandTile *stan = NULL;
    f32 angle = 0.0f;
    const ArenaWeapon *w = NULL;
    s32 body = BODY_Russian_Soldier, head = -1;
    ChrRecord *chr;
    PropRecord *bp;

    if (!portModeActive() || s == NULL)
    {
        return -1;
    }
    if (s->bodyname != NULL || s->body >= 0)
    {   /* explicit body (wins over skin); head: a name, an id, or random (-1; bodies with a built-in head ignore it) */
        if (s->bodyname != NULL)
        {
            if (!namedFind(sBodies, s->bodyname, &body))
            {
                return -4;
            }
        }
        else if (s->body >= BODIES_MAX || s->body >= HEAD_START)
        {
            return -4;
        }
        else
        {
            body = s->body;
        }
        head = -1;
        if (s->headname != NULL)
        {
            if (nameEq(s->headname, "none") || nameEq(s->headname, "random"))
            {
                head = -1;
            }
            else if (!namedFind(sHeads, s->headname, &head))
            {
                return -5;
            }
        }
        else if (s->head >= 0)
        {
            if (s->head < HEAD_START || s->head >= BODIES_MAX)
            {
                return -5;
            }
            head = s->head;
        }
    }
    else if (s->skin != NULL)
    {
        s32 k = portBotSkinFind(s->skin), pb, ph;
        f32 pov;

        if (k < 0 || !portMpCharBodyHead(portBotSkinMp(k), &pb, &ph, &pov))
        {
            return -2;
        }
        body = pb;
        head = ph;
    }
    if (s->weapon != NULL)
    {
        if (nameEq(s->weapon, "none") || nameEq(s->weapon, "unarmed"))
        {
            w = NULL;
        }
        else
        {
            w = findWeapon(s->weapon);
            if (w == NULL)
            {
                return -3;
            }
        }
    }
    else
    {
        w = findWeapon("ak47");
    }
    if (s->behaviour == PORTLUA_BEH_CIVILIAN)
    {
        w = NULL;   /* civilians are unarmed */
    }
    bp = anchorProp();
    if (s->hasoff && (s->offx != 0.0f || s->offz != 0.0f))
    {   /* heist job 6: offset from the pad: walk the stan from the pad's own tile to that point (a wall or a ledge in between refuses
         * it). The pad centre may be taken (portPadStan's clear-circle test would refuse); chrSpawnAtCoord tests the new spot itself. */
        extern int portHeistOffsetStan(coord3d *pos, StandTile **stan, float dx, float dz);
        extern int portHeistPadRaw(int pad, coord3d *pos, StandTile **stan, float *angle);

        if (bp == NULL || !portHeistPadRaw(pad, &pos, &stan, &angle) || !portHeistOffsetStan(&pos, &stan, s->offx, s->offz))
        {
            return -1;
        }
    }
    else if (bp == NULL || !portPadStan(pad, &pos, &stan, &angle))
    {
        return -1;
    }
    if (s->hasface)
    {
        angle = atan2f(s->fx - pos.x, s->fz - pos.z);
    }
    while (chrFindByLiteralId(sNextNum) != NULL)
    {
        sNextNum++;
    }
    memset(&d, 0, sizeof(d));
    d.body = body;
    d.head = head;
    d.nearprop = bp;
    d.angle = angle;
    d.ailistid = behaviourList(s->behaviour, rangeClass(w, s->engage > 0.0f ? s->engage : (s->knives ? 6000.0f : 0.0f)));
    if (s->alarmPad >= 0 && s->behaviour != PORTLUA_BEH_CIVILIAN)
    {   /* the stock alarm raiser: on sight / hearing it runs to padpreset1 and sounds the alarm, then runs at Bond */
        d.ailistid = GAILIST_SIMPLE_GUARD_ALARM_RAISER;
    }
    d.maxdamage = (s->health > 0.0f) ? 4.0f * s->health : 0.0f;
    d.chrnum = sNextNum;
    gPortSpawnExtraFlags = s->sunglasses ? 1 : 0;   /* SPAWN_SUNGLASSES */
    chr = portSpawnChrAt(&d, &pos, stan);
    gPortSpawnExtraFlags = 0;
    if (chr == NULL)
    {
        return -1;
    }
    sNextNum++;
    if (chr->model == NULL || chr->model->obj == NULL)
    {   /* never leave an armed chr that cannot be drawn */
        portConLogf("ge: arena: spawn REFUSED: chr %d (%s) has no model\n", (int)chr->chrnum, s->skin != NULL ? s->skin : "random");
        chr->hidden |= CHRHIDDEN_REMOVE;
        return -1;
    }
    /* RUN_TO_BOND (and the other sub-lists) END with AI_Return: with no return list set that reads a NULL list
     * and crashes the AI tick. Anything that returns goes back to the chase. */
    chr->aireturnlist = (s16)GAILIST_ARENA_CHASE;
    if (s->behaviour == PORTLUA_BEH_SPECTATOR)
    {   /* the crowd: decoration only. Invincible (stray fire and knives do nothing), not auto-aimed, no shadow (cheaper),
         * no collision box (CHRFLAG_00010000: every other chr's move test walks the whole crowd; nobody reaches the
         * stands), and in the hostage faction, which nobody targets. */
        chr->chrflags |= CHRFLAG_INVINCIBLE | CHRFLAG_NO_AUTOAIM | CHRFLAG_NO_SHADOW | CHRFLAG_00010000;
        portFactionSetChr(chr, PORT_FACTION_HOSTAGE);
    }
    if (s->alarmPad >= 0)
    {
        chr->padpreset1 = (s16)s->alarmPad;
    }
    if (s->hearing > 0.0f)
    {
        chr->hearingscale = s->hearing;
    }
    if (s->patrol >= 0)
    {   /* a stock patrol path (the setup's PathRecord ids); applied a few ticks after the spawn, as spawn.guard does */
        extern void portHeistPatrolLater(int chrnum, int path);

        portHeistPatrolLater((int)chr->chrnum, (int)s->patrol);
    }
    if (s->behaviour == PORTLUA_BEH_CIVILIAN)
    {   /* not hostile to anyone (guards never target it) but any shot that hits it hurts it; its death counts as a civilian casualty */
        chr->chrflags |= CHRFLAG_COUNT_DEATH_AS_CIVILIAN;
        portFactionSetChr(chr, PORT_FACTION_HOSTAGE);
        portHeistCivilianAdd((int)chr->chrnum);
    }
    else
    {   /* PORT: bank v2: a chr number is reused once its chr is removed; a guard on a removed civilian's number was still taken for a
         * civilian (port_heist.c isCivilian: no hold fire for him, no target for others) */
        extern void portHeistCivilianForget(int chrnum);

        portHeistCivilianForget((int)chr->chrnum);
    }
    if (s->accuracy >= 0)
    {
        chr->accuracyrating = (s8)(s->accuracy > 100 ? 100 : s->accuracy);
    }
    if (s->speed >= 0)
    {
        chr->speedrating = (s8)(s->speed > 100 ? 100 : s->speed);
    }
    if (w != NULL)
    {
        chrGiveWeapon(chr, w->prop, (ITEM_IDS)w->item, 0);
    }
    {   /* the per-chr table: knife numbers, engage range, first-shot flag. The crowd is not an enemy. */
        ArenaInfo *a = &sInfo[chr->chrnum & (INFO_N - 1)];

        memset(a, 0, sizeof(*a));
        a->chrnum = (s32)chr->chrnum;
        a->enemy = (s->behaviour != PORTLUA_BEH_SPECTATOR && s->behaviour != PORTLUA_BEH_CIVILIAN);
        a->spectator = (s->behaviour == PORTLUA_BEH_SPECTATOR);
        a->stockroute = (s->alarmPad >= 0 || s->patrol >= 0) ? 1 : 0;
        a->knifer = s->knives ? 1 : 0;
        a->knifeRange = s->knifeRange > 0.0f ? s->knifeRange : KNIFE_RANGE_DEFAULT;
        a->knifeCooldown = s->knifeCooldown > 0.0f ? s->knifeCooldown : KNIFE_COOLDOWN_DEFAULT;
        a->engage = s->engage > 0.0f ? s->engage : rangeOf(rangeClass(w, s->knives ? 6000.0f : 0.0f));
    }
    /* sight: chr->visionrange is in 100-unit steps (chrCheckTargetInSight, IFISeeBond); the init default is 250 (25000 units), the
     * stock Dam guards have 100. The A5 engage lists use IFICouldSeeBond (a stan ray, no range) so this only matters for IFISeeBond. */
    chr->visionrange = (s->sight > 0.0f ? s->sight : SIGHT_DEFAULT) / 100.0f;
    if (s->armour > 0.0f)
    {   /* the stock armour: chr->damage below zero is armour (hits eat it first, and an armoured chr is not staggered,
         * chraction.c chrlvShotDamage); same units as maxdamage, scaled by the same 007 health modifier */
        f32 base = (s->health > 0.0f) ? 4.0f * s->health : 4.0f;

        chr->damage = -(chr->maxdamage / base) * 4.0f * s->armour;
    }
    if (s->animspeed > 0.0f && s->animspeed != 1.0f)
    {   /* the Fast Animation cheat, for this chr only: model play speed. It scales every animation (walk/run, aim, fire,
         * the knife throw) and the stock movement code multiplies its speed and acceleration by it; no per-tick step stays
         * large enough to skip a stan tile (the cheat runs everyone at 4x). Clamped 0.5 .. 3 here. */
        f32 r = s->animspeed < 0.5f ? 0.5f : (s->animspeed > 3.0f ? 3.0f : s->animspeed);

        modelSetAnimPlaySpeed(chr->model, getAnimationRate() * r, 0.0f);
    }
    portConLogf("ge: arena: spawned chr %d at pad %d (%s, body %d head %d, %s, hp x%.1f, armour x%.1f (damage %.1f/%.1f), anim %.2f, beh %d%s) model ok, %d records, %d chr slots free\n",
                (int)chr->chrnum, (int)pad,
                s->skin != NULL ? s->skin : "random", (int)body, (int)head, w != NULL ? w->name : "unarmed",
                s->health > 0.0f ? (double)s->health : 1.0, (double)s->armour, (double)chr->damage, (double)chr->maxdamage,
                s->animspeed > 0.0f ? (double)s->animspeed : 1.0, (int)s->behaviour, s->knives ? ", knives" : "",
                (int)chr->model->rwdatalen, (int)chrGetNumFree());
    return (int)chr->chrnum;
}

int portLuaGameChrRemove(int chrnum)
{
    ChrRecord *c;

    if (!portModeActive())
    {
        return 0;
    }
    c = chrFindByLiteralId(chrnum);
    if (c == NULL || c->prop == NULL)
    {
        return 0;
    }
    c->hidden |= CHRHIDDEN_REMOVE;
    return 1;
}

/* weaponCreate (propobj.c) asks before it recycles a loose weapon's slot: not one within 350 units of a Bond, in a game mode */
s32 portArenaWeaponNearBond(const WeaponObjRecord *w)
{
    s32 p, n;

    if (!portModeActive() || w->prop == NULL)
    {
        return FALSE;
    }
    n = portBondCount();
    for (p = 0; p < n; p++)
    {
        if (g_playerPointers[p] != NULL && g_playerPointers[p]->prop != NULL)
        {
            f32 dx = g_playerPointers[p]->prop->pos.x - w->prop->pos.x, dz = g_playerPointers[p]->prop->pos.z - w->prop->pos.z;

            if (dx * dx + dz * dz < 350.0f * 350.0f)
            {
                return TRUE;
            }
        }
    }
    return FALSE;
}

/* test helper: a normal lethal hit (death animation, weapon drops, events) from Bond's position; head = count it as a headshot */
int portLuaGameChrKill(int chrnum, int head)
{
    ChrRecord *c = chrFindByLiteralId(chrnum);
    PropRecord *bp = anchorProp();

    if (!portModeActive() || c == NULL || c->prop == NULL || c->model == NULL || bp == NULL || chrIsDead(c))
    {
        return 0;
    }
    chrlvExplosionDamage(c, &bp->pos, c->maxdamage + 1.0f + (c->damage < 0.0f ? -c->damage : 0.0f), TRUE);
    if (head)
    {
        portArenaNoteHitPart(c, 1);
    }
    return 1;
}

/* ---- weapon pool (G_WeaponSlots[30], shared by held and dropped weapons) and prop pool (600) ---- */

static s32 slotIsLoose(const WeaponObjRecord *w)
{   /* on the floor: a prop with no parent, not in flight, not a respawning level pickup */
    return w->prop != NULL && w->prop->parent == NULL && w->prop->type == PROP_TYPE_WEAPON &&
           (w->runtime_bitflags & RUNTIMEBITFLAG_HASPROJECTILE) == 0 && (w->state & PROPSTATE_RESPAWN) == 0 &&
           (w->runtime_bitflags & RUNTIMEBITFLAG_REMOVE) == 0;
}

int portLuaGamePools(int *loose, int *held, int *freeSlots, int *freeProps)
{
    s32 i, nl = 0, nh = 0, nf = 0, np = 0;
    PropRecord *p;

    if (!portModeActive())
    {
        return 0;
    }
    for (i = 0; i < MAX_WEAPON_SLOTS; i++)
    {
        const WeaponObjRecord *w = &g_WeaponSlots[i];

        if (w->prop == NULL)
        {
            nf++;
        }
        else if (w->prop->parent != NULL)
        {
            nh++;
        }
        else if (slotIsLoose(w))
        {
            nl++;
        }
    }
    for (p = g_FreeProps; p != NULL && np < MAX_PROPS + 8; p = p->prev)
    {
        np++;
    }
    *loose = nl;
    *held = nh;
    *freeSlots = nf;
    *freeProps = np;
    return 1;
}

extern TICKOP propPickupByPlayer(PropRecord *prop, bool showstring);   /* propobj.c: the real pickup */
extern void propExecuteTickOperation(PropRecord *prop, TICKOP op);

/* test helper (bond.take_drop): Bond picks up the nearest loose weapon within `radius` through the real pickup path. Returns the item id
 * (0 = none near) and the ammo of its type before and after, so a test can see that a drop brings its ammo. */
int portLuaGameTakeDrop(int player, float radius, int *ammoBefore, int *ammoAfter)
{
    s32 i, best = -1, saved, item, type;
    f32 bd = radius * radius;
    PropRecord *bp;

    if (!portModeActive() || player < 0 || player >= (int)portBondCount() || !portBondInPlay(player) || g_playerPointers[player]->prop == NULL)
    {
        return 0;
    }
    bp = g_playerPointers[player]->prop;
    for (i = 0; i < MAX_WEAPON_SLOTS; i++)
    {
        if (slotIsLoose(&g_WeaponSlots[i]))
        {
            f32 dx = bp->pos.x - g_WeaponSlots[i].prop->pos.x, dz = bp->pos.z - g_WeaponSlots[i].prop->pos.z;

            if (dx * dx + dz * dz < bd)
            {
                bd = dx * dx + dz * dz;
                best = i;
            }
        }
    }
    if (best < 0)
    {
        return 0;
    }
    item = (s32)g_WeaponSlots[best].weaponnum;
    type = get_ammo_type_for_weapon((ITEM_IDS)item);
    saved = get_cur_playernum();
    set_cur_player(player);
    *ammoBefore = type > 0 ? check_cur_player_ammo_amount_in_inventory((AMMOTYPE)type) : 0;
    {
        PropRecord *wp = g_WeaponSlots[best].prop;   /* a local: the pickup can clear the slot's prop before the call below reads it */
        TICKOP op = propPickupByPlayer(wp, TRUE);

        propExecuteTickOperation(wp, op);
    }
    *ammoAfter = type > 0 ? check_cur_player_ammo_amount_in_inventory((AMMOTYPE)type) : 0;
    set_cur_player(saved);
    return item;
}

int portLuaGameTrimDrops(int maxDrops, float keepRadius)
{
    s32 order[MAX_WEAPON_SLOTS], n = 0, i, freed = 0;

    if (!portModeActive() || maxDrops < 0)
    {
        return 0;
    }
    /* oldest first: weaponCreate hands out slots round-robin from g_NextWeaponSlot, so the slot after the pointer was written longest ago */
    for (i = 0; i < MAX_WEAPON_SLOTS; i++)
    {
        s32 k = (g_NextWeaponSlot + i) % MAX_WEAPON_SLOTS;

        if (slotIsLoose(&g_WeaponSlots[k]) && !(g_WeaponSlots[k].timer >= 0x4000))   /* not an A6 centre pickup */
        {
            order[n++] = k;
        }
    }
    for (i = 0; i < n && n - freed > maxDrops; i++)
    {
        WeaponObjRecord *w = &g_WeaponSlots[order[i]];
        s32 p, near = FALSE;

        for (p = 0; p < portBondCount() && !near; p++)
        {
            if (g_playerPointers[p] != NULL && g_playerPointers[p]->prop != NULL)
            {   /* the one Bond is about to pick up stays */
                f32 dx = g_playerPointers[p]->prop->pos.x - w->prop->pos.x, dz = g_playerPointers[p]->prop->pos.z - w->prop->pos.z;

                near = (dx * dx + dz * dz) < keepRadius * keepRadius;
            }
        }
        if (!near)
        {   /* as weaponCreate recycles a slot: a REMOVE flag left the object ticking with a freed model (objTick -> getinstsize) */
            objFreePermanently((ObjectRecord *)w, TRUE);
            freed++;
        }
    }
    if (freed > 0)
    {
        portConLogf("ge: arena: drop cap: freed %d of %d loose weapons (max %d)\n", (int)freed, (int)n, maxDrops);
    }
    return (int)freed;
}

extern void portTeleportBondToPad(PropRecord *bp, s32 pad, f32 offx, f32 offz);   /* port_hostage.c */

int portLuaGameBondWarp(int player, int pad, float dx, float dz)
{
    s32 saved;

    if (!portModeActive() || player < 0 || player >= (int)portBondCount() || !portBondInPlay(player) || pad < 0)
    {
        return 0;
    }
    saved = get_cur_playernum();
    set_cur_player(player);
    portTeleportBondToPad(g_playerPointers[player]->prop, pad, dx, dz);
    set_cur_player(saved);
    return 1;
}

/* bench.look(player, yaw, pitch): the benchmark's camera sweep (degrees, set absolutely every tick) */
int portLuaGameBondLook(int player, float yaw, float pitch)
{
    if (!portModeActive() || player < 0 || player >= (int)portBondCount() || !portBondInPlay(player))
    {
        return 0;
    }
    g_playerPointers[player]->vv_theta = yaw;
    g_playerPointers[player]->vv_verta = pitch;
    return 1;
}

/* bench.sweep(rate, pitch): turn Bond's view at rate degrees a second (0 = hold) and ease the pitch to `pitch`, every game tick (smooth,
 * unlike the 4 Hz Lua tick); bench.sweep_off() stops it */
static s32 sSweepOn;
static f32 sSweepRate, sSweepYaw, sSweepPitch, sSweepPitchNow;

void portBenchSweep(int on, float rate, float pitch)
{
    if (on && !sSweepOn && portModeActive() && portBondCount() > 0 && g_playerPointers[0] != NULL)
    {
        sSweepYaw = g_playerPointers[0]->vv_theta;
        sSweepPitchNow = g_playerPointers[0]->vv_verta;
    }
    sSweepOn = on;
    sSweepRate = rate;
    sSweepPitch = pitch;
}

void portBenchSweepTick(int clockTicks)
{
    f32 dt = (f32)clockTicks / 60.0f, step = 40.0f * dt;

    if (!sSweepOn || !portModeActive() || portBondCount() == 0 || !portBondInPlay(0))
    {
        return;
    }
    sSweepYaw += sSweepRate * dt;
    while (sSweepYaw >= 360.0f) sSweepYaw -= 360.0f;
    while (sSweepYaw < 0.0f) sSweepYaw += 360.0f;
    if (sSweepPitchNow < sSweepPitch - step) sSweepPitchNow += step;
    else if (sSweepPitchNow > sSweepPitch + step) sSweepPitchNow -= step;
    else sSweepPitchNow = sSweepPitch;
    portLuaGameBondLook(0, sSweepYaw, sSweepPitchNow);
}

extern s32 portModelHitEntriesLen(void);   /* initunk_005450.c */

/* bench.pools(): chr slots total / free, props total / free, hit entries total / free / peak used, stage mempool bytes left, display list peak / cap, vertex pool peak / cap
 * (bytes, since bench.begin), converted-model heap used */
int portLuaGameBenchPools(int *out)
{
    extern void portModelHitStats(s32 *used, s32 *peak, s32 reset);   /* objecthandler.c */
    extern void portDynPeaks(s32 *gfx, s32 *gfxCap, s32 *vtx, s32 *vtxCap, s32 reset);
    extern unsigned int portSimHeapUsed(void);
    s32 nf = 0, total = portModelHitEntriesLen(), hitUsed, hitPeak;
    PropRecord *p;

    if (!portModeActive())
    {
        return 0;
    }
    for (p = g_FreeProps; p != NULL && nf < MAX_PROPS + 8; p = p->prev)
    {
        nf++;
    }
    portModelHitStats(&hitUsed, &hitPeak, 0);
    out[0] = g_NumChrSlots;
    out[1] = chrGetNumFree();
    out[2] = MAX_PROPS;
    out[3] = nf;
    out[4] = total;
    out[5] = total - hitUsed;
    out[6] = hitPeak;
    out[7] = mempGetBankSizeLeft(MEMPOOL_STAGE);
    portDynPeaks(&out[8], &out[9], &out[10], &out[11], 0);
    out[12] = (s32)portSimHeapUsed();
    return 1;
}

int portLuaGameChrAlive(int chrnum)
{
    ChrRecord *c;

    if (!portModeActive())
    {
        return 0;
    }
    c = chrFindByLiteralId(chrnum);
    return (c != NULL && c->prop != NULL && c->model != NULL && !chrIsDead(c)) ? 1 : 0;
}

/* Named animations for the crowd: the stock idle fidgets (with their stock end frames) and some reactions. All play to the end
 * and return to the idle pose (ANIM_IDLE_POSE_WHEN_COMPLETE), as the stock idle-animation list does. start (left out = 0) is the first
 * frame played. */
static const struct { const char *name; s32 anim, end; u8 flags; s32 start; } sAnims[] = {
    { "yawn", ANIM_yawning, 193, ANIM_IDLE_POSE_WHEN_COMPLETE | ANIM_PLAY_SFX },
    { "flies", ANIM_swatting_flies, 294, ANIM_IDLE_POSE_WHEN_COMPLETE | ANIM_PLAY_SFX },
    { "scratch_leg", ANIM_scratching_leg, 183, ANIM_IDLE_POSE_WHEN_COMPLETE | ANIM_PLAY_SFX },
    { "scratch_butt", ANIM_scratching_butt, 123, ANIM_IDLE_POSE_WHEN_COMPLETE | ANIM_PLAY_SFX },
    { "adjust", ANIM_adjusting_crotch, 56, ANIM_IDLE_POSE_WHEN_COMPLETE | ANIM_PLAY_SFX },
    { "sneeze", ANIM_sneeze, 137, ANIM_IDLE_POSE_WHEN_COMPLETE | ANIM_PLAY_SFX },
    { "chat", ANIM_conversation, -1, ANIM_IDLE_POSE_WHEN_COMPLETE },
    { "listen", ANIM_conversation_listener, -1, ANIM_IDLE_POSE_WHEN_COMPLETE },
    { "cough", ANIM_coughing_standing, -1, ANIM_IDLE_POSE_WHEN_COMPLETE },
    { "laugh", ANIM_laughing_in_disbelief, -1, ANIM_IDLE_POSE_WHEN_COMPLETE },
    { "startled", ANIM_startled_and_looking_around, -1, ANIM_IDLE_POSE_WHEN_COMPLETE },
    { "surrender", ANIM_surrendering_unarmed, -1, ANIM_IDLE_POSE_WHEN_COMPLETE },
    { "dance", ANIM_dancing, -1, ANIM_IDLE_POSE_WHEN_COMPLETE },
    { "look", ANIM_look_around, -1, ANIM_IDLE_POSE_WHEN_COMPLETE },
    /* crowd boos / jeers: fists raised in a fighting stance (the MP character-select taunt, reads as shaking fists at the pit),
     * and arms folded in disapproval (the end of the guard's holster-and-cross-arms; unarmed, the holster is an empty hand).
     * fists: the whole animation is 448 frames (~15 s) and starts with a weapon drop; frames 248..360 (the stance, where the
     * character select starts it) are about 4 s */
    { "fists", ANIM_drop_weapon_and_show_fight_stance, 360, ANIM_IDLE_POSE_WHEN_COMPLETE, 248 },
    { "cross_arms", ANIM_holster_one_handed_weapon_and_cross_arms, -1, ANIM_IDLE_POSE_WHEN_COMPLETE },
};

/* an idle chr: standing, or a one-shot animation that has run out (it holds the idle pose); the fidget gate of spawn.anim */
static s32 chrAnimIdle(ChrRecord *c)
{
    return c->actiontype == ACT_STAND ||
           (c->actiontype == ACT_ANIM && modelGetAnimFrame(c->model) >= modelGetAnimEndFrame(c->model));
}

int portLuaGameChrIdle(int chrnum)
{
    ChrRecord *c;

    if (!portModeActive())
    {
        return -1;
    }
    c = chrFindByLiteralId(chrnum);
    if (c == NULL || c->prop == NULL || c->model == NULL || chrIsDead(c))
    {
        return -1;
    }
    return chrAnimIdle(c) ? 1 : 0;
}

extern void chrlvPerformAnimationForActor(ChrRecord *self, s32 animID, s32 startframe, s32 endframe, u8 bitfield, s32 interpol_time60);

int portLuaGameChrAnim(int chrnum, const char *name, int force)
{
    ChrRecord *c;
    s32 i;

    if (!portModeActive() || name == NULL)
    {
        return 0;
    }
    for (i = 0; i < (s32)(sizeof(sAnims) / sizeof(sAnims[0])); i++)
    {
        if (nameEq(sAnims[i].name, name))
        {
            break;
        }
    }
    if (i == (s32)(sizeof(sAnims) / sizeof(sAnims[0])))
    {
        return -1;
    }
    c = chrFindByLiteralId(chrnum);
    if (c == NULL || c->prop == NULL || c->model == NULL || chrIsDead(c))
    {
        return 0;
    }
    if (!force)
    {   /* a fidget only while the chr is idle: standing, or an earlier animation that has run out */
        if (!chrAnimIdle(c))
        {
            return 0;
        }
    }
    chrlvPerformAnimationForActor(c, sAnims[i].anim, sAnims[i].start, sAnims[i].end, sAnims[i].flags, ANIM_DEFAULT_INTERPOLATION);
    return 1;
}

int portLuaGameChrWhere(int chrnum, float *x, float *z, int *act)
{
    ChrRecord *c = chrFindByLiteralId(chrnum);

    if (!portModeActive() || c == NULL || c->prop == NULL)
    {
        return 0;
    }
    *x = c->prop->pos.x;
    *z = c->prop->pos.z;
    *act = (int)c->actiontype;
    return 1;
}

int portLuaGameChrFree(void)
{
    return portModeActive() ? (int)chrGetNumFree() : 0;
}

int portLuaGameSkinCount(void)
{
    return portBotSkinCount();
}

const char *portLuaGameSkinName(int k)
{
    return portBotSkinName(k);
}

int portLuaGameGive(int player, const char *itemUpper, int ammo)
{
    int id;

    if (!portModeActive() || player < 0 || player >= (int)portBondCount() || !portBondInPlay(player))
    {
        return 0;
    }
    id = portLuaGameItemId(itemUpper);
    if (id < 0)
    {
        return 0;
    }
    portGiveItemTo(player, id, ammo);
    return 1;
}


/* give_ammo (drop A5): rounds for the guns the Bond already carries (+frac of each ammo type's maximum), no gun. Returns the types topped up. */
int portLuaGameGiveAmmo(int player, float frac)
{
    s32 saved, it, type, n = 0;
    u8 mark[AMMOTYPE_MAX];

    if (!portModeActive() || player < 0 || player >= (int)portBondCount() || !portBondInPlay(player))
    {
        return 0;
    }
    memset(mark, 0, sizeof(mark));
    saved = get_cur_playernum();
    set_cur_player(player);
    for (it = 1; it < 0x60; it++)
    {
        type = get_ammo_type_for_weapon((ITEM_IDS)it);
        if (type > 0 && type <= AMMOTYPE_GLOBAL_MAX && type != AMMO_KNIFE && bondinvItemAvailable((ITEM_IDS)it))
        {
            mark[type] = 1;
        }
    }
    for (type = 1; type <= AMMOTYPE_GLOBAL_MAX; type++)
    {
        if (mark[type])
        {
            s32 cur = check_cur_player_ammo_amount_in_inventory((AMMOTYPE)type);
            s32 mx = get_max_ammo_for_type(type);
            s32 add = (s32)((f32)mx * frac);

            if (cur + add > mx)
            {
                add = mx - cur;
            }
            if (add > 0)
            {
                give_cur_player_ammo(type, cur + add);
                n++;
            }
        }
    }
    set_cur_player(saved);
    return n;
}

static f32 clamp01(f32 v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

void portLuaGameHeal(int player, float health, float armour)
{
    struct player *pl;

    if (!portModeActive() || player < 0 || player >= (int)portBondCount() || !portBondInPlay(player))
    {
        return;
    }
    pl = g_playerPointers[player];
    pl->bondhealth = clamp01(pl->bondhealth + health);
    pl->bondarmour = clamp01(pl->bondarmour + armour);
}

int portLuaGameBondVitals(int player, float *health, float *armour)
{
    struct player *pl;

    if (!portModeActive() || player < 0 || player >= (int)portBondCount() || g_playerPointers[player] == NULL)
    {
        return 0;
    }
    pl = g_playerPointers[player];
    *health = pl->bondhealth;
    *armour = pl->bondarmour;
    return 1;
}

/* ---- world pickups (drop A6): spawn.item / item_alive / item_remove ------------------------------------------------------------
 * A weapon pickup is made the way a co-op death drop is (port_coopdrop.c): create_new_item_instance_of_model for the world model
 * of the item, then gunInitProjectileObject with no owner puts it on the floor at the pad; Bond collects it by walking over it
 * (objTickPlayer -> propPickupByPlayer: within 100 units, +-200 high, a clear stan line, not looking steeply down). The weapon's
 * `timer` field is inert for guns, so it carries a marker (ITEM_MARK + serial): weaponCreate only recycles a slot whose timer is <= 0,
 * so a marked pickup is never recycled by the drop recycler, and the marker doubles as the handle's identity check. Armour has no
 * pool: a small array of static BodyArmourRecords is filled like a setup object and placed by domakedefaultobj on the pad. */
extern void domakedefaultobj(s32 stageId, ObjectRecord *obj, s32 cmdindex);
extern ObjectRecord *create_new_item_instance_of_model(PROP modelnum, s32 weaponid);
extern PROP getPropForHeldItem(ITEM_IDS arg0);
extern void gunInitProjectileObject(ObjectRecord *obj, coord3d *pos, StandTile *stan, Mtxf *matrix, coord3d *velocity, Mtxf *arg5, PropRecord *owner);
extern f32 bondviewYPositionRelated(struct StandTile *arg0, f32 arg1, f32 arg2);
extern void matrix_4x4_set_identity(Mtxf *m);

#define ITEM_N     12
#define ITEM_MARK  0x4000   /* WeaponObjRecord.timer is an s16: marker 0x4000 + a 14-bit serial */

typedef struct { u8 used, armour; s32 serial; ObjectRecord *obj; PropRecord *prop; s32 ammo, item; } ArenaItem;
static ArenaItem sItems[ITEM_N];
/* Records are NOT tied to the sItems slot: spawn.item_remove only flags the old object REMOVE, and the object tick frees it
 * later, so a spawn in the same tick (the "replaced" centre drop) that wiped the same record left that old prop ticking with
 * model == NULL (objTick -> getinstsize crash, found in a shared build). Each spawn takes the next record round-robin; REC_N spawns
 * of one kind are far more than can be pending removal at once. */
#define REC_N      64
static BodyArmourRecord sArmourRecs[REC_N];
static MultiAmmoCrateRecord sAmmoRecs[REC_N];
static s32 sArmourNext, sAmmoNext;
extern s32 get_ammo_type_for_weapon(ITEM_IDS weapon);
static s32 sItemSerial;

static s32 itemMarked(const WeaponObjRecord *w)
{   /* a drop-A6 pickup (not recyclable, not trimmed) */
    return w->timer >= ITEM_MARK;
}

/* port hook for ammo_collected_from_weapon (propobj.c): the rounds an arena pickup holds, -1 = the stock amount */
s32 portArenaItemAmmo(const WeaponObjRecord *w)
{
    s32 i;

    for (i = 0; i < ITEM_N; i++)
    {
        if (sItems[i].used && !sItems[i].armour && sItems[i].obj == (const ObjectRecord *)w && w->timer == ITEM_MARK + sItems[i].serial)
        {
            return sItems[i].ammo;
        }
    }
    return -1;
}

static s32 itemAlive(s32 handle)
{
    s32 i = handle & 15;
    ArenaItem *it;

    if (handle <= 0 || i >= ITEM_N)
    {
        return 0;
    }
    it = &sItems[i];
    if (!it->used || it->serial != (handle >> 4) || it->obj == NULL || it->obj->prop == NULL || it->obj->prop != it->prop)
    {
        return 0;
    }
    if (it->obj->runtime_bitflags & RUNTIMEBITFLAG_REMOVE)
    {
        return 0;
    }
    if (!it->armour && ((WeaponObjRecord *)it->obj)->timer != ITEM_MARK + it->serial)
    {
        return 0;   /* the slot was reused */
    }
    return it->prop->parent == NULL;   /* a picked-up weapon is parented to Bond for an instant */
}

/* spawn.item: 0 failed (no room / no model), -1 unknown item, -2 bad pad, else a handle (> 0).
 * `what`: a weapon name of spawn.weapons() or "armour"; ammo = rounds the pickup gives (< 0: the stock amount); amount = armour 0..1 */
int portLuaGameItemSpawn(int pad, const char *what, int ammo, float amount, int ncrate, const char **cnames, const int *crounds)
{
    s32 i, slot = -1, armour, box;
    const ArenaWeapon *w = NULL;
    coord3d pos;
    StandTile *tile = NULL;
    f32 ang = 0.0f, fl;

    if (!portModeActive())
    {
        return 0;
    }
    armour = nameEq(what, "armour") || nameEq(what, "armor") || nameEq(what, "body_armour") || nameEq(what, "bodyarmour");
    box = nameEq(what, "ammo") || nameEq(what, "ammobox");
    if (!armour && !box)
    {
        w = findWeapon(what);
        if (w == NULL || (s32)getPropForHeldItem((ITEM_IDS)w->item) < 0)
        {
            return -1;
        }
    }
    if (!portPadStan(pad, &pos, &tile, &ang) || tile == NULL)
    {
        return -2;
    }
    for (i = 0; i < ITEM_N; i++)
    {
        if (sItems[i].used && !itemAlive((sItems[i].serial << 4) | i))
        {
            sItems[i].used = 0;   /* collected, removed or gone: the slot is free again */
        }
        if (!sItems[i].used && slot < 0)
        {
            slot = i;
        }
    }
    if (slot < 0)
    {
        return 0;
    }
    fl = bondviewYPositionRelated(tile, pos.x, pos.z);
    sItemSerial = (sItemSerial % 0x3FFF) + 1;
    if (box)
    {   /* the stock MP ammo box (setup PROPDEF_AMMO: extrascale 256, crate model 5): one slot per ammo type, slot = ammotype - 1 */
        MultiAmmoCrateRecord *r = &sAmmoRecs[sAmmoNext];
        s32 k, n = 0;

        sAmmoNext = (sAmmoNext + 1) % REC_N;
        memset(r, 0, sizeof(*r));
        r->extrascale = 256;
        r->type = PROPDEF_AMMO;
        r->obj = 5;
        r->pad = pad;
        r->flags = 1;
        *(s32 *)&r->damage = 1000 << 16;
        for (k = 0; k < AMMOTYPE_GLOBAL_MAX; k++)
        {
            r->slots[k].modelnum = 0xffff;
            r->slots[k].quantity = 0;
        }
        for (k = 0; k < ncrate; k++)
        {
            const ArenaWeapon *cw = findWeapon(cnames[k]);
            s32 at = cw ? get_ammo_type_for_weapon((ITEM_IDS)cw->item) : 0;

            if (at > 0 && at <= AMMOTYPE_GLOBAL_MAX && crounds[k] > 0)
            {
                r->slots[at - 1].quantity = (u16)(crounds[k] > 65535 ? 65535 : crounds[k]);
                n++;
            }
        }
        if (n == 0)
        {
            return -1;
        }
        domakedefaultobj(0, (ObjectRecord *)r, 0);
        r->state &= ~PROPSTATE_RESPAWN;
        if (r->prop == NULL)
        {
            return 0;
        }
        sItems[slot].obj = (ObjectRecord *)r;
        sItems[slot].prop = r->prop;
    }
    else if (armour)
    {
        BodyArmourRecord *r = &sArmourRecs[sArmourNext];

        sArmourNext = (sArmourNext + 1) % REC_N;

        memset(r, 0, sizeof(*r));
        r->extrascale = 0x180;                 /* as the stock MP setup record */
        r->type = PROPDEF_ARMOUR;
        r->obj = PROP_BODYARMOUR;
        r->pad = pad;
        r->flags = 1;
        *(s32 *)&r->damage = 1000 << 16;       /* the raw setup form: domakedefaultobj converts it */
        r->initialamount = amount > 0.0f ? (amount > 1.0f ? 1.0f : amount) : 1.0f;
        r->amount = r->initialamount;
        domakedefaultobj(0, (ObjectRecord *)r, 0);
        r->state &= ~PROPSTATE_RESPAWN;        /* co-op counts as multiplayer: a stage armour would respawn, this one must not */
        if (r->prop == NULL)
        {
            return 0;
        }
        sItems[slot].obj = (ObjectRecord *)r;
        sItems[slot].prop = r->prop;
    }
    else
    {
        WeaponObjRecord *o = (WeaponObjRecord *)create_new_item_instance_of_model(getPropForHeldItem((ITEM_IDS)w->item), w->item);
        Mtxf mtx, rot;
        coord3d vel;

        if (o == NULL || o->prop == NULL)
        {
            return 0;
        }
        o->timer = ITEM_MARK + sItemSerial;
        matrix_4x4_set_identity(&mtx);
        matrix_4x4_set_identity(&rot);
        vel.x = vel.y = vel.z = 0.0f;
        pos.y = fl + 30.0f;
        gunInitProjectileObject((ObjectRecord *)o, &pos, tile, &mtx, &vel, &rot, NULL);
        sItems[slot].obj = (ObjectRecord *)o;
        sItems[slot].prop = o->prop;
    }
    sItems[slot].used = 1;
    armour = armour || box;   /* from here: a non-weapon item (record-based, no timer marker) */
    sItems[slot].armour = (u8)armour;
    sItems[slot].serial = sItemSerial;
    sItems[slot].ammo = armour ? -1 : ammo;
    sItems[slot].item = armour ? -1 : w->item;
    portConLogf("ge: arena: item spawned: %s at pad %d (%.0f,%.0f,%.0f) handle %d%s\n", box ? "ammo box" : armour ? "armour" : w->name, pad, (double)pos.x, (double)fl, (double)pos.z,
                (int)((sItemSerial << 4) | slot), (armour || ammo < 0) ? "" : " (custom ammo)");
    return (sItemSerial << 4) | slot;
}

int portLuaGameItemAlive(int handle)
{
    return portModeActive() ? itemAlive(handle) : 0;
}

int portLuaGameItemRemove(int handle)
{
    if (!portModeActive() || !itemAlive(handle))
    {
        return 0;
    }
    sItems[handle & 15].obj->runtime_bitflags |= RUNTIMEBITFLAG_REMOVE;   /* freed by the object tick, as a pickup is */
    sItems[handle & 15].used = 0;
    return 1;
}

/* where a live item lies: x, z (for tests and the HUD); 0 = gone */
int portLuaGameItemPos(int handle, float *x, float *z)
{
    if (!portModeActive() || !itemAlive(handle))
    {
        return 0;
    }
    *x = sItems[handle & 15].prop->pos.x;
    *z = sItems[handle & 15].prop->pos.z;
    return 1;
}

extern s32 bondinvHasInvItem(ITEM_IDS weapon);

/* bond.ammo(player, weapon): does he carry the gun, and the reserve of its ammo type (a test/HUD query). 0 = no such Bond or weapon. */
int portLuaGameBondAmmo(int player, const char *name, int *has, int *ammo)
{
    const ArenaWeapon *w = findWeapon(name);
    s32 saved, type;

    if (!portModeActive() || w == NULL || player < 0 || player >= (int)portBondCount() || !portBondInPlay(player))
    {
        return 0;
    }
    saved = get_cur_playernum();
    set_cur_player(player);
    type = get_ammo_type_for_weapon((ITEM_IDS)w->item);
    *has = bondinvHasInvItem((ITEM_IDS)w->item) != 0;
    *ammo = type > 0 ? check_cur_player_ammo_amount_in_inventory((AMMOTYPE)type) : 0;
    set_cur_player(saved);
    return 1;
}

/* ---- test helper: bond.walk(player, x, z) -> distance. Real movement: sets pad 0's stick toward (x, z) through the GE_GOTO hook of
 * input_pc.c (g_PortGoto*), driven from Lua each tick, so the pickup tests walk Bond onto an item instead of warping him. Player 0 only. */
extern s32 g_PortGotoActive, g_PortGotoStickX, g_PortGotoStickY;

float portLuaGameBondWalk(int player, float x, float z)
{
    struct player *pl;
    f32 dx, dz, dist;

    if (!portModeActive() || player != 0 || g_playerPointers[0] == NULL || g_playerPointers[0]->prop == NULL)
    {
        g_PortGotoActive = 0;
        return -1.0f;
    }
    pl = g_playerPointers[0];
    dx = x - pl->prop->pos.x;
    dz = z - pl->prop->pos.z;
    dist = sqrtf(dx * dx + dz * dz);
    if (dist < 25.0f)
    {
        g_PortGotoActive = 0;
        g_PortGotoStickX = g_PortGotoStickY = 0;
        return dist;
    }
    /* the PC port's stick STRAFES (turning is the mouse), so lv.c's GE_GOTO steering, which turns with stick x, does not move Bond toward a
     * target. As the co-op puppet does: walk the world direction as forward + sideways stick from his facing f = (-sin th, cos th). */
    {
        f32 th = pl->vv_theta * (3.14159265f / 180.0f), fx = -sinf(th), fz = cosf(th);
        f32 fwd = (dx * fx + dz * fz) / dist, side = (dx * -fz + dz * fx) / dist;

        g_PortGotoStickY = (s32)(fwd * 70.0f);
        g_PortGotoStickX = (s32)(side * 70.0f);
    }
    g_PortGotoActive = 1;
    return dist;
}

void portLuaGameBondWalkStop(void)
{
    g_PortGotoActive = 0;
    g_PortGotoStickX = g_PortGotoStickY = 0;
}

#endif /* TARGET_N64 */
