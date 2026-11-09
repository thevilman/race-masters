# tools/audio: Top Gear's music and sound effects, ripped from your ROM

These tools take every song and sound effect out of your own copy of Top Gear (USA) and
put them, with everything needed to remaster or rebuild them, in a folder **outside the
repository**. The audio comes from the ROM: never commit it.

```
cmake -S tools/audio -B build-audio/audio -DCMAKE_BUILD_TYPE=Release
cmake --build build-audio/audio -j8
python3 tools/audio/build_audio.py --rom "Top Gear (USA).sfc" --out "<your folder>/audio"
```

(`tg_audio` is also a target of the game's own build, `cmake --build build --target tg_audio`, and
the release downloads carry it in `music-tools/`, where the modern-music generator, tools/music,
runs it for the launcher's Build music.)

About 10 minutes, under 1.5 GB of memory; the folder is about 800 MB. The folder gets `music/`, `music/loops/`, `spc/`, `sfx/`, `samples/`,
`rebuild/` (MIDI, SFZ kits, stems, sheets per song), `reference/`, `manifest.json`,
`README.md`; `HANDOFF.md` (this folder) explains it all to whoever modernises the audio.

## Files

| File | What it does |
|------|--------------|
| `tg_audio.c` | C tool on the LakeSnes core (built on its own, links `third_party/lakesnes` and `src/input_script.c`; changes nothing in the game or its build). Subcommands: `songs` (an .spc + raw WAV + DSP note log per song, `--only U`, `--keep MASK` for one-voice stems), `sfx` (each port-1 command), `engine` (RPM sweep), `probe` (which port-0 values play after each upload), `log` (replays an input recording on the original game and prints every CPU write to the APU ports; `--wav` records the game's own mix), `render-spc` (plays an .spc on a fresh APU). |
| `build_audio.py` | Runs `tg_audio` and makes the finished folder: loops found from the driver, fades, ID666 tags, BRR samples, checks, `manifest.json`, `README.md`. |
| `rebuild.py` | From the DSP note logs: MIDI (track per voice, channel per instrument, pitch bend, loop markers, exact tempo), SFZ kits (loop, tuning, envelope), per-song `sheet.json`, and a self-check that renders the MIDI through the kit (own small SFZ player + SNES-style echo) and compares it with the original. |
| `transcribe.py` | Sample tuning (harmonic product spectrum on the loop), key profiles; a quick stand-alone MIDI writer. |
| `spc700_disasm.py` | SPC700 disassembler for reading the sound driver in an .spc. |
| `HANDOFF.md` | For the next agent: the game's sound, the files, track by track facts, how to get modern audio into the port, the deliverable spec, rules. |

## How the game plays sound (what the tools rely on)

- **Driver.** At power-on `Reset` uploads an N-SPC-style driver to APU `$0800` through the
  IPL (`$00:EBB6`). Songs: `MenuStartMusic` (`$0F:8133`, menus) and the race program
  (`$00:8175`) both send `$18` on port 1 (driver mutes and jumps back to the IPL), call
  `UploadSoundDriver` (`$07:8000`) with A = upload set 0-6 (chunk lists at `$07:8164`),
  write 0 to port 0, wait ~65,536 loops, then write set + 1 to port 0.
- **Songs.** Song table at APU `$1399`, 7 entries; every playlist is one pattern played
  again and again (song 6, Qualified, ends). The driver loads a pattern at APU `$0A94`,
  which is how loop points are found. Tempo: APU `$3D`; a driver tick is
  16384 / tempo samples (timer 0 at 500 Hz, scaled by tempo/256).
- **Which song plays where.** 0 title and every menu; races play set
  `((course & 3) ^ 3) + 1`, so the music follows the course's place in its country
  (1st: Las Vegas tune, 2nd: Hiroshima, 3rd: Bordeaux, 4th: Frankfurt); 5 when a country
  is qualified (`$0F:858A`), 6 after the last country (`$0F:855E`).
- **Effects.** Port 1 commands 1-`$1D` (jump table at APU `$10A7`), voices 4-7 (P1: 4
  engine, 5 effects; P2: 6, 7); race songs use voices 0-3. The race IRQ (`$00:B850`) sends
  one command per frame once the driver has echoed the last. Ports 2/3 = engine pitch
  (RPM + `$400`). `$19` pauses, `$01` silences the effects at the end of a race. The
  menus make no sounds. Only the race upload sets contain the effect samples.

## Sources

- SPC file format and ID666 tags: <https://wiki.superfamicom.org/spc-and-rsn-file-format>,
  <https://wiki.superfamicom.org/id666-format>, SPC format v0.31 text (snesmusic.org).
- Track list of the published SPC set (7 songs: Title, Las Vegas, Hiroshima, Bordeaux,
  Frankfurt, Qualified, Ending Theme; music by Barry Leitch and Hiroyuki Masuno per that
  set's credits): <https://ocremix.org/chip/6064>.
- The game and its soundtrack: <https://en.wikipedia.org/wiki/Top_Gear_(video_game)>
  (music by Barry Leitch, largely reworked from his Lotus series tunes on the Amiga),
  OverClocked ReMix's Top Gear remixes (e.g. <https://ocremix.org/remix/OCR01674>).
- SNES DSP behaviour (BRR decoding, ADSR/GAIN rates, echo): the fullsnes document by
  nocash and the SNES Development Manual; LakeSnes `dsp.c`.
- N-SPC driver family layout (pattern lists, track pointers): general SNES ripping
  knowledge; confirmed here by disassembling the driver with `spc700_disasm.py`.

## Limits

- LakeSnes's DSP is accurate but not cycle-exact; `.spc` files can be rendered in
  blargg's snes_spc / higan for reference playback.
- `.spc` files cannot carry the timers' internal phase: playback can start up to ~1 ms
  differently from the WAVs.
- MIDI comes from the DSP log, not from decoding the driver's sequence bytes; note names
  depend on the measured tuning of each sample (one-shot samples are mapped so that
  pitch `$1000` = middle C). Playback through the SFZ kits is pitch-exact either way.
