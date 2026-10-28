/* gfx_chrbatch.inl - the chr batch (included by gfx_pc.cpp after gfx_dxlu_apply_state; render thread, presentation only).
 *
 * A GE chr is ~7 GL draws: every limb changes texture or combiner, and each change flushes the batch. With 150 spectators,
 * 150 kept corpses and 40 live enemies that is a few thousand draws a frame, and on a PC the render thread pays for each one
 * (state changes, a buffer orphan and the draw call: the arena's late waves were render-thread bound at 2000-3200 draws).
 * The triangles of the props the sim marks (PERFCAT_FLAG_BATCH, port_perfcat.c: spectators, corpses, live chrs) are already
 * transformed to clip space by the time they would be drawn, and each one carries everything its draw needs in its vertex
 * floats except the draw state, which DXluState records (the deferred-XLU sorter replays translucent triangles the same way).
 * So instead of drawing them in place, the opaque ones are appended to one buffer PER DRAW STATE and every buffer is drawn
 * once, before the sorted translucent pass of the view: every chr of a skin shares its states, so the whole crowd, the corpses
 * and the fighters cost about one draw per distinct limb texture.
 *   - only depth-tested, depth-writing, non-decal triangles of the world pass (NEWVIEW .. WORLDEND), no rects, no room cache;
 *     opaque ones are bucketed (z-buffered, so order does not show; buckets keep first-seen order, so a model's parts keep
 *     their own relative order), blended ones that write depth (cutout edges) go after all the opaque ones, in submission order
 *   - a decal triangle (ZMODE_DEC) during a batched prop replays the batch first, so it z-compares against the surface it is on
 *   - GE_CHR_BATCH=0 / crowd.batch(false) turns it off (the sim then writes no batch flag). */

struct PcbBucket {
    struct DXluState s;
    std::vector<float> v;
    uint32_t ntris;
};
struct PcbLateRun {
    uint32_t state, offset, nfloats, ntris;
};

static std::vector<PcbBucket> pcb_buckets;      /* [0 .. pcb_n) used this view; capacity kept between frames */
static uint32_t pcb_n;
static int16_t pcb_hash[1024];                  /* state hash -> bucket index + 1 (open addressing, cleared per replay) */
static int32_t pcb_last = -1;                   /* bucket of the last captured triangle (consecutive triangles share it) */
static std::vector<float> pcb_late;             /* blended depth-writing triangles, in submission order */
static std::vector<PcbLateRun> pcb_late_runs;
static unsigned int pcb_stat_tris, pcb_stat_draws; /* since the last GE_CHR_BATCH_LOG line */

static uint32_t pcb_hash_state(const struct DXluState* s) {
    const uint8_t* p = (const uint8_t*) s;
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < sizeof(*s); i++) {
        h = (h ^ p[i]) * 16777619u;
    }
    return h;
}

/* bucket index of state s, a new bucket when unseen this view; -1 when the table is full */
static int32_t pcb_bucket_of(const struct DXluState* s) {
    if (pcb_last >= 0 && memcmp(&pcb_buckets[pcb_last].s, s, sizeof(*s)) == 0) {
        return pcb_last;
    }
    uint32_t h = pcb_hash_state(s) & 1023u;
    for (int probe = 0; probe < 1024; probe++, h = (h + 1) & 1023u) {
        const int16_t e = pcb_hash[h];
        if (e == 0) {
            if (pcb_n >= 768) {
                return -1;
            }
            if (pcb_n >= pcb_buckets.size()) {
                pcb_buckets.emplace_back();
            }
            PcbBucket& b = pcb_buckets[pcb_n];
            b.s = *s;
            b.v.clear();
            b.ntris = 0;
            pcb_hash[h] = (int16_t) (pcb_n + 1);
            return (int32_t) pcb_n++;
        }
        if (memcmp(&pcb_buckets[e - 1].s, s, sizeof(*s)) == 0) {
            return e - 1;
        }
    }
    return -1;
}

static void pcb_make_state(struct DXluState* s, struct ShaderProgram* prg, bool use_alpha, bool use_modulate, bool depth_test,
                           bool depth_update, bool depth_compare, bool depth_source_prim, uint16_t zmode, uint8_t depth_mode,
                           const bool used_textures[2]) {
    memset(s, 0, sizeof(*s)); /* zero padding: the hash and memcmp read every byte */
    s->prg = prg;
    s->use_alpha = use_alpha;
    s->use_modulate = use_modulate;
    s->depth_test = depth_test;
    s->depth_update = depth_update;
    s->depth_compare = depth_compare;
    s->depth_source_prim = depth_source_prim;
    s->zmode = zmode;
    s->depth_mode_enc = depth_mode;
    s->viewport = rendering_state.viewport;
    s->scissor = rendering_state.scissor;
    for (int t = 0; t < 2; t++) {
        if (used_textures[t] && rendering_state.textures[t] != NULL) {
            s->tex_used[t] = true;
            s->tex_id[t] = rendering_state.textures[t]->second.texture_id;
            s->tex_linear[t] = rendering_state.textures[t]->second.linear_filter;
            s->tex_mip[t] = rendering_state.textures[t]->second.mip_filter;
            s->tex_cms[t] = rendering_state.textures[t]->second.cms;
            s->tex_cmt[t] = rendering_state.textures[t]->second.cmt;
        }
    }
}

/* gfx_sp_tri1, the triangle's floats at buf_vbo[start .. buf_vbo_len): true = taken (the caller drops them from buf_vbo) */
static bool pcb_capture(uint32_t start, struct ShaderProgram* prg, bool use_alpha, bool use_modulate, bool depth_test,
                        bool depth_update, bool depth_compare, bool depth_source_prim, uint16_t zmode, uint8_t depth_mode,
                        const bool used_textures[2]) {
    const uint32_t nf = buf_vbo_len - start;
    if (nf == 0 || nf > 3 * DXLU_MAX_VERT_FLOATS) {
        return false;
    }
    struct DXluState s;
    pcb_make_state(&s, prg, use_alpha, use_modulate, depth_test, depth_update, depth_compare, depth_source_prim, zmode, depth_mode,
                   used_textures);
    const int32_t bi = pcb_bucket_of(&s);
    if (bi < 0) {
        return false;
    }
    pcb_last = bi;
    if (!use_alpha) {
        PcbBucket& b = pcb_buckets[bi];
        b.v.insert(b.v.end(), buf_vbo + start, buf_vbo + buf_vbo_len);
        b.ntris++;
    } else {
        /* blended but depth-writing (a cutout's soft edge): after every opaque bucket, in order */
        if (!pcb_late_runs.empty() && pcb_late_runs.back().state == (uint32_t) bi) {
            pcb_late_runs.back().nfloats += nf;
            pcb_late_runs.back().ntris++;
        } else {
            pcb_late_runs.push_back({ (uint32_t) bi, (uint32_t) pcb_late.size(), nf, 1u });
        }
        pcb_late.insert(pcb_late.end(), buf_vbo + start, buf_vbo + buf_vbo_len);
    }
    pc_tris[pc_cur]++;
    pcb_stat_tris++;
    return true;
}

static void pcb_draw(const struct DXluState* s, float* v, size_t nfloats, size_t ntris) {
    gfx_dxlu_apply_state(s);   /* flushes the immediate batch first */
    PC_FLUSH_BEGIN();
    gfx_rapi->draw_triangles(v, nfloats, ntris);
    pc_buf = PERFCAT_BATCH;
    PC_FLUSH_END();
    g_PortGfxDraws++;
    pcb_stat_draws++;
}

/* draw everything batched since the view began (before the sorted translucent pass, a framebuffer change, a decal) */
static void pcb_replay(void) {
    if (pcb_n == 0) {
        return;
    }
    gfx_flush();
    const uint8_t saved = pc_cur;
    pc_switch(PERFCAT_BATCH);
    for (uint32_t i = 0; i < pcb_n; i++) {
        PcbBucket& b = pcb_buckets[i];
        if (b.ntris != 0) {
            pcb_draw(&b.s, b.v.data(), b.v.size(), b.ntris);
        }
    }
    for (const PcbLateRun& r : pcb_late_runs) {
        pcb_draw(&pcb_buckets[r.state].s, pcb_late.data() + r.offset, r.nfloats, r.ntris);
    }
    /* as after the sorted pass: the replay set GL state directly, so the next immediate draw re-derives it */
    rdp.viewport_or_scissor_changed = true;
    pc_switch(saved);
    pcb_n = 0;
    pcb_last = -1;
    pcb_late.clear();
    pcb_late_runs.clear();
    memset(pcb_hash, 0, sizeof(pcb_hash));
}

static bool gfx_chr_batch_enabled(void) {
    static int v = -1;
    if (v < 0) {
        const char* e = getenv("GE_CHR_BATCH");
        v = (e != NULL && e[0] == '0') ? 0 : 1;
    }
    return v != 0;
}
