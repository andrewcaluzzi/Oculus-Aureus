/* port_traits.c - guard personalities for the heist (designs/bank-heist.md section 32; owned by the personalities stream).
 * AC (2026-10-26, approved): each carrier / guard a little puzzle. tools/heist_traits.lua deals each of the bank's own guards one trait
 * from the sim RNG (same seed, same deal); the ai layer (port_ai.c) and the combat layer (port_combat.c) read the per-guard values here
 * through a handful of one-line hooks, and the banker cover (port_heist.c coverScan) reads the suspicion rate.
 *
 *   ROOKIE      hears more (x1.6), his cover meter fills faster (x1.6), turns quicker; radios in on a mere suspicion ("Control, I think
 *               I saw something...": a colleague comes to look); in a fight he aims badly (accuracy -40) and falls back sooner
 *               (retreat_hp x1.6); gives up easily at gunpoint (surrender x2, for the hostages stream).
 *   VETERAN     hears less (x0.55), slow to turn (x1.8), "Probably nothing."; on a watchclock tour he skips stations (`skip`, 0.5 a
 *               station: a gap to slip through, the debrief's "Clocked Out"); his own missed check-in is noticed late (x1.5); deadly
 *               once engaged (accuracy +10: twice the stock hit chance) and holds his ground (retreat_hp x0.5).
 *   BY-THE-BOOK every noise he notices gets a full search (never a look from where he stands) with one more pad; his check-in is missed
 *               early (x0.6); he calls one more guard for backup.
 *   BRIBABLE    a use press face to face while he suspects nothing (ai.trait_bribe, the mode's crew funds): he looks away for good (his
 *               cover meter never fills, he ignores noises but a shot) and hands over his card if he carries one.
 *
 * Every value is neutral (1 for a multiplier, 0 for an addition) when the traits are off or the guard has none, so with
 * S.personalities = false the layers run exactly as before. The deal, the bribes, the watchclock skips, what the crew has observed and
 * the counters are sim state: in portTraitHash (folded into the heist state hash only while on) and a SIM file for the snapshot
 * (scripts/port/snap_embed.py). The skip draws the sim RNG (randomGetNext), only for a veteran at a station. Barks' extra sounds are
 * presentation (port_gamesfx.c, ROM sounds by id). PC only, mode only. */
#ifndef TARGET_N64

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <ultra64.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include "random.h"
#include "game/chr.h"
#include "game/chraction.h"
#include "game/stan.h"
#include "game/player.h"
#include "port_blocks.h"
#include "port_mode.h"
#include "port_lua.h"
#include "port_noise.h"
#include "port_traits.h"

extern s32 g_ClockTimer;
extern bool alarmIsActive(void);
extern void bondviewUpdateGuardTankFlagsRelated(PropRecord *prop, s32 flag);
extern int portHeistKoIs(int chrnum);
extern int portHeistCoverAlert(void);
extern float portHeistCoverSusp(int chrnum, int p);
extern int portAiAwareness(int chrnum, int *state, float *level, float *lastx, float *lastz);
extern int portAiBark(int chrnum, const char *kind, const char *text);
extern int portAiNearestFree(float x, float y, float z, float range, int exclude);
extern int portAiSend(int chrnum, float x, float y, float z, int run, const char *text);
extern int portGameSfxPlay(int sfx, float vol, float pitch, float delay, float pan);

#define TR_MAX     256
#define ST_MAX     32
#define TR_SCAN    15              /* ticks between the observation / gap scans (4 Hz) */
#define OBS_RANGE  1400.0f         /* a Bond with the line to a guard this close has had a look at him */
#define GAP_RANGE  320.0f          /* a Bond this close to a skipped station while the gap is open slipped through it */
#define GAP_TIME   (45 * 60)

typedef struct
{
    s32 chrnum;
    u8 used, trait, bribed, bribeBy, obs, radioPend, accSet;
    s8 acc0;
    s16 skipPad;
    s32 skipAt, radioAt, radioDue, skips, clocked;
    f32 rx, ry, rz;
} Tr;

static Tr sTr[TR_MAX];
static int sTrN, sOn, sAcc;
static s32 sNow;
static s16 sStation[ST_MAX];
static int sStationN;
static s32 sSkipN, sClockedN, sBribeN, sBribeCost, sRadioN, sObsN, sDealt[TRAIT_N];
static struct { f32 skip, radioCd, radioRange; s32 accRookie, accVeteran; } sTt;

static const char *const sName[TRAIT_N] = { "none", "rookie", "veteran", "book", "bribable" };

/* TV_HEAR, TV_SUSP, TV_TURN, TV_CHECKIN, TV_RETREAT, TV_SURRENDER, TV_PADS, TV_BACKUP, TV_FULL */
static const f32 sVal[TRAIT_N][TV_N] = {
    { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f },       /* none */
    { 1.6f, 1.6f, 0.6f, 1.0f, 1.6f, 2.0f, 0.0f, 0.0f, 0.0f },       /* rookie */
    { 0.55f, 0.7f, 1.8f, 1.5f, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f },      /* veteran */
    { 1.0f, 1.0f, 1.0f, 0.6f, 1.0f, 0.8f, 1.0f, 1.0f, 1.0f },       /* by the book */
    { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.2f, 0.0f, 0.0f, 0.0f },       /* bribable */
};
static const f32 sNeutral[TV_N] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f };

static void tuneDefaults(void)
{
    sTt.skip = 0.5f;           /* a veteran skips a watchclock station on his tour with this chance (each time he comes to one) */
    sTt.radioCd = 20.0f;       /* a rookie radios a suspicion at most once in this long */
    sTt.radioRange = 1600.0f;  /* ... and the nearest free colleague within this comes to look */
    sTt.accRookie = -40;       /* accuracy rating added (the stock aim x (100 + r) / 100 below 0, x (1 + r / 10) above) */
    sTt.accVeteran = 10;
}

void portTraitStageReset(void)
{
    memset(sTr, 0, sizeof(sTr));
    sTrN = sOn = sAcc = 0;
    sNow = 0;
    sStationN = 0;
    sSkipN = sClockedN = sBribeN = sBribeCost = sRadioN = sObsN = 0;
    memset(sDealt, 0, sizeof(sDealt));
    tuneDefaults();
}

int portTraitOn(void) { return sOn && portModeActive(); }

int portTraitEnable(int on) { sOn = on ? 1 : 0; return 1; }

static Tr *tFind(s32 chrnum)
{
    int i;

    if (!sOn) return NULL;
    for (i = 0; i < sTrN; i++)
    {
        if (sTr[i].used && sTr[i].chrnum == chrnum) return &sTr[i];
    }
    return NULL;
}

static int chrUp(ChrRecord *c)
{
    return c != NULL && c->prop != NULL && c->model != NULL && !(c->hidden & CHRHIDDEN_REMOVE) && !chrIsDead(c) && !portHeistKoIs(c->chrnum)
        && c->actiontype != ACT_DIE && c->actiontype != ACT_DEAD;
}

int portTraitFind(const char *name)
{
    int k;

    if (name == NULL) return -1;
    for (k = 0; k < TRAIT_N; k++)
    {
        if (strcmp(name, sName[k]) == 0) return k;
    }
    if (strcmp(name, "by_the_book") == 0 || strcmp(name, "by-the-book") == 0) return TRAIT_BOOK;
    return -1;
}

const char *portTraitName(int t) { return (t >= 0 && t < TRAIT_N) ? sName[t] : "?"; }

/* ai.trait(chr, name): give a guard his trait (the deal, a test). His aim changes with it (the stock accuracy rating). */
int portTraitSet(int chrnum, int trait)
{
    ChrRecord *c = chrFindByLiteralId(chrnum);
    Tr *e;
    int i, acc;

    if (!portTraitOn() || c == NULL || trait < 0 || trait >= TRAIT_N) return 0;
    e = tFind(chrnum);
    if (e == NULL)
    {
        for (i = 0; i < sTrN && sTr[i].used; i++) ;
        if (i == sTrN)
        {
            if (sTrN >= TR_MAX) return 0;
            sTrN++;
        }
        e = &sTr[i];
        memset(e, 0, sizeof(*e));
        e->used = 1;
        e->chrnum = chrnum;
        e->skipPad = -1;
    }
    else if (e->trait < TRAIT_N && sDealt[e->trait] > 0)
    {
        sDealt[e->trait]--;
    }
    e->trait = (u8) trait;
    sDealt[trait]++;
    if (!e->accSet) { e->acc0 = c->accuracyrating; e->accSet = 1; }
    acc = e->acc0 + (trait == TRAIT_ROOKIE ? sTt.accRookie : (trait == TRAIT_VETERAN ? sTt.accVeteran : 0));
    if (acc < -100) acc = -100;
    if (acc > 100) acc = 100;
    c->accuracyrating = (s8) acc;
    return 1;
}

int portTraitOf(int chrnum)
{
    Tr *e = tFind(chrnum);

    return (e != NULL && portModeActive()) ? e->trait : TRAIT_NONE;
}

float portTraitVal(int chrnum, int field)
{
    Tr *e;

    if (field < 0 || field >= TV_N) return 0.0f;
    e = tFind(chrnum);
    if (e == NULL || !portModeActive()) return sNeutral[field];
    if (e->bribed && (field == TV_SUSP || field == TV_HEAR)) return 0.0f;   /* paid to look away */
    return sVal[e->trait][field];
}

int portTraitDeaf(int chrnum, int noiseKind)
{
    Tr *e = tFind(chrnum);

    return e != NULL && e->bribed && noiseKind != NOISE_SHOT && noiseKind != NOISE_ALARM;
}

/* ---- barks in his own voice (BK_*: huh, hey, investigate, lost, found_body, radio, backup, surrender) ---- */

const char *portTraitText(int chrnum, int barkKind, const char *text)
{
    static const char *const lines[TRAIT_N][8] = {
        { NULL },
        { "W-what was that?", "Oh God - hey! HEY!", "I-I'll go look...", "It was nothing... right? Right.", NULL, NULL, "Help! I need help here!", NULL },
        { "...Probably nothing.", NULL, "Fine. I'll look.", "Told you. Nothing.", NULL, NULL, NULL, NULL },
        { "Noise. Logging it.", NULL, "Investigating, per procedure.", "Area clear. Logging it, Control.", NULL, NULL,
          "Contact! Requesting backup, all units!", NULL },
        { "Hm?", NULL, NULL, "Not paid enough for this.", NULL, NULL, NULL, NULL },
    };
    Tr *e = tFind(chrnum);

    if (e == NULL || barkKind < 0 || barkKind >= 8 || !portModeActive()) return text;
    if (e->bribed && (barkKind == 0 || barkKind == 3)) return barkKind == 0 ? "I didn't hear anything." : "Didn't see a thing.";
    if (lines[e->trait][barkKind] != NULL) return lines[e->trait][barkKind];
    return text;
}

/* presentation: each trait's own extra stock sound on a bark (a rookie's gasp, a veteran's smoker's cough, the book's radio log) */
void portTraitBarkSfx(int chrnum, int barkKind, float vol, float pan)
{
    Tr *e = tFind(chrnum);

    if (e == NULL || vol <= 0.0f) return;
    switch (e->trait)
    {
        case TRAIT_ROOKIE:   if (barkKind <= 1 || barkKind == 6) portGameSfxPlay(GET_HIT_MALE1_SFX, vol * 0.7f, 1.45f, 0.15f, pan); break;
        case TRAIT_VETERAN:  if (barkKind == 0 || barkKind == 3) portGameSfxPlay(COUGH_SFX, vol * 0.8f, 0.8f, 0.35f, pan); break;
        case TRAIT_BOOK:     if (barkKind == 2 || barkKind == 3) portGameSfxPlay(RADIO_SFX, vol * 0.6f, 1.25f, 0.4f, pan); break;
        default: break;
    }
}

/* ---- the rookie radios a suspicion; the veteran skips stations ---- */

void portTraitSuspicious(int chrnum, float x, float y, float z)
{
    Tr *e = tFind(chrnum);

    if (e == NULL || e->trait != TRAIT_ROOKIE || e->bribed || e->radioPend) return;
    if (e->radioAt > 0 && sNow - e->radioAt < (s32) (sTt.radioCd * 60.0f)) return;
    e->radioPend = 1;
    e->radioDue = sNow + 36;   /* after his "W-what was that?" (the bark limit is 0.5 s a guard) */
    e->rx = x; e->ry = y; e->rz = z;
}

static int isStation(int pad)
{
    int i;

    for (i = 0; i < sStationN; i++)
    {
        if (sStation[i] == pad) return 1;
    }
    return 0;
}

int portTraitSkipPad(int chrnum, int pad)
{
    Tr *e = tFind(chrnum);

    if (e == NULL || e->trait != TRAIT_VETERAN || !isStation(pad) || sTt.skip <= 0.0f) return 0;
    if ((f32) (randomGetNext() % 10000u) / 10000.0f >= sTt.skip) return 0;
    e->skips++;
    e->skipPad = (s16) pad;
    e->skipAt = sNow;
    sSkipN++;
    printf("ge: traits: veteran chr %d skips watchclock station pad %d (skip %d)\n", (int) chrnum, pad, (int) e->skips);
    return 1;
}

int portTraitStations(const int *pads, int n)
{
    int i;

    sStationN = 0;
    for (i = 0; i < n && sStationN < ST_MAX; i++) sStation[sStationN++] = (s16) pads[i];
    return sStationN;
}

/* ---- bribes ---- */

/* ai.trait_bribe(chr, p, cost): 1 paid off, 0 not bribable, -1 refused (he is suspicious, or the alarm is up), 2 already paid */
int portTraitBribe(int chrnum, int p, int cost)
{
    Tr *e = tFind(chrnum);
    ChrRecord *c = chrFindByLiteralId(chrnum);
    int st = 0;
    float lvl = 0.0f, susp;

    if (!portTraitOn() || e == NULL || e->trait != TRAIT_BRIBABLE || !chrUp(c)) return 0;
    if (e->bribed) return 2;
    susp = portHeistCoverSusp(chrnum, p);
    portAiAwareness(chrnum, &st, &lvl, NULL, NULL);
    if (alarmIsActive() || portHeistCoverAlert() || susp >= 0.25f || st >= 1 || lvl >= 0.15f)
    {
        printf("ge: traits: chr %d refuses Bond %d's money (meter %.2f, awareness %d %.2f)\n", chrnum, p, (double) susp, st, (double) lvl);
        return -1;
    }
    e->bribed = 1;
    e->bribeBy = (u8) p;
    sBribeN++;
    sBribeCost += cost > 0 ? cost : 0;
    printf("ge: traits: chr %d takes Bond %d's money (%d): he looks away\n", chrnum, p, cost);
    return 1;
}

/* ---- what the crew has seen of each guard ---- */

int portTraitObserve(int chrnum, int p)
{
    Tr *e = tFind(chrnum);

    if (e == NULL || p < 0 || p > 3) return 0;
    if (!(e->obs & (1 << p)))
    {
        if (e->obs == 0) sObsN++;
        e->obs |= (u8) (1 << p);
    }
    return 1;
}

static int bondHasLine(PropRecord *bp, ChrRecord *c)
{
    StandTile *st = bp->stan;
    int ok;

    if (st == NULL || c->prop->stan == NULL) return 0;
    chrSetMoving(c, FALSE);
    bondviewUpdateGuardTankFlagsRelated(bp, 0);
    ok = stanTestLineUnobstructed(&st, bp->pos.x, bp->pos.z, c->prop->pos.x, c->prop->pos.z,
                                  CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_PATHBLOCKER | CDTYPE_AIOPAQUE, 120.0f, 120.0f, 0.0f, 1.0f) != 0
         && st == c->prop->stan;
    bondviewUpdateGuardTankFlagsRelated(bp, 1);
    chrSetMoving(c, TRUE);
    return ok;
}

static void scan(void)
{
    int i, p, n = (int) portBondCount();

    if (n > 4) n = 4;
    for (i = 0; i < sTrN; i++)
    {
        Tr *e = &sTr[i];
        ChrRecord *c;

        if (!e->used) continue;
        c = chrFindByLiteralId(e->chrnum);
        if (!chrUp(c)) continue;
        for (p = 0; p < n; p++)
        {
            struct player *pl = g_playerPointers[p];
            f32 dx, dy, dz;

            if ((e->obs & (1 << p)) || !portBondInPlay(p) || pl == NULL || pl->prop == NULL || pl->bonddead) continue;
            dx = c->prop->pos.x - pl->prop->pos.x; dy = c->prop->pos.y - pl->prop->pos.y; dz = c->prop->pos.z - pl->prop->pos.z;
            if (dy > 300.0f || dy < -300.0f || dx * dx + dz * dz > OBS_RANGE * OBS_RANGE) continue;
            if (bondHasLine(pl->prop, c)) portTraitObserve(e->chrnum, p);
        }
        /* a veteran's skipped station: the gap is open until he comes round again (or GAP_TIME) */
        if (e->trait == TRAIT_VETERAN && e->skipPad >= 0)
        {
            float px, py, pz;

            if (sNow - e->skipAt > GAP_TIME || !portLuaGamePadPos(e->skipPad, &px, &py, &pz)) { e->skipPad = -1; continue; }
            for (p = 0; p < n; p++)
            {
                struct player *pl = g_playerPointers[p];
                f32 dx, dy, dz;

                if (!portBondInPlay(p) || pl == NULL || pl->prop == NULL || pl->bonddead) continue;
                dx = px - pl->prop->pos.x; dy = py - pl->prop->pos.y; dz = pz - pl->prop->pos.z;
                if (dy > 200.0f || dy < -200.0f || dx * dx + dz * dz > GAP_RANGE * GAP_RANGE) continue;
                e->clocked++;
                sClockedN++;
                printf("ge: traits: Bond %d slipped through veteran chr %d's skipped station (pad %d, %.1f s after the skip)\n", p,
                       (int) e->chrnum, (int) e->skipPad, (sNow - e->skipAt) / 60.0);
                e->skipPad = -1;
                break;
            }
        }
    }
}

/* port_heist.c, after the ai layer's tick */
void portTraitTick(void)
{
    int i;

    if (!portTraitOn()) return;
    sNow += g_ClockTimer;
    sAcc += g_ClockTimer;
    for (i = 0; i < sTrN; i++)
    {
        Tr *e = &sTr[i];
        ChrRecord *c;

        if (!e->used || !e->radioPend || sNow < e->radioDue) continue;
        e->radioPend = 0;
        e->radioAt = sNow;
        c = chrFindByLiteralId(e->chrnum);
        if (!chrUp(c) || alarmIsActive()) continue;
        sRadioN++;
        portAiBark(e->chrnum, "radio", "Control, I think I saw something...");
        {
            int h = portAiNearestFree(e->rx, e->ry, e->rz, sTt.radioRange, e->chrnum);

            printf("ge: traits: rookie chr %d radios a suspicion at %.0f,%.0f: %s\n", (int) e->chrnum, e->rx, e->rz,
                   h >= 0 ? "a colleague comes" : "nobody free");
            if (h >= 0) portAiSend(h, e->rx, e->ry, e->rz, 0, "Copy. I'll swing by and look.");
        }
    }
    if (sAcc >= TR_SCAN)
    {
        sAcc = 0;
        scan();
    }
}

/* ---- reads ---- */

int portTraitCount(void) { return sOn ? sTrN : 0; }

int portTraitEntry(int i, int *chrnum, int *trait, int *obs, int *bribed, int *skips, int *clocked, int *skipPad)
{
    if (!sOn || i < 0 || i >= sTrN || !sTr[i].used) return 0;
    *chrnum = sTr[i].chrnum; *trait = sTr[i].trait; *obs = sTr[i].obs; *bribed = sTr[i].bribed; *skips = sTr[i].skips; *clocked = sTr[i].clocked;
    *skipPad = sTr[i].skipPad;
    return 1;
}

void portTraitStats(int *skips, int *clocked, int *bribes, int *cost, int *radios, int *observed, int *dealt)
{
    int k;

    *skips = sSkipN; *clocked = sClockedN; *bribes = sBribeN; *cost = sBribeCost; *radios = sRadioN; *observed = sObsN;
    for (k = 0; k < TRAIT_N; k++) dealt[k] = sDealt[k];
}

int portTraitTune(const char *name, float v)
{
    if (name == NULL || v < -1000.0f) return 0;
    if (strcmp(name, "skip") == 0) { sTt.skip = v; return 1; }
    if (strcmp(name, "radio_cooldown") == 0) { sTt.radioCd = v; return 1; }
    if (strcmp(name, "radio_range") == 0) { sTt.radioRange = v; return 1; }
    if (strcmp(name, "acc_rookie") == 0) { sTt.accRookie = (s32) v; return 1; }
    if (strcmp(name, "acc_veteran") == 0) { sTt.accVeteran = (s32) v; return 1; }
    return 0;
}

/* folded into the heist state hash (port_heist.c) only while the traits are on: off, the hash is the one before this stream */
unsigned portTraitHash(void)
{
    unsigned h = (unsigned) sTrN * 3u + (unsigned) sNow * 5u + (unsigned) sAcc * 7u + (unsigned) sStationN * 11u;
    int i;

    h = h * 16777619u + (unsigned) sSkipN + (unsigned) sClockedN * 3u + (unsigned) sBribeN * 5u + (unsigned) sBribeCost * 7u
        + (unsigned) sRadioN * 11u + (unsigned) sObsN * 13u + (unsigned) (s32) (sTt.skip * 1000.0f) * 17u;
    for (i = 0; i < sStationN; i++) h = h * 16777619u + (unsigned) sStation[i];
    for (i = 0; i < sTrN; i++)
    {
        const Tr *e = &sTr[i];

        if (!e->used) continue;
        h = h * 16777619u + (unsigned) e->chrnum + (unsigned) e->trait * 3u + (unsigned) e->bribed * 5u + (unsigned) e->bribeBy * 7u
            + (unsigned) e->obs * 11u + (unsigned) e->radioPend * 13u + (unsigned) e->skipPad * 17u + (unsigned) e->acc0 * 19u;
        h = h * 16777619u + (unsigned) e->skipAt + (unsigned) e->radioAt * 3u + (unsigned) e->radioDue * 5u + (unsigned) e->skips * 7u
            + (unsigned) e->clocked * 11u + (unsigned) (s32) e->rx * 13u + (unsigned) (s32) e->rz * 17u;
    }
    return h;
}

#endif /* TARGET_N64 */
