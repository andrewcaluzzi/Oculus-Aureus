/* port_netmsg.h - pure netplay wire formats (little endian, fixed layouts, bounds checked).
 * No game or libc-shadowing includes, so tests/netmsg_test.c can build it. */
#ifndef PORT_NETMSG_H
#define PORT_NETMSG_H

#define PORT_NET_PROTO     7     /* 7 (modes in co-op): NEXTSTAGE carries the host's game mode; SETUP's session text carries "mode=<name>".
                                  * 2: per-player settings block in HELLO/SETUP, run in INPUT, fields in HASH.
                                  * 3 (job 3b): record flags (Esc, Return-as-Start), 12-byte settings block, and the
                                  * block is refreshed at READY / GO (both carry it).
                                  * 4 (job 3c): 12 sub-hashes per HASH; the sim-config list in SETUP and the SIMCFG message.
                                  * 6 (job 5): the session command message (CMD) and the issued-command count in every INPUT packet.
                                  * 5 (job 4): the analog zoom levels in the record flags, the NEXTSTAGE message (the host picks
                                  * the next mission of a session) */
#define PORT_NET_REC_SIZE  20    /* one packed input record */
#define PORT_NET_OPTS_SIZE 12    /* one packed per-player settings block */
#define PORT_NET_RECHEX    (PORT_NET_REC_SIZE * 2)   /* a record as hex text (no terminator) */
#define PORT_NET_HASH_FIELDS 12  /* sub-hashes carried in a HASH message */
#define PORT_NET_SIMCFG_MAX 40   /* values in a sim-config list (port_simcfg.c) */
#define PORT_NET_MAX_RECS  16    /* records per INPUT packet */
#define PORT_NET_STR_BUILD 64
#define PORT_NET_STR_STAGE 32
#define PORT_NET_STR_MODS  256
#define PORT_NET_STR_REASON 96
#define PORT_NET_STR_MODE  32   /* a game mode name (letters, digits, _ and -; "" = none) */
#define PORT_NET_BUF_MAX   1024  /* big enough for any message */

enum {
    PORT_NETMSG_HELLO = 1,   /* guest -> host: protocol, build id */
    PORT_NETMSG_SETUP,       /* host -> guest: the session settings */
    PORT_NETMSG_ACCEPT,      /* guest -> host: settings verified */
    PORT_NETMSG_REFUSE,      /* either way: reason text */
    PORT_NETMSG_INPUT,       /* last records plus an ack (job 2) */
    PORT_NETMSG_HASH,        /* tick + state hash (job 3) */
    PORT_NETMSG_READY,       /* stage loaded, ready for tick 1 (job 2): run number */
    PORT_NETMSG_GO,         /* host -> guest: both ready, tick 1 starts now (job 2): run number */
    PORT_NETMSG_SIMCFG,     /* either way, just before READY / GO (job 3c): run number + the sim-config values */
    PORT_NETMSG_NEXTSTAGE,  /* host -> guest (job 4): the next mission of the session: run, stage text, difficulty, seed */
    PORT_NETMSG_CMD         /* either way (job 5): a session command (spawn, give) for a given sim tick */
};

/* The ini / environment values the sim reads that no settings block carries, in the fixed order of port_simcfg.c. */
typedef struct PortNetSimCfg
{
    int n;
    int v[PORT_NET_SIMCFG_MAX];
} PortNetSimCfg;

/* Record flag bits (PortNetInput.flags): live-device events that used to be read by the sim straight from this machine. */
#define PORT_NETF_ESC       0x0001u   /* the Escape key was pressed: opens / closes (steps back out of) the watch */
#define PORT_NETF_RETSTART  0x0002u   /* the START bit of this tick's buttons came from the Return key alone (it confirms, it must not open the watch) */
/* Job 4: the analog zoom levels of the local pad (a trigger or stick that zooms by how far it is pushed; a key reads as the full level).
 * 4 bits each, 0 = no analog level (the Port action bit alone decides: full rate), 1..15 = level n/15. */
#define PORT_NETF_ZIN_SHIFT 2
#define PORT_NETF_ZOUT_SHIFT 6
#define PORT_NETF_ZIN(f)    (((f) >> PORT_NETF_ZIN_SHIFT) & 15u)
#define PORT_NETF_ZOUT(f)   (((f) >> PORT_NETF_ZOUT_SHIFT) & 15u)
#define PORT_NETF_ZOOM(zi, zo) ((unsigned short) ((((zi) & 15u) << PORT_NETF_ZIN_SHIFT) | (((zo) & 15u) << PORT_NETF_ZOUT_SHIFT)))
#define PORT_NETF_KNOWN     0x03ffu   /* all flag bits defined; an unknown bit in a received record is a protocol error */

/* One player's input for one tick: everything that affects the sim. */
typedef struct PortNetInput
{
    unsigned short buttons;
    signed char    stickX, stickY;
    unsigned int   actions;      /* Port action bits */
    signed char    digX, digY;   /* digital move */
    unsigned short flags;        /* PORT_NETF_* (was the reserved field) */
    int            yawMilli, pitchMilli;
} PortNetInput;

/* One player's sim-affecting settings. Each machine simulates BOTH Bonds, so each side sends its own block and the
 * sim reads the block of whichever Bond it is ticking (never the local ini). All values are small integers. */
typedef struct PortNetPlayerOpts
{
    unsigned char controlStyle;   /* 0..7 = 1.1 .. 2.4, as the player's pad would ask for it (before the port-mode fold) */
    unsigned char padN64;         /* 1 = N64-class device (the N64 styles mean something), 0 = port-driven */
    unsigned char lookUpright;    /* Rare's "Look up/down" option: 1 = upright, 0 = reverse (invert pitch) */
    unsigned char autoAim;        /* auto-aim option */
    unsigned char lookAhead;      /* look-ahead option */
    unsigned char aimToggle;      /* Rare's Aim control option (hold / toggle) */
    unsigned char aimLock;        /* midas.ini aim_lock (changes the aim and sway in-sim) */
    unsigned char mouseAimOff;    /* 1 = auto-aim is blocked for this player because the mouse aims (portMouseBlocksAutoAim) */
    unsigned short aimSens;       /* midas.ini mouse_sens_aim in thousandths (1000 = 1.0): the in-sim crosshair speed in aim mode */
    unsigned char shootNextPrev;  /* [input] shoot_next_prev: the A+Z chord of the weapon binds (joy_port.c) */
    unsigned char detonateUseReload; /* [input] detonate_use_reload: use + reload detonates remote mines */
} PortNetPlayerOpts;

typedef struct PortNetHello
{
    unsigned short    proto;
    char              build[PORT_NET_STR_BUILD];
    PortNetPlayerOpts opts;                      /* the guest's (player 1) settings */
} PortNetHello;

typedef struct PortNetSetup
{
    unsigned short proto;
    char           build[PORT_NET_STR_BUILD];
    char           stage[PORT_NET_STR_STAGE];   /* the GE_STAGE text */
    int            diff;
    unsigned int   seed;
    char           mods[PORT_NET_STR_MODS];     /* "name=value,..." of every non-default modifier */
    unsigned int   luaHash;
    unsigned char  delay;                       /* input delay in ticks */
    PortNetPlayerOpts hostOpts;                 /* the host's (player 0) settings */
    PortNetPlayerOpts guestOpts;                /* the guest's settings as the host received them: the guest verifies the echo */
    PortNetSimCfg  simCfg;                      /* the host's sim-config values (job 3c): the guest refuses a session where one differs */
} PortNetSetup;

typedef struct PortNetHashMsg
{
    unsigned int tick;                          /* the state hash is of the world at the start of this sim tick */
    unsigned int run;                           /* stage launch of the session, so a late hash of an earlier stage is ignored */
    unsigned int hash;
    int          nfields;                       /* 0..PORT_NET_HASH_FIELDS sub-hashes, named by the game side */
    unsigned int fields[PORT_NET_HASH_FIELDS];
} PortNetHashMsg;

typedef struct PortNetInputPkt
{
    unsigned int  run;           /* stage launch of the session; the receiver drops a packet of another run */
    unsigned int  firstTick;
    unsigned int  ack;           /* highest remote tick received contiguously + 1 */
    unsigned int  cmds;          /* job 5: how many session commands the sender has issued this run (the receiver waits for that many before it passes their ticks) */
    int           count;
    PortNetInput  rec[PORT_NET_MAX_RECS];
} PortNetInputPkt;

/* Each Pack returns the byte count, or -1 if cap is too small or a field is out of range.
 * Each Unpack returns 0 on success, -1 on a short, long or malformed buffer. */
void portNetRecPack(const PortNetInput *in, unsigned char *out);
void portNetRecUnpack(const unsigned char *in, PortNetInput *out);

int portNetMsgType(const unsigned char *buf, int len);   /* the type byte, or -1 */

int portNetHelloPack(const PortNetHello *m, unsigned char *buf, int cap);
int portNetHelloUnpack(const unsigned char *buf, int len, PortNetHello *m);
int portNetSetupPack(const PortNetSetup *m, unsigned char *buf, int cap);
int portNetSetupUnpack(const unsigned char *buf, int len, PortNetSetup *m);
int portNetAcceptPack(unsigned char *buf, int cap);
int portNetReasonPack(int type, const char *reason, unsigned char *buf, int cap);   /* REFUSE */
int portNetReasonUnpack(const unsigned char *buf, int len, char *reason, int reasonCap);
int portNetInputPack(const PortNetInputPkt *m, unsigned char *buf, int cap);
int portNetInputUnpack(const unsigned char *buf, int len, PortNetInputPkt *m);
int portNetHashPack(const PortNetHashMsg *m, unsigned char *buf, int cap);
int portNetHashUnpack(const unsigned char *buf, int len, PortNetHashMsg *m);
void portNetOptsPack(const PortNetPlayerOpts *o, unsigned char *out);       /* PORT_NET_OPTS_SIZE bytes */
void portNetOptsUnpack(const unsigned char *in, PortNetPlayerOpts *o);
int portNetOptsEqual(const PortNetPlayerOpts *a, const PortNetPlayerOpts *b);
unsigned int portNetOptsHash(unsigned int h, const PortNetPlayerOpts *o);   /* FNV of the packed block, for the state hash */
/* READY and GO: type byte + run u32 (which stage launch of the session this is) + the settings blocks as they stand when the
 * stage is loaded (job 3b). READY carries the sender's own block (a); GO carries the host's block (a) and the host's copy of the
 * guest's block (b), which the guest verifies. */
int portNetRunPack(int type, unsigned int run, const PortNetPlayerOpts *a, const PortNetPlayerOpts *b, unsigned char *buf, int cap);
int portNetRunUnpack(const unsigned char *buf, int len, int *type, unsigned int *run, PortNetPlayerOpts *a, PortNetPlayerOpts *b);
/* SIMCFG: type byte + run u32 + count u8 + count x i32 */
int portNetSimCfgPack(unsigned int run, const PortNetSimCfg *c, unsigned char *buf, int cap);
int portNetSimCfgUnpack(const unsigned char *buf, int len, unsigned int *run, PortNetSimCfg *c);

/* Validation of network-supplied values: a difficulty is 0..3; a stage id text is digits only (whole string), at most maxId. 0 = ok. */
int portNetDiffValid(int d);
int portNetParseStageId(const char *s, int maxId, int *id);

/* Session commands (job 5): a game command both machines apply at the same sim tick. Wire: type, run u32, tick u32, seq u32, issuer u8,
 * kind u8, player u8, a i32, b i32 (24 bytes exactly). */
enum {
    PORT_NETCMD_SPAWN_GUARD = 1,   /* a = count 1..40, b = ring distance 100..5000; around the issuer's Bond */
    PORT_NETCMD_SPAWN_CLEAR,       /* a = b = 0 */
    PORT_NETCMD_GIVE_ITEM,         /* player = the Bond, a = ITEM_ id (game checks the table), b = ammo (-1 = the pickup amount, else 0..9999) */
    PORT_NETCMD_GIVE_KEY           /* player = the Bond, a = key filter (-2 keycard, -1 any, else a prop model 0..1023), b = ordinal 1..64 */
};
#define PORT_NETCMD_SIZE 24
#define PORT_NETCMD_MAXITEM 255
typedef struct PortNetCmd
{
    unsigned int run;
    unsigned int tick;       /* the sim tick it is applied at (start of the tick) */
    unsigned int seq;        /* 1.. per issuer and run */
    int          issuer;     /* 0 = host, 1 = guest */
    int          kind;
    int          player;
    int          a, b;
} PortNetCmd;
int portNetCmdPack(const PortNetCmd *c, unsigned char *buf, int cap);
int portNetCmdUnpack(const unsigned char *buf, int len, PortNetCmd *c);   /* 0 = a well-formed, in-range command; -1 otherwise */
int portNetCmdValid(const PortNetCmd *c);                                 /* 0 = every field in range for its kind */
/* A name for matching: lowercase letters and digits only, a leading "item" removed ("ITEM_GOLDENEYEKEY", "goldeneye_key" -> "goldeneyekey"). */
void portNetNameKey(const char *s, char *out, int cap);

/* NEXTSTAGE: type byte + run u32 (the stage launch it is for) + stage text + diff u32 + seed u32 */
int portNetNextStagePack(unsigned int run, const char *stage, int diff, unsigned int seed, unsigned char *buf, int cap);
int portNetNextStageUnpack(const unsigned char *buf, int len, unsigned int *run, char *stage, int stageCap, int *diff, unsigned int *seed);
/* modes-in-co-op job: the same message with the host's game mode for that stage ("" = none) as a trailing text. The mode-less calls above
 * pack an empty mode and ignore it on unpack. The mode text is validated by the unpacker (letters, digits, _ and - only). */
int portNetNextStagePackM(unsigned int run, const char *stage, int diff, unsigned int seed, const char *mode, unsigned char *buf, int cap);
int portNetNextStageUnpackM(const unsigned char *buf, int len, unsigned int *run, char *stage, int stageCap, int *diff, unsigned int *seed, char *mode, int modeCap);
int portNetModeValid(const char *mode);   /* 1 if empty or a legal mode name (<= 31 chars of letters, digits, _ and -) */

/* A record as 40 hex digits (the session log) and back; the hex form is the packed 20-byte record. FromHex returns 0 on
 * success, -1 on a bad digit, a short string or an unknown flag bit. */
void portNetRecToHex(const PortNetInput *in, char *out);   /* PORT_NET_RECHEX chars + a terminator */
int portNetRecFromHex(const char *hex, PortNetInput *out);

/* FNV-1a, for the Lua script set hash. Feed with the previous result as h (start 2166136261u). */
unsigned int portNetFnv(unsigned int h, const void *data, int len);

#endif /* PORT_NETMSG_H */
