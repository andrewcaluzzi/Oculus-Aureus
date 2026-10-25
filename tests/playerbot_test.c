/* playerbot_test.c - unit tests for the player bot's pure logic (port_playerbotlogic.c, port_playerbotgraph.c).
 * Built and run by tests/run_logic_tests.sh. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "port_playerbotlogic.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond)                                                \
    do                                                             \
    {                                                              \
        if (cond)                                                  \
        {                                                          \
            g_pass++;                                              \
        }                                                          \
        else                                                       \
        {                                                          \
            g_fail++;                                              \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                          \
    } while (0)

#define NEAR(a, b, eps) (fabsf((a) - (b)) <= (eps))

/* a W x H grid of nodes 100 apart, 4-connected; `wall` closes column wx except row gap */
static void grid(PblGraph *g, int w, int h, int wx, int gap)
{
    int x, y;

    pblGraphAlloc(g, w * h, w * h * 4);
    for (y = 0; y < h; y++)
    {
        for (x = 0; x < w; x++)
        {
            pblGraphNode(g, (float) x * 100.0f, 0.0f, (float) y * 100.0f);
        }
    }
    for (y = 0; y < h; y++)
    {
        for (x = 0; x < w; x++)
        {
            int nb[4], n = 0, i = y * w + x;

            if (x == wx && y != gap)
            {
                pblGraphEdges(g, i, nb, 0);   /* a wall column: no links */
                continue;
            }
            if (x > 0 && !(x - 1 == wx && y != gap)) nb[n++] = i - 1;
            if (x + 1 < w && !(x + 1 == wx && y != gap)) nb[n++] = i + 1;
            if (y > 0 && !(x == wx && y - 1 != gap)) nb[n++] = i - w;
            if (y + 1 < h && !(x == wx && y + 1 != gap)) nb[n++] = i + w;
            pblGraphEdges(g, i, nb, n);
        }
    }
}

static void test_astar_straight(void)
{
    PblGraph g;
    int path[64], n;

    grid(&g, 10, 1, -1, 0);
    n = pblAstar(&g, 0, 9, path, 64);
    CHECK(n == 10);
    CHECK(path[0] == 0 && path[9] == 9);
    n = pblAstar(&g, 4, 4, path, 64);
    CHECK(n == 1 && path[0] == 4);
    pblGraphFree(&g);
}

static void test_astar_wall_gap(void)
{
    PblGraph g;
    int path[256], n, i, through = 0;

    grid(&g, 9, 9, 4, 7);           /* wall at x=4 with a gap at y=7 */
    n = pblAstar(&g, 0, 8, path, 256);   /* (0,0) -> (8,0) */
    CHECK(n > 0);
    for (i = 0; i < n; i++)
    {
        if (path[i] == 7 * 9 + 4)
        {
            through = 1;
        }
    }
    CHECK(through);                 /* must use the gap */
    CHECK(n == 1 + 8 + 7 * 2);      /* 8 across + down 7 and back up 7 */
    for (i = 1; i < n; i++)
    {   /* consecutive nodes are linked */
        int a = path[i - 1], b = path[i], e, ok = 0;

        for (e = g.start[a]; e < g.start[a + 1]; e++)
        {
            ok |= g.to[e] == b;
        }
        CHECK(ok);
    }
    pblGraphFree(&g);
}

static void test_astar_unreachable_and_bad(void)
{
    PblGraph g;
    int path[256], n, largest = 0;

    grid(&g, 9, 3, 4, 99);          /* a full wall: two halves (+ the wall nodes alone) */
    n = pblAstar(&g, 0, 8, path, 256);
    CHECK(n == 0);
    CHECK(pblComponents(&g, &largest) == 2 + 3);
    CHECK(largest == 12);
    pblGraphFree(&g);

    grid(&g, 5, 1, -1, 0);
    g.nbad[2] = 1;                  /* a closed node in a corridor */
    CHECK(pblAstar(&g, 0, 4, path, 256) == 0);
    CHECK(pblAstar(&g, 0, 2, path, 256) == 3);   /* the goal itself may be closed */
    pblGraphClearMarks(&g);
    CHECK(pblAstar(&g, 0, 4, path, 256) == 5);
    pblGraphFree(&g);
}

static void test_astar_penalty_detour(void)
{
    PblGraph g;
    int path[256], n, i, hit = 0;

    grid(&g, 5, 3, -1, 0);
    n = pblAstar(&g, 5, 9, path, 256);         /* middle row, straight */
    CHECK(n == 5);
    g.npen[7] = 10000.0f;                      /* the middle of the middle row is expensive */
    n = pblAstar(&g, 5, 9, path, 256);
    for (i = 0; i < n; i++)
    {
        hit |= path[i] == 7;
    }
    CHECK(n == 7 && !hit);
    pblGraphPenalize(&g, 5, 6, 5.0f);
    CHECK(pblEdgeCost(&g, 5, g.start[5]) >= 100.0f);
    pblGraphFree(&g);
}

/* a locked door's doorway: the bot makes the LINKS through it expensive (both ways) for one plan and takes the
 * penalty back before the next (port_playerbot.c graphApplyLocks) - the cost must come back exactly */
static void test_astar_link_penalty_undo(void)
{
    PblGraph g;
    int path[256], n, i, hit = 0, e;
    float before[64];

    grid(&g, 5, 3, -1, 0);
    for (e = 0; e < g.ne; e++)
    {
        before[e] = g.pen[e];
    }
    pblGraphPenalize(&g, 6, 7, 20000.0f);      /* the middle row's doorway 6 <-> 7 */
    pblGraphPenalize(&g, 7, 6, 20000.0f);      /* (found again from the other tile: recorded twice) */
    n = pblAstar(&g, 5, 9, path, 256);
    for (i = 1; i < n; i++)
    {
        hit |= (path[i - 1] == 6 && path[i] == 7) || (path[i - 1] == 7 && path[i] == 6);
    }
    CHECK(n == 7 && !hit);                     /* round it, through the next row */
    pblGraphPenalize(&g, 7, 6, -20000.0f);
    pblGraphPenalize(&g, 6, 7, -20000.0f);
    for (e = 0; e < g.ne; e++)
    {
        CHECK(g.pen[e] == before[e]);
    }
    CHECK(pblAstar(&g, 5, 9, path, 256) == 5);
    pblGraphFree(&g);
}

static void test_astar_truncate(void)
{
    PblGraph g;
    int path[4], n;

    grid(&g, 10, 1, -1, 0);
    n = pblAstar(&g, 0, 9, path, 4);
    CHECK(n == 4 && path[0] == 0 && path[3] == 3);   /* the first part of the route */
    pblGraphFree(&g);
}

static void test_astar_big(void)
{
    PblGraph g;
    int path[4096], n;

    grid(&g, 60, 60, 30, 59);
    n = pblAstar(&g, 0, 59, path, 4096);
    CHECK(n == 1 + 59 + 59 * 2);
    pblGraphFree(&g);
}

/* portals of a route through 100x100 grid cells: cells[] are (col,row) pairs */
static int cellPortals(const int *cells, int ncells, float *ax, float *az, float *bx, float *bz, float *cx, float *cz)
{
    int i;

    for (i = 0; i < ncells; i++)
    {
        cx[i] = 50.0f + 100.0f * (float) cells[i * 2];
        cz[i] = 50.0f + 100.0f * (float) cells[i * 2 + 1];
    }
    for (i = 0; i + 1 < ncells; i++)
    {
        int c0 = cells[i * 2], r0 = cells[i * 2 + 1], c1 = cells[i * 2 + 2], r1 = cells[i * 2 + 3];

        if (c0 == c1)
        {   /* vertical step: horizontal edge */
            float z = 100.0f * (float) (r0 > r1 ? r0 : r1);

            ax[i] = 100.0f * (float) c0; az[i] = z;
            bx[i] = 100.0f * (float) (c0 + 1); bz[i] = z;
        }
        else
        {
            float x = 100.0f * (float) (c0 > c1 ? c0 : c1);

            ax[i] = x; az[i] = 100.0f * (float) (r0 + 1);   /* deliberately the "other" order from above */
            bx[i] = x; bz[i] = 100.0f * (float) r0;
        }
    }
    return ncells - 1;
}

static void test_funnel(void)
{
    float ax[16], az[16], bx[16], bz[16], cx[16], cz[16], ox[16], oz[16];
    int n, nc;
    static const int straight[] = { 0, 0, 0, 1, 0, 2, 0, 3 };
    static const int ell[] = { 0, 0, 0, 1, 0, 2, 0, 3, 1, 3, 2, 3, 3, 3 };
    static const int ellLeft[] = { 3, 0, 3, 1, 3, 2, 3, 3, 2, 3, 1, 3, 0, 3 };   /* the mirror image: turns the other way */
    static const int zig[] = { 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3 };

    n = cellPortals(straight, 4, ax, az, bx, bz, cx, cz);
    nc = pblFunnel(ax, az, bx, bz, cx, cz, n, 50.0f, 50.0f, 50.0f, 350.0f, 0.0f, ox, oz, 8);
    CHECK(nc == 1 && NEAR(ox[0], 50.0f, 0.01f) && NEAR(oz[0], 350.0f, 0.01f));

    n = cellPortals(ell, 7, ax, az, bx, bz, cx, cz);
    nc = pblFunnel(ax, az, bx, bz, cx, cz, n, 50.0f, 50.0f, 350.0f, 350.0f, 0.0f, ox, oz, 8);
    CHECK(nc == 2);
    CHECK(NEAR(ox[0], 100.0f, 0.01f) && NEAR(oz[0], 300.0f, 0.01f));   /* the inner corner */
    CHECK(NEAR(ox[1], 350.0f, 0.01f) && NEAR(oz[1], 350.0f, 0.01f));
    nc = pblFunnel(ax, az, bx, bz, cx, cz, n, 50.0f, 50.0f, 350.0f, 350.0f, 20.0f, ox, oz, 8);
    /* kept 20 off the walls: round the corner via (80,300) and (100,320) */
    CHECK(nc == 3 && NEAR(ox[0], 80.0f, 0.01f) && NEAR(oz[0], 300.0f, 0.01f) && NEAR(ox[1], 100.0f, 0.01f) &&
          NEAR(oz[1], 320.0f, 0.01f));
    if (nc != 3)
    {
        int k;

        for (k = 0; k < nc; k++)
        {
            printf("  shrink corner %d: %.1f %.1f\n", k, ox[k], oz[k]);
        }
    }

    n = cellPortals(ellLeft, 7, ax, az, bx, bz, cx, cz);
    nc = pblFunnel(ax, az, bx, bz, cx, cz, n, 350.0f, 50.0f, 50.0f, 350.0f, 0.0f, ox, oz, 8);
    CHECK(nc == 2 && NEAR(ox[0], 300.0f, 0.01f) && NEAR(oz[0], 300.0f, 0.01f));

    n = cellPortals(zig, 6, ax, az, bx, bz, cx, cz);
    nc = pblFunnel(ax, az, bx, bz, cx, cz, n, 50.0f, 50.0f, 250.0f, 350.0f, 0.0f, ox, oz, 8);
    CHECK(nc == 1 && NEAR(ox[0], 250.0f, 0.01f));   /* the straight line fits through this staircase */
    /* hugging the right wall of the first cell: the first step's corner (100,100) is in the way */
    nc = pblFunnel(ax, az, bx, bz, cx, cz, n, 95.0f, 5.0f, 205.0f, 395.0f, 0.0f, ox, oz, 8);
    CHECK(nc >= 1 && NEAR(ox[nc - 1], 205.0f, 0.01f));
}

static void test_angles(void)
{
    /* forward = (-sin t, cos t): t = 0 faces +z, t = 90 faces -x */
    CHECK(NEAR(pblYawTo(0.0f, 1.0f), 0.0f, 0.01f));
    CHECK(NEAR(pblYawTo(-1.0f, 0.0f), 90.0f, 0.01f));
    CHECK(NEAR(pblYawTo(1.0f, 0.0f), 270.0f, 0.01f));
    CHECK(NEAR(pblYawTo(0.0f, -1.0f), 180.0f, 0.01f));
    CHECK(NEAR(pblWrap180(350.0f), -10.0f, 0.001f));
    CHECK(NEAR(pblWrap180(-190.0f), 170.0f, 0.001f));
    CHECK(NEAR(pblWrap180(180.0f), 180.0f, 0.001f));
    CHECK(NEAR(pblPitchTo(100.0f, 100.0f), 45.0f, 0.01f));
    CHECK(NEAR(pblPitchTo(-50.0f, 0.0f), -88.85f, 0.1f));
    CHECK(pblClamp(5.0f, 0.0f, 1.0f) == 1.0f);
}

static void test_plan_parse(void)
{
    static PblPlan plan;
    char err[160];
    int n;

    n = pblParsePlan("# comment\nname dam\n"
                     "goto pad 330 30\n"
                     "obj=0 auto\n"
                     "diff=1-3 destroy obj 4   # trailing comment\n"
                     "use obj 6\n"
                     "use pad 0x2710\n"
                     "goto 100 -50.5 200\n"
                     "goto obj 9 80\n"
                     "gadget bombdefuser obj 12\n"
                     "equip pp7\n"
                     "equip wppk\n"
                     "flag 0x1000 600\n"
                     "press z 5\n"
                     "walk 30 80 -20\n"
                     "exit\n",
                     &plan, err, sizeof(err));
    CHECK(n == -1);   /* pp7 is not an item name */
    CHECK(strstr(err, "line 11") != NULL);

    n = pblParsePlan("# comment\nname dam\n"
                     "goto pad 330 30\n"
                     "obj=0 auto\n"
                     "diff=1-3 destroy obj 4   # trailing comment\n"
                     "use obj 6\n"
                     "use pad 0x2710\n"
                     "goto 100 -50.5 200\n"
                     "goto obj 9 80\n"
                     "gadget bombdefuser obj 12\n"
                     "equip wppk\n"
                     "flag 0x1000 600\n"
                     "press z 5\n"
                     "walk 30 80 -20\n"
                     "exit\n",
                     &plan, err, sizeof(err));
    CHECK(n == 13);
    CHECK(strcmp(plan.name, "dam") == 0);
    CHECK(plan.s[0].op == PBL_GOTO_PAD && plan.s[0].a == 330 && NEAR(plan.s[0].r, 30.0f, 0.01f));
    CHECK(plan.s[1].op == PBL_AUTO && plan.s[1].condObj == 0);
    CHECK(plan.s[2].op == PBL_DESTROY_OBJ && plan.s[2].a == 4 && plan.s[2].diffMin == 1 && plan.s[2].diffMax == 3);
    CHECK(strcmp(plan.s[2].text, "diff=1-3 destroy obj 4") == 0);
    CHECK(plan.s[3].op == PBL_USE_OBJ && plan.s[3].a == 6);
    CHECK(plan.s[4].op == PBL_USE_PAD && plan.s[4].a == 10000);
    CHECK(plan.s[5].op == PBL_GOTO_XYZ && NEAR(plan.s[5].y, -50.5f, 0.01f) && NEAR(plan.s[5].z, 200.0f, 0.01f));
    CHECK(plan.s[6].op == PBL_COLLECT_OBJ && plan.s[6].a == 9 && plan.s[6].b == 1 && NEAR(plan.s[6].r, 80.0f, 0.01f));
    CHECK(plan.s[7].op == PBL_GADGET_OBJ && plan.s[7].a == 39 && plan.s[7].b == 12);
    CHECK(plan.s[8].op == PBL_EQUIP && plan.s[8].a == 4);
    CHECK(plan.s[9].op == PBL_FLAG && plan.s[9].a == 0x1000 && plan.s[9].b == 600);
    CHECK(plan.s[10].op == PBL_PRESS && plan.s[10].a == 0x2000 && plan.s[10].b == 5);
    CHECK(plan.s[11].op == PBL_WALK && plan.s[11].a == 30 && plan.s[11].b == 80 && plan.s[11].c == -20);
    CHECK(plan.s[12].op == PBL_EXIT);
    CHECK(plan.s[12].line == 15);

    CHECK(pblParsePlan("jump 3\n", &plan, err, sizeof(err)) == -1);
    CHECK(pblParsePlan("goto pad\n", &plan, err, sizeof(err)) == -1);
    CHECK(pblParsePlan("\n\n# nothing\n", &plan, err, sizeof(err)) == 0);
    CHECK(pblItemByName("camera") == 40 && pblItemByName("keycard") == 81 && pblItemByName("nope") == -1);
}

static void test_stuck(void)
{
    PblStuck s;
    int t;

    pblStuckReset(&s, 0, 0.0f, 0.0f, 1000.0f);
    for (t = 1; t <= 60; t++)
    {   /* making progress */
        CHECK(pblStuckFeed(&s, t, (float) t * 5.0f, 0.0f, 1000.0f - (float) t * 5.0f) == 0);
    }
    for (t = 61; t <= 60 + 91; t++)
    {
        pblStuckFeed(&s, t, 300.0f, 0.0f, 700.0f);
    }
    CHECK(s.level == 1);
    for (; t <= 60 + 241; t++)
    {
        pblStuckFeed(&s, t, 300.0f, 0.0f, 700.0f);
    }
    CHECK(s.level == 2);
    for (; t <= 60 + 721; t++)
    {
        pblStuckFeed(&s, t, 300.0f, 0.0f, 700.0f);
    }
    CHECK(s.level == 3);
    CHECK(pblStuckFeed(&s, t, 300.0f, 0.0f, 600.0f) == 0);   /* progress again */
}

static void test_combat_geometry(void)
{
    float fwd, lat;
    static PblPlan plan;
    char err[160];

    /* yaw 0 faces +z: a point 100 ahead and 30 to the side */
    pblRaySplit(0.0f, 0.0f, 0.0f, 30.0f, 100.0f, &fwd, &lat);
    CHECK(NEAR(fwd, 100.0f, 0.01f) && NEAR(lat, 30.0f, 0.01f));
    /* yaw 90 faces -x */
    pblRaySplit(0.0f, 0.0f, 90.0f, -200.0f, 10.0f, &fwd, &lat);
    CHECK(NEAR(fwd, 200.0f, 0.01f) && NEAR(lat, 10.0f, 0.01f));
    /* a bystander right in front of a target 500 away is in danger; one well off the line, or behind the shooter, is not */
    CHECK(pblInLineOfFire(0.0f, 0.0f, 0.0f, 500.0f, 20.0f, 300.0f));
    CHECK(pblInLineOfFire(0.0f, 0.0f, 0.0f, 500.0f, 40.0f, 800.0f));     /* behind the target: a miss carries on */
    CHECK(!pblInLineOfFire(0.0f, 0.0f, 0.0f, 500.0f, 200.0f, 300.0f));
    CHECK(!pblInLineOfFire(0.0f, 0.0f, 0.0f, 500.0f, 0.0f, -100.0f));
    CHECK(!pblInLineOfFire(0.0f, 0.0f, 0.0f, 500.0f, 0.0f, 1200.0f));    /* far past the target */
    /* a chr facing +z (rotation 0): a point ahead is 0 off, behind is 180, to the +x side 90 */
    CHECK(NEAR(pblOffFacing(0.0f, 0.0f, 0.0f, 0.0f, 100.0f), 0.0f, 0.01f));
    CHECK(NEAR(pblOffFacing(0.0f, 0.0f, 0.0f, 0.0f, -100.0f), 180.0f, 0.01f));
    CHECK(NEAR(pblOffFacing(0.0f, 0.0f, 0.0f, 100.0f, 0.0f), 90.0f, 0.01f));
    /* facing +x (pi/2): +x is ahead; the game's atan2 range [0, 2pi) is handled */
    CHECK(NEAR(pblOffFacing(1.5707963f, 50.0f, 50.0f, 150.0f, 50.0f), 0.0f, 0.05f));
    CHECK(NEAR(pblOffFacing(6.2831853f - 0.1f, 0.0f, 0.0f, 0.0f, 100.0f), 5.73f, 0.05f));
    /* threat order: an alarm raiser beats a nearer shooter; a shooter beats a nearer idle guard */
    CHECK(pblThreatScore(1200.0f, 0, 1, 0) < pblThreatScore(400.0f, 1, 0, 1));
    CHECK(pblThreatScore(600.0f, 1, 0, 0) < pblThreatScore(400.0f, 0, 0, 0));
    CHECK(pblThreatScore(300.0f, 0, 0, 0) < pblThreatScore(600.0f, 0, 0, 0));
    /* exposure: min(1, 300 / d) per guard that sees the spot; none sees it -> the no-shot score */
    {
        float d[3] = { 150.0f, 600.0f, 1500.0f };
        int all[3] = { 1, 1, 1 }, far1[3] = { 0, 0, 1 }, none[3] = { 0, 0, 0 };

        CHECK(NEAR(pblExposure(d, all, 3, 0.25f), 1.0f + 0.5f + 0.2f, 0.001f));
        CHECK(NEAR(pblExposure(d, far1, 3, 0.25f), 0.2f, 0.001f));     /* one far guard beats hiding */
        CHECK(NEAR(pblExposure(d, none, 3, 0.25f), 0.25f, 0.001f));
        CHECK(NEAR(pblExposure(d, all, 0, 0.25f), 0.25f, 0.001f));
        CHECK(pblExposure(d, none, 3, 0.25f) < pblExposure(d + 1, all, 1, 0.25f));   /* hiding beats one at 600 */
    }
    /* speed-strafing helpers */
    CHECK(NEAR(pblAngleEma(40.0f, 50.0f, 0.5f), 45.0f, 0.001f));
    CHECK(NEAR(fabsf(pblAngleEma(170.0f, -170.0f, 0.5f)), 180.0f, 0.001f));   /* across the wrap, not through 0 */
    CHECK(NEAR(pblAngleEma(-45.0f, -45.0f, 0.2f), -45.0f, 0.001f));
    CHECK(pblStrafeSide(1, 10.0f, -45.0f) == 1 && pblStrafeSide(-1, -10.0f, -45.0f) == -1);   /* gentle bends: keep */
    CHECK(pblStrafeSide(1, 60.0f, -45.0f) == -1);    /* C-right swings motion to -yaw: a +yaw turn wants C-left */
    CHECK(pblStrafeSide(-1, -60.0f, -45.0f) == 1);
    CHECK(pblStrafeSide(-1, 60.0f, 45.0f) == 1);     /* the other calibration */
    /* the plan steps that go with them */
    CHECK(pblParsePlan("quiet 1\nkey 0x2\nkey 12\n", &plan, err, sizeof(err)) == 3);
    CHECK(plan.s[0].op == PBL_QUIET && plan.s[0].a == 1);
    CHECK(plan.s[1].op == PBL_KEY && plan.s[1].a == 2 && plan.s[2].a == 12);
    CHECK(pblParsePlan("rush 1\navoid pad 22\n", &plan, err, sizeof(err)) == 2);
    CHECK(plan.s[0].op == PBL_RUSH && plan.s[0].a == 1 && plan.s[1].op == PBL_AVOID && plan.s[1].a == 22);
    CHECK(pblParsePlan("avoid 22\n", &plan, err, sizeof(err)) == -1);
    CHECK(pblParsePlan("avoid pad -1\n", &plan, err, sizeof(err)) == 1 && plan.s[0].a == -1);
    CHECK(pblParsePlan("key 0\n", &plan, err, sizeof(err)) == -1);
    CHECK(pblParsePlan("key\n", &plan, err, sizeof(err)) == -1);
    CHECK(pblParsePlan("equip fist\nequip unarmed\n", &plan, err, sizeof(err)) == 2);
    CHECK(plan.s[0].op == PBL_EQUIP && plan.s[0].a == 1 && plan.s[1].a == 0);
    CHECK(pblParsePlan("spare ai 0x419\nspare ai 0\n", &plan, err, sizeof(err)) == 2);
    CHECK(plan.s[0].op == PBL_SPARE && plan.s[0].a == 0x419 && plan.s[1].a == 0);
    CHECK(pblParsePlan("spare 0x419\n", &plan, err, sizeof(err)) == -1);
    /* the aim-progress weight: a guard whose aim is 90% built beats a nearer one that has just started */
    CHECK(NEAR(pblThreatAimScore(1000.0f, 0.0f), 1000.0f, 0.01f));
    CHECK(NEAR(pblThreatAimScore(1000.0f, 0.5f), 700.0f, 0.01f));
    CHECK(NEAR(pblThreatAimScore(1000.0f, 3.0f), 400.0f, 0.01f));     /* clamped to 1 */
    CHECK(NEAR(pblThreatAimScore(1000.0f, -1.0f), 1000.0f, 0.01f));
    CHECK(pblThreatAimScore(1000.0f, 0.9f) < pblThreatAimScore(700.0f, 0.0f));
    CHECK(NEAR(pblThreatAimScore(-100.0f, 1.0f), -100.0f, 0.01f));    /* raisers stay first */
    CHECK(pblParsePlan("hunt\nhunt ai 0x403\n", &plan, err, sizeof(err)) == 2);
    CHECK(plan.s[0].op == PBL_HUNT && plan.s[0].a == 0);
    CHECK(plan.s[1].op == PBL_HUNT && plan.s[1].a == 1 && plan.s[1].b == 0x403);
    CHECK(pblParsePlan("hunt 0x403\n", &plan, err, sizeof(err)) == -1);
    CHECK(pblParsePlan("escort ai 0x408 350\nescort ai 0x408\nescort ai 0\n", &plan, err, sizeof(err)) == 3);
    CHECK(plan.s[0].op == PBL_ESCORT && plan.s[0].a == 0x408 && NEAR(plan.s[0].r, 350.0f, 0.01f));
    CHECK(NEAR(plan.s[1].r, 400.0f, 0.01f) && plan.s[2].a == 0);
    CHECK(pblParsePlan("escort 0x408\n", &plan, err, sizeof(err)) == -1);
    CHECK(pblParsePlan("hunt ai\n", &plan, err, sizeof(err)) == -1);
}

int main(void)
{
    test_combat_geometry();
    test_astar_straight();
    test_astar_wall_gap();
    test_astar_unreachable_and_bad();
    test_astar_penalty_detour();
    test_astar_link_penalty_undo();
    test_astar_truncate();
    test_astar_big();
    test_funnel();
    test_angles();
    test_plan_parse();
    test_stuck();
    printf("playerbot_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail != 0;
}
