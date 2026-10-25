# Audio

## The path

- The real libultra AL synthesizer and Rare's compressed-MIDI sequencer are compiled for PC (`src/libultra/audio`,
  `src/libultrare/audio`). Rare's sound player is `src/snd.c`; music is `src/music.c`.
- `port/src/audio_pc.c` emulates the RSP audio microcode on the CPU (`portAcmdRun`: ADPCM, RESAMPLE, ENVMIXER, MIXER,
  POLEF, ...) and queues the result to SDL. Output: 22050 Hz, s16 stereo.
- **The pump is the only thing that advances audio state.** `portAudioPump` (from `pcsched.c`) calls `alAudioFrame`,
  which is the only caller of every AL client handler: the SFX event queue, the sound-state list and the three music
  sequencers advance only while the pump renders. Upstream 1.0.2b paces it on wall time with the SDL queue as a
  catch-up trigger, retries a failed device every 2 s, and treats a full queue that does not move for ~1 s as a dead
  sink (`audio_pc.c`, upstream commit 7de0c0f).
- Audio frames follow the **wall clock**, sim ticks follow the tick clock. Under fast-forward (the bot's
  `GE_CLOCKSCALE`, sim-only ticks) many sim ticks' worth of sound events land in one audio frame. This is the root of
  the crash below.

## Playing a sound

- Every one-shot effect goes through `sndPlaySfx(bank, id, state)` (`src/snd.c` ~1235). The id is an index into the
  sound bank; the fork's `port/src/port_gamesfx.c` keeps a name table indexed by the game's own `SFX_ID` enum, so Lua
  can play effects by name in any build (`sound.play_sfx`, `sound.sfx_burst`).
- `sndPlaySfx` returns an `ALSoundState *` that the sim may keep (doors, alarms, AI sounds). That matters for
  rollback: a restored snapshot can hold a handle the audio engine has since recycled.
- Positional volume: `sub_GAME_7F053894` / `sub_GAME_7F0539E4` (`propobj.c`). In co-op (fork) each window hears its own
  Bond; the one value the sim keeps (`sfx_related[].Volume`, read by `IFSfxChannelVolumeLessThan`) is still measured from
  the nearest of all Bonds, so both machines agree.
- Level music: `music_setup_entries[]` in `src/game/music_0D2720.c` (main, ambient bg, X track per stage). Picking a
  random track draws from the sim RNG (`randomGetNext`), so a music override must not add or remove that draw in a
  replay.

## Pools and priorities

`GE_SNDSCALE` (1-4, default 2; 1 = N64) multiplies every pool (`src/music.c` ~117):

| Pool | N64 | PC default |
| --- | --- | --- |
| Physical synth voices (shared by music and SFX) | 24 | 48 |
| Voices per music track | 16 | 32 |
| Concurrent SFX (`sndp->maxSounds`, the gunfire cut-off) | 8 | 16 |
| SFX sound states / SFX events | 64 / 64 | 128 / 128 |
| **Synth parameter updates** | 128 | 256 |

When the player is full, a new sound preempts an older one. A post into a full event queue is silently lost; the port
(upstream) logs it (`ge: snd: event queue FULL`, `src/libultra/audio/event.c`), because a lost DEACTIVATE leaves a sound that never stops.

**Presentation sounds (fork, commit 06ff16a).** Crowd yells and other cosmetic sounds go through `portSndPlayPresent`
(`snd.c` ~1527): the state is marked `portPresent`, gets level, pitch and pan before its PLAY event, and has synth
priority 1. When the player is full a presentation sound **gives up instead of preempting**, and a game sound preempts
a presentation one first (`portSndPresentSlot` ~528). So the crowd never cuts gunfire, and cosmetic audio never changes
which game sounds play.

## The `alAdpcmPull` crash (bot branch, commit 53ff7c9; worth upstreaming)

Symptom: a random segfault in `alAdpcmPull` at `src/libultra/audio/load.c` ~68 (the ADPCM book load,
`f->table->waveInfo.adpcmWave.book`), 3 crashes in about 9 replays of one Bunker 2 recording, none under gdb.

Cause, measured with a crash-time dump of the filter (`table` 0, `first` 1, `sample` 0):

1. Under fast-forward the synthesizer's **parameter-update pool runs dry** (many sim ticks of sound events per audio
   frame; random because audio frames follow the wall clock).
2. `alSynStartVoice` / `alSynStartVoiceParams` then **drop the START** and return.
3. A later volume, pan or stop update on that voice makes GE's env mixer pull it. GE's copy of `_pullSubFrame`
   (`src/libultrare/audio/env.c` ~368) tests only `outCount`, not whether the voice is playing, so it pulls a load
   filter that never got a wavetable: a NULL `table`.

Fix (`#ifndef TARGET_N64`): `_pullSubFrame` returns unless the voice is playing (`e->motion == AL_PLAYING`, the stock
libultra test), so a dropped START is just a silent sound; and the steal path of `alSynAllocVoice` checks its
`__allocParam` result, which it used to write through NULL (`synallocvoice.c` ~67). After: 14 replays, no crash;
instrumented runs showed dropped STARTs in 5 of 8 and no table-less pull; the dam1 oracle identical.

It is on the bot branch only. Any fast-forward or heavy-sound mode on `heist` can still hit it.

## Rollback and audio

Audio is never part of a snapshot: an early experiment that restored `audi.c`/`snd.c` but not the AL heap hung in
`alSynAllocVoice` -> `_allocatePVoice` (corrupted voice list). Audio state must be wholly in or wholly out, and out is
the only sane choice. Re-simulated ticks re-trigger their sounds (not gated yet).

## The WAV mixer (fork, `port/src/port_soundfile.c`)

A small PCM mixer added into each rendered audio frame for the arena's crowd: 12 voices (6 heard), volume ramps,
ducking, a spinlock against the audio thread. Level = the game's SFX volume x `crowd_volume`. Files come from
`Data/Sounds/` (`cheer_small`, `boo`, ... plus numbered variants); missing files are skipped and no audio content is
shipped. Headless builds stub it. Lua: `sound.play_file` and friends. It never touches the synth, so it cannot affect
game sounds or the sim.
