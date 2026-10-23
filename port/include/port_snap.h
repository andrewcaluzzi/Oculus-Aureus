/* port_snap.h - rollback snapshots (rollback job R2).
 * Plain C types only: the Lua glue (built against the real libc headers) and the game both include / mirror it. */
#ifndef PORT_SNAP_H
#define PORT_SNAP_H

#define PORT_SNAP_MAGIC 0x50414E53u     /* "SNAP": the table has been filled in by scripts/port/snap_embed.py */
#define PORT_SNAP_TABLE_CAP 2048        /* ranges (adjacent ones are merged) */
#define PORT_SNAP_STR_CAP 49152         /* bytes of range names */

typedef struct {
    unsigned int addr;
    unsigned int size;
    unsigned int nameOff;               /* into PortSnapTable.str */
    unsigned int cat;                   /* 'S' = sim statics */
} PortSnapEnt;

typedef struct {
    unsigned int magic;                 /* 0 = placeholder (not embedded), PORT_SNAP_MAGIC */
    unsigned int version;               /* 1 */
    unsigned int capEnt;
    unsigned int capStr;
    unsigned int count;
    unsigned int strUsed;
    unsigned int reserved0;
    unsigned int reserved1;
    PortSnapEnt ent[PORT_SNAP_TABLE_CAP];
    char str[PORT_SNAP_STR_CAP];
} PortSnapTable;

extern const PortSnapTable g_PortSnapTable;

/* ---- registration (call from a constructor or an init function; before the first stage tick) ---- */

/* sim state outside the embedded table (a heap block, an arena) with a fixed extent */
void portSnapAddRange(const char *name, const void *p, unsigned int n);
/* a "live" range inside included statics that a restore must NOT rewind (an input accumulator, a wall-clock sample) */
void portSnapLive(const char *name, const void *p, unsigned int n);
/* a range that is saved and restored but ignored by the byte-exact compare (a lookup cache keyed by an address that varies) */
void portSnapBenign(const char *name, const void *p, unsigned int n);
/* sim state whose extent changes (an arena's used part): fn(i, &lo, &len) returns 1 and the i-th range, 0 when there is no i-th */
typedef int (*PortSnapDynFn)(int i, unsigned char **lo, unsigned int *len);
void portSnapAddDyn(const char *name, PortSnapDynFn fn);

/* ---- the production API (rollback R3) ----
 * The tick hook (boss.c, before the tick runs) calls portSnapTickBegin(): with the ring on it saves the start state of that tick.
 * portSnapEnable(n): ring of n slots (about 10); portSnapHave(tick); portSnapRollbackTo(tick): restore the start state of `tick` and
 * re-simulate up to the tick about to run, sim-only, inside this frame (see port_snap.c for the conditions). */
void portSnapEnable(int n);
int portSnapHave(unsigned int tick);
int portSnapRollbackTo(unsigned int fromTick);
void portSnapTickBegin(void);
int portSnapNeedIdle(void);
void portSnapStageBegin(void);
void portSnapReport(void);

/* 1 while a re-simulated tick runs (rollback): logging / net bookkeeping / sounds of the original tick are not repeated */
extern int g_PortResim;

#endif
