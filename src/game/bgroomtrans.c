/**
file bgroomtrans.c
*/

#include <ultra64.h>
#include "player.h"
#include "bg.h"
#include "bgroomtrans.h"
#include "matrixmath.h"

#ifndef TARGET_N64
#include <stdio.h>  /* GE_ROOM_AUDIT matrix-pool watch (see bg.c) */
#include <string.h> /* drawn-set change detection */
static s32 s_MtxPoolInitialized; /* scan is meaningless on BSS-zero arrays */
#endif

#ifdef VERSION_EU
#define AMT300 100
#else
#define AMT300 300
#endif

// bss
/**
 * EU .bss 8007DC90
*/
u8 roomStatusFlags[AMT300];

s32 roomIndices[AMT300]; //mtxbufferroom
s32 roomOwners[AMT300];
Mtx roomMatrices[AMT300];

#ifndef TARGET_N64
/* MP fix: each room matrix bakes in one player's view origin, so with split
 * screen a single per-room binding (field_36, unused on PC) got stolen
 * mid-frame and rooms drew displaced; PC keeps one binding per player, and each
 * slot records its owner. */
#define PORT_MTX_MAXROOMS 512
static s16 s_RoomMtxIdByPlayer[4][PORT_MTX_MAXROOMS];
static u8 s_RoomSlotPlayer[AMT300];
#define ROOM_MTXID(room) s_RoomMtxIdByPlayer[get_cur_playernum() & 3][room]
#else
#define ROOM_MTXID(room) g_BgRoomInfo[room].field_36
#endif


/**
 * Initialize room and player-related data structures.
 * Sets all rooms and players to an initial state.
 *
 * Address: 0x7F0BC530
 */
void initializeRoomData(void)
{
    int i;

    for (i=0; i<portAllPlayerCount(); i++)
    {
        g_playerPointers[i]->curRoomIndex = -1;
    }

    for (i=0; i<AMT300; i++)
    {
      roomIndices[i] = -1;
      roomStatusFlags[i] = 2;

      roomOwners[i] = -1;


    }

    for (i=0; i<getMaxNumRooms(); ++i)
    {
        g_BgRoomInfo[i].field_36 = -1;
    }

#ifndef TARGET_N64
    {
        s32 pl;
        for (pl = 0; pl < 4; pl++) {
            for (i = 0; i < PORT_MTX_MAXROOMS; i++) {
                s_RoomMtxIdByPlayer[pl][i] = -1;
            }
        }
        for (i = 0; i < AMT300; i++) {
            s_RoomSlotPlayer[i] = 0;
        }
    }
    s_MtxPoolInitialized = 1;
#endif
}


/**
 * Set the player's room field.
 *
 * Address: 0x7F0BC624
 */
void setPlayerRoomField(s32 roomIndex) {
  g_CurrentPlayer->curRoomIndex = roomIndex;
}


/**
 * Assigns a room index to a specific room ID.
 *
 * Address: 0x7F0BC634
 */
void assignRoomIndexToRoomID(int mtx,int room)
{
#ifdef DEBUG
    //check we are clear first before assignment
    assert(g_BgRoomInfo[room].mtxid == -1);
    assert(mtxbufferroom[mtx] == -1);
#endif

#ifndef TARGET_N64
    ROOM_MTXID(room) = (s16)mtx;
    s_RoomSlotPlayer[mtx] = (u8)(get_cur_playernum() & 3);
#else
    g_BgRoomInfo[room].field_36 = mtx;
#endif
    roomIndices[mtx] = room;
}


/**
 * Removes the room index assignment for a specific room ID.
 *
 * Address: 0x7F0BC660
 */
void removeRoomIndexFromRoomID(int mtx,int room)
{
#ifdef DEBUG
    // check the requested mtx is assigned before removing
    assert(g_BgRoomInfo[room].mtxid == mtx);
    assert(mtxbufferroom[mtx] == room);
#endif

#ifndef TARGET_N64
    /* clear the BINDER's map entry, not necessarily the current player's -
     * resetRoomState evicts slots bound by any player */
    s_RoomMtxIdByPlayer[s_RoomSlotPlayer[mtx]][room] = -1;
#else
    g_BgRoomInfo[room].field_36 = -1;
#endif
    roomIndices[mtx] = -1;
}


/**
 * Resets a room's state to its initial condition.
 *
 * Address: 0x7F0BC690
 */
void resetRoomState(int roomIndex)
{
    if (roomIndices[roomIndex] != -1) {
        removeRoomIndexFromRoomID(roomIndex,roomIndices[roomIndex]);
    }
    roomStatusFlags[roomIndex] = 2;
    roomOwners[roomIndex] = -1;
}


/**
 * Finds and returns the first available room index.
 * Returns 0 if no available room is found.
 *
 * Address: 0x7F0BC6F0
 */
s32 findAvailableRoomIndex(void)
{
    s32 i;

    for (i = 0; i<AMT300; i++)
    {
        if (((s32) roomStatusFlags[i] >= 2) && (roomOwners[i] == -1))
        {
            return i;
        }
    }

#ifndef TARGET_N64
    { /* GE_ROOM_AUDIT: the fallback below overwrites slot 0 without unbinding its old room,
       * which then renders with the wrong matrix (warped rooms); this reports it. */
        extern s32 portRoomAuditEnabled(void);
        if (portRoomAuditEnabled()) {
            fprintf(stderr,
                    "roomaudit: *** MTX POOL EXHAUSTED (all %d slots busy) - slot 0 (room %d) stomped\n",
                    AMT300, roomIndices[0]);
            fflush(stderr);
        }
    }
#endif

    return 0;
}


/**
 * Updates the status flags for rooms, resetting those that are inactive.
 *
 * NTSC address 0x7F0BC7D4.
 */
void updateRoomStatusFlags(void)
{
    s32 i;

    for(i = 0; i<AMT300; ++i)
    {
        if (roomOwners[i] > -1)
        {
            roomStatusFlags[i]++;

            if (roomStatusFlags[i] >= 2)
            {
                resetRoomState(i);
            }
        }
    }
}




/**
 * Manages room index allocation and matrix setup for a given room.
 *
 * NTSC address 0x7F0BC85C.
 */
s32 setupRoomTransformationMatrix(s32 room)
{
    s32 mtx;
    Mtxf roomTransformMatrix;

    mtx = ROOM_MTXID(room);//mtxid

#ifndef TARGET_N64
    /* the pool-exhaustion fallback in findAvailableRoomIndex stomps slot 0
     * without clearing the evicted tenant's binding; treat a binding whose
     * slot now serves another room as unbound (do NOT remove - the slot
     * belongs to its new tenant) */
    if (mtx != -1 && roomIndices[mtx] != room) {
        ROOM_MTXID(room) = -1;
        mtx = -1;
    }
#endif

    if ((mtx == -1) || (g_CurrentPlayer->curRoomIndex != roomOwners[mtx]))
    {
        if (mtx != -1)
        {
            removeRoomIndexFromRoomID(mtx, room);
        }

        mtx = findAvailableRoomIndex();
        assignRoomIndexToRoomID(mtx, room);

        roomStatusFlags[mtx] = 0;
#ifdef DEBUG
        assert(g_BgRoomInfo[room].mtxid == mtx);
       // assert(mtxbufferroom[mtx] == room);
#endif
    }
    else
    {
        roomStatusFlags[mtx] = 0;
        #ifdef DEBUG
        assert(g_BgRoomInfo[room].mtxid == mtx);
        //assert(mtxbufferroom[mtx] == room);
        #endif
        return mtx;
    }

    roomOwners[mtx] = g_CurrentPlayer->curRoomIndex;

    matrix_4x4_set_identity(&roomTransformMatrix);

    // set room size according to level scaling
    roomTransformMatrix.m[0][0] = room_data_float2;
    roomTransformMatrix.m[1][1] = room_data_float2;
    roomTransformMatrix.m[2][2] = room_data_float2;

    // room translation to position it relative to the player
    roomTransformMatrix.m[3][0] = (ptr_bgdata_room_fileposition_list[room].pos.f[0] * room_data_float2) - g_CurrentPlayer->current_model_pos.f[0];
    roomTransformMatrix.m[3][1] = (ptr_bgdata_room_fileposition_list[room].pos.f[1] * room_data_float2) - g_CurrentPlayer->current_model_pos.f[1];
    roomTransformMatrix.m[3][2] = (ptr_bgdata_room_fileposition_list[room].pos.f[2] * room_data_float2) - g_CurrentPlayer->current_model_pos.f[2];

    matrix_4x4_f32_to_s32(&roomTransformMatrix, &roomMatrices[mtx]);

    return mtx;
}


#ifndef TARGET_N64
/* GE_ROOM_AUDIT draw-time watch: records every room drawn this frame (logged by
 * portRoomMtxDrawnFlush when the set changes) and checks each room matrix at
 * draw time, to tell portal-culling faults from transform faults. */
static u8 s_DrawnRoomBits[64]; /* 512 rooms / 8 */

static void portRoomMtxCheckAtDraw(s32 roomID, s32 slot)
{
    extern s32 portRoomAuditEnabled(void);
    struct player *p;
    f32 ex, ey, ez, dx, dy, dz;
    Mtxf unpacked;
    static u32 lastLogFrameByRoom[64]; /* coarse rate limit, keyed room&63 */
    extern u32 portRoomAuditGetFrame(void);
    u32 frame;

    if (!portRoomAuditEnabled() || !s_MtxPoolInitialized) {
        return;
    }
    if (roomID > 0 && roomID < 512) {
        s_DrawnRoomBits[roomID >> 3] |= (u8)(1 << (roomID & 7));
    }

    /* MP: the DL being built belongs to g_CurrentPlayer - the matrix must
     * match THAT player's anchor (comparing vs player 0 false-positived on
     * every other viewport's draws) */
    p = g_CurrentPlayer;
    if (p == NULL || slot < 0 || slot >= AMT300) {
        return;
    }

    ex = (ptr_bgdata_room_fileposition_list[roomID].pos.f[0] * room_data_float2) - p->current_model_pos.f[0];
    ey = (ptr_bgdata_room_fileposition_list[roomID].pos.f[1] * room_data_float2) - p->current_model_pos.f[1];
    ez = (ptr_bgdata_room_fileposition_list[roomID].pos.f[2] * room_data_float2) - p->current_model_pos.f[2];

    sub_GAME_7F058E78((Mtxf *)&roomMatrices[slot], &unpacked);
    dx = unpacked.m[3][0] - ex;
    dy = unpacked.m[3][1] - ey;
    dz = unpacked.m[3][2] - ez;

    if (dx > 1.0f || dx < -1.0f || dy > 1.0f || dy < -1.0f || dz > 1.0f || dz < -1.0f) {
        frame = portRoomAuditGetFrame();
        if (frame - lastLogFrameByRoom[roomID & 63] >= 60u) {
            lastLogFrameByRoom[roomID & 63] = frame;
            fprintf(stderr,
                    "roomaudit: *** DRAW-TIME STALE MTX room=%d slot=%d owner=%d curidx=%d delta=(%.1f %.1f %.1f) f=%u\n",
                    roomID, slot, roomOwners[slot], p->curRoomIndex, dx, dy, dz, frame);
            fflush(stderr);
        }
    }
}

/* Called from portRoomAuditTick between frames: logs the drawn-room set when it changes.
 * Don't use sprintf here: in game code it resolves to Rare's libultra version,
 * whose %d is broken on PC. */
void portRoomMtxDrawnFlush(u32 frame)
{
    extern s32 portRoomAuditEnabled(void);
    static u8 prevBits[64];
    s32 i, count = 0;

    if (!portRoomAuditEnabled() || !s_MtxPoolInitialized) {
        return;
    }

    for (i = 1; i < 512; i++) {
        if (s_DrawnRoomBits[i >> 3] & (1 << (i & 7))) {
            count++;
        }
    }

    if (count > 0 && memcmp(s_DrawnRoomBits, prevBits, sizeof(prevBits)) != 0) {
        fprintf(stderr, "roomaudit: drawn set (%d):", count);
        for (i = 1; i < 512; i++) {
            if (s_DrawnRoomBits[i >> 3] & (1 << (i & 7))) {
                fprintf(stderr, " %d", i);
            }
        }
        fprintf(stderr, " f=%u\n", frame);
        fflush(stderr);
    }

    memcpy(prevBits, s_DrawnRoomBits, sizeof(prevBits));
    memset(s_DrawnRoomBits, 0, sizeof(s_DrawnRoomBits));
}
#endif /* !TARGET_N64 */


/**
 * Updates the display list with the room matrix for a specific room roomID.
 *
 * Address: 0x7F0BC9C4
 */
Gfx * applyRoomMatrixToDisplayList(Gfx *gdl,int roomID)
{
    s32 roomIndex;

    roomIndex = setupRoomTransformationMatrix(roomID);
#ifndef TARGET_N64
    portRoomMtxCheckAtDraw(roomID, roomIndex);
#endif
    gSPMatrix(gdl++, &roomMatrices[roomIndex], G_MTX_MODELVIEW|G_MTX_LOAD|G_MTX_NOPUSH);
    return gdl;
}


/**
 * Returns the position of a room by its roomID.
 *
 * Address: 0x7f0bca14
 */
struct coord3d* getRoomPositionByIndex(s32 roomID)
{
    return &ptr_bgdata_room_fileposition_list[roomID].pos;
}


/**
 * Retrieves and scales the position of a room by its roomID.
 *
 * Address: 0x7F0BCA34
 */
void getRoomPositionScaledByIndex(s32 roomID, coord3d *scaledPos)
{
    scaledPos->x = ptr_bgdata_room_fileposition_list[roomID].pos.x * room_data_float2;
    scaledPos->y = ptr_bgdata_room_fileposition_list[roomID].pos.y * room_data_float2;
    scaledPos->z = ptr_bgdata_room_fileposition_list[roomID].pos.z * room_data_float2;
}


#ifndef TARGET_N64
/* GE_ROOM_AUDIT: per-frame check that the slot-to-room and room-to-slot matrix
 * bindings agree (a mismatch means a room draws with another room's matrix),
 * plus a once-a-second slot-occupancy log to spot pool leaks. */
void portRoomMtxAuditScan(u32 frame)
{
    extern s32 getMaxNumRooms(void);
    s32 i;
    s32 inuse = 0;
    s32 maxrooms = getMaxNumRooms();

    if (!s_MtxPoolInitialized) {
        return; /* pre-stage BSS state: every slot "claims room 0" */
    }

    for (i = 0; i < AMT300; i++) {
        s32 room = roomIndices[i];

        if (roomOwners[i] != -1) {
            inuse++;
        }
        if (room == -1) {
            continue;
        }
        if (room < 0 || room >= maxrooms) {
            fprintf(stderr,
                    "roomaudit: *** MTX SLOT BAD ROOM slot=%d room=%d f=%u\n", i, room, frame);
            continue;
        }
        if (s_RoomMtxIdByPlayer[s_RoomSlotPlayer[i]][room] != i) {
            fprintf(stderr,
                    "roomaudit: *** MTX SLOT ALIAS slot=%d claims room=%d (binder p%d) but that binding uses slot=%d f=%u\n",
                    i, room, s_RoomSlotPlayer[i], s_RoomMtxIdByPlayer[s_RoomSlotPlayer[i]][room], frame);
        }
    }

    /* reverse direction: per-player bindings pointing at slots that disown them */
    {
        s32 pl;
        for (pl = 0; pl < 4; pl++) {
            for (i = 1; i < maxrooms; i++) {
                s32 mtx = s_RoomMtxIdByPlayer[pl][i];

                if (mtx == -1) {
                    continue;
                }
                if (mtx < 0 || mtx >= AMT300) {
                    fprintf(stderr,
                            "roomaudit: *** MTX ROOM BAD SLOT p%d room=%d slot=%d f=%u\n", pl, i, mtx, frame);
                    continue;
                }
                if (roomIndices[mtx] != i || s_RoomSlotPlayer[mtx] != pl) {
                    fprintf(stderr,
                            "roomaudit: *** MTX ROOM ORPHANED p%d room=%d uses slot=%d but slot claims room=%d (binder p%d) f=%u - room renders with WRONG MATRIX\n",
                            pl, i, mtx, roomIndices[mtx], s_RoomSlotPlayer[mtx], frame);
                }
            }
        }
    }

    if ((frame % 60u) == 0u) {
        fprintf(stderr, "roomaudit: mtx slots in use: %d/%d f=%u\n", inuse, AMT300, frame);
    }

    /* ---- matrix content check: recompute each active slot's expected translation from the
     * current view origin and flag mismatches, since a stale cached matrix
     * shifts the whole world. */
    {
        static f32 prevAnchor[3] = { 999999.0f, 999999.0f, 999999.0f };
        static s32 prevRoomIdx = -2;
        static u8 diverged[AMT300];
        struct player *p = g_playerPointers[0];
        s32 curidx;

        if (p == NULL) {
            fflush(stderr);
            return;
        }
        curidx = p->curRoomIndex;

        if (curidx != prevRoomIdx ||
            p->current_model_pos.f[0] != prevAnchor[0] ||
            p->current_model_pos.f[1] != prevAnchor[1] ||
            p->current_model_pos.f[2] != prevAnchor[2]) {
            fprintf(stderr,
                    "roomaudit: anchor room %d->%d pos (%.1f %.1f %.1f)->(%.1f %.1f %.1f) f=%u\n",
                    prevRoomIdx, curidx,
                    prevAnchor[0], prevAnchor[1], prevAnchor[2],
                    p->current_model_pos.f[0], p->current_model_pos.f[1], p->current_model_pos.f[2],
                    frame);
            prevRoomIdx = curidx;
            prevAnchor[0] = p->current_model_pos.f[0];
            prevAnchor[1] = p->current_model_pos.f[1];
            prevAnchor[2] = p->current_model_pos.f[2];
        }

        for (i = 0; i < AMT300; i++) {
            s32 room = roomIndices[i];
            f32 ex, ey, ez, dx, dy, dz;
            Mtxf unpacked;
            struct player *binder; /* MP: judge each slot vs ITS OWN player's anchor */

            binder = g_playerPointers[s_RoomSlotPlayer[i]];

            /* only slots the binder will cache-hit; slots owned by a
             * previous room index are legitimately stale and get rebuilt
             * on next use */
            if (room <= 0 || room >= maxrooms || binder == NULL ||
                roomOwners[i] != binder->curRoomIndex) {
                if (i < AMT300) {
                    diverged[i] = 0;
                }
                continue;
            }

            ex = (ptr_bgdata_room_fileposition_list[room].pos.f[0] * room_data_float2) - binder->current_model_pos.f[0];
            ey = (ptr_bgdata_room_fileposition_list[room].pos.f[1] * room_data_float2) - binder->current_model_pos.f[1];
            ez = (ptr_bgdata_room_fileposition_list[room].pos.f[2] * room_data_float2) - binder->current_model_pos.f[2];

            sub_GAME_7F058E78((Mtxf *)&roomMatrices[i], &unpacked);
            dx = unpacked.m[3][0] - ex;
            dy = unpacked.m[3][1] - ey;
            dz = unpacked.m[3][2] - ez;

            if (dx > 1.0f || dx < -1.0f || dy > 1.0f || dy < -1.0f || dz > 1.0f || dz < -1.0f) {
                if (!diverged[i] || (frame % 60u) == 0u) {
                    fprintf(stderr,
                            "roomaudit: *** STALE ROOM MATRIX slot=%d room=%d owner=%d expect=(%.1f %.1f %.1f) cached=(%.1f %.1f %.1f) delta=(%.1f %.1f %.1f) f=%u\n",
                            i, room, roomOwners[i], ex, ey, ez,
                            unpacked.m[3][0], unpacked.m[3][1], unpacked.m[3][2],
                            dx, dy, dz, frame);
                }
                diverged[i] = 1;
            } else {
                diverged[i] = 0;
            }
        }
    }

    fflush(stderr);
}
#endif /* !TARGET_N64 */

