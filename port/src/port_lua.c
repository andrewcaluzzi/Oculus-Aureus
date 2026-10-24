/* port_lua.c - the game-facing half of the Lua scripting layer (see port_lua.h and
 * modding/lua.md). It sees the game's headers, which shadow the C library's
 * (include/limits.h, math.h ...), so it holds no Lua code: port/luavm/port_luavm.c does,
 * in its own library built against the real libc headers, and calls the plain-C helpers here.
 *
 * Here: the solo/no-demo gate, the Data dir, and the WeaponStats table (field offsets,
 * item names, in-place access, the stock snapshot). */
#ifndef TARGET_N64

#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <ultra64.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include "game/gun.h"
#include "port_lua.h"
#include "port_mode.h"
#include "port_blocks.h"
#include "port_hostage.h"
#include "port_coop.h"
#include <stdlib.h>

typedef char portlua_hostage_max_matches[(PORTLUA_HOSTAGE_MAX == PORT_HOSTAGE_MAX) ? 1 : -1];

extern WeaponStats default_weaponstats;   /* assets/obseg/gun/gunWeaponStats.inc.c */
extern const char *portRootPath(const char *name);   /* main.c */
extern s32 getPlayerCount(void);
extern s32 get_is_ramrom_flag(void);
extern s32 get_recording_ramrom_flag(void);

/* Same predicate as the co-op bot: solo, no demo playing or recording. */
int portLuaGameAllowed(void)
{
    return getPlayerCount() == 1 && get_is_ramrom_flag() == 0 && get_recording_ramrom_flag() == 0;
}

const char *portLuaGameScriptDir(void)
{
    return portRootPath("Data/Scripts");
}

/* ---- fields ------------------------------------------------------------ */

typedef struct { const char *name; size_t off; int type; } WField;

#define F(alias, member, type) { alias, offsetof(WeaponStats, member), type }
static const WField sFields[] = {
    F("magsize", MagSize, PORTLUA_S16),
    F("firerate", AutomaticFiringRate, PORTLUA_U8),
    F("singlerate", SingleFiringRate, PORTLUA_S8),
    F("penetration", ObjectsShootThrough, PORTLUA_U8),
    F("sound", Sound, PORTLUA_U16),
    F("damage", DestructionAmount, PORTLUA_F32),
    F("inaccuracy", Inaccuracy, PORTLUA_F32),
    F("zoom", Zoom, PORTLUA_F32),
    F("crosshairspeed", CrosshairSpeed, PORTLUA_F32),
    F("aimlockspeed", AimLockSpeed, PORTLUA_F32),
    F("sway", Sway, PORTLUA_F32),
    F("recoilback", RecoilBack, PORTLUA_F32),
    F("recoilup", RecoilUp, PORTLUA_F32),
    F("boltrecoil", BoltRecoilBack, PORTLUA_F32),
    F("loudnessmin", LoudnessMin, PORTLUA_F32),
    F("loudnessmax", LoudnessMax, PORTLUA_F32),
    F("noiseshot", NoiseIncreasePerShot, PORTLUA_F32),
    F("impact", ForceOfImpact, PORTLUA_F32),
    F("ammotype", AmmoType, PORTLUA_S32),
};
#undef F

int portLuaGameFieldCount(void)
{
    return (int) (sizeof(sFields) / sizeof(sFields[0]));
}

int portLuaGameField(int i, const char **name, size_t *off, int *type)
{
    if (i < 0 || i >= portLuaGameFieldCount())
    {
        return 0;
    }
    *name = sFields[i].name;
    *off = sFields[i].off;
    *type = sFields[i].type;
    return 1;
}

/* ---- items ------------------------------------------------------------- */

typedef struct { const char *name; int id; } WItem;

/* The ITEM_IDS enum names without the prefix; the aliases are the in-game names. Lua lowercases. */
#define ITEM_LIST \
    X(FIST) X(KNIFE) X(THROWKNIFE) X(WPPK) X(WPPKSIL) X(TT33) X(SKORPION) X(AK47) X(UZI) \
    X(MP5K) X(MP5KSIL) X(SPECTRE) X(M16) X(FNP90) X(SHOTGUN) X(AUTOSHOT) X(SNIPERRIFLE) \
    X(RUGER) X(GOLDENGUN) X(SILVERWPPK) X(GOLDWPPK) X(LASER) X(WATCHLASER) X(GRENADELAUNCH) \
    X(ROCKETLAUNCH) X(GRENADE) X(TIMEDMINE) X(PROXIMITYMINE) X(REMOTEMINE) X(TRIGGER) \
    X(TASER) X(TANKSHELLS) X(BOMBCASE) X(PLASTIQUE) X(FLAREPISTOL) X(PITONGUN) X(BUNGEE) \
    X(DOORDECODER) X(BOMBDEFUSER) X(CAMERA) X(LOCKEXPLODER) X(DOOREXPLODER) X(BRIEFCASE) \
    X(WEAPONCASE) X(SAFECRACKERCASE) X(KEYANALYSERCASE) X(BUG) X(MICROCAMERA) X(BUGDETECTOR) \
    X(EXPLOSIVEFLOPPY) X(POLARIZEDGLASSES) X(DARKGLASSES) X(CREDITCARD) X(GASKEYRING) \
    X(DATATHIEF) X(WATCHIDENTIFIER) X(WATCHCOMMUNICATOR) X(WATCHGEIGERCOUNTER) \
    X(WATCHMAGNETREPEL) X(WATCHMAGNETATTRACT) X(GOLDENEYEKEY) X(BLACKBOX) X(CIRCUITBOARD) \
    X(CLIPBOARD) X(STAFFLIST) X(DOSSIERRED) X(PLANS) X(SPYFILE) X(BLUEPRINTS) X(MAP) \
    X(AUDIOTAPE) X(VIDEOTAPE) X(DATTAPE) X(SPOOLTAPE) X(MICROFILM) X(MICROCODE) X(LECTRE) \
    X(MONEY) X(GOLDBAR) X(HEROIN) X(KEYCARD) X(KEYYALE) X(KEYBOLT) X(SUIT_LF_HAND) X(JOYPAD) \
    X(ROCKETROUND) X(GRENADEROUND) X(TOKEN)

#define X(n) { #n, ITEM_##n },
static const WItem sItems[] = {
    ITEM_LIST
    { "PP7", ITEM_WPPK }, { "PP7SIL", ITEM_WPPKSIL }, { "DD44", ITEM_TT33 }, { "KLOBB", ITEM_SKORPION },
    { "KF7", ITEM_AK47 }, { "ZMG", ITEM_UZI }, { "AR33", ITEM_M16 }, { "RCP90", ITEM_FNP90 },
};
#undef X

static WeaponStats *portLuaStatsFor(int id)
{
    WeaponStats *ws = get_ptr_item_statistics((ITEM_IDS) id);

    return ws == &default_weaponstats ? NULL : ws;
}

int portLuaGameItemCount(void)
{
    return (int) (sizeof(sItems) / sizeof(sItems[0]));
}

/* The i'th item's name (upper case), or NULL when it has no stats struct of its own. */
const char *portLuaGameItemName(int i)
{
    if (i < 0 || i >= portLuaGameItemCount() || portLuaStatsFor(sItems[i].id) == NULL)
    {
        return NULL;
    }
    return sItems[i].name;
}

/* Index of the named item (upper case, no ITEM_ prefix), or -1. */
int portLuaGameItemFind(const char *upper)
{
    int i;

    for (i = 0; i < portLuaGameItemCount(); i++)
    {
        if (strcmp(sItems[i].name, upper) == 0)
        {
            return i;
        }
    }
    return -1;
}

/* The live stats struct for item index i, or NULL (the shared default struct is refused). */
void *portLuaGameStats(int i)
{
    return (i < 0 || i >= portLuaGameItemCount()) ? NULL : (void *) portLuaStatsFor(sItems[i].id);
}

/* ---- stock snapshot ---------------------------------------------------- */

static WeaponStats sStock[ITEM_IDS_MAX];
static u8 sStockValid[ITEM_IDS_MAX];
static s32 sSnapped;

void portLuaGameSnapshot(void)
{
    int i;

    if (sSnapped)
    {
        return;
    }
    sSnapped = 1;
    for (i = 0; i < ITEM_IDS_MAX; i++)
    {
        WeaponStats *ws = portLuaStatsFor(i);

        if (ws != NULL)
        {
            sStock[i] = *ws;
            sStockValid[i] = 1;
        }
    }
}

/* Restores every weapon to the snapshot; a no-op if none was ever taken. */
void portLuaGameWeaponReset(void)
{
    int i;

    for (i = 0; sSnapped && i < ITEM_IDS_MAX; i++)
    {
        if (sStockValid[i])
        {
            *portLuaStatsFor(i) = sStock[i];
        }
    }
}

/* ---- mode bridge (step 2): thin wrappers over port_mode / port_blocks / port_hostage ----
 * Everything that changes the simulation checks portModeActive(), so a script that runs outside a
 * mode (a Data/Scripts/*.lua at a normal stage start) gets nil/0 back and nothing happens. */

extern s32 portStageRunning(void);   /* front.c: a stage is loading or running (not the menus) */
int portLuaGameStageLive(void) { return portStageRunning() != 0; }

int portLuaGameModeActive(void) { return portModeActive(); }
const char *portLuaGameModeName(void) { return portModeName(); }

int portLuaGameItemId(const char *upper)
{
    int i = portLuaGameItemFind(upper);

    return i < 0 ? -1 : sItems[i].id;
}

int portLuaGamePadCount(void) { return portModeActive() ? (int) portPadCount() : 0; }

int portLuaGamePadPos(int pad, float *x, float *y, float *z)
{
    coord3d p;

    if (!portModeActive() || !portPadPos(pad, &p))
    {
        return 0;
    }
    *x = p.x; *y = p.y; *z = p.z;
    return 1;
}

int portLuaGamePadList(float x, float z, float mindist, float maxdist, int wponly, int *out, int max)
{
    coord3d from;

    if (!portModeActive())
    {
        return 0;
    }
    from.x = x; from.y = 0.0f; from.z = z;
    return (int) portPadList(&from, mindist, maxdist, wponly, out, max);
}

/* GE_LUA_TEST_LEAKLOCAL=1 (test only, a negative control): the no-argument bond.* forms read the LOCAL player again, as before the
 * modes-in-co-op job. A hostage session with that set on one machine must desync (net_lockstep.sh phase 41). */
static int luaLeakLocal(void)
{
    static int on = -1;

    if (on < 0)
    {
        const char *e = getenv("GE_LUA_TEST_LEAKLOCAL");

        on = e != NULL && e[0] != '\0' && e[0] != '0';
    }
    return on;
}

/* The Bond a bond.*() call means: an explicit index, else (the no-argument form) player 0 on every machine. */
static int luaBond(int player)
{
    if (player >= 0)
    {
        return player;
    }
    return luaLeakLocal() ? (int) portLocalPlayer() : 0;
}

int portLuaGameBondCount(void) { return portModeActive() ? (int) portBondCount() : 0; }

int portLuaGameBondPos(int player, float *x, float *y, float *z)
{
    coord3d p;

    if (!portModeActive() || !portBondPosOf(luaBond(player), &p))
    {
        return 0;
    }
    *x = p.x; *y = p.y; *z = p.z;
    return 1;
}

int portLuaGameBondDead(int player)
{
    int p = luaBond(player);

    if (!portModeActive() || p < 0 || p >= (int) portBondCount())
    {
        return -1;
    }
    return portBondDeadOf(p) ? 1 : 0;
}

int portLuaGameBondNearest(float x, float y, float z, float *dist)
{
    float d = 0.0f;
    int p;

    if (!portModeActive())
    {
        return -1;
    }
    p = (int) portBondNearest(x, y, z, &d);
    if (p >= 0 && dist != NULL)
    {
        *dist = d;
    }
    return p;
}

int portLuaGameBondAllDown(void) { return portModeActive() && portBondAllDown() ? 1 : 0; }

int portLuaGameCoop(void) { return portCoopActive() ? 1 : 0; }

/* A script's math.random in co-op comes from the session seed (port_luavm.c seeds its own generator from this at every stage start),
 * so both machines draw the same numbers. 0 = solo: any seed will do. */
unsigned portLuaGameRandomSeed(void)
{
    unsigned s;

    if (!portCoopActive())
    {
        return 0u;
    }
    s = portCoopSessionSeed() ^ 0x4D4F4445u;
    return s != 0u ? s : 1u;
}

int portLuaGameSessionMod(const char *name, int *out)
{
    return portCoopSessionGet(name, out);
}

int portLuaGameSpawnHostage(int pad) { return portModeActive() ? (int) portHostageSpawn(pad) : -1; }
int portLuaGameSpawnGuard(int pad, int patrol, int item) { return portModeActive() ? (int) portGuardSpawn(pad, patrol, item) : -1; }

int portLuaGameZoneAdd(const char *name, float x, float y, float z, float r)
{
    return portModeActive() ? (int) portZoneAdd(name, x, y, z, r) : -1;
}
void portLuaGameZoneMove(int zone, float x, float y, float z) { if (portModeActive()) portZoneMove(zone, x, y, z); }
void portLuaGameZoneRemove(int zone) { if (portModeActive()) portZoneRemove(zone); }
int portLuaGameZoneWatch(int chr) { return portModeActive() ? (int) portZoneWatchChr(chr) : 0; }
int portLuaGameZoneContains(int zone, int chr)
{
    /* chr -2 / -3 name a Bond (bond.id(p)); the old CHR_BOND constant is player 0, never the local player */
    return portModeActive() ? (int) portZoneContains(zone, chr) : 0;
}
int portLuaGameZoneBonds(int zone) { return portModeActive() ? (int) portZoneBondMask(zone) : 0; }
const char *portLuaGameZoneName(int zone) { return portModeActive() ? portZoneName(zone) : NULL; }

void portLuaGameHudSet(int player, int line, const char *text) { if (portModeActive()) portModeHudSetFor(player, line, text); }
void portLuaGameHudClear(int player) { if (portModeActive()) portModeHudClearFor(player); }
void portLuaGameHudPanel(int player, int line, const char *text) { if (portModeActive()) portModeHudPanelSet(player, line, text); }
void portLuaGameHudPanelClear(int player) { if (portModeActive()) portModeHudPanelClear(player); }
const char *portLuaGameHudPanelGet(int line) { return portModeHudPanelGetLocal(line); }

int portLuaGameHostageList(int *chrs, int *states, int *leaders, int max)
{
    s32 c[PORT_HOSTAGE_MAX], s[PORT_HOSTAGE_MAX];
    int i, n;

    if (!portModeActive())
    {
        return 0;
    }
    n = (int) portHostageList(c, s, PORT_HOSTAGE_MAX);
    for (i = 0; i < n && i < max; i++)
    {
        chrs[i] = (int) c[i];
        states[i] = (int) s[i];
        leaders[i] = (int) portHostageLeader(c[i]);
    }
    return i;
}
int portLuaGameHostageState(int chr) { return portModeActive() ? (int) portHostageState(chr) : -1; }
/* player < 0 (the script named none): player 0, never the local one */
int portLuaGameHostageSet(int chr, int state, int player)
{
    return portModeActive() ? (int) portHostageSetStateBy(chr, state, player < 0 ? 0 : player) : 0;
}
int portLuaGameHostageExtract(int chr) { return portModeActive() ? (int) portHostageExtract(chr) : 0; }

int portLuaGameMissionEnded(void) { return portModeEndResult() != 0; }

int portLuaGameEndMission(int win) { return (int) portModeEndMission(win); }

#endif /* TARGET_N64 */
