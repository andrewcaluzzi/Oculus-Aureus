/* ---- GE port: static room geometry cache ("roc") --------------------------
 *
 * Included by gfx_pc.cpp (it lives on gfx_pc's statics: rsp, rdp, buf_vbo,
 * rendering_state, gfx_rapi, DXluState, the texture cache). Enabled by
 * gfx_room_cache ([display] room_cache; default OFF everywhere since the 2026-09-11 A/B).
 *
 * WHY: on the Original Xbox the frame is CPU-bound in this interpreter -
 * gfx_sp_tri1 + gfx_run_dl + the deferred-XLU sorter are ~40% of every
 * heavy frame (docs/port/xbox-port.md, nineteenth run's line profile) - and
 * most of those triangles are the background rooms, whose display lists are
 * immutable between load and unload (bg.c: built once by bgLoadRoomModelData
 * + texLoadFromGdl; only lightfixture.c's shot-out lights edit vertex colours,
 * and that site invalidates us). Per frame the game emits, per visible room,
 * a modelview G_MTX (floating origin: changes every frame), a vertex segment
 * and G_DL(room OPA list); fog colour / position and the scissor rectangle
 * are entry state set outside the list.
 *
 * WHAT: the first time a registered room OPA list runs, the interpreter
 * renders it normally AND records every draw it produced: the exact
 * buf_vbo float stream per vertex (uv, combiner inputs - all static), the
 * model-space position of each emitted vertex, the draw state (a DXluState
 * plus the texture cache keys), and the per-draw constants the dynamic
 * fields depend on. Triangles that the frame culled or rejected are recorded
 * too (they still hit the state path; their floats are dropped again after
 * recording), so the cache holds the whole room. On later frames the G_DL is
 * answered from the cache: per draw apply the state (current viewport /
 * scissor, cached everything else), then per triangle transform the three
 * model-space positions with the CURRENT MP matrix, re-derive the aspect
 * adjust, clip codes, trivial reject, backface cull, z remap / decal pull,
 * fog colour + per-vertex fog alpha and pixfog registers exactly as
 * gfx_sp_vertex / gfx_sp_tri1 would, copy the static floats over, and feed
 * draw_triangles. The renderer's own clipper handles the rest, as before.
 *
 * WHAT IS NOT CACHED: XLU room lists (they must reach the deferred sorter),
 * props/characters/weapons/effects (not registered), any room whose list
 * loads a matrix, draws a rect/sky triangle or takes the deferred-XLU path
 * during capture (marked uncacheable, interpreter forever). A room whose
 * cached texture ids no longer match the texture cache (eviction) falls
 * back to the interpreter for that frame and is recaptured on the next.
 *
 * INVALIDATION: bg.c delete_room_data (portRoomCacheForgetRoom), the light
 * break edits in lightfixture.c (same), gfx_texture_cache_clear_c = stage
 * boundary (portRoomCacheClear), a changed list pointer at registration.
 * ------------------------------------------------------------------------ */

#include <unordered_map>
#include <vector>
#include <cstdarg>

/* default 0 everywhere since the room-cache A/B of 2026-09-11 (Cradle,
 * `[display] room_cache` 0 vs 1: 59-62 us per draw either way - the replay
 * cost what the interpreter would have). Kept as the skeleton of a GPU-side
 * room path; `room_cache = 1` in the ini re-enables it. */
/* 0 = off, 1 = the CPU replay above, 2 = GPU RETAINED (thirty-first build):
 * at capture the room becomes an object-space vertex buffer in the backend
 * (gfx_rendering_api retained_*), per frame the CPU applies each draw's state,
 * hands the vertex program the room's MP matrix / fog constants and issues one
 * DRAW_ARRAYS per draw - no per-triangle work at all. Mode 2 falls back to the
 * interpreter for rooms the builder refuses (decal-pull draws, y-inverted
 * captures, backend out of buffers). */
extern "C" uint32_t gfx_room_cache =
#ifdef GE_XBOX
    2;
#else
    0;
#endif

#define ROC_MAX_ROOMS 1024
#define ROC_SECONDARY 512 /* fifty-first build: a room's SECONDARY (translucent) list is room + 512 */
#define ROC_MAX_TOTAL_FLOATS (4u * 1024u * 1024u) /* 16 MB of cached vertex floats, all rooms */

struct RocDraw {
    struct DXluState state;
    struct TextureCacheKey tex_key[2];
    bool tex_key_valid[2];
    uint32_t tex_serial[2]; /* TextureCacheValue::serial of the node bound at capture (forty-ninth build:
                             * the game deletes a texture by address when its pool slot is reused, the
                             * next import of the same key gets the SAME texture_id back from the free
                             * list - the forty-eighth run's railing wearing a guard's sunglasses) */
    TextureCacheNode* tex_node[2]; /* resolved per replay by the validation pass (twenty-fifth build) */
    int gpu_desc;           /* mode 2: backend draw descriptor, -1 = none */
    uint32_t gpu_first;     /* mode 2: first vertex in the room's buffer */
    const Gfx* first_cmd;   /* parts: the G_TRI command that started this draw */
    const Gfx* last_cmd;    /* parts: its last G_TRI = the walk-replay issue point (every vertex the
                             * draw selects has been loaded, so every matrix slot has its snapshot) */
    bool lit;               /* G_LIGHTING at capture: the program lights from the normals */
    uint8_t num_lights;     /* rsp.current_num_lights at capture (incl. ambient) */
    uint8_t input_kind[8];  /* combiner inputs: see gfx_rendering_api retained_build */
    int8_t shade_slot;
    uint32_t mtx_gen;       /* (diagnostic) roc_mtx_gen at capture */
    uint8_t mtx_rel;        /* parts: matrix generation (slot) of this draw's FIRST vertex: its lights' modelview */
    uint16_t gen_mask;      /* parts: the matrix slots this draw's vertices select (bit per slot) */
    uint32_t first_float;   /* into RocRoom::floats */
    uint32_t first_model;   /* into RocRoom::model (4 floats per emitted vertex: x y z slot) */
    uint32_t num_tris;
    uint32_t stride;        /* floats per vertex */
    int fog_ofs;            /* index of the fog r float within a vertex (alpha at +3), -1 = none */
    int pixfog_ofs;         /* index of the pixfog pair, -1 = none */
    bool fog_on;            /* G_FOG in the geometry mode at capture */
    bool invert_y, z01;
    bool dec_pull;
    float dec_pull_b, dec_pull_k;
    uint32_t cull_mode;     /* rsp.geometry_mode & G_CULL_BOTH at capture */
    bool no_clip;           /* G_NO_CLIPPING_EXT at capture */
    bool blender_fog;       /* the far-clip mode 2 test at capture */
    bool xlu;               /* deferred-XLU at capture: replay ENQUEUES it to the sorter (roc_xlu_enqueue) */
};

struct RocRoom {
    const Gfx* dl = nullptr;
    int room = -1;
    int state = 0;          /* 0 free, 1 registered, 2 captured, 3 uncacheable */
    std::vector<float> floats;
    std::vector<float> model;
    std::vector<RocDraw> draws;
    std::vector<float> nrm; /* 3 floats per emitted vertex (n/127), parts */
    uint32_t tris = 0;
    int gpu_buffer = -1;    /* mode 2: backend vertex buffer, -1 = none */
    int kind = 0;           /* 0 = room (skip replay), 1 = model part (walk replay) */
    const void* vtx = nullptr; /* parts: the vertex segment base the list ran with (key) */
    uint32_t tex_gen = 0xffffffffu; /* ge_texcache_gen at which the draws' tex_node pointers were validated */
    bool pinned = false;    /* the draws' nodes carry this list's pin (TextureCacheValue::pins) */
    uint32_t del_gen = 0;   /* ge_texcache_delete_gen at that validation: a node re-imported since with NO game
                             * delete in between is the same texture back from an LRU round trip - rebind, do
                             * not recapture (fifty-fourth build: the pause screen's thrash recaptured every
                             * list every frame; the sunglasses case had a delete in between) */
    uint64_t sig = 0;       /* parts: roc_entry_sig() the capture (attempt) ran under */
    uint64_t key = 0;       /* parts: roc_part_by_key entry (base or entry-state variant) */
    bool passthrough = false; /* some triangles stay on the interpreter (deferred XLU): walk with G_VTX run */
    std::vector<const Gfx*> pass_cmds; /* the TRI commands the walk hands to the interpreter (sorted) */
};

/* the render state a part list inherits from its model's pre-commands */
static inline uint64_t roc_entry_sig(void) {
    uint64_t h = rdp.combine_mode * 0x9E3779B97F4A7C15ull;
    h ^= ((uint64_t)rdp.other_mode_l << 32) ^ (uint64_t)rdp.other_mode_h;
    h ^= (uint64_t)rsp.geometry_mode * 0xC2B2AE3D27D4EB4Full;
    return h;
}
static uint32_t roc_stat_part_sigmiss;

static RocRoom roc_rooms[ROC_MAX_ROOMS];
static std::unordered_map<const Gfx*, int> roc_by_dl;
/* MODEL PARTS (thirty-seventh build): every DLCOLLISION node's list, keyed by
 * (list, vertex segment base) so instances sharing a model share one entry
 * and per-instance vertex copies get their own. Replayed by WALKING the list
 * (state and matrix commands re-run, geometry skipped, retained draws issued
 * at their captured triangle). */
#define ROC_MAX_PARTS 2048
static RocRoom roc_parts[ROC_MAX_PARTS];
static std::unordered_map<uint64_t, int> roc_part_by_key;
static RocRoom* roc_walk = nullptr;      /* the part being walk-replayed */
static size_t roc_walk_next = 0;         /* next draw to issue */
static uint32_t roc_stat_parts_replayed, roc_stat_part_draws, roc_stat_part_misses;
static inline uint64_t roc_part_key(const void* dl, const void* vtx) {
    return ((uint64_t)(uintptr_t)dl << 20) ^ (uint64_t)(uintptr_t)vtx;
}
static RocRoom* roc_part_find(const Gfx* dl, const void* vtx) {
    auto it = roc_part_by_key.find(roc_part_key(dl, vtx));
    return it == roc_part_by_key.end() ? nullptr : &roc_parts[it->second];
}
/* roc_capturing / roc_capture_bad / roc_model_pos are defined at the top of gfx_pc.cpp */
static uint32_t roc_total_floats = 0;
static uint32_t roc_stat_replayed, roc_stat_captured, roc_stat_tris, roc_stat_fallback, roc_stat_culled;
static int roc_log_budget = 120; /* first captures / fallbacks / part captures are logged */

static uint32_t roc_stat_fb_missing, roc_stat_fb_deleted, roc_stat_fb_id; /* validation fallbacks by cause */
/* pin / unpin the nodes of a list's draws (by key: a pointer may be stale) */
static void roc_pin(RocRoom& r, bool pin) {
    if (r.pinned == pin) {
        return;
    }
    r.pinned = pin;
    for (size_t di = 0; di < r.draws.size(); di++) {
        RocDraw& d = r.draws[di];
        for (int t = 0; t < 2; t++) {
            if (!d.tex_key_valid[t]) continue;
            TextureCacheMap::iterator it = gfx_texture_cache.map.find(d.tex_key[t]);
            if (it == gfx_texture_cache.map.end() || it->second.serial != d.tex_serial[t]) continue;
            if (pin) {
                it->second.pins++;
            } else if (it->second.pins > 0) {
                it->second.pins--;
            }
        }
    }
}

static void roc_release_data(RocRoom& r) {
    roc_pin(r, false);
    if (r.gpu_buffer >= 0) {
        if (gfx_rapi->retained_destroy) gfx_rapi->retained_destroy(r.gpu_buffer);
        r.gpu_buffer = -1;
    }
    roc_total_floats = roc_total_floats >= (uint32_t)r.floats.size() ? roc_total_floats - (uint32_t)r.floats.size() : 0u;
    std::vector<float>().swap(r.floats);
    std::vector<float>().swap(r.model);
    std::vector<float>().swap(r.nrm);
    std::vector<RocDraw>().swap(r.draws);
    r.tris = 0;
}

static RocRoom* roc_part_alloc(const void* dl, const void* vtx, uint64_t key);
/* lv.c: a stage was loaded. The part table is void (memp reuses the
 * addresses the keys are made of), and the front end (stage 0x5A: folders,
 * logos, the "Classified" stamp - models that recolour their vertices in
 * place) stays on the interpreter: the forty-seventh run's red text and
 * missing insignia were its gun-DL-node lists captured once. */
static int roc_stage = -1;
extern "C" void portPartCacheStage(int stage) {
    roc_stage = stage;
    for (int i = 0; i < ROC_MAX_PARTS; i++) {
        roc_release_data(roc_parts[i]);
        roc_parts[i].dl = nullptr;
        roc_parts[i].vtx = nullptr;
        roc_parts[i].state = 0;
    }
    roc_part_by_key.clear();
    if (roc_walk != nullptr && roc_walk->kind == 1) roc_walk = nullptr;
    if (roc_capturing != nullptr && roc_capturing_kind == 1) roc_capture_bad = true;
}
/* model.c modelRenderNodeDl: a part list about to run with vertex base vtx */
extern "C" int32_t portIsDynVertexPointer(const void* p); /* dyn.c */
extern "C" void portPartCacheRegister(const void* dl, const void* vtx) {
    if (gfx_room_cache != 2 || dl == NULL || vtx == NULL || roc_stage == 0x5A) {
        return;
    }
    if (portIsDynVertexPointer(vtx)) {
        return; /* rebuilt every frame (front end highlights, glass): interpreter */
    }
    const uint64_t key = roc_part_key(dl, vtx);
    if (roc_part_by_key.find(key) != roc_part_by_key.end()) {
        return;
    }
    roc_part_alloc(dl, vtx, key);
}

/* a free part slot registered under key; NULL = table full (interpreter) */
static RocRoom* roc_part_alloc(const void* dl, const void* vtx, uint64_t key) {
    static int next_slot = 0;
    int slot = -1;
    for (int n = 0; n < ROC_MAX_PARTS; n++) {
        const int i = (next_slot + n) % ROC_MAX_PARTS;
        if (roc_parts[i].state == 0) { slot = i; break; }
    }
    if (slot < 0) {
        return nullptr;
    }
    next_slot = (slot + 1) % ROC_MAX_PARTS;
    RocRoom& r = roc_parts[slot];
    roc_release_data(r);
    r.dl = (const Gfx*)dl;
    r.vtx = vtx;
    r.room = slot;
    r.kind = 1;
    r.state = 1;
    r.key = key;
    r.sig = 0;
    roc_part_by_key[key] = slot;
    return &r;
}

/* the same list under another entry state (a chr fading out, a prop's
 * translucent variant) gets its own capture: key ^ signature */
static inline uint64_t roc_part_vkey(const void* dl, const void* vtx, uint64_t sig) {
    return roc_part_key(dl, vtx) ^ (sig * 0x9E3779B97F4A7C15ull) ^ 0x5bd1e9955bd1e995ull;
}
static RocRoom* roc_part_find_key(uint64_t key) {
    auto it = roc_part_by_key.find(key);
    return it == roc_part_by_key.end() ? nullptr : &roc_parts[it->second];
}

/* the per-instance vertex copy at vtx is being freed / replaced (propobj.c,
 * vtxstore.c): every part keyed on it is void */
extern "C" void portPartCacheForgetVertices(const void* vtx) {
    if (vtx == NULL) {
        return;
    }
    for (int i = 0; i < ROC_MAX_PARTS; i++) {
        RocRoom& r = roc_parts[i];
        if (r.state != 0 && r.vtx == vtx) {
            if (roc_capturing == &r) roc_capture_bad = true;
            if (roc_walk == &r) roc_walk = nullptr;
            roc_part_by_key.erase(r.key);
            roc_release_data(r);
            r.dl = nullptr;
            r.vtx = nullptr;
            r.state = 0;
        }
    }
}

static RocRoom* roc_find(const Gfx* dl) {
    auto it = roc_by_dl.find(dl);
    return it == roc_by_dl.end() ? nullptr : &roc_rooms[it->second];
}

extern "C" void portRoomCacheRegister(int room, const void* dl) {
    if (room < 0 || room >= ROC_MAX_ROOMS || dl == NULL) {
        return;
    }
    RocRoom& r = roc_rooms[room];
    if (r.dl == (const Gfx*)dl) {
        return;
    }
    if (r.dl != nullptr) {
        roc_by_dl.erase(r.dl);
    }
    roc_release_data(r);
    r.dl = (const Gfx*)dl;
    r.room = room;
    r.state = 1;
    roc_by_dl[r.dl] = room;
}

extern "C" void portRoomCacheForgetRoom(int room) {
    if (room < 0 || room >= ROC_MAX_ROOMS) {
        return;
    }
    if (room < ROC_SECONDARY) {
        portRoomCacheForgetRoom(room + ROC_SECONDARY); /* its translucent list goes with it */
    }
    RocRoom& r = roc_rooms[room];
    if (roc_capturing == &r) {
        roc_capture_bad = true; /* mid-capture edit: drop it at capture end */
    }
    if (r.dl != nullptr) {
        roc_by_dl.erase(r.dl);
    }
    roc_release_data(r);
    r.dl = nullptr;
    r.state = 0;
}

extern "C" void portRoomCacheClear(void) {
    for (int i = 0; i < ROC_MAX_ROOMS; i++) {
        roc_release_data(roc_rooms[i]);
        roc_rooms[i].dl = nullptr;
        roc_rooms[i].state = 0;
    }
    roc_by_dl.clear();
    for (int i = 0; i < ROC_MAX_PARTS; i++) {
        roc_release_data(roc_parts[i]);
        roc_parts[i].dl = nullptr;
        roc_parts[i].vtx = nullptr;
        roc_parts[i].state = 0;
    }
    roc_part_by_key.clear();
    roc_walk = nullptr;
    roc_total_floats = 0;
    if (roc_capturing != nullptr) {
        roc_capture_bad = true;
    }
}

/* Rollback R2 (gfx_invalidate_ranges): a snapshot restore rewrote these ranges of game memory. Every room list and model part whose
 * display list or vertex base lies in one is void. (A room whose VERTICES changed under an unchanged list is the game's own
 * portRoomCacheRoomFreed case; the restore puts the room records back too, so that state is consistent again.) */
static int roc_forget_ranges(const GfxRanges& rs) {
    int dropped = 0;
    for (int i = 0; i < ROC_MAX_ROOMS; i++) {
        RocRoom& r = roc_rooms[i];
        if (r.state != 0 && r.dl != nullptr && gfx_range_hit(rs, (uintptr_t)r.dl, (uintptr_t)r.dl + 8u)) {
            if (roc_capturing == &r) roc_capture_bad = true;
            roc_by_dl.erase(r.dl);
            roc_release_data(r);
            r.dl = nullptr;
            r.state = 0;
            dropped++;
        }
    }
    for (int i = 0; i < ROC_MAX_PARTS; i++) {
        RocRoom& r = roc_parts[i];
        if (r.state != 0 && ((r.dl != nullptr && gfx_range_hit(rs, (uintptr_t)r.dl, (uintptr_t)r.dl + 8u)) ||
                             (r.vtx != nullptr && gfx_range_hit(rs, (uintptr_t)r.vtx, (uintptr_t)r.vtx + 16u)))) {
            if (roc_capturing == &r) roc_capture_bad = true;
            if (roc_walk == &r) roc_walk = nullptr;
            roc_part_by_key.erase(r.key);
            roc_release_data(r);
            r.dl = nullptr;
            r.vtx = nullptr;
            r.state = 0;
            dropped++;
        }
    }
    return dropped;
}

static uint32_t roc_stat_foreign_slots;
static uint32_t roc_stat_rebinds; /* draw textures re-bound after an LRU round trip (no recapture) */
static void roc_capture_begin(RocRoom& r) {
    roc_capturing = &r;
    roc_capturing_kind = r.kind;
    roc_capture_bad = false;
    roc_capture_bad_why = "matrix load / rect / deferred-XLU inside its list";
    roc_capture_gen0 = roc_mtx_gen;
    r.tex_gen = 0xffffffffu;
    r.del_gen = ge_texcache_delete_gen;
    r.passthrough = false;
    r.pass_cmds.clear();
    memset(roc_slot_loaded, 0, sizeof(roc_slot_loaded));
    roc_release_data(r);
}

/* forty-fifth build: a deferred-XLU triangle in a captured list no longer
 * refuses the list - its TRI command is handed to the interpreter by the
 * walk (which then runs the list's G_VTX and texture chains too) */
static void roc_capture_passthrough(const Gfx* cmd) {
    RocRoom& r = *roc_capturing;
    r.passthrough = true;
    if (r.pass_cmds.empty() || r.pass_cmds.back() != cmd) {
        r.pass_cmds.push_back(cmd);
    }
}

static void roc_apply_state(const RocDraw& d); /* defined below, next to the CPU replay */

/* Mode 2: hand the captured draws to the backend as one object-space vertex
 * buffer + a descriptor per draw, then drop the CPU copies. false = keep the
 * room on the interpreter. */
static bool roc_gpu_build(RocRoom& r) {
    if (gfx_rapi->retained_create == NULL || gfx_rapi->retained_build == NULL) {
        return false;
    }
    uint32_t total = 0;
    for (size_t di = 0; di < r.draws.size(); di++) {
        const RocDraw& d = r.draws[di];
        if (d.invert_y) {
            return false; /* per-triangle CPU math the program does not do (the decal pull it does: c[78]) */
        }
        total += d.num_tris * 3u;
    }
    if (total == 0) {
        return false;
    }
    const int buf = gfx_rapi->retained_create(total);
    if (buf < 0) {
        return false;
    }
    uint32_t first = 0;
    for (size_t di = 0; di < r.draws.size(); di++) {
        RocDraw& d = r.draws[di];
        d.gpu_desc = gfx_rapi->retained_build(buf, first, d.state.prg, r.floats.data() + d.first_float, d.stride,
                                               d.num_tris, r.model.data() + d.first_model,
                                               d.lit ? r.nrm.data() + d.first_model : NULL,
                                               d.state.tex_id, d.state.tex_used, d.input_kind, d.shade_slot);
        if (d.gpu_desc < 0) {
            gfx_rapi->retained_destroy(buf);
            return false;
        }
        d.gpu_first = first;
        first += d.num_tris * 3u;
    }
    r.gpu_buffer = buf;
    bool has_xlu = false;
    for (size_t di = 0; di < r.draws.size(); di++) {
        if (r.draws[di].xlu) has_xlu = true;
    }
    /* the float stream and model positions now live in the buffer (the
     * model positions stay when the sorter needs them for XLU keys) */
    roc_total_floats = roc_total_floats >= (uint32_t)r.floats.size() ? roc_total_floats - (uint32_t)r.floats.size() : 0u;
    std::vector<float>().swap(r.floats);
    if (!has_xlu) std::vector<float>().swap(r.model);
    std::vector<float>().swap(r.nrm);
    return true;
}

/* one retained draw with the frame's constants: matrix, fixups, fog, lights
 * (calculate_normal_dir per light, as gfx_sp_vertex would on its next lit
 * vertex), prim / env colours */
extern "C" uint32_t gfx_xbox_gpu_lit = 1; /* [display] xbox_gpu_lit: 0 = lit parts use their captured colours (bisect) */
static uint16_t roc_walk_gen_uploaded; /* mats[] slots already holding this walk's snapshots */

/* the MP matrix a retained vertex of slot `slot` was captured against, at
 * this replay: the walk's snapshot (parts) or the current one (rooms) */
static const float (*roc_xlu_slot_mp(const RocRoom& r, int slot))[4] {
    if (roc_walk == &r && slot >= 0 && slot < ROC_WALK_GENS && roc_walk_gen_valid[slot]) {
        return roc_walk_mp[slot];
    }
    return rsp.MP_matrix;
}

/* fiftieth build: a retained draw's XLU triangles go to the deferred sorter
 * as retained-buffer triangles - keys and corners from the object-space
 * positions (kept in r.model) through the slot's MP, exactly the numbers
 * gfx_sp_tri1's capture derives from the interpreter's clip vertices.
 * false = enqueue impossible this frame (sorter closed / full): draw now. */
static bool roc_xlu_enqueue(RocRoom& r, const RocDraw& d, const float mvp[16], const float fx[4], const float fogp[4],
                            const float fogsel[4], const float fog_rgb[3], uint32_t cull_mode, const float* lights,
                            const float prim_env[12], const float decal[4]) {
    if (!gfx_xbox_xlu_retained || !gfx_dxlu_enabled() || dxlu_closed || r.model.empty() || d.gpu_desc < 0) {
        return false;
    }
    if (dxlu_num_ret_units >= DXLU_MAX_RET_UNITS || dxlu_num_tris + d.num_tris > DXLU_MAX_TRIS
        || dxlu_num_states >= DXLU_MAX_STATES) {
        return false;
    }
    if (d.first_model + d.num_tris * 12u > (uint32_t)r.model.size()) {
        return false;
    }
    /* the sorter's state record: the draw's, with the frame's viewport / scissor */
    struct DXluState s = d.state;
    s.viewport = rdp.viewport;
    s.scissor = rdp.scissor;
    for (int t = 0; t < 2; t++) {
        if (s.tex_used[t] && d.tex_node[t] != nullptr) {
            s.tex_id[t] = d.tex_node[t]->second.texture_id;
        }
    }
    uint32_t si;
    if (dxlu_num_states > 0 && memcmp(&dxlu_states[dxlu_num_states - 1], &s, sizeof(s)) == 0) {
        si = dxlu_num_states - 1;
    } else {
        si = dxlu_num_states++;
        dxlu_states[si] = s;
    }
    const int32_t unit = (int32_t)dxlu_num_ret_units++;
    struct DXluRetUnit& u = dxlu_ret_units[unit];
    u.desc = d.gpu_desc;
    memcpy(u.mvp, mvp, sizeof(u.mvp));
    memcpy(u.fx, fx, sizeof(u.fx));
    memcpy(u.fogp, fogp, sizeof(u.fogp));
    memcpy(u.fogsel, fogsel, sizeof(u.fogsel));
    memcpy(u.fog_rgb, fog_rgb, sizeof(u.fog_rgb));
    memcpy(u.decal, decal, sizeof(u.decal));
    u.has_lights = lights != NULL;
    if (lights != NULL) memcpy(u.lights, lights, sizeof(u.lights));
    memcpy(u.prim_env, prim_env, sizeof(u.prim_env));
    u.cull_mode = cull_mode;
    u.nmats = 0;
    if (roc_walk == &r) {
        for (int g = 1; g < ROC_WALK_GENS; g++) {
            if ((d.gen_mask & (1u << g)) && roc_walk_gen_valid[g]) {
                u.mat_slot[u.nmats] = (uint8_t)g;
                memcpy(u.mats[u.nmats], &roc_walk_mp[g][0][0], sizeof(u.mats[0]));
                u.nmats++;
            }
        }
    }
    const float* mp = r.model.data() + d.first_model;
    const uint32_t first_tri = dxlu_num_tris;
    for (uint32_t k = 0; k < d.num_tris; k++) {
        struct DXluTri* dt = &dxlu_tris[dxlu_num_tris];
        bool eye_ok = true;
        float w[3];
        dt->bb[0] = dt->bb[2] = 1e30f;
        dt->bb[1] = dt->bb[3] = -1e30f;
        bool bb_inf = false;
        for (int j = 0; j < 3; j++) {
            const float* ob = mp + (k * 3 + j) * 4;
            const float (*M)[4] = roc_xlu_slot_mp(r, (int)ob[3]);
            float clip[4];
            for (int c = 0; c < 4; c++) {
                clip[c] = ob[0] * M[0][c] + ob[1] * M[1][c] + ob[2] * M[2][c] + M[3][c];
            }
            w[j] = gfx_dxlu_vertex_dist(clip, dt->e + 3 * j, &eye_ok);
            if (clip[3] <= 1e-6f) {
                bb_inf = true;
            } else if (!bb_inf) {
                const float sx = clip[0] / clip[3], sy = clip[1] / clip[3];
                if (sx < dt->bb[0]) dt->bb[0] = sx;
                if (sx > dt->bb[1]) dt->bb[1] = sx;
                if (sy < dt->bb[2]) dt->bb[2] = sy;
                if (sy > dt->bb[3]) dt->bb[3] = sy;
            }
        }
        if (bb_inf) {
            dt->bb[0] = dt->bb[2] = -1e30f;
            dt->bb[1] = dt->bb[3] = 1e30f;
        }
        float key = w[0] > w[1] ? w[0] : w[1];
        if (w[2] > key) key = w[2];
        dt->key = key;
        dt->gkey = (d.state.zmode == ZMODE_DEC && gfx_dxlu_pin_enabled()) ? DXLU_KEY_PINNED : key;
        dt->maxd = key;
        dt->mind = w[0] < w[1] ? w[0] : w[1];
        if (w[2] < dt->mind) dt->mind = w[2];
        dt->eye_ok = eye_ok ? 1 : 0;
        dt->grp = 0;
        dt->gmode = 0;
        dt->scope = dxlu_scope_cur;
        dt->state = si;
        dt->offset = 0;
        dt->nfloats = 0;
        dt->ret = unit;
        dt->ret_first = d.gpu_first + k * 3u;
        dxlu_num_tris++;
        roc_stat_tri_xluret++;
    }
    /* fifty-second build: COPLANAR RUNS AS UNITS. Consecutive triangles of
     * this draw that lie on one plane (a grating panel's strip of quads)
     * become one sorter unit - mode 7, submission order inside, key = the
     * farthest member (the room-unit rule that held) - instead of one unit
     * each: ~690 per-triangle units a frame drove the sorter's 8%. Never a
     * whole draw, never across a plane change (the Dam railings were one
     * list of railings at many depths). Inside an authored group the
     * group's close stamps every triangle anyway, so nothing is done. */
    if (!dxlu_group_open && gfx_xbox_xlu_planar_units) {
        uint32_t i = first_tri;
        while (i < dxlu_num_tris) {
            const struct DXluTri* a = &dxlu_tris[i];
            uint32_t j = i + 1;
            if (a->eye_ok) {
                const float* e = a->e;
                const float ax = e[3] - e[0], ay = e[4] - e[1], az = e[5] - e[2];
                const float bx = e[6] - e[0], by = e[7] - e[1], bz = e[8] - e[2];
                float n[3] = { ay * bz - az * by, az * bx - ax * bz, ax * by - ay * bx };
                const float len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                if (len > 1e-6f) {
                    n[0] /= len; n[1] /= len; n[2] /= len;
                    const float pd = -(n[0] * e[0] + n[1] * e[1] + n[2] * e[2]);
                    if (fabsf(pd) >= DXLU_PLANE_EPS) {
                        for (; j < dxlu_num_tris; j++) {
                            const struct DXluTri* b = &dxlu_tris[j];
                            if (!b->eye_ok) break;
                            bool on = true;
                            for (int v = 0; v < 3 && on; v++) {
                                const float sd = n[0] * b->e[v * 3] + n[1] * b->e[v * 3 + 1] + n[2] * b->e[v * 3 + 2] + pd;
                                if (fabsf(sd) > DXLU_PLANE_EPS) on = false;
                            }
                            if (!on) break;
                        }
                    }
                }
            }
            if (j - i >= 2) {
                float gkey = 0.0f;
                for (uint32_t t = i; t < j; t++) {
                    if (dxlu_tris[t].key > gkey) gkey = dxlu_tris[t].key;
                }
                if (d.state.zmode == ZMODE_DEC && gfx_dxlu_pin_enabled()) gkey = DXLU_KEY_PINNED;
                dxlu_group_count++;
                for (uint32_t t = i; t < j; t++) {
                    dxlu_tris[t].gkey = gkey;
                    dxlu_tris[t].grp = dxlu_group_count;
                    dxlu_tris[t].gmode = 7;
                }
                roc_stat_xlu_planar_units++;
            }
            i = j;
        }
    }
    return true;
}

/* the sorted pass: one indexed draw of a retained unit's triangles */
static void roc_xlu_replay_unit(int32_t unit, const uint16_t* idx, uint32_t n) {
    if (unit < 0 || (uint32_t)unit >= dxlu_num_ret_units || gfx_rapi->retained_draw_indexed == NULL) {
        return;
    }
    gfx_flush(); /* float triangles pending before this run draw first */
    const struct DXluRetUnit& u = dxlu_ret_units[unit];
    if (gfx_rapi->retained_matrices != NULL) {
        for (int m = 0; m < u.nmats; m++) {
            gfx_rapi->retained_matrices(u.mat_slot[m], 1, u.mats[m]);
        }
    }
    gfx_rapi->retained_draw_indexed(u.desc, idx, n, u.mvp, u.fx, u.fogp, u.fogsel, u.fog_rgb, u.cull_mode,
                                    u.has_lights ? u.lights : NULL, u.prim_env, u.decal);
    roc_stat_tris += n / 3u;
    roc_stat_draw_origin[8]++; /* an XLU replay draw, like the float path's flushes */
}

static void roc_issue_gpu_draw(RocRoom& r, const RocDraw& d) {
    (void)r;
    if (!d.xlu) {
        roc_apply_state(d);
    }
    if (roc_walk == &r && gfx_rapi->retained_matrices != NULL) {
        /* slot 0 is the mvp below; the other slots this draw's vertices
         * select get their snapshot once per walk */
        uint16_t need = (uint16_t)(d.gen_mask & ~roc_walk_gen_uploaded & ~1u);
        for (int g = 1; g < ROC_WALK_GENS && need != 0; g++) {
            if ((need & (1u << g)) && roc_walk_gen_valid[g]) {
                gfx_rapi->retained_matrices(g, 1, &roc_walk_mp[g][0][0]);
                roc_walk_gen_uploaded |= (uint16_t)(1u << g);
            }
            need &= (uint16_t)~(1u << g);
        }
    }
    const bool walking = roc_walk == &r;
    const float (*MP)[4] = (walking && roc_walk_gen_valid[0]) ? roc_walk_mp[0] : rsp.MP_matrix;
    const bool snap = walking && d.mtx_rel < ROC_WALK_GENS && roc_walk_gen_valid[d.mtx_rel];
    const float (*MV)[4] = snap ? roc_walk_mv[d.mtx_rel] : rsp.modelview_matrix_stack[rsp.modelview_matrix_stack_size - 1];
    float mvp[16];
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            mvp[i * 4 + j] = MP[i][j];
        }
    }
    float fx[4] = { 1.0f, 0.0f, d.z01 ? 0.5f : 1.0f, d.z01 ? 0.5f : 0.0f };
    if (!fbActive) {
        const float k = gfx_current_dimensions.aspect_ratio != 0.0f
            ? rsp.aspect_scale / gfx_current_dimensions.aspect_ratio : 1.0f;
        fx[0] = k;
        fx[1] = rsp.aspect_ofs * k;
    }
    const float fogp[4] = { (float)rsp.fog_mul / 255.0f, (float)rsp.fog_offset / 255.0f, 0.001f,
                            rdp.fog_color.a / 255.0f };
    const float fog_rgb[3] = { rdp.fog_color.r / 255.0f, rdp.fog_color.g / 255.0f, rdp.fog_color.b / 255.0f };
    static const float sel_gpu[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
    static const float sel_vertex[4] = { 0.0f, 0.0f, 1.0f, 0.0f };
    static const float sel_flat[4] = { 0.0f, 1.0f, 0.0f, 0.0f };
    /* no G_FOG: gfx_sp_vertex gives the vertex rdp.fog_color.a as loaded. A
     * room's vertices keep their captured value (the thirty-sixth run); a
     * part's fog colour is set per model per frame right before its lists,
     * so the frame's value (fogp.w) is the one its G_VTX would have taken. */
    const float* fogsel = d.fog_on ? sel_gpu : (r.kind == 1 ? sel_flat : sel_vertex);
    float lights[20];
    const float* lp = NULL;
    if (d.lit && gfx_xbox_gpu_lit && rsp.current_num_lights >= 1) {
        memset(lights, 0, sizeof(lights));
        const Light_t& amb = rsp.current_lights[rsp.current_num_lights - 1];
        lights[0] = amb.col[0] / 255.0f; lights[1] = amb.col[1] / 255.0f; lights[2] = amb.col[2] / 255.0f;
        const int n = rsp.current_num_lights - 1 < 2 ? rsp.current_num_lights - 1 : 2;
        for (int i = 0; i < n; i++) {
            float coeff[3];
            { /* calculate_normal_dir against the modelview of the draw's vertices */
                const Light_t* light = &rsp.current_lights[i];
                const float light_dir[3] = { light->dir[0] / 127.f, light->dir[1] / 127.f, light->dir[2] / 127.f };
                gfx_transposed_matrix_mul(coeff, light_dir, MV);
                gfx_normalize_vector(coeff);
            }
            lights[4 + i * 8 + 0] = coeff[0]; lights[4 + i * 8 + 1] = coeff[1]; lights[4 + i * 8 + 2] = coeff[2];
            lights[8 + i * 8 + 0] = rsp.current_lights[i].col[0] / 255.0f;
            lights[8 + i * 8 + 1] = rsp.current_lights[i].col[1] / 255.0f;
            lights[8 + i * 8 + 2] = rsp.current_lights[i].col[2] / 255.0f;
        }
        lp = lights;
    }
    const float prim_env[12] = {
        rdp.prim_color.r / 255.0f, rdp.prim_color.g / 255.0f, rdp.prim_color.b / 255.0f, rdp.prim_color.a / 255.0f,
        rdp.env_color.r / 255.0f, rdp.env_color.g / 255.0f, rdp.env_color.b / 255.0f, rdp.env_color.a / 255.0f,
        rdp.prim_lod_fraction / 255.0f, 0.0f, 0.0f, 0.0f
    };
    const float decal[4] = { d.dec_pull ? d.dec_pull_b * d.dec_pull_k : 0.0f, d.dec_pull ? d.dec_pull_k : 0.0f,
                             d.dec_pull ? 4.0f * d.dec_pull_k : 0.0f, 0.0f };
    if (d.xlu) {
        if (roc_xlu_enqueue(r, d, mvp, fx, fogp, fogsel, fog_rgb, d.cull_mode, lp, prim_env, decal)) {
            return;
        }
        roc_apply_state(d); /* the sorter cannot take it this frame: drawn here, in list order */
    }
    gfx_rapi->retained_draw(d.gpu_desc, mvp, fx, fogp, fogsel, fog_rgb, d.cull_mode, lp, prim_env, decal);
    roc_stat_tris += d.num_tris;
}

/* walk replay of a part list: validate its textures, then let gfx_run_dl run
 * the list with roc_walk set (roc_walk_tri issues the draws) */
/* a validated draw texture: the node for the captured key, if it is still the
 * capture's texture. found + same serial = live; found + new serial with no
 * game delete since the list's last validation = the same texture re-imported
 * after an LRU eviction (rebind); anything else = not this texture. */
static bool roc_validate_draw_tex(RocRoom& r, RocDraw& d, int t) {
    d.tex_node[t] = nullptr;
    if (!d.tex_key_valid[t]) {
        return true;
    }
    TextureCacheMap::iterator it = gfx_texture_cache.map.find(d.tex_key[t]);
    if (it == gfx_texture_cache.map.end()) {
        roc_stat_fb_missing++;
        return false;
    }
    if (it->second.serial != d.tex_serial[t]) {
        if (r.del_gen != ge_texcache_delete_gen) {
            roc_stat_fb_deleted++;
            return false; /* a game delete happened: the address may hold another texture now */
        }
        d.tex_serial[t] = it->second.serial;
        d.state.tex_id[t] = it->second.texture_id;
        roc_stat_rebinds++;
    } else if (it->second.texture_id != d.state.tex_id[t]) {
        roc_stat_fb_id++;
        return false;
    }
    d.tex_node[t] = &*it;
    gfx_texture_cache.lru.splice(gfx_texture_cache.lru.end(), gfx_texture_cache.lru, it->second.lru_location);
    return true;
}

/* No cache node was erased since the draws' tex_node pointers were
 * validated (ge_texcache_gen): they are live - touch their LRU slots and
 * skip the hash finds (forty-third build: 1.2% of the frame). */
static bool roc_textures_still_live(RocRoom& r) {
    if (r.tex_gen != ge_texcache_gen) {
        return false;
    }
    for (size_t di = 0; di < r.draws.size(); di++) {
        RocDraw& d = r.draws[di];
        for (int t = 0; t < 2; t++) {
            if (d.tex_node[t] != nullptr) {
                gfx_texture_cache.lru.splice(gfx_texture_cache.lru.end(), gfx_texture_cache.lru, d.tex_node[t]->second.lru_location);
            }
        }
    }
    return true;
}

static bool roc_walk_begin(RocRoom& r) {
    if (!roc_textures_still_live(r)) {
        for (size_t di = 0; di < r.draws.size(); di++) {
            RocDraw& d = r.draws[di];
            for (int t = 0; t < 2; t++) {
                if (!roc_validate_draw_tex(r, d, t)) {
                    roc_release_data(r);
                    r.state = 1;
                    roc_stat_fallback++;
                    return false;
                }
            }
        }
        r.tex_gen = ge_texcache_gen;
        r.del_gen = ge_texcache_delete_gen;
        r.pinned = false; /* the pins moved with the nodes: pin the current set */
        roc_pin(r, true);
    }
    roc_walk = &r;
    roc_walk_next = 0;
    roc_walk_gen0 = roc_mtx_gen;
    roc_walk_gen_uploaded = 0;
    memset(roc_walk_gen_valid, 0, sizeof(roc_walk_gen_valid));
    return true;
}

/* true = the command is the walk's (a captured draw issued here, or skipped);
 * false = a passthrough command the interpreter must run */
static bool roc_walk_tri(const Gfx* cmd) {
    RocRoom& r = *roc_walk;
    if (r.passthrough && std::binary_search(r.pass_cmds.begin(), r.pass_cmds.end(), cmd)) {
        return false;
    }
    if (roc_walk_next < r.draws.size() && r.draws[roc_walk_next].last_cmd == cmd) {
        roc_issue_gpu_draw(r, r.draws[roc_walk_next]);
        roc_walk_next++;
        roc_stat_part_draws++;
    }
    return true;
}

static void roc_walk_end(RocRoom& r) {
    if (roc_walk == &r && roc_walk_next != r.draws.size()) {
        roc_stat_part_misses++; /* the list branched differently from its capture */
        /* GE_ROCPARTDBG=1 (diagnostic): name the part, at most once a second each */
        static int dbg = -1;
        static std::unordered_map<uint64_t, std::chrono::steady_clock::time_point> last_logged;
        if (dbg < 0) {
            const char* e = getenv("GE_ROCPARTDBG");
            dbg = (e != NULL && e[0] == '1') ? 1 : 0;
        }
        if (dbg) {
            const auto now = std::chrono::steady_clock::now();
            auto it = last_logged.find(r.key);
            if (it == last_logged.end() || now - it->second >= std::chrono::seconds(1)) {
                last_logged[r.key] = now;
                const RocDraw& nd = r.draws[roc_walk_next];
                fprintf(stderr, "ge: roc: part MISS dl=%p vtx=%p issued %u of %u draws (next waits for cmd %p, %u tris, xlu %d)\n",
                        (const void*)r.dl, r.vtx, (unsigned)roc_walk_next, (unsigned)r.draws.size(),
                        (const void*)nd.last_cmd, nd.num_tris, nd.xlu ? 1 : 0);
            }
        }
    }
    roc_walk = nullptr;
    roc_stat_parts_replayed++;
    ge_state_serial++;
    rdp.textures_changed[0] = rdp.textures_changed[1] = true;
    rdp.viewport_or_scissor_changed = true;
}

/* Mode 2 replay: state per draw, constants per room, one draw call per draw. */
static bool roc_replay_gpu(RocRoom& r) {
    if (!roc_textures_still_live(r)) {
        for (size_t di = 0; di < r.draws.size(); di++) {
            RocDraw& d = r.draws[di];
            for (int t = 0; t < 2; t++) {
                if (!roc_validate_draw_tex(r, d, t)) {
                    roc_release_data(r);
                    r.state = 1; /* recapture on the next visit */
                    roc_stat_fallback++;
                    if (roc_log_budget > 0) {
                        roc_log_budget--;
                        fprintf(stderr, "ge: roc: room %d texture evicted - recapturing\n", r.room);
                    }
                    return false;
                }
            }
        }
        r.tex_gen = ge_texcache_gen;
        r.del_gen = ge_texcache_delete_gen;
        r.pinned = false;
        roc_pin(r, true);
    }
    gfx_flush(); /* the interpreter's pending batch draws before the room */

    float mvp[16];
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            mvp[i * 4 + j] = rsp.MP_matrix[i][j];
        }
    }
    /* gfx_adjust_x_for_aspect_ratio in NDC: x' = (ofs*w + x)*k -> x/w*k + ofs*k;
     * the 0..1 depth remap (z+w)/2 -> z/w*0.5 + 0.5 */
    float fixup[4] = { 1.0f, 0.0f, 0.5f, 0.5f };
    if (!fbActive) {
        const float k = gfx_current_dimensions.aspect_ratio != 0.0f
            ? rsp.aspect_scale / gfx_current_dimensions.aspect_ratio : 1.0f;
        fixup[0] = k;
        fixup[1] = rsp.aspect_ofs * k;
    }
    const float fogp[4] = { (float)rsp.fog_mul / 255.0f, (float)rsp.fog_offset / 255.0f, 0.001f,
                            rdp.fog_color.a / 255.0f };
    const float fog_rgb[3] = { rdp.fog_color.r / 255.0f, rdp.fog_color.g / 255.0f, rdp.fog_color.b / 255.0f };
    static const float sel_gpu[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
    static const float sel_flat[4] = { 0.0f, 1.0f, 0.0f, 0.0f };
    static const float sel_vertex[4] = { 0.0f, 0.0f, 1.0f, 0.0f };

    for (size_t di = 0; di < r.draws.size(); di++) {
        const RocDraw& d = r.draws[di];
        if (d.lit || d.xlu) { /* lit: the shared issue path handles lights; XLU: it enqueues to the sorter */
            roc_issue_gpu_draw(r, d);
            continue;
        }
        roc_apply_state(d);
        /* G_FOG draws: the program's fog from z/w. Draws with blender fog
         * but no G_FOG geometry (the player's room in Cradle): the
         * interpreter uses whatever per-vertex fog the vertex load left,
         * and that value is what the capture stored in the vertex's
         * specular alpha - so the vertex value, NOT the flat fog alpha
         * (0xff in every fogSetRenderFogColor = fully fogged, the blue
         * catwalk of the thirty-fourth run). */
        const float* fogsel = d.fog_on ? sel_gpu : sel_vertex;
        (void)sel_flat;
        const bool z01 = d.z01;
        float fx[4] = { fixup[0], fixup[1], z01 ? 0.5f : 1.0f, z01 ? 0.5f : 0.0f };
        const float decal[4] = { d.dec_pull ? d.dec_pull_b * d.dec_pull_k : 0.0f, d.dec_pull ? d.dec_pull_k : 0.0f,
                                 d.dec_pull ? 4.0f * d.dec_pull_k : 0.0f, 0.0f };
        gfx_rapi->retained_draw(d.gpu_desc, mvp, fx, fogp, fogsel, fog_rgb, d.cull_mode, NULL, NULL, decal);
        roc_stat_tris += d.num_tris;
    }
    /* as roc_replay: the interpreter re-derives its state on the next triangle */
    ge_state_serial++;
    rdp.textures_changed[0] = rdp.textures_changed[1] = true;
    rdp.viewport_or_scissor_changed = true;
    roc_stat_replayed++;
    return true;
}

static void roc_capture_end(RocRoom& r) {
    roc_capturing = nullptr;
    { /* GE_ROCLOGALL=1 (diagnostic): no budget on the capture log */
        static int all = -1;
        if (all < 0) {
            const char* e = getenv("GE_ROCLOGALL");
            all = (e != NULL && e[0] == '1') ? 1 : 0;
            if (all) roc_log_budget = 1 << 30;
        }
    }
    if (roc_capture_bad) {
        roc_release_data(r);
        r.state = 3;
        if (roc_log_budget > 0 || r.kind == 0) { /* every room, once */
            if (roc_log_budget > 0) roc_log_budget--;
            fprintf(stderr, "ge: roc: %s %d UNCACHEABLE (%s)\n", r.kind == 1 ? "part" : "room", r.room, roc_capture_bad_why);
        }
        return;
    }
    if (r.draws.empty()) {
        roc_release_data(r);
        r.state = 1;
        return;
    }
    std::sort(r.pass_cmds.begin(), r.pass_cmds.end());
    if (roc_total_floats + (uint32_t)r.floats.size() > ROC_MAX_TOTAL_FLOATS) {
        static bool warned;
        if (!warned) {
            warned = true;
            fprintf(stderr, "ge: roc: cache full (%u floats), room %d and later stay on the interpreter\n",
                    roc_total_floats, r.room);
        }
        roc_release_data(r);
        r.state = 1;
        return;
    }
    roc_total_floats += (uint32_t)r.floats.size();
    r.state = 2;
    roc_stat_captured++;
    if (roc_log_budget > 0) {
        roc_log_budget--;
        fprintf(stderr, "ge: roc: room %d captured: %u draws, %u tris, %u KB (total %u KB)\n", r.room,
                (unsigned)r.draws.size(), r.tris, (unsigned)(r.floats.size() * 4 / 1024),
                roc_total_floats * 4 / 1024);
    }
    if (r.kind == 1 && roc_log_budget > 0) {
        roc_log_budget--;
        fprintf(stderr, "ge: roc: part dl=%p vtx=%p draws=%u tris=%u\n", (const void*)r.dl, r.vtx,
                (unsigned)r.draws.size(), r.tris);
        for (size_t di = 0; di < r.draws.size() && di < 6; di++) {
            const RocDraw& d = r.draws[di];
            float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
            for (uint32_t k = 0; k < d.num_tris * 3u; k++) {
                for (int c = 0; c < 3; c++) {
                    const float f = r.model[d.first_model + k * 4 + c];
                    if (f < lo[c]) lo[c] = f;
                    if (f > hi[c]) hi[c] = f;
                }
            }
            fprintf(stderr, "ge: roc:   draw %u: tris %u stride %u lit %d nl %u mtx %u rel %u gens %x tex0 used %d id %u tex1 used %d id %u shade %d kinds %d%d%d%d fog %d ofs %d pos [%.0f..%.0f %.0f..%.0f %.0f..%.0f]\n",
                    (unsigned)di, d.num_tris, d.stride, d.lit ? 1 : 0, d.num_lights, d.mtx_gen, d.mtx_rel, d.gen_mask,
                    d.state.tex_used[0] ? 1 : 0, d.state.tex_id[0], d.state.tex_used[1] ? 1 : 0, d.state.tex_id[1],
                    d.shade_slot, d.input_kind[0], d.input_kind[1], d.input_kind[2], d.input_kind[3],
                    d.fog_on ? 1 : 0, d.fog_ofs, lo[0], hi[0], lo[1], hi[1], lo[2], hi[2]);
        }
    }
    if (gfx_room_cache == 2) {
        if (!roc_gpu_build(r)) {
            roc_release_data(r);
            r.state = 3; /* the interpreter draws it from now on */
            if (roc_log_budget > 0 || r.kind == 0) {
                if (roc_log_budget > 0) roc_log_budget--;
                fprintf(stderr, "ge: roc: %s %d not GPU-retainable (inverted y / backend) - interpreter\n", r.kind == 1 ? "part" : "room", r.room);
            }
        }
    }
}

/* Called from gfx_sp_tri1 after the vertex emission loop wrote this
 * triangle's floats at buf_vbo[vstart..buf_vbo_len). */
static void roc_record_tri(struct ShaderProgram* prg, const ColorCombiner* comb, uint8_t num_inputs, bool use_alpha, bool use_modulate,
                           bool depth_test, bool depth_update, bool depth_compare, bool depth_source_prim,
                           uint16_t zmode, uint8_t depth_mode, const bool used_textures[2],
                           uint32_t vstart, int fog_ofs, int pixfog_ofs, bool invert_y, bool z01,
                           bool dec_pull, float dec_pull_b, float dec_pull_k,
                           struct LoadedVertex* const v_arr[3], bool xlu) {
    RocRoom& r = *roc_capturing;
    const uint32_t nf = buf_vbo_len - vstart;
    const uint32_t stride = nf / 3;
    if (nf == 0 || nf % 3 != 0) {
        roc_capture_bad = true;
        return;
    }
    if (rsp.geometry_mode & G_TEXTURE_GEN) {
        roc_capture_bad = true; /* view-dependent texcoords: interpreter */
        return;
    }
    const bool lit = (rsp.geometry_mode & G_LIGHTING) != 0;
    /* each vertex selects its own matrix (the generation of its G_VTX) in
     * the program, so a joint triangle spanning two limbs is fine; a list
     * with more matrix loads than mats[] slots goes to the interpreter */
    uint8_t mtx_rel = 0;
    uint16_t gen_mask = 0;
    for (int i = 0; i < 3; i++) {
        const size_t slot = (size_t)(v_arr[i] - rsp.loaded_vertices);
        if (slot < MAX_VERTICES) {
            const uint8_t g = roc_slot_gen[slot];
            if (g >= ROC_WALK_GENS) {
                roc_capture_bad = true;
                roc_capture_bad_why = "more than 16 matrix loads";
                return;
            }
            if (i == 0) {
                mtx_rel = g;
            }
            gen_mask |= (uint16_t)(1u << g);
        }
    }
    struct DXluState s;
    memset(&s, 0, sizeof(s));
    s.prg = prg;
    s.use_alpha = use_alpha;
    s.use_modulate = use_modulate;
    s.depth_test = depth_test;
    s.depth_update = depth_update;
    s.depth_compare = depth_compare;
    s.depth_source_prim = depth_source_prim;
    s.zmode = zmode;
    s.depth_mode_enc = depth_mode;
    /* viewport/scissor are entry state: the replay uses the frame's own */
    for (int t = 0; t < 2; t++) {
        if (used_textures[t] && rendering_state.textures[t] != NULL) {
            s.tex_used[t] = true;
            s.tex_id[t] = rendering_state.textures[t]->second.texture_id;
            s.tex_linear[t] = rendering_state.textures[t]->second.linear_filter;
            s.tex_mip[t] = rendering_state.textures[t]->second.mip_filter;
            s.tex_cms[t] = rendering_state.textures[t]->second.cms;
            s.tex_cmt[t] = rendering_state.textures[t]->second.cmt;
        }
    }
    const uint32_t cull_mode = rsp.geometry_mode & G_CULL_BOTH;
    const bool fog_on = (rsp.geometry_mode & G_FOG) != 0;
    const bool no_clip = (rsp.extra_geometry_mode & G_NO_CLIPPING_EXT) != 0;
    const bool blender_fog = ((rdp.other_mode_l >> 30) == G_BL_CLR_FOG) || ((rdp.other_mode_l >> 26) == G_BL_A_FOG);

    RocDraw* d = r.draws.empty() ? nullptr : &r.draws.back();
    const bool same = d != nullptr && memcmp(&d->state, &s, sizeof(s)) == 0 && d->stride == stride
        && d->fog_ofs == fog_ofs && d->pixfog_ofs == pixfog_ofs && d->fog_on == fog_on
        && d->invert_y == invert_y && d->z01 == z01 && d->dec_pull == dec_pull
        && d->dec_pull_b == dec_pull_b && d->dec_pull_k == dec_pull_k && d->cull_mode == cull_mode
        && d->no_clip == no_clip && d->blender_fog == blender_fog && d->lit == lit && d->xlu == xlu
        && d->num_lights == (uint8_t)rsp.current_num_lights;
    if (!same) {
        RocDraw nd;
        memset(&nd, 0, sizeof(nd));
        nd.state = s;
        nd.gpu_desc = -1;
        nd.first_cmd = roc_cur_cmd;
        nd.last_cmd = roc_cur_cmd;
        nd.mtx_gen = roc_mtx_gen;
        nd.mtx_rel = mtx_rel;
        nd.gen_mask = 0;
        nd.lit = lit;
        nd.num_lights = (uint8_t)rsp.current_num_lights;
        nd.shade_slot = -1;
        for (int j = 0; j < 8; j++) {
            uint8_t kind = 0;
            if (comb != NULL && j < num_inputs) {
                /* the rgb mapping decides the source; alpha-only PRIM_LOD is kind 6 */
                switch (comb->shader_input_mapping[0][j]) {
                    case G_CCMUX_PRIMITIVE: kind = 1; break;
                    case G_CCMUX_ENVIRONMENT: kind = 2; break;
                    case G_CCMUX_PRIMITIVE_ALPHA: kind = 3; break;
                    case G_CCMUX_ENV_ALPHA: kind = 4; break;
                    case G_CCMUX_PRIM_LOD_FRAC: kind = 5; break;
                    case G_CCMUX_SHADE: kind = 7; nd.shade_slot = (int8_t)j; break;
                    default: kind = 0; break;
                }
                if (use_alpha && kind == 0 && comb->shader_input_mapping[1][j] == G_ACMUX_PRIM_LOD_FRAC) kind = 6;
            }
            nd.input_kind[j] = kind;
        }
        for (int t = 0; t < 2; t++) {
            if (s.tex_used[t]) {
                nd.tex_key[t] = rendering_state.textures[t]->first;
                nd.tex_serial[t] = rendering_state.textures[t]->second.serial;
                nd.tex_key_valid[t] = true;
            }
        }
        nd.first_float = (uint32_t)r.floats.size();
        nd.first_model = (uint32_t)r.model.size();
        nd.num_tris = 0;
        nd.stride = stride;
        nd.fog_ofs = fog_ofs;
        nd.pixfog_ofs = pixfog_ofs;
        nd.fog_on = fog_on;
        nd.invert_y = invert_y;
        nd.z01 = z01;
        nd.dec_pull = dec_pull;
        nd.dec_pull_b = dec_pull_b;
        nd.dec_pull_k = dec_pull_k;
        nd.cull_mode = cull_mode;
        nd.no_clip = no_clip;
        nd.blender_fog = blender_fog;
        nd.xlu = xlu;
        r.draws.push_back(nd);
        d = &r.draws.back();
    }
    d->gen_mask |= gen_mask;
    d->last_cmd = roc_cur_cmd;
    r.floats.insert(r.floats.end(), buf_vbo + vstart, buf_vbo + buf_vbo_len);
    for (int i = 0; i < 3; i++) {
        const size_t slot = (size_t)(v_arr[i] - rsp.loaded_vertices);
        if (slot >= MAX_VERTICES) { /* rect / sky scratch slots carry no model position */
            roc_capture_bad = true;
            return;
        }
        if (!roc_slot_loaded[slot]) { /* loaded by an earlier list: no model position of ours */
            roc_stat_foreign_slots++;
            roc_capture_bad = true;
            return;
        }
        r.model.push_back(roc_model_pos[slot][0]);
        r.model.push_back(roc_model_pos[slot][1]);
        r.model.push_back(roc_model_pos[slot][2]);
        r.model.push_back((float)roc_slot_gen[slot]);
        r.nrm.push_back(roc_model_nrm[slot][0]);
        r.nrm.push_back(roc_model_nrm[slot][1]);
        r.nrm.push_back(roc_model_nrm[slot][2]);
    }
    d->num_tris++;
    r.tris++;
}

/* gfx_dxlu_apply_state, but viewport / scissor come from the frame (entry
 * state set per room by bg.c) */
static void roc_apply_state(const RocDraw& d) {
    const struct DXluState* s = &d.state;
    /* Twenty-fifth build: apply only what differs from rendering_state, and
     * flush only if something does - consecutive cached draws with the same
     * state (a re-bind of the same texture split them at capture) merge
     * into one draw call, and the textures stay bound through
     * rendering_state.textures like the interpreter's own bindings (the
     * serial bump + textures_changed for the interpreter happen once at
     * the end of roc_replay, not per draw). */
    const bool viewport_diff = memcmp(&rdp.viewport, &rendering_state.viewport, sizeof(rdp.viewport)) != 0;
    const bool scissor_diff = memcmp(&rdp.scissor, &rendering_state.scissor, sizeof(rdp.scissor)) != 0;
    bool tex_diff[2] = { false, false };
    bool samp_diff[2] = { false, false };
    bool diff = s->prg != rendering_state.shader_program
        || s->use_alpha != rendering_state.alpha_blend || s->use_modulate != rendering_state.modulate
        || s->depth_mode_enc != rendering_state.depth_mode || viewport_diff || scissor_diff;
    for (int i = 0; i < 2; i++) {
        if (s->tex_used[i]) {
            TextureCacheNode* n = d.tex_node[i];
            tex_diff[i] = rendering_state.textures[i] != n;
            samp_diff[i] = tex_diff[i] || n->second.linear_filter != s->tex_linear[i]
                || n->second.mip_filter != s->tex_mip[i]
                || n->second.cms != s->tex_cms[i] || n->second.cmt != s->tex_cmt[i];
            diff = diff || samp_diff[i];
        }
    }
    if (!diff) {
        return;
    }
    gfx_flush();
    if (s->prg != rendering_state.shader_program) {
        gfx_rapi->unload_shader(rendering_state.shader_program);
        gfx_rapi->load_shader(s->prg);
        rendering_state.shader_program = s->prg;
    }
    if (s->use_alpha != rendering_state.alpha_blend || s->use_modulate != rendering_state.modulate) {
        gfx_rapi->set_use_alpha(s->use_alpha, s->use_modulate);
        rendering_state.alpha_blend = s->use_alpha;
        rendering_state.modulate = s->use_modulate;
    }
    if (s->depth_mode_enc != rendering_state.depth_mode) {
        gfx_rapi->set_depth_mode(s->depth_test, s->depth_update, s->depth_compare, s->depth_source_prim, s->zmode);
        rendering_state.depth_mode = s->depth_mode_enc;
    }
    if (viewport_diff) {
        gfx_rapi->set_viewport(rdp.viewport.x, rdp.viewport.y, rdp.viewport.width, rdp.viewport.height);
        rendering_state.viewport = rdp.viewport;
    }
    if (scissor_diff) {
        gfx_rapi->set_scissor(rdp.scissor.x, rdp.scissor.y, rdp.scissor.width, rdp.scissor.height);
        rendering_state.scissor = rdp.scissor;
    }
    for (int i = 0; i < 2; i++) {
        if (!s->tex_used[i]) {
            continue;
        }
        TextureCacheNode* n = d.tex_node[i];
        if (tex_diff[i]) {
            gfx_rapi->select_texture(i, s->tex_id[i], s->tex_linear[i]);
            rendering_state.textures[i] = n;
        }
        if (samp_diff[i]) {
            ge_mip_active = s->tex_mip[i] ? 1 : 0;
            gfx_rapi->set_sampler_parameters(i, s->tex_linear[i], s->tex_cms[i], s->tex_cmt[i]);
            ge_mip_active = 0;
            n->second.linear_filter = s->tex_linear[i];
            n->second.mip_filter = s->tex_mip[i];
            n->second.cms = (uint8_t)s->tex_cms[i];
            n->second.cmt = (uint8_t)s->tex_cmt[i];
        }
    }
}

/* true = drawn from the cache; false = the caller must run the interpreter */
static bool roc_replay(RocRoom& r) {
    /* every cached texture must still be the same cache entry (eviction
     * recycles ids); touching them keeps them hot in the LRU */
    for (size_t di = 0; di < r.draws.size(); di++) {
        RocDraw& d = r.draws[di];
        for (int t = 0; t < 2; t++) {
            d.tex_node[t] = nullptr;
            if (!d.tex_key_valid[t]) {
                continue;
            }
            TextureCacheMap::iterator it = gfx_texture_cache.map.find(d.tex_key[t]);
            if (it != gfx_texture_cache.map.end()) {
                d.tex_node[t] = &*it;
            }
            if (it == gfx_texture_cache.map.end() || it->second.texture_id != d.state.tex_id[t]) {
                roc_release_data(r);
                r.state = 1; /* recapture on the next visit */
                roc_stat_fallback++;
                if (roc_log_budget > 0) {
                    roc_log_budget--;
                    fprintf(stderr, "ge: roc: room %d texture evicted - recapturing\n", r.room);
                }
                return false;
            }
            gfx_texture_cache.lru.splice(gfx_texture_cache.lru.end(), gfx_texture_cache.lru, it->second.lru_location);
        }
    }

    /* no entry flush: roc_apply_state flushes only when the first cached
     * draw's state differs from what the interpreter left */
    const float (*M)[4] = rsp.MP_matrix;
    const int farclip = gfx_ge_farclip_mode();
    const float fog_r = rdp.fog_color.r / 255.0f;
    const float fog_g = rdp.fog_color.g / 255.0f;
    const float fog_b = rdp.fog_color.b / 255.0f;
    const float fog_a_flat = rdp.fog_color.a / 255.0f;
    const float pix_mul = (float)rsp.fog_mul;
    const float pix_ofs = (float)rsp.fog_offset;

    for (size_t di = 0; di < r.draws.size(); di++) {
        const RocDraw& d = r.draws[di];
        roc_apply_state(d);
        const float* src = r.floats.data() + d.first_float;
        const float* mp = r.model.data() + d.first_model;
        const uint32_t stride = d.stride;
        for (uint32_t tri = 0; tri < d.num_tris; tri++) {
            float cx[3], cy[3], cz[3], cw[3];
            uint8_t code[3];
            for (int k = 0; k < 3; k++) {
                const float* ob = mp + (tri * 3 + k) * 4;
                float x = ob[0] * M[0][0] + ob[1] * M[1][0] + ob[2] * M[2][0] + M[3][0];
                float y = ob[0] * M[0][1] + ob[1] * M[1][1] + ob[2] * M[2][1] + M[3][1];
                float z = ob[0] * M[0][2] + ob[1] * M[1][2] + ob[2] * M[2][2] + M[3][2];
                float w = ob[0] * M[0][3] + ob[1] * M[1][3] + ob[2] * M[2][3] + M[3][3];
                x = gfx_adjust_x_for_aspect_ratio(x, w);
                uint8_t c = 0;
                if (x < -w) c |= 1;
                if (x > w) c |= 2;
                if (y < -w) c |= 4;
                if (y > w) c |= 8;
                if (z > w) c |= 32;
                cx[k] = x; cy[k] = y; cz[k] = z; cw[k] = w; code[k] = c;
            }
            if (!d.no_clip) {
                const uint8_t rej = code[0] & code[1] & code[2];
                bool reject = (rej & ~32u) != 0;
                if (!reject && (rej & 32u)) {
                    if (farclip == 1) {
                        reject = true;
                    } else if (farclip == 2) {
                        reject = !d.blender_fog;
                    }
                }
                if (reject) {
                    roc_stat_culled++;
                    continue;
                }
            }
            if (d.cull_mode != 0) {
                const float dx1 = cx[0] / cw[0] - cx[1] / cw[1];
                const float dy1 = cy[0] / cw[0] - cy[1] / cw[1];
                const float dx2 = cx[2] / cw[2] - cx[1] / cw[1];
                const float dy2 = cy[2] / cw[2] - cy[1] / cw[1];
                float cross = dx1 * dy2 - dy1 * dx2;
                if ((cw[0] < 0) ^ (cw[1] < 0) ^ (cw[2] < 0)) {
                    cross = -cross;
                }
                bool culled = false;
                switch (d.cull_mode) {
                    case G_CULL_FRONT: culled = cross <= 0; break;
                    case G_CULL_BACK: culled = cross >= 0; break;
                    default: culled = true; break;
                }
                if (culled) {
                    roc_stat_culled++;
                    continue;
                }
            }
            /* the three vertices are contiguous in the cache: one copy per
             * triangle, and the copy is memory-latency bound (the room data
             * is not in the 128 KB L2), so prefetch two triangles ahead */
            {
                const float* tsrc = src + tri * 3 * stride;
                __builtin_prefetch(tsrc + 6 * stride);
                __builtin_prefetch(tsrc + 6 * stride + 16);
                memcpy(buf_vbo + buf_vbo_len, tsrc, 3 * stride * sizeof(float));
            }
            for (int k = 0; k < 3; k++) {
                float* dst = buf_vbo + buf_vbo_len;
                float z = cz[k];
                const float w = cw[k];
                if (d.dec_pull && w > 4.0f * d.dec_pull_k) {
                    z += d.dec_pull_b * d.dec_pull_k / (w - d.dec_pull_k);
                }
                if (d.z01) {
                    z = (z + w) / 2.0f;
                }
                dst[0] = cx[k];
                dst[1] = d.invert_y ? -cy[k] : cy[k];
                dst[2] = z;
                dst[3] = w;
                if (d.fog_ofs >= 0) {
                    float fog_a = fog_a_flat;
                    if (d.fog_on) {
                        float ww = w;
                        if (fabsf(ww) < 0.001f) {
                            ww = 0.001f;
                        }
                        float winv = 1.0f / ww;
                        if (winv < 0.0f) {
                            winv = 32767.0f;
                        }
                        const float fog_z = cz[k] * winv * rsp.fog_mul + rsp.fog_offset;
                        fog_a = clampf(fog_z, 0.f, 255.f) / 255.0f;
                    }
                    dst[d.fog_ofs + 0] = fog_r;
                    dst[d.fog_ofs + 1] = fog_g;
                    dst[d.fog_ofs + 2] = fog_b;
                    dst[d.fog_ofs + 3] = fog_a;
                }
                if (d.pixfog_ofs >= 0) {
                    dst[d.pixfog_ofs + 0] = pix_mul;
                    dst[d.pixfog_ofs + 1] = pix_ofs;
                }
                buf_vbo_len += stride;
            }
            roc_stat_tris++;
            if (++buf_vbo_num_tris == MAX_BUFFERED) {
                gfx_flush();
            }
        }
    }
    /* the interpreter resumes under whatever state the last cached draw
     * left in rendering_state: its next triangle must re-derive and
     * re-check its bindings (the recompute flushes on any real change, so
     * the cached triangles still in buf_vbo draw with their own state) */
    ge_state_serial++;
    rdp.textures_changed[0] = rdp.textures_changed[1] = true;
    rdp.viewport_or_scissor_changed = true; /* flag-gated in gfx_sp_tri1: re-check next draw */
    roc_stat_replayed++;
    return true;
}

/* fifty-third build: the WORST frame of each 300-frame window (by interpreter
 * + XLU replay draws, the cost proxy), as per-frame numbers - the 300-frame
 * averages hide a spike (murkantor: crouching and firing at the ground = 25-30
 * fps; the muzzle smoke / sparks?) */
static struct {
    uint32_t score, interp, rect, dyn, other, cap, refused, xlu_float, xlu_ret, xlu_draws, units, parts, rooms, tris_interp, frame;
} roc_worst;
static void roc_frame_worst(unsigned frame) {
    static uint32_t p_draw[16], p_dxlu, p_xluret, p_units, p_xludraws, p_parts, p_rooms, p_tri[16];
    static bool baseline_valid = false;
    if (roc_stat_draw_origin[0] + roc_stat_draw_origin[1] + roc_stat_draw_origin[8] < p_draw[0] + p_draw[1] + p_draw[8]
        || roc_stat_tri_dxlu < p_dxlu || roc_stat_parts_replayed < p_parts || !baseline_valid) {
        /* the counters were cleared (or never sampled): rebase, no delta this frame */
        memset(p_draw, 0, sizeof(p_draw));
        memset(p_tri, 0, sizeof(p_tri));
        p_dxlu = p_xluret = p_units = p_xludraws = p_parts = p_rooms = 0;
        baseline_valid = true;
    }
    uint32_t interp = 0, tris = 0;
    for (int i = 0; i < 16; i++) {
        if (i != 8) interp += roc_stat_draw_origin[i] - p_draw[i];
        tris += roc_stat_tri_origin[i] - p_tri[i];
    }
    const uint32_t xlu_draws = roc_stat_draw_origin[8] - p_xludraws;
    const uint32_t score = interp + xlu_draws;
    if (score > roc_worst.score) {
        roc_worst.score = score;
        roc_worst.interp = interp;
        roc_worst.rect = roc_stat_draw_origin[1] - p_draw[1];
        roc_worst.dyn = roc_stat_draw_origin[7] - p_draw[7];
        roc_worst.other = roc_stat_draw_origin[0] - p_draw[0];
        roc_worst.cap = roc_stat_draw_origin[5] - p_draw[5];
        roc_worst.refused = (roc_stat_draw_origin[2] - p_draw[2]) + (roc_stat_draw_origin[3] - p_draw[3])
                          + (roc_stat_draw_origin[4] - p_draw[4]) + (roc_stat_draw_origin[6] - p_draw[6]);
        roc_worst.xlu_float = roc_stat_tri_dxlu - p_dxlu;
        roc_worst.xlu_ret = roc_stat_tri_xluret - p_xluret;
        roc_worst.xlu_draws = xlu_draws;
        roc_worst.units = dxlu_stat_units - p_units;
        roc_worst.parts = roc_stat_parts_replayed - p_parts;
        roc_worst.rooms = roc_stat_replayed - p_rooms;
        roc_worst.tris_interp = tris;
        roc_worst.frame = frame;
    }
    memcpy(p_draw, roc_stat_draw_origin, sizeof(p_draw));
    memcpy(p_tri, roc_stat_tri_origin, sizeof(p_tri));
    p_dxlu = roc_stat_tri_dxlu; p_xluret = roc_stat_tri_xluret; p_units = dxlu_stat_units;
    p_xludraws = roc_stat_draw_origin[8]; p_parts = roc_stat_parts_replayed; p_rooms = roc_stat_replayed;
}
static void roc_frame_worst_reset(void) { /* called where the window's counters are cleared */
    memset(&roc_worst, 0, sizeof(roc_worst));
}

/* the 300-frame stats lines: the console's ge.log always; the PC's stderr
 * with GE_ROCSTATS=1 (the same text - the survey's ledger format) */
#ifdef GE_XBOX
#define roc_trace portXboxTrace
#else
static void roc_trace(const char* fmt, ...) {
    static int on = -1;
    if (on < 0) {
        const char* e = getenv("GE_ROCSTATS");
        on = (e != NULL && e[0] != '0') ? 1 : 0;
    }
    if (!on) {
        return;
    }
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    fprintf(stderr, "ge: %s\n", buf);
}
extern "C" void gfx_opengl_retained_stats(unsigned* draws, unsigned* tris, unsigned* buffers, unsigned* descs); /* gfx_opengl.cpp */
#endif
static void roc_frame_stats(unsigned frame) {
    roc_frame_worst(frame);
    if (frame % 300u == 0u) {
        if (roc_stat_replayed || roc_stat_captured || roc_stat_fallback) {
            roc_trace("roc: parts replayed %u draws %u misses %u foreign-slot refusals %u rebinds %u fallbacks: missing %u deleted %u id %u (per 300 frames)", roc_stat_parts_replayed,
                          roc_stat_part_draws, roc_stat_part_misses, roc_stat_foreign_slots, roc_stat_rebinds,
                          roc_stat_fb_missing, roc_stat_fb_deleted, roc_stat_fb_id);
            roc_stat_rebinds = 0;
            roc_stat_fb_missing = roc_stat_fb_deleted = roc_stat_fb_id = 0;
            roc_trace("roc: part entry-state misses %u | interp tris: other %u rect %u room3 %u part3 %u sigmiss %u capture %u walkpass %u dynlist %u (per 300 frames)",
                          roc_stat_part_sigmiss, roc_stat_tri_origin[0], roc_stat_tri_origin[1], roc_stat_tri_origin[2], roc_stat_tri_origin[3],
                          roc_stat_tri_origin[4], roc_stat_tri_origin[5], roc_stat_tri_origin[6], roc_stat_tri_origin[7]);
            roc_trace("roc: interp draws: other %u rect %u room3 %u part3 %u sigmiss %u capture %u walkpass %u dynlist %u (per 300 frames)",
                          roc_stat_draw_origin[0], roc_stat_draw_origin[1], roc_stat_draw_origin[2], roc_stat_draw_origin[3],
                          roc_stat_draw_origin[4], roc_stat_draw_origin[5], roc_stat_draw_origin[6], roc_stat_draw_origin[7]);
            const uint32_t xlu_draws = roc_stat_draw_origin[8];
            memset(roc_stat_draw_origin, 0, sizeof(roc_stat_draw_origin));
            roc_stat_draw_origin[8] = xlu_draws; /* printed on the xlu line below, then cleared */
            /* (portXboxTrace formats into 256 chars: two lines) */
            roc_trace("roc: xlu deferred %u retained %u draws %u merged %u planar %u | xlu sorter passes %u units %u tests %u visits %u cut %u moves %u (per 300 frames)",
                          roc_stat_tri_dxlu, roc_stat_tri_xluret, roc_stat_draw_origin[8], dxlu_stat_merged, roc_stat_xlu_planar_units, dxlu_stat_passes, dxlu_stat_units, dxlu_stat_tests, dxlu_stat_visits, dxlu_stat_cut, dxlu_stat_moves);
            roc_stat_part_sigmiss = 0; memset(roc_stat_tri_origin, 0, sizeof(roc_stat_tri_origin)); roc_stat_tri_dxlu = 0;
            dxlu_stat_passes = dxlu_stat_units = dxlu_stat_tests = dxlu_stat_moves = dxlu_stat_visits = dxlu_stat_cut = 0;
            roc_stat_draw_origin[8] = 0;
            dxlu_stat_merged = 0;
            roc_stat_tri_xluret = 0;
            roc_stat_xlu_planar_units = 0;
            roc_trace("roc: worst frame %u: interp draws %u (rect %u dyn %u other %u capture %u refused %u) interp tris %u | xlu float tris %u retained %u units %u draws %u | parts %u rooms %u",
                          roc_worst.frame, roc_worst.interp, roc_worst.rect, roc_worst.dyn, roc_worst.other, roc_worst.cap, roc_worst.refused, roc_worst.tris_interp,
                          roc_worst.xlu_float, roc_worst.xlu_ret, roc_worst.units, roc_worst.xlu_draws, roc_worst.parts, roc_worst.rooms);
            roc_frame_worst_reset();
            roc_stat_parts_replayed = roc_stat_part_draws = roc_stat_part_misses = roc_stat_foreign_slots = 0;
            roc_trace("roc: rooms replayed %u captured %u fallback %u tris %u culled %u (per 300 frames) cache %u KB",
                          roc_stat_replayed, roc_stat_captured, roc_stat_fallback, roc_stat_tris, roc_stat_culled,
                          roc_total_floats * 4 / 1024);
            roc_stat_replayed = roc_stat_captured = roc_stat_fallback = roc_stat_tris = roc_stat_culled = 0;
        }
        { /* what the interpreter is actually chewing on: commands per opcode */
            uint32_t total = 0;
            char line[400];
            int len;
            for (int i = 0; i < 256; i++) total += roc_dbg_ops[i];
            len = snprintf(line, sizeof(line), "dl: %u cmds (per 300 frames) top:", total);
            for (int n = 0; n < 12; n++) {
                uint32_t best = 0; int bi = -1;
                for (int i = 0; i < 256; i++) {
                    if (roc_dbg_ops[i] > best) { best = roc_dbg_ops[i]; bi = i; }
                }
                if (bi < 0 || best == 0) break;
                len += snprintf(line + len, sizeof(line) - (size_t)len, " %02x=%u", bi, best);
                roc_dbg_ops[bi] = 0;
            }
            roc_trace("%s", line);
            memset(roc_dbg_ops, 0, sizeof(roc_dbg_ops));
        }
    }
#ifndef GE_XBOX
    if (frame % 300u == 0u) {
        unsigned d, t, b, n;
        gfx_opengl_retained_stats(&d, &t, &b, &n);
        if (d != 0 || b != 0) {
            roc_trace("roc: gl retained draws %u tris %u (per 300 frames) | buffers %u descs %u", d, t, b, n);
        }
        { /* per-frame averages over the window: the render thread's gfx_run, the
           * XLU sorter's two halves, GL draw calls */
            static unsigned long long run_us0; static uint32_t draws0;
            const unsigned long long run_us = g_PortGfxRunUs - run_us0;
            const uint32_t draws = g_PortGfxDraws - draws0;
            roc_trace("roc: per frame: gfx_run %u us | xlu tris %u sort %u us replay %u us draws %u | gl draws %u (300-frame means)",
                      (unsigned)(run_us / 300u), dxlu_stat_frame_tris / 300u, (unsigned)(dxlu_stat_sort_us / 300u),
                      (unsigned)(dxlu_stat_replay_us / 300u), dxlu_stat_frame_draws / 300u, draws / 300u);
            run_us0 = g_PortGfxRunUs; draws0 = g_PortGfxDraws;
            dxlu_stat_sort_us = dxlu_stat_replay_us = 0; dxlu_stat_frame_tris = dxlu_stat_frame_draws = 0;
        }
    }
#endif
}
