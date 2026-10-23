/* port_music.h - choosable level music: the per-stage override table behind the Lua `music` lib,
 * the `level_music` modifier and the `music` console command. Plain types only, so port_luavm.c
 * (real libc headers) can include it. */
#ifndef PORT_MUSIC_H
#define PORT_MUSIC_H

/* Track ids are the M_* enum of src/bondconstants.h (1 .. M_END_SOMETHING). Names are the enum names
 * lowercased without "M_" ("aztec", "facility"). */
int         portMusicTrackCount(void);                /* highest valid id */
const char *portMusicName(int id);                    /* NULL when id is out of range */
int         portMusicFind(const char *name);          /* id, or -1; case-insensitive */

/* level_music modifier value: 0 = Stock, 1 = Random, n >= 2 = track id n - 1. */
void        portMusicModText(int v, char *buf, int n);
int         portMusicModParse(const char *text, int *v);   /* "stock", "random" or a track name; 1 = parsed */

/* Per-stage override (cleared at every stage start). Lua music.set: the main track of the current/next stage. */
int         portMusicSetMain(int id);                 /* 1 = accepted (solo, no demo) */
int         portMusicPlayNow(int id, int needLive);   /* main channel now; 1 = started */
int         portMusicCurrent(void);                   /* id on the main channel, 0 when none */

void        portMusicStageStart(void);                /* lvlStageLoad (via portModifierStageStart): drops the overrides */
void        portMusicConsole(const char *arg1);       /* `music list`, `music <name>` */

#endif /* PORT_MUSIC_H */
