/* port_arenamarkers.h - arena (drop A7, v2 in drop A8): screen markers on pickups (centre ammo box, armour, crowd gifts, dropped guns
 * and thrown knives). The sim publishes a small list (port_arenamode.c, portLuaGameMarkerSync, from the mode script's tick); the console
 * overlay (port_console.c) projects, fades, merges and draws it. Presentation only: nothing here is read back by the sim or hashed. */
#ifndef PORT_ARENAMARKERS_H
#define PORT_ARENAMARKERS_H

#define PORT_MARKER_MAX 48   /* 12 arena items + the 30 weapon slots fit, so nothing near the player is ever cut */

/* kinds: the "good stuff" (full marker, edge arrow, fades after a while and comes back up close) and the drops (a dot, close up only) */
#define PORT_MK_AMMO    0
#define PORT_MK_ARMOUR  1
#define PORT_MK_GIFT    2    /* a crowd / sponsor gift (drop A8) */
#define PORT_MK_ITEM    3    /* any other spawn.item pickup (a gun placed by the script) */
#define PORT_MK_GUN     4    /* a dropped enemy gun */
#define PORT_MK_KNIFE   5    /* a thrown knife lying on the floor */
#define PORT_MK_GOOD(k) ((k) <= PORT_MK_ITEM)

typedef struct
{
    float x, y, z;          /* world position of the pickup */
    unsigned int rgba;      /* marker colour */
    char label[8];          /* "AMMO", "ARMOUR", "GIFT", "GUN", "KNIFE" */
    int kind;               /* PORT_MK_* */
    int born;               /* g_GlobalTimer (60 Hz) when it was first published: the fade-out clock */
} PortPickupMarker;

/* the look, from the arena script (spawn.markers(on, {...})); distances in world units (100 = 1 m), times in seconds */
typedef struct
{
    float fade;             /* the good stuff fades out this long after it appears (0 = never) */
    float nearDist;         /* ... and shows again within this distance */
    int   drops;            /* dropped guns: 0 = no marker, 1 = a dot within dropNear, 2 = always a dot */
    int   knives;           /* thrown knives: the same choices */
    float dropNear;
    float merge;            /* markers closer than this on screen (console px) become one with a count */
    int   edge;             /* off-screen edge arrows for the good stuff */
    int   maxGroups;        /* draw budget: at most this many markers after merging */
    int   legacy;           /* 1 = the A7 look (a beacon over every pickup), for before/after comparisons */
} PortMarkerStyle;

/* copy the published list into out (up to max) and the style; 0 = markers off or none */
int portArenaMarkers(PortPickupMarker *out, int max, PortMarkerStyle *style);

#endif
