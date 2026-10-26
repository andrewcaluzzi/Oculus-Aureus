#define _GNU_SOURCE   /* ucontext REG_EFL for the write trap below */
/* port_roomvis.c - room visibility: the old-coupling switches and test hooks (netplay job 7). Everything is off unless its env var is set, and none of the
 * hooks feeds the sim except where it is meant to (the teleport and the door command move things like the existing GE_COOP_MOVETOCHR hook).
 *
 * Switches for the negative controls of net_lockstep.sh:
 *   GE_NET_TEST_SIMVISOLD=1   phase 22: the sim reads the room lists / flags the last (render) walk left, and that walk runs last (the pre-job-7 coupling)
 *   GE_NET_TEST_DOORVTXOLD=1  phase 23: doorInit does not keep the clipped door vertices at once (the late copy out of the per-frame vertex pool, the playtest-7 desync)
 *   GE_NET_TEST_MONITORCOLOLD=1  phase 26: a shot at a monitor walks the display list its last renderer left (the seed-32 soak desync)
 *   GE_NET_RENDERWALK=1       the render walk runs even when the window is the canonical view (it must then give the sim walk's sets)
 * Logging and test hooks (net_roomwalk.sh uses them):
 *   GE_ROOMLOG=<path>         one line per tick and Bond, "ge: rooms:": the walk's start room against the room of the Bond's own floor tile, the rooms of its
 *                             canonical SIM walk and the rooms its view actually DRAWS (bgLevelRender), both sorted; "ge: note:" lines from the game code
 *                             (a hit test that finds a room without bounds); every 60 ticks "ge: diverge:" (how often a sim read would have differed from the render walk)
 *   GE_ROOMTEST_TP=<tick>:<player>:<x>:<y>:<z>:<yawdeg>[,...]   at that sim tick the Bond is put on the floor below (x, y, z) facing yawdeg; the form
 *                             <tick>:<player>:@<chrnum>:<yawdeg> puts it beside that character (works in solo, local co-op and netplay)
 *   GE_ROOMTEST_DOORS=<tick>:o|c[,...]  at that tick every door is told to open / close
 *   GE_ROOMTEST_SHOT=<tick>[,...]       a screenshot (GE_SHOT's BMP writer) at the first frame drawn after that sim tick
 *   GE_ROOMTEST_NOINTRO=1               solo reference runs: skip the intro cinematic (its camera has its own room)
 *   GE_ROOMTEST_POS=<every>, _CHRS=1, _PORTALS=<tick>   log both Bonds' positions, every chr at tick 5, every portal's state at that tick (to pick waypoints / debug)
 *   GE_ROOMTEST_TRAP=<hex address>      Linux, 32-bit: log the call stack of the first write to that address (who overwrites a model's vertices?)
 * The fingerprint lines (teleports, doors, screenshots) go through portCoopFpLog (GE_COOP_FPFILE); the room log has its own file, because the draw lines differ between
 * machines by design. */
#ifndef TARGET_N64

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ultra64.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include "game/player.h"
#include "game/bondview.h"
#include "game/bg.h"
#include "game/stan.h"
#include "game/chr.h"
#include "game/chrai.h"
#include "game/propobj.h"

extern int portAllPlayerCount(void);
extern void portCoopFpLog(const char *fmt, ...);
extern int ge_shot_force;
extern f32 bondviewYPositionRelated(struct StandTile *arg0, f32 arg1, f32 arg2);
extern s32 bgSimRoomsForPlayer(s32 pl, s32 *rooms, s32 max);
extern s32 bgSimVisibleRoomsForPlayer(s32 pl, s32 *rooms, s32 max);
extern s32 portStageRunning(void);

static unsigned sTick;   /* sim ticks since the stage load (0 until the first tick has run) */
static int sInit;
static const char *sTp, *sShot;
static int sPosEvery, sChrs;
static FILE *sRoomLog;
static unsigned sLogged[4];   /* the last tick a Bond's line was written: one line per tick and Bond */

/* GE_NET_TEST_SIMVISOLD=1 (test, negative control of net_lockstep phase 22): the pre-job-7 coupling, the sim reads the live lists the last
 * (render) walk left, and that walk runs last. GE_NET_RENDERWALK=1 (test): the render walk also runs when the window is the canonical view. */
int portSimVisOld(void)
{
    static int v = -1;

    if (v < 0) {
        const char *e = getenv("GE_NET_TEST_SIMVISOLD");

        v = (e != NULL && e[0] != '\0' && e[0] != '0');
    }
    return v;
}

/* GE_NET_TEST_DOORVTXOLD=1 (test, negative control of net_lockstep phase 23): doorInit no longer keeps the clipped vertices at once, the first door tick copies them out of the
 * per-frame vertex pool as before (garbage on one machine of two: the playtest-7 desync). */
int portDoorVtxOld(void)
{
    static int v = -1;

    if (v < 0) {
        const char *e = getenv("GE_NET_TEST_DOORVTXOLD");

        v = (e != NULL && e[0] != '\0' && e[0] != '0');
    }
    return v;
}

/* GE_NET_TEST_MONITORCOLOLD=1 (test, negative control of net_lockstep phase 26): a shot at a monitor screen walks the display list the last pass that rendered it left
 * (propobj.c sPortStaticMonitorCol off), as before the soak fix: with the other Bond's watch open the result depends on which Bond is local. */
int portMonitorColOld(void)
{
    static int v = -1;

    if (v < 0) {
        const char *e = getenv("GE_NET_TEST_MONITORCOLOLD");

        v = (e != NULL && e[0] != '\0' && e[0] != '0');
    }
    return v;
}

int portRenderWalkAlways(void)
{
    static int v = -1;

    if (v < 0) {
        const char *e = getenv("GE_NET_RENDERWALK");

        v = (e != NULL && e[0] != '\0' && e[0] != '0');
    }
    return v;
}

void portRoomVisReset(void)
{
    sTick = 0;
    memset(sLogged, 0, sizeof(sLogged));
}

int portRoomLogOn(void)
{
    if (!sInit) {
        const char *e;

        sInit = 1;
        sTp = getenv("GE_ROOMTEST_TP");
        sShot = getenv("GE_ROOMTEST_SHOT");
        e = getenv("GE_ROOMTEST_POS");
        sPosEvery = (e != NULL) ? atoi(e) : 0;
        sChrs = getenv("GE_ROOMTEST_CHRS") != NULL;
        e = getenv("GE_ROOMLOG");
        if (e != NULL && e[0] != '\0') {
            sRoomLog = fopen(e, "w");   /* the draw lines differ between machines by design (each draws its own window), so they never go in the fingerprint file */
        }
    }
    return sRoomLog != NULL;
}

static int cmpS32(const void *a, const void *b)
{
    return *(const s32 *) a - *(const s32 *) b;
}

static void putList(char *dst, int cap, s32 *l, int n)
{
    int i, o = 0;

    qsort(l, (size_t) n, sizeof(s32), cmpS32);
    dst[0] = '\0';
    for (i = 0; i < n && o < cap - 8; i++) {
        o += snprintf(dst + o, (size_t) (cap - o), "%s%d", i ? "," : "", (int) l[i]);
    }
}

/* GE_ROOMLOG notes from the game code (the hit test that finds a room without bounds, ...): one line, tagged with the sim tick */
void portRoomVisNote(const char *fmt, ...)
{
    va_list ap;

    if (!portRoomLogOn()) {
        return;
    }
    fprintf(sRoomLog, "ge: note: tick %u ", sTick);
    va_start(ap, fmt);
    vfprintf(sRoomLog, fmt, ap);
    va_end(ap);
    fflush(sRoomLog);
}

/* Called from bgLevelRender (the world draw of a pass), where bgRoomVisibilityRelated has already run for g_CurrentPlayer: the
 * rooms the view is about to draw (draw[]) against its canonical sim walk. */
static s32 sDrawn[4][64];   /* the rooms each view drew last (test.rooms_drawn, tests: the generated level's culling check) */
static s32 sDrawnN[4];

s32 portRoomVisDrawn(s32 pl, s32 *out, s32 max)
{
    s32 i;

    if (pl < 0 || pl > 3) {
        return 0;
    }
    for (i = 0; i < sDrawnN[pl] && i < max; i++) {
        out[i] = sDrawn[pl][i];
    }
    return i;
}

void portRoomVisDrawLog(s32 pl, s32 startroom, s32 *draw, s32 ndraw)
{
    s32 sim[256], vis[64];
    char a[1024], b[1024], c[256];
    int ns, nv;
    struct player *p;
    s32 tileroom = -1;

    if (pl >= 0 && pl <= 3) {
        s32 i;

        for (i = 0; i < ndraw && i < 64; i++) {
            sDrawn[pl][i] = draw[i];
        }
        sDrawnN[pl] = i;
    }
    if (!portRoomLogOn() || pl < 0 || pl > 3 || sLogged[pl] == sTick + 1) {
        return;
    }
    sLogged[pl] = sTick + 1;
    p = g_playerPointers[pl];
    if (p != NULL && p->prop != NULL && p->prop->stan != NULL) {
        tileroom = getTileRoom((StandTile *) p->prop->stan);
    }
    ns = bgSimRoomsForPlayer(pl, sim, 256);
    nv = bgSimVisibleRoomsForPlayer(pl, vis, 64);
    putList(a, sizeof(a), sim, ns);
    putList(c, sizeof(c), vis, nv);
    putList(b, sizeof(b), draw, ndraw);
    fprintf(sRoomLog, "ge: rooms: tick %u p%d start %d tile %d sim %d: %s draw %d: %s vis %d: %s\n", sTick, pl, (int) startroom, (int) tileroom, ns, a, ndraw, b, nv, c);
    fflush(sRoomLog);
}

static void teleport(int pl, float x, float y, float z, float yaw)
{
    struct player *p = g_playerPointers[pl];
    StandTile *tile;
    coord3d to;
    f32 h = 0.0f;
    s32 saved;

    if (p == NULL || p->prop == NULL) {
        return;
    }
    to.x = x;
    to.y = y;
    to.z = z;
    tile = stanFindTileBelowPos(&to, NULL, &h);
    if (tile == NULL) {
        portCoopFpLog("ge: roomtest: no floor below %.0f %.0f %.0f for player %d at tick %u\n", x, y, z, pl, sTick);
        return;
    }
    saved = get_cur_playernum();
    set_cur_player(pl);
    to.y = g_CurrentPlayer->eyeheight + h;
    g_CurrentPlayer->field_70 = h;
    g_CurrentPlayer->stanHeight = h;
    change_player_pos_to_target(&g_CurrentPlayer->field_488, &to, tile);
    g_CurrentPlayer->prop->pos.x = g_CurrentPlayer->bondprevpos.x = to.x;
    g_CurrentPlayer->prop->pos.y = g_CurrentPlayer->bondprevpos.y = to.y;
    g_CurrentPlayer->prop->pos.z = g_CurrentPlayer->bondprevpos.z = to.z;
    g_CurrentPlayer->prop->stan = tile;
    g_CurrentPlayer->vv_theta = yaw;
    g_CurrentPlayer->vv_verta = 0.0f;
    set_cur_player(saved);
    portCoopFpLog("ge: roomtest: tick %u player %d moved to %.0f %.0f %.0f yaw %.0f (floor %.0f room %d)\n", sTick, pl, to.x, to.y, to.z, yaw, h, (int) getTileRoom(tile));
}

#ifdef __linux__
/* GE_ROOMTEST_TRAP=<hex address> (Linux, debugging): the page is made read-only at sim tick 1 and the first write into [address, address + 16) is logged with its
 * call stack (raw return addresses: resolve with addr2line on the exe), then the page is writable again. Who overwrites a model's vertices? */
#include <signal.h>
#include <ucontext.h>
#include <execinfo.h>
#include <sys/mman.h>
#include <unistd.h>

static unsigned long sTrapAddr, sTrapPage;

static void trapHandler(int sig, siginfo_t *si, void *ctx)
{
    void *bt[32];
    int n, i;
    unsigned long a = (unsigned long) si->si_addr;
    ucontext_t *uc = (ucontext_t *) ctx;
    FILE *out = sRoomLog != NULL ? sRoomLog : stderr;

    (void) sig;
    if (a >= sTrapAddr && a < sTrapAddr + 16) {
        n = backtrace(bt, 32);
        fprintf(out, "ge: trap: write at %lx, stack:", a);
        for (i = 0; i < n; i++) {
            fprintf(out, " %p", bt[i]);
        }
        fprintf(out, "%c", 10);
        fflush(out);
    }
    mprotect((void *) sTrapPage, 4096, PROT_READ | PROT_WRITE);
    uc->uc_mcontext.gregs[REG_EFL] |= 0x100;   /* single-step the faulting write, then trapStep protects the page again */
}

static void trapStep(int sig, siginfo_t *si, void *ctx)
{
    ucontext_t *uc = (ucontext_t *) ctx;

    (void) sig;
    (void) si;
    uc->uc_mcontext.gregs[REG_EFL] &= ~0x100;
    mprotect((void *) sTrapPage, 4096, PROT_READ);
}

static void trapInstall(void)
{
    const char *e = getenv("GE_ROOMTEST_TRAP");
    struct sigaction sa;

    if (e == NULL) {
        return;
    }
    sTrapAddr = strtoul(e, NULL, 16);
    sTrapPage = sTrapAddr & ~4095UL;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = trapHandler;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, NULL);
    sa.sa_sigaction = trapStep;
    sigaction(SIGTRAP, &sa, NULL);
    mprotect((void *) sTrapPage, 4096, PROT_READ);
}
#endif

/* once per sim tick, after the Bonds' view-move ticks (boss.c) */
void portRoomVisSimTick(void)
{
    const char *s;
    int i;

    portRoomLogOn();
    sTick++;
#ifdef __linux__
    if (sTick == 1) {
        trapInstall();
    }
#endif
    if (sTick == 1 && getenv("GE_ROOMTEST_NOINTRO") != NULL && g_playerPointers[0] != NULL) {
        /* the solo reference run of net_roomwalk.sh: no intro cinematic (its camera has its own tile and position, so the walk would start
         * from the camera's room), straight into the first-person view with the stage's fog loaded, as a co-op stage starts */
        extern void fogLoadLevelEnvironment(s32 level_id, s32 arg1);
        extern s32 bossGetStageNum(void);
        extern void bondviewSetCameraMode(s32 mode);

        if (g_CameraMode == CAMERAMODE_INTRO || g_CameraMode == CAMERAMODE_FADESWIRL || g_CameraMode == CAMERAMODE_SWIRL) {
            set_cur_player(0);
            bondviewSetCameraMode(CAMERAMODE_FP);
            fogLoadLevelEnvironment(bossGetStageNum(), 0);
        }
    }
    for (s = sTp; s != NULL && *s != '\0';) {
        unsigned t = 0;
        int pl = 0, used = 0, cn = -1;
        float x = 0, y = 0, z = 0, yaw = 0;

        if (sscanf(s, "%u:%d:@%d:%f%n", &t, &pl, &cn, &yaw, &used) >= 4 && used > 0) {   /* tick:player:@chrnum:yaw: on the floor beside that chr */
            ChrRecord *c = chrFindByLiteralId(cn);

            if (c != NULL && c->prop != NULL) {
                x = c->prop->pos.x;
                y = c->prop->pos.y + 100.0f;
                z = c->prop->pos.z;
            } else {
                x = y = z = 0.0f;
                cn = -2;
            }
        } else if (sscanf(s, "%u:%d:%f:%f:%f:%f%n", &t, &pl, &x, &y, &z, &yaw, &used) < 6 || used <= 0) {
            break;
        }
        if (t == sTick && pl >= 0 && pl < 4 && cn != -2) {
            teleport(pl, x, y, z, yaw);
        }
        s += used;
        if (*s == ',') {
            s++;
        }
    }
    for (s = sShot; s != NULL && *s != '\0';) {
        unsigned t = (unsigned) strtoul(s, (char **) &s, 10);

        if (t == sTick) {
            ge_shot_force = 1;
            portCoopFpLog("ge: roomtest: screenshot after tick %u\n", sTick);
        }
        if (*s == ',') {
            s++;
        } else {
            break;
        }
    }
    if (sRoomLog != NULL && sTick % 60 == 0) {   /* how often a sim read would have returned another answer from the render walk (the old coupling) */
        extern u32 g_PortDiv[8];

        fprintf(sRoomLog, "ge: diverge: tick %u room-rendered reads %u would differ %u (render walk against the sim walk), drawn-list reads %u (%u with a render walk) would differ %u" "%c", sTick, g_PortDiv[0], g_PortDiv[1], g_PortDiv[4], g_PortDiv[2], g_PortDiv[3], 10);
        fflush(sRoomLog);
    }
    if (sPosEvery > 0 && sTick % (unsigned) sPosEvery == 0) {
        for (i = 0; i < 4 && i < portAllPlayerCount(); i++) {
            struct player *p = g_playerPointers[i];

            if (p != NULL && p->prop != NULL) {
                portCoopFpLog("ge: roomtest: pos tick %u p%d %.0f %.0f %.0f yaw %.0f room %d\n", sTick, i, p->prop->pos.x, p->prop->pos.y, p->prop->pos.z, p->vv_theta,
                              p->prop->stan != NULL ? (int) getTileRoom((StandTile *) p->prop->stan) : -1);
            }
        }
    }
    {   /* GE_ROOMTEST_PORTALS=<tick>: one line per portal that is not plainly open: the shared bit, each Bond's own view, the last event */
        static int pt = -2;
        extern u8 g_PortPortalView[4][PORTMAX];
        extern u8 g_PortPortalEvent[PORTMAX];

        if (pt == -2) {
            const char *e = getenv("GE_ROOMTEST_PORTALS");

            pt = (e != NULL) ? atoi(e) : -1;
        }
        if (pt > 0 && sTick == (unsigned) pt) {
            int k;

            for (k = 0; k < PORTMAX && g_BgPortals[k].offset_portal != NULL; k++) {
                portCoopFpLog("ge: roomtest: portal %d rooms %d-%d shared %d view %d %d %d %d event %d\n", k, (int) g_BgPortals[k].connectedRoom1, (int) g_BgPortals[k].connectedRoom2,
                              (int) (g_BgPortals[k].controlbytes1 & 1), (int) g_PortPortalView[0][k], (int) g_PortPortalView[1][k], (int) g_PortPortalView[2][k],
                              (int) g_PortPortalView[3][k], (int) g_PortPortalEvent[k]);
            }
        }
    }
    {   /* GE_ROOMTEST_DOORS=<tick>:o|c[,...]: at that sim tick every door of the stage is told to open (o) or close (c), the way a Bond's use would */
        static const char *dr;
        static int drinit;

        if (!drinit) {
            drinit = 1;
            dr = getenv("GE_ROOMTEST_DOORS");
        }
        for (s = dr; s != NULL && *s != '\0';) {
            unsigned t = 0;
            char op = 0;
            int used = 0;

            if (sscanf(s, "%u:%c%n", &t, &op, &used) < 2 || used <= 0) {
                break;
            }
            if (t == sTick) {
                PropRecord *p;
                int n = 0;
                s32 saved = get_cur_playernum();

                set_cur_player(0);
                for (p = chrpropGetActiveTail(); p != NULL; p = p->prev) {
                    if (p->type == PROP_TYPE_DOOR && p->door != NULL) {
                        doorActivate(p->door, op == 'o' ? DOORSTATE_OPENING : DOORSTATE_CLOSING);
                        n++;
                    }
                }
                set_cur_player(saved);
                portCoopFpLog("ge: roomtest: tick %u: %d doors told to %s\n", sTick, n, op == 'o' ? "open" : "close");
            }
            s += used;
            if (*s == ',') {
                s++;
            }
        }
    }
    if (sChrs && sTick == 5) {
        for (i = 0; i < g_NumChrSlots; i++) {
            ChrRecord *c = &g_ChrSlots[i];

            if (c->model != NULL && c->prop != NULL) {
                portCoopFpLog("ge: roomtest: chr %d at %.0f %.0f %.0f room %d\n", (int) c->chrnum, c->prop->pos.x, c->prop->pos.y, c->prop->pos.z, (int) c->prop->rooms[0]);
            }
        }
    }
}

#endif
