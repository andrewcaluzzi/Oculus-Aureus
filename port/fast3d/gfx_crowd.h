/* gfx_crowd.h - the arena's impostor crowd, shared by port/src/port_crowd.c (sim thread, emits the commands) and
 * gfx_pc.cpp / gfx_opengl.cpp (render thread). Presentation only: nothing here is sim state. */
#ifndef GFX_CROWD_H
#define GFX_CROWD_H

#define G_CROWD_EXT 0x4d          /* w0 low byte = the op below */

enum { CROWD_CAP_BEGIN = 1, CROWD_CAP_CELL = 2, CROWD_CAP_END = 3, CROWD_DRAW = 4 };

#define CROWD_ATLAS 1024          /* the capture atlas: 8 x 4 cells of 128 x 256, skin + 16 * pose */
#define CROWD_COLS 8
#define CROWD_ROWS 4
#define CROWD_CELL_W (CROWD_ATLAS / CROWD_COLS)
#define CROWD_CELL_H (CROWD_ATLAS / CROWD_ROWS)

typedef struct { float x, y, z, skin, t0, t1; } CrowdImpostor;   /* cheer pose while t0 <= now < t1 */

/* The draw carries its own world-to-clip matrix (the chrs' projection times the camera, row vectors: MP[i][j] at
 * mvp[i * 4 + j], laid out like Fast3D's MP_matrix) and loads nothing into the RSP. The gun draws after the crowd
 * with whatever projection the world left loaded (gunfire.c gunWeaponFovBegin), so a matrix the crowd loaded and
 * "restored" to the wrong one put the view-space gun into world space, on the floor in front of Bond. */
typedef struct {
    float mvp[16];
    const CrowdImpostor *imp;
    int n, version;
    float right[2];               /* the camera's horizontal right axis (x, z): cylindrical billboards */
    float now;
    float box[3];                 /* quad half width, bottom (from the seat), height */
    float tint[4];                /* rgb blended in by a, like a chr's shade */
} CrowdFrame;

#endif
