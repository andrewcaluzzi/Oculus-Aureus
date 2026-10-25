/* port_simsnd.h - the sim's own view of the sounds it starts (port_simsnd.c, modding/snapshot-fidelity.md). Plain C types. */
#ifndef PORT_SIMSND_H
#define PORT_SIMSND_H

#define PORT_SIMSND_MAX 256
#define PORT_SIMSND_FOREVER 0x7fffffff

/* flags: the tracked sound's sound-player flags that decide its life (snd.h SoundFlags) */
#define PORT_SIMSND_FINAL 0x01
#define PORT_SIMSND_LOOPED 0x02
#define PORT_SIMSND_RETRIGGER 0x10

/* states: the sound player's playingState values the sim compares against (snd.h SoundState; 0 = AL_STOPPED) */
#define PORT_SIMSND_STOPPED 0
#define PORT_SIMSND_PLAYING 1
#define PORT_SIMSND_STOPPING 2
#define PORT_SIMSND_INIT 5

int portSimSndOn(void);
void portSimSndSetOn(int on);                       /* tests */
int portSimSndIsToken(const void *p);
int portSimSndSlot(const void *tok);
int portSimSndGen(const void *tok);
int portSimSndNow(void);
int portSimSndFields(long us);
/* a sound the sim starts: its voice starts startUs after now, plays playUs (attack + decay), then rings releaseUs; LOOPED /
 * RETRIGGER sounds play until stopped. owner = the sim's owner word (cleared when the sound ends) or NULL. Returns the token
 * (NULL when the table is full). */
void *portSimSndStart(int sound, long startUs, long playUs, long releaseUs, int flags, void **owner);
int portSimSndState(const void *tok);
void portSimSndStop(const void *tok);
void portSimSndStopByFlag(int flag);
void portSimSndTick(int fields);
void portSimSndKillAll(void);
int portSimSndLive(void);

#endif
