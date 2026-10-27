/* port_arenascores.c - arena drop A7 (modding/arena.md "Drop A7"): the arena's best scores on disk, and the result lines the
 * Colosseum's report page (front.c print_objectives_and_status_to_menu) prints.
 *
 * The scores are a flat key=value file, arena_scores.ini, next to midas.ini (portRootPath; GE_SCOREFILE=<path> overrides it):
 *
 *     [arena_scores]
 *     endless_best_wave=17
 *     fixed20_best_time=1534.5
 *
 * Lua has no io, so the mode script reads and writes it through scores.get / scores.set (port_luavm_arena.c). The file is read
 * once, at the first use, and rewritten whole on every set. GE_NOSCORESAVE=1 stops the writes (tests); the values still change
 * in memory for that run.
 *
 * Solo only: in co-op or netplay each machine would read its own file, and a Lua value that differs between the machines must
 * never exist (the Lua heap is sim state), so the store is off there (get finds nothing, set does nothing).
 * Nothing here is read by the simulation: presentation (PRES in scripts/port/snap_embed.py). */
#ifndef TARGET_N64

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "port_arena_a7.h"

extern const char *portRootPath(const char *name);   /* main.c */
extern void portConLogf(const char *fmt, ...);
extern int portNetActive(void);                       /* port_net.c */
extern int portLuaGameCoop(void);                     /* port_lua.c: two Bonds */
extern int portLuaGameBondCount(void);

#define SCORE_N    64
#define SCORE_KEY  48

typedef struct { char key[SCORE_KEY]; double v; } Score;

static Score sScore[SCORE_N];
static int sScoreN;
static int sLoaded;
static char sPath[1200];

int portArenaScoresEnabled(void)
{
    return !portNetActive() && !portLuaGameCoop() && portLuaGameBondCount() <= 1;
}

const char *portArenaScoresPath(void)
{
    if (sPath[0] == '\0')
    {
        const char *e = getenv("GE_SCOREFILE");

        snprintf(sPath, sizeof(sPath), "%s", (e != NULL && e[0] != '\0') ? e : portRootPath("arena_scores.ini"));
    }
    return sPath;
}

static int keyOk(const char *k)
{
    size_t n = 0;

    if (k == NULL || k[0] == '\0')
    {
        return 0;
    }
    for (; *k != '\0'; k++, n++)
    {
        const char c = *k;

        if (n >= SCORE_KEY - 1 || !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-'))
        {
            return 0;
        }
    }
    return 1;
}

static Score *find(const char *key)
{
    int i;

    for (i = 0; i < sScoreN; i++)
    {
        if (strcmp(sScore[i].key, key) == 0)
        {
            return &sScore[i];
        }
    }
    return NULL;
}

static void load(void)
{
    char line[160];
    FILE *f;

    if (sLoaded)
    {
        return;
    }
    sLoaded = 1;
    sScoreN = 0;
    f = fopen(portArenaScoresPath(), "r");
    if (f == NULL)
    {
        portConLogf("ge: arena: scores: no file yet (%s)\n", portArenaScoresPath());
        return;
    }
    while (fgets(line, sizeof(line), f) != NULL && sScoreN < SCORE_N)
    {
        char *eq = strchr(line, '='), *nl;

        if (eq == NULL || line[0] == ';' || line[0] == '[' || line[0] == '#')
        {
            continue;
        }
        *eq = '\0';
        for (nl = eq + 1; *nl != '\0'; nl++)   /* (no strpbrk: the port's include path picks the libultra string.h first) */
        {
            if (*nl == '\r' || *nl == '\n')
            {
                *nl = '\0';
                break;
            }
        }
        if (keyOk(line) && find(line) == NULL)
        {
            snprintf(sScore[sScoreN].key, SCORE_KEY, "%s", line);
            sScore[sScoreN].v = strtod(eq + 1, NULL);
            sScoreN++;
        }
    }
    fclose(f);
    portConLogf("ge: arena: scores: read %d from %s\n", sScoreN, portArenaScoresPath());
}

static void save(void)
{
    const char *no = getenv("GE_NOSCORESAVE");
    FILE *f;
    int i;

    if (no != NULL && no[0] != '\0' && no[0] != '0')
    {
        portConLogf("ge: arena: scores: not written (GE_NOSCORESAVE)\n");
        return;
    }
    f = fopen(portArenaScoresPath(), "w");
    if (f == NULL)
    {
        portConLogf("ge: arena: scores: cannot write %s\n", portArenaScoresPath());
        return;
    }
    fprintf(f, "[arena_scores]\n");
    for (i = 0; i < sScoreN; i++)
    {
        fprintf(f, "%s=%.10g\n", sScore[i].key, sScore[i].v);
    }
    fclose(f);
    portConLogf("ge: arena: scores: wrote %d to %s\n", sScoreN, portArenaScoresPath());
}

int portArenaScoreGet(const char *key, double *out)
{
    Score *s;

    if (!portArenaScoresEnabled() || !keyOk(key))
    {
        return 0;
    }
    load();
    s = find(key);
    if (s == NULL)
    {
        return 0;
    }
    *out = s->v;
    return 1;
}

int portArenaScoreSet(const char *key, double v)
{
    Score *s;

    if (!portArenaScoresEnabled() || !keyOk(key))
    {
        return 0;
    }
    load();
    s = find(key);
    if (s == NULL)
    {
        if (sScoreN >= SCORE_N)
        {
            return 0;
        }
        s = &sScore[sScoreN++];
        snprintf(s->key, SCORE_KEY, "%s", key);
    }
    s->v = v;
    save();
    return 1;
}

int portArenaScoreCount(void)
{
    if (!portArenaScoresEnabled())
    {
        return 0;
    }
    load();
    return sScoreN;
}

const char *portArenaScoreKey(int k, double *v)
{
    if (!portArenaScoresEnabled() || k < 0)
    {
        return NULL;
    }
    load();
    if (k >= sScoreN)
    {
        return NULL;
    }
    *v = sScore[k].v;
    return sScore[k].key;
}

/* ---- the report page lines ---- */

#define DEBRIEF_N   8
#define DEBRIEF_LEN 64

static char sDebrief[DEBRIEF_N][DEBRIEF_LEN];

/* The longest run of non-space bytes a line may hold. front.c draws the lines through the stock textWrap, which copies each word into a
 * 32-byte stack buffer with no bound: a run of 32+ would overflow it. 29 leaves room for the stock 2-byte-glyph step and the terminator. */
#define DEBRIEF_RUN_MAX 29

/* arena.debrief (Lua): the text is copied as printable ASCII only (0x20..0x7E: the bytes the stock font has a glyph for; anything else,
 * newlines included, is dropped: front.c adds the one newline the line counter needs), and a run longer than DEBRIEF_RUN_MAX non-space
 * bytes is broken with a space. At most DEBRIEF_LEN - 1 bytes. */
void portArenaDebriefSet(int k, const char *text)
{
    char *out;
    int n = 0, run = 0, dropped = 0, breaks = 0;

    if (k < 0 || k >= DEBRIEF_N)
    {
        return;
    }
    out = sDebrief[k];
    for (; text != NULL && *text != '\0' && n < DEBRIEF_LEN - 1; text++)
    {
        const unsigned char c = (unsigned char) *text;

        if (c < 0x20 || c > 0x7E)
        {
            dropped++;
            continue;
        }
        if (c == ' ')
        {
            run = 0;
        }
        else if (run >= DEBRIEF_RUN_MAX)
        {
            out[n++] = ' ';
            run = 0;
            breaks++;
            if (n >= DEBRIEF_LEN - 1)
            {
                break;
            }
        }
        if (c != ' ')
        {
            run++;
        }
        out[n++] = (char) c;
    }
    out[n] = '\0';
    if (dropped > 0 || breaks > 0)
    {
        portConLogf("ge: arena: debrief line %d cleaned: %d byte(s) without a glyph dropped, %d long run(s) broken: \"%s\"\n", k, dropped, breaks, out);
    }
}

/* port_mode.c: every stage start except the title (the report page is drawn on the title stage) */
void portArenaDebriefClear(void)
{
    memset(sDebrief, 0, sizeof(sDebrief));
}

const char *portArenaDebriefLine(int k)
{
    if (k < 0 || k >= DEBRIEF_N || sDebrief[k][0] == '\0')
    {
        return NULL;
    }
    return sDebrief[k];
}

#endif /* TARGET_N64 */
