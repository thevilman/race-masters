#ifndef TG_MUSIC_BUILD_H
#define TG_MUSIC_BUILD_H

// The launcher's "Build music" (docs/AUDIO.md, tools/music/README.md): what it knows about a
// build of the modern music it runs on the player's computer, without SDL so the tests can
// check it (tests/test_music_build.c). src/launcher.c runs the processes and draws the card.
//
// A build is up to three programs in turn, each sending progress as one flat JSON object a
// line on its standard output (tools/music/tgmusic/progress.py), and stopping cleanly on a
// "cancel" line or the end of its standard input:
//   1. Windows only: the python.org embeddable Python, fetched by the launcher itself
//      (curl.exe and tar.exe, which Windows 10 and 11 include), SHA-256 checked;
//   2. tools/music/setup_tools.py: pip and the pinned packages, hash-checked;
//   3. tools/music/generate.py: the notes from the ROM, the plan (the notes each instrument of
//      each track plays), the libraries (only the samples those notes can play), the tracks.
// Nothing is downloaded before the player agrees (to at most every file the styles may play;
// a build usually takes about a third of that), and every finished part is kept when a build
// stops: the next one goes on from there.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// The python.org embeddable package the launcher fetches on Windows; tools/music/setup_tools.py
// pins the same (the music_tools test checks they agree).
#define TG_MB_PY_VERSION "3.12.10"
#define TG_MB_PY_URL "https://www.python.org/ftp/python/3.12.10/python-3.12.10-embed-amd64.zip"
#define TG_MB_PY_SHA256 "4acbed6dd1c744b0376e3b1cf57ce906f9dc9e95e68824584c8099a63025a3c3"
#define TG_MB_PY_BYTES 11133606
#define TG_MB_PY_PTH_NAME "python312._pth"
#define TG_MB_PY_PTH "python312.zip\n.\nLib\\site-packages\nimport site\n"
#define TG_MB_PY_STAMP "race-masters-python.txt"
// Several of the packages' Windows wheels need MSVCP140.dll, which Windows doesn't include.
#define TG_MB_VCREDIST_URL "https://aka.ms/vs/17/release/vc_redist.x64.exe"

#define TG_MB_TRACKS 149  // 7 songs x 3 styles + 32 cities x 4 styles

// The styles, as generate.py --styles names them, and as the launcher shows them.
enum { TG_MB_STYLE_COUNT = 7, TG_MB_STYLES_ALL = (1 << TG_MB_STYLE_COUNT) - 1 };
extern const char* const kTgMbStyleKeys[TG_MB_STYLE_COUNT];   // "faithful", ..., "D"
extern const char* const kTgMbStyleNames[TG_MB_STYLE_COUNT];  // "Faithful", ..., "Race"
// The tracks a choice of styles makes (the faithful title is always made).
int tg_mb_style_tracks(unsigned mask);
// generate.py's --styles value: "all", or e.g. "faithful,D".
void tg_mb_styles_arg(unsigned mask, char* out, size_t n);

// --- One progress line --------------------------------------------------------------------
// A flat JSON object: string, number, true / false / null values (no nesting).
typedef struct {
  char key[24];
  char str[400];  // the value as text (numbers too); non-ASCII characters as '?'
  double num;
  bool isNum;
} TgMbField;

typedef struct {
  TgMbField f[24];
  int n;
} TgMbLine;

bool tg_mb_parse_line(const char* line, TgMbLine* out);
const char* tg_mb_str(const TgMbLine* l, const char* key);  // NULL if absent
double tg_mb_num(const TgMbLine* l, const char* key, double def);

// Splits a child's output into lines (the bytes come in pieces).
typedef struct {
  char buf[4096];
  size_t len;
  bool overlong;  // dropping the rest of a line too long to keep
} TgMbLines;

void tg_mb_lines_push(TgMbLines* s, const char* data, size_t n, void (*fn)(const char* line, void* user), void* user);

// --- A build ------------------------------------------------------------------------------

typedef enum {
  TG_MB_IDLE,        // nothing running
  TG_MB_RUNNING,
  TG_MB_CANCELLING,  // asked to stop; waiting for it to
  TG_MB_STOPPED,     // stopped by the player: what was done is kept
  TG_MB_FAILED,
  TG_MB_FINISHED,
} TgMbPhase;

typedef enum {
  TG_MB_STEP_NONE,
  TG_MB_STEP_TOOLS,      // Python and its packages (and, on Windows, Python itself)
  TG_MB_STEP_NOTES,      // the notes from the ROM
  TG_MB_STEP_PLAN,       // the plan: what each track's instruments play, so what to download
  TG_MB_STEP_LIBRARIES,  // the instrument libraries (the samples the plan needs)
  TG_MB_STEP_RENDER,     // the tracks
  TG_MB_STEP_COUNT
} TgMbStep;

typedef struct {
  TgMbPhase phase;
  TgMbStep step;
  char stepText[160];  // what the step is doing
  char detail[240];    // more about it: the library, sizes, speed; the track
  double fraction;     // of the step, 0-1 (negative: unknown)
  double etaS;         // time left for the whole build (negative: unknown)
  int tracksDone, tracksTotal, tracksFailed;
  int downloadMb, downloadMaxMb;  // the download: usually / exactly once planned, and at most (-1: not said)
  bool planned;                   // the plan is made: downloadMb is exact
  char errorCode[24];  // network, hash, disk, vcredist, python, syslibs, tools, tracks, consent, other
  char errorText[400];
  char logPath[400];
  char python[1024];  // the Python setup_tools.py made ("ready")
  char lastPlain[240];  // the last line that wasn't progress (for an unexpected end)
  bool sawCancelled, sawDone, sawReady;
} TgMbState;

void tg_mb_reset(TgMbState* s);
// A build starts; etaS: the estimate shown until the generator sends its own.
void tg_mb_begin(TgMbState* s, double etaS);
// A line of a child's output.
void tg_mb_feed(TgMbState* s, const char* line);
// The launcher was asked to stop the build.
void tg_mb_cancel(TgMbState* s);
// A child ended, its output all fed. last: it was the build's last program (generate.py).
// Returns true when the build goes on with the next program. What happened is read from its
// last lines ("ready" / "done", "error", "cancelled"), not from exitCode: on Windows the
// launcher starts its children without a console window, and SDL then reports no exit code
// (pass TG_MB_EXIT_UNKNOWN). A program that ends without saying either way failed.
#define TG_MB_EXIT_UNKNOWN (-100000)
bool tg_mb_child_exited(TgMbState* s, int exitCode, bool last);
// A failure the launcher itself found (the Python download, a program that won't start).
void tg_mb_fail(TgMbState* s, const char* code, const char* text);
// The step number shown ("Step 2 of 5"), 1-based, and the steps there are.
int tg_mb_step_number(TgMbStep step);
#define TG_MB_STEPS 5

// --- Words and numbers for the card -------------------------------------------------------
void tg_mb_format_eta(double seconds, char* out, size_t n);  // "about 3 h 10 min left", "" if unknown
void tg_mb_format_size(double bytes, char* out, size_t n);   // "1.9 GB", "350 MB"
void tg_mb_format_hours(double seconds, char* out, size_t n);  // "about 6 hours", "about 40 minutes"
// A rough time for a whole build, for before it starts: tracks to render (jobs at once), MB to
// download, and whether the notes are still to be read. trackS / notesS / rateMbs: consent.txt's.
double tg_mb_estimate_seconds(int tracks, int jobs, double downloadMb, bool notes, int trackS, int notesS, int rateMbs);
// Tracks to render at once (generate.py --jobs): half the logical processors, as many as the
// memory holds (up to ~1.6 GB each); the generator runs below normal priority.
int tg_mb_auto_jobs(int ramMb, int cpus);

// --- What the launcher remembers (launcher.txt in the settings folder) ---------------------
// "key value" lines; "rom" stays the first, as earlier launchers read only that line.
typedef enum { TG_MB_OFFER_ASK = 0, TG_MB_OFFER_LATER = 1, TG_MB_OFFER_NEVER = 2 } TgMbOffer;

typedef struct {
  char rom[2048];
  int offer;          // TgMbOffer: the first-run offer to build the modern music
  unsigned styles;    // the styles to build (default all)
  bool cleanAfter;    // delete the instrument libraries once every track is made
} TgLauncherPrefs;

void tg_prefs_defaults(TgLauncherPrefs* p);
void tg_prefs_parse(TgLauncherPrefs* p, const char* text);
size_t tg_prefs_write(const TgLauncherPrefs* p, char* out, size_t n);

// Offer to build the modern music now? Once a session, with a verified ROM, the tools there,
// nothing built and nothing building, unless the player said not to ask again.
bool tg_mb_should_offer(bool romOk, bool toolsFound, int tracksBuilt, bool running, int offer, bool shownThisSession);

// --- tools/music/tgmusic/data/consent.txt: what a build downloads, under which licences ----
typedef struct {
  char id[32];
  char name[120];
  char licence[64];
  char homepage[96];
  int mbWindows, mbLinux;
} TgMbItem;

typedef struct {
  int tracks;
  TgMbItem libs[16];  // mbWindows / mbLinux: the most a build downloads of each (every file listed)
  int nLibs;
  unsigned styleLibs[TG_MB_STYLE_COUNT];  // the libraries (bits, libs[] order) each style plays on
  int typical[16][TG_MB_STYLE_COUNT];     // MB a build of one style usually downloads of each (-1: all)
  TgMbItem tools;
  int libsMb, toolsMb, musicMb, peakMb;  // all styles: libraries unpacked (at most), tools, music, the most at once
  int trackS, notesS, rateMbs;
} TgMbConsent;

bool tg_mb_parse_consent(const char* text, TgMbConsent* c);
// The libraries (bits) a choice of styles needs (the faithful title's always).
unsigned tg_mb_consent_libs(const TgMbConsent* c, unsigned styles);
// Downloads in MB for those libraries, at most (every file listed), with the tools or not.
int tg_mb_consent_download_mb(const TgMbConsent* c, unsigned libs, bool tools, bool windows);
// What a build of a choice of styles usually downloads, in MB, with the tools or not: of each
// library the most one of the styles (or the faithful title) takes ("typical"), else all of it.
int tg_mb_consent_typical_mb(const TgMbConsent* c, unsigned styles, bool tools, bool windows);
// The same for one library (index into libs; 0 when the choice doesn't use it).
int tg_mb_consent_lib_typical_mb(const TgMbConsent* c, int lib, unsigned styles, bool windows);
// Disk in MB for a choice: kept after the build, usually (libraries, tools, music), and the most
// free space it may need at once (with every listed file).
void tg_mb_consent_disk(const TgMbConsent* c, unsigned styles, bool windows, int* keptMb, int* peakMb, int* musicMb);

#endif
