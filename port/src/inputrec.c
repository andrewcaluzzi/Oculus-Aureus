/**
 * Input recorder / replayer (diagnostic harness).
 *
 * GE_INPUTREC=<file>  records player 1's game-facing input to <file>.
 * GE_INPUTPLAY=<file> replays it headless, replacing every live source.
 *
 * Purpose: some render bugs only appear after a specific route through a level
 * that GE_SPAWNXYZ / GE_GOTO cannot reach. Play the route once with
 * GE_INPUTREC, then replay it and take GE_SHOT captures.
 *
 * Record kinds - each is the value the game is about to consume:
 *   P  libultra_shim.c osContGetReadData: pad 0 after every merge + the gate
 *   A  joy_port.c portJoyPortPollCommit: the Port actions down this poll
 *   M  bondview2.c: the tick's mouse-look sample (scaled + direct degrees)
 *   D  input_pc.c portInputDigitalMove(0): keyboard / D-pad digital Move
 *   T  bondview2.c, after M: Bond's facing + position (replay checks drift)
 *
 * Every record carries the sim tick it was made in (portInputRecTick, bumped by
 * portInputMouseTickConsume). Polls per tick differ between live and headless
 * runs, so replay matches by tick, not call order: a call gets the next record
 * of its kind from the same tick; with none left it repeats the tick's last
 * record (pad, actions, move are held states) or gets zero (mouse deltas must
 * apply once); records from ticks replay never reaches are skipped. Start both
 * runs the same way (GE_STAGE, GE_SPAWNXYZ, GE_SEED, same midas.ini tickhz) or
 * replay desyncs.
 *
 * Read when touching: osContGetReadData, portJoyPortPollCommit, the mouse
 * sample in bondviewProcessInput, portInputDigitalMove,
 * portInputMouseTickConsume.
 */
#include <ultra64.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void portConLogf(const char *fmt, ...);

enum { REC_OFF = 0, REC_RECORD = 1, REC_PLAY = 2 };
enum { K_PAD, K_ACT, K_MOUSE, K_DIG, K_CHECK, K_COUNT };

typedef struct { u32 t, a, b, c; f32 f[4]; } RecEntry;

static s32 sMode = -1;
static FILE *sOut;
static u32 sTick;
static RecEntry *sPlay[K_COUNT];
static u32 sPlayN[K_COUNT], sPlayAt[K_COUNT];
static RecEntry *sPlayLast[K_COUNT]; /* the last record handed out, for repeats */
static s32 sPlayDoneLogged;
static s32 sSkipLogged;

static void inputRecInit(void)
{
    const char *rec = getenv("GE_INPUTREC");
    const char *play = getenv("GE_INPUTPLAY");
    sMode = REC_OFF;
    if (play != NULL && play[0] != '\0')
    {
        FILE *f = fopen(play, "r");
        char line[256];
        u32 cap[K_COUNT] = { 0 };
        if (f == NULL)
        {
            portConLogf("ge: inputrec: cannot open %s\n", play);
            return;
        }
        while (fgets(line, sizeof(line), f) != NULL)
        {
            s32 k;
            RecEntry e;
            memset(&e, 0, sizeof(e));
            switch (line[0])
            {
                case 'P': k = K_PAD;   sscanf(line + 1, "%u %x %x %x", &e.t, &e.a, &e.b, &e.c); break;
                case 'A': k = K_ACT;   sscanf(line + 1, "%u %x %x", &e.t, &e.a, &e.b); break;
                case 'M': k = K_MOUSE; sscanf(line + 1, "%u %u %f %f %f %f", &e.t, &e.a, &e.f[0], &e.f[1], &e.f[2], &e.f[3]); break;
                case 'D': k = K_DIG;   sscanf(line + 1, "%u %d %d", &e.t, (s32 *) &e.a, (s32 *) &e.b); break;
                case 'T': k = K_CHECK; sscanf(line + 1, "%u %f %f %f %f", &e.t, &e.f[0], &e.f[1], &e.f[2], &e.f[3]); break;
                default: continue;
            }
            if (sPlayN[k] == cap[k])
            {
                cap[k] = cap[k] ? cap[k] * 2 : 4096;
                sPlay[k] = (RecEntry *) realloc(sPlay[k], cap[k] * sizeof(RecEntry));
            }
            sPlay[k][sPlayN[k]++] = e;
        }
        fclose(f);
        sMode = REC_PLAY;
        portConLogf("ge: inputrec: replaying %s (%u pad, %u action, %u mouse, %u move records)\n",
                    play, sPlayN[K_PAD], sPlayN[K_ACT], sPlayN[K_MOUSE], sPlayN[K_DIG]);
    }
    else if (rec != NULL && rec[0] != '\0')
    {
        sOut = fopen(rec, "w");
        if (sOut == NULL)
        {
            portConLogf("ge: inputrec: cannot create %s\n", rec);
            return;
        }
        sMode = REC_RECORD;
        portConLogf("ge: inputrec: recording player 1 to %s\n", rec);
    }
}

static inline s32 inputRecMode(void)
{
    if (sMode < 0)
    {
        inputRecInit();
    }
    return sMode;
}

/** input_pc.c portInputMouseTickConsume: one sim tick has started */
void portInputRecTick(void)
{
    sTick++;
}

/** the sim tick the records are being made in (port_playerbot.c decides once per tick) */
u32 portInputRecTickNow(void)
{
    return sTick;
}

/** joy_port.c: judge the staleness of a latched one-shot press in sim ticks, not wall time? Yes while recording or
 * replaying, and under GE_FIXEDTICK=1 (the machine-independent mode the bot harness runs in): a wall-clock test makes
 * the sim depend on how fast the machine ran that tick */
int portInputRecTickClock(void)
{
    static int fixed = -1;

    if (fixed < 0)
    {
        const char *e = getenv("GE_FIXEDTICK");

        fixed = e != NULL && e[0] != '\0' && e[0] != '0';
    }
    return fixed || inputRecMode() != REC_OFF;
}

/** flush a recording before the process leaves through _exit (port_playerbot.c result) */
void portInputRecFlush(void)
{
    if (sOut != NULL)
    {
        fflush(sOut);
    }
}

/* port_playerbot.c: the player bot writes player 1's input here, before it is recorded (GE_PLAYERBOT=1) */
extern void portPlayerBotPad(OSContPad *pad);
extern void portPlayerBotLook(f32 *dx, f32 *dy, s32 *direct, f32 *yawdeg, f32 *pitchdeg);
extern void portPlayerBotDigMove(s32 *dx, s32 *dy);
extern void portPlayerBotActions(u8 *down, s32 count);

/* the record of kind k for this call: the next one made in the current tick,
 * else (no record left in this tick) the tick's last one again when `repeat`,
 * else NULL = neutral */
static RecEntry *inputRecNext(s32 k, s32 repeat)
{
    while (sPlayAt[k] < sPlayN[k] && sPlay[k][sPlayAt[k]].t < sTick)
    {   /* a tick (or call) this run skipped: a pad record that was never handed out and differs from the one before it
         * is input the replay lost (a tick with no controller poll here, where the recording had one) */
        RecEntry *e = &sPlay[k][sPlayAt[k]++];

        if (k == K_PAD && sPlayLast[k] != NULL && sPlayLast[k] != e && sSkipLogged < 20 &&
            (e->a != sPlayLast[k]->a || e->b != sPlayLast[k]->b || e->c != sPlayLast[k]->c))
        {
            sSkipLogged++;
            portConLogf("ge: inputrec: LOST pad record of tick %u (btn %x stick %d,%d) at tick %u: no poll in that tick\n",
                        e->t, e->a, (s8) (u8) e->b, (s8) (u8) e->c, sTick);
        }
        sPlayLast[k] = e;
    }
    if (sPlayAt[k] < sPlayN[k] && sPlay[k][sPlayAt[k]].t == sTick)
    {
        sPlayLast[k] = &sPlay[k][sPlayAt[k]++];
        return sPlayLast[k];
    }
    if (sPlayAt[k] >= sPlayN[k] && k == K_PAD && !sPlayDoneLogged)
    {
        sPlayDoneLogged = 1;
        portConLogf("ge: inputrec: recording used up at tick %u - neutral input from here\n", sTick);
    }
    if (repeat && sPlayAt[k] < sPlayN[k])
    {
        return sPlayLast[k];
    }
    return NULL;
}

/** libultra_shim.c osContGetReadData, after the stick gate: pad 0 */
void portInputRecPad(OSContPad *pad)
{
    s32 m = inputRecMode();
    portPlayerBotPad(pad);
    if (m == REC_RECORD)
    {
        fprintf(sOut, "P %u %x %x %x\n", sTick, pad->button, (u8) pad->stick_x, (u8) pad->stick_y);
        fflush(sOut); /* once a poll: the port leaves through _exit, which skips stdio */
    }
    else if (m == REC_PLAY)
    {
        RecEntry *e = inputRecNext(K_PAD, 1);
        pad->button = e ? (u16) e->a : 0;
        pad->stick_x = e ? (s8) (u8) e->b : 0;
        pad->stick_y = e ? (s8) (u8) e->c : 0;
    }
}

/** joy_port.c portJoyPortPollCommit, before the edges: pad 0's actions down this poll */
void portInputRecActions(u8 *down, s32 count)
{
    s32 m = inputRecMode();
    s32 i;
    portPlayerBotActions(down, count);
    if (m == REC_RECORD)
    {
        u32 lo = 0, hi = 0;
        for (i = 0; i < count && i < 64; i++)
        {
            if (down[i])
            {
                if (i < 32) lo |= 1u << i; else hi |= 1u << (i - 32);
            }
        }
        fprintf(sOut, "A %u %x %x\n", sTick, lo, hi);
    }
    else if (m == REC_PLAY)
    {
        RecEntry *e = inputRecNext(K_ACT, 1);
        for (i = 0; i < count && i < 64; i++)
        {
            down[i] = e ? (u8) (((i < 32 ? e->a >> i : e->b >> (i - 32)) & 1u) != 0) : 0;
        }
    }
}

/** bondview2.c, player 1's per-tick mouse-look sample */
void portInputRecMouse(f32 *dx, f32 *dy, s32 *direct, f32 *yawdeg, f32 *pitchdeg)
{
    s32 m = inputRecMode();
    portPlayerBotLook(dx, dy, direct, yawdeg, pitchdeg);
    if (m == REC_RECORD)
    {
        fprintf(sOut, "M %u %u %.9g %.9g %.9g %.9g\n", sTick, (u32) *direct, *dx, *dy, *yawdeg, *pitchdeg);
    }
    else if (m == REC_PLAY)
    {
        RecEntry *e = inputRecNext(K_MOUSE, 0);
        *direct = e ? (s32) e->a : *direct;
        *dx = e ? e->f[0] : 0.0f;
        *dy = e ? e->f[1] : 0.0f;
        *yawdeg = e ? e->f[2] : 0.0f;
        *pitchdeg = e ? e->f[3] : 0.0f;
    }
}

/** input_pc.c portInputDigitalMove, slot 0 */
void portInputRecDigMove(s32 *dx, s32 *dy)
{
    s32 m = inputRecMode();
    portPlayerBotDigMove(dx, dy);
    if (m == REC_RECORD)
    {
        fprintf(sOut, "D %u %d %d\n", sTick, *dx, *dy);
    }
    else if (m == REC_PLAY)
    {
        RecEntry *e = inputRecNext(K_DIG, 1);
        *dx = e ? (s32) e->a : 0;
        *dy = e ? (s32) e->b : 0;
    }
}

/** bondview2.c, right after the mouse sample: Bond's facing (degrees) and
 * position. Recording writes it; replay compares it with the recording's
 * value for the same tick and reports drift (first, then every ~120 ticks) */
void portInputRecCheck(f32 theta, f32 x, f32 y, f32 z)
{
    s32 m = inputRecMode();
    if (m == REC_RECORD)
    {
        fprintf(sOut, "T %u %.3f %.2f %.2f %.2f\n", sTick, theta, x, y, z);
    }
    else if (m == REC_PLAY)
    {
        static u32 lastlog, drifts, checks;
        RecEntry *e = inputRecNext(K_CHECK, 0);
        if (e != NULL)
        {
            f32 dt = theta - e->f[0], dx = x - e->f[1], dz = z - e->f[3];
            checks++;
            if (dt > 180.0f) dt -= 360.0f;
            if (dt < -180.0f) dt += 360.0f;
            if (dt > 0.5f || dt < -0.5f || dx > 2.0f || dx < -2.0f || dz > 2.0f || dz < -2.0f)
            {
                drifts++;
                if (drifts == 1 || sTick - lastlog >= 120)
                {
                    lastlog = sTick;
                    portConLogf("ge: inputrec: DRIFT tick %u: theta %.2f (rec %.2f) pos %.1f %.1f (rec %.1f %.1f)\n",
                                sTick, theta, e->f[0], x, z, e->f[1], e->f[3]);
                }
            }
            else if ((checks % 600) == 0)
            {
                portConLogf("ge: inputrec: in step at tick %u (theta %.2f pos %.1f %.1f)\n", sTick, theta, x, z);
            }
        }
    }
}
