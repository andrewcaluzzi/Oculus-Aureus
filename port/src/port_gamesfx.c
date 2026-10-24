/* port_gamesfx.c - the game's own sound effects played from Lua (sound.play_sfx, sound.sfx_burst: the arena's scream crowd).
 *
 * A play goes through snd.c portSndPlayPresent, which is sndPlaySfx with the new states marked portPresent: their level, pitch
 * and pan are set before the PLAY event reads them, their synth priority is 1 (they steal no voice from a game sound, a game
 * sound can steal theirs), and when the player is full (maxSounds, 8 x GE_SNDSCALE) a presentation sound gives up instead of
 * preempting, while a game sound preempts a presentation one first. So the crowd never cuts gunfire. On top of that this side
 * keeps at most maxSounds presentation sounds alive (counting the ones still before their PLAY event: sndPlaySfx posts it
 * keymap->velocityMax x 33 ms late) and drops the rest.
 *
 * Plays may be delayed (a burst is staggered 30..150 ms): the queue is drained by the audio pump (audio_pc.c, before each mixer
 * frame) on its own sample clock, so the timing is the audio's, not the sim tick's (the mode ticks 4 times a second).
 *
 * Presentation only: nothing here reads or writes sim state, the burst picks (names, pitch, pan, gaps) use their own RNG, and
 * nothing it returns may steer the game. Main thread only (Lua and the audio pump both run there).
 * GE_SOUNDLOG=1 logs every start and drop. */
#ifndef TARGET_N64

#include <stdlib.h>
#include <ultra64.h>
#include <PR/libaudio.h>
#include <bondconstants.h>
#include "snd.h"

extern void portConLogf(const char *fmt, ...);   /* port_console.c */
extern unsigned long long portProfNow(void);     /* pcsched.c */
extern ALSndPlayer *g_sndPlayerPtr;              /* snd.c */
extern s16 g_sndAllocatedVoicesCount;            /* snd.c */
extern void portSndPresentStats(s32 *gaveUp, s32 *yielded, s32 *gamePreempts);   /* snd.c */

#define GS_RATE   22050   /* audio_pc.c PORT_AUDIO_RATE */
#define GS_SLOTS  64
#define GS_COUNT  (BIG_CLANK_SFX + 1)

typedef char gsCheck[(GET_HIT_MALE0_SFX == 134 && EVIL_LAUGH_SFX == 54 && CRUSHED_YELL_SFX == 183 && BIG_CLANK_SFX == 261) ? 1 : -1];

/* SFX_ID names without _SFX, indexed by the enum itself (so a name can never sit on the wrong id); the DEBUG-only
 * SFX_ID_ToString in bondconstants.h numbers them the same way */
#define N(x) [x##_SFX] = #x
static const char *const sName[GS_COUNT] = {
    N(NOTHING), N(ROCKET_LAUNCH), N(GLASS_SHATTERING), N(KNIFE_HIT_WALL), N(GRENADE_THROW), N(GRENADE_THROW_QUIET),
    N(GRENADE_THROW_FAINT), N(TRAIN_SLIDE_DOOR_SLIDE), N(TRAIN_RAILS), N(TRAIN_RAILS2), N(WATCH_INTERFERENCE), N(GUN_TANK2BIG_1),
    N(GUN_TANK2BIGBIG_1), N(GET_HIT_GIRL1), N(GET_HIT_GIRL2), N(GET_HIT_GIRL3), N(BEEP), N(BEEP_QUIET), N(OPTION_CLICK2),
    N(RICO_12_GBU_A), N(RICO_12_GBU_B), N(RICO_12_GBU_C), N(RICO_12_GBU_D), N(RICO_6_TAJ_A), N(RICO_6_TAJ_B), N(RICO_6_TAJ_C),
    N(RICO_6_TAJ_D), N(RICO_8_AFDM_A), N(RICO_8_AFDM_B), N(RICO_8_AFDM_C), N(RICO_8_AFDM_D), N(RICO_4_A), N(RICO_4_B), N(RICO_4_C),
    N(RICO_4_D), N(RICO_5_A), N(RICO_5_B), N(RICO_5_C), N(RICO_5_D), N(RICO_6_HBBA_A), N(RICO_6_HBBA_B), N(RICO_6_HBBA_C),
    N(RICO_6_HBBA_D), N(OPTION_CHOOSE), N(UNKNOWN1), N(DROP_GUN), N(GUN_SILPPK_A), N(PUNCH1), N(PUNCH2), N(PUNCH3),
    N(GUN_RIFLECOCK), N(TRAIN_CLUTTER3), N(TRAIN_CLUTTER3B), N(TRAIN_CLUTTER3C), N(EVIL_LAUGH), N(EVIL_LAUGH_QUIET),
    N(EVIL_LAUGH_FAINT), N(EVIL_LAUGH_HUSH), N(HELI_RUN), N(HELI_FLY), N(ENGINE_ROOM), N(TRAIN_STOP_STILL), N(TANK), N(TRAIN_STOP),
    N(TRAIN_GO), N(TRUCK_RUN), N(TRUCK_START), N(TRUCK_ENGINE), N(BOND_GET_HIT1), N(HIT_BULLET_FLESH), N(HIT_BULLET_GLASS),
    N(HIT_GLASS_SMASH), N(HIT_BULLET_METAL_A), N(HIT_BULLET_METAL_B), N(HIT_BULLET_SNOW), N(HIT_BULLET_WOOD), N(HIT_BULLET_WATER),
    N(PAPER_TURN), N(PAPER_TURN_2), N(COPY_FILE), N(RADIO_MESSAGE), N(ARMOUR_COLLECT), N(OBJ_REGEN), N(DOOR_DECODER),
    N(GIRL_GET_HIT1), N(CONSOLE_OFF), N(CONSOLE_OFF2), N(CONSOLE_ON2), N(CONSOLE_ON3), N(EMPTY_GUN_FIRE), N(SHELL_CASE),
    N(RICO_LASER1), N(RICO_LASER2), N(RICO_LASER3), N(RADIO), N(KNIFE_THROW1), N(KNIFE_THROW2), N(KNIFE_THROW3), N(COUGH),
    N(COUGH2), N(GUN_TASER), N(GUN_TASER_LOOP), N(GAS_HISS), N(UNKNOWN2), N(UNKNOWN3), N(PUNCHING_AIR), N(GUN_B1_MGUN3_3),
    N(GUN_B2_HEAVY), N(GUN_UNKNOWN), N(GUN_B4_BOLTACTION), N(GUN_B5_WINC44), N(GUN_RIFLE7BIG_1), N(GUN_B8_ANOTHER),
    N(GUN_B9_CANNON), N(GUN_GRENADE_LAUNCHER), N(GUN_UNKNOWN2), N(GUN_B12_FULLAMRIFLE), N(GUN_B13_M60AMMGUN), N(GUN_M60AMMGUN_3),
    N(GUN_UNKNOWN3), N(HIT_METAL_OBJECT1), N(GUN_B17_RIFLE), N(CART_SPENT), N(BODY_FALL_C1), N(BODY_FALL_C2), N(BODY_FALL_C3),
    N(BODY_FALL_D1), N(BODY_FALL_D2), N(BODY_FALL_D3), N(BODY_FALL_D4), N(BODY_FALL_E1), N(BODY_FALL_E2), N(BODY_FALL_E3),
    N(BODY_ROLLOVER), N(GET_HIT_MALE0), N(GET_HIT_MALE1), N(GET_HIT_MALE2), N(GET_HIT_MALE3), N(GET_HIT_MALE4), N(GET_HIT_MALE5),
    N(GET_HIT_MALE6), N(GET_HIT_MALE7), N(GET_HIT_MALE8), N(GET_HIT_MALE9), N(GET_HIT_MALE10), N(GET_HIT_MALE11),
    N(GET_HIT_MALE12), N(GET_HIT_MALE13), N(GET_HIT_MALE14), N(GET_HIT_MALE15), N(GET_HIT_MALE16), N(GET_HIT_MALE17),
    N(GET_HIT_MALE18), N(GET_HIT_MALE19), N(GET_HIT_MALE20), N(GET_HIT_MALE21), N(GET_HIT_MALE22), N(GET_HIT_MALE23),
    N(GET_HIT_MALE24), N(CAMERA_BEEP1), N(BING), N(ALARM1), N(ALARM2), N(ALARM3), N(RICO_EAR_WHISTLE1), N(RICO_EAR_WHISTLE2),
    N(RICO_EAR_WHISTLE3), N(RICO_EAR_WHISTLE4), N(RICO_EAR_WHISTLE5), N(EXPLOSION_2A), N(EXPLOSION_2B), N(EXPLOSION_3),
    N(EXPLOSION_4A), N(EXPLOSION_4B), N(EXPLOSION_5A), N(EXPLOSION_5B), N(EXPLOSION_5C), N(EXPLOSION_6), N(EXPLOSION_7),
    N(EXPLOSION_8), N(EXPLOSION_9), N(EXPLOSION_1B), N(EXPLOSION_1C), N(CRUSHED_YELL), N(KEY_SEQUENCE), N(HIT_BULLET_DIRT2),
    N(ALARM_SWITCH), N(DOOR_WOOD_CLOSE), N(DOOR_WOOD_OPEN), N(ATOMIC_BOMB), N(KEY_ANALYSER2), N(DOOR_WOOD_SLIDE),
    N(TRAIN_SLIDE_DOOR_CATCH), N(GAS_LEAK), N(DOOR_SHUTTER_OPEN), N(DOOR_SHUTTER_CLOSE), N(DOOR_METAL_OPEN), N(DOOR_METAL_CLOSE),
    N(CONSOLE_ON), N(DOOR_METAL_CLOSE2), N(DOOR_METAL_OPEN3), N(DOOR_METAL_CLOSE3), N(METAL_SLIDE_OPEN), N(METAL_SLIDE_CLOSE),
    N(METAL_SLIDE_LOOP), N(UNKNOWN4), N(UNKNOWN5), N(UNKNOWN6), N(HIT_BULLET_STONE1), N(HIT_BULLET_STONE2), N(DOOR_SMART_CATCH1),
    N(DOOR_SMART_SLIDE1), N(HIT_BULLET_TILE), N(TANK_CRUSH_MAN), N(HEAVY_SLIDE_OPEN), N(HEAVY_SLIDE_CLOSE), N(HEAVY_SINGLE_LOOP),
    N(HIT_BULLET_WOOD2), N(DOOR_HYDRAL_CLOSE), N(DOOR_HYDRAL_OPEN), N(HIT_BULLET_DIRT1), N(HIT_METAL_OBJECT2), N(DOOR_LOCK),
    N(TRAIN_CLUTTER2), N(HIT_BULLET_MUD3), N(DOOR_SLIDE_STONE_OPEN), N(DOOR_SLIDE_STONE_CLOSE), N(DATA_DOWNLOAD), N(LASER_GUN),
    N(KEYCARD), N(HIT_BULLET_MUD2), N(HIT_BULLET_MUD1), N(PICKUP_GUN), N(PICKUP_KNIFE), N(PICKUP_AMMO), N(PICKUP_MINE),
    N(WATCH_STATIC), N(WATCH_ON), N(WATCH_OFF), N(HIT_BULLET_METAL_A3), N(HIT_BULLET_METAL_A4), N(ATTACH_MINE), N(PICKUP_LASER),
    N(WATCH_DETONATE_MINE), N(CAMERA_CLICK), N(KEY_ANALYSER), N(MAGNETIC_HUM), N(BOMB_DEFUSE), N(BI_PLANE), N(TRAIN_CLUTTER),
    N(TRAIN_CLUTTERB), N(TRAIN_CLUTTERC), N(TRAIN_CLUTTERD), N(GUN_B9_CANNON_SHORT), N(UNKNOWN_QUIET), N(CAMERA_ZOOM_LOOP),
    N(CAMERA_ZOOM_STOP), N(SNEEZE), N(RARELOGO), N(RARELOGO_QUIET), N(RARELOGO_FAINT), N(BIG_CLANK),
};
#undef N

typedef struct
{
    s32 id;               /* handle; 0 = free */
    s32 queued;           /* 1 = waiting for its time, 0 = started (owner reads its state until it ends) */
    s16 sfx;
    u32 due;              /* sample clock */
    f32 vol, pitch, pan;
    ALSoundState *owner;  /* snd.c clears it when the sound ends */
} GsSlot;

static GsSlot sS[GS_SLOTS];
static u32 sClock;        /* samples rendered by the audio pump */
static s32 sNextId = 1;
static u32 sRng;
static s32 sLog = -1;
static u32 sStarted, sDropped;

static s32 logOn(void)
{
    if (sLog < 0)
    {
        const char *e = getenv("GE_SOUNDLOG");

        sLog = (e != NULL && e[0] != '\0' && e[0] != '0') ? 1 : 0;
    }
    return sLog;
}

/* presentation RNG (xorshift), never the sim's */
static u32 rnd(void)
{
    if (sRng == 0)
    {
        sRng = (u32) portProfNow() ^ 0x9e3779b9u;
        if (sRng == 0)
        {
            sRng = 0x2545f491u;
        }
    }
    sRng ^= sRng << 13;
    sRng ^= sRng >> 17;
    sRng ^= sRng << 5;
    return sRng;
}

static f32 frand(f32 lo, f32 hi)
{
    return lo + (hi - lo) * (f32) (rnd() & 0xffffff) / (f32) 0x1000000;
}

static s32 upper(s32 c)
{
    return (c >= 'a' && c <= 'z') ? c - 'a' + 'A' : c;
}

/* "get_hit_male3", "GET_HIT_MALE3_SFX" -> GET_HIT_MALE3_SFX; 0 when unknown */
int portGameSfxId(const char *name)
{
    s32 i, k, n;

    if (name == NULL)
    {
        return 0;
    }
    for (n = 0; name[n] != '\0'; n++)
    {
    }
    if (n > 4 && upper(name[n - 4]) == '_' && upper(name[n - 3]) == 'S' && upper(name[n - 2]) == 'F' && upper(name[n - 1]) == 'X')
    {
        n -= 4;
    }
    for (i = 1; i < GS_COUNT; i++)
    {
        const char *s = sName[i];

        if (s == NULL)
        {
            continue;
        }
        for (k = 0; k < n && s[k] != '\0' && upper(name[k]) == s[k]; k++)
        {
        }
        if (k == n && s[k] == '\0')
        {
            return i;
        }
    }
    return 0;
}

const char *portGameSfxName(int id)
{
    return (id > 0 && id < GS_COUNT) ? sName[id] : NULL;
}

int portGameSfxCount(void)
{
    return GS_COUNT;
}

/* started sounds still playing; frees the slots of the ended ones */
static s32 alive(void)
{
    s32 i, n = 0;

    for (i = 0; i < GS_SLOTS; i++)
    {
        if (sS[i].id != 0 && !sS[i].queued)
        {
            if (sS[i].owner != NULL)
            {
                n++;
            }
            else
            {
                sS[i].id = 0;
            }
        }
    }
    return n;
}

/* log: the started ones by state (voiced = has a synth voice, waiting = for a voice, other = before its PLAY event or ending) */
static void census(s32 *voiced, s32 *waiting, s32 *other)
{
    s32 i;

    *voiced = *waiting = *other = 0;
    for (i = 0; i < GS_SLOTS; i++)
    {
        ALSoundState *st = sS[i].id != 0 && !sS[i].queued ? sS[i].owner : NULL;

        if (st == NULL) continue;
        if (st->unk3e & 4) (*voiced)++;
        else if (st->playingState == SOUND_STATE_WAIT_VOICE) (*waiting)++;
        else (*other)++;
    }
}

static s32 aliveCap(void)
{
    s32 m = g_sndPlayerPtr != NULL ? (s32) g_sndPlayerPtr->maxSounds : 8;

    return m < 2 ? 2 : m;
}

static s32 start(GsSlot *s)
{
    s32 n = alive(), cap = aliveCap();

    if (n >= cap || portSndPlayPresent(s->sfx, s->vol, s->pitch, s->pan, &s->owner) == NULL)
    {
        sDropped++;
        if (logOn())
        {
            portConLogf("ge: sfx: dropped %s (%d): %s (%d of %d presentation sounds, game voices %d of %d), %u dropped so far\n",
                        sName[s->sfx], s->sfx, n >= cap ? "the presentation share is full" : "no sound state free",
                        n, cap, (s32) g_sndAllocatedVoicesCount, g_sndPlayerPtr ? (s32) g_sndPlayerPtr->maxSounds : 0, sDropped);
        }
        s->id = 0;
        return 0;
    }
    s->queued = 0;
    sStarted++;
    if (logOn())
    {
        s32 cv, cw, co;

        census(&cv, &cw, &co);
        portConLogf("ge: sfx: play %s (%d) as %d: vol %.2f pitch %.2f pan %+.2f; %d of %d presentation sounds (%d voiced, %d waiting"
                    " for a voice, %d starting or ending), player voices %d of %d\n",
                    sName[s->sfx], s->sfx, s->id, (double) s->vol, (double) s->pitch, (double) s->pan, n + 1, cap, cv, cw, co,
                    (s32) g_sndAllocatedVoicesCount, g_sndPlayerPtr ? (s32) g_sndPlayerPtr->maxSounds : 0);
    }
    return s->id;
}

/* a sound now (delay <= 0) or after `delay` seconds of audio; returns a handle (0 = dropped or no slot) */
int portGameSfxPlay(int sfx, float vol, float pitch, float delay, float pan)
{
    GsSlot *s = NULL;
    s32 i;

    if (sfx <= 0 || sfx >= GS_COUNT || !(vol > 0.0f))
    {
        return 0;
    }
    alive();   /* frees the ended ones */
    for (i = 0; i < GS_SLOTS && s == NULL; i++)
    {
        if (sS[i].id == 0)
        {
            s = &sS[i];
        }
    }
    if (s == NULL)
    {
        sDropped++;
        return 0;
    }
    s->id = sNextId++;
    if (sNextId <= 0)
    {
        sNextId = 1;
    }
    s->sfx = (s16) sfx;
    s->vol = vol;
    s->pitch = pitch > 0.0f ? pitch : 1.0f;
    s->pan = pan;
    s->owner = NULL;
    s->queued = 1;   /* until start() has its state (alive() frees started slots without one) */
    if (!(delay > 0.0f))
    {
        return start(s);
    }
    if (delay > 10.0f)
    {
        delay = 10.0f;
    }
    s->due = sClock + (u32) (delay * GS_RATE);
    return s->id;
}

/* `count` (lo..hi, picked here) of the sounds in ids[0..n), each once before any repeats, in a random order, staggered by
 * gaps of glo..ghi seconds from `delay`, each at a random pitch plo..phi and a random pan inside +-panSpread; returns how many
 * were queued or started */
int portGameSfxBurst(const int *ids, int n, int lo, int hi, float vol, float plo, float phi, float glo, float ghi, float delay,
                     float panSpread)
{
    int order[GS_COUNT];
    int i, k, m = 0, count, done = 0;
    float t = delay > 0.0f ? delay : 0.0f;

    for (i = 0; i < n && m < GS_COUNT; i++)
    {
        if (ids[i] > 0 && ids[i] < GS_COUNT)
        {
            order[m++] = ids[i];
        }
    }
    if (m == 0 || hi < 1)
    {
        return 0;
    }
    if (lo < 1)
    {
        lo = 1;
    }
    if (hi < lo)
    {
        hi = lo;
    }
    count = lo + (int) (rnd() % (u32) (hi - lo + 1));
    if (count > GS_SLOTS)
    {
        count = GS_SLOTS;
    }
    for (k = 0; k < count; k++)
    {
        int at = k % m;

        if (at == 0)
        {   /* a fresh shuffle each pass through the list */
            for (i = m - 1; i > 0; i--)
            {
                int j = (int) (rnd() % (u32) (i + 1)), x = order[i];

                order[i] = order[j];
                order[j] = x;
            }
        }
        if (k > 0)
        {
            t += frand(glo, ghi);
        }
        if (portGameSfxPlay(order[at], vol, frand(plo, phi), t, frand(-panSpread, panSpread)) != 0)
        {
            done++;
        }
    }
    return done;
}

void portGameSfxStop(int id)
{
    s32 i;

    for (i = 0; i < GS_SLOTS; i++)
    {
        if (sS[i].id == id && id != 0)
        {
            if (sS[i].queued)
            {
                sS[i].id = 0;
            }
            else if (sS[i].owner != NULL)
            {
                sndDeactivate(sS[i].owner);
            }
        }
    }
}

/* playing (started and not ended) and queued presentation sounds */
int portGameSfxPlaying(int *queued)
{
    s32 i, q = 0;

    for (i = 0; i < GS_SLOTS; i++)
    {
        if (sS[i].id != 0 && sS[i].queued)
        {
            q++;
        }
    }
    if (queued != NULL)
    {
        *queued = q;
    }
    return alive();
}

/* audio_pc.c portAudioPump, before each mixer frame of `samples`: start what is due, then advance the clock */
void portGameSfxPump(u32 samples)
{
    s32 i, any = 1;

    while (any)
    {   /* earliest first, so a full share drops the latest of a burst */
        GsSlot *first = NULL;

        any = 0;
        for (i = 0; i < GS_SLOTS; i++)
        {
            if (sS[i].id != 0 && sS[i].queued && (s32) (sS[i].due - sClock) <= 0
                && (first == NULL || (s32) (sS[i].due - first->due) < 0))
            {
                first = &sS[i];
            }
        }
        if (first != NULL)
        {
            start(first);
            any = 1;
        }
    }
    sClock += samples;
    if (logOn() && sStarted != 0)
    {   /* what the game's sound player did about a full house, when it changes */
        static s32 last[3] = { -1, -1, -1 };
        s32 st[3];

        portSndPresentStats(&st[0], &st[1], &st[2]);
        if (st[0] != last[0] || st[1] != last[1] || st[2] != last[2])
        {
            last[0] = st[0];
            last[1] = st[1];
            last[2] = st[2];
            portConLogf("ge: sfx: player full: %d presentation sounds gave up, %d made way for a game sound; %d game sounds "
                        "preempted another game sound (stock); %u started, %u dropped here\n", st[0], st[1], st[2], sStarted, sDropped);
        }
    }
}

#endif /* TARGET_N64 */
