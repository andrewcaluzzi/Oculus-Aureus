/* port_arenarain_draw.c - the RAIN mutator's picture (THUNDERBALL; the sim side is port_arenarain.c): the light dims, thin slanted
 * streaks fall around the camera and small rings splash on the sand near it. Drawn in the console's 2D space over the 3D picture,
 * before the HUD (port_console.c), with port_hudstyle.c's fills: no new art, no textures. The drops live in world space (a cylinder
 * around the camera, re-seeded as they land or fall behind), so they have parallax; each streak is the projection of the drop and of
 * where it was a moment ago, drawn as a dotted line of 1-unit dots; a ring is 14 projected dots on the floor. The amount (0..1) is
 * the sim's roll-in / roll-out. PRESENTATION ONLY (snap_embed.py PRES): its own RNG and clock, never read by the sim. */
#ifndef TARGET_N64

#include <math.h>
#include <string.h>
#include <ultra64.h>
#include <bondtypes.h>

#include "game/player.h"
#include "port_hudstyle.h"
#include "port_mode.h"

extern float portArenaRainAmount(void);
extern Mtxf *camGetWorldToScreenMtxf(void);
extern Mtxf *currentPlayerGetProjectionMatrixF(void);
extern coord3d *bondviewGetCurrentPlayersPosition(void);
extern s16 bondviewGetCurrentPlayerViewportWidth(void);
extern s16 bondviewGetCurrentPlayerViewportHeight(void);
extern s16 bondviewGetCurrentPlayerViewportUly(void);
extern s16 get_curplayer_viewport_ulx(void);
extern s16 getWidth320or440(void);
extern s16 getHeight330or240(void);
extern s32 g_GlobalTimer;

#define DROPS    280
#define SPLASHES 28
#define RADIUS   1500.0f   /* drops fall within this of the camera */
#define TOP      1100.0f   /* ... from up to this above the floor */
#define FALL     2300.0f   /* units a second, down */
#define WINDX    380.0f    /* and sideways (the slant) */
#define WINDZ    140.0f
#define STREAK   0.030f    /* a streak is where the drop was this long ago */

typedef struct { f32 x, y, z; s32 ok; } Drop;
typedef struct { f32 x, y, z, age; } Splash;

static Drop sDrop[DROPS];
static Splash sSplash[SPLASHES];
static s32 sSplashI;
static u32 sRng = 0x2545F491u;
static s32 sLastTimer = -1;
static f32 sW, sH;

static f32 rnd(void) { sRng = sRng * 1664525u + 1013904223u; return (f32) (sRng >> 8) / 16777216.0f; }   /* 0..1 */

/* world -> console space (port_gadgets.c's projection); 0 when behind the camera */
static int project(f32 x, f32 y, f32 z, f32 *sx, f32 *sy)
{
    Mtxf *cam = camGetWorldToScreenMtxf(), *proj = currentPlayerGetProjectionMatrixF();
    const f32 w[3] = { x, y, z };
    f32 v[4], c[4], vx, vy, vw, vh, fw, fh;
    int i, j;

    if (cam == NULL || proj == NULL) return 0;
    for (j = 0; j < 4; j++) v[j] = w[0] * cam->m[0][j] + w[1] * cam->m[1][j] + w[2] * cam->m[2][j] + cam->m[3][j];
    for (j = 0; j < 4; j++)
    {
        c[j] = 0.0f;
        for (i = 0; i < 4; i++) c[j] += v[i] * proj->m[i][j];
    }
    if (c[3] <= 20.0f) return 0;
    fw = (f32) getWidth320or440();
    fh = (f32) getHeight330or240();
    vx = (f32) get_curplayer_viewport_ulx() / fw * sW;
    vy = (f32) bondviewGetCurrentPlayerViewportUly() / fh * sH;
    vw = (f32) bondviewGetCurrentPlayerViewportWidth() / fw * sW;
    vh = (f32) bondviewGetCurrentPlayerViewportHeight() / fh * sH;
    *sx = vx + (c[0] / c[3] + 1.0f) * 0.5f * vw;
    *sy = vy + (1.0f - c[1] / c[3]) * 0.5f * vh;
    return 1;
}

static void seed(Drop *d, const coord3d *cam, f32 floor, int anywhere)
{
    const f32 a = rnd() * 6.2831853f, r = sqrtf(rnd()) * RADIUS;

    d->x = cam->x + cosf(a) * r - WINDX * 0.3f;   /* (upwind a little: the slant carries them across) */
    d->z = cam->z + sinf(a) * r - WINDZ * 0.3f;
    d->y = floor + (anywhere ? rnd() : 0.85f + rnd() * 0.15f) * TOP;
    d->ok = 1;
}

/* a dotted line between two console points */
static void dots(HudCanvas *c, f32 x0, f32 y0, f32 x1, f32 y1, u32 rgba)
{
    const f32 dx = x1 - x0, dy = y1 - y0;
    const f32 len = fabsf(dx) > fabsf(dy) ? fabsf(dx) : fabsf(dy);
    s32 i, n = (s32) (len / 1.5f) + 1;

    if (n > 24) n = 24;
    for (i = 0; i <= n; i++)
    {
        const f32 t = n > 0 ? (f32) i / (f32) n : 0.0f;
        const s32 x = (s32) (x0 + dx * t), y = (s32) (y0 + dy * t);

        hudFill(c, x, y, x + 1, y + 1, rgba);
    }
}

/* port_console.c, over the 3D picture and before the HUD */
Gfx *portArenaRainRender(Gfx *gdl)
{
    const f32 amt = portArenaRainAmount();
    HudCanvas c;
    const coord3d *cam;
    f32 dt, floor;
    s32 i, k;

    if (amt <= 0.002f || !portModeActive() || g_CurrentPlayer == NULL || (cam = bondviewGetCurrentPlayersPosition()) == NULL)
    {
        sLastTimer = -1;
        return gdl;
    }
    dt = sLastTimer < 0 ? 0.0f : (f32) (g_GlobalTimer - sLastTimer) / 60.0f;
    if (dt < 0.0f || dt > 0.25f) dt = sLastTimer < 0 ? 0.0f : 0.25f;
    sLastTimer = g_GlobalTimer;
    floor = g_CurrentPlayer->stanHeight;

    gdl = hudBegin(gdl, &c);
    if (!c.ok)
    {
        return gdl;
    }
    sW = (f32) c.w;
    sH = (f32) c.h;

    /* the light: an overcast dim over everything (the gun too) */
    hudFill(&c, 0, 0, c.w, c.h, hudAlpha(0x0E121CFFu, 0.26f * amt));

    /* the splashes: rings spreading on the sand, fading */
    for (i = 0; i < SPLASHES; i++)
    {
        Splash *s = &sSplash[i];
        f32 r, a;

        if (s->age <= 0.0f) continue;
        s->age += dt / 0.35f;
        if (s->age >= 1.0f) { s->age = 0.0f; continue; }
        r = 6.0f + 34.0f * s->age;
        a = (1.0f - s->age) * 0.55f * amt;
        for (k = 0; k < 14; k++)
        {
            const f32 ang = (f32) k * 0.4487989f;
            f32 sx, sy;

            if (project(s->x + cosf(ang) * r, s->y + 1.0f, s->z + sinf(ang) * r, &sx, &sy))
            {
                hudFill(&c, (s32) sx, (s32) sy, (s32) sx + 1, (s32) sy + 1, hudAlpha(0xD8E2F0FFu, a));
            }
        }
    }

    /* the drops: fall, re-seed, draw */
    {
        const s32 n = (s32) ((f32) DROPS * (0.25f + 0.75f * amt));
        const u32 col = hudAlpha(0xBCC8D8FFu, 0.42f * amt);

        for (i = 0; i < n; i++)
        {
            Drop *d = &sDrop[i];
            const f32 ddx = d->x - cam->x, ddz = d->z - cam->z;
            f32 x0, y0, x1, y1;

            if (!d->ok || ddx * ddx + ddz * ddz > RADIUS * RADIUS * 1.3f)
            {
                seed(d, cam, floor, !d->ok);
            }
            d->x += WINDX * dt;
            d->z += WINDZ * dt;
            d->y -= FALL * dt;
            if (d->y <= floor)
            {   /* landed: a splash if it is near enough to see, then up again */
                const f32 lx = d->x - cam->x, lz = d->z - cam->z;

                if (lx * lx + lz * lz < 900.0f * 900.0f && rnd() < 0.5f)
                {
                    Splash *s = &sSplash[sSplashI];

                    sSplashI = (sSplashI + 1) % SPLASHES;
                    s->x = d->x;
                    s->y = floor;
                    s->z = d->z;
                    s->age = 0.001f;
                }
                seed(d, cam, floor, 0);
                continue;
            }
            if (project(d->x, d->y, d->z, &x0, &y0) &&
                project(d->x - WINDX * STREAK, d->y + FALL * STREAK, d->z - WINDZ * STREAK, &x1, &y1))
            {
                if (x0 > -20.0f && x0 < sW + 20.0f && y0 > -20.0f && y0 < sH + 20.0f)
                {
                    dots(&c, x1, y1, x0, y0, col);
                }
            }
        }
    }
    return hudEnd(gdl, &c);
}

#endif /* !TARGET_N64 */
