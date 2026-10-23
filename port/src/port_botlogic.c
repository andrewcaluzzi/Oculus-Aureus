/* port_botlogic.c - pure co-op bot logic: factions, combatant rule, engagement and attack tables.
 * No game headers or globals, so it is unit-testable outside the game. */
#include <string.h>
#include "port_botlogic.h"

int portFactionResolveVanilla(int isCombatant)
{
    return isCombatant ? PORT_FACTION_HOSTILE : PORT_FACTION_NEUTRAL;
}

int portFactionRelHostile(int fa, int fb)
{
    return (fa == PORT_FACTION_BOND && fb == PORT_FACTION_HOSTILE) ||
           (fa == PORT_FACTION_HOSTILE && fb == PORT_FACTION_BOND);
}

int portBotIsCombatant(int holdsWeapon, int civilianFlag, int isAllyBody)
{
    return holdsWeapon && !civilianFlag && !isAllyBody;
}

static void engageRemoveAt(PortEngageTable *t, int i)
{
    t->e[i] = t->e[t->count - 1];
    t->count--;
}

static int engageFind(const PortEngageTable *t, int guard)
{
    int i;
    for (i = 0; i < t->count; i++)
    {
        if (t->e[i].guard == guard)
        {
            return i;
        }
    }
    return -1;
}

void portEngageReset(PortEngageTable *t)
{
    memset(t, 0, sizeof(*t));
}

void portEngageSet(PortEngageTable *t, int guard, int target, float now)
{
    int i = engageFind(t, guard);

    if (i < 0)
    {
        if (t->count < PORT_ENGAGE_MAX)
        {
            i = t->count++;
        }
        else
        {
            int k;
            i = 0;
            for (k = 1; k < t->count; k++)
            {
                if (t->e[k].lastSeen < t->e[i].lastSeen)
                {
                    i = k;
                }
            }
        }
    }
    t->e[i].guard = guard;
    t->e[i].target = target;
    t->e[i].lastSeen = now;
}

void portEngageSeen(PortEngageTable *t, int guard, float now)
{
    int i = engageFind(t, guard);

    if (i >= 0)
    {
        t->e[i].lastSeen = now;
    }
}

int portEngageTarget(PortEngageTable *t, int guard, float now, float timeout)
{
    int i = engageFind(t, guard);

    if (i < 0)
    {
        return -1;
    }
    if (now - t->e[i].lastSeen > timeout)
    {
        engageRemoveAt(t, i);
        return -1;
    }
    return t->e[i].target;
}

void portEngageDropTarget(PortEngageTable *t, int target)
{
    int i = 0;
    while (i < t->count)
    {
        if (t->e[i].target == target)
        {
            engageRemoveAt(t, i);
        }
        else
        {
            i++;
        }
    }
}

void portEngageDropGuard(PortEngageTable *t, int guard)
{
    int i = engageFind(t, guard);

    if (i >= 0)
    {
        engageRemoveAt(t, i);
    }
}

void portAttackReset(PortAttackTable *t)
{
    memset(t, 0, sizeof(*t));
}

void portAttackNote(PortAttackTable *t, int victim, int attacker, float now)
{
    int i;
    int slot = -1;

    for (i = 0; i < t->count; i++)
    {
        if (t->e[i].victim == victim)
        {
            slot = i;
            break;
        }
    }
    if (slot < 0)
    {
        if (t->count < PORT_ATTACK_MAX)
        {
            slot = t->count++;
        }
        else
        {
            slot = 0;
            for (i = 1; i < t->count; i++)
            {
                if (t->e[i].when < t->e[slot].when)
                {
                    slot = i;
                }
            }
        }
    }
    t->e[slot].victim = victim;
    t->e[slot].attacker = attacker;
    t->e[slot].when = now;
}

/* Timers accumulate dt in float; the epsilon keeps "N calls at dt" boundaries exact. */
#define BOT_EPS 1e-4f

void portBotTuningDefaults(PortBotTuning *t)
{
    t->leash = 700.0f;
    t->knifeRange = 1000.0f;
    t->knifeCooldown = 1.2f;
    t->catchupAfter = 2.0f;
    t->respawnAfter = 5.0f;
    t->followSlack = 50.0f;
    t->loiterAfter = 3.0f;
    t->loiterMove = 50.0f;
    t->loiterPauseMin = 2.0f;
    t->loiterPauseMax = 4.0f;
    t->loiter = 1;
    portBotTuningSetFollowDist(t, 250.0f);
}

void portBotTuningSetFollowDist(PortBotTuning *t, float followDist)
{
    t->followDist = followDist;
    t->catchupDist = followDist + 150.0f;   /* sprint only when well beyond personal space */
}

void portBotMemoryReset(PortBotMemory *m)
{
    m->sinceThrow = 1e9f;
    m->farFor = 0.0f;
    m->deadFor = 0.0f;
    m->stillFor = 0.0f;
    m->refX = 0.0f;
    m->refZ = 0.0f;
    m->following = 0;
    m->haveRef = 0;
}

PortBotAct portBotDecide(const PortBotInputs *in, const PortBotTuning *t, PortBotMemory *m)
{
    int loiterNow;

    if (!in->alive)
    {
        m->deadFor += in->dt;
        if (m->deadFor >= t->respawnAfter - BOT_EPS)
        {
            m->deadFor = 0.0f;
            return PORT_BOTACT_RESPAWN;
        }
        return PORT_BOTACT_NONE;
    }
    m->deadFor = 0.0f;
    m->sinceThrow += in->dt;

    /* Bond's stillness: time since he last strayed loiterMove from the reference point */
    {
        float dx = in->bondX - m->refX;
        float dz = in->bondZ - m->refZ;

        if (!m->haveRef || dx * dx + dz * dz >= t->loiterMove * t->loiterMove)
        {
            m->haveRef = 1;
            m->refX = in->bondX;
            m->refZ = in->bondZ;
            m->stillFor = 0.0f;
        }
        else
        {
            m->stillFor += in->dt;
        }
    }
    /* loiter: enabled, Bond still long enough, no target to fight, and she is within the loiter band
     * (further out she goes back to him first) */
    loiterNow = t->loiter && !in->hasTarget && m->stillFor >= t->loiterAfter - BOT_EPS &&
                in->distToBond <= 2.0f * t->followDist + t->followSlack;

    if (!loiterNow && (in->distToBond > t->catchupDist || in->navFailed))
    {
        m->farFor += in->dt;
    }
    else
    {
        m->farFor = 0.0f;
    }
    if (m->farFor >= t->catchupAfter - BOT_EPS && !in->bondSeesBot)
    {
        m->farFor = 0.0f;
        return PORT_BOTACT_TELEPORT;
    }

    /* the leash keeps her near Bond, but a hostile this close is thrown at even when she trails him */
    if (in->hasTarget && in->targetVisible && in->targetDist <= t->knifeRange &&
        (in->distToBond <= t->leash || in->targetDist <= t->leash * 0.5f))
    {
        if (m->sinceThrow >= t->knifeCooldown - BOT_EPS)
        {
            m->sinceThrow = 0.0f;
            return PORT_BOTACT_THROW;
        }
        return PORT_BOTACT_FACE_TARGET;
    }

    if (loiterNow)
    {
        return PORT_BOTACT_LOITER;
    }

    /* personal space: follow from beyond followDist + slack until half the slack past followDist (the brain
     * ticks a few times a second, so she overshoots a little and still settles at about followDist), else make
     * no move */
    if (m->following)
    {
        if (in->distToBond <= t->followDist + 0.5f * t->followSlack)
        {
            m->following = 0;
        }
    }
    else if (in->distToBond > t->followDist + t->followSlack)
    {
        m->following = 1;
    }
    return m->following ? PORT_BOTACT_RUN_TO_BOND : PORT_BOTACT_IDLE;
}

/* ---- private PRNG ---- */

void portBotRngSeed(unsigned int *s, unsigned int stage)
{
    *s = stage * 2654435761u + 0x9E3779B9u;
    if (*s == 0)
    {
        *s = 1;
    }
}

unsigned int portBotRngNext(unsigned int *s)
{
    unsigned int x = *s;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

float portBotRngFloat(unsigned int *s)
{
    return (float)(portBotRngNext(s) >> 8) * (1.0f / 16777216.0f);   /* 24 bits: exact in a float, [0, 1) */
}

void portBotLoiterPick(unsigned int *s, float followDist, float *turn, float *dist)
{
    *turn = portBotRngFloat(s);
    *dist = followDist * (1.0f + portBotRngFloat(s));
}

float portBotLoiterPause(unsigned int *s, const PortBotTuning *t)
{
    return t->loiterPauseMin + (t->loiterPauseMax - t->loiterPauseMin) * portBotRngFloat(s);
}

/* ---- skins: mp_chr_setup indices (front.c) with friendly names. The order is the roster's. ---- */

typedef struct { const char *name; unsigned char mp; } BotSkin;

static const BotSkin sSkins[] = {
    { "bond", 0 },              { "natalya", 1 },           { "trevelyan", 2 },         { "xenia", 3 },
    { "ourumov", 4 },           { "boris", 5 },             { "valentin", 6 },          { "mishkin", 7 },
    { "mayday", 8 },            { "jaws", 9 },              { "oddjob", 10 },           { "baron_samedi", 11 },
    { "russian_soldier", 12 },  { "russian_infantry", 13 }, { "scientist_m", 14 },      { "scientist_f", 15 },
    { "russian_commandant", 16 }, { "janus_marine", 17 },   { "naval_officer", 18 },    { "helicopter_pilot", 19 },
    { "st_petersburg_guard", 20 }, { "civilian_f", 21 },    { "civilian_m1", 22 },      { "civilian_m2", 23 },
    { "civilian_m3", 24 },      { "siberian_guard1", 25 },  { "arctic_commando", 26 },  { "siberian_guard2", 27 },
    { "siberian_special_forces", 28 }, { "jungle_commando", 29 }, { "janus_special_forces", 30 },
    { "moonraker_m", 31 },      { "moonraker_f", 32 },      { "rosika", 33 },
    { "karl", 34 },             { "martin", 35 },           { "mark", 36 },             { "dave", 37 },
    { "duncan", 38 },           { "b", 39 },                { "steve_ellis", 40 },      { "grant", 41 },
    { "graeme", 42 },           { "ken", 43 },              { "alan", 44 },             { "pete", 45 },
    { "shaun", 46 },            { "dwayne", 47 },           { "des", 48 },              { "chris", 49 },
    { "lee", 50 },              { "neil", 51 },             { "jim", 52 },              { "robin", 53 },
    { "steve_h", 54 },          { "terrorist", 55 },        { "biker", 56 },            { "joel", 57 },
    { "scott", 58 },            { "joe", 59 },              { "sally", 60 },            { "marion", 61 },
    { "mandy", 62 },            { "vivien", 63 },
    { "bond_boiler", 64 },      { "bond_formal", 65 },      { "bond_jungle", 66 },      { "bond_parka", 67 },
    { "trevelyan_006", 68 },    { "natalya_jungle", 69 },
};

_Static_assert(sizeof(sSkins) / sizeof(sSkins[0]) == PORT_BOT_SKIN_COUNT, "PORT_BOT_SKIN_COUNT");

int portBotSkinCount(void)
{
    return PORT_BOT_SKIN_COUNT;
}

const char *portBotSkinName(int k)
{
    return (k >= 0 && k < PORT_BOT_SKIN_COUNT) ? sSkins[k].name : NULL;
}

int portBotSkinMp(int k)
{
    return (k >= 0 && k < PORT_BOT_SKIN_COUNT) ? (int)sSkins[k].mp : -1;
}

static char skinLc(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

int portBotSkinFind(const char *name)
{
    int k;

    if (name == NULL || name[0] == '\0')
    {
        return -1;
    }
    for (k = 0; k < PORT_BOT_SKIN_COUNT; k++)
    {
        const char *a = sSkins[k].name;
        const char *b = name;

        while (*a != '\0' && skinLc(*a) == skinLc(*b))
        {
            a++;
            b++;
        }
        if (*a == '\0' && *b == '\0')
        {
            return k;
        }
    }
    return -1;
}

int portBotSkinFindMp(int mp)
{
    int k;

    for (k = 0; k < PORT_BOT_SKIN_COUNT; k++)
    {
        if ((int)sSkins[k].mp == mp)
        {
            return k;
        }
    }
    return -1;
}

int portAttackLast(const PortAttackTable *t, int victim, float now, float window)
{
    int i;

    for (i = 0; i < t->count; i++)
    {
        if (t->e[i].victim == victim)
        {
            return (now - t->e[i].when > window) ? -1 : t->e[i].attacker;
        }
    }
    return -1;
}
