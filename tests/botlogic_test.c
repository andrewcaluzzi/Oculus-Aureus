/* botlogic_test.c - unit tests for the pure co-op bot logic in port_botlogic.c.
 * Add new test functions to main's list; the summary line reports passes and failures. */
#include <stdio.h>
#include "port_botlogic.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond)                                              \
    do                                                           \
    {                                                            \
        if (cond)                                                \
        {                                                        \
            g_pass++;                                            \
        }                                                        \
        else                                                     \
        {                                                        \
            g_fail++;                                            \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                        \
    } while (0)

static void test_rel_bond_vs_hostile(void)
{
    CHECK(portFactionRelHostile(PORT_FACTION_BOND, PORT_FACTION_HOSTILE) == 1);
    CHECK(portFactionRelHostile(PORT_FACTION_HOSTILE, PORT_FACTION_BOND) == 1);
}

static void test_rel_never(void)
{
    CHECK(portFactionRelHostile(PORT_FACTION_BOND, PORT_FACTION_BOND) == 0);
    CHECK(portFactionRelHostile(PORT_FACTION_HOSTILE, PORT_FACTION_HOSTILE) == 0);
    CHECK(portFactionRelHostile(PORT_FACTION_BOND, PORT_FACTION_NEUTRAL) == 0);
    CHECK(portFactionRelHostile(PORT_FACTION_HOSTILE, PORT_FACTION_NEUTRAL) == 0);
    CHECK(portFactionRelHostile(PORT_FACTION_BOND, PORT_FACTION_VANILLA) == 0);
    CHECK(portFactionRelHostile(PORT_FACTION_VANILLA, PORT_FACTION_HOSTILE) == 0);
    CHECK(portFactionRelHostile(PORT_FACTION_HOSTAGE, PORT_FACTION_HOSTILE) == 0);
    CHECK(portFactionRelHostile(PORT_FACTION_HOSTILE, PORT_FACTION_HOSTAGE) == 0);
    CHECK(portFactionRelHostile(PORT_FACTION_BOND, PORT_FACTION_HOSTAGE) == 0);
    CHECK(portFactionRelHostile(PORT_FACTION_HOSTAGE, PORT_FACTION_BOND) == 0);
}

static void test_resolve_vanilla(void)
{
    CHECK(portFactionResolveVanilla(1) == PORT_FACTION_HOSTILE);
    CHECK(portFactionResolveVanilla(0) == PORT_FACTION_NEUTRAL);
}

static void test_combatant(void)
{
    CHECK(portBotIsCombatant(1, 0, 0) == 1);
    CHECK(portBotIsCombatant(0, 0, 0) == 0);
    CHECK(portBotIsCombatant(1, 1, 0) == 0);
    CHECK(portBotIsCombatant(1, 0, 1) == 0);
}

static void test_engage_set_get(void)
{
    PortEngageTable t;
    portEngageReset(&t);
    portEngageSet(&t, 10, 4999, 1.0f);
    CHECK(portEngageTarget(&t, 10, 2.0f, 3.0f) == 4999);
    CHECK(portEngageTarget(&t, 10, 4.5f, 3.0f) == -1);
    CHECK(t.count == 0);
}

static void test_engage_seen_refresh(void)
{
    PortEngageTable t;
    portEngageReset(&t);
    portEngageSet(&t, 10, 4999, 1.0f);
    portEngageSeen(&t, 10, 3.5f);
    CHECK(portEngageTarget(&t, 10, 6.0f, 3.0f) == 4999);
}

static void test_engage_update_in_place(void)
{
    PortEngageTable t;
    portEngageReset(&t);
    portEngageSet(&t, 10, 4999, 1.0f);
    portEngageSet(&t, 10, 4998, 2.0f);
    CHECK(t.count == 1);
    CHECK(portEngageTarget(&t, 10, 2.5f, 3.0f) == 4998);
}

static void test_engage_full_evicts_oldest(void)
{
    PortEngageTable t;
    int i;
    portEngageReset(&t);
    for (i = 0; i < PORT_ENGAGE_MAX; i++)
    {
        portEngageSet(&t, i, 4000 + i, (float)i);
    }
    CHECK(t.count == PORT_ENGAGE_MAX);
    portEngageSet(&t, 999, 5000, 100.0f);
    CHECK(t.count == PORT_ENGAGE_MAX);
    CHECK(portEngageTarget(&t, 0, 100.0f, 1000.0f) == -1);
    CHECK(portEngageTarget(&t, 999, 100.0f, 1000.0f) == 5000);
}

static void test_engage_drop_target(void)
{
    PortEngageTable t;
    portEngageReset(&t);
    portEngageSet(&t, 10, 4999, 1.0f);
    portEngageSet(&t, 11, 4999, 1.0f);
    portEngageSet(&t, 12, 5001, 1.0f);
    portEngageDropTarget(&t, 4999);
    CHECK(t.count == 1);
    CHECK(portEngageTarget(&t, 10, 1.0f, 3.0f) == -1);
    CHECK(portEngageTarget(&t, 11, 1.0f, 3.0f) == -1);
    CHECK(portEngageTarget(&t, 12, 1.0f, 3.0f) == 5001);
}

static void test_engage_drop_guard(void)
{
    PortEngageTable t;
    portEngageReset(&t);
    portEngageSet(&t, 10, 4999, 1.0f);
    portEngageSet(&t, 11, 4999, 1.0f);
    portEngageDropGuard(&t, 10);
    CHECK(t.count == 1);
    CHECK(portEngageTarget(&t, 11, 1.0f, 3.0f) == 4999);
}

static void test_attack_note_last(void)
{
    PortAttackTable t;
    portAttackReset(&t);
    portAttackNote(&t, 20, 4999, 1.0f);
    CHECK(portAttackLast(&t, 20, 1.5f, 2.0f) == 4999);
    CHECK(portAttackLast(&t, 20, 3.5f, 2.0f) == -1);
    CHECK(portAttackLast(&t, 21, 1.5f, 2.0f) == -1);
}

static void test_attack_latest_wins(void)
{
    PortAttackTable t;
    portAttackReset(&t);
    portAttackNote(&t, 20, 4999, 1.0f);
    portAttackNote(&t, 20, 5001, 1.2f);
    CHECK(portAttackLast(&t, 20, 1.5f, 2.0f) == 5001);
    CHECK(t.count == 1);
}

static void test_attack_full_evicts_oldest(void)
{
    PortAttackTable t;
    int i;
    portAttackReset(&t);
    for (i = 0; i < PORT_ATTACK_MAX; i++)
    {
        portAttackNote(&t, i, 4000 + i, (float)i);
    }
    CHECK(t.count == PORT_ATTACK_MAX);
    portAttackNote(&t, 999, 5000, 100.0f);
    CHECK(t.count == PORT_ATTACK_MAX);
    CHECK(portAttackLast(&t, 0, 100.0f, 1000.0f) == -1);
    CHECK(portAttackLast(&t, 999, 100.0f, 1000.0f) == 5000);
}

static PortBotInputs decideInputs(float dist)
{
    PortBotInputs in;
    in.alive = 1;
    in.dt = 1.0f / 60.0f;
    in.distToBond = dist;
    in.bondSeesBot = 1;
    in.navFailed = 0;
    in.hasTarget = 0;
    in.targetDist = 0.0f;
    in.targetVisible = 0;
    in.bondX = 0.0f;
    in.bondZ = 0.0f;
    return in;
}

static void test_decide_follow_bands(void)
{
    PortBotTuning t;
    PortBotMemory m;
    PortBotInputs in;
    portBotTuningDefaults(&t);
    t.loiter = 0;   /* the follow rule alone */
    portBotMemoryReset(&m);
    CHECK(t.followDist == 250.0f);
    in = decideInputs(200.0f);
    CHECK(portBotDecide(&in, &t, &m) == PORT_BOTACT_IDLE);       /* inside personal space: no move order */
    in = decideInputs(50.0f);
    CHECK(portBotDecide(&in, &t, &m) == PORT_BOTACT_IDLE);
    in = decideInputs(260.0f);
    CHECK(portBotDecide(&in, &t, &m) == PORT_BOTACT_IDLE);       /* inside the slack: still no move */
    in = decideInputs(320.0f);
    CHECK(portBotDecide(&in, &t, &m) == PORT_BOTACT_RUN_TO_BOND);
    in = decideInputs(290.0f);
    CHECK(portBotDecide(&in, &t, &m) == PORT_BOTACT_RUN_TO_BOND);   /* hysteresis: keeps following until back near followDist */
    in = decideInputs(276.0f);
    CHECK(portBotDecide(&in, &t, &m) == PORT_BOTACT_RUN_TO_BOND);
    in = decideInputs(274.0f);                                   /* stops half the slack past followDist: she overshoots a little */
    CHECK(portBotDecide(&in, &t, &m) == PORT_BOTACT_IDLE);
    in = decideInputs(260.0f);
    CHECK(portBotDecide(&in, &t, &m) == PORT_BOTACT_IDLE);       /* and stays put until beyond the slack again */
}

static void test_decide_follow_dist_setting(void)
{
    PortBotTuning t;
    PortBotMemory m;
    PortBotInputs in;
    portBotTuningDefaults(&t);
    t.loiter = 0;
    portBotTuningSetFollowDist(&t, 400.0f);
    CHECK(t.catchupDist == 550.0f);
    portBotMemoryReset(&m);
    in = decideInputs(390.0f);
    CHECK(portBotDecide(&in, &t, &m) == PORT_BOTACT_IDLE);
    in = decideInputs(460.0f);
    CHECK(portBotDecide(&in, &t, &m) == PORT_BOTACT_RUN_TO_BOND);
}

/* Bond standing still: run n calls of the same input, return the last act */
static PortBotAct stillFor(const PortBotTuning *t, PortBotMemory *m, PortBotInputs in, int n)
{
    PortBotAct a = PORT_BOTACT_NONE;
    int i;
    for (i = 0; i < n; i++)
    {
        a = portBotDecide(&in, t, m);
    }
    return a;
}

static void test_decide_loiter_only_when_idle(void)
{
    PortBotTuning t;
    PortBotMemory m;
    PortBotInputs in;
    int i;
    portBotTuningDefaults(&t);
    portBotMemoryReset(&m);
    in = decideInputs(300.0f);
    CHECK(stillFor(&t, &m, in, 170) != PORT_BOTACT_LOITER);   /* 2.8 s still: not yet */
    CHECK(stillFor(&t, &m, in, 20) == PORT_BOTACT_LOITER);    /* past 3 s: loiter */
    in.bondX = 60.0f;                                         /* Bond moves 60 units (> 50): loiter stops at once */
    CHECK(portBotDecide(&in, &t, &m) != PORT_BOTACT_LOITER);
    CHECK(m.stillFor == 0.0f);
    portBotMemoryReset(&m);                                   /* a Bond who keeps walking never lets her loiter */
    for (i = 0; i < 600; i++)
    {
        in.bondX = (float)i * 2.0f;
        CHECK(portBotDecide(&in, &t, &m) != PORT_BOTACT_LOITER);
    }
    portBotMemoryReset(&m);                                   /* drift under 50 units does not reset the clock */
    in = decideInputs(300.0f);
    in.bondX = 10.0f;
    stillFor(&t, &m, in, 100);
    in.bondX = 30.0f;
    CHECK(stillFor(&t, &m, in, 100) == PORT_BOTACT_LOITER);
}

static void test_decide_loiter_blockers(void)
{
    PortBotTuning t;
    PortBotMemory m;
    PortBotInputs in;
    portBotTuningDefaults(&t);
    t.loiter = 0;                                             /* disabled */
    portBotMemoryReset(&m);
    in = decideInputs(320.0f);
    CHECK(stillFor(&t, &m, in, 400) == PORT_BOTACT_RUN_TO_BOND);
    t.loiter = 1;                                             /* a target beats loiter */
    portBotMemoryReset(&m);
    CHECK(stillFor(&t, &m, in, 400) == PORT_BOTACT_LOITER);
    in.hasTarget = 1;
    in.targetVisible = 1;
    in.targetDist = 500.0f;
    CHECK(portBotDecide(&in, &t, &m) == PORT_BOTACT_THROW);
    CHECK(portBotDecide(&in, &t, &m) == PORT_BOTACT_FACE_TARGET);
    in.targetDist = 1200.0f;                                  /* out of knife range is still a fight */
    CHECK(portBotDecide(&in, &t, &m) != PORT_BOTACT_LOITER);
    portBotMemoryReset(&m);                                   /* beyond the loiter band she goes back to him first */
    in = decideInputs(900.0f);
    CHECK(stillFor(&t, &m, in, 400) == PORT_BOTACT_RUN_TO_BOND);
}

static void test_decide_loiter_band(void)
{
    PortBotTuning t;
    PortBotMemory m;
    PortBotInputs in;
    int i, tele = 0;
    portBotTuningDefaults(&t);
    portBotMemoryReset(&m);
    in = decideInputs(80.0f);                                 /* inside personal space with Bond still: she strolls out */
    CHECK(stillFor(&t, &m, in, 200) == PORT_BOTACT_LOITER);
    portBotMemoryReset(&m);
    in = decideInputs(520.0f);                                /* past catchupDist (400), inside the loiter band */
    in.bondSeesBot = 0;
    stillFor(&t, &m, in, 200);                                /* until Bond has been still long enough to loiter */
    for (i = 0; i < 1000; i++)
    {
        if (portBotDecide(&in, &t, &m) == PORT_BOTACT_TELEPORT)
        {
            tele++;
        }
    }
    CHECK(tele == 0);                                         /* strolling at the band's edge never counts as left behind */
}

static void test_rng_private_and_deterministic(void)
{
    unsigned int a, b, c;
    int i;
    portBotRngSeed(&a, 33);
    portBotRngSeed(&b, 33);
    portBotRngSeed(&c, 34);
    for (i = 0; i < 100; i++)
    {
        CHECK(portBotRngNext(&a) == portBotRngNext(&b));
    }
    portBotRngSeed(&a, 33);
    CHECK(portBotRngNext(&a) != portBotRngNext(&c));          /* a different stage is a different stream */
    portBotRngSeed(&a, 0);
    CHECK(a != 0);
    for (i = 0; i < 1000; i++)
    {
        float f = portBotRngFloat(&a);
        CHECK(f >= 0.0f && f < 1.0f);
    }
}

static void test_loiter_pick_band_and_pause(void)
{
    PortBotTuning t;
    unsigned int s;
    int i;
    float minD = 1e9f, maxD = 0.0f;
    portBotTuningDefaults(&t);
    portBotRngSeed(&s, 7);
    for (i = 0; i < 2000; i++)
    {
        float turn, dist, pause;
        portBotLoiterPick(&s, t.followDist, &turn, &dist);
        CHECK(turn >= 0.0f && turn < 1.0f);
        CHECK(dist >= t.followDist && dist <= 2.0f * t.followDist);
        if (dist < minD) minD = dist;
        if (dist > maxD) maxD = dist;
        pause = portBotLoiterPause(&s, &t);
        CHECK(pause >= 2.0f && pause <= 4.0f);
    }
    CHECK(minD < 1.1f * t.followDist);                        /* the whole band is used */
    CHECK(maxD > 1.9f * t.followDist);
}

static void test_skins_table(void)
{
    int k, j;
    CHECK(portBotSkinCount() == PORT_BOT_SKIN_COUNT);
    CHECK(portBotSkinFind("natalya") >= 0);
    CHECK(portBotSkinMp(portBotSkinFind("natalya")) == 1);
    CHECK(portBotSkinFind("NATALYA") == portBotSkinFind("natalya"));
    CHECK(portBotSkinFind("random") == -1);
    CHECK(portBotSkinFind("nobody") == -1);
    CHECK(portBotSkinFind("") == -1 && portBotSkinFind(NULL) == -1);
    CHECK(portBotSkinName(-1) == NULL && portBotSkinName(portBotSkinCount()) == NULL);
    for (k = 0; k < portBotSkinCount(); k++)
    {
        CHECK(portBotSkinName(k) != NULL);
        CHECK(portBotSkinFind(portBotSkinName(k)) == k);      /* names are unique and round trip */
        CHECK(portBotSkinFindMp(portBotSkinMp(k)) == k);
        for (j = 0; portBotSkinName(k)[j] != '\0'; j++)
        {
            char c = portBotSkinName(k)[j];
            CHECK((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_');   /* friendly lowercase */
        }
    }
    CHECK(portBotSkinFind("oddjob") >= 0 && portBotSkinFind("jaws") >= 0 && portBotSkinFind("baron_samedi") >= 0);
}

static void test_decide_throw_then_cooldown(void)
{
    PortBotTuning t;
    PortBotMemory m;
    PortBotInputs in;
    int i;
    portBotTuningDefaults(&t);
    portBotMemoryReset(&m);
    in = decideInputs(100.0f);
    in.hasTarget = 1;
    in.targetVisible = 1;
    in.targetDist = 500.0f;
    CHECK(portBotDecide(&in, &t, &m) == PORT_BOTACT_THROW);
    CHECK(portBotDecide(&in, &t, &m) == PORT_BOTACT_FACE_TARGET);
    for (i = 0; i < 70; i++)
    {
        portBotDecide(&in, &t, &m);
    }
    CHECK(portBotDecide(&in, &t, &m) == PORT_BOTACT_THROW);
}

static void test_decide_leash_beats_target(void)
{
    PortBotTuning t;
    PortBotMemory m;
    PortBotInputs in;
    portBotTuningDefaults(&t);
    portBotMemoryReset(&m);
    in = decideInputs(800.0f);
    in.hasTarget = 1;
    in.targetVisible = 1;
    in.targetDist = 500.0f;
    CHECK(portBotDecide(&in, &t, &m) == PORT_BOTACT_RUN_TO_BOND);
    in.targetDist = 300.0f;   /* close enough that the leash no longer applies */
    CHECK(portBotDecide(&in, &t, &m) == PORT_BOTACT_THROW);
}

static void test_decide_out_of_range(void)
{
    PortBotTuning t;
    PortBotMemory m;
    PortBotInputs in;
    portBotTuningDefaults(&t);
    portBotMemoryReset(&m);
    in = decideInputs(100.0f);
    in.hasTarget = 1;
    in.targetVisible = 1;
    in.targetDist = 1200.0f;
    CHECK(portBotDecide(&in, &t, &m) == PORT_BOTACT_IDLE);   /* out of knife range inside personal space: the brain walks toward it */
}

static void test_decide_teleport(void)
{
    PortBotTuning t;
    PortBotMemory m;
    PortBotInputs in;
    int i;
    portBotTuningDefaults(&t);
    portBotMemoryReset(&m);
    in = decideInputs(500.0f);
    in.bondSeesBot = 0;
    for (i = 1; i < 120; i++)
    {
        CHECK(portBotDecide(&in, &t, &m) == PORT_BOTACT_RUN_TO_BOND);
    }
    CHECK(portBotDecide(&in, &t, &m) == PORT_BOTACT_TELEPORT);
}

static void test_decide_no_teleport_if_seen(void)
{
    PortBotTuning t;
    PortBotMemory m;
    PortBotInputs in;
    int i;
    int teleports = 0;
    portBotTuningDefaults(&t);
    portBotMemoryReset(&m);
    in = decideInputs(500.0f);
    in.bondSeesBot = 1;
    for (i = 0; i < 600; i++)
    {
        if (portBotDecide(&in, &t, &m) == PORT_BOTACT_TELEPORT)
        {
            teleports++;
        }
    }
    CHECK(teleports == 0);
}

static void test_decide_respawn_timer(void)
{
    PortBotTuning t;
    PortBotMemory m;
    PortBotInputs in;
    int i;
    portBotTuningDefaults(&t);
    portBotMemoryReset(&m);
    in = decideInputs(50.0f);
    in.alive = 0;
    for (i = 1; i < 300; i++)
    {
        CHECK(portBotDecide(&in, &t, &m) == PORT_BOTACT_NONE);
    }
    CHECK(portBotDecide(&in, &t, &m) == PORT_BOTACT_RESPAWN);
}

static void test_decide_respawn_timer_dt(void)
{
    PortBotTuning t;
    PortBotMemory m;
    PortBotInputs in;
    int i;
    portBotTuningDefaults(&t);
    portBotMemoryReset(&m);
    in = decideInputs(50.0f);
    in.alive = 0;
    in.dt = 1.0f / 120.0f;
    for (i = 1; i < 600; i++)
    {
        CHECK(portBotDecide(&in, &t, &m) == PORT_BOTACT_NONE);
    }
    CHECK(portBotDecide(&in, &t, &m) == PORT_BOTACT_RESPAWN);
}

int main(void)
{
    test_rel_bond_vs_hostile();
    test_rel_never();
    test_resolve_vanilla();
    test_combatant();
    test_engage_set_get();
    test_engage_seen_refresh();
    test_engage_update_in_place();
    test_engage_full_evicts_oldest();
    test_engage_drop_target();
    test_engage_drop_guard();
    test_attack_note_last();
    test_attack_latest_wins();
    test_attack_full_evicts_oldest();
    test_decide_follow_bands();
    test_decide_follow_dist_setting();
    test_decide_loiter_only_when_idle();
    test_decide_loiter_blockers();
    test_decide_loiter_band();
    test_rng_private_and_deterministic();
    test_loiter_pick_band_and_pause();
    test_skins_table();
    test_decide_throw_then_cooldown();
    test_decide_leash_beats_target();
    test_decide_out_of_range();
    test_decide_teleport();
    test_decide_no_teleport_if_seen();
    test_decide_respawn_timer();
    test_decide_respawn_timer_dt();

    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail;
}
