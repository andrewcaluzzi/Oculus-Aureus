#ifndef PORT_HUDSTYLE_H
#define PORT_HUDSTYLE_H
/* port_hudstyle.c - shared drawing helpers for mode HUDs in the console's 2D space: filled and framed boxes, scaled text in the stock
 * fonts (Bank Gothic, Zurich Bold) with a drop shadow or an outline, colour and pulse helpers. The heist's debrief / intel panels use
 * the same primitives (their own static copies); new HUDs should use these. Presentation only: nothing here is sim state.
 *
 * Use: inside a renderer called from port_console.c portConRender,
 *     HudCanvas c;
 *     gdl = hudBegin(gdl, &c);           // console space (c.w x c.h, about 640 x 480), own display list
 *     hudFill(&c, ...); hudText(&c, ...);
 *     gdl = hudEnd(gdl, &c);
 * Draw order is call order; text and boxes may be mixed freely (the combiner state is switched as needed). */
#include <ultra64.h>

typedef struct HudCanvas
{
    Gfx *g, *end;            /* the sub display list being written, and its end */
    s32 w, h;                /* console space */
    u32 prim;
    s32 havePrim, texMode;
    s16 vx, vy;              /* the frame's VI size, put back by hudEnd */
    s32 ok;                  /* 0: nothing is drawn (no room, no fonts) */
    f32 s;                   /* the transform (hudSetTransform): console x = ox + x * s, y = oy + y * s; text scales by s too */
    s32 ox, oy;
} HudCanvas;

#define HUD_FONT_BANK   0    /* Bank Gothic: the console / HUD face */
#define HUD_FONT_ZURICH 1    /* Zurich Bold: the bigger, heavier face (the stock objectives and briefings) */

#define HUD_ALIGN_LEFT   0
#define HUD_ALIGN_CENTRE 1
#define HUD_ALIGN_RIGHT  2

#define HUD_FX_SHADOW  1     /* a dark copy 1 unit (x scale) down and right */
#define HUD_FX_OUTLINE 2     /* dark copies all round (8 directions) */

Gfx *hudBegin(Gfx *gdl, HudCanvas *c);
Gfx *hudEnd(Gfx *gdl, HudCanvas *c);
/* draw a block in its own units: everything after this is placed at (ox, oy) + (x, y) * s and text is scaled by s (layout widths
 * from hudTextW(t, scale) are already in the block's units). hudSetTransform(c, 0, 0, 1.0f) goes back to plain console units. */
void hudSetTransform(HudCanvas *c, s32 ox, s32 oy, f32 s);

void hudFill(HudCanvas *c, s32 x0, s32 y0, s32 x1, s32 y1, u32 rgba);
void hudFrame(HudCanvas *c, s32 x0, s32 y0, s32 x1, s32 y1, s32 t, u32 rgba);
void hudGradientV(HudCanvas *c, s32 x0, s32 y0, s32 x1, s32 y1, u32 top, u32 bottom, s32 steps);
void hudGradientH(HudCanvas *c, s32 x0, s32 y0, s32 x1, s32 y1, u32 left, u32 right, s32 steps);

s32  hudTextW(const char *t, f32 scale, s32 font);
s32  hudTextH(f32 scale, s32 font);
/* text at (x, y) (top left of the line, after `align` against x); fx = HUD_FX_* | 0; the shadow / outline colour is `dark` */
void hudText(HudCanvas *c, s32 x, s32 y, const char *t, u32 rgba, f32 scale, s32 font, s32 align, s32 fx, u32 dark);
/* the same, squashed in x by sx (0.02..1; a card-flip "spin" is sx = |cos(angle)|) */
void hudTextSq(HudCanvas *c, s32 x, s32 y, const char *t, u32 rgba, f32 scale, f32 sx, s32 font, s32 align, s32 fx, u32 dark);

/* per-letter drawing: fn gets each non-space letter (index i of n) with dx/dy = 0, scale = sx = 1, the line's colour, and may change
 * them (offsets in the block's units; scale and squash about the letter's centre), swap the character (a scramble) or hide it */
typedef struct HudLetter { f32 dx, dy, scale, sx; u32 rgba; s32 hide; char ch; f32 ox; } HudLetter;   /* ox (read only): the letter's
                                                                                                        * centre minus the line's, x */
typedef void (*HudLetterFn)(s32 i, s32 n, HudLetter *l, void *ud);
void hudTextLetters(HudCanvas *c, s32 x, s32 y, const char *t, u32 rgba, f32 scale, s32 font, s32 align, s32 fx, u32 dark,
                    HudLetterFn fn, void *ud);
/* the same layout and fn without drawing: the horizontal span (block units, before the transform) of the letters hudTextLetters would
 * draw; 0 when none would be. fn is called once per letter as for drawing, so it must not keep state between calls. */
s32 hudTextLettersExtent(HudCanvas *c, s32 x, s32 y, const char *t, f32 scale, s32 font, s32 align, HudLetterFn fn, void *ud,
                         f32 *x0, f32 *x1);

/* a filled disc / ring (horizontal spans), centre and radii in the block's units */
void hudDisc(HudCanvas *c, s32 cx, s32 cy, s32 r, u32 rgba);
void hudRing(HudCanvas *c, s32 cx, s32 cy, s32 r0, s32 r1, u32 rgba);

u32  hudAlpha(u32 rgba, f32 a);                 /* rgba with its alpha times a (0..1) */
u32  hudLerp(u32 a, u32 b, f32 t);              /* colour a..b */
u32  hudHue(f32 h, u32 alpha);                  /* a bright colour at hue h (0..1) */
f32  hudPulse(f32 secs, f32 hz, f32 amp);       /* 1 + amp * sin(2 pi hz secs) */
f32  hudEaseOut(f32 t);                         /* 0..1 -> 0..1, fast then slow */
f32  hudEaseOutBack(f32 t, f32 overshoot);      /* 0..1 -> 0..1 past 1 by about overshoot, then back */
f32  hudEaseOutBounce(f32 t);                   /* 0..1 -> 0..1 with bounces at the end */

#endif
