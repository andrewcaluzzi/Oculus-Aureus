#include <ultra64.h>
#include "vi.h"
#include "fr.h"
#include <bondgame.h>
#include <bondconstants.h>
#include "player.h" // g_CurrentPlayer
#include "sched.h"
#include "rsp.h"
#include <PR/os.h>
#include "indy_comms.h"
#include "game/bondview.h"
#include "game/viewport.h"
#include "game/dyn.h"
#ifdef ENABLE_USB
#include "usb.h"
#endif

/**
 * EU .data, offset from start of data_seg : 0x2484
*/

/**
 * @file video.c
 * This file contains video handling code.
 */

// same codegen as (x & 0xffff)
#define TO_U16_A(x) ((u16)(x))
// same codegen as ((u16)(x))
#define TO_U16_B(x) (x & 0xffff)

#define TO_U16_C(x) ((u16)(x & 0xffff))

#define TO_U16_M(x) ((x) % 0xffff)

/**
 * Splits a 32 bit value into upper and lower 16, adds a 16 bit value to each, and combines back to 32 bits.
 *
 * Note: both unsigned 16 bit casts result in the same codegen, but I'm using different
 * macros to give a more "natural" explanation of the asm codegen.
 */
#define ADD_LOW_AND_HI_16_TRUNCATE(x32, add16) ((TO_U16_B(TO_U16_A((x32) >> 16) + (add16)) << 16) | TO_U16_B(TO_U16_A(x32) + (add16)))

#define ADD_LOW_AND_HI_16_MOD(x32, add16) ((TO_U16_M(TO_U16_B((x32) >> 16) + (add16)) << 16) | TO_U16_M(TO_U16_A(x32) + (add16)))

/**
 * Address 80023240.
 */
u32 g_unused80023240 = 0;

#if defined(VERSION_EU)
struct VideoSettings_s g_ViDataArray[NUM_VIDEO_SETTINGS] =
{
    {0, 0, 0, 0, 320, 272, 60.0f, 1.17647063732f, 30.0f, 10000.0f, SCREEN_WIDTH, SCREEN_HEIGHT, 320, 272, 0, 0, TRUE, NULL},
    {0, 0, 0, 0, 320, 272, 60.0f, 1.17647063732f, 30.0f, 10000.0f, SCREEN_WIDTH, SCREEN_HEIGHT, 320, 272, 0, 0, TRUE, NULL}
};
#else
struct VideoSettings_s g_ViDataArray[NUM_VIDEO_SETTINGS] =
{
    {MD_BLACK, 0, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, FOV_Y_F, ASPECT_RATIO, 30.0f, 10000.0f, SCREEN_WIDTH, SCREEN_HEIGHT, SCREEN_WIDTH, SCREEN_HEIGHT, 0, 0, TRUE, NULL},
    {MD_BLACK, 0, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, FOV_Y_F, ASPECT_RATIO, 30.0f, 10000.0f, SCREEN_WIDTH, SCREEN_HEIGHT, SCREEN_WIDTH, SCREEN_HEIGHT, 0, 0, TRUE, NULL}};
#endif

/**
 * Address 8002329C.
 * vimode hStart horizontal offset in video_related_8.
 */
s32 g_viHorizontalOffset = 0;

/**
 * Address 800232A0.
 * vimode vStart vertical offset in video_related_8.
 * (see g_ViShakeIntensity before changing the name)
 */
s32 D_800232A0 = 0;

VideoSettings *g_ViFrontData = &g_ViDataArray[0];
VideoSettings *g_ViBackData = &g_ViDataArray[0];
s32 g_viColorOutputMode = COLORMODE_16BIT;

/**
 * Address 800232B0.
 * g_ViShakeIntensity multiplier sign. This is only ever 1 or -1.
 */
s32 g_viVstartSign = 1;

/**
 * Address 800232B4.
 * vimode vStart vertical offset in viVsyncRelated.
 * (see D_800232A0 before changing the name)
 */
s32 g_ViShakeIntensity = 0;

/**
 * Address 800232B8.
 * Some kind of counter for how frequently g_ViShakeIntensity is zero'd in viVsyncRelated.
 */
u32 g_ViShakeTimer = 0;

/**
 * Address 800232BC.
 * Some kind of counter for how frequently osViBlack is called with "active" parameter.
 */
u32 g_ViUnblackTimer = 3;

/**
 * Address 800232C0.
 * should correlate to g_schedViCurrentFrameBuffer
 */
s32 D_800232C0 = 0;

#if defined(LEFTOVERDEBUG)
s32 g_indyJpg16BitGrabnum = 1;
s32 g_indyJpg32BitGrabnum = 1;
s32 g_indyRgb16BitGrabnum = 1;
s32 g_indyRgb32BitGrabnum = 1;
#endif

f32 g_viProjectionMatrixF[4][4];
Mtx *g_viProjectionMatrix;
u16 g_viPerspNorm;

/**
 * Address 80060828.
 * Original vi mode.
 * (name following n64devkit\ultra\usr\src\pr\demos\threadmonkey\block.c)
 */
OSViMode g_viOriginalVideoMode;

/**
 * Address 80060878.
 * cfb_16 index for g_ViFrontData.
 */
u8 g_ViFrontIndex;

/**
 * Address 80060879.
 * cfb_16 index for g_ViBackData and g_CurrentPlayer->viewports.
 */
u8 g_ViBackIndex;

/**
 * Current vi mode pointer.
 * EU .bss 0x80053E4C
 */
OSViMode *viMode;

/**
 * Address 80060880.
 * Original viMode->comRegs.hStart.
 * EU .bss 0x80053E50
 */
u32 g_viOriginalHstart;

/**
 * Address 80060884.
 * Original viMode->fldRegs[0].vStart.
 * EU .bss 0x80053E54
 */
u32 g_viOriginalVstart0;

/**
 * Address 80060888.
 * Original viMode->fldRegs[1].vStart.
 * EU .bss 0x80053E58
 */
u32 g_viOriginalVstart1;

s32 g_unused8006088C;

/**
 * Address 80028480.
 * Const runtime supported screen widths.
 */
const s16 g_viRuntimeScreenWidths[] = {SCREEN_WIDTH_MIN, SCREEN_WIDTH, SCREEN_WIDTH_MAX};

/**
 * Address 80028488.
 * Const runtime supported screen heights.
 */
#ifndef VERSION_EU
const s16 g_viRuntimeScreenHeights[] = {SCREEN_HEIGHT_MIN, SCREEN_HEIGHT, SCREEN_HEIGHT_MAX};
#else
const s16 g_viRuntimeScreenHeights[] = {SCREEN_HEIGHT_272, SCREEN_HEIGHT_272, SCREEN_HEIGHT_MAX_EU};
#endif

/**
 * 3C60	70003060
 */
void viInitVideoSettings(void)
{
    g_ViFrontIndex = 0;
    g_ViBackIndex = 1;

    g_ViFrontData = (VideoSettings*)((u8*)&g_ViDataArray + (g_ViFrontIndex * sizeof(VideoSettings))); //AKA front = viDataArray[0]
    g_ViFrontData->framebuf = cfb_16[g_ViFrontIndex];

    g_ViBackData = (VideoSettings*)((u8*)&g_ViDataArray + (g_ViBackIndex * sizeof(VideoSettings))); //AKA back = viDataArray[1] - was Rare mental?
    g_ViBackData->framebuf = cfb_16[g_ViBackIndex];

    g_viHorizontalOffset = 0;
    #ifndef VERSION_EU
    D_800232A0 = 0;
    #else
    D_800232A0 = 0xffffffe0;
    #endif
#ifndef TARGET_N64
    {   /* Rollback / savestates (modding/snapshot-fidelity.md): the sim's camera model reads the back VI settings (fovy,
         * aspect, view size: viGetFovY, viGetViewX...) and the sim writes them every pass, so they are sim state, though
         * this file is presentation. The front/back pair flips at every drawn frame (video_related_8): a restore that
         * leaves the pointer where the abandoned ticks put it hands the sim the other entry, set by those ticks
         * (Frigate: a different room walk in the very next tick). The VI mode ring (D_800232C0) goes with the pair. */
        extern void portSnapAddRange(const char *name, const void *p, unsigned int n);
        static s32 registered;

        if (!registered)
        {
            registered = 1;
            portSnapAddRange("fr.vidata", g_ViDataArray, sizeof(g_ViDataArray));
            portSnapAddRange("fr.vifront", &g_ViFrontData, sizeof(g_ViFrontData));
            portSnapAddRange("fr.viback", &g_ViBackData, sizeof(g_ViBackData));
            portSnapAddRange("fr.vifrontindex", &g_ViFrontIndex, sizeof(g_ViFrontIndex));
            portSnapAddRange("fr.vibackindex", &g_ViBackIndex, sizeof(g_ViBackIndex));
            portSnapAddRange("fr.vimodering", &D_800232C0, sizeof(D_800232C0));
            /* the projection the view setup builds from them: the player keeps a POINTER to this matrix
             * (currentPlayerSetProjectionMatrixF) and the sim's onscreen and portal tests read it */
            portSnapAddRange("fr.projf", g_viProjectionMatrixF, sizeof(g_viProjectionMatrixF));
            portSnapAddRange("fr.proj", &g_viProjectionMatrix, sizeof(g_viProjectionMatrix));
            portSnapAddRange("fr.perspnorm", &g_viPerspNorm, sizeof(g_viPerspNorm));
        }
    }
#endif
}

void viInitBuffers(void)
{
    s32 i;
    u8* p1 = cfb_16[0];
    u8* p2 = cfb_16[1];

    zbufDeallocate();

    for (i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT * 2; i++)
    {
        p1[i] = 0;
        p2[i] = 0;
    }
}

void frBlack(u32 black)
{
    black += 2;
    g_ViUnblackTimer = black;
}

/**
 * 3DA0	700031A0
 * Calls osViSetMode with current viMode.
 * Also calls osViBlack.
 * Applies verticalOffset to vstart.
 *
 * Looks related to n64devkit\ultra\usr\src\pr\demos\blockmonkey\block.c
 * in particular, ModifyVStart
 */
void viVsyncRelated(void)
{
    s32 verticalOffset;
    s32 registerValue;

    if (g_ViShakeTimer > 0)
    {
        g_ViShakeTimer--;

        if (g_ViShakeTimer == 0)
        {
            g_ViShakeIntensity = 0;
        }
    }

    verticalOffset = g_viVstartSign * g_ViShakeIntensity;

    // note the implicit cast from unsigned to signed.
    registerValue = g_viOriginalVstart0;
    (*viMode).fldRegs[0].vStart = ADD_LOW_AND_HI_16_TRUNCATE(registerValue, verticalOffset);

    registerValue = g_viOriginalVstart1;
    (*viMode).fldRegs[1].vStart = ADD_LOW_AND_HI_16_TRUNCATE(registerValue, verticalOffset);

    osViSetMode(viMode);
    osViBlack(g_ViUnblackTimer);

    // g_ViUnblackTimer might be declared signed which means the if statement is
    //
    //    if (g_ViUnblackTimer != 0 && g_ViUnblackTimer < 3)
    //
    // but this way feels "safer", though the cast to (s32) now seems unnatural.
    // The "right" answer depends on what's supposed to happen for values like -1 or 0xffffffff ...
    if (g_ViUnblackTimer > 0 && (s32)g_ViUnblackTimer < 3)
    {
        g_ViUnblackTimer--;
    }

    //smooth dither pattern and disable weird gamma
    //(N64 was supposed to compensate for CRT's 2.2 gamma, but by this time most games were made with sRGB in mind)
    osViSetSpecialFeatures(OS_VI_DITHER_FILTER_ON | OS_VI_GAMMA_OFF);

    g_viVstartSign = -g_viVstartSign;
}


#ifndef VERSION_EU
void video_related_8(void)
{
    u32 packedStart;
    s32 registerValue;
    f32 calculatedYScale;
    s32 yScaleMax;
    f32 calculatedXScale;
    VideoSettings *settings;
    s32 nextMode;

    if (g_ViFrontData->mode != g_ViBackData->mode)
    {
        registerValue = g_ViBackData->mode;

        switch (registerValue)
        {
            case MD_BLACK:
            {
                if (D_800232C0);

                osViSetYScale(1.0f);
                osViBlack(TRUE);
                break;
            }

            case MD_NORMAL:
            {
                if (g_viColorOutputMode != COLORMODE_32BIT)
                {
                    if (osTvType == TV_TYPE_MPAL)
                    {
                        g_viOriginalVideoMode = osViModeTable[OS_VI_MPAL_LAN1];
                    }
                    else
                    {
                        g_viOriginalVideoMode = osViModeTable[OS_VI_NTSC_LAN1];
                    }
                }
                else
                {
                    if (osTvType == TV_TYPE_MPAL)
                    {
                        g_viOriginalVideoMode = osViModeTable[OS_VI_MPAL_LAN2];
                    }
                    else
                    {
                        g_viOriginalVideoMode = osViModeTable[OS_VI_NTSC_LAN2];
                    }
                }

                viMode = &g_viOriginalVideoMode;
                g_viOriginalHstart = viMode->comRegs.hStart;
                g_viOriginalVstart0 = viMode->fldRegs[0].vStart;
                g_viOriginalVstart1 = viMode->fldRegs[1].vStart;

                frBlack(0);
                break;
            }

            case MD_MAXIMUM:
                if (osTvType == TV_TYPE_MPAL)
                {
                    viMode = &osViModeTable[OS_VI_MPAL_HAF1];
                }
                else
                {
                    viMode = &osViModeTable[OS_VI_NTSC_HAF1];
                }

                g_viOriginalHstart = viMode->comRegs.hStart;
                g_viOriginalVstart0 = viMode->fldRegs[0].vStart;
                g_viOriginalVstart1 = viMode->fldRegs[1].vStart;

                osViBlack(FALSE);
                break;
        }

        osViSetSpecialFeatures(OS_VI_DITHER_FILTER_ON | OS_VI_GAMMA_OFF);
    }

    calculatedXScale = (f32)g_ViBackData->x / (f32)g_ViBackData->bufx;
    calculatedYScale = (f32)g_ViBackData->y / (f32)g_ViBackData->bufy;

    /* Keep these statements on the same logical source line for IDO scheduling. */
    if (g_ViBackData->mode == MD_BLACK) \
    { \
        calculatedYScale = 1.0f; \
    } nextMode = D_800232C0;

    yScaleMax = YSCALE_MAX;

    if (1);
    if (1);
    if (1);
    if (1);

    g_ViXScales[nextMode] = calculatedXScale;
    g_ViYScales[nextMode] = calculatedYScale;

    if (g_ViBackData->mode == VIDEOMODE_320x240)
    {
        g_ViModePtrs[nextMode] = &g_viOriginalVideoMode;

        if (g_viColorOutputMode != COLORMODE_32BIT)
        {
            if (osTvType == TV_TYPE_MPAL)
            {
                g_ViModes[nextMode] = osViModeTable[OS_VI_MPAL_LAN1];
            }
            else
            {
                g_ViModes[nextMode] = osViModeTable[OS_VI_NTSC_LAN1];
            }
        }
        else
        {
            if (osTvType == TV_TYPE_MPAL)
            {
                g_ViModes[nextMode] = osViModeTable[OS_VI_MPAL_LAN2];
            }
            else
            {
                g_ViModes[nextMode] = osViModeTable[OS_VI_NTSC_LAN2];
            }
        }

        g_ViModes[nextMode].comRegs.width = g_ViBackData->bufx;
        g_ViModes[nextMode].comRegs.xScale = (g_ViBackData->bufx * XSCALE_MAX) / SCREEN_WIDTH_MAX;

        if (1)
        {
            g_ViModes[nextMode].fldRegs[0].yScale = (g_ViBackData->bufy * yScaleMax) / SCREEN_HEIGHT_MAX;
        }

        packedStart = g_ViModes[nextMode].comRegs.hStart;

        g_ViModes[nextMode].fldRegs[1].yScale = (g_ViBackData->bufy * yScaleMax) / SCREEN_HEIGHT_MAX;
        g_ViModes[nextMode].fldRegs[0].origin = g_ViBackData->bufx * 2;
        g_ViModes[nextMode].fldRegs[1].origin = g_ViBackData->bufx * 2;

        registerValue = packedStart;
        registerValue = ADD_LOW_AND_HI_16_MOD(registerValue, g_viHorizontalOffset);

        g_ViModes[nextMode].comRegs.hStart = registerValue;
        g_viOriginalHstart = registerValue;

        packedStart = g_ViModes[nextMode].fldRegs[0].vStart;
        registerValue = packedStart;
        registerValue = ADD_LOW_AND_HI_16_MOD(registerValue, D_800232A0);

        g_ViModes[nextMode].fldRegs[0].vStart = registerValue;
        g_viOriginalVstart0 = registerValue;

        packedStart = g_ViModes[nextMode].fldRegs[1].vStart;
        registerValue = packedStart;
        registerValue = ADD_LOW_AND_HI_16_MOD(registerValue, D_800232A0);

        g_ViModes[nextMode].fldRegs[1].vStart = registerValue;
        g_viOriginalVstart1 = registerValue;

        g_ViChangeVideoModes[nextMode] = 1;
    }
    else
    {
        g_ViChangeVideoModes[nextMode] = 0;
    }

    nextMode = (nextMode + 1) % NUM_VIDEO_FRAME_BUFFERS;
    D_800232C0 = nextMode;

    if (g_viColorOutputMode != COLORMODE_32BIT)
    {
        ((struct GfxInfo_s *)g_gfxTaskSettingsList)->cfb = g_ViBackData->framebuf;
    }
    else
    {
        ((struct GfxInfo_s *)g_gfxTaskSettingsList)->cfb = cfb_16[0];
    }

    settings = g_ViBackData;

    g_ViFrontIndex = (g_ViFrontIndex + 1) % NUM_VIDEO_SETTINGS;
    g_ViBackIndex = (g_ViBackIndex + 1) % NUM_VIDEO_SETTINGS;

    g_ViFrontData = g_ViDataArray + g_ViFrontIndex;
    g_ViBackData = g_ViDataArray + g_ViBackIndex;

    bcopy(settings, g_ViBackData, sizeof(VideoSettings));

    g_ViBackData->framebuf = cfb_16[g_ViBackIndex];
}
#endif
#ifdef VERSION_EU
void video_related_8(void)
{
    s32 new_var2;
    s32 registerValue;
    f32 calculatedYScale;
    s32 new_var;
    f32 calculatedXScale;
    VideoSettings *settings;
    s32 modeIndex;
    s32 upperAdjust;

    if (g_ViFrontData->mode != g_ViBackData->mode)
    {
        registerValue = g_ViBackData->mode;

        switch (registerValue)
        {
            case 0:
                osViSetYScale(1.0f);
                osViBlack(1);
                break;

            case 1:
                if (g_viColorOutputMode != 0)
                {
                    g_viOriginalVideoMode = osViModeTable[16];
                }
                else
                {
                    g_viOriginalVideoMode = osViModeTable[20];
                }

                viMode = &g_viOriginalVideoMode;
                g_viOriginalHstart = viMode->comRegs.hStart;
                g_viOriginalVstart0 = viMode->fldRegs[0].vStart;
                g_viOriginalVstart1 = viMode->fldRegs[1].vStart;

                frBlack(0);
                break;

            case 2:
                viMode = &osViModeTable[25];
                g_viOriginalHstart = viMode->comRegs.hStart;
                g_viOriginalVstart0 = viMode->fldRegs[0].vStart;
                g_viOriginalVstart1 = viMode->fldRegs[1].vStart;

                osViBlack(0);
                break;
        }
        osViSetSpecialFeatures(0x0040 | 0x0002);
    }

    calculatedXScale = ((f32)g_ViBackData->x) / ((f32)g_ViBackData->bufx);
    calculatedYScale = ((f32)g_ViBackData->y) / ((f32)g_ViBackData->bufy);

    if (g_ViBackData->mode == 0) { calculatedYScale = 1.0f; } modeIndex = D_800232C0;
    if (1);
    if (1);
    if (1);
    if (1);
    new_var = 0xffff;

    g_ViXScales[modeIndex] = calculatedXScale;
    g_ViYScales[modeIndex] = calculatedYScale;

    if (g_ViBackData);
    if (g_ViBackData->mode == 1)
    {
        g_ViModePtrs[modeIndex] = &g_viOriginalVideoMode;

        if (g_viColorOutputMode != 0)
        {
            g_ViModes[modeIndex] = osViModeTable[16];
        }
        else
        {
            g_ViModes[modeIndex] = osViModeTable[20];
        }

        g_ViModes[modeIndex].comRegs.width = g_ViBackData->bufx;
        g_ViModes[modeIndex].comRegs.xScale = (g_ViBackData->bufx * 0x400) / 640;
        registerValue = (g_ViBackData->bufy == 330) ? (28) : (0);
        upperAdjust = 0;
        g_ViModes[modeIndex].fldRegs[0].yScale = (g_ViBackData->bufy * 0x800) / (registerValue + 0x220);
        registerValue = (g_ViBackData->bufy == 330) ? (28) : (0);
        g_ViModes[modeIndex].fldRegs[1].yScale = (g_ViBackData->bufy * 0x800) / (registerValue + 0x220);
        g_ViModes[modeIndex].fldRegs[0].origin = g_ViBackData->bufx * 2;
        g_ViModes[modeIndex].fldRegs[1].origin = g_ViBackData->bufx * 2;

        new_var2 = g_ViModes[modeIndex].comRegs.hStart;
        registerValue = (((((new_var2 >> 16) & 0xffff) + g_viHorizontalOffset) % 0xffff) << 16) |
                        ((((u16)new_var2) + g_viHorizontalOffset) % 0xffff);
        g_ViModes[modeIndex].comRegs.hStart = registerValue;
        g_viOriginalHstart = registerValue;

        new_var2 = g_ViModes[modeIndex].fldRegs[0].vStart;
        upperAdjust = (g_ViBackData->bufy == 330) ? (-14) : (0);
        registerValue =
            ((((((g_ViBackData->bufy == 330) ? (14) : (0)) + ((u16)new_var2)) + D_800232A0) + 0x40) % new_var) |
            ((((((new_var2 >> 16) & 0xffff) + D_800232A0) + upperAdjust) % new_var) << 16);
        g_ViModes[modeIndex].fldRegs[0].vStart = registerValue;
        g_viOriginalVstart0 = registerValue;

        new_var2 = g_ViModes[modeIndex].fldRegs[1].vStart;
        upperAdjust = (g_ViBackData->bufy == 330) ? (-14) : (0);
        registerValue =
            ((((((g_ViBackData->bufy == 330) ? (14) : (0)) + ((u16)new_var2)) + D_800232A0) + 0x40) % new_var) |
            ((((((new_var2 >> 16) & 0xffff) + D_800232A0) + upperAdjust) % new_var) << 16);
        g_ViModes[modeIndex].fldRegs[1].vStart = registerValue;
        g_viOriginalVstart1 = registerValue;

        g_ViChangeVideoModes[modeIndex] = 1;
    }
    else
    {
        g_ViChangeVideoModes[modeIndex] = 0;
    }

    modeIndex = (modeIndex + 1) % 2;
    D_800232C0 = modeIndex;

    if (g_viColorOutputMode != (g_viColorOutputMode * 0))
    {
        ((struct GfxInfo_s *)g_gfxTaskSettingsList)->cfb = g_ViBackData->framebuf;
    }
    else
    {
        ((struct GfxInfo_s *)g_gfxTaskSettingsList)->cfb = cfb_16[0];
    }

    settings = g_ViBackData;

    g_ViFrontIndex = (g_ViFrontIndex + 1) % 2;
    g_ViBackIndex = (g_ViBackIndex + 1) % 2;

    g_ViFrontData = g_ViDataArray + g_ViFrontIndex;
    g_ViBackData = g_ViDataArray + g_ViBackIndex;

    bcopy(settings, g_ViBackData, sizeof(VideoSettings));

    g_ViBackData->framebuf = cfb_16[g_ViBackIndex];
}
#endif


void viShake(f32 intensity)
{
    if (intensity > 14.0f)
    {
        intensity = 14.0f;
    }

    if (intensity < 0.0f)
    {
        intensity = 0.0f;
    }

    g_ViShakeIntensity = (s32)intensity;
    g_ViShakeTimer = 10;
}

void viRecvMesg(int count)
{
    do
    {
        osRecvMesg(&vi_c_debug_MQ, NULL, 1);
        count--;
    }
    while (count > 0);
}

void viSetVideoMode(s32 m)
{
    //if (m < 0) {
    //    assertPrint_8291E690(".\\ported\\fr.cpp",0x22a,"Assertion failed: m>=MD_BLACK");
    //}
    //if (2 < iStack00000014) {
    //    assertPrint_8291E690(".\\ported\\fr.cpp",0x22b,"Assertion failed: m<MD_MAXIMUM");
    //}
    g_ViBackData->mode = m;
    g_ViBackData->x = g_ViBackData->bufx = g_viRuntimeScreenWidths[m];
    g_ViBackData->y = g_ViBackData->bufy = g_viRuntimeScreenHeights[m];
}

void viSetColorMode16Bit(void)
{
    g_viColorOutputMode = COLORMODE_16BIT;
}

void viSetColorMode32Bit(void)
{
    g_viColorOutputMode = COLORMODE_32BIT;
}

u8 *viGetFrameBuf2(void)
{
    return g_ViBackData->framebuf;
}

u8 *viGetFrameBuf1(void)
{
    return g_ViFrontData->framebuf;
}

void viSetFrameBuf2(u8 *buf)
{
    g_ViBackData->framebuf = buf;
}

u16 viGetPerspNorm(void)
{
    return g_viPerspNorm;
}


Gfx *viSetupCurrentPlayerView(Gfx *gdl)
{
    if (g_CurrentPlayer != NULL)
    {
        g_CurrentPlayer->viewports[g_ViBackIndex].vp.vscale[0] = (g_ViBackData->viewx * 2);
        g_CurrentPlayer->viewports[g_ViBackIndex].vp.vtrans[0] = (g_ViBackData->viewx * 2) + (g_ViBackData->viewleft * 4);
        g_CurrentPlayer->viewports[g_ViBackIndex].vp.vscale[1] = (g_ViBackData->viewy * 2);
        g_CurrentPlayer->viewports[g_ViBackIndex].vp.vtrans[1] = (g_ViBackData->viewy * 2) + (g_ViBackData->viewtop * 4);
    }

    // Make the RSP's viewport transform the current player's screen rectangle.
    gSPViewport(gdl++, OS_K0_TO_PHYSICAL(&g_CurrentPlayer->viewports[g_ViBackIndex]));

    // Create both a floating-point matrix for the CPU side and a fixed-point matrix for the RSP.
    g_viProjectionMatrix = dynAllocateMatrix();
#ifndef TARGET_N64
    { /* GE_GLFARX=n (diag): scale ONLY the projection far plane - zrange (portal/room
       * culls, fog params) stays vanilla - to isolate GL far-plane clipping */
        extern char *getenv(const char *);
        static f32 glfarx = -1.0f;
        if (glfarx < 0.0f) { const char *e = getenv("GE_GLFARX"); glfarx = (e != NULL) ? (f32) atof(e) : 1.0f; if (glfarx < 1.0f) glfarx = 1.0f; }
        guPerspectiveF(g_viProjectionMatrixF, &g_viPerspNorm, g_ViBackData->fovy, g_ViBackData->aspect, g_ViBackData->znear, g_ViBackData->zfar * glfarx, 1.0f);
    }
#else
    guPerspectiveF(g_viProjectionMatrixF, &g_viPerspNorm, g_ViBackData->fovy, g_ViBackData->aspect, g_ViBackData->znear, g_ViBackData->zfar, 1.0f);
#endif
    guMtxF2L(g_viProjectionMatrixF, g_viProjectionMatrix);

    /** 
     * Load g_viProjectionMatrix as the current projection matrix.
     * All subsequent rendering uses this projection until a another projection matrix is loaded.
     */
    gSPMatrix(gdl++, OS_K0_TO_PHYSICAL(g_viProjectionMatrix), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);
    gSPPerspNormalize(gdl++, g_viPerspNorm);

    // Store the float and non-float projection matrices so we can recall them later instead of having to rebuild them.
    currentPlayerSetProjectionMatrix(g_viProjectionMatrix);
    currentPlayerSetProjectionMatrixF(g_viProjectionMatrixF);

    // Normal rendering mode is a 16-bit RGBA image.
    if (g_viColorOutputMode != COLORMODE_32BIT)
    {
        gDPSetColorImage(gdl++, G_IM_FMT_RGBA, G_IM_SIZ_16b, g_ViBackData->bufx, OS_K0_TO_PHYSICAL(g_ViBackData->framebuf));
    }
    // 32-bit mode is only used for the 32-bit screenshot routines.
    else
    {
        gDPSetColorImage(gdl++, G_IM_FMT_RGBA, G_IM_SIZ_32b, g_ViBackData->bufx, OS_K0_TO_PHYSICAL(cfb_16[0]));
    }

    return gdl;
}


Gfx *viClearZBufCurrentPlayer(Gfx *gdl)
{
    if (g_ViBackData->usezbuf != 0)
    {
        gdl = zbufClearCurrentPlayer(zbufInit(gdl));
    }

    return gdl;
}

Gfx *viFillScreen(Gfx *gdl)
{
	gDPSetCycleType(gdl++, G_CYC_FILL);
	gDPFillRectangle(gdl++, 0, 0, (g_ViBackData->bufx - 1), (g_ViBackData->bufy - 1));
	gDPPipeSync(gdl++);

    return gdl;
}

Gfx *viSetupScreensForNumPlayers(Gfx *gdl)
{
    gDPSetCycleType(gdl++, G_CYC_FILL);
    gDPSetScissor(gdl++, G_SC_NON_INTERLACE, 0, 0, viGetX(), viGetY());
    gDPSetFillColor(gdl++, ((GPACK_RGBA5551(0,0,0,1) << 16) | GPACK_RGBA5551(0,0,0,1)));

    if (viGetViewTop() > 0)
    {
        if (get_player_position_in_shuffled(get_cur_playernum()) == 0)
        {
            gDPFillRectangle(gdl++, 0, 0, (viGetX() - 1), (viGetViewTop() - 1));
            gDPPipeSync(gdl++);
        }
    }

    if (getPlayerCount() == 1)
    {
        if ((viGetViewTop() + viGetViewHeight()) < viGetY())
        {
            gDPFillRectangle(gdl++, 0, (viGetViewTop() + viGetViewHeight()), (viGetX() - 1), (viGetY() - 1));
            gDPPipeSync(gdl++);
        }
    }
    else
    {
        if (get_player_position_in_shuffled(get_cur_playernum()) == 0)
        {
            if ((viGetViewTop() + viGetViewHeight() + viGetViewHeight()) < viGetY())
            {
                gDPFillRectangle(gdl++, 0, (viGetViewTop() + viGetViewHeight() + viGetViewHeight()), (viGetX() - 1), (viGetY() - 1));
                gDPPipeSync(gdl++);
            }

            gDPFillRectangle(gdl++, 0, ((SCREEN_HEIGHT / 2) - 1), (viGetX() - 1), (SCREEN_HEIGHT / 2));
            gDPPipeSync(gdl++);

            if (getPlayerCount() >= 3)
            {
                /* PC WIDESCREEN: the vertical divider must track the live
                 * view width (viGetX == SCREEN_WIDTH on N64) */
                gDPFillRectangle(gdl++, ((viGetX() / 2) - 1), 0, (viGetX() / 2), (viGetY() - 1));
                gDPPipeSync(gdl++);
            }

            if (getPlayerCount() == 3)
            {
                gDPFillRectangle(gdl++, ((viGetX() / 2) + 1), ((SCREEN_HEIGHT / 2) + 1), (viGetX() - 1), (viGetY() - 1));
                gDPPipeSync(gdl++);
            }
        }
    }

    return gdl;
}

void viSet800232A0(s32 arg0)
{
    D_800232A0 = arg0;
}

s32 viGet800232A0(void)
{
    return D_800232A0;
}

void viSetHorizontalOffset(s32 arg0)
{
    g_viHorizontalOffset = arg0;
}

s32 viGetHorizontalOffset(void)
{
    return g_viHorizontalOffset;
}

void viSetBuf(s16 x, s16 y)
{
    g_ViBackData->bufx = x;
    g_ViBackData->bufy = y;
}

s16 viGetBufX(void)
{
    return g_ViBackData->bufx;
}

s16 viGetBufY(void)
{
    return g_ViBackData->bufy;
}

void viSetXY(s16 x, s16 y)
{
    g_ViBackData->x = x;
    g_ViBackData->y = y;
}

s16 viGetX(void)
{
    return g_ViBackData->x;
}

s16 viGetY(void)
{
    return g_ViBackData->y;
}

#ifndef TARGET_N64
/* PORT: netplay job 3c: in a co-op stage the player's CAMERA MODEL (the c_* fields: screen rectangle, perspective, frustum
 * planes, room visibility, onscreen tests, autoaim, the aim-to-ray maths) is a canonical, fixed view, so the simulation
 * does not depend on the window, the aspect or the FOV setting of the machine. The back data (g_ViBackData) keeps the
 * REAL view: the display list's viewport and projection (viSetupCurrentPlayerView) and the HUD layout. */
extern s32 portSimViewOn(void);          /* port_coop.c: a co-op stage is running */
extern f32 portSimViewAspect(void);      /* port_coop.c: the canonical aspect (16:9) */
extern f32 portSimModelFovY(f32 fovy);   /* port_coop.c: the zoom-chain fov as the canonical camera widens it (the 240-line view) */
extern f32 portSimDisplayFovY(f32 fovy, s32 realViewY);   /* port_coop.c: canonical fov -> the fov the window draws */
static f32 sViSimFov[4] = { 60.0f, 60.0f, 60.0f, 60.0f };   /* the zoom-chain fov per player (what the sim reads as viGetFovY) */

static void viApplyCameraModel(void)
{
    f32 fovy = g_ViBackData->fovy;
    f32 aspect = g_ViBackData->aspect;

    if (portSimViewOn() && g_CurrentPlayer != NULL)
    {
        fovy = g_CurrentPlayer->c_perspfovy;   /* the model's own, set by viSetFovY */
        aspect = portSimViewAspect();
    }
    currentPlayerSetPerspective(g_ViBackData->znear, fovy, aspect);
    currentPlayerSetCameraScale();
}

/* the real view equals the canonical one (320x240 at 0,0, 16:9, the same fov): per-room scissors, computed in the canonical
 * camera's screen space, are then exact. Also the default PC window (full height, 16:9). */
s32 viSimViewMatchesDraw(void)
{
    return !portSimViewOn()
        || (g_ViBackData->viewx == 320 && g_ViBackData->viewy == 240 && g_ViBackData->viewleft == 0 && g_ViBackData->viewtop == 0
            && g_CurrentPlayer != NULL && g_ViBackData->fovy == g_CurrentPlayer->c_perspfovy
            && g_ViBackData->aspect > portSimViewAspect() - 0.001f && g_ViBackData->aspect < portSimViewAspect() + 0.001f);
}

/* PORT: netplay job 7: the RENDER walk (bg.c bgRoomVisibilityRelated) runs the portal walk with the view's REAL camera model, the one a solo game
 * would have: the window's view rectangle, display fov and aspect. These two swap the current player's c_* block (screen rectangle, perspective,
 * scales, frustum normals: bondview.h c_screenwidth .. c_cameraleftnorm) to the real values and back, so the canonical model the sim reads is
 * untouched outside the render walk. */
#include <string.h>
#include <stddef.h>
#define PORT_CAMBLOCK_BYTES ((int) (offsetof(struct player, c_cameraleftnorm) + sizeof(coord3d) - offsetof(struct player, c_screenwidth)))

_Static_assert(PORT_CAMBLOCK_BYTES <= 256, "bgRoomVisibilityRelated saves the camera block in a 256-byte buffer");

void viSimCameraToReal(void *save)
{
    memcpy(save, &g_CurrentPlayer->c_screenwidth, PORT_CAMBLOCK_BYTES);
    currentPlayerSetScreenSize(g_ViBackData->viewx, g_ViBackData->viewy);
    currentPlayerSetScreenPosition(g_ViBackData->viewleft, g_ViBackData->viewtop);
    currentPlayerSetPerspective(g_ViBackData->znear, g_ViBackData->fovy, g_ViBackData->aspect);
    currentPlayerSetCameraScale();
}

void viSimCameraRestore(const void *save)
{
    memcpy(&g_CurrentPlayer->c_screenwidth, save, PORT_CAMBLOCK_BYTES);
}

/* PORT: heist CCTV view (port_cctvview.c): the same swap for a security camera's walk, with the camera's own fov. The block also holds
 * the world-to-screen matrix pointer (field_10CC), so the caller may point it at the camera's for the walk; Restore puts Bond's back. */
void viCctvCameraModel(void *save, f32 fovy)
{
    memcpy(save, &g_CurrentPlayer->c_screenwidth, PORT_CAMBLOCK_BYTES);
    currentPlayerSetScreenSize(g_ViBackData->viewx, g_ViBackData->viewy);
    currentPlayerSetScreenPosition(g_ViBackData->viewleft, g_ViBackData->viewtop);
    currentPlayerSetPerspective(g_ViBackData->znear, fovy, g_ViBackData->aspect);
    currentPlayerSetCameraScale();
}

/* the display projection a camera feed draws with: the window's aspect and z range, the camera's fov */
void viCctvPerspectiveF(f32 mf[4][4], f32 fovy)
{
    u16 norm;

    guPerspectiveF(mf, &norm, fovy, g_ViBackData->aspect, g_ViBackData->znear, g_ViBackData->zfar, 1.0f);
}
#endif

void viSetViewSize(s16 x, s16 y)
{
    g_ViBackData->viewx = x;
    g_ViBackData->viewy = y;
#ifndef TARGET_N64
    if (portSimViewOn())
    {
        currentPlayerSetScreenSize(320, 240);   /* the canonical rectangle: the PC default (full height) */
        currentPlayerSetCameraScale();
        return;
    }
#endif
    currentPlayerSetScreenSize(g_ViBackData->viewx, g_ViBackData->viewy);
    currentPlayerSetCameraScale();
}

s16 viGetViewWidth(void)
{
    return g_ViBackData->viewx;
}

s16 viGetViewHeight(void)
{
    return g_ViBackData->viewy;
}

void viSetViewPosition(s16 left, s16 top)
{
    g_ViBackData->viewleft = left;
    g_ViBackData->viewtop = top;
#ifndef TARGET_N64
    if (portSimViewOn())
    {
        currentPlayerSetScreenPosition(0, 0);   /* canonical */
        return;
    }
#endif
    currentPlayerSetScreenPosition(g_ViBackData->viewleft, g_ViBackData->viewtop);
}

s16 viGetViewLeft(void)
{
    return g_ViBackData->viewleft;
}

s16 viGetViewTop(void)
{
    return g_ViBackData->viewtop;
}

void viSetUseZBuf(s32 usezbuf)
{
  g_ViBackData->usezbuf = usezbuf;
}

void viSetFovY(f32 fovy)
{
#ifndef TARGET_N64
    if (portSimViewOn() && g_CurrentPlayer != NULL)
    {   /* fovy is the canonical zoom-chain value: the camera model widens it for the 240-line view, the window draws its display fov */
        const s32 pn = get_cur_playernum();

        sViSimFov[(pn >= 0 && pn < 4) ? pn : 0] = fovy;
        g_CurrentPlayer->c_perspfovy = portSimModelFovY(fovy);
        g_ViBackData->fovy = portSimDisplayFovY(fovy, g_ViBackData->viewy);
        viApplyCameraModel();
        return;
    }
#endif
    g_ViBackData->fovy = fovy;
    currentPlayerSetPerspective(g_ViBackData->znear, g_ViBackData->fovy, g_ViBackData->aspect);
    currentPlayerSetCameraScale();
}

void viSetAspect(f32 aspect)
{
    g_ViBackData->aspect = aspect;
#ifndef TARGET_N64
    if (portSimViewOn())
    {
        viApplyCameraModel();
        return;
    }
#endif
    currentPlayerSetPerspective(g_ViBackData->znear, g_ViBackData->fovy, g_ViBackData->aspect);
    currentPlayerSetCameraScale();
}

f32 viGetFovY(void)
{
#ifndef TARGET_N64
    if (portSimViewOn() && g_CurrentPlayer != NULL)
    {
        const s32 pn = get_cur_playernum();

        return sViSimFov[(pn >= 0 && pn < 4) ? pn : 0];   /* the zoom-chain fov, whatever the window draws */
    }
#endif
    return g_ViBackData->fovy;
}

void viSetFov(f32 fovx, f32 fovy)
{
    g_ViBackData->fovy = fovy;
    g_ViBackData->aspect = (f32) (fovx / fovy);
    currentPlayerSetPerspective(g_ViBackData->znear, g_ViBackData->fovy, g_ViBackData->aspect);
    currentPlayerSetCameraScale();
}

void viSetZRange(f32 near, f32 far)
{
    g_ViBackData->znear = near;
    g_ViBackData->zfar = far;
#ifndef TARGET_N64
    if (portSimViewOn())
    {
        viApplyCameraModel();
        return;
    }
#endif
    currentPlayerSetPerspective(g_ViBackData->znear, g_ViBackData->fovy, g_ViBackData->aspect);
    currentPlayerSetCameraScale();
}

void viGetZRange(f32 *zrange)
{
    zrange[0] = g_ViBackData->znear;
    zrange[1] = g_ViBackData->zfar;
}

Gfx *viSetFillColor(Gfx *gdl, s32 r, s32 g, s32 b)
{
#ifndef TARGET_N64
    /* The sky backdrop fill rect loses precision in the RGBA5551 fill colour,
     * so on PC it shows as a visible line against the exact-colour cloud/water
     * fans. Pass the exact 8-bit colour to the renderer in a tagged NOOP that
     * must sit directly before the SETFILLCOLOR. */
    gImmp1(gdl++, G_NOOP,
           0x46000000u | (((u32) r & 0xFF) << 16) | (((u32) g & 0xFF) << 8) | ((u32) b & 0xFF));
#endif

    if (g_viColorOutputMode != COLORMODE_32BIT)
    {
        gDPSetFillColor(gdl++, ((GPACK_RGBA5551(r, g, b, 1) << 16) | GPACK_RGBA5551(r, g, b, 1)));
    }
    else
    {
        gDPSetFillColor(gdl++, ((r << 24) | (g << 16) | (b << 8) | 0xFF));
    }

    return gdl;
}

/**
 * 5358	70004758
 *     image capture routine, jpeg 16bit colour
 */
void indyGrabJpg16bit(void)
{
#ifdef LEFTOVERDEBUG
    s32 *pgrabnum = &g_indyJpg16BitGrabnum;
    char buffer[250];
    s32 filesize;

    while (1)
    {
        sprintf(buffer, "grab.%d.jpeg", *pgrabnum);
        if (indycommHostCheckFileExists((u8*)&buffer, &filesize) == 0)
        {
            break;
        }

        *pgrabnum = *pgrabnum + 1;
    }

    sprintf(buffer, "grab.%d.temp.uix", *pgrabnum);
    indycommHostSendDump((u8*)&buffer, (u8*)g_ViBackData->framebuf, (viGetX() * viGetY() * 2));

    sprintf(buffer, "uix2pix grab.%d.temp.uix", *pgrabnum);
    indycommHostSendCmd((u8*)&buffer);

    sprintf(buffer, "fromalias grab.%d.temp.pix grab.%d.temp.rgb", *pgrabnum, *pgrabnum);
    indycommHostSendCmd((u8*)&buffer);

    sprintf(buffer, "imgcopy -fjfif grab.%d.temp.rgb grab.%d.jpeg", *pgrabnum, *pgrabnum);
    indycommHostSendCmd((u8*)&buffer);

    sprintf(buffer, "rm grab.%d.temp.uix grab.%d.temp.pix grab.%d.temp.rgb", *pgrabnum, *pgrabnum, *pgrabnum);
    indycommHostSendCmd((u8*)&buffer);

    sprintf(buffer, "imgview grab.%d.jpeg", *pgrabnum);
    indycommHostSendCmd((u8*)&buffer);
#endif
}

/**
 * 54D4	700048D4
 *     image capture routine, jpeg 32bit colour
 */
void indyGrabJpg32bit(void)
{
    #if defined(LEFTOVERDEBUG)
    s32 *pgrabnum = &g_indyJpg32BitGrabnum;
    char buffer[250];
    s32 filesize;

    while (1)
    {
        sprintf(buffer, "grab.%d.jpeg", *pgrabnum);
        if (indycommHostCheckFileExists((u8*)&buffer, &filesize) == 0)
        {
            break;
        }

        *pgrabnum = *pgrabnum + 1;
    }

    sprintf(buffer, "grab.%d.temp.Uix", *pgrabnum);
    indycommHostSendDump((u8*)&buffer, (u8*)&cfb_16, (viGetX() * viGetY() * 4));

    sprintf(buffer, "Uix2pix -xs%d grab.%d.temp.Uix", viGetX(), *pgrabnum);
    indycommHostSendCmd((u8*)&buffer);

    sprintf(buffer, "fromalias grab.%d.temp.pix grab.%d.temp.rgb", *pgrabnum, *pgrabnum);
    indycommHostSendCmd((u8*)&buffer);

    sprintf(buffer, "imgcopy -fjfif grab.%d.temp.rgb grab.%d.jpeg", *pgrabnum, *pgrabnum);
    indycommHostSendCmd((u8*)&buffer);

    sprintf(buffer, "rm grab.%d.temp.Uix grab.%d.temp.pix grab.%d.temp.rgb", *pgrabnum, *pgrabnum, *pgrabnum);
    indycommHostSendCmd((u8*)&buffer);

    sprintf(buffer, "imgview grab.%d.jpeg", *pgrabnum);
    indycommHostSendCmd((u8*)&buffer);
    #endif
}

/**
 * 5660	70004A60
 *     image capture routine, rgb 16bit colour
 */
void indyGrabRgb16bit(void)
{
    #if defined(LEFTOVERDEBUG)
    s32 *pgrabnum = &g_indyRgb16BitGrabnum;
    char buffer[250];
    s32 filesize;

    while (1)
    {
        sprintf(buffer, "grab.%d.rgb", *pgrabnum);
        if (indycommHostCheckFileExists((u8*)&buffer, &filesize) == 0)
        {
            break;
        }

        *pgrabnum = *pgrabnum + 1;
    }

    sprintf(buffer, "grab.%d.temp.uix", *pgrabnum);
    indycommHostSendDump((u8*)&buffer, (u8*)g_ViBackData->framebuf, (viGetX() * viGetY() * 2));

    sprintf(buffer, "uix2pix grab.%d.temp.uix", *pgrabnum);
    indycommHostSendCmd((u8*)&buffer);

    sprintf(buffer, "fromalias grab.%d.temp.pix grab.%d.rgb", *pgrabnum, *pgrabnum);
    indycommHostSendCmd((u8*)&buffer);

    sprintf(buffer, "rm grab.%d.temp.uix grab.%d.temp.pix", *pgrabnum, *pgrabnum);
    indycommHostSendCmd((u8*)&buffer);

    sprintf(buffer, "imgview grab.%d.rgb", *pgrabnum);
    indycommHostSendCmd((u8*)&buffer);
#endif
}

/**
 * 57B4	70004BB4
 *     image capture routine, rgb 32bit colour
 */
void indyGrabRgb32bit(void)
{
    #if defined(LEFTOVERDEBUG)
    s32 *pgrabnum = &g_indyRgb32BitGrabnum;
    char buffer[250];
    s32 filesize;

    while (1)
    {
        sprintf(buffer, "grab.%d.rgb", *pgrabnum);
        if (indycommHostCheckFileExists((u8*)&buffer, &filesize) == 0)
        {
            break;
        }

        *pgrabnum = *pgrabnum + 1;
    }

    sprintf(buffer, "grab.%d.temp.Uix", *pgrabnum);
    indycommHostSendDump((u8*)&buffer, (u8*)&cfb_16, (viGetX() * viGetY() * 4));

    sprintf(buffer, "Uix2pix -xs%d grab.%d.temp.Uix", viGetX(), *pgrabnum);
    indycommHostSendCmd((u8*)&buffer);

    sprintf(buffer, "fromalias grab.%d.temp.pix grab.%d.rgb", *pgrabnum, *pgrabnum);
    indycommHostSendCmd((u8*)&buffer);

    sprintf(buffer, "rm grab.%d.temp.Uix grab.%d.temp.pix", *pgrabnum, *pgrabnum);
    indycommHostSendCmd((u8*)&buffer);

    sprintf(buffer, "imgview grab.%d.rgb", *pgrabnum);
    indycommHostSendCmd((u8*)&buffer);
    #endif
}
