/* port_event.h - minimal C event bus for game modes.
 * Emitting with no listener is a table lookup and a return; listeners must not change sim state
 * that the oracle sees outside a mode. Mode-only emit points live in port_blocks.c; the game-event
 * ones (DAMAGE, ITEM_PICKUP...) sit in src/ behind #ifndef TARGET_N64 and fire on any solo stage. Chr ids are chrnums; PORT_CHR_BOND stands for Bond, PORT_CHR_NONE for unknown.
 *
 * Limits: PORT_EVENT_MAX_LISTENERS (8) listeners per type; port_blocks.h adds 32 zones, 32 watched
 * chrs and a 64-entry last-attacker ring (older victims are overwritten).
 * Ownership: listeners are never cleared automatically (they survive stage loads). Whoever
 * registers (a mode's init, the Lua layer) removes them with portEventOff or portEventClearAll. */
#ifndef PORT_EVENT_H
#define PORT_EVENT_H

#define PORT_CHR_NONE (-1)
#define PORT_CHR_BOND (-2)
/* The Bond of player p (0 = the host's Bond): -2, -3, ... PORT_CHR_BOND itself is player 0, so solo and the old scripts are unchanged. */
#define PORT_CHR_BONDP(p) (PORT_CHR_BOND - (p))
#define PORT_CHR_IS_BOND(id) ((id) <= PORT_CHR_BOND && (id) >= PORT_CHR_BOND - 3)
#define PORT_CHR_BOND_PLAYER(id) (PORT_CHR_BOND - (id))
#define PORT_EVENT_MAX_LISTENERS 8

typedef enum PortEventType
{
    PORT_EV_NONE = 0,
    PORT_EV_CHR_DIED,      /* a = victim chrnum, b = killer chrnum (PORT_CHR_BOND / PORT_CHR_NONE), c = 0 */
    PORT_EV_ZONE_ENTER,    /* a = zone id, b = chrnum (PORT_CHR_BONDP(p) for Bond p; PORT_CHR_BOND = player 0), c = 0 */
    PORT_EV_ZONE_LEAVE,    /* same args as ZONE_ENTER */
    PORT_EV_STAGE_START,   /* a = stage id; first tick every Bond is in first person, mode stages only */
    PORT_EV_BOND_DIED,     /* a = killer chrnum (always PORT_CHR_NONE in v1), b = the player index that died (co-op: per Bond); fires even in the death cam */
    PORT_EV_HOSTAGE_FOLLOW, /* a = hostage chrnum, b = the leader's player index (the Bond who told it to follow); Bond's use press or portHostageSetState (port_hostage.h) */
    PORT_EV_HOSTAGE_WAIT,   /* same args as HOSTAGE_FOLLOW */
    PORT_EV_DAMAGE,         /* a = victim (chrnum / PORT_CHR_BOND), b = attacker (chrnum / BOND / NONE), c = amount in
                             * milli-units (damage actually applied * 1000), d = weapon id or -1 */
    PORT_EV_CHR_SPAWNED,    /* a = chrnum (port spawns only) */
    PORT_EV_ITEM_PICKUP,    /* a = PROPDEF object type, b = id (weapon number / ammo type, else -1), c = quantity */
    PORT_EV_OBJECTIVE_CHANGED, /* a = objective index, b = new status (OBJECTIVESTATUS_*) */
    PORT_EV_COUNT
} PortEventType;

typedef void (*PortEventFn)(int type, int a, int b, int c, int d, void *ud);

/* 0 = ok, -1 = bad type, NULL fn, or the type's table is full. The same (fn, ud) pair may register twice. */
int         portEventOn(int type, PortEventFn fn, void *ud);
void        portEventOff(int type, PortEventFn fn, void *ud);   /* removes one matching (fn, ud) */
void        portEventClearAll(void);                            /* drop every listener of every type */
void        portEventEmit(int type, int a, int b, int c, int d);
int         portEventAnyListeners(void);                        /* nonzero if any type has a listener */
void        portEventEmitDamage(int victim, int attacker, float amount, int weapon);   /* no-op without listeners */
void        portEventSetAttacker(int chrnum);                   /* the chr whose shot is being resolved; PORT_CHR_NONE clears */
int         portEventAttacker(void);
const char *portEventName(int type);

#endif /* PORT_EVENT_H */
