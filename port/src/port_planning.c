/* port_planning.c - the heist's PLANNING phase, C side (designs/bank-heist.md section 27; the rules and the board's content are in
 * tools/heist_planning.lua, the Lua glue in port/luavm/port_luavm_planning.c, the board itself is port_intel.c's "plan mode").
 *
 * Before the heist clock starts the crew sits in the van with the planning board up. What C does:
 *   - the HOLD: while the script holds the start (plan.hold(true)) every Bond's stick and buttons are taken in his own tick
 *     (bondview2.c, before MoveBond: his synchronized input) and never reach MoveBond, so nobody moves or fires; and the controls are
 *     not live (bondview2.c portMouseLookPermitted asks portPlanningHeld, as for a camera feed): no mouse look, no Move keys' steps,
 *     no Look stick, no Port binds, and the pointer is let go so the board's mouse cursor can move;
 *   - the board's INPUT as sim state: the same tick turns his buttons into edge events (up / down / left / right / select / back /
 *     start) that the script takes with plan.input(p). Up / down: the stick, the D-pad, C-up / C-down. Left / right: the stick, the
 *     D-pad, C-left / C-right, and the Port's next / previous weapon or inventory (the mouse wheel, , and ., the bumpers: the INTEL
 *     board's own page keys; port_intel.c lets them through to the sim while planning). Select: fire (Z) or A. Back: B or the Port's
 *     use key. Start: START.
 * Everything here is sim state: it reads only Bond p's own synchronized input, it is in the co-op / netplay state hash
 * (portPlanningHash, folded in port_heist.c) and in the rollback snapshot (this file is SIM in scripts/port/snap_embed.py). PC only. */
#ifndef TARGET_N64

#include <string.h>
#include <ultra64.h>
#include <bondtypes.h>

#define PL_MAX 4

enum { PE_UP = 1, PE_DOWN = 2, PE_LEFT = 4, PE_RIGHT = 8, PE_SELECT = 16, PE_BACK = 32, PE_START = 64 };

extern int portModeActive(void);
extern s32 portJoyPortCctvTake(void);
extern u16 joyGetButtonsPressedThisFrame(s8 contpadnum, u16 mask);

static s32 sHold;               /* the script holds the start: nobody moves, the buttons drive the board */
static u16 sPrevBtn[PL_MAX];
static s8 sPrevX[PL_MAX], sPrevY[PL_MAX];
#define PL_QUEUE 32
static u8 sEv[PL_MAX][PL_QUEUE];   /* events not yet taken by the script, in order (the script ticks at 4 Hz: two presses in one
                                    * script tick stay two) */
static s32 sEvN[PL_MAX];
static u32 sTicks[PL_MAX];      /* ticks held */
/* the board's MOUSE (section 27, "the planfix"): a click is local UI (port_intel.c hit-tests the cursor), but what it does arrives here
 * as a session command (port_netcmd.c PORT_NETCMD_PLAN_POINT: in netplay both machines apply it at the same agreed tick; solo and
 * local co-op at once), so the plan only ever changes from sim state. a = page * 64 + row (0 = no row), b = PP_* */
#define PL_PTRQ 16
static s16 sPtr[PL_MAX][PL_PTRQ][2];
static s32 sPtrN[PL_MAX];

void portPlanningReset(void)
{
    sHold = 0;
    memset(sPtr, 0, sizeof(sPtr));
    memset(sPtrN, 0, sizeof(sPtrN));
    memset(sPrevBtn, 0, sizeof(sPrevBtn));
    memset(sPrevX, 0, sizeof(sPrevX));
    memset(sPrevY, 0, sizeof(sPrevY));
    memset(sEv, 0, sizeof(sEv));
    memset(sEvN, 0, sizeof(sEvN));
    memset(sTicks, 0, sizeof(sTicks));
}

static void push(int p, u32 ev)
{
    if (ev != 0 && sEvN[p] < PL_QUEUE) sEv[p][sEvN[p]++] = (u8) ev;
}

void portPlanningHold(int on)
{
    sHold = on ? 1 : 0;
    if (!sHold) { memset(sEv, 0, sizeof(sEv)); memset(sEvN, 0, sizeof(sEvN)); memset(sPtr, 0, sizeof(sPtr)); memset(sPtrN, 0, sizeof(sPtrN)); }
}

int portPlanningHeld(void) { return sHold && portModeActive(); }

/* ticks Bond p has spent held at the board (input_pc.c GE_SKIPINTRO: the intro taps stop once the board has the controls) */
int portPlanningBondTicks(int p) { return (p >= 0 && p < PL_MAX && sHold && portModeActive()) ? (int) sTicks[p] : 0; }

/* port_netcmd.c portSessionCmdExec (PORT_NETCMD_PLAN_POINT): a board click for Bond p, sim side (the same tick on every machine) */
void portPlanningPointerPush(int p, int a, int b)
{
    if (p < 0 || p >= PL_MAX || !sHold || sPtrN[p] >= PL_PTRQ) return;
    sPtr[p][sPtrN[p]][0] = (s16) a;
    sPtr[p][sPtrN[p]][1] = (s16) b;
    sPtrN[p]++;
}

/* plan.pointer(p): Bond p's next board click, taken; 0 = none left */
int portPlanningPointerTake(int p, int *a, int *b)
{
    if (p < 0 || p >= PL_MAX || sPtrN[p] <= 0) return 0;
    *a = sPtr[p][0][0];
    *b = sPtr[p][0][1];
    memmove(&sPtr[p][0], &sPtr[p][1], sizeof(sPtr[p][0]) * (PL_PTRQ - 1));
    sPtr[p][PL_PTRQ - 1][0] = sPtr[p][PL_PTRQ - 1][1] = 0;
    sPtrN[p]--;
    return 1;
}

/* bondview2.c, Bond p's tick, before MoveBond. 1 = held (nothing reaches MoveBond). */
int portPlanningBondTick(int p, s8 *sx, s8 *sy, u16 *buttons)
{
    u16 raw, pressed;
    s8 x, y;
    s32 acts;
    u32 ev = 0;

    if (p < 0 || p >= PL_MAX) return 0;
    raw = *buttons;
    x = *sx;
    y = *sy;
    if (!sHold || !portModeActive())
    {
        sPrevBtn[p] = raw;
        sPrevX[p] = x;
        sPrevY[p] = y;
        return 0;
    }
    pressed = (u16) ((raw & ~sPrevBtn[p]) | joyGetButtonsPressedThisFrame((s8) p, 0xFFFF));   /* (a tap between two ticks too) */
    acts = portJoyPortCctvTake();   /* next / previous (weapon, inventory), use / menu back: every latch taken */
    if ((pressed & (U_JPAD | U_CBUTTONS)) || (y > 50 && sPrevY[p] <= 50)) ev |= PE_UP;
    if ((pressed & (D_JPAD | D_CBUTTONS)) || (y < -50 && sPrevY[p] >= -50)) ev |= PE_DOWN;
    if ((pressed & (L_JPAD | L_CBUTTONS)) || (x < -50 && sPrevX[p] >= -50) || (acts & 2)) ev |= PE_LEFT;
    if ((pressed & (R_JPAD | R_CBUTTONS)) || (x > 50 && sPrevX[p] <= 50) || (acts & 1)) ev |= PE_RIGHT;
    if (pressed & (Z_TRIG | A_BUTTON)) ev |= PE_SELECT;
    if ((pressed & B_BUTTON) || (acts & 4)) ev |= PE_BACK;
    if (pressed & START_BUTTON) ev |= PE_START;
    push(p, ev);
    sTicks[p]++;
    sPrevBtn[p] = raw;
    sPrevX[p] = x;
    sPrevY[p] = y;
    *sx = 0;
    *sy = 0;
    *buttons = 0;
    return 1;
}

/* the script takes Bond p's next event (a bit mask, PE_*: one Bond tick's presses), 0 = none left */
unsigned portPlanningTake(int p)
{
    u32 e;

    if (p < 0 || p >= PL_MAX || sEvN[p] <= 0) return 0;
    e = sEv[p][0];
    memmove(&sEv[p][0], &sEv[p][1], (size_t) (PL_QUEUE - 1));
    sEv[p][PL_QUEUE - 1] = 0;
    sEvN[p]--;
    return e;
}

/* tests: events for Bond p as if he had pressed them (the script's own tick: sim side, the same on every machine) */
void portPlanningTestInput(int p, unsigned mask)
{
    if (p >= 0 && p < PL_MAX) push(p, mask & 0x7Fu);
}

/* tests: 1 = Bond p carries the item (ITEM_*) in his inventory (reads only; bond.equip would add it) */
extern int bondinvHasInvItem(ITEM_IDS item);
extern s32 get_cur_playernum(void);
extern void set_cur_player(s32 p);
extern int portLuaGameBondCount(void);

int portPlanningHasItem(int p, int item)
{
    s32 saved;
    int r;

    if (!portModeActive() || p < 0 || p >= PL_MAX || p >= portLuaGameBondCount() || item <= 0) return 0;
    saved = get_cur_playernum();
    set_cur_player(p);
    r = bondinvHasInvItem((ITEM_IDS) item) ? 1 : 0;
    set_cur_player(saved);
    return r;
}

/* plan.strip(p, item): the stage's own start weapon out of Bond p's inventory (the plan sells the guns) - sim, at the stage start */
extern void bondinvRemoveItemByID(ITEM_IDS weaponnum);

int portPlanningStrip(int p, int item)
{
    s32 saved;
    int had;

    if (!portModeActive() || p < 0 || p >= PL_MAX || p >= portLuaGameBondCount() || item <= ITEM_FIST) return 0;
    saved = get_cur_playernum();
    set_cur_player(p);
    had = bondinvHasInvItem((ITEM_IDS) item) ? 1 : 0;
    if (had) bondinvRemoveItemByID((ITEM_IDS) item);
    set_cur_player(saved);
    return had;
}

unsigned portPlanningHash(void)
{
    unsigned h = 2166136261u;
    int p, k;

    h = h * 16777619u + (unsigned) sHold;
    for (p = 0; p < PL_MAX; p++)
    {
        h = h * 16777619u + (unsigned) sPrevBtn[p];
        h = h * 16777619u + (unsigned) (u8) sPrevX[p];
        h = h * 16777619u + (unsigned) (u8) sPrevY[p];
        for (k = 0; k < sEvN[p]; k++) h = h * 16777619u + sEv[p][k];
        h = h * 16777619u + (unsigned) sEvN[p];
        h = h * 16777619u + sTicks[p];
        for (k = 0; k < sPtrN[p]; k++) h = h * 16777619u + ((unsigned) (u16) sPtr[p][k][0] << 8) + (unsigned) (u16) sPtr[p][k][1];
        h = h * 16777619u + (unsigned) sPtrN[p];
    }
    return h;
}

#endif /* !TARGET_N64 */
