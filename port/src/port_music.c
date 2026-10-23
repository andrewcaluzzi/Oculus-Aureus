/* port_music.c - choosable level music.
 *
 * The game picks a stage's tracks in src/game/music_0D2720.c (main, bg, xtrack). Those three lookups ask
 * portMusicOverride() first. With nothing set it returns 0 at once, before touching any state or RNG, so the
 * code path (and the game's RNG stream) is exactly vanilla. An override comes from the Lua `music.set`
 * (a per-stage table, cleared at every stage start) or from the `level_music` modifier (Stock / Random / a
 * track). Random uses a private LCG, never randomGetNext(), and is remembered per stage so every lookup of
 * that stage agrees. Solo only, never in a demo. */
#ifndef TARGET_N64

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <ultra64.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include "game/player.h"
#include "port_modifier.h"
#include "port_music.h"

extern void portConLogf(const char *fmt, ...);               /* port_console.c */
extern s32  get_is_ramrom_flag(void);
extern s32  get_recording_ramrom_flag(void);
extern s32  portStageRunning(void);
extern s32  g_musicXTrack1CurrentTrackNum;                   /* src/music.c */
extern s32  g_CurrentStageToLoad;                            /* src/game/lv.c */
extern void lvlPlayMusicTrack1(MUSIC_TRACKS track);          /* src/game/lv.c */
extern s16  random_tracks[];                                 /* src/game/music_0D2720.c, terminated by M_NONE */

#define MUS_MAXID   M_END_SOMETHING
#define MUS_UNSET   (-2)
#define MUS_SLOTS   8

/* the M_* enum, lowercased without the prefix (built once into sName) */
#define MUS_LIST(X) \
    X(SHORT_SOLO_DEATH) X(INTRO) X(TRAIN) X(DEPOT) X(MPTHEME) X(CITADEL) X(FACILITY) X(CONTROL) X(DAM) \
    X(FRIGATE) X(ARCHIVES) X(SILO) X(MPTHEME2) X(STREETS) X(BUNKER1) X(BUNKER2) X(STATUE) X(ELEVATOR_CONTROL) \
    X(CRADLE) X(UNK) X(ELEVATOR_WC) X(EGYPTIAN) X(FOLDERS) X(WATCH) X(AZTEC) X(WATERCAVERNS) X(DEATHSOLO) \
    X(SURFACE2) X(TRAINX) X(UNK2) X(FACILITYX) X(DEPOTX) X(CONTROLX) X(WATERCAVERNSX) X(DAMX) X(FRIGATEX) \
    X(ARCHIVESX) X(SILOX) X(EGYPTIANX) X(STREETSX) X(BUNKER1X) X(BUNKER2X) X(JUNGLEX) X(INTROSWOOSH) \
    X(STATUEX) X(AZTECX) X(EGYPTX) X(CRADLEX) X(CUBA) X(RUNWAY) X(RUNWAYPLANE) X(MPTHEME3) X(WIND) \
    X(GUITARGLISS) X(JUNGLE) X(RUNWAYX) X(SURFACE1) X(MPDEATH) X(SURFACE2X) X(SURFACE2END) X(STATUEPART) \
    X(END_SOMETHING)

#define MUS_ONE(n) +1
enum { MUS_LISTED = 0 MUS_LIST(MUS_ONE) };
/* the list covers ids 1..M_END_SOMETHING in order (a missing entry breaks this at compile time) */
typedef char MusListMatchesEnum[(MUS_LISTED == M_END_SOMETHING) ? 1 : -1];

#define MUS_ID(n) M_##n,
static const s16 sListId[] = { MUS_LIST(MUS_ID) };

static char sName[MUS_MAXID + 1][24];
static int  sNamesBuilt;

typedef struct { s32 stage, main, bg, x; } MusOvr;
static MusOvr sOvr[MUS_SLOTS];
static int    sOvrN;
static u32    sRng;

static void musBuildNames(void)
{
    static const char *const raw[] = {
#define MUS_STR(n) #n,
        MUS_LIST(MUS_STR)
#undef MUS_STR
    };
    int i, k;

    if (sNamesBuilt)
    {
        return;
    }
    sNamesBuilt = 1;
    for (i = 0; i < MUS_LISTED; i++)
    {
        const char *s = raw[i];
        int id = sListId[i];

        for (k = 0; s[k] != '\0' && k < 23; k++)
        {
            sName[id][k] = (char)((s[k] >= 'A' && s[k] <= 'Z') ? s[k] + 32 : s[k]);
        }
        sName[id][k] = '\0';
    }
}

int portMusicTrackCount(void)
{
    return MUS_MAXID;
}

const char *portMusicName(int id)
{
    musBuildNames();
    return (id >= 1 && id <= MUS_MAXID && sName[id][0] != '\0') ? sName[id] : NULL;
}

static int musEqI(const char *a, const char *b)
{
    for (; *a != '\0' && *b != '\0'; a++, b++)
    {
        char x = (*a >= 'A' && *a <= 'Z') ? (char)(*a + 32) : *a;
        char y = (*b >= 'A' && *b <= 'Z') ? (char)(*b + 32) : *b;

        if (x != y)
        {
            return 0;
        }
    }
    return *a == *b;
}

int portMusicFind(const char *name)
{
    int id;

    if (name == NULL || name[0] == '\0')
    {
        return -1;
    }
    musBuildNames();
    if (name[0] == 'M' && name[1] == '_')
    {
        name += 2;   /* tolerate the enum spelling */
    }
    for (id = 1; id <= MUS_MAXID; id++)
    {
        if (sName[id][0] != '\0' && musEqI(sName[id], name))
        {
            return id;
        }
    }
    return -1;
}

/* ---- the level_music modifier value ---- */

void portMusicModText(int v, char *buf, int n)
{
    if (n < 1)
    {
        return;
    }
    if (v <= 0)
    {
        snprintf(buf, (size_t)n, "Stock");
    }
    else if (v == 1)
    {
        snprintf(buf, (size_t)n, "Random");
    }
    else
    {
        const char *s = portMusicName(v - 1);

        snprintf(buf, (size_t)n, "%s", s != NULL ? s : "?");
    }
}

int portMusicModParse(const char *text, int *v)
{
    int id;

    if (text == NULL)
    {
        return 0;
    }
    if (musEqI(text, "stock"))
    {
        *v = 0;
        return 1;
    }
    if (musEqI(text, "random"))
    {
        *v = 1;
        return 1;
    }
    id = portMusicFind(text);
    if (id < 0)
    {
        return 0;
    }
    *v = id + 1;
    return 1;
}

/* ---- the override table ---- */

/* Solo, no demo playing or recording, and a real solo stage (not the title). */
static int musAllowed(s32 stage)
{
    return getPlayerCount() == 1 && get_is_ramrom_flag() == 0 && get_recording_ramrom_flag() == 0 &&
           stage > 0 && stage < LEVELID_MAX;
}

static MusOvr *musSlot(s32 stage, int create)
{
    int i;

    for (i = 0; i < sOvrN; i++)
    {
        if (sOvr[i].stage == stage)
        {
            return &sOvr[i];
        }
    }
    if (!create || sOvrN >= MUS_SLOTS)
    {
        return NULL;
    }
    sOvr[sOvrN].stage = stage;
    sOvr[sOvrN].main = sOvr[sOvrN].bg = sOvr[sOvrN].x = MUS_UNSET;
    return &sOvr[sOvrN++];
}

static u32 musRand(void)
{
    if (sRng == 0)
    {
        sRng = (u32)time(NULL) | 1u;
    }
    sRng = sRng * 1664525u + 1013904223u;   /* private, never the game's RNG */
    return sRng >> 16;
}

static int musLevelMusicIdx(void)
{
    static int idx = -2;

    if (idx == -2)
    {
        idx = portModifierFind("level_music");
    }
    return idx;
}

/* kind 0 = main, 1 = bg, 2 = xtrack. Returns 1 with *track set when the stage's track is overridden. */
int portMusicOverride(s32 kind, s32 stage, s32 *track)
{
    MusOvr *o;
    s32 t;
    int lm;

    if (sOvrN == 0 && (kind != 0 || musLevelMusicIdx() < 0 || portModifierGet(musLevelMusicIdx()) == 0))
    {
        return 0;   /* nothing set: untouched */
    }
    if (!musAllowed(stage))
    {
        return 0;
    }
    o = musSlot(stage, 0);
    t = (o == NULL) ? MUS_UNSET : (kind == 0 ? o->main : kind == 1 ? o->bg : o->x);
    if (t == MUS_UNSET && kind == 0)
    {
        lm = (musLevelMusicIdx() >= 0) ? portModifierGet(musLevelMusicIdx()) : 0;
        if (lm == 1)
        {
            int n = 0;

            while (random_tracks[n] != M_NONE)
            {
                n++;
            }
            t = random_tracks[musRand() % (u32)n];
        }
        else if (lm >= 2 && lm - 1 <= MUS_MAXID)
        {
            t = lm - 1;
        }
        if (t != MUS_UNSET)
        {   /* remembered, so every lookup of this stage (and the log) agrees */
            o = musSlot(stage, 1);
            if (o != NULL)
            {
                o->main = t;
            }
            portConLogf("ge: music: stage %d main track -> %d (%s) [level_music]\n", (int)stage, (int)t,
                        portMusicName((int)t) != NULL ? portMusicName((int)t) : "?");
        }
    }
    if (t == MUS_UNSET)
    {
        return 0;
    }
    *track = t;
    return 1;
}

int portMusicSetMain(int id)
{
    MusOvr *o;

    if (id < 1 || id > MUS_MAXID || !musAllowed(g_CurrentStageToLoad))
    {
        return 0;
    }
    o = musSlot(g_CurrentStageToLoad, 1);
    if (o == NULL)
    {
        return 0;
    }
    o->main = id;
    portConLogf("ge: music: stage %d main track -> %d (%s) [music.set]\n", (int)g_CurrentStageToLoad, id, portMusicName(id));
    return 1;
}

int portMusicPlayNow(int id, int needLive)
{
    if (id < 1 || id > MUS_MAXID || (needLive && !portStageRunning()))
    {
        return 0;
    }
    lvlPlayMusicTrack1((MUSIC_TRACKS)id);   /* also remembers it for the death and end jingles */
    portConLogf("ge: music: playing %d (%s)\n", id, portMusicName(id));
    return 1;
}

int portMusicCurrent(void)
{
    int t = (int)g_musicXTrack1CurrentTrackNum;

    return (t >= 1 && t <= MUS_MAXID) ? t : 0;
}

void portMusicStageStart(void)
{
    sOvrN = 0;
}

void portMusicConsole(const char *arg1)
{
    int id;

    if (arg1 == NULL || arg1[0] == '\0')
    {
        const char *n = portMusicName(portMusicCurrent());

        portConLogf("ge: music: " "now %s - try: music list, music <name>\n", n != NULL ? n : "(none)");
        return;
    }
    if (strcmp(arg1, "list") == 0)
    {
        char line[160];
        int n = 0;

        line[0] = '\0';
        for (id = 1; id <= MUS_MAXID; id++)
        {
            n += snprintf(line + n, sizeof(line) - (size_t)n, "%s%s", n ? " " : "", portMusicName(id));
            if (n > 110 || id == MUS_MAXID)
            {
                portConLogf("ge: music: " "%s\n", line);
                line[0] = '\0';
                n = 0;
            }
        }
        return;
    }
    id = portMusicFind(arg1);
    if (id < 0)
    {
        portConLogf("ge: music: " "no track \"%s\" - try: music list\n", arg1);
        return;
    }
    portMusicPlayNow(id, 0);
}

#endif /* TARGET_N64 */
