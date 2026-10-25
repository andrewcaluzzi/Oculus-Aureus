/* port_ai.c - guard awareness, barks and teamwork for the heist (designs/bank-heist.md section 18; owned by the ai stream).
 * AC (2026-10-26): "enemy AI really needs to be greatly improved ... you should be able to hear a person get slapped behind you even if
 * you're not facing that way."
 *
 * Every guard the mode registers (ai.guard) has an awareness state: 0 unaware, 1 suspicious, 2 searching, 3 alert, 4 combat, and a
 * level 0..1. A noise he hears (port_noise.c: distance, his hearing scale, the rooms and doors between) raises the level by its loudness
 * and kind. Suspicious: he stops and turns to the noise ("huh"). Then searching: he walks to the noise point (runs when it was loud or he
 * is alert), looks around, visits a couple of nearby pads, looks again, gives up ("must be nothing") and walks back to his post and
 * facing. A slap or a body falling close by sends him straight to searching. His own AI list (the stock simple guard / alarm raiser
 * detection loop) keeps running the whole time: the moment it sees Bond (the banker cover still decides that) or hears a shot it jumps
 * to its attack list, and this file stops steering him (combat while he has Bond in sight, alert after). Nothing here makes a guard see
 * through walls: hearing only tells him where to look.
 *
 * The banker cover (port_heist.c) gets the noise too: a staffer Bond near a noise a guard heard draws suspicion, not a blown cover;
 * a slap or a thud the guard can pin on a Bond he can see (any facing) blows it at once.
 *
 * Teamwork: a guard who goes down misses his radio check-in `checkin` s later and the nearest free colleague is sent to his post;
 * a guard who finds a body radios it (the stock body_found already puts everyone on alert: here everyone's awareness goes to alert);
 * a guard who goes into combat calls backup and the nearest free guards come, each to a different side of him (spread, not a bunch).
 * The cameras (the cover's gap): a camera that sees a Bond doing an act (a slap, a robbery, a search, a shot) or sees a body lying in
 * its view reports it to the security room at once (the cctv_spotted event the mode routes through its security_delay).
 *
 * All decisions read sim state only and live in the state hash (portAiHash). Barks (guard_bark events, presentation sounds) are
 * presentation: the event is fired from the sim at the same tick on every machine; the sound plays at the local Bond's ears.
 * PC only, mode only (ai.enable). */
#ifndef TARGET_N64

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <ultra64.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include <bondaicommands.h>
#include "game/chrai.h"
#include "game/stan.h"
#include "game/player.h"
#include "game/chr.h"
#include "game/chraction.h"
#include "game/gun.h"
#include "game/model.h"
#include "game/lv.h"
#include "game/math_atan2f.h"
#include "snd.h"
#include "port_blocks.h"
#include "port_mode.h"
#include "port_event.h"
#include "port_lua.h"
#include "port_noise.h"

extern s32 g_GlobalTimer;
extern s32 g_ClockTimer;
extern bool alarmIsActive(void);
extern s32 plot_course_for_actor(ChrRecord *self, coord3d *arg1, StandTile *stan, SPEED speed);
extern void bondviewUpdateGuardTankFlagsRelated(PropRecord *prop, s32 flag);
extern int portHeistKoIs(int chrnum);
extern int portHeistChrWaking(const ChrRecord *c);
static int othersSteer(const ChrRecord *c);
extern int portHeistCoverIsOn(void);
extern int portHeistCoverAlert(void);
extern int portHeistCoverActing(int p);
extern int portHeistCoverNoise(int chrnum, int p, float add, int blow);
extern int portHeistCctvList(int *out, int max);
extern void *portHeistCctvRecord(int idx);
extern int portHeistCivilianState(int chrnum);
extern int portHeistBodyCheckOn(void);
extern int portGameSfxPlay(int sfx, float vol, float pitch, float delay, float pan);
extern int portLocalPlayer(void);
extern void portNoiseEmitRoom(float x, float y, float z, float radius, int kind, int srcChr, int room);
extern float portNoiseRadius(int kind);
extern void portNoiseStageReset(void);
extern unsigned portNoiseHash(void);
extern int portNoiseNearDoor(float x, float z, float r);

#define AI_MAX       384
#define AI_SCAN      4              /* ticks between scans (15 Hz) */
#define CAM_SCAN     15
#define CAMBODY_MAX  128
#define FOUND_MAX    256
#define ROUTE_MAX    16
#define ROUTE_PADS   12
#define TAU          6.2831853f

#define KIND_CARRIER 3              /* ai.guard kinds (tools/heist_ai.lua KIND_CODE): 3 = a key-card carrier */

enum { AS_UNAWARE, AS_SUSPICIOUS, AS_SEARCHING, AS_ALERT, AS_COMBAT, AS_N };
enum { SUB_NONE, SUB_TURN, SUB_GO, SUB_LOOK, SUB_RETURN, SUB_PATROL };
enum { BK_HUH, BK_HEY, BK_INVESTIGATE, BK_LOST, BK_FOUND_BODY, BK_RADIO, BK_BACKUP, BK_SURRENDER, BK_N };
static const char *const sStateName[AS_N] = { "unaware", "suspicious", "searching", "alert", "combat" };
static const char *const sBarkName[BK_N] = { "huh", "hey", "investigate", "lost", "found_body", "radio", "backup", "surrender" };

typedef struct
{
    s32 chrnum;
    u8 used, state, sub, nkind, down, missed, backup, run, looks, lookMax, kind, alertOnly;
    s16 postPad, route, ridx;
    s32 stateAt, subAt, heardAt, downAt, plotAt, seenAt, barkAt, plots;
    f32 level, nx, ny, nz, tx, tz, postx, posty, postz, postFace;
    s32 nroom, src;
    AIRecord *idle;            /* the list it was spawned with (its detection loop); a cache, not hashed: compared each scan */
    ChrRecord *slot;           /* chrFindByLiteralId cache (re-checked by chrnum) */
} AiG;

static AiG sG[AI_MAX];
static int sGN, sOn, sAcc, sCamAcc;
static s32 sNow;
static s32 sBarkN, sSearchN, sGiveUpN, sCheckinN, sBackupN, sCamActN, sCamBodyN;
static s32 sCamBody[CAMBODY_MAX];
static int sCamBodyCnt;
static s32 sFound[FOUND_MAX];
static int sFoundCnt;
static u8 sActRep[4], sAlarmWas;
static f32 sBpx[4], sBpz[4], sStepT[4];
static u8 sBpOk[4];
static struct { s16 pad[ROUTE_PADS]; u8 n; } sRoute[ROUTE_MAX];
static int sRouteN;

/* ai.tune: seconds unless said */
static struct
{
    f32 turn, alertTurn, look, searchTime, searchRadius, decay, suspLevel, searchLevel, checkin, backupRange, coverNoise, stepSpeed,
        runSpeed, stepEvery, attribute, near, lure;
    f32 weight[NOISE_KIND_N];
    s32 searchPads, backupN;
} sT;

static void tuneDefaults(void)
{
    static const f32 w[NOISE_KIND_N] = { 1.2f, 0.45f, 0.6f, 0.5f, 1.5f, 1.2f, 0.9f, 0.8f, 2.0f };

    memcpy(sT.weight, w, sizeof(w));
    sT.turn = 1.2f;            /* suspicious: stopped, turned to the noise this long before he walks over */
    sT.alertTurn = 0.4f;       /* ... when alert or the noise was a slap / a thud */
    sT.look = 2.5f;            /* each look round */
    sT.searchTime = 30.0f;     /* the whole search, then he gives up */
    sT.searchRadius = 450.0f;  /* the pads he checks round the noise point */
    sT.decay = 25.0f;          /* the level drains from 1 to 0 in this long (to the alert floor when alert) */
    sT.suspLevel = 0.15f;      /* a level this high makes him stop and turn */
    sT.searchLevel = 0.3f;     /* ... this high and he walks over to look */
    sT.checkin = 25.0f;        /* a downed guard misses his radio check-in this long after (0 = off) */
    sT.backupRange = 1600.0f;
    sT.coverNoise = 0.5f;      /* the cover: suspicion a heard noise adds to a staffer near it (x loudness x kind) */
    sT.stepSpeed = 120.0f;     /* Bond's footsteps (units/s): faster than this is a step, */
    sT.runSpeed = 380.0f;      /* faster than this a run (full stick ~500) */
    sT.stepEvery = 0.5f;
    sT.attribute = 650.0f;     /* a slap / thud heard with the culprit in sight within this: the cover is blown */
    sT.near = 500.0f;          /* a staffer this close to a heard noise draws the suspicion */
    sT.lure = 0.25f;           /* a coin heard at least this loud (0..1) draws him over to look; fainter, he turns and looks (0 = off) */
    sT.searchPads = 2;
    sT.backupN = 2;
}

void portAiStageReset(void)
{
    memset(sG, 0, sizeof(sG));
    sGN = sOn = sAcc = sCamAcc = 0;
    sNow = 0;
    sBarkN = sSearchN = sGiveUpN = sCheckinN = sBackupN = sCamActN = sCamBodyN = 0;
    sCamBodyCnt = sFoundCnt = 0;
    memset(sActRep, 0, sizeof(sActRep));
    sAlarmWas = 0;
    memset(sBpOk, 0, sizeof(sBpOk));
    memset(sStepT, 0, sizeof(sStepT));
    memset(sRoute, 0, sizeof(sRoute));
    sRouteN = 0;
    tuneDefaults();
    portNoiseStageReset();
}

int portAiOn(void) { return sOn && portModeActive(); }

static ChrRecord *gChr(AiG *g)
{
    ChrRecord *c = g->slot;

    if (c == NULL || c->chrnum != g->chrnum || c->model == NULL)
    {
        c = chrFindByLiteralId(g->chrnum);
        g->slot = c;
    }
    return c;
}

static AiG *gFind(s32 chrnum)
{
    int i;

    for (i = 0; i < sGN; i++)
    {
        if (sG[i].used && sG[i].chrnum == chrnum) return &sG[i];
    }
    return NULL;
}

int portAiGuardCount(void) { return portAiOn() ? sGN : 0; }
int portAiGuardChr(int i) { return (i >= 0 && i < sGN && sG[i].used && !sG[i].down) ? sG[i].chrnum : -1; }

static int chrUp(ChrRecord *c)
{
    return c != NULL && c->prop != NULL && c->model != NULL && !(c->hidden & CHRHIDDEN_REMOVE) && !chrIsDead(c) && !portHeistKoIs(c->chrnum)
        && c->actiontype != ACT_DIE && c->actiontype != ACT_DEAD;
}

static f32 heading(f32 fx, f32 fz, f32 tx, f32 tz)
{
    f32 a = atan2f(tx - fx, tz - fz);

    if (a < 0.0f) a += TAU;
    return a;
}

static void faceAngle(ChrRecord *c, f32 a)
{
    while (a < 0.0f) a += TAU;
    while (a >= TAU) a -= TAU;
    check_set_actor_standing_still(c, TARGET_COMPASS, (s32) (a / TAU * 65535.0f));
}

/* ---- barks ---- */

typedef struct { s32 chr, kind; const char *text; } BarkArgs;
static BarkArgs sBk[4];
static int sBkN;

int portAiBarkArgs(int *chr, const char **kind, const char **text)
{
    if (sBkN <= 0) return 0;
    *chr = sBk[sBkN - 1].chr;
    *kind = sBarkName[sBk[sBkN - 1].kind];
    *text = sBk[sBkN - 1].text;
    return 1;
}

const char *portAiBarkName(int k) { return (k >= 0 && k < BK_N) ? sBarkName[k] : "?"; }

/* the stock sounds a bark plays at the guard (GoldenEye's guards have no "What was that?" lines: a throat-clear, a grunt, the radio).
 * Presentation only: port_gamesfx.c's own slots, volume and pan from where he stands relative to THIS machine's Bond. */
static void barkSfx(ChrRecord *c, int kind)
{
    int lp = portLocalPlayer();
    struct player *pl = (lp >= 0 && lp < 4) ? g_playerPointers[lp] : NULL;
    f32 vol = 0.0f, pan = 0.0f;

    if (c == NULL || c->prop == NULL || pl == NULL || pl->prop == NULL) return;
    {
        f32 dx = c->prop->pos.x - pl->prop->pos.x, dz = c->prop->pos.z - pl->prop->pos.z, d = sqrtf(dx * dx + dz * dz), th, rx;

        if (d > 2600.0f) return;
        vol = d < 250.0f ? 1.0f : 1.0f - 0.85f * (d - 250.0f) / 2350.0f;
        th = pl->vv_theta * (3.14159265f / 180.0f);
        rx = d > 1.0f ? (dx * cosf(th) + dz * sinf(th)) / d : 0.0f;
        pan = rx < -1.0f ? -1.0f : (rx > 1.0f ? 1.0f : rx);
    }
    switch (kind)
    {
        case BK_HUH:         portGameSfxPlay(COUGH2_SFX, vol * 0.7f, 1.3f, 0.0f, pan); break;
        case BK_HEY:         portGameSfxPlay(GET_HIT_MALE5_SFX, vol, 1.15f, 0.0f, pan); break;
        case BK_INVESTIGATE: portGameSfxPlay(RADIO_SFX, vol * 0.6f, 1.0f, 0.0f, pan); break;
        case BK_LOST:        portGameSfxPlay(COUGH_SFX, vol * 0.6f, 0.95f, 0.0f, pan); break;
        case BK_FOUND_BODY:  portGameSfxPlay(GET_HIT_MALE12_SFX, vol, 1.1f, 0.0f, pan);
                             portGameSfxPlay(RADIO_SFX, vol * 0.8f, 1.1f, 0.5f, pan);
                             portGameSfxPlay(RADIO_MESSAGE_SFX, vol, 1.0f, 0.7f, pan); break;
        case BK_RADIO:       portGameSfxPlay(RADIO_SFX, vol * 0.8f, 1.0f, 0.0f, pan);
                             portGameSfxPlay(RADIO_MESSAGE_SFX, vol * 0.9f, 0.95f, 0.25f, pan); break;
        case BK_BACKUP:      portGameSfxPlay(RADIO_SFX, vol * 0.8f, 1.15f, 0.0f, pan);
                             portGameSfxPlay(RADIO_SFX, vol * 0.8f, 0.95f, 0.2f, pan);
                             portGameSfxPlay(RADIO_MESSAGE_SFX, vol, 1.1f, 0.4f, pan); break;
        default: break;
    }
}

static void bark(AiG *g, ChrRecord *c, int kind, const char *text)
{
    if (g != NULL && kind != BK_HEY && kind != BK_FOUND_BODY && kind != BK_BACKUP && sNow - g->barkAt < 30 && g->barkAt > 0) return;
    if (g != NULL) g->barkAt = sNow;
    sBarkN++;
    printf("ge: ai: bark chr %d %s \"%s\" (%s, level %.2f)\n", c != NULL ? (int) c->chrnum : -1, sBarkName[kind], text,
           g != NULL ? sStateName[g->state] : "-", g != NULL ? (double) g->level : 0.0);
    barkSfx(c, kind);
    if (portEventListenerCount(PORT_EV_GUARD_BARK) > 0 && sBkN < 4 && c != NULL)
    {
        sBk[sBkN].chr = c->chrnum;
        sBk[sBkN].kind = kind;
        sBk[sBkN].text = text;
        sBkN++;
        portEventEmit(PORT_EV_GUARD_BARK, c->chrnum, kind, g != NULL ? g->state : 0, 0);
        sBkN--;
    }
}

/* ai.bark(chr, kind, text): a script makes a guard say something */
int portAiBark(int chrnum, const char *kind, const char *text)
{
    ChrRecord *c = chrFindByLiteralId(chrnum);
    int k;

    if (!portAiOn() || c == NULL || kind == NULL) return 0;
    for (k = 0; k < BK_N && strcmp(kind, sBarkName[k]) != 0; k++) ;
    if (k == BK_N) return 0;
    {
        static char keep[4][96];
        static int at;

        snprintf(keep[at], sizeof(keep[0]), "%s", text != NULL ? text : "");   /* the text outlives the call (the event reads it) */
        bark(gFind(chrnum), c, k, keep[at]);
        at = (at + 1) & 3;
    }
    return 1;
}

/* ---- registration ---- */

int portAiGuardAdd(int chrnum, int kind)
{
    ChrRecord *c = chrFindByLiteralId(chrnum);
    AiG *g = gFind(chrnum);
    int ids[32], n, i, best = -1;
    f32 bd = 1e18f;

    if (!portModeActive() || c == NULL || c->prop == NULL || c->model == NULL) return 0;
    if (g == NULL)
    {
        for (i = 0; i < sGN && sG[i].used; i++) ;
        if (i == sGN)
        {
            if (sGN >= AI_MAX) return 0;
            sGN++;
        }
        g = &sG[i];
    }
    memset(g, 0, sizeof(*g));
    g->used = 1;
    g->chrnum = chrnum;
    g->kind = (u8) kind;
    g->slot = c;
    g->idle = c->ailist;
    g->postx = c->prop->pos.x;
    g->posty = c->prop->pos.y;
    g->postz = c->prop->pos.z;
    g->postFace = getsubroty(c->model);
    g->route = -1;
    g->src = -1;
    g->nroom = -1;
    n = portLuaGamePadList(g->postx, g->postz, 0.0f, 300.0f, 1, ids, 32);
    for (i = 0; i < n; i++)
    {
        float x, y, z, d;

        if (!portLuaGamePadPos(ids[i], &x, &y, &z) || y - c->prop->pos.y > 150.0f || y - c->prop->pos.y < -150.0f) continue;
        d = (x - g->postx) * (x - g->postx) + (z - g->postz) * (z - g->postz);
        if (d < bd) { bd = d; best = ids[i]; }
    }
    g->postPad = (s16) best;
    return 1;
}

int portAiEnable(int on) { sOn = on ? 1 : 0; return 1; }

/* ai.patrol(chr, {pads}): walk these pads in a loop while unaware (the layout's patrol_<route>_<n> pads) */
int portAiPatrol(int chrnum, const int *pads, int n)
{
    AiG *g = gFind(chrnum);
    int i;

    if (g == NULL || n < 2 || sRouteN >= ROUTE_MAX) return 0;
    if (n > ROUTE_PADS) n = ROUTE_PADS;
    for (i = 0; i < n; i++) sRoute[sRouteN].pad[i] = (s16) pads[i];
    sRoute[sRouteN].n = (u8) n;
    g->route = (s16) sRouteN++;
    g->ridx = 0;
    g->sub = SUB_PATROL;
    g->plotAt = 0;
    return 1;
}

/* ---- moving him ---- */

static int goToPoint(ChrRecord *c, f32 x, f32 y, f32 z, int run)
{
    coord3d p;
    f32 h;
    StandTile *t;
    int ids[16], n, i, best = -1;
    f32 bd = 1e18f;

    p.x = x; p.y = y + 50.0f; p.z = z;
    t = stanFindTileBelowPos(&p, NULL, &h);
    if (t != NULL)
    {
        p.y = h;
        if (plot_course_for_actor(c, &p, t, run ? SPEED_RUN : SPEED_WALK)) return 1;
    }
    n = portLuaGamePadList(x, z, 0.0f, 400.0f, 1, ids, 16);
    for (i = 0; i < n; i++)
    {
        float px, py, pz, d;

        if (!portLuaGamePadPos(ids[i], &px, &py, &pz) || py - y > 200.0f || py - y < -200.0f) continue;
        d = (px - x) * (px - x) + (pz - z) * (pz - z);
        if (d < bd) { bd = d; best = ids[i]; }
    }
    if (best >= 0 && chrGoToPad(c, best, run ? SPEED_RUN : SPEED_WALK)) return 1;
    printf("ge: ai: chr %d found no route to %.0f,%.0f,%.0f (tile %s, %d pads near)\n", (int) c->chrnum, x, y, z, t != NULL ? "yes" : "no", n);
    return 0;
}

static int moving(ChrRecord *c) { return c->actiontype == ACT_GOPOS || c->actiontype == ACT_RUNPOS || c->actiontype == ACT_PATROL; }

static void setState(AiG *g, int st)
{
    if (g->state != st)
    {
        g->state = (u8) st;
        g->stateAt = sNow;
    }
}

static void setSub(AiG *g, int sub)
{
    g->sub = (u8) sub;
    g->subAt = sNow;
}

/* search: the next spot to walk to (the noise point first, then pads round it, nearest the point first, skipping ones already seen) */
static int searchNext(AiG *g, ChrRecord *c)
{
    int ids[64], n, i, pick = -1;
    f32 x, y, z;

    if (g->looks == 0)
    {
        g->tx = g->nx; g->tz = g->nz;
        return goToPoint(c, g->nx, g->ny, g->nz, g->run) ? 1 : 0;
    }
    n = portLuaGamePadList(g->nx, g->nz, 120.0f, sT.searchRadius, 1, ids, 64);
    if (n <= 0) return 0;
    /* a different pad each look, picked from the chr and the look (no RNG draw) */
    for (i = 0; i < n && pick < 0; i++)
    {
        int k = (int) (((u32) g->chrnum * 2654435761u + (u32) g->looks * 40503u + (u32) i * 977u) % (u32) n);
        float px, py, pz;

        if (!portLuaGamePadPos(ids[k], &px, &py, &pz) || py - g->ny > 150.0f || py - g->ny < -150.0f) continue;
        if (portNoiseNearDoor(px, pz, 160.0f)) continue;   /* not a doorway pad: he would stand in the door */
        pick = ids[k];
        x = px; y = py; z = pz;
    }
    if (pick < 0) return 0;
    g->tx = x; g->tz = z;
    return chrGoToPad(c, pick, g->state >= AS_ALERT ? SPEED_RUN : SPEED_WALK) ? 1 : 0;
}

static void startSearch(AiG *g, ChrRecord *c, int run)
{
    g->looks = 0;
    g->lookMax = (u8) (1 + (sT.searchPads > 0 ? sT.searchPads : 0));
    g->run = (u8) (run ? 1 : 0);
    g->plots = 0;
    if (g->state < AS_SEARCHING) setState(g, AS_SEARCHING);
    sSearchN++;
    bark(g, c, BK_INVESTIGATE, g->run ? "Over there! Checking it out." : "I'll take a look.");
    setSub(g, SUB_GO);
    g->plotAt = sNow;
    if (!searchNext(g, c)) setSub(g, SUB_LOOK);
}

static void giveUp(AiG *g, ChrRecord *c)
{
    sGiveUpN++;
    bark(g, c, BK_LOST, g->state >= AS_ALERT ? "Nothing. Stay sharp." : "Must be nothing.");
    if (g->state < AS_ALERT) setState(g, AS_UNAWARE);
    if (g->state < AS_ALERT && g->level > sT.suspLevel * 0.5f) g->level = sT.suspLevel * 0.5f;   /* calmed down: the next noise starts over */
    setSub(g, SUB_RETURN);
    g->plotAt = sNow;
    printf("ge: ai: chr %d gives up at %.0f,%.0f: back to his post %.0f,%.0f (pad %d)\n", (int) c->chrnum, c->prop->pos.x, c->prop->pos.z,
           g->postx, g->postz, (int) g->postPad);
    if (!goToPoint(c, g->postx, g->posty, g->postz, 0) && (g->postPad < 0 || !chrGoToPad(c, g->postPad, SPEED_WALK))) setSub(g, SUB_NONE);
}

/* ---- hearing (port_noise.c calls this for every tracked guard in range) ---- */

static int seesProp(ChrRecord *g, PropRecord *bp, f32 maxd);

void portAiHear(int i, int kind, float loud, float x, float y, float z, int room, int srcChr)
{
    AiG *g;
    ChrRecord *c;
    f32 add;
    int close;

    if (i < 0 || i >= sGN || !sG[i].used) return;
    g = &sG[i];
    c = gChr(g);
    if (!chrUp(c)) return;
    add = loud * sT.weight[kind];
    g->level += add;
    if (kind == NOISE_COIN && sT.lure > 0.0f)
    {   /* a distraction is meant to be followed: inside `lure` of his hearing range (loud = 1 at the noise, 0 at the edge of radius x
         * his hearing x the rooms and doors between) he walks over; further out he still turns and looks from where he stands */
        if (loud >= sT.lure && g->level < sT.searchLevel + 0.12f) g->level = sT.searchLevel + 0.12f;
        else if (g->level < sT.suspLevel + 0.02f) g->level = sT.suspLevel + 0.02f;
    }
    if (g->level > 1.0f) g->level = 1.0f;
    g->heardAt = sNow;
    close = (kind == NOISE_SLAP || kind == NOISE_THUD) && loud > 0.15f;
    /* the louder (or the more alarming) noise wins the search point */
    if (g->sub != SUB_GO || add >= 0.3f || close || g->state < AS_SEARCHING)
    {
        g->nx = x; g->ny = y; g->nz = z; g->nroom = room; g->nkind = (u8) kind; g->src = srcChr;
    }
    /* the cover: a staffer Bond near what he heard */
    if (portHeistCoverIsOn() && !portHeistCoverAlert() && kind != NOISE_STEP && kind != NOISE_RUN && kind != NOISE_SHOT && kind != NOISE_ALARM)
    {
        int p, n = (int) portBondCount();

        for (p = 0; p < n && p < 4; p++)
        {
            struct player *pl = g_playerPointers[p];
            f32 dx, dz;

            if (!portBondInPlay(p) || pl == NULL || pl->prop == NULL || pl->bonddead) continue;
            dx = pl->prop->pos.x - x;
            dz = pl->prop->pos.z - z;
            if (dx * dx + dz * dz > sT.near * sT.near) continue;
            if (close && (srcChr == -2 - p || dx * dx + dz * dz < 150.0f * 150.0f) && seesProp(c, pl->prop, sT.attribute))
            {
                portHeistCoverNoise(g->chrnum, p, 1.0f, 1);   /* heard it, turned, and there he is: blown */
            }
            else
            {
                portHeistCoverNoise(g->chrnum, p, sT.coverNoise * add, 0);
            }
        }
    }
    if (g->state == AS_COMBAT) return;
    if (kind == NOISE_ALARM) { setState(g, AS_ALERT); if (g->level < 0.8f) g->level = 0.8f; return; }
    if (othersSteer(c) || c->ailist != g->idle) return;   /* a waker / a scripted walk / his own list took over: not steered */
    if ((close || kind == NOISE_SHOT || kind == NOISE_GLASS) && (g->state < AS_SEARCHING || g->sub == SUB_RETURN || g->sub == SUB_PATROL))
    {
        if (g->state < AS_SEARCHING) setState(g, AS_SEARCHING);
        bark(g, c, close ? BK_HEY : BK_HUH, close ? "Hey! What was that?" : "What was that?!");
        faceAngle(c, heading(c->prop->pos.x, c->prop->pos.z, x, z));
        setSub(g, SUB_TURN);
        g->run = 1;
        return;
    }
    if (g->level >= sT.suspLevel && (g->state == AS_UNAWARE || g->state == AS_ALERT) && (g->sub == SUB_NONE || g->sub == SUB_RETURN || g->sub == SUB_PATROL))
    {
        if (g->state == AS_UNAWARE) setState(g, AS_SUSPICIOUS);
        bark(g, c, BK_HUH, g->state == AS_ALERT ? "There!" : "Huh?");
        faceAngle(c, heading(c->prop->pos.x, c->prop->pos.z, x, z));
        setSub(g, SUB_TURN);
        g->run = (u8) (g->state >= AS_ALERT);
    }
}

/* ---- sight helpers ---- */

/* chr g has a clear stan line to prop bp within maxd (any facing: he turned to the noise) */
static int seesProp(ChrRecord *g, PropRecord *bp, f32 maxd)
{
    PropRecord *gp = g->prop;
    f32 dx = bp->pos.x - gp->pos.x, dy = bp->pos.y - gp->pos.y, dz = bp->pos.z - gp->pos.z, h;
    StandTile *st;
    int ok;

    if (dy > 300.0f || dy < -300.0f || dx * dx + dz * dz > maxd * maxd || gp->stan == NULL || bp->stan == NULL) return 0;
    st = gp->stan;
    h = g->chrheight - 20.0f;
    chrSetMoving(g, FALSE);
    if (bp->type == PROP_TYPE_CHR && bp->chr != NULL) chrSetMoving(bp->chr, FALSE);
    else bondviewUpdateGuardTankFlagsRelated(bp, 0);
    ok = stanTestLineUnobstructed(&st, gp->pos.x, gp->pos.z, bp->pos.x, bp->pos.z,
                                  CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_CHRS | CDTYPE_PATHBLOCKER | CDTYPE_AIOPAQUE, h, h, 0.0f, 1.0f) != 0
         && st == bp->stan;
    if (bp->type == PROP_TYPE_CHR && bp->chr != NULL) chrSetMoving(bp->chr, TRUE);
    else bondviewUpdateGuardTankFlagsRelated(bp, 1);
    chrSetMoving(g, TRUE);
    return ok;
}

/* a free guard: up, his own detection loop running, nobody else steering him, not already busy */
static int freeGuard(AiG *g, ChrRecord *c)
{
    return chrUp(c) && c->ailist == g->idle && !othersSteer(c) && g->state != AS_COMBAT
        && (g->sub == SUB_NONE || g->sub == SUB_RETURN || g->sub == SUB_PATROL);
}

/* send guard g to search point (x, y, z), running or walking */
static void sendTo(AiG *g, ChrRecord *c, f32 x, f32 y, f32 z, int run, int st)
{
    g->nx = x; g->ny = y; g->nz = z; g->nkind = NOISE_SHOT;
    if (g->level < 0.6f) g->level = 0.6f;
    setState(g, st > g->state ? st : g->state);
    startSearch(g, c, run);
}

/* the nearest free guards to (x, z) within range, best first; returns how many. holdCards: a key-card carrier holds his post (a
 * backup call: the others answer it, so the card stays where the player cased it) */
static int nearestFree(f32 x, f32 y, f32 z, f32 range, int *out, int max, s32 exclude, int holdCards)
{
    int i, n = 0, k;
    f32 d[8];

    if (max > 8) max = 8;
    for (i = 0; i < sGN; i++)
    {
        AiG *g = &sG[i];
        ChrRecord *c;
        f32 dx, dz, dd;

        if (!g->used || g->down || g->chrnum == exclude || (holdCards && g->kind == KIND_CARRIER)) continue;
        c = gChr(g);
        if (!freeGuard(g, c)) continue;
        dx = c->prop->pos.x - x; dz = c->prop->pos.z - z;
        if (c->prop->pos.y - y > 400.0f || c->prop->pos.y - y < -400.0f) continue;
        dd = dx * dx + dz * dz;
        if (dd > range * range) continue;
        for (k = n; k > 0 && d[k - 1] > dd; k--)
        {
            if (k < max) { d[k] = d[k - 1]; out[k] = out[k - 1]; }
        }
        if (k < max) { d[k] = dd; out[k] = i; if (n < max) n++; }
    }
    return n;
}

static void callBackup(AiG *g, ChrRecord *c)
{
    int who[8], n, k;

    g->backup = 1;
    n = nearestFree(c->prop->pos.x, c->prop->pos.y, c->prop->pos.z, sT.backupRange, who, sT.backupN, g->chrnum, 1);
    bark(g, c, BK_BACKUP, n > 0 ? "Contact! Need backup!" : "Contact!");
    for (k = 0; k < n; k++)
    {
        AiG *h = &sG[who[k]];
        ChrRecord *hc = gChr(h);
        f32 a = g->postFace + (f32) k * 2.0943951f + 1.0f, r = 220.0f;   /* each to a different side of him */

        f32 bx = c->prop->pos.x + sinf(a) * r, bz = c->prop->pos.z + cosf(a) * r;

        sBackupN++;
        if (portNoiseNearDoor(bx, bz, 160.0f)) { bx = c->prop->pos.x; bz = c->prop->pos.z; }   /* not in a doorway */
        sendTo(h, hc, bx, c->prop->pos.y, bz, 1, AS_ALERT);
        bark(h, hc, BK_RADIO, "Copy, moving up.");
    }
}

/* ai.backup(chr) -> how many answered: a tracked guard calls for backup now, as when he goes into combat (scripts, tests). Once per
 * guard, like the combat call. */
int portAiBackup(int chrnum)
{
    AiG *g = gFind(chrnum);
    ChrRecord *c;
    s32 before = sBackupN;

    if (!portAiOn() || g == NULL || !chrUp(c = gChr(g)) || g->backup) return 0;
    callBackup(g, c);
    return (int) (sBackupN - before);
}

/* ai.nearest_free(x, y, z [, range]) -> the nearest free tracked guard (up, his own list idle, not busy), -1 none */
int portAiNearestFree(float x, float y, float z, float range, int exclude)
{
    int who[1];

    if (!portAiOn() || nearestFree(x, y, z, range > 0.0f ? range : 20000.0f, who, 1, exclude, 0) != 1) return -1;
    return sG[who[0]].chrnum;
}

/* ai.send(chr, x, y, z [, run [, text]]): a script sends a tracked guard to search a point (a missed watchclock station ...) */
int portAiSend(int chrnum, float x, float y, float z, int run, const char *text)
{
    AiG *g = gFind(chrnum);
    ChrRecord *c;

    if (!portAiOn() || g == NULL || !chrUp(c = gChr(g))) return 0;
    if (text != NULL && text[0] != '\0')
    {
        static char keep[4][96];
        static int at;

        snprintf(keep[at], sizeof(keep[0]), "%s", text);
        bark(g, c, BK_RADIO, keep[at]);
        at = (at + 1) & 3;
    }
    sendTo(g, c, x, y, z, run, AS_SUSPICIOUS);
    return 1;
}

/* port_heist.c portHeistChrWaking (so port_arenamode.c portArenaDirect keeps the stock route): this layer is walking him somewhere
 * (to a noise, a search pad, back to his post, his patrol). Without it the arena's direct route re-aims a spawned guard at Bond. */
static int sSelfAsk;

int portAiSteers(const ChrRecord *c)
{
    AiG *g;

    if (sSelfAsk || c == NULL || !portAiOn()) return 0;
    g = gFind(c->chrnum);
    return g != NULL && g->sub != SUB_NONE && g->state != AS_COMBAT && c->ailist == g->idle;
}

/* a KO waker or a scripted walk (spawn.go) steers him: not this layer */
static int othersSteer(const ChrRecord *c)
{
    int r;

    sSelfAsk = 1;
    r = portHeistChrWaking(c);
    sSelfAsk = 0;
    return r;
}

/* ---- the per-guard step ---- */

static void guardStep(AiG *g, f32 dt)
{
    ChrRecord *c = gChr(g);
    int listOwns, seen;

    if (c == NULL || c->prop == NULL)
    {
        g->used = 0;   /* removed */
        return;
    }
    if (!chrUp(c))
    {
        if (!g->down) { g->down = 1; g->downAt = sNow; }
        return;
    }
    if (g->down)
    {   /* woken up (a colleague, a script): he knows */
        g->down = 0;
        g->missed = 0;
        setState(g, AS_ALERT);
        g->level = 1.0f;
        setSub(g, SUB_NONE);
    }
    /* the level drains (to the alert floor) */
    {
        f32 floor = g->state >= AS_ALERT ? 0.6f : 0.0f;

        g->level -= dt / sT.decay;
        if (g->level < floor) g->level = floor;
    }
    seen = c->lastseetarget60 > 0 && g_GlobalTimer - c->lastseetarget60 < 180;
    listOwns = c->ailist != g->idle;
    if (listOwns || c->actiontype == ACT_ATTACK || c->actiontype == ACT_ATTACKWALK || c->actiontype == ACT_ATTACKROLL)
    {   /* his own list has him: attacking, running to Bond, raising the alarm */
        int st = seen || c->actiontype == ACT_ATTACK || c->actiontype == ACT_ATTACKWALK || c->actiontype == ACT_ATTACKROLL ? AS_COMBAT : AS_ALERT;

        if (g->state < AS_ALERT) bark(g, c, BK_HEY, "Hey! You there!");
        if (st == AS_COMBAT) g->seenAt = sNow;
        else if (g->state == AS_COMBAT && sNow - g->seenAt < 600) st = AS_COMBAT;   /* combat holds 10 s after the last sight */
        setState(g, st);
        g->level = 1.0f;
        if (g->sub != SUB_NONE) setSub(g, SUB_NONE);
        if (st == AS_COMBAT && !g->backup && sT.backupN > 0) callBackup(g, c);
        return;
    }
    if (othersSteer(c)) { setSub(g, SUB_NONE); return; }   /* the KO waker / a scripted walk steers him */
    switch (g->sub)
    {
        case SUB_TURN:
            if (sNow - g->subAt >= (s32) (((g->state >= AS_ALERT || g->run) ? sT.alertTurn : sT.turn) * 60.0f))
            {
                if (g->level >= sT.searchLevel || g->state >= AS_SEARCHING)
                {
                    startSearch(g, c, g->run || g->state >= AS_ALERT);
                }
                else
                {   /* a faint one: he looks a while from where he stands */
                    chrTrySurprisedLookAround(c);
                    setSub(g, SUB_LOOK);
                    g->looks = g->lookMax = 1;
                }
            }
            break;
        case SUB_GO:
        {
            f32 dx = g->tx - c->prop->pos.x, dz = g->tz - c->prop->pos.z;
            int arrived = dx * dx + dz * dz < 90.0f * 90.0f;

            if (!arrived && !moving(c) && sNow - g->plotAt > 45)
            {   /* stopped short (a refused door, a blocked route): re-plot twice, then look from here */
                if (++g->plots > 2 || !(g->looks == 0 ? goToPoint(c, g->tx, g->ny, g->tz, g->run) : searchNext(g, c))) arrived = 1;
                g->plotAt = sNow;
            }
            if (arrived)
            {
                if (moving(c)) chraiStopAnimation(c);
                if (g->looks == 0 && portHeistCoverIsOn() && !portHeistCoverAlert())
                {   /* at the noise point: a staffer standing right there is the obvious suspect */
                    int p, n = (int) portBondCount();

                    for (p = 0; p < n && p < 4; p++)
                    {
                        struct player *pl = g_playerPointers[p];

                        if (!portBondInPlay(p) || pl == NULL || pl->prop == NULL || pl->bonddead) continue;
                        if (seesProp(c, pl->prop, 350.0f)) portHeistCoverNoise(g->chrnum, p, 0.35f, 0);
                    }
                }
                chrTrySurprisedLookAround(c);
                g->looks++;
                setSub(g, SUB_LOOK);
            }
            else if (sNow - g->stateAt > (s32) (sT.searchTime * 60.0f))
            {
                giveUp(g, c);
            }
            break;
        }
        case SUB_LOOK:
            if (sNow - g->subAt >= (s32) (sT.look * 60.0f))
            {
                if (g->looks < g->lookMax && sNow - g->stateAt < (s32) (sT.searchTime * 60.0f))
                {
                    setSub(g, SUB_GO);
                    g->plotAt = sNow;
                    g->plots = 0;
                    if (!searchNext(g, c)) { g->looks = g->lookMax; setSub(g, SUB_LOOK); }
                }
                else
                {
                    giveUp(g, c);
                }
            }
            break;
        case SUB_RETURN:
        {
            f32 dx = g->postx - c->prop->pos.x, dz = g->postz - c->prop->pos.z;

            if (dx * dx + dz * dz < 80.0f * 80.0f || (!moving(c) && sNow - g->subAt > 60))
            {
                if (dx * dx + dz * dz < 200.0f * 200.0f || sNow - g->subAt > 60 * 40)
                {
                    faceAngle(c, g->postFace);
                    setSub(g, g->route >= 0 ? SUB_PATROL : SUB_NONE);
                    g->plotAt = 0;
                }
                else if (sNow - g->plotAt > 120)
                {
                    g->plotAt = sNow;
                    if (!goToPoint(c, g->postx, g->posty, g->postz, 0) && g->postPad >= 0) chrGoToPad(c, g->postPad, SPEED_WALK);
                }
            }
            break;
        }
        case SUB_PATROL:
            if (g->route >= 0 && g->route < sRouteN && g->state < AS_SEARCHING && !moving(c) && sNow - g->plotAt > 90)
            {
                int pad = sRoute[g->route].pad[g->ridx % sRoute[g->route].n];

                g->ridx = (s16) ((g->ridx + 1) % sRoute[g->route].n);
                g->plotAt = sNow;
                chrGoToPad(c, pad, SPEED_WALK);
            }
            break;
        default:
            if (g->state == AS_SUSPICIOUS && sNow - g->stateAt > 600) setState(g, AS_UNAWARE);
            if (g->state == AS_SEARCHING) setState(g, AS_UNAWARE);
            break;
    }
}

/* ---- noises from the sim ---- */

static int chrRoom(ChrRecord *c) { return (c != NULL && c->prop != NULL && c->prop->stan != NULL) ? (int) (c->prop->stan->room & 0xFF) : -1; }

/* port_heist.c portHeistSlap: Bond (the current player) slapped or punched `c` */
void portAiSlapped(ChrRecord *c)
{
    if (!portAiOn() || c == NULL || c->prop == NULL) return;
    portNoiseEmitRoom(c->prop->pos.x, c->prop->pos.y, c->prop->pos.z, portNoiseRadius(NOISE_SLAP), NOISE_SLAP, -2 - (int) get_cur_playernum(), chrRoom(c));
}

/* port_heist.c portHeistCoverFired: the current Bond fires; the gun's noise (gunTickNoise) x 100, as the stock hearing; one noise per
 * 10 ticks per Bond */
static s32 sShotAt[4];

void portAiBondFired(void)
{
    int p = (int) get_cur_playernum();
    struct player *pl;
    f32 n;

    if (!portAiOn() || p < 0 || p > 3 || (pl = g_playerPointers[p]) == NULL || pl->prop == NULL) return;
    if (sShotAt[p] != 0 && g_GlobalTimer - sShotAt[p] < 10 && g_GlobalTimer >= sShotAt[p]) return;
    n = getCurrentPlayerNoise(GUNRIGHT);
    if (get_hands_firing_status(GUNLEFT) && getCurrentPlayerNoise(GUNLEFT) > n) n = getCurrentPlayerNoise(GUNLEFT);
    if (n <= 0.0f) return;
    sShotAt[p] = g_GlobalTimer;
    portNoiseEmitRoom(pl->prop->pos.x, pl->prop->pos.y, pl->prop->pos.z, n * 100.0f, NOISE_SHOT, -2 - p,
                      pl->prop->stan != NULL ? (int) (pl->prop->stan->room & 0xFF) : -1);
}

/* glass.c: a window shatters (shot, blown) */
void portAiGlass(f32 x, f32 y, f32 z)
{
    if (!portAiOn()) return;
    portNoiseEmit(x, y, z, portNoiseRadius(NOISE_GLASS), NOISE_GLASS, -1);
}

/* the C event bus: a body hits the floor (knocked out or killed): a thud where it lies; a body found: everyone alert */
static void onEvent(int type, int a, int b, int c, int d, void *ud)
{
    (void) c; (void) d; (void) ud;
    if (!portAiOn()) return;
    if (type == PORT_EV_CHR_KNOCKED_OUT || type == PORT_EV_CHR_DIED)
    {
        ChrRecord *v = chrFindByLiteralId(a);

        if (v != NULL && v->prop != NULL)
        {
            portNoiseEmitRoom(v->prop->pos.x, v->prop->pos.y, v->prop->pos.z, portNoiseRadius(NOISE_THUD), NOISE_THUD, a, chrRoom(v));
        }
    }
    else if (type == PORT_EV_BODY_FOUND)
    {
        AiG *g = gFind(a);
        ChrRecord *gc = chrFindByLiteralId(a);
        int i;

        if (sFoundCnt < FOUND_MAX) sFound[sFoundCnt++] = b;
        bark(g, gc, BK_FOUND_BODY, c ? "Man down! Someone's out cold here!" : "Man down! We have a man down!");
        for (i = 0; i < sGN; i++)
        {
            if (!sG[i].used || sG[i].down) continue;
            if (sG[i].state < AS_ALERT) setState(&sG[i], AS_ALERT);
            if (sG[i].level < 0.8f) sG[i].level = 0.8f;
        }
    }
}

static int sHooked;

static void hookEvents(void)
{
    if (sHooked) return;
    sHooked = 1;
    portEventOn(PORT_EV_CHR_KNOCKED_OUT, onEvent, NULL);
    portEventOn(PORT_EV_CHR_DIED, onEvent, NULL);
    portEventOn(PORT_EV_BODY_FOUND, onEvent, NULL);
}

static int found(s32 chrnum)
{
    int i;

    for (i = 0; i < sFoundCnt; i++)
    {
        if (sFound[i] == chrnum) return 1;
    }
    return 0;
}

/* Bond's footsteps: every stepEvery seconds a step (walking) or a run (full stick) where he is */
static void footsteps(f32 dt)
{
    int p, n = (int) portBondCount();

    for (p = 0; p < n && p < 4; p++)
    {
        struct player *pl = g_playerPointers[p];
        f32 sp = 0.0f;

        if (!portBondInPlay(p) || pl == NULL || pl->prop == NULL || pl->bonddead) { sBpOk[p] = 0; continue; }
        if (sBpOk[p] && dt > 0.0f)
        {
            f32 dx = pl->prop->pos.x - sBpx[p], dz = pl->prop->pos.z - sBpz[p];

            sp = sqrtf(dx * dx + dz * dz) / dt;
        }
        sBpx[p] = pl->prop->pos.x;
        sBpz[p] = pl->prop->pos.z;
        sBpOk[p] = 1;
        sStepT[p] += dt;
        if (sp < sT.stepSpeed || sp > 3000.0f || sStepT[p] < sT.stepEvery) continue;   /* (a warp is not a step) */
        sStepT[p] = 0.0f;
        portNoiseEmitRoom(pl->prop->pos.x, pl->prop->pos.y, pl->prop->pos.z, portNoiseRadius(sp > sT.runSpeed ? NOISE_RUN : NOISE_STEP),
                          sp > sT.runSpeed ? NOISE_RUN : NOISE_STEP, -2 - p, pl->prop->stan != NULL ? (int) (pl->prop->stan->room & 0xFF) : -1);
    }
}

/* ---- teamwork: missed check-ins ---- */

static void checkins(void)
{
    int i;

    if (sT.checkin <= 0.0f) return;
    for (i = 0; i < sGN; i++)
    {
        AiG *g = &sG[i];
        int who[1];

        if (!g->used || !g->down || g->missed || sNow - g->downAt < (s32) (sT.checkin * 60.0f)) continue;
        g->missed = 1;
        if (found(g->chrnum) || alarmIsActive()) continue;
        sCheckinN++;
        if (nearestFree(g->postx, 0.0f, g->postz, 20000.0f, who, 1, g->chrnum, 0) == 1)
        {
            AiG *h = &sG[who[0]];
            ChrRecord *hc = gChr(h);
            ChrRecord *gc = gChr(g);
            static char txt[4][64];
            static int at;

            snprintf(txt[at], sizeof(txt[0]), "Post %d isn't answering. Going to look.", (int) g->chrnum);
            bark(h, hc, BK_RADIO, txt[at]);
            at = (at + 1) & 3;
            printf("ge: ai: chr %d missed his check-in (down %.1f s): chr %d sent to his post\n", (int) g->chrnum,
                   (sNow - g->downAt) / 60.0, (int) h->chrnum);
            sendTo(h, hc, g->postx, gc != NULL && gc->prop != NULL ? gc->prop->pos.y : hc->prop->pos.y, g->postz, 0, AS_SUSPICIOUS);
        }
    }
}

/* ---- the cameras: acts and bodies (the cover's gap) ---- */

static int camSees(CCTVRecord *cam, PropRecord *tp)
{
    ObjectRecord *obj = (ObjectRecord *) cam;
    PropRecord *prop = obj->prop;
    f32 xd, yd, zd, range = cam->unkE8, a, face, delta;
    StandTile *st;
    int ok;

    if (prop == NULL || prop->stan == NULL || tp == NULL || (obj->flags & PROPFLAG_CCTV_DISABLED) || (obj->flags & PROPFLAG_INMOTION)) return 0;
    xd = tp->pos.x - obj->runtime_pos.x; yd = tp->pos.y - obj->runtime_pos.y; zd = tp->pos.z - obj->runtime_pos.z;
    if (range > 0.0f && range * range < xd * xd + yd * yd + zd * zd) return 0;
    a = atan2f(xd, zd);
    face = cam->unkC8;
    if (face < 0.0f) face += M_TAU_F; else if (face >= M_TAU_F) face -= M_TAU_F;
    face += cam->unkC4;
    if (face >= M_TAU_F) face -= M_TAU_F;
    delta = a - face;
    if (a < face) delta += M_TAU_F;
    delta -= M_PI_F;
    if (delta < 0.0f) delta += M_TAU_F;
    if (delta > M_PI_F) delta -= M_TAU_F;
    if (delta > DegToRad(45) || delta < DegToRad(-45)) return 0;
    st = prop->stan;
    if (tp->type == PROP_TYPE_CHR && tp->chr != NULL) chrSetMoving(tp->chr, FALSE);
    else bondviewUpdateGuardTankFlagsRelated(tp, 0);
    ok = stanTestLineUnobstructed(&st, prop->pos.x, prop->pos.z, tp->pos.x, tp->pos.z, 0x1B, 100.0f, 100.0f, 0.0f, 1.0f) != 0;
    /* on the camera's floor only (as cctv.sees, port_cctvview.c): the stan line ends on his tile, or at least in his room */
    if (ok && tp->stan != NULL && st != tp->stan && (st == NULL || (st->room & 0xFF) != (tp->stan->room & 0xFF))) ok = 0;
    if (tp->type == PROP_TYPE_CHR && tp->chr != NULL) chrSetMoving(tp->chr, TRUE);
    else bondviewUpdateGuardTankFlagsRelated(tp, 1);
    return ok;
}

static int camBodyDone(s32 chrnum)
{
    int i;

    for (i = 0; i < sCamBodyCnt; i++)
    {
        if (sCamBody[i] == chrnum) return 1;
    }
    return 0;
}

static void cameras(void)
{
    int cams[32], nc, k, p, n = (int) portBondCount(), i;

    if (!portHeistCoverIsOn() || alarmIsActive()) return;
    nc = portHeistCctvList(cams, 32);
    /* acts: a Bond slapping, robbing, searching or shooting in a camera's view */
    for (p = 0; p < n && p < 4; p++)
    {
        struct player *pl = g_playerPointers[p];

        if (!portHeistCoverActing(p)) { sActRep[p] = 0; continue; }
        if (sActRep[p] || !portBondInPlay(p) || pl == NULL || pl->prop == NULL || pl->bonddead) continue;
        for (k = 0; k < nc; k++)
        {
            CCTVRecord *cam = (CCTVRecord *) portHeistCctvRecord(cams[k]);

            if (cam != NULL && camSees(cam, pl->prop))
            {
                sActRep[p] = 1;
                sCamActN++;
                printf("ge: ai: camera %d saw Bond %d do something (an act): reported to the security room\n", cams[k], p);
                portEventEmit(PORT_EV_CCTV_SPOTTED, cams[k], p, 0, 0);
                break;
            }
        }
    }
    /* bodies: a dead or knocked-out chr lying in a camera's view (not with mode.body_check(false): bodies are not found at all) */
    if (g_ChrSlots == NULL || !portHeistBodyCheckOn()) return;
    for (i = 0; i < g_NumChrSlots; i++)
    {
        ChrRecord *b = &g_ChrSlots[i];

        if (b->model == NULL || b->prop == NULL || (b->hidden & CHRHIDDEN_REMOVE)) continue;
        if (!(chrIsDead(b) || portHeistKoIs(b->chrnum)) || camBodyDone(b->chrnum)) continue;
        for (k = 0; k < nc; k++)
        {
            CCTVRecord *cam = (CCTVRecord *) portHeistCctvRecord(cams[k]);

            if (cam != NULL && camSees(cam, b->prop))
            {
                if (sCamBodyCnt < CAMBODY_MAX) sCamBody[sCamBodyCnt++] = b->chrnum;
                sCamBodyN++;
                printf("ge: ai: camera %d sees a body (chr %d, %s): reported to the security room\n", cams[k], (int) b->chrnum,
                       chrIsDead(b) ? "dead" : "knocked out");
                portEventEmit(PORT_EV_CCTV_SPOTTED, cams[k], -1, 0, 0);
                break;
            }
        }
    }
}

/* ---- the tick (portHeistAiTick) ---- */

void portAiTick(void)
{
    int i;

    if (!portAiOn()) return;
    hookEvents();
    sNow += g_ClockTimer;
    sAcc += g_ClockTimer;
    sCamAcc += g_ClockTimer;
    if (sAcc >= AI_SCAN)
    {
        f32 dt = sAcc / 60.0f;

        sAcc = 0;
        footsteps(dt);
        {
            extern void portNoiseDoorsTick(void);

            portNoiseDoorsTick();
        }
        if (alarmIsActive() && !sAlarmWas)
        {
            for (i = 0; i < sGN; i++)
            {
                if (sG[i].used && !sG[i].down && sG[i].state < AS_ALERT) { setState(&sG[i], AS_ALERT); if (sG[i].level < 0.8f) sG[i].level = 0.8f; }
            }
        }
        sAlarmWas = alarmIsActive() ? 1 : 0;
        for (i = 0; i < sGN; i++)
        {
            if (sG[i].used) guardStep(&sG[i], dt);
        }
        checkins();
    }
    if (sCamAcc >= CAM_SCAN)
    {
        sCamAcc = 0;
        cameras();
    }
}

/* ---- reads ---- */

int portAiAwareness(int chrnum, int *state, float *level, float *lastx, float *lastz)
{
    AiG *g = gFind(chrnum);

    if (g == NULL) return 0;
    if (state) *state = g->state;
    if (level) *level = g->level;
    if (lastx) *lastx = g->nx;
    if (lastz) *lastz = g->nz;
    return 1;
}

const char *portAiStateName(int st) { return (st >= 0 && st < AS_N) ? sStateName[st] : "?"; }

/* the way chr faces, degrees (atan2(dx, dz) convention: a heading to a point is atan2(x - cx, z - cz)); -1000 none */
float portAiFace(int chrnum)
{
    ChrRecord *c = chrFindByLiteralId(chrnum);

    return (c != NULL && c->model != NULL) ? getsubroty(c->model) * (180.0f / 3.14159265f) : -1000.0f;
}

static const char *const sSubName[] = { "none", "turn", "go", "look", "return", "patrol" };

int portAiInfo(int chrnum, int *sub, const char **subName, float *postx, float *postz, int *looks, int *down, float *heardAgo)
{
    AiG *g = gFind(chrnum);

    if (g == NULL) return 0;
    *sub = g->sub;
    *subName = sSubName[g->sub < 6 ? g->sub : 0];
    *postx = g->postx;
    *postz = g->postz;
    *looks = g->looks;
    *down = g->down;
    *heardAgo = g->heardAt > 0 ? (sNow - g->heardAt) / 60.0f : -1.0f;
    return 1;
}

void portAiCounts(int *barks, int *searches, int *giveups, int *checkins_, int *backups, int *camacts, int *cambodies)
{
    *barks = sBarkN; *searches = sSearchN; *giveups = sGiveUpN; *checkins_ = sCheckinN; *backups = sBackupN; *camacts = sCamActN;
    *cambodies = sCamBodyN;
}

/* ai.tune: by name; < 0 keeps */
int portAiTune(const char *name, float v)
{
    static const struct { const char *n; f32 *p; } t[] = {
        { "turn", &sT.turn }, { "alert_turn", &sT.alertTurn }, { "look", &sT.look }, { "search_time", &sT.searchTime },
        { "search_radius", &sT.searchRadius }, { "decay", &sT.decay }, { "susp_level", &sT.suspLevel }, { "search_level", &sT.searchLevel },
        { "checkin", &sT.checkin }, { "backup_range", &sT.backupRange }, { "cover_noise", &sT.coverNoise }, { "step_speed", &sT.stepSpeed },
        { "run_speed", &sT.runSpeed }, { "step_every", &sT.stepEvery }, { "attribute", &sT.attribute }, { "near", &sT.near },
        { "lure", &sT.lure },
    };
    int i;

    if (name == NULL || v < 0.0f) return 0;
    if (strcmp(name, "search_pads") == 0) { sT.searchPads = (s32) v; return 1; }
    if (strcmp(name, "backup") == 0) { sT.backupN = (s32) v; return 1; }
    for (i = 0; i < (int) (sizeof(t) / sizeof(t[0])); i++)
    {
        if (strcmp(name, t[i].n) == 0) { *t[i].p = v; return 1; }
    }
    if (strncmp(name, "weight_", 7) == 0)
    {
        int k = portNoiseKindFind(name + 7);

        if (k >= 0) { sT.weight[k] = v; return 1; }
    }
    return 0;
}

/* folded into the heist state hash (port_heist.c portHeistStateHash) */
unsigned portAiHash(void)
{
    unsigned h = (unsigned) sOn * 3u + (unsigned) sGN * 5u + (unsigned) sNow * 7u + (unsigned) sAcc * 11u + (unsigned) sCamAcc * 13u;
    int i, p;

    if (!sOn) return h;
    h = h * 16777619u + (unsigned) sBarkN + (unsigned) sSearchN * 3u + (unsigned) sGiveUpN * 5u + (unsigned) sCheckinN * 7u
        + (unsigned) sBackupN * 11u + (unsigned) sCamActN * 13u + (unsigned) sCamBodyN * 17u + (unsigned) sFoundCnt * 19u + sAlarmWas;
    for (p = 0; p < 4; p++)
    {
        h = h * 16777619u + sActRep[p] + (unsigned) sBpOk[p] * 3u + (unsigned) (s32) sBpx[p] * 5u + (unsigned) (s32) sBpz[p] * 7u
            + (unsigned) (s32) (sStepT[p] * 100.0f) * 11u + (unsigned) sShotAt[p] * 13u;
    }
    for (i = 0; i < sCamBodyCnt; i++) h = h * 16777619u + (unsigned) sCamBody[i];
    for (i = 0; i < sGN; i++)
    {
        const AiG *g = &sG[i];

        if (!g->used) continue;
        h = h * 16777619u + (unsigned) g->chrnum + (unsigned) g->state * 3u + (unsigned) g->sub * 5u + (unsigned) g->nkind * 7u
            + (unsigned) g->down * 11u + (unsigned) g->missed * 13u + (unsigned) g->backup * 17u + (unsigned) g->looks * 19u;
        h = h * 16777619u + (unsigned) g->stateAt + (unsigned) g->subAt * 3u + (unsigned) g->heardAt * 5u + (unsigned) g->downAt * 7u
            + (unsigned) g->plotAt * 11u + (unsigned) g->barkAt * 13u + (unsigned) g->postPad * 17u + (unsigned) g->ridx * 19u;
        h = h * 16777619u + (unsigned) (s32) (g->level * 1000.0f) + (unsigned) (s32) g->nx * 3u + (unsigned) (s32) g->nz * 5u
            + (unsigned) (s32) g->tx * 7u + (unsigned) (s32) g->tz * 11u + (unsigned) g->src * 13u;
    }
    for (i = 0; i < sRouteN; i++) h = h * 16777619u + sRoute[i].n + (unsigned) sRoute[i].pad[0] * 3u;
    return h * 16777619u + portNoiseHash();
}

#endif /* TARGET_N64 */
