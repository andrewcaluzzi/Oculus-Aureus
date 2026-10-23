/* port_bot.h - the co-op bot: lifecycle (spawn, death, respawn, per-stage reset).
 * Off unless enabled, and never during
 * demo playback/recording or multiplayer, so the attract demos stay bit-identical. */
#ifndef PORT_BOT_H
#define PORT_BOT_H

#ifndef TARGET_N64

#include <ultra64.h>
#include <bondtypes.h>

#define PORT_BOT_CHRNUM 4999   /* below the 5000+ runtime range; u8 AI ids can't reach it */

void       portBotReset(void);              /* lvlStageLoad */
void       portBotTick(void);               /* lifecycle, once per chrlvAllChrTick */
ChrRecord *portBotChr(void);                /* NULL when absent or dead */
s32        portBotIsBot(ChrRecord *chr);
s32        portBotEnabled(void);            /* coop_bot ini || GE_BOT=1 */
void       portBotSummon(void);             /* spawn/respawn now */
void       portBotDismiss(void);
void       portBotChrTick(ChrRecord *self); /* the brain: chrlvActionTick runs it instead of ai() */

s32        portBotSetOption(const char *key, const char *value);   /* TRUE if key is coop_bot*; bad values are logged and ignored */
s32        portBotOptionGet(const char *key, s32 *out);            /* ints as is, floats as tenths */
void       portBotConsole(const char *arg, const char *arg2);      /* NULL/"" = summon, "off", "info", "skin <name|random|list>" */

/* The Bot Skin modifier's value: 0 = Default (coop_bot_skin), 1 = Random, 2 + k = skin k of the roster */
void       portBotSkinModText(int v, char *buf, int n);
int        portBotSkinModParse(const char *text, int *v);          /* "default", "random" or a skin name; 1 = parsed */

/* Brain callbacks: plain C now, the future Lua seam. NULL entries are skipped. */
typedef struct PortBotHooks {
    void       (*onSpawn)(ChrRecord *bot);
    /* A live chr's prop or Bond's viewer prop (solo Bond has no chr); anything else is ignored.
     * Default: portFactionNearestHostile(bot, knifeRange, 1)'s prop; GE_BOT_TARGETBOND: Bond. */
    PropRecord *(*pickTarget)(ChrRecord *bot);
    void       (*onAct)(ChrRecord *bot, s32 act);   /* after the action is issued */
    void       (*onDeath)(ChrRecord *bot);      /* bot is NULL if the chr was already removed */
} PortBotHooks;
extern PortBotHooks g_PortBotHooks;

#endif /* TARGET_N64 */
#endif /* PORT_BOT_H */
