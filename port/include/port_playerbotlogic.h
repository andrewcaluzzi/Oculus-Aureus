/* port_playerbotlogic.h - pure logic for the player bot (port_playerbot.c): the waypoint graph and its
 * A* search, view angles, the plan-file parser and the stuck detector.
 * No game headers or globals, so tests/playerbot_test.c can exercise it outside the game. */
#ifndef PORT_PLAYERBOTLOGIC_H
#define PORT_PLAYERBOTLOGIC_H

/* The game's include/math.h is an IRIX header whose prototypes sit inside `#if 0`, so in the game build these would
 * be implicit int functions (garbage results). Declare them here; this matches libm's declarations. */
float sqrtf(float);
float fabsf(float);
float atan2f(float, float);
float fmodf(float, float);

/* ---- graph ---------------------------------------------------------------------------------------------------------- */
/* A graph in compressed rows, allocated for a capacity (the bot uses one node per stan tile: thousands). */
typedef struct PblGraph {
    int    n, ne, cap, ecap;
    float *x, *y, *z;
    float *npen;            /* extra cost to enter node i (stuck spots, locked doors) */
    unsigned char *nbad;    /* 1 = node i is closed */
    int   *start;           /* edges of node i: [start[i], start[i+1]) */
    int   *to;
    float *pen;             /* extra cost on the edge */
    /* A* scratch */
    float *gs;
    int   *prev, *heap, *hpos;
    unsigned char *state;
} PblGraph;

/* Build: pblGraphAlloc, pblGraphNode for every node in order, then pblGraphEdges per node in order */
int   pblGraphAlloc(PblGraph *g, int maxNodes, int maxEdges);   /* 0 on out of memory */
void  pblGraphFree(PblGraph *g);
int   pblGraphNode(PblGraph *g, float x, float y, float z);      /* returns the index or -1 */
int   pblGraphEdges(PblGraph *g, int node, const int *nbrs, int count);   /* nodes in order 0..n-1 */
void  pblGraphPenalize(PblGraph *g, int a, int b, float add);   /* both directions */
float pblEdgeCost(const PblGraph *g, int a, int e);             /* 3D length + climb surcharge + penalties */
void  pblGraphClearMarks(PblGraph *g);                          /* npen = 0, nbad = 0 (edge penalties stay) */
/* A*: path of node indices from `from` to `to` inclusive into out[max]; returns its length, 0 = unreachable.
 * A route longer than max is cut to its first max nodes (the caller plans again at the end). */
int   pblAstar(PblGraph *g, int from, int to, int *out, int max);
/* nearest node to a point (3D, vertical distance weighted x3); -1 if empty */
int   pblNearest(const PblGraph *g, float x, float y, float z);
/* connected components (edges taken as given): number of them, and the size of the largest */
int   pblComponents(const PblGraph *g, int *largest);

/* ---- funnel (string pulling) ------------------------------------------------------------------------------------------
 * Portals are the shared edges between consecutive tiles of a route, each given as two x/z points (a, b) in any order;
 * cx/cz are the centres of the tiles on either side (tile i and i+1) so each portal can be oriented. Writes the corners
 * of the shortest path from (sx,sz) through every portal to (ex,ez) - the last one is the end point - and returns how
 * many (at most maxc). `shrink` pulls each portal's ends in by that much (the walker's radius) when it is wide enough. */
int pblFunnel(const float *ax, const float *az, const float *bx, const float *bz, const float *cx, const float *cz, int n,
              float sx, float sz, float ex, float ez, float shrink, float *outx, float *outz, int maxc);

/* ---- angles (GE convention: forward = (-sin t, cos t) in x/z, t in degrees) ---------------------------------------- */
float pblWrap180(float a);
float pblYawTo(float dx, float dz);            /* the vv_theta that faces direction (dx, dz), [0, 360) */
float pblPitchTo(float dy, float dh);          /* degrees above the horizon of a target dy up and dh away */
float pblClamp(float v, float lo, float hi);

/* ---- combat geometry ------------------------------------------------------------------------------------------------ */
/* Where q lies relative to a ray from p along the view yaw (degrees, GE convention): *fwd ahead along it, *lat to the side
 * (unsigned). */
void  pblRaySplit(float px, float pz, float yawDeg, float qx, float qz, float *fwd, float *lat);
/* 1 when a bystander at q is in danger from a shot fired from p along yawDeg at a target `range` away: ahead of p, not
 * far beyond the target, and within a body width plus the spread (4% of its distance) of the line of fire */
int   pblInLineOfFire(float px, float pz, float yawDeg, float range, float qx, float qz);
/* The angle (degrees, 0..180) between where a chr faces and the direction to the point (x, z): 0 = straight ahead. The
 * chr's facing is the model's y rotation in radians (getsubroty: forward = (sin r, cos r) in x/z). Guards see within 110. */
float pblOffFacing(float facingRad, float cx, float cz, float x, float z);
/* Threat ranking for target choice, lower = shoot first: distance, less for a guard who is shooting at Bond, much less for
 * one running to raise the alarm or one that hurt Bond lately. */
float pblThreatScore(float dist, int attacking, int alarmRaiser, int hurtUsLately);

/* ---- plans ------------------------------------------------------------------------------------------------------------ */
enum PblOp {
    PBL_GOTO_PAD = 1,   /* a = pad number, r = radius */
    PBL_GOTO_XYZ,       /* x y z, r */
    PBL_USE_OBJ,        /* a = tag: walk to it, face it, tap B until it activates (or `wait` ticks pass) */
    PBL_USE_PAD,        /* a = pad: walk to it, face along the pad's look, tap B */
    PBL_DESTROY_OBJ,    /* a = tag: get line of sight and shoot it until destroyed */
    PBL_COLLECT_OBJ,    /* a = tag: walk onto it until it is in the inventory */
    PBL_GADGET_OBJ,     /* a = item id, b = tag: equip the item, face the object, fire it (plant / throw / use) */
    PBL_PHOTO_OBJ,      /* a = tag: equip the camera, face the object, take pictures */
    PBL_WAIT,           /* a = ticks */
    PBL_EQUIP,          /* a = item id */
    PBL_FLAG,           /* a = stage flag mask: wait (fighting) until set, up to b ticks (0 = forever) */
    PBL_PRESS,          /* a = button mask, b = ticks held */
    PBL_EXIT,           /* stand still and tap A until the mission ends */
    PBL_AUTO,           /* do the generic objective handlers (destroy / collect / photograph) for every open objective */
    PBL_KILL,           /* a = radius: fight every visible hostile within it, then continue */
    PBL_FACE,           /* x y z: turn to face the point */
    PBL_LOOK,           /* a = pitch in degrees */
    PBL_WALK,           /* a = ticks, b = stick y, c = stick x (raw stick, no navigation) */
    PBL_PEACE,          /* a = 1: do not shoot from here on (stealth / civilian levels), 0 = shoot again */
    PBL_THROW_OBJ,      /* a = item id, b = tag: like GADGET but throws (fire held, then released) */
    PBL_HUNT,           /* walk to the nearest live armed hostile and fight, until none is left (or obj= completes) */
    PBL_MEET,           /* a = 0 chr number / 1 AI list id, b = the number: walk up to that chr (r) and stay c ticks
                         * (or until the step's obj= objective completes) - e.g. a double agent who talks first */
    PBL_QUIET,          /* a = 1: stealth - prefer silenced guns while no alarm sounds, knock key carriers out from behind
                         * instead of shooting them when they have not seen Bond; 0 = off */
    PBL_KEY,            /* a = key flags: fetch a key with any of them (lying, on a guard: knock out / kill and search, on a
                         * civilian: aim at it until it hands it over); done once Bond has it */
    PBL_RUSH,           /* a = 1: keep walking the route while fighting (strafe-walk, the look on the target); 0 = stop and
                         * fight (the default) */
    PBL_AVOID,          /* a = pad: keep routes out of that pad's room from here on (a room that ends the mission) */
    PBL_COUNT_
};

#define PBL_MAXSTEPS 256
typedef struct PblStep {
    int   op;
    int   a, b, c;
    float x, y, z, r;
    int   condObj;         /* -1 = always; else only when objective condObj is open (needed at this difficulty, not complete) */
    int   diffMin, diffMax;/* only on these difficulties (0 agent .. 3 007) */
    int   line;            /* line in the plan file, for logs */
    char  text[64];        /* the source line, for logs */
} PblStep;

typedef struct PblPlan {
    int     n;
    PblStep s[PBL_MAXSTEPS];
    char    name[32];
} PblPlan;

/* parse plan text; returns the number of steps, -1 on a syntax error (message in err) */
int  pblParsePlan(const char *text, PblPlan *plan, char *err, int errlen);
const char *pblOpName(int op);
/* item names used by plans (equip pp7, gadget bomb_defuser ...): -1 unknown */
int  pblItemByName(const char *name);

/* ---- stuck detection --------------------------------------------------------------------------------------------------- */
typedef struct PblStuck {
    float bestDist;     /* closest to the current goal so far */
    int   lastGain;     /* tick of the last real progress */
    float lastX, lastZ;
    int   lastMoveTick; /* tick Bond last moved more than a little */
    int   level;        /* 0 fine, 1 stuck (try use / sidestep), 2 badly stuck (replan without the edge), 3 hopeless */
    int   since;        /* tick the level last rose */
} PblStuck;

void pblStuckReset(PblStuck *s, int tick, float x, float z, float dist);
/* feed every tick while moving to a goal; returns the level */
int  pblStuckFeed(PblStuck *s, int tick, float x, float z, float dist);

#endif
