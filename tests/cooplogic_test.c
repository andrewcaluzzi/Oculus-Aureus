/* cooplogic_test.c - unit tests for the pure co-op rules in port_cooplogic.c. */
#include <stdio.h>
#include "port_cooplogic.h"

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

static void test_nearest_basic(void)
{
    float x[2] = {0.0f, 100.0f}, y[2] = {0.0f, 0.0f}, z[2] = {0.0f, 0.0f};
    int dead[2] = {0, 0}, has[2] = {1, 1};

    CHECK(portCoopPickNearest(2, x, y, z, dead, has, 10.0f, 0.0f, 0.0f) == 0);
    CHECK(portCoopPickNearest(2, x, y, z, dead, has, 90.0f, 0.0f, 0.0f) == 1);
    CHECK(portCoopPickNearest(2, x, y, z, dead, has, 90.0f, 500.0f, 0.0f) == 1);   /* y counts */
}

static void test_nearest_tie_lower_index(void)
{
    float x[2] = {-50.0f, 50.0f}, y[2] = {0.0f, 0.0f}, z[2] = {0.0f, 0.0f};
    int dead[2] = {0, 0}, has[2] = {1, 1};
    float px[2] = {50.0f, -50.0f};

    CHECK(portCoopPickNearest(2, x, y, z, dead, has, 0.0f, 0.0f, 0.0f) == 0);      /* equidistant */
    CHECK(portCoopPickNearest(2, px, y, z, dead, has, 0.0f, 0.0f, 0.0f) == 0);     /* swapped positions: still index 0 */
    CHECK(portCoopPickNearest(2, x, y, z, dead, has, 0.0f, 0.0f, 0.0f) ==
          portCoopPickNearest(2, x, y, z, dead, has, 0.0f, 0.0f, 0.0f));           /* pure: same answer twice */
}

static void test_nearest_skips_dead(void)
{
    float x[2] = {0.0f, 100.0f}, y[2] = {0.0f, 0.0f}, z[2] = {0.0f, 0.0f};
    int dead[2] = {1, 0}, has[2] = {1, 1};

    CHECK(portCoopPickNearest(2, x, y, z, dead, has, 0.0f, 0.0f, 0.0f) == 1);      /* P0 is closer but dead */
    dead[0] = 0;
    dead[1] = 1;
    CHECK(portCoopPickNearest(2, x, y, z, dead, has, 100.0f, 0.0f, 0.0f) == 0);
}

static void test_nearest_all_dead_and_absent(void)
{
    float x[2] = {0.0f, 100.0f}, y[2] = {0.0f, 0.0f}, z[2] = {0.0f, 0.0f};
    int dead[2] = {1, 1}, has[2] = {1, 1};

    CHECK(portCoopPickNearest(2, x, y, z, dead, has, 100.0f, 0.0f, 0.0f) == 0);    /* all dead: lowest index */
    has[0] = 0;
    CHECK(portCoopPickNearest(2, x, y, z, dead, has, 0.0f, 0.0f, 0.0f) == 1);      /* absent player is skipped */
    dead[1] = 0;
    CHECK(portCoopPickNearest(2, x, y, z, dead, has, 0.0f, 0.0f, 0.0f) == 1);
    has[1] = 0;
    CHECK(portCoopPickNearest(2, x, y, z, dead, has, 0.0f, 0.0f, 0.0f) == 0);      /* nobody: 0 */
}

static void test_all_dead(void)
{
    int dead[2] = {1, 0}, has[2] = {1, 1};

    CHECK(portCoopAllDeadOf(2, dead, has) == 0);
    dead[1] = 1;
    CHECK(portCoopAllDeadOf(2, dead, has) == 1);
    dead[0] = 0;
    CHECK(portCoopAllDeadOf(2, dead, has) == 0);
    has[0] = 0;
    CHECK(portCoopAllDeadOf(2, dead, has) == 1);                                      /* only P1 present, dead */
    has[1] = 0;
    CHECK(portCoopAllDeadOf(2, dead, has) == 0);                                      /* nobody present: not "all dead" */
}

static void test_respawn_partner(void)
{
    int dead[2] = {1, 0}, has[2] = {1, 1};

    CHECK(portCoopRespawnPartner(2, dead, has, 0) == 1);                              /* P1 respawns next to P2 */
    CHECK(portCoopRespawnPartner(2, dead, has, 1) == -1);                             /* P2 is the only one alive: it has no partner */
    dead[1] = 1;
    CHECK(portCoopRespawnPartner(2, dead, has, 0) == -1);                             /* partner dead too */
    dead[1] = 0;
    has[1] = 0;
    CHECK(portCoopRespawnPartner(2, dead, has, 0) == -1);                             /* partner absent */
    {
        int d3[3] = {1, 1, 0}, h3[3] = {1, 1, 1};

        CHECK(portCoopRespawnPartner(3, d3, h3, 0) == 2);                             /* lowest living index, deterministic */
    }
}

static void test_respawn_timing(void)
{
    CHECK(PORT_COOP_RESPAWN_FIELDS == 600);
    CHECK(portCoopRespawnFieldsLeft(100, 100, 600) == 600);
    CHECK(portCoopRespawnFieldsLeft(100, 399, 600) == 301);
    CHECK(portCoopRespawnFieldsLeft(100, 700, 600) == 0);                             /* exactly 600 fields later */
    CHECK(portCoopRespawnFieldsLeft(100, 5000, 600) == 0);                            /* never negative */
    CHECK(portCoopRespawnSecs(600) == 10);
    CHECK(portCoopRespawnSecs(541) == 10);
    CHECK(portCoopRespawnSecs(540) == 9);
    CHECK(portCoopRespawnSecs(1) == 1);
    CHECK(portCoopRespawnSecs(0) == 0);
    CHECK(portCoopRespawnSecs(-5) == 0);
    CHECK(portCoopRespawnDue(0, 0) == 1);
    CHECK(portCoopRespawnDue(1, 0) == 0);
    CHECK(portCoopRespawnDue(0, 1) == 0);                                             /* all dead: no respawn */
}

static void test_respawn_spots(void)
{
    float ox = -1.0f, oz = -1.0f;

    /* partner at (1000, 2000) facing +z (fx 0, fz 1): "right" is +x (the game's facing is (-sin th, cos th)) */
    CHECK(portCoopRespawnSpot(0, 1000.0f, 2000.0f, 0.0f, 1.0f, &ox, &oz) == 1);
    CHECK(ox == 1090.0f && oz == 2000.0f);
    CHECK(portCoopRespawnSpot(1, 1000.0f, 2000.0f, 0.0f, 1.0f, &ox, &oz) == 1);
    CHECK(ox == 910.0f && oz == 2000.0f);
    CHECK(portCoopRespawnSpot(4, 1000.0f, 2000.0f, 0.0f, 1.0f, &ox, &oz) == 1);      /* behind: -z */
    CHECK(ox == 1000.0f && oz == 1920.0f);
    CHECK(portCoopRespawnSpot(5, 1000.0f, 2000.0f, 0.0f, 1.0f, &ox, &oz) == 1);      /* ahead: +z */
    CHECK(ox == 1000.0f && oz == 2080.0f);
    /* facing +x (fx 1, fz 0): right is -z */
    CHECK(portCoopRespawnSpot(0, 0.0f, 0.0f, 1.0f, 0.0f, &ox, &oz) == 1);
    CHECK(ox == 0.0f && oz == -90.0f);
    CHECK(portCoopRespawnSpot(6, 0.0f, 0.0f, 1.0f, 0.0f, &ox, &oz) == 0);            /* out of range */
    CHECK(portCoopRespawnSpot(-1, 0.0f, 0.0f, 1.0f, 0.0f, &ox, &oz) == 0);
}

static void test_drops(void)
{
    float ox = -1.0f, oz = -1.0f;
    int k;

    /* ring offsets: the spot itself, then 6 at 45 units, then 6 at 90; nothing past 12 */
    CHECK(portCoopDropOffset(0, &ox, &oz) == 1 && ox == 0.0f && oz == 0.0f);
    CHECK(portCoopDropOffset(1, &ox, &oz) == 1 && ox == 45.0f && oz == 0.0f);
    CHECK(portCoopDropOffset(4, &ox, &oz) == 1 && ox == -45.0f && oz == 0.0f);
    CHECK(portCoopDropOffset(7, &ox, &oz) == 1 && ox == 90.0f && oz == 0.0f);
    CHECK(portCoopDropOffset(12, &ox, &oz) == 1);
    CHECK(portCoopDropOffset(13, &ox, &oz) == 0);
    CHECK(portCoopDropOffset(-1, &ox, &oz) == 0);
    for (k = 1; k <= 12; k++) {   /* every ring spot is a distinct place */
        float ax, az, bx, bz;
        int j;

        portCoopDropOffset(k, &ax, &az);
        for (j = 0; j < k; j++) {
            portCoopDropOffset(j, &bx, &bz);
            CHECK(!(ax == bx && az == bz));
        }
    }
    /* the spot test: a tile under it, the spot inside it, feet near the floor, the floor above the level's lowest room */
    CHECK(portCoopDropSpotSafe(1, 1, 100.0f, 100.0f, 1, -500.0f, 0) == 1);
    CHECK(portCoopDropSpotSafe(0, 0, 100.0f, 100.0f, 1, -500.0f, 0) == 0);           /* no stand tile: no floor */
    CHECK(portCoopDropSpotSafe(1, 0, 100.0f, 100.0f, 1, -500.0f, 0) == 0);           /* off the tile (out of the level) */
    CHECK(portCoopDropSpotSafe(1, 1, 400.0f, 100.0f, 1, -500.0f, 0) == 0);           /* mid-air above a drop */
    CHECK(portCoopDropSpotSafe(1, 1, -40.0f, 100.0f, 1, -500.0f, 0) == 1);           /* within the 150 band */
    CHECK(portCoopDropSpotSafe(1, 1, -100.0f, 100.0f, 1, -500.0f, 0) == 0);          /* 200 below the floor: outside it */
    CHECK(portCoopDropSpotSafe(1, 1, -400.0f, -400.0f, 1, -300.0f, 0) == 0);         /* the floor is below the level's lowest room */
    CHECK(portCoopDropSpotSafe(1, 1, -400.0f, -400.0f, 0, 0.0f, 0) == 1);            /* no level floor known: not a reason */
    CHECK(portCoopDropSpotSafe(1, 1, 100.0f, 100.0f, 1, -500.0f, 1) == 0);           /* the test hook forces unsafe */
    /* a dropped gun's rounds: the hand's magazine, else a full one, never more than the pool */
    CHECK(portCoopDropMagazine(1, 17, 30, 80) == 17);
    CHECK(portCoopDropMagazine(0, 17, 30, 80) == 30);
    CHECK(portCoopDropMagazine(0, 0, 30, 12) == 12);
    CHECK(portCoopDropMagazine(1, 25, 30, 10) == 10);
    CHECK(portCoopDropMagazine(0, 0, 30, -5) == 0);
    CHECK(portCoopDropMagazine(1, -3, 30, 50) == 0);
}

static void test_escort(void)
{
    float d[2];
    int dead[2] = {0, 0}, has[2] = {1, 1};

    d[0] = 100.0f; d[1] = 90.0f;
    CHECK(portCoopEscortPick(2, d, dead, has, -1, 0, 0.7f, 180) == 1);            /* nobody followed yet: the nearest */
    CHECK(portCoopEscortPick(2, d, dead, has, 0, 1000, 0.7f, 180) == 0);          /* 10% closer is not enough */
    d[1] = 60.0f;
    CHECK(portCoopEscortPick(2, d, dead, has, 0, 1000, 0.7f, 180) == 1);          /* 40% closer and long enough: switch */
    CHECK(portCoopEscortPick(2, d, dead, has, 0, 100, 0.7f, 180) == 0);           /* ... but not before 3 s */
    d[0] = 50.0f; d[1] = 50.0f;
    CHECK(portCoopEscortPick(2, d, dead, has, 1, 1000, 0.7f, 180) == 1);          /* equal: keep */
    dead[0] = 1;
    CHECK(portCoopEscortPick(2, d, dead, has, 0, 0, 0.7f, 180) == 1);             /* the followed Bond is dead: at once */
    dead[1] = 1;
    CHECK(portCoopEscortPick(2, d, dead, has, 0, 0, 0.7f, 180) == 0);             /* nobody lives: first present */
}

int main(void)
{
    test_escort();
    test_nearest_basic();
    test_nearest_tie_lower_index();
    test_nearest_skips_dead();
    test_nearest_all_dead_and_absent();
    test_all_dead();
    test_respawn_partner();
    test_respawn_timing();
    test_respawn_spots();
    test_drops();

    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail;
}
