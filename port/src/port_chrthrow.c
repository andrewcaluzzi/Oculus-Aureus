/* port_chrthrow.c - chr thrown knife (see port_chrthrow.h).
 *
 * The projectile setup copies the chr rocket path in chrlvFireWeaponRelated (spawn at the
 * gun hand, gunInitProjectileObject with the chr's prop as owner) and the knife fields of
 * generate_player_thrown_knife (flag 2, bounce 0.1, refresh rate, the knife runtime bit),
 * so the vanilla projectile tick flies it and its knife-hit branch damages the chr it hits.
 * It draws no RNG itself. That branch hands a Bond-team chr's hits to portChrKnifeHit. */
#ifndef TARGET_N64

#include <stdlib.h>
#include <math.h>   /* resolves to include/math.h: sqrtf only; sinf/cosf come from PR/gu.h */
#include <ultra64.h>
#include <PR/gu.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include "game/bondview.h"
#include "game/chr.h"
#include "game/chraction.h"
#include "game/gun.h"
#include "game/initanitable.h"
#include "game/math_atan2f.h"   /* not declared anywhere else; -ffreestanding -w hides the implicit int */
#include "game/matrixmath.h"
#include "game/model.h"
#include "game/player.h"
#include "game/propobj.h"
#include "game/stan.h"
#include "port_event.h"
#include "port_faction.h"
#include "port_bot.h"
#include "port_arena.h"
#include "port_chrthrow.h"

extern int portModEnemyKnifeOwner(PropRecord *ownerprop);   /* port_modifier.c */

extern void portConLogf(const char *fmt, ...);   /* port_console.c */

#define KNIFE_SPEED        25.0f   /* generate_player_thrown_knife: aim dir x 25 ... */
#define KNIFE_LIFT         5.0f    /* ... plus 5 up */
#define KNIFE_NEAR_RANGE   1300.0f /* up to here: the stock speed-25 throw. Beyond: faster, ballistic, with spread (see below) */
#define KNIFE_SPEED_MAX    140.0f  /* horizontal launch speed cap per tick (a 4500-unit throw is ~86; the cap reaches ~7000+) */
#define KNIFE_SPREAD_DEG_PER_1000 0.8f    /* long throws: aim error half angle, degrees per 1000 units of range (+-3.6 deg at 4500, +-190 units) */
#define KNIFE_HAND_UP      30.0f   /* chrlvFireWeaponRelated's no-weapon gun-hand fallback */
#define KNIFE_HAND_SIDE    10.0f
#define KNIFE_CHEST        25.0f   /* above the target's prop y (pelvis: ~ground + 105 standing) */
#define KNIFE_BOND_CHEST   50.0f   /* below Bond's prop y (his eye); his hit box is floor .. duck height + 10 */
#define KNIFE_VIEWER_SCALE 0.125f  /* handles_shot_actors: a player's damage is the chr damage x 0.125 */
#define KNIFE_GRAVITY      0.27777779f   /* propobj.c PROP_PROJECTILE_GRAVITY_MODIFIER, per tick^2 */
#define KNIFE_SPIN_DEG     (360.0f / 12.3f)   /* player knife: 360 / (12.1 .. 12.6) per tick */
#define KNIFE_BOUNCE       0.1f
#ifdef REFRESH_PAL
#define KNIFE_REFRESH      50      /* gun.c THROWN_ITEM_REFRESH_RATE */
#else
#define KNIFE_REFRESH      60
#endif
#define THROW_ANIM_START   112.0f  /* PTR_ANIM_fire_throw_grenade releases at 119 (chrlvTickThrowGrenade) */
#define THROW_ANIM_END     193.0f
#define THROW_ANIM_SPEED   0.65f   /* chrlvThrowGrenade: 0.5 .. 0.8 by speed rating */

static f32 sFriendlyFire = 0.5f;   /* a setting (coop_bot_friendly_fire): the bot's knives on Bond */

static u32 sThrowRng = 0x1B873593u;

/* private LCG for the long-throw spread: never the game's RNG (the throw itself draws none) */
static u32 throwRand(void)
{
    sThrowRng = sThrowRng * 1664525u + 1013904223u;
    return sThrowRng >> 8;
}

void portChrThrowSetFriendlyFire(f32 scale)
{
    sFriendlyFire = (scale < 0.0f) ? 0.0f : (scale > 1.0f) ? 1.0f : scale;
}

f32 portChrThrowFriendlyFire(void)
{
    return sFriendlyFire;
}

static s32 throwDebug(void)
{
    static s32 dbg = -1;

    if (dbg < 0)
    {
        const char *e = getenv("GE_BOT_DEBUG");
        dbg = (e != NULL && e[0] != '\0' && e[0] != '0') ? 1 : 0;
    }
    return dbg;
}

static f32 throwWrapAngle(f32 a)
{
    while (a >= M_TAU_F)
    {
        a -= M_TAU_F;
    }
    while (a < 0.0f)
    {
        a += M_TAU_F;
    }
    return a;
}

/* chrnum for logs; -2 = Bond (his viewer prop has no chr) */
static int throwTargetId(PropRecord *target)
{
    return (target->type == PROP_TYPE_CHR && target->chr != NULL) ? (int)target->chr->chrnum : -2;
}

/* the faction clock in 1/60 s, the t= base of the bot and faction logs */
static int throwTicks(void)
{
    return (int)(portFactionNow() * 60.0f + 0.5f);
}

s32 portChrThrowKnife(ChrRecord *thrower, PropRecord *target)
{
    PropRecord *tp;
    ChrRecord *tc;   /* the target chr, NULL for Bond */
    ObjectRecord *obj;
    StandTile *stan;
    coord3d hand, chest, vel;
    Mtxf rot, spin;
    f32 yaw, dx, dy, dz, len;

    if (thrower == NULL || thrower->prop == NULL || thrower->model == NULL || thrower->prop->stan == NULL ||
        target == NULL)
    {
        return FALSE;
    }
    tc = (target->type == PROP_TYPE_CHR) ? target->chr : NULL;
    if ((target->type == PROP_TYPE_CHR && (tc == NULL || tc->model == NULL)) ||
        (target->type != PROP_TYPE_CHR && target->type != PROP_TYPE_VIEWER))
    {
        return FALSE;
    }
    tp = thrower->prop;

    /* face the target first: the vanilla grenade throw turns to Bond instead */
    yaw = throwWrapAngle(atan2f(target->pos.x - tp->pos.x, target->pos.z - tp->pos.z));
    setsubroty(thrower->model, yaw);
    modelSetAnimation(thrower->model, (ModelAnimation *)&ptr_animation_table->data[PTR_ANIM_fire_throw_grenade],
                      FALSE, THROW_ANIM_START, THROW_ANIM_SPEED, 16.0f);
    modelSetAnimEndFrame(thrower->model, THROW_ANIM_END);

    /* right gun hand, as the rocket path's fallback (hand 0) */
    hand.x = tp->pos.x - cosf(yaw) * KNIFE_HAND_SIDE;
    hand.y = tp->pos.y + KNIFE_HAND_UP;
    hand.z = tp->pos.z + sinf(yaw) * KNIFE_HAND_SIDE;
    stan = tp->stan;
    if (!stanTestLineUnobstructed(&stan, tp->pos.x, tp->pos.z, hand.x, hand.z, CDTYPE_DOORS,
                                  hand.y - thrower->ground, hand.y - thrower->ground, 0.0f, 1.0f))
    {
        hand.x = tp->pos.x;   /* hand inside a door: release from the body */
        hand.z = tp->pos.z;
        stan = tp->stan;
    }

    chest.x = target->pos.x;
    chest.y = (tc != NULL) ? target->pos.y + KNIFE_CHEST : target->pos.y - KNIFE_BOND_CHEST;   /* follows kneeling */
    chest.z = target->pos.z;
    dx = chest.x - hand.x;
    dy = chest.y - hand.y;
    dz = chest.z - hand.z;
    len = sqrtf(dx * dx + dy * dy + dz * dz);
    if (len < 1.0f)
    {
        return FALSE;
    }
    if (len <= KNIFE_NEAR_RANGE)
    {
        /* Aim below the chest by the arc's rise at the target (the lift minus gravity over
         * len/speed ticks), or mid-range throws sail over the head. */
        f32 t = len / KNIFE_SPEED;

        dy -= KNIFE_LIFT * t - 0.5f * KNIFE_GRAVITY * t * t;
        len = sqrtf(dx * dx + dy * dy + dz * dz);
        vel.x = dx / len * KNIFE_SPEED;
        vel.y = dy / len * KNIFE_SPEED + KNIFE_LIFT;
        vel.z = dz / len * KNIFE_SPEED;
    }
    else
    {   /* A long throw (arena knife sisters, up to 4500+ units): launch faster and solve the arc. The horizontal speed grows with
         * the range (flight time stays near 50 ticks), then the vertical launch speed is exactly what lands the knife on the
         * chest after t ticks of gravity: vy = dy / t + g t / 2. A little aim error that grows with range, so a long throw
         * is a gamble and not a laser. The projectile tick moves it with a swept test (sub_GAME_7F042EB4), so speed is safe. */
        f32 dh = sqrtf(dx * dx + dz * dz);
        f32 vh = KNIFE_SPEED * (dh / KNIFE_NEAR_RANGE);
        f32 t, vy, err, ang, ch, sh, nx, nz;

        if (vh > KNIFE_SPEED_MAX)
        {
            vh = KNIFE_SPEED_MAX;
        }
        t = dh / vh;
        vy = dy / t + 0.5f * KNIFE_GRAVITY * t;
        err = (dh / 1000.0f) * KNIFE_SPREAD_DEG_PER_1000 * (3.14159265f / 180.0f);   /* total cone half angle */
        ang = ((f32)(throwRand() & 0xFFFF) / 32768.0f - 1.0f) * err;                  /* yaw error */
        ch = cosf(ang);
        sh = sinf(ang);
        nx = dx / dh;
        nz = dz / dh;
        vel.x = (nx * ch - nz * sh) * vh;
        vel.z = (nz * ch + nx * sh) * vh;
        vel.y = vy + ((f32)(throwRand() & 0xFFFF) / 32768.0f - 1.0f) * err * vh;      /* pitch error, in vertical speed */
        len = dh;
    }

    obj = create_new_item_instance_of_model(PROP_CHRKNIFE, ITEM_THROWKNIFE);
    if (obj == NULL)
    {
        if (throwDebug())
        {
            portConLogf("ge: bot: knife at chr %d: no free object\n", throwTargetId(target));
        }
        return FALSE;
    }

    matrix_4x4_set_rotation_around_y(yaw, &rot);
    guRotateF(spin.m, KNIFE_SPIN_DEG, cosf(yaw), 0.0f, -sinf(yaw));   /* end over end */
    gunInitProjectileObject(obj, &hand, stan, &rot, &vel, &spin, tp);

    if (obj->runtime_bitflags & RUNTIMEBITFLAG_HASPROJECTILE)
    {
        obj->projectile->flags |= 2;
        obj->projectile->unk8C = KNIFE_BOUNCE;
        obj->projectile->refreshrate = KNIFE_REFRESH;
        obj->runtime_bitflags |= RUNTIMEBITFLAG_THROWING_KNIFE_RELATED;
    }
    objUpdateThrowKnifeSound(obj);

    if (throwDebug() && tc != NULL)
    {
        portConLogf("ge: bot: knife at chr %d dist %.0f hand (%.0f %.0f %.0f) chest y %.0f (prop y %.0f ground %.0f) "
                    "proj %d, target damage %.2f/%.2f arghs %d act %d\n",
                    (int)tc->chrnum, len, hand.x, hand.y, hand.z, chest.y, target->pos.y,
                    tc->ground, (obj->runtime_bitflags & RUNTIMEBITFLAG_HASPROJECTILE) ? 1 : 0,
                    tc->damage, tc->maxdamage, (int)tc->numarghs, (int)tc->actiontype);
    }
    else if (throwDebug())
    {
        portConLogf("ge: bot: knife at Bond dist %.0f hand (%.0f %.0f %.0f) chest y %.0f (eye y %.0f) proj %d, "
                    "health %.3f armour %.3f, t=%d\n",
                    len, hand.x, hand.y, hand.z, chest.y, target->pos.y,
                    (obj->runtime_bitflags & RUNTIMEBITFLAG_HASPROJECTILE) ? 1 : 0,
                    g_CurrentPlayer->bondhealth, g_CurrentPlayer->bondarmour, throwTicks());
    }
    return TRUE;
}

/* Bond's hits on chrs so far (handles_shot_actors' isPlayer counters), for the stats check. */
static s32 throwBondHitCount(void)
{
    return get_curplayer_shot_register(SHOT_REGISTER_HEAD) + get_curplayer_shot_register(SHOT_REGISTER_BODY) +
           get_curplayer_shot_register(SHOT_REGISTER_LIMB) + get_curplayer_shot_register(SHOT_REGISTER_GUN) +
           get_curplayer_shot_register(SHOT_REGISTER_HAT);
}

/* Solo first-person Bond has no chr, so do what handles_shot_actors' viewer branch would:
 * the knife's damage (the hit box has no body part: 1x) x 0.125 to the player. */
static s32 throwHitBond(PropRecord *victim, coord3d *dir, s32 weaponnum, f32 scale)
{
    s32 idx = getPlayerPointerIndex(victim);
    s32 cur = get_cur_playernum();
    f32 health, armour;

    if (idx < 0)
    {
        return FALSE;
    }
    set_cur_player(idx);
    health = g_CurrentPlayer->bondhealth;
    armour = g_CurrentPlayer->bondarmour;
    record_damage_kills(gunItemGetDestructionAmount(weaponnum) * KNIFE_VIEWER_SCALE * scale, dir->x, dir->z, -1, TRUE);
    if (throwDebug())
    {
        portConLogf("ge: bot: knife hit Bond, health %.3f -> %.3f, armour %.3f -> %.3f, t=%d\n", health,
                    g_CurrentPlayer->bondhealth, armour, g_CurrentPlayer->bondarmour, throwTicks());
    }
    set_cur_player(cur);
    return TRUE;
}

s32 portChrKnifeHit(PropRecord *victim, s32 hitpart, coord3d *dir, s32 weaponnum, PropRecord *ownerprop)
{
    ChrRecord *chr;
    f32 before;
    s32 bondhits, hit;
    /* enemy_knives: a guard's knife hurts Bond at full strength, and only Bond's team (not the guards it passes) */
    const s32 enemy = portModEnemyKnifeOwner(ownerprop);

    if (victim == NULL)
    {
        return FALSE;
    }
    if (enemy && victim->type == PROP_TYPE_CHR && !portFactionPropIsBondTeam(victim))
    {
        return FALSE;
    }
    if (ownerprop != NULL && ownerprop->chr != NULL)
    {
        portEventSetAttacker(ownerprop->chr->chrnum);   /* DAMAGE attacker for the hit below; cleared on every exit */
    }
    if (victim->type == PROP_TYPE_VIEWER && victim->chr == NULL)
    {
        s32 r;

        if (enemy && ownerprop != NULL && ownerprop->chr != NULL)
        {   /* A5 test log: the distance the knife flew (owner now to Bond now) */
            f32 hx = victim->pos.x - ownerprop->pos.x, hz = victim->pos.z - ownerprop->pos.z;

            portConLogf("ge: arena: knife HIT Bond: thrower %d, distance %.0f\n", (int)ownerprop->chr->chrnum, sqrtf(hx * hx + hz * hz));
        }
        r = throwHitBond(victim, dir, weaponnum, enemy ? 1.0f : sFriendlyFire);

        portEventSetAttacker(PORT_CHR_NONE);
        return r;
    }
    chr = victim->chr;
    if (chr == NULL || (victim->type != PROP_TYPE_CHR && victim->type != PROP_TYPE_VIEWER))
    {
        portEventSetAttacker(PORT_CHR_NONE);
        return FALSE;
    }
    if (victim->type == PROP_TYPE_CHR)
    {
        portFactionNoteHit(chr, ownerprop);   /* the guard retaliates against the thrower */
    }
    before = chr->damage;
    bondhits = throwBondHitCount();
    /* isPlayer = 0: not Bond's hit stats, no g_AiHealthModifier (a player body takes the viewer branch) */
    hit = handles_shot_actors(chr, hitpart, dir, weaponnum, FALSE) ? TRUE : FALSE;
    portEventSetAttacker(PORT_CHR_NONE);
    if (hit && victim->type == PROP_TYPE_CHR && ownerprop != NULL && ownerprop->chr != NULL &&
        portBotIsBot(ownerprop->chr))
    {
        portArenaNoteHit(chrIsDead(chr) || chr->damage >= chr->maxdamage);   /* scoreboard: the bot's knife hit */
    }
    if (throwDebug())
    {
        portConLogf("ge: bot: knife hit chr %d part %d, damage %.2f -> %.2f/%.2f, Bond's hit count %d -> %d, t=%d\n",
                    (int)chr->chrnum, (int)hitpart, before, chr->damage, chr->maxdamage, bondhits,
                    throwBondHitCount(), throwTicks());
    }
    return hit;
}

#endif /* TARGET_N64 */
