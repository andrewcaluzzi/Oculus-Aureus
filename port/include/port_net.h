/* port_net.h - netplay phase 3 (lockstep co-op over UDP). Off unless GE_NET is set: then no socket and no
 * ENet init. */
#ifndef PORT_NET_H
#define PORT_NET_H

#include "port_netmsg.h"

/* First thing in main(), before the ini and the tick rate are read: sets the GE_COOP / GE_FIXEDTICK /
 * GE_TICKHZ / GE_SKIPINTRO / GE_COOP_VIEW environment netplay forces. A no-op when GE_NET is unset. */
void portNetPreinit(void);

/* GE_NET=host:<port> | join:<ip>:<port>. Called once from main() after the ini is read and before the
 * game starts: opens the socket (does not wait). The handshake then runs without blocking, polled by
 * portNetHold. A no-op when GE_NET is unset. */
void portNetBoot(void);

/* Once per sim tick (kept for the hook in input_pc.c; the link is serviced by portNetHold). */
void portNetTick(void);

int portNetActive(void);   /* 1 once the handshake succeeded */

/* boss.c, in the retrace branch every vsync: services the link, runs the handshake and the READY/GO
 * exchange, and returns 1 when no sim tick may start now (handshake or partner not there yet, or the
 * partner's input for the next tick has not arrived). Shows "Waiting for partner..." in the window
 * title. Returns 0 at once when netplay is off. */
int portNetHold(void);

/* port_coop.c, at every co-op stage launch (before the stage loads): bumps the run number, resets the
 * lockstep state and reseeds the sim RNG with the session seed. `stage` is the LEVELID being loaded: from the second stage of a
 * session on, the host tells the guest (NEXTSTAGE: stage, difficulty, a new seed) and the guest loads the same one (job 4). */
void portNetStageBegin(int stage);

/* 1 on the guest while the session lasts: its mission select is read-only ("Host chooses the mission", front.c). */
int portNetMissionLocked(void);
/* The console's `net [info]`: role, link state, delay, ping, run, last hash tick, desync count. */
void portNetConsole(const char *arg1);

/* 1 while lockstep drives the sim input (after GO, until the partner is lost or the stage ends). */
int portNetLockActive(void);
int portNetDelay(void);

/* port_coop.c, at the start of sim tick `tick` (numbered from 1): the scheduled local and remote records
 * for that tick. Returns 0 if they are missing (cannot happen behind portNetHold). */
int portNetTickInputs(unsigned int tick, PortNetInput *local, PortNetInput *remote);

/* ---- per-player sim-affecting settings (job 3) ----
 * Every machine simulates both Bonds, so each Bond's settings must be known to both. Each side sends its block in the
 * handshake (HELLO: the guest's, SETUP: the host's plus an echo of the guest's, verified). While netplay is on the sim
 * reads the block of the Bond it ticks, never the local ini. Game code (options.c, bondview2.c, ...) asks through
 * portNetOpt / portSimPadIsN64 with the `which` numbers below. */
enum {
    PORT_NETOPT_STYLE = 0,     /* the control style the Bond plays (0..7 = 1.1 .. 2.4), already folded for port-driven pads */
    PORT_NETOPT_PADN64,        /* 1 = N64-class input mode, 0 = port-driven */
    PORT_NETOPT_LOOK,          /* Look up/down option value: 1 = upright, 0 = reverse (invert pitch) */
    PORT_NETOPT_AUTOAIM,
    PORT_NETOPT_LOOKAHEAD,
    PORT_NETOPT_AIMCTL,        /* Aim control option: hold / toggle */
    PORT_NETOPT_AIMLOCK,       /* midas.ini aim_lock */
    PORT_NETOPT_MOUSEAIMOFF,   /* 1 = auto-aim is blocked because this player aims with the mouse */
    PORT_NETOPT_AIMSENS,       /* mouse_sens_aim in thousandths (the aim-mode crosshair speed) */
    PORT_NETOPT_SHOOTNEXTPREV, /* [input] shoot_next_prev (joy_port.c: the A+Z chord of the weapon binds) */
    PORT_NETOPT_DETONATEUR     /* [input] detonate_use_reload (joy_port.c) */
};

/* The value of one setting for player `player` (0 host, 1 guest), or -1 when it does not apply (netplay off, the
 * handshake not done, no co-op stage, or GE_NET_OPTS_OFF=1 for the negative test): the caller then uses its normal
 * local path. */
int portNetOpt(int player, int which);
/* portInputPadIsN64 for the sim: the player's block under netplay, else the device answer for pad slot `player`. */
int portSimPadIsN64(int player);
/* FNV over both players' blocks, for the state hash. */
unsigned int portNetSettingsHash(unsigned int h);

/* port_coop.c, every 30th sim tick (before the sim tick runs): hash the world, send it, compare with the partner's. */
void portNetStateCheck(unsigned int tick);
/* GE_NET_TEST_DESYNC=<tick> (test): 1 when the tick-`tick` perturbation is due on this side. */
int portNetTestDesync(unsigned int tick);
int portNetTestExtraDraw(unsigned int tick);   /* GE_NET_TEST_EXTRADRAW=<tick> (job 3c): this side makes one extra sim RNG draw at that tick */

/* port_coop.c, at the start of the next tick: the finished local sample, scheduled `delay` ticks ahead
 * of the tick it was taken in, and sent. */
void portNetPushLocal(const PortNetInput *in);

/* ---- session log and offline replay (job 3b) ----
 * GE_NET_LOG=1 writes Data/Logs/netlog_run<N>_<host|guest>.txt (under the profile): the setup, both players' records for every
 * tick, and this side's per-tick state fingerprint. GE_NET_REPLAY=<file> [GE_NET_REPLAY_VIEW=0|1] runs that log offline as
 * local co-op (no socket) and compares the fingerprints and state hashes with the log's. */
int portNetReplaying(void);   /* 1 while GE_NET_REPLAY drives the sim */
int portNetLogging(void);     /* 1 while GE_NET_LOG writes a log */
/* port_coop.c, at the start of every lockstep / replay sim tick (after the state-hash check): fingerprint of the world, the
 * comparison with the log when replaying. Returns 0 normally; a replay that has run out of log exits the process here. */
void portNetSessionTick(unsigned int tick);
/* port_coop.c, once the tick's two records are known (index = player): writes the log line (live) */
void portNetSessionRecords(unsigned int tick, const PortNetInput *rec0, const PortNetInput *rec1);
/* A live-device action the sim must not take while netplay is on (the Tab classic-mode toggle, console commands that change the
 * sim): returns 1 and logs `ge: net: <what> is blocked in netplay` (once per distinct text) when netplay or a replay is on. */
int portNetBlocked(const char *what);

/* ---- session commands (job 5) ---- */
int portNetCmdLive(void);    /* 1 in a live lockstep stage: commands can be issued */
int portNetCmdSelf(void);    /* this machine's Bond index (0 host, 1 guest), -1 when netplay is off */
int portNetCmdIssue(int kind, int player, int a, int b, char *why, int whycap);   /* 0, or -1 with the reason in why */
void portNetSessionCmds(unsigned int tick);   /* boss.c via portCoopDeathTick, at the start of every lockstep / replay tick */

#endif /* PORT_NET_H */
