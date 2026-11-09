// The course as a real 3D circuit: see track3d.h.

#include "track3d.h"

#include "brand.h"  // TG_APP_NAME

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PI 3.14159265358979f

static uint16_t r16(const uint8_t* m, uint32_t a) { return (uint16_t) (m[a] | m[a + 1] << 8); }

static TgVec3 v3(float x, float y, float z) { return (TgVec3) {x, y, z}; }
static TgVec3 add(TgVec3 a, TgVec3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static TgVec3 scale(TgVec3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static TgVec3 lerp(TgVec3 a, TgVec3 b, float t) { return add(a, scale(add(b, scale(a, -1)), t)); }
static TgVec3 normalise(TgVec3 a) {
  float l = sqrtf(a.x * a.x + a.y * a.y + a.z * a.z);
  return l > 0 ? scale(a, 1 / l) : a;
}
static TgVec3 cross(TgVec3 a, TgVec3 b) {
  return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}

TgTrackScale tg_track3d_default_scale(void) {
  // Tuned by eye; the original's projection has no true scale. A segment of 2.5 m makes
  // Las Vegas about 1.8 km a lap; the 50-unit hills come out about 7 m high; the road is
  // about four cars wide, as in the original.
  return (TgTrackScale) {.seg_len = 2.5f, .half_width = 4.0f, .rise = 0.0040f, .max_slope = 120};
}

bool tg_track3d_read_tables(const uint8_t* m, TgTrackTables* out) {
  // In a race the NMI trampoline at $0000 is JML $00:EC1A (docs/notes/engine.md); the
  // menus use this low RAM for other things.
  if(m[0] != 0x5C || m[1] != 0x1A || m[2] != 0xEC || m[3] != 0x00) return false;
  // Record 0 of the car list ($0200) starts with the lap length in track units.
  uint16_t len = r16(m, 0x0200);
  if(len < 0x400 || len % TG_TRACK3D_SEG_UNITS != 0) return false;
  int n = len / TG_TRACK3D_SEG_UNITS;
  if(n > TG_TRACK3D_MAX_SEGMENTS) return false;
  out->segments = n;
  for(int i = 0; i < n; i++) {
    out->curve[i] = (int16_t) r16(m, 0x4C00 + 2 * (uint32_t) i);
    out->slope[i] = (int16_t) r16(m, 0x4000 + 2 * (uint32_t) i);
    out->stripe[i] = r16(m, 0x5800 + 2 * (uint32_t) i);
    out->object[i] = r16(m, 0x6400 + 2 * (uint32_t) i);
  }
  return true;
}

static uint32_t tables_key(const TgTrackTables* tb) {
  uint32_t h = 2166136261u;  // FNV-1a over the tables that shape the road
  for(int i = 0; i < tb->segments; i++) {
    h = (h ^ (uint16_t) tb->curve[i]) * 16777619u;
    h = (h ^ (uint16_t) tb->slope[i]) * 16777619u;
  }
  return (h ^ (uint32_t) tb->segments) * 16777619u;
}

bool tg_track3d_build(TgTrack3D* t, const TgTrackTables* tb, TgTrackScale sc) {
  int n = tb->segments;
  if(n <= 0 || n > TG_TRACK3D_MAX_SEGMENTS) return false;
  if(&t->tables != tb) t->tables = *tb;
  t->scale = sc;
  t->key = tables_key(tb);

  // Turn rate per curve unit: the lap turns one full circle. A course that turns as much
  // left as right (a figure of eight) can't be scaled that way; it gets a fixed rate.
  long total = 0;
  for(int i = 0; i < n; i++) total += t->tables.curve[i];
  float k = labs(total) >= 64 ? 2 * PI / (float) total : 2 * PI / 5824.0f;

  // Gradients, capped, and balanced so the lap ends at the height it starts: the climbs (or
  // the descents, whichever is short) are scaled up. Flat road stays flat; the course's
  // own hills just get a little taller or deeper.
  static float grad[TG_TRACK3D_MAX_SEGMENTS];
  float up = 0, down = 0;
  for(int i = 0; i < n; i++) {
    int s = t->tables.slope[i];
    if(s > sc.max_slope) s = sc.max_slope;
    if(s < -sc.max_slope) s = -sc.max_slope;
    grad[i] = (float) s;
    if(s > 0) up += s;
    else down -= s;
  }
  if(up > 0 && down > 0) {
    float kUp = up < down ? down / up : 1, kDown = down < up ? up / down : 1;
    for(int i = 0; i < n; i++) grad[i] *= grad[i] > 0 ? kUp : kDown;
  }
  // Plan and height, segment by segment (midpoint rule for the heading).
  float x = 0, north = 0, h = 0, y = 0;
  float px[TG_TRACK3D_MAX_SEGMENTS + 1], pn[TG_TRACK3D_MAX_SEGMENTS + 1], py[TG_TRACK3D_MAX_SEGMENTS + 1];
  for(int i = 0; i < n; i++) {
    px[i] = x;
    pn[i] = north;
    py[i] = y;
    t->heading[i] = h;
    float hm = h + t->tables.curve[i] * k / 2;
    x += cosf(hm) * sc.seg_len;
    north += sinf(hm) * sc.seg_len;
    h += t->tables.curve[i] * k;
    y += grad[i] * sc.rise;
  }
  // Close the loop: spread what is left over the lap.
  for(int i = 0; i < n; i++) {
    float f = (float) i / n;
    px[i] -= x * f;
    pn[i] -= north * f;
    py[i] -= y * f;
  }
  px[n] = px[0];
  pn[n] = pn[0];
  py[n] = py[0];
  // The headings from the road as it now lies. The game's curves do not close a lap, and
  // what is left over is spread over it (above), which turns every segment's chord off the
  // heading the curves gave it - by 12 degrees on Las Vegas, straights included. Everything
  // placed along the frame (the cars, the right vector the road's width hangs on) must point
  // along the road that is drawn: so each segment's chord gives its direction, and a node's
  // heading is the mean of the chords either side (the Catmull-Rom tangent,
  // (p[i+1] - p[i-1]) / 2). Unwrapped against the curves' own heading, so a lap still turns
  // by exactly what it did.
  {
    static float chord[TG_TRACK3D_MAX_SEGMENTS];
    for(int i = 0; i < n; i++) {
      float hm = t->heading[i] + t->tables.curve[i] * k / 2;
      float a = atan2f(pn[i + 1] - pn[i], px[i + 1] - px[i]) - hm;
      a -= 2 * PI * floorf((a + PI) / (2 * PI));
      chord[i] = hm + a;
    }
    for(int i = 0; i < n; i++) {
      float before = i > 0 ? chord[i - 1] : chord[n - 1] - h;
      t->heading[i] = (before + chord[i]) / 2;
    }
  }
  t->heading[n] = t->heading[0] + h;

  t->min = v3(1e30f, 1e30f, 1e30f);
  t->max = v3(-1e30f, -1e30f, -1e30f);
  for(int i = 0; i <= n; i++) {
    TgVec3 p = v3(px[i], py[i], -pn[i]);
    t->centre[i] = p;
    if(p.x < t->min.x) t->min.x = p.x;
    if(p.y < t->min.y) t->min.y = p.y;
    if(p.z < t->min.z) t->min.z = p.z;
    if(p.x > t->max.x) t->max.x = p.x;
    if(p.y > t->max.y) t->max.y = p.y;
    if(p.z > t->max.z) t->max.z = p.z;
  }
  for(int i = 0; i < n; i++) {
    float dx = px[i + 1] - px[i], dn = pn[i + 1] - pn[i];
    float run = sqrtf(dx * dx + dn * dn);
    t->grade[i] = run > 0 ? (py[i + 1] - py[i]) / run : 0;
  }
  t->grade[n] = t->grade[0];
  return true;
}

bool tg_track3d_update(TgTrack3D* t, const uint8_t* wram) {
  static TgTrackTables tb;  // too big for the stack on some platforms
  if(!tg_track3d_read_tables(wram, &tb)) return false;
  if(t->tables.segments == tb.segments && t->key == tables_key(&tb)) return true;
  return tg_track3d_build(t, &tb, tg_track3d_default_scale());
}

TgTrackFrame tg_track3d_frame(const TgTrack3D* t, float pos, float side) {
  int n = t->tables.segments;
  float seg = pos / TG_TRACK3D_SEG_UNITS;
  seg = fmodf(seg, (float) n);
  if(seg < 0) seg += n;
  int i = (int) seg;
  if(i >= n) i = n - 1;
  float f = seg - i;
  TgVec3 c = lerp(t->centre[i], t->centre[i + 1], f);
  float h = t->heading[i] + (t->heading[i + 1] - t->heading[i]) * f;
  float g = t->grade[i];
  // Ahead: along the heading, climbing with the grade. Right: level, across the road.
  TgVec3 fwd = normalise(v3(cosf(h), g, -sinf(h)));
  TgVec3 right = v3(sinf(h), 0, cosf(h));
  TgVec3 up = normalise(cross(right, fwd));
  TgTrackFrame fr;
  fr.forward = fwd;
  fr.right = right;
  fr.up = up;
  fr.pos = add(c, scale(right, side / TG_TRACK3D_SIDE_EDGE * t->scale.half_width));
  return fr;
}

float tg_track3d_length(const TgTrack3D* t) {
  float l = 0;
  for(int i = 0; i < t->tables.segments; i++) {
    TgVec3 d = add(t->centre[i + 1], scale(t->centre[i], -1));
    l += sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
  }
  return l;
}

bool tg_track3d_write_obj(const TgTrack3D* t, const char* path) {
  char mtl[1024];
  snprintf(mtl, sizeof(mtl), "%s.mtl", path);
  FILE* fm = fopen(mtl, "w");
  if(fm == NULL) return false;
  fprintf(fm, "newmtl road\nKd 0.29 0.31 0.35\n");
  fprintf(fm, "newmtl kerb_red\nKd 0.85 0.20 0.25\n");
  fprintf(fm, "newmtl kerb_white\nKd 0.95 0.95 0.92\n");
  fprintf(fm, "newmtl sand\nKd 0.92 0.77 0.55\n");
  fprintf(fm, "newmtl start\nKd 1 1 1\n");
  fclose(fm);
  FILE* f = fopen(path, "w");
  if(f == NULL) return false;
  const char* base = strrchr(mtl, '/');
  fprintf(f, "# The course from the player's ROM as a 3D circuit (" TG_APP_NAME ")\nmtllib %s\n", base ? base + 1 : mtl);
  int n = t->tables.segments;
  // Across the road at each segment start: sand, kerb, road, road, kerb, sand.
  const float across[6] = {-2.2f, -1.12f, -1.0f, 1.0f, 1.12f, 2.2f};
  for(int i = 0; i <= n; i++) {
    for(int j = 0; j < 6; j++) {
      TgTrackFrame fr = tg_track3d_frame(t, (float) (i % n) * TG_TRACK3D_SEG_UNITS, across[j] * TG_TRACK3D_SIDE_EDGE);
      if(i == n) fr = tg_track3d_frame(t, 0, across[j] * TG_TRACK3D_SIDE_EDGE);
      fprintf(f, "v %.3f %.3f %.3f\n", fr.pos.x, fr.pos.y, fr.pos.z);
    }
  }
  const char* strip[5] = {"sand", NULL, "road", NULL, "sand"};
  for(int j = 0; j < 5; j++) {
    for(int i = 0; i < n; i++) {
      const char* m = strip[j];
      if(m == NULL) m = (t->tables.stripe[i] >> 1 & 7) < 4 ? "kerb_red" : "kerb_white";
      if(i == 0 && j == 2) m = "start";
      fprintf(f, "usemtl %s\n", m);
      int a = i * 6 + j + 1, b = (i + 1) * 6 + j + 1;
      fprintf(f, "f %d %d %d %d\n", a, a + 1, b + 1, b);
    }
  }
  fclose(f);
  return true;
}
