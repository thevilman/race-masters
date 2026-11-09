// Core tests. Without arguments they need no ROM; with --rom they boot the game.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "coverage.h"
#include "game.h"
#include "input_script.h"
#include "rom.h"
#include "sha256.h"
#include "track3d.h"
#include "transition.h"

static int failures = 0;

#define CHECK(cond) do { \
  if(!(cond)) { fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); failures++; } \
} while(0)

static void hex(const uint8_t* d, char* out) {
  for(int i = 0; i < 32; i++) sprintf(out + i * 2, "%02x", d[i]);
}

static void testSha256(void) {
  uint8_t d[32];
  char h[65];
  tg_sha256((const uint8_t*) "", 0, d);
  hex(d, h);
  CHECK(strcmp(h, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") == 0);
  tg_sha256((const uint8_t*) "abc", 3, d);
  hex(d, h);
  CHECK(strcmp(h, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0);
  const char* s = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
  tg_sha256((const uint8_t*) s, strlen(s), d);
  hex(d, h);
  CHECK(strcmp(h, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1") == 0);
}

static void testButtons(void) {
  uint16_t b;
  CHECK(tg_parse_buttons("-", &b) && b == 0);
  CHECK(tg_parse_buttons("start", &b) && b == TG_BTN_START);
  CHECK(tg_parse_buttons("B+RIGHT+R", &b) && b == (TG_BTN_B | TG_BTN_RIGHT | TG_BTN_R));
  CHECK(!tg_parse_buttons("TURBO", &b));
  char text[96];
  tg_format_buttons(TG_BTN_B | TG_BTN_RIGHT, text, sizeof(text));
  CHECK(strcmp(text, "B+RIGHT") == 0);
  tg_format_buttons(0, text, sizeof(text));
  CHECK(strcmp(text, "-") == 0);
}

static void testLengths(void) {
  CHECK(tg_w65816_length(0xa9, true, true) == 2);   // LDA #imm8
  CHECK(tg_w65816_length(0xa9, false, true) == 3);  // LDA #imm16
  CHECK(tg_w65816_length(0xa2, true, false) == 3);  // LDX #imm16
  CHECK(tg_w65816_length(0x22, true, true) == 4);   // JSL long
  CHECK(tg_w65816_length(0x0b, true, true) == 1);   // PHD
  CHECK(tg_w65816_length(0xc2, false, false) == 2); // REP
  CHECK(tg_lorom_offset(0x008000) == 0);
  CHECK(tg_lorom_offset(0x80ffff) == 0x7fff);
  CHECK(tg_lorom_offset(0x0f8000) == 0x78000);
  CHECK(tg_lorom_offset(0x7e8000) == -1);
  CHECK(tg_lorom_offset(0x001000) == -1);
}

static void testBoot(const char* romPath) {
  TgRom rom;
  TgRomStatus status = tg_rom_load(romPath, &rom);
  CHECK(status == TG_ROM_OK);
  if(status != TG_ROM_OK) return;
  TgGame* game = tg_game_create(&rom);
  CHECK(game != NULL);
  if(game == NULL) return;
  for(int i = 0; i < 300; i++) tg_game_run_frame(game);
  CHECK(tg_game_frame_count(game) >= 300);
  uint8_t* pixels = calloc(TG_FB_WIDTH * TG_FB_HEIGHT, 4);
  tg_game_get_pixels(game, pixels);
  int lit = 0;
  for(int i = 0; i < TG_FB_WIDTH * TG_FB_HEIGHT; i++) lit += (pixels[i * 4] | pixels[i * 4 + 1] | pixels[i * 4 + 2]) != 0;
  CHECK(lit > 1000);  // something is on screen after 5 seconds
  free(pixels);
  tg_game_destroy(game);
  tg_rom_free(&rom);
}

static float dist3(TgVec3 a, TgVec3 b) {
  float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
  return sqrtf(dx * dx + dy * dy + dz * dz);
}

// 3D mod: a course laid out as a circuit must close, keep its segment length, and put a
// car at its side position.
static void testTrack3d(void) {
  static TgTrackTables tb;
  static TgTrack3D t;
  memset(&tb, 0, sizeof(tb));
  // A stadium: straight, half circle, straight, half circle, with a hill on the first
  // straight and a drop that is never climbed back (as the original has).
  tb.segments = 400;
  for(int i = 0; i < 400; i++) {
    tb.curve[i] = (i >= 100 && i < 200) || (i >= 300) ? 16 : 0;
    tb.slope[i] = (i >= 10 && i < 40) ? 50 : (i >= 40 && i < 70) ? -50 : (i >= 220 && i < 250) ? -750 : 0;
  }
  TgTrackScale sc = tg_track3d_default_scale();
  CHECK(tg_track3d_build(&t, &tb, sc));
  CHECK(dist3(t.centre[0], t.centre[400]) < 1e-3f);
  // A straight segment keeps its length (the closing gap of a perfect stadium is tiny).
  CHECK(fabsf(dist3(t.centre[50], t.centre[51]) - sc.seg_len) < 0.05f);
  // The hill rises, the far straight is lower (the drop), the loop comes back level.
  CHECK(t.centre[40].y > t.centre[10].y + 1);
  CHECK(t.centre[260].y < t.centre[200].y - 1);
  CHECK(fabsf(t.centre[0].y - t.centre[400].y) < 1e-3f);
  // A half circle turns the road round: the far straight runs the other way.
  TgTrackFrame a = tg_track3d_frame(&t, 50 * TG_TRACK3D_SEG_UNITS, 0);
  TgTrackFrame b = tg_track3d_frame(&t, 250 * TG_TRACK3D_SEG_UNITS, 0);
  CHECK(a.forward.x * b.forward.x + a.forward.z * b.forward.z < -0.95f);
  // Side position: the road edge is half_width to the right, wrapping past the lap.
  TgTrackFrame e = tg_track3d_frame(&t, 50 * TG_TRACK3D_SEG_UNITS, TG_TRACK3D_SIDE_EDGE);
  CHECK(fabsf(dist3(a.pos, e.pos) - sc.half_width) < 1e-3f);
  TgTrackFrame w = tg_track3d_frame(&t, (400 + 50) * TG_TRACK3D_SEG_UNITS, 0);
  CHECK(dist3(a.pos, w.pos) < 1e-3f);
  // Not a race: nothing to read.
  static uint8_t wram[0x20000];
  CHECK(!tg_track3d_read_tables(wram, &tb));
}

// How many of a list's triangles hold the point (x, y).
static int trianglesAt(const TgV3List* l, float x, float y) {
  int n = 0;
  for(int i = 0; i + 2 < l->nv; i += 3) {
    const TgV3Vertex* v = l->v + i;
    float d0 = (v[1].x - v[0].x) * (y - v[0].y) - (v[1].y - v[0].y) * (x - v[0].x);
    float d1 = (v[2].x - v[1].x) * (y - v[1].y) - (v[2].y - v[1].y) * (x - v[1].x);
    float d2 = (v[0].x - v[2].x) * (y - v[2].y) - (v[0].y - v[2].y) * (x - v[2].x);
    if((d0 >= 0 && d1 >= 0 && d2 >= 0) || (d0 <= 0 && d1 <= 0 && d2 <= 0)) n++;
  }
  return n;
}

// The F7 transition (transition.h): its timing, a press turning it back, and the two
// pictures' shares covering the screen.
static void testTransition(void) {
  TgTransition t = {0};
  tg_transition_start(&t, false, true);
  CHECK(t.active && t.x == 0 && t.alpha == 0);
  float s = 0;
  for(int i = 0; i < 1000 && t.active; i++) tg_transition_step(&t, 1 / 120.0f), s += 1 / 120.0f;
  CHECK(!t.active && t.x == 1 && fabsf(s - 0.5f) < 0.02f);  // 0.4 s turning, 0.1 s fading
  t = (TgTransition) {0};
  t.x = 0;
  CHECK(tg_transition_year(&t, NULL) == 1992);
  t.x = 1;
  CHECK(tg_transition_year(&t, NULL) == 2026);
  int last = 0;
  for(int i = 0; i <= 100; i++) {
    t.x = i / 100.0f;
    int y = tg_transition_year(&t, NULL);
    CHECK(y >= last);
    last = y;
  }
  // Half way round a second press turns the hand back from where it is.
  t = (TgTransition) {0};
  tg_transition_start(&t, false, true);
  for(int i = 0; i < 24; i++) tg_transition_step(&t, 1 / 120.0f);
  float x = t.x, a = tg_transition_angle(&t);
  CHECK(fabsf(x - 0.5f) < 0.01f && t.alpha == 1);
  tg_transition_start(&t, true, false);
  CHECK(t.active && t.x == x && !t.to3d);
  tg_transition_step(&t, 1 / 120.0f);
  CHECK(t.x < x && fabsf(tg_transition_angle(&t) - a) < 0.25f);
  s = 1 / 120.0f;
  for(int i = 0; i < 1000 && t.active; i++) tg_transition_step(&t, 1 / 120.0f), s += 1 / 120.0f;
  CHECK(!t.active && t.x == 0 && fabsf(s - 0.3f) < 0.02f);
  // A press and another before the picture moves: back where it was at once.
  t = (TgTransition) {0};
  tg_transition_start(&t, true, false);
  tg_transition_start(&t, false, true);
  tg_transition_step(&t, 1 / 120.0f);
  CHECK(!t.active && t.x == 1);
  // Every point of the screen is in exactly one of the two shares; the overlay draws.
  const float xs[] = {0, 0.05f, 0.3f, 0.5f, 0.77f, 0.99f, 1};
  for(size_t k = 0; k < sizeof(xs) / sizeof(xs[0]); k++) {
    TgV3List a3 = {0}, a2 = {0}, ov = {0};
    t = (TgTransition) {.active = true, .to3d = true, .x = xs[k], .alpha = 1};
    tg_transition_region(&t, 1366, 768, true, &a3);
    tg_transition_region(&t, 1366, 768, false, &a2);
    int bad = 0;
    for(int j = 0; j < 19; j++)
      for(int i = 0; i < 33; i++) {
        float px = (i + 0.37f) * 1366 / 33, py = (j + 0.61f) * 768 / 19;
        if(trianglesAt(&a3, px, py) + trianglesAt(&a2, px, py) != 1) bad++;
      }
    CHECK(bad == 0);
    tg_transition_overlay(&t, 1366, 768, &ov);
    CHECK(ov.nv > 300 && ov.nb == 1 && ov.b[0].pic == -1 && ov.v[0].w < 0);
    tg_v3_list_free(&a3);
    tg_v3_list_free(&a2);
    tg_v3_list_free(&ov);
  }
}

int main(int argc, char** argv) {
  testSha256();
  testButtons();
  testLengths();
  testTrack3d();
  testTransition();
  if(argc == 3 && strcmp(argv[1], "--rom") == 0) testBoot(argv[2]);
  if(failures) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  printf("all checks passed\n");
  return 0;
}
