/* port_modifier.h - the modifier registry: named, individually switchable gameplay changes.
 * Every modifier is off or at its default unless set. The backends are existing cheats, a port
 * option and new port flags. Plain types only, so port_luavm.c (real libc headers) can include it.
 */
#ifndef PORT_MODIFIER_H
#define PORT_MODIFIER_H

#define PORT_MOD_TOGGLE 0   /* value 0 or 1 */
#define PORT_MOD_INT    1   /* value in [min, max] on a grid of step */

typedef struct PortModifierInfo
{
    const char *name;    /* snake_case id: console, GE_MODS, ini, Lua */
    const char *label;   /* menu text */
    int         kind;    /* PORT_MOD_TOGGLE / PORT_MOD_INT */
    int         min, max, step;   /* TOGGLE: 0, 1, 1 */
    int         def;     /* the default (0, or 100 for the percent knobs) */
    const char *desc;    /* one line */
} PortModifierInfo;

int  portModifierCount(void);
const PortModifierInfo *portModifierAt(int i);   /* NULL when i is out of range */
int  portModifierFind(const char *name);         /* index, or -1; case-insensitive */
int  portModifierGet(int i);                     /* the value that stands (0 for a bad index) */
int  portModifierSet(int i, int v);              /* the SAVED layer; clamps; 1 = accepted; written to midas.ini. Get returns the effective value (override, else saved). */

int  portModifierSetQuiet(int i, int v);         /* a per-stage OVERRIDE (Lua): never saved, cleared at the next stage start */

/* Value text for a menu or the console: "on"/"off" for a toggle, the number for an int. */
void portModifierValueText(int i, char *buf, int n);

/* Applies a "name,name=value,..." list (GE_MODS, the ini, Lua). Bad names and values are
 * logged and skipped. save=1 sets the saved layer and writes the ini; save=0 sets per-stage overrides. Returns how many items were applied. */
int  portModifierApplyList(const char *list, int save);
/* the four co-op session modifiers (coop_exits, coop_respawn, coop_lives, coop_lives_mode): the netplay host's values rule the session */
int  portModifierIsSession(int i);
void portModifierSessionText(char *out, int cap);   /* "coop_exits=0,coop_respawn=0,coop_lives=0,coop_lives_mode=0" (this machine's, GE_MODS included) */

#endif /* PORT_MODIFIER_H */
