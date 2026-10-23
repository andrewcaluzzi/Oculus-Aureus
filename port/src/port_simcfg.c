/* port_simcfg.c - the configuration the simulation reads that no per-player settings block carries (netplay job 3c).
 *
 * midas.ini keys (and a few diagnostic environment variables) change what the sim does: fix_shootthrough, true_enemy_rockets,
 * the explosion cap, distance fading, the multiplayer cheat switches ... Two machines whose inis differ in one of them desync
 * with nothing to say why. Both sides therefore list these values (in the SETUP message, and again in a SIMCFG message at
 * READY and GO) and refuse the session, naming the key, when they differ. The list is a fixed table, identical on both sides of
 * a session because the build id is checked first. Add a key here when the sim starts reading a new setting. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "port_netmsg.h"

int portSimCfgCount(void);
const char *portSimCfgName(int i);
void portSimCfgCollect(PortNetSimCfg *c);
int portSimCfgDiff(const PortNetSimCfg *mine, const PortNetSimCfg *theirs, char *out, int cap);
unsigned int portSimCfgHash(unsigned int h, const PortNetSimCfg *c);

/* the ini getters live in video.c; a headless build has none, hence weak */
#define GETTER(name) extern int name(void) __attribute__((weak))
GETTER(portHighLod);
GETTER(portFixShootThrough);
GETTER(portTrueEnemyRocketsEnabled);
GETTER(portDistanceFadingEnabled);
GETTER(portNoHeadRoll);
GETTER(portAdzyMode);
GETTER(portUnlockAll);
GETTER(portExplosionCap);
GETTER(portClassicHitches);
GETTER(portVideoScopeSteady);
GETTER(portWatchStatic);
GETTER(portMpFriendlyFireOff);
GETTER(portMpLtkHealthActive);
GETTER(portMpNoArmorActive);
GETTER(portMpSpawnWeapon);
GETTER(portMpRandomSpawns);
GETTER(portMpP1EndsMatch);
extern int portMpCheatSetting(int which) __attribute__((weak));

enum { K_FUNC, K_CHEAT, K_ENV, K_ENVPRESENT };

typedef struct { const char *name; int kind; int (*fn)(void); int arg; } SimCfgKey;

static const SimCfgKey sKeys[] = {
    { "high_lod",            K_FUNC,  portHighLod, 0 },
    { "fix_shootthrough",    K_FUNC,  portFixShootThrough, 0 },
    { "true_enemy_rockets",  K_FUNC,  portTrueEnemyRocketsEnabled, 0 },
    { "distance_fading",     K_FUNC,  portDistanceFadingEnabled, 0 },
    { "no_head_roll",        K_FUNC,  portNoHeadRoll, 0 },
    { "adzy",                K_FUNC,  portAdzyMode, 0 },
    { "unlock_all",          K_FUNC,  portUnlockAll, 0 },
    { "explosion_cap",       K_FUNC,  portExplosionCap, 0 },
    { "classic_hitches",     K_FUNC,  portClassicHitches, 0 },
    { "scope_steady",        K_FUNC,  portVideoScopeSteady, 0 },
    { "watch_static",        K_FUNC,  portWatchStatic, 0 },
    { "mp_friendly_fire_off", K_FUNC, portMpFriendlyFireOff, 0 },
    { "mp_ltk_health",       K_FUNC,  portMpLtkHealthActive, 0 },
    { "mp_no_armor",         K_FUNC,  portMpNoArmorActive, 0 },
    { "mp_spawn_weapon",     K_FUNC,  portMpSpawnWeapon, 0 },
    { "mp_random_spawns",    K_FUNC,  portMpRandomSpawns, 0 },
    { "mp_p1_ends_match",    K_FUNC,  portMpP1EndsMatch, 0 },
    { "mp_cheat_invisibility", K_CHEAT, 0, 0 },
    { "mp_cheat_dk_mode",    K_CHEAT, 0, 1 },
    { "mp_cheat_tiny_players", K_CHEAT, 0, 2 },
    { "mp_cheat_paintball",  K_CHEAT, 0, 3 },
    { "mp_cheat_no_radar",   K_CHEAT, 0, 4 },
    { "mp_cheat_turbo",      K_CHEAT, 0, 5 },
    { "mp_cheat_unlimited_ammo", K_CHEAT, 0, 6 },
    { "env.GE_FARX",         K_ENV,   0, 0 },   /* far plane multiplier: room culling */
    { "env.GE_PORTALPAD",    K_ENV,   0, 1 },   /* room scissor padding: draw only, but part of the room walk's inputs */
    { "env.GE_EDGEINSET",    K_ENVPRESENT, 0, 2 },
    { "env.GE_NOFOG",        K_ENVPRESENT, 0, 3 },
    { "env.GE_NOVISCRIPT",   K_ENVPRESENT, 0, 4 },
    { "env.GE_FORCEROOM",    K_ENV,   0, 5 },
    { "env.GE_ALLROOMS",     K_ENVPRESENT, 0, 6 },
};
#define NKEYS ((int) (sizeof(sKeys) / sizeof(sKeys[0])))

static const char *const sEnvNames[] = { "GE_FARX", "GE_PORTALPAD", "GE_EDGEINSET", "GE_NOFOG", "GE_NOVISCRIPT", "GE_FORCEROOM", "GE_ALLROOMS" };

int portSimCfgCount(void)
{
    return NKEYS;
}

const char *portSimCfgName(int i)
{
    return (i >= 0 && i < NKEYS) ? sKeys[i].name : "?";
}

static int envValue(int which, int presentOnly)
{
    const char *e = getenv(sEnvNames[which]);
    unsigned int h = 2166136261u;

    if (e == NULL) {
        return 0;
    }
    if (presentOnly) {
        return 1;
    }
    for (; *e != '\0'; e++) {
        h = (h ^ (unsigned char) *e) * 16777619u;
    }
    return (int) (h | 1u);   /* never 0: 0 means unset */
}

void portSimCfgCollect(PortNetSimCfg *c)
{
    int i;

    memset(c, 0, sizeof(*c));
    c->n = NKEYS;
    for (i = 0; i < NKEYS; i++) {
        const SimCfgKey *k = &sKeys[i];
        int v = 0;

        switch (k->kind) {
        case K_FUNC:
            v = (k->fn != NULL) ? k->fn() : 0;
            break;
        case K_CHEAT:
            v = (portMpCheatSetting != NULL) ? portMpCheatSetting(k->arg) : 0;
            break;
        case K_ENV:
            v = envValue(k->arg, 0);
            break;
        case K_ENVPRESENT:
            v = envValue(k->arg, 1);
            break;
        }
        c->v[i] = v;
    }
    {   /* GE_NET_TEST_SIMCFG=<key>=<value> (test, one side): this machine REPORTS another value for that key, to prove a mismatch is refused */
        const char *t = getenv("GE_NET_TEST_SIMCFG");
        const char *eq = (t != NULL) ? strchr(t, '=') : NULL;

        if (eq != NULL) {
            for (i = 0; i < NKEYS; i++) {
                if ((size_t) (eq - t) == strlen(sKeys[i].name) && strncmp(t, sKeys[i].name, (size_t) (eq - t)) == 0) {
                    c->v[i] = atoi(eq + 1);
                }
            }
        }
    }
}

/* writes "key a/b, key a/b" for every key that differs (a = ours, b = the partner's) into out; returns how many differ */
int portSimCfgDiff(const PortNetSimCfg *mine, const PortNetSimCfg *theirs, char *out, int cap)
{
    int i, n = 0, used = 0;

    out[0] = '\0';
    if (mine->n != theirs->n) {
        snprintf(out, (size_t) cap, "the setting lists differ in length (%d vs %d): other builds", mine->n, theirs->n);
        return 1;
    }
    for (i = 0; i < mine->n && i < NKEYS; i++) {
        if (mine->v[i] != theirs->v[i]) {
            if (used < cap - 1) {
                used += snprintf(out + used, (size_t) (cap - used), "%s%s (mine %d, theirs %d)", n > 0 ? ", " : "", sKeys[i].name, mine->v[i], theirs->v[i]);
            }
            n++;
        }
    }
    return n;
}

unsigned int portSimCfgHash(unsigned int h, const PortNetSimCfg *c)
{
    int i;

    for (i = 0; i < c->n; i++) {
        h = portNetFnv(h, &c->v[i], (int) sizeof(int));
    }
    return h;
}
