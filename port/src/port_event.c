/* port_event.c - the C event bus (port_event.h). */
#ifndef TARGET_N64

#include <stddef.h>
#include "port_event.h"

static struct { PortEventFn fn; void *ud; } sL[PORT_EV_COUNT][PORT_EVENT_MAX_LISTENERS];
static int sN[PORT_EV_COUNT];

int portEventOn(int type, PortEventFn fn, void *ud)
{
    if (type <= PORT_EV_NONE || type >= PORT_EV_COUNT || fn == NULL || sN[type] >= PORT_EVENT_MAX_LISTENERS)
    {
        return -1;
    }
    sL[type][sN[type]].fn = fn;
    sL[type][sN[type]].ud = ud;
    sN[type]++;
    return 0;
}

void portEventOff(int type, PortEventFn fn, void *ud)
{
    int i;

    if (type <= PORT_EV_NONE || type >= PORT_EV_COUNT)
    {
        return;
    }
    for (i = 0; i < sN[type]; i++)
    {
        if (sL[type][i].fn == fn && sL[type][i].ud == ud)
        {
            sN[type]--;
            sL[type][i] = sL[type][sN[type]];
            return;
        }
    }
}

void portEventClearAll(void)
{
    int i;

    for (i = 0; i < PORT_EV_COUNT; i++)
    {
        sN[i] = 0;
    }
}

void portEventEmit(int type, int a, int b, int c, int d)
{
    int i, n;

    if (type <= PORT_EV_NONE || type >= PORT_EV_COUNT || sN[type] == 0)
    {
        return;
    }
    n = sN[type];
    for (i = 0; i < n && i < sN[type]; i++)
    {
        sL[type][i].fn(type, a, b, c, d, sL[type][i].ud);
    }
}

int portEventListenerCount(int type)
{
    return (type > PORT_EV_NONE && type < PORT_EV_COUNT) ? sN[type] : 0;
}

int portEventAnyListeners(void)
{
    int i;

    for (i = 1; i < PORT_EV_COUNT; i++)
    {
        if (sN[i] != 0)
        {
            return 1;
        }
    }
    return 0;
}

void portEventEmitDamage(int victim, int attacker, float amount, int weapon)
{
    if (sN[PORT_EV_DAMAGE] != 0)
    {
        float m = amount * 1000.0f;

        if (!(m == m))
        {
            m = 0.0f;   /* NaN */
        }
        else if (m > 1.0e9f)
        {
            m = 1.0e9f;
        }
        else if (m < -1.0e9f)
        {
            m = -1.0e9f;
        }
        portEventEmit(PORT_EV_DAMAGE, victim, attacker, (int)m, weapon);
    }
}

static int sAttacker = PORT_CHR_NONE;

void portEventSetAttacker(int chrnum)
{
    sAttacker = chrnum;
}

int portEventAttacker(void)
{
    return sAttacker;
}

const char *portEventName(int type)
{
    static const char *const names[PORT_EV_COUNT] = { "NONE", "CHR_DIED", "ZONE_ENTER", "ZONE_LEAVE", "STAGE_START", "BOND_DIED",
                                                       "HOSTAGE_FOLLOW", "HOSTAGE_WAIT", "DAMAGE", "CHR_SPAWNED",
                                                       "ITEM_PICKUP", "OBJECTIVE_CHANGED", "ALARM", "CCTV_SPOTTED", "USE", "GADGET_USED",
                                                       "BODY_FOUND", "CIVILIAN_KILLED", "CIVILIAN_SURRENDERED", "CIVILIAN_ROBBED",
                                                       "CHR_KNOCKED_OUT", "CHR_WOKEN", "CHR_SEARCHED", "CCTV_VIEW",
                                                       "COVER_BLOWN", "COVER_REPORT", "NOISE", "GUARD_BARK" };

    return (type > 0 && type < PORT_EV_COUNT) ? names[type] : "?";
}

#endif /* TARGET_N64 */
