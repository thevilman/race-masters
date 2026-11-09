# HANDOFF: modernising Top Gear's music and sound effects

For the agent who will re-arrange / remaster / rebuild the audio. Everything you need is
in the player's folder `$TG_DIR/audio/` (`$TG_DIR`: the folder with the player's ROM; made by
`tools/audio/build_audio.py`).
The copy of this file in the repository (`tools/audio/HANDOFF.md`) has no track tables;
the copy in the player's folder has them filled in from the rip.

## 1. The game and its sound

- **Top Gear** (SNES, 1992; developed by Gremlin Graphics, published by Kemco). The music
  and its arrangements belong to their rights holders (see section 7).
- **Sound chip.** SNES S-SMP/S-DSP: 8 voices, 64 KiB of audio RAM shared by driver, song
  and samples, BRR-compressed samples, output 32 kHz stereo, one global echo (delay up
  to 240 ms, feedback, 8-tap FIR), per-voice ADSR/GAIN envelopes, a noise generator.
- **Driver.** An N-SPC-style driver at APU `$0800`, uploaded at power-on. Every song comes
  with its own upload set (song data + its instruments): `UploadSoundDriver` (`$07:8000`)
  sends set 0-6, then the CPU writes set + 1 to APU port 0 to start it. Port 1 carries the
  sound-effect commands, ports 2/3 the engine pitch.
- **Loops.** Every song is one long pattern that the driver plays again and again (the
  loop start is the very start of the song; the Qualified jingle plays once). Pass lengths
  are fixed per song (to within a few ms of tick rounding); the title's first pass is
  0.6 s shorter than later ones.
- **Voices.** Race songs use voices 0-3; voices 4-7 are the effects (player 1: 4 engine,
  5 effects; player 2: 6, 7). The title, Qualified and Ending use all 8 (no effects then).
  The menus make no sounds at all.

## 2. The project and the direction

- **This repository** is a PC port of the game in C (a fork of thebradshaw/topgear-pc):
  the original runs on an embedded emulator core (LakeSnes) while routines are translated
  to C; the **modern look** mod draws the races in 3D and redraws the menus
  (`docs/MODERN-TEAM-PLAN.md`, `docs/TRACKER.md`, `docs/3D-MOD.md`). Rule of the project:
  the game stays the game (no change to its logic, RAM or timing); only presentation
  changes.
- **The player's goal:** the game should feel modern and fresh, yet faithful. The audio
  should do what the visuals do: same game, same melodies, today's production.
- **Visual direction to match:** navy / teal UI, Barlow Condensed type, synthwave-ish neon
  accents on the title, a colour per country. Musically that suggests: keep the
  original's tempo, keys, hooks and structure; modern drums and bass (tight, punchy,
  sidechained), analogue-style synth leads and pads (the title can lean synthwave:
  gated reverb, arpeggios, neon pads), a cleaner wider mix; per-country flavour is
  optional (the songs follow the course's place in its country, not the country).

## 3. Track by track

<!-- TRACKS -->

## 4. Sound effects

<!-- SFX -->

What matters most to modernise, in order:

1. **Engine** (heard all race long). The original is one short looped sample whose pitch
   follows the RPM (pitch register = RPM + `$400`, 32 kHz at `$1000`; races use
   `$0400`-`$0E00`, i.e. the sample at 8-28 kHz). A modern engine wants layered loops
   (idle / mid / high) crossfaded by RPM, plus load (throttle) layers. Gear changes are
   only heard as the RPM drop (no separate sound).
2. **Crash** and **bump**: short hits; modern versions want impact + debris/scrape layers
   and some variation (the game sends the same command each time).
3. **Skid**: very short in the original (0.2 s, re-sent while skidding): a modern skid
   should be a loop that starts and stops with the skid flag.
4. **Pit stop**: air-wrench-like loop on the engine voice.
5. **Missing in the original** (nothing to rip, design new if wanted): nitro, gear shift,
   countdown / start lights, lap / final-lap cues, menu move / select / back sounds,
   results jingles. Adding them means new triggers (presentation-only hooks on game
   state), never changes to the game.

## 5. Getting modern audio into the game

**What exists today.** The port plays the emulated APU's output: `src/main.c` `runFrame()`
calls `tg_game_get_samples()` (`src/game.c` -> `snes_setSamples` -> LakeSnes
`dsp_getSamples`, which resamples the DSP's 32 kHz ring to the frame's sample count) and
queues the result on an SDL3 audio stream at 48 kHz. There is no music/effect separation
and no replacement mechanism yet.

**Where songs start.** Every song start is a CPU write to APU port 0 (`$2140`) of
set + 1 after an upload; a `$18` on port 1 (`$2141`) comes before every upload (silence);
`$19` on port 1 is pause; `$01` on port 1 ends a race's effects. In the code:
`MenuStartMusic` (`$0F:8133`, C: `src/port/menu_flow.c` `MenuStartMusic`), the race music
(`$00:8175`, C: `src/port/program.c` `start_race_music`) and the boot (`$00:8064`, still
emulated). All of them write through `snes_write` (translated code uses `tg_bus_write8`
-> `snes_write`), so **one observer on bus writes to `$2140-$2143`** sees every song
change in every port mode, without touching game logic.

**Done (2026-11-03): see docs/AUDIO.md and src/audio/music.c** (music only; effects still open). The plan it followed:

1. An `AudioDirector` in the frontend layer, fed by a read-only observer of `$2140/$2141`
   writes (LakeSnes `snes->writeHook`, which `src/game.c` already uses for tracing: chain
   it rather than replace it). It tracks: song = value written to port 0 minus 1 after a
   `$18`; pause (`$19` on port 1 until it changes); race end (`$01`).
2. Replacement files in `audio-modern/` next to the executable, keyed by song id:
   `music_01.ogg` ... `music_07.ogg` (and later `sfx_*.ogg`). If a file exists for the
   current song: stream it with its loop points (from the file's metadata) and silence the
   emulated music; else play the APU as today.
3. Silencing only the music: race songs use voices 0-3 and effects 4-7, so mute voices
   0-3 at the DSP (a per-voice output mask in LakeSnes `dsp.c`, default off, applied where
   the voice is mixed: it never changes what the game reads back). Title / Qualified /
   Ending use all 8 voices and have no effects: mute all 8.
4. Mixing in `runFrame()`: APU samples (effects) + the modern stream, both at 48 kHz;
   a short crossfade (~150 ms) on song changes, follow the game's own silences (`$18`
   before each upload, pause), duck the music ~3 dB under engine-heavy moments if wanted.
5. Data needed per song: id (1-7 = upload set + 1), loop start / end in samples of the
   modern file, intro (none in the originals: loops start at 0), fade-out behaviour
   (Qualified ends by itself).
6. Effects (later): the same observer sees port 1 commands (table in section 4) and
   ports 2/3 (engine pitch), enough to drive modern effects (engine layers by RPM) and
   mute voices 4-7.

Keep it off by default and out of the regressions' path (`tg_headless` must still
produce the same WRAM and frame hashes).

## 6. Deliverable spec for modernised files

- **Format:** 48 kHz, 24-bit stereo WAV masters; game copies as Ogg Vorbis q6-q8 (or
  48 kHz 16-bit WAV). Effects: 48 kHz mono or stereo WAV, engine loops seamless.
- **Loudness:** music about -14 LUFS integrated (EBU R128 / ITU-R BS.1770), true peak
  at or below -1 dBTP; effects normalised to sit about 3-6 dB under the music's loudest
  parts; engine loops about -20 LUFS.
- **Loops:** sample-accurate loop points stored in the file: `LOOPSTART` / `LOOPLENGTH`
  Vorbis comments (in samples) for OGG, a `smpl` chunk for WAV; also listed in
  `audio-modern/manifest.json`. A loop must be seamless (render the tail of the loop
  into its start).
- **Naming:** `music_01_title.ogg` ... `music_07_ending.ogg`, `sfx_01_engine_idle.wav`,
  etc., using the ids in `manifest.json`; folder `audio-modern/` in the player's folder.
- **Length:** at least one full pass of the original structure; no fade-out in the
  looping files (the game loops them).

## 7. Constraints

- The ripped originals, the MIDI/SFZ/stems derived from them, and any new arrangement of
  the game's melodies are copyrighted material or derivative works: they stay in the
  player's private folder. Not in git, not in releases, not published, not shared.
- Tools and documentation without audio can go in the repository (`tools/audio/`).
- No change to game logic, RAM or timing; integration is presentation-only and opt-in.

## 8. Workflow and tools

1. **Listen** to `music/*.wav` and the `.spc` files (foo_gme / SPC Player / Audacious;
   for reference-grade playback, a cycle-accurate SPC player).
2. **Rebuild in a DAW** from `rebuild/NN_name/`: import `NN_name.mid` (one track per SNES
   voice; channel = instrument), load `NN_name.sfz` in an SFZ player (sfizz, free;
   Sforzando) on each channel, or the single-instrument files in `instruments/` one per
   DAW track. Set the MIDI pitch-bend range to 12 semitones if your DAW ignores the RPN in
   the file. Recreate the echo from `sheet.json` (`echo`: delay ms, feedback, FIR taps =
   a lowpass-ish filter, which voices). Compare against `stems/voiceN_*.wav` and
   `stems/mix.wav`.
3. **Re-arrange**: swap the SFZ instruments for modern synths/drums track by track; keep
   melody, bass line, harmony and tempo; use the `loopStart`/`loopEnd` markers for the
   loop; then mix and master to the spec above.
4. **Stems / separation**: the per-voice stems are exact (rendered from the chip with the
   other voices silenced), so no AI stem separation is needed.
5. **MIDI method**: the MIDI comes from logging the DSP while the driver plays (every
   key-on/off, pitch and volume change), not from decoding the sequence bytes. Timing,
   pitch and dynamics are exact; note names depend on each sample's measured tuning
   (`sheet.json` -> `instruments` -> `natural_rate_note`, `f0_hz_at_32k`); one-shot
   (drum) samples are mapped so pitch `$1000` = MIDI 60. The self-check per song
   (`sheet.json` -> `self_check`) renders the MIDI through the kit and compares it with
   the original.
6. **Regenerate** everything at any time: build `tools/audio` and run
   `python3 tools/audio/build_audio.py --rom ... --out ...` (about 10 minutes).
