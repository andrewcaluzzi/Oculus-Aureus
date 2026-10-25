/* port_heist.c - generic bindings for the heist mode (Lua door.* / spawn.key / bond.has_key / alarm.* / cctv.* / hud.countdown and the
 * alarm, cctv_spotted, use and gadget_used events; modding/lua.md, designs/bank-heist.md P0 job 2).
 * PC only. Everything here is sim state a mode script drives identically on both machines (mode scripts run lock-step), per player where
 * a player is involved. Door and camera indices are the setup-record indices the probe logs (`ge: probe: DOOR idx=N`). */
#ifndef TARGET_N64

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <stdint.h>
#include <ultra64.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include <bondaicommands.h>
#include "game/chrai.h"
#include "game/loadobjectmodel.h"
#include "game/stan.h"
#include "game/player.h"
#include "game/chr.h"
#include "game/chraction.h"
#include "game/initanitable.h"
#include "game/model.h"
#include "game/gun.h"
#include "game/bondview.h"
#include "game/lv.h"
#include "game/math_atan2f.h"   /* the game's atan2f: the shadow <math.h> (include/math.h) declares only sinf/cosf/sqrtf, and an
                                 * implicit atan2f / fabsf returned garbage (bond.face, the civilian aim test, offset spawns) */
#include "snd.h"
#include "music.h"
#include "port_blocks.h"
#include "port_console.h"
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
void portHeistGuardsReset(void);
static void cctvBondReset(void);
static void holdFireReset(void);
static void coverReset(void);
static int coverActive(void);
static int coverCamSuspect(int p);
static void coverAct(int p);
static void coverShot(int p);
static void doorRulesReset(void);
static void radioSfx(s32 chrnum, int relay);
extern int portGadgetsDisguiseClass(int p, int cls);        /* port_gadgets.c: the room class through Bond p's disguise */
extern float portGadgetsDisguiseCloseScale(int p);          /* port_gadgets.c: a disguised Bond is looked at closely */

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
    cctvBondReset();
    holdFireReset();
    portHeistGuardsReset();
    coverReset();
    {
        extern void portHeistModeStateReset(void);   /* mode.state and the per-Bond use-key flags */

        portHeistModeStateReset();
    }
    {
        extern void portCctvViewReset(void);   /* port_cctvview.c: nobody watches a camera at a stage start */

        portCctvViewReset();
    }
    {
        extern void portGadgetsReset(void);   /* port_gadgets.c: the gadgets' triggers, reels and disguises */

        portGadgetsReset();
    }
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

/* the CCTV record of setup index idx (port_cctvview.c), NULL if it is not a camera or no mode runs */
void *portHeistCctvRecord(int idx)
{
    return heistCctv(idx);
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

/* door.one_way(i [, false]): whether door i is a stock one-way door (flags2 0x10000000 / 0x08000000: it opens from its free side whatever
 * the key, propdoorInteract); `clear` drops both bits so only the key opens it. -1 no such door, else the state before. */
int portHeistDoorOneWay(int idx, int clear)
{
    DoorRecord *door = heistDoor(idx);
    int was;

    if (door == NULL) return -1;
    was = (door->flags2 & (PROPFLAG2_10000000 | PROPFLAG2_08000000)) != 0;
    if (clear) door->flags2 &= ~(PROPFLAG2_10000000 | PROPFLAG2_08000000);
    return was;
}

/* door.snap(i, open): put door i (and its linked siblings) fully open / shut now, whatever stands in the doorway (stock: a blocked door just
 * stops where it is). For a vault time lock. The portal opens / closes as the stock move would. -1 no such door, 0 no prop, 1 done. */
extern void doorBuildClippedVertices(DoorRecord *inDoor);
extern void doorFinishClose(DoorRecord *door);
extern void doorStartOpen(DoorRecord *door);
extern void doorUpdateBbox(DoorRecord *door);
extern s32 g_GlobalTimer;

int portHeistDoorSnap(int idx, int open)
{
    DoorRecord *door = heistDoor(idx), *d;
    int n = 0;

    if (door == NULL) return -1;
    if (((ObjectRecord *) door)->prop == NULL) return 0;
    for (d = door; d != NULL && n < 8; n++)
    {
        f32 to = open ? d->maxFrac : 0.0f;
        int moved = d->openPosition != to;

        if (open && moved && d->openPosition <= 0.0f) doorStartOpen(d);   /* the portal opens (and the stock open sound) */
        d->openPosition = to;
        d->lastcalc60f = to;
        d->speed = 0.0f;
        d->openstate = DOORSTATE_STATIONARY;
        d->openedTime = open ? (u32) g_GlobalTimer : 0;
        if (((ObjectRecord *) d)->prop != NULL)
        {
            doorUpdateBbox(d);
            doorBuildClippedVertices(d);
            if (!open && moved) doorFinishClose(d);   /* the portal closes */
        }
        d = d->linkedDoor;
        if (d == door) break;
    }
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

/* ---- any object (obj.get / obj.test_damage; the levelgen object tests, P2.2) ---- */

extern void objApplyDamage(ObjectRecord *obj, f32 damage, coord3d *pos, ITEM_IDS itemnum, s32 playernum);
extern bool objIsHealthy(ObjectRecord *self);

static ObjectRecord *heistObj(int idx)
{
    heistScan();
    if (!portModeActive() || idx < 0 || idx >= sRecN) return NULL;
    switch (sRec[idx]->type)
    {
        case PROPDEF_DOOR: case PROPDEF_PROP: case PROPDEF_KEY: case PROPDEF_ALARM: case PROPDEF_CCTV: case PROPDEF_MAGAZINE:
        case PROPDEF_COLLECTABLE: case PROPDEF_MONITOR: case PROPDEF_MULTI_MONITOR: case PROPDEF_RACK: case PROPDEF_AUTOGUN:
        case PROPDEF_HAT: case PROPDEF_AMMO: case PROPDEF_ARMOUR: case PROPDEF_GLASS: case PROPDEF_SAFE: case PROPDEF_TINTED_GLASS:
            return (ObjectRecord *) sRec[idx];
        default:
            return NULL;
    }
}

int portHeistObjGet(int idx, PortHeistObj *o)
{
    ObjectRecord *obj = heistObj(idx);

    if (obj == NULL) return 0;
    memset(o, 0, sizeof(*o));
    o->idx = idx;
    o->type = (int) obj->type;
    o->model = (int) obj->obj;
    o->pad = (int) obj->pad;
    o->live = obj->prop != NULL;
    o->room = -1;
    o->health = obj->damage;        /* after setup: the hit points (the setup's fixed-point "damage" field) */
    o->damage = obj->maxdamage;     /* damage taken so far */
    o->destroyed = obj->prop != NULL ? (objGetDestroyedLevel(obj) > 0 || !objIsHealthy(obj)) : 0;
    if (obj->prop != NULL)
    {
        o->x = obj->prop->pos.x; o->y = obj->prop->pos.y; o->z = obj->prop->pos.z;
        o->room = propRoom(obj->prop);
    }
    return 1;
}

/* the stock damage path, as a hit from Bond `player`'s PP7 at the object's position would take it (a padlock shot off, an alarm box or
 * camera shot out) */
int portHeistObjDamage(int idx, float amount, int player)
{
    ObjectRecord *obj = heistObj(idx);
    coord3d pos;

    if (obj == NULL || obj->prop == NULL) return -1;
    pos = obj->prop->pos;
    objApplyDamage(obj, amount, &pos, ITEM_WPPK, player < 0 ? 0 : player);
    return 1;
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

extern void chrlvKneelingAnimationRelated7F023E48(ChrRecord *self);

/* alarm.stop() sticks: a raiser whose alarm animation is past frame 60 (chrlvTickStartAlarm re-fires alarmActivate every tick from there
 * to the end of the animation) is stopped as if the animation had ended (the stock end-of-animation step, chrStop); its list then goes on
 * to "run to Bond" as it would have. A raiser still before frame 60 has not raised it yet and still will. Mode only. */
void portHeistAlarm(int on)
{
    int i;

    if (!portModeActive()) return;
    if (on) { alarmActivate(); return; }
    alarmDeactivate();
    for (i = 0; g_ChrSlots != NULL && i < g_NumChrSlots; i++)
    {
        ChrRecord *c = &g_ChrSlots[i];

        if (c->model != NULL && c->prop != NULL && c->actiontype == ACT_STARTALARM && modelGetAnimFrame(c->model) >= 60.0f)
        {
            printf("ge: heist: alarm.stop: raiser chr %d stopped at frame %.0f of %.0f\n", (int) c->chrnum, modelGetAnimFrame(c->model),
                   modelGetAnimEndFrame(c->model));
            chrlvKneelingAnimationRelated7F023E48(c);
        }
    }
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

static int cctvSpottedBy(const void *rec, int p)
{
    if (portEventListenerCount(PORT_EV_CCTV_SPOTTED) > 0)
    {
        portEventEmit(PORT_EV_CCTV_SPOTTED, portHeistSetupIndex(rec), p, 0, 0);
    }
    return sCctvAlarmOnSpot;
}

/* propobj.c, the camera saw the current Bond for the full time: tell the script; returns whether the stock alarm should also go off */
int portHeistCctvSpotted(const void *rec)
{
    return cctvSpottedBy(rec, (int) get_cur_playernum());
}

/* ---- co-op (design P3): a camera sees every Bond, each on his own sight timer ----
 * The stock test (propobj.c objTick) reads getCurrentPlayerProp(): the Bond whose pass is the sim pass, so in co-op a camera never saw the
 * other one. With more than one Bond this replaces it (the stock solo path is untouched): the same range, sweep cone (45 degrees either
 * side), "not while it moves" and stan line, for every living Bond, each with his own accumulated sight time on this camera (stock: the
 * time adds up while seen and is cleared when the alarm fires). The camera's own timer shows the longest. Sim state (hashed). */
#define CCTV_BOND_CAMS 32
static const void *sCamBondRec[CCTV_BOND_CAMS];
static s32 sCamBondT[CCTV_BOND_CAMS][4];
static int sCamBondN;

extern void bondviewUpdateGuardTankFlagsRelated(PropRecord *prop, s32 flag);

static void cctvBondReset(void)
{
    sCamBondN = 0;
    memset(sCamBondRec, 0, sizeof(sCamBondRec));
    memset(sCamBondT, 0, sizeof(sCamBondT));
}

static int cctvSeesBond(PropRecord *prop, CCTVRecord *cam, PropRecord *bp)
{
    ObjectRecord *obj = (ObjectRecord *) cam;
    f32 xd = bp->pos.x - obj->runtime_pos.x, yd = bp->pos.y - obj->runtime_pos.y, zd = bp->pos.z - obj->runtime_pos.z;
    f32 range = cam->unkE8, a, face, delta;
    StandTile *st;
    int ok;

    if (range > 0.0f && range * range < xd * xd + yd * yd + zd * zd) return 0;
    if (obj->flags & PROPFLAG_INMOTION) return 0;
    a = atan2f(xd, zd);
    face = cam->unkC8;
    if (face < 0.0f) face += M_TAU_F; else if (face >= M_TAU_F) face -= M_TAU_F;
    face += cam->unkC4;
    if (face >= M_TAU_F) face -= M_TAU_F;
    delta = a - face;
    if (a < face) delta += M_TAU_F;
    delta -= M_PI_F;
    if (delta < 0.0f) delta += M_TAU_F;
    if (delta > M_PI_F) delta -= M_TAU_F;
    if (delta > DegToRad(45) || delta < DegToRad(-45)) return 0;
    st = prop->stan;
    bondviewUpdateGuardTankFlagsRelated(bp, 0);
    ok = stanTestLineUnobstructed(&st, prop->pos.x, prop->pos.z, bp->pos.x, bp->pos.z, 0x1B, 100.0f, 100.0f, 0.0f, 1.0f) != 0;
    bondviewUpdateGuardTankFlagsRelated(bp, 1);
    return ok;
}

/* propobj.c objTick, a camera's sight test: 1 = done here (co-op), 0 = one Bond: the stock test runs */
int portHeistCctvAllBonds(PropRecord *prop, void *rec, int stockFrames)
{
    CCTVRecord *cam = (CCTVRecord *) rec;
    int n = (int) portBondCount(), k, p, best = 0;

    if (n <= 1 && !coverActive()) return 0;   /* solo: the stock test, unless the heist cover gates what a camera times (then here) */
    for (k = 0; k < sCamBondN && sCamBondRec[k] != rec; k++)
    {
    }
    if (k == sCamBondN)
    {
        if (sCamBondN >= CCTV_BOND_CAMS) return 0;
        sCamBondRec[k] = rec;
        memset(sCamBondT[k], 0, sizeof(sCamBondT[k]));
        sCamBondN++;
    }
    for (p = 0; p < n && p < 4; p++)
    {
        struct player *pl = g_playerPointers[p];

        if (!portBondInPlay(p) || pl == NULL || pl->prop == NULL || pl->bonddead) continue;
        if (cctvSeesBond(prop, cam, pl->prop) && coverCamSuspect(p))
        {
            sCamBondT[k][p] += g_ClockTimer;
            if (sCamBondT[k][p] >= portHeistCctvAlarmFrames(rec, stockFrames))
            {
                if (cctvSpottedBy(rec, p)) alarmActivate();
                sCamBondT[k][p] = 0;
            }
        }
        else if (coverActive())
        {   /* cover: a staffer the camera has no reason to time (or out of its sight) lets the timer run back down */
            sCamBondT[k][p] = sCamBondT[k][p] > g_ClockTimer ? sCamBondT[k][p] - g_ClockTimer : 0;
        }
        if (sCamBondT[k][p] > best) best = sCamBondT[k][p];
    }
    cam->timer = best;
    return 1;
}

/* the sight time camera `idx` has on Bond p (co-op), else the camera's own timer; -1 no such camera */
int portHeistCctvBondTimer(int idx, int p)
{
    CCTVRecord *cam = heistCctv(idx);
    int k;

    if (cam == NULL) return -1;
    for (k = 0; k < sCamBondN; k++)
    {
        if (sCamBondRec[k] == cam) return (p >= 0 && p < 4) ? sCamBondT[k][p] : 0;
    }
    return (int) cam->timer;
}

static unsigned cctvBondHash(void)
{
    unsigned h = (unsigned) sCamBondN;
    int k, p;

    for (k = 0; k < sCamBondN; k++)
    {
        for (p = 0; p < 4; p++) h = h * 16777619u + (unsigned) sCamBondT[k][p];
    }
    return h;
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

static u8 sUseKey[4];   /* per Bond: the use press being handled is the Port use action (1), not B (use + reload, 0) */

/* chrprop.c bond_interact_object: the current Bond pressed use on `prop` (an object or door) */
void portHeistNoteUse(PropRecord *prop)
{
    extern s32 portJoyPortUseKeyPeek(void);
    int kind, idx, ot, p = (int) get_cur_playernum();

    if (portEventListenerCount(PORT_EV_USE) == 0) return;
    heistTarget(prop, &kind, &idx, &ot);
    if (p >= 0 && p < 4) sUseKey[p] = (u8) (portJoyPortUseKeyPeek() ? 1 : 0);
    portEventEmit(PORT_EV_USE, p, kind, idx, ot);
    if (p >= 0 && p < 4) sUseKey[p] = 0;
}

/* bond.use_key(p), inside a `use` handler: the press came from the use action (the Port use bind), not the B button (which is also
 * reload). The heist's screens open the camera feeds only on the use action. */
int portHeistUseKey(int p)
{
    return (p >= 0 && p < 4) ? (int) sUseKey[p] : 0;
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

/* ---- guards (job 3): chr slots, patrols, body discovery ---- */

#define HEIST_MODE_SLOTS  250     /* extra chr slots for a mode whose name starts "heist" (on top of the stage's own + 10) */
#define BODY_MAX          512
#define PATROL_MAX        64
#define CIV_MAX           128

/* stage init runs before any Lua: the mode comes from GE_MODE (or the Mods page pick); GE_MODE_SLOTS=n overrides for any mode */
s32 portHeistExtraSlots(void)
{
    const char *e = getenv("GE_MODE_SLOTS");
    const char *m;

    if (e != NULL && atoi(e) > 0)
    {
        int n = atoi(e);

        return n > 3000 ? 3000 : n;
    }
    m = portModeChoice();
    return (m != NULL && strncmp(m, "heist", 5) == 0) ? HEIST_MODE_SLOTS : 0;
}

/* initunk_005520.c: spare object model slots (stock: setup objects + 30). Every gun a spawned chr holds, every key and every loot pickup
 * takes one; with the stock 30 a heist runs out after ~30 armed guards and nothing else can appear (spawn.item / spawn.key fail). */
s32 portHeistExtraObjSlots(void)
{
    s32 n = portHeistExtraSlots();

    return n > 0 ? n + 160 : 0;
}

/* propobj.c weaponCreate, when every stock weapon slot (30) is held and no setup record can be reused: a record from this pool, so a crowd
 * of armed guards stays armed (stock would leave guard 31+ unarmed). A record is free while it has no prop (objFree clears it). Mode only. */
#define XWEAPON_N 320
static WeaponObjRecord sXWeapon[XWEAPON_N];

WeaponObjRecord *portHeistExtraWeapon(s32 musthaveprop, s32 musthavemodel)
{
    int i;

    if (musthaveprop || musthavemodel || !portModeActive() || portHeistExtraSlots() <= 0) return NULL;
    for (i = 0; i < XWEAPON_N; i++)
    {
        if (sXWeapon[i].prop == NULL)
        {
            if (i % 25 == 0) portConLogf("ge: heist: extra weapon slot %d of %d handed out (the stock 30 are held)\n", i + 1, XWEAPON_N);
            return &sXWeapon[i];
        }
    }
    portConLogf("ge: heist: all %d extra weapon slots are in use\n", XWEAPON_N);
    return NULL;
}

/* dyn.c grows the display list and vertex pools by this many slots (the base was sized for ~240 chrs) */
s32 portHeistBigSlots(void)
{
    s32 n = portHeistExtraSlots();

    return n > 60 ? n - 60 : 0;
}

static struct { s32 chrnum, path, at; } sPatrol[PATROL_MAX];
static int sPatrolN;
static s32 sFound[BODY_MAX];
static int sFoundN;
static int sBodyCheck = 1;
static s32 sAcc, sNow;

void portHeistPatrolLater(int chrnum, int path)
{
    if (sPatrolN < PATROL_MAX)
    {
        sPatrol[sPatrolN].chrnum = chrnum;
        sPatrol[sPatrolN].path = path;
        sPatrol[sPatrolN].at = sNow + 6;
        sPatrolN++;
    }
}

int portHeistBodiesFound(void) { return sFoundN; }
void portHeistBodyCheck(int on) { sBodyCheck = on ? 1 : 0; }

static int koIsDown(s32 chrnum);

/* a body to find: dead, or knocked out (job 5) */
static int chrIsBody(ChrRecord *c)
{
    return (chrIsDead(c) || koIsDown(c->chrnum)) ? 1 : 0;
}

int portHeistChrDown(int chrnum)
{
    ChrRecord *c = chrFindByLiteralId(chrnum);

    return (c != NULL && c->model != NULL && c->prop != NULL && chrIsBody(c)) ? 1 : 0;
}

static int bodyFoundAlready(s32 chrnum)
{
    int i;

    for (i = 0; i < sFoundN; i++)
    {
        if (sFound[i] == chrnum) return 1;
    }
    return 0;
}

extern s32 chrlvMaybeSameRoom(ChrRecord *self, coord3d *pos, StandTile *stan);
extern void chrDropItems(ChrRecord *self);
extern int portLuaGameChrAnim(int chrnum, const char *name, int force);
extern s32 g_GlobalTimer;
extern s32 g_ClockTimer;

static int isCivilian(s32 chrnum);
static void koFoundBy(ChrRecord *body, ChrRecord *finder);

/* every ~8 ticks: a living, non-civilian guard that stands within sight of a body (dead chr that is still lying there) notices it:
 * chrseedie is the stock "I saw someone die" field, so the guard's own AI list reacts (simple guard / alarm raiser run to Bond or the alarm pad,
 * standard guards investigate). The first guard to notice a body fires body_found. Same-room test and distance, no facing test (a guard with his back
 * turned still counts). */
static void bodyScan(void)
{
    int i, j;

    if (g_NumChrSlots <= 0 || g_ChrSlots == NULL) return;
    for (i = 0; i < g_NumChrSlots; i++)
    {
        ChrRecord *b = &g_ChrSlots[i];

        if (b->model == NULL || b->prop == NULL || (b->hidden & CHRHIDDEN_REMOVE) || !chrIsBody(b) || bodyFoundAlready(b->chrnum)) continue;
        {
            int first = -1;

            for (j = 0; j < g_NumChrSlots; j++)
            {
                ChrRecord *g = &g_ChrSlots[j];
                float dx, dy, dz, sight;

                if (j == i || g->model == NULL || g->prop == NULL || chrIsBody(g) || isCivilian(g->chrnum)) continue;
                if (g->actiontype == ACT_DIE || g->actiontype == ACT_DEAD || g->actiontype == ACT_ARGH) continue;
                if (g->chrseedie >= 0) continue;   /* already alerted by a death */
                dx = g->prop->pos.x - b->prop->pos.x;
                dy = g->prop->pos.y - b->prop->pos.y;
                dz = g->prop->pos.z - b->prop->pos.z;
                sight = g->visionrange * 100.0f;
                if (sight > 1500.0f) sight = 1500.0f;
                if (dx * dx + dy * dy + dz * dz > sight * sight || dy > 150.0f || dy < -150.0f) continue;
                if (!chrlvMaybeSameRoom(g, &b->prop->pos, b->prop->stan)) continue;
                g->chrseedie = (s16) b->chrnum;
                if (first < 0) first = j;
            }
            if (first >= 0 && sFoundN < BODY_MAX)
            {
                int ko = koIsDown(b->chrnum);

                sFound[sFoundN++] = b->chrnum;
                if (ko) koFoundBy(b, &g_ChrSlots[first]);   /* stretch: the finder walks over and wakes it */
                portEventEmit(PORT_EV_BODY_FOUND, g_ChrSlots[first].chrnum, b->chrnum, ko, 0);
            }
        }
    }
}

/* ---- civilians (job 4) ---- */

/* aimP / threatP: each Bond's own time aiming at it (design P3: every Bond's aim counts on its own, the first to reach the time wins);
 * aimT / threatT are the largest of them */
typedef struct { s32 chrnum; u8 state, dropped, counted, used; f32 aimT, idleT, threatT, cowerT; s32 byPlayer; f32 aimP[4], threatP[4]; } HeistCiv;
static HeistCiv sCiv[CIV_MAX];
static int sCivN, sCivKilled;
static f32 sCivRange = 800.0f, sCivAimSec = 0.5f, sCivThreatSec = 2.0f, sCivCowerSec = 4.0f;

static HeistCiv *civFind(s32 chrnum)
{
    int i;

    for (i = 0; i < sCivN; i++)
    {
        if (sCiv[i].used && sCiv[i].chrnum == chrnum) return &sCiv[i];
    }
    return NULL;
}

static int isCivilian(s32 chrnum) { return civFind(chrnum) != NULL; }

void portHeistCivilianAdd(int chrnum)
{
    HeistCiv *c = civFind(chrnum);

    if (c == NULL && sCivN < CIV_MAX) c = &sCiv[sCivN++];
    if (c != NULL)
    {
        memset(c, 0, sizeof(*c));
        c->chrnum = chrnum;
        c->used = 1;
    }
}

/* a chr spawned as anything but a civilian on a number a removed civilian had (bank v2) */
void portHeistCivilianForget(int chrnum)
{
    HeistCiv *c = civFind(chrnum);

    if (c != NULL) c->used = 0;
}

int portHeistCivilianState(int chrnum)
{
    HeistCiv *c = civFind(chrnum);

    if (c != NULL && koIsDown(chrnum)) return 3;   /* knocked out */
    return c != NULL ? (int) c->state : -1;
}

void portHeistCivilianTune(float range, float aim_sec, float threat_sec, float cower_sec)
{
    if (range > 0.0f) sCivRange = range;
    if (aim_sec > 0.0f) sCivAimSec = aim_sec;
    if (threat_sec > 0.0f) sCivThreatSec = threat_sec;
    if (cower_sec > 0.0f) sCivCowerSec = cower_sec;
}

int portHeistCivilianKilled(void) { return sCivKilled; }

/* ---- hold fire (mode.hold_fire; AC delegated, Claude's call 2026-10-25) ----
 * A guard about to fire holds the shot while a bank civilian (standing, cowering or knocked out) is near his line of fire to his
 * target: within HOLD_RADIUS of the segment and short of the target, or within HOLD_NEAR of the target on any side (P2.4). He keeps
 * his stance and aim and fires when the line clears.
 * Only with mode.hold_fire(true) (the heist); stock behaviour otherwise. The check reads only sim state (positions, the civilian
 * table) and the shot it drops is decided the same way on every machine; the counters below are for tests and feed nothing back. */
#define HOLD_RADIUS 60.0f   /* P2.4: 40 -> 60 (a miss scatters round the line) */
#define HOLD_SHORT  30.0f
#define HOLD_NEAR   90.0f   /* P2.4: a civilian this close to the target, on any side of him, also holds the shot: a miss lands round
                             * Bond, and on the bank a Bond among the tellers had all four killed by guards' misses in ~10 s */
#define FIRESTAT_N  64
static int sHoldFire;
static struct { s32 chrnum; s32 fired, held; } sFireStat[FIRESTAT_N];
static int sFireStatN;

void portHeistSetHoldFire(int on) { sHoldFire = on ? 1 : 0; }
int portHeistHoldFireOn(void) { return sHoldFire; }

static void fireStat(s32 chrnum, int held)
{
    int i;

    for (i = 0; i < sFireStatN; i++)
    {
        if (sFireStat[i].chrnum == chrnum) break;
    }
    if (i == sFireStatN)
    {
        if (sFireStatN >= FIRESTAT_N) return;
        sFireStat[sFireStatN].chrnum = chrnum;
        sFireStat[sFireStatN].fired = sFireStat[sFireStatN].held = 0;
        sFireStatN++;
    }
    if (held) sFireStat[i].held++; else sFireStat[i].fired++;
}

/* spawn.fire_stats(chr) -> shots fired, shots held (since the stage started; the first 64 chrs that tried to shoot) */
int portHeistFireStats(int chrnum, int *fired, int *held)
{
    int i;

    *fired = *held = 0;
    for (i = 0; i < sFireStatN; i++)
    {
        if (sFireStat[i].chrnum == chrnum)
        {
            *fired = sFireStat[i].fired;
            *held = sFireStat[i].held;
            return 1;
        }
    }
    return 0;
}

static void holdFireReset(void)
{
    sHoldFire = 0;
    sFireStatN = 0;
}

/* chraction.c chrlvTriggerFireWeapon: `self` is about to fire at the current player (the action tick's Bond); 1 = hold this shot */
int portHeistHoldFire(ChrRecord *self)
{
    PropRecord *target, *me;
    f32 dx, dz, len2, len;
    int i, hold = 0;

    if (!sHoldFire || !portModeActive() || self == NULL || self->prop == NULL || isCivilian(self->chrnum)) return 0;
    me = self->prop;
    target = getCurrentPlayerProp();
    if (target == NULL) return 0;
    dx = target->pos.x - me->pos.x;
    dz = target->pos.z - me->pos.z;
    len2 = dx * dx + dz * dz;
    if (len2 < 1.0f) return 0;
    len = sqrtf(len2);
    for (i = 0; i < sCivN && !hold; i++)
    {
        ChrRecord *c;
        f32 px, pz, t, ex, ez, along, ty;

        if (!sCiv[i].used) continue;
        c = chrFindByLiteralId(sCiv[i].chrnum);
        if (c == NULL || c->prop == NULL || c == self || (chrIsDead(c) && !koIsDown(c->chrnum))) continue;
        px = c->prop->pos.x - me->pos.x;
        pz = c->prop->pos.z - me->pos.z;
        ex = c->prop->pos.x - target->pos.x;
        ez = c->prop->pos.z - target->pos.z;
        if (ex * ex + ez * ez < HOLD_NEAR * HOLD_NEAR && c->prop->pos.y < target->pos.y + 200.0f && c->prop->pos.y > target->pos.y - 250.0f)
        {
            hold = 1;                                                    /* beside (or just behind) the target */
            continue;
        }
        t = (px * dx + pz * dz) / len2;
        if (t <= 0.0f || t >= 1.0f) continue;
        along = t * len;
        if (along > len - HOLD_SHORT) continue;                      /* at or behind the target: not in the way */
        ex = px - t * dx;
        ez = pz - t * dz;
        if (ex * ex + ez * ez > HOLD_RADIUS * HOLD_RADIUS) continue;
        ty = me->pos.y + t * (target->pos.y - me->pos.y);
        if (c->prop->pos.y > ty + 200.0f || c->prop->pos.y < ty - 250.0f) continue;   /* another floor */
        hold = 1;
    }
    fireStat(self->chrnum, hold);
    return hold;
}

/* port_blocks.c deathsTick: chr `chrnum` just died; killerPlayer = the Bond who killed it, -1 unknown */
void portHeistChrDied(s32 chrnum, s32 killerPlayer)
{
    HeistCiv *c = civFind(chrnum);

    if (c != NULL && !c->counted)
    {
        c->counted = 1;
        sCivKilled++;
        portEventEmit(PORT_EV_CIVILIAN_KILLED, chrnum, killerPlayer, 0, 0);
    }
}

/* a gun or a knife in Bond p's right hand. The slappers (ITEM_FIST), bare hands and gadgets (the door decoder, ...) are not a threat. */
static int bondArmed(int p)
{
    s32 saved = get_cur_playernum();
    int w;

    set_cur_player(p);
    w = (int) getCurrentPlayerWeaponId(GUNRIGHT);
    set_cur_player(saved);
    return w >= ITEM_KNIFE && w <= ITEM_ROCKETLAUNCH;
}

#define CIV_AIM_NEAR   40.0f     /* units: closer than this straight ahead is a shove, not an aim */
#define CIV_AIM_LAT0   30.0f     /* the view ray must pass within LAT0 + LATK * distance of it (a body is ~30 units wide) */
#define CIV_AIM_LATK   0.04f

/* a line of sight between Bond and the civilian: in the same room, yes (the stan line cannot see over a step or a raised floor, which a
 * big hall has); else the stock guard "can see Bond" test (chrlvCurrentPlayerCall7F0B0E24: the stan line from Bond's tile at his eye
 * height to the chr, cdtypes 0x13, ending on the chr's tile), for any Bond. Walls and closed doors block it. */
static int civLineOfSight(ChrRecord *civ, PropRecord *bond)
{
    StandTile *st = bond->stan;
    int ok;

    if (st == NULL || civ->prop->stan == NULL) return 0;
    if (st->room == civ->prop->stan->room) return 1;
    chrSetMoving(civ, 0);
    ok = stanTestLineUnobstructed(&st, bond->pos.x, bond->pos.z, civ->prop->pos.x, civ->prop->pos.z, 0x13, bond->pos.y, bond->pos.y, 0.0f, 1.0f) != 0
         && st == civ->prop->stan;
    chrSetMoving(civ, 1);
    return ok;
}

/* every Bond whose crosshair is on `civ` (bit p = Bond p): holding a gun, within sCivRange, ahead of him, the view ray passing within
 * 30 units (+4% of the distance) of it, and a clear line between them (walls and doors block it). Standing next to one with a gun
 * out, or pointing it past one, is not aiming at it. Each Bond is tested on his own canonical sim state (position, vv_theta). */
static int aimedMask(ChrRecord *civ)
{
    int p, n = (int) portBondCount(), mask = 0;

    for (p = 0; p < n && p < 4; p++)
    {
        struct player *pl = g_playerPointers[p];
        float dx, dy, dz, th, fx, fz, fwd, lat;

        if (!portBondInPlay(p) || pl == NULL || pl->prop == NULL || pl->prop->stan == NULL) continue;
        dx = civ->prop->pos.x - pl->prop->pos.x;
        dy = civ->prop->pos.y - pl->prop->pos.y;
        dz = civ->prop->pos.z - pl->prop->pos.z;
        if (dy > 300.0f || dy < -300.0f) continue;
        th = pl->vv_theta * (3.14159265f / 180.0f);
        fx = -sinf(th);
        fz = cosf(th);
        fwd = dx * fx + dz * fz;
        lat = dx * -fz + dz * fx;
        if (fwd <= CIV_AIM_NEAR || fwd > sCivRange || ABS(lat) > CIV_AIM_LAT0 + CIV_AIM_LATK * fwd) continue;
        if (!bondArmed(p)) continue;
        if (!civLineOfSight(civ, pl->prop)) continue;
        mask |= 1 << p;
    }
    return mask;
}

/* add dt to each aiming Bond's timer in t[] (the others decay, or reset); returns the lowest-index Bond at or past `need`, -1 none;
 * *maxT = the largest timer */
static int civTimers(f32 *t, int mask, f32 dt, f32 need, int decay, f32 *maxT)
{
    int p, first = -1;

    *maxT = 0.0f;
    for (p = 0; p < 4; p++)
    {
        if (mask & (1 << p)) t[p] += dt;
        else if (decay) t[p] = t[p] > dt ? t[p] - dt : 0.0f;
        else t[p] = 0.0f;
        if (t[p] > *maxT) *maxT = t[p];
        if (first < 0 && (mask & (1 << p)) && t[p] >= need) first = p;
    }
    return first;
}

/* test helper: turn Bond `player`'s view toward (x, z) */
int portHeistFaceBond(int player, float x, float z, float y)
{
    struct player *pl;
    float dx, dz;

    (void) y;
    if (!portModeActive() || player < 0 || player >= (int) portBondCount() || (pl = g_playerPointers[player]) == NULL || pl->prop == NULL) return 0;
    dx = x - pl->prop->pos.x;
    dz = z - pl->prop->pos.z;
    pl->vv_theta = atan2f(-dx, dz) * RAD2DEG;
    if (pl->vv_theta < 0.0f) pl->vv_theta += 360.0f;
    pl->vv_verta = 0.0f;
    return 1;
}

/* test helpers: a gunshot of loudness `noise` fired by Bond `player` (the stock hearing check, bondview2.c), and whether a chr heard one */
extern void chrCheckGuardsHeardSound(f32 noise);

int portHeistTestNoise(int player, float noise)
{
    s32 saved;

    if (!portModeActive() || player < 0 || player >= (int) portBondCount() || !portBondInPlay(player)) return 0;
    saved = get_cur_playernum();
    set_cur_player(player);
    coverShot(player);   /* as bondview2.c's portHeistCoverFired does before the stock hearing check (a test shot: whatever is in hand) */
    chrCheckGuardsHeardSound(noise);
    set_cur_player(saved);
    return 1;
}

int portHeistChrHeard(int chrnum, int clear)
{
    ChrRecord *c = chrFindByLiteralId(chrnum);
    int heard;

    if (c == NULL) return -1;
    heard = c->lastheartarget60 > 0 && g_GlobalTimer - c->lastheartarget60 < 30;   /* the AI clears the ALERT hidden bit within a tick */
    if (clear) c->lastheartarget60 = 0;
    return heard;
}

static void civTick(HeistCiv *v, f32 dt)
{
    ChrRecord *c = chrFindByLiteralId(v->chrnum);
    int by, mask;

    if (c == NULL || c->prop == NULL || c->model == NULL || (c->hidden & CHRHIDDEN_REMOVE) || chrIsDead(c))
    {
        return;   /* gone or dead: portHeistChrDied counts the death */
    }
    if (koIsDown(v->chrnum))
    {
        return;   /* knocked out: the KO tick holds it down */
    }
    mask = aimedMask(c);
    if (v->state == 1)
    {   /* hands up: keep the pose; a sustained threat (one Bond's own) makes it hand over what it carries */
        portLuaGameChrAnim(v->chrnum, "surrender", 0);
        if (mask != 0)
        {
            v->idleT = 0.0f;
            by = civTimers(v->threatP, mask, dt, sCivThreatSec, 0, &v->threatT);
            if (!v->dropped && by >= 0)
            {
                v->dropped = 1;
                chrDropItems(c);   /* CHR_DROP_ALL_CONCEALED_ITEMS: a key from spawn.key falls where it stands */
                coverAct(by);      /* robbing staff in view blows the cover */
                portEventEmit(PORT_EV_CIVILIAN_ROBBED, v->chrnum, by, 0, 0);
            }
        }
        else
        {
            v->idleT += dt;
            if (v->idleT > 3.0f)
            {   /* nobody aims at it any more: lowers its hands */
                v->state = 0;
                v->aimT = v->threatT = 0.0f;
                memset(v->aimP, 0, sizeof(v->aimP));
                memset(v->threatP, 0, sizeof(v->threatP));
            }
        }
        return;
    }
    if (v->state == 2)
    {   /* cowering after gunfire */
        portLuaGameChrAnim(v->chrnum, "startled", 0);
        v->cowerT -= dt;
        by = -1;
        if (mask != 0 && v->cowerT > 0.0f) by = civTimers(v->aimP, mask, dt, sCivAimSec, 1, &v->aimT);
        if (v->cowerT <= 0.0f) v->state = 0;
        if (by < 0) return;
    }
    else
    {
        by = civTimers(v->aimP, mask, dt, sCivAimSec, 1, &v->aimT);
    }
    if (by >= 0)
    {
        v->state = 1;
        v->threatT = v->idleT = 0.0f;
        memset(v->threatP, 0, sizeof(v->threatP));
        v->byPlayer = by;
        portLuaGameChrAnim(v->chrnum, "surrender", 1);
        portEventEmit(PORT_EV_CIVILIAN_SURRENDERED, v->chrnum, by, 0, 0);
        return;
    }
    if (mask != 0) return;
    /* gunfire nearby: chrCheckGuardsHeardSound stamps every chr within the noise radius (lastheartarget60) */
    if (c->lastheartarget60 > 0 && g_GlobalTimer - c->lastheartarget60 < 12)
    {
        v->state = 2;
        v->cowerT = sCivCowerSec;
        c->lastheartarget60 = 0;
        portLuaGameChrAnim(v->chrnum, "startled", 1);
    }
}

/* ---- knock-out (job 5) ----
 * Bond's slappers (ITEM_FIST) on a chr: the hit path (chraction.c handles_shot_actors) calls portHeistSlap first. Slaps below the KO count
 * play the stock hit reaction with no damage; the slap that reaches it knocks the chr out: the soft face-down death animation, held at its last
 * frame, no AI list, no collision box, no auto-aim. Not dead: no chr_died, no civilian casualty, no death counters; a gun still kills it (then it
 * is an ordinary death). It never wakes by itself; a guard who finds it (the body scan) can walk over and wake it (mode.guards_wake), or a
 * script calls ko.wake. What it carries stays on it until a Bond presses use next to it (ko.search: chrDropItems, as a robbed civilian). */

#define KO_MAX        256
#define KO_ANIM       ANIM_death_forward_face_down_soft
#define KO_FLAGS      (CHRFLAG_00010000 | CHRFLAG_NO_AUTOAIM)   /* no collision box (chr.c chrUpdateCollisionBounds), no auto-aim */
#define WAKE_REACH    90.0f      /* units: the waker stops this close */
#define WAKE_GIVEUP   20.0f      /* seconds walking before it gives up (and just goes on alert) */
#define WAKE_LOOK     1.5f       /* seconds it looks the body over before it gets up */
#define SEARCH_RANGE  130.0f

typedef struct
{
    s32 chrnum, byPlayer, downAt, waker;
    u32 addedFlags;      /* the KO_FLAGS bits the KO set (cleared again on waking) */
    f32 wT;
    s16 wPlot;
    u8 used, down, searched, slaps, wstate;   /* wstate: 0 nobody, 1 a guard walks over, 2 it looks the body over */
    s8 wArghs;
} HeistKo;

static HeistKo sKo[KO_MAX];
static int sKoN, sKoBusy, sKoTotal, sKoWoken;
static int sSlapsToKo, sGuardsWake = 1;

extern s32 portHostageIsHostage(ChrRecord *chr);
extern s32 portBotIsBot(ChrRecord *chr);
extern int portCoopChrIsBondBody(ChrRecord *chr);
extern bool portChrGoToProp(ChrRecord *self, PropRecord *target, SPEED speed);
extern void chrlvPerformAnimationForActor(ChrRecord *self, s32 animID, s32 startframe, s32 endframe, u8 bitfield, s32 interpol_time60);

static HeistKo *koFind(s32 chrnum)
{
    int i;

    for (i = 0; i < sKoN; i++)
    {
        if (sKo[i].used && sKo[i].chrnum == chrnum) return &sKo[i];
    }
    return NULL;
}

static HeistKo *koGet(s32 chrnum)
{
    HeistKo *e = koFind(chrnum);
    int i;

    if (e != NULL) return e;
    for (i = 0; i < sKoN && sKo[i].used; i++) ;
    if (i == sKoN)
    {
        if (sKoN >= KO_MAX) return NULL;
        sKoN++;
    }
    e = &sKo[i];
    memset(e, 0, sizeof(*e));
    e->used = 1;
    e->chrnum = chrnum;
    e->waker = -1;
    return e;
}

static int koIsDown(s32 chrnum)
{
    HeistKo *e = sKoN > 0 ? koFind(chrnum) : NULL;

    return e != NULL && e->down;
}

static void bodyForget(s32 chrnum)
{
    int i;

    for (i = 0; i < sFoundN; i++)
    {
        if (sFound[i] == chrnum) { sFound[i] = sFound[--sFoundN]; return; }
    }
}

static void koPose(ChrRecord *c, int fall)
{
    chrlvPerformAnimationForActor(c, KO_ANIM, 0, -1, ANIM_UNKNOWN | ANIM_LOOP_HOLD_LAST_FRAME, fall ? 16 : 0);
    if (!fall) modelSetAnimFrame(c->model, modelGetAnimEndFrame(c->model));   /* back down at once (after a hit reaction) */
}

static void koDown(ChrRecord *c, HeistKo *e, int player)
{
    e->down = 1;
    e->byPlayer = player;
    e->downAt = sNow;
    e->searched = 0;
    e->addedFlags = KO_FLAGS & ~c->chrflags;
    c->chrflags |= KO_FLAGS;
    koPose(c, 1);
    sKoBusy++;
    sKoTotal++;
    bodyForget(c->chrnum);   /* knocked out again after a wake: it can be found again */
    portEventEmit(PORT_EV_CHR_KNOCKED_OUT, c->chrnum, player, 0, 0);
}

static int koEligible(ChrRecord *c)
{
    return c != NULL && c->prop != NULL && c->model != NULL && c->prop->type == PROP_TYPE_CHR && !chrIsDead(c)
        && !(c->chrflags & CHRFLAG_INVINCIBLE) && !portHostageIsHostage(c) && !portBotIsBot(c) && !portCoopChrIsBondBody(c);
}

/* chraction.c handles_shot_actors, a hit by Bond's slappers on `c` (the current player is the Bond): 0 = not a KO slap (stock hit),
 * 1 = handled (knocked out now, or already out): skip the stock hit, 2 = a slap that counts but does not knock out: stock reaction, no damage */
int portHeistSlap(ChrRecord *c)
{
    HeistKo *e;

    if (sSlapsToKo <= 0 || !portModeActive() || !koEligible(c)) return 0;
    e = koGet(c->chrnum);
    if (e == NULL) return 0;
    if (e->down) return 1;
    coverAct((int) get_cur_playernum());   /* slapping someone in view blows the cover */
    if (e->slaps < 255) e->slaps++;
    if ((int) e->slaps < sSlapsToKo) return 2;
    koDown(c, e, (int) get_cur_playernum());
    return 1;
}

/* the chr runs no AI list this tick (chraction.c): knocked out, or a guard walking over to wake one */
int portHeistChrNoAi(ChrRecord *c)
{
    extern int portArFreezeAi(void);   /* port_arhud.c: `diag freeze` (solo test tool) */
    int i;

    if (portArFreezeAi()) return 1;
    if (sKoBusy <= 0) return 0;
    for (i = 0; i < sKoN; i++)
    {
        const HeistKo *e = &sKo[i];

        if (e->used && ((e->down && e->chrnum == c->chrnum) || (e->wstate && e->waker == c->chrnum))) return 1;
    }
    return 0;
}

/* port_arenamode.c portArenaDirect: a waker keeps the stock route to the body (the arena route re-plots to Bond) */
int portHeistChrWaking(const ChrRecord *c)
{
    int i;

    extern int portHeistChrOnGo(const ChrRecord *c);

    if (c != NULL && portHeistChrOnGo(c)) return 1;   /* a scripted walk (spawn.go) keeps the stock route to its pad too */
    if (sKoBusy <= 0 || c == NULL) return 0;
    for (i = 0; i < sKoN; i++)
    {
        if (sKo[i].used && sKo[i].wstate && sKo[i].waker == c->chrnum) return 1;
    }
    return 0;
}

static void koWakerRelease(HeistKo *e, int forget)
{
    e->wstate = 0;
    e->waker = -1;
    if (forget) bodyForget(e->chrnum);   /* the waker is gone: the next guard to see it finds it again */
}

static void koWake(ChrRecord *b, HeistKo *e, ChrRecord *g)
{
    HeistCiv *v = civFind(b->chrnum);

    e->down = 0;
    e->slaps = 0;
    koWakerRelease(e, 1);
    b->chrflags &= ~e->addedFlags;
    e->addedFlags = 0;
    chrlvMergeKneelToStand(b, 30.0f);   /* blends up from the lying pose */
    sKoWoken++;
    if (v != NULL)
    {
        v->state = 2;   /* a woken civilian cowers */
        v->cowerT = sCivCowerSec;
        v->aimT = v->threatT = 0.0f;
        memset(v->aimP, 0, sizeof(v->aimP));
        memset(v->threatP, 0, sizeof(v->threatP));
    }
    else if (g != NULL)
    {
        b->chrseedie = (s16) g->chrnum;   /* on alert: the stock "saw someone die" reaction of its own list */
    }
    if (g != NULL) g->chrseedie = (s16) b->chrnum;
    portEventEmit(PORT_EV_CHR_WOKEN, b->chrnum, g != NULL ? g->chrnum : -1, 0, 0);
}

/* bodyScan: guard `g` was the first to find knocked-out `b`: with mode.guards_wake on, it walks over to wake it */
static void koFoundBy(ChrRecord *b, ChrRecord *g)
{
    HeistKo *e = koFind(b->chrnum);
    int i;

    if (!sGuardsWake || e == NULL || !e->down || e->wstate) return;
    if (g->actiontype == ACT_ATTACK || g->actiontype == ACT_ATTACKWALK || g->actiontype == ACT_ATTACKROLL || !koEligible(g)) return;
    for (i = 0; i < sKoN; i++)
    {
        if (sKo[i].used && sKo[i].wstate && sKo[i].waker == g->chrnum) return;   /* already on its way to another */
    }
    e->waker = g->chrnum;
    e->wstate = 1;
    e->wT = 0.0f;
    e->wPlot = 0;
    e->wArghs = g->numarghs;
    g->chrseedie = CHR_FREE;   /* it reacts by coming over (its list runs again once the other is up) */
    sKoBusy++;
}

static void koWakerTick(ChrRecord *b, HeistKo *e, f32 dt)
{
    ChrRecord *g = chrFindByLiteralId(e->waker);
    f32 dx, dz;

    if (g == NULL || g->prop == NULL || g->model == NULL || (g->hidden & CHRHIDDEN_REMOVE) || chrIsDead(g) || koIsDown(g->chrnum))
    {
        koWakerRelease(e, 1);
        return;
    }
    if (g->numarghs != e->wArghs)
    {   /* shot at on the way: back to its own list, alerted */
        koWakerRelease(e, 0);
        g->chrseedie = (s16) b->chrnum;
        return;
    }
    e->wT += dt;
    dx = b->prop->pos.x - g->prop->pos.x;
    dz = b->prop->pos.z - g->prop->pos.z;
    if (e->wstate == 1)
    {
        if (dx * dx + dz * dz <= WAKE_REACH * WAKE_REACH)
        {
            printf("ge: heist: chr %d reached knocked-out chr %d (%.0f units) after %.1f s\n", (int) g->chrnum, (int) b->chrnum,
                   sqrtf(dx * dx + dz * dz), e->wT);
            chrlvKneelingAnimationRelated7F023E48(g);   /* stop */
            portLuaGameChrAnim(g->chrnum, "look", 1);
            e->wstate = 2;
            e->wT = 0.0f;
            return;
        }
        if (e->wT > WAKE_GIVEUP)
        {
            koWakerRelease(e, 0);
            g->chrseedie = (s16) b->chrnum;
            return;
        }
        if (--e->wPlot <= 0)
        {
            if (g->actiontype != ACT_GOPOS && g->actiontype != ACT_RUNPOS) portChrGoToProp(g, b->prop, SPEED_RUN);
            e->wPlot = 30;
        }
        return;
    }
    if (e->wT >= WAKE_LOOK) koWake(b, e, g);
}

static void koTick(f32 dt)
{
    int i, busy = 0;

    for (i = 0; i < sKoN; i++)
    {
        HeistKo *e = &sKo[i];
        ChrRecord *c;

        if (!e->used) continue;
        c = chrFindByLiteralId(e->chrnum);
        if (c == NULL || c->prop == NULL || c->model == NULL || (c->hidden & CHRHIDDEN_REMOVE))
        {
            memset(e, 0, sizeof(*e));   /* removed */
            e->waker = -1;
            continue;
        }
        if (!e->down) continue;
        if (chrIsDead(c))
        {   /* shot while out: an ordinary death from here (counted by portHeistChrDied like any other) */
            e->down = 0;
            koWakerRelease(e, 0);
            continue;
        }
        if (c->actiontype != ACT_ANIM || (s32) (intptr_t) c->model->anim != animation_table_ptrs1[KO_ANIM]) koPose(c, 0);   /* a hit reaction got it up */
        c->chrflags |= KO_FLAGS;
        if (e->wstate) koWakerTick(c, e, dt);
    }
    for (i = 0; i < sKoN; i++)
    {
        if (sKo[i].used && (sKo[i].down || sKo[i].wstate)) busy++;
    }
    sKoBusy = busy;
}

int portHeistKnockOut(int chrnum, int player)
{
    ChrRecord *c = chrFindByLiteralId(chrnum);
    HeistKo *e;

    if (!portModeActive() || !koEligible(c)) return 0;
    e = koGet(chrnum);
    if (e == NULL) return 0;
    if (!e->down) koDown(c, e, player);
    return 1;
}

int portHeistKoWake(int chrnum)
{
    ChrRecord *c = chrFindByLiteralId(chrnum);
    HeistKo *e = koFind(chrnum);

    if (!portModeActive() || c == NULL || e == NULL || !e->down || chrIsDead(c)) return 0;
    koWake(c, e, NULL);
    return 1;
}

int portHeistKoIs(int chrnum) { return portModeActive() && koIsDown(chrnum); }

int portHeistKoSlaps(int chrnum)
{
    HeistKo *e = koFind(chrnum);

    return e != NULL ? (int) e->slaps : 0;
}

void portHeistKoCount(int *down, int *total, int *woken)
{
    int i, n = 0;

    for (i = 0; i < sKoN; i++)
    {
        if (sKo[i].used && sKo[i].down) n++;
    }
    *down = n;
    *total = sKoTotal;
    *woken = sKoWoken;
}

/* -1 = no guard is on its way */
int portHeistKoWaker(int chrnum)
{
    HeistKo *e = koFind(chrnum);

    return (e != NULL && e->down && e->wstate) ? (int) e->waker : -1;
}

void portHeistSetSlapsToKo(int n) { sSlapsToKo = n < 0 ? 0 : (n > 255 ? 255 : n); }
int portHeistSlapsToKo(void) { return sSlapsToKo; }
void portHeistSetGuardsWake(int on) { sGuardsWake = on ? 1 : 0; }

/* Bond `player` presses use: the nearest knocked-out chr within reach that has not been searched drops what it carries.
 * Returns its chrnum, -1 none. */
int portHeistKoSearch(int player)
{
    struct player *pl;
    HeistKo *best = NULL;
    ChrRecord *bc = NULL;
    f32 bestd = SEARCH_RANGE * SEARCH_RANGE;
    int i;

    if (!portModeActive() || sKoBusy <= 0 || player < 0 || player >= (int) portBondCount() || !portBondInPlay(player)
        || (pl = g_playerPointers[player]) == NULL || pl->prop == NULL) return -1;
    for (i = 0; i < sKoN; i++)
    {
        HeistKo *e = &sKo[i];
        ChrRecord *c;
        f32 dx, dy, dz, d;

        if (!e->used || !e->down || e->searched) continue;
        c = chrFindByLiteralId(e->chrnum);
        if (c == NULL || c->prop == NULL) continue;
        dx = c->prop->pos.x - pl->prop->pos.x;
        dy = c->prop->pos.y - pl->prop->pos.y;
        dz = c->prop->pos.z - pl->prop->pos.z;
        d = dx * dx + dz * dz;
        if (d <= bestd && dy < 200.0f && dy > -200.0f) { bestd = d; best = e; bc = c; }
    }
    if (best == NULL) return -1;
    best->searched = 1;
    coverAct(player);   /* going through a body's pockets in view blows the cover */
    chrDropItems(bc);
    portEventEmit(PORT_EV_CHR_SEARCHED, best->chrnum, player, 0, 0);
    return best->chrnum;
}

/* bondview2.c: the current Bond's use tap; 1 = it searched a knocked-out chr (the tap is consumed) */
s32 portHeistKoUse(void)
{
    return portHeistKoSearch((int) get_cur_playernum()) >= 0 ? 1 : 0;
}

/* test hook: Bond `player`'s slappers hit `chrnum` through the stock hit path (handles_shot_actors, ITEM_FIST, chest) */
int portHeistTestSlap(int player, int chrnum)
{
    ChrRecord *c = chrFindByLiteralId(chrnum);
    struct player *pl;
    coord3d dir;
    s32 saved;
    int r;

    if (!portModeActive() || c == NULL || c->prop == NULL || c->model == NULL || player < 0 || player >= (int) portBondCount()
        || !portBondInPlay(player) || (pl = g_playerPointers[player]) == NULL || pl->prop == NULL) return 0;
    dir.x = c->prop->pos.x - pl->prop->pos.x;
    dir.y = 0.0f;
    dir.z = c->prop->pos.z - pl->prop->pos.z;
    saved = get_cur_playernum();
    set_cur_player(player);
    r = handles_shot_actors(c, HIT_CHEST, &dir, ITEM_FIST, TRUE) ? 1 : 0;
    set_cur_player(saved);
    return r;
}

/* ---- loot (job 6): gold bars and money, spawn.item(pad, "goldbar" | "money") ----
 * A stock standard prop (PROPDEF_PROP) with the beta pickup model (PROP_CHRGOLDBAR / PROP_CHRMONEY), placed by domakedefaultobj on the
 * pad (the pad is moved by the offset for the call and put back). Not a stock pickup: every sim tick a living Bond within LOOT_REACH of
 * it collects it (this file), it is removed and item_pickup(PROPDEF_PROP, ITEM_GOLDBAR | ITEM_MONEY, 1, player) fires. Its own pool, so
 * the arena's 12 world pickups and the 30 weapon slots are untouched. A record is reused only well after its prop was freed (the arena's
 * same-tick reuse crash, port_arenamode.c). */
#define LOOT_N       160
#define LOOT_REACH   90.0f
#define LOOT_REUSE   60        /* ticks after a removal before the record can be reused */

static ObjectRecord sLootRec[LOOT_N];
static struct { u8 used, kind; s32 serial, freedAt; } sLoot[LOOT_N];
static s32 sLootSerial, sLootTaken, sLootNext;

/* a pad's position, stan tile and facing, with no clear-circle test (portPadStan refuses a pad a chr stands on) */
int portHeistPadRaw(int pad, coord3d *pos, StandTile **stan, float *angle)
{
    PadRecord *p;

    if (pad < 0 || pad >= (int) portPadCount() || g_CurrentSetup.pads == NULL) return 0;
    p = &g_CurrentSetup.pads[pad];
    if (p->stan == NULL) return 0;
    *pos = p->pos;
    *stan = p->stan;
    if (angle != NULL) *angle = atan2f(p->look.f[0], p->look.f[2]);
    return 1;
}

/* (pos, stan) moved by (dx, dz) along the floor: the stan walk from the start tile must reach the point (no wall, ledge or gap in
 * between) and the point must be on the same floor height (within 60 units). 1 = moved, 0 = refused (pos / stan unchanged). */
int portHeistOffsetStan(coord3d *pos, StandTile **stan, float dx, float dz)
{
    StandTile *st = *stan;
    f32 y0, y1;

    if (st == NULL) return 0;
    if (!walkTilesBetweenPoints_NoCallback(&st, pos->x, pos->z, pos->x + dx, pos->z + dz) || st == NULL) return 0;
    y0 = stanGetPositionYValue(*stan, pos->x, pos->z);
    y1 = stanGetPositionYValue(st, pos->x + dx, pos->z + dz);
    if (y1 - y0 > 60.0f || y0 - y1 > 60.0f) return 0;
    pos->x += dx;
    pos->z += dz;
    pos->y += y1 - y0;
    *stan = st;
    return 1;
}

static int lootSlotAlive(int i)
{
    ObjectRecord *o = &sLootRec[i];

    return sLoot[i].used && o->prop != NULL && !(o->runtime_bitflags & RUNTIMEBITFLAG_REMOVE);
}

static int lootFromHandle(int handle)
{
    int i;

    if (handle < PORT_HEIST_LOOT_HANDLE) return -1;
    i = (handle - PORT_HEIST_LOOT_HANDLE) & 0xFF;
    if (i >= LOOT_N || sLoot[i].serial != ((handle - PORT_HEIST_LOOT_HANDLE) >> 8) || !lootSlotAlive(i)) return -1;
    return i;
}

int portHeistLootSpawn(int pad, int kind, float dx, float dz)
{
    PadRecord *p, saved;
    ObjectRecord *o;
    int i, k, slot = -1;

    if (!portModeActive()) return 0;
    if (pad < 0 || pad >= (int) portPadCount() || g_CurrentSetup.pads == NULL || g_CurrentSetup.pads[pad].stan == NULL) return -2;
    for (k = 0; k < LOOT_N; k++)
    {
        i = (sLootNext + k) % LOOT_N;
        if (sLoot[i].used && !lootSlotAlive(i)) { sLoot[i].used = 0; sLoot[i].freedAt = sNow; }
        if (!sLoot[i].used && (sLoot[i].serial == 0 || sNow - sLoot[i].freedAt >= LOOT_REUSE)) { slot = i; break; }
    }
    if (slot < 0) return 0;
    sLootNext = (slot + 1) % LOOT_N;
    p = &g_CurrentSetup.pads[pad];
    saved = *p;
    if (dx != 0.0f || dz != 0.0f)
    {
        coord3d at = p->pos;
        StandTile *st = p->stan;

        if (!portHeistOffsetStan(&at, &st, dx, dz)) return 0;   /* a wall or a ledge in the way */
        p->pos.x = at.x;
        p->pos.z = at.z;
        p->stan = st;
    }
    o = &sLootRec[slot];
    memset(o, 0, sizeof(*o));
    o->extrascale = 256;
    o->type = PROPDEF_PROP;
    o->obj = kind ? PROP_CHRMONEY : PROP_CHRGOLDBAR;
    o->pad = pad;
    o->flags = 1 | PROPFLAG_INVINCIBLE;      /* fall to the floor; shots do not destroy it */
    *(s32 *) &o->damage = 1000 << 16;        /* the raw setup form: domakedefaultobj converts it */
    domakedefaultobj(0, o, 0);
    *p = saved;
    o->state &= ~PROPSTATE_RESPAWN;
    if (o->prop == NULL) return 0;
    sLootSerial = (sLootSerial % 0xFFFF) + 1;
    sLoot[slot].used = 1;
    sLoot[slot].kind = (u8) (kind ? 1 : 0);
    sLoot[slot].serial = sLootSerial;
    return PORT_HEIST_LOOT_HANDLE + ((sLootSerial << 8) | slot);
}

int portHeistLootAlive(int handle) { return portModeActive() && lootFromHandle(handle) >= 0; }

int portHeistLootRemove(int handle)
{
    int i = portModeActive() ? lootFromHandle(handle) : -1;

    if (i < 0) return 0;
    sLootRec[i].runtime_bitflags |= RUNTIMEBITFLAG_REMOVE;   /* freed by the object tick, as a pickup is */
    sLoot[i].used = 0;
    sLoot[i].freedAt = sNow;
    return 1;
}

int portHeistLootPos(int handle, float *x, float *z)
{
    int i = portModeActive() ? lootFromHandle(handle) : -1;

    if (i < 0) return 0;
    *x = sLootRec[i].prop->pos.x;
    *z = sLootRec[i].prop->pos.z;
    return 1;
}

static void lootTick(void)
{
    int i, p, n = (int) portBondCount();

    for (i = 0; i < LOOT_N; i++)
    {
        PropRecord *lp;

        if (!sLoot[i].used) continue;
        if (!lootSlotAlive(i)) { sLoot[i].used = 0; sLoot[i].freedAt = sNow; continue; }
        lp = sLootRec[i].prop;
        for (p = 0; p < n && p < 4; p++)
        {
            struct player *pl = g_playerPointers[p];
            f32 dx, dy, dz;

            if (!portBondInPlay(p) || pl == NULL || pl->prop == NULL || pl->bonddead) continue;
            dx = pl->prop->pos.x - lp->pos.x;
            dy = pl->prop->pos.y - lp->pos.y;   /* Bond's prop is at eye height */
            dz = pl->prop->pos.z - lp->pos.z;
            if (dx * dx + dz * dz > LOOT_REACH * LOOT_REACH || dy < -100.0f || dy > 320.0f) continue;
            sLootRec[i].runtime_bitflags |= RUNTIMEBITFLAG_REMOVE;
            sLoot[i].used = 0;
            sLoot[i].freedAt = sNow;
            sLootTaken++;
            sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, PICKUP_AMMO_SFX, 0);
            portEventEmit(PORT_EV_ITEM_PICKUP, PROPDEF_PROP, sLoot[i].kind ? ITEM_MONEY : ITEM_GOLDBAR, 1, p);
            break;
        }
    }
}

/* ---- weapon in hand, difficulty (job 6) ---- */

extern int bondinvHasInvItem(ITEM_IDS item);
extern int bondinvAddInvItem(ITEM_IDS item);

/* bond.equip: Bond `player` draws `item` (the stock weapon switch, as the start-of-level loadout does); gives it first if he lacks it */
int portHeistBondEquip(int player, int item)
{
    s32 saved;

    if (!portModeActive() || player < 0 || player >= (int) portBondCount() || !portBondInPlay(player) || item < 0 || item >= ITEM_BOMBCASE + 64) return 0;
    saved = get_cur_playernum();
    set_cur_player(player);
    if (item > ITEM_FIST && !bondinvHasInvItem((ITEM_IDS) item)) bondinvAddInvItem((ITEM_IDS) item);
    currentPlayerEquipWeaponWrapper(GUNRIGHT, item);
    currentPlayerEquipWeaponWrapper(GUNLEFT, ITEM_UNARMED);
    set_cur_player(saved);
    return 1;
}

int portHeistBondWeapon(int player)
{
    s32 saved;
    int w;

    if (!portModeActive() || player < 0 || player >= (int) portBondCount() || g_playerPointers[player] == NULL) return -1;
    saved = get_cur_playernum();
    set_cur_player(player);
    w = (int) getCurrentPlayerWeaponId(GUNRIGHT);
    set_cur_player(saved);
    return w;
}

int portHeistDifficulty(void) { return (int) lvlGetSelectedDifficulty(); }

void portHeistGuardsReset(void)
{
    {
        const char *m = portModeChoice();

        sSlapsToKo = (m != NULL && strncmp(m, "heist", 5) == 0) ? 2 : 0;   /* heist modes KO with 2 slaps; other modes keep stock slaps */
    }
    sGuardsWake = 1;
    sKoN = sKoBusy = sKoTotal = sKoWoken = 0;
    memset(sKo, 0, sizeof(sKo));
    sPatrolN = 0;
    sFoundN = 0;
    sBodyCheck = 1;
    sAcc = sNow = 0;
    sCivN = sCivKilled = 0;
    memset(sCiv, 0, sizeof(sCiv));
    sCivRange = 800.0f; sCivAimSec = 0.5f; sCivThreatSec = 2.0f; sCivCowerSec = 4.0f;
    memset(sLootRec, 0, sizeof(sLootRec));
    memset(sLoot, 0, sizeof(sLoot));
    memset(sXWeapon, 0, sizeof(sXWeapon));
    sLootSerial = sLootTaken = sLootNext = 0;
}

/* called every sim tick from portBlocksTick while a mode runs */
void portHeistAiTick(void)
{
    f32 dt = g_ClockTimer / 60.0f;
    int i;

    sNow += g_ClockTimer;
    sAcc += g_ClockTimer;
    {
        extern void portHeistCoverTick(void);

        portHeistCoverTick();   /* the disguise's suspicion scan and the scripted walks (below) */
    }
    if (sKoN > 0) koTick(dt);
    if (sLootSerial > 0) lootTick();
    for (i = 0; i < sCivN; i++)
    {
        if (sCiv[i].used) civTick(&sCiv[i], dt);
    }
    for (i = 0; i < sPatrolN; i++)
    {
        ChrRecord *c;

        if (sPatrol[i].chrnum < 0 || sNow < sPatrol[i].at) continue;
        c = chrFindByLiteralId(sPatrol[i].chrnum);
        if (c != NULL && c->model != NULL && !chrIsDead(c))
        {
            extern PathRecord *pathFindById(s32 ID);
            PathRecord *path = pathFindById(sPatrol[i].path);

            if (path != NULL) if_actor_able_set_on_path(c, (s32) (intptr_t) path);
            else printf("ge: heist: spawn.chr patrol=%d: no such patrol path on this stage\n", sPatrol[i].path);
        }
        sPatrol[i].chrnum = -1;
    }
    if (sAcc >= 8)
    {
        sAcc = 0;
        if (sBodyCheck) bodyScan();
    }
}

/* ---- cover: the banker disguise (designs/bank-heist.md section 16, AC's answers 2026-10-25) ----
 * The crew starts as bank staff. A guard the mode registers (cover.guard) does not see a Bond whose cover holds with him: the stock sight
 * test every guard list uses (chrCheckTargetInSight, IFISeeBond) asks portHeistCoverHides first and gets "not seen" (before its random
 * roll). Instead this file keeps a suspicion meter per guard and per Bond (0..1), filled while the guard could see that Bond (in range,
 * inside his cone, the stock guard-to-Bond stan line as chrCanSeeBond draws it) by what the Bond is doing: a gun or a knife in hand (fast),
 * loot carried (instant), an act (a slap, a robbery, a search, a shot fired: instant for act seconds), the room he stands in (restricted:
 * fast; staff-only: slow; public: nothing), running near the guard, or lingering right next to him. It drains while nothing is wrong.
 * Full: that guard sees through him ("Hey!", cover_blown(guard, p, why)); only that guard: the hook stops hiding that Bond from him, his
 * own list attacks. After `radio` seconds, if he is still up (not dead, not knocked out), he reports it (cover_report(guard, p)); the mode
 * raises the alarm. A guard who hears a Bond fire, or is hit, sees through the shooter at once. cover.alert (the alarm, a body found) ends
 * every disguise: the hook hides nobody. Cameras (portHeistCctvAllBonds) only time a Bond who looks wrong to a camera (a gun, loot, an
 * act, a restricted room, or the alert). Per Bond in co-op. All of it reads sim state only (positions, facings, the synchronized input's
 * results, never the local player) and is in the state hash. The power (power.set) and the scripted walks (spawn.go) live here too. */
#define COVER_MAX   384
#define COVER_SCAN  4          /* ticks between scans (15 Hz at 60) */
#define COVER_HASH  1024       /* chrnum -> entry index (open addressing) */
#define GO_MAX      32

enum { CW_NONE, CW_ARMED, CW_LOOT, CW_ACT, CW_RESTRICTED, CW_STAFF, CW_RUNNING, CW_CLOSE, CW_HEARD, CW_HURT, CW_SCRIPT, CW_N };
static const char *const sCoverWhy[CW_N] = { "none", "armed", "loot", "act", "restricted", "staff", "running", "close", "heard", "hurt", "script" };

typedef struct
{
    s32 chrnum, heyAt[4], hear;
    f32 susp[4], linger[4];
    ChrRecord *slot;           /* a cache of where chrFindByLiteralId found it (not hashed: re-checked by chrnum) */
    u8 role, witness, reported, sees, why[4];
    s8 arghs;
} HeistCover;

static HeistCover sCov[COVER_MAX];
static s16 sCovIdx[COVER_HASH];
static int sCovN, sCoverOn, sCoverAlert, sPowerOut;
static u8 sRoomClass[256];      /* 0 public, 1 staff only, 2 restricted (cover.room) */
static u8 sCoverLoot[4];
static s32 sCoverActUntil[4], sCoverShooter, sCoverShotAt, sCovAcc;
static f32 sBondPX[4], sBondPZ[4], sBondSpeed[4];
static u8 sBondPOk[4];
static struct
{   /* seconds to fill the meter from empty (armed, restricted, staff, run, close), units, degrees; cover.tune */
    f32 range, cone, armed, restricted, staff, run, runSpeed, runRange, close, closeTime, linger, decay, radio, act;
} sCt;
static struct { s32 chrnum, pad, until; u8 used; } sGo[GO_MAX];
static int sGoN;

static int coverActive(void) { return sCoverOn && portModeActive(); }

static void coverTuneDefaults(void)
{
    sCt.range = 1800.0f;      /* a guard notices staff conduct this far off (the hall is 2000 x 1280) */
    sCt.cone = 75.0f;         /* either side of where he faces (the stock sight cone is 110: walking behind him is the point) */
    sCt.armed = 0.4f;         /* a gun in hand: a glance */
    sCt.restricted = 1.0f;    /* seen in the security wing or on the vault level */
    sCt.staff = 5.0f;         /* staff-only rooms: a few seconds, then "Hey!" */
    sCt.run = 4.0f;           /* running close past a guard */
    sCt.runSpeed = 450.0f;    /* units/s counted as running (Bond at full stick: about 500; tests/heist/heist_cover.lua logs it) */
    sCt.runRange = 400.0f;    /* (900 filled a hall walk at full stick to 0.81: full stick is how keyboard players always move) */
    sCt.close = 150.0f;       /* "lingering": closer than this ... */
    sCt.linger = 2.0f;        /* ... for longer than this ... */
    sCt.closeTime = 4.0f;     /* ... then it fills in this long */
    sCt.decay = 12.0f;        /* a full meter drains in this long while nothing is wrong */
    sCt.radio = 5.0f;         /* a witness radios it in after this long (knock him out or kill him first) */
    sCt.act = 1.5f;           /* a slap / a robbery / a search / a shot counts as seen for this long */
}

static void coverReset(void)
{
    memset(sCov, 0, sizeof(sCov));
    memset(sCovIdx, 0, sizeof(sCovIdx));
    sCovN = sCoverOn = sCoverAlert = sPowerOut = 0;
    memset(sRoomClass, 0, sizeof(sRoomClass));
    memset(sCoverLoot, 0, sizeof(sCoverLoot));
    memset(sCoverActUntil, 0, sizeof(sCoverActUntil));
    sCoverShooter = -1;
    sCoverShotAt = sCovAcc = 0;
    memset(sBondPX, 0, sizeof(sBondPX));
    memset(sBondPZ, 0, sizeof(sBondPZ));
    memset(sBondSpeed, 0, sizeof(sBondSpeed));
    memset(sBondPOk, 0, sizeof(sBondPOk));
    memset(sGo, 0, sizeof(sGo));
    sGoN = 0;
    doorRulesReset();
    coverTuneDefaults();
}

static HeistCover *covFind(s32 chrnum)
{
    int h = (int) ((u32) chrnum & (COVER_HASH - 1)), k;

    for (k = 0; k < COVER_HASH; k++)
    {
        int s = sCovIdx[(h + k) & (COVER_HASH - 1)];

        if (s == 0) return NULL;
        if (sCov[s - 1].chrnum == chrnum) return &sCov[s - 1];
    }
    return NULL;
}

static ChrRecord *covChr(HeistCover *e)
{
    ChrRecord *c = e->slot;

    if (c == NULL || c->chrnum != e->chrnum || c->model == NULL)
    {
        c = chrFindByLiteralId(e->chrnum);
        e->slot = c;
    }
    return c;
}

/* cover.guard(chr [, role]): this guard judges the disguise (role 1 = security staff: the mode's camera link needs one of them up) */
int portHeistCoverGuard(int chrnum, int role)
{
    HeistCover *e = covFind(chrnum);
    ChrRecord *c = chrFindByLiteralId(chrnum);
    int h, k;

    if (!portModeActive() || c == NULL || c->prop == NULL) return 0;
    if (e == NULL)
    {
        if (sCovN >= COVER_MAX) return 0;
        e = &sCov[sCovN];
        memset(e, 0, sizeof(*e));
        e->chrnum = chrnum;
        e->arghs = c->numarghs;
        e->hear = c->lastheartarget60;
        h = (int) ((u32) chrnum & (COVER_HASH - 1));
        for (k = 0; k < COVER_HASH && sCovIdx[(h + k) & (COVER_HASH - 1)] != 0; k++) ;
        if (k == COVER_HASH) return 0;
        sCovIdx[(h + k) & (COVER_HASH - 1)] = (s16) (sCovN + 1);
        sCovN++;
    }
    e->role = (u8) role;
    e->slot = c;
    return 1;
}

void portHeistCoverSetOn(int on) { sCoverOn = on ? 1 : 0; }
int portHeistCoverIsOn(void) { return sCoverOn; }
void portHeistCoverSetAlert(int on) { sCoverAlert = on ? 1 : 0; }
int portHeistCoverAlert(void) { return sCoverAlert; }
void portHeistCoverRoom(int room, int cls) { if (room >= 0 && room < 256) sRoomClass[room] = (u8) (cls < 0 ? 0 : (cls > 2 ? 2 : cls)); }
int portHeistCoverRoomClass(int room) { return (room >= 0 && room < 256) ? (int) sRoomClass[room] : 0; }
void portHeistCoverCarrying(int p, int on) { if (p >= 0 && p < 4) sCoverLoot[p] = on ? 1 : 0; }
const char *portHeistCoverWhyName(int why) { return (why >= 0 && why < CW_N) ? sCoverWhy[why] : "?"; }

/* cover.tune{...}: < 0 keeps a value */
void portHeistCoverTune(const float *v, int n)
{
    f32 *dst[] = { &sCt.range, &sCt.cone, &sCt.armed, &sCt.restricted, &sCt.staff, &sCt.run, &sCt.runSpeed, &sCt.runRange, &sCt.close,
                   &sCt.closeTime, &sCt.linger, &sCt.decay, &sCt.radio, &sCt.act };
    int i;

    for (i = 0; i < n && i < (int) (sizeof(dst) / sizeof(dst[0])); i++)
    {
        if (v[i] >= 0.0f) *dst[i] = v[i] > 0.01f ? v[i] : 0.01f;
    }
}

static void coverAct(int p)
{
    if (coverActive() && p >= 0 && p < 4) sCoverActUntil[p] = sNow + (s32) (sCt.act * 60.0f);
}

/* a shot by Bond p: an act for those who see it, and whoever hears it (the stock hearing check right after) sees through him */
static void coverShot(int p)
{
    if (!coverActive() || p < 0 || p > 3) return;
    sCoverShooter = p;
    sCoverShotAt = g_GlobalTimer;
    coverAct(p);
}

/* bondview2.c: the current Bond fires (before the stock hearing check). Slaps are counted in portHeistSlap, not here. */
void portHeistCoverFired(void)
{
    int p = (int) get_cur_playernum();

    if (!coverActive() || p < 0 || p > 3 || !bondArmed(p)) return;
    coverShot(p);
}

/* cover.test_act(p): test hook, Bond p did something that blows the cover in view (a slap, a robbery, a shot) */
int portHeistCoverTestAct(int p)
{
    if (!coverActive() || p < 0 || p >= (int) portBondCount()) return 0;
    coverAct(p);
    return 1;
}

static void coverHey(HeistCover *e, ChrRecord *g, int p, int why)
{
    struct player *pl;

    if (p < 0 || p > 3 || (e->witness & (1 << p))) return;
    e->witness |= (u8) (1 << p);
    e->susp[p] = 1.0f;
    e->heyAt[p] = sNow;
    e->why[p] = (u8) why;
    pl = g_playerPointers[p];
    if (g != NULL && pl != NULL && pl->prop != NULL)
    {   /* he has just seen Bond p: the stock "saw the target recently" (chrSawTargetRecently), so his next sight test goes straight to
         * the stan line instead of the random roll */
        g->lastseetarget60 = g_GlobalTimer;
        g->lastknowntargetpos = pl->prop->pos;
        g->targetTile = pl->prop->stan;
    }
    portEventEmit(PORT_EV_COVER_BLOWN, e->chrnum, p, why, 0);
}

/* cover.blow(chr, p): a script makes that guard see through Bond p now */
int portHeistCoverBlow(int chrnum, int p)
{
    HeistCover *e = covFind(chrnum);

    if (!coverActive() || e == NULL || p < 0 || p >= (int) portBondCount()) return 0;
    coverHey(e, covChr(e), p, CW_SCRIPT);
    return 1;
}

/* guard g could see Bond prop bp: within range and 350 units of height, inside his cone (anything within 100 units counts from any side),
 * and the stock guard-to-Bond stan line (chrCanSeeBond's flags and heights) ending on Bond's tile */
static int coverSees(ChrRecord *g, PropRecord *bp, f32 *dist)
{
    PropRecord *gp = g->prop;
    f32 dx = bp->pos.x - gp->pos.x, dy = bp->pos.y - gp->pos.y, dz = bp->pos.z - gp->pos.z, d2 = dx * dx + dz * dz, a, h;
    StandTile *st;
    int ok;

    *dist = sqrtf(d2);
    if (dy > 350.0f || dy < -350.0f || d2 > sCt.range * sCt.range) return 0;
    if (d2 > 100.0f * 100.0f)
    {
        a = atan2f(dx, dz) - getsubroty(g->model);
        while (a > M_PI_F) a -= M_TAU_F;
        while (a < -M_PI_F) a += M_TAU_F;
        if (a > DegToRad(sCt.cone) || a < -DegToRad(sCt.cone)) return 0;
    }
    if (gp->stan == NULL || bp->stan == NULL) return 0;
    st = gp->stan;
    h = g->chrheight - 20.0f;
    chrSetMoving(g, FALSE);
    bondviewUpdateGuardTankFlagsRelated(bp, 0);
    ok = stanTestLineUnobstructed(&st, gp->pos.x, gp->pos.z, bp->pos.x, bp->pos.z,
                                  CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_CHRS | CDTYPE_PATHBLOCKER | CDTYPE_AIOPAQUE, h, h, 0.0f, 1.0f) != 0
         && st == bp->stan;
    bondviewUpdateGuardTankFlagsRelated(bp, 1);
    chrSetMoving(g, TRUE);
    return ok;
}

/* the Bond nearest guard g (an armed one first): who shot him */
static int coverNearest(ChrRecord *g, const int *armed, const int *ok, int n)
{
    int p, best = -1, pass;
    f32 bd = 1e18f;

    for (pass = 0; pass < 2 && best < 0; pass++)
    {
        for (p = 0; p < n; p++)
        {
            struct player *pl = g_playerPointers[p];
            f32 dx, dz, d;

            if (!ok[p] || (pass == 0 && !armed[p])) continue;
            dx = pl->prop->pos.x - g->prop->pos.x;
            dz = pl->prop->pos.z - g->prop->pos.z;
            d = dx * dx + dz * dz;
            if (d < bd) { bd = d; best = p; }
        }
    }
    return best;
}

static int coverGuardUp(ChrRecord *g)
{
    return g != NULL && g->prop != NULL && g->model != NULL && !(g->hidden & CHRHIDDEN_REMOVE) && !chrIsDead(g) && !koIsDown(g->chrnum)
        && g->actiontype != ACT_DIE && g->actiontype != ACT_DEAD;
}

static void coverScan(f32 dt)
{
    int n = (int) portBondCount(), p, i;
    int armed[4] = { 0, 0, 0, 0 }, cls[4] = { 0, 0, 0, 0 }, act[4] = { 0, 0, 0, 0 }, ok[4] = { 0, 0, 0, 0 };

    if (n > 4) n = 4;
    for (p = 0; p < n; p++)
    {
        struct player *pl = g_playerPointers[p];

        if (!portBondInPlay(p) || pl == NULL || pl->prop == NULL || pl->prop->stan == NULL || pl->bonddead)
        {
            sBondPOk[p] = 0;
            sBondSpeed[p] = 0.0f;
            continue;
        }
        ok[p] = 1;
        armed[p] = bondArmed(p);
        cls[p] = portGadgetsDisguiseClass(p, sRoomClass[pl->prop->stan->room & 0xFF]);   /* (a uniform: port_gadgets.c) */
        act[p] = sNow < sCoverActUntil[p];
        if (sBondPOk[p] && dt > 0.0f)
        {
            f32 dx = pl->prop->pos.x - sBondPX[p], dz = pl->prop->pos.z - sBondPZ[p];

            sBondSpeed[p] = sqrtf(dx * dx + dz * dz) / dt;
        }
        else
        {
            sBondSpeed[p] = 0.0f;
        }
        sBondPX[p] = pl->prop->pos.x;
        sBondPZ[p] = pl->prop->pos.z;
        sBondPOk[p] = 1;
    }
    for (i = 0; i < sCovN; i++)
    {
        HeistCover *e = &sCov[i];
        ChrRecord *g = covChr(e);

        e->sees = 0;
        if (!coverGuardUp(g)) continue;
        if (g->numarghs != e->arghs)
        {   /* hit: he knows who shot him (the nearest armed Bond, else the nearest) */
            int q = coverNearest(g, armed, ok, n);

            e->arghs = g->numarghs;
            if (q >= 0) coverHey(e, g, q, CW_HURT);
        }
        if (g->lastheartarget60 != e->hear)
        {   /* heard a shot (the stock hearing check stamps it): he sees through the shooter */
            e->hear = g->lastheartarget60;
            if (e->hear > 0 && sCoverShooter >= 0 && sCoverShooter < n && ok[sCoverShooter] && g_GlobalTimer - sCoverShotAt < 30)
            {
                coverHey(e, g, sCoverShooter, CW_HEARD);
            }
        }
        for (p = 0; p < n; p++)
        {
            f32 d, rate = 0.0f;
            int why = CW_NONE, inst = 0, seen;

            if (!ok[p]) continue;
            seen = coverSees(g, g_playerPointers[p]->prop, &d);
            if (seen) e->sees |= (u8) (1 << p);
            if (e->witness & (1 << p)) continue;
            if (!seen)
            {
                e->linger[p] = 0.0f;
                e->susp[p] = e->susp[p] > dt / sCt.decay ? e->susp[p] - dt / sCt.decay : 0.0f;
                continue;
            }
            if (sCoverLoot[p]) { inst = 1; why = CW_LOOT; }
            else if (act[p]) { inst = 1; why = CW_ACT; }
            else
            {
                if (armed[p]) { rate = 1.0f / sCt.armed; why = CW_ARMED; }
                if (cls[p] == 2 && 1.0f / sCt.restricted > rate) { rate = 1.0f / sCt.restricted; why = CW_RESTRICTED; }
                if (cls[p] == 1) { rate += 1.0f / sCt.staff; if (why == CW_NONE) why = CW_STAFF; }
                if (sBondSpeed[p] > sCt.runSpeed && d < sCt.runRange) { rate += 1.0f / sCt.run; if (why == CW_NONE) why = CW_RUNNING; }
                if (d < sCt.close)
                {
                    e->linger[p] += dt;
                    if (e->linger[p] > sCt.linger) { rate += portGadgetsDisguiseCloseScale(p) / sCt.closeTime; if (why == CW_NONE) why = CW_CLOSE; }
                }
                else
                {
                    e->linger[p] = 0.0f;
                }
            }
            if (inst) e->susp[p] = 1.0f;
            else if (rate > 0.0f) e->susp[p] += rate * dt;
            else e->susp[p] = e->susp[p] > dt / sCt.decay ? e->susp[p] - dt / sCt.decay : 0.0f;
            if (e->susp[p] >= 1.0f) coverHey(e, g, p, why);
        }
        for (p = 0; p < 4; p++)
        {   /* the radio: a witness still up after `radio` seconds reports it */
            u8 bit = (u8) (1 << p);

            if ((e->witness & bit) && !(e->reported & bit) && sNow - e->heyAt[p] >= (s32) (sCt.radio * 60.0f))
            {
                e->reported |= bit;
                radioSfx(e->chrnum, 0);   /* the call is heard where he stands (presentation) */
                portEventEmit(PORT_EV_COVER_REPORT, e->chrnum, p, 0, 0);
            }
        }
    }
}

/* chraction.c chrCheckTargetInSight: 1 = the current Bond's cover holds with this guard (not seen) */
int portHeistCoverHides(ChrRecord *self)
{
    HeistCover *e;
    int p;

    if (!sCoverOn || sCoverAlert || sCovN == 0 || !portModeActive() || self == NULL) return 0;
    e = covFind(self->chrnum);
    if (e == NULL) return 0;
    p = (int) get_cur_playernum();
    if (p < 0 || p > 3) return 0;
    return (e->witness & (1 << p)) == 0;
}

/* a camera times Bond p (portHeistCctvAllBonds) only when he looks wrong to a camera; without the cover every Bond does (stock) */
static int coverCamSuspect(int p)
{
    struct player *pl = (p >= 0 && p < 4) ? g_playerPointers[p] : NULL;

    if (!coverActive() || sCoverAlert) return 1;
    if (p < 0 || p > 3) return 1;
    if (sCoverLoot[p] || sNow < sCoverActUntil[p]) return 1;
    if (pl != NULL && pl->prop != NULL && pl->prop->stan != NULL && portGadgetsDisguiseClass(p, sRoomClass[pl->prop->stan->room & 0xFF]) == 2) return 1;
    return bondArmed(p);
}

/* cover.state(chr): the guard's meter. Returns 0 = not a cover guard. */
int portHeistCoverState(int chrnum, float *susp, int *witness, int *reported, int *sees, int *role, int *why)
{
    HeistCover *e = covFind(chrnum);
    int p;

    if (e == NULL) return 0;
    *susp = 0.0f;
    *why = 0;
    for (p = 0; p < 4; p++)
    {
        if (e->susp[p] > *susp) *susp = e->susp[p];
        if ((e->witness & (1 << p)) && *why == 0) *why = e->why[p];
    }
    *witness = e->witness;
    *reported = e->reported;
    *sees = e->sees;
    *role = e->role;
    return 1;
}

float portHeistCoverSusp(int chrnum, int p)
{
    HeistCover *e = covFind(chrnum);

    return (e != NULL && p >= 0 && p < 4) ? e->susp[p] : -1.0f;
}

/* cover.meter(p) and the HUD's eye: Bond p's cover as the guards see it. 0 off, 1 nobody watching, 2 watched (calm), 3 suspicious,
 * 4 blown (a guard that is still up saw through him), 5 alert (the disguise is gone). *level = the highest meter of a guard still up,
 * *chr = that guard (-1). Reads sim state; the HUD calls it for the local Bond (presentation). */
int portHeistCoverMeter(int p, float *level, int *chr)
{
    int i, watched = 0, blown = 0;

    *level = 0.0f;
    *chr = -1;
    if (!coverActive() || p < 0 || p > 3) return 0;
    if (sCoverAlert) { *level = 1.0f; return 5; }
    for (i = 0; i < sCovN; i++)
    {
        HeistCover *e = &sCov[i];
        ChrRecord *g = e->slot;

        if (g == NULL || g->chrnum != e->chrnum || !coverGuardUp(g)) continue;
        if (e->witness & (1 << p)) { blown = 1; *level = 1.0f; *chr = e->chrnum; continue; }
        if (e->sees & (1 << p)) watched = 1;
        if (!blown && e->susp[p] > *level) { *level = e->susp[p]; *chr = e->chrnum; }
    }
    if (blown) return 4;
    if (*level >= 0.04f) return 3;
    return watched ? 2 : 1;
}

/* cover.seen(p): some cover guard that is up has Bond p in sight right now (the power-room work stops) */
int portHeistCoverSeen(int p)
{
    int i;

    if (!coverActive() || p < 0 || p > 3) return 0;
    for (i = 0; i < sCovN; i++)
    {
        if ((sCov[i].sees & (1 << p)) && coverGuardUp(covChr(&sCov[i]))) return 1;
    }
    return 0;
}

int portHeistCoverCount(void) { return sCovN; }
float portHeistCoverBondSpeed(int p) { return (p >= 0 && p < 4) ? sBondSpeed[p] : 0.0f; }

/* ---- the power (power.set): the mains feed the alarm panels, the siren and the cameras ----
 * Out: alarmActivate does nothing (propobj.c), so no panel, raiser, camera or script raises the alarm; the mode switches the cameras
 * off and the siren down. Mode only. */
void portHeistPowerSet(int on) { sPowerOut = on ? 0 : 1; }
int portHeistPowerOn(void) { return !sPowerOut; }
int portHeistAlarmBlocked(void) { return sPowerOut && portModeActive(); }

/* ---- scripted walks (spawn.go): a chr walks (or runs) to a pad on the stock route, its own list still running ---- */
int portHeistChrGo(int chrnum, int pad, int run)
{
    ChrRecord *c = chrFindByLiteralId(chrnum);
    int i, slot = -1;

    if (!portModeActive() || c == NULL || c->prop == NULL || c->model == NULL || chrIsDead(c) || pad < 0) return 0;
    if (!chrGoToPad(c, pad, run ? SPEED_RUN : SPEED_WALK)) return 0;
    for (i = 0; i < GO_MAX; i++)
    {
        if (sGo[i].used && sGo[i].chrnum == chrnum) { slot = i; break; }
        if (!sGo[i].used && slot < 0) slot = i;
    }
    if (slot < 0) return 1;   /* it walks, but the arena route may re-aim it */
    sGo[slot].used = 1;
    sGo[slot].chrnum = chrnum;
    sGo[slot].pad = pad;
    sGo[slot].until = sNow + 60 * 60;
    if (slot >= sGoN) sGoN = slot + 1;
    return 1;
}

int portHeistChrOnGo(const ChrRecord *c)
{
    int i;

    for (i = 0; i < sGoN; i++)
    {
        if (sGo[i].used && sGo[i].chrnum == c->chrnum) return 1;
    }
    return 0;
}

static void goTick(void)
{
    int i;

    for (i = 0; i < sGoN; i++)
    {
        ChrRecord *c;

        if (!sGo[i].used) continue;
        c = chrFindByLiteralId(sGo[i].chrnum);
        if (c == NULL || c->prop == NULL || chrIsDead(c) || sNow > sGo[i].until
            || (sNow > sGo[i].until - 60 * 60 + 10 && c->actiontype != ACT_GOPOS && c->actiontype != ACT_RUNPOS))
        {
            sGo[i].used = 0;   /* arrived (stopped), gone, or a minute on the way */
        }
    }
}

/* ---- the radio (AC 2026-10-25): a witness's call and the security room's relay are heard where the guard is ----
 * RADIO_MESSAGE_SFX (80) is the sound Caverns' level AI plays when Bond uses its radio (UsetupcaveZ.c: sfx_play(0x5000, 0) = sound 80);
 * RADIO_SFX (94) a short squelch. Played through port_gamesfx.c (its own slots, never a game sound's place, its own RNG): presentation
 * only, at a volume and pan from the guard's position relative to THIS machine's Bond. Nothing comes back into the sim. */
static void radioSfx(s32 chrnum, int relay)
{
    extern int portGameSfxPlay(int sfx, float vol, float pitch, float delay, float pan);
    extern int portLocalPlayer(void);
    ChrRecord *c = chrFindByLiteralId(chrnum);
    int lp = portLocalPlayer();
    struct player *pl = (lp >= 0 && lp < 4) ? g_playerPointers[lp] : NULL;
    f32 vol = 0.9f, pan = 0.0f;

    if (c != NULL && c->prop != NULL && pl != NULL && pl->prop != NULL)
    {
        f32 dx = c->prop->pos.x - pl->prop->pos.x, dz = c->prop->pos.z - pl->prop->pos.z, d = sqrtf(dx * dx + dz * dz), th, rx;

        vol = d < 300.0f ? 1.0f : (d > 3000.0f ? 0.25f : 1.0f - 0.75f * (d - 300.0f) / 2700.0f);   /* never silent: it is a cue */
        th = pl->vv_theta * (3.14159265f / 180.0f);
        rx = d > 1.0f ? (dx * cosf(th) + dz * sinf(th)) / d : 0.0f;   /* his right, for a view along (-sin, cos) */
        pan = rx < -1.0f ? -1.0f : (rx > 1.0f ? 1.0f : rx);
    }
    if (relay)
    {   /* the security room relays a camera's report: two quick squelches and the message */
        portGameSfxPlay(RADIO_SFX, vol * 0.8f, 1.15f, 0.0f, pan);
        portGameSfxPlay(RADIO_SFX, vol * 0.8f, 0.95f, 0.25f, pan);
        portGameSfxPlay(RADIO_MESSAGE_SFX, vol, 1.05f, 0.45f, pan);
    }
    else
    {
        portGameSfxPlay(RADIO_MESSAGE_SFX, vol, 1.0f, 0.0f, pan);
    }
}

/* cover.radio(chr [, "relay"]): the script plays the radio at that guard (the heist: the security room's relay) */
int portHeistRadioSfx(int chrnum, int relay)
{
    if (!portModeActive()) return 0;
    radioSfx(chrnum, relay);
    return 1;
}

/* ---- guard door rules (AC 2026-10-25: guards who heard Bond in the vault walked over and opened the vault door) ----
 * The stock route (chraction.c, the waypoint follower) opens any door a chr walks into, keyed or not. With mode.guard_doors(true) a door
 * the script gave a rule (door.guard_access) opens only for a guard whose role (spawn.role) is in its allowed set: "never" (the vault
 * doors) is the empty set; a door with no rule opens for anyone (stock). A refused guard walks into the shut door and waits there. Roles
 * are names the script picks (the heist: a guard's area, "squad", "wave", "barracks" ...), up to 32. Mode only; in the hash. */
#define ROLE_MAX      32
#define ROLE_CHR_MAX  768
#define DOOR_RULE_MAX 64
static char sRoleName[ROLE_MAX][24];
static int sRoleN;
static struct { s32 chrnum; u32 bits; } sRoleChr[ROLE_CHR_MAX];
static int sRoleChrN;
static struct { s32 idx; u32 allow; u8 hadAiLock; } sDoorRule[DOOR_RULE_MAX];
static int sDoorRuleN, sGuardDoors;
static s32 sDoorRefused, sDoorOpened;

static void doorRulesReset(void)
{
    memset(sRoleName, 0, sizeof(sRoleName));
    sRoleN = sRoleChrN = sDoorRuleN = sGuardDoors = 0;
    sDoorRefused = sDoorOpened = 0;
}

/* a role name's bit (registered on first use); -1 when the table is full */
int portHeistRoleBit(const char *name)
{
    int i;

    for (i = 0; i < sRoleN; i++)
    {
        if (strcmp(sRoleName[i], name) == 0) return i;
    }
    if (sRoleN >= ROLE_MAX) return -1;
    snprintf(sRoleName[sRoleN], sizeof(sRoleName[0]), "%s", name);
    return sRoleN++;
}

/* spawn.role(chr, roles): the chr's roles (replaces) */
int portHeistSetChrRoles(int chrnum, unsigned bits)
{
    int i;

    for (i = 0; i < sRoleChrN; i++)
    {
        if (sRoleChr[i].chrnum == chrnum) { sRoleChr[i].bits = bits; return 1; }
    }
    if (sRoleChrN >= ROLE_CHR_MAX) return 0;
    sRoleChr[sRoleChrN].chrnum = chrnum;
    sRoleChr[sRoleChrN++].bits = bits;
    return 1;
}

unsigned portHeistChrRoles(int chrnum)
{
    int i;

    for (i = 0; i < sRoleChrN; i++)
    {
        if (sRoleChr[i].chrnum == chrnum) return sRoleChr[i].bits;
    }
    return 0;
}

/* A ruled door also gets the stock "AI cannot operate" bit (flags2 0x20000000) while the rules are on: an on-screen chr's move test
 * ignores ordinary doors (it opens them, chraction.c) and collides only with doors locked to AI (CDTYPE_DOORSLOCKEDTOAI,
 * propDoorGetCdTypes), so without it a refused guard walked straight through the shut door. A role holder still finds the door on his
 * path (the door finder looks for closed doors) and opens it: the finder skips a door with this bit (chraction.c), so it asks
 * portHeistChrMayOpenDoor, whose 2 lets him past the bit; an open door lets anyone through, as it lets Bond. */
static void doorRuleLock(int i, int on)
{
    DoorRecord *d = heistDoor(sDoorRule[i].idx);

    if (d == NULL) return;
    if (on) d->flags2 |= PROPFLAG2_20000000;
    else if (!sDoorRule[i].hadAiLock) d->flags2 &= ~PROPFLAG2_20000000;
}

/* door.guard_access(i, ...): rule 1 sets the allowed role bits (0 = never), rule 0 removes the rule (anyone may open it) */
int portHeistDoorGuardAccess(int idx, int rule, unsigned allow)
{
    DoorRecord *d = heistDoor(idx);
    int i;

    if (d == NULL) return 0;
    for (i = 0; i < sDoorRuleN; i++)
    {
        if (sDoorRule[i].idx == idx) break;
    }
    if (!rule)
    {
        if (i < sDoorRuleN)
        {
            doorRuleLock(i, 0);
            sDoorRule[i] = sDoorRule[--sDoorRuleN];
        }
        return 1;
    }
    if (i == sDoorRuleN)
    {
        if (sDoorRuleN >= DOOR_RULE_MAX) return 0;
        sDoorRuleN++;
        sDoorRule[i].hadAiLock = (d->flags2 & PROPFLAG2_20000000) ? 1 : 0;
    }
    sDoorRule[i].idx = idx;
    sDoorRule[i].allow = allow;
    doorRuleLock(i, sGuardDoors);
    return 1;
}

void portHeistSetGuardDoors(int on)
{
    int i;

    sGuardDoors = on ? 1 : 0;
    for (i = 0; i < sDoorRuleN; i++) doorRuleLock(i, sGuardDoors);
}
int portHeistGuardDoorsOn(void) { return sGuardDoors; }
/* chraction.c chrlvTravelTickMagic: an offscreen chr hops from pad to pad with no collision (stock), so a shut door is no obstacle.
 * 1 = the hop to `to` passes within 90 units of a ruled door that is not open and this chr may not open: he does not hop (stock's
 * "destination blocked" branch: he walks instead, and the door, locked to AI, stops him). */
int portHeistHopBlocked(ChrRecord *self, coord3d *to)
{
    int i;

    if (!sGuardDoors || sDoorRuleN == 0 || !portModeActive() || self == NULL || self->prop == NULL) return 0;
    for (i = 0; i < sDoorRuleN; i++)
    {
        DoorRecord *d = heistDoor(sDoorRule[i].idx);
        PropRecord *dp;
        f32 ax, az, bx, bz, px, pz, len2, t, ex, ez;

        if (d == NULL || (dp = ((ObjectRecord *) d)->prop) == NULL || d->openPosition >= 0.5f) continue;
        if (sDoorRule[i].allow & portHeistChrRoles(self->chrnum)) continue;
        if (dp->pos.y - self->prop->pos.y > 300.0f || dp->pos.y - self->prop->pos.y < -300.0f) continue;
        ax = self->prop->pos.x; az = self->prop->pos.z;
        bx = to->x - ax; bz = to->z - az;
        px = dp->pos.x - ax; pz = dp->pos.z - az;
        len2 = bx * bx + bz * bz;
        t = len2 > 1.0f ? (px * bx + pz * bz) / len2 : 0.0f;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        ex = px - t * bx;
        ez = pz - t * bz;
        if (ex * ex + ez * ez < 90.0f * 90.0f)
        {
            if (sDoorRefused < 0x7FFFFFF0) sDoorRefused++;
            return 1;
        }
    }
    return 0;
}
void portHeistDoorStats(int *refused, int *opened) { *refused = sDoorRefused; *opened = sDoorOpened; }

/* chraction.c, the waypoint follower: chr `self` walks into door `door` (a DoorRecord); 0 = he may not open it, 1 = no rule (stock: he
 * opens it unless the door is locked to AI), 2 = the rule lets him (he opens it although the rule's own AI-lock bit is on it: the stock
 * door finder skips a door with that bit, so without this no allowed guard on screen opened a ruled door; a door locked to AI in the
 * layout itself stays shut, 1) */
int portHeistChrMayOpenDoor(ChrRecord *self, void *door)
{
    int idx, i;

    if (!sGuardDoors || sDoorRuleN == 0 || !portModeActive() || self == NULL || door == NULL) return 1;
    idx = portHeistSetupIndex(door);
    for (i = 0; i < sDoorRuleN; i++)
    {
        if (sDoorRule[i].idx == idx)
        {
            DoorRecord *d = (DoorRecord *) door;

            if (sDoorRule[i].allow & portHeistChrRoles(self->chrnum))
            {
                if (d->openPosition <= 0.0f && d->openstate != DOORSTATE_OPENING) sDoorOpened++;
                return sDoorRule[i].hadAiLock ? 1 : 2;
            }
            if (sDoorRefused < 0x7FFFFFF0) sDoorRefused++;
            if ((sDoorRefused & 63) == 1)
            {
                printf("ge: heist: guard %d may not open door %d (refused %d so far)\n", (int) self->chrnum, idx, (int) sDoorRefused);
            }
            return 0;
        }
    }
    return 1;
}

static unsigned doorRulesHash(void)
{
    unsigned h = (unsigned) sGuardDoors * 7u + (unsigned) sRoleN * 11u + (unsigned) sDoorRefused * 13u + (unsigned) sDoorOpened * 17u;
    int i;

    for (i = 0; i < sRoleChrN; i++) h = h * 16777619u + (unsigned) sRoleChr[i].chrnum + sRoleChr[i].bits * 3u;
    for (i = 0; i < sDoorRuleN; i++) h = h * 16777619u + (unsigned) sDoorRule[i].idx + sDoorRule[i].allow * 5u;
    return h;
}

/* portHeistAiTick */
void portHeistCoverTick(void)
{
    if (sGoN > 0) goTick();
    if (!sCoverOn || sCoverAlert || sCovN == 0) return;
    sCovAcc += g_ClockTimer;
    if (sCovAcc >= COVER_SCAN)
    {
        coverScan(sCovAcc / 60.0f);
        sCovAcc = 0;
    }
}

unsigned portHeistCoverHash(void)
{
    unsigned h = (unsigned) sCoverOn * 3u + (unsigned) sCoverAlert * 5u + (unsigned) sPowerOut * 7u + (unsigned) sCovN * 11u;
    int i, p;

    for (i = 0; i < 256; i++) h = h * 31u + sRoomClass[i];
    for (p = 0; p < 4; p++)
    {
        h = h * 16777619u + sCoverLoot[p] + (unsigned) sCoverActUntil[p] * 3u + (unsigned) sBondPOk[p] * 5u
            + (unsigned) (s32) (sBondSpeed[p] * 16.0f) * 7u + (unsigned) (s32) sBondPX[p] * 11u + (unsigned) (s32) sBondPZ[p] * 13u;
    }
    h = h * 16777619u + (unsigned) sCoverShooter + (unsigned) sCoverShotAt * 3u + (unsigned) sCovAcc * 5u;
    h = h * 16777619u + (unsigned) (s32) (sCt.range + sCt.cone * 3.0f + sCt.armed * 50.0f + sCt.restricted * 70.0f + sCt.staff * 90.0f
                                            + sCt.run * 110.0f + sCt.runSpeed + sCt.close + sCt.decay * 130.0f + sCt.radio * 170.0f);
    for (i = 0; i < sCovN; i++)
    {
        const HeistCover *e = &sCov[i];

        h = h * 16777619u + (unsigned) e->chrnum + (unsigned) e->role * 3u + (unsigned) e->witness * 5u + (unsigned) e->reported * 7u
            + (unsigned) e->sees * 11u + (unsigned) (int) e->arghs * 13u + (unsigned) e->hear * 17u;
        for (p = 0; p < 4; p++)
        {
            h = h * 16777619u + (unsigned) (s32) (e->susp[p] * 1000.0f) + (unsigned) (s32) (e->linger[p] * 100.0f) * 3u
                + (unsigned) e->heyAt[p] * 5u + (unsigned) e->why[p] * 7u;
        }
    }
    for (i = 0; i < sGoN; i++)
    {
        if (sGo[i].used) h = h * 16777619u + (unsigned) sGo[i].chrnum + (unsigned) sGo[i].pad * 3u + (unsigned) sGo[i].until * 5u;
    }
    return h * 16777619u + doorRulesHash();
}

/* mode.state(text): the mode script's own sim state as one string (the heist: phase, crack progress and jams, the vault timer, loot,
 * waves, ...), set every tick. Its FNV hash goes into the state hash below and the text into the state dump, so a desync in script
 * state shows as one, and the dump says where. Reset at every stage start. */
static unsigned sModeStateHash;
static char sModeStateText[600];

void portHeistModeState(const char *s)
{
    unsigned h = 2166136261u;
    const char *c;

    for (c = s != NULL ? s : ""; *c != '\0'; c++) h = (h ^ (unsigned char) *c) * 16777619u;
    sModeStateHash = h;
    snprintf(sModeStateText, sizeof(sModeStateText), "%s", s != NULL ? s : "");
}

const char *portHeistModeStateText(void) { return sModeStateText; }

void portHeistModeStateReset(void)
{
    sModeStateHash = 0;
    sModeStateText[0] = '\0';
    memset(sUseKey, 0, sizeof(sUseKey));
}

/* folded into the co-op state hash (port_coop.c "flags"); 0 outside a mode */
unsigned portHeistStateHash(void)
{
    unsigned h = 0;
    int i;

    if (sCamBondN > 0) h = cctvBondHash();   /* co-op cameras: every Bond's sight time (any stage, mode or not) */
    if (!portModeActive()) return h;
    h = h * 16777619u + sModeStateHash;
    for (i = 0; i < (int) portBondCount() && i < 4; i++)
    {
        h = h * 16777619u + portHeistBondKeys(i);   /* per-player cards (mode.per_player_keys) */
    }
    for (i = 0; i < 4; i++) h = h * 16777619u + sUseKey[i];
    h = h * 16777619u + (unsigned) sFoundN * 131u + (unsigned) sCivKilled * 7919u + (unsigned) sBodyCheck;
    for (i = 0; i < sCivN; i++)
    {
        h = h * 16777619u + (unsigned) sCiv[i].state + (unsigned) sCiv[i].dropped * 4u + (unsigned) sCiv[i].counted * 8u + (unsigned) (sCiv[i].aimT * 100.0f);
        h = h * 16777619u + (unsigned) sCiv[i].chrnum + (unsigned) sCiv[i].byPlayer * 5u + (unsigned) (sCiv[i].threatT * 100.0f) * 7u
            + (unsigned) (sCiv[i].idleT * 100.0f) * 11u + (unsigned) (sCiv[i].cowerT * 100.0f) * 13u;
        {
            int p;

            for (p = 0; p < 4; p++) h = h * 16777619u + (unsigned) (sCiv[i].aimP[p] * 100.0f) + (unsigned) (sCiv[i].threatP[p] * 100.0f) * 3u;
        }
    }
    h = h * 16777619u + (unsigned) sNow + (unsigned) sAcc * 3u + (unsigned) sKeyNext * 5u + (unsigned) sPatrolN * 7u;
    for (i = 0; i < sFoundN; i++) h = h * 16777619u + (unsigned) sFound[i];
    h += (unsigned) sPerPlayerKeys * 0x9E3779B1u + (unsigned) sCctvAllTicks * 31u + (unsigned) sCctvAlarmOnSpot * 7u;
    for (i = 0; i < sCctvOvN; i++) h = h * 16777619u + (unsigned) sCctvTicks[i];
    for (i = 0; i < (int) sizeof(sCctvOff); i++) h = h * 16777619u + sCctvOff[i];
    h = h * 16777619u + (unsigned) sSlapsToKo * 97u + (unsigned) sGuardsWake + (unsigned) sKoTotal * 389u + (unsigned) sKoWoken * 1543u;
    for (i = 0; i < sKoN; i++)
    {
        const HeistKo *e = &sKo[i];

        if (!e->used) continue;
        h = h * 16777619u + (unsigned) e->chrnum * 3u + (unsigned) e->slaps * 5u + (unsigned) e->down * 11u + (unsigned) e->searched * 13u
            + (unsigned) e->wstate * 17u + (unsigned) e->waker * 19u + (unsigned) (e->wT * 100.0f);
        h = h * 16777619u + (unsigned) e->byPlayer * 3u + (unsigned) e->downAt * 5u + (unsigned) e->addedFlags + (unsigned) e->wPlot * 7u
            + (unsigned) (int) e->wArghs * 11u;
    }
    if (countdownTimerIsRunning()) h = h * 16777619u + (unsigned) countdownTimerGetValue();
    h = h * 16777619u + (unsigned) sLootSerial * 211u + (unsigned) sLootTaken * 977u;
    for (i = 0; i < LOOT_N; i++)
    {
        if (sLoot[i].used) h = h * 16777619u + (unsigned) i * 3u + (unsigned) sLoot[i].kind;
    }
    {
        extern unsigned portCctvViewHash(void);   /* port_cctvview.c: who watches which camera (sim state: it holds the Bond) */
        extern unsigned portHeistCoverHash(void);   /* the disguise, the power, the scripted walks (below) */

        h = h * 16777619u + portCctvViewHash();
        h = h * 16777619u + portHeistCoverHash();
    }
    {
        extern unsigned portGadgetsHash(void);   /* port_gadgets.c: triggers, reels, disguises */

        h = h * 16777619u + portGadgetsHash();
    }
    return h;
}

#endif /* TARGET_N64 */
