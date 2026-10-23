/* netmsg_test.c - unit tests for the pure netplay wire formats in port_netmsg.c. */
#include <stdio.h>
#include <string.h>
#include "port_netmsg.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond)                                              \
    do                                                           \
    {                                                            \
        if (cond)                                                \
        {                                                        \
            g_pass++;                                            \
        }                                                        \
        else                                                     \
        {                                                        \
            g_fail++;                                            \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                        \
    } while (0)

static PortNetInput sample(int k)
{
    PortNetInput in;
    memset(&in, 0, sizeof(in));
    in.buttons = (unsigned short) (0x8001 + k);
    in.stickX = (signed char) (-80 + k);
    in.stickY = (signed char) (79 - k);
    in.actions = 0xDEAD0000u | (unsigned) k;
    in.digX = -1;
    in.digY = 1;
    in.flags = (unsigned short) (k & 3);
    in.yawMilli = -123456 - k;
    in.pitchMilli = 98765 + k;
    return in;
}

static int same(const PortNetInput *a, const PortNetInput *b)   /* field by field: padding bytes are indeterminate */
{
    return a->buttons == b->buttons && a->stickX == b->stickX && a->stickY == b->stickY && a->actions == b->actions
        && a->digX == b->digX && a->digY == b->digY && a->flags == b->flags && a->yawMilli == b->yawMilli
        && a->pitchMilli == b->pitchMilli;
}

static void test_record(void)
{
    PortNetInput a = sample(3), b;
    unsigned char buf[PORT_NET_REC_SIZE];
    static const unsigned char golden[PORT_NET_REC_SIZE] = {
        0x04, 0x80, 0xB3, 0x4C, 0x03, 0x00, 0xAD, 0xDE, 0xFF, 0x01, 0x03, 0x00,
        0x00 /* yaw below */, 0, 0, 0, 0, 0, 0, 0 };
    portNetRecPack(&a, buf);
    portNetRecUnpack(buf, &b);
    CHECK(same(&a, &b));
    /* fixed little-endian layout: check the leading 12 bytes against hand-computed values (bytes 10..11 are the flags, job 3b) */
    CHECK(memcmp(buf, golden, 12) == 0);
    CHECK(b.flags == (PORT_NETF_ESC | PORT_NETF_RETSTART));
    /* yaw -123459 = 0xFFFE1DBD, pitch 98768 = 0x000181D0 */
    CHECK(buf[12] == 0xBD && buf[13] == 0x1D && buf[14] == 0xFE && buf[15] == 0xFF);
    CHECK(buf[16] == 0xD0 && buf[17] == 0x81 && buf[18] == 0x01 && buf[19] == 0x00);
}

static PortNetPlayerOpts opts_sample(int k)
{
    PortNetPlayerOpts o;
    memset(&o, 0, sizeof(o));
    o.controlStyle = (unsigned char) (k % 8);
    o.padN64 = (unsigned char) (k & 1);
    o.lookUpright = (unsigned char) ((k >> 1) & 1);
    o.autoAim = 1;
    o.lookAhead = (unsigned char) (k & 1);
    o.aimToggle = (unsigned char) ((k >> 2) & 1);
    o.aimLock = (unsigned char) ((k >> 1) & 1);
    o.mouseAimOff = (unsigned char) (k == 5);
    o.aimSens = (unsigned short) (1000 + 257 * k);
    o.shootNextPrev = (unsigned char) (k & 1);
    o.detonateUseReload = (unsigned char) ((k >> 2) & 1);
    return o;
}

static void test_opts(void)
{
    PortNetPlayerOpts a = opts_sample(5), b, c;
    unsigned char p[PORT_NET_OPTS_SIZE];
    static const unsigned char golden[PORT_NET_OPTS_SIZE] = { 5, 1, 0, 1, 1, 1, 0, 1, 0xED, 0x08, 1, 1 };   /* aimSens 2285 = 0x08ED, little endian; then shootNextPrev, detonateUseReload */
    portNetOptsPack(&a, p);
    CHECK(memcmp(p, golden, PORT_NET_OPTS_SIZE) == 0);    /* fixed layout, field order as declared */
    portNetOptsUnpack(p, &b);
    CHECK(portNetOptsEqual(&a, &b));
    c = a;
    c.lookUpright = 1;                                    /* invert pitch differs */
    CHECK(!portNetOptsEqual(&a, &c));
    c = a;
    c.controlStyle = 6;                                   /* control style differs */
    CHECK(!portNetOptsEqual(&a, &c));
    c = a;
    c.shootNextPrev = 0;                                  /* the two job 3b fields count too */
    CHECK(!portNetOptsEqual(&a, &c));
    c = a;
    c.detonateUseReload = 0;
    CHECK(!portNetOptsEqual(&a, &c));
    c = a;
    c.mouseAimOff = 0;
    CHECK(!portNetOptsEqual(&a, &c));
    CHECK(portNetOptsHash(2166136261u, &a) != portNetOptsHash(2166136261u, &c));
    CHECK(portNetOptsHash(2166136261u, &a) == portNetOptsHash(2166136261u, &b));
}

static void test_hello(void)
{
    PortNetHello a, b;
    unsigned char buf[PORT_NET_BUF_MAX];
    int n;
    memset(&a, 0, sizeof(a));
    a.proto = PORT_NET_PROTO;
    strcpy(a.build, "123-20261021 abcdef0");
    a.opts = opts_sample(1);
    n = portNetHelloPack(&a, buf, sizeof(buf));
    CHECK(n == 1 + 2 + 1 + (int) strlen(a.build) + PORT_NET_OPTS_SIZE);
    CHECK(portNetMsgType(buf, n) == PORT_NETMSG_HELLO);
    CHECK(portNetHelloUnpack(buf, n, &b) == 0);
    CHECK(b.proto == a.proto && strcmp(a.build, b.build) == 0);
    CHECK(portNetOptsEqual(&a.opts, &b.opts));         /* the guest's settings block rides in HELLO */
    CHECK(portNetHelloUnpack(buf, n - 1, &b) == -1);   /* short */
    CHECK(portNetHelloPack(&a, buf, n - 1) == -1);     /* cap too small */
    buf[n] = 0;
    CHECK(portNetHelloUnpack(buf, n + 1, &b) == -1);   /* trailing byte */
    buf[0] = PORT_NETMSG_SETUP;
    CHECK(portNetHelloUnpack(buf, n, &b) == -1);       /* wrong type */
}

static void test_setup(void)
{
    PortNetSetup a, b;
    unsigned char buf[PORT_NET_BUF_MAX];
    int n;
    memset(&a, 0, sizeof(a));
    a.proto = PORT_NET_PROTO;
    strcpy(a.build, "b");
    strcpy(a.stage, "dam");
    a.diff = 2;
    a.seed = 0xCAFEBABEu;
    strcpy(a.mods, "turbo=1,enemy_damage=400");
    a.luaHash = 0x811C9DC5u;
    a.delay = 3;
    a.hostOpts = opts_sample(2);
    a.guestOpts = opts_sample(7);
    a.simCfg.n = 5;
    a.simCfg.v[0] = 1; a.simCfg.v[1] = -2; a.simCfg.v[4] = 0x7fffffff;
    n = portNetSetupPack(&a, buf, sizeof(buf));
    CHECK(n > 0);
    CHECK(portNetSetupUnpack(buf, n, &b) == 0);
    CHECK(b.simCfg.n == 5 && b.simCfg.v[0] == 1 && b.simCfg.v[1] == -2 && b.simCfg.v[4] == 0x7fffffff);   /* the host's sim-config list rides in SETUP */
    CHECK(b.proto == a.proto && b.diff == 2 && b.seed == a.seed && b.luaHash == a.luaHash && b.delay == 3);
    CHECK(strcmp(b.stage, "dam") == 0 && strcmp(b.mods, a.mods) == 0 && strcmp(b.build, "b") == 0);
    CHECK(portNetOptsEqual(&a.hostOpts, &b.hostOpts) && portNetOptsEqual(&a.guestOpts, &b.guestOpts));
    CHECK(!portNetOptsEqual(&b.hostOpts, &b.guestOpts));   /* the two blocks are not mixed up */
    CHECK(portNetSetupUnpack(buf, n - 1, &b) == -1);
    memset(a.mods, 'x', sizeof(a.mods));   /* no terminator: refused, never truncated */
    CHECK(portNetSetupPack(&a, buf, sizeof(buf)) == -1);
    a.mods[sizeof(a.mods) - 1] = '\0';     /* 255 chars: the longest that fits */
    n = portNetSetupPack(&a, buf, sizeof(buf));
    CHECK(n > 0 && portNetSetupUnpack(buf, n, &b) == 0 && strlen(b.mods) == 255);
}

static void test_accept_refuse(void)
{
    unsigned char buf[PORT_NET_BUF_MAX];
    char why[PORT_NET_STR_REASON];
    int n = portNetAcceptPack(buf, sizeof(buf));
    CHECK(n == 1 && portNetMsgType(buf, n) == PORT_NETMSG_ACCEPT);
    CHECK(portNetAcceptPack(buf, 0) == -1);
    n = portNetReasonPack(PORT_NETMSG_REFUSE, "build mismatch", buf, sizeof(buf));
    CHECK(n == 1 + 1 + 14);
    CHECK(portNetReasonUnpack(buf, n, why, sizeof(why)) == 0 && strcmp(why, "build mismatch") == 0);
    CHECK(portNetReasonUnpack(buf, n, why, 6) == 0 && strcmp(why, "build") == 0);   /* small out buffer is cut */
    {
        char longr[300];
        memset(longr, 'r', sizeof(longr));
        longr[299] = '\0';
        n = portNetReasonPack(PORT_NETMSG_REFUSE, longr, buf, sizeof(buf));
        CHECK(n == 1 + 1 + PORT_NET_STR_REASON - 1);   /* cut to fit */
        CHECK(portNetReasonUnpack(buf, n, why, sizeof(why)) == 0 && strlen(why) == PORT_NET_STR_REASON - 1);
    }
    CHECK(portNetReasonUnpack(buf, 1, why, sizeof(why)) == -1);
}

static void test_input_pkt(void)
{
    PortNetInputPkt a, b;
    unsigned char buf[PORT_NET_BUF_MAX];
    int i, n;
    memset(&a, 0, sizeof(a));
    a.run = 4;
    a.firstTick = 1000;
    a.ack = 998;
    a.count = PORT_NET_MAX_RECS;
    for (i = 0; i < a.count; i++) a.rec[i] = sample(i);
    n = portNetInputPack(&a, buf, sizeof(buf));
    CHECK(n == 1 + 4 + 4 + 4 + 1 + 4 + PORT_NET_MAX_RECS * PORT_NET_REC_SIZE);   /* (job 5: + the issued-command count) */
    CHECK(portNetInputUnpack(buf, n, &b) == 0);
    CHECK(b.run == 4 && b.firstTick == 1000 && b.ack == 998 && b.count == PORT_NET_MAX_RECS);
    CHECK(same(&a.rec[0], &b.rec[0]));
    CHECK(same(&a.rec[15], &b.rec[15]));
    CHECK(portNetInputUnpack(buf, n - 1, &b) == -1);
    CHECK(portNetInputPack(&a, buf, n - 1) == -1);
    a.count = PORT_NET_MAX_RECS + 1;
    CHECK(portNetInputPack(&a, buf, sizeof(buf)) == -1);
    a.count = 0;
    n = portNetInputPack(&a, buf, sizeof(buf));
    CHECK(n == 18 && portNetInputUnpack(buf, n, &b) == 0 && b.count == 0);
    buf[13] = 17;   /* count lies */
    CHECK(portNetInputUnpack(buf, n, &b) == -1);
}

static void test_ready_go(void)
{
    unsigned char buf[PORT_NET_BUF_MAX];
    int n, type = 0;
    unsigned int run = 0;
    PortNetPlayerOpts oa = opts_sample(5), ob = opts_sample(2), ra, rb;
    memset(&ra, 0, sizeof(ra));
    memset(&rb, 0, sizeof(rb));
    n = portNetRunPack(PORT_NETMSG_READY, 0x01020304u, &oa, NULL, buf, sizeof(buf));
    CHECK(n == 5 + PORT_NET_OPTS_SIZE && buf[0] == PORT_NETMSG_READY && buf[1] == 0x04 && buf[4] == 0x01);
    CHECK(portNetRunUnpack(buf, n, &type, &run, &ra, &rb) == 0 && type == PORT_NETMSG_READY && run == 0x01020304u);
    CHECK(portNetOptsEqual(&oa, &ra));                                   /* READY carries the sender's refreshed block */
    n = portNetRunPack(PORT_NETMSG_GO, 7, &oa, &ob, buf, sizeof(buf));
    CHECK(n == 5 + 2 * PORT_NET_OPTS_SIZE && portNetRunUnpack(buf, n, &type, &run, &ra, &rb) == 0 && type == PORT_NETMSG_GO && run == 7);
    CHECK(portNetOptsEqual(&oa, &ra) && portNetOptsEqual(&ob, &rb) && !portNetOptsEqual(&ra, &rb));   /* GO: host block, then the guest's echo */
    CHECK(portNetRunUnpack(buf, n - 1, &type, &run, &ra, &rb) == -1);    /* short */
    CHECK(portNetRunPack(PORT_NETMSG_GO, 7, &oa, &ob, buf, 4) == -1);    /* cap too small */
    CHECK(portNetRunPack(PORT_NETMSG_GO, 7, &oa, NULL, buf, sizeof(buf)) == -1);   /* GO needs both blocks */
    CHECK(portNetRunPack(PORT_NETMSG_HASH, 7, &oa, &ob, buf, sizeof(buf)) == -1);  /* not a run message */
    buf[0] = PORT_NETMSG_ACCEPT;
    CHECK(portNetRunUnpack(buf, 5, &type, &run, &ra, &rb) == -1);
    buf[0] = PORT_NETMSG_GO;
    buf[n] = 0;
    CHECK(portNetRunUnpack(buf, n + 1, &type, &run, &ra, &rb) == -1);    /* trailing byte */
    n = portNetRunPack(PORT_NETMSG_READY, 3, &oa, NULL, buf, sizeof(buf));
    CHECK(portNetRunUnpack(buf, n - 1, &type, &run, &ra, &rb) == -1);    /* a READY of the old (block-less) size is refused */
}

static void test_flags_hex(void)
{
    PortNetInput a = sample(2), b, c;
    PortNetInputPkt p, q;
    unsigned char buf[PORT_NET_BUF_MAX];
    char hex[PORT_NET_RECHEX + 1];
    int n;

    /* every flag combination survives pack / unpack, and flags do not leak into the neighbouring fields */
    for (n = 0; n <= PORT_NETF_KNOWN; n++)
    {
        a.flags = (unsigned short) n;
        portNetRecPack(&a, buf);
        portNetRecUnpack(buf, &b);
        CHECK(same(&a, &b) && b.flags == (unsigned short) n);
    }
    /* the hex form of a record (the session log): exact round trip, and a golden string */
    memset(&a, 0, sizeof(a));
    a.buttons = 0x8001; a.stickX = -2; a.stickY = 3; a.actions = 0x00000040u; a.digX = 1; a.digY = -1;
    a.flags = PORT_NETF_ESC; a.yawMilli = 1000; a.pitchMilli = -1;
    portNetRecToHex(&a, hex);
    CHECK(strcmp(hex, "0180fe0340000000" "01ff0100" "e8030000" "ffffffff") == 0);
    CHECK(portNetRecFromHex(hex, &c) == 0 && same(&a, &c));
    hex[0] = 'G';
    CHECK(portNetRecFromHex(hex, &c) == -1);                             /* bad digit */
    hex[0] = '0';
    CHECK(portNetRecFromHex("0180fe03", &c) == -1);                      /* short */
    portNetRecToHex(&a, hex);
    hex[22] = '0'; hex[23] = '4';                                        /* byte 11, the flags' high byte: 0x0400 is no such flag (job 4: bits 2..9 are the zoom levels) */
    CHECK(portNetRecFromHex(hex, &c) == -1);
    portNetRecToHex(&a, hex);
    hex[20] = '0'; hex[21] = '4';                                        /* byte 10, the low byte: 0x04 is the zoom-in level 1 now */
    CHECK(portNetRecFromHex(hex, &c) == 0 && PORT_NETF_ZIN(c.flags) == 1);
    portNetRecToHex(&a, hex);
    CHECK(portNetRecFromHex(hex, &c) == 0);
    {   /* a long string with the record followed by more text is not a record */
        char longer[PORT_NET_RECHEX + 8];
        memcpy(longer, hex, PORT_NET_RECHEX + 1);
        strcpy(longer + PORT_NET_RECHEX, "00");
        CHECK(portNetRecFromHex(longer, &c) == -1);
        strcpy(longer + PORT_NET_RECHEX, " tail");
        CHECK(portNetRecFromHex(longer, &c) == 0);                       /* the log puts a space between the two records */
    }
    /* an INPUT packet with a record that has an unknown flag bit is refused as a whole */
    memset(&p, 0, sizeof(p));
    p.run = 1; p.firstTick = 5; p.ack = 5; p.count = 2;
    p.rec[0] = sample(1); p.rec[1] = sample(2);
    n = portNetInputPack(&p, buf, sizeof(buf));
    CHECK(portNetInputUnpack(buf, n, &q) == 0 && q.rec[0].flags == 1 && q.rec[1].flags == 2);
    buf[1 + 4 + 4 + 4 + 1 + 4 + PORT_NET_REC_SIZE + 11] = 0x04;             /* the second record's flags high byte: bit 10 is unknown */
    CHECK(portNetInputUnpack(buf, n, &q) == -1);
    /* the zoom levels (job 4): 4 bits each, they do not touch the Esc / Return bits */
    {
        PortNetInput z;
        unsigned zi, zo;

        for (zi = 0; zi < 16; zi++)
        {
            for (zo = 0; zo < 16; zo++)
            {
                memset(&z, 0, sizeof(z));
                z.flags = (unsigned short) (PORT_NETF_ZOOM(zi, zo) | PORT_NETF_RETSTART);
                portNetRecPack(&z, buf);
                portNetRecUnpack(buf, &b);
                CHECK(PORT_NETF_ZIN(b.flags) == zi && PORT_NETF_ZOUT(b.flags) == zo && (b.flags & PORT_NETF_RETSTART) && !(b.flags & PORT_NETF_ESC));
                CHECK((b.flags & ~PORT_NETF_KNOWN) == 0);
            }
        }
    }
}

static void test_nextstage(void)
{
    unsigned char buf[PORT_NET_BUF_MAX];
    char st[PORT_NET_STR_STAGE];
    unsigned int run = 0, seed = 0;
    int diff = 0, n;

    n = portNetNextStagePack(2, "facility", 3, 0xDEADBEEFu, buf, sizeof(buf));
    CHECK(n == 1 + 4 + 1 + 8 + 4 + 4 + 1 && portNetMsgType(buf, n) == PORT_NETMSG_NEXTSTAGE);   /* (protocol 7: a trailing mode text, empty here) */
    {   /* modes in co-op: the host's game mode rides in NEXTSTAGE */
        char md[PORT_NET_STR_MODE];
        char longMode[PORT_NET_STR_MODE + 4];

        n = portNetNextStagePackM(3, "42", 1, 77, "hostage-flatland", buf, sizeof(buf));
        CHECK(n == 1 + 4 + 1 + 2 + 4 + 4 + 1 + 16);
        CHECK(portNetNextStageUnpackM(buf, n, &run, st, sizeof(st), &diff, &seed, md, sizeof(md)) == 0 && run == 3 && strcmp(st, "42") == 0 && seed == 77 && strcmp(md, "hostage-flatland") == 0);
        CHECK(portNetNextStageUnpack(buf, n, &run, st, sizeof(st), &diff, &seed) == 0);     /* the mode-less call reads the same message */
        CHECK(portNetNextStageUnpackM(buf, n - 1, &run, st, sizeof(st), &diff, &seed, md, sizeof(md)) == -1);   /* short */
        n = portNetNextStagePackM(3, "42", 1, 77, "", buf, sizeof(buf));
        CHECK(n > 0 && portNetNextStageUnpackM(buf, n, &run, st, sizeof(st), &diff, &seed, md, sizeof(md)) == 0 && md[0] == '\0');
        CHECK(portNetNextStagePackM(3, "42", 1, 77, "bad mode!", buf, sizeof(buf)) == -1);    /* the packer refuses an illegal name ... */
        CHECK(portNetNextStagePackM(3, "42", 1, 77, "a/b", buf, sizeof(buf)) == -1);
        CHECK(portNetNextStagePackM(3, "42", 1, 77, "..", buf, sizeof(buf)) == -1);
        memset(longMode, 'm', sizeof(longMode) - 1);
        longMode[sizeof(longMode) - 1] = '\0';
        CHECK(portNetNextStagePackM(3, "42", 1, 77, longMode, buf, sizeof(buf)) == -1);        /* ... and one that is too long */
        n = portNetNextStagePackM(3, "42", 1, 77, "ok_mode-1", buf, sizeof(buf));
        CHECK(n > 0);
        buf[n - 3] = '/';                                                                      /* a peer that sends a path in the mode text: the unpacker refuses it */
        CHECK(portNetNextStageUnpackM(buf, n, &run, st, sizeof(st), &diff, &seed, md, sizeof(md)) == -1);
        CHECK(portNetModeValid("") == 1 && portNetModeValid("hostage") == 1 && portNetModeValid("a b") == 0 && portNetModeValid("x.lua") == 0);
        n = portNetNextStagePack(2, "facility", 3, 0xDEADBEEFu, buf, sizeof(buf));
    }
    CHECK(portNetNextStageUnpack(buf, n, &run, st, sizeof(st), &diff, &seed) == 0 && run == 2 && strcmp(st, "facility") == 0 && diff == 3 && seed == 0xDEADBEEFu);
    CHECK(portNetNextStageUnpack(buf, n - 1, &run, st, sizeof(st), &diff, &seed) == -1);   /* short */
    buf[n] = 0;
    CHECK(portNetNextStageUnpack(buf, n + 1, &run, st, sizeof(st), &diff, &seed) == -1);   /* trailing byte */
    CHECK(portNetNextStagePack(2, "facility", 3, 1, buf, 10) == -1);                       /* cap too small */
    {
        char longName[PORT_NET_STR_STAGE + 4];

        memset(longName, 'x', sizeof(longName) - 1);
        longName[sizeof(longName) - 1] = '\0';
        CHECK(portNetNextStagePack(2, longName, 0, 1, buf, sizeof(buf)) == -1);            /* a stage text too long is refused, not cut */
    }
    n = portNetNextStagePack(7, "", 0, 0, buf, sizeof(buf));
    CHECK(n > 0 && portNetNextStageUnpack(buf, n, &run, st, sizeof(st), &diff, &seed) == 0 && st[0] == '\0' && diff == 0);
    buf[0] = PORT_NETMSG_GO;
    CHECK(portNetNextStageUnpack(buf, n, &run, st, sizeof(st), &diff, &seed) == -1);       /* another type */
    /* network-supplied values are validated (security review): difficulty range, stage id text, settings block */
    n = portNetNextStagePack(2, "30", 4, 1, buf, sizeof(buf));
    CHECK(n > 0 && portNetNextStageUnpack(buf, n, &run, st, sizeof(st), &diff, &seed) == -1);   /* difficulty 4 */
    n = portNetNextStagePack(2, "30", -1, 1, buf, sizeof(buf));
    CHECK(n > 0 && portNetNextStageUnpack(buf, n, &run, st, sizeof(st), &diff, &seed) == -1);   /* difficulty -1 */
    n = portNetNextStagePack(2, "30", 3, 1, buf, sizeof(buf));
    CHECK(n > 0 && portNetNextStageUnpack(buf, n, &run, st, sizeof(st), &diff, &seed) == 0 && diff == 3);
    {
        int id = -1;

        CHECK(portNetParseStageId("30", 100, &id) == 0 && id == 30);
        CHECK(portNetParseStageId("0", 100, &id) == 0 && id == 0);
        CHECK(portNetParseStageId("101", 100, &id) == -1);          /* above the largest level */
        CHECK(portNetParseStageId("30x", 100, &id) == -1);          /* trailing junk */
        CHECK(portNetParseStageId("x30", 100, &id) == -1);
        CHECK(portNetParseStageId("-3", 100, &id) == -1);           /* sign */
        CHECK(portNetParseStageId("3 0", 100, &id) == -1);
        CHECK(portNetParseStageId("", 100, &id) == -1);
        CHECK(portNetParseStageId("4294967296", 100, &id) == -1);   /* overflow */
        CHECK(portNetParseStageId("0000000000030", 100, &id) == -1);   /* too long */
        CHECK(portNetParseStageId(0, 100, &id) == -1);
    }
    {   /* a settings block with a control style the sim cannot index is refused with the whole message */
        PortNetPlayerOpts o, p;
        unsigned char raw[PORT_NET_OPTS_SIZE];
        unsigned char rb[PORT_NET_BUF_MAX];
        PortNetHello h, h2;

        memset(&o, 0, sizeof(o));
        o.controlStyle = 9;
        memset(&h, 0, sizeof(h));
        h.proto = PORT_NET_PROTO;
        h.opts = o;
        n = portNetHelloPack(&h, rb, sizeof(rb));
        CHECK(n > 0 && portNetHelloUnpack(rb, n, &h2) == -1);
        h.opts.controlStyle = 7;
        n = portNetHelloPack(&h, rb, sizeof(rb));
        CHECK(n > 0 && portNetHelloUnpack(rb, n, &h2) == 0 && h2.opts.controlStyle == 7);
        (void) raw; (void) p;
    }
    {   /* a SETUP with a difficulty outside 0..3 is refused */
        PortNetSetup s, s2;

        memset(&s, 0, sizeof(s));
        s.proto = PORT_NET_PROTO;
        s.diff = 7;
        n = portNetSetupPack(&s, buf, sizeof(buf));
        CHECK(n > 0 && portNetSetupUnpack(buf, n, &s2) == -1);
        s.diff = 3;
        n = portNetSetupPack(&s, buf, sizeof(buf));
        CHECK(n > 0 && portNetSetupUnpack(buf, n, &s2) == 0);
        CHECK(portNetSetupUnpack(buf, n - 5, &s2) == -1);            /* truncated */
    }
}

static void test_hash_fnv(void)
{
    unsigned char buf[PORT_NET_BUF_MAX];
    PortNetHashMsg a, b;
    int i, n;
    memset(&a, 0, sizeof(a));
    a.tick = 30;
    a.run = 2;
    a.hash = 0x12345678u;
    a.nfields = 3;
    for (i = 0; i < 3; i++) a.fields[i] = 0xA0000000u + (unsigned) i;
    n = portNetHashPack(&a, buf, sizeof(buf));
    CHECK(n == 1 + 4 + 4 + 4 + 1 + 3 * 4);
    CHECK(portNetHashUnpack(buf, n, &b) == 0 && b.tick == 30 && b.run == 2 && b.hash == 0x12345678u && b.nfields == 3);
    CHECK(b.fields[0] == 0xA0000000u && b.fields[2] == 0xA0000002u);
    CHECK(portNetHashUnpack(buf, n - 1, &b) == -1);
    CHECK(portNetHashPack(&a, buf, n - 1) == -1);
    a.nfields = PORT_NET_HASH_FIELDS + 1;
    CHECK(portNetHashPack(&a, buf, sizeof(buf)) == -1);
    a.nfields = 0;
    n = portNetHashPack(&a, buf, sizeof(buf));
    CHECK(n == 14 && portNetHashUnpack(buf, n, &b) == 0 && b.nfields == 0);
    buf[13] = 9;                                          /* nfields lies */
    CHECK(portNetHashUnpack(buf, n, &b) == -1);
    buf[13] = PORT_NET_HASH_FIELDS + 1;
    CHECK(portNetHashUnpack(buf, n + 4 * (PORT_NET_HASH_FIELDS + 1), &b) == -1);
    /* FNV-1a 32 reference values */
    CHECK(portNetFnv(2166136261u, "", 0) == 2166136261u);
    CHECK(portNetFnv(2166136261u, "a", 1) == 0xE40C292Cu);
    CHECK(portNetFnv(2166136261u, "foobar", 6) == 0xBF9CF968u);
    CHECK(portNetFnv(portNetFnv(2166136261u, "foo", 3), "bar", 3) == 0xBF9CF968u);   /* chaining */
}

static void test_simcfg(void)
{
    unsigned char buf[PORT_NET_BUF_MAX];
    PortNetSimCfg a, b;
    PortNetSetup s;
    unsigned int run = 0;
    int n, i;
    memset(&a, 0, sizeof(a));
    a.n = 3;
    a.v[0] = 7; a.v[1] = -1; a.v[2] = 100000;
    n = portNetSimCfgPack(9, &a, buf, sizeof(buf));
    CHECK(n == 1 + 4 + 1 + 3 * 4 && portNetMsgType(buf, n) == PORT_NETMSG_SIMCFG);
    CHECK(portNetSimCfgUnpack(buf, n, &run, &b) == 0 && run == 9 && b.n == 3 && b.v[0] == 7 && b.v[1] == -1 && b.v[2] == 100000);
    CHECK(portNetSimCfgUnpack(buf, n - 1, &run, &b) == -1);     /* short */
    CHECK(portNetSimCfgPack(9, &a, buf, n - 1) == -1);          /* cap too small */
    buf[n] = 0;
    CHECK(portNetSimCfgUnpack(buf, n + 1, &run, &b) == -1);     /* trailing byte */
    a.n = PORT_NET_SIMCFG_MAX + 1;
    CHECK(portNetSimCfgPack(9, &a, buf, sizeof(buf)) == -1);    /* too many values */
    a.n = PORT_NET_SIMCFG_MAX;
    for (i = 0; i < a.n; i++) a.v[i] = i * 3 - 5;
    n = portNetSimCfgPack(1, &a, buf, sizeof(buf));
    CHECK(n > 0 && portNetSimCfgUnpack(buf, n, &run, &b) == 0 && b.n == PORT_NET_SIMCFG_MAX && b.v[PORT_NET_SIMCFG_MAX - 1] == a.v[PORT_NET_SIMCFG_MAX - 1]);
    buf[5] = PORT_NET_SIMCFG_MAX + 1;                           /* the count lies */
    CHECK(portNetSimCfgUnpack(buf, n, &run, &b) == -1);
    /* the biggest SETUP (longest strings, a full sim-config list) still fits the message buffer */
    memset(&s, 0, sizeof(s));
    s.proto = PORT_NET_PROTO;
    memset(s.build, 'b', PORT_NET_STR_BUILD - 1);
    memset(s.stage, 's', PORT_NET_STR_STAGE - 1);
    memset(s.mods, 'm', PORT_NET_STR_MODS - 1);
    s.simCfg.n = PORT_NET_SIMCFG_MAX;
    CHECK(portNetSetupPack(&s, buf, sizeof(buf)) > 0);
}

int main(void)
{
    test_record();
    test_simcfg();
    test_opts();
    test_hello();
    test_setup();
    test_accept_refuse();
    test_input_pkt();
    test_ready_go();
    test_flags_hex();
    test_nextstage();
    test_hash_fnv();
    printf("netmsg: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail != 0;
}
