/* port_coopdrop.c - co-op respawn loadout and death drops (netplay job 3d).
 *
 * Everything here runs from the sim tick (portCoopDeathTick, port_coop.c) or from the respawn handler it calls, as the
 * dead / respawning Bond, from state both machines share (the inventory ring in order, the death position, the loadout
 * snapshot taken at the stage's first tick), and draws no RNG, so it is the same on both machines. Co-op only; solo and
 * the other modes never call it. */
#ifndef TARGET_N64

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ultra64.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include "game/player.h"
#include "game/bondview.h"
#include "game/bondinv.h"
#include "game/bg.h"
#include "game/gun.h"
#include "game/propobj.h"
#include "game/stan.h"
#include "game/objective_status.h"
#include "game/chr.h"
#include "game/chraction.h"
#include "game/chrai.h"
#include "port_coop.h"
#include "port_cooplogic.h"

extern int portCoopActive(void);
extern int portAllPlayerCount(void);
extern int portCoopRespawnPartnerOf(int self);
extern void portCoopFpLog(const char *fmt, ...);
extern s32 get_ammo_type_for_weapon(ITEM_IDS weapon);
extern PROP getPropForHeldItem(ITEM_IDS arg0);
extern ObjectRecord *create_new_item_instance_of_model(PROP modelnum, s32 weaponid);
extern void gunInitProjectileObject(ObjectRecord *obj, coord3d *pos, StandTile *stan, Mtxf *matrix, coord3d *velocity, Mtxf *arg5, PropRecord *owner);
extern f32 bondviewYPositionRelated(struct StandTile *arg0, f32 arg1, f32 arg2);
extern void matrix_4x4_set_identity(Mtxf *m);
extern void objDetach(PropRecord *prop);
extern void chrpropReparent(PropRecord *child, PropRecord *newparent);

#define MAXSNAP 40

/* ---- the stage's starting loadout ------------------------------------------------------------------------------ */

typedef struct {
    int valid;
    int n;
    int kind[MAXSNAP];    /* INV_ITEM_WEAPON / INV_ITEM_DUAL */
    int a[MAXSNAP], b[MAXSNAP];
    int ammo[30];         /* reserve plus the loaded magazines */
    int startR, startL;   /* starting_weapon[] */
} Snap;

static Snap sSnap[4];
#define DROP_TABLE 32
static struct { ObjectRecord *obj; int qty; } sAmmo[DROP_TABLE];   /* rounds a dropped gun holds, by object */
static int sAmmoNext;

void portCoopDropReset(void)
{
    memset(sSnap, 0, sizeof(sSnap));
    memset(sAmmo, 0, sizeof(sAmmo));
    sAmmoNext = 0;
}

/* Called once, at the stage's first sim tick, for every Bond: this is the full starting loadout the solo stage start
 * built (intro items, whatever the stage script gave, the slapper) and its ammo. */
void portCoopLoadoutSnapshot(int p)
{
    struct player *pl = (p >= 0 && p < 4) ? g_playerPointers[p] : NULL;
    InvItem *first;
    InvItem *it;
    Snap *s;
    int guard = 0;
    int h;

    if (pl == NULL) {
        return;
    }
    s = &sSnap[p];
    memset(s, 0, sizeof(*s));
    first = pl->ptr_inventory_first_in_cycle;
    it = first;
    while (it != NULL && guard++ < 64 && s->n < MAXSNAP) {
        if (it->type == INV_ITEM_WEAPON) {
            s->kind[s->n] = INV_ITEM_WEAPON;
            s->a[s->n] = (int) it->type_inv_item.type_weap.weapon;
            s->b[s->n] = -1;
            s->n++;
        } else if (it->type == INV_ITEM_DUAL) {
            s->kind[s->n] = INV_ITEM_DUAL;
            s->a[s->n] = (int) it->type_inv_item.type_dual.weapon_right;
            s->b[s->n] = (int) it->type_inv_item.type_dual.weapon_left;
            s->n++;
        }
        it = it->next;
        if (it == first) {
            break;
        }
    }
    for (h = 0; h < 30; h++) {
        s->ammo[h] = (int) pl->ammoheldarr[h];
    }
    for (h = 0; h < 2; h++) {
        ITEM_IDS w = pl->hands[h].weaponnum;
        int t = (w > ITEM_UNARMED) ? (int) get_ammo_type_for_weapon(w) : 0;

        if (t > 0 && t < 30) {
            s->ammo[t] += (int) pl->hands[h].weapon_ammo_in_magazine;
        }
    }
    s->startR = (int) starting_weapon[GUNRIGHT];
    s->startL = (int) starting_weapon[GUNLEFT];
    s->valid = 1;
}

/* mp_respawn_handler (co-op only), as the respawning Bond, after the setup's intro items: the full starting loadout
 * and ammo, the slapper, nothing else (body armour and picked-up guns are gone), and the starting weapon in hand. */
void portCoopLoadoutRestore(int p)
{
    Snap *s = (p >= 0 && p < 4) ? &sSnap[p] : NULL;
    int i;

    g_CurrentPlayer->bondarmour = 0.0f;
    if (s == NULL || !s->valid) {
        return;
    }
    bondinvAddInvItem(ITEM_FIST);
    for (i = 0; i < s->n; i++) {
        if (s->kind[i] == INV_ITEM_DUAL) {
            bondinvAddDoublesInvItem((ITEM_IDS) s->a[i], (ITEM_IDS) s->b[i]);
        } else {
            bondinvAddInvItem((ITEM_IDS) s->a[i]);
        }
    }
    for (i = 0; i < 30; i++) {
        g_CurrentPlayer->ammoheldarr[i] = s->ammo[i];
    }
    currentPlayerEquipWeaponWrapper(GUNLEFT, s->startL);
    currentPlayerEquipWeaponWrapper(GUNRIGHT, s->startR > 0 ? s->startR : ITEM_FIST);
}

static int inStartSingles(const Snap *s, int id)
{
    int i;

    if (id == ITEM_FIST) {
        return 1;
    }
    for (i = 0; i < s->n; i++) {
        if (s->kind[i] == INV_ITEM_WEAPON && s->a[i] == id) {
            return 1;
        }
    }
    return 0;
}

static int inStartDuals(const Snap *s, int r, int l)
{
    int i;

    for (i = 0; i < s->n; i++) {
        if (s->kind[i] == INV_ITEM_DUAL && s->a[i] == r && s->b[i] == l) {
            return 1;
        }
    }
    return 0;
}

/* ---- dropped guns carry their magazine ------------------------------------------------------------------------- */

/* propobj.c ammo_collected_from_weapon: the rounds a dropped gun holds (-1: an ordinary pickup, the stage's rule). Taken once. */
int portCoopDropAmmoTake(ObjectRecord *obj)
{
    int i;

    for (i = 0; i < DROP_TABLE; i++) {
        if (sAmmo[i].obj == obj) {
            int q = sAmmo[i].qty;

            sAmmo[i].obj = NULL;
            return q;
        }
    }
    return -1;
}

/* ---- the death drops ------------------------------------------------------------------------------------------- */

static int forceUnsafe(void)
{
    static int on = -1;

    if (on < 0) {
        const char *e = getenv("GE_COOP_UNSAFEDROP");

        on = (e != NULL && e[0] != '\0' && e[0] != '0');
    }
    return on;
}

/* The k-th spot around the death position: the spot itself for k = 0, else a ring offset whose tile the Bond could walk to
 * at about the same floor height, else the spot itself. */
static void spotFor(PropRecord *bp, int k, float *x, float *z, StandTile **tile, float *floorY)
{
    float bx = bp->pos.x;
    float bz = bp->pos.z;
    StandTile *bs = (StandTile *) bp->stan;
    float f0 = bondviewYPositionRelated(bs, bx, bz);
    float ox, oz;

    *x = bx;
    *z = bz;
    *tile = bs;
    *floorY = f0;
    if (k > 0 && portCoopDropOffset(k, &ox, &oz)) {
        StandTile *walk = bs;
        float h;

        walkTilesBetweenPoints_NoCallback(&walk, bx, bz, bx + ox, bz + oz);
        if (walk != NULL && isPointInsideTriStandTileUnscaled_Maybe(walk, bx + ox, bz + oz)) {
            h = bondviewYPositionRelated(walk, bx + ox, bz + oz);
            if (h - f0 <= 60.0f && f0 - h <= 60.0f) {
                *x = bx + ox;
                *z = bz + oz;
                *tile = walk;
                *floorY = h;
            }
        }
    }
}

/* put an object that now has no parent into the world at (x, y, z) in `tile`. Netplay job 3e: the drops are placed with NO owner (callers pass
 * NULL): an owner is the prop whose render pass runs the object's physics, and a drop must not depend on the dead Bond's pass (it then runs
 * in the first pass of the shuffled order, like a gun a guard drops). Who may pick it up is the pickup rule itself: propPickupByPlayer refuses
 * a dead Bond, everyone else collects at once, the former owner too after its respawn. */
static void placeObject(ObjectRecord *obj, PropRecord *owner, float x, float y, float z, StandTile *tile)
{
    Mtxf mtx;
    Mtxf rot;
    coord3d pos;
    coord3d vel;

    matrix_4x4_set_identity(&mtx);
    matrix_4x4_set_identity(&rot);
    pos.x = x;
    pos.y = y;
    pos.z = z;
    vel.x = 0.0f;
    vel.y = 0.0f;
    vel.z = 0.0f;
    gunInitProjectileObject(obj, &pos, tile, &mtx, &vel, &rot, owner);
}

/* Netplay job 6: the objective items that live in the gadget ring (a given GoldenEye key, DAT tape, documents, ...) drop at the death spot like a
 * quest prop does; the world model is the one the level's own copy of the item uses (the game itself throws the GoldenEye key with
 * PROP_CHRGOLDENEYEKEY). -1 = not an objective item (the watch, the built-in gadgets): those stay with the partner as before. */
static int questItemModel(int id)
{
    switch (id) {
    case ITEM_GOLDENEYEKEY:  return PROP_CHRGOLDENEYEKEY;
    case ITEM_BLACKBOX:      return PROP_CHRBLACKBOX;
    case ITEM_CIRCUITBOARD:  return PROP_CHRCIRCUITBOARD;
    case ITEM_CLIPBOARD:     return PROP_CHRCLIPBOARD;
    case ITEM_STAFFLIST:     return PROP_CHRSTAFFLIST;
    case ITEM_DOSSIERRED:    return PROP_CHRDOSSIERRED;
    case ITEM_PLANS:         return PROP_CHRPLANS;
    case ITEM_SPYFILE:       return PROP_CHRSPYFILE;
    case ITEM_BLUEPRINTS:    return PROP_CHRBLUEPRINTS;
    case ITEM_MAP:           return PROP_CHRMAP;
    case ITEM_AUDIOTAPE:     return PROP_CHRAUDIOTAPE;
    case ITEM_VIDEOTAPE:     return PROP_CHRVIDEOTAPE;
    case ITEM_DATTAPE:       return PROP_CHRDATTAPE;
    case ITEM_SPOOLTAPE:     return PROP_CHRSPOOLTAPE;
    case ITEM_MICROFILM:     return PROP_CHRMICROFILM;
    case ITEM_MICROCODE:     return PROP_CHRMICROCODE;
    case ITEM_LECTRE:        return PROP_CHRLECTRE;
    case ITEM_MONEY:         return PROP_CHRMONEY;
    case ITEM_GOLDBAR:       return PROP_CHRGOLDBAR;
    case ITEM_HEROIN:        return PROP_CHRHEROIN;
    case ITEM_KEYYALE:       return PROP_CHRKEYYALE;
    case ITEM_KEYBOLT:       return PROP_CHRKEYBOLT;
    default:                 return -1;
    }
}

typedef struct {
    int kind;          /* 0 gun, 1 quest prop, 2 gadget (no world model), 3 objective item of the gadget ring (job 6) */
    int id;            /* weapon id */
    int hand;          /* -1, or the hand (0 right, 1 left) this gun is in */
    InvItem *it;
    PropRecord *prop;
} Ent;

/* Called once when the sim first sees Bond p dead (portCoopDeathTick), before anything else touches the inventory. */
void portCoopDeathDrops(int p)
{
    struct player *pl = (p >= 0 && p < 4) ? g_playerPointers[p] : NULL;
    Snap *snap = (p >= 0 && p < 4) ? &sSnap[p] : NULL;
    const int partner = portCoopRespawnPartnerOf(p);
    s32 saved = get_cur_playernum();
    Ent ent[64];
    int n = 0;
    int pool[30];
    int i, k = 0, guard = 0, safe, hasTile, inTile;
    float foot, floorY, wf;
    PropRecord *bp;
    InvItem *first;
    InvItem *it;

    if (pl == NULL || pl->prop == NULL || snap == NULL) {
        return;
    }
    bp = pl->prop;
    set_cur_player(p);

    /* ---- the spot ---- */
    hasTile = bp->stan != NULL;
    inTile = hasTile && isPointInsideTriStandTileUnscaled_Maybe((StandTile *) bp->stan, bp->pos.x, bp->pos.z);
    floorY = hasTile ? bondviewYPositionRelated((StandTile *) bp->stan, bp->pos.x, bp->pos.z) : 0.0f;
    foot = bp->pos.y - pl->eyeheight;
    wf = bgGetLevelFloorY();   /* room-bounds space: world * room_data_float1 */
    safe = portCoopDropSpotSafe(hasTile, inTile, foot, floorY, wf != BG_LEVEL_FLOOR_NONE && get_room_data_float1() > 0.0f,
                                get_room_data_float1() > 0.0f ? wf / get_room_data_float1() - 200.0f : 0.0f, forceUnsafe());

    /* ---- gather, in inventory order ---- */
    for (i = 0; i < 30; i++) {
        pool[i] = (int) pl->ammoheldarr[i];
    }
    for (i = 0; i < 2; i++) {
        ITEM_IDS w = pl->hands[i].weaponnum;
        int t = (w > ITEM_UNARMED) ? (int) get_ammo_type_for_weapon(w) : 0;

        if (t > 0 && t < 30) {
            pool[t] += (int) pl->hands[i].weapon_ammo_in_magazine;
        }
    }
    first = pl->ptr_inventory_first_in_cycle;
    it = first;
    while (it != NULL && guard++ < 64 && n < 62) {
        if (it->type == INV_ITEM_WEAPON) {
            int id = (int) it->type_inv_item.type_weap.weapon;

            if (!inStartSingles(snap, id)) {
                ent[n].kind = ((id < ITEM_BOMBCASE) && ((s32) getPropForHeldItem((ITEM_IDS) id) >= 0)) ? 0 : questItemModel(id) >= 0 ? 3 : 2;
                ent[n].id = id;
                ent[n].it = it;
                ent[n].prop = NULL;
                n++;
            }
        } else if (it->type == INV_ITEM_DUAL) {
            int r = (int) it->type_inv_item.type_dual.weapon_right;
            int l = (int) it->type_inv_item.type_dual.weapon_left;

            if (!inStartDuals(snap, r, l)) {
                int side;

                for (side = 0; side < 2; side++) {
                    int id = side == 0 ? r : l;

                    if (id > ITEM_UNARMED && !inStartSingles(snap, id) && n < 62) {
                        ent[n].kind = ((id < ITEM_BOMBCASE) && ((s32) getPropForHeldItem((ITEM_IDS) id) >= 0)) ? 0 : 2;
                        ent[n].id = id;
                        ent[n].it = side == 0 ? it : NULL;   /* the dual entry is removed once */
                        ent[n].prop = NULL;
                        n++;
                    }
                }
            }
        } else if (it->type == INV_ITEM_PROP) {
            ent[n].kind = 1;
            ent[n].id = -1;
            ent[n].it = it;
            ent[n].prop = it->type_inv_item.type_prop.prop;
            n++;
        }
        it = it->next;
        if (it == first) {
            break;
        }
    }
    for (i = 0; i < n; i++) {
        ent[i].hand = -1;
        if (ent[i].kind == 0) {
            if (pl->hands[0].weaponnum == (ITEM_IDS) ent[i].id) {
                ent[i].hand = 0;
            } else if (pl->hands[1].weaponnum == (ITEM_IDS) ent[i].id) {
                ent[i].hand = 1;
            }
        }
    }

    portCoopFpLog("ge: coop: drops p%d: %d item(s) to place, spot %.0f %.0f %.0f %s (floor %.0f), partner %d, armour %.2f lost\n", p, n,
                  bp->pos.x, bp->pos.y, bp->pos.z, safe ? "safe" : "UNSAFE", floorY, partner, pl->bondarmour);
    pl->bondarmour = 0.0f;

    /* ---- act, in the same order: take every entry out of the ring first, then place it ---- */
    for (i = 0; i < n; i++) {
        if (ent[i].it != NULL) {
            bondinvRemoveItem(ent[i].it);
        }
    }
    for (i = 0; i < n; i++) {
        float x, z, fl;
        StandTile *tile;

        if (ent[i].kind == 0) {            /* a picked-up gun: at the spot with its magazine, reserve ammo lost */
            WeaponStats *ws = get_ptr_item_statistics((ITEM_IDS) ent[i].id);
            int type = (int) get_ammo_type_for_weapon((ITEM_IDS) ent[i].id);
            int qty = 0;
            WeaponObjRecord *w;

            if (!safe) {
                portCoopFpLog("ge: coop: drops p%d: gun %d lost (unsafe spot)\n", p, ent[i].id);
                continue;
            }
            if (type > 0 && type < 30) {
                qty = portCoopDropMagazine(ent[i].hand >= 0, ent[i].hand >= 0 ? (int) pl->hands[ent[i].hand].weapon_ammo_in_magazine : 0,
                                           ws != NULL ? (int) ws->MagSize : 0, pool[type]);
                pool[type] -= qty;
            }
            spotFor(bp, k, &x, &z, &tile, &fl);
            w = (WeaponObjRecord *) create_new_item_instance_of_model(getPropForHeldItem((ITEM_IDS) ent[i].id), ent[i].id);
            if (w == NULL || w->prop == NULL) {
                portCoopFpLog("ge: coop: drops p%d: gun %d could not be created\n", p, ent[i].id);
                continue;
            }
            placeObject((ObjectRecord *) w, NULL, x, fl + 30.0f, z, tile);
            sAmmo[sAmmoNext].obj = (ObjectRecord *) w;
            sAmmo[sAmmoNext].qty = qty;
            sAmmoNext = (sAmmoNext + 1) % DROP_TABLE;
            portCoopFpLog("ge: coop: drops p%d: gun %d dropped with %d round(s) at %.0f %.0f %.0f\n", p, ent[i].id, qty, x, fl + 30.0f, z);
            k++;
        } else if (ent[i].kind == 1 && ent[i].prop != NULL) {    /* a quest item: at the spot, else to the partner */
            ObjectRecord *obj = ent[i].prop->obj;
            int typ = obj != NULL ? (int) obj->type : -1;

            objDetach(ent[i].prop);
            if (safe) {
                spotFor(bp, k, &x, &z, &tile, &fl);
                placeObject(obj, NULL, x, fl + 15.0f, z, tile);
                portCoopFpLog("ge: coop: drops p%d: quest item (type %d) dropped at %.0f %.0f %.0f\n", p, typ, x, fl + 15.0f, z);
                k++;
            } else if (partner >= 0 && g_playerPointers[partner] != NULL && g_playerPointers[partner]->prop != NULL) {
                set_cur_player(partner);
                chrpropReparent(ent[i].prop, getCurrentPlayerProp());
                bondinvAddPropToInv(ent[i].prop);
                set_cur_player(p);
                portCoopFpLog("ge: coop: drops p%d: quest item (type %d) to player %d (unsafe spot)\n", p, typ, partner);
            } else {
                portCoopFpLog("ge: coop: drops p%d: quest item (type %d) lost (unsafe spot, no partner)\n", p, typ);
            }
        } else if (ent[i].kind == 3) {                  /* an objective item of the gadget ring: at the spot, else to the partner (unsafe spot) */
            WeaponObjRecord *w = NULL;

            if (safe) {
                spotFor(bp, k, &x, &z, &tile, &fl);
                w = (WeaponObjRecord *) create_new_item_instance_of_model((PROP) questItemModel(ent[i].id), ent[i].id);
                if (w != NULL && w->prop != NULL) {
                    placeObject((ObjectRecord *) w, NULL, x, fl + 15.0f, z, tile);
                    portCoopFpLog("ge: coop: drops p%d: objective item %d dropped at %.0f %.0f %.0f\n", p, ent[i].id, x, fl + 15.0f, z);
                    k++;
                    continue;
                }
                portCoopFpLog("ge: coop: drops p%d: objective item %d could not be created, to the partner\n", p, ent[i].id);
            }
            if (partner >= 0 && g_playerPointers[partner] != NULL) {
                set_cur_player(partner);
                bondinvAddInvItem((ITEM_IDS) ent[i].id);
                set_cur_player(p);
                portCoopFpLog("ge: coop: drops p%d: objective item %d to player %d (unsafe spot)\n", p, ent[i].id, partner);
            } else {
                portCoopFpLog("ge: coop: drops p%d: objective item %d lost (no partner)\n", p, ent[i].id);
            }
        } else if (ent[i].kind == 2) {                  /* a gadget: no world model, so straight to the partner */
            if (partner >= 0 && g_playerPointers[partner] != NULL) {
                set_cur_player(partner);
                bondinvAddInvItem((ITEM_IDS) ent[i].id);
                set_cur_player(p);
                portCoopFpLog("ge: coop: drops p%d: gadget %d to player %d\n", p, ent[i].id, partner);
            } else {
                portCoopFpLog("ge: coop: drops p%d: gadget %d lost (no partner)\n", p, ent[i].id);
            }
        }
    }
    set_cur_player(saved);
}

/* ---- test hooks (env, all off by default) ----------------------------------------------------------------------- */

extern void give_cur_player_ammo(s32 ammo_type, s32 ammo_amount);

/* Netplay job 5 test hooks (both env, off by default; sim-tick keyed, so deterministic on both machines):
 * GE_COOP_MOVETOCHR=<tick>:<player>:<chrnum>[,...]  at that sim tick the Bond is moved onto the walkable spot beside that character (or back
 *     beside Bond 0 with chrnum -1), far from the other Bond: the guest's "ran off to another room" of playtest 7
 * GE_COOP_VISLOG=<every>   every that many sim ticks, for every live arena guard (chr 7000+): its room, and for each Bond whether that room is in the
 *     rooms the Bond's CANONICAL sim view drew (bgSimRoomsForPlayer) and the distance: the line the visibility test checks */
extern s32 bgSimRoomsForPlayer(s32 pl, s32 *rooms, s32 max);
extern f32 bondviewYPositionRelated(struct StandTile *arg0, f32 arg1, f32 arg2);

static void portCoopVisTestTick(unsigned tick)
{
    static const char *mvc, *vis;
    static int init, every;
    const char *s;

    if (!init) {
        init = 1;
        mvc = getenv("GE_COOP_MOVETOCHR");
        vis = getenv("GE_COOP_VISLOG");
        every = (vis != NULL && atoi(vis) > 0) ? atoi(vis) : 0;
    }
    for (s = mvc; s != NULL && *s != '\0';) {
        unsigned t = 0;
        int pl = 0, cn = 0, used = 0, onto = 0;

        if (sscanf(s, "%u:%d:%d%n", &t, &pl, &cn, &used) < 3 || used <= 0) {
            break;
        }
        if (s[used] == ':') {   /* optional 4th field ":1": stand ON the character's own spot (a hostage's use press then needs no facing) */
            int more = 0;

            if (sscanf(s + used + 1, "%d%n", &onto, &more) >= 1 && more > 0) {
                used += 1 + more;
            }
        }
        if (t == tick && pl >= 0 && pl < 4 && g_playerPointers[pl] != NULL) {
            PropRecord *op = NULL;
            ChrRecord *c = cn >= 0 ? chrFindByLiteralId(cn) : NULL;

            if (cn < 0) {
                op = g_playerPointers[0]->prop;
            } else if (c != NULL && c->prop != NULL) {
                op = c->prop;
            }
            if (op != NULL && op->stan != NULL) {
                static const float cand[5][2] = { {80.0f, 0.0f}, {-80.0f, 0.0f}, {0.0f, 80.0f}, {0.0f, -80.0f}, {0.0f, 0.0f} };
                StandTile *tile = (StandTile *) op->stan;
                coord3d to;
                s32 saved = get_cur_playernum();
                float h;
                int k;

                to.x = op->pos.x;
                to.z = op->pos.z;
                for (k = onto ? 4 : 0; k < 5; k++) {
                    StandTile *walk = (StandTile *) op->stan;
                    float f0 = bondviewYPositionRelated((StandTile *) op->stan, op->pos.x, op->pos.z);

                    walkTilesBetweenPoints_NoCallback(&walk, op->pos.x, op->pos.z, op->pos.x + cand[k][0], op->pos.z + cand[k][1]);
                    if (walk != NULL && isPointInsideTriStandTileUnscaled_Maybe(walk, op->pos.x + cand[k][0], op->pos.z + cand[k][1])) {
                        float hh = bondviewYPositionRelated(walk, op->pos.x + cand[k][0], op->pos.z + cand[k][1]);

                        if (hh - f0 <= 60.0f && f0 - hh <= 60.0f) {
                            to.x = op->pos.x + cand[k][0];
                            to.z = op->pos.z + cand[k][1];
                            tile = walk;
                            break;
                        }
                    }
                }
                h = bondviewYPositionRelated(tile, to.x, to.z);
                set_cur_player(pl);
                to.y = g_CurrentPlayer->eyeheight + h;
                g_CurrentPlayer->field_70 = h;
                g_CurrentPlayer->stanHeight = h;
                change_player_pos_to_target(&g_CurrentPlayer->field_488, &to, tile);
                g_CurrentPlayer->prop->pos.x = g_CurrentPlayer->bondprevpos.x = to.x;
                g_CurrentPlayer->prop->pos.y = g_CurrentPlayer->bondprevpos.y = to.y;
                g_CurrentPlayer->prop->pos.z = g_CurrentPlayer->bondprevpos.z = to.z;
                g_CurrentPlayer->prop->stan = tile;
                set_cur_player(saved);
                portCoopFpLog("ge: coop: test: moved player %d beside chr %d at tick %u to %.0f %.0f\n", pl, cn, tick, to.x, to.z);
            }
        }
        s += used;
        if (*s == ',') {
            s++;
        }
    }
    {   /* GE_COOP_MOVETOPAD=<tick>:<player>:<pad>[,...] (mission-end job): at that sim tick the Bond is put on the setup pad (e.g. 330 = Dam's
         * bungee-jump trigger room): a real level exit can be reached without walking there. Sim-tick keyed, the same on both machines. */
        static const char *mp;
        static int mpinit;

        if (!mpinit) {
            mpinit = 1;
            mp = getenv("GE_COOP_MOVETOPAD");
        }
        for (s = mp; s != NULL && *s != '\0';) {
            unsigned t = 0;
            int pl = 0, pad = 0, used = 0;

            if (sscanf(s, "%u:%d:%d%n", &t, &pl, &pad, &used) < 3 || used <= 0) {
                break;
            }
            if (t == tick && pl >= 0 && pl < 4 && g_playerPointers[pl] != NULL && pad >= 0 && pad < 10000) {
                PadRecord *pd = &g_CurrentSetup.pads[pad];

                if (pd->stan != NULL) {
                    StandTile *tile = (StandTile *) pd->stan;
                    coord3d to;
                    s32 saved = get_cur_playernum();
                    float h = bondviewYPositionRelated(tile, pd->pos.x, pd->pos.z);

                    to.x = pd->pos.x;
                    to.z = pd->pos.z;
                    set_cur_player(pl);
                    to.y = g_CurrentPlayer->eyeheight + h;
                    g_CurrentPlayer->field_70 = h;
                    g_CurrentPlayer->stanHeight = h;
                    change_player_pos_to_target(&g_CurrentPlayer->field_488, &to, tile);
                    g_CurrentPlayer->prop->pos.x = g_CurrentPlayer->bondprevpos.x = to.x;
                    g_CurrentPlayer->prop->pos.y = g_CurrentPlayer->bondprevpos.y = to.y;
                    g_CurrentPlayer->prop->pos.z = g_CurrentPlayer->bondprevpos.z = to.z;
                    g_CurrentPlayer->prop->stan = tile;
                    set_cur_player(saved);
                    portCoopFpLog("ge: coop: test: moved player %d onto pad %d at tick %u to %.0f %.0f %.0f\n", pl, pad, tick, to.x, to.y, to.z);
                }
            }
            s += used;
            if (*s == ',') {
                s++;
            }
        }
    }
    if (every > 0 && tick % (unsigned) every == 0 && g_playerPointers[0] != NULL && g_playerPointers[1] != NULL) {
        s32 i;

        for (i = 0; i < g_NumChrSlots; i++) {
            ChrRecord *c = &g_ChrSlots[i];
            s32 rooms[256];
            int p;

            if (c->model == NULL || c->prop == NULL || c->chrnum < 7000 || chrIsDead(c)) {
                continue;
            }
            portCoopFpLog("ge: vis: tick %u chr %d room %d", tick, (int) c->chrnum, (int) c->prop->rooms[0]);
            for (p = 0; p < 2; p++) {
                s32 n = bgSimRoomsForPlayer(p, rooms, 256), k, in = 0;
                float dx = g_playerPointers[p]->prop->pos.x - c->prop->pos.x, dz = g_playerPointers[p]->prop->pos.z - c->prop->pos.z;

                for (k = 0; k < n; k++) {
                    in |= rooms[k] == (s32) c->prop->rooms[0];
                }
                portCoopFpLog(" p%d in %d of %d dist %.0f", p, in, (int) n, __builtin_sqrtf(dx * dx + dz * dz));
            }
            portCoopFpLog("\n");
        }
    }
}

/* GE_COOP_GIVEWEAPON=<tick>:<player>:<item>[:<ammo>][,...]  at that sim tick the Bond gets the weapon (as if picked up) and `ammo` rounds
 * GE_COOP_MOVETO=<tick>:<player>:<other>[:1][,...]         at that sim tick the Bond is moved 60 units beside the other Bond (:1 = onto its spot)
 * (the real pickup code then runs for it, so a dropped item can be picked up again without a walking script) */
void portCoopDropTestTick(unsigned tick)
{
    static const char *give, *mv;
    static int init;
    const char *s;

    if (!init) {
        init = 1;
        give = getenv("GE_COOP_GIVEWEAPON");
        mv = getenv("GE_COOP_MOVETO");
    }
    for (s = give; s != NULL && *s != '\0';) {
        unsigned t = 0;
        int pl = 0, item = 0, ammo = -1, used = 0;

        if (sscanf(s, "%u:%d:%d:%d%n", &t, &pl, &item, &ammo, &used) < 3) {
            break;
        }
        if (used <= 0) {
            sscanf(s, "%u:%d:%d%n", &t, &pl, &item, &used);
        }
        if (t == tick && pl >= 0 && pl < 4 && g_playerPointers[pl] != NULL) {
            s32 saved = get_cur_playernum();
            int type = (int) get_ammo_type_for_weapon((ITEM_IDS) item);

            set_cur_player(pl);
            bondinvAddInvItem((ITEM_IDS) item);
            if (type > 0 && ammo >= 0) {
                give_cur_player_ammo(type, ammo);
            }
            set_cur_player(saved);
            portCoopFpLog("ge: coop: test: gave player %d weapon %d (ammo type %d, %d rounds) at tick %u\n", pl, item, type, ammo, tick);
        }
        s += used;
        if (*s == ',') {
            s++;
        }
    }
    {   /* GE_COOP_CYCLE=<tick>:<player>:<count>:<gap>  from that tick the Bond cycles to the next weapon every `gap` ticks, `count` times,
         * and logs what it holds after each (proves cycling reaches every weapon of the inventory) */
        static const char *cyc;
        static int cinit;

        if (!cinit) {
            cinit = 1;
            cyc = getenv("GE_COOP_CYCLE");
        }
        if (cyc != NULL) {
            unsigned t0 = 0;
            int pl = 0, count = 0, gap = 20;

            if (sscanf(cyc, "%u:%d:%d:%d", &t0, &pl, &count, &gap) >= 3 && gap > 0 && tick >= t0 && (tick - t0) % (unsigned) gap == 0
                && (int) ((tick - t0) / (unsigned) gap) < count && g_playerPointers[pl] != NULL) {
                extern void advance_through_inventory(void);
                s32 saved = get_cur_playernum();

                set_cur_player(pl);
                advance_through_inventory();
                portCoopFpLog("ge: coop: test: player %d cycled (#%d) at tick %u: holds %d/%d cur %d\n", pl, (int) ((tick - t0) / (unsigned) gap) + 1, tick,
                              (int) g_CurrentPlayer->hands[0].weaponnum, (int) g_CurrentPlayer->hands[1].weaponnum, (int) g_CurrentPlayer->equipcuritem);
                set_cur_player(saved);
            }
        }
    }
    {   /* the line after the one above is logged once the switch animation has run */
        static const char *cyc2;
        static int c2init;

        if (!c2init) {
            c2init = 1;
            cyc2 = getenv("GE_COOP_CYCLE");
        }
        if (cyc2 != NULL) {
            unsigned t0 = 0;
            int pl = 0, count = 0, gap = 20;

            if (sscanf(cyc2, "%u:%d:%d:%d", &t0, &pl, &count, &gap) >= 3 && gap > 0 && tick >= t0 + 45 && (tick - t0 - 45) % (unsigned) gap == 0
                && (int) ((tick - t0 - 45) / (unsigned) gap) < count && g_playerPointers[pl] != NULL) {
                portCoopFpLog("ge: coop: test: player %d after cycle #%d (tick %u): holds %d/%d\n", pl, (int) ((tick - t0 - 45) / (unsigned) gap) + 1, tick,
                              (int) g_playerPointers[pl]->hands[0].weaponnum, (int) g_playerPointers[pl]->hands[1].weaponnum);
            }
        }
    }
    portCoopVisTestTick(tick);
    for (s = mv; s != NULL && *s != '\0';) {
        unsigned t = 0;
        int pl = 0, ot = 0, used = 0, exact = 0, used2 = 0;

        if (sscanf(s, "%u:%d:%d%n", &t, &pl, &ot, &used) < 3 || used <= 0) {
            break;
        }
        if (s[used] == ':' && sscanf(s + used + 1, "%d%n", &exact, &used2) >= 1) {   /* :1 = onto the other Bond's own spot, not beside it */
            used += 1 + used2;
        }
        if (t == tick && pl != ot && pl >= 0 && pl < 4 && ot >= 0 && ot < 4 && g_playerPointers[pl] != NULL && g_playerPointers[ot] != NULL) {
            s32 saved = get_cur_playernum();
            PropRecord *op = g_playerPointers[ot]->prop;
            coord3d to;
            float h;

            static const float cand[5][2] = { {60.0f, 0.0f}, {-60.0f, 0.0f}, {0.0f, 60.0f}, {0.0f, -60.0f}, {0.0f, 0.0f} };
            StandTile *tile = (StandTile *) op->stan;
            int c;

            to.x = op->pos.x;
            to.z = op->pos.z;
            for (c = exact ? 4 : 0; c < 5; c++) {   /* the first spot beside the other Bond that is a walkable tile at about the same floor */
                StandTile *walk = (StandTile *) op->stan;
                float f0 = bondviewYPositionRelated((StandTile *) op->stan, op->pos.x, op->pos.z);

                walkTilesBetweenPoints_NoCallback(&walk, op->pos.x, op->pos.z, op->pos.x + cand[c][0], op->pos.z + cand[c][1]);
                if (walk != NULL && isPointInsideTriStandTileUnscaled_Maybe(walk, op->pos.x + cand[c][0], op->pos.z + cand[c][1])) {
                    float hh = bondviewYPositionRelated(walk, op->pos.x + cand[c][0], op->pos.z + cand[c][1]);

                    if (hh - f0 <= 60.0f && f0 - hh <= 60.0f) {
                        to.x = op->pos.x + cand[c][0];
                        to.z = op->pos.z + cand[c][1];
                        tile = walk;
                        break;
                    }
                }
            }
            h = bondviewYPositionRelated(tile, to.x, to.z);
            set_cur_player(pl);
            to.y = g_CurrentPlayer->eyeheight + h;
            g_CurrentPlayer->field_70 = h;
            g_CurrentPlayer->stanHeight = h;
            change_player_pos_to_target(&g_CurrentPlayer->field_488, &to, tile);
            g_CurrentPlayer->prop->pos.x = g_CurrentPlayer->bondprevpos.x = to.x;
            g_CurrentPlayer->prop->pos.y = g_CurrentPlayer->bondprevpos.y = to.y;
            g_CurrentPlayer->prop->pos.z = g_CurrentPlayer->bondprevpos.z = to.z;
            g_CurrentPlayer->prop->stan = tile;
            set_cur_player(saved);
            portCoopFpLog("ge: coop: test: moved player %d beside player %d at tick %u\n", pl, ot, tick);
        }
        s += used;
        if (*s == ',') {
            s++;
        }
    }
}

/* ---- test hook: GE_COOP_INVLOG=1 ------------------------------------------------------------------------------- */

static int invLogOn(void)
{
    static int on = -1;

    if (on < 0) {
        on = getenv("GE_COOP_INVLOG") != NULL;
    }
    return on;
}

/* One line per Bond: the inventory ring in order (W weapon id, D dual right/left, P prop with its object type), the held
 * weapons, magazines and the reserve ammo. */
void portCoopInvLog(const char *tag, int p)
{
    struct player *pl = (p >= 0 && p < 4) ? g_playerPointers[p] : NULL;
    InvItem *first;
    InvItem *it;
    char buf[640];
    int len = 0;
    int guard = 0;
    int a;

    if (!invLogOn() || pl == NULL) {
        return;
    }
    first = pl->ptr_inventory_first_in_cycle;
    it = first;
    buf[0] = '\0';
    while (it != NULL && guard++ < 64) {
        if (it->type == INV_ITEM_WEAPON) {
            len += snprintf(buf + len, sizeof(buf) - len, " W%d", (int) it->type_inv_item.type_weap.weapon);
        } else if (it->type == INV_ITEM_DUAL) {
            len += snprintf(buf + len, sizeof(buf) - len, " D%d/%d", (int) it->type_inv_item.type_dual.weapon_right,
                            (int) it->type_inv_item.type_dual.weapon_left);
        } else if (it->type == INV_ITEM_PROP) {
            PropRecord *pr = it->type_inv_item.type_prop.prop;
            ObjectRecord *ob = pr != NULL ? pr->obj : NULL;

            len += snprintf(buf + len, sizeof(buf) - len, " P(t%d)", ob != NULL ? (int) ob->type : -1);
        } else {
            len += snprintf(buf + len, sizeof(buf) - len, " ?%d", (int) it->type);
        }
        if (len >= (int) sizeof(buf) - 24) {
            break;
        }
        it = it->next;
        if (it == first) {
            break;
        }
    }
    portCoopFpLog("ge: coop: inv %s p%d:%s | hands %d/%d mag %d/%d cur %d | ammo", tag, p, buf, (int) pl->hands[0].weaponnum,
                  (int) pl->hands[1].weaponnum, (int) pl->hands[0].weapon_ammo_in_magazine, (int) pl->hands[1].weapon_ammo_in_magazine,
                  (int) pl->equipcuritem);
    for (a = 0; a < 30; a++) {
        if (pl->ammoheldarr[a] != 0) {
            portCoopFpLog(" %d:%d", a, (int) pl->ammoheldarr[a]);
        }
    }
    portCoopFpLog(" armour %.1f | starting_weapon %d/%d | objectives", pl->bondarmour, (int) starting_weapon[GUNRIGHT], (int) starting_weapon[GUNLEFT]);
    for (a = 0; a < objectiveGetCount() && a < 10; a++) {
        portCoopFpLog(" %d:%d", a, (int) objectiveStatuses[a]);   /* 0 incomplete, 1 complete, 2 failed */
    }
    portCoopFpLog("\n");
}

#endif
