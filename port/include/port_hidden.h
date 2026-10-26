#ifndef PORT_HIDDEN_H
#define PORT_HIDDEN_H

/* Hidden mode (GE_HIDDEN=1 or --hidden): the game runs with a window that is
 * never shown, so agents can smoke-test the Windows exe on a desktop someone is
 * using. See port/README-headless.md. */

#ifdef __cplusplus
extern "C" {
#endif

/* 1 when hidden mode is on. Cheap; safe from any thread after portHiddenInit. */
int portHidden(void);

/* main(), before anything touches SDL: turns --hidden into GE_HIDDEN=1 and, when
 * hidden, sets the defaults and SDL hints (silent audio, no controllers, fixed
 * window size, no ini writes, below-normal priority on Windows). */
void portHiddenInit(int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif
