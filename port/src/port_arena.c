/* port_arena.c - bot test arena (see port_arena.h).
 *
 * GE_ARENA=1 makes Bond invincible with infinite ammo (the cheat system), adds spare chr and anim
 * slots at stage load, and turns the bot on. `spawn guard` queues a ring of armed guards that
 * portArenaTick spawns in the sim update. GE_ARENA_BENCH=guards:N,dist:D,secs:S spawns them 2 s
 * after the bot is up, runs S seconds of sim time, prints one scoreboard line and exits. */
#ifndef TARGET_N64

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ultra64.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include <bondaicommands.h>   /* GAILIST_ */
#include <memp.h>
#include "game/math_atan2f.h"   /* not declared anywhere else; -ffreestanding -w hides the implicit int */
#include "game/chr.h"
#include "game/chraction.h"
#include "game/objecthandler.h"   /* chrGiveWeapon */
#include "port_botlogic.h"
#include "port_spawn.h"
#include "port_faction.h"
#include "port_bot.h"
#include "port_arena.h"
#include "port_net.h"
#include "game/player.h"

extern void portConLogf(const char *fmt, ...);   /* port_console.c */
extern s32 chrGetNumFree(void);                  /* chr.c */

#define ARENA_CHRNUM_BASE   7000   /* arena guards: unique, above the corpse range */
#define ARENA_MAX_GUARDS    40
#define ARENA_BENCH_DELAY   2.0f   /* seconds after the bot is up */
#define ARENA_DEF_COUNT     4
#define ARENA_DEF_DIST      800.0f

static struct {
    s32 pendN;            /* queued `spawn guard`, 0 = none */
    f32 pendDist;
    s32 pendClear;
    s32 nextNum;          /* next arena chrnum offset */
    s32 nums[ARENA_MAX_GUARDS * 4];
    s32 count;
    f32 firstSpawnAt;     /* portFactionNow of the first arena guard, -1 = none */
    s32 loggedMem;
    /* bench */
    s32 benchGuards;
    f32 benchDist, benchSecs;
    f32 benchBotUp;       /* seconds the bot has been alive */
    f32 benchRun;         /* seconds since the guards were spawned, <0 = not yet */
    /* scoreboard */
    s32 throws, hits, kills, deaths;
    f32 firstThrow, firstKill, distSum;
} sA;

static s32 sOn = -1;
static s32 sBenchParsed;

s32 portArenaEnabled(void)
{
    if (sOn < 0)
    {
        const char *e = getenv("GE_ARENA");

        sOn = (e != NULL && e[0] != '\0' && e[0] != '0') ? 1 : 0;
    }
    return sOn;
}

s32 portArenaExtraSlots(void)
{
    return portArenaEnabled() ? PORT_ARENA_EXTRA_SLOTS : 0;
}

static f32 benchField(const char *s, const char *key, f32 def)
{
    const char *p = strstr(s, key);

    return p != NULL ? (f32)atof(p + strlen(key)) : def;
}

void portArenaReset(void)
{
    const char *e;

    memset(&sA, 0, sizeof(sA));
    sA.firstSpawnAt = -1.0f;
    sA.firstThrow = sA.firstKill = -1.0f;
    sA.benchRun = -1.0f;
    if (!portArenaEnabled())
    {
        return;
    }
    e = getenv("GE_ARENA_BENCH");
    if (e != NULL && e[0] != '\0')
    {
        sA.benchGuards = (s32)benchField(e, "guards:", 6.0f);
        sA.benchDist = benchField(e, "dist:", ARENA_DEF_DIST);
        sA.benchSecs = benchField(e, "secs:", 60.0f);
        if (!sBenchParsed)
        {
            sBenchParsed = TRUE;
            portConLogf("ge: arena: bench guards %d dist %.0f secs %.0f\n", (int)sA.benchGuards,
                        sA.benchDist, sA.benchSecs);
        }
    }
}

void portArenaNoteThrow(f32 targetDist)
{
    sA.throws++;
    sA.distSum += targetDist;
    if (sA.firstThrow < 0.0f)
    {
        sA.firstThrow = portFactionNow();
    }
}

void portArenaNoteHit(s32 killed)
{
    sA.hits++;
    if (killed)
    {
        sA.kills++;
        if (sA.firstKill < 0.0f)
        {
            sA.firstKill = portFactionNow();
        }
    }
}

void portArenaNoteDeath(void)
{
    sA.deaths++;
}

void portArenaTotals(s32 *throws, s32 *hits, s32 *kills, s32 *deaths)
{
    *throws = sA.throws;
    *hits = sA.hits;
    *kills = sA.kills;
    *deaths = sA.deaths;
}

/* Seconds since the guards went in (or since stage start), -1 = never. */
static f32 statRel(f32 at)
{
    f32 base = sA.firstSpawnAt > 0.0f ? sA.firstSpawnAt : 0.0f;

    return at < 0.0f ? -1.0f : at - base;
}

static void statsLine(const char *prefix)
{
    portConLogf("%s throws %d hits %d kills %d deaths %d acc %.2f first_throw %.1fs first_kill %.1fs "
                "mean_throw_dist %.0f\n", prefix, (int)sA.throws, (int)sA.hits, (int)sA.kills, (int)sA.deaths,
                sA.throws > 0 ? (double)sA.hits / (double)sA.throws : 0.0, statRel(sA.firstThrow),
                statRel(sA.firstKill), sA.throws > 0 ? sA.distSum / (f32)sA.throws : 0.0f);
}

void portArenaStatsPrint(void)
{
    statsLine("ge: bot: stats:");
}

/* ---- guards ---- */

/* An existing live armed guard's body and head, so no new model file loads mid-level. */
static void arenaPickBody(s32 *body, s32 *head)
{
    s32 i;

    *body = BODY_Russian_Soldier;
    *head = -1;   /* netplay job 5: no live guard to copy (Flatland has none): a random head (head 0 is a whole body, so the spawn failed there) */
    for (i = 0; i < g_NumChrSlots; i++)
    {
        ChrRecord *c = &g_ChrSlots[i];

        if (c->model != NULL && c->prop != NULL && c->prop->type == PROP_TYPE_CHR && c->chrnum != PORT_BOT_CHRNUM && c->chrnum < ARENA_CHRNUM_BASE &&
            !chrIsDead(c) && chrGetEquippedWeaponPropWithCheck(c, GUNRIGHT) != NULL)
        {
            *body = c->bodynum;
            *head = c->headnum;
            return;
        }
    }
}

static void arenaClear(void)
{
    s32 i, n = 0;

    for (i = 0; i < sA.count; i++)
    {
        ChrRecord *c = chrFindByLiteralId(sA.nums[i]);

        if (c != NULL && c->prop != NULL)
        {
            c->hidden |= CHRHIDDEN_REMOVE;   /* same removal portBotDismiss uses */
            n++;
        }
    }
    sA.count = 0;
    portConLogf("ge: arena: cleared %d guards\n", (int)n);
}

static void arenaSpawnGuards(s32 n, f32 dist, PropRecord *bp)
{
    static const f32 shrink[] = { 1.0f, 0.75f, 0.5f, 0.3f };
    PortSpawnDesc d;
    s32 body, head, i, made = 0;

    arenaPickBody(&body, &head);
    memset(&d, 0, sizeof(d));
    d.body = body;
    d.head = head;
    d.nearprop = bp;
    d.ailistid = GAILIST_PERSISTENTLY_CHASE_AND_ATTACK_BOND;
    for (i = 0; i < n; i++)
    {
        f32 ang = (f32)i * (M_TAU_F / (f32)n);
        s32 k, ok = FALSE;
        coord3d pos;
        StandTile *stan = NULL;
        ChrRecord *chr;

        for (k = 0; k < 4 && !ok; k++)
        {
            ok = portSpawnRingPoint(bp, ang, dist * shrink[k], &pos, &stan);
        }
        if (!ok)
        {
            portConLogf("ge: arena: ring point %d: no valid floor, skipped\n", (int)i);
            continue;
        }
        if (sA.count >= (s32)(sizeof(sA.nums) / sizeof(sA.nums[0])))
        {
            break;
        }
        if (chrGetNumFree() < PORT_SPAWN_MIN_FREE)
        {
            portConLogf("ge: arena: chr slots full (%d free), stopped at %d\n", (int)chrGetNumFree(), (int)made);
            break;
        }
        d.angle = atan2f(bp->pos.x - pos.x, bp->pos.z - pos.z);
        d.chrnum = ARENA_CHRNUM_BASE + sA.nextNum;
        chr = portSpawnChrAt(&d, &pos, stan);
        if (chr == NULL)
        {
            continue;   /* portSpawnChr logged why */
        }
        sA.nextNum++;
        sA.nums[sA.count++] = chr->chrnum;
        chrGiveWeapon(chr, PROP_CHRKALASH, ITEM_AK47, 0);
        made++;
        portConLogf("ge: arena: spawned guard chr %d at (%.0f %.0f) dist %.0f\n", (int)chr->chrnum, pos.x, pos.z,
                    sqrtf((pos.x - bp->pos.x) * (pos.x - bp->pos.x) + (pos.z - bp->pos.z) * (pos.z - bp->pos.z)));
    }
    if (made > 0 && sA.firstSpawnAt < 0.0f)
    {
        sA.firstSpawnAt = portFactionNow();
    }
    portConLogf("ge: arena: %d of %d guards spawned, %d slots free\n", (int)made, (int)n, (int)chrGetNumFree());
}

extern int portSessionCmdRun(int kind, int player, int a, int b);   /* port_netcmd.c */

/* Netplay (job 5): the spawn runs as a session command, at the same tick on both machines, around the ISSUER's Bond (index 0 host, 1 guest).
 * Nothing here reads the local view; the ring points come from the Bond's prop position, the chr numbers from the counters. */
void portArenaNetSpawn(s32 issuer, s32 n, f32 dist)
{
    struct player *pl = (issuer >= 0 && issuer < 4) ? g_playerPointers[issuer] : NULL;

    if (pl == NULL || pl->prop == NULL || pl->prop->stan == NULL || pl->bonddead)
    {
        portConLogf("ge: arena: spawn guard: player %d has no position to spawn around: nothing done\n", (int)issuer + 1);
        return;
    }
    arenaSpawnGuards(n, dist, pl->prop);
}

void portArenaNetClear(void)
{
    arenaClear();
}

void portArenaConsole(const char *sub, const char *rest)
{
    if (sub != NULL && strcmp(sub, "clear") == 0)
    {
        if (portNetActive())
        {
            portSessionCmdRun(PORT_NETCMD_SPAWN_CLEAR, portNetCmdSelf(), 0, 0);
            return;
        }
        sA.pendClear = TRUE;
    }
    else if (sub != NULL && strcmp(sub, "guard") == 0)
    {
        s32 n = ARENA_DEF_COUNT;
        f32 dist = ARENA_DEF_DIST;

        if (rest != NULL && rest[0] != '\0')
        {
            char *end;

            n = (s32)strtol(rest, &end, 10);
            if (*end != '\0')
            {
                dist = (f32)atof(end);
            }
        }
        if (n < 1 || n > ARENA_MAX_GUARDS || dist < 100.0f || dist > 5000.0f)
        {
            portConLogf("ge: arena: usage: spawn guard [n 1..%d] [dist 100..5000]\n", ARENA_MAX_GUARDS);
            return;
        }
        if (portNetActive())
        {
            portSessionCmdRun(PORT_NETCMD_SPAWN_GUARD, portNetCmdSelf(), n, (s32)dist);
            return;
        }
        sA.pendN = n;
        sA.pendDist = dist;
    }
    else
    {
        portConLogf("ge: arena: usage: spawn guard [n=4] [dist=800] | spawn clear\n");
        return;
    }
    if (!portBotEnabled())
    {
        portConLogf("ge: arena: queued, but the bot/arena is off so nothing ticks (set GE_ARENA=1)\n");
    }
}

void portArenaTick(f32 dt, PropRecord *bp)
{
    if (!portArenaEnabled() && sA.pendN == 0 && !sA.pendClear)
    {
        return;
    }
    if (!sA.loggedMem && portArenaEnabled())
    {
        sA.loggedMem = TRUE;
        portConLogf("ge: arena: stage mempool %d bytes free, %d chr slots free (+%d arena slots)\n",
                    (int)mempGetBankSizeLeft(MEMPOOL_STAGE), (int)chrGetNumFree(), (int)portArenaExtraSlots());
    }
    if (sA.pendClear)
    {
        sA.pendClear = FALSE;
        arenaClear();
    }
    if (sA.pendN > 0)
    {
        s32 n = sA.pendN;

        sA.pendN = 0;
        arenaSpawnGuards(n, sA.pendDist, bp);
    }

    if (sA.benchGuards > 0)
    {
        if (sA.benchRun < 0.0f)
        {
            sA.benchBotUp = portBotChr() != NULL ? sA.benchBotUp + dt : 0.0f;
            if (sA.benchBotUp >= ARENA_BENCH_DELAY)
            {
                sA.benchRun = 0.0f;
                arenaSpawnGuards(sA.benchGuards, sA.benchDist, bp);
            }
        }
        else
        {
            sA.benchRun += dt;
            if (sA.benchRun >= sA.benchSecs)
            {
                char pre[96];

                snprintf(pre, sizeof(pre), "ge: arena: bench guards %d dist %.0f secs %.0f:", (int)sA.benchGuards,
                         sA.benchDist, sA.benchSecs);
                statsLine(pre);
                fflush(stdout);
                exit(0);
            }
        }
    }
}

#endif /* TARGET_N64 */
