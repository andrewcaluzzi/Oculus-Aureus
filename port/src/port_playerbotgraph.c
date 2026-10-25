/* port_playerbotgraph.c - the player bot's navigation graph and A* (pure; see port_playerbotlogic.h). */
#include "port_playerbotlogic.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

int pblGraphAlloc(PblGraph *g, int maxNodes, int maxEdges)
{
    memset(g, 0, sizeof(*g));
    g->cap = maxNodes;
    g->ecap = maxEdges;
    g->x = (float *) calloc((size_t) maxNodes, sizeof(float));
    g->y = (float *) calloc((size_t) maxNodes, sizeof(float));
    g->z = (float *) calloc((size_t) maxNodes, sizeof(float));
    g->npen = (float *) calloc((size_t) maxNodes, sizeof(float));
    g->nbad = (unsigned char *) calloc((size_t) maxNodes, 1);
    g->start = (int *) calloc((size_t) maxNodes + 1, sizeof(int));
    g->to = (int *) calloc((size_t) maxEdges, sizeof(int));
    g->pen = (float *) calloc((size_t) maxEdges, sizeof(float));
    g->gs = (float *) calloc((size_t) maxNodes, sizeof(float));
    g->prev = (int *) calloc((size_t) maxNodes, sizeof(int));
    g->heap = (int *) calloc((size_t) maxNodes, sizeof(int));
    g->hpos = (int *) calloc((size_t) maxNodes, sizeof(int));
    g->state = (unsigned char *) calloc((size_t) maxNodes, 1);
    if (!g->x || !g->y || !g->z || !g->npen || !g->nbad || !g->start || !g->to || !g->pen || !g->gs || !g->prev ||
        !g->heap || !g->hpos || !g->state)
    {
        pblGraphFree(g);
        return 0;
    }
    return 1;
}

void pblGraphFree(PblGraph *g)
{
    free(g->x); free(g->y); free(g->z); free(g->npen); free(g->nbad); free(g->start); free(g->to); free(g->pen);
    free(g->gs); free(g->prev); free(g->heap); free(g->hpos); free(g->state);
    memset(g, 0, sizeof(*g));
}

int pblGraphNode(PblGraph *g, float x, float y, float z)
{
    if (g->n >= g->cap)
    {
        return -1;
    }
    g->x[g->n] = x;
    g->y[g->n] = y;
    g->z[g->n] = z;
    g->npen[g->n] = 0.0f;
    g->nbad[g->n] = 0;
    g->start[g->n] = g->ne;
    g->start[g->n + 1] = g->ne;
    return g->n++;
}

int pblGraphEdges(PblGraph *g, int node, const int *nbrs, int count)
{
    int i;

    if (node < 0 || node >= g->n)
    {
        return 0;
    }
    g->start[node] = g->ne;
    for (i = 0; i < count && g->ne < g->ecap; i++)
    {
        if (nbrs[i] < 0 || nbrs[i] >= g->n || nbrs[i] == node)
        {
            continue;
        }
        g->to[g->ne] = nbrs[i];
        g->pen[g->ne] = 0.0f;
        g->ne++;
    }
    g->start[node + 1] = g->ne;
    return 1;
}

static int edgeIndex(const PblGraph *g, int a, int b)
{
    int e;

    if (a < 0 || a >= g->n)
    {
        return -1;
    }
    for (e = g->start[a]; e < g->start[a + 1]; e++)
    {
        if (g->to[e] == b)
        {
            return e;
        }
    }
    return -1;
}

void pblGraphPenalize(PblGraph *g, int a, int b, float add)
{
    int e = edgeIndex(g, a, b);

    if (e >= 0)
    {
        g->pen[e] += add;
    }
    e = edgeIndex(g, b, a);
    if (e >= 0)
    {
        g->pen[e] += add;
    }
}

void pblGraphClearMarks(PblGraph *g)
{
    int i;

    for (i = 0; i < g->n; i++)
    {
        g->npen[i] = 0.0f;
        g->nbad[i] = 0;
    }
}

float pblEdgeCost(const PblGraph *g, int a, int e)
{
    int b = g->to[e];
    float dx = g->x[b] - g->x[a], dy = g->y[b] - g->y[a], dz = g->z[b] - g->z[a];

    return sqrtf(dx * dx + dy * dy + dz * dz) + (dy > 0.0f ? dy * 0.5f : 0.0f) + g->pen[e] + g->npen[b];
}

static float heur(const PblGraph *g, int a, int b)
{
    float dx = g->x[b] - g->x[a], dy = g->y[b] - g->y[a], dz = g->z[b] - g->z[a];

    return sqrtf(dx * dx + dy * dy + dz * dz);
}

/* binary min-heap of node ids keyed by sFs = gs + heur */
static void heapSwap(PblGraph *g, int i, int j)
{
    int t = g->heap[i];

    g->heap[i] = g->heap[j];
    g->heap[j] = t;
    g->hpos[g->heap[i]] = i;
    g->hpos[g->heap[j]] = j;
}

/* the A* f-scores: scratch, every entry read is written in the same search (a savestate leaves it alone: the bot registers
 * both as live state, modding/snapshot-fidelity.md) */
float *g_PblAstarFs;
int    g_PblAstarFsCap;
#define sFs g_PblAstarFs
#define sFsCap g_PblAstarFsCap

static void heapUp(PblGraph *g, int i)
{
    while (i > 0)
    {
        int p = (i - 1) / 2;

        if (sFs[g->heap[p]] <= sFs[g->heap[i]])
        {
            break;
        }
        heapSwap(g, i, p);
        i = p;
    }
}

static void heapDown(PblGraph *g, int i, int n)
{
    for (;;)
    {
        int l = i * 2 + 1, r = l + 1, m = i;

        if (l < n && sFs[g->heap[l]] < sFs[g->heap[m]])
        {
            m = l;
        }
        if (r < n && sFs[g->heap[r]] < sFs[g->heap[m]])
        {
            m = r;
        }
        if (m == i)
        {
            break;
        }
        heapSwap(g, i, m);
        i = m;
    }
}

int pblAstar(PblGraph *g, int from, int to, int *out, int max)
{
    int i, n = 0, hn = 0, found = 0;

    if (from < 0 || to < 0 || from >= g->n || to >= g->n || max <= 0)
    {
        return 0;
    }
    if (sFsCap < g->cap)
    {
        free(sFs);
        sFs = (float *) calloc((size_t) g->cap, sizeof(float));
        sFsCap = sFs ? g->cap : 0;
        if (!sFs)
        {
            return 0;
        }
    }
    memset(g->state, 0, (size_t) g->n);
    g->gs[from] = 0.0f;
    sFs[from] = heur(g, from, to);
    g->prev[from] = -1;
    g->state[from] = 1;
    g->heap[hn] = from;
    g->hpos[from] = hn++;
    while (hn > 0)
    {
        int cur = g->heap[0], e;

        heapSwap(g, 0, hn - 1);
        hn--;
        heapDown(g, 0, hn);
        if (cur == to)
        {
            found = 1;
            break;
        }
        g->state[cur] = 2;
        for (e = g->start[cur]; e < g->start[cur + 1]; e++)
        {
            int nb = g->to[e];
            float c;

            if (g->state[nb] == 2 || (g->nbad[nb] && nb != to))
            {
                continue;
            }
            c = g->gs[cur] + pblEdgeCost(g, cur, e);
            if (g->state[nb] == 0)
            {
                g->gs[nb] = c;
                sFs[nb] = c + heur(g, nb, to);
                g->prev[nb] = cur;
                g->state[nb] = 1;
                g->heap[hn] = nb;
                g->hpos[nb] = hn++;
                heapUp(g, hn - 1);
            }
            else if (c < g->gs[nb])
            {
                g->gs[nb] = c;
                sFs[nb] = c + heur(g, nb, to);
                g->prev[nb] = cur;
                heapUp(g, g->hpos[nb]);
            }
        }
    }
    if (!found)
    {
        return 0;
    }
    /* count the route, then write it front to back (cut to max) */
    for (i = to; i >= 0; i = g->prev[i])
    {
        n++;
        if (i == from)
        {
            break;
        }
    }
    {
        int k = n - 1, keep = n < max ? n : max;

        for (i = to; i >= 0 && k >= 0; i = g->prev[i], k--)
        {
            if (k < keep)
            {
                out[k] = i;
            }
            if (i == from)
            {
                break;
            }
        }
        return keep;
    }
}

/* ---- funnel ------------------------------------------------------------------------------------------------------ */

#define FUNNEL_MAX 128

static float triarea2(float ax, float az, float bx, float bz, float cx, float cz)
{
    return (cx - ax) * (bz - az) - (bx - ax) * (cz - az);
}

int pblFunnel(const float *ax, const float *az, const float *bx, const float *bz, const float *cx, const float *cz, int n,
              float sx, float sz, float ex, float ez, float shrink, float *outx, float *outz, int maxc)
{
    float lx[FUNNEL_MAX + 2], lz[FUNNEL_MAX + 2], rx[FUNNEL_MAX + 2], rz[FUNNEL_MAX + 2];
    float apx, apz, plx, plz, prx, prz;
    int np, i, apexi = 0, lefti = 0, righti = 0, nc = 0;

    if (n > FUNNEL_MAX)
    {
        n = FUNNEL_MAX;
    }
    /* portal 0 = the start, then the real ones (oriented: "left" seen walking from tile i to tile i+1), then the end */
    lx[0] = rx[0] = sx;
    lz[0] = rz[0] = sz;
    for (i = 0; i < n; i++)
    {
        float dx = cx[i + 1] - cx[i], dz = cz[i + 1] - cz[i];
        float side = dx * (az[i] - cz[i]) - dz * (ax[i] - cx[i]);
        float pax = ax[i], paz = az[i], pbx = bx[i], pbz = bz[i];
        float wx = pbx - pax, wz = pbz - paz;
        float w = sqrtf(wx * wx + wz * wz);

        if (w > 2.0f * shrink + 10.0f)
        {   /* keep the walker's radius off both ends */
            pax += wx / w * shrink;
            paz += wz / w * shrink;
            pbx -= wx / w * shrink;
            pbz -= wz / w * shrink;
        }
        else
        {   /* narrow: the middle */
            pax = pbx = (pax + pbx) * 0.5f;
            paz = pbz = (paz + pbz) * 0.5f;
        }
        if (side > 0.0f)
        {
            lx[i + 1] = pax; lz[i + 1] = paz; rx[i + 1] = pbx; rz[i + 1] = pbz;
        }
        else
        {
            lx[i + 1] = pbx; lz[i + 1] = pbz; rx[i + 1] = pax; rz[i + 1] = paz;
        }
    }
    lx[n + 1] = rx[n + 1] = ex;
    lz[n + 1] = rz[n + 1] = ez;
    np = n + 2;

    apx = sx; apz = sz;
    plx = lx[0]; plz = lz[0];
    prx = rx[0]; prz = rz[0];
    for (i = 1; i < np && nc < maxc; i++)
    {
        float nlx = lx[i], nlz = lz[i], nrx = rx[i], nrz = rz[i];

        /* tighten the right side */
        if (triarea2(apx, apz, prx, prz, nrx, nrz) <= 0.0f)
        {
            if ((apx == prx && apz == prz) || triarea2(apx, apz, plx, plz, nrx, nrz) > 0.0f)
            {
                prx = nrx; prz = nrz;
                righti = i;
            }
            else
            {   /* right crossed left: the left point is a corner */
                outx[nc] = plx; outz[nc] = plz; nc++;
                apx = plx; apz = plz;
                apexi = lefti;
                prx = plx = apx; prz = plz = apz;
                lefti = righti = apexi;
                i = apexi;
                continue;
            }
        }
        /* tighten the left side */
        if (triarea2(apx, apz, plx, plz, nlx, nlz) >= 0.0f)
        {
            if ((apx == plx && apz == plz) || triarea2(apx, apz, prx, prz, nlx, nlz) < 0.0f)
            {
                plx = nlx; plz = nlz;
                lefti = i;
            }
            else
            {   /* left crossed right: the right point is a corner */
                outx[nc] = prx; outz[nc] = prz; nc++;
                apx = prx; apz = prz;
                apexi = righti;
                prx = plx = apx; prz = plz = apz;
                lefti = righti = apexi;
                i = apexi;
                continue;
            }
        }
    }
    if (nc < maxc && !(nc > 0 && outx[nc - 1] == ex && outz[nc - 1] == ez))
    {
        outx[nc] = ex;
        outz[nc] = ez;
        nc++;
    }
    return nc;
}

int pblNearest(const PblGraph *g, float x, float y, float z)
{
    int i, best = -1;
    float bd = 1e30f;

    for (i = 0; i < g->n; i++)
    {
        float dx = g->x[i] - x, dy = (g->y[i] - y) * 3.0f, dz = g->z[i] - z;
        float d = dx * dx + dy * dy + dz * dz;

        if (d < bd)
        {
            bd = d;
            best = i;
        }
    }
    return best;
}

int pblComponents(const PblGraph *g, int *largest)
{
    int *comp = (int *) malloc((size_t) (g->n > 0 ? g->n : 1) * sizeof(int));
    int *q = (int *) malloc((size_t) (g->n > 0 ? g->n : 1) * sizeof(int));
    int i, nc = 0, big = 0;

    if (!comp || !q)
    {
        free(comp);
        free(q);
        *largest = 0;
        return 0;
    }
    for (i = 0; i < g->n; i++)
    {
        comp[i] = -1;
    }
    for (i = 0; i < g->n; i++)
    {
        int h = 0, t = 0;

        if (comp[i] >= 0)
        {
            continue;
        }
        comp[i] = nc;
        q[t++] = i;
        while (h < t)
        {
            int u = q[h++], e;

            for (e = g->start[u]; e < g->start[u + 1]; e++)
            {
                if (comp[g->to[e]] < 0)
                {
                    comp[g->to[e]] = nc;
                    q[t++] = g->to[e];
                }
            }
        }
        big = t > big ? t : big;
        nc++;
    }
    free(comp);
    free(q);
    *largest = big;
    return nc;
}
