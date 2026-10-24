/* port_crowd.c - the arena's impostor crowd (modding/arena.md): camera-facing quads on the spare seats, textured
 * with the crowd skins themselves, rendered into an atlas once per stage (idle and arms-up poses).
 * Presentation only: impostors are not chrs or props, have no collision, and use their own RNG and clock.
 * The models built here are private (malloc, not the model slots); the render side is gfx_pc.cpp / gfx_opengl.cpp. */
#ifndef TARGET_N64

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ultra64.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include "game/model.h"
#include "game/chr_b.h"
#include "game/chrobjdata.h"
#include "game/matrixmath.h"
#include "game/initanitable.h"
#include "game/bondview.h"
#include "game/dyn.h"
#include "port_mode.h"
#include "port_botlogic.h"
#include "../fast3d/gfx_crowd.h"

extern s32 portMpCharBodyHead(s32 c, s32 *body, s32 *head, f32 *pov);   /* front.c */
extern unsigned long long portProfNow(void);                            /* pcsched.c */
extern void portConLogf(const char *fmt, ...);
extern s32 g_PortRoomCacheMode;                                          /* roc_hooks.c */
extern ModelRenderData D_8002CCBC;                                       /* chr.c: a chr's render data */

#define CROWD_MAX   4096
#define SKINS_MAX   16
#define RW_WORDS    4096

static CrowdImpostor sImp[CROWD_MAX];
static s32 sN, sVersion;
static s32 sSkinN, sBody[SKINS_MAX], sHead[SKINS_MAX];
static Model *sModel[SKINS_MAX];
static u32 *sRw[SKINS_MAX];
static s32 sWant;                        /* a capture is wanted this stage */
static s32 sWaited;                      /* frames spent waiting for the skins' model files */
static f32 sBox[3];
static unsigned long long sT0;
static u32 sRng = 0x9e3779b9u;

static f32 rnd(void)                     /* presentation RNG (xorshift), never the sim's */
{
    sRng ^= sRng << 13;
    sRng ^= sRng >> 17;
    sRng ^= sRng << 5;
    return (f32)(sRng >> 8) / 16777216.0f;
}

static f32 now(void)
{
    return (f32)((portProfNow() - sT0) / 1000) / 1000.0f;
}

void portCrowdStageLoaded(void)
{
    s32 k;

    for (k = 0; k < SKINS_MAX; k++)
    {
        free(sModel[k]);
        free(sRw[k]);
        sModel[k] = NULL;
        sRw[k] = NULL;
    }
    sN = 0;
    sSkinN = 0;
    sWant = 0;
    sWaited = 0;
    sVersion++;
}

/* crowd.impostors: xyz = n seat positions (x, y, z each); skins = names as spawn.chr takes them. Returns the count. */
int portCrowdSet(const float *xyz, int n, const char *const *skins, int nskins)
{
    s32 i;

    sSkinN = 0;
    for (i = 0; i < nskins && sSkinN < SKINS_MAX; i++)
    {
        s32 k = portBotSkinFind(skins[i]), b, h;
        f32 pov;

        if (k >= 0 && portMpCharBodyHead(portBotSkinMp(k), &b, &h, &pov))
        {
            sBody[sSkinN] = b;
            sHead[sSkinN] = h;
            sSkinN++;
        }
    }
    if (sSkinN == 0)
    {
        n = 0;
    }
    if (n > CROWD_MAX)
    {
        n = CROWD_MAX;
    }
    if (sT0 == 0)
    {
        sT0 = portProfNow();
    }
    for (i = 0; i < n; i++)
    {
        sImp[i].x = xyz[i * 3];
        sImp[i].y = xyz[i * 3 + 1];
        sImp[i].z = xyz[i * 3 + 2];
        sImp[i].skin = (f32)(s32)(rnd() * sSkinN);
        sImp[i].t0 = sImp[i].t1 = 0.0f;
    }
    sN = n;
    sVersion++;
    if (n > 0 && sModel[0] == NULL)
    {
        sWant = 1;
    }
    return n;
}

/* crowd.cheer: for `secs`, `frac` of the impostors raise their arms, each a little late and for its own while */
void portCrowdCheer(float secs, float frac)
{
    f32 t = now();
    s32 i;

    for (i = 0; i < sN; i++)
    {
        if (rnd() < frac)
        {
            sImp[i].t0 = t + rnd() * 0.8f;
            sImp[i].t1 = sImp[i].t0 + secs * (0.5f + 0.5f * rnd());
        }
    }
    sVersion++;
}

static s32 loaded(s32 id)
{
    return id >= 0 && c_item_entries[id].header != NULL && c_item_entries[id].header->RootNode != NULL;
}

/* a private body + head, as makeonebody builds a guard's but in our own memory */
static Model *makeModel(s32 k)
{
    ModelFileHeader *bh = c_item_entries[sBody[k]].header;
    ModelFileHeader *hh = (sHead[k] >= 0 && !c_item_entries[sBody[k]].hasHead) ? c_item_entries[sHead[k]].header : NULL;

    sModel[k] = calloc(1, sizeof(Model) + 64);
    sRw[k] = calloc(RW_WORDS, sizeof(u32));
    modelCalculateRwDataLen(bh);
    animInit(sModel[k], bh, sRw[k]);
    makeonebody(sBody[k], sHead[k], bh, hh, 0, sModel[k]);
    return sModel[k];
}

static void pose(Model *m, s32 p)
{
    coord3d zero = { 0.0f, 0.0f, 0.0f };

    setsuboffset(m, &zero);
    setsubroty(m, 0.0f);
    if (p == 0)
    {
        modelSetAnimation(m, (ModelAnimation *)animation_table_ptrs1[ANIM_idle_unarmed], 0, 0.0f, 0.0f, 0.0f);
    }
    else
    {
        modelSetAnimation(m, (ModelAnimation *)animation_table_ptrs1[ANIM_surrendering_unarmed], 0, 45.0f, 0.0f, 0.0f);
    }
    modelTickAnim(m, 1, 0);
    subcalcpos(m);
}

static void calc(Model *m, Mtxf *base, Mtxf *list)
{
    ModelRenderData rd = D_8002CCBC;

    rd.basemtx = base;
    rd.mtxlist = list;
    subcalcmatrices(&rd, m);
}

/* after the world (lv.c): once per stage, render every skin in both poses into the atlas */
Gfx *portCrowdCapture(Gfx *gdl)
{
    static Mtxf tmp[256];
    f32 mn[3] = { 1e9f, 1e9f, 1e9f }, mx[3] = { -1e9f, -1e9f, -1e9f }, h, hw, cy, bottom;
    Mtxf ident, view;
    Mtx *proj;
    s32 k, p, i, roc;

    if (!sWant || sN == 0)
    {
        return gdl;
    }
    for (k = 0; k < sSkinN; k++)
    {
        if (!loaded(sBody[k]) || (sHead[k] >= 0 && !c_item_entries[sBody[k]].hasHead && !loaded(sHead[k])))
        {
            break;
        }
    }
    if (k < sSkinN && ++sWaited < 300)
    {
        return gdl;   /* the real spectators load the skins' files; wait for them (10 s) */
    }
    sWant = 0;
    if (k < sSkinN)
    {
        portConLogf("ge: crowd: %d of %d skins loaded, the rest use them\n", k, sSkinN);
        for (i = 0; i < sN; i++)
        {
            if ((s32)sImp[i].skin >= k)
            {
                sImp[i].skin = (f32)(k > 0 ? i % k : 0);
            }
        }
        sSkinN = k;
        sVersion++;
        if (k == 0)
        {
            sN = 0;
            return gdl;
        }
    }

    matrix_4x4_set_identity(&ident);
    modelSetDistanceDisabled(1);
    for (k = 0; k < sSkinN; k++)
    {
        Model *m = makeModel(k);

        for (p = 0; p < 2; p++)
        {
            pose(m, p);
            if (m->obj->numMatrices > 256)
            {
                continue;
            }
            calc(m, &ident, tmp);
            for (i = 0; i < m->obj->numMatrices; i++)
            {
                s32 a;

                for (a = 0; a < 3; a++)
                {
                    if (tmp[i].m[3][a] < mn[a]) mn[a] = tmp[i].m[3][a];
                    if (tmp[i].m[3][a] > mx[a]) mx[a] = tmp[i].m[3][a];
                }
            }
        }
    }
    h = (mx[1] - mn[1]) * 1.2f;
    hw = fmaxf(fmaxf(fabsf(mn[0]), fabsf(mx[0])), fmaxf(fabsf(mn[2]), fabsf(mx[2]))) * 1.3f;
    if (hw * 4.0f > h) h = hw * 4.0f;
    hw = h / 4.0f;
    bottom = mn[1] - (mx[1] - mn[1]) * 0.06f;
    cy = bottom + h * 0.5f;
    sBox[0] = hw;
    sBox[1] = bottom - mn[1];
    sBox[2] = h;
    portConLogf("ge: crowd: capturing %d skins x 2 poses for %d impostors (box %.0f x %.0f)\n", sSkinN, sN, 2.0f * hw, h);

    roc = g_PortRoomCacheMode;
    g_PortRoomCacheMode = 0;   /* no retained copies of these draws */
    proj = dynAllocate(sizeof(Mtx));
    guOrtho(proj, -hw, hw, -h * 0.5f, h * 0.5f, 100.0f, 6000.0f, 1.0f);
    matrix_4x4_set_lookat_target(&view, 0.0f, cy, 3000.0f, 0.0f, cy, 0.0f, 0.0f, 1.0f, 0.0f);

    gdl->words.w0 = ((uintptr_t)G_CROWD_EXT << 24) | CROWD_CAP_BEGIN;
    gdl->words.w1 = 0;
    gdl++;
    gSPMatrix(gdl++, proj, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPClearGeometryMode(gdl++, G_FOG | G_LIGHTING | G_TEXTURE_GEN | G_CULL_BOTH);
    gSPSetGeometryMode(gdl++, G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH | G_CULL_BACK);
    sub_GAME_7F073FC8(0);   /* no shadow */
    for (p = 0; p < 2; p++)
    {
        for (k = 0; k < sSkinN; k++)
        {
            Model *m = sModel[k];
            ModelRenderData rd = D_8002CCBC;
            Mtxf *list = dynAllocate(m->obj->numMatrices * sizeof(Mtxf));

            pose(m, p);
            calc(m, &view, list);
            gdl->words.w0 = ((uintptr_t)G_CROWD_EXT << 24) | CROWD_CAP_CELL;
            gdl->words.w1 = (uintptr_t)(k + p * 16);
            gdl++;
            rd.gdl = gdl;
            rd.zbufferenabled = TRUE;
            rd.flags = 3;
            rd.PropType = 7;
            rd.envcolour.word = 0;
            rd.fogcolour.word = 0;
            subdraw(&rd, m);
            gdl = rd.gdl;
            bondviewTransformManyPosToViewMatrix((RenderPosView *)list, m->obj->numMatrices);   /* as a chr's: to Mtx in place */
        }
    }
    gdl->words.w0 = ((uintptr_t)G_CROWD_EXT << 24) | CROWD_CAP_END;
    gdl->words.w1 = 0;
    gdl++;
    g_PortRoomCacheMode = roc;
    modelSetDistanceDisabled(0);
    return gdl;
}

/* before the world-end marker (lv.c): one batched draw of every impostor, depth-tested against the scene */
Gfx *portCrowdDraw(Gfx *gdl)
{
    Mtxf *cam, *proj;
    CrowdFrame *f;
    f32 rx, rz, l;
    s32 i, j;

    if (sN == 0 || sModel[0] == NULL || !portModeActive())
    {
        return gdl;
    }
    cam = camGetWorldToScreenMtxf();
    proj = currentPlayerGetProjectionMatrixF();   /* the chrs' projection */
    f = dynAllocate(sizeof(CrowdFrame));
    if (cam == NULL || proj == NULL || f == NULL)
    {
        return gdl;
    }
    /* world -> clip in the frame itself (gfx_crowd.h): no gSPMatrix, so the RSP matrices the gun inherits are untouched */
    for (i = 0; i < 4; i++)
    {
        for (j = 0; j < 4; j++)
        {
            f->mvp[i * 4 + j] = cam->m[i][0] * proj->m[0][j] + cam->m[i][1] * proj->m[1][j]
                              + cam->m[i][2] * proj->m[2][j] + cam->m[i][3] * proj->m[3][j];
        }
    }
    rx = cam->m[0][0];
    rz = cam->m[2][0];
    l = sqrtf(rx * rx + rz * rz);
    f->imp = sImp;
    f->n = sN;
    f->version = sVersion;
    f->right[0] = l > 0.0f ? rx / l : 1.0f;
    f->right[1] = l > 0.0f ? rz / l : 0.0f;
    f->now = now();
    f->box[0] = sBox[0];
    f->box[1] = sBox[1];
    f->box[2] = sBox[2];
    f->tint[0] = f->tint[1] = f->tint[2] = f->tint[3] = 0.0f;
    gdl->words.w0 = ((uintptr_t)G_CROWD_EXT << 24) | CROWD_DRAW;
    gdl->words.w1 = (uintptr_t)f;
    gdl++;
    return gdl;
}

#endif
