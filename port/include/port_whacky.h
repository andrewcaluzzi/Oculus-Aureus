/* port_whacky.h - the "Whacky" arena layer (features/arena.md "Whacky", modding/arena.md):
 *
 *   port_chrfx.c   PRESENTATION: per-chr colour wash and translucency, with animated effects (colour cycle, pulse, flicker,
 *                  shimmer). Read by chrRenderProp (chr.c) only; the sim never reads it. Lua table `chrfx`.
 *   port_whacky.c  SIM: the minion launcher (Bond's rockets burst into tiny turbo knife throwers on his side), the global
 *                  animation rate (the Fast / Slow Animation cheats, kept relative per chr), line mode, the 2x weapon cheats for
 *                  any Bond, a chr scale with collision size. Lua table `whacky`.
 * Everything is off until a mode script turns it on, and returns at once while off (the demo oracles stay identical). */
#ifndef PORT_WHACKY_H
#define PORT_WHACKY_H

#ifndef TARGET_N64

#include <ultra64.h>
#include <bondtypes.h>

/* ---- port_chrfx.c (presentation) ---- */
enum { CHRFX_ANIM_NONE = 0, CHRFX_ANIM_CYCLE, CHRFX_ANIM_PULSE, CHRFX_ANIM_FLICKER, CHRFX_ANIM_SHIMMER, CHRFX_ANIM_PULSE2 };

typedef struct PortChrFx {
    u8  r, g, b, amount;    /* the wash colour and how strongly it covers the chr (0 = none, 255 = solid colour) */
    u8  r2, g2, b2;         /* PULSE2: the second colour the wash swings to */
    u8  alpha;              /* 255 = opaque; less draws the chr in the translucent pass (the stock fade path) */
    u8  anim;               /* CHRFX_ANIM_* */
    u8  lo;                 /* PULSE: the wash's low point (x amount / 255); FLICKER / SHIMMER: the alpha's low point */
    u16 period;             /* animation period in 1/60 s (cycle: one turn of the hue wheel) */
} PortChrFx;

int  portChrFxSet(int chrnum, const PortChrFx *fx);   /* 1 = set */
int  portChrFxClear(int chrnum);                      /* 1 = there was one */
void portChrFxClearAll(void);
int  portChrFxCount(void);
int  portChrFxGet(int chrnum, PortChrFx *out);

/* chrRenderProp hooks (chr.c). Both return at once when no chr has an effect. */
s32  portChrFxAlpha(ChrRecord *chr, s32 alpha);       /* the fade alpha after the effect (never 0 if it was > 0) */
void portChrFxShade(ChrRecord *chr, rgba_s32 *shade); /* composes the wash into the chr's stan colour (fog colour + amount) */

/* ---- port_whacky.c (sim) ---- */
void portWhackyReset(void);                            /* stage load (portFactionReset) */
void portWhackyTick(f32 dt);                           /* every sim tick (chraction.c, next to portBotTick; solo and co-op) */
void portWhackyRocketBoom(PropRecord *rocket);         /* propobj.c: a rocket round is about to explode */

#endif /* TARGET_N64 */
#endif /* PORT_WHACKY_H */
