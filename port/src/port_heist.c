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
void portHeistGuardsReset(void);

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
    portHeistGuardsReset();
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

typedef struct { s32 chrnum; u8 state, dropped, counted, used; f32 aimT, idleT, threatT, cowerT; s32 byPlayer; } HeistCiv;
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

static int bondArmed(int p)
{
    s32 saved = get_cur_playernum();
    int w;

    set_cur_player(p);
    w = (int) getCurrentPlayerWeaponId(GUNRIGHT);
    set_cur_player(saved);
    return w != 0 && w != ITEM_UNARMED;   /* note: the slappers are ITEM_FIST, so they count as armed here (heist_guards relies on it) */
}

/* the first Bond (lowest index) whose crosshair is on `civ`: within sCivRange, ahead of him, and the view ray passes within 90 units of it */
static int aimedBy(ChrRecord *civ)
{
    int p, n = (int) portBondCount();

    for (p = 0; p < n && p < 4; p++)
    {
        struct player *pl = g_playerPointers[p];
        float dx, dy, dz, th, fx, fz, fwd, lat;

        if (!portBondInPlay(p) || pl == NULL || pl->prop == NULL) continue;
        dx = civ->prop->pos.x - pl->prop->pos.x;
        dy = civ->prop->pos.y - pl->prop->pos.y;
        dz = civ->prop->pos.z - pl->prop->pos.z;
        if (dy > 300.0f || dy < -300.0f) continue;
        th = pl->vv_theta * (3.14159265f / 180.0f);
        fx = -sinf(th);
        fz = cosf(th);
        fwd = dx * fx + dz * fz;
        lat = dx * -fz + dz * fx;
        if (fwd <= 0.0f || fwd > sCivRange || fabsf(lat) > 90.0f) continue;
        if (!bondArmed(p)) continue;
        return p;
    }
    return -1;
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
    int by;

    if (c == NULL || c->prop == NULL || c->model == NULL || (c->hidden & CHRHIDDEN_REMOVE) || chrIsDead(c))
    {
        return;   /* gone or dead: portHeistChrDied counts the death */
    }
    if (koIsDown(v->chrnum))
    {
        return;   /* knocked out: the KO tick holds it down */
    }
    by = aimedBy(c);
    if (v->state == 1)
    {   /* hands up: keep the pose; a sustained threat makes it hand over what it carries */
        portLuaGameChrAnim(v->chrnum, "surrender", 0);
        if (by >= 0)
        {
            v->idleT = 0.0f;
            v->threatT += dt;
            if (!v->dropped && v->threatT >= sCivThreatSec)
            {
                v->dropped = 1;
                chrDropItems(c);   /* CHR_DROP_ALL_CONCEALED_ITEMS: a key from spawn.key falls where it stands */
                portEventEmit(PORT_EV_CIVILIAN_ROBBED, v->chrnum, by, 0, 0);
            }
        }
        else
        {
            v->idleT += dt;
            if (v->idleT > 3.0f) { v->state = 0; v->aimT = v->threatT = 0.0f; }   /* nobody aims at it any more: lowers its hands */
        }
        return;
    }
    if (v->state == 2)
    {   /* cowering after gunfire */
        portLuaGameChrAnim(v->chrnum, "startled", 0);
        v->cowerT -= dt;
        if (by >= 0 && v->cowerT > 0.0f) { v->aimT += dt; }
        if (v->cowerT <= 0.0f) v->state = 0;
        if (by < 0 || v->aimT < sCivAimSec) return;
    }
    if (by >= 0)
    {
        v->aimT += dt;
        if (v->aimT >= sCivAimSec)
        {
            v->state = 1;
            v->threatT = v->idleT = 0.0f;
            v->byPlayer = by;
            portLuaGameChrAnim(v->chrnum, "surrender", 1);
            portEventEmit(PORT_EV_CIVILIAN_SURRENDERED, v->chrnum, by, 0, 0);
        }
        return;
    }
    v->aimT = v->aimT > dt ? v->aimT - dt : 0.0f;
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
    if (e->slaps < 255) e->slaps++;
    if ((int) e->slaps < sSlapsToKo) return 2;
    koDown(c, e, (int) get_cur_playernum());
    return 1;
}

/* the chr runs no AI list this tick (chraction.c): knocked out, or a guard walking over to wake one */
int portHeistChrNoAi(ChrRecord *c)
{
    int i;

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
}

/* called every sim tick from portBlocksTick while a mode runs */
void portHeistAiTick(void)
{
    f32 dt = g_ClockTimer / 60.0f;
    int i;

    sNow += g_ClockTimer;
    sAcc += g_ClockTimer;
    if (sKoN > 0) koTick(dt);
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

/* folded into the co-op state hash (port_coop.c "flags"); 0 outside a mode */
unsigned portHeistStateHash(void)
{
    unsigned h = 0;
    int i;

    if (!portModeActive()) return 0;
    h = h * 16777619u + (unsigned) sFoundN * 131u + (unsigned) sCivKilled * 7919u + (unsigned) sBodyCheck;
    for (i = 0; i < sCivN; i++)
    {
        h = h * 16777619u + (unsigned) sCiv[i].state + (unsigned) sCiv[i].dropped * 4u + (unsigned) sCiv[i].counted * 8u + (unsigned) (sCiv[i].aimT * 100.0f);
    }
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
    }
    if (countdownTimerIsRunning()) h = h * 16777619u + (unsigned) countdownTimerGetValue();
    return h;
}

#endif /* TARGET_N64 */
