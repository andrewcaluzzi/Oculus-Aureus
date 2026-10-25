/* port_playerbotlogic.c - pure logic for the player bot: angles, the plan parser, the stuck detector.
 * The graph and A* are in port_playerbotgraph.c. See port_playerbotlogic.h. */
#include "port_playerbotlogic.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- angles ------------------------------------------------------------------------------------------------------- */

float pblWrap180(float a)
{
    if (!(a > -1e6f && a < 1e6f))
    {
        return 0.0f;   /* garbage or NaN: no turn */
    }
    if (a > 540.0f || a < -540.0f)
    {
        a = fmodf(a, 360.0f);
    }
    while (a > 180.0f)
    {
        a -= 360.0f;
    }
    while (a <= -180.0f)
    {
        a += 360.0f;
    }
    return a;
}

/* atan2 in (-pi, pi]. In the game build `atan2f` is the decomp's own (src/game/math_atan2f), which returns [0, 2pi). */
static float pblAtan2(float y, float x)
{
    float a = atan2f(y, x);

    if (a > 3.14159265f)
    {
        a -= 2.0f * 3.14159265f;
    }
    return a;
}

float pblYawTo(float dx, float dz)
{
    float t = pblAtan2(-dx, dz) * (180.0f / 3.14159265f);

    if (t < 0.0f)
    {
        t += 360.0f;
    }
    return t;
}

float pblPitchTo(float dy, float dh)
{
    return pblAtan2(dy, dh < 1.0f ? 1.0f : dh) * (180.0f / 3.14159265f);
}

float pblClamp(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* ---- combat geometry ------------------------------------------------------------------------------------------------ */

void pblRaySplit(float px, float pz, float yawDeg, float qx, float qz, float *fwd, float *lat)
{
    float th = yawDeg * 3.14159265f / 180.0f;
    float fx = -sinf(th), fz = cosf(th);
    float dx = qx - px, dz = qz - pz;

    *fwd = dx * fx + dz * fz;
    *lat = fabsf(dx * -fz + dz * fx);
}

int pblInLineOfFire(float px, float pz, float yawDeg, float range, float qx, float qz)
{
    float fwd, lat;

    pblRaySplit(px, pz, yawDeg, qx, qz, &fwd, &lat);
    if (fwd < -20.0f || fwd > range + 500.0f)
    {
        return 0;
    }
    return lat < 45.0f + 0.04f * (fwd > 0.0f ? fwd : 0.0f);
}

float pblOffFacing(float facingRad, float cx, float cz, float x, float z)
{
    float to = atan2f(x - cx, z - cz) * 180.0f / 3.14159265f;
    float f = facingRad * 180.0f / 3.14159265f;

    return fabsf(pblWrap180(to - f));
}

float pblThreatAimScore(float score, float aimSum)
{
    float a = aimSum < 0.0f ? 0.0f : (aimSum > 1.0f ? 1.0f : aimSum);

    return score > 0.0f ? score * (1.0f - 0.6f * a) : score;
}

float pblThreatScore(float dist, int attacking, int alarmRaiser, int hurtUsLately)
{
    float s = dist;

    if (attacking)
    {
        s *= 0.6f;
    }
    if (hurtUsLately)
    {
        s *= 0.7f;
    }
    if (alarmRaiser)
    {
        s = s * 0.3f - 400.0f;
    }
    return s;
}

float pblAngleEma(float oldDeg, float sampleDeg, float k)
{
    return pblWrap180(oldDeg + k * pblWrap180(sampleDeg - oldDeg));
}

int pblStrafeSide(int cur, float turnDeg, float offRight)
{
    if (fabsf(turnDeg) < 50.0f)
    {
        return cur;   /* (a flip swings the view 76 degrees: keep the side through ordinary bends) */
    }
    return (turnDeg > 0.0f) == (offRight > 0.0f) ? 1 : -1;
}

float pblExposure(const float *dist, const int *seen, int n, float noShot)
{
    float s = 0.0f;
    int k, any = 0;

    for (k = 0; k < n; k++)
    {
        if (seen[k])
        {
            any = 1;
            s += dist[k] > 300.0f ? 300.0f / dist[k] : 1.0f;
        }
    }
    return any ? s : noShot;
}

/* ---- plans --------------------------------------------------------------------------------------------------------- */

static const char *const sItemNames[] = {
    "unarmed", "fist", "knife", "throwknife", "wppk", "wppksil", "tt33", "skorpion", "ak47", "uzi", "mp5k", "mp5ksil",
    "spectre", "m16", "fnp90", "shotgun", "autoshot", "sniperrifle", "ruger", "goldengun", "silverwppk", "goldwppk",
    "laser", "watchlaser", "grenadelaunch", "rocketlaunch", "grenade", "timedmine", "proximitymine", "remotemine",
    "trigger", "taser", "tankshells", "bombcase", "plastique", "flarepistol", "pitongun", "bungee", "doordecoder",
    "bombdefuser", "camera", "lockexploder", "doorexploder", "briefcase", "weaponcase", "safecrackercase",
    "keyanalysercase", "bug", "microcamera", "bugdetector", "explosivefloppy", "polarizedglasses", "darkglasses",
    "creditcard", "gaskeyring", "datathief", "watchidentifier", "watchcommunicator", "watchgeigercounter",
    "watchmagnetrepel", "watchmagnetattract", "goldeneyekey", "blackbox", "circuitboard", "clipboard", "stafflist",
    "dossierred", "plans", "spyfile", "blueprints", "map", "audiotape", "videotape", "dattape", "spooltape",
    "microfilm", "microcode", "lectre", "money", "goldbar", "heroin", "keycard", "keyyale", "keybolt",
};

int pblItemByName(const char *name)
{
    int i;

    for (i = 0; i < (int) (sizeof(sItemNames) / sizeof(sItemNames[0])); i++)
    {
        if (strcmp(sItemNames[i], name) == 0)
        {
            return i;
        }
    }
    if (name[0] >= '0' && name[0] <= '9')
    {
        return (int) strtol(name, NULL, 0);
    }
    return -1;
}

static const struct { const char *name; int op; } sOps[] = {
    { "goto", PBL_GOTO_PAD },  /* goto pad N [r] | goto obj T [r] | goto x y z [r] */
    { "use", PBL_USE_OBJ },    /* use obj T [r] | use pad N */
    { "destroy", PBL_DESTROY_OBJ },
    { "collect", PBL_COLLECT_OBJ },
    { "gadget", PBL_GADGET_OBJ },
    { "throw", PBL_THROW_OBJ },
    { "photo", PBL_PHOTO_OBJ },
    { "wait", PBL_WAIT },
    { "equip", PBL_EQUIP },
    { "flag", PBL_FLAG },
    { "press", PBL_PRESS },
    { "exit", PBL_EXIT },
    { "auto", PBL_AUTO },
    { "kill", PBL_KILL },
    { "face", PBL_FACE },
    { "look", PBL_LOOK },
    { "walk", PBL_WALK },
    { "peace", PBL_PEACE },
    { "meet", PBL_MEET },      /* meet chr N [r] [ticks] | meet ai ID [r] [ticks] */
    { "hunt", PBL_HUNT },
    { "quiet", PBL_QUIET },
    { "key", PBL_KEY },        /* key FLAGS (0x2, 4, ...) */
    { "rush", PBL_RUSH },
    { "avoid", PBL_AVOID },    /* avoid pad N */
    { "spare", PBL_SPARE },    /* spare ai ID */
};

const char *pblOpName(int op)
{
    int i;

    if (op == PBL_GOTO_XYZ)
    {
        return "goto";
    }
    if (op == PBL_USE_PAD)
    {
        return "use";
    }
    for (i = 0; i < (int) (sizeof(sOps) / sizeof(sOps[0])); i++)
    {
        if (sOps[i].op == op)
        {
            return sOps[i].name;
        }
    }
    return "?";
}

static int buttonByName(const char *s)
{
    static const struct { const char *n; int m; } b[] = {
        { "a", 0x8000 }, { "b", 0x4000 }, { "z", 0x2000 }, { "start", 0x1000 }, { "du", 0x0800 }, { "dd", 0x0400 },
        { "dl", 0x0200 }, { "dr", 0x0100 }, { "l", 0x0020 }, { "r", 0x0010 }, { "cu", 0x0008 }, { "cd", 0x0004 },
        { "cl", 0x0002 }, { "cr", 0x0001 },
    };
    int i;

    for (i = 0; i < (int) (sizeof(b) / sizeof(b[0])); i++)
    {
        if (strcmp(b[i].n, s) == 0)
        {
            return b[i].m;
        }
    }
    return (int) strtol(s, NULL, 0);
}

#define MAXTOK 12

static int tokenize(char *line, char **tok)
{
    int n = 0;
    char *p = line;

    while (*p != '\0' && n < MAXTOK)
    {
        while (*p == ' ' || *p == '\t')
        {
            p++;
        }
        if (*p == '\0' || *p == '#')
        {
            break;
        }
        tok[n++] = p;
        while (*p != '\0' && *p != ' ' && *p != '\t')
        {
            p++;
        }
        if (*p != '\0')
        {
            *p++ = '\0';
        }
    }
    return n;
}

static long num(const char *s)
{
    return strtol(s, NULL, 0);
}

int pblParsePlan(const char *text, PblPlan *plan, char *err, int errlen)
{
    char line[256];
    int lineno = 0;
    const char *p = text;

    plan->n = 0;
    plan->name[0] = '\0';
    err[0] = '\0';
    while (*p != '\0')
    {
        char *tok[MAXTOK];
        int nt, k, i, len = 0;
        PblStep st;

        lineno++;
        while (*p != '\0' && *p != '\n' && len < (int) sizeof(line) - 1)
        {
            line[len++] = (*p == '\r') ? ' ' : *p;
            p++;
        }
        while (*p != '\0' && *p != '\n')
        {
            p++;
        }
        if (*p == '\n')
        {
            p++;
        }
        line[len] = '\0';
        memset(&st, 0, sizeof(st));
        st.condObj = -1;
        st.diffMin = 0;
        st.diffMax = 3;
        st.r = 60.0f;
        st.line = lineno;
        {   /* keep the trimmed source for logs */
            const char *q = line;

            while (*q == ' ' || *q == '\t')
            {
                q++;
            }
            snprintf(st.text, sizeof(st.text), "%.*s", (int) sizeof(st.text) - 1, q);
            for (i = 0; st.text[i] != '\0'; i++)
            {
                if (st.text[i] == '#')
                {
                    st.text[i] = '\0';
                    break;
                }
            }
            for (i = (int) strlen(st.text) - 1; i >= 0 && (st.text[i] == ' ' || st.text[i] == '\t'); i--)
            {
                st.text[i] = '\0';
            }
        }
        nt = tokenize(line, tok);
        if (nt == 0)
        {
            continue;
        }
        k = 0;
        if (strcmp(tok[0], "name") == 0 && nt >= 2)
        {
            snprintf(plan->name, sizeof(plan->name), "%.*s", (int) sizeof(plan->name) - 1, tok[1]);
            continue;
        }
        /* prefixes: obj=N (only while objective N is open), diff=A-B */
        while (k < nt && (strncmp(tok[k], "obj=", 4) == 0 || strncmp(tok[k], "diff=", 5) == 0))
        {
            if (tok[k][0] == 'o')
            {
                st.condObj = (int) num(tok[k] + 4);
            }
            else
            {
                const char *d = tok[k] + 5;

                st.diffMin = d[0] - '0';
                st.diffMax = (d[1] == '-' && d[2] != '\0') ? d[2] - '0' : st.diffMin;
            }
            k++;
        }
        if (k >= nt)
        {
            snprintf(err, errlen, "line %d: no command", lineno);
            return -1;
        }
        st.op = 0;
        for (i = 0; i < (int) (sizeof(sOps) / sizeof(sOps[0])); i++)
        {
            if (strcmp(sOps[i].name, tok[k]) == 0)
            {
                st.op = sOps[i].op;
            }
        }
        if (st.op == 0)
        {
            snprintf(err, errlen, "line %d: unknown command '%s'", lineno, tok[k]);
            return -1;
        }
        k++;
#define NEED(c) do { if (nt - k < (c)) { snprintf(err, errlen, "line %d: '%s' needs %d argument(s)", lineno, pblOpName(st.op), (c)); return -1; } } while (0)
        switch (st.op)
        {
            case PBL_GOTO_PAD:
            case PBL_USE_OBJ:
                NEED(2);
                if (strcmp(tok[k], "pad") == 0)
                {
                    st.op = (st.op == PBL_GOTO_PAD) ? PBL_GOTO_PAD : PBL_USE_PAD;
                    st.a = (int) num(tok[k + 1]);
                    if (nt - k >= 3)
                    {
                        st.r = (float) atof(tok[k + 2]);
                    }
                }
                else if (strcmp(tok[k], "obj") == 0)
                {
                    st.op = (st.op == PBL_GOTO_PAD) ? PBL_COLLECT_OBJ : PBL_USE_OBJ;
                    st.a = (int) num(tok[k + 1]);
                    if (nt - k >= 3)
                    {
                        st.r = (float) atof(tok[k + 2]);
                    }
                    if (st.op == PBL_COLLECT_OBJ)
                    {
                        st.b = 1;   /* "goto obj": just reach it */
                    }
                }
                else if (st.op == PBL_GOTO_PAD)
                {
                    NEED(3);
                    st.op = PBL_GOTO_XYZ;
                    st.x = (float) atof(tok[k]);
                    st.y = (float) atof(tok[k + 1]);
                    st.z = (float) atof(tok[k + 2]);
                    if (nt - k >= 4)
                    {
                        st.r = (float) atof(tok[k + 3]);
                    }
                }
                else
                {
                    snprintf(err, errlen, "line %d: want 'pad N' or 'obj T'", lineno);
                    return -1;
                }
                break;
            case PBL_DESTROY_OBJ:
            case PBL_COLLECT_OBJ:
            case PBL_PHOTO_OBJ:
                NEED(2);
                if (strcmp(tok[k], "obj") != 0)
                {
                    snprintf(err, errlen, "line %d: want 'obj T'", lineno);
                    return -1;
                }
                st.a = (int) num(tok[k + 1]);
                break;
            case PBL_GADGET_OBJ:
            case PBL_THROW_OBJ:
                NEED(3);
                st.a = pblItemByName(tok[k]);
                if (st.a < 0 || strcmp(tok[k + 1], "obj") != 0)
                {
                    snprintf(err, errlen, "line %d: want '<item> obj T'", lineno);
                    return -1;
                }
                st.b = (int) num(tok[k + 2]);
                break;
            case PBL_WAIT:
            case PBL_LOOK:
            case PBL_PEACE:
            case PBL_QUIET:
            case PBL_RUSH:
                NEED(1);
                st.a = (int) num(tok[k]);
                break;
            case PBL_AVOID:
                NEED(2);
                if (strcmp(tok[k], "pad") != 0)
                {
                    snprintf(err, errlen, "line %d: want 'avoid pad N'", lineno);
                    return -1;
                }
                st.a = (int) num(tok[k + 1]);
                break;
            case PBL_SPARE:
                NEED(2);
                if (strcmp(tok[k], "ai") != 0)
                {
                    snprintf(err, errlen, "line %d: want 'spare ai ID'", lineno);
                    return -1;
                }
                st.a = (int) strtol(tok[k + 1], NULL, 0);
                break;
            case PBL_KEY:
                NEED(1);
                st.a = (int) strtoul(tok[k], NULL, 0);
                if (st.a == 0)
                {
                    snprintf(err, errlen, "line %d: 'key' needs non-zero key flags", lineno);
                    return -1;
                }
                break;
            case PBL_KILL:
                st.a = (nt - k >= 1) ? (int) num(tok[k]) : 1500;
                break;
            case PBL_HUNT:
                if (nt - k >= 1)
                {   /* hunt ai ID: only chrs running that AI list */
                    NEED(2);
                    if (strcmp(tok[k], "ai") != 0)
                    {
                        snprintf(err, errlen, "line %d: want 'hunt' or 'hunt ai ID'", lineno);
                        return -1;
                    }
                    st.a = 1;
                    st.b = (int) strtol(tok[k + 1], NULL, 0);
                }
                break;
            case PBL_EQUIP:
                NEED(1);
                st.a = pblItemByName(tok[k]);
                if (st.a < 0)
                {
                    snprintf(err, errlen, "line %d: unknown item '%s'", lineno, tok[k]);
                    return -1;
                }
                break;
            case PBL_FLAG:
                NEED(1);
                st.a = (int) strtoul(tok[k], NULL, 0);
                st.b = (nt - k >= 2) ? (int) num(tok[k + 1]) : 0;
                break;
            case PBL_PRESS:
                NEED(1);
                st.a = buttonByName(tok[k]);
                st.b = (nt - k >= 2) ? (int) num(tok[k + 1]) : 2;
                break;
            case PBL_FACE:
                NEED(3);
                st.x = (float) atof(tok[k]);
                st.y = (float) atof(tok[k + 1]);
                st.z = (float) atof(tok[k + 2]);
                break;
            case PBL_MEET:
                NEED(2);
                if (strcmp(tok[k], "chr") != 0 && strcmp(tok[k], "ai") != 0)
                {
                    snprintf(err, errlen, "line %d: want 'meet chr N' or 'meet ai ID'", lineno);
                    return -1;
                }
                st.a = tok[k][0] == 'a';
                st.b = (int) num(tok[k + 1]);
                st.r = (nt - k >= 3) ? (float) atof(tok[k + 2]) : 150.0f;
                st.c = (nt - k >= 4) ? (int) num(tok[k + 3]) : 1200;
                break;
            case PBL_WALK:
                NEED(2);
                st.a = (int) num(tok[k]);
                st.b = (int) num(tok[k + 1]);
                st.c = (nt - k >= 3) ? (int) num(tok[k + 2]) : 0;
                break;
            default:
                break;
        }
#undef NEED
        if (plan->n >= PBL_MAXSTEPS)
        {
            snprintf(err, errlen, "line %d: too many steps", lineno);
            return -1;
        }
        plan->s[plan->n++] = st;
    }
    return plan->n;
}

/* ---- stuck detection ---------------------------------------------------------------------------------------------- */

void pblStuckReset(PblStuck *s, int tick, float x, float z, float dist)
{
    s->bestDist = dist;
    s->lastGain = tick;
    s->lastX = x;
    s->lastZ = z;
    s->lastMoveTick = tick;
    s->level = 0;
    s->since = tick;
}

int pblStuckFeed(PblStuck *s, int tick, float x, float z, float dist)
{
    float mx = x - s->lastX, mz = z - s->lastZ;
    int want;

    if (dist < s->bestDist - 15.0f)
    {
        s->bestDist = dist;
        s->lastGain = tick;
    }
    if (mx * mx + mz * mz > 20.0f * 20.0f)
    {
        s->lastX = x;
        s->lastZ = z;
        s->lastMoveTick = tick;
    }
    /* no progress toward the goal for 1.5 s -> 1; 4 s -> 2; 12 s -> 3 */
    if (tick - s->lastGain > 720)
    {
        want = 3;
    }
    else if (tick - s->lastGain > 240)
    {
        want = 2;
    }
    else if (tick - s->lastGain > 90)
    {
        want = 1;
    }
    else
    {
        want = 0;
    }
    if (want != s->level)
    {
        s->level = want;
        s->since = tick;
    }
    return s->level;
}
