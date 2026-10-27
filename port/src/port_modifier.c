/* port_modifier.c - the modifier registry (see port_modifier.h).
 *
 * A modifier is a named value with a backend: an existing cheat (turned on or off through the
 * cheat menu's own entry points, only while a solo stage is live), a port option (coop_bot) or a
 * plain port flag read by a hook in src/. Values set before a stage is live are stored and applied
 * about a second into the next stage, the same point GE_CHEATS uses. Every default is off (or 100%),
 * and the hooks return at once at the default, so the game is unchanged until one is set.
 * Ways to set one: the console `mod` command, GE_MODS, the ini `mods=` key and Lua `mods`. */
#ifndef TARGET_N64

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ultra64.h>
#include <PR/gu.h>   /* sqrtf (include/math.h declares only that) */
#include <bondtypes.h>
#include <bondconstants.h>
#include <bondaicommands.h>
#include "game/chr.h"
#include "game/chraction.h"
#include "game/cheat.h"
#include "game/player.h"
#include "game/bondview.h"
#include "game/lv.h"
#include "port_coop.h"
#include "port_faction.h"
#include "port_chrthrow.h"
#include "port_modparse.h"
#include "port_modifier.h"
#include "port_music.h"
#include "port_botlogic.h"
#include "port_bot.h"

extern void portConLogf(const char *fmt, ...);               /* port_console.c */
extern s32  portIniWriteStrs(const char *section, const char *const *keys, const char *const *vals, s32 n);   /* video.c */
extern s32  portOptFind(const char *key);                    /* port_options.c */
extern s32  portOptGet(s32 i);
extern s32  portOptSetValue(s32 i, s32 v);
extern s32  portOptSetValueQuiet(s32 i, s32 v);
extern s32  get_is_ramrom_flag(void);
extern s32  get_recording_ramrom_flag(void);
extern void cheatButtonHandleCheatsTurnedOn(CHEAT_ID cheat_id);
extern void cheatButtonHandleCheatsTurnedOff(CHEAT_ID cheat_id);
extern u8   g_CheatActivated[];
extern s32  g_AppendCheatSinglePlayer;
extern bool chrIsNotDeadOrShot(ChrRecord *self);   /* chraction.c, not in chraction.h */

enum { B_CHEAT, B_ROCKETS, B_OPT, B_FLAG };

typedef struct Mod
{
    PortModifierInfo info;
    int         backend;
    int         cheat;    /* B_CHEAT / B_ROCKETS: the CHEAT_ID */
    const char *optKey;   /* B_OPT: the port option's ini key (it saves itself) */
    int         cosmetic; /* does not count towards the cheat run */
} Mod;

#define TOG(name, label, desc, be, cheat, key) \
    { { name, label, PORT_MOD_TOGGLE, 0, 1, 1, 0, desc }, be, cheat, key }
#define PCT(name, label, desc) \
    { { name, label, PORT_MOD_INT, 25, 400, 25, 100, desc }, B_FLAG, 0, NULL }

static const Mod sMods[] = {
    TOG("infinite_ammo",  "Infinite Ammo",   "Weapons never run dry.",                              B_CHEAT, CHEAT_INFINITE_AMMO, NULL),
    TOG("invincible",     "Invincible",      "Bond takes no damage.",                               B_CHEAT, CHEAT_INVINCIBILITY, NULL),
    TOG("all_guns",       "All Guns",        "Every weapon in the inventory.",                      B_CHEAT, CHEAT_ALLGUNS, NULL),
    TOG("turbo",          "Turbo",           "Everything runs faster (Turbo Mode).",                B_CHEAT, CHEAT_TURBO_MODE, NULL),
    TOG("dk_mode",        "DK Mode",         "Big heads, long arms (DK Mode).",                     B_CHEAT, CHEAT_DK_MODE, NULL),
    TOG("tiny_bond",      "Tiny Bond",       "Bond is half size.",                                  B_CHEAT, CHEAT_TINY_BOND, NULL),
    TOG("paintball",      "Paintball",       "Bullets leave paint splats.",                         B_CHEAT, CHEAT_PAINTBALL, NULL),
    TOG("enemy_rockets",  "Enemy Rockets",   "Armed guards carry rocket launchers (next stage).",   B_ROCKETS, CHEAT_ENEMY_ROCKETS, NULL),
    TOG("enemy_ff",       "Enemy FF",        "A guard's stray shots hurt the chrs they hit.",       B_CHEAT, CHEAT_PORT_ENEMY_FF, NULL),
    TOG("unlock_doors",   "Unlock Doors",    "Every door opens for Bond.",                          B_CHEAT, CHEAT_PORT_UNLOCK_DOORS, NULL),
    TOG("coop_bot",       "Co-op Bot",       "A computer ally fights beside Bond.",                 B_OPT, 0, "coop_bot"),
    TOG("enemy_knives",   "Enemy Knives",    "Guards sometimes throw a knife instead of shooting.", B_FLAG, 0, NULL),
    TOG("one_hit_kills",  "One-Hit Kills",   "Any damage to a chr other than Bond is lethal.",      B_FLAG, 0, NULL),
    PCT("enemy_damage",   "Enemy Damage",    "Percent of the damage Bond takes."),
    PCT("enemy_health",   "Enemy Health",    "Percent of health chrs have (damage they take is scaled inversely)."),
    /* PORT: music. An INT whose value text is the choice: 0 Stock, 1 Random, n = track id n-1 (port_music.c reads it). */
    { { "level_music", "Level Music", PORT_MOD_INT, 0, M_END_SOMETHING + 1, 1, 0,
        "Stock, Random or a chosen track for the level theme (cosmetic: not a cheat run)." }, B_FLAG, 0, NULL, 1 },
    /* PORT: co-op bot skin. An INT whose value text is the choice: 0 Default (coop_bot_skin), 1 Random, 2 + k = roster skin k (port_bot.c reads it). */
    { { "bot_skin", "Bot Skin", PORT_MOD_INT, 0, PORT_BOT_SKIN_COUNT + 1, 1, 0,
        "Default (coop_bot_skin), Random or a chosen character for the co-op bot (cosmetic: not a cheat run)." }, B_FLAG, 0, NULL, 1 },
    /* PORT: co-op SESSION settings (job CO-OP-EXITS). Two Bonds only; the HOST's values rule a netplay session (the guest takes them from the
     * handshake and neither checks them against its own). Value text is the choice: both / first, start / partner, unlimited / N, player / party. */
    { { "coop_exits", "Co-op Exits", PORT_MOD_INT, 0, 1, 1, 0,
        "both: every living Bond must reach a level exit before the mission ends. first: the first Bond to reach it ends the mission (co-op only)." }, B_FLAG, 0, NULL, 1 },
    { { "coop_respawn", "Co-op Respawn", PORT_MOD_INT, 0, 1, 1, 0,
        "start: a dead Bond respawns at the level's start spawn. partner: beside the living partner. (A start that cannot reach the partner falls back to the partner.)" }, B_FLAG, 0, NULL, 1 },
    { { "coop_lives", "Co-op Lives", PORT_MOD_INT, 0, 99, 1, 0,
        "Lives each Bond (or the party) has; 0 = unlimited. With none left a dead Bond stays down and watches; the mission fails when nobody can continue." }, B_FLAG, 0, NULL, 1 },
    { { "coop_lives_mode", "Co-op Lives Mode", PORT_MOD_INT, 0, 1, 1, 0,
        "player: every Bond has its own lives. party: one pool of lives shared by both Bonds." }, B_FLAG, 0, NULL, 1 },
};

/* the four co-op session modifiers (their text names, the host's values travel in the netplay SETUP) */
static int modIsSessionName(const char *n)
{
    return strcmp(n, "coop_exits") == 0 || strcmp(n, "coop_respawn") == 0 || strcmp(n, "coop_lives") == 0 || strcmp(n, "coop_lives_mode") == 0;
}

#define MOD_COUNT ((int)(sizeof(sMods) / sizeof(sMods[0])))

#define KNIFE_SLOTS 64
typedef struct { s32 chrnum; s32 next; } KnifeSlot;
static KnifeSlot sKnifeSlot[KNIFE_SLOTS];   /* enemy_knives side table keyed by chrnum; a collision just resets that chr's cooldown */

/* Two layers (B_CHEAT / B_ROCKETS / B_FLAG; B_OPT lives in the option): the saved value (ini, console, menu)
 * and a per-stage override (Lua mods.set, GE_MODS) that portModifierStageStart clears. */
static int sSaved[MOD_COUNT];
static int sOvr[MOD_COUNT];
static u8  sHasOvr[MOD_COUNT];
static int sOptBase[MOD_COUNT];   /* B_OPT: the option's value before an override touched it */
static u8  sOwn[MOD_COUNT];       /* B_CHEAT: this modifier switched the cheat on, so only it may switch it off */
static int sOptIdx[MOD_COUNT];    /* B_OPT: portOptFind result, resolved lazily; -1 = none */
static int sLive;                 /* a solo stage is running and the 1 s cheat point has passed */
static int sEnvLogged;
static int sRocketsSet;           /* we set g_CheatActivated[ENEMY_ROCKETS] */
static int sFlagWeSet;            /* we raised g_AppendCheatSinglePlayer (the stock flag was FALSE) */

#define LAYER_SAVED    0
#define LAYER_OVERRIDE 1

static int modEff(int i)
{
    return portModEffective(sSaved[i], sHasOvr[i], sOvr[i]);
}

/* hot-path copies of the flag modifiers */
static int sKnives, sOneHit, sDamagePct = 100, sHealthPct = 100;

/* ---- registry ------------------------------------------------------------- */

int portModifierCount(void)
{
    return MOD_COUNT;
}

const PortModifierInfo *portModifierAt(int i)
{
    return (i >= 0 && i < MOD_COUNT) ? &sMods[i].info : NULL;
}

static char lc(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

/* The stored values start at each modifier's default (0, or 100 for the percent knobs). */
static void modInit(void)
{
    static int done;
    int i;

    if (done)
    {
        return;
    }
    done = 1;
    for (i = 0; i < MOD_COUNT; i++)
    {
        sSaved[i] = sMods[i].info.def;
    }
}

int portModifierFind(const char *name)
{
    int i;

    modInit();
    if (name == NULL || name[0] == '\0')
    {
        return -1;
    }
    for (i = 0; i < MOD_COUNT; i++)
    {
        const char *a = sMods[i].info.name;
        const char *b = name;

        while (*a != '\0' && lc(*a) == lc(*b))
        {
            a++;
            b++;
        }
        if (*a == '\0' && *b == '\0')
        {
            return i;
        }
    }
    return -1;
}

/* One test for every place the game state is touched: solo, no demo playing or recording. */
static int modSimOK(void)
{
    return getPlayerCount() == 1 && get_is_ramrom_flag() == 0 && get_recording_ramrom_flag() == 0;
}

static int modOptIndex(int i)
{
    if (sOptIdx[i] == 0)
    {
        sOptIdx[i] = portOptFind(sMods[i].optKey) + 1;   /* 0 = not looked up yet; +1 keeps -1 distinct */
    }
    return sOptIdx[i] - 1;
}

int portModifierGet(int i)
{
    modInit();
    if (i < 0 || i >= MOD_COUNT)
    {
        return 0;
    }
    if (sMods[i].backend == B_OPT)
    {
        int o = modOptIndex(i);

        return (o >= 0) ? (portOptGet(o) != 0) : 0;
    }
    return modEff(i);
}

static int modEqNoCase(const char *a, const char *b)
{
    while (*a != '\0' && lc(*a) == lc(*b))
    {
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

/* the value text of a co-op session modifier: both / first, start / partner, unlimited / N, player / party */
static void modSessionText(const char *name, int v, char *buf, int n)
{
    if (strcmp(name, "coop_exits") == 0)
    {
        snprintf(buf, (size_t)n, "%s", v ? "first" : "both");
    }
    else if (strcmp(name, "coop_respawn") == 0)
    {
        snprintf(buf, (size_t)n, "%s", v ? "partner" : "start");
    }
    else if (strcmp(name, "coop_lives") == 0)
    {
        if (v > 0)
        {
            snprintf(buf, (size_t)n, "%d", v);
        }
        else
        {
            snprintf(buf, (size_t)n, "unlimited");
        }
    }
    else
    {
        snprintf(buf, (size_t)n, "%s", v ? "party" : "player");
    }
}

static int modSessionParse(const char *name, const char *t, int *v)
{
    if (strcmp(name, "coop_exits") == 0 && (modEqNoCase("both", t) || modEqNoCase("first", t)))
    {
        *v = modEqNoCase("first", t);
        return 1;
    }
    if (strcmp(name, "coop_respawn") == 0 && (modEqNoCase("start", t) || modEqNoCase("partner", t)))
    {
        *v = modEqNoCase("partner", t);
        return 1;
    }
    if (strcmp(name, "coop_lives") == 0 && (modEqNoCase("unlimited", t) || modEqNoCase("off", t)))
    {
        *v = 0;
        return 1;
    }
    if (strcmp(name, "coop_lives_mode") == 0 && (modEqNoCase("player", t) || modEqNoCase("party", t)))
    {
        *v = modEqNoCase("party", t);
        return 1;
    }
    return 0;
}

void portModifierValueText(int i, char *buf, int n)
{
    if (i < 0 || i >= MOD_COUNT || n < 1)
    {
        if (n > 0)
        {
            buf[0] = '\0';
        }
        return;
    }
    if (sMods[i].info.kind == PORT_MOD_TOGGLE)
    {
        snprintf(buf, (size_t)n, "%s", portModifierGet(i) ? "on" : "off");
    }
    else if (strcmp(sMods[i].info.name, "level_music") == 0)
    {
        portMusicModText(portModifierGet(i), buf, n);
    }
    else if (strcmp(sMods[i].info.name, "bot_skin") == 0)
    {
        portBotSkinModText(portModifierGet(i), buf, n);
    }
    else if (modIsSessionName(sMods[i].info.name))
    {
        modSessionText(sMods[i].info.name, portModifierGet(i), buf, n);
    }
    else
    {
        snprintf(buf, (size_t)n, "%d", portModifierGet(i));
    }
}

/* A value from text: "on/off/<int>" (port_modparse.c), plus Stock / Random / a track name for level_music. */
static int modParseValue(int i, const char *text, int *v)
{
    if (strcmp(sMods[i].info.name, "level_music") == 0 && portMusicModParse(text, v))
    {
        return 1;
    }
    if (strcmp(sMods[i].info.name, "bot_skin") == 0 && portBotSkinModParse(text, v))
    {
        return 1;
    }
    if (modIsSessionName(sMods[i].info.name) && modSessionParse(sMods[i].info.name, text, v))
    {
        return 1;
    }
    return portModParseValue(text, v);
}

/* ---- backends --------------------------------------------------------------- */

static void modSyncFlags(void)
{
    sKnives = modEff(portModifierFind("enemy_knives"));
    sOneHit = modEff(portModifierFind("one_hit_kills"));
    sDamagePct = modEff(portModifierFind("enemy_damage"));
    sHealthPct = modEff(portModifierFind("enemy_health"));
}

static int modLiveOK(void)
{
    return sLive && modSimOK();
}

/* Turn a cheat on or off only when it differs, so a cheat the player or GE_CHEATS already set is
 * never switched off by a modifier that is off, and DK/Tiny never scale a model twice. */
static void modApplyCheat1(int i, int want);

/* Cheat bits are per player; in co-op a modifier's cheat goes on and off for every Bond. */
static void modApplyCheat(int i, int want)
{
    extern void set_cur_player(s32 playernum);
    extern s32 get_cur_playernum(void);

    if (portCoopActive())
    {
        const s32 saved = get_cur_playernum();
        const int own0 = sOwn[i];
        int own = own0;
        int k;

        for (k = 0; k < portAllPlayerCount(); k++)
        {
            sOwn[i] = own0;
            set_cur_player(k);
            modApplyCheat1(i, want);
            own |= sOwn[i];
        }
        set_cur_player(saved);
        sOwn[i] = want ? own : 0;
        return;
    }
    modApplyCheat1(i, want);
}

static void modApplyCheat1(int i, int want)
{
    const int cheat = sMods[i].cheat;
    const int on = cheatIsActive((CHEAT_ID)cheat) ? 1 : 0;

    if (want && !on)
    {
        cheatButtonHandleCheatsTurnedOn((CHEAT_ID)cheat);
        sOwn[i] = 1;
        portConLogf("ge: mod: cheat %d on (active now %d)\n", cheat, cheatIsActive((CHEAT_ID)cheat) ? 1 : 0);
    }
    else if (!want && on && sOwn[i])
    {   /* only a cheat this modifier switched on; one from the stock cheat menu stays */
        cheatButtonHandleCheatsTurnedOff((CHEAT_ID)cheat);
        sOwn[i] = 0;
        portConLogf("ge: mod: cheat %d off (active now %d)\n", cheat, cheatIsActive((CHEAT_ID)cheat) ? 1 : 0);
    }
    else if (!want)
    {
        sOwn[i] = 0;
    }
}

/* Cheat run, mirroring the stock cheat missions: front.c sets g_AppendCheatSinglePlayer when a cheat mission is
 * picked, and file.c end_of_mission_briefing / front.c then skip the completion, best time and unlock records.
 * Raised while any modifier except coop_bot is off its default; lowered again only if this module raised it
 * and no stock cheat is selected (g_CheatActivated, minus our own Enemy Rockets). */
static int modIsModded(void)
{
    int i;

    for (i = 0; i < MOD_COUNT; i++)
    {
        if (sMods[i].backend != B_OPT && !sMods[i].cosmetic && modEff(i) != sMods[i].info.def)
        {
            return 1;
        }
    }
    return 0;
}

static int modStockCheatSelected(void)
{
    int c;

    for (c = 1; c < CHEAT_INVALID; c++)
    {
        if (g_CheatActivated[c] && !(c == CHEAT_ENEMY_ROCKETS && sRocketsSet))
        {
            return 1;
        }
    }
    return 0;
}

static void modCheatRun(void)
{
    if (!modSimOK())
    {
        return;
    }
    if (modIsModded())
    {
        if (!g_AppendCheatSinglePlayer)
        {
            g_AppendCheatSinglePlayer = TRUE;
            sFlagWeSet = 1;
            portConLogf("ge: mod: cheat run (no completion or time is recorded)\n");
        }
    }
    else if (sFlagWeSet)
    {
        if (!modStockCheatSelected())
        {
            g_AppendCheatSinglePlayer = FALSE;
        }
        sFlagWeSet = 0;
        portConLogf("ge: mod: cheat run ended\n");
    }
}

/* mods= for the ini: the non-default values of the registry's own backends (coop_bot saves itself). */
static void modSave(void)
{
    char buf[256];
    const char *keys[1];
    const char *vals[1];
    int i, n = 0;

    buf[0] = '\0';
    for (i = 0; i < MOD_COUNT; i++)
    {
        if (sMods[i].backend == B_OPT || sSaved[i] == sMods[i].info.def)
        {   /* the saved layer only: a Lua or GE_MODS override never reaches the ini */
            continue;
        }
        n += snprintf(buf + n, sizeof(buf) - (size_t)n, "%s%s=%d", n ? "," : "", sMods[i].info.name, sSaved[i]);
        if (n >= (int)sizeof(buf) - 24)
        {
            break;
        }
    }
    keys[0] = "mods";
    vals[0] = buf;
    portIniWriteStrs("[display]", keys, vals, 1);
}

/* layer SAVED: ini, console, menu (write = also write midas.ini; an explicit set drops that modifier's override).
 * layer OVERRIDE: Lua, GE_MODS; cleared at every stage start. */
static int modSet(int i, int v, int layer, int write)
{
    const Mod *m;
    int before;

    modInit();
    if (i < 0 || i >= MOD_COUNT)
    {
        return 0;
    }
    before = modEff(i);
    m = &sMods[i];
    v = portModClamp(v, m->info.min, m->info.max, m->info.step);
    if (m->backend == B_OPT)
    {
        int o = modOptIndex(i);

        if (o < 0)
        {
            return 0;
        }
        if (layer == LAYER_OVERRIDE && !sHasOvr[i])
        {
            sOptBase[i] = portOptGet(o);
            sHasOvr[i] = 1;
        }
        else if (layer == LAYER_SAVED)
        {
            sHasOvr[i] = 0;
        }
        if (!(write ? portOptSetValue(o, v) : portOptSetValueQuiet(o, v)))
        {
            return 0;
        }
        portConLogf("ge: mod: %s = %d\n", m->info.name, v);
        return 1;
    }
    if (layer == LAYER_OVERRIDE)
    {
        sOvr[i] = v;
        sHasOvr[i] = 1;
    }
    else
    {
        sSaved[i] = v;
        sHasOvr[i] = 0;
    }
    modSyncFlags();
    if (m->backend == B_CHEAT && modLiveOK())
    {
        modApplyCheat(i, modEff(i));
    }
    if (modLiveOK() && modEff(i) != before)
    {
        modCheatRun();
    }
    {
        char vt[24];

        if (strcmp(m->info.name, "level_music") == 0)
        {
            portMusicModText(modEff(i), vt, sizeof(vt));
        }
        else if (strcmp(m->info.name, "bot_skin") == 0)
        {
            portBotSkinModText(modEff(i), vt, sizeof(vt));
        }
        else if (modIsSessionName(m->info.name))
        {
            modSessionText(m->info.name, modEff(i), vt, sizeof(vt));
        }
        else
        {
            snprintf(vt, sizeof(vt), "%d", modEff(i));
        }
        portConLogf("ge: mod: %s = %s%s%s\n", m->info.name, vt,
                    layer == LAYER_OVERRIDE ? " (this stage only)" : "",
                    (m->backend == B_ROCKETS) ? " (applies at the next stage load)" : (m->backend == B_CHEAT && !modLiveOK()) ? " (applies in the next stage)" : "");
    }
    if (write)
    {
        modSave();
    }
    return 1;
}

int portModifierSet(int i, int v)
{
    return modSet(i, v, LAYER_SAVED, 1);
}

/* An override for this stage (Lua mods.set): not saved, cleared at the next stage start. */
int portModifierSetQuiet(int i, int v)
{
    return modSet(i, v, LAYER_OVERRIDE, 0);
}

static int modApplyList(const char *list, int layer, int write)
{
    const char *p = list;
    char name[40], val[24];
    int applied = 0;

    if (list == NULL)
    {
        return 0;
    }
    while (portModListNext(&p, name, sizeof(name), val, sizeof(val)))
    {
        int i = portModifierFind(name);
        int v = 1;

        if (i < 0)
        {
            portConLogf("ge: mod: unknown modifier \"%s\"\n", name);
            continue;
        }
        if (val[0] != '\0' && !modParseValue(i, val, &v))
        {
            portConLogf("ge: mod: bad value \"%s\" for %s\n", val, name);
            continue;
        }
        if (modSet(i, v, layer, 0))
        {
            applied++;
        }
    }
    if (write && applied > 0)
    {
        modSave();
    }
    return applied;
}

/* save != 0: the saved layer, written to midas.ini. save == 0: a per-stage override, never written. */
int portModifierApplyList(const char *list, int save)
{
    return modApplyList(list, save ? LAYER_SAVED : LAYER_OVERRIDE, save);
}

/* The ini loader (video.c videoLoadConfig): the `mods=` key sets the saved layer, no write. 1 = consumed. */
int portModifierIsSession(int i)
{
    return i >= 0 && i < MOD_COUNT && modIsSessionName(sMods[i].info.name);
}

/* This machine's co-op session settings as text, "coop_exits=0,coop_respawn=0,coop_lives=0,coop_lives_mode=0" (numbers, all four, always):
 * the saved layer (ini, console, Mods page) with GE_MODS applied on top, even before the stage-start point that applies it. The netplay host
 * sends this in SETUP; local co-op reads it at the stage start. */
void portModifierSessionText(char *out, int cap)
{
    int val[MOD_COUNT];
    int i, n = 0;
    const char *e = getenv("GE_MODS");

    modInit();
    for (i = 0; i < MOD_COUNT; i++)
    {
        val[i] = modEff(i);
    }
    if (e != NULL && e[0] != '\0')
    {
        const char *p = e;
        char name[40], v[24];

        while (portModListNext(&p, name, sizeof(name), v, sizeof(v)))
        {
            int k = portModifierFind(name);
            int x = 1;

            if (k >= 0 && modIsSessionName(sMods[k].info.name) && (v[0] == '\0' || modParseValue(k, v, &x)))
            {
                val[k] = portModClamp(x, sMods[k].info.min, sMods[k].info.max, sMods[k].info.step);
            }
        }
    }
    out[0] = '\0';
    for (i = 0; i < MOD_COUNT; i++)
    {
        if (modIsSessionName(sMods[i].info.name))
        {
            n += snprintf(out + n, (size_t)(cap - n), "%s%s=%d", n > 0 ? "," : "", sMods[i].info.name, val[i]);
            if (n >= cap - 1)
            {
                return;
            }
        }
    }
}

int portModifierSetOption(const char *key, const char *value)
{
    if (key == NULL || strcmp(key, "mods") != 0)
    {
        return 0;
    }
    modApplyList(value, LAYER_SAVED, 0);
    return 1;
}

/* ---- stage lifecycle ----------------------------------------------------------- */

/* lvlStageLoad, before the level builds: GE_MODS (once per session), the load-time rockets. */
void portModifierStageStart(void)
{
    const char *e;
    int i;

    modInit();
    sLive = 0;
    memset(sKnifeSlot, 0, sizeof(sKnifeSlot));
    memset(sOwn, 0, sizeof(sOwn));   /* the stage load clears every cheat */
    portMusicStageStart();           /* PORT: music.set overrides end with the stage */
    for (i = 0; i < MOD_COUNT; i++)
    {   /* the Lua/GE_MODS overrides of the last stage end here; this runs before the scripts load */
        if (sHasOvr[i] && sMods[i].backend == B_OPT && modOptIndex(i) >= 0)
        {
            portOptSetValueQuiet(modOptIndex(i), sOptBase[i]);
        }
        sHasOvr[i] = 0;
    }
    e = getenv("GE_MODS");   /* a launch-time choice: an override again at every stage */
    if (e != NULL && e[0] != '\0')
    {
        if (!sEnvLogged)
        {
            portConLogf("ge: mod: GE_MODS=%s\n", e);
            sEnvLogged = 1;
        }
        modApplyList(e, LAYER_OVERRIDE, 0);
    }
    modSyncFlags();
    if (modEff(portModifierFind("enemy_rockets")) && modSimOK())
    {   /* the same state GE_CHEATS=rockets sets (lv.c portCheatApplyLoadTimeSafe); the cheat-run flag is modCheatRun's */
        g_CheatActivated[CHEAT_ENEMY_ROCKETS] = TRUE;
        cheatButtonHandleCheatsTurnedOn(CHEAT_ENEMY_ROCKETS);
        sRocketsSet = 1;
    }
    else if (sRocketsSet)
    {
        g_CheatActivated[CHEAT_ENEMY_ROCKETS] = FALSE;
        sRocketsSet = 0;
    }
    modCheatRun();
}

/* lv.c portCheatTick, about a second into the stage: the player is live, so the cheats go on. */
void portModifierStageLive(void)
{
    int i, any = 0;

    if (!modSimOK())
    {
        return;
    }
    sLive = 1;
    for (i = 0; i < MOD_COUNT; i++)
    {
        if (sMods[i].backend == B_CHEAT && modEff(i))
        {
            modApplyCheat(i, 1);
            any = 1;
        }
    }
    modCheatRun();
    if (any || sKnives || sOneHit || sDamagePct != 100 || sHealthPct != 100)
    {
        portConLogf("ge: mod: live on stage start\n");
    }
}

/* lvlUnloadStageTextData: the cheats are cleared there, so a late set only stores. */
void portModifierStageEnd(void)
{
    sLive = 0;
}

/* ---- hooks read by src/ ----------------------------------------------------------- */

/* one_hit_kills and enemy_health touch enemies only: not Bond's team (the co-op bot) and not hostages. */
static int modChrIsEnemy(ChrRecord *chr)
{
    s32 f = portFactionOfChr(chr);

    return f != PORT_FACTION_BOND && f != PORT_FACTION_HOSTAGE;
}

int portModOneHitKills(ChrRecord *chr)
{
    return sOneHit && modSimOK() && modChrIsEnemy(chr);
}

/* Bond's incoming damage scale (record_damage_kills); exactly 1.0f at the default. */
f32 portModEnemyDamageScale(void)
{
    return (sDamagePct == 100 || !modSimOK()) ? 1.0f : (f32)sDamagePct / 100.0f;
}

/* Chr damage-taken scale (handles_shot_actors): health 200% means half the damage. */
f32 portModChrDamageScale(ChrRecord *chr)
{
    return (sHealthPct == 100 || !modSimOK() || !modChrIsEnemy(chr)) ? 1.0f : 100.0f / (f32)sHealthPct;
}

/* ---- enemy knives ------------------------------------------------------------------ */

#define KNIFE_MIN_DIST   150.0f
#define KNIFE_MAX_DIST   900.0f
#define KNIFE_COOLDOWN   180    /* ticks between throws by one chr (3 s) */
#define KNIFE_RETRY      30     /* ticks until a chr that declined may try again */

static u32 sKnifeRng = 0x2545F491u;

static u32 knifeRand(void)
{
    sKnifeRng = sKnifeRng * 1664525u + 1013904223u;   /* private, never the game's RNG */
    return sKnifeRng >> 16;
}

/* The owner of a flying knife is an enemy chr while enemy_knives is on: its knife may hit Bond (propobj.c). */
extern s32 portArenaChrKnifer(const ChrRecord *chr);   /* port_arenamode.c: a chr the arena mode made a knife thrower */

int portModEnemyKnifeOwner(PropRecord *ownerprop)
{
    return ownerprop != NULL && ownerprop->type == PROP_TYPE_CHR && ownerprop->chr != NULL &&
           (sKnives || portArenaChrKnifer(ownerprop->chr)) && !portFactionPropIsBondTeam(ownerprop) && modSimOK();
}

/* chrlvTriggerFireWeapon: TRUE = this shot became a thrown knife (the caller drops the shot). The target is the
 * chr the guard is engaged with (the bot), else Bond when the guard is attacking him and has seen him lately. */
static int modEnemyKnifeThrow(ChrRecord *self)
{
    PropRecord *tp;
    ChrRecord *eng;
    KnifeSlot *slot;
    f32 dx, dy, dz, d;
    s32 now = g_GlobalTimer;
    int tid;
    const s32 mine = portArenaChrKnifer(self);   /* an arena knife thrower: every eligible shot, shorter cooldown */

    if ((!sKnives && !mine) || self == NULL || self->prop == NULL || self->model == NULL || !modSimOK())
    {
        return FALSE;
    }
    if (self->actiontype != ACT_ATTACK && self->actiontype != ACT_ATTACKWALK && self->actiontype != ACT_ATTACKROLL)
    {
        return FALSE;
    }
    if (portFactionPropIsBondTeam(self->prop))
    {
        return FALSE;   /* the bot throws its own */
    }
    eng = portFactionEngagedChr(self);
    if (eng != NULL)
    {
        if (eng->prop == NULL || chrIsDead(eng))
        {
            return FALSE;
        }
        if (self->actiontype == ACT_ATTACK &&
            ((self->act_attack.attacktype & TARGET_BOND) || self->act_attack.entityid != (u32)eng->chrnum))
        {
            return FALSE;   /* aimed at something else */
        }
        tp = eng->prop;
        tid = (int)eng->chrnum;
    }
    else
    {
        if (self->actiontype == ACT_ATTACK && !(self->act_attack.attacktype & TARGET_BOND))
        {
            return FALSE;
        }
        if (self->seen_bond_time < now - CHRLV_SEEN_RECENT_CHECK)
        {
            return FALSE;
        }
        tp = getCurrentPlayerProp();
        tid = -2;
        if (tp == NULL)
        {
            return FALSE;
        }
    }
    dx = tp->pos.x - self->prop->pos.x;
    dy = tp->pos.y - self->prop->pos.y;
    dz = tp->pos.z - self->prop->pos.z;
    d = sqrtf(dx * dx + dy * dy + dz * dz);
    {   /* an arena thrower has its own range (spawn.chr knife_range, default 4500) and cooldown (knife_cooldown seconds, default 1.2) */
        f32 maxd = KNIFE_MAX_DIST;

        if (mine)
        {
            extern s32 portArenaKnifeParams(const ChrRecord *chr, f32 *range, f32 *cooldownTicks);
            f32 cdt;

            portArenaKnifeParams(self, &maxd, &cdt);
        }
        if (d < KNIFE_MIN_DIST || d > maxd)
        {
            return FALSE;
        }
    }
    slot = &sKnifeSlot[(u32)self->chrnum & (KNIFE_SLOTS - 1)];
    if (slot->chrnum != (s32)self->chrnum)
    {
        slot->chrnum = (s32)self->chrnum;
        slot->next = 0;
    }
    if (now < slot->next)
    {
        return FALSE;
    }
    if (!mine && (knifeRand() & 1u))
    {   /* half the eligible shots stay shots */
        slot->next = now + KNIFE_RETRY;
        return FALSE;
    }
    if (!chrIsNotDeadOrShot(self) || !portChrThrowKnife(self, tp))
    {
        slot->next = now + KNIFE_RETRY;
        return FALSE;
    }
    {
        s32 cd;

        if (mine)
        {
            extern s32 portArenaKnifeParams(const ChrRecord *chr, f32 *range, f32 *cooldownTicks);
            f32 rng, cdt;

            portArenaKnifeParams(self, &rng, &cdt);
            cd = (s32)cdt + (s32)(knifeRand() % (u32)((cdt / 3.0f) + 1.0f));
        }
        else
        {
            cd = KNIFE_COOLDOWN + (s32)(knifeRand() % 60u);
        }

        if (mine && self->model->playspeed > 1.0f)
        {
            cd = (s32)((f32)cd / self->model->playspeed);   /* a turbo thrower (spawn.chr anim_speed) also recovers faster */
        }
        slot->next = now + cd;
    }
    portConLogf("ge: mod: enemy_knives chr %d threw at %d dist %.0f\n", (int)self->chrnum, tid, d);
    return TRUE;
}

/* A7: an arena chr flagged knife-only (the KNIVES ONLY wave mutator, port_arenafx.c) never fires a bullet: a shot that does not
 * become a knife is dropped too. */
int portModEnemyKnife(ChrRecord *self)
{
    extern s32 portArenaChrKnifeOnly(const ChrRecord *chr);

    if (modEnemyKnifeThrow(self))
    {
        return TRUE;
    }
    return self != NULL && portArenaChrKnifeOnly(self) && modSimOK();
}

/* ---- console ------------------------------------------------------------------------ */

static void modPrintRow(int i)
{
    const PortModifierInfo *m = &sMods[i].info;
    char v[24];

    portModifierValueText(i, v, sizeof(v));
    if (m->kind == PORT_MOD_INT)
    {
        portConLogf("ge: mod: " "%-14s %-4s %d..%d/%d  %s\n", m->name, v, m->min, m->max, m->step, m->desc);
    }
    else
    {
        portConLogf("ge: mod: " "%-14s %-4s %s\n", m->name, v, m->desc);
    }
}

/* `mod list`, `mod <name>`, `mod <name> on|off|<n>` (port_console.c). */
void portModifierConsole(const char *arg1, const char *arg2)
{
    int i, v;

    if (arg1 == NULL || strcmp(arg1, "list") == 0)
    {
        for (i = 0; i < MOD_COUNT; i++)
        {
            modPrintRow(i);
        }
        return;
    }
    i = portModifierFind(arg1);
    if (i < 0)
    {
        portConLogf("ge: mod: " "no modifier \"%s\" - try: mod list\n", arg1);
        return;
    }
    if (arg2 == NULL || arg2[0] == '\0')
    {
        modPrintRow(i);
        return;
    }
    if (!modParseValue(i, arg2, &v))
    {
        portConLogf("ge: mod: " "bad value \"%s\" - use on, off, a number (or stock, random, a track name)\n", arg2);
        return;
    }
    if (portModifierSet(i, v))
    {
        modPrintRow(i);
    }
    else
    {
        portConLogf("ge: mod: " "could not set %s\n", sMods[i].info.name);
    }
}

#endif /* TARGET_N64 */
