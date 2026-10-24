/* port_heist.c - generic bindings for the heist mode (Lua door.* / spawn.key / bond.has_key / alarm.* / cctv.* / hud.countdown and the
 * alarm, cctv_spotted, use and gadget_used events; modding/lua.md, designs/bank-heist.md P0 job 2).
 * PC only. Everything here is sim state a mode script drives identically on both machines (mode scripts run lock-step), per player where
 * a player is involved. Door and camera indices are the setup-record indices the probe logs (`ge: probe: DOOR idx=N`). */
#ifndef TARGET_N64

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <ultra64.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include "game/chrai.h"
#include "game/loadobjectmodel.h"
#include "game/stan.h"
#include "game/player.h"
#include "port_blocks.h"
#include "port_mode.h"
#include "port_event.h"
#include "port_lua.h"

#define HEIST_REC_MAX 6000
#define CCTV_OV_MAX   32
#define KEY_REC_N     64
#define RAD2DEG       (180.0f / 3.14159265f)

extern void domakedefaultobj(s32 stageId, ObjectRecord *obj, s32 cmdindex);
extern void doorActivate(DoorRecord *door, DOORSTATE state);
extern void alarmActivate(void);
extern void alarmDeactivate(void);
extern bool alarmIsActive(void);
extern s32 objGetDestroyedLevel(ObjectRecord *obj);
extern void countdownTimerSetVisible(int clocklockbits, bool unset);
extern void countdownTimerSetValue(f32 time);
extern f32 countdownTimerGetValue(void);
extern void countdownTimerSetRunning(bool enable);
extern bool countdownTimerIsRunning(void);
extern s32 get_cur_playernum(void);
extern PropRecord *propFindForInteract(void);
extern u32 bondinvPlayerKeyFlags(struct player *pl);
extern s32 alarm_timer;
void portHeistNoteUse(PropRecord *prop);
void portHeistNoteGadget(int item);

static PropDefHeaderRecord *sRec[HEIST_REC_MAX];
static int sRecN = -1;                 /* -1 = not scanned yet for this stage */
static int sPerPlayerKeys;
static int sCctvAllTicks;              /* set_alarm_time without a camera: every camera (0 = stock) */
static const void *sCctvPtr[CCTV_OV_MAX];
static int sCctvTicks[CCTV_OV_MAX];
static int sCctvOvN;
static int sCctvAlarmOnSpot = 1;
static unsigned char sCctvOff[HEIST_REC_MAX / 8 + 1];   /* cameras a script switched off (so enable() never revives a shot one) */
static KeyRecord sKeyRecs[KEY_REC_N];
static int sKeyNext;

void portHeistStageReset(void)
{
    sRecN = -1;
    sPerPlayerKeys = 0;
    sCctvAllTicks = 0;
    sCctvOvN = 0;
    sCctvAlarmOnSpot = 1;
    memset(sCctvOff, 0, sizeof(sCctvOff));
    memset(sKeyRecs, 0, sizeof(sKeyRecs));
    sKeyNext = 0;
}

static void heistScan(void)
{
    PropDefHeaderRecord *ph;

    if (sRecN >= 0)
    {
        return;
    }
    sRecN = 0;
    if (g_CurrentSetup.propDefs == NULL)
    {
        return;
    }
    for (ph = g_CurrentSetup.propDefs; ph->type != PROPDEF_END && sRecN < HEIST_REC_MAX; ph = (PropDefHeaderRecord *) (((u32 *) ph) + sizepropdef(ph)))
    {
        sRec[sRecN++] = ph;
    }
}

int portHeistSetupIndex(const void *rec)
{
    int i;

    heistScan();
    for (i = 0; i < sRecN; i++)
    {
        if ((const void *) sRec[i] == rec) return i;
    }
    return -1;
}

static DoorRecord *heistDoor(int idx)
{
    heistScan();
    if (!portModeActive() || idx < 0 || idx >= sRecN || sRec[idx]->type != PROPDEF_DOOR) return NULL;
    return (DoorRecord *) sRec[idx];
}

static CCTVRecord *heistCctv(int idx)
{
    heistScan();
    if (!portModeActive() || idx < 0 || idx >= sRecN || sRec[idx]->type != PROPDEF_CCTV) return NULL;
    return (CCTVRecord *) sRec[idx];
}

static int heistList(int type, int *out, int max)
{
    int i, n = 0;

    heistScan();
    if (!portModeActive()) return 0;
    for (i = 0; i < sRecN && n < max; i++)
    {
        if (sRec[i]->type == type) out[n++] = i;
    }
    return n;
}

static int propRoom(const PropRecord *p)
{
    return (p != NULL && p->rooms[0] != 0xFF) ? (int) p->rooms[0] : -1;
}

/* ---- doors ---- */

int portHeistDoorList(int *out, int max) { return heistList(PROPDEF_DOOR, out, max); }

int portHeistDoorGet(int idx, PortHeistDoor *d)
{
    DoorRecord *door = heistDoor(idx);
    ObjectRecord *o = (ObjectRecord *) door;

    if (door == NULL) return 0;
    memset(d, 0, sizeof(*d));
    d->idx = idx;
    d->keyflags = (unsigned) door->keyflags;
    d->state = (int) door->openstate;
    d->open = door->openPosition;
    d->live = o->prop != NULL;
    d->autoclose = (int) door->autoCloseFrames;
    d->linked = door->linkedDoor != NULL ? portHeistSetupIndex(door->linkedDoor) : -1;
    d->cell = o->obj == PROP_DOORPRISON1;
    d->padlocked = (o->runtime_bitflags & RUNTIMEBITFLAG_PADLOCKEDDOOR) != 0;
    d->room = -1;
    if (o->prop != NULL)
    {
        d->x = o->prop->pos.x; d->y = o->prop->pos.y; d->z = o->prop->pos.z;
        d->room = propRoom(o->prop);
    }
    return 1;
}

/* open / close: start the move unless the door already is (or is going) that way; -1 no such door, 0 nothing to do, 1 started */
int portHeistDoorMove(int idx, int open)
{
    DoorRecord *door = heistDoor(idx);

    if (door == NULL) return -1;
    if (((ObjectRecord *) door)->prop == NULL) return 0;
    if (open)
    {
        if (door->openstate == DOORSTATE_OPENING || (door->openstate != DOORSTATE_CLOSING && door->openPosition > 0.5f)) return 0;
        doorActivate(door, DOORSTATE_OPENING);
    }
    else
    {
        if (door->openstate == DOORSTATE_CLOSING || (door->openstate != DOORSTATE_OPENING && door->openstate != DOORSTATE_WAITING && door->openPosition <= 0.0f)) return 0;
        doorActivate(door, DOORSTATE_CLOSING);
    }
    return 1;
}

/* mode 0: keyflags |= flags (lock), 1: keyflags &= ~flags (unlock; flags 0xFFFFFFFF clears all), 2: keyflags = flags */
int portHeistDoorKeyflags(int idx, int mode, unsigned flags)
{
    DoorRecord *door = heistDoor(idx);

    if (door == NULL) return 0;
    if (mode == 0) door->keyflags |= flags;
    else if (mode == 1) door->keyflags &= ~flags;
    else door->keyflags = flags;
    return 1;
}

extern void set_cur_player(s32 player);
extern TICKOP propdoorInteract(PropRecord *doorprop);

/* Bond `player` uses door `idx` the way the use key would (same key check as stock, with THAT Bond as the current player, plus the
 * `use` event): 1 = the door started to move, 0 = refused (locked), -1 no such door / Bond. A script helper and the test hook. */
int portHeistTryDoor(int player, int idx)
{
    DoorRecord *door = heistDoor(idx);
    PropRecord *prop;
    s32 saved;
    int before, moved;

    if (door == NULL || player < 0 || player >= (int) portBondCount() || !portBondInPlay(player)) return -1;
    prop = ((ObjectRecord *) door)->prop;
    if (prop == NULL) return -1;
    saved = get_cur_playernum();
    set_cur_player(player);
    before = (int) door->openstate;
    portHeistNoteUse(prop);
    propdoorInteract(prop);
    moved = (int) door->openstate != before;
    set_cur_player(saved);
    return moved ? 1 : 0;
}

/* test hook: run the gadget_used plumbing for Bond `player` and a gadget name (the real source is the gadget's use animation, gunfire.c) */
int portHeistTestGadget(int player, const char *name)
{
    static const struct { const char *n; int id; } tab[] = { { "doordecoder", ITEM_DOORDECODER }, { "bombdefuser", ITEM_BOMBDEFUSER },
        { "datathief", ITEM_DATATHIEF }, { "safecracker", ITEM_SAFECRACKERCASE }, { "lockexploder", ITEM_LOCKEXPLODER }, { NULL, 0 } };
    int i;
    s32 saved;

    if (!portModeActive() || player < 0 || player >= (int) portBondCount() || !portBondInPlay(player)) return 0;
    for (i = 0; tab[i].n != NULL; i++)
    {
        if (strcmp(tab[i].n, name) == 0)
        {
            saved = get_cur_playernum();
            set_cur_player(player);
            portHeistNoteGadget(tab[i].id);
            set_cur_player(saved);
            return 1;
        }
    }
    return 0;
}

/* ---- keys ---- */

int portHeistPerPlayerKeys(void) { return sPerPlayerKeys; }

void portHeistSetPerPlayerKeys(int on) { sPerPlayerKeys = on ? 1 : 0; }

unsigned portHeistBondKeys(int player)
{
    if (!portModeActive() || player < 0 || player >= (int) portBondCount() || g_playerPointers[player] == NULL) return 0;
    return (unsigned) bondinvPlayerKeyFlags(g_playerPointers[player]);
}

/* a key record: assigned to a chr (pad = chrnum, flag 0x4000) or on a pad. Returns 1..KEY_REC_N, 0 = no chr / prop / model, -2 = bad pad */
int portHeistSpawnKey(int where, int isChr, unsigned flags, int model)
{
    KeyRecord *r;
    ObjectRecord *o;
    int h;

    if (!portModeActive()) return 0;
    if (isChr)
    {
        ChrRecord *c = chrFindByLiteralId(where);

        if (c == NULL || c->prop == NULL || c->model == NULL) return 0;
    }
    else if (where < 0 || where >= (int) portPadCount())
    {
        return -2;
    }
    h = sKeyNext;
    sKeyNext = (sKeyNext + 1) % KEY_REC_N;
    r = &sKeyRecs[h];
    memset(r, 0, sizeof(*r));
    o = (ObjectRecord *) r;
    o->extrascale = 256;
    o->type = PROPDEF_KEY;
    o->obj = model > 0 ? model : 86;     /* 86 = the swipe card */
    o->pad = where;
    o->flags = isChr ? PROPFLAG_ASSIGNEDTOCHR : 1;
    *(s32 *) &o->damage = 1000 << 16;    /* the raw setup form: domakedefaultobj converts it */
    r->keyflags = flags;
    domakedefaultobj(0, o, 0);
    o->state &= ~PROPSTATE_RESPAWN;
    if (o->prop == NULL) return 0;
    return h + 1;
}

/* 1 = the key object exists and nobody carries it off the ground... : alive = still in the world (on the floor or on a chr); x, z when it has a prop */
int portHeistKeyState(int handle, float *x, float *z, int *held)
{
    ObjectRecord *o;

    if (handle < 1 || handle > KEY_REC_N) return 0;
    o = (ObjectRecord *) &sKeyRecs[handle - 1];
    if (o->type != PROPDEF_KEY || o->prop == NULL || (o->runtime_bitflags & RUNTIMEBITFLAG_REMOVE)) return 0;
    *x = o->prop->pos.x;
    *z = o->prop->pos.z;
    *held = o->prop->parent != NULL;
    return 1;
}

/* ---- alarm ---- */

void portHeistAlarm(int on)
{
    if (!portModeActive()) return;
    if (on) alarmActivate(); else alarmDeactivate();
}

int portHeistAlarmActive(void) { return alarmIsActive() ? 1 : 0; }

/* ---- CCTV ---- */

int portHeistCctvList(int *out, int max) { return heistList(PROPDEF_CCTV, out, max); }

int portHeistCctvGet(int idx, PortHeistCctv *c)
{
    CCTVRecord *cam = heistCctv(idx);
    ObjectRecord *o = (ObjectRecord *) cam;
    float face;

    if (cam == NULL) return 0;
    memset(c, 0, sizeof(*c));
    c->idx = idx;
    c->live = o->prop != NULL;
    c->room = -1;
    if (o->prop != NULL)
    {
        c->x = o->prop->pos.x; c->y = o->prop->pos.y; c->z = o->prop->pos.z;
        c->room = propRoom(o->prop);
    }
    face = cam->unkC4 * RAD2DEG + 180.0f;
    if (face >= 360.0f) face -= 360.0f;
    c->face = face;
    c->timer = (int) cam->timer;
    c->disabled = (o->flags & PROPFLAG_CCTV_DISABLED) != 0;
    c->destroyed = c->disabled && !(sCctvOff[idx >> 3] & (1 << (idx & 7)));
    c->lookpad = cam->lookpad;
    c->alarm_ticks = portHeistCctvAlarmFrames(cam, 0);
    return 1;
}

/* 1 = changed, 0 = refused (a shot camera stays dead), -1 no such camera */
int portHeistCctvEnable(int idx, int on)
{
    CCTVRecord *cam = heistCctv(idx);
    ObjectRecord *o = (ObjectRecord *) cam;

    if (cam == NULL) return -1;
    if (on)
    {
        if (!(sCctvOff[idx >> 3] & (1 << (idx & 7)))) return (o->flags & PROPFLAG_CCTV_DISABLED) ? 0 : 1;
        sCctvOff[idx >> 3] &= ~(1 << (idx & 7));
        o->flags &= ~PROPFLAG_CCTV_DISABLED;
    }
    else
    {
        if (o->flags & PROPFLAG_CCTV_DISABLED) return 1;   /* off already (shot or scripted) */
        if (o->prop == NULL) return 0;
        sCctvOff[idx >> 3] |= (1 << (idx & 7));
        o->flags |= PROPFLAG_CCTV_DISABLED;
        cam->timer = 0;
    }
    return 1;
}

/* seconds <= 0 restores the stock time; idx < 0 = every camera */
int portHeistCctvSetAlarmTime(int idx, float seconds)
{
    int ticks = seconds > 0.0f ? (int) (seconds * 60.0f + 0.5f) : 0;
    int i;

    if (idx < 0)
    {
        sCctvAllTicks = ticks;
        sCctvOvN = 0;   /* an all-cameras setting replaces the per-camera ones */
        return 1;
    }
    {
        CCTVRecord *cam = heistCctv(idx);

        if (cam == NULL) return -1;
        for (i = 0; i < sCctvOvN; i++)
        {
            if (sCctvPtr[i] == cam) { sCctvTicks[i] = ticks; return 1; }
        }
        if (sCctvOvN >= CCTV_OV_MAX) return 0;
        sCctvPtr[sCctvOvN] = cam;
        sCctvTicks[sCctvOvN++] = ticks;
    }
    return 1;
}

void portHeistCctvAlarmOnSpot(int on) { sCctvAlarmOnSpot = on ? 1 : 0; }

/* the sight time (ticks) after which camera `rec` fires: the override, else `stock` */
int portHeistCctvAlarmFrames(const void *rec, int stock)
{
    int i;

    for (i = 0; i < sCctvOvN; i++)
    {
        if (sCctvPtr[i] == rec && sCctvTicks[i] > 0) return sCctvTicks[i];
    }
    return sCctvAllTicks > 0 ? sCctvAllTicks : stock;
}

/* propobj.c, the camera saw the current Bond for the full time: tell the script; returns whether the stock alarm should also go off */
int portHeistCctvSpotted(const void *rec)
{
    if (portEventListenerCount(PORT_EV_CCTV_SPOTTED) > 0)
    {
        portEventEmit(PORT_EV_CCTV_SPOTTED, portHeistSetupIndex(rec), (int) get_cur_playernum(), 0, 0);
    }
    return sCctvAlarmOnSpot;
}

/* ---- use / gadget events ---- */

void portHeistNoteUse(PropRecord *prop);
void portHeistNoteGadget(int item);

static void heistTarget(PropRecord *prop, int *kind, int *idx, int *objtype)
{
    const void *rec = NULL;

    *kind = -1; *idx = -1; *objtype = -1;
    if (prop == NULL) return;
    *kind = (int) prop->type;
    if (prop->type == PROP_TYPE_DOOR) rec = prop->door;
    else if (prop->type == PROP_TYPE_OBJ || prop->type == PROP_TYPE_WEAPON) rec = prop->obj;
    if (rec != NULL)
    {
        *idx = portHeistSetupIndex(rec);
        *objtype = (int) ((const ObjectRecord *) rec)->type;
    }
}

/* chrprop.c bond_interact_object: the current Bond pressed use on `prop` (an object or door) */
void portHeistNoteUse(PropRecord *prop)
{
    int kind, idx, ot;

    if (portEventListenerCount(PORT_EV_USE) == 0) return;
    heistTarget(prop, &kind, &idx, &ot);
    portEventEmit(PORT_EV_USE, (int) get_cur_playernum(), kind, idx, ot);
}

/* gunfire.c: the current Bond's gadget `item` ran its use animation (first frame); the target is what the use prompt points at */
void portHeistNoteGadget(int item)
{
    int kind, idx, ot;

    if (portEventListenerCount(PORT_EV_GADGET_USED) == 0) return;
    heistTarget(propFindForInteract(), &kind, &idx, &ot);
    portEventEmit(PORT_EV_GADGET_USED, (int) get_cur_playernum(), item, idx, ot);
}

/* ---- HUD countdown (the stock mission clock) ---- */

void portHeistCountdown(float seconds)
{
    if (!portModeActive()) return;
    if (seconds < 0.0f)
    {
        countdownTimerSetRunning(FALSE);
        countdownTimerSetVisible(1, FALSE);   /* sets the hide bit */
        return;
    }
    countdownTimerSetValue(seconds * 60.0f);
    countdownTimerSetRunning(TRUE);
    countdownTimerSetVisible(1, TRUE);        /* clears the hide bit */
}

float portHeistCountdownLeft(void)
{
    return countdownTimerIsRunning() ? countdownTimerGetValue() / 60.0f : -1.0f;
}

/* folded into the co-op state hash (port_coop.c "flags"); 0 outside a mode */
unsigned portHeistStateHash(void)
{
    unsigned h = 0;
    int i;

    if (!portModeActive()) return 0;
    h = (unsigned) sPerPlayerKeys * 0x9E3779B1u + (unsigned) sCctvAllTicks * 31u + (unsigned) sCctvAlarmOnSpot * 7u;
    for (i = 0; i < sCctvOvN; i++) h = h * 16777619u + (unsigned) sCctvTicks[i];
    for (i = 0; i < (int) sizeof(sCctvOff); i++) h = h * 16777619u + sCctvOff[i];
    if (countdownTimerIsRunning()) h = h * 16777619u + (unsigned) countdownTimerGetValue();
    return h;
}

#endif /* TARGET_N64 */
