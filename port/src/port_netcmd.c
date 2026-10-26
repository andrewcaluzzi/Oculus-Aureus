/* port_netcmd.c - session commands: `give` and the game side of every command (netplay job 5).
 *
 * A session command is a game command (spawn guards, give an item) that must change both machines' worlds identically. In netplay it
 * is carried by port_net.c and applied by portSessionCmdExec at the start of an agreed sim tick (boss.c -> portCoopDeathTick);
 * solo and local co-op apply it at once. Everything in portSessionCmdExec reads only sim state and the command, never the window
 * or the local player. */
#ifndef TARGET_N64

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ultra64.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include "game/player.h"
#include "game/bondinv.h"
#include "game/chrai.h"
#include "game/gun.h"
#include "game/propobj.h"
#include "port_net.h"
#include "port_arena.h"

extern void portConLogf(const char *fmt, ...);                  /* port_console.c: log + console */
extern void portConPrintf(int level, const char *fmt, ...);      /* console only */
extern int portCoopActive(void);
extern int portLocalPlayer(void);
extern int portAllPlayerCount(void);
extern s32 get_ammo_type_for_weapon(ITEM_IDS weapon);
extern s32 check_cur_player_ammo_amount_in_inventory(AMMOTYPE type);
extern s32 get_max_ammo_for_type(s32 type);
extern TICKOP propPickupByPlayer(PropRecord *prop, bool showstring);
extern void propExecuteTickOperation(PropRecord *prop, TICKOP op);

typedef struct { const char *name; int id; } GiveItem;
#define N(x) { #x, x }
static const GiveItem sItems[] = {
    N(ITEM_KNIFE), N(ITEM_THROWKNIFE), N(ITEM_WPPK), N(ITEM_WPPKSIL), N(ITEM_TT33), N(ITEM_SKORPION), N(ITEM_AK47), N(ITEM_UZI), N(ITEM_MP5K),
    N(ITEM_MP5KSIL), N(ITEM_SPECTRE), N(ITEM_M16), N(ITEM_FNP90), N(ITEM_SHOTGUN), N(ITEM_AUTOSHOT), N(ITEM_SNIPERRIFLE), N(ITEM_RUGER),
    N(ITEM_GOLDENGUN), N(ITEM_SILVERWPPK), N(ITEM_GOLDWPPK), N(ITEM_LASER), N(ITEM_WATCHLASER), N(ITEM_GRENADELAUNCH), N(ITEM_ROCKETLAUNCH),
    N(ITEM_GRENADE), N(ITEM_TIMEDMINE), N(ITEM_PROXIMITYMINE), N(ITEM_REMOTEMINE), N(ITEM_TRIGGER), N(ITEM_TASER), N(ITEM_TANKSHELLS),
    N(ITEM_BOMBCASE), N(ITEM_PLASTIQUE), N(ITEM_FLAREPISTOL), N(ITEM_PITONGUN), N(ITEM_BUNGEE), N(ITEM_DOORDECODER), N(ITEM_BOMBDEFUSER),
    N(ITEM_CAMERA), N(ITEM_LOCKEXPLODER), N(ITEM_DOOREXPLODER), N(ITEM_BRIEFCASE), N(ITEM_WEAPONCASE), N(ITEM_SAFECRACKERCASE),
    N(ITEM_KEYANALYSERCASE), N(ITEM_BUG), N(ITEM_MICROCAMERA), N(ITEM_BUGDETECTOR), N(ITEM_EXPLOSIVEFLOPPY), N(ITEM_POLARIZEDGLASSES),
    N(ITEM_DARKGLASSES), N(ITEM_CREDITCARD), N(ITEM_GASKEYRING), N(ITEM_DATATHIEF), N(ITEM_WATCHIDENTIFIER), N(ITEM_WATCHCOMMUNICATOR),
    N(ITEM_WATCHGEIGERCOUNTER), N(ITEM_WATCHMAGNETREPEL), N(ITEM_WATCHMAGNETATTRACT), N(ITEM_GOLDENEYEKEY), N(ITEM_BLACKBOX),
    N(ITEM_CIRCUITBOARD), N(ITEM_CLIPBOARD), N(ITEM_STAFFLIST), N(ITEM_DOSSIERRED), N(ITEM_PLANS), N(ITEM_SPYFILE), N(ITEM_BLUEPRINTS),
    N(ITEM_MAP), N(ITEM_AUDIOTAPE), N(ITEM_VIDEOTAPE), N(ITEM_DATTAPE), N(ITEM_SPOOLTAPE), N(ITEM_MICROFILM), N(ITEM_MICROCODE),
    N(ITEM_LECTRE), N(ITEM_MONEY), N(ITEM_GOLDBAR), N(ITEM_HEROIN), N(ITEM_ROCKETROUND), N(ITEM_GRENADEROUND), N(ITEM_TOKEN),
};
#undef N
#define NITEMS ((int) (sizeof(sItems) / sizeof(sItems[0])))

/* the in-game names, as friendly aliases */
static const struct { const char *alias; int id; } sAliases[] = {
    { "kf7", ITEM_AK47 }, { "kf7soviet", ITEM_AK47 }, { "pp7", ITEM_WPPK }, { "pp7silenced", ITEM_WPPKSIL }, { "silencedpp7", ITEM_WPPKSIL },
    { "klobb", ITEM_SKORPION }, { "dd44", ITEM_TT33 }, { "dostovei", ITEM_TT33 }, { "zmg", ITEM_UZI }, { "d5k", ITEM_MP5K }, { "d5ksilenced", ITEM_MP5KSIL },
    { "phantom", ITEM_SPECTRE }, { "ar33", ITEM_M16 }, { "rcp90", ITEM_FNP90 }, { "sniper", ITEM_SNIPERRIFLE }, { "cougar", ITEM_RUGER },
    { "magnum", ITEM_RUGER }, { "goldengun", ITEM_GOLDENGUN }, { "moonraker", ITEM_LASER }, { "rocket", ITEM_ROCKETLAUNCH },
    { "grenadelauncher", ITEM_GRENADELAUNCH }, { "gekey", ITEM_GOLDENEYEKEY }, { "dat", ITEM_DATTAPE },
};

/* key props (PROPDEF_KEY objects of the level) are not ITEM_ ids: keycards, Yale and bolt keys, the keys of a mission */
enum { KEYF_ANY = -1, KEYF_CARD = -2 };
static const struct { const char *name; int filter; } sKeyFamilies[] = {
    { "key", KEYF_ANY }, { "keycard", KEYF_CARD }, { "keyyale", PROP_CHRKEYYALE }, { "keybolt", PROP_CHRKEYBOLT },
};

static int itemKnown(int id)
{
    int i;

    for (i = 0; i < NITEMS; i++) {
        if (sItems[i].id == id) {
            return 1;
        }
    }
    return 0;
}

static const char *itemName(int id)
{
    int i;

    for (i = 0; i < NITEMS; i++) {
        if (sItems[i].id == id) {
            return sItems[i].name + 5;   /* without "ITEM_" */
        }
    }
    return "?";
}

/* The rounds a pickup of this weapon gives (propobj.c ammo_collected_from_weapon without the solo multiplier). */
static int pickupAmmo(int type)
{
    switch (type) {
    case AMMO_9MM: case AMMO_9MM_2: case AMMO_RIFLE: return 10;
    case AMMO_SHOTGUN: case AMMO_MAGNUM: return 5;
    case AMMO_GGUN: case AMMO_GRENADEROUND: return 3;
    case AMMO_DARTS: return 4;
    default: return 1;
    }
}

/* ---- the game side of a command ------------------------------------------------------------------------------------------ */

static void giveItem(int player, int id, int ammo)
{
    s32 saved = get_cur_playernum();
    struct player *pl = (player >= 0 && player < 4) ? g_playerPointers[player] : NULL;
    int type, added;

    if (!itemKnown(id)) {
        portConLogf("ge: give: unknown item id %d\n", id);
        return;
    }
    if (pl == NULL || pl->prop == NULL || pl->bonddead) {
        portConLogf("ge: give: player %d is not alive here: nothing given\n", player + 1);
        return;
    }
    set_cur_player(player);
    added = bondinvAddInvItem((ITEM_IDS) id);
    type = (int) get_ammo_type_for_weapon((ITEM_IDS) id);
    if (type > 0) {
        int q = ammo >= 0 ? ammo : pickupAmmo(type);
        int cur = (int) check_cur_player_ammo_amount_in_inventory((AMMOTYPE) type);
        int mx = (int) get_max_ammo_for_type(type);

        if (cur + q > mx) {
            q = mx - cur > 0 ? mx - cur : 0;
        }
        give_cur_player_ammo(type, cur + q);
        ammo = q;
    }
    set_cur_player(saved);
    portConLogf("ge: give: player %d got %s (%s, ammo type %d, +%d rounds)\n", player + 1, itemName(id), added ? "new" : "already held", type, type > 0 ? ammo : 0);
}

/* Lua (the arena mode): give item `id` (an ITEM_ id) to a Bond by index, the same as the console's give. */
void portGiveItemTo(int player, int id, int ammo)
{
    giveItem(player, id, ammo);
}

static int keyMatches(PropRecord *p, int filter)
{
    ObjectRecord *o;

    if (p->type != PROP_TYPE_OBJ || p->obj == NULL || p->obj->type != PROPDEF_KEY) {
        return 0;
    }
    o = p->obj;
    if (filter == KEYF_ANY) {
        return 1;
    }
    if (filter == KEYF_CARD) {
        return o->obj != PROP_CHRKEYYALE && o->obj != PROP_CHRKEYBOLT;
    }
    return o->obj == filter;
}

static void giveKey(int player, int filter, int ordinal)
{
    s32 saved = get_cur_playernum();
    struct player *pl = (player >= 0 && player < 4) ? g_playerPointers[player] : NULL;
    PropRecord *p;
    int n = 0;

    if (pl == NULL || pl->prop == NULL || pl->bonddead) {
        portConLogf("ge: give: player %d is not alive here: nothing given\n", player + 1);
        return;
    }
    for (p = chrpropGetActiveTail(); p != NULL; p = p->prev) {   /* the level's key objects still lying about, in list order (the same on both machines) */
        if (keyMatches(p, filter) && ++n == ordinal) {
            TICKOP op;
            KeyRecord *k = (KeyRecord *) p->obj;
            unsigned int flags = (unsigned int) k->keyflags;
            int model = (int) k->obj;

            set_cur_player(player);
            op = propPickupByPlayer(p, TRUE);   /* exactly the pickup: the message, the sound, the inventory */
            propExecuteTickOperation(p, op);
            set_cur_player(saved);
            portConLogf("ge: give: player %d picked up key object #%d (model %d, key flags 0x%x)\n", player + 1, ordinal, model, flags);
            return;
        }
    }
    portConLogf("ge: give: no free key object #%d (filter %d) in this level: nothing given (%d matching)\n", ordinal, filter, n);
}

void portSessionCmdExec(const PortNetCmd *c)
{
    switch (c->kind) {
    case PORT_NETCMD_SPAWN_GUARD:
        portArenaNetSpawn(c->issuer, c->a, (float) c->b);
        break;
    case PORT_NETCMD_SPAWN_CLEAR:
        portArenaNetClear();
        break;
    case PORT_NETCMD_GIVE_ITEM:
        giveItem(c->player, c->a, c->b);
        break;
    case PORT_NETCMD_GIVE_KEY:
        giveKey(c->player, c->a, c->b);
        break;
    case PORT_NETCMD_PLAN_POINT:
        {   /* the heist planning board's mouse: a click, into the plan's sim input (port_planning.c) */
            extern void portPlanningPointerPush(int p, int a, int b);
            portPlanningPointerPush(c->player, c->a, c->b);
        }
        break;
    default:
        break;
    }
}

/* ---- the console ---------------------------------------------------------------------------------------------------------- */

/* Netplay: queue the command for both machines. Otherwise apply it now as the local player's. Returns 1 if it took effect or was queued. */
int portSessionCmdRun(int kind, int player, int a, int b)
{
    if (portNetActive()) {
        char why[96];

        if (portNetCmdIssue(kind, player, a, b, why, sizeof(why)) != 0) {
            portConLogf("ge: session command not done: %s\n", why);
            return 0;
        }
        return 1;
    }
    {
        PortNetCmd c;

        memset(&c, 0, sizeof(c));
        c.issuer = portLocalPlayer() & 1;
        c.kind = kind;
        c.player = player;
        c.a = a;
        c.b = b;
        portSessionCmdExec(&c);
        return 1;
    }
}

static void giveList(const char *filter)
{
    char line[160];
    char key[64];
    int i, len = 0, shown = 0;

    if (filter != NULL) {
        portNetNameKey(filter, key, sizeof(key));
    } else {
        key[0] = '\0';
    }
    line[0] = '\0';
    for (i = 0; i < NITEMS; i++) {
        char nk[64];
        const char *nm = sItems[i].name + 5;

        portNetNameKey(nm, nk, sizeof(nk));
        if (key[0] != '\0' && strstr(nk, key) == NULL) {
            continue;
        }
        len += snprintf(line + len, sizeof(line) - (size_t) len, "%s ", nm);
        shown++;
        if (len > 60) {
            portConPrintf(0, "%s\n", line);
            len = 0;
            line[0] = '\0';
        }
    }
    if (len > 0) {
        portConPrintf(0, "%s\n", line);
    }
    portConPrintf(0, "%d item names (case and underscores do not matter: goldeneye_key = GOLDENEYEKEY). Aliases: kf7 pp7 klobb zmg d5k ar33 rcp90 ...\n", shown);
    portConPrintf(0, "keys lying in the level: key, keycard, keyyale, keybolt, with _a _b _c (or 1 2 3) for the 1st, 2nd, 3rd one\n");
}

/* give <item> [p1|p2] [ammo] ; give list [text] */
void portGiveConsole(const char *arg1, const char *rest)
{
    char name[64], base[64], key[64];
    int player = -1, ammo = -1, id = -1, filter = 0, ordinal = 1, isKey = 0, i, count = portAllPlayerCount();
    char *us;
    const char *r;

    if (arg1 == NULL || arg1[0] == '\0') {
        portConLogf("ge: give: usage: give <item> [p1|p2] [ammo]   (give list shows the item names)\n");
        return;
    }
    if (strcmp(arg1, "list") == 0) {
        giveList(rest);
        return;
    }
    /* [p1|p2] [ammo], any order */
    for (r = rest; r != NULL && *r != '\0';) {
        char tok[24];
        int n = 0;

        while (*r == ' ' || *r == '\t') r++;
        while (*r != '\0' && *r != ' ' && *r != '\t' && n < (int) sizeof(tok) - 1) tok[n++] = *r++;
        tok[n] = '\0';
        if (n == 0) break;
        if ((tok[0] == 'p' || tok[0] == 'P') && tok[1] >= '1' && tok[1] <= '4' && tok[2] == '\0') {
            player = tok[1] - '1';
        } else {
            char *end;
            long v = strtol(tok, &end, 10);

            if (*end != '\0' || v < 0 || v > 9999) {
                portConLogf("ge: give: '%s' is not p1, p2 or an ammo count 0..9999\n", tok);
                return;
            }
            ammo = (int) v;
        }
    }
    if (player < 0) {
        player = portCoopActive() ? portLocalPlayer() : 0;
    }
    if (player >= count || player > 1) {
        portConLogf("ge: give: player %d is not in this game\n", player + 1);
        return;
    }
    snprintf(name, sizeof(name), "%s", arg1);
    /* keys: key, keycard_a, keyyale2 ... */
    snprintf(base, sizeof(base), "%s", name);
    us = strrchr(base, '_');
    if (us != NULL && us[1] != '\0' && us[2] == '\0' && us[1] >= 'a' && us[1] <= 'z') {
        ordinal = us[1] - 'a' + 1;
        *us = '\0';
    } else if (us != NULL && us[1] >= '0' && us[1] <= '9') {
        ordinal = atoi(us + 1);
        *us = '\0';
    }
    portNetNameKey(base, key, sizeof(key));
    {
        char *dg = key + strlen(key);

        while (dg > key && dg[-1] >= '0' && dg[-1] <= '9' && strncmp(key, "key", 3) == 0) dg--;   /* keycard2 */
        if (*dg != '\0' && strncmp(key, "key", 3) == 0) {
            ordinal = atoi(dg);
            *dg = '\0';
        }
    }
    for (i = 0; i < (int) (sizeof(sKeyFamilies) / sizeof(sKeyFamilies[0])); i++) {
        if (strcmp(key, sKeyFamilies[i].name) == 0) {
            isKey = 1;
            filter = sKeyFamilies[i].filter;
        }
    }
    if (isKey) {
        if (ordinal < 1 || ordinal > 64) {
            portConLogf("ge: give: key number %d is out of range 1..64\n", ordinal);
            return;
        }
        portSessionCmdRun(PORT_NETCMD_GIVE_KEY, player, filter, ordinal);
        return;
    }
    portNetNameKey(name, key, sizeof(key));
    for (i = 0; i < NITEMS && id < 0; i++) {
        char nk[64];

        portNetNameKey(sItems[i].name, nk, sizeof(nk));
        if (strcmp(nk, key) == 0) {
            id = sItems[i].id;
        }
    }
    for (i = 0; i < (int) (sizeof(sAliases) / sizeof(sAliases[0])) && id < 0; i++) {
        if (strcmp(sAliases[i].alias, key) == 0) {
            id = sAliases[i].id;
        }
    }
    if (id < 0) {
        portConLogf("ge: give: no item called '%s' (give list shows the names)\n", name);
        return;
    }
    portSessionCmdRun(PORT_NETCMD_GIVE_ITEM, player, id, ammo);
}

#endif /* TARGET_N64 */
