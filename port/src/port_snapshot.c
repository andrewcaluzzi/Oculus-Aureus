/* port_snapshot.c - rollback-netcode spike: snapshot / restore of the game simulation state.
 *
 * TEST ONLY and OFF by default. GE_SNAPTEST=N,K (with GE_DEMO=<demo> GE_DEMODIAG=1) makes the demo
 * tick hook (ramromreplay.c, right after port_demo_ticks++) do this:
 *   tick N     save a snapshot (timed)
 *   N..N+K     hash every snapshotted object / memp page each tick (pass 1)
 *   tick N+K   restore the snapshot (timed). The tick counter, the demo cursor, the RNG and the joy ring
 *              are all inside the snapshot, so the inputs repeat.
 *   N..N+K     hash again (pass 2) and compare to pass 1, entry by entry
 * The "ge: demo tick" fingerprint lines of both passes are compared outside (tests/snap_check.py).
 *
 * What is snapshotted:
 *   - .data/.bss ranges of the game objects (src/game, src/*.c) and the port's sim-state files,
 *     listed in snap_ranges.txt (generated from the ld map by tests/snap_map.sh)
 *   - the used part of the memp pools STAGE, ME and PERMANENT (+ a margin past the bump pointer)
 *   - optionally (GE_SNAPHIGH=1) the portAllocHigh arena
 * Env knobs: GE_SNAPCATS (default GEP), GE_SNAPEXCL (comma list of object names to leave out),
 *            GE_SNAPRANGES (path of the ranges file), GE_SNAPMARGIN (bytes, default 1 MB). */
#ifndef TARGET_N64

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ultra64.h>
#include <memp.h>

extern MemoryPool g_mempPools[];
extern const char *portRootPath(const char *name);          /* main.c */
extern unsigned long long portProfNow(void);                /* pcsched.c, microseconds */
extern void portHighArenaRange(u8 **base, u32 *used);       /* pcsched.c */
extern void romdataSnapRanges(u8 **rom, u32 *romsz, u8 **cdata, u32 *cdatasz);   /* romdata.c */

#define PAGE 4096u

typedef struct { char cat; char name[32]; u8 *addr; u32 size; } SRange;
typedef struct { u8 *p; u32 n; u32 off; u32 eoff; int copy; int grp; } Seg;
typedef struct { u8 *p; u32 n; int grp; int sidx; } Ent;

enum { G_STATIC, G_STAGE, G_ME, G_PERM, G_HIGH, G_ROM, G_COUNT };
static const char *const grpName[G_COUNT] = { "statics", "stage", "me", "perm", "high", "rom" };

static int sMode = -1, sN, sK, sPhase;
static SRange *sRng;
static int sNRng;
static Seg *sSeg;
static int sNSeg;
static Ent *sEnt;
static int sNEnt;
static u8 *sSnap;
static u32 sSnapSize, sGrpBytes[G_COUNT], sEndSize;
static u8 *sEnd;
static u32 sHighAtSave;
static unsigned long long sSimT0, sSimSum, sSimMax, sSimCnt;
static u32 *sHash[2];   /* [pass][ti * sNEnt + e] */
static u32 sTiHashMax;

static u32 hashBytes(const u8 *p, u32 n)
{
    u32 h = 0x811C9DC5u, i = 0;

    for (; i + 4 <= n; i += 4)
    {
        u32 w;

        memcpy(&w, p + i, 4);
        h = (h ^ w) * 0x9E3779B1u;
        h ^= h >> 15;
    }
    for (; i < n; i++)
    {
        h = (h ^ p[i]) * 0x01000193u;
    }
    return h;
}

static int inList(const char *list, const char *name)
{
    size_t ln = strlen(name);
    const char *p = list;

    while (p != NULL && *p != '\0')
    {
        const char *e = strchr(p, ',');
        size_t l = e != NULL ? (size_t) (e - p) : strlen(p);

        if (l == ln && strncmp(p, name, l) == 0)
        {
            return 1;
        }
        p = e != NULL ? e + 1 : NULL;
    }
    return 0;
}

static int loadRanges(void)
{
    const char *path = getenv("GE_SNAPRANGES");
    const char *cats = getenv("GE_SNAPCATS");
    const char *excl = getenv("GE_SNAPEXCL");
    char line[160];
    FILE *f;
    int cap = 0;

    if (path == NULL)
    {
        path = portRootPath("snap_ranges.txt");
    }
    if (cats == NULL)
    {
        cats = "GEP";
    }
    if (excl == NULL)
    {
        /* audio (the AL heap is music.c's static arena, so audi/snd/music statics must match it, which
         * they cannot once the mixer keeps running), scheduler / VI / profiling, framebuffer, thread
         * stacks, N64 filler. Found by trial: restoring audi.c + snd.c alone hung alSynAllocVoice. */
        excl = "audi.c,snd.c,music.c,mp_music.c,music_0D2720.c,sched.c,vi.c,pi.c,speed_graph.c,"
               "speedgraphrenderer.c,cfb.c,stacks.c,c_data_filler.c";
    }
    f = fopen(path, "r");
    if (f == NULL)
    {
        printf("ge: snaptest: cannot open %s\n", path);
        return 0;
    }
    while (fgets(line, sizeof(line), f) != NULL)
    {
        char c, name[64];
        unsigned a, s;

        if (sscanf(line, "%c %x %x %63s", &c, &a, &s, name) != 4 || strchr(cats, c) == NULL || inList(excl, name))
        {
            continue;
        }
        if (sNRng == cap)
        {
            cap = cap ? cap * 2 : 256;
            sRng = (SRange *) realloc(sRng, (size_t) cap * sizeof(SRange));
        }
        sRng[sNRng].cat = c;
        snprintf(sRng[sNRng].name, sizeof(sRng[sNRng].name), "%s", name);
        sRng[sNRng].addr = (u8 *) (uintptr_t) a;
        sRng[sNRng].size = s;
        sNRng++;
    }
    fclose(f);
    printf("ge: snaptest: %d static ranges from %s (cats %s, excluding %s)\n", sNRng, path, cats, excl);
    return sNRng > 0;
}

static void addSeg(u8 *p, u32 n, int copy, int grp)
{
    sSeg = (Seg *) realloc(sSeg, (size_t) (sNSeg + 1) * sizeof(Seg));
    sSeg[sNSeg].p = p;
    sSeg[sNSeg].n = n;
    sSeg[sNSeg].copy = copy;
    sSeg[sNSeg].grp = grp;
    sNSeg++;
}

static void addEnt(u8 *p, u32 n, int grp, int sidx)
{
    sEnt = (Ent *) realloc(sEnt, (size_t) (sNEnt + 1) * sizeof(Ent));
    sEnt[sNEnt].p = p;
    sEnt[sNEnt].n = n;
    sEnt[sNEnt].grp = grp;
    sEnt[sNEnt].sidx = sidx;
    sNEnt++;
}

static void addRegion(u8 *lo, u32 n, int copy, int grp)
{
    u32 o;

    addSeg(lo, n, copy, grp);
    for (o = 0; o < n; o += PAGE)
    {
        addEnt(lo + o, n - o < PAGE ? n - o : PAGE, grp, (int) (o / PAGE));
    }
}

/* Build the segment and hash-entry lists for the current memp / arena extents. */
static void buildLayout(void)
{
    static const int pools[3] = { MEMPOOL_STAGE, MEMPOOL_ME, MEMPOOL_PERMANENT };
    static const int pgrp[3] = { G_STAGE, G_ME, G_PERM };
    const char *m = getenv("GE_SNAPMARGIN");
    u32 margin = m != NULL ? (u32) atol(m) : (1u << 20);
    int i;
    u8 *hb = NULL;
    u32 hu = 0;
    u32 off = 0;

    /* statics: adjacent ranges merge into one memcpy segment, but each object stays its own hash entry */
    for (i = 0; i < sNRng; i++)
    {
        if (sNSeg > 0 && sSeg[sNSeg - 1].grp == G_STATIC && sSeg[sNSeg - 1].p + sSeg[sNSeg - 1].n == sRng[i].addr)
        {
            sSeg[sNSeg - 1].n += sRng[i].size;
        }
        else
        {
            addSeg(sRng[i].addr, sRng[i].size, 1, G_STATIC);
        }
        addEnt(sRng[i].addr, sRng[i].size, G_STATIC, i);
    }
    for (i = 0; i < 3; i++)
    {
        MemoryPool *mp = &g_mempPools[pools[i]];
        u8 *lo = mp->start, *hi = mp->pos;

        if (lo == NULL || hi == NULL || hi < lo || hi > mp->end)
        {
            continue;
        }
        hi = (mp->end - hi > (long) margin) ? hi + margin : mp->end;
        addRegion(lo, (u32) (hi - lo), 1, pgrp[i]);
    }
    portHighArenaRange(&hb, &hu);
    if (hb != NULL && hu > 0)
    {
        addRegion(hb, hu, getenv("GE_SNAPHIGH") != NULL, G_HIGH);
    }
    {   /* the ROM image + inflated data segment: should be immutable, hash-only to prove it */
        u8 *rom = NULL, *cd = NULL;
        u32 rs = 0, cs = 0;

        romdataSnapRanges(&rom, &rs, &cd, &cs);
        if (rom != NULL && rs > 0 && getenv("GE_SNAPNOROM") == NULL)
        {
            addRegion(rom, rs, 0, G_ROM);
        }
        if (cd != NULL && cs > 0 && getenv("GE_SNAPNOROM") == NULL)
        {
            addRegion(cd, cs, 0, G_ROM);
        }
    }
    for (i = 0; i < sNSeg; i++)
    {
        sSeg[i].off = off;
        if (sSeg[i].copy)
        {
            off += sSeg[i].n;
            sGrpBytes[sSeg[i].grp] += sSeg[i].n;
        }
    }
    sSnapSize = off;
    off = 0;
    for (i = 0; i < sNSeg; i++)
    {
        sSeg[i].eoff = off;
        off += sSeg[i].n;
    }
    sEndSize = off;
}

/* Bytes of pass-1 state at tick N+K for every segment (copied or not), used to name differing bytes. */
static void saveEnd(void)
{
    int i;

    for (i = 0; i < sNSeg; i++)
    {
        memcpy(sEnd + sSeg[i].eoff, sSeg[i].p, sSeg[i].n);
    }
}

static void diffEntry(int e)
{
    int i, shown = 0;

    for (i = 0; i < sNSeg; i++)
    {
        if (sEnt[e].p >= sSeg[i].p && sEnt[e].p < sSeg[i].p + sSeg[i].n)
        {
            u32 base = (u32) (sEnt[e].p - sSeg[i].p), k;

            for (k = 0; k + 4 <= sEnt[e].n && shown < 4; k += 4)
            {
                u32 a, b;

                memcpy(&a, sEnd + sSeg[i].eoff + base + k, 4);
                memcpy(&b, sEnt[e].p + k, 4);
                if (a != b)
                {
                    printf("ge: snaptest:       %p: pass1 %08x pass2 %08x\n", (void *) (sEnt[e].p + k), a, b);
                    shown++;
                }
            }
            return;
        }
    }
}

static void saveSnap(void)
{
    int i;

    for (i = 0; i < sNSeg; i++)
    {
        if (sSeg[i].copy)
        {
            memcpy(sSnap + sSeg[i].off, sSeg[i].p, sSeg[i].n);
        }
    }
}

static void restoreSnap(void)
{
    int i;

    for (i = 0; i < sNSeg; i++)
    {
        if (sSeg[i].copy)
        {
            memcpy(sSeg[i].p, sSnap + sSeg[i].off, sSeg[i].n);
        }
    }
}

static u32 diffSnap(void)   /* bytes of the live state that differ from the snapshot */
{
    u32 d = 0;
    int i;

    for (i = 0; i < sNSeg; i++)
    {
        u32 j;

        if (!sSeg[i].copy)
        {
            continue;
        }
        for (j = 0; j < sSeg[i].n; j++)
        {
            d += sSeg[i].p[j] != sSnap[sSeg[i].off + j];
        }
    }
    return d;
}

static void sample(int pass, int ti)
{
    u32 *h = sHash[pass] + (size_t) ti * sNEnt;
    int e;

    for (e = 0; e < sNEnt; e++)
    {
        h[e] = hashBytes(sEnt[e].p, sEnt[e].n);
    }
}

static void entName(int e, char *buf, size_t bl)
{
    if (sEnt[e].grp == G_STATIC)
    {
        snprintf(buf, bl, "%c:%s", sRng[sEnt[e].sidx].cat, sRng[sEnt[e].sidx].name);
    }
    else
    {
        snprintf(buf, bl, "%s+0x%x", grpName[sEnt[e].grp], (unsigned) sEnt[e].sidx * PAGE);
    }
}

static void init(void)
{
    const char *e = getenv("GE_SNAPTEST");

    sMode = 0;
    if (e == NULL || sscanf(e, "%d,%d", &sN, &sK) != 2 || sN < 1 || sK < 1)
    {
        return;
    }
    if (!loadRanges())
    {
        return;
    }
    sMode = 1;
    printf("ge: snaptest: armed, snapshot at demo tick %d, replay %d ticks\n", sN, sK);
}

static void report(void)
{
    int ti, e, g;
    u32 firstBad[G_COUNT];
    u32 badAtEnd[G_COUNT];
    int anyBad = 0;
    unsigned long long dirtySum = 0, dirtyMax = 0, dirtyStatSum = 0;
    char nm[64];
    int shown = 0;

    for (g = 0; g < G_COUNT; g++)
    {
        firstBad[g] = 0;
        badAtEnd[g] = 0;
    }
    for (ti = 1; ti <= sK; ti++)
    {
        for (e = 0; e < sNEnt; e++)
        {
            if (sHash[0][(size_t) ti * sNEnt + e] != sHash[1][(size_t) ti * sNEnt + e])
            {
                g = sEnt[e].grp;
                anyBad = 1;
                if (firstBad[g] == 0)
                {
                    firstBad[g] = (u32) (sN + ti);
                }
                if (ti == sK)
                {
                    badAtEnd[g]++;
                }
                if (shown < 24)
                {
                    int already = 0, k;

                    /* list each mismatching entry once, at its first bad tick */
                    for (k = 1; k < ti && !already; k++)
                    {
                        already = sHash[0][(size_t) k * sNEnt + e] != sHash[1][(size_t) k * sNEnt + e];
                    }
                    if (!already)
                    {
                        entName(e, nm, sizeof(nm));
                        printf("ge: snaptest:   MISMATCH %-28s %p first at tick %d\n", nm, (void *) sEnt[e].p, sN + ti);
                        shown++;
                        if (sHash[0][(size_t) sK * sNEnt + e] != sHash[1][(size_t) sK * sNEnt + e])
                        {
                            diffEntry(e);
                        }
                    }
                }
            }
        }
    }
    {
        unsigned long long grpDirty[G_COUNT] = { 0 };
        unsigned romChanged = 0;

        for (ti = 1; ti <= sK; ti++)
        {
            unsigned long long d = 0, ds = 0;

            for (e = 0; e < sNEnt; e++)
            {
                if (sHash[0][(size_t) ti * sNEnt + e] != sHash[0][(size_t) (ti - 1) * sNEnt + e])
                {
                    d += sEnt[e].n;
                    grpDirty[sEnt[e].grp] += sEnt[e].n;
                    if (sEnt[e].grp == G_STATIC)
                    {
                        ds += sEnt[e].n;
                    }
                }
                if (sEnt[e].grp == G_ROM && sHash[0][(size_t) ti * sNEnt + e] != sHash[0][e])
                {
                    romChanged++;
                }
            }
            dirtySum += d;
            dirtyStatSum += ds;
            if (d > dirtyMax)
            {
                dirtyMax = d;
            }
        }
        printf("ge: snaptest: dirty per tick (entries whose hash changed; pool pages are %u B): avg %llu B (statics %llu B), max %llu B\n",
               PAGE, dirtySum / (unsigned) sK, dirtyStatSum / (unsigned) sK, dirtyMax);
        printf("ge: snaptest: dirty avg per tick by group:");
        for (g = 0; g < G_COUNT; g++)
        {
            printf(" %s=%llu B", grpName[g], grpDirty[g] / (unsigned) sK);
        }
        printf("\nge: snaptest: ROM image / cdata pages that differ from tick %d at some later tick: %u\n", sN, romChanged);
    }
    {
        u8 *hb = NULL;
        u32 hu = 0;

        portHighArenaRange(&hb, &hu);
        printf("ge: snaptest: high arena used: %u KB at tick %d snapshot, %u KB after the replay (not rolled back: %u KB leaked)\n",
               sHighAtSave / 1024, sN, hu / 1024, (hu - sHighAtSave) / 1024);
    }
    if (sSimCnt > 0)
    {
        printf("ge: snaptest: sim step (lvlTick + view, no render) over %llu ticks: avg %.3f ms, max %.3f ms\n", sSimCnt,
               (double) sSimSum / (double) sSimCnt / 1000.0, (double) sSimMax / 1000.0);
    }
    for (g = 0; g < G_COUNT; g++)
    {
        if (firstBad[g] != 0)
        {
            printf("ge: snaptest:   group %-8s first state mismatch at tick %u, %u entries differ at tick %d\n", grpName[g],
                   firstBad[g], badAtEnd[g], sN + sK);
        }
    }
    printf("ge: snaptest: STATE-HASH %s (%d entries x %d ticks compared)\n", anyBad ? "MISMATCH" : "MATCH", sNEnt, sK);
}

/* boss.c brackets lvlTick + lvlViewMoveTick (the whole simulation step, no rendering) with these. */
void portSnapSimMark(s32 begin)
{
    if (sMode != 1 || sPhase < 1)
    {
        return;
    }
    if (begin)
    {
        sSimT0 = portProfNow();
    }
    else if (sSimT0 != 0)
    {
        unsigned long long d = portProfNow() - sSimT0;

        sSimSum += d;
        sSimCnt++;
        if (d > sSimMax)
        {
            sSimMax = d;
        }
        sSimT0 = 0;
    }
}

void portSnapTick(s32 tick)
{
    if (sMode < 0)
    {
        init();
    }
    if (sMode == 0 || sPhase >= 3)
    {
        return;
    }
    if (sPhase == 0)
    {
        unsigned long long t0, t1;
        int i;

        if (tick != sN)
        {
            return;
        }
        buildLayout();
        sSnap = (u8 *) malloc(sSnapSize ? sSnapSize : 1);
        sEnd = (u8 *) malloc(sEndSize ? sEndSize : 1);
        {
            u32 hi = 0;
            int q;

            sHighAtSave = 0;
            for (q = 0; q < sNSeg; q++)
            {
                sHighAtSave += sSeg[q].grp == G_HIGH ? sSeg[q].n : 0;
            }

            for (q = 0; q < sNSeg; q++)
            {
                hi += sSeg[q].grp == G_HIGH ? sSeg[q].n : 0;
            }
            printf("ge: snaptest: layout: %d segments, %d hash entries, hashed bytes %u KB (high arena used %u KB, %s)\n",
                   sNSeg, sNEnt, sEndSize / 1024, hi / 1024, getenv("GE_SNAPHIGH") ? "snapshotted" : "hash only");
        }
        sTiHashMax = (u32) sK + 1;
        sHash[0] = (u32 *) calloc((size_t) sTiHashMax * sNEnt, 4);
        sHash[1] = (u32 *) calloc((size_t) sTiHashMax * sNEnt, 4);
        t0 = portProfNow();
        saveSnap();
        t1 = portProfNow();
        printf("ge: snaptest: SAVE at tick %d: %u KB (statics %u KB, stage %u KB, me %u KB, perm %u KB), %.3f ms first call\n",
               tick, sSnapSize / 1024, sGrpBytes[G_STATIC] / 1024, sGrpBytes[G_STAGE] / 1024, sGrpBytes[G_ME] / 1024,
               sGrpBytes[G_PERM] / 1024, (double) (t1 - t0) / 1000.0);
        t0 = portProfNow();
        for (i = 0; i < 50; i++)
        {
            saveSnap();
        }
        t1 = portProfNow();
        printf("ge: snaptest: SAVE warm average over 50: %.3f ms\n", (double) (t1 - t0) / 1000.0 / 50.0);
        t0 = portProfNow();
        sample(0, 0);
        t1 = portProfNow();
        printf("ge: snaptest: (full-state hash of %d entries: %.3f ms)\n", sNEnt, (double) (t1 - t0) / 1000.0);
        sPhase = 1;
        return;
    }
    if (sPhase == 1)
    {
        int ti = tick - sN;

        if (ti < 1 || ti > sK)
        {
            return;
        }
        sample(0, ti);
        if (ti == sK)
        {
            unsigned long long t0, t1;
            int i;

            saveEnd();
            t0 = portProfNow();
            restoreSnap();
            t1 = portProfNow();
            printf("ge: snaptest: RESTORE at tick %d (back to tick %d): %.3f ms first call; live-vs-snapshot differing bytes now %u\n",
                   tick, sN, (double) (t1 - t0) / 1000.0, diffSnap());
            t0 = portProfNow();
            for (i = 0; i < 50; i++)
            {
                restoreSnap();
            }
            t1 = portProfNow();
            printf("ge: snaptest: RESTORE warm average over 50: %.3f ms\n", (double) (t1 - t0) / 1000.0 / 50.0);
            sPhase = 2;
        }
        return;
    }
    if (sPhase == 2)
    {
        int ti = tick - sN;

        if (ti < 1 || ti > sK)
        {
            return;
        }
        sample(1, ti);
        if (ti == sK)
        {
            report();
            sPhase = 3;
        }
    }
}

#endif /* TARGET_N64 */
