/* port_cooplogic.c - pure co-op rules, unit tested by tests/cooplogic_test.c. */
#include <math.h>
#include "port_cooplogic.h"

int portCoopPickNearest(int n, const float *x, const float *y, const float *z, const int *dead, const int *has,
                        float tx, float ty, float tz)
{
    int best = -1;
    float bestd = 0.0f;
    int firstPresent = -1;
    int i;

    for (i = 0; i < n; i++)
    {
        float dx, dy, dz, d;

        if (!has[i])
        {
            continue;
        }
        if (firstPresent < 0)
        {
            firstPresent = i;
        }
        if (dead[i])
        {
            continue;
        }
        dx = x[i] - tx;
        dy = y[i] - ty;
        dz = z[i] - tz;
        d = dx * dx + dy * dy + dz * dz;
        if (best < 0 || d < bestd)   /* strict: an equal distance keeps the lower index */
        {
            best = i;
            bestd = d;
        }
    }
    if (best >= 0)
    {
        return best;
    }
    return firstPresent >= 0 ? firstPresent : 0;
}

int portCoopEscortPick(int n, const float *dist, const int *dead, const int *has, int cur, int sinceFields, float ratio, int minFields)
{
    int best = -1;
    int firstPresent = -1;
    int i;

    for (i = 0; i < n; i++)
    {
        if (!has[i])
        {
            continue;
        }
        if (firstPresent < 0)
        {
            firstPresent = i;
        }
        if (dead[i])
        {
            continue;
        }
        if (best < 0 || dist[i] < dist[best])   /* strict: an equal distance keeps the lower index */
        {
            best = i;
        }
    }
    if (best < 0)
    {
        return firstPresent >= 0 ? firstPresent : 0;   /* nobody lives: the old choice stays meaningful, else the first present */
    }
    if (cur < 0 || cur >= n || !has[cur] || dead[cur])
    {
        return best;   /* the followed Bond is gone (dead, or none yet): the nearest living one at once */
    }
    if (best != cur && dist[best] < dist[cur] * ratio && sinceFields >= minFields)
    {
        return best;
    }
    return cur;
}

int portCoopAllDeadOf(int n, const int *dead, const int *has)
{
    int any = 0;
    int i;

    for (i = 0; i < n; i++)
    {
        if (has[i])
        {
            any = 1;
            if (!dead[i])
            {
                return 0;
            }
        }
    }
    return any;
}

/* ---- Job 4: respawn timing and placement ---- */

int portCoopRespawnPartner(int n, const int *dead, const int *has, int self)
{
    int i;

    for (i = 0; i < n; i++)
    {
        if (i != self && has[i] && !dead[i])
        {
            return i;
        }
    }
    return -1;
}

int portCoopRespawnFieldsLeft(int deadSince, int now, int total)
{
    int left = total - (now - deadSince);

    return left > 0 ? left : 0;
}

int portCoopRespawnSecs(int fieldsLeft)
{
    return fieldsLeft <= 0 ? 0 : (fieldsLeft + PORT_COOP_FIELDS_PER_SEC - 1) / PORT_COOP_FIELDS_PER_SEC;
}

int portCoopRespawnDue(int fieldsLeft, int allDead)
{
    return fieldsLeft <= 0 && !allDead;
}

int portCoopRespawnSpot(int k, float px, float pz, float fx, float fz, float *ox, float *oz)
{
    /* (right, forward) offsets from the partner, tried in this order: beside, then behind or ahead */
    static const float cand[PORT_COOP_RESPAWN_SPOTS][2] = {
        {90.0f, 0.0f}, {-90.0f, 0.0f}, {100.0f, -40.0f}, {-100.0f, -40.0f}, {0.0f, -80.0f}, {0.0f, 80.0f}
    };

    if (k < 0 || k >= PORT_COOP_RESPAWN_SPOTS)
    {
        return 0;
    }
    *ox = px + fz * cand[k][0] + fx * cand[k][1];
    *oz = pz - fx * cand[k][0] + fz * cand[k][1];
    return 1;
}

/* ---- Netplay job 3d: death drops ---- */

int portCoopDropOffset(int k, float *ox, float *oz)
{
    /* sin / cos of 0, 60, 120, 180, 240, 300 degrees (fixed constants, no libm) */
    static const float c6[6] = {1.0f, 0.5f, -0.5f, -1.0f, -0.5f, 0.5f};
    static const float s6[6] = {0.0f, 0.8660254f, 0.8660254f, 0.0f, -0.8660254f, -0.8660254f};

    if (k < 0 || k > 12)
    {
        return 0;
    }
    if (k == 0)
    {
        *ox = 0.0f;
        *oz = 0.0f;
        return 1;
    }
    if (k <= 6)
    {
        *ox = 45.0f * c6[k - 1];
        *oz = 45.0f * s6[k - 1];
        return 1;
    }
    *ox = 90.0f * c6[k - 7];
    *oz = 90.0f * s6[k - 7];
    return 1;
}

int portCoopDropSpotSafe(int hasTile, int inTile, float footY, float floorY, int worldFloorValid, float worldFloor, int forceUnsafe)
{
    float d = footY - floorY;

    if (forceUnsafe || !hasTile || !inTile)
    {
        return 0;
    }
    if (d > 150.0f || d < -150.0f)
    {
        return 0;
    }
    if (worldFloorValid && floorY < worldFloor)
    {
        return 0;
    }
    return 1;
}

int portCoopDropMagazine(int inHand, int handMag, int magSize, int pool)
{
    int q = inHand ? handMag : magSize;

    if (q < 0)
    {
        q = 0;
    }
    if (pool < 0)
    {
        pool = 0;
    }
    return q < pool ? q : pool;
}

int portCoopShotBlocked(float sx, float sy, float sz, float tx, float ty, float tz, float px, float py, float pz, float radius, float spread,
                        float band)
{
    float dx = tx - sx, dz = tz - sz, qx = px - sx, qz = pz - sz;
    float len2 = dx * dx + dz * dz, along = qx * dx + qz * dz, t, perp2, m, lineY;

    if (len2 < 1.0f || along <= 0.0f || along >= len2)
    {
        return 0;   /* no line, or the body is behind the gun or beyond the target */
    }
    t = along / len2;
    perp2 = qx * qx + qz * qz - along * t;   /* squared distance from the line */
    m = radius + spread * t * sqrtf(len2);   /* the margin widens with the distance from the gun */
    if (perp2 > m * m)
    {
        return 0;
    }
    lineY = sy + t * (ty - sy);
    return (py - lineY <= band && lineY - py <= band) ? 1 : 0;
}

int portCoopBotTrigger(int want, int usesAmmo, int mag, int blocked)
{
    return (want && !blocked && (!usesAmmo || mag > 0)) ? 1 : 0;
}

void portCoopBotSkillPreset(int step, PortCoopBotSkill *out)
{
    static const PortCoopBotSkill k[PORT_COOP_BOT_SKILL_MAX] = {
        { 0.80f, 9.0f, 120.0f, 3000.0f },   /* 1 ROOKIE */
        { 0.55f, 6.0f, 180.0f, 3600.0f },   /* 2 AGENT */
        { 0.35f, 4.0f, 240.0f, 4200.0f },   /* 3 SECRET AGENT */
        { 0.18f, 2.0f, 300.0f, 4800.0f },   /* 4 00 AGENT */
        { 0.0f, 0.0f, 360.0f, 5500.0f },    /* 5 007: the bot as built */
    };

    if (step < PORT_COOP_BOT_SKILL_MIN) step = PORT_COOP_BOT_SKILL_MIN;
    if (step > PORT_COOP_BOT_SKILL_MAX) step = PORT_COOP_BOT_SKILL_MAX;
    *out = k[step - 1];
}

static float clampf(float v, float lo, float hi, float nanv)
{
    if (!(v == v)) return nanv;   /* NaN */
    return v < lo ? lo : (v > hi ? hi : v);
}

void portCoopBotSkillClamp(PortCoopBotSkill *k)
{
    k->reaction = clampf(k->reaction, 0.0f, 3.0f, 0.0f);
    k->aimError = clampf(k->aimError, 0.0f, 30.0f, 0.0f);
    k->turn = clampf(k->turn, 30.0f, 1440.0f, 360.0f);
    k->range = clampf(k->range, 300.0f, 20000.0f, 5500.0f);
}

int portCoopBotReact(int acquired, int visible, int reactFields, int *left)
{
    if (acquired) {
        *left = reactFields > 0 ? reactFields : 0;
    }
    if (visible && *left > 0) {
        (*left)--;
        return 0;   /* (the tick it reaches 0 still holds: it fires from the next one, reactFields ticks after it acquired) */
    }
    return *left <= 0 ? 1 : 0;
}

float portCoopBotAimRoll(unsigned int bits, float amp)
{
    if (!(amp > 0.0f)) return 0.0f;
    return amp * ((float) (bits >> 8) * (2.0f / 16777215.0f) - 1.0f);   /* the top 24 bits -> -1..1 */
}
