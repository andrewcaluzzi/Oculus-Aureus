/* port_snap.c - rollback snapshots: save / restore the simulation state, the ring, the re-sim test drivers (rollback job R2).
 *
 * What a snapshot holds:
 *   - the sim statics: the ranges embedded in the exe at link time (g_PortSnapTable, scripts/port/snap_embed.py), plus ranges that
 *     code registers (portSnapAddRange), minus the "live" ranges (portSnapLive: input accumulators, wall-clock samples);
 *   - the used part of the memp pools STAGE / ME / PERMANENT, without the master display list buffers (g_GfxBuffers: drawing only);
 *   - dynamic extents registered with portSnapAddDyn: the Lua VM arena, the sim high heap (converted model display lists), the model
 *     texture pool.
 * Not in it (presentation, never rewound): audio, scheduler / VI / framebuffers / thread stacks, input devices, the renderer and its
 * caches, the console, the network transport, wall-clock bookkeeping. The classification is scripts/port/snap_embed.py.
 *
 * A restore compares page by page and copies back only the pages that differ. The differing ranges are handed to the renderer
 * (gfx_invalidate_ranges) so its caches keyed on pool contents (textures, retained room / part lists) are dropped for exactly the
 * memory that changed. A restore must run with no gfx task in flight (boss.c waits for pendingGfx == 0 when the next tick restores).
 *
 * Test drivers (env, all off by default; they work in solo, co-op and netplay):
 *   GE_SNAPRESIM=T,K[,every]  at the start of tick T save; at the start of tick T+K restore and re-simulate ticks T..T+K-1 sim-only
 *                             inside that frame, then carry on; with <every> the pair repeats every <every> ticks.
 *   GE_SNAPSTRESS=D[,start]   from tick <start>: restore and re-simulate the last D ticks at EVERY tick (constant rollback of depth D).
 *   GE_SNAPRING=N             save a snapshot at every tick into a ring of N (the production cost, no restore).
 *   GE_SNAPCMP=1              after a re-sim compare the whole live state with the state the restore started from, byte for byte.
 * Ticks are counted from 1 per stage (the number of port_simonly.c, the co-op / netplay fingerprint tick). */
#ifndef TARGET_N64

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ultra64.h>
#include <memp.h>
#include "port_snap.h"

extern MemoryPool g_mempPools[];
extern unsigned long long portProfNow(void);                 /* pcsched.c, microseconds */
extern unsigned int portSimOnlyTickCount(void);              /* port_simonly.c: ticks begun in this stage */
extern void portConLogf(const char *fmt, ...);
extern void portBossResim(int ticks, unsigned int fromTick); /* boss.c: re-simulate n ticks sim-only, polling the pads in between */
extern s32 ge_joy_reads;                                     /* joy.c: controller polls that read a sample */
extern u8 *g_GfxBuffers[3];                                  /* dyn.c: the master display list buffers (drawing only: not saved) */
extern u8 *g_VtxBuffers[3];                                  /* dyn.c: the per-frame vertex / matrix pool */
extern void gfx_invalidate_ranges(const unsigned int (*r)[2], int n);   /* gfx_pc.cpp (weak no-op below) */

__attribute__((weak)) void gfx_invalidate_ranges(const unsigned int (*r)[2], int n)
{
    (void) r;
    (void) n;
}

int g_PortResim;

#define PAGE 4096u
#define RING_MAX 64
#define DIRTY_MAX 2048

typedef struct { u8 *p; u32 n; u32 off; u8 kind; u8 grp; } Seg;        /* kind 0 = static range, 1 = pool / arena (page compare on restore) */
typedef struct { u32 tick; int valid; u8 *buf; u32 cap, size; Seg *seg; int nseg, segCap; } Slot;
typedef struct { u8 *lo; u32 n; } Rng;
typedef struct { const char *name; PortSnapDynFn fn; } Dyn;

static int sInit, sOn, sTableOk;
static Rng *sStatic;                    /* the merged static ranges (table + extras - live) */
static int sNStatic;
static Rng *sExtra, *sLive, *sBenign;
static int sNExtra, sNLive, sNBenign;
static Dyn sDyn[8];
static int sNDyn;
static Seg *sCur;                       /* the segment list being built */
static int sNCur, sCurCap;
static u32 sGrp[8];                     /* bytes by group of the last save: statics, stage, me, perm, dyn... */

/* ---- scheduling ---- */
static int sRingN;                      /* >0: save at every tick into a ring of this many */
static int sRsT, sRsK, sRsEvery;        /* GE_SNAPRESIM */
static int sStressD, sStressStart;      /* GE_SNAPSTRESS */
static int sCmp;
static int sHashFrom, sHashTo;           /* GE_SNAPHASH */
static Slot sRing[RING_MAX];
static Slot sKeep, sPre;
static int sSoloReq;                    /* solo savestates (below): 1 save, 2 restore at the next tick begin */

/* ---- cost counters ---- */
static unsigned long long sSaveSum, sSaveMax, sSaveN, sRestSum, sRestMax, sRestN, sResimSum, sResimTicks, sResimMax, sFrames;
static unsigned long long sDirtySum;
static u32 sLastSize;
static u32 sResimEvents, sCmpFail;

/* The pump polls the controllers once per retrace, and a held netplay tick or a slow frame makes the count between two ticks vary. The
 * ring, the poll counters and the edge latches depend on it, so each tick admit records how many polls happened since the previous one,
 * and a re-simulation polls exactly that many times between its ticks. */
#define POLLHIST 64
static unsigned char sPollHist[POLLHIST];
static s32 sLastReads;

int portSnapPollsBefore(unsigned int tick)
{
    return sPollHist[tick % POLLHIST];
}

/* ---- the dirty ranges of the last restore ---- */
static unsigned int sDirty[DIRTY_MAX][2];
static int sNDirty;
static int sDirtyOverflow;

/* ------------------------------------------------------------------------------------------------------------------------------ */

static const PortSnapEnt *tableFind(unsigned int a)
{
    const PortSnapTable *t = &g_PortSnapTable;
    unsigned int lo = 0, hi = t->count;

    while (lo < hi)
    {
        const unsigned int mid = (lo + hi) / 2;

        if (t->ent[mid].addr + t->ent[mid].size <= a)
        {
            lo = mid + 1;
        }
        else
        {
            hi = mid;
        }
    }
    return (lo < t->count && t->ent[lo].addr <= a) ? &t->ent[lo] : NULL;
}

static const char *whatIs(const void *p, char *buf, size_t bl)
{
    const unsigned int a = (unsigned int) (uintptr_t) p;
    const PortSnapEnt *e = tableFind(a);
    int i;

    if (e != NULL)
    {
        snprintf(buf, bl, "static %s+0x%x @%08x", g_PortSnapTable.str + e->nameOff, a - e->addr, a);
        return buf;
    }
    for (i = 0; i < 3; i++)
    {
        static const char *const nm[3] = { "stage", "me", "perm" };
        static const int pid[3] = { MEMPOOL_STAGE, MEMPOOL_ME, MEMPOOL_PERMANENT };
        const MemoryPool *mp = &g_mempPools[pid[i]];

        if (mp->start != NULL && (u8 *) p >= mp->start && (u8 *) p < mp->end)
        {
            snprintf(buf, bl, "%s+0x%x", nm[i], (unsigned) ((u8 *) p - mp->start));
            return buf;
        }
    }
    for (i = 0; i < sNExtra; i++)
    {
        if ((u8 *) p >= sExtra[i].lo && (u8 *) p < sExtra[i].lo + sExtra[i].n)
        {
            snprintf(buf, bl, "extra#%d+0x%x", i, (unsigned) ((u8 *) p - sExtra[i].lo));
            return buf;
        }
    }
    for (i = 0; i < sNDyn; i++)
    {
        u8 *lo;
        u32 n;
        int k;

        for (k = 0; sDyn[i].fn(k, &lo, &n); k++)
        {
            if ((u8 *) p >= lo && (u8 *) p < lo + n)
            {
                snprintf(buf, bl, "%s+0x%x", sDyn[i].name, (unsigned) ((u8 *) p - lo));
                return buf;
            }
        }
    }
    snprintf(buf, bl, "%p", p);
    return buf;
}

/* ---- registration ---- */

void portSnapAddRange(const char *name, const void *p, unsigned int n)
{
    (void) name;
    if (p == NULL || n == 0)
    {
        return;
    }
    sExtra = (Rng *) realloc(sExtra, (size_t) (sNExtra + 1) * sizeof(Rng));
    sExtra[sNExtra].lo = (u8 *) p;
    sExtra[sNExtra].n = n;
    sNExtra++;
    sNStatic = -1;      /* rebuild */
}

void portSnapLive(const char *name, const void *p, unsigned int n)
{
    (void) name;
    if (p == NULL || n == 0)
    {
        return;
    }
    sLive = (Rng *) realloc(sLive, (size_t) (sNLive + 1) * sizeof(Rng));
    sLive[sNLive].lo = (u8 *) p;
    sLive[sNLive].n = n;
    sNLive++;
    sNStatic = -1;
}

void portSnapAddDyn(const char *name, PortSnapDynFn fn)
{
    if (sNDyn < 8)
    {
        sDyn[sNDyn].name = name;
        sDyn[sNDyn].fn = fn;
        sNDyn++;
    }
}

void portSnapBenign(const char *name, const void *p, unsigned int n)
{
    (void) name;
    if (p == NULL || n == 0)
    {
        return;
    }
    sBenign = (Rng *) realloc(sBenign, (size_t) (sNBenign + 1) * sizeof(Rng));
    sBenign[sNBenign].lo = (u8 *) p;
    sBenign[sNBenign].n = n;
    sNBenign++;
}

static int cmpRng(const void *a, const void *b)
{
    const Rng *x = (const Rng *) a, *y = (const Rng *) b;

    return x->lo < y->lo ? -1 : (x->lo > y->lo ? 1 : 0);
}

/* the static segments: table ∪ extras, minus the live ranges, merged */
static void buildStatic(void)
{
    const PortSnapTable *t = &g_PortSnapTable;
    int total = (int) t->count + sNExtra, i, k, n = 0;
    Rng *all = (Rng *) malloc((size_t) (total + 1) * sizeof(Rng));
    Rng *mer = (Rng *) malloc((size_t) (total + 1) * sizeof(Rng));
    Rng *res;
    int nres = 0;

    for (i = 0; i < (int) t->count; i++)
    {
        all[n].lo = (u8 *) (uintptr_t) t->ent[i].addr;
        all[n].n = t->ent[i].size;
        n++;
    }
    for (i = 0; i < sNExtra; i++)
    {
        all[n++] = sExtra[i];
    }
    qsort(all, (size_t) n, sizeof(Rng), cmpRng);
    k = 0;
    for (i = 0; i < n; i++)     /* union */
    {
        if (k > 0 && all[i].lo <= mer[k - 1].lo + mer[k - 1].n)
        {
            u8 *end = all[i].lo + all[i].n;

            if (end > mer[k - 1].lo + mer[k - 1].n)
            {
                mer[k - 1].n = (u32) (end - mer[k - 1].lo);
            }
        }
        else
        {
            mer[k++] = all[i];
        }
    }
    qsort(sLive, (size_t) sNLive, sizeof(Rng), cmpRng);
    res = (Rng *) malloc((size_t) (k + sNLive * 2 + 4) * sizeof(Rng) * 2);
    for (i = 0; i < k; i++)     /* subtract the live ranges */
    {
        u8 *cur = mer[i].lo, *end = mer[i].lo + mer[i].n;
        int j;

        for (j = 0; j < sNLive; j++)
        {
            u8 *ls = sLive[j].lo, *le = sLive[j].lo + sLive[j].n;

            if (le <= cur || ls >= end)
            {
                continue;
            }
            if (ls > cur)
            {
                res[nres].lo = cur;
                res[nres].n = (u32) (ls - cur);
                nres++;
            }
            cur = le > cur ? le : cur;
        }
        if (cur < end)
        {
            res[nres].lo = cur;
            res[nres].n = (u32) (end - cur);
            nres++;
        }
    }
    free(all);
    free(mer);
    free(sStatic);
    sStatic = res;
    sNStatic = nres;
}

/* ---- the segment list of this moment ---- */

static void addSeg(u8 *p, u32 n, u8 kind, int grp)
{
    if (n == 0)
    {
        return;
    }
    if (sNCur == sCurCap)
    {
        sCurCap = sCurCap ? sCurCap * 2 : 512;
        sCur = (Seg *) realloc(sCur, (size_t) sCurCap * sizeof(Seg));
    }
    sCur[sNCur].p = p;
    sCur[sNCur].n = n;
    sCur[sNCur].kind = kind;
    sCur[sNCur].grp = (u8) grp;
    sCur[sNCur].off = 0;
    sNCur++;
    sGrp[grp] += n;
}

static void currentSegs(void)
{
    static const int pools[3] = { MEMPOOL_STAGE, MEMPOOL_ME, MEMPOOL_PERMANENT };
    int i, k;
    u32 off = 0;

    sNCur = 0;
    memset(sGrp, 0, sizeof(sGrp));
    if (sNStatic < 0)
    {
        buildStatic();
    }
    for (i = 0; i < sNStatic; i++)
    {
        addSeg(sStatic[i].lo, sStatic[i].n, 0, 0);
    }
    for (i = 0; i < 3; i++)
    {
        MemoryPool *mp = &g_mempPools[pools[i]];
        u8 *lo = mp->start, *hi = mp->pos;

        if (lo == NULL || hi == NULL || hi <= lo || hi > mp->end)
        {
            continue;
        }
        if (pools[i] == MEMPOOL_STAGE && g_GfxBuffers[0] != NULL && g_GfxBuffers[2] != NULL && g_GfxBuffers[0] >= lo && g_GfxBuffers[2] <= hi)
        {   /* the master display list buffers: drawing only, and the renderer may be reading them */
            addSeg(lo, (u32) (g_GfxBuffers[0] - lo), 1, 1 + i);
            addSeg(g_GfxBuffers[2], (u32) (hi - g_GfxBuffers[2]), 1, 1 + i);
        }
        else
        {
            addSeg(lo, (u32) (hi - lo), 1, 1 + i);
        }
    }
    for (i = 0; i < sNDyn; i++)
    {
        u8 *lo;
        u32 n;
        const char *skip = getenv("GE_SNAPSKIPDYN");   /* debugging: leave these extents out (comma list of names) */

        if (skip != NULL && strstr(skip, sDyn[i].name) != NULL)
        {
            continue;
        }
        for (k = 0; sDyn[i].fn(k, &lo, &n); k++)
        {
            addSeg(lo, n, 1, 4 + (i < 3 ? i : 3));
        }
    }
    for (i = 0; i < sNCur; i++)
    {
        sCur[i].off = off;
        off += sCur[i].n;
    }
}

/* ---- slots ---- */

static void slotSave(Slot *s, u32 tick)
{
    int i;
    u32 total = 0;

    currentSegs();
    for (i = 0; i < sNCur; i++)
    {
        total += sCur[i].n;
    }
    if (total > s->cap)
    {
        s->cap = total + (total >> 3) + 65536u;
        free(s->buf);
        s->buf = (u8 *) malloc(s->cap);
    }
    if (sNCur > s->segCap)
    {
        s->segCap = sNCur + 64;
        s->seg = (Seg *) realloc(s->seg, (size_t) s->segCap * sizeof(Seg));
    }
    memcpy(s->seg, sCur, (size_t) sNCur * sizeof(Seg));
    s->nseg = sNCur;
    for (i = 0; i < sNCur; i++)
    {
        memcpy(s->buf + sCur[i].off, sCur[i].p, sCur[i].n);
    }
    s->size = total;
    s->tick = tick;
    s->valid = 1;
}

static void dirtyAdd(u8 *lo, u32 n)
{
    const unsigned int a = (unsigned int) (uintptr_t) lo;

    if (sNDirty > 0 && sDirty[sNDirty - 1][1] == a)
    {
        sDirty[sNDirty - 1][1] = a + n;
    }
    else if (sNDirty < DIRTY_MAX)
    {
        sDirty[sNDirty][0] = a;
        sDirty[sNDirty][1] = a + n;
        sNDirty++;
    }
    else
    {
        sDirtyOverflow = 1;
    }
}

/* returns the bytes that differed (and were copied back) */
static u32 slotRestore(const Slot *s)
{
    static const int pools[3] = { MEMPOOL_STAGE, MEMPOOL_ME, MEMPOOL_PERMANENT };
    u8 *livePos[3];
    int i;
    u32 dirty = 0;

    for (i = 0; i < 3; i++)
    {
        livePos[i] = g_mempPools[pools[i]].pos;
    }
    sNDirty = 0;
    sDirtyOverflow = 0;
    for (i = 0; i < s->nseg; i++)
    {
        const Seg *g = &s->seg[i];
        const u8 *src = s->buf + g->off;

        if (g->kind == 0)
        {
            memcpy(g->p, src, g->n);
        }
        else
        {
            u32 o;

            for (o = 0; o < g->n; o += PAGE)
            {
                const u32 n = g->n - o < PAGE ? g->n - o : PAGE;

                if (memcmp(g->p + o, src + o, n) != 0)
                {
                    memcpy(g->p + o, src + o, n);
                    dirty += n;
                    dirtyAdd(g->p + o, n);
                }
            }
        }
    }
    /* Fresh pool memory is zero (the first stage in a process), and code that carves a block from a pool may rely on that: the bytes the rolled-back
     * allocations dirtied beyond the restored bump pointer go back to zero, so the re-simulation sees what the first run saw. */
    for (i = 0; i < 3; i++)
    {
        u8 *now = g_mempPools[pools[i]].pos;

        if (livePos[i] != NULL && now != NULL && livePos[i] > now && livePos[i] <= g_mempPools[pools[i]].end)
        {
            memset(now, 0, (size_t) (livePos[i] - now));
            dirty += (u32) (livePos[i] - now);
            dirtyAdd(now, (u32) (livePos[i] - now));
        }
    }
    return dirty;
}

/* A word that differs only in ways that carry no state: both values point into the C stack (a dead luaL_Buffer placeholder in a Lua
 * stack slot: the call stack is deeper inside a re-simulation than in the main loop), or it lies in a range registered benign (a
 * lookup cache keyed by a C string's address: Lua's string cache). Only used by the compare. */
static int ignorable(const u8 *live, const u8 *snap, u32 n, u32 o)
{
    const u32 w = o & ~3u;
    u32 a, b;
    unsigned int sp = (unsigned int) (uintptr_t) &a;
    int i;

    if (w + 4 <= n)
    {
        memcpy(&a, live + w, 4);
        memcpy(&b, snap + w, 4);
        if ((a > sp - 0x200000u && a < sp + 0x200000u) && (b > sp - 0x200000u && b < sp + 0x200000u))
        {
            return 1;
        }
    }
    if (sCmp == 1 && g_VtxBuffers[0] != NULL && live + o >= g_VtxBuffers[0] && live + o < g_VtxBuffers[2])
    {
        return 1;   /* the per-frame vertex pool: a sim-only tick leaves the vertices the last rendered tick built (only the matrices are read back) */
    }
    for (i = 0; i < sNBenign; i++)
    {
        if (live + o >= sBenign[i].lo && live + o < sBenign[i].lo + sBenign[i].n)
        {
            return 1;
        }
    }
    return 0;
}

/* byte-exact compare of the live state against a slot; prints what differs (a debugging aid and the R2 proof) */
static u32 slotCompare(const Slot *s, const char *label, int verbose)
{
    int i, shown = 0;
    u32 diff = 0;
    const int maxShown = getenv("GE_SNAPCMPMAX") != NULL ? atoi(getenv("GE_SNAPCMPMAX")) : 40;   /* DIFF lines printed */

    for (i = 0; i < s->nseg; i++)
    {
        const Seg *g = &s->seg[i];
        const u8 *src = s->buf + g->off;
        u32 o = 0;

        while (o < g->n)
        {
            u32 e, last, cnt = 1;

            if (g->p[o] == src[o] || ignorable(g->p, src, g->n, o))
            {
                o++;
                continue;
            }
            last = o;       /* a run of differing bytes, joined across gaps of up to 16 equal bytes */
            for (e = o + 1; e < g->n && e - last <= 16; e++)
            {
                if (g->p[e] != src[e] && !ignorable(g->p, src, g->n, e))
                {
                    last = e;
                    cnt++;
                }
            }
            diff += cnt;
            if (verbose && shown < maxShown)
            {
                char w[96];
                u32 a, b, ws = o & ~3u, k;
                char la[80], lb[80];
                int na = 0, nb = 0;

                la[0] = lb[0] = '\0';
                for (k = 0; k < 6 && ws + 4 * k + 4 <= g->n; k++)
                {
                    memcpy(&a, g->p + ws + 4 * k, 4);
                    memcpy(&b, src + ws + 4 * k, 4);
                    na += snprintf(la + na, sizeof(la) - (size_t) na, "%08x ", a);
                    nb += snprintf(lb + nb, sizeof(lb) - (size_t) nb, "%08x ", b);
                }
                portConLogf("ge: snap: %s: DIFF %s len %u: live %s| snapshot %s\n", label, whatIs(g->p + o, w, sizeof(w)), last + 1u - o, la, lb);
                shown++;
            }
            o = last + 1u;
        }
    }
    return diff;
}

/* ---- the renderer's caches ---- */

static void forgetDirty(void)
{
    if (sNDirty > 0 || sDirtyOverflow)
    {
        gfx_invalidate_ranges(sDirty, sDirtyOverflow ? -1 : sNDirty);
    }
}

/* ---- setup ---- */

static void init(void)
{
    const char *e;
    int a = 0, b = 0, c = 0, n;

    sInit = 1;
    {   /* wall-clock bookkeeping that sits in snapshotted files: a restore must not rewind real time */
        extern u32 copy_of_osgetcount_value_0, copy_of_osgetcount_value_1;   /* frametiming.c */
        extern u32 g_PortTicksAdmitted;                                       /* boss.c: counts admitted ticks (front-end sequences) */
        extern s32 ge_joy_calls, ge_joy_disables, ge_joy_enables;             /* joy.c: diagnostic poll counters */
        extern s32 g_PortStatFps, g_PortStatTps;                              /* frametiming.c: the title-bar stats */

        portSnapLive("ft.osgetcount0", &copy_of_osgetcount_value_0, sizeof(copy_of_osgetcount_value_0));
        portSnapLive("ft.osgetcount1", &copy_of_osgetcount_value_1, sizeof(copy_of_osgetcount_value_1));
        portSnapLive("boss.ticksadmitted", &g_PortTicksAdmitted, sizeof(g_PortTicksAdmitted));
        portSnapLive("joy.calls", &ge_joy_calls, sizeof(ge_joy_calls));
        portSnapLive("joy.reads", &ge_joy_reads, sizeof(ge_joy_reads));
        portSnapLive("joy.disables", &ge_joy_disables, sizeof(ge_joy_disables));
        portSnapLive("joy.enables", &ge_joy_enables, sizeof(ge_joy_enables));
        portSnapLive("ft.statfps", &g_PortStatFps, sizeof(g_PortStatFps));
        {   /* handles into the audio engine (ALSoundState pointers): audio is wholly outside the snapshot, so a restored handle could name a recycled voice.
             * With the sim's own sound view (port_simsnd.c, the default) every handle the sim holds is a token of sim state, and these are rewound
             * like the rest (left live they would keep the abandoned ticks' tokens). */
            extern void *g_ImpactSfxStates[4];   /* gun.c */
            extern void *g_UnusedSfxState;
            extern int portSimSndOn(void);

            if (!portSimSndOn())
            {
                portSnapLive("gun.impactsfx", g_ImpactSfxStates, sizeof(g_ImpactSfxStates));
                portSnapLive("gun.unusedsfx", &g_UnusedSfxState, sizeof(g_UnusedSfxState));
            }
        }
        portSnapLive("ft.stattps", &g_PortStatTps, sizeof(g_PortStatTps));
    }
    sTableOk = g_PortSnapTable.magic == PORT_SNAP_MAGIC && g_PortSnapTable.count > 0;
    e = getenv("GE_SNAPRING");
    if (e != NULL && (n = atoi(e)) > 0)
    {
        sRingN = n > RING_MAX ? RING_MAX : n;
    }
    e = getenv("GE_SNAPRESIM");
    if (e != NULL && sscanf(e, "%d,%d,%d", &a, &b, &c) >= 2 && a >= 1 && b >= 1)
    {
        sRsT = a;
        sRsK = b;
        sRsEvery = c;
        if (sRsEvery > 0 && sRsEvery <= sRsK)
        {
            sRsEvery = sRsK + 1;
        }
        if (sRingN > 0 && sRingN < sRsK + 1)
        {
            sRingN = sRsK + 1 > RING_MAX ? RING_MAX : sRsK + 1;
        }
    }
    e = getenv("GE_SNAPSTRESS");
    if (e != NULL && sscanf(e, "%d,%d", &a, &b) >= 1 && a >= 1)
    {
        sStressD = a > RING_MAX - 1 ? RING_MAX - 1 : a;
        sStressStart = (sscanf(e, "%d,%d", &a, &b) >= 2 && b >= 1) ? b : sStressD + 1;
        if (sRingN < sStressD + 1)
        {
            sRingN = sStressD + 1;
        }
    }
    sCmp = getenv("GE_SNAPCMP") != NULL ? atoi(getenv("GE_SNAPCMP")) : 0;   /* 1 = ignore the per-frame vertex pool (render-only bytes), 2 = strict */
    if (getenv("GE_SNAPCMP") != NULL && sCmp == 0) { sCmp = 1; }
    e = getenv("GE_SNAPHASH");
    if (e != NULL && sscanf(e, "%d,%d", &a, &b) == 2 && a >= 1 && b >= a)
    {
        sHashFrom = a;
        sHashTo = b;
    }
    sOn = (sRingN > 0 || sRsT > 0 || sStressD > 0 || sHashTo > 0);
    if (sOn && !sTableOk)
    {
        portConLogf("ge: snap: GE_SNAP* is set but this exe has no embedded sim-statics table (scripts/port/snap_embed.py did not run): snapshots stay off\n");
        sOn = 0;
        return;
    }
    if (sOn)
    {
        portConLogf("ge: snap: armed: ring %d%s%s; table %u ranges\n", sRingN, sRsT > 0 ? ", resim test" : "", sStressD > 0 ? ", stress" : "",
                    g_PortSnapTable.count);
    }
}

void portSnapStageBegin(void)
{
    int i;

    if (!sInit)
    {
        init();
    }
    for (i = 0; i < RING_MAX; i++)
    {
        sRing[i].valid = 0;
    }
    sKeep.valid = 0;
    sPre.valid = 0;
}

static unsigned int stageTicks(void)
{
    return portSimOnlyTickCount();
}

static int slotFor(unsigned int tick, Slot **out)
{
    Slot *s;

    if (sRingN > 0)
    {
        s = &sRing[tick % (unsigned) sRingN];
        if (s->valid && s->tick == tick)
        {
            *out = s;
            return 1;
        }
    }
    if (sKeep.valid && sKeep.tick == tick)
    {
        *out = &sKeep;
        return 1;
    }
    return 0;
}

/* boss.c retrace gate: will the NEXT tick restore? Then no gfx task may be in flight (the restore rewrites memory a task reads). */
int portSnapNeedIdle(void)
{
    unsigned int tick;

    if (!sInit)
    {
        init();
    }
    if (sSoloReq == 2)
    {
        return 1;   /* a solo savestate goes back at the next tick */
    }
    {
        extern int portSnapSoloTestRestoring(void);

        if (portSnapSoloTestRestoring())
        {
            return 1;
        }
    }
    if (!sOn)
    {
        return 0;
    }
    tick = stageTicks() + 1u;
    if (sStressD > 0 && (int) tick >= sStressStart)
    {
        return 1;
    }
    if (sRsT > 0 && (int) tick >= sRsT + sRsK)
    {
        const int d = (int) tick - sRsT - sRsK;

        return sRsEvery > 0 ? (d % sRsEvery) == 0 : d == 0;
    }
    return 0;
}

static void hashLine(unsigned int tick, const char *tag);

/* back to the snapshot of tick `from` and re-simulate [from, to) */
static void rollbackTo(unsigned int from, unsigned int to, Slot *s)
{
    unsigned long long t0, t1, t2;
    u32 dirty;
    const int n = (int) (to - from);

    if (sCmp)
    {
        slotSave(&sPre, to);        /* the state we must come back to */
    }
    t0 = portProfNow();
    dirty = slotRestore(s);
    forgetDirty();
    t1 = portProfNow();
    portBossResim(n, from);
    sLastReads = ge_joy_reads;      /* the polls of the re-simulation are replays of polls already counted */
    t2 = portProfNow();
    sRestSum += t1 - t0;
    sRestN++;
    if (t1 - t0 > sRestMax)
    {
        sRestMax = t1 - t0;
    }
    sResimSum += t2 - t1;
    sResimTicks += (unsigned) n;
    if ((t2 - t1) / (unsigned) n > sResimMax)
    {
        sResimMax = (t2 - t1) / (unsigned) n;
    }
    sDirtySum += dirty;
    sResimEvents++;
    if (sHashTo > 0 && (int) to >= sHashFrom && (int) to <= sHashTo)
    {
        hashLine(to, ".resim");
    }
    if (sCmp)
    {
        const u32 d = slotCompare(&sPre, "resim", 1);

        if (d != 0)
        {
            sCmpFail++;
        }
        portConLogf("ge: snap: COMPARE after re-sim of ticks %u..%u: %u differing bytes of %u KB (%s)\n", from, to - 1u, d, sPre.size / 1024u,
                    d == 0 ? "BIT-IDENTICAL" : "DIFFERENT");
    }
    if (sResimEvents % 100u == 0)
    {
        portSnapReport();
    }
    if (sResimEvents <= 3u || sResimEvents % 300u == 0)
    {
        portConLogf("ge: snap: ROLLBACK at tick %u back to %u (%d ticks): restore %.3f ms (%u KB differed, %d ranges), re-sim %.3f ms (%.3f ms/tick)\n", to, from, n,
                    (double) (t1 - t0) / 1000.0, dirty / 1024u, sNDirty, (double) (t2 - t1) / 1000.0, (double) (t2 - t1) / 1000.0 / (double) n);
    }
}

/* GE_SNAPHASH=<from>,<to>: one line per tick with a hash of each group of the snapshot (a determinism instrument: two runs, or a run and
 * its re-simulation, must print the same lines) */
static void hashLine(unsigned int tick, const char *tag)
{
    u32 h[8];
    int i;

    currentSegs();
    for (i = 0; i < 8; i++)
    {
        h[i] = 0x811C9DC5u;
    }
    {   /* GE_SNAPHASHSKIP=a,b,...: the statics group hashed per table range, leaving out ranges whose name contains one of
         * the strings (the player bot's own state, inputrec's file handles: they differ between a bot run and its replay
         * by design); with GE_SNAPHASHRANGES=1 also one line per range that changed since the last hashed tick */
        static const char *skip = (const char *) -1;
        static u32 *prev;

        if (skip == (const char *) -1)
        {
            skip = getenv("GE_SNAPHASHSKIP");
        }
        if (skip != NULL && sTableOk)
        {
            const PortSnapTable *t = &g_PortSnapTable;
            const int ranges = getenv("GE_SNAPHASHRANGES") != NULL;
            u32 x = 0x811C9DC5u, k;

            if (ranges && prev == NULL)
            {
                prev = (u32 *) calloc(t->count + 1, sizeof(u32));
            }
            for (k = 0; k < t->count; k++)
            {
                const char *nm = t->str + t->ent[k].nameOff;
                char buf[64];
                const char *s = skip;
                int skipit = 0;
                u32 o, y = 0x811C9DC5u;

                while (*s && !skipit)
                {
                    int l = 0;

                    while (s[l] && s[l] != ',' && l < 63)
                    {
                        buf[l] = s[l];
                        l++;
                    }
                    buf[l] = '\0';
                    skipit = l > 0 && strstr(nm, buf) != NULL;
                    s += l + (s[l] == ',');
                }
                if (skipit)
                {
                    continue;
                }
                for (o = 0; o + 4 <= t->ent[k].size; o += 4)
                {
                    u32 w;

                    memcpy(&w, (const u8 *) (uintptr_t) t->ent[k].addr + o, 4);
                    y = (y ^ w) * 0x9E3779B1u;
                    y ^= y >> 15;
                }
                x = (x ^ y) * 0x9E3779B1u;
                if (ranges && prev[k] != y)
                {
                    if (prev[k] != 0)
                    {
                        portConLogf("ge: snaphash: tick %u range %s changed\n", tick, nm);
                    }
                    prev[k] = y;
                }
            }
            h[0] = x;
        }
    }
    for (i = 0; i < sNCur; i++)
    {
        const u8 *p = sCur[i].p;
        u32 n = sCur[i].n, o, x = h[sCur[i].grp];

        if (sCur[i].grp == 0 && getenv("GE_SNAPHASHSKIP") != NULL && sTableOk)
        {
            continue;   /* (hashed per range above) */
        }

        for (o = 0; o + 4 <= n; o += 4)
        {
            u32 w;

            memcpy(&w, p + o, 4);
            x = (x ^ w) * 0x9E3779B1u;
            x ^= x >> 15;
        }
        h[sCur[i].grp] = x;
    }
    {   /* the ROM image and the inflated data: immutable by design; a hash that moves means something converts or edits served data in place */
        extern void romdataSnapRanges(u8 **rom, u32 *romsz, u8 **cdata, u32 *cdatasz);
        u8 *rom = NULL, *cd = NULL;
        u32 rs = 0, cs = 0, x = 0x811C9DC5u, o;

        romdataSnapRanges(&rom, &rs, &cd, &cs);
        for (o = 0; rom != NULL && o + 4 <= rs; o += 4) { u32 w; memcpy(&w, rom + o, 4); x = (x ^ w) * 0x9E3779B1u; x ^= x >> 15; }
        for (o = 0; cd != NULL && o + 4 <= cs; o += 4) { u32 w; memcpy(&w, cd + o, 4); x = (x ^ w) * 0x9E3779B1u; x ^= x >> 15; }
        h[7] = x;
    }
    portConLogf("ge: snaphash%s: tick %u rom %08x statics %08x stage %08x me %08x perm %08x %s %08x %s %08x %s %08x\n", tag, tick, h[7], h[0], h[1], h[2], h[3],
                sNDyn > 0 ? sDyn[0].name : "-", h[4], sNDyn > 1 ? sDyn[1].name : "-", h[5], sNDyn > 2 ? sDyn[2].name : "-", h[6]);
}

/* ---- solo savestates (the player bot's segment optimiser, GE_PLAYERBOT_OPT) ----
 * The bot asks between ticks (it decides at the controller poll after a tick); the save / restore runs here, at the start
 * of the next tick, where the rollback ring saves too. A restore puts the whole sim state back - the bot's own statics are
 * in it (port_playerbot.c is snapshotted) - plus this stage's tick count (port_simonly.c: which ticks draw), and then the
 * tick simply runs: no re-simulation. sSolo* is presentation-side state, never rewound. */
static Slot sSolo;
static unsigned int sSoloCount;     /* port_simonly's tick count at the save */
static unsigned sSoloSaves, sSoloRestores;
extern void portSimOnlySetTickCount(unsigned int n);

int portSnapSoloAvailable(void)
{
    if (!sInit)
    {
        init();
    }
    return sTableOk;
}

void portSnapSoloRequest(int what)
{
    sSoloReq = what;
}

int portSnapSoloPending(void)
{
    return sSoloReq;
}

static void soloTick(void)
{
    if (sSoloReq == 1)
    {
        sSoloCount = stageTicks();
        slotSave(&sSolo, sSoloCount + 1u);
        sSoloSaves++;
        if (sSoloSaves <= 2)
        {
            portConLogf("ge: snap: solo save at tick %u: %u KB\n", sSoloCount + 1u, sSolo.size / 1024u);
        }
    }
    else if (sSoloReq == 2 && sSolo.valid)
    {
        u32 dirty = slotRestore(&sSolo);

        forgetDirty();
        portSimOnlySetTickCount(sSoloCount);
        sSoloRestores++;
        if (sSoloRestores <= 2 || (sSoloRestores % 50u) == 0)
        {
            portConLogf("ge: snap: solo restore %u to tick %u (%u KB differed)\n", sSoloRestores, sSoloCount + 1u, dirty / 1024u);
        }
    }
    sSoloReq = 0;
}

/* ---- GE_SNAPSOLOTEST=T,N: the fidelity test of a solo savestate (snapshot-fidelity.md) ----
 * At the start of tick T save; ticks T..T+N-1 run (pass 1) while every tick's start state is fingerprinted in 4 KB chunks; at
 * the start of tick T+N the whole state is kept and the save restored; the same ticks run again (pass 2, the same inputs: a
 * replay) and each tick's chunks are compared with pass 1's. The first tick whose start state differs names the chunks (state
 * the tick before read from outside the snapshot), and at T+N the whole state is compared byte for byte (DIFF lines). Then
 * the run carries on. Solo only; the optimiser's own savestate (sSolo) is not touched. */
static int sStT, sStN, sStExit, sStPass, sStFirstDiff, sStDiffTicks;
static Slot sStSave, sStRef, sStAt;
static u32 *sStHash;            /* [N + 1][sStChunks] */
static int sStChunks;
static unsigned int sStSaveCount;

static int stChunkHashes(u32 *out, int cap)
{
    int i, n = 0;

    currentSegs();
    for (i = 0; i < sNCur; i++)
    {
        u32 o;

        for (o = 0; o < sCur[i].n; o += PAGE)
        {
            const u32 len = sCur[i].n - o < PAGE ? sCur[i].n - o : PAGE;
            u32 x = 0x811C9DC5u, k;

            for (k = 0; k + 4 <= len; k += 4)
            {
                u32 w;

                memcpy(&w, sCur[i].p + o + k, 4);
                x = (x ^ w) * 0x9E3779B1u;
                x ^= x >> 15;
            }
            for (; k < len; k++)
            {
                x = (x ^ sCur[i].p[o + k]) * 0x01000193u;
            }
            if (n < cap)
            {
                out[n] = x;
            }
            n++;
        }
    }
    return n;
}

static u8 *stChunkAddr(int idx, u32 *len)
{
    int i, n = 0;

    for (i = 0; i < sNCur; i++)
    {
        const int k = (int) ((sCur[i].n + PAGE - 1) / PAGE);

        if (idx < n + k)
        {
            const u32 o = (u32) (idx - n) * PAGE;

            *len = sCur[i].n - o < PAGE ? sCur[i].n - o : PAGE;
            return sCur[i].p + o;
        }
        n += k;
    }
    *len = 0;
    return NULL;
}

#ifdef __linux__
static void stOutHash(int report);
#endif

static void soloTestTick(void)
{
    static int init0;
    const unsigned int tick = stageTicks() + 1u;

    if (!init0)
    {
        const char *e = getenv("GE_SNAPSOLOTEST");

        init0 = 1;
        if (e == NULL || sscanf(e, "%d,%d,%d", &sStT, &sStN, &sStExit) < 2 || sStT < 1 || sStN < 1)
        {
            sStT = 0;
        }
        else
        {
            portConLogf("ge: snaptest: armed: save at tick %d, restore at %d, replay and compare\n", sStT, sStT + sStN);
        }
    }
    if (sStT == 0 || sStPass >= 3 || !sTableOk)
    {
        return;
    }
    if (sStPass == 0 && (int) tick == sStT)
    {
        sStSaveCount = stageTicks();
        slotSave(&sStSave, tick);
        sStChunks = stChunkHashes(NULL, 0) + 2048;     /* the pools and arenas grow over the run: room for 8 MB more */
        sStHash = (u32 *) calloc((size_t) (sStN + 1) * (size_t) sStChunks, sizeof(u32));
        sStPass = 1;
        portConLogf("ge: snaptest: pass 1 from tick %u (%u KB, %d chunks)\n", tick, sStSave.size / 1024u, sStChunks);
#ifdef __linux__
        if (getenv("GE_SNAPTESTOUT") != NULL)
        {
            stOutHash(0);
        }
#endif
    }
    if (sStPass == 1)
    {
        const int d = (int) tick - sStT;

        if (d < sStN)
        {
            u32 *h = sStHash + (size_t) d * (size_t) sStChunks;
            const int n = stChunkHashes(h + 1, sStChunks - 1);

            h[0] = (u32) n;
            if (getenv("GE_SNAPTESTAT") != NULL && atoi(getenv("GE_SNAPTESTAT")) == (int) tick)
            {   /* GE_SNAPTESTAT=<tick>: keep the whole state of that tick too; pass 2 compares it byte for byte */
                slotSave(&sStAt, tick);
            }
            return;
        }
        slotSave(&sStRef, tick);
#ifdef __linux__
        if (getenv("GE_SNAPTESTOUT") != NULL)
        {
            stOutHash(1);
        }
#endif
        {
            u32 dirty = slotRestore(&sStSave);

            forgetDirty();
            portSimOnlySetTickCount(sStSaveCount);
            portConLogf("ge: snaptest: pass 2: restored tick %d at tick %u (%u KB differed)\n", sStT, tick, dirty / 1024u);
        }
        sStPass = 2;
        sStFirstDiff = -1;
        sStDiffTicks = 0;
        return;     /* tick T runs again */
    }
    if (sStPass == 2)
    {
        const int d = (int) tick - sStT;

        if (d < sStN)
        {
            u32 *h = sStHash + (size_t) d * (size_t) sStChunks;
            u32 *now = (u32 *) malloc((size_t) sStChunks * sizeof(u32));
            const int n = stChunkHashes(now, sStChunks);

            if (sStAt.valid && sStAt.tick == tick)
            {
                char lab[32];

                snprintf(lab, sizeof(lab), "at-%u", tick);
                portConLogf("ge: snaptest: tick %u compared byte for byte: %u bytes differ\n", tick, slotCompare(&sStAt, lab, 1));
                {   /* and the extents themselves (a pool / arena that grew differently) */
                    int g;

                    currentSegs();
                    for (g = 0; g < sNCur || g < sStAt.nseg; g++)
                    {
                        if (g >= sNCur || g >= sStAt.nseg || sCur[g].p != sStAt.seg[g].p || sCur[g].n != sStAt.seg[g].n)
                        {
                            char w[96];

                            portConLogf("ge: snaptest:   segment %d: now %s len %u, pass 1 len %u at %p\n", g,
                                        g < sNCur ? whatIs(sCur[g].p, w, sizeof(w)) : "-", g < sNCur ? sCur[g].n : 0u,
                                        g < sStAt.nseg ? sStAt.seg[g].n : 0u, g < sStAt.nseg ? (void *) sStAt.seg[g].p : NULL);
                        }
                    }
                }
            }
            int i, bad = 0;

            if ((u32) n != h[0])
            {
                bad = 1;
                if (sStFirstDiff < 0)
                {
                    portConLogf("ge: snaptest: tick %u: the state's layout differs (%d chunks, pass 1 %u)\n", tick, n, h[0]);
                }
            }
            else if (n > sStChunks - 1)
            {
                bad = 1;
                if (sStFirstDiff < 0)
                {
                    portConLogf("ge: snaptest: tick %u: %d chunks, more than the %d the test keeps: not compared\n", tick, n, sStChunks - 1);
                }
            }
            else
            {
                for (i = 0; i < n; i++)
                {
                    if (now[i] != h[1 + i])
                    {
                        bad++;
                    }
                }
            }
            if (bad)
            {
                static u8 *seen;        /* chunks already reported */
                static int shownTotal;

                sStDiffTicks++;
                if (sStFirstDiff < 0)
                {
                    sStFirstDiff = (int) tick;
                    portConLogf("ge: snaptest: FIRST DIFFERENCE at the start of tick %u (%d chunks differ): tick %u read state outside the snapshot\n",
                                tick, bad, tick - 1u);
                }
                if (seen == NULL)
                {
                    seen = (u8 *) calloc((size_t) sStChunks, 1);
                }
                /* every chunk once, at the first tick it differs: where each divergence entered */
                for (i = 0; i < n && (u32) n == h[0] && shownTotal < 300; i++)
                {
                    if (now[i] != h[1 + i] && !seen[i])
                    {
                        char w[96];
                        u32 len;
                        u8 *p = stChunkAddr(i, &len);

                        seen[i] = 1;
                        shownTotal++;
                        portConLogf("ge: snaptest:   tick %u: chunk %d: %s (%u bytes)\n", tick, i, whatIs(p, w, sizeof(w)), len);
                        if (sStAt.valid && sStAt.tick == tick)
                        {   /* where in the chunk, against the whole state GE_SNAPTESTAT kept */
                            int g;

                            for (g = 0; g < sStAt.nseg; g++)
                            {
                                const Seg *sg = &sStAt.seg[g];

                                if (p >= sg->p && p < sg->p + sg->n)
                                {
                                    const u8 *old = sStAt.buf + sg->off + (p - sg->p);
                                    u32 k, lim = (u32) (sg->p + sg->n - p) < len ? (u32) (sg->p + sg->n - p) : len;

                                    for (k = 0; k + 4 <= lim; k += 4)
                                    {
                                        u32 a, b;

                                        memcpy(&a, p + k, 4);
                                        memcpy(&b, old + k, 4);
                                        if (a != b)
                                        {
                                            portConLogf("ge: snaptest:     +0x%x: now %08x, pass 1 %08x\n", k, a, b);
                                            break;
                                        }
                                    }
                                    if (k + 4 > lim)
                                    {
                                        portConLogf("ge: snaptest:     the same as pass 1 over the %u bytes pass 1 had (its segment ends %u bytes in)\n", lim, lim);
                                    }
                                    break;
                                }
                            }
                            if (g == sStAt.nseg)
                            {
                                portConLogf("ge: snaptest:     not in pass 1's segments at this tick\n");
                            }
                        }
                    }
                }
            }
            free(now);
            return;
        }
        {
            const u32 diff = slotCompare(&sStRef, "snaptest", 1);

            portConLogf("ge: snaptest: RESULT ticks %d..%d: %s (first difference %d, %d of %d tick starts differed, %u bytes differ at tick %u)\n", sStT,
                        sStT + sStN - 1, diff == 0 && sStDiffTicks == 0 ? "BIT-EXACT" : "DIFFERENT", sStFirstDiff, sStDiffTicks, sStN, diff, tick);
        }
        sStPass = 3;
        free(sStHash);
        sStHash = NULL;
        if (sStExit)
        {   /* GE_SNAPSOLOTEST=T,N,1: the test is all this run is for */
            fflush(stdout);
            fflush(stderr);
            _exit(0);
        }
    }
}

#ifdef __linux__
/* GE_SNAPTESTOUT=1: the exe's writable data outside the snapshot, hashed in 4 KB chunks at the save and again at the restore:
 * the chunks that changed over pass 1 are what pass 2 starts from that pass 1 did not (the candidates for a sim read of
 * state outside the snapshot). */
extern char __data_start[], _end[];
static u32 *sStOut;
static int sStOutN;

static int stInStatics(const u8 *p, u32 n)
{
    int i;

    for (i = 0; i < sNStatic; i++)
    {
        if (p >= sStatic[i].lo && p + n <= sStatic[i].lo + sStatic[i].n)
        {
            return 1;
        }
    }
    return 0;
}

static u8 *sStOutCopy;

static void stOutHash(int report)
{
    const u8 *lo = (const u8 *) (((uintptr_t) __data_start) & ~(uintptr_t) (PAGE - 1)), *hi = (const u8 *) _end;
    const int n = (int) ((hi - lo + PAGE - 1) / PAGE);
    int i, shown = 0;

    if (!report)
    {   /* a copy of all of it, for the word-level report */
        free(sStOutCopy);
        sStOutCopy = (u8 *) malloc((size_t) (hi - lo));
        if (sStOutCopy != NULL)
        {
            memcpy(sStOutCopy, lo, (size_t) (hi - lo));
        }
    }
    else if (sStOutCopy != NULL)
    {
        u32 o = 0, total = (u32) (hi - lo);
        int runs = 0;

        while (o + 4 <= total && runs < 4000)
        {
            u32 a, b, e;

            memcpy(&a, lo + o, 4);
            memcpy(&b, sStOutCopy + o, 4);
            if (a == b || lo + o < (const u8 *) __data_start || stInStatics(lo + o, 4)
                || (lo + o >= (const u8 *) &sStOutCopy && lo + o < (const u8 *) &sStOutCopy + sizeof(sStOutCopy)))
            {
                o += 4;
                continue;
            }
            for (e = o + 4; e + 4 <= total && e - o < 4096; e += 4)
            {
                memcpy(&a, lo + e, 4);
                memcpy(&b, sStOutCopy + e, 4);
                if (a == b || stInStatics(lo + e, 4))
                {
                    break;
                }
            }
            memcpy(&a, lo + o, 4);
            memcpy(&b, sStOutCopy + o, 4);
            portConLogf("ge: snaptest: outside word run @%08x len %u: now %08x was %08x\n", (unsigned) (uintptr_t) (lo + o), e - o, a, b);
            runs++;
            o = e;
        }
    }
    if (sStOut == NULL)
    {
        sStOut = (u32 *) calloc((size_t) n, sizeof(u32));
        sStOutN = n;
    }
    for (i = 0; i < n && i < sStOutN; i++)
    {
        const u8 *p = lo + (size_t) i * PAGE;
        u32 x = 0x811C9DC5u, k, len = (u32) (hi - p < (long) PAGE ? hi - p : PAGE);

        if (p < (const u8 *) __data_start)
        {
            p = (const u8 *) __data_start;
            len = PAGE - (u32) ((uintptr_t) __data_start & (PAGE - 1));
        }
        if (stInStatics(p, len))
        {
            continue;
        }
        for (k = 0; k + 4 <= len; k += 4)
        {
            u32 w;

            if (stInStatics(p + k, 4))
            {
                continue;   /* (a page that is partly in the snapshot: only its other bytes count) */
            }
            memcpy(&w, p + k, 4);
            x = (x ^ w) * 0x9E3779B1u;
            x ^= x >> 15;
        }
        if (report && x != sStOut[i] && shown < 400)
        {
            portConLogf("ge: snaptest: outside the snapshot, changed over pass 1: @%08x (%u bytes)\n", (unsigned) (uintptr_t) p, len);
            shown++;
        }
        sStOut[i] = x;
    }
}
#endif

int portSnapSoloTestRestoring(void)
{
    return sStT > 0 && sStPass == 1 && (int) (stageTicks() + 1u) == sStT + sStN;
}

static unsigned int sTicksBegun;    /* in-stage ticks begun in this process: presentation, never rewound */

unsigned int portSnapTicksBegun(void)
{
    return sTicksBegun;
}

void portSnapTickBegin(void)
{
    unsigned int tick;
    Slot *s;

    sTicksBegun++;
    if (!sInit)
    {
        init();
    }
    if (sSoloReq != 0 && sTableOk)
    {
        soloTick();
    }
    soloTestTick();
    if (!sOn)
    {
        return;
    }
    tick = stageTicks() + 1u;
    sFrames++;
    if (sHashTo > 0 && (int) tick >= sHashFrom && (int) tick <= sHashTo)
    {
        hashLine(tick, "");
    }
    {
        const s32 reads = ge_joy_reads - sLastReads;

        sPollHist[tick % POLLHIST] = (unsigned char) (reads < 0 ? 0 : (reads > 255 ? 255 : reads));
        sLastReads = ge_joy_reads;
    }
    if (sRingN > 0)
    {
        unsigned long long t0 = portProfNow(), d;

        slotSave(&sRing[tick % (unsigned) sRingN], tick);
        d = portProfNow() - t0;
        sSaveSum += d;
        sSaveN++;
        if (d > sSaveMax)
        {
            sSaveMax = d;
        }
        sLastSize = sRing[tick % (unsigned) sRingN].size;
        if (sSaveN == 1)
        {
            portConLogf("ge: snap: first save at tick %u: %u KB (statics %u KB in %d ranges, stage %u KB, me %u KB, perm %u KB, arenas/heaps %u KB), %.3f ms\n",
                        tick, sLastSize / 1024u, sGrp[0] / 1024u, sNStatic, sGrp[1] / 1024u, sGrp[2] / 1024u, sGrp[3] / 1024u, sGrp[4] / 1024u,
                        (double) d / 1000.0);
        }
        if (sSaveN % 600u == 0)
        {
            portConLogf("ge: snap: ring of %d: %llu saves of %u KB: avg %.3f ms, max %.3f ms\n", sRingN, sSaveN, sLastSize / 1024u,
                        (double) sSaveSum / (double) sSaveN / 1000.0, (double) sSaveMax / 1000.0);
        }
    }
    if (sRsT > 0 && sRingN == 0 && (int) tick >= sRsT)
    {
        const int d = (int) tick - sRsT;

        if (sRsEvery > 0 ? (d % sRsEvery) == 0 : d == 0)
        {
            unsigned long long t0 = portProfNow(), t1;

            slotSave(&sKeep, tick);
            t1 = portProfNow();
            sSaveSum += t1 - t0;
            sSaveN++;
            sLastSize = sKeep.size;
            if (sSaveN <= 2)
            {
                portConLogf("ge: snap: SAVE at tick %u: %u KB (statics %u KB in %d ranges, stage %u KB, me %u KB, perm %u KB, arenas/heaps %u KB), %.3f ms\n",
                            tick, sLastSize / 1024u, sGrp[0] / 1024u, sNStatic, sGrp[1] / 1024u, sGrp[2] / 1024u, sGrp[3] / 1024u, sGrp[4] / 1024u,
                            (double) (t1 - t0) / 1000.0);
            }
        }
    }
    if (sStressD > 0 && (int) tick >= sStressStart && tick > (unsigned) sStressD)
    {
        if (slotFor(tick - (unsigned) sStressD, &s))
        {
            rollbackTo(tick - (unsigned) sStressD, tick, s);
        }
    }
    else if (sRsT > 0 && (int) tick >= sRsT + sRsK)
    {
        const int d = (int) tick - sRsT - sRsK;

        if (sRsEvery > 0 ? (d % sRsEvery) == 0 : d == 0)
        {
            const unsigned int from = (unsigned int) ((int) tick - sRsK);

            if (slotFor(from, &s))
            {
                rollbackTo(from, tick, s);
            }
            else
            {
                portConLogf("ge: snap: tick %u: no snapshot of tick %u to go back to\n", tick, from);
            }
        }
    }
}

/* ---- the production API (rollback R3 calls these from its tick hook; the env drivers above use the same code) ---- */

/* Turn the per-tick ring on with `n` slots (0 = off). The ring keeps the start state of the last n ticks of the stage. */
void portSnapEnable(int n)
{
    if (!sInit)
    {
        init();
    }
    if (!sTableOk)
    {
        return;
    }
    sRingN = n < 0 ? 0 : (n > RING_MAX ? RING_MAX : n);
    sOn = sRingN > 0 || sRsT > 0 || sStressD > 0 || sHashTo > 0;
}

/* 1 when the start state of stage tick `tick` is in the ring */
int portSnapHave(unsigned int tick)
{
    Slot *s;

    return slotFor(tick, &s);
}

/* Go back to the start of stage tick `fromTick` (it must be in the ring and before the tick about to run) and re-simulate fromTick ..
 * the tick before the one about to run, sim-only, inside this frame. Call from the tick hook (boss.c, portSnapTickBegin's place), with
 * no gfx task in flight (portSnapNeedIdle). Returns the number of ticks re-simulated, 0 if it could not. */
int portSnapRollbackTo(unsigned int fromTick)
{
    Slot *s;
    const unsigned int now = stageTicks() + 1u;

    if (fromTick >= now || !slotFor(fromTick, &s))
    {
        return 0;
    }
    rollbackTo(fromTick, now, s);
    return (int) (now - fromTick);
}

/* a line with the totals, for the tests (called by the tick hook of the last tick? no: on demand from the console / at exit) */
void portSnapReport(void)
{
    if (!sOn)
    {
        return;
    }
    portConLogf("ge: snap: TOTALS: %llu saves (avg %.3f ms, max %.3f ms, last %u KB), %llu restores (avg %.3f ms, max %.3f ms, avg %u KB differed), "
                "%llu re-simulated ticks (avg %.3f ms/tick, max %.3f), %u compare failures\n",
                sSaveN, sSaveN ? (double) sSaveSum / (double) sSaveN / 1000.0 : 0.0, (double) sSaveMax / 1000.0, sLastSize / 1024u, sRestN,
                sRestN ? (double) sRestSum / (double) sRestN / 1000.0 : 0.0, (double) sRestMax / 1000.0,
                sRestN ? (unsigned) (sDirtySum / sRestN / 1024u) : 0u, sResimTicks,
                sResimTicks ? (double) sResimSum / (double) sResimTicks / 1000.0 : 0.0, (double) sResimMax / 1000.0, sCmpFail);
}

int portSnapActive(void)
{
    if (!sInit)
    {
        init();
    }
    return sOn;
}

#endif /* TARGET_N64 */
