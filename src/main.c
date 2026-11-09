// Windows (and desktop) frontend: window, fullscreen, scaling, keyboard, gamepads, audio.
//
// Every session is recorded to recordings/ next to the executable: the inputs (which
// replay exactly, for regression tests) and which parts of the ROM ran (for the
// disassembly). Command line: race-masters [rom] [--no-record] [--replay file.txt]
// Debugging a replay without a window (SDL_VIDEO_DRIVER=offscreen): --window WxH,
// --shots DIR [--shots-every N] [--shots-from F] (the window's picture as BMPs), --frames N
// (quit after N game frames), --fake-hz N (pictures paced as on an N Hz display, not the clock),
// --toggle-3d-at F[,F...] (F7 pressed before game frame F: the 3D view on or off, with its
// transition).

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "brand.h"
#include "coverage.h"
#include "game.h"
#include "input_script.h"
#include "port/port.h"
#include "rom.h"
#include "sha256.h"
#include "gl3d.h"
#include "view3d.h"
#include "music.h"
#include "userdir.h"
#include "select_input.h"
#include "country_input.h"
#include "transition.h"

#define SNES_W 256
#define SNES_H 224
// NTSC SNES: 21.477272 MHz master clock / (1364 * 262 clocks per frame).
#define FRAME_NS (1000000000.0 * 1364.0 * 262.0 / 21477272.0)
#define AUDIO_RATE 48000
// The SDL renderer fallback's textures, one per view3d picture: as many as gl3d.c keeps
// (tg_view3d_pic_count is 517 - the HUD font and the menus' pictures are past 512).
#define V3_PICS 1024

typedef enum { ASPECT_4_3, ASPECT_8_7, ASPECT_COUNT } AspectMode;
static const char* aspectNames[] = {"4:3 (TV)", "8:7 (square pixels)"};
// Display filters (F4): sharp pixels, sharp-bilinear (pixels scaled up by a whole number,
// then smoothly to the window, so they stay crisp without uneven widths), or scanlines.
typedef enum { FILTER_SHARP, FILTER_SMOOTH, FILTER_SCANLINES, FILTER_COUNT } FilterMode;
static const char* filterNames[] = {"sharp", "sharp-bilinear", "scanlines"};
// What a picture shows: the game's own picture (2D), a race in 3D, a menu redrawn in 3D.
typedef enum { PIC_2D, PIC_RACE3D, PIC_MENU3D } PicKind;

static struct {
  SDL_Window* window;
  SDL_Renderer* renderer;
  SDL_Texture* texture;
  SDL_AudioStream* audio;
  SDL_Gamepad* pads[2];
  TgGame* game;
  bool fullscreen;
  bool integerScale;
  AspectMode aspect;
  FilterMode filter;
  bool rumble;  // controller rumble in races (F8, --rumble)
  bool tvColour;  // colours as a TV showed them (a CRT's deeper gamma, a little more saturation) rather than exact
  SDL_Texture* upscaled;  // sharp-bilinear's intermediate picture
  int upscaledFactor, upscaledW;
  SDL_Texture* wideTexture;  // the widescreen picture, wideTextureW x 224
  int wideTextureW;
  uint16_t keys[3];  // keyboard state per player
  uint32_t frame;  // frames run since power-on
  // recording
  FILE* record;
  uint16_t recorded[3];
  bool resetRequested;
  TgCoverage* coverage;
  char coveragePath[2400];
  // replay
  TgInputScript replay;
  bool replaying;
  bool original;  // the game's own code throughout (--original)
  bool smooth;  // enhancement: race logic every frame instead of every third (F6, --smooth)
  bool recordedSmooth;
  bool highFps;  // enhancement: in-between frames at the display's rate (Shift+F6, --high-fps)
  bool vsync;
  bool fullView;  // enhancement: 1-player races fill the whole screen (F9, --full-view)
  bool recordedFullView;
  bool tallRoad;  // full view with the road twice as tall, filling the screen (F9 again, --tall-road)
  bool recordedTallRoad;
  bool mapTop;  // full view: the map at the top left instead of above the gear
  bool speedBottom;  // full view: the speedometer and timer at the bottom right
  bool wide;  // enhancement: races drawn as wide as the window (F10, --wide)
  bool sideBySide;  // enhancement: 2-player widescreen views side by side (F12, --side-by-side)
  bool recordedSideBySide;
  bool ghost;  // enhancement: race against your best race on each course (F1, --ghost)
  bool recordedGhost;
  char ghostPath[2400];  // where the best races are kept; empty when there is nowhere
  bool autoSave;  // enhancement: countries reached are remembered between sessions (Shift+F1, --auto-save)
  uint8_t saved[3];  // the countries open at each skill level, as saved
  char progressPath[2400];
  char settingsPath[2400];  // the options, remembered between sessions
  int recordedWide;
  // 3D mod (docs/3D-MOD.md): races drawn in real 3D from the game's own state (F7, --3d)
  bool view3d;
  bool models3d;  // simple models instead of the game's own pictures (Ctrl+F7)
  bool classic3d; // the game's pictures or simple models, not the modern look (Ctrl+F7)
  bool gl3d;      // the 3D races drawn by gl3d.c (OpenGL, a depth buffer)
  int camera3d;   // the game's camera, a high one, or the bumper (Shift+F7)
  bool titleClassic;  // the title screen (3D, modern look): the original's logo rebuilt in 3D, not the modern one
  bool race3d;    // a race is on, so the 3D view has something to draw
  TgView3D* v3;
  TgV3List v3list;
  SDL_Texture* v3tex[V3_PICS];  // (the SDL renderer fallback: a texture per view3d picture)
  uint32_t v3ver[V3_PICS];
  int v3w[V3_PICS], v3h[V3_PICS];
  uint32_t* v3pixels;
  char toast[96];       // a short message shown over the picture (fullscreen hides the title)
  Uint64 toastUntil;
  // The frame-time log (Shift+F2, --perf-log FILE, TG_PERF_LOG=FILE): a CSV line a picture.
  FILE* perf;
  Uint64 perfStart, perfLast;
  struct { double game, capture, build, gl, present; int frames; } perfNow;
  // Modern music (docs/AUDIO.md): the player's remade songs from audio-modern/ next to the
  // game, in place of the sound chip's music; musicSet is a TgMusicSet (Tab menu, --music).
  TgMusic* music;
  int musicSet;
  // The modern car select's Left / Right through the cars in a line (select_input.h).
  TgSelectInput selectInput[3];
  // Country select FREE: a locked country picked on the country screen gets its password
  // typed by the frontend (country_input.h); off (PASSWORD), the player types it.
  bool countryFree;
  TgCountryInput countryInput;
  // Debugging (--shots DIR, --shots-every N, --shots-from F, --frames N): the picture shown,
  // as the window has it, written to DIR/frameNNNNNN.bmp when the game reaches a shot frame;
  // --frames quits after N game frames. For replays with SDL's offscreen video driver.
  const char* shotsDir;
  uint32_t shotsEvery, shotsNext, quitAt;
  // --fake-hz N: the main loop's clock moves 1/N s a picture shown (not the wall clock), so a
  // slow offscreen replay paces pictures and game frames as a display at N Hz would.
  uint32_t fakeHz;
  Uint64 fakeNs;
  // F7's transition (transition.h): a clock's hand sweeping the 3D view in over the original
  // picture or back, 0.5 s, presentation only (view3d changes at the press as it always did).
  // On unless turned off (options menu "2D/3D transition", mode_transition, --no-transition).
  bool modeTransition;
  TgTransition trans;
  PicKind trans3d;       // the 3D picture it sweeps: the live one while 3D is on, else the last drawn
  uint32_t transFrame;   // the game frame of the last press
  Uint64 transLast;      // loopNow() at its last picture
  SDL_Texture* trans2d;  // the original picture as it was when 3D came on (held still meanwhile)
  int trans2dTexW, trans2dTexH, trans2dW;
  bool trans2dWide, trans2dOk;
  SDL_Texture* trans3dTex;  // the SDL renderer fallback: the 3D picture, drawn through its share
  int trans3dTexW, trans3dTexH;
  TgV3List transList;
  PicKind shownKind;     // what the last picture showed
  uint32_t toggleAt[16];  // --toggle-3d-at: F7 pressed before these game frames
  int toggleCount, toggleNext;
} app;

static Uint64 loopNow(void) { return app.fakeHz ? app.fakeNs : SDL_GetTicksNS(); }

// A line of the session's recording. Lines reach the disk at the end of the frame that wrote
// them (recordFlush): stdio's buffer was written only when the game closed, and Windows has no
// line buffering (setvbuf _IOLBF is full buffering there), so a crash left an empty file.
static bool recordDirty;
static void SDL_PRINTF_VARARG_FUNC(1) recordf(SDL_PRINTF_FORMAT_STRING const char* fmt, ...) {
  if(!app.record) return;
  va_list ap;
  va_start(ap, fmt);
  vfprintf(app.record, fmt, ap);
  va_end(ap);
  recordDirty = true;
}

static void recordFlush(void) {
  if(recordDirty && app.record) fflush(app.record);
  recordDirty = false;
}

static void showToast(const char* text);
static double msSince(Uint64 t0) { return (SDL_GetTicksNS() - t0) / 1e6; }

static void perfOpen(const char* path) {
  app.perf = fopen(path, "w");
  if(!app.perf) return;
  fprintf(app.perf, "# " TG_APP_NAME " frame-time log: one line a picture shown. Times in ms (CPU, this machine);\n"
                    "# gpu_ms the GPU's time for the 3D views (timer queries, it lags a frame or two).\n");
  fprintf(app.perf, "t_ms,frame,in_race,game_frames,game_ms,capture_ms,build_ms,gl_ms,present_ms,frame_ms,"
                    "triangles,draws,vertex_kb,upload_kb,uploads,gpu_ms,width,height\n");
  app.perfStart = app.perfLast = SDL_GetTicksNS();
  memset(&app.perfNow, 0, sizeof(app.perfNow));
  if(app.gl3d) tg_gl3d_set_perf(true);
}

static void perfClose(void) {
  if(!app.perf) return;
  fclose(app.perf);
  app.perf = NULL;
  if(app.gl3d) tg_gl3d_set_perf(false);
}

// Shift+F2: a new log in recordings/ (next to the session's inputs), or the log closed.
static void perfToggle(void) {
  if(app.perf) {
    perfClose();
    showToast("Frame-time log saved");
    return;
  }
  const char* base = SDL_GetBasePath();
  char dir[2048];
  snprintf(dir, sizeof(dir), "%srecordings", base ? base : "");
  if(!SDL_CreateDirectory(dir)) {
    char* pref = SDL_GetPrefPath(TG_PREF_ORG, TG_PREF_APP);
    if(pref == NULL) return;
    snprintf(dir, sizeof(dir), "%srecordings", pref);
    SDL_free(pref);
    if(!SDL_CreateDirectory(dir)) return;
  }
  SDL_Time now;
  SDL_DateTime t;
  SDL_GetCurrentTime(&now);
  SDL_TimeToDateTime(now, &t, true);
  char path[2300];
  snprintf(path, sizeof(path), "%s/perf-%04d%02d%02d-%02d%02d%02d.csv", dir, t.year, t.month, t.day, t.hour, t.minute, t.second);
  perfOpen(path);
  showToast(app.perf ? "Frame-time log on (Shift+F2 to stop)" : "Frame-time log: cannot write recordings/");
}

// After each picture: its line.
static void perfLine(double presentMs) {
  if(!app.perf) return;
  Uint64 now = SDL_GetTicksNS();
  TgGl3dStats gs = {0};
  if(app.gl3d) tg_gl3d_take_stats(&gs);
  int w = 0, h = 0;
  SDL_GetCurrentRenderOutputSize(app.renderer, &w, &h);
  fprintf(app.perf, "%.2f,%u,%d,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%d,%d,%zu,%zu,%d,%.3f,%d,%d\n", (now - app.perfStart) / 1e6, app.frame,
          app.race3d ? 1 : 0, app.perfNow.frames, app.perfNow.game, app.perfNow.capture, app.perfNow.build, app.perfNow.gl, presentMs,
          (now - app.perfLast) / 1e6, gs.triangles, gs.draws, gs.vertex_bytes / 1024, gs.upload_bytes / 1024, gs.uploads,
          gs.gpu_views ? gs.gpu_ms : -1.0, w, h);
  app.perfLast = now;
  memset(&app.perfNow, 0, sizeof(app.perfNow));
  static int lines;
  if(++lines % 120 == 0) fflush(app.perf);  // (most of it kept if the game is killed)
}

// --shots: the window's picture before it is shown (what SDL and gl3d.c drew into it).
static void shotIfDue(void) {
  if(!app.shotsDir || app.frame < app.shotsNext) return;
  SDL_Surface* s = SDL_RenderReadPixels(app.renderer, NULL);
  if(s) {
    char path[2400];
    snprintf(path, sizeof(path), "%s/frame%06u.bmp", app.shotsDir, app.frame);
    SDL_SaveBMP(s, path);
    SDL_DestroySurface(s);
  }
  app.shotsNext = (app.frame / app.shotsEvery + 1) * app.shotsEvery;
}

// SDL_RenderPresent, timed for the log (it waits for the display's refresh with vsync on).
static void presentTimed(void) {
  shotIfDue();
  Uint64 t0 = SDL_GetTicksNS();
  SDL_RenderPresent(app.renderer);
  perfLine(msSince(t0));
}

static const char* defaultRomNames[] = {"Top Gear (USA).sfc", "Top Gear (USA).smc", "topgear.sfc"};

static bool findRom(const char* romArg, TgRom* rom, char* message, size_t messageSize) {
  if(romArg) {
    TgRomStatus status = tg_rom_load(romArg, rom);
    if(status == TG_ROM_OK) return true;
    snprintf(message, messageSize, "%s\n\n%s\n\nSHA-256 of your file:\n%s", romArg, tg_rom_status_message(status), rom->sha256);
    return false;
  }
  const char* base = SDL_GetBasePath();
  for(size_t i = 0; i < SDL_arraysize(defaultRomNames); i++) {
    char path[2048];
    snprintf(path, sizeof(path), "%s%s", base ? base : "", defaultRomNames[i]);
    TgRomStatus status = tg_rom_load(path, rom);
    if(status == TG_ROM_OK) return true;
    if(status != TG_ROM_NOT_FOUND) {
      snprintf(message, messageSize, "%s\n\n%s\n\nSHA-256 of your file:\n%s", path, tg_rom_status_message(status), rom->sha256);
      return false;
    }
  }
  snprintf(message, messageSize,
    TG_APP_NAME " needs your own copy of the game's ROM.\n\n"
    "Open " TG_LAUNCHER_EXE " and choose your Top Gear (USA) ROM,\n"
    "or put it next to " TG_GAME_EXE " named \"Top Gear (USA).sfc\",\n"
    "or drag the ROM file onto " TG_GAME_EXE ".\n\n"
    "Expected SHA-256:\n" TG_ROM_SHA256);
  return false;
}

// Controls: controls.txt next to the game maps keys and gamepad buttons to SNES
// buttons (written with the defaults when missing).
static const char* kButtonNames[12] = {"B", "Y", "SELECT", "START", "UP", "DOWN", "LEFT", "RIGHT", "A", "X", "L", "R"};

typedef struct Binding {
  bool pad;
  int code;  // SDL_Keycode, or SDL_GamepadButton
  uint16_t button;
  int player;  // keys: 1 or 2 (gamepads: each pad is its own player)
} Binding;

static Binding bindings[96];
static int bindingCount;

static const char* kDefaultControls =
  "# " TG_APP_NAME " controls: one binding per line, <SNES button> key <key name> or\n"
  "# <SNES button> pad <gamepad button>. A button can have several bindings.\n"
  "# SNES buttons: B Y SELECT START UP DOWN LEFT RIGHT A X L R\n"
  "# Key names as SDL writes them (Z, Return, Right Shift, Keypad Enter, Space, ...).\n"
  "# Gamepad buttons by position: a (bottom) b (right) x (left) y (top) back start\n"
  "# leftshoulder rightshoulder dpup dpdown dpleft dpright. The left stick steers and the\n"
  "# triggers work as L and R. Start a line with P2 for player 2's keys (P2 B key N).\n"
  "# Delete this file to get the defaults back.\n"
  "UP key Up\nDOWN key Down\nLEFT key Left\nRIGHT key Right\n"
  "B key Z\nA key X\nY key A\nX key S\nL key Q\nR key W\n"
  "START key Return\nSTART key Keypad Enter\nSELECT key Right Shift\n"
  "B pad a\nA pad b\nY pad x\nX pad y\nL pad leftshoulder\nR pad rightshoulder\n"
  "START pad start\nSELECT pad back\nUP pad dpup\nDOWN pad dpdown\nLEFT pad dpleft\nRIGHT pad dpright\n";

static void parseControls(const char* text) {
  bindingCount = 0;
  while(*text) {
    const char* end = strchr(text, '\n');
    size_t n = end ? (size_t) (end - text) : strlen(text);
    char line[128];
    if(n >= sizeof(line)) n = sizeof(line) - 1;
    memcpy(line, text, n);
    line[n] = 0;
    text += end ? n + 1 : n;
    while(n > 0 && (line[n - 1] == '\r' || line[n - 1] == ' ')) line[--n] = 0;
    char name[16], device[8];
    int used = 0;
    const char* start = line;
    int player = 1;
    if(SDL_strncasecmp(start, "P2 ", 3) == 0) {
      player = 2;
      start += 3;
    }
    if(line[0] == '#' || sscanf(start, "%15s %7s %n", name, device, &used) < 2 || used == 0) continue;
    const char* what = start + used;
    int b = -1;
    for(int i = 0; i < 12; i++) if(SDL_strcasecmp(name, kButtonNames[i]) == 0) b = i;
    if(b < 0 || bindingCount == SDL_arraysize(bindings)) continue;
    Binding bind = {false, 0, (uint16_t) (1 << b), player};
    if(SDL_strcasecmp(device, "key") == 0) {
      bind.code = (int) SDL_GetKeyFromName(what);
      if(bind.code == SDLK_UNKNOWN) continue;
    } else if(SDL_strcasecmp(device, "pad") == 0) {
      bind.pad = true;
      bind.code = SDL_GetGamepadButtonFromString(what);
      if(bind.code == SDL_GAMEPAD_BUTTON_INVALID) continue;
    } else {
      continue;
    }
    bindings[bindingCount++] = bind;
  }
}

static void loadControls(void) {
  const char* base = SDL_GetBasePath();
  char path[2048];
  snprintf(path, sizeof(path), "%scontrols.txt", base ? base : "");
  size_t size;
  char* text = SDL_LoadFile(path, &size);
  if(text) {
    parseControls(text);
    SDL_free(text);
    if(bindingCount > 0) return;
  } else {
    SDL_SaveFile(path, kDefaultControls, strlen(kDefaultControls));
  }
  parseControls(kDefaultControls);
}

static uint16_t keyToButton(SDL_Keycode key, int player) {
  uint16_t b = 0;
  for(int i = 0; i < bindingCount; i++) if(!bindings[i].pad && bindings[i].player == player && bindings[i].code == (int) key) b |= bindings[i].button;
  return b;
}

static uint16_t padButtons(SDL_Gamepad* pad) {
  if(pad == NULL) return 0;
  uint16_t b = 0;
  for(int i = 0; i < bindingCount; i++) {
    if(bindings[i].pad && SDL_GetGamepadButton(pad, (SDL_GamepadButton) bindings[i].code)) b |= bindings[i].button;
  }
  const Sint16 dead = 16000;
  Sint16 x = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX);
  Sint16 y = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY);
  if(x < -dead) b |= TG_BTN_LEFT;
  if(x > dead) b |= TG_BTN_RIGHT;
  if(y < -dead) b |= TG_BTN_UP;
  if(y > dead) b |= TG_BTN_DOWN;
  // Triggers double as shoulder buttons.
  if(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > dead) b |= TG_BTN_L;
  if(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > dead) b |= TG_BTN_R;
  return b;
}

static void openPad(SDL_JoystickID id) {
  for(int i = 0; i < 2; i++) {
    if(app.pads[i] == NULL) {
      app.pads[i] = SDL_OpenGamepad(id);
      return;
    }
  }
}

static void closePad(SDL_JoystickID id) {
  for(int i = 0; i < 2; i++) {
    if(app.pads[i] && SDL_GetGamepadID(app.pads[i]) == id) {
      SDL_CloseGamepad(app.pads[i]);
      app.pads[i] = NULL;
    }
  }
}

// The options are remembered in settings.txt in the settings folder, one "name 0|1" per line.
typedef struct {
  const char* name;
  bool* value;
} Setting;

static const Setting kSettings[] = {
  {"full_screen", &app.fullscreen}, {"integer_scale", &app.integerScale}, {"rumble", &app.rumble},
  {"smooth", &app.smooth}, {"full_view", &app.fullView},
  {"tall_road", &app.tallRoad}, {"map_top", &app.mapTop}, {"speed_bottom", &app.speedBottom}, {"wide_screen", &app.wide}, {"side_by_side", &app.sideBySide},
  {"ghost", &app.ghost}, {"auto_save", &app.autoSave}, {"high_fps", &app.highFps}, {"tv_colour", &app.tvColour},
  {"race_3d", &app.view3d}, {"models3d", &app.models3d}, {"classic3d", &app.classic3d},
  {"title_classic", &app.titleClassic}, {"country_free", &app.countryFree}, {"mode_transition", &app.modeTransition},
};

// The 3D look: modern (new models, materials and sky), the game's own pictures, or simple
// models made from them.
static TgV3Look look3d(void) {
  return !app.classic3d ? TG_V3_LOOK_MODERN : app.models3d ? TG_V3_LOOK_MODELS : TG_V3_LOOK_SPRITES;
}

static const char* look3dName(void) {
  return !app.classic3d ? "modern" : app.models3d ? "models" : "game pictures";
}

static void nextLook3d(void) {
  if(!app.classic3d) app.classic3d = true, app.models3d = false;
  else if(!app.models3d) app.models3d = true;
  else app.classic3d = false;
}

static void loadSettings(void) {
  char* text = SDL_LoadFile(app.settingsPath, NULL);
  if(text == NULL) return;
  // Earlier launchers wrote "fullscreen" and "wide" for full_screen and wide_screen. The
  // game never writes those, so when one is there it is the launcher's latest choice: it
  // wins over the game's own line.
  int oldFull = -1, oldWide = -1;
  for(char* line = text; line && *line; ) {
    char* next = strchr(line, '\n');
    if(next) *next++ = 0;
    char name[32];
    int value;
    if(sscanf(line, "%31s %d", name, &value) == 2) {
      for(size_t i = 0; i < SDL_arraysize(kSettings); i++) if(strcmp(name, kSettings[i].name) == 0) *kSettings[i].value = value != 0;
      if(strcmp(name, "fullscreen") == 0) oldFull = value != 0;
      if(strcmp(name, "wide") == 0) oldWide = value != 0;
      if(strcmp(name, "aspect") == 0 && value >= 0 && value < ASPECT_COUNT) app.aspect = (AspectMode) value;
      if(strcmp(name, "filter") == 0 && value >= 0 && value < FILTER_COUNT) app.filter = (FilterMode) value;
      if(strcmp(name, "music") == 0 && value >= 0 && value < TG_MUSIC_SET_COUNT) app.musicSet = value;
    }
    line = next;
  }
  if(oldFull >= 0) app.fullscreen = oldFull;
  if(oldWide >= 0) app.wide = oldWide;
  SDL_free(text);
}

static void saveSettings(void) {
  if(!app.settingsPath[0] || app.replaying) return;
  FILE* f = fopen(app.settingsPath, "w");
  if(f == NULL) return;
  fprintf(f, "aspect %d\nfilter %d\nmusic %d\n", (int) app.aspect, (int) app.filter, app.musicSet);
  for(size_t i = 0; i < SDL_arraysize(kSettings); i++) fprintf(f, "%s %d\n", kSettings[i].name, *kSettings[i].value ? 1 : 0);
  fclose(f);
}

static void saveProgress(void) {
  FILE* f = app.progressPath[0] ? fopen(app.progressPath, "w") : NULL;
  if(f == NULL) return;
  fprintf(f, "%d %d %d\n", app.saved[0], app.saved[1], app.saved[2]);
  fclose(f);
}

static void loadProgress(void) {
  char* text = app.progressPath[0] ? SDL_LoadFile(app.progressPath, NULL) : NULL;
  if(text == NULL) return;
  int a, b, c;
  if(sscanf(text, "%d %d %d", &a, &b, &c) == 3) {
    app.saved[0] = (uint8_t) (a & 7);
    app.saved[1] = (uint8_t) (b & 7);
    app.saved[2] = (uint8_t) (c & 7);
  }
  SDL_free(text);
}

static void updateTitle(void) {
  saveSettings();
  char title[128];
  snprintf(title, sizeof(title), TG_APP_NAME "  |  %s, %s%s%s%s%s", aspectNames[app.aspect], filterNames[app.filter],
           app.integerScale ? ", integer scale" : "",
           app.smooth && !app.original ? ", smooth motion" : "",
           app.fullView && !app.original ? (app.tallRoad ? ", full-screen view (tall road)" : ", full-screen view") : "", app.rumble ? ", rumble" : "");
  if(app.wide && !app.original) SDL_strlcat(title, ", widescreen", sizeof(title));
  if(app.sideBySide && !app.original) SDL_strlcat(title, ", side by side", sizeof(title));
  if(app.ghost && !app.original) SDL_strlcat(title, ", ghost car", sizeof(title));
  if(app.autoSave) SDL_strlcat(title, ", auto-save", sizeof(title));
  if(app.highFps && !app.original) SDL_strlcat(title, ", high frame rate", sizeof(title));
  if(app.view3d && !app.original) {
    static const char* cams[] = {"game camera", "high camera", "bumper camera"};
    char t3[96];
    snprintf(t3, sizeof(t3), ", 3D (%s, %s)", look3dName(), cams[app.camera3d % 3]);
    SDL_strlcat(title, t3, sizeof(title));
  }
  SDL_SetWindowTitle(app.window, title);
}

static bool menuOpen(void);
static void drawMenu(void);

static void showToast(const char* text) {
  snprintf(app.toast, sizeof(app.toast), "%s", text);
  app.toastUntil = SDL_GetTicksNS() + 2000000000ull;
}

static void drawToast(void) {
  if(!app.toast[0] || SDL_GetTicksNS() > app.toastUntil) return;
  int outW, outH;
  SDL_GetCurrentRenderOutputSize(app.renderer, &outW, &outH);
  float s = outH >= 720 ? 2 : 1;
  SDL_SetRenderScale(app.renderer, s, s);
  float w = 8 * (float) strlen(app.toast) + 16, x = (outW / s - w) / 2, y = outH / s * 0.2f;
  SDL_FRect box = {x, y, w, 20};
  SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(app.renderer, 0, 0, 0, 170);
  SDL_RenderFillRect(app.renderer, &box);
  SDL_SetRenderDrawColor(app.renderer, 255, 236, 120, 255);
  SDL_RenderDebugText(app.renderer, x + 8, y + 6, app.toast);
  SDL_SetRenderScale(app.renderer, 1, 1);
}

// 3D mod: the race in 3D needs the port (not --original).
static bool want3d(void) { return app.view3d && !app.original; }

static void hudText(float x, float y, const char* text) {
  SDL_SetRenderDrawColor(app.renderer, 0, 0, 0, 255);
  SDL_RenderDebugText(app.renderer, x + 1, y + 1, text);
  SDL_SetRenderDrawColor(app.renderer, 255, 236, 120, 255);
  SDL_RenderDebugText(app.renderer, x, y, text);
}

static void present3dHudText(int outW, int outH) {
  TgV3Hud hud;
  tg_view3d_hud(tg_game_wram(app.game), &hud);
  float s = outH / 240.0f;
  if(s < 1) s = 1;
  SDL_SetRenderScale(app.renderer, s, s);
  float w = outW / s, h = outH / s;
  char line[64];
  static const char* kSuffix[] = {"TH", "ST", "ND", "RD"};
  int pos = hud.position;
  snprintf(line, sizeof(line), "%d%s", pos, kSuffix[(pos % 100 >= 11 && pos % 100 <= 13) || pos % 10 > 3 ? 0 : pos % 10]);
  hudText(8, 8, line);
  snprintf(line, sizeof(line), "LAP %d/%d", hud.lap, hud.laps);
  hudText(8, 20, line);
  snprintf(line, sizeof(line), "%3d %s", hud.speed, hud.kph ? "KM/H" : "MPH");
  hudText(w - 8 - 8 * (float) strlen(line), 8, line);
  snprintf(line, sizeof(line), "%02d'%02d\"%02d", hud.minutes, hud.seconds, hud.hundredths);
  hudText(w - 8 - 8 * (float) strlen(line), 20, line);
  snprintf(line, sizeof(line), "GEAR %d", hud.gear);
  hudText(8, h - 28, line);
  snprintf(line, sizeof(line), "NITRO x%d", hud.nitros);
  hudText(8, h - 16, line);
  SDL_FRect bar = {w - 16, h - 8 - 60, 8, 60};
  SDL_SetRenderDrawColor(app.renderer, 0, 0, 0, 255);
  SDL_RenderFillRect(app.renderer, &bar);
  SDL_FRect fill = {bar.x + 1, bar.y + 1 + 58 * (1 - hud.fuel / 100.0f), 6, 58 * hud.fuel / 100.0f};
  SDL_SetRenderDrawColor(app.renderer, hud.fuel > 25 ? 40 : 230, hud.fuel > 25 ? 200 : 40, 40, 255);
  SDL_RenderFillRect(app.renderer, &fill);
  hudText(w - 18, h - 8 - 72, "F");
  SDL_SetRenderScale(app.renderer, 1, 1);
}

// A list of view3d's triangles (a 3D view, the transition's clock), offset by (ox, oy) on the
// screen (the SDL renderer fallback).
static void drawList(const TgV3List* list, float ox, float oy) {
  static SDL_Vertex* sv;
  static int svCap;
  SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);  // shadows, glow and fading bands see through
  for(int b = 0; b < list->nb; b++) {
    const TgV3Batch* batch = &list->b[b];
    SDL_Texture* tex = NULL;
    const TgV3Pic* mp = batch->pic >= 0 ? tg_view3d_pic(app.v3, batch->pic) : NULL;
    if(mp && mp->mask && mp->w) {
      // A car's texture: its alpha is a tint mask, not see-through (TgV3Pic.mask): the model
      // is drawn in its corners' colours.
    } else if(batch->pic >= 0) {
      const TgV3Pic* p = tg_view3d_pic(app.v3, batch->pic);
      if(p == NULL || p->w == 0 || batch->pic >= V3_PICS) continue;
      int i = batch->pic;
      if(app.v3tex[i] == NULL || app.v3w[i] != p->w || app.v3h[i] != p->h) {
        if(app.v3tex[i]) SDL_DestroyTexture(app.v3tex[i]);
        app.v3tex[i] = SDL_CreateTexture(app.renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, p->w, p->h);
        if(app.v3tex[i] == NULL) continue;
        SDL_SetTextureScaleMode(app.v3tex[i], p->smooth ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
        SDL_SetTextureBlendMode(app.v3tex[i], SDL_BLENDMODE_BLEND);
        app.v3w[i] = p->w;
        app.v3h[i] = p->h;
        app.v3ver[i] = p->version - 1;
      }
      if(app.v3ver[i] != p->version) {
        SDL_UpdateTexture(app.v3tex[i], NULL, p->px, p->w * 4);
        app.v3ver[i] = p->version;
      }
      tex = app.v3tex[i];
    }
    if(batch->count > svCap) {
      svCap = batch->count * 2;
      sv = SDL_realloc(sv, sizeof(SDL_Vertex) * (size_t) svCap);
    }
    // (a screen-door batch, a car seen through: no per-pixel dither here, its share as alpha)
    float door = (16 - batch->door) / 16.0f;
    for(int k = 0; k < batch->count; k++) {
      const TgV3Vertex* v = &list->v[batch->first + k];
      sv[k].position = (SDL_FPoint) {v->x + ox, v->y + oy};
      uint32_t c = tg_v3_vertex_colour(v, list->haze);  // (shaded materials lit at the vertex)
      sv[k].color = (SDL_FColor) {(c >> 16 & 255) / 255.0f, (c >> 8 & 255) / 255.0f, (c & 255) / 255.0f, (c >> 24) / 255.0f * door};
      sv[k].tex_coord = (SDL_FPoint) {v->u, v->v};
    }
    SDL_RenderGeometry(app.renderer, tex, sv, batch->count, NULL, 0);
  }
}

// The session's recording notes (comment lines) how each 3D race was drawn - views and window
// size - and the first OpenGL errors of the 3D views: what the player's PC did, for a replay.
static void noteRace3d(int views, int w, int h) {
  static int lastViews, lastW, lastH;
  static uint32_t lastFrame;
  if(app.record && (views != lastViews || w != lastW || h != lastH || app.frame > lastFrame + 2))
    recordf("# 3D race from frame %u: %d view%s, %dx%d\n", app.frame, views, views == 1 ? "" : "s", w, h);
  lastViews = views, lastW = w, lastH = h, lastFrame = app.frame;
}

static void noteGlError(void) {
  static int noted;
  unsigned e = app.gl3d ? tg_gl3d_take_error() : 0;
  if(e && app.record && noted < 8) {
    recordf("# OpenGL error 0x%04X drawing the 3D views at frame %u\n", e, app.frame);
    noted++;
  }
}

// A race's 3D views (view3d.h): with OpenGL inside a picture (tg_gl3d_begin / tg_gl3d_end),
// else into the renderer's target. Two players: their views one above the other, or side by
// side (F12).
static void drawRaceViews(int outW, int outH) {
  TgV3Look look = look3d();
  TgV3Camera cam = (TgV3Camera) (app.camera3d % TG_V3_CAM_COUNT);
  int views = tg_view3d_views(app.v3);
  bool side = views == 2 && app.sideBySide;
  int vw = side ? outW / 2 : outW, vh = views == 2 && !side ? outH / 2 : outH;
  // Two views: each leaves 2 pixels at their common edge to the black of the clear, the line
  // between them. With OpenGL all views are drawn in one go by gl3d.c's context, between the
  // renderer's clear and its present, with the renderer drawing nothing over or between them,
  // so a 2-player picture reaches the driver the way a 1-player one does. Before, the
  // renderer's context was made current again between the views and then drew that line over
  // them before the present, the only thing a 1-player picture never does; the player's
  // 2-player race was black on his PC (Intel's OpenGL driver) while 1-player races were fine,
  // and Mesa drew both (QA-034).
  int gap = views == 2 ? 2 : 0;
  for(int k = 0; k < views; k++) {
    Uint64 t0 = SDL_GetTicksNS();
    SDL_Rect r = {side ? k * vw : 0, side ? 0 : k * vh, vw, vh};
    if(side) r.w -= gap, r.x += k ? gap : 0;
    else r.h -= gap, r.y += k ? gap : 0;
    tg_view3d_draw_view(app.v3, k, r.w, r.h, look, cam, &app.v3list);
    app.perfNow.build += msSince(t0);
    t0 = SDL_GetTicksNS();
    if(app.gl3d) {
      tg_gl3d_draw(app.v3, &app.v3list, r.x, r.y, r.w, r.h, outH);
    } else {
      SDL_SetRenderClipRect(app.renderer, &r);
      drawList(&app.v3list, (float) r.x, (float) r.y);
    }
    app.perfNow.gl += msSince(t0);
  }
  SDL_SetRenderClipRect(app.renderer, NULL);
}

// A menu redrawn in the modern look (the car select, the title, the others), whole-window.
static void buildMenuPicture(int outW, int outH) {
  float seconds = SDL_GetTicks() / 1000.0f;
  if(tg_view3d_select_up(app.v3)) tg_view3d_draw_select(app.v3, outW, outH, seconds, &app.v3list);
  else if(tg_view3d_title_up(app.v3)) tg_view3d_draw_title(app.v3, outW, outH, seconds, &app.v3list);
  else tg_view3d_draw_menu(app.v3, outW, outH, seconds, &app.v3list);
}

// The race in 3D, with the game's HUD over it.
static void present3d(void) {
  int outW, outH;
  SDL_GetCurrentRenderOutputSize(app.renderer, &outW, &outH);
  SDL_SetRenderDrawColor(app.renderer, 0, 0, 0, 255);
  SDL_RenderClear(app.renderer);
  if(app.gl3d) {
    SDL_FlushRenderer(app.renderer);  // the clear, under the 3D views
    tg_gl3d_begin(outW, outH);
  }
  drawRaceViews(outW, outH);
  if(app.gl3d) tg_gl3d_end();
  noteRace3d(tg_view3d_views(app.v3), outW, outH);
  noteGlError();
  // The game's own HUD comes with the picture; if it isn't there, its values as text.
  if(!tg_view3d_has_hud(app.v3)) present3dHudText(outW, outH);
  drawToast();
  if(menuOpen()) drawMenu();
  presentTimed();
}

// The car select screen redrawn in the modern look (view3d.c sees when it is up).
static void presentSelect(void) {
  int outW, outH;
  SDL_GetCurrentRenderOutputSize(app.renderer, &outW, &outH);
  SDL_SetRenderDrawColor(app.renderer, 0, 0, 0, 255);
  SDL_RenderClear(app.renderer);
  buildMenuPicture(outW, outH);
  if(app.gl3d) {
    SDL_FlushRenderer(app.renderer);
    tg_gl3d_draw(app.v3, &app.v3list, 0, 0, outW, outH, outH);
    // The loading card before a race: the race's pictures (materials, font, skyline) go to
    // the GPU now, a few MB a picture, so its first frames send nothing big.
    if(tg_view3d_loading_up(app.v3)) tg_gl3d_warm(app.v3, 4u << 20);
    noteGlError();
  } else {
    drawList(&app.v3list, 0, 0);
  }
  drawToast();
  if(menuOpen()) drawMenu();
  presentTimed();
}

// What a picture would show now.
static PicKind picKind(void) {
  if(want3d() && app.race3d) return PIC_RACE3D;
  if(want3d() && app.v3 && look3d() == TG_V3_LOOK_MODERN && (tg_view3d_select_up(app.v3) || tg_view3d_title_up(app.v3) || tg_view3d_menu_up(app.v3)))
    return PIC_MENU3D;
  return PIC_2D;
}

// The game's own picture as it is now: its texture (the widescreen one, srcW x 224, or the
// framebuffer).
static void live2d(SDL_Texture** tex, bool* wide, int* srcW) {
  int margin = tg_port_wide_margin;
  *wide = margin > 0 && app.wideTexture && app.wideTextureW == SNES_W + 2 * margin;
  *srcW = *wide ? app.wideTextureW : SNES_W;
  *tex = *wide ? app.wideTexture : app.texture;
}

// The renderer's clear and the game's own picture (tex, as live2d gives it), scaled to the
// window with the chosen aspect, filter and scanlines.
static void draw2d(SDL_Texture* tex, bool wide, int srcW) {
  int outW, outH;
  SDL_GetCurrentRenderOutputSize(app.renderer, &outW, &outH);
  // Picture size in output pixels before scaling: 256x224 (or wider) with the chosen pixel
  // aspect.
  double picW = (app.aspect == ASPECT_4_3 ? SNES_H * 4.0 / 3.0 / SNES_W : 1.0) * srcW;
  double picH = SNES_H;
  double scale = SDL_min(outW / picW, outH / picH);
  if(app.integerScale && scale >= 1.0) scale = SDL_floor(scale);
  SDL_FRect dst;
  dst.w = (float) (picW * scale);
  dst.h = (float) (picH * scale);
  dst.x = (float) SDL_floor((outW - dst.w) / 2);
  dst.y = (float) SDL_floor((outH - dst.h) / 2);
  // The framebuffer is 512x480 with 2x2 pixels; the 224 visible lines start at y=16.
  SDL_FRect src = {0, 16, TG_FB_WIDTH, SNES_H * 2};
  if(wide) src = (SDL_FRect) {0, 0, (float) srcW, SNES_H};
  SDL_SetRenderDrawColor(app.renderer, 0, 0, 0, 255);
  SDL_RenderClear(app.renderer);
  if(app.filter == FILTER_SMOOTH) {
    int k = (int) SDL_ceil(SDL_max(dst.w / srcW, dst.h / SNES_H));
    if(k < 1) k = 1;
    if(k > 8) k = 8;
    if(app.upscaled == NULL || app.upscaledFactor != k || app.upscaledW != srcW) {
      if(app.upscaled) SDL_DestroyTexture(app.upscaled);
      app.upscaled = SDL_CreateTexture(app.renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_TARGET, srcW * k, SNES_H * k);
      if(app.upscaled) SDL_SetTextureScaleMode(app.upscaled, SDL_SCALEMODE_LINEAR);
      app.upscaledFactor = k;
      app.upscaledW = srcW;
    }
    if(app.upscaled) {
      SDL_SetRenderTarget(app.renderer, app.upscaled);
      SDL_RenderTexture(app.renderer, tex, &src, NULL);
      SDL_SetRenderTarget(app.renderer, NULL);
      SDL_RenderTexture(app.renderer, app.upscaled, NULL, &dst);
    } else {
      SDL_RenderTexture(app.renderer, tex, &src, &dst);
    }
  } else {
    SDL_RenderTexture(app.renderer, tex, &src, &dst);
  }
  if(app.filter == FILTER_SCANLINES && dst.h >= SNES_H * 2) {
    // Darken the lower part of every line, as the gaps between a CRT's scanlines.
    float line = dst.h / SNES_H;
    SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(app.renderer, 0, 0, 0, 96);
    for(int y = 0; y < SNES_H; y++) {
      SDL_FRect r = {dst.x, dst.y + line * y + line * 0.6f, dst.w, line * 0.4f};
      SDL_RenderFillRect(app.renderer, &r);
    }
    SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_NONE);
  }
}

// ---------------------------------------------------------------------------------------
// F7's transition between the original picture and the 3D view (transition.h). Only what is
// drawn changes: view3d (and so the game's full view, the in-between frames, the inputs the
// modern menus send) changes at the press as it always did, the game runs every frame.
//
// A picture of it: the original's share is the game's picture as the renderer draws it (live
// when 3D is off, else as it was at the press, held still); the 3D view's share is the live 3D
// picture when 3D is on, else the last one drawn again (the game no longer feeds it); then the
// clock over both. With OpenGL the order of every 3D picture is kept: the renderer draws first
// (the original picture, where a 3D picture has its clear), then gl3d draws everything else in
// one go - the 3D views kept out of the original's share by a depth mask, then the clock - and
// the renderer presents. Nothing of the renderer's goes between or over gl3d's drawing (QA-034;
// the toast and the Tab menu, as in every 3D picture). The SDL renderer fallback (--no-gl)
// draws the 3D picture into a texture first and that through the 3D view's share.

// The original picture now, held for a transition to 3D (a copy of its texture).
static void snapshot2d(void) {
  SDL_Texture* tex;
  bool wide;
  int srcW;
  live2d(&tex, &wide, &srcW);
  app.trans2dOk = false;
  if(tex == NULL) return;
  int w = wide ? srcW : TG_FB_WIDTH, h = wide ? SNES_H : TG_FB_HEIGHT;
  if(app.trans2d == NULL || app.trans2dTexW != w || app.trans2dTexH != h) {
    if(app.trans2d) SDL_DestroyTexture(app.trans2d);
    app.trans2d = SDL_CreateTexture(app.renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_TARGET, w, h);
    if(app.trans2d == NULL) return;
    SDL_SetTextureScaleMode(app.trans2d, SDL_SCALEMODE_NEAREST);
    app.trans2dTexW = w, app.trans2dTexH = h;
  }
  SDL_BlendMode mode;
  SDL_GetTextureBlendMode(tex, &mode);
  SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_NONE);
  SDL_SetRenderTarget(app.renderer, app.trans2d);
  SDL_RenderTexture(app.renderer, tex, NULL, NULL);
  SDL_SetRenderTarget(app.renderer, NULL);
  SDL_SetTextureBlendMode(tex, mode);
  app.trans2dWide = wide, app.trans2dW = srcW, app.trans2dOk = true;
}

// The 3D view on or off (F7, the options menu, --toggle-3d-at), with the transition.
static void setView3d(bool on) {
  if(on == app.view3d) return;
  app.view3d = on;
  // (from the options menu the game waits under it: the change is made at once, as before)
  if(!app.modeTransition || app.original || !app.v3 || menuOpen()) {
    app.trans.active = false;
    return;
  }
  if(!app.trans.active) {
    // 3D on but this screen not redrawn in 3D (the original picture is up): nothing to sweep.
    if(!on && app.shownKind == PIC_2D) return;
    app.trans3d = on ? PIC_2D : app.shownKind;  // (going to 3D: none until the game has run with it)
    app.transLast = loopNow();
  }
  tg_transition_start(&app.trans, !on, on);  // (while one is on, the hand turns back from where it is)
  app.transFrame = app.frame;
  if(on) snapshot2d();
}

static void present(void);

static void presentTransition(void) {
  Uint64 now = loopNow();
  tg_transition_step(&app.trans, now > app.transLast ? (float) ((now - app.transLast) / 1e9) : 0.0f);
  app.transLast = now;
  // Going to 3D, the 3D picture is the live one, drawn whenever present() would draw it (so the
  // 3D view is drawn as often as without the transition: its camera's springs step by the
  // pictures drawn); a screen with none two game frames after the press ends the transition.
  if(want3d()) {
    PicKind k = picKind();
    if(k != PIC_2D) app.trans3d = k;
    else if(app.frame >= app.transFrame + 2) app.trans.active = false;
  } else if(app.trans3d == PIC_2D) {
    app.trans.active = false;
  }
  if(!app.trans.active) {
    present();
    return;
  }
  int outW, outH;
  SDL_GetCurrentRenderOutputSize(app.renderer, &outW, &outH);
  PicKind side3d = app.trans3d;  // (PIC_2D: no 3D picture yet)
  // (drawn whenever it is live, even with no share yet: the 3D view steps its camera's springs
  // by the pictures drawn, so it is drawn as often as without the transition)
  bool any3d = side3d != PIC_2D && (app.trans.x > 0 || want3d());
  bool held2d = want3d() && app.trans2dOk;
  SDL_Texture* tex;
  bool wide;
  int srcW;
  live2d(&tex, &wide, &srcW);
  if(held2d) tex = app.trans2d, wide = app.trans2dWide, srcW = app.trans2dW;
  if(any3d && side3d == PIC_MENU3D) buildMenuPicture(outW, outH);
  // (the transition's own work - the original picture, the mask, the clock - in the frame-time
  // log's gl_ms, the views' as in any 3D picture)
  Uint64 t0 = SDL_GetTicksNS();
  if(app.gl3d) {
    draw2d(tex, wide, srcW);
    SDL_FlushRenderer(app.renderer);  // the original picture, under gl3d's drawing
    tg_gl3d_begin(outW, outH);
    if(any3d) {
      tg_v3_list_clear(&app.transList);
      tg_transition_region(&app.trans, outW, outH, false, &app.transList);
      tg_gl3d_mask(&app.transList, outW, outH);
      app.perfNow.gl += msSince(t0);
      if(side3d == PIC_RACE3D) drawRaceViews(outW, outH);
      else tg_gl3d_draw(app.v3, &app.v3list, 0, 0, outW, outH, outH);
      t0 = SDL_GetTicksNS();
      tg_gl3d_unmask();
    }
    tg_v3_list_clear(&app.transList);
    tg_transition_overlay(&app.trans, outW, outH, &app.transList);
    tg_gl3d_draw(app.v3, &app.transList, 0, 0, outW, outH, outH);
    tg_gl3d_end();
    noteGlError();
    app.perfNow.gl += msSince(t0);
  } else {
    if(any3d && (app.trans3dTex == NULL || app.trans3dTexW != outW || app.trans3dTexH != outH)) {
      if(app.trans3dTex) SDL_DestroyTexture(app.trans3dTex);
      app.trans3dTex = SDL_CreateTexture(app.renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_TARGET, outW, outH);
      if(app.trans3dTex) SDL_SetTextureScaleMode(app.trans3dTex, SDL_SCALEMODE_NEAREST);
      app.trans3dTexW = outW, app.trans3dTexH = outH;
    }
    any3d = any3d && app.trans3dTex;
    if(any3d) {
      // The 3D picture first, into its texture, as present3d / presentSelect draw it.
      SDL_SetRenderTarget(app.renderer, app.trans3dTex);
      SDL_SetRenderDrawColor(app.renderer, 0, 0, 0, 255);
      SDL_RenderClear(app.renderer);
      app.perfNow.gl += msSince(t0);
      if(side3d == PIC_RACE3D) drawRaceViews(outW, outH);
      else drawList(&app.v3list, 0, 0);
      t0 = SDL_GetTicksNS();
      SDL_SetRenderTarget(app.renderer, NULL);
    }
    draw2d(tex, wide, srcW);
    if(any3d) {
      tg_v3_list_clear(&app.transList);
      tg_transition_region(&app.trans, outW, outH, true, &app.transList);
      static SDL_Vertex* sv;
      static int svCap;
      if(app.transList.nv > svCap) {
        svCap = app.transList.nv * 2;
        sv = SDL_realloc(sv, sizeof(SDL_Vertex) * (size_t) svCap);
      }
      for(int k = 0; sv && k < app.transList.nv; k++) {
        const TgV3Vertex* v = &app.transList.v[k];
        sv[k] = (SDL_Vertex) {{v->x, v->y}, {1, 1, 1, 1}, {v->u, v->v}};
      }
      SDL_SetTextureBlendMode(app.trans3dTex, SDL_BLENDMODE_NONE);
      if(sv) SDL_RenderGeometry(app.renderer, app.trans3dTex, sv, app.transList.nv, NULL, 0);
    }
    tg_v3_list_clear(&app.transList);
    tg_transition_overlay(&app.trans, outW, outH, &app.transList);
    drawList(&app.transList, 0, 0);
    SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_NONE);
    app.perfNow.gl += msSince(t0);
  }
  if(any3d && side3d == PIC_RACE3D && want3d()) noteRace3d(tg_view3d_views(app.v3), outW, outH);
  app.shownKind = app.trans.x >= 0.5f ? side3d : PIC_2D;
  // (F7's toast waits: the clock says it, the toast's last 1.5 s say the look and camera after)
  if(menuOpen()) drawMenu();
  presentTimed();
}

static void present(void) {
  if(app.trans.active) {
    presentTransition();
    return;
  }
  app.shownKind = picKind();
  if(app.shownKind == PIC_RACE3D) {
    present3d();
    return;
  }
  if(app.shownKind == PIC_MENU3D) {
    presentSelect();
    return;
  }
  SDL_Texture* tex;
  bool wide;
  int srcW;
  live2d(&tex, &wide, &srcW);
  draw2d(tex, wide, srcW);
  drawToast();
  if(menuOpen()) drawMenu();
  presentTimed();
}

static void startRecording(void) {
  const char* base = SDL_GetBasePath();
  char dir[2048];
  snprintf(dir, sizeof(dir), "%srecordings", base ? base : "");
  if(!SDL_CreateDirectory(dir)) {
    char* pref = SDL_GetPrefPath(TG_PREF_ORG, TG_PREF_APP);
    if(pref == NULL) return;
    snprintf(dir, sizeof(dir), "%srecordings", pref);
    SDL_free(pref);
    if(!SDL_CreateDirectory(dir)) return;
  }
  SDL_Time now;
  SDL_DateTime t;
  SDL_GetCurrentTime(&now);
  SDL_TimeToDateTime(now, &t, true);
  char path[2300];
  snprintf(path, sizeof(path), "%s/session-%04d%02d%02d-%02d%02d%02d.txt", dir, t.year, t.month, t.day, t.hour, t.minute, t.second);
  app.record = fopen(path, "w");
  if(app.record) {
    recordf("# " TG_APP_NAME " input recording from power-on. Replay: tg_headless --input <this file>\n");
    recordf("# ROM SHA-256 %s\n", TG_ROM_SHA256);
    recordf(app.original ? "# Played with the original code (--original)\n"
                         : "# Played with the C race code: replay with tg_headless --port native\n");
  }
  snprintf(app.coveragePath, sizeof(app.coveragePath), "%s/coverage.tgcov", dir);
  app.coverage = SDL_malloc(sizeof(TgCoverage));
  if(app.coverage) {
    tg_coverage_load(app.coverage, app.coveragePath);
    tg_game_set_coverage(app.game, app.coverage);
  }
}

static void stopRecording(void) {
  if(app.record) {
    uint8_t d[32];
    tg_sha256(tg_game_wram(app.game), 0x20000, d);
    recordf("# end frame %u wram ", app.frame);
    for(int i = 0; i < 32; i++) recordf("%02x", d[i]);
    recordf("\n");
    fclose(app.record);
    app.record = NULL;
  }
  if(app.coverage) {
    tg_game_set_coverage(app.game, NULL);
    tg_coverage_save(app.coverage, app.coveragePath);
    SDL_free(app.coverage);
    app.coverage = NULL;
  }
}

// Controller rumble in races: a knock when a car bumps something ($1EF5/$1EF6 bit 7) and
// a long light rumble when its nitro fires ($1E6C/$1E6E bit 15). Pad 1 drives the first
// car, pad 2 the second.
static void rumble(void) {
  static uint32_t lastIrqs;
  static uint8_t lastBump[2], lastNitro[2];
  bool inRace = tg_port_race_irqs != lastIrqs;
  lastIrqs = tg_port_race_irqs;
  const uint8_t* ram = tg_game_wram(app.game);
  for(int p = 0; p < 2; p++) {
    uint8_t bump = inRace ? ram[0x1EF5 + p] & 0x80 : 0;
    uint8_t nitro = inRace ? ram[0x1E6D + 2 * p] & 0x80 : 0;
    if(app.rumble && app.pads[p] && ram[0x1FF6] == 0) {  // not in the attract demo
      if(bump && !lastBump[p]) SDL_RumbleGamepad(app.pads[p], 0xC000, 0xFFFF, 200);
      else if(nitro && !lastNitro[p]) SDL_RumbleGamepad(app.pads[p], 0x3000, 0x8000, 800);
    }
    lastBump[p] = bump;
    lastNitro[p] = nitro;
  }
}

// Widescreen: the margin that makes the picture as wide as the window.
static int wantedMargin(void) {
  if(!app.wide || app.original || want3d()) return 0;  // in 3D the 3D view is the wide picture
  int outW, outH;
  if(!SDL_GetCurrentRenderOutputSize(app.renderer, &outW, &outH) || outH <= 0) return 0;
  double pixel = app.aspect == ASPECT_4_3 ? SNES_H * 4.0 / 3.0 / SNES_W : 1.0;
  int m = (int) SDL_floor(((double) outW / outH * SNES_H / pixel - SNES_W) / 2);
  return m < 0 ? 0 : m > 96 ? 96 : m;
}

// The game's picture into the textures.
// TV colours: a CRT's gamma (about 2.5 against a PC screen's 2.2) and 10% more saturation.
static void tvColours(uint32_t* p, size_t n) {
  static uint8_t lut[256];
  static bool ready;
  if(!ready) {
    for(int i = 0; i < 256; i++) lut[i] = (uint8_t) SDL_lroundf(255.0f * SDL_powf(i / 255.0f, 2.5f / 2.2f));
    ready = true;
  }
  // Neighbouring pixels are usually the same colour (each game pixel is two wide), so the last
  // answer is kept.
  uint32_t lastIn = 0, lastOut = 0;
  bool have = false;
  for(size_t i = 0; i < n; i++) {
    if(have && p[i] == lastIn) {
      p[i] = lastOut;
      continue;
    }
    lastIn = p[i];
    int r = lut[(p[i] >> 16) & 0xff], g = lut[(p[i] >> 8) & 0xff], b = lut[p[i] & 0xff];
    int y = (r * 77 + g * 150 + b * 29) >> 8;
    r = y + (r - y) * 11 / 10;
    g = y + (g - y) * 11 / 10;
    b = y + (b - y) * 11 / 10;
    r = r < 0 ? 0 : r > 255 ? 255 : r;
    g = g < 0 ? 0 : g > 255 ? 255 : g;
    b = b < 0 ? 0 : b > 255 ? 255 : b;
    p[i] = lastOut = (uint32_t) (r << 16 | g << 8 | b);
    have = true;
  }
}

static void uploadPicture(void) {
  if(app.tvColour) {
    // Converted in ordinary memory: reading back from a locked texture can be very slow, as
    // the driver may hand out memory that's only quick to write.
    static uint32_t tv[TG_FB_WIDTH * TG_FB_HEIGHT];
    tg_game_get_pixels(app.game, (uint8_t*) tv);
    tvColours(tv, SDL_arraysize(tv));
    SDL_UpdateTexture(app.texture, NULL, tv, TG_FB_WIDTH * 4);
  } else {
    void* pixels;
    int pitch;
    if(SDL_LockTexture(app.texture, NULL, &pixels, &pitch)) {
      tg_game_get_pixels(app.game, pixels);
      SDL_UnlockTexture(app.texture);
    }
  }
  const uint32_t* wide = tg_game_wide_pixels(app.game);
  static uint32_t wideTv[(SNES_W + 2 * 96) * SNES_H];
  if(wide && app.tvColour) {
    size_t n = (size_t) (SNES_W + 2 * tg_port_wide_margin) * SNES_H;
    if(n <= SDL_arraysize(wideTv)) {
      memcpy(wideTv, wide, n * 4);
      tvColours(wideTv, n);
      wide = wideTv;
    }
  }
  if(wide) {
    int w = SNES_W + 2 * tg_port_wide_margin;
    if(app.wideTexture == NULL || app.wideTextureW != w) {
      if(app.wideTexture) SDL_DestroyTexture(app.wideTexture);
      app.wideTexture = SDL_CreateTexture(app.renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, w, SNES_H);
      if(app.wideTexture) SDL_SetTextureScaleMode(app.wideTexture, SDL_SCALEMODE_NEAREST);
      app.wideTextureW = w;
    }
    if(app.wideTexture) SDL_UpdateTexture(app.wideTexture, NULL, wide, w * 4);
  }
}

static void runFrame(int samples) {
  uint16_t input[3] = {0, app.keys[1] | padButtons(app.pads[0]), app.keys[2] | padButtons(app.pads[1])};
  bool reset = app.resetRequested;
  app.resetRequested = false;
  if(app.replaying) {
    tg_input_script_advance(&app.replay, app.frame);
    input[1] = app.replay.state[1];
    input[2] = app.replay.state[2];
    reset = app.replay.resetPending;
    app.replay.resetPending = false;
    if(app.replay.smooth != app.smooth) { app.smooth = app.replay.smooth; updateTitle(); }
    if(app.replay.fullview != app.fullView) { app.fullView = app.replay.fullview; updateTitle(); }
    if(app.replay.tallRoad != app.tallRoad) { app.tallRoad = app.replay.tallRoad; updateTitle(); }
    if((app.replay.wide > 0) != app.wide) { app.wide = app.replay.wide > 0; updateTitle(); }
    if(app.replay.sideBySide != app.sideBySide) { app.sideBySide = app.replay.sideBySide; updateTitle(); }
    if(app.replay.ghost != app.ghost) { app.ghost = app.replay.ghost; updateTitle(); }
  }
  // Smooth motion runs the race logic every frame; the original updates it every third
  // frame (its pace is scaled by the frames elapsed, so the race takes the same time).
  tg_port_race_frames = app.smooth ? 1 : 3;
  // The 3D view takes the 1-player view's sprites, with their object tags (view3d.h).
  bool fullView = app.fullView || want3d();
  if(fullView != tg_port_full_view) tg_game_set_full_view(app.game, fullView);
  tg_game_set_tall_road(app.game, app.tallRoad);
  tg_game_set_hud_layout(app.game, app.mapTop, app.speedBottom);
  int margin = app.replaying ? app.replay.wide : wantedMargin();
  if(margin != tg_port_wide_margin) tg_game_set_wide(app.game, margin);
  if(app.sideBySide != tg_port_side_by_side) tg_game_set_side_by_side(app.game, app.sideBySide);
  if(app.ghost != tg_port_ghost) tg_game_set_ghost(app.game, app.ghost);
  bool interp = app.highFps || want3d();
  if(interp != tg_port_interp) tg_game_set_interp(app.game, interp);
  if(!app.replaying) {
    // The modern car select shows the cars in a line: Left / Right become the presses the
    // game's 2 x 2 grid needs (select_input.h). The game gets, and the recording keeps,
    // those presses; a replay already holds them.
    bool line = want3d() && app.v3 && look3d() == TG_V3_LOOK_MODERN && tg_view3d_select_up(app.v3);
    int cursor = tg_game_wram(app.game)[0x5A] & 3;
    for(int p = 1; p <= 2; p++) input[p] = tg_select_input(&app.selectInput[p], input[p], cursor, line);
    // The country screen and its password (country_input.h): the modern screen's Left /
    // Right across its two columns, and with Country select FREE a locked country's password
    // typed in; the game gets, and the recording keeps, the presses.
    bool modernMenu = want3d() && app.v3 && look3d() == TG_V3_LOOK_MODERN && tg_view3d_menu_up(app.v3);
    input[1] = tg_country_input(&app.countryInput, input[1], modernMenu, tg_game_wram(app.game), app.countryFree);
  }
  if(app.v3) tg_view3d_set_country_select(app.v3, app.countryFree && !app.replaying, tg_country_input_busy(&app.countryInput));
  if(app.record) {
    if(reset) recordf("%u reset\n", app.frame);
    if(app.smooth != app.recordedSmooth) {
      recordf("%u smooth %s\n", app.frame, app.smooth ? "on" : "off");
      app.recordedSmooth = app.smooth;
    }
    if(app.fullView != app.recordedFullView) {
      recordf("%u fullview %s\n", app.frame, app.fullView ? "on" : "off");
      app.recordedFullView = app.fullView;
    }
    if(app.tallRoad != app.recordedTallRoad) {
      recordf("%u tallroad %s\n", app.frame, app.tallRoad ? "on" : "off");
      app.recordedTallRoad = app.tallRoad;
    }
    if(margin != app.recordedWide) {
      recordf("%u wide %d\n", app.frame, margin);
      app.recordedWide = margin;
    }
    if(app.sideBySide != app.recordedSideBySide) {
      recordf("%u sidebyside %s\n", app.frame, app.sideBySide ? "on" : "off");
      app.recordedSideBySide = app.sideBySide;
    }
    if(app.ghost != app.recordedGhost) {
      recordf("%u ghost %s\n", app.frame, app.ghost ? "on" : "off");
      app.recordedGhost = app.ghost;
    }
    for(int p = 1; p <= 2; p++) {
      if(input[p] != app.recorded[p] || (app.frame == 0 && p == 1)) {
        char names[96];
        tg_format_buttons(input[p], names, sizeof(names));
        recordf("%u %d %s\n", app.frame, p, names);
        app.recorded[p] = input[p];
      }
    }
  }
  if(reset) tg_game_reset(app.game, false);
  if(app.replaying && app.replay.unlockPending) {
    uint16_t u = app.replay.unlock;
    uint8_t open[3] = {u & 15, (u >> 4) & 15, (u >> 8) & 15};
    tg_game_unlock(app.game, open);
    app.replay.unlockPending = false;
  }
  if(app.autoSave && !app.replaying) {
    // Countries reached in earlier sessions are opened again (after power-on clears them).
    uint8_t open[3];
    tg_game_progress(app.game, open);
    if(open[0] < app.saved[0] || open[1] < app.saved[1] || open[2] < app.saved[2]) {
      tg_game_unlock(app.game, app.saved);
      uint8_t now[3];
      tg_game_progress(app.game, now);
      if(app.record && memcmp(now, open, 3) != 0) recordf("%u unlock %d %d %d\n", app.frame, app.saved[0], app.saved[1], app.saved[2]);
    }
  }
  recordFlush();  // this frame's lines (and the notes of the pictures before it) on disk
  tg_game_set_input(app.game, 1, input[1]);
  tg_game_set_input(app.game, 2, input[2]);
  Uint64 t0 = SDL_GetTicksNS();
  tg_game_run_frame(app.game);
  app.perfNow.game += msSince(t0);
  app.perfNow.frames++;
  app.frame++;
  if(app.music) tg_music_end_frame(app.music, app.frame);
  if(want3d() && app.v3) {
    t0 = SDL_GetTicksNS();
    TgSpriteState ss;
    tg_game_sprite_state(app.game, &ss);
    tg_game_get_pixels(app.game, (uint8_t*) app.v3pixels);
    app.race3d = tg_view3d_capture(app.v3, tg_game_wram(app.game), &ss, app.v3pixels);
    app.perfNow.capture += msSince(t0);
  } else {
    app.race3d = false;
  }
  // A new best race is saved straight away (not while replaying someone's recording).
  if(tg_game_ghosts_changed(app.game) && !app.replaying && app.ghostPath[0]) tg_game_save_ghosts(app.game, app.ghostPath);
  if(app.autoSave && !app.replaying) {
    uint8_t open[3];
    tg_game_progress(app.game, open);
    bool more = false;
    for(int i = 0; i < 3; i++) {
      if(open[i] > app.saved[i]) {
        app.saved[i] = open[i];
        more = true;
      }
    }
    if(more) saveProgress();
  }
  rumble();

  uploadPicture();

  static int16_t buffer[2048 * 2];
  tg_game_get_samples(app.game, buffer, samples);
  // Keep at most ~6 frames queued so latency can't build up. The modern music moves on only
  // with what is heard, so a frame dropped here leaves no gap in it.
  if(app.audio && SDL_GetAudioStreamQueued(app.audio) < 6 * 800 * 4) {
    if(app.music) tg_music_mix(app.music, buffer, samples);
    SDL_PutAudioStreamData(app.audio, buffer, samples * 4);
  }
}


// ---------------------------------------------------------------------------------------
// The options menu (Tab, or a gamepad's guide button): every option, the controls and the
// gamepad order in one place. The game waits while it is open.

typedef enum {
  MI_RESUME, MI_HOTKEYS, MI_VIEW3D, MI_CAM3D, MI_TITLE, MI_TRANSITION, MI_MUSIC, MI_ASPECT, MI_FILTER, MI_INTEGER, MI_FULLSCREEN, MI_WIDE, MI_FULLVIEW, MI_MAPPOS, MI_SPEEDPOS, MI_SIDE,
  MI_COLOUR, MI_SMOOTH, MI_HIGHFPS, MI_GHOST, MI_AUTOSAVE, MI_COUNTRY, MI_RUMBLE, MI_SWAP, MI_KEYS,
  MI_KEYS2, MI_PADS, MI_RESET, MI_QUIT, MI_COUNT
} MenuItem;

static struct {
  bool open;
  int page;    // 0 options, 1 player 1's keys, 2 gamepad controls, 3 player 2's keys, 4 hotkeys
  int sel;
  int binding; // controls page: the SNES button waiting for a key or gamepad button, or -1
  bool quit;
} menu = {false, 0, 0, -1, false};

// The keys this frontend adds over the original's controls (the Hotkeys page; README.md
// lists the same).
static const char* const kHotkeys[][2] = {
  {"F7", "2D / 3D (time runs 1992-2026)"},
  {"Ctrl+F7", "3D look: modern, pictures, models"},
  {"Shift+F7", "3D camera: game, high, bumper"},
  {"F11, Alt+Enter", "Fullscreen (Esc leaves it)"},
  {"F10", "Widescreen"},
  {"F9", "Full-screen view, tall road, off"},
  {"F12", "2 players side by side"},
  {"F1", "Ghost car: race your best"},
  {"Shift+F1", "Auto-save the countries reached"},
  {"F2", "Picture shape"},
  {"Shift+F2", "Frame-time log"},
  {"F3", "Whole-pixel scaling"},
  {"F4", "Picture filter"},
  {"F5", "Reset the game"},
  {"F6", "Smooth motion"},
  {"Shift+F6", "High frame rate"},
  {"F8", "Rumble"},
  {"Tab", "Options (gamepad: guide button)"},
};
enum { HOTKEY_COUNT = sizeof(kHotkeys) / sizeof(kHotkeys[0]) };

static void setFullscreen(bool on) {
  app.fullscreen = on;
  SDL_SetWindowFullscreen(app.window, on);
  saveSettings();
}

static void setWide(bool on) {
  app.wide = on;
  if(!app.fullscreen) {
    // A window gets the shape of the picture: 16:9 for widescreen, else 4:3.
    int w, h;
    SDL_GetWindowSize(app.window, &w, &h);
    SDL_SetWindowSize(app.window, on ? h * 16 / 9 : h * 4 / 3, h);
  }
  updateTitle();
}

// Off, then full view with the original's low horizon, then with a tall road.
static void stepFullView(void) {
  if(!app.fullView) app.fullView = true;
  else if(!app.tallRoad) app.tallRoad = true;
  else app.fullView = app.tallRoad = false;
  updateTitle();
}

static void setHighFps(bool on) {
  // In-between frames need the race to move every frame: smooth motion comes on too.
  app.highFps = on;
  if(on && !app.replaying) app.smooth = true;
  updateTitle();
}

static void setSmooth(bool on) {
  app.smooth = on;
  if(!on) app.highFps = false;
  updateTitle();
}

// The names of a SNES button's keyboard (player's) or gamepad bindings.
static void bindingNames(int button, bool pad, int player, char* out, size_t size) {
  out[0] = 0;
  for(int i = 0; i < bindingCount; i++) {
    if(bindings[i].pad != pad || bindings[i].button != (uint16_t) (1 << button)) continue;
    if(!pad && bindings[i].player != player) continue;
    const char* name = pad ? SDL_GetGamepadStringForButton((SDL_GamepadButton) bindings[i].code) : SDL_GetKeyName((SDL_Keycode) bindings[i].code);
    if(out[0]) SDL_strlcat(out, ", ", size);
    SDL_strlcat(out, name ? name : "?", size);
  }
  if(!out[0]) SDL_strlcpy(out, "-", size);
}

static void saveControls(void) {
  const char* base = SDL_GetBasePath();
  char path[2048];
  snprintf(path, sizeof(path), "%scontrols.txt", base ? base : "");
  FILE* f = fopen(path, "w");
  if(f == NULL) return;
  // The comment lines of the defaults, then the bindings.
  for(const char* t = kDefaultControls; *t == '#'; ) {
    const char* end = strchr(t, '\n');
    fwrite(t, 1, (size_t) (end - t + 1), f);
    t = end + 1;
  }
  for(int i = 0; i < bindingCount; i++) {
    int b = 0;
    while(b < 12 && bindings[i].button != (uint16_t) (1 << b)) b++;
    if(bindings[i].pad) fprintf(f, "%s pad %s\n", kButtonNames[b], SDL_GetGamepadStringForButton((SDL_GamepadButton) bindings[i].code));
    else fprintf(f, "%s%s key %s\n", bindings[i].player == 2 ? "P2 " : "", kButtonNames[b], SDL_GetKeyName((SDL_Keycode) bindings[i].code));
  }
  fclose(f);
}

// Replaces a SNES button's keyboard (player's) or gamepad bindings with code.
static void rebind(int button, bool pad, int player, int code) {
  int n = 0;
  for(int i = 0; i < bindingCount; i++) {
    bool same = bindings[i].pad == pad && (pad || bindings[i].player == player);
    if(same && bindings[i].button == (uint16_t) (1 << button)) continue;
    if(bindings[i].pad == pad && bindings[i].code == code) continue;  // the key or button moves here
    bindings[n++] = bindings[i];
  }
  bindingCount = n;
  if(bindingCount < (int) SDL_arraysize(bindings)) bindings[bindingCount++] = (Binding) {pad, code, (uint16_t) (1 << button), player};
  saveControls();
}

static const char* onOff(bool on) { return on ? "on" : "off"; }

static void menuItemText(int i, char* out, size_t size) {
  switch(i) {
    case MI_RESUME: snprintf(out, size, "Back to the game"); break;
    case MI_HOTKEYS: snprintf(out, size, "Hotkeys (F7 2D/3D, F11 ...)..."); break;
    case MI_VIEW3D: snprintf(out, size, "3D races (F7)      %s", !app.view3d ? "off" : !app.classic3d ? "on, modern" : app.models3d ? "on, models" : "on, game pictures"); break;
    case MI_CAM3D: {
      static const char* cams[] = {"the game's", "high", "bumper"};
      snprintf(out, size, "  3D camera        %s", cams[app.camera3d % TG_V3_CAM_COUNT]);
      break;
    }
    case MI_TITLE: snprintf(out, size, "  Title style      %s", app.titleClassic ? "CLASSIC 3D" : "MODERN"); break;
    case MI_TRANSITION: snprintf(out, size, "  2D/3D transition %s", onOff(app.modeTransition)); break;
    case MI_MUSIC:
      snprintf(out, size, "Music              %s%s", tg_music_set_name(app.musicSet),
               app.musicSet != TG_MUSIC_ORIGINAL && !(app.music && tg_music_available(app.music)) ? " (not built: see launcher)" : "");
      break;
    case MI_ASPECT: snprintf(out, size, "Aspect             %s", aspectNames[app.aspect]); break;
    case MI_FILTER: snprintf(out, size, "Display filter     %s", filterNames[app.filter]); break;
    case MI_INTEGER: snprintf(out, size, "Integer scaling    %s", onOff(app.integerScale)); break;
    case MI_FULLSCREEN: snprintf(out, size, "Fullscreen         %s", onOff(app.fullscreen)); break;
    case MI_WIDE: snprintf(out, size, "Widescreen         %s", onOff(app.wide)); break;
    case MI_FULLVIEW: snprintf(out, size, "Full-screen view   %s", !app.fullView ? "off" : app.tallRoad ? "on, tall road" : "on"); break;
    case MI_MAPPOS: snprintf(out, size, "  Map              %s", app.mapTop ? "top" : "bottom"); break;
    case MI_SPEEDPOS: snprintf(out, size, "  Speed and time   %s", app.speedBottom ? "bottom" : "top"); break;
    case MI_SIDE: snprintf(out, size, "2P side by side    %s", onOff(app.sideBySide)); break;
    case MI_SMOOTH: snprintf(out, size, "Smooth motion      %s", onOff(app.smooth)); break;
    case MI_HIGHFPS: snprintf(out, size, "High frame rate    %s", onOff(app.highFps)); break;
    case MI_GHOST: snprintf(out, size, "Ghost car          %s", onOff(app.ghost)); break;
    case MI_AUTOSAVE: snprintf(out, size, "Auto-save          %s", onOff(app.autoSave)); break;
    case MI_COUNTRY: snprintf(out, size, "Country select     %s", app.countryFree ? "FREE (passwords entered for you)" : "PASSWORD"); break;
    case MI_COLOUR: snprintf(out, size, "Colours            %s", app.tvColour ? "as on a TV" : "exact"); break;
    case MI_RUMBLE: snprintf(out, size, "Rumble             %s", onOff(app.rumble)); break;
    case MI_SWAP: snprintf(out, size, "Swap gamepads      P1: %s", app.pads[0] ? SDL_GetGamepadName(app.pads[0]) : "none"); break;
    case MI_KEYS: snprintf(out, size, "Player 1 keys..."); break;
    case MI_KEYS2: snprintf(out, size, "Player 2 keys..."); break;
    case MI_PADS: snprintf(out, size, "Gamepad controls..."); break;
    case MI_RESET: snprintf(out, size, "Reset the game"); break;
    case MI_QUIT: snprintf(out, size, "Quit"); break;
  }
}

static int menuCount(void) { return menu.page == 0 ? MI_COUNT : menu.page == 4 ? 1 : 13; }

static void menuBack(void);

// Changes the selected item (dir: -1 left, +1 right or select).
static void menuActivate(int dir) {
  if(menu.page != 0) {
    if(menu.page == 4 || menu.sel == 12) {
      menuBack();
    } else {
      menu.binding = menu.sel;
    }
    return;
  }
  bool locked = app.replaying;  // a replay sets the options that change the race
  switch(menu.sel) {
    case MI_RESUME: menu.open = false; break;
    case MI_HOTKEYS: menu.page = 4; menu.sel = 0; break;
    case MI_CAM3D:
      app.camera3d = (app.camera3d + (dir < 0 ? TG_V3_CAM_COUNT - 1 : 1)) % TG_V3_CAM_COUNT;
      updateTitle();
      break;
    case MI_VIEW3D:
      // Off, then modern, the game's pictures, models.
      if(!app.view3d) app.classic3d = false, setView3d(true);
      else if(app.classic3d && app.models3d) setView3d(false);
      else nextLook3d();
      updateTitle();
      break;
    case MI_TRANSITION:
      app.modeTransition = !app.modeTransition;
      if(!app.modeTransition) app.trans.active = false;
      saveSettings();
      break;
    case MI_TITLE:
      app.titleClassic = !app.titleClassic;
      if(app.v3) tg_view3d_set_title_style(app.v3, app.titleClassic ? TG_V3_TITLE_CLASSIC : TG_V3_TITLE_MODERN);
      saveSettings();
      break;
    case MI_MUSIC:
      // Original, faithful, modern (the cities' race versions), then the other sets.
      app.musicSet = (app.musicSet + (dir < 0 ? TG_MUSIC_SET_COUNT - 1 : 1)) % TG_MUSIC_SET_COUNT;
      if(app.music) tg_music_set_set(app.music, app.musicSet);
      saveSettings();
      break;
    case MI_ASPECT: app.aspect = (app.aspect + 1) % ASPECT_COUNT; updateTitle(); break;
    case MI_FILTER: app.filter = (FilterMode) ((app.filter + (dir < 0 ? FILTER_COUNT - 1 : 1)) % FILTER_COUNT); updateTitle(); break;
    case MI_INTEGER: app.integerScale = !app.integerScale; updateTitle(); break;
    case MI_FULLSCREEN: setFullscreen(!app.fullscreen); break;
    case MI_WIDE: if(!locked) setWide(!app.wide); break;
    case MI_FULLVIEW: if(!locked) stepFullView(); break;
    case MI_MAPPOS: app.mapTop = !app.mapTop; saveSettings(); break;
    case MI_SPEEDPOS: app.speedBottom = !app.speedBottom; saveSettings(); break;
    case MI_SIDE: if(!locked) { app.sideBySide = !app.sideBySide; updateTitle(); } break;
    case MI_SMOOTH: if(!locked) setSmooth(!app.smooth); break;
    case MI_HIGHFPS: setHighFps(!app.highFps); break;
    case MI_GHOST: if(!locked) { app.ghost = !app.ghost; updateTitle(); } break;
    case MI_AUTOSAVE: app.autoSave = !app.autoSave; updateTitle(); break;
    case MI_COUNTRY: app.countryFree = !app.countryFree; saveSettings(); break;
    case MI_RUMBLE: app.rumble = !app.rumble; updateTitle(); break;
    case MI_COLOUR: app.tvColour = !app.tvColour; uploadPicture(); updateTitle(); break;
    case MI_SWAP: {
      SDL_Gamepad* t = app.pads[0];
      app.pads[0] = app.pads[1];
      app.pads[1] = t;
      break;
    }
    case MI_KEYS: menu.page = 1; menu.sel = 0; break;
    case MI_KEYS2: menu.page = 3; menu.sel = 0; break;
    case MI_PADS: menu.page = 2; menu.sel = 0; break;
    case MI_RESET: app.resetRequested = true; menu.open = false; break;
    case MI_QUIT: menu.quit = true; break;
  }
}

static void menuBack(void) {
  if(menu.binding >= 0) menu.binding = -1;
  else if(menu.page != 0) {
    menu.sel = menu.page == 1 ? MI_KEYS : menu.page == 3 ? MI_KEYS2 : menu.page == 4 ? MI_HOTKEYS : MI_PADS;
    menu.page = 0;
  } else menu.open = false;
}

static void menuMove(int d) {
  int n = menuCount();
  menu.sel = (menu.sel + d + n) % n;
}

static void openMenu(void) {
  menu.open = true;
  menu.page = 0;
  menu.sel = MI_RESUME;
  menu.binding = -1;
  app.keys[1] = app.keys[2] = 0;
}

// A key while the menu is open.
static void menuKey(SDL_Keycode key) {
  if(menu.binding >= 0) {
    if(key == SDLK_ESCAPE) menu.binding = -1;
    else if(menu.page == 1 || menu.page == 3) {
      rebind(menu.binding, false, menu.page == 3 ? 2 : 1, (int) key);
      menu.binding = -1;
    }
    return;
  }
  switch(key) {
    case SDLK_UP: menuMove(-1); break;
    case SDLK_DOWN: menuMove(1); break;
    case SDLK_LEFT: if(menu.page == 0) menuActivate(-1); break;
    case SDLK_RIGHT: case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE: menuActivate(1); break;
    case SDLK_ESCAPE: case SDLK_BACKSPACE: menuBack(); break;
    case SDLK_TAB: menu.open = false; break;
    default: break;
  }
}

static void menuPadButton(SDL_GamepadButton b) {
  if(menu.binding >= 0) {
    if(menu.page == 2 && b != SDL_GAMEPAD_BUTTON_GUIDE) {
      rebind(menu.binding, true, 1, (int) b);
      menu.binding = -1;
    }
    return;
  }
  switch(b) {
    case SDL_GAMEPAD_BUTTON_DPAD_UP: menuMove(-1); break;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: menuMove(1); break;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: if(menu.page == 0) menuActivate(-1); break;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: case SDL_GAMEPAD_BUTTON_SOUTH: menuActivate(1); break;
    case SDL_GAMEPAD_BUTTON_EAST: menuBack(); break;
    case SDL_GAMEPAD_BUTTON_GUIDE: case SDL_GAMEPAD_BUTTON_START: menu.open = false; break;
    default: break;
  }
}

static bool menuOpen(void) { return menu.open; }

static void drawMenu(void) {
  int outW, outH;
  SDL_GetCurrentRenderOutputSize(app.renderer, &outW, &outH);
  int lines = menuCount() + 4 + (menu.page == 4 ? HOTKEY_COUNT + 1 : 0);
  float scale = SDL_max(1.0f, SDL_floorf(SDL_min(outH / (lines * 12.0f + 16), outW / 400.0f)));
  SDL_SetRenderScale(app.renderer, scale, scale);
  float w = outW / scale, h = outH / scale;
  SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(app.renderer, 0, 0, 0, 200);
  SDL_FRect box = {0, 0, w, h};
  SDL_RenderFillRect(app.renderer, &box);
  float x = SDL_max(8.0f, (w - 50 * 8) / 2), y = SDL_max(8.0f, (h - lines * 12) / 2);
  const char* title = menu.page == 0   ? "OPTIONS"
                      : menu.page == 1 ? "PLAYER 1 KEYS"
                      : menu.page == 3 ? "PLAYER 2 KEYS"
                      : menu.page == 4 ? "HOTKEYS"
                                       : "GAMEPAD CONTROLS (BOTH PADS)";
  SDL_SetRenderDrawColor(app.renderer, 255, 210, 80, 255);
  SDL_RenderDebugText(app.renderer, x, y, title);
  y += 20;
  if(menu.page == 4) {
    for(int i = 0; i < HOTKEY_COUNT; i++) {
      SDL_SetRenderDrawColor(app.renderer, 255, 236, 120, 255);
      SDL_RenderDebugText(app.renderer, x + 16, y, kHotkeys[i][0]);
      SDL_SetRenderDrawColor(app.renderer, 210, 210, 210, 255);
      SDL_RenderDebugText(app.renderer, x + 16 + 16 * 8, y, kHotkeys[i][1]);
      y += 12;
    }
    y += 12;
  }
  for(int i = 0; i < menuCount(); i++) {
    char text[160];
    if(menu.page == 0) {
      menuItemText(i, text, sizeof(text));
    } else if(i == 12 || menu.page == 4) {
      snprintf(text, sizeof(text), "Back");
    } else {
      char names[96];
      bindingNames(i, menu.page == 2, menu.page == 3 ? 2 : 1, names, sizeof(names));
      snprintf(text, sizeof(text), "%-7s %s", kButtonNames[i], menu.binding == i ? (menu.page != 2 ? "press a key (Esc: cancel)" : "press a button") : names);
    }
    bool sel = i == menu.sel;
    SDL_SetRenderDrawColor(app.renderer, sel ? 255 : 200, sel ? 255 : 200, sel ? 255 : 200, 255);
    SDL_RenderDebugText(app.renderer, x, y, sel ? ">" : " ");
    SDL_RenderDebugText(app.renderer, x + 16, y, text);
    y += 12;
  }
  y += 8;
  SDL_SetRenderDrawColor(app.renderer, 150, 150, 150, 255);
  SDL_RenderDebugText(app.renderer, x, y,
                      menu.page == 0   ? "Up/Down choose, Enter or Left/Right change, Tab closes"
                      : menu.page == 4 ? "Enter or Esc: back, Tab closes"
                                       : "Enter: change, Esc: back");
  SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_NONE);
  SDL_SetRenderScale(app.renderer, 1, 1);
}

// What F7 did, in a toast.
static void f7Toast(void) {
  static const char* cams[] = {"game camera", "high camera", "bumper camera"};
  char t[96];
  if(app.view3d) snprintf(t, sizeof(t), "3D: %s, %s", look3dName(), cams[app.camera3d % 3]);
  else snprintf(t, sizeof(t), "Original picture (F7 for 3D)");
  showToast(t);
}

// --toggle-3d-at: F7 as if pressed before the frame about to run.
static void toggleIfDue(void) {
  while(app.toggleNext < app.toggleCount && app.frame >= app.toggleAt[app.toggleNext]) {
    app.toggleNext++;
    setView3d(!app.view3d);
    updateTitle();
    f7Toast();
  }
}

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

// A crash: the recording keeps what was played and says where (the module and the offset in
// it, to look up in its map), then Windows goes on as it would have (its crash handling).
static LONG WINAPI crashNote(EXCEPTION_POINTERS* info) {
  if(app.record && info && info->ExceptionRecord) {
    const void* at = info->ExceptionRecord->ExceptionAddress;
    HMODULE mod = NULL;
    char path[MAX_PATH] = "?";
    if(GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR) at, &mod))
      GetModuleFileNameA(mod, path, sizeof(path));
    path[sizeof(path) - 1] = 0;
    const char* name = strrchr(path, '\\');
    fprintf(app.record, "# crashed at frame %u: exception 0x%08lX at %s+0x%llx\n", app.frame,
            (unsigned long) info->ExceptionRecord->ExceptionCode, name ? name + 1 : path,
            (unsigned long long) ((uintptr_t) at - (uintptr_t) mod));
    fflush(app.record);
  }
  return EXCEPTION_CONTINUE_SEARCH;
}
#endif

int main(int argc, char** argv) {
  SDL_SetAppMetadata(TG_APP_NAME, "0.1", TG_APP_ID);
  if(!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, TG_APP_NAME, SDL_GetError(), NULL);
    return 1;
  }

  // Remembered options first; the command line can turn more on.
  app.aspect = ASPECT_4_3;
  app.view3d = true;  // races in 3D unless turned off (F7, --2d)
  app.fullscreen = true;  // fullscreen (F11) and widescreen (F10) unless turned off
  app.wide = true;
  app.musicSet = TG_MUSIC_DEFAULT;  // the faithful remakes, when the player has them
  for(int p = 0; p < 3; p++) tg_select_input_reset(&app.selectInput[p]);
  // Any country without its password: the game is unchanged (the frontend only types the
  // password the game itself asks for), so it is on unless the player turns it off.
  app.countryFree = true;
  tg_country_input_reset(&app.countryInput);
  app.titleClassic = true;  // the title: the original's logo in 3D (the options menu has the modern one)
  app.modeTransition = true;  // F7's change between 2D and 3D as time passing (options menu)
  char* prefDir = SDL_GetPrefPath(TG_PREF_ORG, TG_PREF_APP);
  if(prefDir) {
    snprintf(app.settingsPath, sizeof(app.settingsPath), "%ssettings.txt", prefDir);
    snprintf(app.progressPath, sizeof(app.progressPath), "%sprogress.txt", prefDir);
    SDL_free(prefDir);
    loadSettings();
    loadProgress();
  }
  const char* romArg = NULL;
  const char* replayPath = NULL;
  bool record = true;
  bool original = false;
  bool noGl = false;  // --no-gl: the 3D races drawn as before (no OpenGL)
  const char* perfPath = SDL_getenv("TG_PERF_LOG");  // --perf-log FILE: the frame-time log from the start
  int windowW = 0, windowH = 0;  // --window WxH: the window's first size
  for(int i = 1; i < argc; i++) {
    if(strcmp(argv[i], "--no-record") == 0) record = false;
    else if(strcmp(argv[i], "--original") == 0) original = true;
    else if(strcmp(argv[i], "--smooth") == 0) app.smooth = true;
    else if(strcmp(argv[i], "--rumble") == 0) app.rumble = true;
    else if(strcmp(argv[i], "--smooth-scaling") == 0) {}  // removed; still accepted
    else if(strcmp(argv[i], "--full-view") == 0) app.fullView = true;
    else if(strcmp(argv[i], "--tall-road") == 0) app.tallRoad = true;
    else if(strcmp(argv[i], "--map-top") == 0) app.mapTop = true;
    else if(strcmp(argv[i], "--speed-bottom") == 0) app.speedBottom = true;
    else if(strcmp(argv[i], "--wide") == 0) app.wide = true;
    else if(strcmp(argv[i], "--3d") == 0) app.view3d = true;
    else if(strcmp(argv[i], "--no-gl") == 0) noGl = true;
    else if(strcmp(argv[i], "--perf-log") == 0 && i + 1 < argc) perfPath = argv[++i];
    else if(strcmp(argv[i], "--2d") == 0) app.view3d = false;
    else if(strcmp(argv[i], "--3d-models") == 0) app.view3d = app.models3d = app.classic3d = true;
    else if(strcmp(argv[i], "--3d-pictures") == 0) app.view3d = app.classic3d = true, app.models3d = false;
    else if(strcmp(argv[i], "--3d-modern") == 0) app.view3d = true, app.classic3d = false;
    else if(strcmp(argv[i], "--title-modern") == 0) app.titleClassic = false;
    else if(strcmp(argv[i], "--title-classic") == 0) app.titleClassic = true;
    else if(strcmp(argv[i], "--side-by-side") == 0) app.sideBySide = true;
    else if(strcmp(argv[i], "--ghost") == 0) app.ghost = true;
    else if(strcmp(argv[i], "--auto-save") == 0) app.autoSave = true;
    else if(strcmp(argv[i], "--country-free") == 0) app.countryFree = true;
    else if(strcmp(argv[i], "--country-password") == 0) app.countryFree = false;
    else if(strcmp(argv[i], "--tv-colour") == 0) app.tvColour = true;
    else if(strcmp(argv[i], "--high-fps") == 0) app.highFps = app.smooth = true;
    else if(strcmp(argv[i], "--filter") == 0 && i + 1 < argc) {
      i++;
      for(int f = 0; f < FILTER_COUNT; f++) if(strcmp(argv[i], filterNames[f]) == 0) app.filter = f;
    }
    else if(strcmp(argv[i], "--replay") == 0 && i + 1 < argc) replayPath = argv[++i];
    else if(strcmp(argv[i], "--shots") == 0 && i + 1 < argc) app.shotsDir = argv[++i];
    else if(strcmp(argv[i], "--shots-every") == 0 && i + 1 < argc) app.shotsEvery = (uint32_t) SDL_atoi(argv[++i]);
    else if(strcmp(argv[i], "--shots-from") == 0 && i + 1 < argc) app.shotsNext = (uint32_t) SDL_atoi(argv[++i]);
    else if(strcmp(argv[i], "--frames") == 0 && i + 1 < argc) app.quitAt = (uint32_t) SDL_atoi(argv[++i]);
    else if(strcmp(argv[i], "--fake-hz") == 0 && i + 1 < argc) app.fakeHz = (uint32_t) SDL_atoi(argv[++i]);
    else if(strcmp(argv[i], "--toggle-3d-at") == 0 && i + 1 < argc) {
      for(const char* p = argv[++i]; *p && app.toggleCount < (int) SDL_arraysize(app.toggleAt);) {
        app.toggleAt[app.toggleCount++] = (uint32_t) SDL_atoi(p);
        while(*p && *p != ',') p++;
        if(*p == ',') p++;
      }
    }
    else if(strcmp(argv[i], "--no-transition") == 0) app.modeTransition = false;
    else if(strcmp(argv[i], "--transition") == 0) app.modeTransition = true;
    else if(strcmp(argv[i], "--window") == 0 && i + 1 < argc) sscanf(argv[++i], "%dx%d", &windowW, &windowH);
    else if(strcmp(argv[i], "--music") == 0 && i + 1 < argc) {
      int set = tg_music_set_from_key(argv[++i]);
      if(set >= 0) app.musicSet = set;
    }
    else romArg = argv[i];
  }

  if(app.shotsEvery == 0) app.shotsEvery = 1;
  if(windowW <= 0 || windowH <= 0) windowW = app.wide ? 1366 : 1024, windowH = 768;
  TgRom rom;
  char message[4096];
  if(!findRom(romArg, &rom, message, sizeof(message))) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, TG_APP_NAME, message, NULL);
    SDL_Quit();
    return 1;
  }
  app.game = tg_game_create(&rom);
  if(app.game == NULL) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, TG_APP_NAME, "Failed to start the game.", NULL);
    return 1;
  }
  // The translated C code runs the races; --original runs the game's own code throughout,
  // with its slowdown and loading pauses (recordings made that way can be checked call by
  // call: tg_headless --port verify).
  app.original = original;
  tg_game_set_port_mode(app.game, original ? TG_PORT_OFF : TG_PORT_NATIVE);
  {
    // The modern music (the player's own files, made by the launcher's Build music):
    // audio-modern next to the game, or the per-user one when the launcher couldn't write
    // into the game's folder.
    const char* base = SDL_GetBasePath();
    char dir[2400], user[2048];
    snprintf(dir, sizeof(dir), "%saudio-modern", base ? base : "");
    if(!tg_music_dir_has_music(dir) && tg_user_data_dir(user, sizeof(user))) {
      char alt[2400];
      snprintf(alt, sizeof(alt), "%saudio-modern", user);
      if(tg_music_dir_has_music(alt)) snprintf(dir, sizeof(dir), "%s", alt);
    }
    app.music = tg_music_create(dir, AUDIO_RATE);
    if(app.music) {
      tg_music_set_set(app.music, app.musicSet);
      tg_music_attach(app.music, app.game);
    }
  }

  if(replayPath) {
    if(!tg_input_script_load(replayPath, &app.replay, message, sizeof(message))) {
      SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, TG_APP_NAME, message, NULL);
      return 1;
    }
    app.replaying = true;
    record = false;
  }
  if(record) startRecording();
#ifdef _WIN32
  if(app.record) SetUnhandledExceptionFilter(crashNote);
#endif
  // Ghost cars: the best race on each course, kept with the settings.
  char* pref = SDL_GetPrefPath(TG_PREF_ORG, TG_PREF_APP);
  if(pref) {
    snprintf(app.ghostPath, sizeof(app.ghostPath), "%sghosts.bin", pref);
    SDL_free(pref);
    tg_game_load_ghosts(app.game, app.ghostPath);
  }

  loadControls();
  // The 3D races draw with OpenGL and a depth buffer (gl3d.c): the window gets one, and the
  // game's 2D renderer is SDL's OpenGL one (else everything as before).
  bool wantGl = !noGl;
  if(wantGl) {
    tg_gl3d_prepare();
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");
  }
  if(!SDL_CreateWindowAndRenderer(TG_APP_NAME, windowW, windowH, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY, &app.window, &app.renderer)) {
    SDL_ResetHint(SDL_HINT_RENDER_DRIVER);
    SDL_GL_ResetAttributes();
    wantGl = false;
    if(!SDL_CreateWindowAndRenderer(TG_APP_NAME, windowW, windowH, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY, &app.window, &app.renderer)) {
      SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, TG_APP_NAME, SDL_GetError(), NULL);
      return 1;
    }
  }
  app.gl3d = wantGl && tg_gl3d_init(app.window, app.renderer);
  {
    // What draws the game on this PC, in the log and in the session's recording (comment lines).
    char what[256];
    snprintf(what, sizeof(what), "renderer %s, 3D races by %s%s", SDL_GetRendererName(app.renderer),
             app.gl3d ? "OpenGL: " : "the renderer", tg_gl3d_info());
    SDL_Log("%s", what);
    if(app.record) recordf("# %.200s\n", what);
  }
  SDL_SetWindowMinimumSize(app.window, SNES_W, SNES_H);
  if(app.fullscreen) SDL_SetWindowFullscreen(app.window, true);
  app.texture = SDL_CreateTexture(app.renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, TG_FB_WIDTH, TG_FB_HEIGHT);
  app.v3 = tg_view3d_create();
  if(app.v3) tg_view3d_set_title_style(app.v3, app.titleClassic ? TG_V3_TITLE_CLASSIC : TG_V3_TITLE_MODERN);
  if(app.v3 && app.settingsPath[0]) {
    // What the 3D view learns is kept with the settings (it comes from the player's ROM).
    char dir[2400];
    snprintf(dir, sizeof(dir), "%s", app.settingsPath);
    char* cut = strrchr(dir, '/');
    char* cut2 = strrchr(dir, '\\');
    if(cut2 > cut) cut = cut2;
    if(cut) {
      cut[1] = 0;
      tg_view3d_set_cache_dir(app.v3, dir);
    }
  }
  if(app.v3) {
    // Replacement pictures (upscaled or redrawn) in art3d next to the game.
    char* base = SDL_GetBasePath() ? SDL_strdup(SDL_GetBasePath()) : NULL;
    if(base) {
      char dir[2400];
      snprintf(dir, sizeof(dir), "%sart3d%c", base, base[0] && strchr(base, '\\') ? '\\' : '/');
      tg_view3d_set_art_dir(app.v3, dir);
      SDL_free(base);
    }
  }
  app.v3pixels = SDL_calloc(TG_FB_WIDTH * TG_FB_HEIGHT, 4);
  if(perfPath && perfPath[0]) perfOpen(perfPath);
  SDL_SetTextureScaleMode(app.texture, SDL_SCALEMODE_NEAREST);
  updateTitle();
  // The 3D title screens show the new keys; the original's picture doesn't, so say them once.
  if(!want3d()) {
    showToast("Tab: options and hotkeys    F7: 3D");
    app.toastUntil = SDL_GetTicksNS() + 6000000000ull;
  }

  SDL_AudioSpec spec = {SDL_AUDIO_S16, 2, AUDIO_RATE};
  app.audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
  if(app.audio) SDL_ResumeAudioStreamDevice(app.audio);

  Uint64 next = loopNow();
  double sampleAcc = 0;
  bool running = true;
  while(running) {
    SDL_Event e;
    while(SDL_PollEvent(&e)) {
      switch(e.type) {
        case SDL_EVENT_QUIT: running = false; break;
        case SDL_EVENT_GAMEPAD_ADDED: openPad(e.gdevice.which); break;
        case SDL_EVENT_GAMEPAD_REMOVED: closePad(e.gdevice.which); break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
          if(menu.open) menuPadButton((SDL_GamepadButton) e.gbutton.button);
          else if(e.gbutton.button == SDL_GAMEPAD_BUTTON_GUIDE) openMenu();
          break;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP: {
          bool down = e.type == SDL_EVENT_KEY_DOWN;
          if(menu.open) {
            if(down) menuKey(e.key.key);
            break;
          }
          if(down && !e.key.repeat && e.key.key == SDLK_TAB) {
            openMenu();
            break;
          }
          if(down && !e.key.repeat) {
            if(e.key.key == SDLK_F11 || (e.key.key == SDLK_RETURN && (e.key.mod & SDL_KMOD_ALT))) {
              app.fullscreen = !app.fullscreen;
              SDL_SetWindowFullscreen(app.window, app.fullscreen);
              saveSettings();
              break;
            }
            if(e.key.key == SDLK_ESCAPE && app.fullscreen) {
              app.fullscreen = false;
              SDL_SetWindowFullscreen(app.window, false);
              saveSettings();
              break;
            }
            if(e.key.key == SDLK_F2 && (e.key.mod & SDL_KMOD_SHIFT)) { perfToggle(); break; }
            if(e.key.key == SDLK_F2) { app.aspect = (app.aspect + 1) % ASPECT_COUNT; updateTitle(); break; }
            if(e.key.key == SDLK_F3) { app.integerScale = !app.integerScale; updateTitle(); break; }
            if(e.key.key == SDLK_F4) { app.filter = (app.filter + 1) % FILTER_COUNT; updateTitle(); break; }
            if(e.key.key == SDLK_F5) { app.resetRequested = true; break; }
            if(e.key.key == SDLK_F6 && (e.key.mod & SDL_KMOD_SHIFT)) {
              // In-between frames need the race to move every frame: smooth motion comes on too.
              app.highFps = !app.highFps;
              if(app.highFps && !app.replaying) app.smooth = true;
              updateTitle();
              break;
            }
            if(e.key.key == SDLK_F6 && !app.replaying) {
              app.smooth = !app.smooth;
              if(!app.smooth) app.highFps = false;
              updateTitle();
              break;
            }
            if(e.key.key == SDLK_F7) {
              // 3D mod: on and off; Shift: the camera; Ctrl: the game's pictures or models.
              if(e.key.mod & SDL_KMOD_SHIFT) app.camera3d = (app.camera3d + 1) % TG_V3_CAM_COUNT;
              else if(e.key.mod & SDL_KMOD_CTRL) nextLook3d();
              else setView3d(!app.view3d);
              updateTitle();
              f7Toast();
              break;
            }
            if(e.key.key == SDLK_F8) { app.rumble = !app.rumble; updateTitle(); break; }
            if(e.key.key == SDLK_F9 && !app.replaying) {
              // Off, then full view with the original's low horizon, then with a tall road.
              if(!app.fullView) app.fullView = true;
              else if(!app.tallRoad) app.tallRoad = true;
              else app.fullView = app.tallRoad = false;
              updateTitle();
              break;
            }
            if(e.key.key == SDLK_F12 && !app.replaying) { app.sideBySide = !app.sideBySide; updateTitle(); break; }
            if(e.key.key == SDLK_F1 && (e.key.mod & SDL_KMOD_SHIFT)) { app.autoSave = !app.autoSave; updateTitle(); break; }
            if(e.key.key == SDLK_F1 && !app.replaying) { app.ghost = !app.ghost; updateTitle(); break; }
            if(e.key.key == SDLK_F10 && !app.replaying) {
              app.wide = !app.wide;
              if(!app.fullscreen) {
                // A window gets the shape of the picture: 16:9 for widescreen, else 4:3.
                int w, h;
                SDL_GetWindowSize(app.window, &w, &h);
                SDL_SetWindowSize(app.window, app.wide ? h * 16 / 9 : h * 4 / 3, h);
              }
              updateTitle();
              break;
            }
          }
          for(int p = 1; p <= 2; p++) {
            uint16_t button = keyToButton(e.key.key, p);
            if(button && !(e.key.mod & SDL_KMOD_ALT)) {
              if(down) app.keys[p] |= button;
              else app.keys[p] &= ~button;
            }
          }
          break;
        }
      }
    }

    if(menu.quit || (app.quitAt && app.frame >= app.quitAt)) break;
    Uint64 now = loopNow();
    if(menu.open) {
      // The game waits; the picture stays, with the menu over it.
      present();
      SDL_DelayPrecise((Uint64) FRAME_NS);
      next = SDL_GetTicksNS();
      continue;
    }
    if(now - next > (Uint64) (FRAME_NS * 8) && now > next) next = now;  // resync after a stall
    // High frame rate, and the 3D view in a race, show a picture every refresh of the
    // display, waiting for it (no tearing), moving on smoothly between the game's frames.
    bool race3d = want3d() && app.race3d;
    bool highFps = (app.highFps || race3d) && !app.original;
    if(highFps != app.vsync) {
      SDL_SetRenderVSync(app.renderer, highFps ? 1 : 0);
      app.vsync = highFps;
    }
    if(highFps) {
      // Every frame due runs, so the game keeps its pace when pictures take longer than a
      // frame to draw.
      for(int k = 0; k < 4 && now >= next; k++) {
        sampleAcc += AUDIO_RATE * FRAME_NS / 1e9;
        int samples = (int) sampleAcc;
        sampleAcc -= samples;
        toggleIfDue();
        runFrame(samples);
        next += (Uint64) FRAME_NS;
        now = loopNow();
      }
      // The picture between the last two frames, as far on as the time since the last.
      double t = 1.0 - (double) (next > now ? next - now : 0) / FRAME_NS;
      if(race3d) tg_view3d_set_subframe(app.v3, (float) t);
      else if(tg_game_interp(app.game, (float) t)) uploadPicture();
      present();
      if(app.fakeHz) app.fakeNs += 1000000000ull / app.fakeHz;
    } else if(now >= next) {
      // As above: frames that are late run before the next picture.
      for(int k = 0; k < 4 && now >= next; k++) {
        sampleAcc += AUDIO_RATE * FRAME_NS / 1e9;
        int samples = (int) sampleAcc;
        sampleAcc -= samples;
        toggleIfDue();
        runFrame(samples);
        next += (Uint64) FRAME_NS;
        now = loopNow();
      }
      present();
      if(app.fakeHz) app.fakeNs += 1000000000ull / app.fakeHz;
    } else if(app.fakeHz) {
      app.fakeNs = next;
    } else {
      SDL_DelayPrecise(next - now);
    }
  }

  stopRecording();
  perfClose();
  tg_input_script_free(&app.replay);
  tg_music_destroy(app.music);
  tg_game_destroy(app.game);
  tg_rom_free(&rom);
  for(int i = 0; i < 2; i++) if(app.pads[i]) SDL_CloseGamepad(app.pads[i]);
  if(app.audio) SDL_DestroyAudioStream(app.audio);
  SDL_DestroyTexture(app.texture);
  for(int i = 0; i < V3_PICS; i++) if(app.v3tex[i]) SDL_DestroyTexture(app.v3tex[i]);
  if(app.trans2d) SDL_DestroyTexture(app.trans2d);
  if(app.trans3dTex) SDL_DestroyTexture(app.trans3dTex);
  tg_v3_list_free(&app.v3list);
  tg_v3_list_free(&app.transList);
  tg_gl3d_shutdown();
  tg_view3d_destroy(app.v3);
  SDL_free(app.v3pixels);
  SDL_DestroyRenderer(app.renderer);
  SDL_DestroyWindow(app.window);
  SDL_Quit();
  return 0;
}
