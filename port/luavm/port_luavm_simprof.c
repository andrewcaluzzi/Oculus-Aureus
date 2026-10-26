/* port_luavm_simprof.c - GE_PROFILE_SIM: a CPU-time sampling profiler of the sim step (designs/bank-heist.md section 28).
 *
 * Off unless GE_PROFILE_SIM=<file> is set; then nothing in the sim changes (the profiler only reads), and the `simprof` Lua table
 * exists (only then: a normal run's Lua state is untouched).
 *
 * How: a POSIX timer on the sim thread's own CPU clock (CLOCK_THREAD_CPUTIME_ID, SIGPROF to that thread only) fires every
 * 1/GE_PROFILE_HZ s of the thread's CPU time (default 1000 Hz). Load on other cores does not move the samples: a tick that is
 * descheduled earns none. Each sample taken inside a sim-only tick (port_simonly.c, GE_SIMONLY_TEST) records the tick, the phase
 * (simprof.phase), the native EBP chain (the tree is built -fno-omit-frame-pointer) and the running Lua frames (function source:line).
 * Each sim-only tick's thread CPU time is recorded too (the tick histogram and its worst ticks).
 *
 * Lua: simprof.phase(name) tags the following samples and ticks (default "-"). simprof.count(name) bumps a named counter.
 * C: portSimProfCount(key) bumps a call counter (key is a string literal; compared by pointer first). g_PortSimProfOn is 1 while on.
 * The dump is written at exit: tests/heist/simprof_report.py turns it into the hotspot table (it runs addr2line in the container).
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lua.h"
#include "lauxlib.h"

int g_PortSimProfOn;

#if defined(__linux__) && defined(__i386__)
#include <signal.h>
#include <time.h>
#include <pthread.h>
#include <ucontext.h>
#include <unistd.h>
#include <sys/syscall.h>
#include "lstate.h"
#include "lobject.h"

#ifndef REG_EIP   /* (the force-included ge_lua_ctype.h pulls libc in before _GNU_SOURCE: the i386 gregs indices) */
#define REG_EBP 6
#define REG_EIP 14
#endif
#ifndef sigev_notify_thread_id
#define sigev_notify_thread_id _sigev_un._tid
#endif

#define NNAT 40          /* native frames kept per sample */
#define NLUA 8           /* Lua frames kept per sample */
#define BUFW (24u << 20) /* u32 words of sample buffer (96 MB) */
#define NPROTO 4096
#define NPHASE 32
#define NCOUNT 64
#define NTICKS (1u << 20)

static int sInit, sIn;
static unsigned sTick, sPhase;
static unsigned *sBuf;
static volatile unsigned sBufN;
static unsigned sDropped;
static uintptr_t sStackLo, sStackHi;
static char sOut[512];
static const void *sProtoKey[NPROTO];
static char sProtoName[NPROTO][72];
static unsigned sNProto;
static char sPhaseName[NPHASE][32] = { "-" };
static unsigned sNPhase = 1;
static const char *sCountKey[NCOUNT];
static char sCountName[NCOUNT][48];
static unsigned long long sCount[NCOUNT][NPHASE];
static unsigned sNCount;
static unsigned (*sTicks)[3];    /* tick, phase, thread CPU us */
static unsigned sNTicks;
static struct timespec sT0;

static unsigned protoIndex(const Proto *p)
{
    unsigned h = (unsigned) (((uintptr_t) p >> 4) * 2654435761u) & (NPROTO - 1), i;

    for (i = 0; i < NPROTO; i++, h = (h + 1) & (NPROTO - 1)) {
        if (sProtoKey[h] == p) {
            return h;
        }
        if (sProtoKey[h] == NULL) {
            const char *src = "?";
            if (p->source != NULL) {
                src = getstr(p->source);
            }
            if (src[0] == '@' || src[0] == '=') {
                src++;
            }
            {   /* the tail of the path: the file name */
                const char *s = strrchr(src, '/');
                if (s != NULL) src = s + 1;
            }
            snprintf(sProtoName[h], sizeof(sProtoName[h]), "%.56s:%d", src, p->linedefined);
            sProtoKey[h] = p;
            sNProto++;
            return h;
        }
    }
    return 0xffffu;
}

static void onSample(int sig, siginfo_t *si, void *uc_)
{
    ucontext_t *uc = (ucontext_t *) uc_;
    unsigned pcs[NNAT], lua[NLUA];
    unsigned nn = 0, nl = 0, need, base, k;
    uintptr_t fp;
    lua_State *L;

    (void) sig;
    (void) si;
    if (!sIn || sBuf == NULL) {
        return;
    }
    pcs[nn++] = (unsigned) uc->uc_mcontext.gregs[REG_EIP];
    fp = (uintptr_t) uc->uc_mcontext.gregs[REG_EBP];
    while (nn < NNAT && fp >= sStackLo && fp + 8 <= sStackHi && (fp & 3) == 0) {
        uintptr_t next = ((uintptr_t *) fp)[0];
        unsigned ret = (unsigned) ((uintptr_t *) fp)[1];

        if (ret < 0x1000) {
            break;
        }
        pcs[nn++] = ret;
        if (next <= fp) {
            break;
        }
        fp = next;
    }
    {
        extern lua_State *portLuaVmState(int create);

        L = portLuaVmState(0);
    }
    if (L != NULL) {
        CallInfo *ci = L->ci;
        int guard = 0;

        while (ci != NULL && ci != &L->base_ci && nl < NLUA && guard++ < 200) {
            StkId f = ci->func.p;

            if (f >= L->stack.p && f < L->stack_last.p && !(ci->callstatus & CIST_C) && ttisLclosure(s2v(f))) {
                LClosure *cl = clLvalue(s2v(f));
                if (cl != NULL && cl->p != NULL) {
                    lua[nl++] = protoIndex(cl->p);
                }
            }
            ci = ci->previous;
        }
    }
    need = 4 + nn + nl;
    base = sBufN;
    if (base + need > BUFW) {
        sDropped++;
        return;
    }
    sBuf[base] = sTick;
    sBuf[base + 1] = sPhase;
    sBuf[base + 2] = nn;
    sBuf[base + 3] = nl;
    for (k = 0; k < nn; k++) sBuf[base + 4 + k] = pcs[k];
    for (k = 0; k < nl; k++) sBuf[base + 4 + nn + k] = lua[k];
    sBufN = base + need;
}

static void dump(void)
{
    FILE *f;
    unsigned i, j, p;

    if (!g_PortSimProfOn) {
        return;
    }
    sIn = 0;
    f = fopen(sOut, "w");
    if (f == NULL) {
        printf("ge: simprof: cannot write %s\n", sOut);
        return;
    }
    fprintf(f, "simprof 1\nanchor %08x\nhz %s\ndropped %u\n", (unsigned) (uintptr_t) &dump,
            getenv("GE_PROFILE_HZ") ? getenv("GE_PROFILE_HZ") : "1000", sDropped);
    for (i = 0; i < sNPhase; i++) fprintf(f, "N %u %s\n", i, sPhaseName[i]);
    for (i = 0; i < NPROTO; i++) if (sProtoKey[i] != NULL) fprintf(f, "P %u %s\n", i, sProtoName[i]);
    for (i = 0; i < sNCount; i++)
        for (p = 0; p < sNPhase; p++)
            if (sCount[i][p] != 0) fprintf(f, "C %s %u %llu\n", sCountName[i], p, sCount[i][p]);
    for (i = 0; i < sNTicks; i++) fprintf(f, "T %u %u %u\n", sTicks[i][0], sTicks[i][1], sTicks[i][2]);
    for (i = 0; i < sBufN;) {
        unsigned nn = sBuf[i + 2], nl = sBuf[i + 3];

        fprintf(f, "S %u %u |", sBuf[i], sBuf[i + 1]);
        for (j = 0; j < nn; j++) fprintf(f, " %x", sBuf[i + 4 + j]);
        fprintf(f, " |");
        for (j = 0; j < nl; j++) fprintf(f, " %u", sBuf[i + 4 + nn + j]);
        fprintf(f, "\n");
        i += 4 + nn + nl;
    }
    fclose(f);
    printf("ge: simprof: wrote %s (%u ticks, dropped %u samples)\n", sOut, sNTicks, sDropped);
    fflush(stdout);
}

static void init(void)
{
    const char *e = getenv("GE_PROFILE_SIM");
    struct sigaction sa;
    struct sigevent sev;
    timer_t tm;
    struct itimerspec its;
    pthread_attr_t attr;
    void *lo;
    size_t sz;
    int hz = getenv("GE_PROFILE_HZ") ? atoi(getenv("GE_PROFILE_HZ")) : 1000;

    sInit = 1;
    if (e == NULL || e[0] == '\0') {
        return;
    }
    snprintf(sOut, sizeof(sOut), "%s", e);
    sBuf = (unsigned *) malloc(BUFW * sizeof(unsigned));
    sTicks = malloc(NTICKS * sizeof(*sTicks));
    if (sBuf == NULL || sTicks == NULL || hz <= 0) {
        printf("ge: simprof: off (no memory or bad GE_PROFILE_HZ)\n");
        return;
    }
    if (pthread_getattr_np(pthread_self(), &attr) == 0) {
        pthread_attr_getstack(&attr, &lo, &sz);
        sStackLo = (uintptr_t) lo;
        sStackHi = (uintptr_t) lo + sz;
        pthread_attr_destroy(&attr);
    }
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = onSample;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGPROF, &sa, NULL);
    memset(&sev, 0, sizeof(sev));
    sev.sigev_notify = SIGEV_THREAD_ID;
    sev.sigev_signo = SIGPROF;
    sev.sigev_notify_thread_id = (pid_t) syscall(SYS_gettid);
    if (timer_create(CLOCK_THREAD_CPUTIME_ID, &sev, &tm) != 0) {
        printf("ge: simprof: timer_create failed\n");
        return;
    }
    its.it_interval.tv_sec = 0;
    its.it_interval.tv_nsec = 1000000000L / hz;
    its.it_value = its.it_interval;
    timer_settime(tm, 0, &its, NULL);
    g_PortSimProfOn = 1;
    atexit(dump);
    printf("ge: simprof: on, %d Hz of sim-thread CPU, stack %08x..%08x -> %s\n", hz, (unsigned) sStackLo, (unsigned) sStackHi, sOut);
    fflush(stdout);
}

/* port_simonly.c: a sim-only tick begins / ends */
void portSimProfBegin(unsigned tick)
{
    if (!sInit) {
        init();
    }
    if (!g_PortSimProfOn) {
        return;
    }
    sTick = tick;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &sT0);
    sIn = 1;
}

void portSimProfEnd(void)
{
    struct timespec t1;

    if (!g_PortSimProfOn || !sIn) {
        return;
    }
    sIn = 0;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t1);
    if (sNTicks < NTICKS) {
        long long us = (long long) (t1.tv_sec - sT0.tv_sec) * 1000000LL + (t1.tv_nsec - sT0.tv_nsec) / 1000;
        sTicks[sNTicks][0] = sTick;
        sTicks[sNTicks][1] = sPhase;
        sTicks[sNTicks][2] = (unsigned) (us < 0 ? 0 : us);
        sNTicks++;
    }
}

static unsigned countSlot(const char *key)
{
    unsigned i;

    for (i = 0; i < sNCount; i++) {
        if (sCountKey[i] == key || strcmp(sCountName[i], key) == 0) {
            return i;
        }
    }
    if (sNCount >= NCOUNT) {
        return NCOUNT;
    }
    sCountKey[sNCount] = key;
    snprintf(sCountName[sNCount], sizeof(sCountName[0]), "%s", key);
    return sNCount++;
}

void portSimProfCount(const char *key)
{
    unsigned i;

    if (!g_PortSimProfOn || !sIn) {
        return;
    }
    i = countSlot(key);
    if (i < NCOUNT) {
        sCount[i][sPhase]++;
    }
}

static int lPhase(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    unsigned i;

    for (i = 0; i < sNPhase; i++) {
        if (strcmp(sPhaseName[i], name) == 0) break;
    }
    if (i == sNPhase && sNPhase < NPHASE) {
        snprintf(sPhaseName[sNPhase++], sizeof(sPhaseName[0]), "%s", name);
    }
    sPhase = (i < NPHASE) ? i : 0;
    printf("ge: simprof: phase %s from tick %u\n", name, sTick);
    fflush(stdout);
    return 0;
}

static int lCount(lua_State *L)
{
    char key[48];
    unsigned i;

    snprintf(key, sizeof(key), "lua:%s", luaL_checkstring(L, 1));
    if (g_PortSimProfOn && sIn) {
        i = countSlot(key);
        if (i < NCOUNT && sCountKey[i] == key) {
            sCountKey[i] = sCountName[i];   /* not the stack buffer */
        }
        if (i < NCOUNT) sCount[i][sPhase]++;
    }
    return 0;
}

void portLuaSimProfRegister(lua_State *L)
{
    static const luaL_Reg lib[] = { { "phase", lPhase }, { "count", lCount }, { NULL, NULL } };
    const char *e = getenv("GE_PROFILE_SIM");

    if (e == NULL || e[0] == '\0') {
        return;   /* off: the Lua state is a normal run's */
    }
    luaL_newlib(L, lib);
    lua_setglobal(L, "simprof");
}

#else   /* not 32-bit Linux: off */
void portSimProfBegin(unsigned tick) { (void) tick; }
void portSimProfEnd(void) {}
void portSimProfCount(const char *key) { (void) key; }
void portLuaSimProfRegister(lua_State *L) { (void) L; }
#endif
