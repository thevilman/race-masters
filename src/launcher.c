// Launcher: choose your Top Gear (USA) ROM, set the optional enhancements, then start the game.
//
// Works with a mouse, the keyboard or a gamepad. The options are the ones the game keeps in
// settings.txt (in the settings folder), so the game starts with them and its own options
// menu (Tab) still changes them. The chosen ROM is remembered in launcher.txt there.
//
// The Music card builds the optional modern music on this computer, from the player's own ROM
// (src/music_build.h, tools/music): after the player agrees, the launcher runs the music tools
// as child processes, shows their progress, and stops them cleanly on Cancel or when it closes
// (what is done is kept; the next build goes on from there). Nothing of the music is ever
// downloaded: only free instrument libraries and the tools, from their official sources.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "brand.h"
#include "launcher_art.h"
#include "music_build.h"
#include "rom.h"
#include "sha256.h"
#include "unzip.h"
#include "userdir.h"

#ifdef _WIN32
#define SEP "\\"
#else
#define SEP "/"
#endif

#define VIEW_W 1280
#define VIEW_H 720
#define TEXT 16  // 8x8 debug font drawn at twice its size


// --- Options -------------------------------------------------------------------------

typedef enum { OPT_BOOL, OPT_CHOICE, OPT_FULLVIEW } OptKind;

typedef struct {
  const char* group;  // heading shown above the first option of a group
  const char* label;
  const char* key;  // name in settings.txt
  OptKind kind;
  const char* choices[8];
  int count;
  const char* help;
  const char* hotkey;
} Option;

enum {
  O_FULLSCREEN, O_ASPECT, O_FILTER, O_INTEGER, O_COLOUR,
  O_WIDE, O_SIDE, O_FULLVIEW, O_MAP, O_SPEED,
  O_SMOOTH, O_HIGHFPS, O_GHOST, O_AUTOSAVE, O_COUNTRY, O_RUMBLE, O_MUSIC, O_COUNT
};

static const Option kOptions[O_COUNT] = {
  {"Display", "Fullscreen", "full_screen", OPT_BOOL, {"Off", "On"}, 2,
   "Start in fullscreen. Esc leaves it while you play.", "F11"},
  {NULL, "Aspect", "aspect", OPT_CHOICE, {"4:3 TV", "8:7 pixels"}, 2,
   "4:3 is the shape a TV showed. 8:7 keeps the SNES pixels square.", "F2"},
  {NULL, "Filter", "filter", OPT_CHOICE, {"Sharp", "Smooth", "Scanlines"}, 3,
   "Sharp pixels, sharp-bilinear (smooth edges without uneven pixels), or TV scanlines.", "F4"},
  {NULL, "Integer scaling", "integer_scale", OPT_BOOL, {"Off", "On"}, 2,
   "Scale the picture by whole numbers only, for perfectly even pixels.", "F3"},
  {NULL, "Colours", "tv_colour", OPT_CHOICE, {"Exact", "As on a TV"}, 2,
   "Exact SNES colours, or the deeper, richer colours a CRT TV showed.", "Tab menu"},
  {"Widescreen", "Widescreen", "wide_screen", OPT_BOOL, {"Off", "On"}, 2,
   "Races fill a wide screen: the road and scenery carry on past the original edges and the HUD moves out to the sides.", "F10"},
  {NULL, "2P side by side", "side_by_side", OPT_BOOL, {"Off", "On"}, 2,
   "With widescreen on, the two players' views sit side by side, each the full height of the screen.", "F12"},
  {"Full-screen view", "1P full view", "full_view", OPT_FULLVIEW, {"Off", "On", "Tall road"}, 3,
   "In 1-player races your view fills the whole screen instead of sharing it with the computer's. Tall road makes the road fill more of it.", "F9"},
  {NULL, "Map", "map_top", OPT_CHOICE, {"Bottom", "Top"}, 2,
   "Where the course map sits in full view: bottom left, or top left.", "Tab menu"},
  {NULL, "Speed and time", "speed_bottom", OPT_CHOICE, {"Top", "Bottom"}, 2,
   "Where the speedometer and timer sit in full view: top right, or the bottom right corner.", "Tab menu"},
  {"Motion", "Smooth motion", "smooth", OPT_BOOL, {"Off", "On"}, 2,
   "The race moves every frame instead of every third, so the road and cars glide instead of stepping.", "F6"},
  {NULL, "High frame rate", "high_fps", OPT_BOOL, {"Off", "On"}, 2,
   "On a 120 Hz or faster display, extra pictures are drawn between frames. Turns on smooth motion too.", "Shift+F6"},
  {"Extras", "Ghost car", "ghost", OPT_BOOL, {"Off", "On"}, 2,
   "Race against a see-through replay of your best race on each course.", "F1"},
  {NULL, "Auto-save", "auto_save", OPT_BOOL, {"Off", "On"}, 2,
   "Remember the countries you reach, so you don't need passwords.", "Shift+F1"},
  {NULL, "Country select", "country_free", OPT_CHOICE, {"Password", "Free"}, 2,
   "Free: pick any country on the country screen and its password is entered for you. Password: type it, as in the original.", "Tab menu"},
  {NULL, "Rumble", "rumble", OPT_BOOL, {"Off", "On"}, 2,
   "Gamepad rumble when your car hits something and when nitro fires.", "F8"},
  // The numbers are src/audio/music.h's TgMusicSet, as the game keeps them.
  {"Sound", "Music", "music", OPT_CHOICE,
   {"Original", "Faithful", "Modern", "City band", "Local band", "Postcard", "Synthwave", "Orchestral"}, 8,
   "Original: the SNES music. The others are the modern versions, made on this computer with Build music below: "
   "Faithful, Modern (each city's racing version), city band, local band, postcard, synthwave, orchestral.", "Tab menu"},
};

// Focus order: the ROM button, every option, the Music card's two buttons, then Play.
enum { F_ROM = 0, F_OPTION0 = 1, F_MUSIC1 = 1 + O_COUNT, F_MUSIC2, F_PLAY, F_COUNT };

// What the Music card's buttons do.
typedef enum { MA_NONE, MA_BUILD, MA_CANCEL, MA_RETRY, MA_VCREDIST, MA_LOG, MA_FOLDER } MusicAction;
// The programs of a build, in turn (src/music_build.h).
typedef enum { ST_NONE, ST_FETCH, ST_TOOLS, ST_GENERATE } Stage;
typedef enum { MODAL_NONE, MODAL_OFFER, MODAL_CONSENT, MODAL_QUIT } Modal;
// The consent panel's controls: the styles, "delete the libraries", agree, cancel.
enum { C_STYLE0 = 0, C_CLEAN = TG_MB_STYLE_COUNT, C_AGREE, C_CANCEL, C_COUNT };

static struct {
  SDL_Window* window;
  SDL_Renderer* renderer;
  SDL_Gamepad* pads[4];
  int value[O_COUNT];
  char settingsPath[2400];
  char launcherPath[2400];
  char extra[4096];  // settings.txt lines the launcher doesn't know, kept as they were
  char romPath[2048];
  bool romOk;
  char romStatus[160];
  int focus;
  int hover;  // option row under the mouse, or -1
  bool dialogOpen;
  SDL_Mutex* lock;
  char picked[2048];  // set by the file dialog's callback
  bool pickedReady;
  char message[256];  // a passing note at the bottom
  Uint64 messageUntil;
  bool messageGood;   // news rather than a problem
  float scroll;
  Uint64 heldSince;
  int heldDir;
  Uint64 nextRepeat;
  SDL_Texture* logo;  // the logo: launcher_art.h's, or a logo.bmp next to the launcher
  bool logoBmp;  // the logo is that logo.bmp
  SDL_Texture* playWord;  // "START", in the logo's chrome
  TgLauncherPrefs prefs;  // launcher.txt
  // The modern music.
  char dataRoot[1024];     // the per-user data folder (src/userdir.h), "" if none
  char toolsDir[1100];     // the music tools: a release's music-tools/, a checkout's tools/ ("" if not found)
  char tgAudio[1400];      // tools/audio's ready-made tg_audio, if found
  char musicHere[1400];    // audio-modern next to the game
  char musicUser[1400];    // audio-modern in the per-user data folder
  char musicDir[1400];     // where the player's music is (or goes)
  int tracks;              // finished tracks in musicDir
  TgMbConsent consent;
  bool consentOk;
  TgMbState mb;            // the build
  Stage stage;
  SDL_Process* proc;
  TgMbLines lines;
  Uint64 killAt;           // a cancelled program that hasn't stopped by then is ended
  bool quitWhenStopped;
  char buildOut[1400];     // where this build writes
  const char* fake;        // TG_MUSIC_FAKE: tools/music/tests/fake_generate.py's scenario
  Modal modal;
  int modalFocus;
  bool offerShown;
  unsigned consentStyles;
  bool consentClean;
} L;

// --- Settings ------------------------------------------------------------------------

static int optionIndex(const char* key) {
  for(int i = 0; i < O_COUNT; i++) if(strcmp(kOptions[i].key, key) == 0) return i;
  return -1;
}

static void loadSettings(void) {
  L.value[O_FILTER] = 0;
  L.value[O_MUSIC] = 1;  // faithful unless chosen
  L.value[O_COUNTRY] = 1;  // any country, its password entered for you, unless chosen
  char* text = SDL_LoadFile(L.settingsPath, NULL);
  if(text == NULL) return;
  bool tall = false;
  size_t used = 0;
  // Earlier launchers wrote "fullscreen" and "wide", which the game never read (it reads
  // "full_screen" and "wide_screen") and never writes: when one is there, it is the last
  // choice made here, so it wins over the game's line.
  int oldFull = -1, oldWide = -1;
  for(char* line = text; line && *line; ) {
    char* next = strchr(line, '\n');
    if(next) *next++ = 0;
    char name[32];
    int v;
    if(sscanf(line, "%31s %d", name, &v) == 2) {
      int i = optionIndex(name);
      if(i >= 0) L.value[i] = v < 0 ? 0 : v >= kOptions[i].count ? kOptions[i].count - 1 : v;
      else if(strcmp(name, "fullscreen") == 0) oldFull = v != 0;
      else if(strcmp(name, "wide") == 0) oldWide = v != 0;
      else if(strcmp(name, "tall_road") == 0) tall = v != 0;
      else if(used + strlen(line) + 2 < sizeof(L.extra)) used += (size_t) snprintf(L.extra + used, sizeof(L.extra) - used, "%s\n", line);
    }
    line = next;
  }
  if(L.value[O_FULLVIEW] && tall) L.value[O_FULLVIEW] = 2;
  if(oldFull >= 0) L.value[O_FULLSCREEN] = oldFull;
  if(oldWide >= 0) L.value[O_WIDE] = oldWide;
  SDL_free(text);
}

static void saveSettings(void) {
  if(!L.settingsPath[0]) return;
  FILE* f = fopen(L.settingsPath, "w");
  if(f == NULL) return;
  for(int i = 0; i < O_COUNT; i++) {
    if(i == O_FULLVIEW) fprintf(f, "full_view %d\ntall_road %d\n", L.value[i] != 0, L.value[i] == 2);
    else fprintf(f, "%s %d\n", kOptions[i].key, L.value[i]);
  }
  fputs(L.extra, f);
  fclose(f);
}

// launcher.txt: the ROM, and the music build's choices (src/music_build.h).
static void saveLauncher(void) {
  if(!L.launcherPath[0]) return;
  snprintf(L.prefs.rom, sizeof(L.prefs.rom), "%s", L.romPath);
  char text[3000];
  size_t n = tg_prefs_write(&L.prefs, text, sizeof(text));
  if(n) SDL_SaveFile(L.launcherPath, text, n);
}

static void loadLauncher(void) {
  tg_prefs_defaults(&L.prefs);
  char* text = L.launcherPath[0] ? SDL_LoadFile(L.launcherPath, NULL) : NULL;
  if(text) {
    tg_prefs_parse(&L.prefs, text);
    SDL_free(text);
  }
}

// --- ROM -----------------------------------------------------------------------------

static const char* baseName(const char* path) {
  const char* b = path;
  for(const char* p = path; *p; p++) if(*p == '/' || *p == '\\') b = p + 1;
  return b;
}

static bool checkRom(const char* path, bool remember) {
  TgRom rom;
  TgRomStatus status = tg_rom_load(path, &rom);
  if(status == TG_ROM_OK) {
    tg_rom_free(&rom);
    snprintf(L.romPath, sizeof(L.romPath), "%s", path);
    L.romOk = true;
    snprintf(L.romStatus, sizeof(L.romStatus), "Verified: Top Gear (USA)");
    if(remember) saveLauncher();
    return true;
  }
  if(remember) {
    // Keep showing a good ROM if one was already chosen; say what was wrong with this one.
    const char* why = status == TG_ROM_WRONG_HASH ? "That file isn't an unmodified Top Gear (USA) ROM."
                    : status == TG_ROM_WRONG_SIZE ? "That file is the wrong size for Top Gear (USA)."
                    : "That file couldn't be read.";
    snprintf(L.message, sizeof(L.message), "%s", why);
    L.messageUntil = SDL_GetTicks() + 6000;
    L.messageGood = false;
    if(!L.romOk) {
      snprintf(L.romPath, sizeof(L.romPath), "%s", path);
      snprintf(L.romStatus, sizeof(L.romStatus), "%s", why);
    }
  }
  tg_rom_free(&rom);
  return false;
}

static void findRom(void) {
  if(L.prefs.rom[0] && checkRom(L.prefs.rom, false)) return;
  static const char* names[] = {"Top Gear (USA).sfc", "Top Gear (USA).smc", "topgear.sfc"};
  const char* base = SDL_GetBasePath();
  for(size_t i = 0; i < SDL_arraysize(names); i++) {
    char path[2048];
    snprintf(path, sizeof(path), "%s%s", base ? base : "", names[i]);
    if(checkRom(path, false)) return;
  }
  L.romPath[0] = 0;
  snprintf(L.romStatus, sizeof(L.romStatus), "Choose your Top Gear (USA) ROM to play.");
}

static void SDLCALL onPicked(void* user, const char* const* files, int filter) {
  (void) user;
  (void) filter;
  SDL_LockMutex(L.lock);
  if(files && files[0]) {
    snprintf(L.picked, sizeof(L.picked), "%s", files[0]);
    L.pickedReady = true;
  }
  L.dialogOpen = false;
  SDL_UnlockMutex(L.lock);
}

static void chooseRom(void) {
  if(L.dialogOpen) return;
  static const SDL_DialogFileFilter filters[] = {{"SNES ROM", "sfc;smc"}, {"All files", "*"}};
  L.dialogOpen = true;
  SDL_ShowOpenFileDialog(onPicked, NULL, L.window, filters, 2, L.romOk ? L.romPath : NULL, false);
}

// --- Launching -----------------------------------------------------------------------

static bool launch(void) {
  if(!L.romOk) {
    snprintf(L.message, sizeof(L.message), "Choose your Top Gear (USA) ROM first.");
    L.messageUntil = SDL_GetTicks() + 4000;
    L.messageGood = false;
    L.focus = F_ROM;
    return false;
  }
  saveSettings();
  char exe[2400];
  const char* base = SDL_GetBasePath();
  snprintf(exe, sizeof(exe), "%s%s", base ? base : "", TG_GAME_EXE);
  const char* args[] = {exe, L.romPath, NULL};
  // No standard input or output: the launcher is a windowed program and has none to pass
  // on (on Windows, asking to inherit them makes starting the game fail).
  SDL_PropertiesID props = SDL_CreateProperties();
  SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, (void*) args);
  SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
  SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_NULL);
  SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_NULL);
  SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
  SDL_Process* p = SDL_CreateProcessWithProperties(props);
  SDL_DestroyProperties(props);
  if(p == NULL) {
    snprintf(L.message, sizeof(L.message), "Couldn't start %s (%s).", TG_GAME_EXE, SDL_GetError());
    L.messageUntil = SDL_GetTicks() + 6000;
    L.messageGood = false;
    return false;
  }
  SDL_DestroyProcess(p);  // the game keeps running on its own
  return true;
}

static bool building(void) { return L.mb.phase == TG_MB_RUNNING || L.mb.phase == TG_MB_CANCELLING; }

static void note(const char* text, Uint64 ms) {
  snprintf(L.message, sizeof(L.message), "%s", text);
  L.messageUntil = SDL_GetTicks() + ms;
  L.messageGood = true;
}

// --- The modern music ---------------------------------------------------------------------

#ifdef _WIN32
#define EXE ".exe"
#else
#define EXE ""
#endif

static bool pathIs(const char* path, SDL_PathType type) {
  SDL_PathInfo info;
  return SDL_GetPathInfo(path, &info) && info.type == type;
}

static bool isFile(const char* path) { return pathIs(path, SDL_PATHTYPE_FILE); }
static bool isDir(const char* path) { return pathIs(path, SDL_PATHTYPE_DIRECTORY); }

static Sint64 fileSize(const char* path) {
  SDL_PathInfo info;
  return SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_FILE ? (Sint64) info.size : -1;
}

// dir (ending with a separator) holds the music tools: music/generate.py, audio/build_audio.py.
static bool toolsIn(const char* dir) {
  char a[1400], b[1400];
  snprintf(a, sizeof(a), "%smusic" SEP "generate.py", dir);
  snprintf(b, sizeof(b), "%saudio" SEP "build_audio.py", dir);
  return isFile(a) && isFile(b);
}

// The music tools: $TG_MUSIC_TOOLS, a release's music-tools/ beside the launcher, or the
// tools/ folder of the source checkout a build folder is in. Then tools/audio's ready-made
// tg_audio and what a build downloads (consent.txt): without that, no build is offered.
static void findMusicTools(void) {
  const char* base = SDL_GetBasePath();
  char dir[1024], t[1100];
  L.toolsDir[0] = 0;
  const char* env = SDL_getenv("TG_MUSIC_TOOLS");
  if(env && env[0]) {
    size_t n = strlen(env);
    snprintf(dir, sizeof(dir), "%s%s", env, env[n - 1] == '/' || env[n - 1] == '\\' ? "" : SEP);
    if(toolsIn(dir)) snprintf(L.toolsDir, sizeof(L.toolsDir), "%s", dir);
  }
  if(!L.toolsDir[0] && base) {
    snprintf(t, sizeof(t), "%smusic-tools" SEP, base);
    if(toolsIn(t)) snprintf(L.toolsDir, sizeof(L.toolsDir), "%s", t);
    snprintf(dir, sizeof(dir), "%s", base);
    for(int up = 0; up < 4 && !L.toolsDir[0]; up++) {
      snprintf(t, sizeof(t), "%stools" SEP, dir);
      if(toolsIn(t)) {
        snprintf(L.toolsDir, sizeof(L.toolsDir), "%s", t);
        break;
      }
      size_t n = strlen(dir);
      if(n < 2) break;
      n--;  // dir ends with a separator: up to the one before
      while(n > 0 && dir[n - 1] != '/' && dir[n - 1] != '\\') n--;
      if(n == 0) break;
      dir[n] = 0;
    }
  }
  L.tgAudio[0] = 0;
  if(L.toolsDir[0]) {
    snprintf(L.tgAudio, sizeof(L.tgAudio), "%stg_audio" EXE, L.toolsDir);
    if(!isFile(L.tgAudio)) L.tgAudio[0] = 0;
  }
  if(!L.tgAudio[0] && base) {
    snprintf(L.tgAudio, sizeof(L.tgAudio), "%stg_audio" EXE, base);
    if(!isFile(L.tgAudio)) L.tgAudio[0] = 0;
  }
  L.consentOk = false;
  if(L.toolsDir[0]) {
    char c[1400];
    snprintf(c, sizeof(c), "%smusic" SEP "tgmusic" SEP "data" SEP "consent.txt", L.toolsDir);
    char* text = SDL_LoadFile(c, NULL);
    if(text) {
      L.consentOk = tg_mb_parse_consent(text, &L.consent);
      SDL_free(text);
    }
  }
  if(!L.consentOk) L.toolsDir[0] = 0;
}

typedef struct {
  int count;
} TrackCount;

// A finished track is its .ogg with its .json beside it (the generator writes the .json last).
static SDL_EnumerationResult SDLCALL countTrack(void* user, const char* dirname, const char* fname) {
  size_t n = strlen(fname);
  if(n > 4 && fname[0] != '.' && SDL_strcasecmp(fname + n - 4, ".ogg") == 0) {
    char json[2400];
    snprintf(json, sizeof(json), "%s%.*s.json", dirname, (int) (n - 4), fname);
    if(isFile(json)) ((TrackCount*) user)->count++;
  }
  return SDL_ENUM_CONTINUE;
}

static SDL_EnumerationResult SDLCALL countFolder(void* user, const char* dirname, const char* fname) {
  char sub[2400];
  snprintf(sub, sizeof(sub), "%s%s", dirname, fname);
  if(isDir(sub)) SDL_EnumerateDirectory(sub, countTrack, user);
  return SDL_ENUM_CONTINUE;
}

static int countTracks(const char* musicDir) {
  static const char* const kFolders[] = {"variations", "stages", "stages-band", "stages-race"};
  TrackCount c = {0};
  for(size_t i = 0; i < SDL_arraysize(kFolders); i++) {
    char d[2400];
    snprintf(d, sizeof(d), "%s" SEP "%s", musicDir, kFolders[i]);
    if(isDir(d)) SDL_EnumerateDirectory(d, countFolder, &c);
  }
  return c.count;
}

// Where the player's music is: audio-modern next to the game, or the per-user one (where
// the launcher builds it when the game's folder can't be written; the game looks there too).
static void refreshMusic(void) {
  const char* base = SDL_GetBasePath();
  snprintf(L.musicHere, sizeof(L.musicHere), "%saudio-modern", base ? base : "");
  if(L.dataRoot[0]) snprintf(L.musicUser, sizeof(L.musicUser), "%saudio-modern", L.dataRoot);
  else L.musicUser[0] = 0;
  int here = countTracks(L.musicHere), user = L.musicUser[0] ? countTracks(L.musicUser) : 0;
  bool useUser = user > here;
  snprintf(L.musicDir, sizeof(L.musicDir), "%s", useUser ? L.musicUser : L.musicHere);
  L.tracks = useUser ? user : here;
}

// Can files be made in dir? (A file is made there and removed.)
static bool canWrite(const char* dir) {
  char probe[2400];
  size_t n = strlen(dir);
  snprintf(probe, sizeof(probe), "%s%s.race-masters-write-test", dir, n && (dir[n - 1] == '/' || dir[n - 1] == '\\') ? "" : SEP);
  SDL_IOStream* io = SDL_IOFromFile(probe, "wb");
  if(io == NULL) return false;
  bool ok = SDL_WriteIO(io, "x", 1) == 1;
  SDL_CloseIO(io);
  SDL_RemovePath(probe);
  return ok;
}

// Where a build writes: where the music already is, else next to the game if it can be
// written to, else the per-user folder.
static const char* outputDir(void) {
  if(countTracks(L.musicHere) > 0) return L.musicHere;
  if(L.musicUser[0] && countTracks(L.musicUser) > 0) return L.musicUser;
  const char* base = SDL_GetBasePath();
  bool here = isDir(L.musicHere) ? canWrite(L.musicHere) : base && canWrite(base);
  return here || !L.musicUser[0] ? L.musicHere : L.musicUser;
}

static bool windowsBuild(void) {
#ifdef _WIN32
  return true;
#else
  return false;
#endif
}

static int buildJobs(void) { return tg_mb_auto_jobs(SDL_GetSystemRAM(), SDL_GetNumLogicalCPUCores()); }

// A rough time for a build of these styles on this computer.
static double estimateSeconds(unsigned styles) {
  if(!L.consentOk) return -1;
  int mb = tg_mb_consent_typical_mb(&L.consent, styles, false, windowsBuild());
  return tg_mb_estimate_seconds(tg_mb_style_tracks(styles), buildJobs(), mb, true, L.consent.trackS, L.consent.notesS,
                                L.consent.rateMbs);
}

// The folder of the music tools' own Python (inside the per-user folder).
static void toolsRoot(char* out, size_t n) { snprintf(out, n, "%smusic-tools", L.dataRoot); }  // out: 1100

// --- Running a build ----------------------------------------------------------------------

// A child with its output (and errors) piped to the launcher, and its input too when it
// takes "cancel". Windows: no console window (SDL's background mode, which reports no exit
// code: the programs' last lines say how they ended, src/music_build.h).
static SDL_Process* spawn(const char* const* args, bool input, SDL_IOStream* outputFile) {
  SDL_PropertiesID props = SDL_CreateProperties();
  SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, (void*) args);
  SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, input ? SDL_PROCESS_STDIO_APP : SDL_PROCESS_STDIO_NULL);
  if(outputFile) {
    // The program's output goes to a file, its errors to the launcher.
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_REDIRECT);
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_POINTER, outputFile);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_APP);
  } else {
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
    SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
  }
  SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, windowsBuild());
  SDL_Process* p = SDL_CreateProcessWithProperties(props);
  SDL_DestroyProperties(props);
  return p;
}

static void onLine(const char* line, void* user) {
  (void) user;
  tg_mb_feed(&L.mb, line);
}

// Everything the child has written so far (it never blocks).
static void pumpOutput(void) {
  if(L.proc == NULL) return;
  SDL_PropertiesID props = SDL_GetProcessProperties(L.proc);
  SDL_IOStream* out = SDL_GetPointerProperty(props, SDL_PROP_PROCESS_STDOUT_POINTER, NULL);
  if(out == NULL) out = SDL_GetPointerProperty(props, SDL_PROP_PROCESS_STDERR_POINTER, NULL);
  if(out == NULL) return;
  char buf[4096];
  for(int k = 0; k < 256; k++) {
    size_t n = SDL_ReadIO(out, buf, sizeof(buf));
    if(n == 0) break;
    tg_mb_lines_push(&L.lines, buf, n, onLine, NULL);
  }
}

static void finishBuild(void) {
  L.stage = ST_NONE;
  refreshMusic();
  if(L.mb.phase == TG_MB_FINISHED && L.tracks > 0) note("The modern music is ready: pick it in Sound > Music.", 8000);
  if(L.quitWhenStopped) {
    SDL_Event q = {.type = SDL_EVENT_QUIT};
    SDL_PushEvent(&q);
  }
}

static void startStage(Stage stage);

// The Python the music tools run on: Windows, the embeddable one the launcher unpacks; Linux,
// the system's python3 for setup_tools.py, which makes a venv for the rest.
static void toolsPython(char* out, size_t n, bool forGenerate) {
  char root[1100];
  toolsRoot(root, sizeof(root));
  if(forGenerate && L.mb.python[0]) snprintf(out, n, "%s", L.mb.python);
  else if(windowsBuild()) snprintf(out, n, "%s" SEP "python" SEP "python.exe", root);
  else if(forGenerate) snprintf(out, n, "%s" SEP "venv" SEP "bin" SEP "python", root);
  else snprintf(out, n, "python3");
}

// --- Windows: the python.org embeddable Python ---------------------------------------------
// Downloaded by Windows' own curl.exe into a file the launcher opens (so no folder name
// reaches curl.exe, whose arguments can't carry every name), SHA-256 checked, and unpacked by
// the launcher (src/unzip.c). A download that stops goes on from where it was next time.

static void runtimePaths(char* dir, char* part, size_t n) {
  char root[1100];
  toolsRoot(root, sizeof(root));
  snprintf(dir, n, "%s" SEP "python", root);
  snprintf(part, n, "%s" SEP "downloads" SEP "python-%s-embed-amd64.zip.part", root, TG_MB_PY_VERSION);
}

static bool runtimeReady(void) {
  char dir[2400], part[2400], exe[2600], stamp[2600];
  runtimePaths(dir, part, sizeof(dir));
  snprintf(exe, sizeof(exe), "%s" SEP "python.exe", dir);
  snprintf(stamp, sizeof(stamp), "%s" SEP TG_MB_PY_STAMP, dir);
  char* text = isFile(exe) ? SDL_LoadFile(stamp, NULL) : NULL;
  bool ok = text && strncmp(text, TG_MB_PY_SHA256, 64) == 0;
  SDL_free(text);
  return ok;
}

static void systemProgram(char* out, size_t n, const char* name) {
#ifdef _WIN32
  const char* root = SDL_getenv("SystemRoot");
  snprintf(out, n, "%s\\System32\\%s.exe", root && root[0] ? root : "C:\\Windows", name);
#else
  snprintf(out, n, "%s", name);
#endif
}

static bool sha256Matches(const char* path, const char* want) {
  size_t n = 0;
  void* data = SDL_LoadFile(path, &n);
  if(data == NULL) return false;
  uint8_t d[32];
  tg_sha256(data, n, d);
  SDL_free(data);
  char hex[65];
  for(int i = 0; i < 32; i++) snprintf(hex + 2 * i, 3, "%02x", d[i]);
  return strcmp(hex, want) == 0;
}

static SDL_EnumerationResult SDLCALL removeEntry(void* user, const char* dirname, const char* fname) {
  (void) user;
  char p[2400];
  snprintf(p, sizeof(p), "%s%s", dirname, fname);
  if(isDir(p)) SDL_EnumerateDirectory(p, removeEntry, NULL);
  SDL_RemovePath(p);
  return SDL_ENUM_CONTINUE;
}

static void removeTree(const char* dir) {
  if(!isDir(dir)) return;
  SDL_EnumerateDirectory(dir, removeEntry, NULL);
  SDL_RemovePath(dir);
}

typedef struct {
  const char* dir;
  bool ok;
} UnzipTo;

static bool writeUnzipped(const char* name, const uint8_t* data, size_t size, void* user) {
  UnzipTo* u = user;
  char path[2600];
  snprintf(path, sizeof(path), "%s" SEP "%s", u->dir, name);
  for(char* c = path + strlen(u->dir); *c; c++) if(*c == '/') *c = SEP[0];
  char* slash = strrchr(path, SEP[0]);
  if(slash) {
    *slash = 0;
    SDL_CreateDirectory(path);
    *slash = SEP[0];
  }
  return SDL_SaveFile(path, data, size);
}

static void runtimeFetchEnded(void) {
  char dir[2400], part[2400];
  runtimePaths(dir, part, sizeof(dir));
  Sint64 size = fileSize(part);
  if(L.mb.phase == TG_MB_CANCELLING) {
    L.mb.phase = TG_MB_STOPPED;
    finishBuild();
    return;
  }
  if(size != TG_MB_PY_BYTES || !sha256Matches(part, TG_MB_PY_SHA256)) {
    // Complete but wrong (or too long), or a server that won't send the rest: start again next time.
    if(size >= TG_MB_PY_BYTES || strstr(L.mb.lastPlain, "byte ranges")) SDL_RemovePath(part);
    char why[400];
    if(size >= TG_MB_PY_BYTES) snprintf(why, sizeof(why), "The Python download from python.org didn't match its pinned SHA-256; it was removed. Try again.");
    else snprintf(why, sizeof(why), "Couldn't download Python from python.org (%s). What was downloaded is kept: try again.",
                  L.mb.lastPlain[0] ? L.mb.lastPlain : "no connection?");
    tg_mb_fail(&L.mb, size >= TG_MB_PY_BYTES ? "hash" : "network", why);
    finishBuild();
    return;
  }
  // Unpack beside the final folder, then put it in place: never half a Python.
  L.mb.fraction = 0.25;
  snprintf(L.mb.detail, sizeof(L.mb.detail), "Unpacking Python %s", TG_MB_PY_VERSION);
  char tmp[2600];
  snprintf(tmp, sizeof(tmp), "%s.tmp", dir);
  removeTree(tmp);
  SDL_CreateDirectory(tmp);
  size_t n = 0;
  void* zip = SDL_LoadFile(part, &n);
  char err[300] = "";
  UnzipTo u = {tmp, true};
  bool ok = zip && tg_unzip(zip, n, writeUnzipped, &u, err, sizeof(err));
  SDL_free(zip);
  char pth[2700], stamp[2700], exe[2700];
  snprintf(pth, sizeof(pth), "%s" SEP TG_MB_PY_PTH_NAME, tmp);
  snprintf(stamp, sizeof(stamp), "%s" SEP TG_MB_PY_STAMP, tmp);
  snprintf(exe, sizeof(exe), "%s" SEP "python.exe", tmp);
  ok = ok && isFile(exe) && SDL_SaveFile(pth, TG_MB_PY_PTH, strlen(TG_MB_PY_PTH)) &&
       SDL_SaveFile(stamp, TG_MB_PY_SHA256 "\n", 65);
  if(ok) {
    removeTree(dir);
    ok = SDL_RenamePath(tmp, dir);
  }
  if(!ok) {
    removeTree(tmp);
    char why[400];
    snprintf(why, sizeof(why), "Couldn't unpack Python into %.200s (%.150s).", dir, err[0] ? err : SDL_GetError());
    tg_mb_fail(&L.mb, "disk", why);
    finishBuild();
    return;
  }
  SDL_RemovePath(part);
  if(SDL_getenv("TG_MUSIC_RUNTIME_TEST") && !windowsBuild()) {
    // Checking this step on Linux (curl, the hash, the unpacking): the Windows Python can't run here.
    L.mb.phase = TG_MB_FINISHED;
    finishBuild();
    return;
  }
  startStage(ST_TOOLS);
}

static void runtimeFetch(void) {
  char dir[2400], part[2400], downloads[2400];
  runtimePaths(dir, part, sizeof(dir));
  snprintf(downloads, sizeof(downloads), "%s", part);
  char* slash = strrchr(downloads, SEP[0]);
  if(slash) *slash = 0;
  SDL_CreateDirectory(downloads);
  Sint64 have = fileSize(part);
  if(have >= TG_MB_PY_BYTES) {  // already all there (or too long): check it now
    runtimeFetchEnded();
    return;
  }
  if(have < 0) have = 0;
  char curl[600], from[32];
  systemProgram(curl, sizeof(curl), "curl");
  snprintf(from, sizeof(from), "%lld", (long long) have);
  SDL_IOStream* file = SDL_IOFromFile(part, have > 0 ? "ab" : "wb");
  if(file == NULL) {
    char why[400];
    snprintf(why, sizeof(why), "Couldn't write into %.200s (%.150s).", downloads, SDL_GetError());
    tg_mb_fail(&L.mb, "disk", why);
    finishBuild();
    return;
  }
  // The rest of the file to standard output (the open file); errors to the launcher. No
  // --retry: with standard output curl would write a retried part twice (Retry resumes).
  const char* args[] = {curl, "--fail", "--location", "--silent", "--show-error", "--connect-timeout", "30",
                        "--continue-at", from, TG_MB_PY_URL, NULL};
  L.proc = spawn(args, false, file);
  SDL_CloseIO(file);  // the child has its own handle now
  if(L.proc == NULL) {
    tg_mb_fail(&L.mb, "other", windowsBuild() ? "Couldn't start Windows' curl.exe (Windows 10 version 1803 or newer is needed)."
                                              : "Couldn't start curl.");
    finishBuild();
    return;
  }
  L.stage = ST_FETCH;
  L.mb.step = TG_MB_STEP_TOOLS;
  snprintf(L.mb.stepText, sizeof(L.mb.stepText), "Getting Python %s (python.org)", TG_MB_PY_VERSION);
}

// --- The stages ---------------------------------------------------------------------------

static void startStage(Stage stage) {
  char py[1400], script[1400], root[1100];
  toolsRoot(root, sizeof(root));
  L.lines.len = 0;
  L.lines.overlong = false;
  if(stage == ST_FETCH) {
    runtimeFetch();
    return;
  }
  if(L.fake) {
    // tools/music/tests/fake_generate.py: progress lines only, for checking this card.
    snprintf(script, sizeof(script), "%smusic" SEP "tests" SEP "fake_generate.py", L.toolsDir);
    const char* args[] = {"python3", "-B", script, L.fake, NULL};
    L.proc = spawn(args, true, NULL);
    stage = ST_GENERATE;
  } else if(stage == ST_TOOLS) {
    toolsPython(py, sizeof(py), false);
    snprintf(script, sizeof(script), "%smusic" SEP "setup_tools.py", L.toolsDir);
    const char* args[] = {py, "-B", "-s", "-E", "-X", "utf8", script, "--root", root, "--progress-json", NULL};
    L.proc = spawn(args, true, NULL);
  } else {
    toolsPython(py, sizeof(py), true);
    snprintf(script, sizeof(script), "%smusic" SEP "generate.py", L.toolsDir);
    char styles[64], jobs[16];
    tg_mb_styles_arg(L.prefs.styles, styles, sizeof(styles));
    snprintf(jobs, sizeof(jobs), "%d", buildJobs());
    const char* args[24] = {py, "-B", "-s", "-E", "-X", "utf8", script, "--rom", L.romPath, "--out", L.buildOut,
                            "--styles", styles, "--jobs", jobs, "--yes", "--progress-json"};
    int n = 17;
    // Checking: the notes and the plan, the exact download shown; only the instruments'
    // definition files (0.5 MB) downloaded.
    if(SDL_getenv("TG_MUSIC_DRY_RUN")) args[n++] = "--dry-run";
    if(L.prefs.cleanAfter) args[n++] = "--clean-after";
    if(L.tgAudio[0]) args[n++] = "--tg-audio", args[n++] = L.tgAudio;
    args[n] = NULL;
    L.proc = spawn(args, true, NULL);
  }
  if(L.proc == NULL) {
    char why[400];
    if(stage == ST_TOOLS && !windowsBuild())
      snprintf(why, sizeof(why), "Couldn't start python3: install Python 3 (3.10, 3.11 or 3.12) and try again.");
    else
      snprintf(why, sizeof(why), "Couldn't start the music tools (%s).", SDL_GetError());
    tg_mb_fail(&L.mb, "python", why);
    finishBuild();
    return;
  }
  L.stage = stage;
}

static void stageEnded(int code) {
  Stage was = L.stage;
  L.stage = ST_NONE;
  if(was == ST_FETCH) {
    runtimeFetchEnded();
    return;
  }
  bool last = was == ST_GENERATE;
  if(tg_mb_child_exited(&L.mb, windowsBuild() ? TG_MB_EXIT_UNKNOWN : code, last)) {
    startStage(ST_GENERATE);
    return;
  }
  finishBuild();
}

static void pollBuild(void) {
  if(L.proc == NULL) return;
  pumpOutput();
  if(L.stage == ST_FETCH) {
    char dir[2400], part[2400];
    runtimePaths(dir, part, sizeof(dir));
    Sint64 got = fileSize(part);
    if(got > 0) {
      char a[32], b[32];
      tg_mb_format_size((double) got, a, sizeof(a));
      tg_mb_format_size(TG_MB_PY_BYTES, b, sizeof(b));
      L.mb.fraction = 0.2 * (double) got / TG_MB_PY_BYTES;
      snprintf(L.mb.detail, sizeof(L.mb.detail), "Downloading Python: %s of %s", a, b);
    }
  }
  if(L.mb.phase == TG_MB_CANCELLING && SDL_GetTicks() >= L.killAt) {
    SDL_KillProcess(L.proc, true);
    L.killAt = SDL_GetTicks() + 60000;
  }
  int code = 0;
  if(SDL_WaitProcess(L.proc, false, &code)) {
    pumpOutput();  // what it wrote last
    if(L.lines.len) {
      L.lines.buf[L.lines.len] = 0;
      onLine(L.lines.buf, NULL);
      L.lines.len = 0;
    }
    SDL_DestroyProcess(L.proc);
    L.proc = NULL;
    stageEnded(code);
  }
}

// Builds what the player agreed to (the styles and the clean-up in L.prefs).
static void startBuild(void) {
  if(building() || !L.toolsDir[0] || !L.romOk) return;
  if(!(L.prefs.styles & TG_MB_STYLES_ALL)) L.prefs.styles = TG_MB_STYLES_ALL;
  snprintf(L.buildOut, sizeof(L.buildOut), "%s", outputDir());
  tg_mb_begin(&L.mb, estimateSeconds(L.prefs.styles));
  L.quitWhenStopped = false;
  if(L.fake) {
    startStage(ST_GENERATE);
    return;
  }
  if(!L.dataRoot[0]) {
    tg_mb_fail(&L.mb, "disk", "There is no per-user data folder for the music tools (LOCALAPPDATA / HOME is not set).");
    return;
  }
  if(windowsBuild() || SDL_getenv("TG_MUSIC_RUNTIME_TEST")) {
#ifdef _WIN32
    // Several of the tools' packages need Microsoft's Visual C++ runtime (MSVCP140.dll).
    char dll[600];
    const char* sysRoot = SDL_getenv("SystemRoot");
    snprintf(dll, sizeof(dll), "%s\\System32\\msvcp140.dll", sysRoot && sysRoot[0] ? sysRoot : "C:\\Windows");
    if(!isFile(dll)) {
      tg_mb_fail(&L.mb, "vcredist", "The Microsoft Visual C++ Redistributable (x64) is needed first: several of the music "
                 "tools use its MSVCP140.dll. Get it from Microsoft (the button below), install it, then press Retry.");
      return;
    }
#endif
    if(!runtimeReady()) {
      startStage(ST_FETCH);
      return;
    }
  }
  startStage(ST_TOOLS);
}

// Stop the build: the programs finish what they're writing and keep everything done.
static void cancelBuild(void) {
  if(L.mb.phase != TG_MB_RUNNING) return;
  tg_mb_cancel(&L.mb);
  if(L.proc == NULL) {
    L.mb.phase = TG_MB_STOPPED;
    finishBuild();
    return;
  }
  if(L.stage == ST_FETCH) {
    SDL_KillProcess(L.proc, true);  // curl: the part downloaded is kept
    L.killAt = SDL_GetTicks() + 60000;
    return;
  }
  SDL_IOStream* in = SDL_GetProcessInput(L.proc);
  if(in) {
    SDL_WriteIO(in, "cancel\n", 7);
    SDL_CloseIO(in);  // and the end of its input, which also means stop
  }
  L.killAt = SDL_GetTicks() + 20000;  // a render process gets 10 s to stop
}

static void openPath(const char* path) {
  char url[2600];
  snprintf(url, sizeof(url), "file:///%s", path[0] == '/' ? path + 1 : path);
  for(char* c = url; *c; c++) if(*c == '\\') *c = '/';
  SDL_OpenURL(url);
}

// The Music card's buttons for the state it is in (MA_NONE: no button there).
static void musicActions(MusicAction* a, MusicAction* b) {
  *a = *b = MA_NONE;
  if(!L.toolsDir[0] || !L.romOk) return;
  switch(L.mb.phase) {
    case TG_MB_RUNNING: *a = MA_CANCEL; return;
    case TG_MB_CANCELLING: return;
    case TG_MB_FAILED:
      *a = MA_RETRY;
      *b = strcmp(L.mb.errorCode, "vcredist") == 0 ? MA_VCREDIST : L.mb.logPath[0] ? MA_LOG : MA_NONE;
      return;
    default: break;
  }
  if(L.tracks < TG_MB_TRACKS) *a = MA_BUILD;
  if(L.tracks > 0) *b = MA_FOLDER;
}

static const char* actionLabel(MusicAction a) {
  switch(a) {
    case MA_BUILD:
      return L.mb.phase == TG_MB_STOPPED ? "Resume" : L.tracks > 0 ? "Build the rest" : "Build music";
    case MA_CANCEL: return "Cancel";
    case MA_RETRY: return "Retry";
    case MA_VCREDIST: return "Get it from Microsoft";
    case MA_LOG: return "Show log";
    case MA_FOLDER: return "Show folder";
    default: return "";
  }
}

static void openConsent(void) {
  snprintf(L.buildOut, sizeof(L.buildOut), "%s", outputDir());
  L.consentStyles = L.prefs.styles ? L.prefs.styles : TG_MB_STYLES_ALL;
  L.consentClean = L.prefs.cleanAfter;
  L.modal = MODAL_CONSENT;
  L.modalFocus = C_AGREE;
}

static void musicAction(MusicAction a) {
  switch(a) {
    case MA_BUILD:
      if(L.mb.phase == TG_MB_STOPPED) startBuild();  // agreed already: go on
      else openConsent();
      break;
    case MA_RETRY: startBuild(); break;
    case MA_CANCEL: cancelBuild(); break;
    case MA_VCREDIST: SDL_OpenURL(TG_MB_VCREDIST_URL); break;
    case MA_LOG: openPath(L.mb.logPath); break;
    case MA_FOLDER: openPath(L.musicDir); break;
    default: break;
  }
}

// The first-run offer: once a session, with a verified ROM and nothing built.
static void maybeOffer(void) {
  if(L.modal != MODAL_NONE) return;
  if(tg_mb_should_offer(L.romOk, L.toolsDir[0] != 0, L.tracks, building(), L.prefs.offer, L.offerShown)) {
    L.offerShown = true;
    L.modal = MODAL_OFFER;
    L.modalFocus = 0;
  }
}

// --- Changing options ----------------------------------------------------------------

static bool optionEnabled(int i) {
  if(i == O_SIDE) return L.value[O_WIDE] != 0;
  if(i == O_MAP || i == O_SPEED) return L.value[O_FULLVIEW] != 0;
  return true;
}

static void change(int i, int dir) {
  if(!optionEnabled(i)) return;
  int n = kOptions[i].count;
  L.value[i] = (L.value[i] + (dir < 0 ? n - 1 : 1)) % n;
  // High frame rate needs smooth motion.
  if(i == O_HIGHFPS && L.value[i]) L.value[O_SMOOTH] = 1;
  if(i == O_SMOOTH && !L.value[i]) L.value[O_HIGHFPS] = 0;
  saveSettings();
}

// Start: the game starts and the launcher closes; while the music builds, the launcher stays
// open (minimized) to look after it.
static void play(void) {
  if(!launch()) return;
  if(building()) {
    note("The music goes on building here while you play.", 10000);
    SDL_MinimizeWindow(L.window);
  } else {
    SDL_Event q = {.type = SDL_EVENT_QUIT};
    SDL_PushEvent(&q);
  }
}

static MusicAction musicFocusAction(int f) {
  MusicAction a, b;
  musicActions(&a, &b);
  return f == F_MUSIC1 ? a : f == F_MUSIC2 ? b : MA_NONE;
}

static bool focusable(int f) { return (f != F_MUSIC1 && f != F_MUSIC2) || musicFocusAction(f) != MA_NONE; }

static void activate(int dir) {
  if(L.focus == F_ROM) chooseRom();
  else if(L.focus == F_PLAY) play();
  else if(L.focus == F_MUSIC1 || L.focus == F_MUSIC2) musicAction(musicFocusAction(L.focus));
  else change(L.focus - F_OPTION0, dir);
}

static void moveFocus(int dir) {
  for(int k = 0; k < F_COUNT; k++) {
    L.focus = (L.focus + dir + F_COUNT) % F_COUNT;
    if(focusable(L.focus)) return;
  }
}

// --- The panels over the launcher: the first-run offer, the consent, quitting mid-build -----

static int modalItems(void) { return L.modal == MODAL_OFFER ? 3 : L.modal == MODAL_CONSENT ? C_COUNT : L.modal == MODAL_QUIT ? 2 : 0; }

static void closeModal(void) {
  L.modal = MODAL_NONE;
  L.modalFocus = 0;
}

static void modalActivate(int item) {
  if(L.modal == MODAL_OFFER) {
    if(item == 0) {
      openConsent();
      return;
    }
    L.prefs.offer = item == 1 ? TG_MB_OFFER_LATER : TG_MB_OFFER_NEVER;
    saveLauncher();
    if(item == 2) note("The launcher won't ask again: Build music is in the Music card.", 6000);
    closeModal();
  } else if(L.modal == MODAL_CONSENT) {
    if(item >= C_STYLE0 && item < C_STYLE0 + TG_MB_STYLE_COUNT) {
      unsigned bit = 1u << (item - C_STYLE0);
      if((L.consentStyles & ~bit) || !(L.consentStyles & bit)) L.consentStyles ^= bit;  // keep at least one
    } else if(item == C_CLEAN) {
      L.consentClean = !L.consentClean;
    } else if(item == C_AGREE) {
      closeModal();
      L.prefs.styles = L.consentStyles;  // the choices are kept for next time
      L.prefs.cleanAfter = L.consentClean;
      saveLauncher();
      startBuild();
      L.focus = focusable(F_MUSIC1) ? F_MUSIC1 : L.focus;
    } else {
      closeModal();
    }
  } else if(L.modal == MODAL_QUIT) {
    if(item == 0) {
      L.quitWhenStopped = true;
      cancelBuild();
    }
    closeModal();
  }
}

// Esc, B or a click beside it: the panel's "no".
static void modalBack(void) {
  if(L.modal == MODAL_OFFER) modalActivate(1);  // Later
  else closeModal();
}

// Arrows / D-pad: dir 1 up, 2 down, 3 left, 4 right.
static void modalMove(int dir) {
  int n = modalItems(), f = L.modalFocus;
  if(L.modal != MODAL_CONSENT) {
    L.modalFocus = (f + (dir == 1 || dir == 3 ? n - 1 : 1)) % n;
    return;
  }
  int row = f < C_CLEAN ? 0 : f == C_CLEAN ? 1 : 2;
  if(dir == 3 || dir == 4) {
    int step = dir == 3 ? -1 : 1;
    if(row == 0) L.modalFocus = (f + step + TG_MB_STYLE_COUNT) % TG_MB_STYLE_COUNT;
    else if(row == 2) L.modalFocus = f == C_AGREE ? C_CANCEL : C_AGREE;
  } else {
    row = (row + (dir == 1 ? 2 : 1)) % 3;
    L.modalFocus = row == 0 ? C_STYLE0 : row == 1 ? C_CLEAN : C_AGREE;
  }
}

// Quitting while the music builds asks first (Stop and quit / Keep building).
static bool requestQuit(void) {
  if(building() && !L.quitWhenStopped && !SDL_getenv("TG_LAUNCHER_SHOT")) {
    L.modal = MODAL_QUIT;
    L.modalFocus = 1;
    return false;
  }
  return true;
}

// --- Drawing -------------------------------------------------------------------------

typedef struct { Uint8 r, g, b; } Rgb;
static const Rgb kBg = {14, 16, 24}, kPanel = {26, 29, 42}, kPanelHi = {40, 44, 64};
static const Rgb kText = {236, 238, 244}, kDim = {140, 146, 166}, kFaint = {84, 90, 110};
static const Rgb kAccent = {255, 92, 56}, kAccent2 = {255, 196, 64}, kGood = {96, 214, 140}, kBad = {255, 110, 110};

static void color(Rgb c, Uint8 a) { SDL_SetRenderDrawColor(L.renderer, c.r, c.g, c.b, a); }

static void fill(float x, float y, float w, float h, Rgb c, Uint8 a) {
  color(c, a);
  SDL_FRect r = {x, y, w, h};
  SDL_RenderFillRect(L.renderer, &r);
}

// A panel with softly cut corners.
static void panel(float x, float y, float w, float h, Rgb c, Uint8 a) {
  fill(x + 3, y, w - 6, h, c, a);
  fill(x, y + 3, 3, h - 6, c, a);
  fill(x + w - 3, y + 3, 3, h - 6, c, a);
  fill(x + 1, y + 1, 2, 2, c, a);
  fill(x + w - 3, y + 1, 2, 2, c, a);
  fill(x + 1, y + h - 3, 2, 2, c, a);
  fill(x + w - 3, y + h - 3, 2, 2, c, a);
}

static void text(float x, float y, float scale, Rgb c, const char* s) {
  SDL_SetRenderScale(L.renderer, scale, scale);
  color((Rgb) {0, 0, 0}, 140);
  SDL_RenderDebugText(L.renderer, (x + scale * 0.5f) / scale, (y + scale * 0.5f) / scale, s);
  color(c, 255);
  SDL_RenderDebugText(L.renderer, x / scale, y / scale, s);
  SDL_SetRenderScale(L.renderer, 1, 1);
}

static float textWidth(const char* s, float scale) { return (float) strlen(s) * 8 * scale; }

// Wrapped text within width, at most maxLines lines (the last cut with "~" if more is left);
// returns the height used.
static float wrappedLines(float x, float y, float w, float scale, Rgb c, const char* s, int maxLines) {
  int perLine = (int) (w / (8 * scale));
  float y0 = y;
  for(int lines = 0; *s && lines < maxLines; lines++) {
    int n = (int) strlen(s);
    if(n > perLine) {
      n = perLine;
      while(n > 0 && s[n] != ' ') n--;
      if(n == 0) n = perLine;
    }
    char line[256];
    if(lines == maxLines - 1 && (int) strlen(s) > n) snprintf(line, sizeof(line), "%.*s~", n < perLine ? n : perLine - 1, s);
    else snprintf(line, sizeof(line), "%.*s", n, s);
    text(x, y, scale, c, line);
    y += 8 * scale + 6;
    s += n;
    while(*s == ' ' || *s == '\n') s++;
  }
  return y - y0;
}

static float wrapped(float x, float y, float w, float scale, Rgb c, const char* s) { return wrappedLines(x, y, w, scale, c, s, 1000); }

// s cut to fit chars characters ("~" where it was cut).
static void fit(char* out, size_t n, const char* s, int chars) {
  if((int) strlen(s) <= chars) snprintf(out, n, "%s", s);
  else snprintf(out, n, "%.*s~", chars - 1, s);
}

// s cut from its start, for paths: "~ders/Race Masters/audio-modern".
static void fitTail(char* out, size_t n, const char* s, int chars) {
  int len = (int) strlen(s);
  if(len <= chars) snprintf(out, n, "%s", s);
  else snprintf(out, n, "~%s", s + len - (chars - 1));
}

// Rows of the option list: a heading row before each group.
static float optionY(int i) {
  float y = 0;
  for(int k = 0; k <= i; k++) {
    if(kOptions[k].group) y += 34;
    if(k < i) y += 28;
  }
  return y;
}

#define LIST_X 530.0f
#define LIST_Y 100.0f
#define LIST_W 710.0f
#define LIST_H 400.0f
#define LEFT_X 40.0f
#define LEFT_W 440.0f
#define ROMBTN_Y 236.0f
#define PLAY_Y 592.0f
#define MUSIC_X 526.0f
#define MUSIC_Y 516.0f
#define MUSIC_W 718.0f
#define MUSIC_H 176.0f
#define MBTN_Y (MUSIC_Y + 134.0f)
#define MBTN_H 30.0f

// A picture of launcher_art.h (made by tools/gen_launcher_art.py): premultiplied RGBA at
// LAUNCHER_ART_SCALE texels a pixel, run-length packed (a byte c < 128 is the next pixel
// c + 1 times, c >= 128 is c - 127 pixels as they are).
static SDL_Texture* artTexture(const unsigned char* packed, size_t size, int w, int h) {
  SDL_Surface* s = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32);
  if(s == NULL) return NULL;
  size_t n = 0, total = (size_t) w * (size_t) h;
  for(size_t i = 0; i < size && n < total; ) {
    int c = packed[i++];
    size_t count = c < 128 ? (size_t) c + 1 : (size_t) c - 127, bytes = c < 128 ? 4 : 4 * count;
    if(i + bytes > size) break;
    for(size_t k = 0; k < count && n < total; k++, n++)
      memcpy((Uint8*) s->pixels + (n / (size_t) w) * (size_t) s->pitch + (n % (size_t) w) * 4, packed + i + (c < 128 ? 0 : 4 * k), 4);
    i += bytes;
  }
  SDL_Texture* tex = SDL_CreateTextureFromSurface(L.renderer, s);
  SDL_DestroySurface(s);
  if(tex) {
    if(!SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND_PREMULTIPLIED)) SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
  }
  return tex;
}

// The logo: the emblem, the name and the line under it (launcher_art.h, drawn for
// LAUNCHER_ART_NAME; a build with another TG_APP_NAME shows its name as text instead). A
// logo.bmp next to the launcher replaces it (magenta is see-through).
static void loadLogo(void) {
  char path[2400];
  const char* base = SDL_GetBasePath();
  snprintf(path, sizeof(path), "%slogo.bmp", base ? base : "");
  SDL_Surface* s = SDL_LoadBMP(path);
  if(s) {
    SDL_SetSurfaceColorKey(s, true, SDL_MapSurfaceRGB(s, 255, 0, 255));
    L.logo = SDL_CreateTextureFromSurface(L.renderer, s);
    SDL_DestroySurface(s);
    if(L.logo) {
      SDL_SetTextureScaleMode(L.logo, SDL_SCALEMODE_LINEAR);
      L.logoBmp = true;
      return;
    }
  }
  if(strcmp(LAUNCHER_ART_NAME, TG_APP_NAME) == 0) L.logo = artTexture(kLogoArt, sizeof(kLogoArt), LOGO_ART_W, LOGO_ART_H);
}

// --- Header art ------------------------------------------------------------------------
// A tour map behind the header's right side: a faint grid of parallels and meridians, the
// eight regions' cities where they are on a flat map (one each, in the tour's order), the
// legs between them as arcs, and a light flying the tour. Drawn as soft-edged geometry, so it
// stays smooth at any window size.

#define HEADER_H 86.0f
#define MAP_X0 560.0f
#define MAP_X1 1250.0f
#define MAP_Y0 10.0f
#define MAP_Y1 80.0f
#define MAX_LINE 128

static SDL_FColor fcol(Rgb c, float a) { return (SDL_FColor) {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, a}; }

// Longitude and latitude (degrees) on the header.
static SDL_FPoint mapAt(float lon, float lat) {
  return (SDL_FPoint) {MAP_X0 + (lon + 130) / 285 * (MAP_X1 - MAP_X0), MAP_Y1 - (lat + 35) / 105 * (MAP_Y1 - MAP_Y0)};
}

// A line through p[0..n-1], w wide, alpha a[i] along its middle fading to nothing at its sides.
static void softLine(const SDL_FPoint* p, const float* a, int n, float w, Rgb c) {
  SDL_Vertex v[MAX_LINE * 3];
  int idx[MAX_LINE * 12], k = 0;
  if(n < 2 || n > MAX_LINE) return;
  for(int i = 0; i < n; i++) {
    SDL_FPoint p0 = p[i > 0 ? i - 1 : i], p1 = p[i < n - 1 ? i + 1 : i];
    float dx = p1.x - p0.x, dy = p1.y - p0.y, len = SDL_sqrtf(dx * dx + dy * dy);
    float nx = len > 0 ? -dy / len * w / 2 : 0, ny = len > 0 ? dx / len * w / 2 : 0;
    v[i * 3] = (SDL_Vertex) {{p[i].x + nx, p[i].y + ny}, fcol(c, 0), {0, 0}};
    v[i * 3 + 1] = (SDL_Vertex) {p[i], fcol(c, a[i]), {0, 0}};
    v[i * 3 + 2] = (SDL_Vertex) {{p[i].x - nx, p[i].y - ny}, fcol(c, 0), {0, 0}};
  }
  for(int i = 0; i + 1 < n; i++)
    for(int side = 0; side < 2; side++) {
      int a0 = i * 3 + side, a1 = a0 + 1, b0 = a0 + 3, b1 = a1 + 3;
      idx[k++] = a0, idx[k++] = a1, idx[k++] = b0, idx[k++] = a1, idx[k++] = b1, idx[k++] = b0;
    }
  SDL_RenderGeometry(L.renderer, NULL, v, n * 3, idx, k);
}

// A soft round dot.
static void glowDot(SDL_FPoint q, float r, Rgb c, float a) {
  enum { N = 20 };
  SDL_Vertex v[N + 1];
  int idx[N * 3];
  v[0] = (SDL_Vertex) {q, fcol(c, a), {0, 0}};
  for(int i = 0; i < N; i++) {
    float ang = i * 2 * SDL_PI_F / N;
    v[i + 1] = (SDL_Vertex) {{q.x + r * SDL_cosf(ang), q.y + r * SDL_sinf(ang)}, fcol(c, 0), {0, 0}};
    idx[i * 3] = 0, idx[i * 3 + 1] = i + 1, idx[i * 3 + 2] = (i + 1) % N + 1;
  }
  SDL_RenderGeometry(L.renderer, NULL, v, N + 1, idx, N * 3);
}

// The tour: a city of each region, in the order the regions come.
static const float kTour[8][2] = {
  {-115.1f, 36.2f}, {-43.2f, -22.9f}, {139.7f, 35.7f}, {8.7f, 50.1f},
  {18.1f, 59.3f}, {2.4f, 48.9f}, {12.5f, 41.9f}, {-0.1f, 51.5f},
};

// The point u (0-1) along leg i: an arc rising over the straight line, higher for longer legs.
static SDL_FPoint legAt(int i, float u) {
  SDL_FPoint a = mapAt(kTour[i][0], kTour[i][1]), b = mapAt(kTour[(i + 1) % 8][0], kTour[(i + 1) % 8][1]);
  float dx = b.x - a.x, dy = b.y - a.y, len = SDL_sqrtf(dx * dx + dy * dy), lift = SDL_min(26.0f, len * 0.22f);
  return (SDL_FPoint) {a.x + dx * u, a.y + dy * u - lift * 4 * u * (1 - u)};
}

static void drawTourMap(Uint64 t) {
  SDL_Rect clip = {0, 0, VIEW_W, (int) HEADER_H};
  SDL_SetRenderClipRect(L.renderer, &clip);
  const Rgb grid = {150, 166, 206}, route = {255, 92, 56}, light = {255, 214, 170};
  // The grid: dots at every 15 degrees, fading towards the map's left edge.
  for(float lon = -120; lon <= 150; lon += 15)
    for(float lat = -30; lat <= 70; lat += 15) {
      SDL_FPoint q = mapAt(lon, lat);
      float edge = SDL_min(1.0f, (q.x - MAP_X0) / 120.0f);
      glowDot(q, 1.8f, grid, 0.34f * edge);
    }
  // The legs.
  SDL_FPoint p[MAX_LINE];
  float a[MAX_LINE];
  for(int i = 0; i < 7; i++) {
    int n = 40;
    for(int k = 0; k < n; k++) p[k] = legAt(i, (float) k / (n - 1)), a[k] = 0.45f;
    softLine(p, a, n, 2.4f, route);
  }
  // The light flying the tour: a leg every 1.6 seconds, a fading trail behind it.
  float tour = (float) (t % 11200) / 1600.0f;
  int leg = (int) tour;
  float u = tour - leg;
  if(leg < 7) {
    int n = 0;
    for(int k = 0; k <= 24; k++) {
      float uu = u - 0.3f * (1 - k / 24.0f);
      if(uu < 0) continue;
      p[n] = legAt(leg, uu), a[n++] = k / 24.0f;
    }
    softLine(p, a, n, 3.2f, light);
    glowDot(legAt(leg, u), 7, light, 0.9f);
  }
  // The cities, the one just reached brighter.
  for(int i = 0; i < 8; i++) {
    SDL_FPoint q = mapAt(kTour[i][0], kTour[i][1]);
    bool reached = leg < 7 ? i <= leg : true;
    glowDot(q, 6, route, reached ? 0.55f : 0.25f);
    glowDot(q, 2.4f, (Rgb) {255, 236, 224}, reached ? 1.0f : 0.6f);
  }
  SDL_SetRenderClipRect(L.renderer, NULL);
}

// --- The Music card and the panels over the launcher ----------------------------------------

static SDL_FRect gModalRect[C_COUNT];  // where the open panel's controls are, for the mouse

static void button(float x, float y, float w, float h, const char* label, bool focused, Rgb face) {
  panel(x, y, w, h, focused ? kAccent : face, 255);
  text(x + (w - textWidth(label, 2)) / 2, y + (h - 16) / 2, 2, kText, label);
}

static float buttonWidth(const char* label) { return textWidth(label, 2) + 36; }

static void musicButtonRect(int which, SDL_FRect* r) {
  MusicAction a, b;
  musicActions(&a, &b);
  float x = MUSIC_X + 18;
  if(which == 1 && a != MA_NONE) x += buttonWidth(actionLabel(a)) + 12;
  *r = (SDL_FRect) {x, MBTN_Y, buttonWidth(actionLabel(which == 0 ? a : b)), MBTN_H};
}

static const char* stepWords(char* buf, size_t n) {
  int k = tg_mb_step_number(L.mb.step);
  switch(L.mb.step) {
    case TG_MB_STEP_NOTES: snprintf(buf, n, "Step %d of %d: reading your ROM's notes", k, TG_MB_STEPS); break;
    case TG_MB_STEP_PLAN: snprintf(buf, n, "Step %d of %d: working out what to download", k, TG_MB_STEPS); break;
    case TG_MB_STEP_LIBRARIES: snprintf(buf, n, "Step %d of %d: instrument libraries", k, TG_MB_STEPS); break;
    case TG_MB_STEP_RENDER:
      snprintf(buf, n, "Step %d of %d: %d of %d tracks made", k, TG_MB_STEPS, L.mb.tracksDone,
               L.mb.tracksTotal > 0 ? L.mb.tracksTotal : TG_MB_TRACKS);
      break;
    default: snprintf(buf, n, "Step %d of %d: getting the music tools", k, TG_MB_STEPS); break;
  }
  return buf;
}

static void drawMusicCard(Uint64 t) {
  const float x = MUSIC_X, y = MUSIC_Y, w = MUSIC_W;
  panel(x, y, w, MUSIC_H, kPanel, 255);
  text(x + 18, y + 14, 2, kAccent, "MODERN MUSIC");
  const char* pill = "ORIGINAL ONLY";
  Rgb pillFace = kPanelHi, pillText = kDim, headColor = kText;
  char head[96], detail[480] = "", eta[64] = "";
  double bar = -2;  // no bar
  if(!L.toolsDir[0]) {
    snprintf(head, sizeof(head), "Original music only");
    snprintf(detail, sizeof(detail), "The music tools aren't beside the launcher (its music-tools folder), so the modern "
             "music can't be built from here.");
  } else if(!L.romOk) {
    snprintf(head, sizeof(head), "Original music only");
    snprintf(detail, sizeof(detail), "Choose your ROM first: the modern music is made from it, here on this computer.");
  } else if(L.mb.phase == TG_MB_RUNNING) {
    pill = "BUILDING", pillFace = (Rgb) {120, 52, 34}, pillText = kText;
    stepWords(head, sizeof(head));
    bar = L.mb.fraction;
    snprintf(detail, sizeof(detail), "%s", L.mb.detail[0] ? L.mb.detail : L.mb.stepText);
    tg_mb_format_eta(L.mb.etaS, eta, sizeof(eta));
  } else if(L.mb.phase == TG_MB_CANCELLING) {
    pill = "STOPPING";
    snprintf(head, sizeof(head), "Stopping...");
    snprintf(detail, sizeof(detail), "Finishing what is being written. Everything done is kept.");
  } else if(L.mb.phase == TG_MB_FAILED) {
    pill = "PROBLEM", pillFace = (Rgb) {96, 36, 40}, pillText = kText, headColor = kBad;
    snprintf(head, sizeof(head), "The music build stopped");
    snprintf(detail, sizeof(detail), "%s", L.mb.errorText);
  } else if(L.mb.phase == TG_MB_STOPPED && L.tracks < TG_MB_TRACKS) {
    pill = "PAUSED";
    snprintf(head, sizeof(head), "Stopped: %d of %d tracks made", L.tracks, TG_MB_TRACKS);
    snprintf(detail, sizeof(detail), "Everything done is kept, downloads too: Resume goes on from there. The "
             "original music plays meanwhile.");
  } else if(L.tracks == 0) {
    snprintf(head, sizeof(head), "Original music only");
    snprintf(detail, sizeof(detail), "Made here from your ROM: %d tracks in a few hours, with about %.1f GB of free "
             "instruments downloaded once.", TG_MB_TRACKS,
             tg_mb_consent_typical_mb(&L.consent, TG_MB_STYLES_ALL, true, windowsBuild()) / 1000.0);
  } else if(L.tracks < TG_MB_TRACKS) {
    pill = "PART BUILT", pillFace = (Rgb) {38, 92, 64}, pillText = kGood;
    snprintf(head, sizeof(head), "Modern music: %d of %d tracks", L.tracks, TG_MB_TRACKS);
    snprintf(detail, sizeof(detail), "Choose them in Sound > Music. Build the rest any time.");
  } else {
    pill = "READY", pillFace = (Rgb) {38, 92, 64}, pillText = kGood, headColor = kGood;
    snprintf(head, sizeof(head), "Modern music: ready, %d of %d", L.tracks, TG_MB_TRACKS);
    snprintf(detail, sizeof(detail), "Choose a version in Sound > Music above. It stays on this computer: keep it "
             "to yourself.");
  }
  float pw = textWidth(pill, 2) + 24;
  panel(x + w - 18 - pw, y + 10, pw, 24, pillFace, 255);
  text(x + w - 18 - pw + 12, y + 14, 2, pillText, pill);
  char line[64];
  fit(line, sizeof(line), head, (int) ((w - 36) / 16));
  text(x + 18, y + 42, 2, headColor, line);
  float ty = y + 68;
  if(bar > -2) {
    float bw = w - 36 - 72, bx = x + 18, by = y + 70;
    fill(bx, by, bw, 12, kPanelHi, 255);
    if(bar >= 0) {
      fill(bx, by, bw * (float) (bar > 1 ? 1 : bar), 12, kAccent, 255);
      char pct[16];
      snprintf(pct, sizeof(pct), "%d%%", (int) (bar * 100 + 0.5));
      text(x + w - 18 - textWidth(pct, 2), by - 2, 2, kText, pct);
    } else {
      // Not measurable: a light running along the bar.
      float u = (float) (t % 1600) / 1600.0f;
      fill(bx + (bw - 80) * u, by, 80, 12, kAccent, 255);
    }
    ty = y + 92;
  }
  wrappedLines(x + 18, ty, w - 36, 2, L.mb.phase == TG_MB_FAILED ? kText : kDim, detail, bar > -2 ? 2 : 3);
  // Buttons, and the time left beside them.
  MusicAction a, b;
  musicActions(&a, &b);
  for(int i = 0; i < 2; i++) {
    MusicAction act = i == 0 ? a : b;
    if(act == MA_NONE) continue;
    SDL_FRect r;
    musicButtonRect(i, &r);
    button(r.x, r.y, r.w, r.h, actionLabel(act), L.modal == MODAL_NONE && L.focus == (i == 0 ? F_MUSIC1 : F_MUSIC2),
           act == MA_BUILD || act == MA_RETRY ? (Rgb) {64, 50, 60} : kPanelHi);
  }
  if(eta[0]) text(x + w - 18 - textWidth(eta, 2), MBTN_Y + 7, 2, kAccent2, eta);
}

static void dimBehind(void) { fill(0, 0, VIEW_W, VIEW_H, (Rgb) {6, 7, 12}, 210); }

static void modalPanel(float x, float y, float w, float h) {
  dimBehind();
  panel(x - 2, y - 2, w + 4, h + 4, kAccent, 255);
  panel(x, y, w, h, kPanel, 255);
}

static void modalButton(int item, float x, float y, const char* label, Rgb face) {
  float w = buttonWidth(label);
  gModalRect[item] = (SDL_FRect) {x, y, w, 34};
  button(x, y, w, 34, label, L.modalFocus == item, face);
}

static void drawOffer(void) {
  const float w = 840, h = 316, x = (VIEW_W - w) / 2, y = (VIEW_H - h) / 2;
  modalPanel(x, y, w, h);
  text(x + 28, y + 24, 3, kAccent2, "Build the modern music?");
  char body[600];
  snprintf(body, sizeof(body), "Race Masters can make modern versions of the game's music on this computer, from your "
           "own ROM: the seven songs in three styles and every city's tune in four, %d tracks. It downloads about %.1f GB "
           "of free instruments once and takes a few hours; you can play with the original music meanwhile.",
           TG_MB_TRACKS, tg_mb_consent_typical_mb(&L.consent, TG_MB_STYLES_ALL, true, windowsBuild()) / 1000.0);
  wrapped(x + 28, y + 72, w - 56, 2, kText, body);
  float bx = x + 28;
  static const char* const kLabels[3] = {"Build now...", "Later", "Don't ask again"};
  for(int i = 0; i < 3; i++) {
    modalButton(i, bx, y + h - 58, kLabels[i], i == 0 ? (Rgb) {64, 50, 60} : kPanelHi);
    bx += buttonWidth(kLabels[i]) + 14;
  }
}

static void drawQuit(void) {
  const float w = 760, h = 270, x = (VIEW_W - w) / 2, y = (VIEW_H - h) / 2;
  modalPanel(x, y, w, h);
  text(x + 28, y + 24, 3, kAccent2, "Stop building the music?");
  wrapped(x + 28, y + 72, w - 56, 2, kText, "Everything done is kept, the tracks and the downloads: the next Build music "
          "goes on from there. To keep it building, leave the launcher open (Start plays the game meanwhile).");
  modalButton(0, x + 28, y + h - 58, "Stop and quit", kPanelHi);
  modalButton(1, x + 28 + buttonWidth("Stop and quit") + 14, y + h - 58, "Keep building", (Rgb) {64, 50, 60});
}

static void drawConsent(void) {
  const float x = 40, y = 22, w = 1200, h = 676;
  modalPanel(x, y, w, h);
  const bool win = windowsBuild();
  const TgMbConsent* c = &L.consent;
  unsigned styles = L.consentStyles, libs = tg_mb_consent_libs(c, styles);
  int toolsMb = win ? c->tools.mbWindows : c->tools.mbLinux;
  int usualMb = tg_mb_consent_typical_mb(c, styles, false, win);
  int kept = 0, peak = 0, music = 0;
  tg_mb_consent_disk(c, styles, win, &kept, &peak, &music);
  int tracks = tg_mb_style_tracks(styles), jobs = buildJobs();
  float cx = x + 28, cy = y + 18, cw = w - 56;
  text(cx, cy, 3, kAccent2, "Build the modern music");
  cy += 40;
  cy += wrapped(cx, cy, cw, 2, kText, "Made here, from your own ROM, and never shared. The launcher reads the notes from "
                "your ROM, downloads from the free instrument libraries below only the samples those notes play, once, "
                "from their official sources, and renders every track on this computer, into:");
  char line[200], where[80];
  fitTail(where, sizeof(where), L.buildOut, (int) (cw / 16));
  text(cx, cy, 2, kAccent2, where);
  cy += 30;
  char a[32], b[32], tl[32], hrs[48];
  tg_mb_format_size((usualMb + toolsMb) * 1e6, a, sizeof(a));
  tg_mb_format_size(usualMb * 1e6, b, sizeof(b));
  tg_mb_format_size(toolsMb * 1e6, tl, sizeof(tl));
  snprintf(line, sizeof(line), "Downloads, once: about %s (instruments %s, Python tools %s)", a, b, tl);
  text(cx, cy, 2, kText, line);
  cy += 22;
  tg_mb_format_size(kept * 1e6, a, sizeof(a));
  tg_mb_format_size(peak * 1e6, b, sizeof(b));
  snprintf(line, sizeof(line), "Disk: about %s kept (about %s free needed while it builds)%s", a, b,
           L.consentClean ? ", 0.7 GB after" : "");
  if(L.consentClean) {
    char m[32];
    tg_mb_format_size((music + c->toolsMb) * 1e6, m, sizeof(m));
    snprintf(line, sizeof(line), "Disk: %s while it builds, %s kept after (libraries deleted)", b, m);
  }
  text(cx, cy, 2, kText, line);
  cy += 22;
  tg_mb_format_hours(estimateSeconds(styles), hrs, sizeof(hrs));
  snprintf(line, sizeof(line), "Time: %s here (%d track%s at a time); you can play meanwhile.", hrs, jobs, jobs == 1 ? "" : "s");
  text(cx, cy, 2, kText, line);
  cy += 32;
  text(cx, cy, 2, kDim, "From their official sources, under their own licences:");
  cy += 24;
  for(int i = 0; i <= c->nLibs; i++) {
    bool isTools = i == c->nLibs;
    const TgMbItem* it = isTools ? &c->tools : &c->libs[i];
    bool used = isTools || (libs & (1u << i));
    char name[64], lic[32], size[24];
    if(isTools) snprintf(name, sizeof(name), "Python %s, pip and 21 packages", TG_MB_PY_VERSION);
    else {
      const char* paren = strstr(it->name, " (");
      snprintf(name, sizeof(name), "%.*s", paren ? (int) (paren - it->name) : (int) strlen(it->name), it->name);
    }
    char nm[48];
    fit(nm, sizeof(nm), name, 40);
    fit(lic, sizeof(lic), it->licence, 21);
    int mb = isTools ? toolsMb : tg_mb_consent_lib_typical_mb(c, i, styles, win);
    if(used) tg_mb_format_size(mb * 1e6, size, sizeof(size));
    else snprintf(size, sizeof(size), "-");
    Rgb col = used ? kText : kFaint;
    text(cx + 8, cy, 2, col, nm);
    text(cx + 8 + 41 * 16, cy, 2, used ? kDim : kFaint, lic);
    text(cx + cw - 8 - textWidth(size, 2), cy, 2, col, size);
    cy += 20;
  }
  cy += 10;
  snprintf(line, sizeof(line), "Styles: %d tracks (the game's Music option picks one)", tracks);
  text(cx, cy, 2, kDim, line);
  cy += 24;
  float px = cx;
  for(int i = 0; i < TG_MB_STYLE_COUNT; i++) {
    bool on = styles & (1u << i), f = L.modalFocus == C_STYLE0 + i;
    float pw = textWidth(kTgMbStyleNames[i], 2) + 22;
    gModalRect[C_STYLE0 + i] = (SDL_FRect) {px, cy, pw, 30};
    panel(px, cy, pw, 30, f ? kAccent : on ? (Rgb) {38, 92, 64} : kPanelHi, 255);
    text(px + 11, cy + 7, 2, on || f ? kText : kFaint, kTgMbStyleNames[i]);
    px += pw + 8;
  }
  cy += 42;
  {
    const char* label = "When it's done, delete the libraries";
    const char* v = L.consentClean ? "Yes" : "No";
    float vw = textWidth(v, 2) + 24;
    bool f = L.modalFocus == C_CLEAN;
    gModalRect[C_CLEAN] = (SDL_FRect) {cx, cy - 4, cw, 30};
    if(f) panel(cx - 6, cy - 4, cw + 12, 30, kPanelHi, 255);
    text(cx, cy + 3, 2, kText, label);
    panel(cx + textWidth(label, 2) + 16, cy - 1, vw, 24, L.consentClean ? (Rgb) {38, 92, 64} : (Rgb) {52, 56, 80}, 255);
    text(cx + textWidth(label, 2) + 28, cy + 3, 2, L.consentClean ? kGood : kText, v);
    snprintf(line, sizeof(line), "(frees about %.1f GB)", (kept - music - c->toolsMb) / 1000.0);
    text(cx + textWidth(label, 2) + 28 + vw, cy + 3, 2, kDim, line);
  }
  text(cx, y + h - 92, 2, kFaint, "Licences in full: music-tools/music/THIRD_PARTY.md");
  modalButton(C_AGREE, cx, y + h - 54, "I agree: build the music", (Rgb) {64, 50, 60});
  modalButton(C_CANCEL, cx + buttonWidth("I agree: build the music") + 14, y + h - 54, "Cancel", kPanelHi);
}

static void drawModal(void) {
  memset(gModalRect, 0, sizeof(gModalRect));
  if(L.modal == MODAL_OFFER) drawOffer();
  else if(L.modal == MODAL_CONSENT) drawConsent();
  else if(L.modal == MODAL_QUIT) drawQuit();
}

static void draw(void) {
  Uint64 t = SDL_GetTicks();
  color(kBg, 255);
  SDL_RenderClear(L.renderer);
  // Header
  for(int y = 0; y < 86; y++) fill(0, (float) y, VIEW_W, 1, (Rgb) {(Uint8) (20 + y / 4), (Uint8) (22 + y / 6), (Uint8) (36 + y / 3)}, 255);
  drawTourMap(t);
  if(L.logo && L.logoBmp) {
    float lw, lh;
    SDL_GetTextureSize(L.logo, &lw, &lh);
    SDL_FRect r = {LEFT_X - 8, 5, 78 * lw / lh, 78};
    SDL_RenderTexture(L.renderer, L.logo, NULL, &r);
  } else if(L.logo) {
    float lw = (float) LOGO_ART_W / LAUNCHER_ART_SCALE, lh = (float) LOGO_ART_H / LAUNCHER_ART_SCALE;
    SDL_FRect r = {LEFT_X - 6, SDL_floorf((86 - lh) / 2), lw, lh};
    SDL_RenderTexture(L.renderer, L.logo, NULL, &r);
  } else {
    text(LEFT_X, 30, 3, kAccent, TG_APP_NAME);
  }
  fill(0, 86, VIEW_W, 2, kAccent, 255);

  // ROM card
  panel(LEFT_X, 110, LEFT_W, 172, kPanel, 255);
  text(LEFT_X + 20, 126, 2, kDim, "GAME ROM");
  char name[40];
  const char* shown = L.romPath[0] ? baseName(L.romPath) : "None chosen";
  if(strlen(shown) > 19) snprintf(name, sizeof(name), "%.18s~", shown);
  else snprintf(name, sizeof(name), "%s", shown);
  text(LEFT_X + 20, 152, 2, kText, name);
  wrapped(LEFT_X + 20, 180, LEFT_W - 40, 2, L.romOk ? kGood : kBad, L.romStatus);
  {
    bool f = L.focus == F_ROM;
    panel(LEFT_X + 20, ROMBTN_Y, LEFT_W - 40, 32, f ? kAccent : kPanelHi, 255);
    const char* label = L.dialogOpen ? "Choosing..." : L.romOk ? "Change ROM" : "Choose ROM";
    text(LEFT_X + 20 + (LEFT_W - 40 - textWidth(label, 2)) / 2, ROMBTN_Y + 8, 2, kText, label);
  }

  // Help for the focused option
  panel(LEFT_X, 300, LEFT_W, 272, kPanel, 255);
  if(L.focus >= F_OPTION0 && L.focus < F_OPTION0 + O_COUNT) {
    const Option* o = &kOptions[L.focus - F_OPTION0];
    text(LEFT_X + 20, 318, 2, kAccent2, o->label);
    float h = wrapped(LEFT_X + 20, 352, LEFT_W - 40, 2, kText, o->help);
    char hk[64];
    snprintf(hk, sizeof(hk), "In game: %s", o->hotkey);
    text(LEFT_X + 20, 352 + h + 10, 2, kDim, hk);
  } else if(L.focus == F_MUSIC1 || L.focus == F_MUSIC2) {
    text(LEFT_X + 20, 318, 2, kAccent2, "Modern music");
    char help[900];
    if(L.mb.phase == TG_MB_FAILED)
      snprintf(help, sizeof(help), "%s%s%s", L.mb.errorText, L.mb.logPath[0] ? " Details: " : "", L.mb.logPath);
    else if(building())
      snprintf(help, sizeof(help), "It goes on while you play: press Start (the launcher stays open, minimized). "
               "Closing the launcher pauses it; Cancel keeps everything done.");
    else
      snprintf(help, sizeof(help), "Arrangements of the game's music, made on this computer from your own ROM: only "
               "free instrument libraries are downloaded, nothing of the game's. They stay here; don't share them.");
    wrappedLines(LEFT_X + 20, 352, LEFT_W - 40, 2, kText, help, 9);
  } else if(L.focus == F_ROM) {
    text(LEFT_X + 20, 318, 2, kAccent2, "Your ROM");
    wrapped(LEFT_X + 20, 352, LEFT_W - 40, 2, kText,
      "The game runs from your own unmodified Top Gear (USA) cartridge dump (.sfc or .smc). You can also drop the file on this window.");
  } else {
    text(LEFT_X + 20, 318, 2, kAccent2, "Ready");
    wrapped(LEFT_X + 20, 352, LEFT_W - 40, 2, kText,
      "Starts the game with these options. Tab (or the gamepad's guide button) opens the options menu while you play.");
  }

  // Play: a dark button with a thin orange edge, the word in the logo's chrome
  {
    bool f = L.focus == F_PLAY;
    float pulse = f ? 0.5f + 0.5f * SDL_sinf((float) t / 260.0f) : 0;
    panel(LEFT_X, PLAY_Y, LEFT_W, 80, f ? kPanelHi : kPanel, 255);
    Rgb edge = !L.romOk ? kFaint : f ? (Rgb) {255, (Uint8) (120 + 76 * pulse), 64} : kAccent;
    color(edge, 255);
    for(int k = 0; k < (f ? 3 : 2); k++) {
      SDL_FRect r = {LEFT_X + k, PLAY_Y + k, LEFT_W - 2 * k, 80 - 2.0f * k};
      SDL_RenderRect(L.renderer, &r);
    }
    if(L.playWord) {
      // Greyed out until there is a ROM to play.
      Uint8 v = L.romOk ? 255 : 110;
      SDL_SetTextureColorMod(L.playWord, v, v, v);
      const float sw = (float) START_ART_W / LAUNCHER_ART_SCALE, sh = (float) START_ART_H / LAUNCHER_ART_SCALE;
      SDL_FRect r = {SDL_roundf(LEFT_X + (LEFT_W - sw) / 2), SDL_roundf(PLAY_Y + (80 - sh) / 2), sw, sh};
      SDL_RenderTexture(L.renderer, L.playWord, NULL, &r);
    }
  }

  // Options list (scrolls to keep the focused row in view)
  float total = optionY(O_COUNT - 1) + 30;
  float want = L.scroll;
  if(L.focus >= F_OPTION0 && L.focus < F_OPTION0 + O_COUNT) {
    float fy = optionY(L.focus - F_OPTION0);
    if(fy - 40 < want) want = fy - 40;
    if(fy + 30 > want + LIST_H) want = fy + 30 - LIST_H;
  }
  if(want > total - LIST_H) want = total - LIST_H;
  if(want < 0) want = 0;
  L.scroll += (want - L.scroll) * 0.25f;
  for(int i = 0; i < O_COUNT; i++) {
    const Option* o = &kOptions[i];
    float y = LIST_Y + optionY(i) - L.scroll;
    if(y < LIST_Y - 2 || y + 26 > LIST_Y + LIST_H) continue;  // only whole rows
    if(o->group && y - 24 >= LIST_Y - 30) {
      text(LIST_X, y - 24, 2, kAccent, o->group);
      fill(LIST_X + textWidth(o->group, 2) + 10, y - 19, LIST_W - textWidth(o->group, 2) - 10, 1, kFaint, 255);
    }
    bool f = L.focus == F_OPTION0 + i, on = optionEnabled(i);
    if(f) panel(LIST_X - 4, y - 2, LIST_W + 8, 28, kPanelHi, 255);
    else if(L.hover == i) panel(LIST_X - 4, y - 2, LIST_W + 8, 28, kPanel, 255);
    if(f) fill(LIST_X - 4, y - 2, 3, 28, kAccent, 255);
    text(LIST_X + 8, y + 4, 2, on ? kText : kFaint, o->label);
    // The value as a pill, with arrows when focused.
    const char* v = o->choices[L.value[i]];
    bool lit = o->kind == OPT_BOOL ? L.value[i] != 0 : o->kind == OPT_FULLVIEW ? L.value[i] != 0 : true;
    float vw = textWidth(v, 2) + 24, vx = LIST_X + LIST_W - vw - 24;
    panel(vx, y + 1, vw, 22, !on ? kPanel : lit && o->kind != OPT_CHOICE ? (Rgb) {38, 92, 64} : (Rgb) {52, 56, 80}, 255);
    text(vx + 12, y + 4, 2, !on ? kFaint : lit && o->kind != OPT_CHOICE ? kGood : kText, v);
    if(f && on) {
      text(vx - 18, y + 4, 2, kAccent2, "<");
      text(LIST_X + LIST_W - 16, y + 4, 2, kAccent2, ">");
    }
  }

  drawMusicCard(t);

  // Footer: controls, or a passing note
  if(L.message[0] && t < L.messageUntil) {
    text(LEFT_X, VIEW_H - 24, 2, L.messageGood ? kAccent2 : kBad, L.message);
  } else {
    bool pad = L.pads[0] || L.pads[1] || L.pads[2] || L.pads[3];
    text(LEFT_X, VIEW_H - 24, 2, kFaint, pad ? "D-pad: choose   A: change   Start: play   B: quit"
                                              : "Arrows: choose   Enter: change   Space: start   Esc: quit");
  }
  drawModal();
}

// --- Input ---------------------------------------------------------------------------

static int hitTest(float x, float y) {
  if(x >= LEFT_X + 20 && x < LEFT_X + LEFT_W - 20 && y >= ROMBTN_Y && y < ROMBTN_Y + 32) return F_ROM;
  if(x >= LEFT_X && x < LEFT_X + LEFT_W && y >= PLAY_Y && y < PLAY_Y + 80) return F_PLAY;
  for(int i = 0; i < 2; i++) {
    int f = i == 0 ? F_MUSIC1 : F_MUSIC2;
    if(!focusable(f)) continue;
    SDL_FRect r;
    musicButtonRect(i, &r);
    if(x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) return f;
  }
  if(x >= LIST_X - 4 && x < LIST_X + LIST_W + 4 && y >= LIST_Y - 4 && y < LIST_Y + LIST_H + 4) {
    for(int i = 0; i < O_COUNT; i++) {
      float ry = LIST_Y + optionY(i) - L.scroll;
      if(y >= ry - 2 && y < ry + 26) return F_OPTION0 + i;
    }
  }
  return -1;
}

static int modalHit(float x, float y) {
  for(int i = 0; i < modalItems(); i++) {
    SDL_FRect r = gModalRect[i];
    if(r.w > 0 && x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) return i;
  }
  return -1;
}

static void openPad(SDL_JoystickID id) {
  for(int i = 0; i < 4; i++) if(L.pads[i] == NULL) { L.pads[i] = SDL_OpenGamepad(id); return; }
}

static void closePad(SDL_JoystickID id) {
  for(int i = 0; i < 4; i++) if(L.pads[i] && SDL_GetGamepadID(L.pads[i]) == id) { SDL_CloseGamepad(L.pads[i]); L.pads[i] = NULL; }
}

// Held directions (stick or d-pad) repeat.
static int padDirection(void) {
  int dir = 0;
  for(int i = 0; i < 4; i++) {
    if(L.pads[i] == NULL) continue;
    Sint16 y = SDL_GetGamepadAxis(L.pads[i], SDL_GAMEPAD_AXIS_LEFTY);
    Sint16 x = SDL_GetGamepadAxis(L.pads[i], SDL_GAMEPAD_AXIS_LEFTX);
    if(SDL_GetGamepadButton(L.pads[i], SDL_GAMEPAD_BUTTON_DPAD_UP) || y < -16000) dir = 1;
    else if(SDL_GetGamepadButton(L.pads[i], SDL_GAMEPAD_BUTTON_DPAD_DOWN) || y > 16000) dir = 2;
    else if(SDL_GetGamepadButton(L.pads[i], SDL_GAMEPAD_BUTTON_DPAD_LEFT) || x < -16000) dir = 3;
    else if(SDL_GetGamepadButton(L.pads[i], SDL_GAMEPAD_BUTTON_DPAD_RIGHT) || x > 16000) dir = 4;
    if(dir) break;
  }
  return dir;
}

static void direction(int dir) {
  if(L.modal != MODAL_NONE) modalMove(dir);
  else if(dir == 1) moveFocus(-1);
  else if(dir == 2) moveFocus(1);
  else if(L.focus >= F_OPTION0 && L.focus < F_OPTION0 + O_COUNT) change(L.focus - F_OPTION0, dir == 3 ? -1 : 1);
  else if((L.focus == F_MUSIC1 || L.focus == F_MUSIC2) && focusable(F_MUSIC1 + F_MUSIC2 - L.focus))
    L.focus = F_MUSIC1 + F_MUSIC2 - L.focus;  // between the card's two buttons
}

static void pollPadRepeat(void) {
  int dir = padDirection();
  Uint64 now = SDL_GetTicks();
  if(dir != L.heldDir) {
    L.heldDir = dir;
    if(dir) { direction(dir); L.nextRepeat = now + 380; }
  } else if(dir && now >= L.nextRepeat && dir <= 2 && L.modal == MODAL_NONE) {
    direction(dir);
    L.nextRepeat = now + 110;
  }
}

// Ends a build that is still running when the launcher closes (after "Stop and quit", or
// a test picture): it is asked to stop, then ended if it hasn't after a few seconds. The
// programs keep everything done either way.
static void stopBuildAtExit(void) {
  if(L.proc == NULL) return;
  if(L.mb.phase == TG_MB_RUNNING) cancelBuild();
  Uint64 until = SDL_GetTicks() + 15000;
  while(L.proc && SDL_GetTicks() < until) {
    pumpOutput();
    int code;
    if(SDL_WaitProcess(L.proc, false, &code)) break;
    SDL_Delay(50);
  }
  if(L.proc) {
    SDL_KillProcess(L.proc, true);
    SDL_WaitProcess(L.proc, true, NULL);
    SDL_DestroyProcess(L.proc);
    L.proc = NULL;
  }
}

int main(int argc, char** argv) {
  (void) argc;
  (void) argv;
  SDL_SetAppMetadata(TG_APP_NAME " Launcher", "0.1", TG_APP_ID);
  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "0");
  if(!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, TG_APP_NAME, SDL_GetError(), NULL);
    return 1;
  }
  L.lock = SDL_CreateMutex();
  L.hover = -1;
  char* pref = SDL_GetPrefPath(TG_PREF_ORG, TG_PREF_APP);
  if(pref) {
    snprintf(L.settingsPath, sizeof(L.settingsPath), "%ssettings.txt", pref);
    snprintf(L.launcherPath, sizeof(L.launcherPath), "%slauncher.txt", pref);
    SDL_free(pref);
  }
  loadSettings();
  loadLauncher();
  findRom();
  L.focus = L.romOk ? F_PLAY : F_ROM;
  tg_user_data_dir(L.dataRoot, sizeof(L.dataRoot));
  findMusicTools();
  refreshMusic();
  tg_mb_reset(&L.mb);
  L.fake = SDL_getenv("TG_MUSIC_FAKE");

  if(!SDL_CreateWindowAndRenderer(TG_APP_NAME, VIEW_W, VIEW_H, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY, &L.window, &L.renderer)) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, TG_APP_NAME, SDL_GetError(), NULL);
    return 1;
  }
  SDL_SetRenderVSync(L.renderer, 1);
  SDL_SetRenderLogicalPresentation(L.renderer, VIEW_W, VIEW_H, SDL_LOGICAL_PRESENTATION_LETTERBOX);
  SDL_SetRenderDrawBlendMode(L.renderer, SDL_BLENDMODE_BLEND);
  loadLogo();
  L.playWord = artTexture(kStartArt, sizeof(kStartArt), START_ART_W, START_ART_H);

  // For checking the layout without a display: SDL_VIDEO_DRIVER=offscreen
  // TG_LAUNCHER_SHOT=out.bmp [TG_LAUNCHER_FOCUS=n] [TG_LAUNCHER_SHOT_MS=ms] saves a picture
  // (after 30 frames, or ms) and quits. TG_LAUNCHER_MODAL=offer|consent|quit opens that panel;
  // TG_MUSIC_FAKE=<scenario> runs tools/music/tests/fake_generate.py as the build at once;
  // TG_LAUNCHER_CANCEL_MS=ms presses its Cancel after ms. TG_MUSIC_DRY_RUN=1: Build music sets
  // up the tools for real but runs generate.py --dry-run (the notes and the plan; nothing
  // downloaded beyond the tools and the instruments' 0.5 MB of definition files).
  const char* shot = SDL_getenv("TG_LAUNCHER_SHOT");
  Uint64 shotAt = SDL_getenv("TG_LAUNCHER_SHOT_MS") ? SDL_GetTicks() + (Uint64) SDL_atoi(SDL_getenv("TG_LAUNCHER_SHOT_MS")) : 0;
  bool shotAtEnd = SDL_getenv("TG_LAUNCHER_SHOT_AT_END") != NULL;  // when the build has ended
  if(SDL_getenv("TG_LAUNCHER_FOCUS")) L.focus = SDL_atoi(SDL_getenv("TG_LAUNCHER_FOCUS"));
  if(L.fake || SDL_getenv("TG_MUSIC_RUNTIME_TEST") || SDL_getenv("TG_MUSIC_DRY_RUN")) {
    L.offerShown = true;
    startBuild();
  }
  Uint64 cancelAt = SDL_getenv("TG_LAUNCHER_CANCEL_MS") ? SDL_GetTicks() + (Uint64) SDL_atoi(SDL_getenv("TG_LAUNCHER_CANCEL_MS")) : 0;
  const char* modal = SDL_getenv("TG_LAUNCHER_MODAL");
  if(modal) {
    L.offerShown = true;
    if(strcmp(modal, "offer") == 0) L.modal = MODAL_OFFER;
    else if(strcmp(modal, "consent") == 0) openConsent();
    else if(strcmp(modal, "quit") == 0) L.modal = MODAL_QUIT, L.modalFocus = 1;
  }
  int frames = 0;
  bool running = true;
  while(running) {
    SDL_Event e;
    while(SDL_PollEvent(&e)) {
      SDL_ConvertEventToRenderCoordinates(L.renderer, &e);
      switch(e.type) {
        case SDL_EVENT_QUIT:
          if(requestQuit()) running = false;
          break;
        case SDL_EVENT_GAMEPAD_ADDED: openPad(e.gdevice.which); break;
        case SDL_EVENT_GAMEPAD_REMOVED: closePad(e.gdevice.which); break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
          if(L.dialogOpen) break;
          if(L.modal != MODAL_NONE) {
            if(e.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH) modalActivate(L.modalFocus);
            else if(e.gbutton.button == SDL_GAMEPAD_BUTTON_EAST) modalBack();
            break;
          }
          switch(e.gbutton.button) {
            case SDL_GAMEPAD_BUTTON_SOUTH: activate(1); break;
            case SDL_GAMEPAD_BUTTON_START: L.focus = F_PLAY; activate(1); break;
            case SDL_GAMEPAD_BUTTON_EAST: if(requestQuit()) running = false; break;
            case SDL_GAMEPAD_BUTTON_WEST: chooseRom(); break;
            case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: L.focus = F_ROM; break;
            case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: L.focus = F_PLAY; break;
          }
          break;
        case SDL_EVENT_KEY_DOWN:
          if(L.dialogOpen) break;
          if(L.modal != MODAL_NONE) {
            switch(e.key.key) {
              case SDLK_UP: direction(1); break;
              case SDLK_DOWN: direction(2); break;
              case SDLK_LEFT: direction(3); break;
              case SDLK_RIGHT: direction(4); break;
              case SDLK_TAB: L.modalFocus = (L.modalFocus + 1) % modalItems(); break;
              case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE: modalActivate(L.modalFocus); break;
              case SDLK_ESCAPE: modalBack(); break;
            }
            break;
          }
          switch(e.key.key) {
            case SDLK_UP: moveFocus(-1); break;
            case SDLK_DOWN: case SDLK_TAB: moveFocus(1); break;
            case SDLK_LEFT: direction(3); break;
            case SDLK_RIGHT: direction(4); break;
            case SDLK_RETURN: case SDLK_KP_ENTER: activate(1); break;
            case SDLK_SPACE: L.focus = F_PLAY; activate(1); break;
            case SDLK_ESCAPE: if(requestQuit()) running = false; break;
          }
          break;
        case SDL_EVENT_MOUSE_MOTION: {
          // Only a mouse that is really moving takes the focus (not one resting where
          // the window opened, while a gamepad is in use).
          static bool seen;
          static float lastX, lastY;
          bool moved = seen && (SDL_fabsf(e.motion.x - lastX) + SDL_fabsf(e.motion.y - lastY)) > 2;
          if(!seen || moved) { seen = true; lastX = e.motion.x; lastY = e.motion.y; }
          if(!moved) break;
          if(L.modal != MODAL_NONE) {
            int m = modalHit(e.motion.x, e.motion.y);
            if(m >= 0) L.modalFocus = m;
            break;
          }
          int h = hitTest(e.motion.x, e.motion.y);
          L.hover = h >= F_OPTION0 && h < F_OPTION0 + O_COUNT ? h - F_OPTION0 : -1;
          if(h >= 0) L.focus = h;
          break;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
          if(L.dialogOpen) break;
          if(L.modal != MODAL_NONE) {
            int m = modalHit(e.button.x, e.button.y);
            if(m >= 0) {
              L.modalFocus = m;
              modalActivate(m);
            }
            break;
          }
          int h = hitTest(e.button.x, e.button.y);
          if(h >= 0) {
            L.focus = h;
            activate(e.button.button == SDL_BUTTON_RIGHT ? -1 : 1);
          }
          break;
        }
        case SDL_EVENT_MOUSE_WHEEL:
          if(L.modal == MODAL_NONE) L.scroll -= e.wheel.y * 30;
          break;
        case SDL_EVENT_DROP_FILE:
          if(e.drop.data) checkRom(e.drop.data, true);
          break;
      }
    }
    SDL_LockMutex(L.lock);
    if(L.pickedReady) {
      L.pickedReady = false;
      char path[2048];
      snprintf(path, sizeof(path), "%s", L.picked);
      SDL_UnlockMutex(L.lock);
      if(checkRom(path, true)) L.focus = F_PLAY;
    } else {
      SDL_UnlockMutex(L.lock);
    }
    if(cancelAt && SDL_GetTicks() >= cancelAt) {
      cancelAt = 0;
      cancelBuild();
    }
    pollBuild();
    if(!running) break;
    maybeOffer();
    if(!focusable(L.focus)) L.focus = focusable(F_MUSIC1) ? F_MUSIC1 : F_PLAY;
    if(!L.dialogOpen) pollPadRepeat();
    // Minimized or covered (the game is playing): no drawing, just looking after the build.
    if(!shot && (SDL_GetWindowFlags(L.window) & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN | SDL_WINDOW_OCCLUDED))) {
      SDL_Delay(100);
      continue;
    }
    draw();
    frames++;
    bool ended = shotAtEnd && L.stage == ST_NONE && L.proc == NULL && L.mb.phase != TG_MB_IDLE && !building();
    if(shot && (shotAtEnd ? ended : shotAt ? SDL_GetTicks() >= shotAt : frames == 30)) {
      SDL_Surface* pic = SDL_RenderReadPixels(L.renderer, NULL);
      if(pic) { SDL_SaveBMP(pic, shot); SDL_DestroySurface(pic); }
      running = false;
    }
    SDL_RenderPresent(L.renderer);
    if(shot && (shotAt || shotAtEnd)) SDL_Delay(5);  // (offscreen: no vsync to wait for)
  }
  stopBuildAtExit();
  for(int i = 0; i < 4; i++) if(L.pads[i]) SDL_CloseGamepad(L.pads[i]);
  SDL_Quit();
  return 0;
}
