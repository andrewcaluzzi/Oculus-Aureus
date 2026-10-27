/* gfx_corpsebake.inl - baked corpses (included by gfx_pc.cpp after gfx_chrbatch.inl; render thread, presentation only).
 *
 * A kept body never moves again, yet every frame its display list is walked, its limbs' vertices transformed and its triangles
 * clipped, culled and emitted on the render thread (~4-5 ms for 150 corpses on an i7-8700K, the largest single cost of a late
 * wave). Baking does that once:
 *   1. the sim (port_perfcat.c) marks a settled corpse's opaque pass with a BAKE marker (PKB_MAGIC: slot + serial). Its triangles
 *      are drawn as usual that frame and also captured here, every one of them: the frustum reject and the backface cull are
 *      skipped for the capture (the room cache's roc_drop path), each with its draw state and cull mode;
 *   2. at the view's CROWD_CORPSES command (port_crowd.c, just before the world end) the captured clip-space positions go back to
 *      world space through the inverse of that view's world-to-clip (the crowd's matrix plus Fast3D's aspect fix and y flip),
 *      and g_PortBakeDone[slot] = serial tells the sim;
 *   3. from then on the sim drops the corpse's opaque commands (still building them: the sim sees the same) and lists it in
 *      CROWD_CORPSES; the listed corpses' triangles are transformed by the current matrix (one 4x4 per vertex) and drawn
 *      bucketed by draw state + cull mode, the cull on the GPU (front = CCW, as the retained room cache proved).
 * Not baked (the sim keeps the normal path): a corpse with translucent, decal, fogged or texgen triangles in its opaque pass;
 * any bake made before a texture-cache eviction (ge_texcache_gen moved: a GL texture id may name another texture now). */

#include "gfx_crowd.h"                          /* CrowdCorpses */
extern "C" uint32_t gfx_room_cache;             /* gfx_roomcache.inl, below */

#define PKB_SLOTS 1024
#define PKB_MAGIC 0x5Bu                        /* G_NOOP w1 >> 24: w1 = magic << 24 | serial (14 bits) << 10 | slot */

struct PkbGroup {
    struct DXluState s;                         /* viewport / scissor zeroed: the current view's are used when drawn */
    uint32_t cull;                              /* G_CULL_* bits at capture */
    uint32_t stride;                            /* floats per vertex */
    uint32_t hash;
    std::vector<float> v;                       /* clip-space floats while pending, world-space after (x y z 1, the rest as captured) */
    uint32_t ntris;
};
struct PkbSlot {
    int32_t serial;
    uint8_t state;                              /* 0 empty, 1 captured (awaiting its view's matrix), 2 baked */
    uint32_t texgen;
    uint32_t last_drawn;                        /* num_dls of the last frame that drew it (old ones are freed) */
    std::vector<PkbGroup> g;
};
static PkbSlot pkb_slot[PKB_SLOTS];
static int pkb_cur = -1;                        /* the slot being captured, -1 none */
static int32_t pkb_cur_serial;
static bool pkb_bad;
static uint32_t pkb_why;                        /* why the capture was refused (bits: see pkb_capture), for GE_BAKE_LOG=1 */
static float pkb_fog_first;                     /* the capture's fog factor (must not vary), -1 = none seen */
static std::vector<int> pkb_pending;            /* captured this view, converted at its CROWD_CORPSES */
static unsigned int pkb_stat_baked, pkb_stat_failed, pkb_stat_drawn, pkb_stat_tris, pkb_stat_dropped;
extern "C" volatile int g_PortBakeDone[PKB_SLOTS]; /* port_perfcat.c: serial baked (> 0), -serial failed, 0 none */

struct PkbBucket {
    struct DXluState s;
    uint32_t cull, hash, ntris;
    std::vector<float> v;
};
static std::vector<PkbBucket> pkb_buckets;
static uint32_t pkb_nbuckets;
static int16_t pkb_bhash[1024];

static uint32_t pkb_hash(const struct DXluState* s, uint32_t cull) {
    const uint8_t* p = (const uint8_t*) s;
    uint32_t h = 2166136261u ^ cull;
    for (size_t i = 0; i < sizeof(*s); i++) {
        h = (h ^ p[i]) * 16777619u;
    }
    return h;
}

static void pkb_fail(void) {
    if (pkb_cur >= 0) {
        static int log = -1, logged = 0;
        if (log < 0) {
            const char* e = getenv("GE_BAKE_LOG");
            log = (e != NULL && e[0] != '0') ? 1 : 0;
        }
        if (log && logged < 40) {
            logged++;
            fprintf(stderr, "ge: bake: slot %d serial %d refused, why 0x%x (1 rect, 2 blended, 4 depth, 8 decal, 16 pixel fog, "
                    "32 texgen, 64 size, 128 fog varies; 0 = nothing captured / no view)\n", pkb_cur, (int) pkb_cur_serial,
                    (unsigned) pkb_why);
        }
        pkb_slot[pkb_cur].state = 0;
        pkb_slot[pkb_cur].g.clear();
        g_PortBakeDone[pkb_cur] = -pkb_cur_serial;
        pkb_stat_failed++;
        pkb_cur = -1;
    }
}

/* a view begins (and a frame): a capture or a conversion left over from the last view never completed */
static void pkb_view_reset(void) {
    pkb_fail();
    for (int slot : pkb_pending) {
        pkb_cur = slot;
        pkb_cur_serial = pkb_slot[slot].serial;
        pkb_fail();
    }
    pkb_pending.clear();
}

/* the BAKE marker (G_NOOP, PKB_MAGIC) */
static void pkb_begin(uint32_t w1) {
    const int slot = (int) (w1 & 1023u);
    const int32_t serial = (int32_t) ((w1 >> 10) & 0x3FFFu);
    if (pkb_cur >= 0) {
        pkb_fail();   /* never closed: something unexpected in between */
    }
    PkbSlot& s = pkb_slot[slot];
    if (s.serial == serial && s.state != 0) {
        return;       /* already baked (the sim has not seen it yet), or a second view of the same frame */
    }
    if (gfx_room_cache != 0 || fbActive || dxlu_closed) {
        s.serial = serial;
        pkb_cur = slot;
        pkb_cur_serial = serial;
        pkb_fail();
        return;
    }
    s.serial = serial;
    s.state = 1;
    s.g.clear();
    pkb_cur = slot;
    pkb_cur_serial = serial;
    pkb_bad = false;
    pkb_why = 0;
    pkb_fog_first = -1.0f;
}

/* the next category marker closes the capture */
static void pkb_end(void) {
    if (pkb_cur < 0) {
        return;
    }
    if (pkb_bad || pkb_slot[pkb_cur].g.empty()) {
        pkb_fail();
        return;
    }
    pkb_pending.push_back(pkb_cur);
    pkb_cur = -1;
}

/* gfx_sp_tri1, every triangle of the prop being captured (rejected and culled ones included), floats at buf_vbo[start..len) */
static void pkb_capture(uint32_t start, bool is_rect, struct ShaderProgram* prg, bool use_alpha, bool use_modulate, bool depth_test,
                        bool depth_update, bool depth_compare, bool depth_source_prim, uint16_t zmode, uint8_t depth_mode,
                        const bool used_textures[2], bool use_fog, bool use_pixfog, int fog_ofs) {
    const uint32_t nf = buf_vbo_len - start;
    /* opaque, or blended without a depth write (the body's shadow blob): both kinds bake; the blended ones draw after */
    const bool shadow = use_alpha && depth_test && depth_compare && !depth_update;
    uint32_t why = (is_rect ? 1u : 0u) | (use_alpha && !shadow ? 2u : 0u) | (!depth_test || (!depth_update && !shadow) || !depth_compare ? 4u : 0u)
        | (zmode == ZMODE_DEC ? 8u : 0u) | (use_pixfog ? 16u : 0u) | ((rsp.geometry_mode & G_TEXTURE_GEN) != 0 ? 32u : 0u)
        | (nf == 0 || nf > 3 * DXLU_MAX_VERT_FLOATS || nf % 3 != 0 ? 64u : 0u);
    if (why == 0 && use_fog && fog_ofs >= 0) {
        /* the fog factor is per vertex and follows the eye distance: a bake can keep it only while it is the same for every
         * vertex (a fogless level: chrs blend their shade through the fog colour at a constant factor) */
        const uint32_t stride = nf / 3;
        for (uint32_t k = 0; k < 3; k++) {
            const float f = buf_vbo[start + k * stride + (uint32_t) fog_ofs + 3];
            if (pkb_fog_first < 0.0f) {
                pkb_fog_first = f;
            } else if (fabsf(f - pkb_fog_first) > 0.5f / 255.0f) {
                why |= 128u;
                break;
            }
        }
    }
    if (why != 0) {
        pkb_bad = true;
        pkb_why |= why;
        return;
    }
    struct DXluState st;
    pcb_make_state(&st, prg, use_alpha, use_modulate, depth_test, depth_update, depth_compare, depth_source_prim, zmode, depth_mode,
                   used_textures);
    memset(&st.viewport, 0, sizeof(st.viewport));
    memset(&st.scissor, 0, sizeof(st.scissor));
    const uint32_t cull = rsp.geometry_mode & G_CULL_BOTH;
    std::vector<PkbGroup>& gs = pkb_slot[pkb_cur].g;
    PkbGroup* g = nullptr;
    for (size_t i = gs.size(); i-- > 0;) {   /* consecutive triangles share a group: the newest first */
        if (gs[i].cull == cull && memcmp(&gs[i].s, &st, sizeof(st)) == 0) {
            g = &gs[i];
            break;
        }
    }
    if (g == nullptr) {
        gs.emplace_back();
        g = &gs.back();
        g->s = st;
        g->cull = cull;
        g->stride = nf / 3;
        g->hash = pkb_hash(&st, cull);
        g->ntris = 0;
    }
    if (g->stride != nf / 3) {
        pkb_bad = true;
        return;
    }
    g->v.insert(g->v.end(), buf_vbo + start, buf_vbo + buf_vbo_len);
    g->ntris++;
}

/* world -> buf_vbo position: the crowd's world-to-clip (row vectors) followed by Fast3D's x fix and y flip */
static void pkb_matrix(const CrowdCorpses* c, const float fix[2], bool flip, double a[4][4]) {
    for (int i = 0; i < 4; i++) {
        const double m0 = c->mvp[i * 4 + 0], m1 = c->mvp[i * 4 + 1], m2 = c->mvp[i * 4 + 2], m3 = c->mvp[i * 4 + 3];
        a[i][0] = m0 * fix[0] + m3 * fix[1];
        a[i][1] = flip ? -m1 : m1;
        a[i][2] = m2;
        a[i][3] = m3;
    }
}

static bool pkb_invert(const double m[4][4], double inv[4][4]) {
    double a[4][8];
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            a[i][j] = m[i][j];
            a[i][j + 4] = (i == j) ? 1.0 : 0.0;
        }
    }
    for (int c = 0; c < 4; c++) {
        int p = c;
        for (int r = c + 1; r < 4; r++) {
            if (fabs(a[r][c]) > fabs(a[p][c])) p = r;
        }
        if (fabs(a[p][c]) < 1e-12) {
            return false;
        }
        if (p != c) {
            for (int j = 0; j < 8; j++) std::swap(a[p][j], a[c][j]);
        }
        const double d = a[c][c];
        for (int j = 0; j < 8; j++) a[c][j] /= d;
        for (int r = 0; r < 4; r++) {
            if (r != c && a[r][c] != 0.0) {
                const double f = a[r][c];
                for (int j = 0; j < 8; j++) a[r][j] -= f * a[c][j];
            }
        }
    }
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) inv[i][j] = a[i][j + 4];
    }
    return true;
}

extern "C" void gfx_opengl_bake_cull(uint32_t cull_bits, int invert_y); /* gfx_opengl.cpp: GPU backface cull for the baked draws (0 = off) */

/* CROWD_CORPSES: bake what this view captured, then draw the listed baked corpses */
static void pkb_corpses(const CrowdCorpses* c, const float fix[2], bool flip) {
    pkb_end();
    double a[4][4], inv[4][4];
    pkb_matrix(c, fix, flip, a);
    const bool ok = pkb_invert(a, inv);
    for (int slot : pkb_pending) {
        PkbSlot& s = pkb_slot[slot];
        if (s.state != 1) {
            continue;
        }
        if (!ok) {
            pkb_cur = slot;
            pkb_cur_serial = s.serial;
            pkb_fail();
            continue;
        }
        for (PkbGroup& g : s.g) {
            for (size_t o = 0; o < g.v.size(); o += g.stride) {
                float* p = &g.v[o];
                double w[4];
                for (int j = 0; j < 4; j++) {
                    w[j] = p[0] * inv[0][j] + p[1] * inv[1][j] + p[2] * inv[2][j] + p[3] * inv[3][j];
                }
                const double iw = (w[3] != 0.0) ? 1.0 / w[3] : 0.0;
                p[0] = (float) (w[0] * iw);
                p[1] = (float) (w[1] * iw);
                p[2] = (float) (w[2] * iw);
                p[3] = 1.0f;
            }
        }
        s.state = 2;
        s.texgen = ge_texcache_gen;
        s.last_drawn = num_dls;
        g_PortBakeDone[slot] = s.serial;
        pkb_stat_baked++;
    }
    pkb_pending.clear();

    /* the listed corpses into buckets by draw state + cull, positions to this view's clip space */
    pkb_nbuckets = 0;
    memset(pkb_bhash, 0, sizeof(pkb_bhash));
    float am[4][4];
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) am[i][j] = (float) a[i][j];
    for (int k = 0; k < c->n; k++) {
        const int slot = c->slot[k] & (PKB_SLOTS - 1);
        PkbSlot& s = pkb_slot[slot];
        if (s.state != 2 || s.serial != c->serial[k]) {
            pkb_stat_dropped++;
            continue;
        }
        if (s.texgen != ge_texcache_gen) {   /* a texture was evicted since: its GL id may be another texture's now */
            s.state = 0;
            s.g.clear();
            g_PortBakeDone[slot] = 0;
            pkb_stat_dropped++;
            continue;
        }
        s.last_drawn = num_dls;
        pkb_stat_drawn++;
        for (const PkbGroup& g : s.g) {
            uint32_t h = g.hash & 1023u;
            PkbBucket* b = nullptr;
            for (int probe = 0; probe < 1024; probe++, h = (h + 1) & 1023u) {
                const int16_t e = pkb_bhash[h];
                if (e == 0) {
                    if (pkb_nbuckets >= pkb_buckets.size()) pkb_buckets.emplace_back();
                    b = &pkb_buckets[pkb_nbuckets];
                    b->s = g.s;
                    b->cull = g.cull;
                    b->hash = g.hash;
                    b->ntris = 0;
                    b->v.clear();
                    pkb_bhash[h] = (int16_t) (++pkb_nbuckets);
                    break;
                }
                PkbBucket* x = &pkb_buckets[e - 1];
                if (x->hash == g.hash && x->cull == g.cull && memcmp(&x->s, &g.s, sizeof(g.s)) == 0) {
                    b = x;
                    break;
                }
            }
            if (b == nullptr) {
                continue;
            }
            const size_t base = b->v.size();
            b->v.insert(b->v.end(), g.v.begin(), g.v.end());
            for (size_t o = base; o < b->v.size(); o += g.stride) {
                float* p = &b->v[o];
                const float x = p[0], y = p[1], z = p[2];
                p[0] = x * am[0][0] + y * am[1][0] + z * am[2][0] + am[3][0];
                p[1] = x * am[0][1] + y * am[1][1] + z * am[2][1] + am[3][1];
                p[2] = x * am[0][2] + y * am[1][2] + z * am[2][2] + am[3][2];
                p[3] = x * am[0][3] + y * am[1][3] + z * am[2][3] + am[3][3];
            }
            b->ntris += g.ntris;
        }
    }
    if (pkb_nbuckets > 0) {
        gfx_flush();
        const uint8_t saved = pc_cur;
        pc_switch(PERFCAT_BATCH);
        for (uint32_t i2 = 0; i2 < 2 * pkb_nbuckets; i2++) {   /* the opaque buckets, then the blended (shadows) over them */
            PkbBucket& b = pkb_buckets[i2 % pkb_nbuckets];
            if (b.s.use_alpha != (i2 >= pkb_nbuckets)) {
                continue;
            }
            b.s.viewport = rdp.viewport;   /* this view's; the whole view as the scissor (the z-buffer does the rest) */
            b.s.scissor.x = rdp.viewport.x;
            b.s.scissor.y = rdp.viewport.y;
            b.s.scissor.width = rdp.viewport.width;
            b.s.scissor.height = rdp.viewport.height;
            gfx_dxlu_apply_state(&b.s);
            gfx_opengl_bake_cull(b.cull, flip ? 1 : 0);
            PC_FLUSH_BEGIN();
            gfx_rapi->draw_triangles(b.v.data(), b.v.size(), b.ntris);
            pc_buf = PERFCAT_BATCH;
            PC_FLUSH_END();
            g_PortGfxDraws++;
            pkb_stat_tris += b.ntris;
        }
        gfx_opengl_bake_cull(0, 0);
        rdp.viewport_or_scissor_changed = true;
        pc_switch(saved);
    }
    /* bakes no view has drawn for 6000 frames (about a minute) go; they come back from a fresh capture when seen again */
    if ((num_dls & 63u) == 0) {
        for (int i = 0; i < PKB_SLOTS; i++) {
            PkbSlot& s = pkb_slot[i];
            if (s.state == 2 && num_dls - s.last_drawn > 6000u) {
                s.state = 0;
                std::vector<PkbGroup>().swap(s.g);
                g_PortBakeDone[i] = 0;
            }
        }
    }
}

/* counters for the bench report (bakes made / failed, corpses drawn baked, their triangles, stale entries skipped) */
extern "C" void gfx_corpsebake_stats(unsigned int out[5]) {
    out[0] = pkb_stat_baked; out[1] = pkb_stat_failed; out[2] = pkb_stat_drawn; out[3] = pkb_stat_tris; out[4] = pkb_stat_dropped;
}
