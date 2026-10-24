/* port_probe.c - read-only level probe for mode authors (Lua `probe.*`, modding/lua.md). Logs the stage's setup objects
 * (doors and their key flags, padlocks, cells, CCTV, alarm boxes, monitors, safes, keys, the guard records that carry them) and
 * the pad table with rooms, as "ge: probe: ..." lines. PC only, no simulation state is read or written beyond reading
 * g_CurrentSetup and the props the stock setup created. Used by tools/mode_probe.lua; designs/bank-heist-bunker2.md has the output. */
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
#include "game/cheat.h"
#include "game/bondview.h"
#include "port_blocks.h"
#include "port_mode.h"

extern double atan2(double y, double x);   /* libm (the game's math.h shadows it) */

#define PROBE_MAX 6000
#define ROOMS 256
#define RAD2DEG (180.0f / 3.14159265f)

static PropDefHeaderRecord *sRec[PROBE_MAX];
static int sRoomCnt[ROOMS][6];   /* door, cctv, monitor, alarm, safe, pad */

static const char *kRoomCol[6] = { "doors", "cctv", "monitors", "alarms", "safes", "pads" };

static int probeIndexOf(const void *p)
{
    int i;

    for (i = 0; i < PROBE_MAX && sRec[i] != NULL; i++)
    {
        if ((const void *) sRec[i] == p)
        {
            return i;
        }
    }
    return -1;
}

/* where the object sits: its prop if the stock setup made one (world position, rooms), else its pad */
static int probeWhere(const ObjectRecord *o, coord3d *pos, int rooms[8])
{
    int i, n = 0;

    rooms[0] = -1;
    if (o->prop != NULL)
    {
        *pos = o->prop->pos;
        for (i = 0; i < 8 && o->prop->rooms[i] != 0xFF; i++)
        {
            rooms[n++] = o->prop->rooms[i];
        }
        if (n < 8) rooms[n] = -1;
        return 1;
    }
    memset(pos, 0, sizeof(*pos));
    if (o->pad >= 0 && o->pad < 10000 && o->pad < portPadCount())
    {
        const PadRecord *p = &g_CurrentSetup.pads[o->pad];

        *pos = p->pos;
        if (p->stan != NULL)
        {
            rooms[0] = (int) p->stan->room;
            rooms[1] = -1;
        }
    }
    return 0;
}

static void probeRoomStr(const int rooms[8], char *out, int cap)
{
    int i, n = 0;

    out[0] = '\0';
    for (i = 0; i < 8 && rooms[i] >= 0 && n < cap - 6; i++)
    {
        n += snprintf(out + n, cap - n, "%s%d", i ? "/" : "", rooms[i]);
    }
    if (n == 0) snprintf(out, cap, "-");
}

static void probeCount(const int rooms[8], int col)
{
    if (rooms[0] >= 0 && rooms[0] < ROOMS) sRoomCnt[rooms[0]][col]++;
}

void portProbeLevel(void)
{
    PropDefHeaderRecord *ph;
    int n = 0, idx, i, live[PROPDEF_MAX + 1], total[PROPDEF_MAX + 1];

    memset(sRec, 0, sizeof(sRec));
    memset(sRoomCnt, 0, sizeof(sRoomCnt));
    memset(live, 0, sizeof(live));
    memset(total, 0, sizeof(total));
    if (!portModeActive() || g_CurrentSetup.propDefs == NULL)
    {
        printf("ge: probe: no mode / no setup (mode=%d propDefs=%p)\n", portModeActive(), (void *) g_CurrentSetup.propDefs);
        fflush(stdout);
        return;
    }
    for (ph = g_CurrentSetup.propDefs; ph->type != PROPDEF_END && n < PROBE_MAX - 1; ph = (PropDefHeaderRecord *) (((u32 *) ph) + sizepropdef(ph)))
    {
        sRec[n++] = ph;
    }
    printf("ge: probe: level propdefs=%d pads=%d\n", n, (int) portPadCount());

    for (idx = 0; idx < n; idx++)
    {
        PropDefHeaderRecord *h = sRec[idx];
        ObjectRecord *o = (ObjectRecord *) h;
        coord3d pos;
        int rooms[8];
        char rs[40];
        int t = h->type;

        if (t < 0 || t > PROPDEF_MAX) continue;
        total[t]++;
        switch (t)
        {
            case PROPDEF_DOOR:
            {
                DoorRecord *d = (DoorRecord *) h;
                int linked = -1;
                const char *kind = "";

                if (probeWhere(o, &pos, rooms)) live[t]++;
                probeRoomStr(rooms, rs, sizeof(rs));
                probeCount(rooms, 0);
                if (d->linkedDoor != NULL) linked = probeIndexOf(d->linkedDoor);
                if (o->obj == PROP_DOORPRISON1) kind = " CELL";
                else if (o->obj == PROP_DAMCHAINDOOR) kind = " GATE";
                printf("ge: probe: DOOR idx=%d model=%d pad=%d pos=%.0f,%.0f,%.0f rooms=%s key=0x%x type=%d dflags=0x%x flags=0x%x flags2=0x%x "
                       "state=%d padlocked=%d linked=%d autoclose=%u%s\n",
                       idx, o->obj, o->pad, pos.x, pos.y, pos.z, rs, (unsigned) d->keyflags, (int) d->doorType, (unsigned) d->doorFlags,
                       (unsigned) o->flags, (unsigned) o->flags2, (int) d->openstate,
                       (o->runtime_bitflags & RUNTIMEBITFLAG_PADLOCKEDDOOR) ? 1 : 0, linked, (unsigned) d->autoCloseFrames, kind);
                break;
            }
            case PROPDEF_LOCK_DOOR:
            {
                LockDoorRecord *l = (LockDoorRecord *) h;
                int di = probeIndexOf(l->door), li = probeIndexOf(l->lock);

                printf("ge: probe: LOCKDOOR idx=%d door=%d lock=%d (raw=%d/%d)\n", idx, di, li, (int) l->Index1, (int) l->Index2);
                break;
            }
            case PROPDEF_CCTV:
            {
                CCTVRecord *c = (CCTVRecord *) h;

                if (probeWhere(o, &pos, rooms)) live[t]++;
                probeRoomStr(rooms, rs, sizeof(rs));
                probeCount(rooms, 1);
                /* unkC4 = base yaw (toward the look pad), unkCC / unkD0 = sweep ends (relative), unkE8 = range (0 = unlimited);
                 * the stock test is +-45 degrees about (base + sweep) and CCTV_ALARM_FRAMES of unbroken sight */
                {
                    /* unkC4 is the yaw from the look pad to the camera; the cone test wants the player on the opposite side, so the camera
                     * faces unkC4 + 180 (degrees clockwise from +Z toward +X, atan2(x, z)) */
                    float face = c->unkC4 * RAD2DEG + 180.0f;

                    if (face >= 360.0f) face -= 360.0f;
                    printf("ge: probe: CCTV idx=%d model=%d pad=%d lookpad=%d pos=%.0f,%.0f,%.0f rooms=%s face=%.0f sweep=%.0f..%.0f range=%.0f flags=0x%x flags2=0x%x live=%d\n",
                           idx, o->obj, o->pad, c->lookpad, pos.x, pos.y, pos.z, rs, face, c->unkCC * RAD2DEG, c->unkD0 * RAD2DEG,
                           c->unkE8, (unsigned) o->flags, (unsigned) o->flags2, o->prop != NULL);
                }
                break;
            }
            case PROPDEF_ALARM:
                if (probeWhere(o, &pos, rooms)) live[t]++;
                probeRoomStr(rooms, rs, sizeof(rs));
                probeCount(rooms, 3);
                printf("ge: probe: ALARM idx=%d model=%d pad=%d pos=%.0f,%.0f,%.0f rooms=%s live=%d\n", idx, o->obj, o->pad, pos.x, pos.y, pos.z, rs, o->prop != NULL);
                break;
            case PROPDEF_MONITOR:
            case PROPDEF_MULTI_MONITOR:
            case PROPDEF_RACK:
                if (probeWhere(o, &pos, rooms)) live[t]++;
                probeRoomStr(rooms, rs, sizeof(rs));
                probeCount(rooms, 2);
                printf("ge: probe: %s idx=%d model=%d pad=%d pos=%.0f,%.0f,%.0f rooms=%s live=%d\n",
                       t == PROPDEF_MONITOR ? "MONITOR" : (t == PROPDEF_MULTI_MONITOR ? "MULTIMON" : "RACK"),
                       idx, o->obj, o->pad, pos.x, pos.y, pos.z, rs, o->prop != NULL);
                break;
            case PROPDEF_SAFE:
                if (probeWhere(o, &pos, rooms)) live[t]++;
                probeRoomStr(rooms, rs, sizeof(rs));
                probeCount(rooms, 4);
                printf("ge: probe: SAFE idx=%d model=%d pad=%d pos=%.0f,%.0f,%.0f rooms=%s live=%d\n", idx, o->obj, o->pad, pos.x, pos.y, pos.z, rs, o->prop != NULL);
                break;
            case PROPDEF_SAFE_ITEM:
            {
                SafeObjectRecord *s = (SafeObjectRecord *) h;
                int ii = probeIndexOf(s->item), si = probeIndexOf(s->safe), di = probeIndexOf(s->door);
                ObjectRecord *item = s->item;

                printf("ge: probe: SAFEITEM idx=%d item=%d(type %d model %d) safe=%d door=%d (raw=%d/%d/%d)\n", idx, ii,
                       (ii >= 0 && item != NULL) ? (int) item->type : -1, (ii >= 0 && item != NULL) ? (int) item->obj : -1, si, di,
                       (int) s->Index1, (int) s->Index2, (int) s->Index3);
                break;
            }
            case PROPDEF_KEY:
            {
                KeyRecord *k = (KeyRecord *) h;
                int assigned = (o->flags & PROPFLAG_ASSIGNEDTOCHR) != 0;

                if (probeWhere(o, &pos, rooms)) live[t]++;
                probeRoomStr(rooms, rs, sizeof(rs));
                printf("ge: probe: KEY idx=%d model=%d keyflags=0x%x %s=%d flags=0x%x live=%d\n", idx, o->obj, (unsigned) k->keyflags,
                       assigned ? "chr" : "pad", o->pad, (unsigned) o->flags, o->prop != NULL);
                if (!assigned)
                {
                    printf("ge: probe:   key at pos=%.0f,%.0f,%.0f rooms=%s\n", pos.x, pos.y, pos.z, rs);
                }
                break;
            }
            case PROPDEF_COLLECTABLE:
            {
                WeaponObjRecord *w = (WeaponObjRecord *) h;
                int assigned = (o->flags & PROPFLAG_ASSIGNEDTOCHR) != 0;

                if (probeWhere(o, &pos, rooms)) live[t]++;
                probeRoomStr(rooms, rs, sizeof(rs));
                if (assigned)
                {   /* o->pad is a chrnum here, not a pad: no position */
                    printf("ge: probe: ITEM idx=%d model=%d weaponnum=%d chr=%d live=%d\n", idx, o->obj, (int) w->weaponnum, o->pad, o->prop != NULL);
                }
                else
                {
                    printf("ge: probe: ITEM idx=%d model=%d weaponnum=%d pad=%d pos=%.0f,%.0f,%.0f rooms=%s live=%d\n", idx, o->obj, (int) w->weaponnum,
                           o->pad, pos.x, pos.y, pos.z, rs, o->prop != NULL);
                }
                break;
            }
            case PROPDEF_PROP:
                if (probeWhere(o, &pos, rooms)) live[t]++;
                probeRoomStr(rooms, rs, sizeof(rs));
                printf("ge: probe: %s idx=%d model=%d pad=%d pos=%.0f,%.0f,%.0f rooms=%s live=%d\n", o->obj == PROP_PADLOCK ? "PADLOCK" : "PROP", idx, o->obj, o->pad,
                       pos.x, pos.y, pos.z, rs, o->prop != NULL);
                if (o->flags & PROPFLAG_ASSIGNEDTOCHR)
                {
                    printf("ge: probe: PROP-ON-CHR idx=%d model=%d chr=%d\n", idx, o->obj, o->pad);
                }
                break;
            case PROPDEF_GUARD:
            {
                GuardRecord *g = (GuardRecord *) h;
                coord3d gp;
                int gr = -1;

                memset(&gp, 0, sizeof(gp));
                if (g->PadID < portPadCount())
                {
                    const PadRecord *p = &g_CurrentSetup.pads[g->PadID];

                    gp = p->pos;
                    if (p->stan != NULL) gr = (int) p->stan->room;
                }
                printf("ge: probe: GUARD idx=%d chr=%d pad=%d pos=%.0f,%.0f,%.0f room=%d body=%d head=%d ailist=0x%x flags=0x%x health=%d\n", idx,
                       (int) g->chrnum, (int) g->PadID, gp.x, gp.y, gp.z, gr, (int) g->BodyID, (int) g->HeadID, (unsigned) g->AIListID,
                       (unsigned) g->bitflags, (int) g->health);
                break;
            }
            case PROPDEF_AUTOGUN:
            case PROPDEF_GLASS:
            case PROPDEF_AMMO:
            case PROPDEF_ARMOUR:
            case PROPDEF_MAGAZINE:
                if (probeWhere(o, &pos, rooms)) live[t]++;
                if (t == PROPDEF_AUTOGUN)
                {
                    probeRoomStr(rooms, rs, sizeof(rs));
                    printf("ge: probe: AUTOGUN idx=%d model=%d pos=%.0f,%.0f,%.0f rooms=%s live=%d\n", idx, o->obj, pos.x, pos.y, pos.z, rs, o->prop != NULL);
                }
                break;
            default:
                break;
        }
    }
    printf("ge: probe: SUMMARY (record count / created as a live prop)\n");
    for (i = 0; i <= PROPDEF_MAX; i++)
    {
        if (total[i] > 0)
        {
            printf("ge: probe:   type %d: %d records, %d live props\n", i, total[i], live[i]);
        }
    }
    fflush(stdout);
}

/* Live state of every CCTV: sight timer (ticks, alarm at 300), current sweep angle, distance and bearing offset to Bond 0. Read-only. */
void portProbeCctv(void)
{
    PropDefHeaderRecord *ph;
    int idx = 0;

    coord3d bp;

    if (!portModeActive() || g_CurrentSetup.propDefs == NULL || !portBondPos(&bp))
    {
        return;
    }
    for (ph = g_CurrentSetup.propDefs; ph->type != PROPDEF_END && idx < PROBE_MAX; ph = (PropDefHeaderRecord *) (((u32 *) ph) + sizepropdef(ph)), idx++)
    {
        if (ph->type == PROPDEF_CCTV)
        {
            CCTVRecord *c = (CCTVRecord *) ph;
            float dx = bp.x - c->runtime_pos.x, dy = bp.y - c->runtime_pos.y, dz = bp.z - c->runtime_pos.z;
            float a = (float) (atan2((double) dx, (double) dz) * 57.29578);

            float dpx = bp.x - c->prop->pos.x, dpz = bp.z - c->prop->pos.z;
            StandTile *tile = c->prop->stan;
            PropRecord *pp = getCurrentPlayerProp();
            int los = -9;

            if (pp != NULL)
            {   /* the same call and arguments as the stock camera tick (propobj.c, CCTV branch) */
                bondviewUpdateGuardTankFlagsRelated(pp, 0);
                los = (int) stanTestLineUnobstructed(&tile, c->prop->pos.x, c->prop->pos.z, pp->pos.x, pp->pos.z, 0x1B, 100.0f, 100.0f, 0.0f, 1.0f);
                bondviewUpdateGuardTankFlagsRelated(pp, 1);
            }
            printf("ge: probe:   cctv %d los=%d camstan=%p camroom=%d playerprop=%p\n", idx, los, (void *) c->prop->stan,
                   c->prop->stan ? (int) c->prop->stan->room : -1, (void *) pp);

            printf("ge: probe: CCTVSTATE idx=%d timer=%d sweep=%.0f dist=%.0f bearing=%.0f bond=%.0f,%.0f,%.0f runtime_pos=%.0f,%.0f,%.0f prop_pos=%.0f,%.0f,%.0f bearing_prop=%.0f\n",
                   idx, (int) c->timer, c->unkC8 * RAD2DEG, sqrtf(dx * dx + dy * dy + dz * dz), a, bp.x, bp.y, bp.z,
                   c->runtime_pos.x, c->runtime_pos.y, c->runtime_pos.z, c->prop->pos.x, c->prop->pos.y, c->prop->pos.z,
                   atan2((double) dpx, (double) dpz) * 57.29578);
        }
    }
    fflush(stdout);
}

int portProbeDoorsUnlocked(void)
{
    return cheatIsActive(CHEAT_PORT_UNLOCK_DOORS) ? 1 : 0;
}

/* the global alarm's siren timer in ticks (propobj.c alarm_timer): 0 = off */
int portProbeAlarm(void)
{
    extern s32 alarm_timer;

    return (int) alarm_timer;
}

void portProbePads(void)
{
    int i, cnt = (int) portPadCount(), wp[2048], nwp, k;
    char isWp[PROBE_MAX];

    if (!portModeActive())
    {
        printf("ge: probe: no mode\n");
        return;
    }
    memset(isWp, 0, sizeof(isWp));
    {
        coord3d o;

        memset(&o, 0, sizeof(o));
        nwp = (int) portPadList(&o, 0.0f, 1.0e9f, 1, wp, 2048);
        for (k = 0; k < nwp; k++)
        {
            if (wp[k] >= 0 && wp[k] < PROBE_MAX) isWp[wp[k]] = 1;
        }
    }
    printf("ge: probe: pads count=%d waypoint_pads=%d\n", cnt, nwp);
    for (i = 0; i < cnt; i++)
    {
        const PadRecord *p = &g_CurrentSetup.pads[i];
        coord3d pos;

        if (!portPadPos(i, &pos)) continue;
        if (p->stan != NULL && p->stan->room < ROOMS) sRoomCnt[p->stan->room][5]++;
        printf("ge: probe: PAD %d pos=%.0f,%.0f,%.0f room=%d wp=%d\n", i, pos.x, pos.y, pos.z, p->stan ? (int) p->stan->room : -1, i < PROBE_MAX ? isWp[i] : 0);
    }
    printf("ge: probe: ROOMS (room: ");
    for (i = 0; i < 6; i++) printf("%s%s", i ? " " : "", kRoomCol[i]);
    printf(")\n");
    for (i = 0; i < ROOMS; i++)
    {
        int c;

        for (c = 0, k = 0; c < 6; c++) k += sRoomCnt[i][c];
        if (k > 0)
        {
            printf("ge: probe: ROOM %d: %d %d %d %d %d %d\n", i, sRoomCnt[i][0], sRoomCnt[i][1], sRoomCnt[i][2], sRoomCnt[i][3], sRoomCnt[i][4], sRoomCnt[i][5]);
        }
    }
    fflush(stdout);
}

#endif /* TARGET_N64 */
