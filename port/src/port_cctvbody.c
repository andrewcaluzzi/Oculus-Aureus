/* port_cctvbody.c - the solo Bond's body on a CCTV feed (designs/bank-heist.md 23c, AC's playtest: "I never see myself on a camera").
 *
 * A solo Bond has no body: the intro builds one in the hands' weapon buffers and bondviewRemovePlayerBody drops it (his prop has no chr).
 * Co-op and netplay keep a body per Bond, so port_cctvview.c draws those as chrs. For solo this file keeps a PRESENTATION-ONLY body:
 *   - the same body / head models the intro (and co-op's second Bond) uses for him, picked the way solo_char_load picks them, loaded by ID
 *     from the ROM data at runtime with load_object_fill_header into this file's own host-heap buffers (the path the weapon buffers use:
 *     nothing from the STAGE pool, no model slot, no chr, no prop: the sim's lists, slots and addresses do not change);
 *   - a Model built in that buffer (animInit + makeonebody with a model given, as the intro does), posed from Bond's position, floor,
 *     yaw and speed (unarmed MP idle / walk / crouch animations, the ones playerTick uses for a Bond's body), ticked only here;
 *   - drawn only from portCctvDrawChrs, inside its RNG / matrix-pool save and restore (portCctvBodyDraw).
 * Rebuilt at each stage start (the files and the texture pool are per stage).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ultra64.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include "game/bondview.h"
#include "game/player.h"
#include "game/chr.h"
#include "game/model.h"
#include "game/image.h"
#include "game/file.h"
#include "game/initanitable.h"
#include "game/objecthandler.h"
#include "port_coop.h"

extern ModelRenderData D_8002CCBC;
extern void subcalcmatrices(ModelRenderData *arg0, Model *arg1);
extern void subdraw(ModelRenderData *mrData, Model *mdl);
extern void *dynAllocate(s32 size);
extern Mtxf *camGetWorldToScreenMtxf(void);
extern void bondviewTransformManyPosToViewMatrix(RenderPosView *arg0, s32 arg1);
extern u32 getSizeBufferWeaponInHand(s32 hand);
extern s32 get_pc_buffer_remaining_value(u8 *name);
extern void modelCalculateRwDataLen(ModelFileHeader *header);
extern struct Model *makeonebody(s32 body, s32 head, struct ModelFileHeader *bodyHeader, struct ModelFileHeader *headHeader, s32 sunglasses,
                                 struct Model *model);
extern struct firing_anim_struct firing_animation_groups[][6];
extern s32 playerGetCrouchPos(s32 playernum);
extern s32 portAdzyMode(void);
extern int g_portCoopEpoch;
extern s32 g_GlobalTimer;

#define ALIGN64(x) (((x) + 0x3f) & ~0x3f)

static struct
{
    s32 epoch;          /* the stage start it was built in (g_portCoopEpoch); 0 = none */
    s32 failed;         /* 1 = could not be built this stage (no retry) */
    u8 *buf;            /* the files, the Model and its rw data */
    u8 *tex;            /* the texture pool */
    ModelFileHeader *bodyhdr, *headhdr;
    Model *model;
    s32 anim;           /* the animation it plays (ptr_animation_table offset), -1 none */
    s32 lastTick;       /* g_GlobalTimer at the last pose */
    s32 draws;          /* bodies drawn (tests) */
} sB;

static void pickBody(s32 *body, s32 *head)
{
    struct player *pl = g_playerPointers[0];
    s32 folder = fileGetBondForCurrentFolder();

    (void) folder;
    *body = BODY_Formal_Wear;
    *head = HEAD_Male_Brosnan_Default;
    switch (pl->bondtype)
    {   /* solo_char_load's choice (every Bond is Brosnan's model there) */
        case CUFF_BOILER: *body = BODY_Special_Operations_Uniform; *head = HEAD_Male_Brosnan_Boiler; break;
        case CUFF_JUNGLE: *body = BODY_Jungle_Fatigues; *head = HEAD_Male_Brosnan_Jungle; break;
        case CUFF_SNOW:   *body = BODY_Parka; *head = HEAD_Male_Brosnan_Tuxedo; break;
        case CUFF_BLUE:   break;
        default:          *body = BODY_Brosnan_Tuxedo; *head = HEAD_Male_Brosnan_Tuxedo; break;
    }
    if (portAdzyMode())
    {
        *body = BODY_Siberian_Special_Forces;
    }
}

static void freeBody(void)
{
    free(sB.buf);
    free(sB.tex);
    sB.buf = NULL;
    sB.tex = NULL;
    sB.model = NULL;
}

static s32 build(void)
{
    s32 body, head, cursor, size0, size1, nrec;
    struct texpool pool;
    u32 *animdata;

    freeBody();
    sB.failed = 1;
    sB.anim = -1;
    pickBody(&body, &head);
    if (c_item_entries[body].header == NULL || c_item_entries[head].header == NULL) return 0;
    size0 = (s32) getSizeBufferWeaponInHand(GUNRIGHT);
    size1 = (s32) getSizeBufferWeaponInHand(GUNLEFT);
    if (size0 < 0x10000 || size1 < 0x1000) return 0;
    sB.buf = (u8 *) calloc(1, (size_t) size0 + 0x40);
    sB.tex = (u8 *) calloc(1, (size_t) size1 + 0x40);
    if (sB.buf == NULL || sB.tex == NULL)
    {
        freeBody();
        return 0;
    }
    {   /* the layout solo_char_load uses in the weapon buffers: body file, head header, head file, Model, rw data */
        u8 *b0 = (u8 *) ALIGN64((uintptr_t) sB.buf);
        u8 *b1 = (u8 *) ALIGN64((uintptr_t) sB.tex);
        static ModelFileHeader bodyhdr;   /* (presentation statics: PRES in the snapshot, never hashed) */

        texInitPool(&pool, b1, size1);
        sB.bodyhdr = &bodyhdr;
        *sB.bodyhdr = *c_item_entries[body].header;
        load_object_fill_header(sB.bodyhdr, (u8 *) c_item_entries[body].filename, b0, size0, &pool);
        cursor = get_pc_buffer_remaining_value((u8 *) c_item_entries[body].filename);
        cursor = ALIGN64(cursor + 0x3f);
        sB.headhdr = (ModelFileHeader *) (b0 + cursor);
        cursor = ALIGN64(cursor + (s32) sizeof(ModelFileHeader) + 0x3f);
        *sB.headhdr = *c_item_entries[head].header;
        load_object_fill_header(sB.headhdr, (u8 *) c_item_entries[head].filename, b0 + cursor, size0 - cursor, &pool);
        cursor = ALIGN64(get_pc_buffer_remaining_value((u8 *) c_item_entries[head].filename) + cursor + 0x3f);
        sB.model = (Model *) (b0 + cursor);
        cursor = ALIGN64(cursor + 0xfb);
        modelCalculateRwDataLen(sB.bodyhdr);
        modelCalculateRwDataLen(sB.headhdr);
        animdata = (u32 *) (b0 + cursor);
        nrec = (sB.bodyhdr->numRecords + sB.headhdr->numRecords) + 0xa;
        cursor = ALIGN64(cursor + (nrec << 2) + 0x3f);
        if (cursor > size0 || sB.bodyhdr->RootNode == NULL || sB.headhdr->RootNode == NULL)
        {
            freeBody();
            return 0;
        }
        animInit(sB.model, sB.bodyhdr, animdata);
        sB.model->rwdatalen = nrec;
        makeonebody(body, head, sB.bodyhdr, sB.headhdr, 0, sB.model);
        modelSetScale(sB.model, sB.model->scale * 0.97f);
    }
    sB.failed = 0;
    sB.epoch = g_portCoopEpoch;
    sB.lastTick = g_GlobalTimer;
    printf("ge: cctv: the solo Bond's feed body: body %d head %d (presentation only)\n", (int) body, (int) head);
    return 1;
}

/* the unarmed MP body animations playerTick picks for a Bond (firing_animation_groups[2]): 0 idle, 1 walk, 5 crouch */
static void pose(struct player *pl)
{
    struct firing_anim_struct *fa;
    s32 sub = 0, anim, ticks;
    coord3d off;
    f32 speed = pl->speedforwards < 0.0f ? -pl->speedforwards : pl->speedforwards;
    f32 side = pl->speedsideways < 0.0f ? -pl->speedsideways : pl->speedsideways;

    if (playerGetCrouchPos(0) <= 1)
    {
        sub = 5;
    }
    else if (speed > 0.05f || side > 0.05f)
    {
        sub = 1;
    }
    fa = &firing_animation_groups[2][sub];
    anim = fa->anim;
    if (anim != 0 && anim != sB.anim)
    {
        modelSetAnimation(sB.model, (ModelAnimation *) (anim + (s32) ptr_animation_table), 0, fa->y >= 0.0f ? fa->y : 0.0f,
                          sub == 1 ? 0.5f : fa->x, 0.0f);
        if (fa->y >= 0.0f) modelSetAnimLooping(sB.model, fa->y, 0.0f);
        if (fa->z >= 0.0f) modelSetAnimEndFrame(sB.model, fa->z);
        sB.anim = anim;
    }
    ticks = g_GlobalTimer - sB.lastTick;
    sB.lastTick = g_GlobalTimer;
    if (ticks <= 0 || ticks >= 30) ticks = 1;   /* (the first pose too: the animation puts the hips at their height) */
    modelTickAnim(sB.model, ticks, 1);   /* (1: the root's translation too, modelSetAnimFrame2WithChrStuff; it touches only this model) */
    {   /* the root sits at the animation's hip height above the ground (model.c: pos.y = anim y + ground; the anim's root y is unk24.y,
         * scaled): Bond's floor plus that */
        ModelRwData_HeaderRecord *rw = (ModelRwData_HeaderRecord *) modelGetNodeRwData(sB.model, sB.model->obj->RootNode);
        f32 hips = rw->unk24.y;

        rw->ground = pl->stanHeight;
        off.y = pl->stanHeight + hips;
    }
    off.x = pl->prop->pos.x;
    off.z = pl->prop->pos.z;
    setsuboffset(sB.model, &off);
    setsubroty(sB.model, ((360.0f - pl->vv_theta) * M_TAU_F) / 360.0f);
}

int portCctvBodyDraws(void)
{
    return sB.draws;
}

/* port_cctvview.c, inside the feed pass's save / restore: the solo Bond (player 0, no chr) when the camera's walk drew his room. gdl in,
 * gdl out; *drawn = 1 when he was drawn. Needs room for its matrices in the feed's pool corner (the caller checked). */
Gfx *portCctvBodyDraw(Gfx *gdl, const u8 *roomOn, const coord3d *eye, const coord3d *look, s32 *drawn)
{
    struct player *pl = NULL;
    ModelRenderData rd;
    f32 dx, dy, dz;
    static s32 off = -1;

    *drawn = 0;
    if (off < 0)
    {   /* GE_CCTV_NOSELF=1: no solo body (the A/B check that it is presentation) */
        const char *e = getenv("GE_CCTV_NOSELF");

        off = (e != NULL && e[0] != '0');
    }
    if (off || portAllPlayerCount() != 1 || (pl = g_playerPointers[0]) == NULL || pl->prop == NULL || pl->prop->chr != NULL
        || pl->bonddead || pl->prop->stan == NULL || !roomOn[pl->prop->stan->room & 0xFF])   /* (his room: his floor tile's) */
    {
        return gdl;
    }
    dx = pl->prop->pos.x - eye->x;
    dy = pl->prop->pos.y - eye->y;
    dz = pl->prop->pos.z - eye->z;
    if (dx * look->x + dy * look->y + dz * look->z < 0.0f) return gdl;
    if (sB.epoch != g_portCoopEpoch || sB.model == NULL)
    {
        if (sB.epoch == g_portCoopEpoch && sB.failed) return gdl;
        sB.epoch = g_portCoopEpoch;
        if (!build()) return gdl;
    }
    pose(pl);
    rd = D_8002CCBC;
    rd.basemtx = camGetWorldToScreenMtxf();
    rd.mtxlist = (Mtxf *) dynAllocate(sB.model->obj->numMatrices * (s32) sizeof(Mtxf));
    subcalcmatrices(&rd, sB.model);
    rd.zbufferenabled = TRUE;
    rd.gdl = gdl;
    rd.flags = 3;
    rd.PropType = 7;
    rd.envcolour.word = 0;
    {   /* the floor tile's light, as a chr's shade (set_color_shading_from_tile only reads the tile) */
        extern void set_color_shading_from_tile(PropRecord *prop, u8 col[4]);
        u8 col[4];

        set_color_shading_from_tile(pl->prop, col);
        rd.fogcolour.word = ((u32) col[0] << 24) | ((u32) col[1] << 16) | ((u32) col[2] << 8) | (u32) col[3];
    }
    subdraw(&rd, sB.model);
    bondviewTransformManyPosToViewMatrix(sB.model->render_pos, sB.model->obj->numMatrices);
    sB.draws++;
    *drawn = 1;
    return rd.gdl;
}
