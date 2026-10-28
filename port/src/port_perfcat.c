/* port_perfcat.c - the arena's render-cost tags and the far-spectator impostors (presentation only; PRES in snap_embed.py).
 *
 * One hook pair in chrprop.c chrpropRender wraps every prop the world pass draws:
 *   portPerfPropBegin  writes a category marker (gfx_perfcat.h) when counting is on (bench.begin, GE_PERFCAT=1), and for
 *                      spectators / corpses / live chrs while crowd.batch is on, with the flag that sends their opaque
 *                      triangles to the render thread's chr batch (gfx_chrbatch.inl: one draw per draw state)
 *   portPerfPropEnd    puts the marker back to "level", or REWINDS the display list to where the prop began:
 *     - the benchmark's category toggles (bench.hide): that category's props are built and then dropped
 *     - a real spectator farther than crowd.lod() from the camera: dropped and drawn as an impostor of its own skin and pose in
 *       the impostor crowd's batch instead (port_crowd.c portCrowdFarAdd / portCrowdDraw, one GL draw for all of them)
 * The prop is always BUILT in full first (chrRenderProp runs as before: its matrices, render_pos, hit model, onscreen flags, the
 * dyn allocations), so the sim sees exactly what it saw without this file; only the commands handed to the renderer change.
 * Nothing here reads or writes sim state, and the camera distance it uses is the prop's view depth (zDepth) of the pass. */
#ifndef TARGET_N64

#include <stdlib.h>
#include <string.h>
#include <ultra64.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include "game/chraction.h"
#include "game/model.h"
#include "game/initanitable.h"
#include "game/lv.h"
#include "port_mode.h"
#include "../fast3d/gfx_perfcat.h"

extern s32 portArenaChrSpectator(const ChrRecord *chr);              /* port_arenamode.c */
extern int portCrowdFarAdd(const ChrRecord *chr, int cheer);           /* port_crowd.c: 1 = taken into the impostor batch */
extern int portCrowdFarHas(const ChrRecord *chr);                     /* ... it was, this pass */
extern int portCrowdFarReady(void);
extern void portConLogf(const char *fmt, ...);

volatile int g_PerfCatOn;          /* render thread reads it (gfx_perfcat.inl) */
static s32 sEnvOn = -1;            /* GE_PERFCAT=1: markers always */
static s32 sBenchOn;               /* bench.begin .. bench.finish */
static u32 sHide;                  /* bench.hide: bit per PERFCAT_* whose props are dropped */
static f32 sLodDist = -1.0f;       /* crowd.lod(): real spectators beyond this view depth draw as impostors; <= 0 off */
static s32 sEnvLod = -2;           /* GE_CROWD_LOD=<units> overrides (0 = off) */
static u32 sFarFrame[2];           /* per-pass statistics for the log (spectators kept real / swapped) */

static void updateOn(void)
{
    if (sEnvOn < 0)
    {
        const char *e = getenv("GE_PERFCAT");
        sEnvOn = (e != NULL && e[0] != '\0' && e[0] != '0') ? 1 : 0;
    }
    g_PerfCatOn = (sEnvOn || sBenchOn) ? 1 : 0;
}

/* port_bench.c: counting with timing while a phase is measured */
void portPerfCatBench(int on)
{
    sBenchOn = on;
    updateOn();
}

/* bench.hide(mask): drop these categories' props from the display list (the benchmark's attribution phases) */
void portPerfCatHide(unsigned mask)
{
    sHide = mask;
}

unsigned portPerfCatHidden(void)
{
    return sHide;
}

/* crowd.lod(units): real spectators farther than this from the camera draw as impostors (0 = never). Returns the value in force. */
float portPerfCrowdLod(float dist)
{
    if (dist >= 0.0f)
    {
        sLodDist = dist;
    }
    if (sEnvLod == -2)
    {
        const char *e = getenv("GE_CROWD_LOD");
        sEnvLod = (e != NULL && e[0] != '\0') ? atoi(e) : -1;
        if (sEnvLod >= 0)
        {
            portConLogf("ge: crowd: GE_CROWD_LOD=%d overrides the far-spectator distance\n", (int)sEnvLod);
        }
    }
    return sEnvLod >= 0 ? (f32)sEnvLod : sLodDist;
}

static s32 propCat(const PropRecord *prop)
{
    switch (prop->type)
    {
        case PROP_TYPE_CHR:
        {
            ChrRecord *chr = prop->chr;

            if (chr == NULL)
            {
                return PERFCAT_LIVE;
            }
            if (portArenaChrSpectator(chr))
            {
                return PERFCAT_SPECT;
            }
            if (chr->actiontype == ACT_DEAD || chr->actiontype == ACT_DIE)
            {
                return PERFCAT_CORPSE;
            }
            return PERFCAT_LIVE;
        }
        case PROP_TYPE_OBJ:
        case PROP_TYPE_WEAPON:
        case PROP_TYPE_DOOR:
            return PERFCAT_OBJ;
        case PROP_TYPE_EXPLOSION:
        case PROP_TYPE_SMOKE:
            return PERFCAT_FX;
        case PROP_TYPE_VIEWER:
            return PERFCAT_BOND;
        default:
            return PERFCAT_LEVEL;
    }
}

static Gfx *mark(Gfx *gdl, s32 cat)
{
    gDPNoOp(gdl);
    gdl->words.w1 = (PERFCAT_MAGIC << 24) | (u32)cat;
    return gdl + 1;
}

/* crowd.batch(on): the opaque triangles of spectators, corpses and live chrs are drawn once per draw state (gfx_chrbatch.inl)
 * instead of a draw per limb. GE_CHR_BATCH=0 forces it off. Returns the state in force. */
static s32 sBatch;
static s32 sEnvBatch = -2;

int portPerfBatch(int on)
{
    if (on >= 0)
    {
        sBatch = on ? 1 : 0;
    }
    if (sEnvBatch == -2)
    {
        const char *e = getenv("GE_CHR_BATCH");
        sEnvBatch = (e != NULL && e[0] != '\0') ? (e[0] != '0') : -1;
        if (sEnvBatch >= 0)
        {
            portConLogf("ge: crowd: GE_CHR_BATCH=%d overrides the chr batch\n", (int)sEnvBatch);
        }
    }
    return sEnvBatch >= 0 ? sEnvBatch : sBatch;
}

static s32 batchCat(s32 cat)
{
    return (cat == PERFCAT_SPECT || cat == PERFCAT_CORPSE || cat == PERFCAT_LIVE) && portPerfBatch(-1);
}

/* ---- baked corpses (gfx_corpsebake.inl) ----
 * crowd.bake(on): a corpse that has lain still for a while is captured once by the render thread and then drawn from that
 * capture (its opaque pass is still built here every frame, then dropped). GE_CORPSE_BAKE=0 forces it off. */
volatile int g_PortBakeDone[1024];   /* render thread: serial baked (> 0), -serial failed, 0 none */
static s32 sBake;
static s32 sEnvBake = -2;
typedef struct { s32 chrnum, serial, dead_since, retry_at; } BakeRec;
static BakeRec sBk[1024];
static s32 sBkSerialNext;
static PropRecord *sDecProp;         /* Begin's bake decision for the prop it opened (End acts on it) */
static s32 sDecMode;                 /* 0 none, 1 capture asked, 2 drawn from its bake */
static u32 sBakeCount[3];            /* the bench line: per view pass, corpses drawn normally / asked to bake / from bakes */
extern int portCrowdBakedAdd(int slot, int serial);   /* port_crowd.c */
extern int portArenaFogDrawn(void);                   /* port_arenafx.c: the arena fog is drawn (fading in, full or fading out) */
extern int portChrFxGet(int chrnum, void *out);       /* port_chrfx.c (Whacky): 1 = this chr has an effect */

int portPerfBake(int on)
{
    if (on >= 0)
    {
        sBake = on ? 1 : 0;
    }
    if (sEnvBake == -2)
    {
        const char *e = getenv("GE_CORPSE_BAKE");
        sEnvBake = (e != NULL && e[0] != '\0') ? (e[0] != '0') : -1;
        if (sEnvBake >= 0)
        {
            portConLogf("ge: crowd: GE_CORPSE_BAKE=%d overrides the corpse bake\n", (int)sEnvBake);
        }
    }
    return sEnvBake >= 0 ? sEnvBake : sBake;
}

#define BAKE_SETTLE_TICKS 90     /* g_GlobalTimer ticks dead before a bake is asked for (the body has stopped) */
#define BAKE_RETRY_TICKS  1800   /* after a failed bake (a translucent or fogged part): try again this much later */

static s32 bakeDecide(ChrRecord *chr)
{
    BakeRec *r;
    s32 slot, done, now = (s32)g_GlobalTimer;
    u8 fx[64];

    if (chr == NULL || chr->actiontype != ACT_DEAD || chr->model == NULL || portArenaFogDrawn() ||
        portChrFxGet((s32)chr->chrnum, fx))
    {
        return 0;
    }
    slot = (s32)((u32)chr->chrnum & 1023u);
    r = &sBk[slot];
    if (r->chrnum != (s32)chr->chrnum)
    {
        r->chrnum = (s32)chr->chrnum;
        r->serial = 0;
        r->dead_since = now;
        r->retry_at = 0;
        return 0;
    }
    if (now - r->dead_since < BAKE_SETTLE_TICKS)
    {
        return 0;
    }
    if (r->serial == 0)
    {
        if (now < r->retry_at)
        {
            return 0;
        }
        sBkSerialNext = (sBkSerialNext % 16383) + 1;   /* 1..16383: 14 bits in the marker */
        r->serial = sBkSerialNext;
    }
    done = g_PortBakeDone[slot];
    if (done == r->serial)
    {
        return 2;
    }
    if (done == -r->serial)
    {
        r->serial = 0;
        r->retry_at = now + BAKE_RETRY_TICKS;
        return 0;
    }
    return 1;   /* asked (again) until the render thread answers */
}

Gfx *portPerfPropBegin(Gfx *gdl, PropRecord *prop, s32 withalpha)
{
    s32 cat, bake = 0;

    if (sEnvOn < 0)
    {
        updateOn();   /* GE_PERFCAT=1 counts from the first frame */
    }
    sDecProp = NULL;
    sDecMode = 0;
    if (!portModeActive() || (!g_PerfCatOn && !portPerfBatch(-1) && !portPerfBake(-1)))
    {
        return gdl;
    }
    cat = propCat(prop);
    if (cat == PERFCAT_CORPSE && !withalpha && portPerfBake(-1) && !(sHide & (1u << cat)))
    {
        bake = bakeDecide(prop->chr);
        sDecProp = prop;
        sDecMode = bake;
        sBakeCount[bake]++;
    }
    if (!g_PerfCatOn && !batchCat(cat) && bake != 1)
    {
        return gdl;   /* nothing to say about this prop */
    }
    gdl = mark(gdl, cat);
    if (batchCat(cat))
    {
        gdl[-1].words.w1 |= PERFCAT_FLAG_BATCH;
    }
    if (bake == 1)
    {   /* capture this corpse (the category marker after it closes the capture) */
        const BakeRec *r = &sBk[(u32)prop->chr->chrnum & 1023u];

        gDPNoOp(gdl);
        gdl->words.w1 = (0x5Bu << 24) | ((u32)(r->serial & 0x3FFF) << 10) | ((u32)prop->chr->chrnum & 1023u);
        gdl++;
    }
    return gdl;
}

/* corpses per view pass drawn normally / asked to bake / drawn from their bakes, since the last call (the bench's report line) */
void portPerfBakeCounts(unsigned out[3])
{
    out[0] = sBakeCount[0];
    out[1] = sBakeCount[1];
    out[2] = sBakeCount[2];
    sBakeCount[0] = sBakeCount[1] = sBakeCount[2] = 0;
}

/* a real spectator's impostor pose: arms up while it plays a cheer (surrender, dance) or a boo with raised fists */
static s32 cheering(ChrRecord *chr)
{
    ModelAnimation *a;

    if (chr->model == NULL || chr->actiontype != ACT_ANIM)
    {
        return 0;
    }
    a = objecthandlerGetModelAnim(chr->model);
    if (a == NULL || modelGetAnimFrame(chr->model) >= modelGetAnimEndFrame(chr->model))
    {
        return 0;
    }
    return a == (ModelAnimation *)animation_table_ptrs1[ANIM_surrendering_unarmed] ||
           a == (ModelAnimation *)animation_table_ptrs1[ANIM_dancing] ||
           a == (ModelAnimation *)animation_table_ptrs1[ANIM_drop_weapon_and_show_fight_stance];
}

Gfx *portPerfPropEnd(Gfx *start, Gfx *gdl, PropRecord *prop, s32 withalpha)
{
    s32 cat;
    f32 lod;

    lod = portPerfCrowdLod(-1.0f);
    if (!portModeActive() || (!g_PerfCatOn && sHide == 0 && lod <= 0.0f && !portPerfBatch(-1) && !portPerfBake(-1)))
    {
        return gdl;
    }
    cat = propCat(prop);
    if (sHide & (1u << cat))
    {
        return start;   /* built, not drawn */
    }
    if (prop == sDecProp && sDecMode == 2 &&
        portCrowdBakedAdd((s32)((u32)prop->chr->chrnum & 1023u), sBk[(u32)prop->chr->chrnum & 1023u].serial))
    {
        sDecProp = NULL;
        return start;   /* built, drawn from its bake (port_crowd.c CROWD_CORPSES) */
    }
    if (prop == sDecProp && sDecMode == 1)
    {
        sDecProp = NULL;
        return mark(gdl, PERFCAT_LEVEL);   /* closes the capture */
    }
    if (cat == PERFCAT_SPECT && lod > 0.0f && prop->zDepth > lod && portCrowdFarReady())
    {
        /* the opaque pass hands it to the impostor batch; the translucent pass (its shadow, any xlu parts) follows that choice.
         * A spectator the batch cannot take (no atlas cell for its skin, the list full) stays a real model in both passes. */
        if (withalpha ? portCrowdFarHas(prop->chr) : portCrowdFarAdd(prop->chr, cheering(prop->chr)))
        {
            sFarFrame[1] += withalpha ? 0 : 1;
            return start;
        }
    }
    if (cat == PERFCAT_SPECT && !withalpha)
    {
        sFarFrame[0]++;
    }
    return (g_PerfCatOn || batchCat(cat)) ? mark(gdl, PERFCAT_LEVEL) : gdl;   /* closes Begin's marker (and its batch flag) */
}

/* port_crowd.c portCrowdStageLoaded: the arena sets these again at its stage start; any other stage runs without them */
void portPerfStageReset(void)
{
    sHide = 0;
    sLodDist = -1.0f;
    sBatch = 0;
    sBake = 0;
    memset(sBk, 0, sizeof(sBk));   /* (the render thread's bakes of the last stage are never asked for again: new serials) */
}

/* builds without the GL renderer (GE_VIDEO=OFF) have no gfx_perfcat.inl: nothing counted */
__attribute__((weak)) void gfx_perfcat_last(PerfCatFrame *out)
{
    memset(out, 0, sizeof(*out));
}

__attribute__((weak)) void gfx_corpsebake_stats(unsigned int out[5])
{
    memset(out, 0, 5 * sizeof(unsigned int));
}

/* spectators drawn as real models / as impostors since the last call (the bench's report line) */
void portPerfCrowdCounts(unsigned *real, unsigned *far)
{
    *real = sFarFrame[0];
    *far = sFarFrame[1];
    sFarFrame[0] = sFarFrame[1] = 0;
}

#endif
