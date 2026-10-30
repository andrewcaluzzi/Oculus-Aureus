/**
 * port_console.c - the tilde console (docs/port/menu-overhaul-spec.md 7).
 *
 * Modelled on the Perfect Dark port's console.c, with these differences:
 *   - there is no netplay, so no chat and no leading slash: every line typed
 *     here is a command.
 *   - commands set midas.ini keys through port_options.c, so a console change
 *     takes the same code path as a menu change and is saved the same way.
 *   - the port's log lines are copied into the console (portConLogf), so
 *     `~` then PageUp shows in-game what ge_run_*.log contains.
 *
 * This file uses no SDL. input_pc.c turns keystrokes into portConKey /
 * portConTextChar, and boss.c calls portConRender once per frame just before
 * gDPFullSync, so headless builds still collect the log but draw nothing.
 */
#ifndef TARGET_N64

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ultra64.h>

#include "port_console.h"
#include "port_console_env.h"
#include "port_mode.h"
#include "port_blocks.h"
#include "game/textrelated.h"
#include "game/dyn.h"
#include "fr.h"

/* ---- ring buffer ---------------------------------------------------------- */

#define CON_ROWS      128          /* scrollback lines (a whole stage load fits) */
#define CON_COLS      120          /* fits a 4:3 window's 640-unit console space
                                   * (G_CONSOLE2D_EXT); long all-caps lines may clip */
#define CON_VISROWS   20           /* about the old box's share of the screen (10 at 240 lines) */
#define CON_MSGROWS   4
#define CON_MSGDWELL  (3000ull * 46875ull) /* transient messages show for 3 s of osGetTime() (46875
                                            * counts/ms); wall clock, since the tick
                                            * rate follows maxfps */
#define CON_HISTORY   16

#define CON_ROW_PITCH 12           /* units between console rows in its 480-line space */
#define CON_TEXT_X    10
#define CON_TOP_Y     4
#define CON_HUD_Y     90           /* mode HUD lines (port_blocks.h), below the transient message rows */

static char conBuf[CON_ROWS][CON_COLS + 1];
static char conInput[CON_COLS + 1];
static char conHistory[CON_HISTORY][CON_COLS + 1];
static s32  conHistoryCount;
static s32  conHistoryPos = -1;    /* -1 = editing a fresh line */
static char *conVisRows[CON_VISROWS];
static char conMsgBuf[CON_MSGROWS][CON_COLS + 1]; /* flashed rows, [0] = newest; only showmsg prints write it */
static s32  conPrintRow;
static s32  conPrintCol;
static s32  conMsgRows;
static OSTime conMsgTimer;         /* osGetTime() deadline for the top row */
static s32  conInputCol;
static s32  conOpen;
static s32  conInited;

#define CON_SCROLLSTEP (CON_VISROWS / 2)
/* conPrintRow's own slot is the live tail, never history: 127 finished rows
 * at most, so the top visible row reaches the oldest one at 127 - 10 */
#define CON_SCROLLMAX  (CON_ROWS - 1 - CON_VISROWS)
static s32 conScrollOfs;
static s32 conRowsDone;            /* finished rows in the ring; 0 after init/clear,
                                    * saturates at CON_ROWS - 1 once it wraps */

/* how far back the view may go: only as far as rows actually written */
static s32 conScrollLimit(void)
{
    s32 lim = conRowsDone - CON_VISROWS;

    if (lim < 0)
    {
        lim = 0;
    }
    if (lim > CON_SCROLLMAX)
    {
        lim = CON_SCROLLMAX;
    }
    return lim;
}

/* Ring lock: several threads log through portConLogf, so every write to the ring takes this lock.
 * It is a bounded try-spin, never a blocking wait, because a holder may be a
 * suspended thread (a logger that gives up only loses the console copy, stderr
 * still has it); drawing reads unlocked, which is safe. */
static volatile s32 conLock;

static s32 conLockTry(void)
{
    s32 spin;

    for (spin = 0; spin < 0x100000; spin++)
    {
        if (conLock == 0 && __sync_lock_test_and_set(&conLock, 1) == 0)
        {
            return 1;
        }
    }
    return 0;
}

static void conUnlock(void)
{
    __sync_lock_release(&conLock);
}

/* Text colour: GE's textRender takes 0xRRGGBBAA. Console green over a dark
 * box, matching the watch's own green rather than PD's. */
#define CON_COL_TEXT   0x60FF60FF
#define CON_COL_PROMPT 0xFFFFFFFF
#define CON_BOX_ALPHA  0xC8

/* Master display-list budget. portConRender appends to dyn.c's master DL after the frame is
 * built, and nothing bounds g_GfxBuffers, so an overrun would corrupt the other
 * buffer half or g_VtxBuffers. Every write is therefore gated on
 * dynGetFreeGfx2() (which counts Gfx commands, not bytes), using these
 * worst-case sizes:
 *   ROW   3 setup commands plus 12 per glyph (SetTextureLUT 1 + LoadTextureBlock <= 7 +
 *         TextureRectangle 3, rounded up), over the longest row text (CON_COLS + 24)
 *   SETUP microcode_constructor (11) + the backing box (<= 6), rounded up
 *   TAIL  combiner_bayer_lod_perspective (4) + boss.c's gDPFullSync and
 *         gSPEndDisplayList (2), rounded up */
#define CON_GFX_ROW    (3 + (CON_COLS + 24) * 12)
#define CON_GFX_ROWLEN(n) (3 + (s32) (n) * 12)   /* the gate for a row of n characters */
#define CON_GFX_SETUP  40                        /* + the space bracket and its scissor */
#define CON_GFX_TAIL   8

static void conRebuildVisRows(void)
{
    s32 i;

    for (i = 0; i < CON_VISROWS; i++)
    {
        s32 row = conPrintRow - i - 1 - conScrollOfs;
        while (row < 0)
        {
            row += CON_ROWS;
        }
        conVisRows[i] = &conBuf[row][0];
    }
}

void portConInit(void)
{
    memset(conBuf, 0, sizeof(conBuf));
    memset(conInput, 0, sizeof(conInput));
    memset(conHistory, 0, sizeof(conHistory));
    memset(conVisRows, 0, sizeof(conVisRows));
    conPrintRow = conPrintCol = 0;
    conHistoryCount = 0;
    conHistoryPos = -1;
    conOpen = 0;
    conMsgRows = 0;
    conMsgTimer = 0;
    conInputCol = 0;
    conScrollOfs = 0;
    conRowsDone = 0;
    conInited = 1;
    conRebuildVisRows();
}

void portConPrint(s32 showmsg, const char *str)
{
    const char *s;
    s32 oldRow;

    if (str == NULL || *str == '\0')
    {
        return;
    }
    if (!conLockTry())
    {
        return; /* another thread mid-print (or frozen holding it) */
    }
    if (!conInited)
    {
        portConInit();
    }

    oldRow = conPrintRow;

    for (s = str; *s != '\0'; s++)
    {
        char ch = *s;

        switch (ch)
        {
        case '\n':
            if (conPrintCol != 0)
            {
                conPrintCol = 0;
                conPrintRow = (conPrintRow + 1) % CON_ROWS;
                conBuf[conPrintRow][0] = '\0';
                if (conRowsDone < CON_ROWS - 1)
                {
                    conRowsDone++;
                }
            }
            break;
        case '\r':
            break;
        case '\t':
            ch = ' ';
            /* fallthrough */
        default:
            /* GE's font tables are ASCII; a high byte indexes off the end of
             * fontchar[] and draws garbage (or worse). Strip like PD does. */
            if ((u8) ch < 0x20 || (u8) ch >= 0x7f)
            {
                ch = '?';
            }
            conBuf[conPrintRow][conPrintCol++] = ch;
            if (conPrintCol == CON_COLS)
            {
                conPrintCol = 0;
                conPrintRow = (conPrintRow + 1) % CON_ROWS;
                conBuf[conPrintRow][0] = '\0';
                if (conRowsDone < CON_ROWS - 1)
                {
                    conRowsDone++;
                }
            }
            break;
        }
        conBuf[conPrintRow][conPrintCol] = '\0';
    }

    if (conPrintRow != oldRow)
    {
        s32 advanced = (conPrintRow > oldRow)
            ? (conPrintRow - oldRow)
            : (conPrintRow + (CON_ROWS - oldRow));

        /* keep a scrolled view anchored to the same absolute rows while new
         * lines pile on at the tail (PD's behaviour) */
        if (conScrollOfs > 0)
        {
            conScrollOfs += advanced;
            if (conScrollOfs > conScrollLimit())
            {
                conScrollOfs = conScrollLimit();
            }
        }

        conRebuildVisRows();

        if (showmsg)
        {
            /* the transient area keeps its OWN copy of the rows this print
             * completed. Drawing the ring's newest rows let any silent line
             * (portConLogf, the "> cmd" echo, help/list output) that landed
             * during the dwell replace the flashed one. */
            s32 n = (advanced < CON_MSGROWS) ? advanced : CON_MSGROWS;
            s32 j;

            for (j = CON_MSGROWS - 1; j >= n; j--)
            {
                memcpy(conMsgBuf[j], conMsgBuf[j - n], sizeof(conMsgBuf[j]));
            }
            for (j = 0; j < n; j++)
            {
                s32 row = conPrintRow - 1 - j;
                while (row < 0)
                {
                    row += CON_ROWS;
                }
                memcpy(conMsgBuf[j], conBuf[row], sizeof(conMsgBuf[j]));
                conMsgBuf[j][CON_COLS] = '\0';
            }

            if (conMsgRows < CON_MSGROWS)
            {
                conMsgRows += advanced;
                if (conMsgRows > CON_MSGROWS)
                {
                    conMsgRows = CON_MSGROWS;
                }
            }
            conMsgTimer = osGetTime() + CON_MSGDWELL;
        }
    }

    conUnlock();
}

static void conPrintLn(s32 showmsg, const char *s)
{
    char tmp[512];

    snprintf(tmp, sizeof(tmp), "%s\n", s);
    portConPrint(showmsg, tmp);
}

void portConPrintf(s32 showmsg, const char *fmt, ...)
{
    char tmp[1024];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    tmp[sizeof(tmp) - 1] = '\0';
    portConPrint(showmsg, tmp);
}

/**
 * The port's logging call: writes to stderr like the fprintf it replaced, plus
 * a silent copy in the console. It doesn't pop up on screen because a stage
 * load logs dozens of lines; open the console to see them.
 */
void portConLogf(const char *fmt, ...)
{
    char tmp[2048];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    tmp[sizeof(tmp) - 1] = '\0';

    {
        extern int g_PortResim;   /* port_snap.c: a rollback re-simulates this tick; its lines are marked so a test can tell them from the first run's */

        if (g_PortResim)
        {
            fputs("[resim] ", stderr);
        }
    }
    fputs(tmp, stderr);
    portConPrint(0, tmp);
}

/* ---- commands ------------------------------------------------------------- */

/* port_options.c - the same bridge the Extended Options rows and the watch
 * pages step through, so a console `set` persists exactly like a menu change. */
extern s32         portOptCount(void);
extern s32         portOptFind(const char *key);
extern const char *portOptKey(s32 i);
extern const char *portOptSection(s32 i);
extern const char *portOptLabel(s32 i);
extern s32         portOptValueIni(s32 i, char *buf, s32 n);
extern s32         portOptSetIni(s32 i, const char *text);
extern s32         portOptRangeText(s32 i, char *buf, s32 n);
extern s32         portOptCategoryCount(void);
extern const char *portOptCategoryLabel(s32 cat);
extern s32         portOptCategoryOf(s32 i);

/* video.c - the midas.ini dispatcher, for the keys that are not option rows
 * (the keyboard / Port binds). Weak-stubbed headless. */
extern s32 portVideoIniSetKey(const char *key, const char *value);
extern s32 portVideoIniGetKey(const char *key, char *buf, s32 n);

static s32 conStrEqI(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0')
    {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb)
        {
            return 0;
        }
        a++;
        b++;
    }
    return *a == *b;
}

/* substring match, case-insensitive; empty needle matches everything */
static s32 conStrHasI(const char *hay, const char *needle)
{
    s32 i;

    if (needle == NULL || needle[0] == '\0')
    {
        return 1;
    }
    for (i = 0; hay[i] != '\0'; i++)
    {
        s32 j = 0;
        while (needle[j] != '\0')
        {
            char ca = hay[i + j], cb = needle[j];
            if (ca == '\0') return 0;
            if (ca >= 'A' && ca <= 'Z') ca += 32;
            if (cb >= 'A' && cb <= 'Z') cb += 32;
            if (ca != cb) break;
            j++;
        }
        if (needle[j] == '\0')
        {
            return 1;
        }
    }
    return 0;
}

static void conCmdHelp(void)
{
    conPrintLn(0, "client commands (cl_ = changes the game; saved to midas.ini):");
    conPrintLn(0, "  cl_<setting> <value>  set any setting - cl_vsync 0, cl_maxfps 144");
    conPrintLn(0, "  cl_<setting>          show it");
    conPrintLn(0, "  cl_showfps 0|1        the FPS / TPS strip");
    conPrintLn(0, "  cl_fov <deg>          field of view (16:9 horizontal); cl_fps = cl_maxfps");
    conPrintLn(0, "  cl_hitchms <ms>       log frames this slow as 'ge: hitch:' (0 = off, not saved)");
    conPrintLn(0, "  cl_fadedbg 0|1        log objects' fade / cull changes as 'ge: fade:'");
    conPrintLn(0, "console commands:");
    conPrintLn(0, "  list [text]           the cl_ settings matching key or label");
    conPrintLn(0, "  harness [text]        the GE_* diagnostic env knobs");
    conPrintLn(0, "  bot [off|info|stats]  summon / dismiss / report / scoreboard for the co-op bot");
    conPrintLn(0, "  mod [list|name [on|off|n]]  gameplay modifiers (GE_MODS, midas.ini mods=)");
    conPrintLn(0, "  net [info]            netplay: role, delay, ping, KB/s, loss/late, run, last hash tick, desync count");
    conPrintLn(0, "  net_graph 0|1|2|3     overlay: 1 numbers, 2 + graphs (ping, bytes, input wait, sim/render ms), 3 + sub-hash lights");
    conPrintLn(0, "  lua <code>            run Lua (weapon.set/get/reset, print); lua_reload re-runs Data/Scripts");
    conPrintLn(0, "  arena next|next+|back|back+   arena test jumps (see modding/arena.md)");
    conPrintLn(0, "  spawn guard [n] [dist]|clear  arena: hostile guards in a ring around Bond (netplay: both sides, around the issuer)");
    conPrintLn(0, "  give <item> [p1|p2] [ammo]    give an item by game name (give list); netplay: both sides");
    conPrintLn(0, "  diag [on|off|freeze]  the diagnostics layer (F3); freeze = no AI lists run (solo)");
    conPrintLn(0, "  ar [on|off|layers|<layer> on|off]  the AR specs (Shift+F3) and their layers");
    conPrintLn(0, "  clear                 empty the scrollback");
    conPrintLn(0, "  help                  this");
    conPrintLn(0, "keys: PgUp/PgDn/Home/End scroll, Up/Down recall");
}

/* Client commands: cl_<key> reads or sets any setting by its midas.ini key (plus a few friendly names
 * and cl_hitchms); the older set / get still work, but help lists only the cl_ forms. */
static const char *const sConClAlias[][2] = {
    { "showfps", "show_fps" },
    { "fov",     "hfov_wide" },
    { "fps",     "maxfps" },
};

static void conCmdGet(const char *key);
static void conCmdSet(const char *key, const char *value);

/* hfov_wide reads 90 for the N64's view, whose true 16:9 width is 91.5
 * (video.c videoHfovWide): the console shows the truth beside it. Empty for
 * every other key and every value that is already true. */
static const char *conFovNote(const char *key, const char *shown, char *note, s32 n)
{
    extern s32 portVideoHfovWideTrueTenths(void);   /* video.c; weak 0 headless */
    const s32 t = portVideoHfovWideTrueTenths();

    note[0] = '\0';
    if (conStrEqI(key, "hfov_wide") && t > 0 && t != atoi(shown) * 10)
    {
        snprintf(note, n, " (True %d.%d)", (int) (t / 10), (int) (t % 10));
    }
    return note;
}

static void conCmdClient(const char *name, const char *value)
{
    extern s32 portVideoOptionGet(const char *key, s32 *out);   /* video.c */
    extern s32 portVideoOptionSet(const char *key, s32 value);
    extern s32 portIniWriteStrs(const char *section, const char *const *keys, const char *const *vals, s32 n);
    const char *key = name;
    char buf[128];
    s32 live;
    u32 a;

    if (name[0] == '\0')
    {
        conPrintLn(0, "usage: cl_<setting> [value] - try: list");
        return;
    }
    if (conStrEqI(name, "fadedbg"))
    {   /* propobj.c: log each object's visibility verdict as it changes */
        extern void portFadeDbgSet(s32 on);
        extern s32 g_PortFadeDbg;

        if (value != NULL)
        {
            portFadeDbgSet(atoi(value) != 0);
        }
        portConPrintf(value != NULL, "cl_fadedbg = %d (log: 'ge: fade:' lines)\n", (int) g_PortFadeDbg);
        return;
    }
    if (conStrEqI(name, "hitchms"))
    {
        extern int portHitchMs(void);          /* hitchlog.c; weak 0 headless */
        extern void portHitchSetMs(int ms);

        if (value != NULL)
        {
            portHitchSetMs(atoi(value));
        }
        portConPrintf(value != NULL, "cl_hitchms = %d%s\n", portHitchMs(), portHitchMs() == 0 ? " (off)" : " ms");
        return;
    }
    for (a = 0; a < sizeof(sConClAlias) / sizeof(sConClAlias[0]); a++)
    {
        if (conStrEqI(name, sConClAlias[a][0]))
        {
            key = sConClAlias[a][1];
        }
    }

    /* a live [display] key with no option-table row: the video
     * bridge sets it, and the ini gets the line set would have written */
    if (portOptFind(key) < 0 && portVideoOptionGet(key, &live))
    {
        if (value == NULL)
        {
            portConPrintf(0, "cl_%s = %d\n", name, (int) live);
            return;
        }
        if (!portVideoOptionSet(key, atoi(value)))
        {
            portConPrintf(1, "bad value \"%s\" for cl_%s\n", value, name);
            return;
        }
        portVideoOptionGet(key, &live);
        snprintf(buf, sizeof(buf), "%d", (int) live);
        {
            const char *k[1] = { key };
            const char *v[1] = { buf };

            portIniWriteStrs("[display]", k, v, 1);
        }
        portConPrintf(1, "cl_%s = %s\n", name, buf);
        return;
    }
    if (value == NULL)
    {
        conCmdGet(key);
    }
    else
    {
        conCmdSet(key, value);
    }
}

static void conCmdGet(const char *key)
{
    char buf[128];
    s32 i;

    if (key == NULL || key[0] == '\0')
    {
        conPrintLn(0, "usage: get <key>");
        return;
    }

    i = portOptFind(key);
    if (i >= 0)
    {
        char range[96];

        if (!portOptValueIni(i, buf, sizeof(buf)))
        {
            portConPrintf(0, "%s = (not available in this build)\n", portOptKey(i));
            return;
        }
        char note[32];

        range[0] = '\0';
        portOptRangeText(i, range, sizeof(range));
        portConPrintf(0, "%s %s = %s%s%s%s\n", portOptSection(i), portOptKey(i), buf,
                      conFovNote(portOptKey(i), buf, note, sizeof(note)),
                      range[0] != '\0' ? "   " : "", range);
        return;
    }

    if (portVideoIniGetKey(key, buf, sizeof(buf)))
    {
        portConPrintf(0, "%s = %s\n", key, buf);
        return;
    }

    portConPrintf(0, "unknown key \"%s\" - try: list\n", key);
}

static void conCmdSet(const char *key, const char *value)
{
    s32 i;

    if (key == NULL || key[0] == '\0' || value == NULL || value[0] == '\0')
    {
        conPrintLn(0, "usage: set <key> <value>");
        return;
    }

    i = portOptFind(key);
    if (i >= 0)
    {
        if (portOptSetIni(i, value))
        {
            char buf[128];
            char note[32];
            buf[0] = '\0';
            portOptValueIni(i, buf, sizeof(buf));
            portConPrintf(1, "%s = %s%s\n", portOptKey(i), buf,
                          conFovNote(portOptKey(i), buf, note, sizeof(note)));
        }
        else
        {
            char range[96];
            range[0] = '\0';
            portOptRangeText(i, range, sizeof(range));
            portConPrintf(1, "bad value \"%s\" for %s%s%s\n", value, portOptKey(i),
                          range[0] != '\0' ? " - " : "", range);
        }
        return;
    }

    {
        const s32 r = portVideoIniSetKey(key, value);

        if (r < 0)
        {
            portConPrintf(1, "bad value \"%s\" for %s - use SDL key names, comma separated\n",
                          value, key);
            return;
        }
        if (r > 0)
        {
            char now[128];
            /* tickhz is derived: echo the rate that took, not the one typed */
            portConPrintf(1, "%s = %s\n", key,
                          (conStrEqI(key, "tickhz") && portVideoIniGetKey(key, now, sizeof(now))) ? now : value);
            return;
        }
    }

    portConPrintf(1, "unknown key \"%s\" - try: list\n", key);
}

/**
 * Lists the settings grouped by the option table's category field (its only
 * remaining use since the Extended Options menu was removed).
 */
static void conCmdList(const char *filter)
{
    const s32 cats = portOptCategoryCount();
    const s32 n = portOptCount();
    s32 cat;
    s32 i;
    s32 shown = 0;

    for (cat = 0; cat < cats; cat++)
    {
        s32 head = 0;

        for (i = 0; i < n; i++)
        {
            const char *key = portOptKey(i);
            const char *label = portOptLabel(i);
            char buf[128];

            if (key == NULL || portOptCategoryOf(i) != cat)
            {
                continue;
            }
            if (!conStrHasI(key, filter) && !(label != NULL && conStrHasI(label, filter)))
            {
                continue;
            }
            if (!head)
            {
                char name[32];
                s32 k;

                head = 1;
                /* the table's labels carry a trailing newline (the watch needs
                 * it); trim it or the bracket lands on the next line */
                for (k = 0; k < (s32) sizeof(name) - 1; k++)
                {
                    char c = portOptCategoryLabel(cat)[k];
                    if (c == '\0' || c == '\n')
                    {
                        break;
                    }
                    name[k] = c;
                }
                name[k] = '\0';
                portConPrintf(0, "[%s]\n", name);
            }
            if (!portOptValueIni(i, buf, sizeof(buf)))
            {
                snprintf(buf, sizeof(buf), "n/a");
            }
            portConPrintf(0, "  cl_%-19s %s\n", key, buf);
            shown++;
        }
    }
    portConPrintf(0, "%d setting(s)\n", shown);
}

static void conCmdHarness(const char *filter)
{
    s32 n = (s32) (sizeof(sPortConEnvNames) / sizeof(sPortConEnvNames[0]));
    s32 i;
    s32 shown = 0;
    s32 set = 0;

    for (i = 0; i < n; i++)
    {
        const char *name = sPortConEnvNames[i];
        const char *v = getenv(name);

        if (!conStrHasI(name, filter))
        {
            continue;
        }
        if (v != NULL)
        {
            portConPrintf(0, "%-26s = %s\n", name, v);
            set++;
        }
        else if (filter != NULL && filter[0] != '\0')
        {
            portConPrintf(0, "%-26s   (unset)\n", name);
        }
        shown++;
    }
    if (filter == NULL || filter[0] == '\0')
    {
        portConPrintf(0, "%d of %d GE_* knob(s) set; `harness <text>` lists matches\n", set, shown);
    }
}

static void conExecute(char *line)
{
    char *cmd;
    char *arg1 = NULL;
    char *arg2 = NULL;
    char *s = line;
    s32 n = (s32) strlen(line);

    /* trailing blanks off the whole line, so `set vsync on ` sets `on`;
     * the inner spaces of a key list in arg2 are kept */
    while (n > 0 && (line[n - 1] == ' ' || line[n - 1] == '\t'))
    {
        line[--n] = '\0';
    }

    while (*s == ' ' || *s == '\t') s++;
    if (*s == '\0')
    {
        return;
    }

    cmd = s;
    while (*s != '\0' && *s != ' ' && *s != '\t') s++;
    if (*s != '\0')
    {
        *s++ = '\0';
        while (*s == ' ' || *s == '\t') s++;
        if (*s != '\0')
        {
            arg1 = s;
            while (*s != '\0' && *s != ' ' && *s != '\t') s++;
            if (*s != '\0')
            {
                *s++ = '\0';
                while (*s == ' ' || *s == '\t') s++;
                if (*s != '\0')
                {
                    arg2 = s; /* rest of the line, spaces kept (key lists) */
                }
            }
        }
    }

    {   /* Netplay job 3b: every command below that changes the game changes it on THIS machine only, while the partner simulates
         * the same Bonds. Reading (get / list / harness / help) and the display-only cl_ settings stay available. */
        extern int portNetBlocked(const char *what);
        static const char *const okCl[] = { "showfps", "show_fps", "fps", "maxfps", "vsync", "hitchms", "fadedbg", NULL };
        const int isCl = (cmd[0] == 'c' || cmd[0] == 'C') && (cmd[1] == 'l' || cmd[1] == 'L') && cmd[2] == '_';
        const char *clName = isCl ? cmd + 3 : (conStrEqI(cmd, "set") && arg1 != NULL ? arg1 : NULL);
        const int readOnlyArg = arg1 == NULL || conStrEqI(arg1, "list") || conStrEqI(arg1, "info") || conStrEqI(arg1, "stats");
        int mutate = conStrEqI(cmd, "lua") || conStrEqI(cmd, "lua_reload") || conStrEqI(cmd, "arena")
                     || ((conStrEqI(cmd, "bot") || conStrEqI(cmd, "coop") || conStrEqI(cmd, "mod")) && !readOnlyArg);

        if (clName != NULL && (isCl ? arg1 != NULL : arg2 != NULL))   /* cl_<key> <value>, set <key> <value>: a write */
        {
            int k, ok = 0;

            for (k = 0; okCl[k] != NULL; k++)
            {
                ok |= conStrEqI(clName, okCl[k]);
            }
            mutate |= !ok;
        }
        if (mutate)
        {
            char what[64];

            snprintf(what, sizeof(what), "console command '%s'", cmd);
            if (portNetBlocked(what))
            {
                portConPrintf(0, "%s: not available while netplay is on (it would change this machine's game only)\n", cmd);
                return;
            }
        }
    }
    if (conStrEqI(cmd, "help") || conStrEqI(cmd, "?"))
    {
        conCmdHelp();
    }
    else if (conStrEqI(cmd, "set"))
    {
        conCmdSet(arg1, arg2);
    }
    else if (conStrEqI(cmd, "get"))
    {
        conCmdGet(arg1);
    }
    else if (conStrEqI(cmd, "list"))
    {
        conCmdList(arg1);
    }
    else if (conStrEqI(cmd, "harness"))
    {
        conCmdHarness(arg1);
    }
    else if ((cmd[0] == 'c' || cmd[0] == 'C') && (cmd[1] == 'l' || cmd[1] == 'L') && cmd[2] == '_')
    {   /* cl_<setting> [value]: the value is the rest of the line (a key list keeps its spaces) */
        char value[160];

        if (arg1 != NULL && arg2 != NULL)
        {
            snprintf(value, sizeof(value), "%s %s", arg1, arg2);
        }
        else if (arg1 != NULL)
        {
            snprintf(value, sizeof(value), "%s", arg1);
        }
        conCmdClient(cmd + 3, arg1 != NULL ? value : NULL);
    }
    else if (conStrEqI(cmd, "bot"))
    {
        extern void portBotConsole(const char *arg, const char *arg2);

        portBotConsole(arg1, arg2);
    }
    else if (conStrEqI(cmd, "coop"))
    {
        extern void portCoopConsole(const char *arg1, const char *arg2);   /* port_coop.c */

        portCoopConsole(arg1, arg2);
    }
    else if (conStrEqI(cmd, "net"))
    {
        extern void portNetConsole(const char *arg1);   /* port_net.c (job 4) */

        portNetConsole(arg1);
    }
    else if (conStrEqI(cmd, "net_graph"))
    {
        extern int portNetGraphLevel(void);   /* port_netstats.c (job 6) */
        extern void portNetGraphSet(int level);

        if (arg1 != NULL && arg1[0] >= '0' && arg1[0] <= '3' && arg1[1] == '\0')
        {
            portNetGraphSet(arg1[0] - '0');
        }
        else if (arg1 != NULL)
        {
            portConPrintf(0, "usage: net_graph 0|1|2|3   (0 off, 1 numbers, 2 + graphs, 3 + sub-hash lights)\n");
            return;
        }
        portConPrintf(0, "net_graph %d\n", portNetGraphLevel());
    }
    else if (conStrEqI(cmd, "mod"))
    {
        extern void portModifierConsole(const char *arg1, const char *arg2);

        portModifierConsole(arg1, arg2);
    }
    else if (conStrEqI(cmd, "music"))
    {
        extern void portMusicConsole(const char *arg1);   /* port_music.c */

        portMusicConsole(arg1);
    }
    else if (conStrEqI(cmd, "lua"))
    {
        extern void portLuaConsole(const char *arg1, const char *arg2);

        portLuaConsole(arg1, arg2);
    }
    else if (conStrEqI(cmd, "arena"))
    {
        extern void portLuaTestKey(const char *name);

        if (arg1 != NULL && (conStrEqI(arg1, "next") || conStrEqI(arg1, "next+") || conStrEqI(arg1, "back") || conStrEqI(arg1, "back+") ||
                             conStrEqI(arg1, "callout") || conStrEqI(arg1, "callout+") || conStrEqI(arg1, "fog") ||
                             conStrEqI(arg1, "rain") || conStrEqI(arg1, "strike") || conStrEqI(arg1, "powerup") || conStrEqI(arg1, "credits")))
        {
            portLuaTestKey(arg1);
        }
        else
        {
            portConPrintf(0, "usage: arena next|next+|back|back+|callout|callout+|fog|rain|strike|powerup|credits   (+= stack on the live enemies / previous callout; "
                             "needs test_keys or GE_ARENA_TESTKEYS=1)\n");
        }
    }
    else if (conStrEqI(cmd, "lua_reload"))
    {
        extern void portLuaReload(void);

        portLuaReload();
    }
    else if (conStrEqI(cmd, "give"))
    {
        extern void portGiveConsole(const char *arg1, const char *rest);   /* port_netcmd.c */

        portGiveConsole(arg1, arg2);
    }
    else if (conStrEqI(cmd, "spawn"))
    {
        extern void portArenaConsole(const char *sub, const char *rest);

        portArenaConsole(arg1, arg2);
    }
    else if (conStrEqI(cmd, "crosshair"))
    {
        extern void portLuaXhairConsole(const char *a1, const char *a2);   /* port_luavm_crosshair.c (presentation only) */

        portLuaXhairConsole(arg1, arg2);
    }
    else if (conStrEqI(cmd, "diag") || conStrEqI(cmd, "ar"))
    {
        extern void portArConsole(const char *cmd, const char *a1, const char *a2);   /* port_arhud.c */

        portArConsole(cmd, arg1, arg2);
    }
    else if (conStrEqI(cmd, "clear"))
    {
        if (conLockTry())
        {
            memset(conBuf, 0, sizeof(conBuf));
            conPrintRow = conPrintCol = 0;
            conScrollOfs = 0;
            conRowsDone = 0;
            conMsgRows = 0;
            conRebuildVisRows();
            conUnlock();
        }
    }
    else
    {
        portConPrintf(0, "unknown command \"%s\" - try: help\n", cmd);
    }
}

/* ---- input ---------------------------------------------------------------- */

/* The scrollback view moves under conLock: portConPrint, on any thread,
 * re-anchors the same offset (conScrollOfs += advanced) and rebuilds
 * conVisRows. step is clamped, so +/-CON_ROWS mean Home / the tail. */
static void conScroll(s32 step)
{
    s32 newOfs;

    if (!conLockTry())
    {
        return;
    }
    newOfs = conScrollOfs + step;
    if (newOfs < 0) newOfs = 0;
    if (newOfs > conScrollLimit()) newOfs = conScrollLimit();
    if (newOfs != conScrollOfs)
    {
        conScrollOfs = newOfs;
        conRebuildVisRows();
    }
    conUnlock();
}

s32 portConIsOpen(void)
{
    return conOpen;
}

void portConToggle(void)
{
    if (!conInited)
    {
        portConInit();
    }
    conOpen = !conOpen;
    if (conOpen)
    {
        conInput[0] = '\0';
        conInputCol = 0;
        conHistoryPos = -1;
    }
    else
    {
        conScroll(-CON_ROWS); /* back to the tail */
    }
}

void portConClose(void)
{
    if (conOpen)
    {
        portConToggle();
    }
}

static void conRecallHistory(s32 dir)
{
    s32 pos;

    if (conHistoryCount == 0)
    {
        return;
    }
    pos = conHistoryPos + dir;
    if (pos < -1)
    {
        pos = -1;
    }
    if (pos >= conHistoryCount)
    {
        pos = conHistoryCount - 1;
    }
    conHistoryPos = pos;
    if (pos < 0)
    {
        conInput[0] = '\0';
    }
    else
    {
        snprintf(conInput, sizeof(conInput), "%s", conHistory[pos]);
    }
    conInputCol = (s32) strlen(conInput);
}

void portConKey(s32 key)
{
    if (!conOpen)
    {
        return;
    }

    switch (key)
    {
    case PORTCON_KEY_ESCAPE:
        portConToggle();
        return;
    case PORTCON_KEY_BACKSPACE:
        if (conInputCol > 0)
        {
            conInput[--conInputCol] = '\0';
        }
        return;
    case PORTCON_KEY_UP:
        conRecallHistory(+1);
        return;
    case PORTCON_KEY_DOWN:
        conRecallHistory(-1);
        return;
    case PORTCON_KEY_ENTER:
        if (conInput[0] != '\0')
        {
            char work[CON_COLS + 1];
            s32 h;

            portConPrintf(0, "> %s\n", conInput);
            /* history: newest first, no immediate duplicate */
            if (conHistoryCount == 0 || strcmp(conHistory[0], conInput) != 0)
            {
                for (h = CON_HISTORY - 1; h > 0; h--)
                {
                    memcpy(conHistory[h], conHistory[h - 1], sizeof(conHistory[0]));
                }
                snprintf(conHistory[0], sizeof(conHistory[0]), "%s", conInput);
                if (conHistoryCount < CON_HISTORY)
                {
                    conHistoryCount++;
                }
            }
            /* conExecute writes NULs into its copy, never into the history */
            snprintf(work, sizeof(work), "%s", conInput);
            conExecute(work);
        }
        conInput[0] = '\0';
        conInputCol = 0;
        conHistoryPos = -1;
        conScroll(-CON_ROWS);
        return;
    default:
        break;
    }

    switch (key)
    {
    case PORTCON_KEY_PAGEUP:   conScroll(+CON_SCROLLSTEP); break;
    case PORTCON_KEY_PAGEDOWN: conScroll(-CON_SCROLLSTEP); break;
    case PORTCON_KEY_HOME:     conScroll(+CON_ROWS);       break; /* clamps to conScrollLimit() */
    case PORTCON_KEY_END:      conScroll(-CON_ROWS);       break; /* clamps to 0 */
    default: break;
    }
}

void portConTextChar(s32 ch)
{
    if (!conOpen || ch < 0x20 || ch >= 0x7f)
    {
        return;
    }
    /* US backstop; input_pc.c swallows the console key's own text on every layout */
    if (ch == '`' || ch == '~')
    {
        return;
    }
    if (conInputCol >= CON_COLS)
    {
        return;
    }
    conInput[conInputCol++] = (char) ch;
    conInput[conInputCol] = '\0';
    conHistoryPos = -1;
}

/* ---- render --------------------------------------------------------------- */

/* The console draws in its own 2D space: 480 lines by the window's full width
 * (video.c portConsoleSpace), bracketed by G_CONSOLE2D_EXT so the renderer
 * skips the 4:3 pin; the world and HUD stay at 320x240. */
static s32 conSpaceW = 640, conSpaceH = 480;
static s16 conSavedViX, conSavedViY;   /* the frame's VI size, while the console borrows it */
#define CON_SPACE(gdl, w, h) do { (gdl)->words.w0 = (u32) 0x4c << 24 /* G_CONSOLE2D_EXT */; \
                                  (gdl)->words.w1 = ((u32) (w) << 16) | (u32) (h); (gdl)++; } while (0)

static Gfx *conSpaceBegin(Gfx *gdl)
{
    extern void portConsoleSpace(s32 *w, s32 *h);   /* video.c; weak 640x480 headless */

    portConsoleSpace(&conSpaceW, &conSpaceH);
    /* the text and rect code clip against the VI size (textRenderGlyph's
     * *x <= viGetX()): it is the console's space while the console draws,
     * and the frame's again at conSpaceEnd - the DL is built, nothing else
     * reads it in between */
    conSavedViX = viGetX();
    conSavedViY = viGetY();
    viSetXY((s16) conSpaceW, (s16) conSpaceH);
    CON_SPACE(gdl, conSpaceW, conSpaceH);
    gdl = microcode_constructor(gdl);
    gDPSetScissor(gdl++, G_SC_NON_INTERLACE, 0, 0, conSpaceW, conSpaceH);   /* microcode_constructor's is the frame's 320x240 */
    return gdl;
}

static Gfx *conSpaceEnd(Gfx *gdl)
{
    gdl = combiner_bayer_lod_perspective(gdl);
    CON_SPACE(gdl, 0, 0);
    viSetXY(conSavedViX, conSavedViY);
    return gdl;
}

static s32 conTextWidth(const char *text)
{
    s32 h = 0, w = 0;

    textMeasure(&h, &w, (char *) text, ptrFontBankGothicChars, ptrFontBankGothic, 0);
    return w;
}

/** How many characters of text fit in maxw: the longest prefix that does
 *  (binary search on its measured width), pulled back to just after a space
 *  when that leaves at least half the line - so words break whole. At least
 *  one character, so a line always advances. */
static s32 conFitLen(const char *text, s32 maxw)
{
    const s32 len = (s32) strlen(text);
    char buf[CON_COLS + 1];
    s32 lo = 1, hi = len, k;

    if (len <= 1 || conTextWidth(text) <= maxw)
    {
        return len;
    }
    while (lo < hi)
    {
        const s32 mid = (lo + hi + 1) / 2;

        memcpy(buf, text, (size_t) mid);
        buf[mid] = '\0';
        if (conTextWidth(buf) <= maxw)
        {
            lo = mid;
        }
        else
        {
            hi = mid - 1;
        }
    }
    for (k = lo; k > lo / 2; k--)
    {
        if (text[k] == ' ')
        {
            return k;
        }
    }
    return lo;
}

static Gfx *conDrawRow(Gfx *gdl, s32 x, s32 y, const char *text, u32 colour)
{
    s32 tx = x;
    s32 ty = y;

    if (text == NULL || text[0] == '\0')
    {
        return gdl;
    }
    /* no master-DL room for this row: drop it, never overrun. Gated on the
     * row's own length (the worst case for 120 columns would drop rows for
     * nothing) */
    if (dynGetFreeGfx2(gdl) < CON_GFX_ROWLEN(strlen(text)) + CON_GFX_TAIL)
    {
        return gdl;
    }
    return textRender(gdl, &tx, &ty, (char *) text, ptrFontBankGothicChars, ptrFontBankGothic,
                      colour, conSpaceW, conSpaceH, 0, 0);
}

/**
 * At the first rendered frame, GE_CONSOLE=1 opens the console and
 * GE_CONCMD="<line>[;<line>...]" runs those lines once. These are for automated
 * testing and screenshots without a keyboard, and are environment variables
 * only, never midas.ini keys.
 */
static void conEnvHarness(void)
{
    static s32 done;
    const char *cmds;
    const char *e;

    if (done)
    {
        return;
    }
    done = 1;

    e = getenv("GE_CONSOLE");
    if (e != NULL && e[0] != '0')
    {
        conOpen = 1;
    }

    cmds = getenv("GE_CONCMD");
    if (cmds != NULL && cmds[0] != '\0')
    {
        char work[512];
        char *p;
        char *start;

        snprintf(work, sizeof(work), "%s", cmds);
        start = work;
        for (p = work; ; p++)
        {
            if (*p == ';' || *p == '\0')
            {
                s32 last = (*p == '\0');
                *p = '\0';
                if (start[0] != '\0')
                {
                    portConPrintf(0, "> %s", start);
                    portConPrint(0, "\n");
                    conExecute(start);
                }
                if (last)
                {
                    break;
                }
                start = p + 1;
            }
        }
    }
}

/* Runs one console line as if typed (test hooks: GE_NET_TEST_CMD). */
void portConExec(const char *line)
{
    char work[256];

    snprintf(work, sizeof(work), "%s", line);
    portConPrintf(0, "> %s", work);
    portConPrint(0, "\n");
    conExecute(work);
}

/**
 * Netplay (port_net.c, boss.c): a whole frame that is only a line of text on black, for the moments the sim is held
 * (handshake, the partner's stage load, a long wait for its input). Self-contained: the caller starts from an empty
 * master list, so nothing of the game's frame is needed. Returns gdl untouched when the fonts or the console are not up.
 */
Gfx *portConNotice(Gfx *gdl, const char *text)
{
    s32 x, y;

    if (text == NULL || text[0] == '\0' || !conInited || ptrFontBankGothic == NULL || ptrFontBankGothicChars == NULL)
    {
        return gdl;
    }
    if (dynGetFreeGfx2(gdl) < CON_GFX_SETUP + CON_GFX_TAIL + CON_GFX_ROWLEN(strlen(text)))
    {
        return gdl;
    }
    gdl = conSpaceBegin(gdl);
    gdl = microcode_constructor_related_to_menus(gdl, 0, 0, conSpaceW, conSpaceH, 0xFF);   /* opaque black */
    x = (conSpaceW - conTextWidth(text)) / 2;
    y = conSpaceH / 2 - CON_ROW_PITCH;
    gdl = conDrawRow(gdl, x < CON_TEXT_X ? CON_TEXT_X : x, y, text, CON_COL_TEXT);
    return conSpaceEnd(gdl);
}

/* ---- net_graph (netplay job 6) ------------------------------------------------------------------------------------------------
 * A Counter-Strike style network / frame-time overlay in the console's own 2D space (480 lines by the window's width, so it scales
 * with any window). Render only: it reads the stats ring (port_netstats.c) and port_net.c's counters, never sim state.
 *   1: numbers (ping, KB/s, packets/s, loss/late, delay, tick, last hash check, desyncs)
 *   2: + scrolling bar graphs of the last ~4 s: ping, bytes in/out, input wait, sim + render ms; yellow = a tick that waited, red = DESYNC
 *   3: + the 12 sub-hash fields as status lights (red = that field differed in the last failed check)
 * Two passes (all rects, then all text) because text and rects need different combiner state. */
#include "port_netstats.h"

#define NG_GRAPH_W   NETSTAT_HIST      /* one unit per sample (the console font is about 7 units per character) */
#define NG_PANEL_W   (NG_GRAPH_W + 8)
#define NG_GRAPH_H   34
#define NG_COL_GREEN  0x60FF60FFu
#define NG_COL_YELLOW 0xFFE040FFu
#define NG_COL_RED    0xFF4848FFu
#define NG_COL_CYAN   0x50E8FFFFu
#define NG_COL_WHITE  0xFFFFFFFFu
#define NG_COL_GRAY   0x9A9A9AFFu
#define NG_COL_ORANGE 0xFFA030FFu
#define NG_RESERVE_TEXT 2600           /* Gfx kept free for the text pass */

static u32 ngLastPrim;
static s32 ngHavePrim;

static Gfx *ngRect(Gfx *gdl, s32 x0, s32 y0, s32 x1, s32 y1, u32 rgba)
{
    if (x1 <= x0 || y1 <= y0)
    {
        return gdl;
    }
    if (!ngHavePrim || rgba != ngLastPrim)
    {
        gDPSetPrimColor(gdl++, 0, 0, (rgba >> 24) & 0xFF, (rgba >> 16) & 0xFF, (rgba >> 8) & 0xFF, rgba & 0xFF);
        ngLastPrim = rgba;
        ngHavePrim = 1;
    }
    gDPFillRectangle(gdl++, x0, y0, x1, y1);
    return gdl;
}

static u32 ngPingColour(s32 ms)
{
    return ms < 60 ? NG_COL_GREEN : ms < 120 ? NG_COL_YELLOW : NG_COL_RED;
}

/* graph kinds */
enum { NG_PING = 0, NG_BYTES, NG_WAIT, NG_MS, NG_FRAME };

static f32 ngValue(const PortNetStatSample *s, s32 kind)
{
    switch (kind)
    {
    case NG_PING:  return (f32) s->pingMs;
    case NG_BYTES: return (f32) s->inBytes;
    case NG_WAIT:  return s->waitMs;
    case NG_FRAME: return s->frameMs;
    default:       return s->simMs + s->renderMs;
    }
}

/* the scale of a graph: the largest visible value, at least `minv`, rounded up to a tidy number */
static f32 ngScale(s32 kind, f32 minv)
{
    f32 m = minv;
    s32 i;
    const s32 n = portNetStatsCount() < NG_GRAPH_W ? portNetStatsCount() : NG_GRAPH_W;

    for (i = 0; i < n; i++)
    {
        const PortNetStatSample *s = portNetStatsBack(i);
        f32 v = ngValue(s, kind);

        if (kind == NG_BYTES && (f32) s->outBytes > v)
        {
            v = (f32) s->outBytes;
        }
        if (kind == NG_PING && (f32) (s->pingMs + s->jitterMs) > v)
        {
            v = (f32) (s->pingMs + s->jitterMs);
        }
        if (v > m)
        {
            m = v;
        }
    }
    if (m > 1000.0f) { m = (f32) ((s32) (m / 500.0f) + 1) * 500.0f; }
    else if (m > 100.0f) { m = (f32) ((s32) (m / 50.0f) + 1) * 50.0f; }
    else if (m > 10.0f) { m = (f32) ((s32) (m / 10.0f) + 1) * 10.0f; }
    return m;
}

/* one graph box; the rects only (labels are in the text pass) */
static Gfx *ngGraphRects(Gfx *gdl, s32 x, s32 y, s32 kind, f32 scale)
{
    const s32 n = portNetStatsCount() < NG_GRAPH_W ? portNetStatsCount() : NG_GRAPH_W;
    s32 i;

    gdl = ngRect(gdl, x - 1, y - 1, x + NG_GRAPH_W + 1, y + NG_GRAPH_H + 1, 0x00000098u);   /* the box */
    gdl = ngRect(gdl, x, y + NG_GRAPH_H / 2, x + NG_GRAPH_W, y + NG_GRAPH_H / 2 + 1, 0x60606060u);   /* half-scale guide */
    if (kind == NG_MS || kind == NG_FRAME)
    {   /* the 60 Hz frame budget (16.7 ms) */
        const s32 gy = y + NG_GRAPH_H - (s32) (16.7f / scale * (f32) NG_GRAPH_H);

        if (gy > y)
        {
            gdl = ngRect(gdl, x, gy, x + NG_GRAPH_W, gy + 1, 0x40C0FF70u);
        }
    }
    for (i = 0; i < n; i++)
    {
        const PortNetStatSample *s = portNetStatsBack(i);
        const s32 cx = x + NG_GRAPH_W - 1 - i;
        f32 v;
        s32 h;

        if (dynGetFreeGfx2(gdl) < NG_RESERVE_TEXT)
        {
            break;
        }
        if (s->flags & NSF_HASH_BAD)
        {   /* a failed hash check: a red column through the whole graph */
            gdl = ngRect(gdl, cx, y, cx + 2, y + NG_GRAPH_H, 0xFF202090u);
        }
        switch (kind)
        {
        case NG_PING:
            v = (f32) s->pingMs;
            h = (s32) (v / scale * (f32) NG_GRAPH_H);
            gdl = ngRect(gdl, cx, y + NG_GRAPH_H - h, cx + 1, y + NG_GRAPH_H, ngPingColour(s->pingMs));
            if (s->jitterMs > 0)
            {   /* the jitter band on top of the ping, dimmer */
                const s32 jh = (s32) ((f32) s->jitterMs / scale * (f32) NG_GRAPH_H);

                gdl = ngRect(gdl, cx, y + NG_GRAPH_H - h - jh, cx + 1, y + NG_GRAPH_H - h, 0xFFFFFF55u);
            }
            break;
        case NG_BYTES:
            h = (s32) ((f32) s->inBytes / scale * (f32) NG_GRAPH_H);
            gdl = ngRect(gdl, cx, y + NG_GRAPH_H - h, cx + 1, y + NG_GRAPH_H, 0x40D040E0u);
            h = (s32) ((f32) s->outBytes / scale * (f32) NG_GRAPH_H);
            gdl = ngRect(gdl, cx, y + NG_GRAPH_H - h - 1, cx + 1, y + NG_GRAPH_H - h + 1, NG_COL_CYAN);
            break;
        case NG_WAIT:
            v = s->waitMs;
            h = (s32) (v / scale * (f32) NG_GRAPH_H);
            if (h < 2 && (s->flags & NSF_WAITED))
            {
                h = 2;
            }
            gdl = ngRect(gdl, cx, y + NG_GRAPH_H - h, cx + 1, y + NG_GRAPH_H, v > 33.0f ? NG_COL_RED : NG_COL_YELLOW);
            break;
        case NG_FRAME:
            h = (s32) (s->frameMs / scale * (f32) NG_GRAPH_H);
            gdl = ngRect(gdl, cx, y + NG_GRAPH_H - h, cx + 1, y + NG_GRAPH_H, s->frameMs > 20.0f ? NG_COL_YELLOW : 0x60FF60E0u);
            break;
        default:
            h = (s32) (s->simMs / scale * (f32) NG_GRAPH_H);
            gdl = ngRect(gdl, cx, y + NG_GRAPH_H - h, cx + 1, y + NG_GRAPH_H, 0x3C9CFFE0u);
            {
                const s32 rh = (s32) (s->renderMs / scale * (f32) NG_GRAPH_H);

                gdl = ngRect(gdl, cx, y + NG_GRAPH_H - h - rh, cx + 1, y + NG_GRAPH_H - h, s->simMs + s->renderMs > 33.4f ? NG_COL_RED : NG_COL_ORANGE);
            }
            break;
        }
        if (s->flags & NSF_WAITED)
        {   /* a tick that waited for the partner: a yellow marker on the top edge */
            gdl = ngRect(gdl, cx, y, cx + 1, y + 4, NG_COL_YELLOW);
        }
        if (s->flags & NSF_HASH_OK)
        {   /* a passed hash check: a green dot on the bottom edge */
            gdl = ngRect(gdl, cx, y + NG_GRAPH_H - 2, cx + 1, y + NG_GRAPH_H, 0x30FF30FFu);
        }
    }
    return gdl;
}

/* a label on the left and a right-aligned value in the same row, inside the graph box (text pass) */
static Gfx *ngLabel(Gfx *gdl, s32 x, s32 y, const char *left, u32 lcol, const char *right)
{
    gdl = conDrawRow(gdl, x + 3, y + 1, left, lcol);
    if (right != NULL)
    {
        gdl = conDrawRow(gdl, x + NG_GRAPH_W - 3 - conTextWidth(right), y + 1, right, NG_COL_GRAY);
    }
    return gdl;
}

/* ---- arena pickup markers (drop A7; v2 drop A8, settings marker_* in the arena script) ---------------------------------------------
 * AC's playtest: the A7 beacons over every pickup got messy and slow with knife throwers and uncollected drops. v2: the good stuff (centre
 * ammo box, armour, crowd gifts) gets a small diamond + a short label "AMMO 12m", fades out marker_fade seconds after it appears and comes
 * back within marker_near; dropped guns get a faint dot only within marker_drop_near (or nothing); thrown knives nothing by default; markers
 * that land within marker_merge px of each other on screen become one with a count; off-screen edge diamonds only for the good stuff. The
 * cost is fixed: at most PORT_MARKER_MAX projections and marker_max groups drawn. Drawn in the console's 2D space from the list the sim
 * published (port_arenamode.c); presentation only. The projection is the player's own: view-to-world rows give view-space x right, y up and
 * z toward the viewer (the camera looks down -z), c_scalex/y the pixels-per-slope of the 3D viewport. */
#include "port_arenamarkers.h"
#include "game/matrixmath.h"
#include "game/player.h"

typedef struct { f32 sx, sy; f32 dist; s32 inview; s32 edge; } PmPoint;

/* world point -> console-space (conSpaceW x 480) position; inview = inside the screen and in front; else sx,sy is clamped to the edge */
static s32 pmProject(const struct player *pl, f32 wx, f32 wy, f32 wz, PmPoint *o)
{
    const Mtxf *m = pl->viewtoworldmtxf;
    f32 dx, dy, dz, vx, vy, vz, u, v;
    const f32 margin = 22.0f;

    dx = wx - m->m[3][0];
    dy = wy - m->m[3][1];
    dz = wz - m->m[3][2];
    vx = m->m[0][0] * dx + m->m[0][1] * dy + m->m[0][2] * dz;
    vy = m->m[1][0] * dx + m->m[1][1] * dy + m->m[1][2] * dz;
    vz = m->m[2][0] * dx + m->m[2][1] * dy + m->m[2][2] * dz;
    o->dist = sqrtf(dx * dx + dy * dy + dz * dz);
    o->inview = 0;
    if (vz < -20.0f)
    {
        const f32 cx = pl->c_screenleft + pl->c_halfwidth;
        const f32 cy = pl->c_screentop + pl->c_halfheight;
        const f32 px = cx + (vx / -vz) * pl->c_recipscalex;
        const f32 py = cy - (vy / -vz) * pl->c_recipscaley;

        u = (px - pl->c_screenleft) / pl->c_screenwidth;
        v = (py - pl->c_screentop) / pl->c_screenheight;
        if (u > 0.04f && u < 0.96f && v > 0.06f && v < 0.94f)
        {
            o->sx = u * (f32) conSpaceW;
            o->sy = v * (f32) conSpaceH;
            o->inview = 1;
            return 1;
        }
    }
    {   /* off screen or behind: the screen-edge point in the direction of the pickup */
        f32 ax = vx, ay = -vy, t, hw = 0.5f * (f32) conSpaceW - margin, hh = 0.5f * (f32) conSpaceH - margin;
        f32 tx, ty;

        if (ax * ax + ay * ay < 1.0f)
        {
            ax = 0.0f;
            ay = 1.0f;
        }
        tx = (ax != 0.0f) ? hw / (ax < 0.0f ? -ax : ax) : 1e30f;
        ty = (ay != 0.0f) ? hh / (ay < 0.0f ? -ay : ay) : 1e30f;
        t = tx < ty ? tx : ty;
        o->sx = 0.5f * (f32) conSpaceW + ax * t;
        o->sy = 0.5f * (f32) conSpaceH + ay * t;
    }
    return 0;
}

typedef struct { f32 sx, sy, dist, a; s32 inview, kind, count, same, good; } PmGroup;

static f32 pmClamp01(f32 v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

static u32 pmAlpha(u32 rgba, f32 a) { return (rgba & 0xFFFFFF00u) | (u32) (pmClamp01(a) * 255.0f); }

/* a small diamond of half-size r centred on (x, y): one 2 px rect per row pair */
static Gfx *pmDiamond(Gfx *gdl, s32 x, s32 y, s32 r, u32 rgba)
{
    s32 dy;

    for (dy = -r; dy <= r; dy += 2)
    {
        const s32 w = r - (dy < 0 ? -dy : dy);

        gdl = ngRect(gdl, x - w, y + dy, x + w + 1, y + dy + 2, rgba);
    }
    return gdl;
}

/* marker colours by kind (PORT_MK_*): the shape, and the slightly lighter label */
static u32 pmColour(s32 kind, s32 label)
{
    switch (kind)
    {
        case PORT_MK_AMMO:   return label ? 0xFFE878FFu : 0xFFE040FFu;
        case PORT_MK_ARMOUR: return label ? 0x80F4FFFFu : 0x40F0FFFFu;
        case PORT_MK_GIFT:   return label ? 0xFF98F0FFu : 0xFF70E8FFu;
        case PORT_MK_ITEM:   return label ? 0xFFB060FFu : 0xFF9020FFu;
        default:             return 0xE8D8B0FFu;   /* drops: a pale sand-white dot */
    }
}

/* the A7 look (a beacon + box over EVERY pickup, nothing fades or merges): marker_style = "a7", for before/after shots */
static Gfx *portPickupMarkersRenderA7(Gfx *gdl, const PortPickupMarker *mk, s32 n)
{
    PmPoint foot[PORT_MARKER_MAX], top[PORT_MARKER_MAX];
    char txt[PORT_MARKER_MAX][20];
    const struct player *pl = g_CurrentPlayer;
    s32 i;

    if (n <= 0 || pl == NULL || pl->viewtoworldmtxf == NULL || pl->c_screenwidth < 1.0f || pl->c_screenheight < 1.0f ||
        dynGetFreeGfx2(gdl) < NG_RESERVE_TEXT + 1500)
    {
        return gdl;
    }
    gdl = conSpaceBegin(gdl);
    for (i = 0; i < n; i++)
    {
        pmProject(pl, mk[i].x, mk[i].y, mk[i].z, &foot[i]);
        pmProject(pl, mk[i].x, mk[i].y + 260.0f, mk[i].z, &top[i]);
        snprintf(txt[i], sizeof(txt[0]), "%s %dm", mk[i].label, (s32) (foot[i].dist / 100.0f + 0.5f));
    }
    /* pass 1: rects */
    ngHavePrim = 0;
    gDPSetRenderMode(gdl++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetCombineMode(gdl++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    for (i = 0; i < n; i++)
    {
        const u32 c = mk[i].rgba;
        const u32 faint = (c & 0xFFFFFF00u) | 0x90u;

        if (dynGetFreeGfx2(gdl) < NG_RESERVE_TEXT)
        {
            break;
        }
        if (foot[i].inview)
        {   /* the beacon: a thin bar from the foot to the head, a box on top */
            f32 y0 = top[i].sy, y1 = foot[i].sy;

            if (y1 - y0 < 14.0f)
            {
                y0 = y1 - 14.0f;
            }
            gdl = ngRect(gdl, (s32) foot[i].sx - 1, (s32) y0, (s32) foot[i].sx + 2, (s32) y1, faint);
            gdl = ngRect(gdl, (s32) foot[i].sx - 7, (s32) y0 - 12, (s32) foot[i].sx + 7, (s32) y0 + 2, 0x000000C0u);
            gdl = ngRect(gdl, (s32) foot[i].sx - 5, (s32) y0 - 10, (s32) foot[i].sx + 5, (s32) y0, c);
        }
        else
        {   /* the edge marker */
            gdl = ngRect(gdl, (s32) foot[i].sx - 8, (s32) foot[i].sy - 8, (s32) foot[i].sx + 8, (s32) foot[i].sy + 8, 0x000000C0u);
            gdl = ngRect(gdl, (s32) foot[i].sx - 6, (s32) foot[i].sy - 6, (s32) foot[i].sx + 6, (s32) foot[i].sy + 6, c);
        }
    }
    gDPSetCombineLERP(gdl++, 0, 0, 0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0, 0, 0, 0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0);
    /* pass 2: labels */
    for (i = 0; i < n; i++)
    {
        s32 tw = conTextWidth(txt[i]);
        s32 lx, ly;

        if (foot[i].inview)
        {
            f32 y0 = top[i].sy, y1 = foot[i].sy;

            if (y1 - y0 < 14.0f)
            {
                y0 = y1 - 14.0f;
            }
            lx = (s32) foot[i].sx - tw / 2;
            ly = (s32) y0 - 26;
        }
        else
        {
            lx = (s32) foot[i].sx - tw / 2;
            ly = (s32) foot[i].sy + 10;
            if (foot[i].sy > 0.5f * (f32) conSpaceH)
            {
                ly = (s32) foot[i].sy - 24;
            }
        }
        if (lx < 2) { lx = 2; }
        if (lx + tw > conSpaceW - 2) { lx = conSpaceW - 2 - tw; }
        gdl = conDrawRow(gdl, lx, ly, txt[i], mk[i].rgba);
    }
    return conSpaceEnd(gdl);
}

static Gfx *portPickupMarkersRender(Gfx *gdl)
{
    extern s32 g_GlobalTimer;
    PortPickupMarker mk[PORT_MARKER_MAX];
    PortMarkerStyle st;
    PmGroup cand[PORT_MARKER_MAX], grp[PORT_MARKER_MAX];
    const struct player *pl = g_CurrentPlayer;
    s32 n, i, j, nc = 0, ng = 0, fogOn;
    f32 fogA = 0.0f, fogB = 0.0f;

    n = portArenaMarkers(mk, PORT_MARKER_MAX, &st);
    if (st.legacy)
    {
        return portPickupMarkersRenderA7(gdl, mk, n);
    }
    {
        extern s32 portFogCurve(f32 *a, f32 *b, f32 rgb[3]);   /* bgfog.c */
        f32 rgb[3];

        fogOn = portFogCurve(&fogA, &fogB, rgb);
    }
    if (n <= 0 || pl == NULL || pl->viewtoworldmtxf == NULL || pl->c_screenwidth < 1.0f || pl->c_screenheight < 1.0f ||
        dynGetFreeGfx2(gdl) < NG_RESERVE_TEXT + 1500)
    {
        return gdl;
    }
    for (i = 0; i < n; i++)
    {   /* what is worth drawing, and how strongly */
        const s32 good = PORT_MK_GOOD(mk[i].kind);
        PmPoint pt;
        f32 a;

        pmProject(pl, mk[i].x, mk[i].y + (good ? 70.0f : 20.0f), mk[i].z, &pt);
        if (good)
        {
            const f32 age = (f32) ((s32) g_GlobalTimer - mk[i].born) / 60.0f;
            const f32 at = st.fade <= 0.0f ? 1.0f : pmClamp01((st.fade - age) / 1.5f);   /* full, then gone over the last 1.5 s */
            const f32 an = st.nearDist > 0.0f ? pmClamp01((st.nearDist - pt.dist) / (st.nearDist * 0.25f)) : 0.0f;

            a = at > an ? at : an;
            if (!pt.inview && !st.edge)
            {
                continue;
            }
        }
        else
        {
            const s32 mode = mk[i].kind == PORT_MK_KNIFE ? st.knives : st.drops;

            if (mode == 0 || !pt.inview)
            {
                continue;   /* no edge arrows for drops */
            }
            a = mode == 2 ? 1.0f : (st.dropNear > 0.0f ? pmClamp01((st.dropNear - pt.dist) / (st.dropNear * 0.3f)) : 0.0f);
        }
        if (fogOn && pt.dist > 1.0f)
        {   /* the arena fog (LOW VISIBILITY): a marker fades as the pickup does (the props' fog curve at its distance) */
            a *= 1.0f - pmClamp01(fogA - fogB / pt.dist);
        }
        if (a < 0.03f)
        {
            continue;
        }
        cand[nc].sx = pt.sx;
        cand[nc].sy = pt.sy;
        cand[nc].dist = pt.dist;
        cand[nc].a = a;
        cand[nc].inview = pt.inview;
        cand[nc].kind = mk[i].kind;
        cand[nc].count = 1;
        cand[nc].same = 1;
        cand[nc].good = good;
        nc++;
    }
    for (i = 1; i < nc; i++)
    {   /* the good stuff first, then nearest first: they lead their groups and win the draw budget */
        PmGroup t = cand[i];

        for (j = i - 1; j >= 0 && (cand[j].good < t.good || (cand[j].good == t.good && cand[j].dist > t.dist)); j--)
        {
            cand[j + 1] = cand[j];
        }
        cand[j + 1] = t;
    }
    for (i = 0; i < nc; i++)
    {   /* merge markers that overlap on screen (same class, both in view or both on the edge) */
        for (j = 0; j < ng; j++)
        {
            const f32 dx = cand[i].sx - grp[j].sx, dy = cand[i].sy - grp[j].sy;

            if (grp[j].good == cand[i].good && grp[j].inview == cand[i].inview && dx * dx + dy * dy < st.merge * st.merge)
            {
                grp[j].count++;
                if (grp[j].kind != cand[i].kind)
                {
                    grp[j].same = 0;
                }
                if (cand[i].a > grp[j].a)
                {
                    grp[j].a = cand[i].a;
                }
                break;
            }
        }
        if (j == ng && ng < st.maxGroups)
        {
            grp[ng++] = cand[i];
        }
    }
    {   /* GE_MARKERLOG=1 (test): what the overlay draws, once a second */
        static s32 want = -1, last;

        if (want < 0)
        {
            const char *e = getenv("GE_MARKERLOG");

            want = e != NULL && e[0] != '\0' && e[0] != '0';
        }
        if (want && (s32) g_GlobalTimer - last >= 60)
        {
            s32 drawnItems = 0;

            last = (s32) g_GlobalTimer;
            for (i = 0; i < ng; i++)
            {
                drawnItems += grp[i].count;
            }
            printf("ge: markers: t=%d published %d, candidates %d, groups drawn %d (covering %d)\n", (int) g_GlobalTimer, (int) n, (int) nc, (int) ng,
                   (int) drawnItems);
            fflush(stdout);
        }
    }
    if (ng == 0)
    {
        return gdl;
    }
    gdl = conSpaceBegin(gdl);
    /* pass 1: the diamonds and dots (a faint dark diamond behind each one so it reads on the sand) */
    ngHavePrim = 0;
    gDPSetRenderMode(gdl++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetCombineMode(gdl++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    for (i = 0; i < ng; i++)
    {
        const s32 x = (s32) grp[i].sx, y = (s32) grp[i].sy;

        if (dynGetFreeGfx2(gdl) < NG_RESERVE_TEXT)
        {
            break;
        }
        if (grp[i].good)
        {
            gdl = pmDiamond(gdl, x, y, 6, pmAlpha(0x00000000u, grp[i].a * 0.35f));
            gdl = pmDiamond(gdl, x, y, 4, pmAlpha(pmColour(grp[i].kind, 0), grp[i].a * 0.85f));
        }
        else
        {   /* a drop: a small faint dot */
            gdl = ngRect(gdl, x - 2, y - 2, x + 2, y + 2, pmAlpha(pmColour(grp[i].kind, 0), grp[i].a * 0.55f));
        }
    }
    gDPSetCombineLERP(gdl++, 0, 0, 0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0, 0, 0, 0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0);
    /* pass 2: short labels: "AMMO 12m", merged "AMMO x2 12m" / "AMMO +1 12m"; a merged dot gets only its count */
    for (i = 0; i < ng; i++)
    {
        char txt[24];
        const char *lab = "";
        s32 tw, lx, ly, k;

        if (dynGetFreeGfx2(gdl) < NG_RESERVE_TEXT)
        {
            break;
        }
        for (k = 0; k < n; k++)
        {   /* the label text of the group's kind */
            if (mk[k].kind == grp[i].kind)
            {
                lab = mk[k].label;
                break;
            }
        }
        if (!grp[i].good)
        {
            if (grp[i].count < 2)
            {
                continue;
            }
            snprintf(txt, sizeof(txt), "%d", (int) grp[i].count);
        }
        else if (grp[i].count > 1)
        {
            snprintf(txt, sizeof(txt), grp[i].same ? "%s x%d %dm" : "%s +%d %dm", lab, (int) (grp[i].same ? grp[i].count : grp[i].count - 1),
                     (int) (grp[i].dist / 100.0f + 0.5f));
        }
        else
        {
            snprintf(txt, sizeof(txt), "%s %dm", lab, (int) (grp[i].dist / 100.0f + 0.5f));
        }
        tw = conTextWidth(txt);
        if (!grp[i].good)
        {
            lx = (s32) grp[i].sx + 4;
            ly = (s32) grp[i].sy - 6;
        }
        else if (grp[i].inview)
        {
            lx = (s32) grp[i].sx - tw / 2;
            ly = (s32) grp[i].sy - 19;
        }
        else
        {   /* beside the edge diamond, toward the middle of the screen */
            lx = (grp[i].sx > 0.5f * (f32) conSpaceW) ? (s32) grp[i].sx - 9 - tw : (s32) grp[i].sx + 9;
            ly = (s32) grp[i].sy - 5;
        }
        if (lx < 2) { lx = 2; }
        if (lx + tw > conSpaceW - 2) { lx = conSpaceW - 2 - tw; }
        if (ly < 2) { ly = 2; }
        gdl = conDrawRow(gdl, lx, ly, txt, pmAlpha(pmColour(grp[i].kind, 1), grp[i].a * (grp[i].good ? 0.80f : 0.55f)));
    }
    return conSpaceEnd(gdl);
}

static Gfx *portNetGraphRender(Gfx *gdl)
{
    const s32 level = portNetGraphLevel();
    const PortNetStatSecond *sec = portNetStatsSecond();
    const PortNetStatSample *cur = portNetStatsBack(0);
    PortNetGraphInfo in;
    char l[7][96];
    u32 lc[7];
    s32 nl = 0;
    s32 px, py, i;
    s32 gx, gy[4], ng;
    f32 sc[4];
    s32 panelH, lightsY = 0, lightsH = 0;
    const s32 *kinds;
    static const s32 kindsOn[4] = { NG_PING, NG_BYTES, NG_WAIT, NG_MS };
    static const f32 floorsOn[4] = { 60.0f, 200.0f, 5.0f, 20.0f };
    static const s32 kindsOff[2] = { NG_MS, NG_FRAME };
    static const f32 floorsOff[2] = { 20.0f, 20.0f };
    const f32 *floors;

    if (level <= 0 || cur == NULL || dynGetFreeGfx2(gdl) < NG_RESERVE_TEXT + 1500)
    {
        return gdl;
    }
    portNetGraphInfo(&in);
    ng = in.netplay ? 4 : 2;   /* no netplay: just the frame-time graphs */
    kinds = in.netplay ? kindsOn : kindsOff;
    floors = in.netplay ? floorsOn : floorsOff;

    /* the numbers */
    if (in.netplay)
    {
        snprintf(l[nl], sizeof(l[0]), "NET %s   tick %u   delay %d", in.role == 1 ? "HOST" : in.role == 2 ? "GUEST" : "REPLAY", in.tick, in.delay);
        lc[nl++] = in.linked ? NG_COL_CYAN : NG_COL_GRAY;
        snprintf(l[nl], sizeof(l[0]), "ping %3d ms +-%d   loss %d%%  rsnd %u", in.pingMs, in.jitterMs, in.enetLossPct, in.resends);
        lc[nl++] = ngPingColour(in.pingMs);
        snprintf(l[nl], sizeof(l[0]), "in  %5.1f KB/s  %3.0f pkt/s", sec->valid ? sec->inKBs : 0.0f, sec->valid ? sec->inPps : 0.0f);
        lc[nl++] = NG_COL_GREEN;
        snprintf(l[nl], sizeof(l[0]), "out %5.1f KB/s  %3.0f pkt/s", sec->valid ? sec->outKBs : 0.0f, sec->valid ? sec->outPps : 0.0f);
        lc[nl++] = NG_COL_CYAN;
        snprintf(l[nl], sizeof(l[0]), "late %u   dup %u   oow %u   wait %.1f ms", in.lateTicks, in.dups, in.oow, cur->waitMs);
        lc[nl++] = in.lateTicks != 0 ? NG_COL_YELLOW : NG_COL_GRAY;
    }
    else
    {
        snprintf(l[nl], sizeof(l[0]), "NET off   (frame times only)");
        lc[nl++] = NG_COL_GRAY;
    }
    snprintf(l[nl], sizeof(l[0]), "sim %.1f  rend %.1f ms   %.0f fps", sec->valid ? sec->simAvg : cur->simMs, sec->valid ? sec->renderAvg : cur->renderMs,
             sec->valid ? sec->fps : 0.0f);
    lc[nl++] = NG_COL_WHITE;
    if (in.netplay)
    {
        if (in.lastHashVerdict < 0)
        {
            snprintf(l[nl], sizeof(l[0]), "hash: no check yet   desyncs %u", in.desyncs);
            lc[nl++] = NG_COL_GRAY;
        }
        else
        {
            snprintf(l[nl], sizeof(l[0]), "hash t%u %s   desyncs %u/%u", in.lastHashTick, in.lastHashVerdict ? "DESYNC" : "OK", in.desyncs, in.hashCompared);
            lc[nl++] = in.lastHashVerdict ? NG_COL_RED : NG_COL_GREEN;
        }
    }

    {   /* placement: the top-right corner, whatever the window's shape */
        s32 w = conSpaceW;
        const s32 need = (level >= 2) ? NG_GRAPH_W + 8 : NG_PANEL_W;

        px = w - need - 6;
        if (px < 2) { px = 2; }
        py = 6;
    }
    panelH = nl * CON_ROW_PITCH + 6;
    gx = px + 4;
    gy[0] = py + panelH + 12;
    for (i = 1; i < ng; i++) { gy[i] = gy[i - 1] + NG_GRAPH_H + 16; }
    if (level >= 3)
    {
        lightsY = gy[ng - 1] + NG_GRAPH_H + 10;
        lightsH = 3 * 13 + 4;
    }

    /* pass 1: rects */
    ngHavePrim = 0;
    gdl = conSpaceBegin(gdl);
    gDPSetRenderMode(gdl++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetCombineMode(gdl++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gdl = ngRect(gdl, px, py - 3, px + (level >= 2 ? NG_GRAPH_W + 8 : NG_PANEL_W), py + panelH, 0x000000A8u);
    if (in.netplay && in.lastHashVerdict == 1)
    {   /* a red frame on the panel while the last check failed */
        const s32 pw = level >= 2 ? NG_GRAPH_W + 8 : NG_PANEL_W;

        gdl = ngRect(gdl, px, py - 3, px + pw, py - 1, NG_COL_RED);
        gdl = ngRect(gdl, px, py + panelH - 2, px + pw, py + panelH, NG_COL_RED);
    }
    if (level >= 2)
    {
        for (i = 0; i < ng; i++)
        {
            sc[i] = ngScale(kinds[i], floors[i]);
            gdl = ngGraphRects(gdl, gx, gy[i], kinds[i], sc[i]);
        }
    }
    if (level >= 3)
    {
        const s32 colw = NG_GRAPH_W / 4;

        gdl = ngRect(gdl, gx - 1, lightsY - 2, gx + NG_GRAPH_W + 1, lightsY + lightsH, 0x000000A0u);
        for (i = 0; i < in.nfields; i++)
        {
            const s32 lx = gx + 3 + (i % 4) * colw;
            const s32 ly = lightsY + (i / 4) * 13 + 2;
            u32 col;

            if (!in.netplay || in.lastHashVerdict < 0) { col = 0x505050FFu; }
            else if (in.diffMask & (1u << i)) { col = 0xFF2020FFu; }
            else { col = 0x30E030FFu; }
            gdl = ngRect(gdl, lx, ly, lx + 8, ly + 8, 0x000000FFu);
            gdl = ngRect(gdl, lx + 1, ly + 1, lx + 7, ly + 7, col);
        }
    }
    gDPSetCombineLERP(gdl++, 0, 0, 0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0, 0, 0, 0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0);

    /* pass 2: text */
    for (i = 0; i < nl; i++)
    {
        gdl = conDrawRow(gdl, px + 4, py + 1 + i * CON_ROW_PITCH, l[i], lc[i]);
    }
    if (level >= 2)
    {
        char a[48], b[48];

        for (i = 0; i < ng; i++)
        {
            u32 col = NG_COL_WHITE;

            snprintf(b, sizeof(b), "max %.0f", sc[i]);
            switch (kinds[i])
            {
            case NG_PING:
                snprintf(a, sizeof(a), "ping %d ms", cur->pingMs);
                col = ngPingColour(cur->pingMs);
                break;
            case NG_BYTES:
                snprintf(a, sizeof(a), "in %u out %u B/frame", cur->inBytes, cur->outBytes);
                col = NG_COL_GREEN;
                break;
            case NG_WAIT:
                snprintf(a, sizeof(a), "input wait %.1f ms", cur->waitMs);
                col = NG_COL_YELLOW;
                break;
            case NG_FRAME:
                snprintf(a, sizeof(a), "frame %.1f ms", cur->frameMs);
                break;
            default:
                snprintf(a, sizeof(a), "sim %.1f render %.1f ms", cur->simMs, cur->renderMs);
                col = NG_COL_ORANGE;
                break;
            }
            (void) col;   /* the labels are white: they sit on bars of the series' own colour */
            gdl = ngLabel(gdl, gx, gy[i], a, NG_COL_WHITE, b);
        }
    }
    if (level >= 3)
    {
        const s32 colw = NG_GRAPH_W / 4;

        for (i = 0; i < in.nfields; i++)
        {
            const s32 lx = gx + 3 + (i % 4) * colw;
            const s32 ly = lightsY + (i / 4) * 13 + 2;
            const s32 bad = in.netplay && in.lastHashVerdict >= 0 && (in.diffMask & (1u << i)) != 0;

            gdl = conDrawRow(gdl, lx + 11, ly - 1, in.fieldName[i], bad ? NG_COL_RED : NG_COL_WHITE);
        }
    }
    return conSpaceEnd(gdl);
}

/* ---- heist CCTV view overlay and the HUD panel (port_cctvview.c, hud.panel) ------------------------------------------------------- */

#define CV_STATIC_COLS 40
#define CV_STATIC_ROWS 30
#define CV_COL_WHITE   0xFFFFFFFFu
#define CV_COL_RED     0xFF3030FFu
#define CV_COL_DIM     0xC0C0C0FFu
#define CV_GFX_NEED    (CV_STATIC_COLS * CV_STATIC_ROWS * 2 + 6 * CON_GFX_ROW + CON_GFX_SETUP + CON_GFX_TAIL + 64)

/* A camera feed on this machine's screen: the name, where it is in the list, a blinking REC dot, the clock, the controls; a camera that
 * is off is full-screen static under the labels. Presentation only (it reads the view state and the camera's flags). */
static Gfx *cctvOverlay(Gfx *gdl)
{
    extern int portCctvOverlayGet(int *cam, char *name, int namelen, int *index, int *count, int *offline, int *seconds, int *ticks);
    static u32 seed = 0x2545F491u;
    int cam, idx, cnt, off, secs, ticks;
    char name[32], line[96];
    s32 W, H, x, y;

    if (!portCctvOverlayGet(&cam, name, (int) sizeof(name), &idx, &cnt, &off, &secs, &ticks))
    {
        return gdl;
    }
    if (dynGetFreeGfx2(gdl) < CV_GFX_NEED)
    {
        return gdl;
    }
    ngHavePrim = 0;
    gdl = conSpaceBegin(gdl);
    W = conSpaceW;
    H = conSpaceH;
    gDPSetRenderMode(gdl++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetCombineMode(gdl++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    if (off)
    {   /* static: a grid of grey blocks, new every frame */
        s32 bw = (W + CV_STATIC_COLS - 1) / CV_STATIC_COLS, bh = (H + CV_STATIC_ROWS - 1) / CV_STATIC_ROWS;

        for (y = 0; y < CV_STATIC_ROWS; y++)
        {
            for (x = 0; x < CV_STATIC_COLS; x++)
            {
                u32 g;

                seed = seed * 1664525u + 1013904223u;
                g = (seed >> 24) & 0xE0u;   /* 8 levels: fewer colour changes */
                gdl = ngRect(gdl, x * bw, y * bh, x * bw + bw, y * bh + bh, (g << 24) | (g << 16) | (g << 8) | 0xFFu);
            }
        }
    }
    gdl = ngRect(gdl, 0, 0, W, 24, 0x00000090u);
    gdl = ngRect(gdl, 0, H - 22, W, H, 0x00000090u);
    if (off)
    {
        gdl = ngRect(gdl, W / 2 - 80, H / 2 - 18, W / 2 + 80, H / 2 + 18, 0x000000E0u);
    }
    if (((ticks / 30) & 1) == 0)
    {
        gdl = ngRect(gdl, W - 64, 7, W - 54, 17, CV_COL_RED);
    }
    gDPSetCombineLERP(gdl++, 0, 0, 0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0, 0, 0, 0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0);
    snprintf(line, sizeof(line), "CAM %d/%d  %s", idx, cnt, name);
    gdl = conDrawRow(gdl, CON_TEXT_X, 6, line, CV_COL_WHITE);
    gdl = conDrawRow(gdl, W - 48, 6, "REC", CV_COL_RED);
    snprintf(line, sizeof(line), "%02d:%02d:%02d", (secs / 3600) % 100, (secs / 60) % 60, secs % 60);
    gdl = conDrawRow(gdl, W - 48 - 16 - conTextWidth(line) - 12, 6, line, CV_COL_WHITE);
    gdl = conDrawRow(gdl, CON_TEXT_X, H - 17, "NEXT: A / C-right / next weapon   PREV: C-left / previous weapon   LEAVE: B / use", CV_COL_DIM);
    if (off)
    {
        const char *t1 = "NO SIGNAL";
        const char *t2 = "CAMERA OFFLINE";

        gdl = conDrawRow(gdl, (W - conTextWidth(t1)) / 2, H / 2 - 12, t1, CV_COL_WHITE);
        gdl = conDrawRow(gdl, (W - conTextWidth(t2)) / 2, H / 2 + 2, t2, CV_COL_WHITE);
    }
    return conSpaceEnd(gdl);
}

/* hud.panel: up to 8 lines in a dark box at the top right, under the feed's top bar */
static Gfx *hudPanel(Gfx *gdl)
{
    char rows[PORT_HUD_PANEL_LINES][64];
    s32 n = 0, i, w = 0, x, y = 30;

    for (i = 0; i < PORT_HUD_PANEL_LINES; i++)
    {
        const char *t = portModeHudPanelGetLocal(i);

        rows[i][0] = '\0';
        if (t != NULL)
        {
            memcpy(rows[i], t, 63);
            rows[i][63] = '\0';
            n = i + 1;
            if (conTextWidth(rows[i]) > w) w = conTextWidth(rows[i]);
        }
    }
    if (n == 0 || dynGetFreeGfx2(gdl) < n * CON_GFX_ROW + CON_GFX_SETUP + CON_GFX_TAIL + 16)
    {
        return gdl;
    }
    ngHavePrim = 0;
    gdl = conSpaceBegin(gdl);
    x = conSpaceW - w - 14;
    gDPSetRenderMode(gdl++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetCombineMode(gdl++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gdl = ngRect(gdl, x - 6, y - 4, conSpaceW - 4, y + n * CON_ROW_PITCH + 2, 0x000000A8u);
    gDPSetCombineLERP(gdl++, 0, 0, 0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0, 0, 0, 0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0);
    for (i = 0; i < n; i++)
    {
        gdl = conDrawRow(gdl, x, y + i * CON_ROW_PITCH, rows[i], i == 0 ? 0xFFE060FFu : CON_COL_TEXT);
    }
    return conSpaceEnd(gdl);
}

/* heist cover (port_heist.c, designs/bank-heist.md section 16): an eye at the top left for the local Bond's disguise. Closed and grey:
 * no guard has him in sight; open: watched; the bar is the most suspicious guard's meter (white, yellow, orange); red: a guard saw through
 * him ("HEY!") or everyone is on alert. Presentation only: it reads the sim's meter for portLocalPlayer(). Hidden on a camera feed. */
static Gfx *coverEye(Gfx *gdl)
{
    extern int portHeistCoverMeter(int p, float *level, int *chr);
    extern int portCctvOverlayGet(int *cam, char *name, int namelen, int *index, int *count, int *offline, int *seconds, int *ticks);
    extern int portLocalPlayer(void);
    static const char *const label[] = { "", "COVER", "WATCHED", "SUSPICIOUS", "HEY! COVER BLOWN", "ALERT: NO COVER" };
    float level;
    int chr, st, cam, idx, cnt, off, secs, ticks;
    char name[8];
    s32 cx, y = 6, bx, bw = 110, fill;
    u32 col, eye;

    st = portHeistCoverMeter(portLocalPlayer(), &level, &chr);
    if (st <= 0 || st > 5 || portCctvOverlayGet(&cam, name, (int) sizeof(name), &idx, &cnt, &off, &secs, &ticks))
    {
        return gdl;
    }
    if (dynGetFreeGfx2(gdl) < 2 * CON_GFX_ROW + CON_GFX_SETUP + CON_GFX_TAIL + 96)
    {
        return gdl;
    }
    if (st == 1) col = 0x909090FFu;
    else if (st == 2 && level < 0.04f) col = 0xF0F0F0FFu;
    else if (st >= 4) col = 0xFF3030FFu;
    else col = level < 0.5f ? 0xFFE040FFu : 0xFF9020FFu;
    eye = st == 1 ? 0x909090FFu : (st >= 4 ? 0xFF3030FFu : 0xF0F0F0FFu);
    ngHavePrim = 0;
    gdl = conSpaceBegin(gdl);
    cx = 100;   /* top left: the intel panel takes the top right and most of the top in the hall */
    bx = cx - 40;
    gDPSetRenderMode(gdl++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetCombineMode(gdl++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gdl = ngRect(gdl, cx - 86, y - 3, cx + 86, y + 29, 0x00000090u);
    if (st == 1)
    {   /* closed: a lid line */
        gdl = ngRect(gdl, cx - 78, y + 7, cx - 52, y + 9, eye);
    }
    else
    {   /* an almond with a pupil */
        gdl = ngRect(gdl, cx - 74, y + 1, cx - 56, y + 3, eye);
        gdl = ngRect(gdl, cx - 78, y + 3, cx - 52, y + 13, eye);
        gdl = ngRect(gdl, cx - 74, y + 13, cx - 56, y + 15, eye);
        gdl = ngRect(gdl, cx - 69, y + 4, cx - 61, y + 12, 0x101010FFu);
    }
    fill = (s32) (level * (f32) bw);
    if (fill < 0) fill = 0;
    if (fill > bw) fill = bw;
    gdl = ngRect(gdl, bx, y + 4, bx + bw, y + 11, 0x404040C0u);
    if (fill > 0) gdl = ngRect(gdl, bx, y + 4, bx + fill, y + 11, col);
    gDPSetCombineLERP(gdl++, 0, 0, 0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0, 0, 0, 0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0);
    gdl = conDrawRow(gdl, cx - conTextWidth(label[st]) / 2, y + 16, label[st], col);
    return conSpaceEnd(gdl);
}

Gfx *portConRender(Gfx *gdl)
{
    s32 i;

    conEnvHarness();

    /* the fonts are NULL before the frontend loads them (boot, early stage
     * load); drawing then walks a NULL fontchar table */
    if (ptrFontBankGothic == NULL || ptrFontBankGothicChars == NULL)
    {
        return gdl;
    }
    if (!conInited)
    {
        return gdl;
    }
    /* the frame already filled the master DL: add nothing to it */
    if (dynGetFreeGfx2(gdl) < CON_GFX_SETUP + CON_GFX_TAIL)
    {
        return gdl;
    }

    if (portModeActive())
    {
        {   /* the arena's RAIN (port_arenarain_draw.c): the dim, the streaks and the splashes, under every HUD line */
            extern Gfx *portArenaRainRender(Gfx *gdl);

            gdl = portArenaRainRender(gdl);
        }
        gdl = cctvOverlay(gdl);   /* heist: a camera feed's labels and static (under every other line) */
        gdl = hudPanel(gdl);
        gdl = coverEye(gdl);      /* heist: the disguise's eye */
        {
            extern Gfx *portGadgetsOverlay(Gfx *gdl);   /* heist gadgets: the piton line, the aim marker, the work bar */

            gdl = portGadgetsOverlay(gdl);
        }
        {
            extern Gfx *portSniperOverlay(Gfx *gdl);   /* the sniper duel: glints, markers, tags, the binoculars' lens frame */

            gdl = portSniperOverlay(gdl);
        }
        {   /* the AR specs and the diagnostics layer (port_arhud.c) */
            extern Gfx *portArHudRender(Gfx *gdl);
            gdl = portArHudRender(gdl);
        }
    }

    gdl = portNetGraphRender(gdl);   /* job 6: net_graph 1-3 (a no-op at 0) */
    gdl = portPickupMarkersRender(gdl);   /* arena (drop A7): pickup markers (a no-op unless pickup_markers is on) */

    if (portModeActive())
    {   /* PORT: mode HUD: 1-3 lines, copied out of the sim's buffer before drawing */
        char hud[PORT_HUD_LINES][64];
        s32 n = 0;

        for (i = 0; i < PORT_HUD_LINES; i++)
        {
            const char *t = portModeHudGetLocal(i);   /* this machine's Bond's text, else the all-players one */

            hud[i][0] = '\0';
            if (t != NULL)
            {
                memcpy(hud[i], t, 63);
                hud[i][63] = '\0';
                n++;
            }
        }
        {   /* the arena's own HUD (port_arenahud.c) draws the wave panel and the callouts instead of the plain rows */
            extern int portArenaHudActive(void);
            extern Gfx *portArenaHudRender(Gfx *gdl);

            if (portArenaHudActive())
            {
                gdl = portArenaHudRender(gdl);
                n = 0;
            }
        }
        if (n > 0)
        {
            gdl = conSpaceBegin(gdl);
            for (i = 0; i < PORT_HUD_LINES; i++)
            {
                gdl = conDrawRow(gdl, CON_TEXT_X, CON_HUD_Y + CON_ROW_PITCH * i, hud[i], CON_COL_TEXT);
            }
            gdl = conSpaceEnd(gdl);
        }
        {   /* heist: the INTEL board, over the HUD, under the console (port_intel.c) */
            extern Gfx *portIntelRender(Gfx *gdl);

            gdl = portIntelRender(gdl);
        }
        {   /* the arena's start menu and post-game, over the HUD (port_arenamenu_draw.c) */
            extern Gfx *portArenaMenuRender(Gfx *gdl);

            gdl = portArenaMenuRender(gdl);
        }
        {   /* a mode's menus on the generic layer (port_uimenu_draw.c) */
            extern Gfx *portUiMenuRender(Gfx *gdl);

            gdl = portUiMenuRender(gdl);
        }
    }
    {   /* heist: the DEBRIEF panel, on every screen until dismissed (port_debrief.c) */
        extern Gfx *portDebriefRender(Gfx *gdl);

        gdl = portDebriefRender(gdl);
    }

    if (!conOpen)
    {
        if (conMsgRows > 0)
        {
            gdl = conSpaceBegin(gdl);
            for (i = 0; i < conMsgRows; i++)
            {
                gdl = conDrawRow(gdl, CON_TEXT_X, CON_TOP_Y + CON_ROW_PITCH * (conMsgRows - i - 1),
                                 conMsgBuf[i], CON_COL_TEXT);
            }
            gdl = conSpaceEnd(gdl);

            {
                const OSTime now = osGetTime();

                if (now >= conMsgTimer)
                {
                    conMsgTimer = now + CON_MSGDWELL;
                    conMsgRows--;
                }
            }
        }
        else
        {
            conMsgTimer = 0;
        }
        return gdl;
    }

    /* the transient rows only draw while closed, and every showmsg line (a
     * `set` answer) is printed while open - so the dwell starts at close */
    if (conMsgRows > 0)
    {
        conMsgTimer = osGetTime() + CON_MSGDWELL;
    }

    {
        const s32 boxBottom = CON_TOP_Y + CON_ROW_PITCH * (CON_VISROWS + 1) + 2;
        char prompt[CON_COLS + 24];

        gdl = conSpaceBegin(gdl);
        gdl = microcode_constructor_related_to_menus(gdl, 0, 0, conSpaceW, boxBottom, CON_BOX_ALPHA);

        {   /* Each row is word-wrapped to the console's width as it draws
             * (conFitLen) and the pieces stack up from the bottom until the box
             * is full; scrolling still moves by whole rows. */
            const s32 maxw = conSpaceW - CON_TEXT_X * 2;
            s32 line = 0;   /* display lines used, from the bottom */

            for (i = 0; i < CON_VISROWS && line < CON_VISROWS; i++)
            {
                /* Draw from a copy: textRender reads each byte twice, and another thread rewriting
                 * the row between reads could index the font table out of bounds. */
                char row[CON_COLS + 1];
                s32 start[CON_COLS + 1];
                s32 len[CON_COLS + 1];
                s32 pieces = 0;
                s32 at = 0;
                s32 k;

                memcpy(row, conVisRows[i], CON_COLS);
                row[CON_COLS] = '\0';
                while (row[at] != '\0' && pieces < CON_COLS)
                {
                    const s32 n = conFitLen(row + at, maxw);

                    start[pieces] = at;
                    len[pieces] = n;
                    pieces++;
                    at += n;
                    while (row[at] == ' ')
                    {
                        at++;   /* a wrapped line does not start with the space it broke at */
                    }
                }
                if (pieces == 0)
                {
                    line++;   /* an empty row keeps its line */
                    continue;
                }
                for (k = pieces - 1; k >= 0 && line < CON_VISROWS; k--, line++)
                {
                    char piece[CON_COLS + 1];

                    memcpy(piece, row + start[k], (size_t) len[k]);
                    piece[len[k]] = '\0';
                    gdl = conDrawRow(gdl, CON_TEXT_X, CON_TOP_Y + CON_ROW_PITCH * (CON_VISROWS - line - 1),
                                     piece, CON_COL_TEXT);
                }
            }
        }

        if (conScrollOfs > 0)
        {
            snprintf(prompt, sizeof(prompt), "[-%d] > %s_", conScrollOfs, conInput);
        }
        else
        {
            snprintf(prompt, sizeof(prompt), "> %s_", conInput);
        }
        {   /* a prompt wider than the box shows its newest end */
            const s32 maxw = conSpaceW - CON_TEXT_X * 2;
            const char *show = prompt;

            while (show[0] != '\0' && show[1] != '\0' && conTextWidth(show) > maxw)
            {
                show++;
            }
            gdl = conDrawRow(gdl, CON_TEXT_X, CON_TOP_Y + CON_ROW_PITCH * CON_VISROWS, show,
                             CON_COL_PROMPT);
        }

        gdl = conSpaceEnd(gdl);
    }

    return gdl;
}

#endif /* TARGET_N64 */
