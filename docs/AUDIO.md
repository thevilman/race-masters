# Modern music in the game

The player's remade songs play in place of the sound chip's music; the engine, crashes,
skids and every other effect still come from the sound chip. Presentation only: the game
runs exactly as before (both regressions pass with the music on; WRAM and picture hashes
are the same with every music set).

Code: `src/audio/music.c` (song changes, file choice, streaming, mixing),
`third_party/lakesnes/dsp.c` (`dsp_setOutputMask`: voices left out of the output),
`src/game.c` (`tg_game_set_apu_observer`, `tg_game_set_voice_mute`), `src/main.c`
(option, settings, the folder, mixing into the SDL stream), `src/launcher.c` (option, the
Music card that builds the music), `src/music_build.c` (the build's state, progress lines,
`launcher.txt`), `tools/tg_headless.c` (`--music*` test options). Decoder:
`third_party/stb_vorbis` (v1.22, MIT or public domain).

## The player's folder

The files stay in the player's folder, never in git, the release downloads or anywhere
online (they are arrangements of the game's compositions). Each player makes them on their
own computer from their own ROM, with the launcher's **Build music** (or by hand,
tools/music/README.md): the notes come from the ROM through `tools/audio`'s `tg_audio`
(which the release downloads carry ready-made in `music-tools/`), the instruments are free
libraries downloaded from their official sources, each file SHA-256 checked: of the files the
styles may play (`tools/music/tgmusic/data/samples.tsv`, at most 3.2 GB, what the player
agrees to), only the samples the notes of the player's tracks can play, worked out on their
computer from those notes before anything is downloaded (usually about 1.0 GB). The styles are
rendered on the player's computer.

The game looks for them in `audio-modern/` next to the game (`race-masters.exe`), then in
`audio-modern/` in the per-user data folder (`%LOCALAPPDATA%\race-masters\`,
`~/.local/share/race-masters/`; `src/userdir.h`), where the launcher builds them when the
game's folder can't be written to (say, under Program Files): the first that has the
faithful title is used. Paths are opened as UTF-8 (Windows: `_wfopen`), so a user name with
accents is fine.

### Build music (the launcher)

The Music card under the options shows where things are: original music only, building
(the step, a progress bar, the time left), stopped, a problem (what, with Retry), or ready
(N of 149 tracks). Build music, after the player agrees to the downloads (what, from where,
under which licence, how much disk and time; the styles, all by default), runs in turn:

1. Windows only: the python.org embeddable Python 3.12 (pinned, SHA-256 checked), fetched by
   Windows' `curl.exe` into a file the launcher opens and unpacked by the launcher
   (`src/unzip.c`), into `music-tools\python`. Before that it checks for MSVCP140.dll (the
   Microsoft Visual C++ Redistributable, which several packages need) and, if it's missing,
   says so and links to Microsoft's download.
2. `tools/music/setup_tools.py`: pip from its pinned wheel, then every package from
   `requirements.txt` with `--require-hashes` (wheels only): into that Python on Windows, into
   a venv from the system's `python3` on Linux.
3. `tools/music/generate.py --styles … --jobs N --yes --progress-json`, in four stages: the
   notes from the ROM; the plan (each track arranged without rendering, for the notes each of
   its instruments plays, kept in the music tools' cache, never with the music); the
   libraries (of the instrument files the chosen styles may play, only the samples those
   notes can play, fetched one by one, six at a time; the exact size is shown once planned);
   the tracks (up to N at once, from the computer: half its processor threads, as many as its
   memory holds at about 1.6 GB a track with 1.5 GB left; beyond a third of that (at least two)
   the generator starts one only when the memory free then holds it; it runs below normal
   priority, so the game comes first; a track's Surge XT parts play at once in threads). A
   sample a track needs
   but the plan missed is fetched just before its instrument loads (logged; it shouldn't
   happen). The card shows the five steps (the tools, then these four).

Each sends its progress as one JSON object a line (`tools/music/tgmusic/progress.py`);
the launcher reads them without blocking and judges the end from the last ones
(`done` / `ready`, `error`, `cancelled`), since on Windows it starts them without a console
window, which leaves SDL without an exit code. Cancel writes `cancel` to the program's input
and closes it; the program stops its render processes (Windows: a job object; Linux: process
groups) and keeps everything finished: tracks (each written under a temporary name, then
renamed; its `.json` last), the notes and the tracks planned (a resumed build plans only
what it hasn't), downloads (each instrument file kept once checked, a whole archive resumed
with HTTP ranges; a later build of more styles fetches only what it adds), unpacked plug-ins.
Closing
the launcher mid-build asks first, then does the same. Start while it builds runs the game
and minimizes the launcher, which goes on looking after the build (the build runs below
normal priority). The first-run offer (Build now / Later / Don't ask again) and the choices
are kept in `launcher.txt` in the settings folder (`music_offer`, `music_styles`,
`music_clean`).

What the folder holds:

| Folder | What | Files |
|---|---|---|
| `variations/NN_song/` | the 7 songs in 3 styles (`faithful`, `synthwave`, `orchestral`): 21 files | `NN_song__STYLE.{ogg,json}` |
| `stages/COUNTRY/` | per city, **A** "Postcard" (faithful + local instrument layers) and **B** "Local band" (local instruments, a faithful v1/v2/v3 re-arrangement as the base: reharmonised, re-grooved or modal lift) | `COUNTRY__CITY__A`, `__B` |
| `stages-band/COUNTRY/` | per city, **C** "Band": a local genre band covers the tune at its own tempo | `COUNTRY__CITY__C` |
| `stages-race/COUNTRY/` | per city, **D** "Race": the C band at racing speed, 140-176 BPM, with a racing drive under it | `COUNTRY__CITY__D` |
| `CREDITS.txt` | the instrument libraries used and their licences | |

The generator makes faithful + D by default (`--styles all` for everything). All files
(128 city files, 32 cities x A-D, and 21 variations): 48 kHz stereo, -14 LUFS; the Ogg
files carry `LOOPSTART` / `LOOPLENGTH` comments (in samples), the WAVs (`--formats
ogg,wav`) a `smpl` loop; every song loops from its start except Qualified (plays once, no
loop tags). Countries and cities are named as `usa / las_vegas`, `south_america / machu_picchu`, ... in the game's
course order (four a country: USA, South America, Japan, Germany, Scandinavia, France,
Italy, UK); `tg_headless --music-courses` prints the ROM's 32 course names beside this
table (all 32 match).

## The options

Music, in the Tab menu and the launcher (Sound), kept in `settings.txt` as `music N`;
`topgear --music KEY` too. The player asked: "the original, the faithful, and some other
variations that are city based ... default is going to be faithful, we can set on options
the original, and the D as modern version".

| N | Option (key) | Title, Qualified, Ending | Race songs |
|---|---|---|---|
| 0 | ORIGINAL (`original`) | the sound chip | the sound chip |
| 1 | **FAITHFUL** (`faithful`, the default) | `variations/…__faithful` | `variations/…__faithful` |
| 2 | MODERN (`modern`) | faithful | the city's **D** (`stages-race/`) |
| 3 | CITY BAND (`band`) | faithful | the city's **C** (`stages-band/`) |
| 4 | LOCAL BAND (`local`) | faithful | the city's **B** (`stages/`) |
| 5 | POSTCARD (`postcard`) | faithful | the city's **A** (`stages/`) |
| 6 | SYNTHWAVE (`synthwave`) | `…__synthwave` | `…__synthwave` |
| 7 | ORCHESTRAL (`orchestral`) | `…__orchestral` | `…__orchestral` |

The cities have no title / Qualified / Ending of their own, so the city sets play the
faithful ones there. The `__v1`-`__v3` re-arrangements are not options of their own (the B
city versions are built on them). A file missing falls back to faithful, then to the
sound chip; `.ogg` is used, else `.wav` (MP3 and FLAC are not read: every file has an Ogg).
Without the folder the menu says "(not built: see launcher)" and the original plays.

Which song plays where (the game's own rule, `tools/audio/HANDOFF.md`): the title tune on
the title and every menu; a race plays the tune of the course's place in its country (1st
Las Vegas tune, 2nd Hiroshima, 3rd Bordeaux, 4th Frankfurt) and it goes on over that
race's results; Qualified when a country is won; Ending after the last country. The city
version is the course's own (`$1F06` bits 0-4 at the song's start), used only when that
course's tune is the one starting.

## Song changes

One read-only observer on the game's writes to the sound ports `$2140-$2143` (every port
mode: the original code and the C code both write through `snes_write`), chained into the
write hook `src/game.c` already has:

- `$18` on port 1: the driver mutes and returns to the loader before every upload: the
  music stops (40 ms fade).
- Port 0 = song 1-7 (upload set + 1) **after** a 0 on port 0 and a gap of over a million
  master cycles: the song starts. During an upload port 0 carries the loader's byte
  counters a few microseconds apart; the song number comes after the upload's last byte
  and a ~65,536-loop delay (seven frames). Port 1 bytes during an upload are data, ignored.
- `$19` on port 1 (pause, and the moment a race is over): the driver halts until port 1
  changes, so the stream pauses and resumes on the next command.
- `$01` on port 1 (race end) only silences effects: the race song goes on over the results,
  as in the game.

The power-on boot (`$00:8064`, still emulated), `MenuStartMusic` (`$0F:8133`) and the race
program (`$00:8175`) are all seen this way.

## Playing a file

When a file is chosen, the sound chip's music voices are left out of what is heard: voices
0-3 in races (4-7 are the engine and effects of both players), all 8 for the title,
Qualified and Ending (they use all 8 and have no effects). The mask is set at the very
port write that starts the song, so no frame of the original leaks. The DSP computes
every voice, its echo buffer in APU RAM and every register exactly as before; only the
output samples change, and the kept voices' echo comes from a shadow echo buffer outside
APU RAM fed by them alone (`dsp.c`).

The file is opened after the frame (stb_vorbis pull API, or a WAV reader for 16/24/32-bit
PCM and float), streamed in 1024-frame blocks, looped by seeking to the loop start at the
loop end (sample-accurate), resampled linearly if not 48 kHz, and added to the chip's
samples before they go to SDL. A frame dropped by the frontend's latency guard drops no
music (the stream moves only with what is heard). Changing the option mid-song crossfades
(150 ms) to the new set's file from its start; to ORIGINAL the chip's music (which kept
running underneath) is heard at once.

Volume: the remakes are mastered to -14 LUFS; each song gets the gain that brings it to
the original song's loudness on the sound chip (ITU-R BS.1770, measured on the player's
`audio/music/*.wav` rips: title -17.2, Las Vegas -12.9, Hiroshima -19.0, Bordeaux -20.3,
Frankfurt -17.4, Qualified -11.2, Ending -11.6 LUFS), capped at +1.5 dB, so the music
sits under the engine and effects as it did in the original. City versions of a tune get
that tune's gain.

## Checking it without the game

```
tg_headless --rom ROM --input REC --port native|verify|off --frames N \
  --music faithful|modern|... --music-dir ".../audio-modern" --music-log - \
  [--music-wav out.wav] [--music-switch FRAME:SET] [--music-courses] [--set-course F:N]
```

`--music-log` prints every start, stop, pause and resume with the frame, the song, the
course and city, the set, why the file was chosen and the file, then its rate, channels,
loop and gain (the file is opened and checked); `--music-wav` also writes what would be
heard (the chip with the masked voices plus the music, 48 kHz, as the frontend mixes it).

Checked (2026-11-03):

- race_1p (`--port off`): title at frame 84, stop at 3551, Las Vegas tune (course 0) at
  3594 = the frames `tools/audio/tg_audio log` shows for the port writes; `--port native`
  the same (60 / 3547 / 3590: the C menus skip a loading pause). WRAM and picture hashes
  unchanged with `--music` on (verify: the pinned hashes; native: identical with
  `original` and `faithful`).
- Render race_1p faithful vs original: on the title the output is the remake alone at
  -3.2 dB (residual 26 dB down = the Ogg coding; correlation with the chip's title 0.03:
  all 8 voices masked); in the race, removing the remake leaves the effects (engine), which
  are uncorrelated (0.002) with what is then left of the original render (the chip's
  music): the effects are kept, the music replaced. Race loudness -12.7 LUFS (original
  -11.9).
- Loop: 9000 frames on the title / attract: at the 123.794 s loop point the output
  continues as the file's start (residual -43 dB, no step larger than elsewhere).
- `--set-course 1900:9 --music modern --music-switch 4200:band`: Hiroshima tune, D
  `japan__hiroshima__D`, then at 4200 C `japan__hiroshima__C`.
- `--music-courses`: the 32 ROM course names = the city table.
- chris_session_01 (`--port off`, 54853 frames, `modern`): the USA tour, Las Vegas,
  Los Angeles, New York and San Francisco each with its own D, the title between and after;
  the run ends on the recording's own WRAM hash. chris_session_02 (verify): the same
  pattern.
- Pause (SELECT in a race, scripted on race_1p, native): `pause` at 4303, `resume` at 4601;
  the render is silent while paused (-200 dB) and the music goes on after.
- Not seen in any recording yet: Qualified and Ending (same path as the title: port 0 =
  6 / 7, all voices masked; Qualified opened without a loop).
