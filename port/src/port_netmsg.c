/* port_netmsg.c - pure netplay wire formats, unit tested by tests/netmsg_test.c. */
#include "port_netmsg.h"

typedef struct W { unsigned char *b; int cap, n, bad; } W;
typedef struct R { const unsigned char *b; int len, n, bad; } R;

static void w8(W *w, unsigned v) { if (w->n + 1 > w->cap) { w->bad = 1; return; } w->b[w->n++] = (unsigned char) v; }
static void w16(W *w, unsigned v) { w8(w, v & 0xFF); w8(w, (v >> 8) & 0xFF); }
static void w32(W *w, unsigned int v) { w16(w, v & 0xFFFF); w16(w, (v >> 16) & 0xFFFF); }

static int slen(const char *s, int cap)   /* length of s, capped at cap-1; cap-1 is also the limit on the wire */
{
    int n = 0;
    while (n < cap - 1 && s[n] != '\0') n++;
    return n;
}

static void wstr(W *w, const char *s, int cap)
{
    int n = slen(s, cap), i;
    if (s[n] != '\0') { w->bad = 1; return; }   /* too long: refuse to truncate silently */
    if (cap > 256) { w16(w, n); } else { w8(w, n); }
    for (i = 0; i < n; i++) w8(w, (unsigned char) s[i]);
}

static unsigned r8(R *r) { if (r->n + 1 > r->len) { r->bad = 1; return 0; } return r->b[r->n++]; }
static unsigned r16(R *r) { unsigned lo = r8(r); unsigned hi = r8(r); return lo | (hi << 8); }
static unsigned int r32(R *r) { unsigned int lo = r16(r); unsigned int hi = r16(r); return lo | (hi << 16); }

static void rstr(R *r, char *out, int cap)
{
    int n = (cap > 256) ? (int) r16(r) : (int) r8(r), i;
    if (r->bad || n >= cap || r->n + n > r->len) { r->bad = 1; out[0] = '\0'; return; }
    for (i = 0; i < n; i++) out[i] = (char) r->b[r->n++];
    out[n] = '\0';
}

void portNetRecPack(const PortNetInput *in, unsigned char *out)
{
    W w;
    w.b = out; w.cap = PORT_NET_REC_SIZE; w.n = 0; w.bad = 0;
    w16(&w, in->buttons);
    w8(&w, (unsigned char) in->stickX);
    w8(&w, (unsigned char) in->stickY);
    w32(&w, in->actions);
    w8(&w, (unsigned char) in->digX);
    w8(&w, (unsigned char) in->digY);
    w16(&w, in->flags);
    w32(&w, (unsigned int) in->yawMilli);
    w32(&w, (unsigned int) in->pitchMilli);
}

void portNetRecUnpack(const unsigned char *in, PortNetInput *out)
{
    R r;
    r.b = in; r.len = PORT_NET_REC_SIZE; r.n = 0; r.bad = 0;
    out->buttons = (unsigned short) r16(&r);
    out->stickX = (signed char) r8(&r);
    out->stickY = (signed char) r8(&r);
    out->actions = r32(&r);
    out->digX = (signed char) r8(&r);
    out->digY = (signed char) r8(&r);
    out->flags = (unsigned short) r16(&r);
    out->yawMilli = (int) r32(&r);
    out->pitchMilli = (int) r32(&r);
}

int portNetMsgType(const unsigned char *buf, int len)
{
    return (len >= 1) ? buf[0] : -1;
}

static void wopts(W *w, const PortNetPlayerOpts *o)
{
    w8(w, o->controlStyle); w8(w, o->padN64); w8(w, o->lookUpright); w8(w, o->autoAim);
    w8(w, o->lookAhead); w8(w, o->aimToggle); w8(w, o->aimLock); w8(w, o->mouseAimOff); w16(w, o->aimSens);
    w8(w, o->shootNextPrev); w8(w, o->detonateUseReload);
}

int portNetDiffValid(int d)
{
    return d >= 0 && d <= 3;
}

int portNetParseStageId(const char *s, int maxId, int *id)
{
    long v = 0;
    int n = 0;
    if (s == 0 || s[0] == '\0') return -1;
    for (; s[n] != '\0'; n++)
    {
        if (s[n] < '0' || s[n] > '9' || n >= 9) return -1;   /* the whole string must be digits (no sign, no junk, no overflow) */
        v = v * 10 + (s[n] - '0');
    }
    if (v > maxId) return -1;
    *id = (int) v;
    return 0;
}

static void ropts(R *r, PortNetPlayerOpts *o)
{
    o->controlStyle = (unsigned char) r8(r); o->padN64 = (unsigned char) r8(r); o->lookUpright = (unsigned char) r8(r);
    o->autoAim = (unsigned char) r8(r); o->lookAhead = (unsigned char) r8(r); o->aimToggle = (unsigned char) r8(r);
    o->aimLock = (unsigned char) r8(r); o->mouseAimOff = (unsigned char) r8(r); o->aimSens = (unsigned short) r16(r);
    o->shootNextPrev = (unsigned char) r8(r); o->detonateUseReload = (unsigned char) r8(r);
    if (o->controlStyle > 7) r->bad = 1;   /* the sim indexes control styles with it */
}

static void wsimcfg(W *w, const PortNetSimCfg *c)
{
    int i;
    if (c->n < 0 || c->n > PORT_NET_SIMCFG_MAX) { w->bad = 1; return; }
    w8(w, (unsigned) c->n);
    for (i = 0; i < c->n; i++) w32(w, (unsigned int) c->v[i]);
}

static void rsimcfg(R *r, PortNetSimCfg *c)
{
    int i;
    c->n = (int) r8(r);
    if (r->bad || c->n > PORT_NET_SIMCFG_MAX) { r->bad = 1; c->n = 0; return; }
    for (i = 0; i < c->n; i++) c->v[i] = (int) r32(r);
}

static int fin(const W *w);
static int finr(const R *r);

int portNetSimCfgPack(unsigned int run, const PortNetSimCfg *c, unsigned char *buf, int cap)
{
    W w;
    w.b = buf; w.cap = cap; w.n = 0; w.bad = 0;
    w8(&w, PORT_NETMSG_SIMCFG);
    w32(&w, run);
    wsimcfg(&w, c);
    return fin(&w);
}

int portNetSimCfgUnpack(const unsigned char *buf, int len, unsigned int *run, PortNetSimCfg *c)
{
    R r;
    r.b = buf; r.len = len; r.n = 0; r.bad = 0;
    if (r8(&r) != PORT_NETMSG_SIMCFG) return -1;
    *run = r32(&r);
    rsimcfg(&r, c);
    return finr(&r);
}

void portNetOptsPack(const PortNetPlayerOpts *o, unsigned char *out)
{
    W w;
    w.b = out; w.cap = PORT_NET_OPTS_SIZE; w.n = 0; w.bad = 0;
    wopts(&w, o);
}

void portNetOptsUnpack(const unsigned char *in, PortNetPlayerOpts *o)
{
    R r;
    r.b = in; r.len = PORT_NET_OPTS_SIZE; r.n = 0; r.bad = 0;
    ropts(&r, o);
}

int portNetOptsEqual(const PortNetPlayerOpts *a, const PortNetPlayerOpts *b)
{
    unsigned char pa[PORT_NET_OPTS_SIZE], pb[PORT_NET_OPTS_SIZE];
    int i;
    portNetOptsPack(a, pa);
    portNetOptsPack(b, pb);
    for (i = 0; i < PORT_NET_OPTS_SIZE; i++) if (pa[i] != pb[i]) return 0;
    return 1;
}

unsigned int portNetOptsHash(unsigned int h, const PortNetPlayerOpts *o)
{
    unsigned char p[PORT_NET_OPTS_SIZE];
    portNetOptsPack(o, p);
    return portNetFnv(h, p, PORT_NET_OPTS_SIZE);
}

static int fin(const W *w) { return w->bad ? -1 : w->n; }

static int finr(const R *r) { return (r->bad || r->n != r->len) ? -1 : 0; }   /* trailing bytes are malformed too */

int portNetHelloPack(const PortNetHello *m, unsigned char *buf, int cap)
{
    W w;
    w.b = buf; w.cap = cap; w.n = 0; w.bad = 0;
    w8(&w, PORT_NETMSG_HELLO);
    w16(&w, m->proto);
    wstr(&w, m->build, PORT_NET_STR_BUILD);
    wopts(&w, &m->opts);
    return fin(&w);
}

int portNetHelloUnpack(const unsigned char *buf, int len, PortNetHello *m)
{
    R r;
    r.b = buf; r.len = len; r.n = 0; r.bad = 0;
    if (r8(&r) != PORT_NETMSG_HELLO) return -1;
    m->proto = (unsigned short) r16(&r);
    rstr(&r, m->build, PORT_NET_STR_BUILD);
    ropts(&r, &m->opts);
    return finr(&r);
}

int portNetSetupPack(const PortNetSetup *m, unsigned char *buf, int cap)
{
    W w;
    w.b = buf; w.cap = cap; w.n = 0; w.bad = 0;
    w8(&w, PORT_NETMSG_SETUP);
    w16(&w, m->proto);
    wstr(&w, m->build, PORT_NET_STR_BUILD);
    wstr(&w, m->stage, PORT_NET_STR_STAGE);
    w32(&w, (unsigned int) m->diff);
    w32(&w, m->seed);
    wstr(&w, m->mods, PORT_NET_STR_MODS);
    w32(&w, m->luaHash);
    w8(&w, m->delay);
    wopts(&w, &m->hostOpts);
    wopts(&w, &m->guestOpts);
    wsimcfg(&w, &m->simCfg);
    return fin(&w);
}

int portNetSetupUnpack(const unsigned char *buf, int len, PortNetSetup *m)
{
    R r;
    r.b = buf; r.len = len; r.n = 0; r.bad = 0;
    if (r8(&r) != PORT_NETMSG_SETUP) return -1;
    m->proto = (unsigned short) r16(&r);
    rstr(&r, m->build, PORT_NET_STR_BUILD);
    rstr(&r, m->stage, PORT_NET_STR_STAGE);
    m->diff = (int) r32(&r);
    if (!portNetDiffValid(m->diff)) return -1;
    m->seed = r32(&r);
    rstr(&r, m->mods, PORT_NET_STR_MODS);
    m->luaHash = r32(&r);
    m->delay = (unsigned char) r8(&r);
    ropts(&r, &m->hostOpts);
    ropts(&r, &m->guestOpts);
    rsimcfg(&r, &m->simCfg);
    return finr(&r);
}

int portNetAcceptPack(unsigned char *buf, int cap)
{
    if (cap < 1) return -1;
    buf[0] = PORT_NETMSG_ACCEPT;
    return 1;
}

int portNetReasonPack(int type, const char *reason, unsigned char *buf, int cap)
{
    W w;
    char tmp[PORT_NET_STR_REASON];
    int n = slen(reason, PORT_NET_STR_REASON), i;
    for (i = 0; i < n; i++) tmp[i] = reason[i];   /* a long reason is cut, not refused */
    tmp[n] = '\0';
    w.b = buf; w.cap = cap; w.n = 0; w.bad = 0;
    w8(&w, type);
    wstr(&w, tmp, PORT_NET_STR_REASON);
    return fin(&w);
}

int portNetReasonUnpack(const unsigned char *buf, int len, char *reason, int reasonCap)
{
    R r;
    char tmp[PORT_NET_STR_REASON];
    int i;
    r.b = buf; r.len = len; r.n = 0; r.bad = 0;
    if (r8(&r) != PORT_NETMSG_REFUSE) return -1;
    rstr(&r, tmp, PORT_NET_STR_REASON);
    if (finr(&r) != 0) return -1;
    for (i = 0; i < reasonCap - 1 && tmp[i] != '\0'; i++) reason[i] = tmp[i];
    reason[i] = '\0';
    return 0;
}

int portNetInputPack(const PortNetInputPkt *m, unsigned char *buf, int cap)
{
    W w;
    int i;
    if (m->count < 0 || m->count > PORT_NET_MAX_RECS) return -1;
    w.b = buf; w.cap = cap; w.n = 0; w.bad = 0;
    w8(&w, PORT_NETMSG_INPUT);
    w32(&w, m->run);
    w32(&w, m->firstTick);
    w32(&w, m->ack);
    w8(&w, m->count);
    w32(&w, m->cmds);
    for (i = 0; i < m->count; i++)
    {
        if (w.n + PORT_NET_REC_SIZE > w.cap) { w.bad = 1; break; }
        portNetRecPack(&m->rec[i], w.b + w.n);
        w.n += PORT_NET_REC_SIZE;
    }
    return fin(&w);
}

int portNetInputUnpack(const unsigned char *buf, int len, PortNetInputPkt *m)
{
    R r;
    int i;
    r.b = buf; r.len = len; r.n = 0; r.bad = 0;
    if (r8(&r) != PORT_NETMSG_INPUT) return -1;
    m->run = r32(&r);
    m->firstTick = r32(&r);
    m->ack = r32(&r);
    m->count = (int) r8(&r);
    m->cmds = r32(&r);
    if (r.bad || m->count > PORT_NET_MAX_RECS || r.n + m->count * PORT_NET_REC_SIZE != len) return -1;
    for (i = 0; i < m->count; i++)
    {
        portNetRecUnpack(buf + r.n, &m->rec[i]);
        if ((m->rec[i].flags & ~PORT_NETF_KNOWN) != 0) return -1;   /* a flag this build does not know: a newer peer */
        r.n += PORT_NET_REC_SIZE;
    }
    return 0;
}

int portNetHashPack(const PortNetHashMsg *m, unsigned char *buf, int cap)
{
    W w;
    int i;
    if (m->nfields < 0 || m->nfields > PORT_NET_HASH_FIELDS) return -1;
    w.b = buf; w.cap = cap; w.n = 0; w.bad = 0;
    w8(&w, PORT_NETMSG_HASH);
    w32(&w, m->tick);
    w32(&w, m->run);
    w32(&w, m->hash);
    w8(&w, (unsigned) m->nfields);
    for (i = 0; i < m->nfields; i++) w32(&w, m->fields[i]);
    return fin(&w);
}

int portNetHashUnpack(const unsigned char *buf, int len, PortNetHashMsg *m)
{
    R r;
    int i;
    r.b = buf; r.len = len; r.n = 0; r.bad = 0;
    if (r8(&r) != PORT_NETMSG_HASH) return -1;
    m->tick = r32(&r);
    m->run = r32(&r);
    m->hash = r32(&r);
    m->nfields = (int) r8(&r);
    if (r.bad || m->nfields > PORT_NET_HASH_FIELDS) return -1;
    for (i = 0; i < m->nfields; i++) m->fields[i] = r32(&r);
    return finr(&r);
}

int portNetRunPack(int type, unsigned int run, const PortNetPlayerOpts *a, const PortNetPlayerOpts *b, unsigned char *buf, int cap)
{
    W w;
    if (type != PORT_NETMSG_READY && type != PORT_NETMSG_GO) return -1;
    if (a == 0 || (type == PORT_NETMSG_GO && b == 0)) return -1;
    w.b = buf; w.cap = cap; w.n = 0; w.bad = 0;
    w8(&w, (unsigned) type);
    w32(&w, run);
    wopts(&w, a);
    if (type == PORT_NETMSG_GO) wopts(&w, b);
    return fin(&w);
}

int portNetRunUnpack(const unsigned char *buf, int len, int *type, unsigned int *run, PortNetPlayerOpts *a, PortNetPlayerOpts *b)
{
    R r;
    int t;
    r.b = buf; r.len = len; r.n = 0; r.bad = 0;
    t = (int) r8(&r);
    if (t != PORT_NETMSG_READY && t != PORT_NETMSG_GO) return -1;
    *type = t;
    *run = r32(&r);
    ropts(&r, a);
    if (t == PORT_NETMSG_GO) ropts(&r, b);
    return finr(&r);
}

int portNetModeValid(const char *mode)
{
    int n = 0;
    if (mode == 0) return 1;
    for (; mode[n] != '\0'; n++)
    {
        char c = mode[n];
        if (n >= PORT_NET_STR_MODE - 1) return 0;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return 0;
    }
    return 1;
}

int portNetNextStagePackM(unsigned int run, const char *stage, int diff, unsigned int seed, const char *mode, unsigned char *buf, int cap)
{
    W w;
    if (!portNetModeValid(mode)) return -1;
    w.b = buf; w.cap = cap; w.n = 0; w.bad = 0;
    w8(&w, PORT_NETMSG_NEXTSTAGE);
    w32(&w, run);
    wstr(&w, stage, PORT_NET_STR_STAGE);
    w32(&w, (unsigned int) diff);
    w32(&w, seed);
    wstr(&w, mode != 0 ? mode : "", PORT_NET_STR_MODE);
    return fin(&w);
}

int portNetNextStagePack(unsigned int run, const char *stage, int diff, unsigned int seed, unsigned char *buf, int cap)
{
    return portNetNextStagePackM(run, stage, diff, seed, "", buf, cap);
}

int portNetNextStageUnpackM(const unsigned char *buf, int len, unsigned int *run, char *stage, int stageCap, int *diff, unsigned int *seed, char *mode, int modeCap)
{
    R r;
    char tmp[PORT_NET_STR_STAGE];
    char mtmp[PORT_NET_STR_MODE];
    int i;
    r.b = buf; r.len = len; r.n = 0; r.bad = 0;
    if (r8(&r) != PORT_NETMSG_NEXTSTAGE) return -1;
    *run = r32(&r);
    rstr(&r, tmp, PORT_NET_STR_STAGE);
    *diff = (int) r32(&r);
    *seed = r32(&r);
    rstr(&r, mtmp, PORT_NET_STR_MODE);
    if (finr(&r) != 0 || !portNetDiffValid(*diff) || !portNetModeValid(mtmp)) return -1;
    for (i = 0; i < stageCap - 1 && tmp[i] != '\0'; i++) stage[i] = tmp[i];
    stage[i] = '\0';
    for (i = 0; mode != 0 && i < modeCap - 1 && mtmp[i] != '\0'; i++) mode[i] = mtmp[i];
    if (mode != 0 && modeCap > 0) mode[i] = '\0';
    return 0;
}

int portNetNextStageUnpack(const unsigned char *buf, int len, unsigned int *run, char *stage, int stageCap, int *diff, unsigned int *seed)
{
    return portNetNextStageUnpackM(buf, len, run, stage, stageCap, diff, seed, 0, 0);
}

static const char sHexDigits[] = "0123456789abcdef";

void portNetRecToHex(const PortNetInput *in, char *out)
{
    unsigned char p[PORT_NET_REC_SIZE];
    int i;
    portNetRecPack(in, p);
    for (i = 0; i < PORT_NET_REC_SIZE; i++)
    {
        out[i * 2] = sHexDigits[p[i] >> 4];
        out[i * 2 + 1] = sHexDigits[p[i] & 15];
    }
    out[PORT_NET_RECHEX] = '\0';
}

static int hexVal(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int portNetRecFromHex(const char *hex, PortNetInput *out)
{
    unsigned char p[PORT_NET_REC_SIZE];
    int i;
    for (i = 0; i < PORT_NET_REC_SIZE; i++)
    {
        int hi = hexVal(hex[i * 2]);
        int lo = (hi < 0) ? -1 : hexVal(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return -1;   /* a bad digit, or the string ended early (the NUL is not a digit) */
        p[i] = (unsigned char) (hi * 16 + lo);
    }
    if (hex[PORT_NET_RECHEX] != '\0' && hex[PORT_NET_RECHEX] != ' ' && hex[PORT_NET_RECHEX] != '\n' && hex[PORT_NET_RECHEX] != '\r') return -1;
    portNetRecUnpack(p, out);
    if ((out->flags & ~PORT_NETF_KNOWN) != 0) return -1;
    return 0;
}

unsigned int portNetFnv(unsigned int h, const void *data, int len)
{
    const unsigned char *p = (const unsigned char *) data;
    int i;
    for (i = 0; i < len; i++)
    {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

/* ---- session commands (job 5) ---- */

int portNetCmdValid(const PortNetCmd *c)
{
    if (c->issuer < 0 || c->issuer > 1 || c->seq == 0 || c->tick == 0) return -1;
    switch (c->kind)
    {
    case PORT_NETCMD_SPAWN_GUARD:
        return (c->a >= 1 && c->a <= 40 && c->b >= 100 && c->b <= 5000 && c->player == c->issuer) ? 0 : -1;
    case PORT_NETCMD_SPAWN_CLEAR:
        return (c->a == 0 && c->b == 0 && c->player == c->issuer) ? 0 : -1;
    case PORT_NETCMD_GIVE_ITEM:
        return (c->player >= 0 && c->player <= 1 && c->a >= 0 && c->a <= PORT_NETCMD_MAXITEM && c->b >= -1 && c->b <= 9999) ? 0 : -1;
    case PORT_NETCMD_GIVE_KEY:
        return (c->player >= 0 && c->player <= 1 && c->a >= -2 && c->a <= 1023 && c->b >= 1 && c->b <= 64) ? 0 : -1;
    case PORT_NETCMD_PLAN_POINT:
        return (c->player == c->issuer && c->a >= 0 && c->a <= 1023 && c->b >= 1 && c->b <= 5) ? 0 : -1;
    default:
        return -1;
    }
}

int portNetCmdPack(const PortNetCmd *c, unsigned char *buf, int cap)
{
    W w;
    if (portNetCmdValid(c) != 0) return -1;
    w.b = buf; w.cap = cap; w.n = 0; w.bad = 0;
    w8(&w, PORT_NETMSG_CMD);
    w32(&w, c->run);
    w32(&w, c->tick);
    w32(&w, c->seq);
    w8(&w, (unsigned) c->issuer);
    w8(&w, (unsigned) c->kind);
    w8(&w, (unsigned) c->player);
    w32(&w, (unsigned int) c->a);
    w32(&w, (unsigned int) c->b);
    return fin(&w);
}

int portNetCmdUnpack(const unsigned char *buf, int len, PortNetCmd *c)
{
    R r;
    r.b = buf; r.len = len; r.n = 0; r.bad = 0;
    if (len != PORT_NETCMD_SIZE || r8(&r) != PORT_NETMSG_CMD) return -1;
    c->run = r32(&r);
    c->tick = r32(&r);
    c->seq = r32(&r);
    c->issuer = (int) r8(&r);
    c->kind = (int) r8(&r);
    c->player = (int) r8(&r);
    c->a = (int) r32(&r);
    c->b = (int) r32(&r);
    if (r.bad || r.n != len) return -1;
    return portNetCmdValid(c);
}

void portNetNameKey(const char *s, char *out, int cap)
{
    int n = 0;
    for (; *s != '\0' && n < cap - 1; s++)
    {
        char ch = *s;
        if (ch >= 'A' && ch <= 'Z') ch = (char) (ch - 'A' + 'a');
        if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) out[n++] = ch;
    }
    out[n] = '\0';
    if (n >= 5 && out[0] == 'i' && out[1] == 't' && out[2] == 'e' && out[3] == 'm')
    {
        int i;
        for (i = 4; i <= n; i++) out[i - 4] = out[i];
    }
}
