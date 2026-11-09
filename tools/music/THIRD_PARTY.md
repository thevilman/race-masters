# tools/music: third-party licences

The generator ships **no** third-party files: the release downloads carry only this project's
code (`music-tools/`: these scripts, tools/audio, and `tg_audio`, built from our source and
LakeSnes). The instrument libraries and plug-ins below are downloaded by `generate.py` on the
player's computer, from each project's official source, at a pinned version with a pinned
SHA-256 (`tgmusic/libraries.py`), into a cache folder outside the repository. Of the sample
libraries (and Surge XT's patches) only files the generator can play are downloaded, with the
libraries' licence and readme files, every one checked against its own pinned SHA-256: the
list (`tgmusic/data/samples.tsv`, at most 3.2 GB instead of the libraries' 10 GB; samples
that only sound with a controller the generator never moves, such as a piano's sustain-pedal
samples or a drum kit's microphones that are off by default, aren't in it) is what the player
agrees to, and a build downloads of it only the samples the notes of its tracks can play,
worked out on the player's computer from their ROM's notes before anything is downloaded
(`tgmusic/needs.py`): usually about 1.0 GB for every style. The Python the
tools run on and its packages are installed by `setup_tools.py` (the launcher's Build music
runs it) into a per-user folder: the python.org embeddable Python on Windows, pip and the
packages from PyPI, every file pinned with its SHA-256 (`setup_tools.py`, `requirements.txt`,
installed with `--require-hashes`). Nobody redistributes any of them: not this repository,
not its release downloads. Nothing is downloaded before the player agrees to the list (the
launcher shows it, with these licences; `generate.py` prints it and asks).

What the generator does with them: it plays notes (read from the player's own ROM) on the
instruments and effects and writes the result as audio files on the same computer. The
licences below all allow that. (Reviewed on the pinned versions; not legal advice.)

## Instrument libraries and plug-ins (downloaded at run time)

| Library (pinned) | Licence | Official source | What the licence allows here | Styles |
|---|---|---|---|---|
| Surge XT 1.3.4, VST3 for Windows x64 or Linux x86-64 (the "plugins only" archives) + the 18 factory patches played, from the source repository at the release's tag `release_xt_1.3.4` (the same files as the release's content pack), with its LICENSE and AUTHORS | GPL-3.0-or-later | <https://github.com/surge-synthesizer/releases-xt/releases/tag/1.3.4>, <https://github.com/surge-synthesizer/surge/tree/release_xt_1.3.4> | Anyone may run the plug-in for any purpose; the audio it makes is the user's (the GPL covers the program, not its output). Loaded into the generator's own process through pedalboard; see "The generator's own licence" below. | faithful, synthwave, A, B, C, D |
| Dragonfly Reverb 3.2.10, VST3 for Windows x64 or Linux x86-64 | GPL-3.0-or-later | <https://github.com/michaelwillis/dragonfly-reverb/releases/tag/3.2.10> | As Surge XT: run for any purpose; the reverberated audio is not covered. | all |
| GeneralUser GS 2.0.3 (SoundFont), by S. Christian Collins, at commit `684543d5` | GeneralUser GS License v2.0 | <https://github.com/mrbumpy409/GeneralUser-GS> (the author's repository "for automated packaging"; site <https://www.schristiancollins.com/generaluser>) | "Use GeneralUser GS without restriction for your own music creation, private or commercial"; the samples "allow full use in music production". The author asks websites not to link straight to his own download files: the generator downloads from his GitHub repository, not his site. | A, B, C, D |
| Sonatina Symphonic Orchestra (SFZ, peastman/sso at `32bbdb16`), original samples by Mattias Westlund | Creative Commons Sampling Plus 1.0 | <https://github.com/peastman/sso> | Sampling the library into new works is allowed (not for advertising); attribution if such a work is shared. No redistribution of the library here. | faithful, orchestral, A, B |
| Karoryfer Big Rusty Drums 1.100 (SFZ; the kit played and its files read out of the release zip with range requests) | CC0-1.0 | <https://github.com/sfzinstruments/karoryfer.big-rusty-drums/releases/tag/v1.100> | Anything (public-domain dedication). | faithful, A, B |
| VSCO 2 Community Edition (SFZ, at `6dd651d5`), Versilian Studios | CC0-1.0 | <https://github.com/sgossner/VSCO-2-CE> | Anything. | C, D |
| Versilian Community Sample Library (SFZ, at `dfcf4a49`) | CC0-1.0 | <https://github.com/sgossner/VCSL> | Anything. | C, D |
| MTG Solo Saxophones (SFZ by kinwie from the MTG samples on freesound, at `b494d256`) | CC-BY-4.0 | <https://github.com/sfzinstruments/MTG.SoloSax> | Any use, including in rendered music; credit the source if a render is shared. `generate.py` writes `CREDITS.txt` in the output folder. | C, D |
| Wilkinson Audio Naked Drums (SFZ by kinwie, at `407732a5`) | CC-BY-4.0 | <https://github.com/sfzinstruments/WilkinsonAudio.NakedDrums> | As above. | C, D |

What may be downloaded of each (`tgmusic/data/samples.tsv`, made by `make_manifest.py` from
the instruments the code names), and what a build of every style downloads with the notes of
Top Gear (USA) (measured; `libraries.TYPICAL_MB` has it per style). The plug-ins, the
SoundFont and Surge XT's patches are downloaded whole:

| Library | Instruments named (played) | Files listed (downloaded) | MB at most (downloaded; the whole library's archive, compressed) |
|---|---|---|---|
| Surge XT patches | 18 patches | 21 (21) | 0.6 (0.6; the content pack: 253) |
| Sonatina Symphonic Orchestra | 19 (18) | 591 (314) | 232 (131; 1,380) |
| Big Rusty Drums | 1 kit | 1,075 (195) | 154 (9.5; the zip: 620) |
| VSCO 2 CE | 38 (27) | 1,230 (447) | 957 (286; 2,300) |
| VCSL | 9 (7) | 644 (205) | 1,251 (323; 4,050) |
| MTG Solo Saxophones | 4 (3) | 780 (182) | 111 (54; 105) |
| Naked Drums | 1 kit | 1,501 (731) | 359 (133; 1,290) |

Not used any more (the original tools downloaded them for a dropped rock style and for
plug-ins that were never used): Karoryfer Black and Green Guitars, Karoryfer Meatbass, BYOD.

The rendered music itself is an arrangement of the game's compositions (music by Barry
Leitch): whatever the instrument licences allow, the renders are for the player's own use
and must not be shared or published.

## The Python the tools run on (downloaded at run time by `setup_tools.py` or the launcher)

| What (pinned) | Licence | Official source | Notes |
|---|---|---|---|
| Python 3.12.10, Windows embeddable package (x64), Windows only | PSF License 2.0 | <https://www.python.org/downloads/release/python-31210/> (`python-3.12.10-embed-amd64.zip`, SHA-256 `4acbed6d…63025a3c3`; its MD5 matches the one python.org publishes) | Unpacked into `%LOCALAPPDATA%\race-masters\music-tools\python`; used only to run the tools. Linux uses the system's Python 3.10-3.12 in a venv |
| pip 25.2 | MIT | <https://pypi.org/project/pip/25.2/> (the wheel, SHA-256 pinned) | Installs itself from its wheel (no get-pip.py, no ensurepip) |

Not downloaded by the tools: the **Microsoft Visual C++ Redistributable (x64)** (MSVCP140.dll),
which several Windows wheels below need (numba, llvmlite, pedalboard, tinysoundfont) and most
PCs already have. If it's missing, the launcher and `setup_tools.py` say so and point to
Microsoft's own download (<https://aka.ms/vs/17/release/vc_redist.x64.exe>); the player installs
it, under Microsoft's licence.

## Python packages (installed by `setup_tools.py` from PyPI, `requirements.txt`)

Every one has wheels for Windows x64 (CPython 3.12) and Linux x86-64 (CPython 3.10-3.12),
installed as wheels only (`--only-binary=:all:`), each checked against its pinned SHA-256
(`--require-hashes`), without resolving dependencies (`--no-deps`: the list is complete).

| Package | Version | Licence | Notes |
|---|---|---|---|
| numpy | 1.26.4 | BSD-3-Clause | wheel bundles OpenBLAS (BSD-3-Clause) and libgfortran (GPL-3.0 with the GCC Runtime Library Exception) |
| scipy | 1.14.0 | BSD-3-Clause | same bundled libraries as numpy |
| numba | 0.60.0 | BSD-2-Clause | |
| llvmlite | 0.43.0 | BSD-2-Clause | bundles LLVM (Apache-2.0 with LLVM exceptions) |
| mido | 1.3.3 | MIT | |
| packaging | 24.1 | Apache-2.0 OR BSD-2-Clause | |
| **pedalboard** | 0.9.26 | **GPL-3.0** | includes JUCE and the VST3 SDK under GPL-3.0; hosts the Surge XT and Dragonfly plug-ins |
| pyloudnorm | 0.2.0 | MIT | |
| soundfile | 0.14.0 | BSD-3-Clause | wheel bundles libsndfile (LGPL-2.1-or-later) |
| cffi | 2.0.0 | MIT | |
| pycparser | 2.21 | BSD-3-Clause | |
| typing_extensions | 4.15.0 | PSF-2.0 | |
| pysfizz | 0.1.3 | BSD-2-Clause | sfizz (BSD-2-Clause) |
| tinysoundfont | 0.3.7 | MIT | TinySoundFont (MIT); its PyAudio dependency (live playback, unused) is left out |
| imageio-ffmpeg | 0.6.0 | BSD-2-Clause | its wheel carries an ffmpeg 7.0.2 binary built under **GPL-3.0-or-later**; the generator runs it as a separate program to encode Ogg Vorbis |
| pytheory | 0.57.13 | MIT | music theory helpers (style B) |
| rich, markdown-it-py, mdurl | 15.0.0, 3.0.0, 0.1.2 | MIT | pytheory's dependencies |
| Pygments | 2.18.0 | BSD-2-Clause | rich's dependency |
| sounddevice | 0.5.6 | MIT | pytheory's dependency (not imported by the generator) |

tools/audio's `tg_audio` (step 1): the release downloads carry it ready-made
(`music-tools/tg_audio[.exe]`, built by the project's CMake build from `tools/audio/tg_audio.c`
and the repository's own LakeSnes copy, MIT, whose licence is in the downloads' `licenses/`).
In a source checkout without one, the generator builds it with CMake and a C compiler.

## The generator's own licence

Recommended: **GPL-3.0-or-later** for `tools/music/`.

- The generator `import`s pedalboard (GPL-3.0) and loads two GPL-3.0 plug-ins (Surge XT,
  Dragonfly Reverb) into its own process. The FSF reads a program combined with GPL code in
  one process as one work, so code written to run that way is safest under a licence that is
  GPL-3.0 compatible, and GPL-3.0-or-later is the simple choice. (It does not matter that we
  ship only our source and the player installs pedalboard: the question is what the combined
  program is.)
- GPL-3.0-or-later is compatible with every other dependency above (BSD, MIT, Apache-2.0,
  PSF, LGPL-2.1, CC0 data).
- The ffmpeg binary runs as a separate program and the instrument libraries are data, so
  neither adds a requirement.
- It does not reach the rest of the repository: the game neither links nor imports any of
  this. It only reads the Ogg files the generator writes, as it would read any audio file, so
  the game's own licence is unaffected.
- The licence covers the code, not the music it renders (which is the player's private
  arrangement of the game's compositions, see above).

To adopt it: add the GPL-3.0 text as `tools/music/LICENSE` (and an
`SPDX-License-Identifier: GPL-3.0-or-later` line in the files, if wanted).
