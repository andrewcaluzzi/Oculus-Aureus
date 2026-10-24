/* port_soundfile.c - WAV files from Data/Sounds/ played on top of the N64 synth (the arena crowd's cheers and boos; Lua `sound`).
 *
 * The port's output is the N64 synthesizer, rendered a frame at a time by audio_pc.c (portAudioPump) as 16-bit stereo at
 * PORT_AUDIO_RATE and queued to SDL. This is a small PCM mixer beside it: a file is loaded once with SDL_LoadWAV (any rate,
 * channel count or sample format SDL reads), converted to that output format with an SDL_AudioStream and kept in a cache;
 * a play takes one of SF_VOICES voices, and portSoundFileMix adds the playing voices into each rendered frame, before the
 * unfocused-window mute, so they follow every output path the synth does.
 *
 * Levels: each voice's volume x the game's sound effects volume (g_sndSfxSlotVolume[0], which also carries the pause fades), x a
 * crowd master that falls as 1/sqrt(voices playing), with a per-sample ramp so nothing clicks. A play may duck the voices already
 * playing (they drop to `duck` for SF_DUCK_SECS, then come back). At most SF_PLAY_MAX voices are heard: a new one fades the oldest
 * out (the slots beyond hold the fading ones). The sum is clamped to 16 bits.
 *
 * Presentation only: nothing here reads or writes sim state, and the variant choice uses its own RNG. Threads: plays come from the
 * Lua side and the mix from the audio pump; a spinlock covers the voices. Loading (disk, conversion) happens outside the lock.
 * GE_SOUNDLOG=1 logs every start and end, and the voices' positions once a second. */
#ifndef TARGET_N64

#include <SDL.h>   /* and SDL's C library throughout: the game's include/ shadows libc's string.h */

#include <ultra64.h>

#define SF_RATE         22050        /* audio_pc.c PORT_AUDIO_RATE */
#define SF_FILES        128          /* cached names, found or not */
#define SF_NAME         48
#define SF_MAX_SECS     60           /* longer files are cut */
#define SF_CACHE_BYTES  (96u << 20)  /* converted PCM kept, all files */
#define SF_VOICES       12
#define SF_PLAY_MAX     6            /* heard at once; more fade the oldest out */
#define SF_VARIANTS     16
#define SF_FADE_IN      64           /* samples */
#define SF_STEAL_SECS   0.08f
#define SF_DUCK_SECS    1.5f
#define SF_RAMP         (1.0f / 2048.0f)   /* gain change per sample at most (~0.1 s full scale) */

extern const char *portRootPath(const char *name);   /* main.c */
extern void portConLogf(const char *fmt, ...);        /* port_console.c */
extern s16 *g_sndSfxSlotVolume;                       /* music.c / snd.c: the game's sfx volume, 0..0x7FFF */

typedef struct
{
    char name[SF_NAME];
    s16 *pcm;        /* stereo frames */
    u32 frames;
    u8 state;        /* 0 unused, 1 loaded, 2 missing or unreadable */
} SfFile;

typedef struct
{
    SfFile *f;       /* NULL = free */
    u32 pos;
    s32 id;
    f32 vol;         /* the play's volume */
    f32 duck;        /* current duck factor, ramps toward duckTo */
    f32 duckTo;
    u32 duckLeft;    /* samples the duck holds */
    f32 fade;        /* 1 playing, ramps to 0 when stopping */
    f32 fadeStep;    /* > 0 while stopping */
    u32 age;         /* play order */
} SfVoice;

static SfFile sFiles[SF_FILES];
static s32 sNFiles;
static u32 sCacheBytes;
static SfVoice sV[SF_VOICES];
static SDL_SpinLock sLock;
static s32 sNextId = 1;
static u32 sAge;
static f32 sMaster = 1.0f;       /* the 1/sqrt(n) master, ramped */
static u32 sRng = 0x2545f491u;   /* presentation RNG (xorshift), never the sim's */
static s32 sLog = -1;
static u32 sLogFrames;

static u32 rnd(void)
{
    sRng ^= sRng << 13;
    sRng ^= sRng >> 17;
    sRng ^= sRng << 5;
    return sRng;
}

static s32 logOn(void)
{
    if (sLog < 0)
    {
        const char *e = SDL_getenv("GE_SOUNDLOG");

        sLog = (e != NULL && e[0] != '\0' && e[0] != '0') ? 1 : 0;
        sRng ^= (u32)SDL_GetPerformanceCounter();
        if (sRng == 0)
        {
            sRng = 0x2545f491u;
        }
    }
    return sLog;
}

/* "cheer1" or "cheer1.wav" -> "cheer1.wav"; letters, digits, _ - . only (no paths) */
static s32 cleanName(const char *in, char *out)
{
    size_t n = SDL_strlen(in), i;

    if (n == 0 || n + 5 > SF_NAME || in[0] == '.')
    {
        return 0;
    }
    for (i = 0; i < n; i++)
    {
        char c = in[i];

        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.'))
        {
            return 0;
        }
    }
    SDL_memcpy(out, in, n + 1);
    if (n < 4 || SDL_strcasecmp(out + n - 4, ".wav") != 0)
    {
        SDL_memcpy(out + n, ".wav", 5);
    }
    return 1;
}

/* the file converted to SF_RATE 16-bit stereo; NULL when missing or unreadable */
static s16 *loadPcm(const char *file, u32 *frames)
{
    char rel[SF_NAME + 16];
    SDL_AudioSpec spec;
    Uint8 *buf = NULL;
    Uint32 len = 0;
    SDL_AudioStream *st;
    s16 *pcm = NULL;
    s32 got, cap;

    SDL_snprintf(rel, sizeof(rel), "Data/Sounds/%s", file);
    if (SDL_LoadWAV(portRootPath(rel), &spec, &buf, &len) == NULL)
    {
        return NULL;
    }
    st = SDL_NewAudioStream(spec.format, spec.channels, spec.freq, AUDIO_S16SYS, 2, SF_RATE);
    if (st != NULL && SDL_AudioStreamPut(st, buf, (int)len) == 0 && SDL_AudioStreamFlush(st) == 0)
    {
        cap = SDL_AudioStreamAvailable(st);
        if (cap > SF_MAX_SECS * SF_RATE * 4)
        {
            cap = SF_MAX_SECS * SF_RATE * 4;
        }
        cap &= ~3;
        if (cap > 0 && sCacheBytes + (u32)cap <= SF_CACHE_BYTES && (pcm = SDL_malloc((size_t)cap)) != NULL)
        {
            got = SDL_AudioStreamGet(st, pcm, cap);
            if (got <= 0)
            {
                SDL_free(pcm);
                pcm = NULL;
            }
            else
            {
                *frames = (u32)got / 4;
                sCacheBytes += (u32)got;
            }
        }
    }
    if (st != NULL)
    {
        SDL_FreeAudioStream(st);
    }
    SDL_FreeWAV(buf);
    return pcm;
}

/* the cache entry for a name, loading it the first time; NULL for a bad name or a full cache */
static SfFile *getFile(const char *name)
{
    char file[SF_NAME];
    s32 i;
    SfFile *f;

    if (name == NULL || !cleanName(name, file))
    {
        return NULL;
    }
    for (i = 0; i < sNFiles; i++)
    {
        if (SDL_strcasecmp(sFiles[i].name, file) == 0)
        {
            return &sFiles[i];
        }
    }
    if (sNFiles >= SF_FILES)
    {
        return NULL;
    }
    f = &sFiles[sNFiles];
    SDL_memcpy(f->name, file, sizeof(f->name));
    f->pcm = loadPcm(file, &f->frames);
    f->state = f->pcm != NULL ? 1 : 2;
    sNFiles++;
    if (f->state == 1)
    {
        portConLogf("ge: sound: loaded Data/Sounds/%s (%.2f s)\n", file, (double)f->frames / SF_RATE);
    }
    else if (logOn())
    {
        portConLogf("ge: sound: Data/Sounds/%s missing or unreadable, skipped\n", file);
    }
    return f;
}

int portSoundFileExists(const char *name)
{
    SfFile *f = getFile(name);

    logOn();
    return f != NULL && f->state == 1;
}

static s32 playingCount(void)
{
    s32 i, n = 0;

    for (i = 0; i < SF_VOICES; i++)
    {
        if (sV[i].f != NULL && sV[i].fadeStep == 0.0f)
        {
            n++;
        }
    }
    return n;
}

int portSoundFilePlay(const char *name, float vol, float duck)
{
    SfFile *f;
    SfVoice *v = NULL;
    s32 i, id, n;

    logOn();
    f = getFile(name);
    if (f == NULL || f->state != 1 || !(vol > 0.0f))
    {
        return 0;
    }
    if (vol > 2.0f)
    {
        vol = 2.0f;
    }
    if (!(duck >= 0.0f))
    {
        duck = 1.0f;
    }
    SDL_AtomicLock(&sLock);
    if (playingCount() >= SF_PLAY_MAX)
    {   /* fade the oldest one out */
        SfVoice *old = NULL;

        for (i = 0; i < SF_VOICES; i++)
        {
            if (sV[i].f != NULL && sV[i].fadeStep == 0.0f && (old == NULL || sV[i].age < old->age))
            {
                old = &sV[i];
            }
        }
        if (old != NULL)
        {
            old->fadeStep = 1.0f / (SF_STEAL_SECS * SF_RATE);
        }
    }
    for (i = 0; i < SF_VOICES && v == NULL; i++)
    {
        if (sV[i].f == NULL)
        {
            v = &sV[i];
        }
    }
    if (v == NULL)
    {   /* every slot holds a fading voice: take the one nearest silence */
        for (i = 0; i < SF_VOICES; i++)
        {
            if (v == NULL || sV[i].fade < v->fade)
            {
                v = &sV[i];
            }
        }
    }
    if (duck < 1.0f)
    {
        for (i = 0; i < SF_VOICES; i++)
        {
            if (sV[i].f != NULL && &sV[i] != v)
            {
                sV[i].duckTo = duck < sV[i].duckTo ? duck : sV[i].duckTo;
                sV[i].duckLeft = (u32)(SF_DUCK_SECS * SF_RATE);
            }
        }
    }
    id = sNextId++;
    if (sNextId <= 0)
    {
        sNextId = 1;
    }
    v->f = f;
    v->pos = 0;
    v->id = id;
    v->vol = vol;
    v->duck = v->duckTo = 1.0f;
    v->duckLeft = 0;
    v->fade = 1.0f;
    v->fadeStep = 0.0f;
    v->age = ++sAge;
    n = playingCount();
    SDL_AtomicUnlock(&sLock);
    if (logOn())
    {
        portConLogf("ge: sound: play %s as voice %d (%.2f s, vol %.2f, duck %.2f), %d playing\n", f->name, id,
                    (double)f->frames / SF_RATE, (double)vol, (double)duck, n);
    }
    return id;
}

/* a random existing variant of base: base.wav, base1.wav .. base16.wav, not the one played last time when there is a choice */
const char *portSoundFilePlayAny(const char *base, float vol, float duck, int *idOut)
{
    static char lastBase[SF_VARIANTS][SF_NAME];
    static s32 lastPick[SF_VARIANTS];
    SfFile *have[SF_VARIANTS + 1];
    char name[SF_NAME];
    s32 n = 0, k, slot = -1, pick;

    if (base == NULL || SDL_strlen(base) + 3 > SF_NAME - 5)
    {
        return NULL;
    }
    for (k = 0; k <= SF_VARIANTS; k++)
    {
        SfFile *f;

        if (k == 0)
        {
            SDL_snprintf(name, sizeof(name), "%s", base);
        }
        else
        {
            SDL_snprintf(name, sizeof(name), "%s%d", base, k);
        }
        f = getFile(name);
        if (f != NULL && f->state == 1)
        {
            have[n++] = f;
        }
    }
    if (n == 0)
    {
        return NULL;
    }
    logOn();
    for (k = 0; k < SF_VARIANTS; k++)
    {
        if (lastBase[k][0] == '\0' || SDL_strcasecmp(lastBase[k], base) == 0)
        {
            slot = k;
            break;
        }
    }
    pick = (s32)(rnd() % (u32)n);
    if (slot >= 0)
    {
        if (n > 1 && lastBase[slot][0] != '\0' && pick == lastPick[slot])
        {
            pick = (pick + 1 + (s32)(rnd() % (u32)(n - 1))) % n;
        }
        SDL_snprintf(lastBase[slot], SF_NAME, "%s", base);
        lastPick[slot] = pick;
    }
    k = portSoundFilePlay(have[pick]->name, vol, duck);
    if (idOut != NULL)
    {
        *idOut = k;
    }
    return k > 0 ? have[pick]->name : NULL;
}

int portSoundFileVariants(const char *base)
{
    char name[SF_NAME];
    s32 n = 0, k;

    if (base == NULL || SDL_strlen(base) + 3 > SF_NAME - 5)
    {
        return 0;
    }
    for (k = 0; k <= SF_VARIANTS; k++)
    {
        if (k == 0)
        {
            SDL_snprintf(name, sizeof(name), "%s", base);
        }
        else
        {
            SDL_snprintf(name, sizeof(name), "%s%d", base, k);
        }
        n += portSoundFileExists(name);
    }
    return n;
}

void portSoundFileStop(int id, float fade)
{
    s32 i;

    SDL_AtomicLock(&sLock);
    for (i = 0; i < SF_VOICES; i++)
    {
        if (sV[i].f != NULL && sV[i].id == id)
        {
            sV[i].fadeStep = 1.0f / ((fade > 0.005f ? fade : 0.005f) * SF_RATE);
        }
    }
    SDL_AtomicUnlock(&sLock);
}

int portSoundFilePlaying(void)
{
    s32 n;

    SDL_AtomicLock(&sLock);
    n = playingCount();
    SDL_AtomicUnlock(&sLock);
    return n;
}

static s16 clampS16(f32 v)
{
    if (v > 32767.0f)
    {
        return 32767;
    }
    if (v < -32768.0f)
    {
        return -32768;
    }
    return (s16)v;
}

/* audio_pc.c portAudioPump: add the playing voices into one rendered frame (16-bit stereo, SF_RATE) */
void portSoundFileMix(s16 *buf, u32 frames)
{
    f32 sfx, target, master0;
    s32 i, n, endedId[SF_VOICES], nEnded = 0, report = 0;
    u32 endedPos[SF_VOICES];

    if (buf == NULL || frames == 0)
    {
        return;
    }
    SDL_AtomicLock(&sLock);
    n = playingCount();
    for (i = 0; i < SF_VOICES; i++)
    {
        if (sV[i].f != NULL)
        {
            break;
        }
    }
    if (i == SF_VOICES)
    {
        sMaster = 1.0f;
        SDL_AtomicUnlock(&sLock);
        return;
    }
    sfx = (g_sndSfxSlotVolume != NULL) ? (f32)g_sndSfxSlotVolume[0] / 32767.0f : 1.0f;
    if (sfx < 0.0f)
    {
        sfx = 0.0f;
    }
    target = n > 1 ? 1.0f / SDL_sqrtf((f32)n) : 1.0f;
    master0 = sMaster;
    for (i = 0; i < SF_VOICES; i++)
    {
        SfVoice *v = &sV[i];
        const s16 *src;
        f32 m = master0;
        u32 k;

        if (v->f == NULL)
        {
            continue;
        }
        src = v->f->pcm;
        for (k = 0; k < frames && v->pos < v->f->frames; k++)
        {
            f32 g, env = 1.0f;

            if (m < target) { m += SF_RAMP; if (m > target) m = target; }
            else if (m > target) { m -= SF_RAMP; if (m < target) m = target; }
            if (v->duckLeft > 0)
            {
                v->duckLeft--;
                if (v->duckLeft == 0)
                {
                    v->duckTo = 1.0f;
                }
            }
            if (v->duck < v->duckTo) { v->duck += SF_RAMP; if (v->duck > v->duckTo) v->duck = v->duckTo; }
            else if (v->duck > v->duckTo) { v->duck -= SF_RAMP; if (v->duck < v->duckTo) v->duck = v->duckTo; }
            if (v->fadeStep > 0.0f)
            {
                v->fade -= v->fadeStep;
                if (v->fade <= 0.0f)
                {
                    v->fade = 0.0f;
                    break;
                }
            }
            if (v->pos < SF_FADE_IN)
            {
                env = (f32)v->pos / SF_FADE_IN;
            }
            g = v->vol * v->duck * v->fade * env * m * sfx;
            buf[k * 2] = clampS16((f32)buf[k * 2] + (f32)src[v->pos * 2] * g);
            buf[k * 2 + 1] = clampS16((f32)buf[k * 2 + 1] + (f32)src[v->pos * 2 + 1] * g);
            v->pos++;
        }
        if (v->pos >= v->f->frames || v->fade <= 0.0f)
        {
            endedId[nEnded] = v->id;
            endedPos[nEnded] = v->pos;
            nEnded++;
            v->f = NULL;
        }
    }
    /* the master ramp, once per frame (every voice saw the same one) */
    for (i = 0; i < (s32)frames; i++)
    {
        if (sMaster < target) { sMaster += SF_RAMP; if (sMaster > target) sMaster = target; }
        else if (sMaster > target) { sMaster -= SF_RAMP; if (sMaster < target) sMaster = target; }
    }
    sLogFrames += frames;
    if (sLogFrames >= SF_RATE)
    {
        sLogFrames = 0;
        report = 1;
    }
    if (report && logOn())
    {
        char line[256];
        s32 len = 0;

        for (i = 0; i < SF_VOICES && len < (s32)sizeof(line) - 40; i++)
        {
            if (sV[i].f != NULL)
            {
                len += SDL_snprintf(line + len, sizeof(line) - (size_t)len, " %d:%s@%u/%u", sV[i].id, sV[i].f->name, sV[i].pos, sV[i].f->frames);
            }
        }
        line[len] = '\0';
        SDL_AtomicUnlock(&sLock);
        portConLogf("ge: sound: mix %d playing, master %.2f, sfx %.2f:%s\n", n, (double)sMaster, (double)sfx, line);
    }
    else
    {
        SDL_AtomicUnlock(&sLock);
    }
    if (logOn())
    {
        for (i = 0; i < nEnded; i++)
        {
            portConLogf("ge: sound: voice %d ended at sample %u\n", endedId[i], endedPos[i]);
        }
    }
}

#endif /* TARGET_N64 */
