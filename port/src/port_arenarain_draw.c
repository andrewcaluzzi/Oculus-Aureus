/* port_arenarain_draw.c - the RAIN mutator's picture (THUNDERBALL; the sim side is port_arenarain.c): the light dims, thin slanted
 * streaks fall around the camera and small rings splash on the sand near it. Drawn in the console's 2D space over the 3D picture,
 * before the HUD (port_console.c), with port_hudstyle.c's fills: no new art, no textures. The drops live in world space (a cylinder
 * around the camera, re-seeded as they land or fall behind), so they have parallax; each streak is the projection of the drop and of
 * where it was a moment ago, drawn as a dotted line of 1-unit dots; a ring is 14 projected dots on the floor. The amount (0..1) is
 * the sim's roll-in / roll-out.
 * The LIGHTNING (arena.strike; the strikes are sim records in port_arenarain.c, read here): on its fuse a strike's point crackles
 * (a flickering ring of dots on the floor); when it lands, a full-screen white flash that fades out in under half a second (with
 * one re-strobe), a jagged bolt from high above down to the point (projected world-space segments, a glow and a white core, its
 * shape re-jagged every few frames, flickering out after 0.3 s), a ring of light on the floor, and the thunder: a sharp crack and
 * a low rumble of stock explosion sounds at low pitch (port_gamesfx.c; the explosion's own sound is the sim's).
 * PRESENTATION ONLY (snap_embed.py PRES): its own RNG and clock, never read by the sim. */
#ifndef TARGET_N64

#include <math.h>
#include <string.h>
#include <ultra64.h>
#include <bondtypes.h>

#include "game/player.h"
#include "port_hudstyle.h"
#include "port_mode.h"

extern float portArenaRainAmount(void);
extern int portArenaStrikePending(int i, float *x, float *y, float *z, float *gone);   /* port_arenarain.c */
extern unsigned portArenaStrikeLanded(int i, float *x, float *y, float *z);
extern int portGameSfxId(const char *name);                                           /* port_gamesfx.c */
extern int portGameSfxPlay(int sfx, float vol, float pitch, float delay, float pan);
extern int portGameSfxBurst(const int *ids, int n, int lo, int hi, float vol, float plo, float phi, float glo, float ghi, float delay,
                            float panSpread);
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

/* ---- the lightning ---- */
#define BOLTS       4         /* (port_arenarain.c keeps the last 4 landed strikes) */
#define STRIKES     8         /* ... and up to 8 on their fuse */
#define FLASH_SECS  0.45f
#define BOLT_SECS   0.30f
#define BOLT_TOP    3200.0f   /* the bolt comes down from this far above the floor */
#define BOLT_SEGS   14

typedef struct { f32 x, y, z; s32 t0; u32 serial; } Bolt;

static Bolt sBolt[BOLTS];

static f32 hrnd(u32 *s) { *s = *s * 1664525u + 1013904223u; return (f32) (*s >> 8) / 16777216.0f; }   /* 0..1, a given seed */

/* a line of w-wide squares between two console points (the bolt) */
static void bar(HudCanvas *c, f32 x0, f32 y0, f32 x1, f32 y1, s32 w, u32 rgba)
{
    const f32 dx = x1 - x0, dy = y1 - y0;
    const f32 len = fabsf(dx) > fabsf(dy) ? fabsf(dx) : fabsf(dy);
    const f32 step = w > 2 ? (f32) w * 0.6f : 1.0f;
    s32 i, n = (s32) (len / step) + 1;

    if (n > 400) n = 400;
    for (i = 0; i <= n; i++)
    {
        const f32 t = (f32) i / (f32) n;
        const s32 x = (s32) (x0 + dx * t) - w / 2, y = (s32) (y0 + dy * t) - w / 2;

        hudFill(c, x, y, x + w, y + w, rgba);
    }
}

/* a ring of dots on the floor */
static void ring(HudCanvas *c, f32 x, f32 y, f32 z, f32 r, s32 n, f32 jitter, u32 *seed, s32 w, u32 rgba)
{
    s32 k;

    for (k = 0; k < n; k++)
    {
        const f32 ang = (f32) k * (6.2831853f / (f32) n), rr = r * (1.0f + (hrnd(seed) - 0.5f) * jitter);
        f32 sx, sy;

        if (project(x + cosf(ang) * rr, y + 2.0f, z + sinf(ang) * rr, &sx, &sy))
        {
            hudFill(c, (s32) sx - w / 2, (s32) sy - w / 2, (s32) sx - w / 2 + w, (s32) sy - w / 2 + w, rgba);
        }
    }
}

/* the thunder: a sharp crack (a stock blast pitched up) and a low rumble (stock explosions pitched down, staggered) */
static void thunder(void)
{
    static int ids[3], crack;

    if (crack == 0)
    {
        crack = portGameSfxId("EXPLOSION_2A");
        ids[0] = portGameSfxId("EXPLOSION_4A");
        ids[1] = portGameSfxId("EXPLOSION_5B");
        ids[2] = portGameSfxId("EXPLOSION_9");
    }
    portGameSfxPlay(crack, 0.85f, 1.7f, 0.0f, 0.0f);
    portGameSfxBurst(ids, 3, 3, 4, 0.75f, 0.30f, 0.50f, 0.10f, 0.35f, 0.12f, 0.6f);
}

/* new landed strikes from the sim: a flash and a bolt each (the oldest bolt gives way) */
static int boltsPoll(void)
{
    s32 i, j, live = 0;

    for (i = 0; i < BOLTS; i++)
    {
        f32 x, y, z;
        const u32 serial = portArenaStrikeLanded(i, &x, &y, &z);
        s32 seen = 0, old = 0;

        if (serial == 0) continue;
        for (j = 0; j < BOLTS; j++)
        {
            if (sBolt[j].serial == serial) seen = 1;
            if (sBolt[j].serial == 0 || (sBolt[old].serial != 0 && sBolt[j].t0 < sBolt[old].t0)) old = j;
        }
        if (seen) continue;
        sBolt[old].x = x;
        sBolt[old].y = y;
        sBolt[old].z = z;
        sBolt[old].t0 = g_GlobalTimer;
        sBolt[old].serial = serial;
        thunder();
    }
    for (i = 0; i < BOLTS; i++)
    {
        if (sBolt[i].serial != 0 && (f32) (g_GlobalTimer - sBolt[i].t0) / 60.0f < FLASH_SECS) live = 1;
    }
    return live;
}

static int strikesPending(void)
{
    f32 x, y, z, g;
    s32 i;

    for (i = 0; i < STRIKES; i++)
    {
        if (portArenaStrikePending(i, &x, &y, &z, &g)) return 1;
    }
    return 0;
}

static void lightningDraw(HudCanvas *c)
{
    f32 flash = 0.0f;
    s32 i, k;

    /* on the fuse: the point crackles, faster and brighter as it comes */
    for (i = 0; i < STRIKES; i++)
    {
        f32 x, y, z, gone;
        u32 seed;

        if (!portArenaStrikePending(i, &x, &y, &z, &gone)) continue;
        seed = (u32) (g_GlobalTimer / 2) * 2654435761u + (u32) i * 40503u;
        if (hrnd(&seed) < 0.25f + 0.6f * gone)
        {
            ring(c, x, y, z, 55.0f + 40.0f * hrnd(&seed), 18, 0.5f, &seed, 2, hudAlpha(0xCFE2FFFFu, 0.35f + 0.55f * gone));
            ring(c, x, y, z, 14.0f, 6, 0.8f, &seed, 3, hudAlpha(0xFFFFFFFFu, 0.4f + 0.6f * gone));
        }
    }

    /* the flash: everything goes white, then fades fast (one re-strobe at ~0.12 s); under the bolts, so they show through it */
    for (i = 0; i < BOLTS; i++)
    {
        const f32 t = (f32) (g_GlobalTimer - sBolt[i].t0) / 60.0f;
        f32 a;

        if (sBolt[i].serial == 0 || t < 0.0f || t >= FLASH_SECS) continue;
        a = 1.0f - t / FLASH_SECS;
        a = 0.90f * a * a * a + ((t > 0.10f && t < 0.16f) ? 0.30f : 0.0f);
        if (a > flash) flash = a;
    }
    if (flash > 0.003f)
    {
        hudFill(c, 0, 0, c->w, c->h, hudAlpha(0xF4F8FFFFu, flash > 1.0f ? 1.0f : flash));
    }

    for (i = 0; i < BOLTS; i++)
    {
        Bolt *b = &sBolt[i];
        const f32 t = (f32) (g_GlobalTimer - b->t0) / 60.0f;
        f32 px[BOLT_SEGS + 1], py[BOLT_SEGS + 1], lean[2], a;
        s32 ok[BOLT_SEGS + 1];
        u32 seed;

        if (b->serial == 0 || t < 0.0f || t >= BOLT_SECS) continue;
        /* the bolt: re-jagged every 3 frames, flickering out after the first 0.08 s */
        if (t > 0.08f && ((g_GlobalTimer / 2) & 1)) continue;
        a = t < 0.08f ? 1.0f : 1.0f - (t - 0.08f) / (BOLT_SECS - 0.08f);
        seed = b->serial * 2246822519u;
        lean[0] = (hrnd(&seed) - 0.5f) * 900.0f;   /* (the bolt's top: the same for the whole strike) */
        lean[1] = (hrnd(&seed) - 0.5f) * 900.0f;
        seed ^= (u32) (g_GlobalTimer / 3) * 3266489917u;
        for (k = 0; k <= BOLT_SEGS; k++)
        {
            const f32 u = (f32) k / (f32) BOLT_SEGS, amp = (k == 0 || k == BOLT_SEGS) ? 0.0f : 260.0f * sinf(u * 3.14159265f);
            const f32 wx = b->x + lean[0] * (1.0f - u) + (hrnd(&seed) - 0.5f) * 2.0f * amp;
            const f32 wz = b->z + lean[1] * (1.0f - u) + (hrnd(&seed) - 0.5f) * 2.0f * amp;

            ok[k] = project(wx, b->y + BOLT_TOP * (1.0f - u), wz, &px[k], &py[k]);
        }
        for (k = 0; k < BOLT_SEGS; k++)
        {
            if (ok[k] && ok[k + 1]) bar(c, px[k], py[k], px[k + 1], py[k + 1], 11, hudAlpha(0x7A9CFFFFu, 0.5f * a));
        }
        for (k = 0; k < BOLT_SEGS; k++)
        {
            if (ok[k] && ok[k + 1]) bar(c, px[k], py[k], px[k + 1], py[k + 1], 4, hudAlpha(0xFFFFFFFFu, a));
        }
        {   /* a fork off the upper third */
            const s32 k0 = BOLT_SEGS / 3;
            f32 fx = px[k0], fy = py[k0];

            if (ok[k0])
            {
                for (k = 1; k <= 4; k++)
                {
                    f32 nx, ny;
                    const f32 u = ((f32) k0 + (f32) k * 0.8f) / (f32) BOLT_SEGS;

                    if (!project(b->x + lean[0] * (1.0f - u) + (f32) k * 120.0f * (hrnd(&seed) + 0.3f),
                                 b->y + BOLT_TOP * (1.0f - u), b->z + lean[1] * (1.0f - u) + (hrnd(&seed) - 0.5f) * 160.0f, &nx, &ny)) break;
                    bar(c, fx, fy, nx, ny, 2, hudAlpha(0xE8F0FFFFu, 0.8f * a));
                    fx = nx;
                    fy = ny;
                }
            }
        }
        /* where it hit: a ring of light on the floor, spreading */
        ring(c, b->x, b->y, b->z, 30.0f + 420.0f * (t / BOLT_SECS), 28, 0.15f, &seed, 3, hudAlpha(0xE0ECFFFFu, 0.8f * a));
    }
}

/* port_console.c, over the 3D picture and before the HUD */
Gfx *portArenaRainRender(Gfx *gdl)
{
    const f32 amt = portArenaRainAmount();
    HudCanvas c;
    const coord3d *cam;
    f32 dt, floor;
    s32 i, k, lit;

    if (!portModeActive() || g_CurrentPlayer == NULL || (cam = bondviewGetCurrentPlayersPosition()) == NULL)
    {
        sLastTimer = -1;
        return gdl;
    }
    lit = boltsPoll() || strikesPending();
    if (amt <= 0.002f && !lit)
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
    if (amt <= 0.002f)
    {   /* (dry, but a strike is still on its fuse or flashing) */
        lightningDraw(&c);
        return hudEnd(gdl, &c);
    }

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
    lightningDraw(&c);   /* (over the rain) */
    return hudEnd(gdl, &c);
}

#endif /* !TARGET_N64 */
