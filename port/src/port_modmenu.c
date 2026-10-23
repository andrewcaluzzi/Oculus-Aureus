/* port_modmenu.c - the data behind the front end's Mods page.
 * Item 0 is the Game mode row; then every registered modifier; then the items Lua scripts added
 * with menu.add_toggle / menu.add_number. front.c shows them as SHROW_MOD rows whose
 * port_sheet_row_opt[] is the item index here. */
#ifndef TARGET_N64

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include "port_modmenu.h"
#include "port_modifier.h"
#include "port_mode.h"
#include "port_lua.h"

extern const char *portRootPath(const char *name);   /* main.c */

#define MODES_MAX 16
static char sModes[MODES_MAX][32];
static int sModeN;
static int sTickCount;

static int nameOk(const char *s)
{
    if (s[0] == '\0')
    {
        return 0;
    }
    for (; *s != '\0'; s++)
    {
        if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') || *s == '_' || *s == '-'))
        {
            return 0;
        }
    }
    return 1;
}

static int cmpName(const void *a, const void *b)
{
    return strcmp((const char *) a, (const char *) b);
}

/* A picked mode whose file is gone (or hidden by the cap) goes back to Off, so the row and the
 * mode the next stage runs agree. */
static void clearMissingChoice(void)
{
    const char *cur = portModeMenuChoice();
    int n;

    if (cur[0] == '\0')
    {
        return;
    }
    for (n = 0; n < sModeN; n++)
    {
        if (strcmp(sModes[n], cur) == 0)
        {
            return;
        }
    }
    portModeSetMenuChoice("");
}

/* Data/Scripts/modes/*.lua, by file name without the extension. */
static void scanModes(void)
{
    char dir[400];
    DIR *d;
    struct dirent *e;

    sModeN = 0;
    snprintf(dir, sizeof(dir), "%s", portRootPath("Data/Scripts/modes"));
    d = opendir(dir);
    if (d == NULL)
    {
        clearMissingChoice();
        return;
    }
    while ((e = readdir(d)) != NULL && sModeN < MODES_MAX)
    {
        size_t l = strlen(e->d_name);
        char base[32];

        if (l > 4 && l - 4 < sizeof(base) && strcmp(e->d_name + l - 4, ".lua") == 0)
        {
            memcpy(base, e->d_name, l - 4);
            base[l - 4] = '\0';
            if (nameOk(base))
            {
                memcpy(sModes[sModeN++], base, l - 3);
            }
        }
    }
    closedir(d);
    qsort(sModes, sModeN, sizeof(sModes[0]), cmpName);
    clearMissingChoice();
}

void portModMenuRescan(void)
{
    scanModes();
    portLuaMenuBoot();
    portLuaMenuRefresh();
}

void portModMenuTick(void)
{
    if (++sTickCount >= 30)
    {
        sTickCount = 0;
        portLuaMenuRefresh();
    }
}

int portModMenuCount(void)
{
    return 1 + portModifierCount() + portLuaMenuCount();
}

/* 0 = mode row, 1 = modifier, 2 = Lua item; *k is the index within that group */
static int classify(int i, int *k)
{
    if (i == 0)
    {
        *k = 0;
        return 0;
    }
    if (i - 1 < portModifierCount())
    {
        *k = i - 1;
        return 1;
    }
    *k = i - 1 - portModifierCount();
    return 2;
}

const char *portModMenuLabel(int i)
{
    static char buf[64];
    int k, g = classify(i, &k);

    if (g == 0)
    {
        return "Game Mode\n";
    }
    if (g == 1)
    {
        const PortModifierInfo *m = portModifierAt(k);

        snprintf(buf, sizeof(buf), "%s\n", m != NULL ? m->label : "?");
        return buf;
    }
    snprintf(buf, sizeof(buf), "%s\n", portLuaMenuLabel(k));
    return buf;
}

int portModMenuIsToggle(int i)
{
    int k, g = classify(i, &k);

    if (g == 1)
    {
        const PortModifierInfo *m = portModifierAt(k);

        return m != NULL && m->kind == PORT_MOD_TOGGLE;
    }
    return g == 2 && portLuaMenuIsToggle(k);
}

int portModMenuIsOn(int i)
{
    int k, g = classify(i, &k);

    if (g == 1)
    {
        return portModifierGet(k) != 0;
    }
    if (g == 2)
    {
        return portLuaMenuValue(k) != 0;
    }
    return 0;
}

/* GE_MODE in the environment wins over the menu, so the row is fixed (greyed) while it is set. */
static const char *envMode(void)
{
    const char *e = getenv("GE_MODE");

    return (e != NULL && e[0] != '\0' && e[0] != '0') ? e : NULL;
}

int portModMenuDisabled(int i)
{
    int k, g = classify(i, &k);

    if (g == 0)
    {
        return envMode() != NULL;
    }
    return g == 2 && portLuaMenuDisabled(k);
}

/* The mode row: entry 0 is Off, 1.. are the scripts. */
int portModMenuListCount(int i)
{
    return (i == 0 && envMode() == NULL) ? 1 + sModeN : 0;
}

const char *portModMenuListText(int i, int idx)
{
    static char buf[40];

    (void) i;
    if (idx <= 0 || idx > sModeN)
    {
        return "Off\n";
    }
    snprintf(buf, sizeof(buf), "%s\n", sModes[idx - 1]);
    if (buf[0] >= 'a' && buf[0] <= 'z')
    {
        buf[0] = (char) (buf[0] - 32);
    }
    return buf;
}

int portModMenuListLive(int i)
{
    const char *cur = portModeMenuChoice();
    int n;

    (void) i;
    for (n = 0; cur[0] != '\0' && n < sModeN; n++)
    {
        if (strcmp(sModes[n], cur) == 0)
        {
            return n + 1;
        }
    }
    return 0;
}

void portModMenuListPick(int i, int idx)
{
    (void) i;
    portModeSetMenuChoice((idx <= 0 || idx > sModeN) ? "" : sModes[idx - 1]);
}

const char *portModMenuValueText(int i)
{
    static char buf[40];
    int k, g = classify(i, &k);

    if (g == 0)
    {
        if (envMode() != NULL)
        {
            snprintf(buf, sizeof(buf), "%.24s (env)\n", envMode());
            return buf;
        }
        return portModMenuListText(0, portModMenuListLive(0));
    }
    if (g == 1)
    {
        const PortModifierInfo *m = portModifierAt(k);
        char t[24];

        if (m != NULL && m->kind == PORT_MOD_TOGGLE)
        {
            return portModifierGet(k) ? "On\n" : "Off\n";
        }
        portModifierValueText(k, t, sizeof(t));
        snprintf(buf, sizeof(buf), "%s\n", t);
        return buf;
    }
    if (portLuaMenuDisabled(k))
    {
        return "Error\n";
    }
    if (portLuaMenuIsToggle(k))
    {
        return portLuaMenuValue(k) ? "On\n" : "Off\n";
    }
    snprintf(buf, sizeof(buf), "%d\n", portLuaMenuValue(k));
    return buf;
}

/* Left / right, or A: a toggle flips, a number steps and wraps, the mode row cycles. */
void portModMenuStep(int i, int dir)
{
    int k, g = classify(i, &k);
    int mn, mx, st, v;

    if (g == 0)
    {
        int n = 1 + sModeN, idx;

        if (envMode() != NULL)
        {
            return;
        }
        idx = portModMenuListLive(0) + ((dir > 0) ? 1 : -1);

        if (idx >= n) idx = 0;
        if (idx < 0) idx = n - 1;
        portModMenuListPick(0, idx);
        return;
    }
    if (g == 1)
    {
        const PortModifierInfo *m = portModifierAt(k);

        if (m == NULL)
        {
            return;
        }
        if (m->kind == PORT_MOD_TOGGLE)
        {
            portModifierSet(k, !portModifierGet(k));
            return;
        }
        v = portModifierGet(k) + ((dir > 0) ? m->step : -m->step);
        if (v > m->max) v = m->min;
        if (v < m->min) v = m->max;
        portModifierSet(k, v);
        return;
    }
    if (portLuaMenuDisabled(k))
    {
        return;
    }
    if (portLuaMenuIsToggle(k))
    {
        portLuaMenuSet(k, !portLuaMenuValue(k));
        return;
    }
    portLuaMenuRange(k, &mn, &mx, &st);
    v = portLuaMenuValue(k) + ((dir > 0) ? st : -st);
    if (v > mx) v = mn;
    if (v < mn) v = mx;
    portLuaMenuSet(k, v);
}

#endif
