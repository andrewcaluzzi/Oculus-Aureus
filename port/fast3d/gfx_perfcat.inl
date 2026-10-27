/* gfx_perfcat.inl - included by gfx_pc.cpp (render thread). See gfx_perfcat.h. Counting is a few integer adds per triangle
 * and per batch; the clock is read only while g_PerfCatOn is set (per category switch and around each GL draw call). */
#include "gfx_perfcat.h"

static uint8_t pc_cur = PERFCAT_FRAME;    /* the category the commands being walked belong to */
static uint8_t pc_buf = PERFCAT_LEVEL;    /* the category of the triangles waiting in buf_vbo */
static bool pc_timing;                    /* latched from g_PerfCatOn at frame start */
static bool pc_batch;                     /* the marker asked for the chr batch (PERFCAT_FLAG_BATCH): gfx_chrbatch.inl */
static int64_t pc_t;                      /* ns of the last switch */
static unsigned int pc_draws[PERFCAT_N], pc_tris[PERFCAT_N];
static int64_t pc_ns[PERFCAT_N], pc_drawns[PERFCAT_N];
static PerfCatFrame pc_last;

static inline int64_t pc_now(void) {
    return (int64_t) std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

static inline void pc_switch(unsigned cat) {
    if (cat >= PERFCAT_N) cat = PERFCAT_LEVEL;
    if (pc_timing) {
        const int64_t t = pc_now();
        pc_ns[pc_cur] += t - pc_t;
        pc_t = t;
    }
    pc_cur = (uint8_t) cat;
}

static void pc_frame_begin(void) {
    memset(pc_draws, 0, sizeof(pc_draws));
    memset(pc_tris, 0, sizeof(pc_tris));
    memset(pc_ns, 0, sizeof(pc_ns));
    memset(pc_drawns, 0, sizeof(pc_drawns));
    pc_timing = g_PerfCatOn != 0;
    pc_t = pc_timing ? pc_now() : 0;
    pc_cur = PERFCAT_FRAME;
    pc_buf = PERFCAT_LEVEL;
    pc_batch = false;
}

static void pc_frame_end(unsigned frame) {
    pc_switch(PERFCAT_FRAME);   /* charges the open stretch */
    for (int i = 0; i < PERFCAT_N; i++) {
        pc_last.draws[i] = pc_draws[i];
        pc_last.tris[i] = pc_tris[i];
        pc_last.ms[i] = (float) pc_ns[i] / 1.0e6f;
        pc_last.drawms[i] = (float) pc_drawns[i] / 1.0e6f;
    }
    pc_last.frame = frame;
}

extern "C" void gfx_perfcat_last(PerfCatFrame *out) {
    *out = pc_last;
}

/* gfx_flush around the GL draw call: the batch and its time go to the category of its triangles, not to whatever the walk
 * has moved on to (a state change of the next prop is what flushes the last prop's batch) */
#define PC_FLUSH_BEGIN() const int64_t pc_f0 = pc_timing ? pc_now() : 0
#define PC_FLUSH_END() do { \
        pc_draws[pc_buf]++; \
        if (pc_timing) { const int64_t pc_dt = pc_now() - pc_f0; pc_drawns[pc_buf] += pc_dt; pc_ns[pc_buf] += pc_dt; pc_ns[pc_cur] -= pc_dt; } \
    } while (0)
