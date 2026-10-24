Data/Sounds - sound files that Lua modes play over the game's own audio (sound.play_file / sound.play_any).

The game ships no sounds here. Put WAV files in this folder: any sample rate, mono or stereo, 8/16/32-bit or float.
They are converted to the game's output format (22050 Hz stereo) when first used. A file longer than 60 s is cut.
The level follows the game's Sound (FX) volume.

The arena crowd (GE_MODE=arena) looks for these names, each with up to 16 variants (cheer_small.wav, cheer_small1.wav ..
cheer_small16.wav; one is picked at random each time):

  cheer_small   a kill
  cheer_big     a headshot, a multi-kill, a wave cleared
  cheer_huge    a boss killed (falls back to cheer_big)
  jeer          Bond takes a big hit (falls back to boo)
  boo           Bond's health drops low
  boo_big       Bond dies (falls back to boo)

Missing files are skipped: the arena works with none of them.

Licensing: only include recordings you are allowed to redistribute (for example CC0 / public domain) if you pass the
folder on or put it in a release. See modding/arena.md.
