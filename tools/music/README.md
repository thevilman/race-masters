# tools/music: make the modern music yourself, from your own ROM

The game can play optional modern versions of its music (Music option, see
[docs/AUDIO.md](../../docs/AUDIO.md)): the seven songs remade (faithful, synthwave,
orchestral) and a version of the race tune for each of the 32 cities (postcard, local band,
city band, race). They are arrangements of the game's compositions, so **they are not in this
repository or its downloads**. This generator makes them on your computer:

1. **Notes from your ROM.** `tools/audio` plays every song of your Top Gear (USA) ROM with the
   game's own sound driver on the emulated sound chip and logs the notes as MIDI. No note of the
   game's music is stored in this repository.
2. **The plan.** Each track is arranged by the generator's own code without rendering
   (`tgmusic/plan.py`), for the notes each of its instruments will play. It stays in the cache
   folder with the notes, never in the repository or with the music.
3. **Free instruments.** From free sample libraries and plug-ins (Surge XT, Dragonfly Reverb,
   GeneralUser GS, Sonatina Symphonic Orchestra, VSCO 2 CE, VCSL, ...) only the samples those
   notes can play (and the libraries' licence and readme files) are downloaded, once, from
   their official sources at pinned versions, each checked against its pinned SHA-256, into a
   cache folder (`tgmusic/needs.py`): about 1.0 GB for every style. They come out of a list of
   every file the styles may play (`tgmusic/data/samples.tsv`, 3.2 GB instead of the libraries'
   10 GB: samples that only sound with a controller the generator never sends, such as a
   piano's sustain-pedal ones, aren't in it; `make_manifest.py`), which is what you agree to.
   Licences: [THIRD_PARTY.md](THIRD_PARTY.md).
4. **Render.** The chosen styles are arranged and rendered to Ogg Vorbis with the loop points
   the game reads (`LOOPSTART` / `LOOPLENGTH`), straight into the game's `audio-modern` folder.
   Should a track need a sample the plan didn't foresee (it doesn't happen; the log would say),
   it is fetched from the list just before its instrument loads.

Nothing is written into the repository (the generator refuses an output, cache or work folder
inside it, and writes no `__pycache__` here).

## The easy way: the launcher

Players don't run anything by hand: the launcher's **Build music** button (and its first-run
offer) does all of this on Windows 10/11 x64 and Linux, with nothing installed beforehand,
after showing what it will download and asking (docs/AUDIO.md, "Build music"). The release
downloads carry this folder as `music-tools/music` (with `music-tools/audio` and a ready-made
`music-tools/tg_audio[.exe]`; `tools/package_music_tools.sh`). The rest of this page is for
running it by hand and for maintainers.

## What you need

- **Windows 10/11 x64** or **Linux x86-64**. macOS is not supported yet (no plug-in builds pinned).
- A Python the pinned wheels are for, which `setup_tools.py` provides:
  - Windows: the python.org embeddable Python 3.12.10 (pinned, SHA-256 checked) in a per-user
    folder, the packages installed into it. Run by hand, any Python 3.8+ can start
    `setup_tools.py`, which fetches it. Several packages' Windows wheels (numba, llvmlite,
    pedalboard, tinysoundfont) use MSVCP140.dll from the **Microsoft Visual C++
    Redistributable (x64)**, which most PCs have; if it's missing, `setup_tools.py` (and the
    launcher) stop and point to Microsoft's download.
  - Linux: the system's Python 3.10, 3.11 or 3.12 (`python3`, or `python3.12` etc. on the
    PATH), in a venv made with `--without-pip` (pip comes from its pinned wheel). The
    two plug-ins need some system libraries (Ubuntu:
    `sudo apt install libasound2 libfreetype6 libgl1 libx11-6 libxcursor1 libxext6 libxrandr2`;
    no display is needed); `setup_tools.py` checks for them.
- `tg_audio` (tools/audio): ready-made in a release (`music-tools/tg_audio[.exe]`); in a source
  checkout build it with the game (`cmake --build build --target tg_audio`, found in `build*/`),
  or the generator builds it with CMake and a C compiler.
- Your ROM: Top Gear (USA), SHA-256 `ca9889f1…e788` (see the main README).
- Disk and network: see [the numbers](#time-disk-and-memory) below (downloads, once: about
  1.0 GB of instruments for every style, at most 3.2 GB, plus 0.15 GB of tools).
- Memory: 0.3 to 1.6 GB per track rendered at once (`--jobs`; most under 1.3 GB).

## Run it by hand

```
python3 tools/music/setup_tools.py          # Windows: py tools\music\setup_tools.py
```

prints the Python to use (Linux `~/.local/share/race-masters/music-tools/venv/bin/python`,
Windows `%LOCALAPPDATA%\race-masters\music-tools\python\python.exe`). It installs pip from its
pinned wheel and every package in `requirements.txt` with
`--no-deps --only-binary=:all: --require-hashes` (each wheel checked against its SHA-256),
checks they load, and keeps the result; run it again any time (a finished install is checked
in seconds). Then:

```
<that python> tools/music/generate.py --rom "/path/to/Top Gear (USA).sfc" --out "/path/to/game/audio-modern"
```

The default makes **faithful** (the seven songs, the game's default Music option) and **D**
(each city's race version, the MODERN option); `--styles all` makes all 149 tracks (the
launcher's default). It lists the libraries to download from, with their licences and how much
at most and usually, and asks before downloading (`--yes` to agree in advance); then it reads
the notes, plans the tracks, says exactly what it downloads, and does. Stop it at any time
(Ctrl+C): tracks already in `--out` are kept (`--force` renders them again), the libraries, a
half-done download, the notes and the plan are reused from the cache.

Then start the game and choose the music in the options (Tab) or the launcher.

| Option | |
|---|---|
| `--styles faithful,D` | what to make: `faithful`, `synthwave`, `orchestral` (the seven songs), `A` postcard, `B` local band, `C` city band, `D` race (the 32 cities); `all` for everything (149 tracks). Aliases: `modern`=D, `band`=C, `local`=B, `postcard`=A |
| `--stages rio,las_vegas` | cities for A-D (names or countries, e.g. `usa`; default all 32; `--list` shows them) |
| `--songs 01_title,…` | songs for faithful / synthwave / orchestral (default all seven) |
| `--jobs N` | tracks rendered at once, at most (default `auto`: half the logical processors, as many as the memory holds at about 1.6 GB each with 1.5 GB left; beyond a third of that, at least two, a track starts only when the memory available then holds it too). Each track in its own process; a track whose process dies, e.g. out of memory, is reported and the rest go on |
| `--threads N` | threads each track's parts play in (default `auto`: the track's share of the processors, 2-4). Its Surge XT parts play at once, each on an instance of its own; the other parts play one after the other, as always; the mix takes the parts in their order. `1`: every part in turn, as before |
| `--cache DIR` | libraries, downloads and work files (default `<user data>/race-masters/music-tools/cache`, or `$TGMUSIC_CACHE`; an earlier version's `~/.cache/topgear-music` is used if it's there) |
| `--tg-audio FILE` | tools/audio's `tg_audio` (default: beside this folder in a release, `build*/` in a checkout, else built with CMake) |
| `--formats ogg,wav,mp3` | also keep the 24-bit WAV master (with a `smpl` loop) and/or an MP3 |
| `--clean-after` | once every track asked for is made, delete the libraries and work files (the notes, the plan; about 1 GB; downloaded again only to make more) |
| `--dry-run` | read the notes and plan the tracks, show what would be rendered and exactly what downloaded, then stop (the exact size needs the instruments' definition files, 0.5 MB: with `--yes` they are fetched) |
| `--progress-json` | for a program watching it (the launcher): progress as JSON lines on standard output, the rest in a log; see below |
| `--keep-downloads`, `--keep-extracted` | keep the downloaded archives / all of tools/audio's output |
| `--plan-notes FILE` | maintainers: render nothing, write FILE (JSON) with every note each instrument of the tracks is sent (`--styles` default all; needs `--rom` only, no libraries; `tgmusic/plan.py`). About a minute for all 149 tracks with `--jobs 6`. FILE is made from your ROM's notes: keep it out of the repository, to yourself |

The faithful title is always made: the game uses the folder only when it holds
`variations/01_title/01_title__faithful.ogg`, and every city option plays the faithful title,
Qualified and Ending.

## Stopping, going on, and a program watching

- **Stopping is safe at any moment.** A track appears only once complete (its files are
  written under temporary names and renamed; its `.json`, which marks it done, last), so a
  stopped render leaves no half track (its temporary files are removed next time). A download
  stops where it is and goes on from there next time (HTTP `Range` + `If-Range`; a server that
  sends the whole file instead, or a file that changed, starts again). An instrument file is
  written under a temporary name and renamed once its size and SHA-256 match, so the files
  done are kept, and only the others are fetched (a file checked once is listed in its
  library's `.tgmusic-files`, so it isn't read again). Every listed file a build fetched stays,
  so a later build of more styles fetches only what it adds. A plug-in is unpacked beside its
  library's folder, then moved in. A library counts as there only once what is wanted of it is
  in place and checked (a stamp file). The notes are read again only if they were not
  finished; each track's plan is kept in `<cache>/work/plan` as it is made (made again only if
  the generator's code, the notes or this computer's measured values change). One build at a
  time per cache (a lock file that the system releases when the build ends, however it ends).
- **The safety net.** Before a render process loads an .sfz instrument, it checks that every
  sample the track's notes can trigger there is there; one that isn't is fetched from the list
  then (SHA-256 checked, under a temporary name of its own so render processes at once don't
  clash), before sfizz loads the instrument (sfizz drops a region whose sample is missing).
  With the plan it never happens; the log says it if it did.
- **The render processes stop with the generator**: on Windows they are in a job object that
  ends them when the generator ends, however it ends; on Linux they are in their own process
  groups, stopped with them, and end if the generator is killed (`PR_SET_PDEATHSIG`). The
  generator lowers its own priority (below normal / `nice 10`), so the game stays smooth. On
  Windows each of its processes (and ffmpeg) also opts out of power throttling ("efficiency
  mode"), which Windows otherwise applies as it sees fit to a process without a window of its
  own: the efficiency cores at a low clock, the same work 2-3 times as long.
- **`--progress-json`** (`tgmusic/progress.py`): standard output carries only progress, one
  flat JSON object a line (strings, numbers, booleans), each with an `"ev"`:
  `plan` (tracks, done, todo, download_mb: usually, download_max_mb: at most, jobs, eta_s;
  again once the tracks are planned, with `planned` and the exact download_mb), `stage`
  (notes / plan / libraries / render, in that order, n of 4), `notes` (a heartbeat while the
  songs play), `planning` (i of n tracks planned), `download` (lib, i of n, got / total and
  all_got / all_total bytes, rate, eta_s), `unpack`, `track` (i of n, label, done / failed,
  secs, eta_s), `render` (a heartbeat: what is rendering), then `done` (with late_samples:
  what the safety net fetched), `cancelled` or `error` (code: network, hash, disk, vcredist,
  consent, tracks, other; text; the log file).
  Everything else goes to `<cache>/logs/build-*.log`. A line `cancel` on standard input, or
  standard input closing, stops it cleanly (exit code 3). `setup_tools.py --progress-json`
  speaks the same (`stage` tools, `tools` steps, `download`, `ready` with the Python, `error`).
  `tests/fake_generate.py` sends made-up lines for checking the launcher
  (`TG_MUSIC_FAKE=<scenario>`).

## What it writes

```
<out>/                                    (the game's audio-modern folder)
  variations/<song>/<song>__<style>.ogg    faithful / synthwave / orchestral, songs 01_title … 07_ending
  stages/<country>/<country>__<city>__A.ogg   and __B.ogg
  stages-band/<country>/<country>__<city>__C.ogg
  stages-race/<country>/<country>__<city>__D.ogg
  … a .json beside each track (tempo, key, loop, loudness, parts), CREDITS.txt
<cache>/
  libs/<library>/          the instrument files the notes play and the plug-ins (+ calibration/ for any new
                           instrument; .views/: what a render process loads, while it loads it;
                           surge-xt/SurgeXTUserData/: Surge XT's user folder, an empty defaults file)
  downloads/               archives while they download (deleted once unpacked; a .part goes on next time)
  logs/                    build-*.log (with --progress-json; the last five; the render processes write
                           into it too, every write at its end)
  work/                    the notes from your ROM: audio/rebuild/<song>/{<song>.mid,sheet.json},
                           events/<song>.json; extract.log; plan/ (what each track's instruments play)
  numba/                   compiled synth code
  results/                 each render process's result while it runs
```

The names, folders and loop tags are the ones `src/audio/music.c` reads: 48 kHz stereo Ogg
Vorbis (q7), mastered to -14 LUFS with true peak at most -1 dBTP, `LOOPSTART=0` and
`LOOPLENGTH=<samples>` on every song that loops (all but 06_qualified). Each loop is seamless:
the tail is folded back into the start and mastered circularly.

Keep these files to yourself: they are arrangements of the game's music.

## Time, disk and memory

Measured on a 12-thread WSL 2 machine with 7.8 GB of memory, a fresh cache, Linux (the
rendering: see below):

| Step | Time | Disk |
|---|---|---|
| Notes from the ROM (once per ROM): play the 7 songs (`tg_audio` ready-made) | 4.3 min (up to 11 on a slower computer) | 1.7 GB while it runs, then 8 MB kept |
| Plan the tracks (6 at once) | 12 s for the default 39, 41 s for all 149 | 1.5 MB in `<cache>/work/plan` |
| The instruments' definition files (once) | 4 s | 0.5 MB |
| Download the instruments (once) | all styles: 1,069 MB on Linux, 1,040 on Windows (the default faithful + D: 918 / 890 MB), 6 files at a time, about 7 MB/s here: 2.5 min | about 1.0 GB in `<cache>/libs` |
| Check them (SHA-256) and unpack the two plug-ins (once) | under a minute | plug-in archives deleted once unpacked |

Rendering, measured on a laptop with Windows 11, an Intel i7-1355U (2 performance + 8
efficiency cores, 12 threads) and 16 GB, with WSL 2 running on it (which left Windows 1-3 GB
of memory available); "Linux" is that WSL 2 (12 threads, 7.8 GB). Before: the generator
until these measurements (one part after another, every Surge XT load slow on Windows, see
below), at `--jobs 2` as the launcher chose; now: `--jobs auto`.

| Rendering | Before | Now |
|---|---|---|
| Windows, ten tracks across every style (the faithful, synthwave and orchestral title and Qualified, Stockholm A-D) | 10.1 min | 4.2 min (3 at once: the memory available held no more) |
| the same, track by track (s) | faithful 148 / 98, synthwave 123 / 70, orchestral 90 / 11, Stockholm A-D 109 / 132 / 106 / 149 | 47 / 8, 39 / 6, 83 / 10, 36 / 46 / 88 / 94 |
| Linux, the same ten | 10.6 min; faithful 113 / 25, synthwave 98 / 22, orchestral 213 / 21, Stockholm A-D 158 / 164 / 217 / 150 | 3.9 min (3 at once: what its 7.8 GB holds); 50 / 6, 36 / 4, 81 / 5, 40 / 45 / 80 / 91 |
| Windows, every track (`--styles all`, 149) | a build measured on the same laptop: 21 tracks in 57 min (faithful 77-258 s a track, synthwave 370-714, orchestral 28-512), about 5 h in all | 35.9 min, none failed (105 s of it the plan; mostly 3 at once: the memory available held no more; 6 to 130 s a track, 41 s on average) |
| A track's memory at its fullest | 0.4-2.9 GB | 0.2-1.6 GB |

Where a track's time went, one at a time, step by step, and what changed:

- Surge XT on Windows. Surge reads its user defaults file at every lookup for as long as the
  file is missing, and it looks one up about 600,000 times as pedalboard lists its parameters
  and thousands of times per patch; each lookup asks Windows whether the file exists, and in
  a Documents folder kept by OneDrive that is slow: loading the plug-in took 158-185 s and its
  first patch 134 s (Linux: about 1.7 s each). Surge now finds its user folder in its library
  (`surge-xt/SurgeXTUserData`, an empty defaults file: every default as before) and loads in
  under a second.
- pedalboard looked at each of Surge's 600 parameters (the text of 1,001 values each) when it
  loaded it and again at the first attribute set (1.7-3.4 s each on Linux); it no longer does.
- Windows ran the render processes, which have no window, power-throttled: the same work 2-3
  times as long. They opt out now.
- A track's parts played one after the other. Its Surge XT parts now play at once in threads
  (with the rest above: Qualified synthwave on Windows, alone, 84 s to 6.7 s). The .sfz parts
  stay one after the other: pysfizz keeps the GIL while it renders, and sfizz's own rendering
  is 98% of their time; more tracks at once use the processors instead.
- Memory: each .sfz instrument is let go after its last part and fewer full-length copies are
  made, so more tracks fit at once.
- What is left of a track: its .sfz parts (an orchestral title about 70 s on Windows, one at a
  time), the effects and the master (5-15 s; the loudness and limiter passes run in turn), the
  encoding (2-5 s), and starting its process (Python, numpy, the plug-ins: 2-5 s, not worth
  keeping processes from one track to the next: a fresh process keeps each track independent
  of the one before).

So the default set (7 faithful songs + 32 race versions, 39 tracks) renders in about 11
minutes on that laptop (a faithful song 31 s and a race version 52 s on average, 3 at once)
and takes about 200 MB in `audio-modern`; everything about 0.6 GB. Render times vary with
what else the computer is doing (here: other programs and WSL 2 used part of the processor
and most of the memory). Plus the Python environment: about 0.5 GB (measured: 509 MB on Linux; 0.15 GB downloaded). At its
fullest (the instruments at most, the tools and the music; the notes are read, and their work
files pruned, before anything is downloaded) a build for every style needs about 4.6 GB free;
the generator checks the free space before the notes and again, for the exact download, once
the tracks are planned.

The .sfz instruments render with sfizz from memory (instruments.py, needs.py): each loads with
every sample its track's notes can play read whole into memory (`<control> hint_ram_based`),
from a folder of links to just those files, so nothing is streamed while it plays and only
the samples the track plays take memory. Streamed, as sfizz plays by default, renders weren't
always the same: its freewheeling mode waits only for the reads its background thread has
already started, and it drops read data on a wall-clock timer, so a long render could play a
voice before its data was there (the same piano parts of Stockholm C came out differently in
3 of about 10 streamed renders). From memory, the same parts rendered 10 times, each in a fresh
process, were bit-identical, and identical to a render with every file of the list there.

What the launcher tells players before they agree (download, disk, time, the libraries and
their licences) comes from `tgmusic/data/consent.txt`, made from `tgmusic/libraries.py` and
`setup_tools.py` by `generate.py --write-consent` (the `music_tools` test checks it is up to
date).

## Reproducible

- The notes: the same ROM gives byte-identical MIDI (tools/audio runs the game's driver on the
  emulator; checked against an earlier extraction: all 7 MIDI files and sheets identical).
- The arrangement: fixed seeds everywhere (humanising, fills, answers, the city's band seeded
  by its name), no wall-clock or hash-order dependence.
- The instruments: pinned versions and hashes. Two tables measured on those instruments when
  the generator was made ship in `tgmusic/data/` (which patches sound an octave off,
  `pitch_calibration.json`; which keys each sampled instrument plays, `sfz_ranges.json`), so a
  render takes the same decisions everywhere; an instrument not in them is measured once and
  cached.
- Each track is rendered in a fresh process, so it does not depend on what was rendered before.
  sfizz (from memory, see above), TinySoundFont, the synth and the drum synth give
  bit-identical output run after run: two builds of ten tracks across every style had all 62
  .sfz parts, 17 SoundFont parts and 11 synth parts the same, and the same whole WAV where no
  Surge XT part plays (the orchestral title and Qualified). Each instrument loads only the
  samples its track's notes need (a download of every listed file renders the same).
- A track's parts in threads (`--threads`): only its Surge XT parts play at once, each on an
  instance of its own (pedalboard lets go of the GIL while a plug-in plays); every other part
  plays on the main thread in its turn, as before, and the mix takes the parts in their order,
  whichever ended first, with the same arithmetic in the same order (some of it now in place:
  the same numbers, fewer full-length arrays). The effects and the master stay on the main
  thread in their order: loading Dragonfly Reverb turns flush-to-zero on for the thread that
  loads it, and what is computed after it depends on that. Checked against the code before:
  the orchestral title, bit-identical master (Linux and Windows); and per part, in five tracks
  with Surge XT (Qualified synthwave, the faithful title, Las Vegas synthwave, Monaco D,
  Stockholm D), every .sfz, SoundFont, synth and drum part bit-identical, and each Surge XT
  part as alike to the code before as two runs of the code before are to each other (waveform
  correlation, spectrum, chroma, onsets). One kind of Surge XT part changes a hair: a patch
  that happens to come out the same run after run when the parts play in turn on one instance
  (Rubber Bass, Bass 2) comes out a little differently on an instance of its own (waveform
  correlation 1.0000, 0.00 dB, the difference 51 dB below the part); `--threads 1` plays them
  in turn on one instance, as before.
- The downloads: the plan reads the notes with the same code as the render, so what a build
  downloads is what its tracks play (the build of every style above fetched nothing more while
  rendering).
- What is not bit-exact: Surge XT starts its oscillators at random phases (and some patches
  drift like analog synths), so the parts it plays differ in their waveform from one run to the
  next while sounding the same; and the mix of a city band (C, D: every part the same) differed
  by a hair between two runs (Stockholm C: waveform correlation 0.9999, true peak 0.01 dB). Checked with `compare.py` against the renders the generator was
  ported from: faithful title waveform correlation 0.90, long-term spectrum within 0.04 dB,
  chroma 0.99, onsets 0.98 (two runs of the generator differ from each other just as much);
  Las Vegas D 0.9995 / 0.00 dB / 1.00 / 0.999; Rio D 0.95 / 0.03 dB / 1.00 / 0.98; Florence
  A / B / C 0.92 / 0.93 / 0.96 (spectra within 0.06 dB); Qualified orchestral (no Surge part)
  1.000 / 0.00 dB; Qualified synthwave (mostly Surge) 0.32 / 0.28 dB / 0.99 / 0.94. Same length,
  loop tags and loudness (-14.0 LUFS) in each case. For scale, two different cities' race
  versions of the same tune compare at 0.02 / 4.2 dB / 0.83 / 0.53.

## How it works

| File | |
|---|---|
| `setup_tools.py` | the Python environment: the embeddable Python (Windows) or a venv (Linux), pip from its pinned wheel, the hash-locked packages (`requirements.txt`) |
| `generate.py` | the entry point: what to make, licences, then the notes, the plan, the downloads, a pool of render processes |
| `tgmusic/progress.py` | `--progress-json` lines, cancelling, the time left, the child processes (no console windows, job object / process groups, priority, out of power throttling; the log shared with them), memory (how much is free, each render process's), how many tracks and threads at once |
| `tgmusic/libraries.py` | the pinned libraries (the plug-ins' Windows and Linux builds); the listed instrument files fetched one by one (from the library's repository at its commit, or out of its release zip with range requests), each checked, only those wanted (`fetch(want=...)`); resumable whole-file downloads + SHA-256 + unpack; what a build usually downloads (`TYPICAL_MB`); CREDITS.txt |
| `tgmusic/data/samples.tsv` | the instrument files the styles may play, with size and SHA-256: the most that is downloaded (made by `make_manifest.py`) |
| `tgmusic/needs.py` | what a build downloads, worked out from the plan (each instrument's .sfz files, the samples its notes can play, the licence and readme files); the plans kept per track; in a render process, the safety net and the folder of links sfizz loads an instrument from, every sample read whole into memory |
| `tgmusic/catalog.py` | every instrument file the code can load, found by reading the code (no packages needed) |
| `tgmusic/sfz.py` | which files an .sfz loads, read the way sfizz reads it (`#include`, `#define`, `default_path`, ...), and which samples given notes can play (`note_samples`: key and velocity ranges, note names, `octave_offset` / `note_offset`, release triggers; every round robin kept) |
| `tgmusic/plan.py` | the plan (a build's second stage, and `--plan-notes`): each track arranged by the real code in its own process, the notes each instrument would get recorded in place of rendering |
| `tgmusic/extract.py` | runs `tools/audio/build_audio.py` on your ROM with tg_audio (ready-made, or built out of tree with CMake) |
| `tgmusic/parse_midi.py` | MIDI + sheet → a note list per song (roles, drums, bends, tempo, loop) |
| `tgmusic/arrange.py` | analysis shared by every style (roles per voice, chords per half bar, sections, voicings) |
| `tgmusic/reimagine.py` | music-theory re-arrangements v1-v3 (reharmonised, re-grooved, modal lift), used by style B |
| `tgmusic/render.py` | the three song styles; stems (Surge XT parts in threads, each instrument let go after its last part) → mix (reverb, delay, SNES-like echo, ducking) → master → files |
| `tgmusic/stages.py` | the 32 cities' A (postcard) and B (local band) recipes |
| `tgmusic/bands.py`, `tgmusic/personality.py` | the 32 cities' bands (C) and their race versions (D): form, groove, the melody performed and handed round |
| `tgmusic/realism.py` | General MIDI voices swapped for sampled instruments (VSCO 2 CE, VCSL, MTG saxes, Naked Drums) |
| `tgmusic/instruments.py`, `tgmusic/synth.py`, `tgmusic/surgestate.py` | the players: sfizz (pysfizz, which has Windows and Linux wheels), TinySoundFont, Surge XT (instances on the main thread, each playing a part at a time; its user folder in its library) and Dragonfly Reverb as VST3 via pedalboard; a small analog-style synth and drum synth |
| `tests/` | `test_tools.py` (the `music_tools` test: progress, cancelling, resumable downloads, the files fetched one by one and out of a zip (a local server: resume, a bad file, a cancel, a server without ranges), only the files wanted and every listed one kept, the planned set and the safety net (several processes at once; with pysfizz: the planned set renders as the whole list does, and again the same), the plans kept, the build's order, every instrument the code names in the list, the .sfz reader (and, with pysfizz installed, that it agrees with sfizz), the pins, consent.txt, how many tracks and threads at once and when another may start (the launcher's choice too), the log shared with the render processes; with the render packages installed, a track's parts played in threads and taken in their order, the mix's arithmetic in place giving the same numbers, the Surge XT MIDI and state as before), `fake_generate.py` (progress lines for checking the launcher) |
| `make_manifest.py` | maintainers: makes `tgmusic/data/samples.tsv` (see below) |
| `tgmusic/jobs.py` | one track = one job, what it writes |
| `compare.py` | QA: compares two renders (length, loop tags, loudness, waveform / spectrum / chroma / onset similarity) |

What the repository holds about the game's music is only what the arrangements need to be
told, never notes: which SNES voice and instrument number plays which part in each song
(`arrange.ROLE_MAP`), which of the extracted samples are which drum (`parse_midi.DRUM_BY_SAMPLE`,
by tools/audio's sample file names), and each city's tune number. The melodies, bass lines,
chords and drum patterns are read from your ROM at run time.

## Licences

The generator's dependencies and libraries, what their licences allow, and the recommended
licence for this folder (GPL-3.0-or-later, because it uses pedalboard and loads GPL-3.0
plug-ins): [THIRD_PARTY.md](THIRD_PARTY.md).

## Maintainers

- Updating a pinned plug-in or SoundFont: change its URL in `tgmusic/libraries.py` (a plug-in
  has a file per system in `platform_files`), run once with `TGMUSIC_PIN=1` (it accepts the
  download and prints the `sha256` / `tree_sha256` to pin), pin them, render again to compare
  (`compare.py`), and run `generate.py --write-consent`.
- Adding or changing an instrument in the code, or a sample library's pin (its `commit`, or
  its release `zip` with `sha256` and `bytes`): run
  `python3 tools/music/make_manifest.py --cache DIR`. It finds every instrument the code names
  (`tgmusic/catalog.py`), lists each library's files at its pin without downloading them
  (GitHub's API, no token; a zip's central directory), reads each .sfz as sfizz does
  (`tgmusic/sfz.py`) for the samples it plays (reporting any missing or differing in case),
  adds the licence and readme files, fetches every file once into `DIR/libs` (the generator's
  layout: `generate.py --cache DIR` uses them) to take its SHA-256, and writes
  `tgmusic/data/samples.tsv`; then run `generate.py --write-consent`. The `music_tools` test
  fails while an instrument the code names is not in the list, or a form of naming one is
  not understood by `catalog.py`.
- After changing the arrangements or the list: `generate.py --rom ROM --plan-notes FILE
  --styles all --yes` (FILE stays out of the repository) prints what a build of each style
  downloads of each sample library: put it in `libraries.TYPICAL_MB` (the launcher's "usually
  about"), then `generate.py --write-consent`. Only these sizes go into the repository.
- Updating a Python package: its version and the SHA-256 of each of its wheels for Windows
  cp312 and Linux cp310-cp312 together in `requirements.txt`
  (`https://pypi.org/pypi/<name>/<version>/json`). The embeddable Python and pip are pinned in
  `setup_tools.py` and, for the launcher, `src/music_build.h` (the test checks they agree).
- `compare.py a.ogg b.ogg` prints how alike two renders are.
