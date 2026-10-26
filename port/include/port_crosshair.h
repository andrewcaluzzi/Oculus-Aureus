/* port_crosshair.h - crosshair styles and the crosshair draw hook (modding/lua.md "Crosshair styles").
 *
 * Shared by the game half (port/src/port_crosshair.c: state, the sight's draw, the rasteriser) and the Lua half
 * (port/luavm/port_luavm_crosshair.c: crosshair.*, the draw_crosshair event, the style pack). Plain C types only: the Lua
 * library is built against the real libc headers, the game against its own.
 *
 * PRESENTATION ONLY. Nothing here is sim state: the styles, the hit-marker timers and the draw list are read only by the
 * sight's draw (gunfire.c gunDrawSight). The sim is read, never written. */
#ifndef PORT_CROSSHAIR_H
#define PORT_CROSSHAIR_H

/* shapes */
enum {
    XHAIR_CLASSIC = 0,   /* the game's own sight sprite (the default) */
    XHAIR_NONE,          /* nothing (a draw hook draws instead) */
    XHAIR_DOT,
    XHAIR_CROSS,
    XHAIR_T,             /* a cross without its top arm */
    XHAIR_X,             /* a diagonal cross */
    XHAIR_CIRCLE,
    XHAIR_CHEVRON,
    XHAIR_BRACKETS,      /* four corner brackets */
    XHAIR_SHAPE_COUNT
};

/* layers: a script's style wins over the player's own choice (console / pack / GE_CROSSHAIR); a script's is dropped at
 * every stage start, the player's lasts for the session */
enum { XHAIR_LAYER_SCRIPT = 0, XHAIR_LAYER_USER = 1, XHAIR_LAYERS = 2 };

#define XHAIR_MAX_WEAPON 16

typedef struct PortXhairStyle {
    int shape;
    float size;          /* arm length / circle radius / dot diameter, HUD pixels (the game's 240-line frame) */
    float gap;           /* space between the aim point and the arms */
    float thick;         /* line thickness */
    unsigned color;      /* 0xRRGGBBAA */
    int dot;             /* also a centre dot */
    float dotsize;
    int outline;
    unsigned ocolor;
    float othick;
    int dynamic;         /* the gap follows the gun's spread, movement and firing */
    float spreadk;       /* x the weapon's real spread (its Inaccuracy, the first-shot bonus) */
    float movek;         /* HUD px at full run speed */
    float firek;         /* HUD px per shot (decays over ~0.2 s) */
    int hit;             /* hit marker */
    unsigned hitcolor, headcolor;
    float hittime;       /* seconds */
    float hitsize;
    int always;          /* also while not aiming (the game shows its sight only in aim mode) */
    int hasdraw;         /* the Lua half holds a draw function for this style */
} PortXhairStyle;

/* game half (port_crosshair.c) */
void portXhairDefaults(PortXhairStyle *s);
void portXhairSet(int layer, const PortXhairStyle *base, int nweap, const int *items, const PortXhairStyle *perweapon);
void portXhairClear(int layer);
int portXhairIsSet(int layer);
int portXhairShapeOf(int layer);   /* -1 unset */
void portXhairHide(int on);
int portXhairHidden(void);
void portXhairStageReset(void);    /* drops the script layer and the hide flag */
void portXhairTestHit(int head);   /* tests: a hit marker as if a shot just landed */
float portXhairSpreadNow(void);    /* the dynamic gap last drawn, HUD px */
int portXhairDrawCount(void);      /* frames the custom sight drew (tests) */
/* the draw list, filled by the style and the Lua hooks during one sight draw */
void portXhairCmdLine(float x0, float y0, float x1, float y1, float thick, unsigned rgba);
void portXhairCmdRect(float x0, float y0, float x1, float y1, unsigned rgba);
void portXhairCmdCircle(float x, float y, float r, float thick, unsigned rgba);   /* thick <= 0: a filled disc */
int portXhairItemId(const char *upper);   /* ITEM_* id of a lower/upper name (port_lua.c names), -1 */

/* Lua half (port_luavm_crosshair.c). Called by the game half from the sight's draw, on the sim thread. The frame info: */
typedef struct PortXhairFrame {
    float x, y;          /* aim point, HUD px */
    float spread;        /* the dynamic gap's extra, HUD px */
    float sincehit;      /* seconds since the last hit, < 0 none yet */
    int headshot;        /* the last hit was to the head */
    int aiming;
    int weapon;          /* ITEM_* in the right hand */
    int player;          /* 0-based */
    float w, h;          /* the HUD frame */
} PortXhairFrame;
int portLuaXhairHooks(void);                         /* draw handlers or a style draw function exist */
void portLuaXhairRun(const PortXhairFrame *f, int layer);   /* layer: the style whose draw function to call (-1 none) */

#endif
