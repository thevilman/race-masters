// Runs the game without a window, for tests and investigation.
//
//   tg_headless --rom <file> [--frames N] [--input script.txt]
//               [--screenshot out.ppm] [--shots-every N --shots-dir DIR [--shots-from F]]
//               [--expect-wram HEX] [--expect-frame HEX] [--coverage FILE] [--dump-wram FILE]
//               [--trace-frame N [--trace-count K] --trace-out FILE] [--port off|verify|native]
//               [--frame-hashes FILE] [--race-frames N] [--ghosts FILE] [--interp-steps K]
//
// Prints the SHA-256 of work RAM and of the final frame so runs can be compared.
// With --expect-*, exits non-zero when a hash differs (used by the regression tests).
// With --coverage, adds this run's code/data coverage to FILE (created if missing).
// With --port verify, every call to a translated routine is checked against the original
// (see src/lockstep.h); any mismatch makes the run fail.
// With --frame-hashes, writes a 64-bit hash of every frame's picture, one line per frame,
// for comparing runs whose timing differs (tools/shift_compare.py).
// --race-frames sets the video frames per race update when the race loop runs in C
// (native mode); the default is 3, the original's pace without its slowdown.
// --interp-steps turns on high frame rate and, with --shots-every, also writes each shot's
// in-between pictures from the frame before (frameNNNNNN_k.ppm, k of K of the way).
// --ghosts loads ghost-car races from FILE and saves them back if a better race is driven.
// 3D mod (docs/3D-MOD.md): --track3d-obj writes the first course raced as a 3D circuit
// (Wavefront OBJ); --scene-log writes every car's place on the course and in the world,
// one CSV row per car per race update; --ppu-shots writes the PPU state (VRAM, CGRAM, OAM)
// next to each shot, for tools/extract_sprites.py.
// Modern music (docs/AUDIO.md): --music SET [--music-dir DIR] --music-log FILE|- logs every
// song the game starts, stops or pauses and the file that plays for it; --music-wav FILE
// also renders what would be heard; --music-courses lists the courses' names from the ROM
// beside the music's city table. --select-linear runs the input through the modern car
// select's Left/Right translation (src/select_input.h) and logs it. --country-free runs it
// through the "Country select: FREE" option (src/country_input.h): a locked country picked
// on the country screen gets its password typed by the frontend; logged. --record-input FILE
// writes what the game received (after those translations), as the game's recordings do.

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "coverage.h"
#include "game.h"
#include "port/port.h"
#include "input_script.h"
#include "rom.h"
#include "scene.h"
#include "view3d.h"
#include "sha256.h"
#include "music.h"
#include "select_input.h"
#include "country_input.h"
#include "now_ms.h"

static TgGame* g_game;
static TgTrack3D g_track;
static TgScene g_scene;

// --shots3d: the race in 3D next to each shot, as frameNNNNNN_3d.ppm (and _3dm.ppm with
// the models look).
static void write3d(const char* path, TgView3D* v, TgV3Look look, int w, int h) {
  static TgV3List list;
  const char* cam = getenv("TG_V3_CAM");  // 0 the game's camera, 1 high, 2 bumper
  TgV3Camera c = cam ? (TgV3Camera) (atoi(cam) % TG_V3_CAM_COUNT) : TG_V3_CAM_GAME;
  uint32_t* buf = calloc((size_t) w * h, 4);
  int views = tg_view3d_views(v), vh = h / views;  // 2 players: one above the other
  for(int k = 0; k < views; k++) {
    double t0 = tg_now_ms();
    tg_view3d_draw_view(v, k, w, vh, look, c, &list);
    double t1 = tg_now_ms();
    tg_v3_raster(v, &list, buf + (size_t) k * vh * w, w, vh);
    if(getenv("TG_V3_PICS") && look == TG_V3_LOOK_MODERN) {  // which pictures the view drew (debugging)
      fprintf(stderr, "pics:");
      for(int b = 0; b < list.nb; b++)
        if(list.b[b].pic >= 0) fprintf(stderr, " %d/%d", list.b[b].pic, list.b[b].count / 3);
      fprintf(stderr, "\n");
    }
    double t2 = tg_now_ms();
    // Read by tools/qa (speed check): the look and view tell the modern look's lines apart;
    // raster is the software rasterizer (headless only), so only a relative number.
    if(getenv("TG_V3_TIME"))
      fprintf(stderr, "draw %.3f ms, %d triangles, %d batches, look %d, view %d of %d, raster %.3f ms\n",
              t1 - t0, list.nv / 3, list.nb, (int) look, k, views, t2 - t1);
  }
  FILE* f = fopen(path, "wb");
  if(f) {
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for(int i = 0; i < w * h; i++) {
      uint8_t rgb[3] = {(uint8_t) (buf[i] >> 16), (uint8_t) (buf[i] >> 8), (uint8_t) buf[i]};
      fwrite(rgb, 1, 3, f);
    }
    fclose(f);
  }
  free(buf);
}

// --showcase DIR: each of the five cars (the select screen's four and the rivals' amber) from a
// few sides, as car<t>_<yaw>.ppm.
static void showcase(TgView3D* v, const char* dir) {
  static TgV3List list;
  static const float yaws[5] = {0, 35, 90, 150, 215};
  int w = 640, h = 400;
  if(getenv("TG_SHOWCASE_SIZE")) sscanf(getenv("TG_SHOWCASE_SIZE"), "%dx%d", &w, &h);  // e.g. 1280x800
  uint32_t* buf = calloc((size_t) w * h, 4);
  for(int t = 0; t < 5; t++)
    for(int k = 0; k < 5; k++) {
      memset(buf, 0, (size_t) w * h * 4);
      tg_view3d_showcase(v, t, yaws[k], k == 0 ? 16 : 12, w, h, &list);
      tg_v3_raster(v, &list, buf, w, h);
      char path[1024];
      if(getenv("TG_SHOWCASE_DUMP")) {
        // The triangles as drawn, for a GL check (tools/qa/gl_check.c): a header (w, h, haze,
        // the vertex's size, batches), per batch its picture's slot in this file (-1 none), its
        // vertex count and screen door (TgV3Batch.door) and vertices (TgV3Vertex), then the pictures (w, h, mips, flags: 1 smooth, 2 mask; pixels
        // with their mip levels).
        snprintf(path, sizeof(path), "%s/car%d_%03d.v3", dir, t, (int) yaws[k]);
        FILE* d = fopen(path, "wb");
        if(d) {
          // (each batch: its picture's slot, its vertex count and its screen door, then its vertices)
          int pics[64], np = 0;
          uint32_t hd[5] = {(uint32_t) w, (uint32_t) h, list.haze, (uint32_t) sizeof(TgV3Vertex), (uint32_t) list.nb};
          fwrite(hd, sizeof(hd), 1, d);
          for(int b = 0; b < list.nb; b++) {
            int slot = -1;
            const TgV3Pic* p = list.b[b].pic >= 0 ? tg_view3d_pic(v, list.b[b].pic) : NULL;
            if(p && p->w && p->px) {
              for(int i = 0; i < np; i++)
                if(pics[i] == list.b[b].pic) slot = i;
              if(slot < 0 && np < 64) pics[slot = np++] = list.b[b].pic;
            }
            int32_t bh[3] = {slot, list.b[b].count, list.b[b].door};
            fwrite(bh, sizeof(bh), 1, d);
            fwrite(&list.v[list.b[b].first], sizeof(TgV3Vertex), (size_t) list.b[b].count, d);
          }
          for(int i = 0; i < np; i++) {
            const TgV3Pic* p = tg_view3d_pic(v, pics[i]);
            uint32_t ph[4] = {(uint32_t) p->w, (uint32_t) p->h, (uint32_t) p->mips, (p->smooth ? 1u : 0u) | (p->mask ? 2u : 0u)};
            fwrite(ph, sizeof(ph), 1, d);
            size_t n = 0;
            for(int l = 0, lw = p->w, lh = p->h; l <= p->mips; l++, lw = lw > 1 ? lw / 2 : 1, lh = lh > 1 ? lh / 2 : 1) n += (size_t) lw * lh;
            fwrite(p->px, 4, n, d);
          }
          fclose(d);
        }
      }
      snprintf(path, sizeof(path), "%s/car%d_%03d.ppm", dir, t, (int) yaws[k]);
      FILE* f = fopen(path, "wb");
      if(!f) continue;
      fprintf(f, "P6\n%d %d\n255\n", w, h);
      for(int i = 0; i < w * h; i++) {
        uint8_t rgb[3] = {(uint8_t) (buf[i] >> 16), (uint8_t) (buf[i] >> 8), (uint8_t) buf[i]};
        fwrite(rgb, 1, 3, f);
      }
      fclose(f);
    }
  free(buf);
}

static void writePpm(const char* path, const uint8_t* xrgb) {
  // Writes the visible 256x224 picture (every other pixel of the 512x448 area), or the
  // wide picture in widescreen.
  const uint32_t* wide = tg_game_wide_pixels(g_game);
  int w = 256 + 2 * tg_game_wide_margin(g_game);
  FILE* f = fopen(path, "wb");
  if(f == NULL) {
    fprintf(stderr, "cannot write %s\n", path);
    return;
  }
  if(wide) {
    fprintf(f, "P6\n%d 224\n255\n", w);
    for(int i = 0; i < w * 224; i++) {
      uint8_t rgb[3] = {(uint8_t) (wide[i] >> 16), (uint8_t) (wide[i] >> 8), (uint8_t) wide[i]};
      fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    return;
  }
  fprintf(f, "P6\n256 224\n255\n");
  for(int y = 0; y < 224; y++) {
    for(int x = 0; x < 256; x++) {
      const uint8_t* p = xrgb + ((16 + y * 2) * TG_FB_WIDTH + x * 2) * 4;
      uint8_t rgb[3] = {p[2], p[1], p[0]};
      fwrite(rgb, 1, 3, f);
    }
  }
  fclose(f);
}

// --music-wav: a 48 kHz 16-bit stereo WAV, its sizes filled in at the end.
static void wavHeader(FILE* f, uint32_t frames) {
  uint32_t data = frames * 4;
  uint8_t h[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 2, 0,
                   0x80, 0xBB, 0, 0, 0, 0xEE, 2, 0, 4, 0, 16, 0, 'd', 'a', 't', 'a'};
  uint32_t riff = 36 + data;
  for(int i = 0; i < 4; i++) h[4 + i] = (uint8_t) (riff >> (8 * i)), h[40 + i] = (uint8_t) (data >> (8 * i));
  fseek(f, 0, SEEK_SET);
  fwrite(h, 1, 44, f);
}

// The course's name in the ROM (as src/mod3d/view3d.c course_name reads it), to check the
// music's city table against.
static void romCourseName(const TgRom* rom, int course, char* out, size_t n) {
  out[0] = 0;
  int32_t o = tg_lorom_offset(0x0DC77C + (uint32_t) ((course & 31) << 1));
  if(o < 0) return;
  uint16_t x = (uint16_t) (rom->data[o] | rom->data[o + 1] << 8);
  int32_t p = tg_lorom_offset(0x0F0000 | x);
  if(p < 0 || (rom->data[p] | rom->data[p + 1] << 8) == 0) return;
  size_t k = 0;
  for(int32_t q = tg_lorom_offset(0x0F0000 | (uint16_t) (x + 4)); q >= 0 && rom->data[q] && k + 1 < n; q++)
    out[k++] = rom->data[q] == '=' ? '.' : (char) rom->data[q];
  out[k] = 0;
}

// Prints the hash; returns false if expected is set and differs.
static bool checkHash(const char* label, const uint8_t* data, size_t len, const char* expected) {
  uint8_t d[32];
  char hex[65];
  tg_sha256(data, len, d);
  for(int i = 0; i < 32; i++) sprintf(hex + i * 2, "%02x", d[i]);
  bool ok = expected == NULL || strcmp(hex, expected) == 0;
  printf("%s %s%s\n", label, hex, ok ? "" : "  MISMATCH");
  if(!ok) printf("%s expected %s\n", label, expected);
  return ok;
}

int main(int argc, char** argv) {
  const char* romPath = NULL;
  const char* inputPath = NULL;
  const char* screenshot = NULL;
  const char* shotsDir = NULL;
  const char* expectWram = NULL;
  const char* expectFrame = NULL;
  const char* coveragePath = NULL;
  const char* dumpWram = NULL;
  long watchWord = -1;  // print this WRAM word once a second
  long setCourseFrame = -1;
  int setCourse = 0;  // --set-course FRAME:N: the course ($1F06 bits 0-4) set at that frame (to see any course)
  const char* frameHashes = NULL;
  const char* ghostsPath = NULL;
  int interpSteps = 0;
  const char* traceOut = NULL;
  long traceFrame = -1;
  long traceCount = 1;
  FILE* trace = NULL;
  long frames = 600;
  long shotsEvery = 0;
  long shotsFrom = 0;
  const char* dumpPpu = NULL;
  const char* trackObj = NULL;
  const char* sceneLog = NULL;
  const char* wramLog = NULL;  // --wram-log: the car list ($0200-$037F) and the race variables ($1E00-$1EFF) each frame, in hex
  bool ppuShots = false;
  int shots3dW = 0, shots3dH = 0;  // --shots3d WxH
  const char* dumpPics = NULL;      // --dump-pics DIR: the 3D view's pictures at the end
  const char* v3Cache = NULL;       // --v3-cache DIR/: keep what the 3D view learns there
  const char* artDir = NULL;        // --art3d DIR/: replacement pictures
  const char* showcaseDir = NULL;   // --showcase DIR: the car models from a few sides at the end
  const char* perfLog = NULL;       // --perf-log FILE: per frame, the 3D view's CPU times and texture uploads (CSV)
  int titleStyle = 0;               // --title-style modern|classic|both (or TG_TITLE_STYLE): which title _sel.ppm shows;
                                    // both: _sel.ppm the modern one, _selc.ppm the classic one
  const char* exportArt = NULL;     // --export-art DIR/: the 3D view's pictures at the end, as BMP  // with each shot, also the PPU state (for tools/extract_sprites.py)
  TgPortMode portMode = TG_PORT_OFF;
  long nativeFrom = -1;  // switch to the C code from this frame (to look at enhancements in a recording made with the original)
  // Modern music (docs/AUDIO.md): --music SET picks the set, --music-dir DIR the player's
  // audio-modern folder, --music-log FILE (- for the output) logs every song change and the
  // file chosen, --music-wav FILE writes what would be heard (48 kHz). --music-courses
  // prints the 32 courses' names from the ROM beside the music's city table.
  int musicSet = -1;
  const char* musicDir = NULL;
  const char* musicLog = NULL;
  const char* musicWav = NULL;
  bool musicCourses = false;
  long musicSwitchFrame = -1;  // --music-switch FRAME:SET: the set changed then, as from the options menu
  int musicSwitchSet = -1;
  // --select-linear: the input is what the player holds; while the modern car select is up
  // it goes through the frontend's line-to-grid translation (src/select_input.h), logged.
  bool selectLinear = false;
  const char* recordInput = NULL;  // --record-input FILE
  bool countryFree = false;  // --country-free: the frontend's password typing (country_input.h)
  for(int i = 1; i < argc; i++) {
    const char* next = i + 1 < argc ? argv[i + 1] : NULL;
    if(strcmp(argv[i], "--rom") == 0 && next) romPath = argv[++i];
    else if(strcmp(argv[i], "--music") == 0 && next) {
      musicSet = tg_music_set_from_key(argv[++i]);
      if(musicSet < 0) {
        fprintf(stderr, "--music: one of");
        for(int k = 0; k < TG_MUSIC_SET_COUNT; k++) fprintf(stderr, " %s", tg_music_set_key(k));
        fprintf(stderr, "\n");
        return 2;
      }
    }
    else if(strcmp(argv[i], "--music-dir") == 0 && next) musicDir = argv[++i];
    else if(strcmp(argv[i], "--music-log") == 0 && next) musicLog = argv[++i];
    else if(strcmp(argv[i], "--music-wav") == 0 && next) musicWav = argv[++i];
    else if(strcmp(argv[i], "--music-courses") == 0) musicCourses = true;
    else if(strcmp(argv[i], "--music-switch") == 0 && next) {
      char key[32];
      if(sscanf(argv[++i], "%ld:%31s", &musicSwitchFrame, key) != 2 || (musicSwitchSet = tg_music_set_from_key(key)) < 0) {
        fprintf(stderr, "--music-switch FRAME:SET\n");
        return 2;
      }
    }
    else if(strcmp(argv[i], "--select-linear") == 0) selectLinear = true;
    else if(strcmp(argv[i], "--country-free") == 0) countryFree = true;
    else if(strcmp(argv[i], "--record-input") == 0 && next) recordInput = argv[++i];
    else if(strcmp(argv[i], "--frames") == 0 && next) frames = strtol(argv[++i], NULL, 10);
    else if(strcmp(argv[i], "--input") == 0 && next) inputPath = argv[++i];
    else if(strcmp(argv[i], "--screenshot") == 0 && next) screenshot = argv[++i];
    else if(strcmp(argv[i], "--shots-every") == 0 && next) shotsEvery = strtol(argv[++i], NULL, 10);
    else if(strcmp(argv[i], "--shots-dir") == 0 && next) shotsDir = argv[++i];
    else if(strcmp(argv[i], "--dump-ppu") == 0 && next) dumpPpu = argv[++i];
    else if(strcmp(argv[i], "--shots-from") == 0 && next) shotsFrom = strtol(argv[++i], NULL, 10);
    else if(strcmp(argv[i], "--expect-wram") == 0 && next) expectWram = argv[++i];
    else if(strcmp(argv[i], "--expect-frame") == 0 && next) expectFrame = argv[++i];
    else if(strcmp(argv[i], "--coverage") == 0 && next) coveragePath = argv[++i];
    else if(strcmp(argv[i], "--dump-wram") == 0 && next) dumpWram = argv[++i];
    else if(strcmp(argv[i], "--watch-word") == 0 && next) watchWord = strtol(argv[++i], NULL, 16);
    else if(strcmp(argv[i], "--set-course") == 0 && next && sscanf(argv[++i], "%ld:%d", &setCourseFrame, &setCourse) == 2) {}
    else if(strcmp(argv[i], "--frame-hashes") == 0 && next) frameHashes = argv[++i];
    else if(strcmp(argv[i], "--ghosts") == 0 && next) ghostsPath = argv[++i];
    else if(strcmp(argv[i], "--interp-steps") == 0 && next) interpSteps = (int) strtol(argv[++i], NULL, 10);
    else if(strcmp(argv[i], "--race-frames") == 0 && next) tg_port_race_frames = (int) strtol(argv[++i], NULL, 10);
    else if(strcmp(argv[i], "--trace-frame") == 0 && next) traceFrame = strtol(argv[++i], NULL, 10);
    else if(strcmp(argv[i], "--trace-out") == 0 && next) traceOut = argv[++i];
    else if(strcmp(argv[i], "--trace-count") == 0 && next) traceCount = strtol(argv[++i], NULL, 10);
    else if(strcmp(argv[i], "--port") == 0 && next && strcmp(next, "off") == 0) { portMode = TG_PORT_OFF; i++; }
    else if(strcmp(argv[i], "--port") == 0 && next && strcmp(next, "verify") == 0) { portMode = TG_PORT_VERIFY; i++; }
    else if(strcmp(argv[i], "--port") == 0 && next && strcmp(next, "native") == 0) { portMode = TG_PORT_NATIVE; i++; }
    else if(strcmp(argv[i], "--native-from") == 0 && next) { nativeFrom = atol(next); i++; }
    else if(strcmp(argv[i], "--track3d-obj") == 0 && next) trackObj = argv[++i];
    else if(strcmp(argv[i], "--scene-log") == 0 && next) sceneLog = argv[++i];
    else if(strcmp(argv[i], "--wram-log") == 0 && next) wramLog = argv[++i];
    else if(strcmp(argv[i], "--ppu-shots") == 0) ppuShots = true;
    else if(strcmp(argv[i], "--dump-pics") == 0 && next) dumpPics = argv[++i];
    else if(strcmp(argv[i], "--v3-cache") == 0 && next) v3Cache = argv[++i];
    else if(strcmp(argv[i], "--art3d") == 0 && next) artDir = argv[++i];
    else if(strcmp(argv[i], "--export-art") == 0 && next) exportArt = argv[++i];
    else if(strcmp(argv[i], "--showcase") == 0 && next) showcaseDir = argv[++i];
    else if(strcmp(argv[i], "--perf-log") == 0 && next) perfLog = argv[++i];
    else if(strcmp(argv[i], "--title-style") == 0 && next) titleStyle = strcmp(argv[++i], "classic") == 0 ? 1 : strcmp(argv[i], "both") == 0 ? 2 : 0;
    else if(strcmp(argv[i], "--shots3d") == 0 && next && sscanf(argv[++i], "%dx%d", &shots3dW, &shots3dH) == 2) {}
    else {
      fprintf(stderr, "usage: %s --rom <file> [--frames N] [--input script] [--screenshot out.ppm] [--shots-every N --shots-dir DIR [--shots-from F]] [--expect-wram HEX] [--expect-frame HEX] [--coverage FILE] [--dump-wram FILE] [--watch-word HEX] [--trace-frame N [--trace-count K] --trace-out FILE] [--port off|verify|native] [--native-from F] [--frame-hashes FILE] [--race-frames N] [--ghosts FILE] [--interp-steps K] [--track3d-obj FILE] [--scene-log FILE] [--ppu-shots] [--title-style modern|classic|both]\n", argv[0]);
      return 2;
    }
  }
  if(romPath == NULL) {
    fprintf(stderr, "--rom is required\n");
    return 2;
  }

  TgRom rom;
  TgRomStatus status = tg_rom_load(romPath, &rom);
  if(status != TG_ROM_OK) {
    fprintf(stderr, "%s: %s\n", romPath, tg_rom_status_message(status));
    return 1;
  }
  TgInputScript script = {0};
  if(inputPath) {
    char error[256];
    if(!tg_input_script_load(inputPath, &script, error, sizeof(error))) {
      fprintf(stderr, "%s\n", error);
      return 1;
    }
  }

  TgGame* game = tg_game_create(&rom);

  g_game = game;
  if(ghostsPath != NULL) tg_game_load_ghosts(game, ghostsPath);
  if(interpSteps > 0 || shots3dW > 0 || selectLinear) tg_game_set_interp(game, true);
  TgView3D* view3d = shots3dW > 0 || selectLinear ? tg_view3d_create() : NULL;
  bool view3dInRace = false;
  if(view3d && v3Cache) tg_view3d_set_cache_dir(view3d, v3Cache);
  if(view3d && artDir) tg_view3d_set_art_dir(view3d, artDir);
  if(getenv("TG_TITLE_STYLE")) titleStyle = strcmp(getenv("TG_TITLE_STYLE"), "classic") == 0 ? 1 : strcmp(getenv("TG_TITLE_STYLE"), "both") == 0 ? 2 : 0;
  if(view3d) tg_view3d_set_title_style(view3d, titleStyle == 1 ? TG_V3_TITLE_CLASSIC : TG_V3_TITLE_MODERN);
  if(game == NULL) {
    fprintf(stderr, "failed to start game\n");
    return 1;
  }
  TgCoverage* coverage = NULL;
  if(coveragePath) {
    coverage = malloc(sizeof(TgCoverage));
    tg_coverage_load(coverage, coveragePath);
    tg_game_set_coverage(game, coverage);
  }
  tg_game_set_port_mode(game, portMode);
  if(musicCourses) {
    for(int c = 0; c < 32; c++) {
      char name[40];
      const char *country, *city;
      romCourseName(&rom, c, name, sizeof(name));
      tg_music_course_city(c, &country, &city);
      printf("course %2d  rom %-14s music %s/%s  tune %s\n", c, name, country, city, tg_music_song_name(((c & 3) ^ 3) + 2));
    }
  }
  TgMusic* music = NULL;
  FILE* musicLogFile = NULL;
  FILE* musicWavFile = NULL;
  uint32_t musicWavFrames = 0;
  double musicSampleAcc = 0;
  if(musicSet >= 0 || musicLog || musicWav) {
    music = tg_music_create(musicDir, 48000);
    tg_music_set_set(music, musicSet >= 0 ? musicSet : TG_MUSIC_DEFAULT);
    musicLogFile = musicLog == NULL ? NULL : strcmp(musicLog, "-") == 0 ? stdout : fopen(musicLog, "w");
    tg_music_set_log(music, musicLogFile);
    tg_music_set_dry(music, musicWav == NULL);
    if(musicWav) {
      musicWavFile = fopen(musicWav, "wb");
      if(musicWavFile) wavHeader(musicWavFile, 0);
    }
    tg_music_attach(music, game);
  }
  TgSelectInput selectInput[3];
  for(int p = 0; p < 3; p++) {
    selectInput[p].prev = 0;
    tg_select_input_reset(&selectInput[p]);
  }
  int lastCursor = -1;
  TgCountryInput countryInput;
  tg_country_input_reset(&countryInput);
  bool countryBusy = false;
  FILE* recorded = recordInput ? fopen(recordInput, "w") : NULL;
  uint16_t recordedState[3] = {0, 0, 0};
  if(recorded) fprintf(recorded, "# what the game received (tg_headless --record-input)\n");
  int raceFrames = tg_port_race_frames;
  uint8_t* pixels = calloc(TG_FB_WIDTH * TG_FB_HEIGHT, 4);
  FILE* hashes = frameHashes ? fopen(frameHashes, "w") : NULL;
  FILE* scenes = sceneLog ? fopen(sceneLog, "w") : NULL;
  FILE* wramLogFile = wramLog ? fopen(wramLog, "w") : NULL;
  if(scenes) fprintf(scenes, "frame,race_time,slot,id,view,lap,track_pos,side,x,y,z,speed\n");
  uint32_t lastSceneKey = 0;
  bool trackWritten = false;
  // --perf-log: the modern look built every frame of a race, as the game does (no raster).
  FILE* perf = perfLog && view3d ? fopen(perfLog, "w") : NULL;
  uint32_t* perfSeen = perf ? calloc((size_t) tg_view3d_pic_count(view3d), 4) : NULL;
  static TgV3List perfList;
  if(perf) fprintf(perf, "frame,in_race,capture_ms,build_ms,triangles,batches,vertex_bytes,upload_bytes\n");
  for(long frame = 0; frame < frames; frame++) {
    tg_input_script_advance(&script, (uint32_t) frame);
    if(frame == nativeFrom) tg_game_set_port_mode(game, TG_PORT_NATIVE);
    if(script.resetPending) {
      tg_game_reset(game, false);
      script.resetPending = false;
    }
    if(script.unlockPending) {
      uint8_t open[3] = {script.unlock & 15, (script.unlock >> 4) & 15, (script.unlock >> 8) & 15};
      tg_game_unlock(game, open);
      script.unlockPending = false;
    }
    tg_port_race_frames = script.smooth ? 1 : raceFrames;
    bool fullview = script.fullview || view3d != NULL;  // the 3D view takes the 1-player view's sprites
    if(fullview != tg_port_full_view) tg_game_set_full_view(game, fullview);
    tg_game_set_tall_road(game, script.tallRoad);
    tg_game_set_hud_layout(game, script.mapTop, script.speedBottom);
    if(script.wide != tg_port_wide_margin) tg_game_set_wide(game, script.wide);
    if(script.sideBySide != tg_port_side_by_side) tg_game_set_side_by_side(game, script.sideBySide);
    if(script.ghost != tg_port_ghost) tg_game_set_ghost(game, script.ghost);
    uint16_t gets[3] = {0, script.state[1], script.state[2]};
    if(selectLinear) {
      bool up = view3d && tg_view3d_select_up(view3d);
      int cursor = tg_game_wram(game)[0x5A] & 3;
      if(up && cursor != lastCursor) printf("frame %ld select cursor %d\n", frame, cursor);
      lastCursor = up ? cursor : -1;
      for(int p = 1; p <= 2; p++) {
        gets[p] = tg_select_input(&selectInput[p], script.state[p], cursor, up);
        if(up && (gets[p] || script.state[p])) {
          char a[96], b[96];
          tg_format_buttons(script.state[p], a, sizeof(a));
          tg_format_buttons(gets[p], b, sizeof(b));
          printf("frame %ld select p%d holds %s, game gets %s (cursor %d)\n", frame, p, a, b, cursor);
        }
      }
    }
    if(countryFree) {
      uint16_t before = gets[1];
      bool modern = view3d && tg_view3d_menu_up(view3d);
      gets[1] = tg_country_input(&countryInput, gets[1], modern, tg_game_wram(game), true);
      bool busy = tg_country_input_busy(&countryInput);
      if(view3d) tg_view3d_set_country_select(view3d, true, busy);
      if(busy != countryBusy) printf("frame %ld country input %s (country %d)\n", frame, busy ? "typing" : "done", tg_game_wram(game)[0x5C]);
      countryBusy = busy;
      if(busy && gets[1] != before) {
        char b[96];
        tg_format_buttons(gets[1], b, sizeof(b));
        printf("frame %ld country input gives %s (cursor %04X, typed %d)\n", frame, b,
               tg_game_wram(game)[0x48] | tg_game_wram(game)[0x49] << 8, tg_game_wram(game)[0x5E]);
      }
    }
    if(recorded) {
      for(int p = 1; p <= 2; p++)
        if(gets[p] != recordedState[p]) {
          char b[96];
          tg_format_buttons(gets[p], b, sizeof(b));
          fprintf(recorded, "%ld %d %s\n", frame, p, b);
          recordedState[p] = gets[p];
        }
    }
    tg_game_set_input(game, 1, gets[1]);
    tg_game_set_input(game, 2, gets[2]);
    if(frame == traceFrame && traceOut) {
      trace = fopen(traceOut, "w");
      tg_game_set_trace(game, trace);
    }
    if(trace) fprintf(trace, "# frame %ld\n", frame);
    if(frame == setCourseFrame) {
      uint8_t* w = (uint8_t*) tg_game_wram(game);
      w[0x1F06] = (uint8_t) ((w[0x1F06] & ~0x1F) | (setCourse & 0x1F));
    }
    tg_game_run_frame(game);
    if(music) {
      if(frame == musicSwitchFrame) tg_music_set_set(music, musicSwitchSet);
      tg_music_end_frame(music, (uint32_t) frame + 1);
      if(musicWavFile) {
        // As the game's frontend does: a frame's samples at 48 kHz, the music mixed in.
        static int16_t buf[2048 * 2];
        musicSampleAcc += 48000.0 * 1364.0 * 262.0 / 21477272.0;
        int n = (int) musicSampleAcc;
        musicSampleAcc -= n;
        tg_game_get_samples(game, buf, n);
        tg_music_mix(music, buf, n);
        fwrite(buf, 4, (size_t) n, musicWavFile);
        musicWavFrames += (uint32_t) n;
      }
    }
    if(view3d) {
      TgSpriteState ss;
      tg_game_sprite_state(game, &ss);
      tg_game_get_pixels(game, pixels);
      double c0 = tg_now_ms();
      view3dInRace = tg_view3d_capture(view3d, tg_game_wram(game), &ss, (const uint32_t*) pixels);
      double c1 = tg_now_ms();
      if(perf) {
        double capMs = c1 - c0, buildMs = 0;
        int tris = 0, batches = 0;
        size_t vbytes = 0, ubytes = 0;
        if(view3dInRace) {
          for(int k = 0; k < tg_view3d_views(view3d); k++) {
            c0 = tg_now_ms();
            tg_view3d_draw_view(view3d, k, shots3dW, shots3dH / tg_view3d_views(view3d), TG_V3_LOOK_MODERN, TG_V3_CAM_GAME, &perfList);
            c1 = tg_now_ms();
            buildMs += c1 - c0;
            tris += perfList.nv / 3, batches += perfList.nb;
            vbytes += (size_t) perfList.nv * sizeof(TgV3Vertex);
            ubytes += tg_v3_list_upload_bytes(view3d, &perfList, perfSeen);
          }
        }
        fprintf(perf, "%ld,%d,%.3f,%.3f,%d,%d,%zu,%zu\n", frame + 1, view3dInRace, capMs, buildMs, tris, batches, vbytes, ubytes);
      }
    }
    if(wramLogFile) {
      const uint8_t* w = tg_game_wram(game);
      fprintf(wramLogFile, "%ld ", frame + 1);
      for(int j = 0x0000; j < 0x0380; j++) fprintf(wramLogFile, "%02X", w[j]);
      fputc(' ', wramLogFile);
      for(int j = 0x1E00; j < 0x1F00; j++) fprintf(wramLogFile, "%02X", w[j]);
      fputc('\n', wramLogFile);
    }
    if(watchWord >= 0 && (getenv("TG_WATCH_ALL") || frame % 60 == 0)) {
      const uint8_t* w = tg_game_wram(game);
      printf("frame %ld word %04lX = %04X\n", frame, watchWord, w[watchWord & 0x1FFFF] | w[(watchWord + 1) & 0x1FFFF] << 8);
    }
    if((trackObj && !trackWritten) || scenes) {
      if(tg_scene_update(&g_scene, &g_track, tg_game_wram(game))) {
        if(trackObj && !trackWritten) {
          trackWritten = true;
          if(tg_track3d_write_obj(&g_track, trackObj))
            printf("track3d %d segments, %.0f m a lap -> %s\n", g_track.tables.segments, tg_track3d_length(&g_track), trackObj);
          else
            fprintf(stderr, "cannot write %s\n", trackObj);
        }
        // One row per car each time anything moves (the race updates every few frames).
        uint32_t key = 2166136261u;
        for(int k = 0; k < g_scene.car_count; k++) key = (key ^ (uint32_t) (g_scene.cars[k].track_pos * 256)) * 16777619u;
        if(scenes && key != lastSceneKey) {
          lastSceneKey = key;
          for(int k = 0; k < g_scene.car_count; k++) {
            const TgSceneCar* c = &g_scene.cars[k];
            fprintf(scenes, "%ld,%u,%d,%d,%d,%d,%.2f,%.0f,%.3f,%.3f,%.3f,%u\n", frame + 1, g_scene.race_time, c->slot,
                    c->id, c->view, c->lap, c->track_pos, c->side, c->world.pos.x, c->world.pos.y, c->world.pos.z, c->speed);
          }
        }
      }
    }
    if(trace && frame == traceFrame + traceCount - 1) {
      tg_game_set_trace(game, NULL);
      fclose(trace);
      trace = NULL;
    }
    if(hashes) {
      tg_game_get_pixels(game, pixels);
      uint64_t h = 0xcbf29ce484222325ull;  // FNV-1a
      for(size_t j = 0; j < (size_t) TG_FB_WIDTH * TG_FB_HEIGHT * 4; j++) h = (h ^ pixels[j]) * 0x100000001b3ull;
      fprintf(hashes, "%016llx\n", (unsigned long long) h);
    }
    if(shotsEvery > 0 && shotsDir && frame + 1 >= shotsFrom && (frame + 1) % shotsEvery == 0) {
      char path[1024];
      snprintf(path, sizeof(path), "%s/frame%06ld.ppm", shotsDir, frame + 1);
      tg_game_get_pixels(game, pixels);
      writePpm(path, pixels);
      if(view3d && (tg_view3d_select_up(view3d) || tg_view3d_title_up(view3d) || tg_view3d_menu_up(view3d))) {  // the redrawn menus
        static TgV3List sl;
        int sw = shots3dW, sh = shots3dH;
        bool title = !tg_view3d_select_up(view3d) && tg_view3d_title_up(view3d);
        for(int pass = 0; pass < (title && titleStyle == 2 ? 2 : 1); pass++) {
          uint32_t* sb = calloc((size_t) sw * sh, 4);
          if(title && titleStyle == 2) tg_view3d_set_title_style(view3d, pass ? TG_V3_TITLE_CLASSIC : TG_V3_TITLE_MODERN);
          if(tg_view3d_select_up(view3d)) tg_view3d_draw_select(view3d, sw, sh, frame / 60.0f, &sl);
          else if(tg_view3d_title_up(view3d)) tg_view3d_draw_title(view3d, sw, sh, frame / 60.0f, &sl);
          else tg_view3d_draw_menu(view3d, sw, sh, frame / 60.0f, &sl);
          if(getenv("TG_MENU_DUMP") && tg_view3d_menu_up(view3d)) {
            fprintf(stderr, "-- frame %ld\n", frame + 1);
            tg_view3d_menu_dump(view3d);
          }
          tg_v3_raster(view3d, &sl, sb, sw, sh);
          snprintf(path, sizeof(path), "%s/frame%06ld_sel%s.ppm", shotsDir, frame + 1, pass ? "c" : "");
          FILE* f = fopen(path, "wb");
          if(f) {
            fprintf(f, "P6\n%d %d\n255\n", sw, sh);
            for(int i = 0; i < sw * sh; i++) {
              uint8_t rgb[3] = {(uint8_t) (sb[i] >> 16), (uint8_t) (sb[i] >> 8), (uint8_t) sb[i]};
              fwrite(rgb, 1, 3, f);
            }
            fclose(f);
          }
          free(sb);
        }
      }
      if(view3d && view3dInRace) {
        {
          // The modern look first: the view's fades and eases move by the game frames since the
          // last picture drawn, and the game draws one look a frame. Drawn after the other two,
          // the modern look's own fades (the landmark, the skyline's learned columns) never
          // moved here, so the shots showed neither (QA-007, QA-009).
          snprintf(path, sizeof(path), "%s/frame%06ld_3dx.ppm", shotsDir, frame + 1);
          write3d(path, view3d, TG_V3_LOOK_MODERN, shots3dW, shots3dH);
          if(!getenv("TG_SHOTS_MODERN_ONLY")) {  // (set: dense renders of the modern look, the other two skipped)
            snprintf(path, sizeof(path), "%s/frame%06ld_3d.ppm", shotsDir, frame + 1);
            write3d(path, view3d, TG_V3_LOOK_SPRITES, shots3dW, shots3dH);
            snprintf(path, sizeof(path), "%s/frame%06ld_3dm.ppm", shotsDir, frame + 1);
            write3d(path, view3d, TG_V3_LOOK_MODELS, shots3dW, shots3dH);
          }
        }
      }
      if(ppuShots) {
        snprintf(path, sizeof(path), "%s/frame%06ld.ppu", shotsDir, frame + 1);
        tg_game_dump_ppu(game, path);
      }
      for(int k = 1; k < interpSteps; k++) {
        if(!tg_game_interp(game, (float) k / interpSteps)) break;
        snprintf(path, sizeof(path), "%s/frame%06ld_%d.ppm", shotsDir, frame + 1, k);
        tg_game_get_pixels(game, pixels);
        writePpm(path, pixels);
      }
    }
  }
  if(music) {
    tg_music_destroy(music);  // leaves the game as it was (no observer, every voice heard)
    if(musicWavFile) {
      wavHeader(musicWavFile, musicWavFrames);
      fclose(musicWavFile);
    }
    if(musicLogFile && musicLogFile != stdout) fclose(musicLogFile);
  }
  if(hashes) fclose(hashes);
  if(recorded) fclose(recorded);
  if(scenes) fclose(scenes);
  if(wramLogFile) fclose(wramLogFile);
  if(perf) fclose(perf);
  free(perfSeen);
  if(view3d && dumpPics) {
    for(int i = 0; i < tg_view3d_pic_count(view3d); i++) {
      const TgV3Pic* p = tg_view3d_pic(view3d, i);
      if(p == NULL || p->w == 0) continue;
      char path[1024];
      snprintf(path, sizeof(path), "%s/pic%03d.ppm", dumpPics, i);
      FILE* f = fopen(path, "wb");
      if(f == NULL) continue;
      fprintf(f, "P6\n%d %d\n255\n", p->w, p->h);
      for(int k = 0; k < p->w * p->h; k++) {
        uint32_t c = p->px[k] >> 24 ? p->px[k] : 0xFF00FF;
        uint8_t rgb[3] = {(uint8_t) (c >> 16), (uint8_t) (c >> 8), (uint8_t) c};
        fwrite(rgb, 1, 3, f);
      }
      fclose(f);
    }
  }
  if(trackObj && !trackWritten) fprintf(stderr, "no race reached: %s not written\n", trackObj);
  tg_game_get_pixels(game, pixels);
  if(screenshot) writePpm(screenshot, pixels);
  if(dumpPpu) tg_game_dump_ppu(game, dumpPpu);
  if(dumpWram) {
    FILE* f = fopen(dumpWram, "wb");
    if(f == NULL || fwrite(tg_game_wram(game), 1, 0x20000, f) != 0x20000) fprintf(stderr, "cannot write %s\n", dumpWram);
    if(f) fclose(f);
  }

  printf("frames %ld\n", frames);
  bool ok = checkHash("wram", tg_game_wram(game), 0x20000, expectWram);
  ok &= checkHash("frame", pixels, (size_t) TG_FB_WIDTH * TG_FB_HEIGHT * 4, expectFrame);
  if(portMode != TG_PORT_OFF && tg_game_port_report(game, stdout) > 0) ok = false;

  if(coverage) {
    if(!tg_coverage_save(coverage, coveragePath)) {
      fprintf(stderr, "cannot write %s\n", coveragePath);
      ok = false;
    }
    free(coverage);
  }
  free(pixels);
  if(ghostsPath != NULL && tg_game_ghosts_changed(game)) tg_game_save_ghosts(game, ghostsPath);
  if(view3d && showcaseDir) showcase(view3d, showcaseDir);
  if(view3d && exportArt) printf("exported %d pictures to %s\n", tg_view3d_export(view3d, exportArt), exportArt);
  tg_view3d_destroy(view3d);  // keeps what it learned (--v3-cache)
  tg_game_destroy(game);
  tg_input_script_free(&script);
  tg_rom_free(&rom);
  return ok ? 0 : 1;
}
