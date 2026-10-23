/* port_modmenu.h - the Mods page's items for front.c. Item 0 = Game mode,
 * then the modifier registry in order, then Lua menu items. Text is "\n"-terminated, in static buffers. */
#ifndef PORT_MODMENU_H
#define PORT_MODMENU_H

void        portModMenuRescan(void);    /* page opened: modes dir, boot scripts, Lua getters */
void        portModMenuTick(void);      /* per update while the page is up: refreshes Lua values now and then */
int         portModMenuCount(void);
const char *portModMenuLabel(int i);
const char *portModMenuValueText(int i);
int         portModMenuIsToggle(int i);
int         portModMenuIsOn(int i);
int         portModMenuDisabled(int i);
int         portModMenuListCount(int i);                 /* the mode row only; 0 = step instead */
const char *portModMenuListText(int i, int idx);
int         portModMenuListLive(int i);
void        portModMenuListPick(int i, int idx);
void        portModMenuStep(int i, int dir);

#endif
