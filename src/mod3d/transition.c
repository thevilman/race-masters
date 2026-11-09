// F7's change between the original picture and the 3D view as time passing (transition.h).
#include "transition.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define TAU 6.28318530718f

typedef struct { float r, g, b; } Col;

// The two eras: the original's warm amber (its HUD's yellow, a CRT's glow) and the modern
// look's cyan (its sky); the face is dark glass.
static const Col kAmber = {1.00f, 0.72f, 0.20f};
static const Col kCyan = {0.36f, 0.86f, 1.00f};
static const Col kWhite = {1.00f, 1.00f, 1.00f};
static const Col kGlass = {0.02f, 0.04f, 0.08f};

static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
static float smooth01(float v) { v = clamp01(v); return v * v * (3 - 2 * v); }

static Col mix(Col a, Col b, float t) { return (Col) {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }

// The colour of a moment between the eras (0: 1992, 1: 2026): amber, through white (a straight
// mix of amber and cyan is a muddy green half way), to cyan.
static Col eraColour(float t) {
  const Col mid = {0.97f, 0.96f, 0.92f};
  return t < 0.5f ? mix(kAmber, mid, t * 2) : mix(mid, kCyan, t * 2 - 1);
}

static uint32_t argb(Col c, float a) {
  uint32_t A = (uint32_t) lroundf(clamp01(a) * 255), R = (uint32_t) lroundf(clamp01(c.r) * 255);
  uint32_t G = (uint32_t) lroundf(clamp01(c.g) * 255), B = (uint32_t) lroundf(clamp01(c.b) * 255);
  return A << 24 | R << 16 | G << 8 | B;
}

void tg_transition_start(TgTransition* t, bool from3d, bool to3d) {
  if(!t->active) {
    t->x = from3d ? 1.0f : 0.0f;
    t->alpha = 0;
    t->trail = 0;
    t->active = true;
  }
  t->to3d = to3d;
}

void tg_transition_step(TgTransition* t, float dt) {
  if(!t->active) return;
  if(!(dt > 0)) dt = 0;
  if(dt > 0.1f) dt = 0.1f;  // (a stall counts as a tenth of a second)
  float target = t->to3d ? 1.0f : 0.0f, omega = 0;
  if(t->x != target) {
    float step = dt / TG_TRANSITION_SWEEP;
    t->x = t->to3d ? fminf(1.0f, t->x + step) : fmaxf(0.0f, t->x - step);
    // The hand turns by smoothstep(x): its speed, radians a second (+ clockwise).
    omega = TAU * 6 * t->x * (1 - t->x) / TG_TRANSITION_SWEEP * (t->to3d ? 1 : -1);
    t->alpha = fminf(1.0f, t->alpha + dt / TG_TRANSITION_FADE_IN);
  } else {
    t->alpha = fmaxf(0.0f, t->alpha - dt / TG_TRANSITION_FADE_OUT);
    if(t->alpha <= 0) t->active = false;
  }
  // The afterglow: as long as the last 35 ms of the hand's path, eased so that it shrinks and
  // grows again on the other side when the hand turns back.
  float want = fmaxf(-0.8f, fminf(0.8f, omega * 0.035f));
  t->trail += (want - t->trail) * (1 - expf(-dt / 0.03f));
}

float tg_transition_angle(const TgTransition* t) { return TAU * smooth01(t->x); }

int tg_transition_year(const TgTransition* t, float* along) {
  // Held at either end for the first and last 15 % of the turn, so both years can be read.
  float a = smooth01((t->x - 0.15f) / 0.7f);
  if(along) *along = a;
  return TG_TRANSITION_YEAR_FROM + (int) lroundf(a * (TG_TRANSITION_YEAR_TO - TG_TRANSITION_YEAR_FROM));
}

// ---------------------------------------------------------------------------------------
// Geometry: plain-colour triangles over everything (TgV3Vertex.w -1).

static void push(TgV3List* l, const TgV3Vertex* vs, int n) {
  if(l->nv + n > l->capv) {
    int cap = l->capv ? l->capv : 1024;
    while(cap < l->nv + n) cap *= 2;
    TgV3Vertex* nv = realloc(l->v, sizeof(TgV3Vertex) * (size_t) cap);
    if(nv == NULL) return;
    l->v = nv;
    l->capv = cap;
  }
  if(l->nb > 0 && l->b[l->nb - 1].pic == -1 && l->b[l->nb - 1].door == 0) {
    l->b[l->nb - 1].count += n;
  } else {
    if(l->nb == l->capb) {
      int cap = l->capb ? l->capb * 2 : 16;
      TgV3Batch* nb = realloc(l->b, sizeof(TgV3Batch) * (size_t) cap);
      if(nb == NULL) return;
      l->b = nb;
      l->capb = cap;
    }
    l->b[l->nb++] = (TgV3Batch) {-1, l->nv, n, 0};
  }
  memcpy(l->v + l->nv, vs, sizeof(TgV3Vertex) * (size_t) n);
  l->nv += n;
}

static TgV3Vertex vtx(float x, float y, uint32_t c) {
  TgV3Vertex v;
  memset(&v, 0, sizeof(v));
  v.x = x, v.y = y, v.argb = c, v.w = -1;
  return v;
}

static void tri(TgV3List* l, TgV3Vertex a, TgV3Vertex b, TgV3Vertex c) {
  TgV3Vertex t[3] = {a, b, c};
  push(l, t, 3);
}

static void quad(TgV3List* l, TgV3Vertex a, TgV3Vertex b, TgV3Vertex c, TgV3Vertex d) {
  TgV3Vertex t[6] = {a, b, c, a, c, d};
  push(l, t, 6);
}

// A convex polygon in colour c at opacity a, with a soft edge `feather` pixels wide outside it
// (opacity to 0): 1 pixel smooths the edge, more makes a glow.
#define POLY_MAX 8
static void poly(TgV3List* l, const float* px, const float* py, int n, Col c, float a, float feather) {
  if(n < 3 || n > POLY_MAX || a <= 0.002f) return;
  uint32_t in = argb(c, a), out = argb(c, 0);
  for(int i = 1; i + 1 < n; i++) tri(l, vtx(px[0], py[0], in), vtx(px[i], py[i], in), vtx(px[i + 1], py[i + 1], in));
  if(feather <= 0) return;
  float area = 0;
  for(int i = 0; i < n; i++) area += px[i] * py[(i + 1) % n] - px[(i + 1) % n] * py[i];
  float nx[POLY_MAX], ny[POLY_MAX], ox[POLY_MAX], oy[POLY_MAX];
  for(int i = 0; i < n; i++) {  // each edge's outward normal
    float dx = px[(i + 1) % n] - px[i], dy = py[(i + 1) % n] - py[i], len = sqrtf(dx * dx + dy * dy);
    if(len < 1e-6f) len = 1e-6f;
    float s = area > 0 ? 1.0f : -1.0f;
    nx[i] = s * dy / len, ny[i] = -s * dx / len;
  }
  for(int i = 0; i < n; i++) {  // each corner moved out by the miter of its two edges
    int p = (i + n - 1) % n;
    float mx = nx[p] + nx[i], my = ny[p] + ny[i], d = 1 + nx[p] * nx[i] + ny[p] * ny[i];
    float k = d > 0.25f ? feather / d : feather * 4;
    ox[i] = px[i] + mx * k, oy[i] = py[i] + my * k;
  }
  for(int i = 0; i < n; i++) {
    int j = (i + 1) % n;
    quad(l, vtx(px[i], py[i], in), vtx(px[j], py[j], in), vtx(ox[j], oy[j], out), vtx(ox[i], oy[i], out));
  }
}

static float px_at(float cx, float r, float a) { return cx + r * sinf(a); }
static float py_at(float cy, float r, float a) { return cy - r * cosf(a); }

// A ring's part between radii r0 and r1 and angles a0 to a1 (clockwise from 12 o'clock), its
// opacity going from alpha0 at a0 to alpha1 at a1, with soft inner and outer edges.
static void arc(TgV3List* l, float cx, float cy, float r0, float r1, float a0, float a1, Col c, float alpha0, float alpha1,
                float feather) {
  if(a1 <= a0 || r1 <= r0 || (alpha0 <= 0.002f && alpha1 <= 0.002f)) return;
  float rad[4] = {r0 - feather, r0, r1, r1 + feather}, k[4] = {0, 1, 1, 0};
  if(rad[0] < 0) rad[0] = 0;
  // Steps of 2 degrees (the afterglow's fade along it), or as long as keeps a small ring's
  // edge within a fifth of a pixel of the circle (the face).
  float edge = r1 + feather, step = edge > 1 ? 2 * sqrtf(0.4f / edge) : TAU / 36;
  if(step < TAU / 180) step = TAU / 180;
  if(step > TAU / 36) step = TAU / 36;
  int n = (int) ceilf((a1 - a0) / step);
  if(n < 1) n = 1;
  for(int i = 0; i < n; i++) {
    float b0 = a0 + (a1 - a0) * i / n, b1 = a0 + (a1 - a0) * (i + 1) / n;
    float al0 = alpha0 + (alpha1 - alpha0) * i / n, al1 = alpha0 + (alpha1 - alpha0) * (i + 1) / n;
    for(int j = 0; j < 3; j++) {
      if(rad[j + 1] - rad[j] < 1e-3f) continue;
      quad(l, vtx(px_at(cx, rad[j], b0), py_at(cy, rad[j], b0), argb(c, al0 * k[j])),
           vtx(px_at(cx, rad[j], b1), py_at(cy, rad[j], b1), argb(c, al1 * k[j])),
           vtx(px_at(cx, rad[j + 1], b1), py_at(cy, rad[j + 1], b1), argb(c, al1 * k[j + 1])),
           vtx(px_at(cx, rad[j + 1], b0), py_at(cy, rad[j + 1], b0), argb(c, al0 * k[j + 1])));
    }
  }
}

// A straight bar from radius r0 to r1 at angle a, w wide.
static void spoke(TgV3List* l, float cx, float cy, float a, float r0, float r1, float w, Col c, float alpha, float feather) {
  float ux = sinf(a), uy = -cosf(a), vx = cosf(a) * w / 2, vy = sinf(a) * w / 2;
  float x0 = cx + ux * r0, y0 = cy + uy * r0, x1 = cx + ux * r1, y1 = cy + uy * r1;
  float qx[4] = {x0 - vx, x1 - vx, x1 + vx, x0 + vx}, qy[4] = {y0 - vy, y1 - vy, y1 + vy, y0 + vy};
  poly(l, qx, qy, 4, c, alpha, feather);
}

// A seven-segment digit in the cell (x, y, w, h), slanted like a clock radio's: the lit
// segments in c at alpha (with a glow when glow > 0), the unlit ones faint, as on a real display.
static const uint8_t kSegments[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
static void digit(TgV3List* l, float x, float y, float w, float h, int d, Col c, float alpha, float glow, float aa) {
  float t = 0.14f * h, g = 0.025f * h, slant = 0.10f, ym = y + h / 2;
  // Segments a-g as (x0, y0) - (x1, y1).
  float xl = x + t / 2, xr = x + w - t / 2, yt = y + t / 2, yb = y + h - t / 2;
  float seg[7][4] = {
      {xl + g, yt, xr - g, yt}, {xr, yt + g, xr, ym - g}, {xr, ym + g, xr, yb - g}, {xl + g, yb, xr - g, yb},
      {xl, ym + g, xl, yb - g}, {xl, yt + g, xl, ym - g}, {xl + g, ym, xr - g, ym}};
  int bits = d >= 0 && d <= 9 ? kSegments[d] : 0;
  for(int s = 0; s < 7; s++) {
    bool lit = (bits >> s) & 1;
    if(glow > 0 && !lit) continue;
    float x0 = seg[s][0], y0 = seg[s][1], x1 = seg[s][2], y1 = seg[s][3];
    float len = fabsf(x1 - x0) + fabsf(y1 - y0);
    float ux = (x1 - x0) / len, uy = (y1 - y0) / len, vx = -uy * t / 2, vy = ux * t / 2, ex = ux * t / 2, ey = uy * t / 2;
    float hx[6] = {x0, x0 + ex + vx, x1 - ex + vx, x1, x1 - ex - vx, x0 + ex - vx};
    float hy[6] = {y0, y0 + ey + vy, y1 - ey + vy, y1, y1 - ey - vy, y0 + ey - vy};
    for(int k = 0; k < 6; k++) hx[k] += slant * (ym - hy[k]);
    if(glow > 0) poly(l, hx, hy, 6, c, alpha * 0.30f, glow);
    else poly(l, hx, hy, 6, c, lit ? alpha : alpha * 0.09f, aa);
  }
}

void tg_transition_region(const TgTransition* t, int w, int h, bool threeD, TgV3List* out) {
  float a = tg_transition_angle(t), a0 = threeD ? 0 : a, a1 = threeD ? a : TAU;
  if(a1 - a0 < 1e-4f || w <= 0 || h <= 0) return;
  float cx = w * 0.5f, cy = h * 0.5f, step = TAU / 120;  // 3 degrees
  int n = (int) ceilf((a1 - a0) / step);
  float R = sqrtf(cx * cx + cy * cy) * 1.05f / cosf((a1 - a0) / n / 2);  // past the corners
  for(int i = 0; i < n; i++) {
    float b0 = a0 + (a1 - a0) * i / n, b1 = a0 + (a1 - a0) * (i + 1) / n;
    TgV3Vertex c = vtx(cx, cy, 0xFFFFFFFFu), p = vtx(px_at(cx, R, b0), py_at(cy, R, b0), 0xFFFFFFFFu),
               q = vtx(px_at(cx, R, b1), py_at(cy, R, b1), 0xFFFFFFFFu);
    TgV3Vertex* vs[3] = {&c, &p, &q};
    for(int k = 0; k < 3; k++) vs[k]->u = vs[k]->x / w, vs[k]->v = vs[k]->y / h;
    tri(out, c, p, q);
  }
}

void tg_transition_overlay(const TgTransition* t, int w, int h, TgV3List* out) {
  float o = smooth01(t->alpha);
  if(o <= 0.002f || w <= 0 || h <= 0) return;
  float m = (float) (w < h ? w : h), cx = w * 0.5f, cy = h * 0.5f;
  float R = sqrtf(cx * cx + cy * cy) * 1.1f;
  float a = tg_transition_angle(t), along;
  int year = tg_transition_year(t, &along);
  Col era = eraColour(smooth01(t->x)), yearCol = eraColour(along);
  float aa = 1.0f;                       // a pixel's soft edge
  float r = 0.105f * m * (0.9f + 0.1f * o);  // the face (grows in, shrinks away)
  float handIn = r * 1.06f;

  // The afterglow behind the hand, brightest at the hand.
  if(t->trail > 0.01f) arc(out, cx, cy, handIn, R, a - t->trail, a, era, 0, 0.34f * o, r * 0.25f);
  else if(t->trail < -0.01f) arc(out, cx, cy, handIn, R, a, a - t->trail, era, 0.34f * o, 0, r * 0.25f);
  // The hand: a glow in the era's colour, a white-hot line.
  float core = fmaxf(2.0f, 0.0032f * m);
  spoke(out, cx, cy, a, handIn, R, core, era, 0.55f * o, 0.02f * m);
  spoke(out, cx, cy, a, handIn, R, core, kWhite, 0.95f * o, aa);

  // The face: dark glass with a soft shadow, the rim lit in the eras' colours (cyan where the
  // 3D view is, amber where the original is), twelve hour marks.
  arc(out, cx, cy, 0, r, 0, TAU, kGlass, 0.74f * o, 0.74f * o, 0.30f * r);
  float rim = fmaxf(2.0f, 0.06f * r);
  arc(out, cx, cy, r - rim, r, 0, a, kCyan, 0.95f * o, 0.95f * o, aa);
  arc(out, cx, cy, r - rim, r, a, TAU, kAmber, 0.95f * o, 0.95f * o, aa);
  for(int i = 0; i < 12; i++)
    spoke(out, cx, cy, TAU * i / 12, r * (i % 3 ? 0.80f : 0.72f), r * 0.88f, fmaxf(1.5f, 0.028f * r), kWhite, 0.42f * o, aa);

  // The year, and fast-forward or rewind under it.
  float dh = 0.40f * r, dw = 0.52f * dh, gap = 0.17f * dh, total = 4 * dw + 3 * gap;
  float x0 = cx - total / 2, y0 = cy - dh / 2 - 0.05f * r;
  int digits[4] = {year / 1000 % 10, year / 100 % 10, year / 10 % 10, year % 10};
  for(int pass = 0; pass < 2; pass++)
    for(int i = 0; i < 4; i++)
      digit(out, x0 + i * (dw + gap), y0, dw, dh, digits[i], yearCol, o, pass == 0 ? 0.16f * dh : 0, aa);
  float ah = 0.13f * r, aw = 0.11f * r, ay = cy + 0.47f * r, dir = t->to3d ? 1.0f : -1.0f;
  for(int i = 0; i < 2; i++) {
    float bx = cx + dir * (i - 1) * aw;  // two marks side by side about the centre, pointing dir
    float tx[3] = {bx, bx + dir * aw, bx}, ty[3] = {ay - ah / 2, ay, ay + ah / 2};
    poly(out, tx, ty, 3, yearCol, 0.9f * o, aa);
  }
}
