// The classic title's logos as vector shapes (see logo_vector.h).
//
// How it is done (the way image tracers do it):
//  1. Outlines: each part's mask (the face; the face and its outline together) is followed
//     along the pixels' edges into closed paths, outer ones and holes (the G's counter, the
//     A's, R's, P's, O's), diagonal neighbours joined (P. Selinger, "Potrace: a polygon-based
//     tracing algorithm", 2003: its path decomposition).
//  2. Lines: the middles of a path's pixel edges are split where they stop being straight
//     (Douglas & Peucker, 1973), each piece fitted with its best line (least squares), so a
//     staircase becomes one slanted line; pieces at almost the same angle are merged again;
//     lines near the logo's italic slant (and the horizontal, the vertical) are turned onto
//     it, so all its stems are parallel; the corners are where the lines meet (the sharp
//     corners kept, as Potrace's optimal polygon and Kopf & Lischinski's "Depixelizing Pixel
//     Art", 2011, keep the corners of pixel art), a stair-step never a corner. A path of
//     five points and five dents is a star: it is made a perfect one.
//  3. Fill: each outer path and its holes, bridged into one ring (D. Eberly, "Triangulation
//     by Ear Clipping"), cut into triangles by ear clipping (as Mapbox's earcut does),
//     the triangles cut into cells so the light and the face's gradient (the picture's
//     colours averaged over a wide box: one smooth gradient, no pixels) are taken often.
//  4. Solid: the face in front with a bevel down to the outline, the outline with a bevel
//     and its sides back to the depth.
#include "logo_vector.h"
#include "logo_glyphs.h"
#include "now_ms.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  float x, y;
} P2;

static double now_ms(void) { return tg_now_ms(); }

#define PUSH(arr, n, cap, val)                                                       \
  do {                                                                               \
    if((n) == (cap)) {                                                               \
      int nc_ = (cap) ? (cap) * 2 : 64;                                              \
      void* q_ = realloc((arr), sizeof(*(arr)) * (size_t) nc_);                      \
      if(!q_) break;                                                                 \
      (arr) = q_;                                                                    \
      (cap) = nc_;                                                                   \
    }                                                                                \
    (arr)[(n)++] = (val);                                                            \
  } while(0)

static float cross2(P2 a, P2 b) { return a.x * b.y - a.y * b.x; }
static P2 sub2(P2 a, P2 b) { return (P2) {a.x - b.x, a.y - b.y}; }
static float len2(P2 a) { return sqrtf(a.x * a.x + a.y * a.y); }

// --- 1. Outlines -----------------------------------------------------------------------
// The edges between a pixel in the mask and one out of it, each going with the mask on its
// right (on the screen, y down): outer paths go clockwise, holes the other way.
static const int kDX[4] = {1, 0, -1, 0}, kDY[4] = {0, 1, 0, -1};

typedef struct {
  int* xy;  // the corners of the pixel grid it passes, one step apart
  int n, cap;
} Chain;

static int trace_mask(const uint8_t* m, int W, int H, Chain** out) {
  int VW = W + 1;
  uint8_t* e = calloc((size_t) VW * (H + 1), 1);
  if(!e) return 0;
  for(int y = 0; y < H; y++)
    for(int x = 0; x < W; x++) {
      if(!m[y * W + x]) continue;
      if(y == 0 || !m[(y - 1) * W + x]) e[y * VW + x] |= 1;
      if(x == W - 1 || !m[y * W + x + 1]) e[y * VW + x + 1] |= 2;
      if(y == H - 1 || !m[(y + 1) * W + x]) e[(y + 1) * VW + x + 1] |= 4;
      if(x == 0 || !m[y * W + x - 1]) e[(y + 1) * VW + x] |= 8;
    }
  Chain* ch = NULL;
  int nch = 0, cch = 0;
  for(int v0 = 0; v0 < VW * (H + 1); v0++) {
    while(e[v0]) {
      Chain c = {0};
      int v = v0, d = 0;
      while(!(e[v] >> d & 1)) d++;
      for(;;) {
        int pt[2] = {v % VW, v / VW};
        PUSH(c.xy, c.n, c.cap, pt[0]);
        PUSH(c.xy, c.n, c.cap, pt[1]);
        e[v] &= (uint8_t) ~(1 << d);
        v += kDX[d] + kDY[d] * VW;
        // On: left first (diagonal neighbours stay one shape), then straight, then right.
        int l = (d + 3) & 3, r = (d + 1) & 3;
        if(e[v] >> l & 1) d = l;
        else if(e[v] >> d & 1) {
        } else if(e[v] >> r & 1) d = r;
        else break;
      }
      c.n /= 2;
      PUSH(ch, nch, cch, c);
    }
  }
  free(e);
  *out = ch;
  return nch;
}

// --- 2. Lines ----------------------------------------------------------------------------
typedef struct {
  float cx, cy, ux, uy;  // a point on it, its direction
  float len;
  int a, b;              // its samples
} Line;

typedef struct {
  P2* s;     // the samples: the middles of the path's pixel edges
  int ns;
  float area;  // > 0: an outer path, < 0: a hole
  Line* ln;
  int nl, cl;
  P2* v;     // the corners
  int nv, cv;
  int outer;  // a hole: its outer path
  int mask;   // 0: the face, 1: the outline (the face and the outline)
  bool star;  // made a perfect star
} Path;

static Line fit_line(const P2* s, int ns, int a, int b) {
  int cnt = b - a + 1, trim = cnt >= 10 ? 2 : cnt >= 4 ? 1 : 0;  // (the ends: the corners, not the line)
  P2 A = s[a % ns], B = s[b % ns];
  Line L = {0};
  L.a = a, L.b = b;
  float dx = B.x - A.x, dy = B.y - A.y, cl = sqrtf(dx * dx + dy * dy);
  L.len = cl;
  int i0 = a + trim, i1 = b - trim;
  if(i1 - i0 < 1) {
    L.cx = (A.x + B.x) / 2, L.cy = (A.y + B.y) / 2;
    L.ux = cl > 1e-6f ? dx / cl : 1, L.uy = cl > 1e-6f ? dy / cl : 0;
    return L;
  }
  double sx = 0, sy = 0, n = 0;
  for(int i = i0; i <= i1; i++) sx += s[i % ns].x, sy += s[i % ns].y, n++;
  sx /= n, sy /= n;
  double sxx = 0, syy = 0, sxy = 0;
  for(int i = i0; i <= i1; i++) {
    double px = s[i % ns].x - sx, py = s[i % ns].y - sy;
    sxx += px * px, syy += py * py, sxy += px * py;
  }
  double ang = 0.5 * atan2(2 * sxy, sxx - syy);
  float ux = (float) cos(ang), uy = (float) sin(ang);
  if(ux * dx + uy * dy < 0) ux = -ux, uy = -uy;
  L.cx = (float) sx, L.cy = (float) sy, L.ux = ux, L.uy = uy;
  return L;
}

static float line_dev(const P2* s, int ns, const Line* L) {
  float m = 0;
  for(int i = L->a; i <= L->b; i++) {
    float d = fabsf((s[i % ns].x - L->cx) * L->uy - (s[i % ns].y - L->cy) * L->ux);
    m = d > m ? d : m;
  }
  return m;
}

static void dp(const P2* s, int ns, int a, int b, float tol, int** brk, int* nb, int* cb) {
  if(b - a < 2) return;
  P2 A = s[a % ns], B = s[b % ns];
  float dx = B.x - A.x, dy = B.y - A.y, L = sqrtf(dx * dx + dy * dy), best = -1;
  int bi = -1;
  for(int i = a + 1; i < b; i++) {
    P2 p = s[i % ns];
    float d = L > 1e-6f ? fabsf((p.x - A.x) * dy - (p.y - A.y) * dx) / L : len2(sub2(p, A));
    if(d > best) best = d, bi = i;
  }
  // (a short piece is held closer: a small shape's corners are a pixel or two apart)
  if(best > fminf(tol, 0.4f + 0.07f * L)) {
    dp(s, ns, a, bi, tol, brk, nb, cb);
    PUSH(*brk, *nb, *cb, bi);
    dp(s, ns, bi, b, tol, brk, nb, cb);
  }
}

static void fit_path(Path* p, float tol) {
  int ns = p->ns;
  // Start at the sample furthest from the middle; split at the one furthest from it.
  float mx = 0, my = 0;
  for(int i = 0; i < ns; i++) mx += p->s[i].x, my += p->s[i].y;
  mx /= ns, my /= ns;
  int i0 = 0;
  float best = -1;
  for(int i = 0; i < ns; i++) {
    float d = (p->s[i].x - mx) * (p->s[i].x - mx) + (p->s[i].y - my) * (p->s[i].y - my);
    if(d > best) best = d, i0 = i;
  }
  P2* r = malloc(sizeof(P2) * (size_t) ns);
  if(!r) return;
  for(int i = 0; i < ns; i++) r[i] = p->s[(i0 + i) % ns];
  free(p->s);
  p->s = r;
  int i1 = 1;
  best = -1;
  for(int i = 1; i < ns; i++) {
    float d = len2(sub2(r[i], r[0]));
    if(d > best) best = d, i1 = i;
  }
  int *brk = NULL, nb = 0, cb = 0;
  PUSH(brk, nb, cb, 0);
  dp(r, ns, 0, i1, tol, &brk, &nb, &cb);
  PUSH(brk, nb, cb, i1);
  dp(r, ns, i1, ns, tol, &brk, &nb, &cb);
  // The pieces' lines; then neighbours at almost the same angle merged while they fit.
  p->nl = 0;
  for(int k = 0; k < nb; k++) {
    Line L = fit_line(r, ns, brk[k], k + 1 < nb ? brk[k + 1] : ns);
    PUSH(p->ln, p->nl, p->cl, L);
  }
  free(brk);
  for(bool again = true; again && p->nl > 3;) {
    again = false;
    for(int k = 0; k < p->nl && p->nl > 3; k++) {
      Line* A = &p->ln[k];
      Line* B = &p->ln[(k + 1) % p->nl];
      if(fabsf(A->ux * B->uy - A->uy * B->ux) > 0.17f || A->ux * B->ux + A->uy * B->uy < 0) continue;
      if(k + 1 == p->nl) continue;  // (not across the start: it is a corner)
      Line M = fit_line(r, ns, A->a, B->b);
      if(line_dev(r, ns, &M) > tol * 1.15f) continue;
      *A = M;
      memmove(&p->ln[k + 1], &p->ln[k + 2], sizeof(Line) * (size_t) (p->nl - k - 2));
      p->nl--;
      again = true;
    }
  }
  // A short piece between two long ones is a corner cut by the pixels: the corner instead.
  for(int k = 0; k < p->nl && p->nl > 3; k++) {
    Line* L = &p->ln[k];
    const Line* A = &p->ln[(k + p->nl - 1) % p->nl];
    Line* B = &p->ln[(k + 1) % p->nl];
    if(L->len >= 1.6f || A->len < 2.5f || B->len < 2.5f || k + 1 == p->nl) continue;
    float den = A->ux * B->uy - A->uy * B->ux;
    if(fabsf(den) < 0.3f) continue;
    float t = ((B->cx - A->cx) * B->uy - (B->cy - A->cy) * B->ux) / den;
    P2 X = {A->cx + A->ux * t, A->cy + A->uy * t}, mid = r[((L->a + L->b) / 2) % ns];
    if(len2(sub2(X, mid)) > 1.6f) continue;
    B->a = (L->a + L->b) / 2;
    memmove(&p->ln[k], &p->ln[k + 1], sizeof(Line) * (size_t) (p->nl - k - 1));
    p->nl--;
    k--;
  }
}

static float line_angle(const Line* L) {  // 0 - 180 degrees
  float a = atan2f(L->uy, L->ux) * 57.29578f;
  while(a < 0) a += 180;
  while(a >= 180) a -= 180;
  return a;
}

static void set_angle(Line* L, float deg) {
  float a = deg / 57.29578f, ux = cosf(a), uy = sinf(a);
  if(ux * L->ux + uy * L->uy < 0) ux = -ux, uy = -uy;
  L->ux = ux, L->uy = uy;
}

// The corners: where each line meets the next; two corners where they hardly meet.
static void corners(Path* p) {
  p->nv = 0;
  int n = p->nl;
  for(int k = 0; k < n; k++) {
    const Line* A = &p->ln[(k + n - 1) % n];
    const Line* B = &p->ln[k];
    P2 at = p->s[B->a % p->ns];
    float den = A->ux * B->uy - A->uy * B->ux;
    P2 pa = {A->cx + A->ux * ((at.x - A->cx) * A->ux + (at.y - A->cy) * A->uy), A->cy + A->uy * ((at.x - A->cx) * A->ux + (at.y - A->cy) * A->uy)};
    P2 pb = {B->cx + B->ux * ((at.x - B->cx) * B->ux + (at.y - B->cy) * B->uy), B->cy + B->uy * ((at.x - B->cx) * B->ux + (at.y - B->cy) * B->uy)};
    P2 mid = {(pa.x + pb.x) / 2, (pa.y + pb.y) / 2};
    if(fabsf(den) < 0.2f) {  // (almost parallel: one corner between them, never a step)
      PUSH(p->v, p->nv, p->cv, mid);
      continue;
    }
    float t = ((B->cx - A->cx) * B->uy - (B->cy - A->cy) * B->ux) / den;
    P2 X = {A->cx + A->ux * t, A->cy + A->uy * t};
    float sharp = A->ux * B->ux + A->uy * B->uy < -0.3f ? 2.5f : 0;  // (a star's point reaches further)
    float reach = fminf(2.2f + sharp, 0.4f * fminf(A->len, B->len) + 0.8f);  // (small shapes: their own size)
    if(len2(sub2(X, at)) > reach) {
      if((pb.x - pa.x) * A->ux + (pb.y - pa.y) * A->uy < 0 || len2(sub2(pa, pb)) < 0.35f) PUSH(p->v, p->nv, p->cv, mid);  // (not back on itself)
      else {
        PUSH(p->v, p->nv, p->cv, pa);
        PUSH(p->v, p->nv, p->cv, pb);
      }
    } else {
      PUSH(p->v, p->nv, p->cv, X);
    }
  }
}

static bool seg_cross(P2 a, P2 b, P2 c, P2 d) {
  float d1 = cross2(sub2(b, a), sub2(c, a)), d2 = cross2(sub2(b, a), sub2(d, a));
  float d3 = cross2(sub2(d, c), sub2(a, c)), d4 = cross2(sub2(d, c), sub2(b, c));
  return ((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0)) && d1 != 0 && d2 != 0 && d3 != 0 && d4 != 0;
}

static bool self_crossing(const P2* v, int n) {
  for(int i = 0; i < n; i++)
    for(int j = i + 2; j < n; j++) {
      if(i == 0 && j == n - 1) continue;
      if(seg_cross(v[i], v[(i + 1) % n], v[j], v[(j + 1) % n])) return true;
    }
  return false;
}

static float ring_area(const P2* v, int n) {
  float a = 0;
  for(int i = 0; i < n; i++) a += cross2(v[i], v[(i + 1) % n]);
  return a / 2;
}

static float snap_to(float a, float slant) {  // (slant < -1: none)
  if(slant < -1) return a;
  if(a < 5 || a > 175) return 0;
  if(fabsf(a - 90) < 3) return 90;
  if(slant > 0 && fabsf(a - slant) < 5) return slant;
  if(fabsf(a - 45) < 3) return 45;
  if(fabsf(a - 135) < 3) return 135;
  return a;
}

// How far the samples a..b (their ends left out) stray from the best line at angle ang (its
// offset along the normal in *off).
static float angle_dev(const P2* s, int ns, int a, int b, float ang, float* off) {
  int cnt = b - a + 1, trim = cnt >= 10 ? 2 : cnt >= 4 ? 1 : 0, i0 = a + trim, i1 = b - trim;
  if(i1 - i0 < 1) return 1e30f;
  float r = ang / 57.29578f, ux = cosf(r), uy = sinf(r), lo = 1e30f, hi = -1e30f;
  for(int i = i0; i <= i1; i++) {
    float d = -s[i % ns].x * uy + s[i % ns].y * ux;
    lo = d < lo ? d : lo, hi = d > hi ? d : hi;
  }
  *off = (hi + lo) / 2;
  return (hi - lo) / 2;
}

static void set_line(Line* L, float ang, float off) {
  float r = ang / 57.29578f, ux = cosf(r), uy = sinf(r);
  if(ux * L->ux + uy * L->uy < 0) ux = -ux, uy = -uy;
  float along = (L->cx * ux + L->cy * uy);
  float nx = -uy, ny = ux, o = off * (nx * -sinf(r) + ny * cosf(r));  // (the offset's sign with the turned normal)
  L->ux = ux, L->uy = uy;
  L->cx = ux * along + nx * o, L->cy = uy * along + ny * o;
}

// A line turned onto the horizontal, the vertical, the slant or a 45-degree chamfer when its
// own samples allow it (they stay within half a pixel of it): short edges too, where the fit
// alone is unsure. Returns the angle it took (-1: none).
static float snap_line(Line* L, const P2* s, int ns, float slant) {
  float cand[5] = {0, 90, 45, 135, slant}, fit = line_angle(L), bestDev = 1e30f, bestA = -1, bestOff = 0;
  for(int c = 0; c < (slant > 0 ? 5 : 4); c++) {
    float off, dev = angle_dev(s, ns, L->a, L->b, cand[c], &off);
    float diff = fabsf(fit - cand[c]);
    diff = diff > 90 ? 180 - diff : diff;
    bool loose = cand[c] == 0 || cand[c] == slant;  // (near these, a little off is still meant)
    if((dev <= 0.55f || (loose && diff <= 5)) && diff < 25 && dev < bestDev) bestDev = dev, bestA = cand[c], bestOff = off;
  }
  if(bestA < 0) return -1;
  set_line(L, bestA, bestOff);
  return bestA;
}

// The polygon refined: each sample to its nearest side, each side fitted again to its own
// samples (those by a corner left out), the corners where the sides meet (Lloyd-like: the
// pieces' ends no longer decide where the corners are).
static void refine(Path* p, float slant) {
  int n = p->nv;
  if(n < 3 || n > 256) return;
  double acc[256][6];
  memset(acc, 0, sizeof(double) * 6 * (size_t) n);
  int last = -1;
  for(int i = 0; i < p->ns; i++) {
    P2 q = p->s[i];
    int best = -1;
    float bd = 1e30f, bt = 0;
    // (the samples go round in order: the side of the last one, or the next ones)
    for(int j = last < 0 ? 0 : -1; j < (last < 0 ? n : 3); j++) {
      int k = last < 0 ? j : (last + j + n) % n;
      P2 a = p->v[k], b = p->v[(k + 1) % n], e = sub2(b, a);
      float l2 = e.x * e.x + e.y * e.y, t = l2 > 1e-9f ? ((q.x - a.x) * e.x + (q.y - a.y) * e.y) / l2 : 0;
      t = t < 0 ? 0 : t > 1 ? 1 : t;
      P2 c = {a.x + e.x * t, a.y + e.y * t};
      float d = (q.x - c.x) * (q.x - c.x) + (q.y - c.y) * (q.y - c.y);
      if(d < bd) bd = d, best = k, bt = t * l2;
    }
    if(bd > 2.25f) last = -1;  // (lost: look at all of them next time)
    else last = best;
    if(best < 0 || bd > 2.25f) continue;
    bt = sqrtf(bt * bt / fmaxf(1e-9f, (p->v[(best + 1) % n].x - p->v[best].x) * (p->v[(best + 1) % n].x - p->v[best].x) + (p->v[(best + 1) % n].y - p->v[best].y) * (p->v[(best + 1) % n].y - p->v[best].y)));
    float L = len2(sub2(p->v[(best + 1) % n], p->v[best]));
    if(bt < 0.8f || bt > L - 0.8f) continue;  // (by a corner)
    double* A = acc[best];
    A[0] += 1, A[1] += q.x, A[2] += q.y, A[3] += q.x * q.x, A[4] += q.y * q.y, A[5] += q.x * q.y;
  }
  Line E[256];
  for(int k = 0; k < n; k++) {
    P2 a = p->v[k], b = p->v[(k + 1) % n], e = sub2(b, a);
    float l = len2(e);
    E[k] = (Line) {(a.x + b.x) / 2, (a.y + b.y) / 2, l > 1e-6f ? e.x / l : 1, l > 1e-6f ? e.y / l : 0, l, 0, 0};
    double* A = acc[k];
    if(A[0] >= 2) {
      double mx = A[1] / A[0], my = A[2] / A[0];
      double sxx = A[3] / A[0] - mx * mx, syy = A[4] / A[0] - my * my, sxy = A[5] / A[0] - mx * my;
      double ang = 0.5 * atan2(2 * sxy, sxx - syy);
      float ux = (float) cos(ang), uy = (float) sin(ang);
      if(ux * E[k].ux + uy * E[k].uy < 0) ux = -ux, uy = -uy;
      if(ux * E[k].ux + uy * E[k].uy > 0.9f) E[k].cx = (float) mx, E[k].cy = (float) my, E[k].ux = ux, E[k].uy = uy;
    }
    if(E[k].len >= 2.5f) set_angle(&E[k], snap_to(line_angle(&E[k]), slant));
  }
  P2 nv[256];
  for(int k = 0; k < n; k++) {
    const Line* A = &E[(k + n - 1) % n];
    const Line* B = &E[k];
    P2 at = p->v[k];
    float ta = (at.x - A->cx) * A->ux + (at.y - A->cy) * A->uy, tb = (at.x - B->cx) * B->ux + (at.y - B->cy) * B->uy;
    P2 pa = {A->cx + A->ux * ta, A->cy + A->uy * ta}, pb = {B->cx + B->ux * tb, B->cy + B->uy * tb};
    P2 mid = {(pa.x + pb.x) / 2, (pa.y + pb.y) / 2};
    float den = A->ux * B->uy - A->uy * B->ux;
    nv[k] = mid;
    if(fabsf(den) < 0.2f) continue;
    float t = ((B->cx - A->cx) * B->uy - (B->cy - A->cy) * B->ux) / den;
    P2 X = {A->cx + A->ux * t, A->cy + A->uy * t};
    float sharp = A->ux * B->ux + A->uy * B->uy < -0.3f ? 2.5f : 0;
    float reach = fminf(1.5f + sharp, 0.4f * fminf(A->len, B->len) + 0.8f);
    if(len2(sub2(X, at)) <= reach) nv[k] = X;
  }
  if(self_crossing(nv, n) || (ring_area(nv, n) > 0) != (ring_area(p->v, n) > 0)) return;
  memcpy(p->v, nv, sizeof(P2) * (size_t) n);
}

// Five points and five dents, the points as far from the middle: a perfect star instead.
static void star_fix(Path* p) {
  if(p->nv != 10 || p->area <= 0) return;
  float mx = 0, my = 0;
  for(int i = 0; i < 10; i++) mx += p->v[i].x, my += p->v[i].y;
  mx /= 10, my /= 10;
  bool cv[10];
  int convex = 0, first = -1;
  for(int i = 0; i < 10; i++) {
    P2 a = p->v[(i + 9) % 10], b = p->v[i], c = p->v[(i + 1) % 10];
    cv[i] = cross2(sub2(b, a), sub2(c, b)) > 0;
    if(cv[i]) convex++;
    if(cv[i] && first < 0) first = i;
  }
  if(convex != 5) return;
  for(int i = 0; i < 10; i++)  // they alternate
    if(cv[i] != (((i - first) & 1) == 0)) return;
  float R = 0, r = 0, R2 = 0, sc = 0, ss = 0;
  for(int i = 0; i < 10; i++) {
    float d = len2(sub2(p->v[i], (P2) {mx, my}));
    if(((i - first) & 1) == 0) {
      R += d / 5, R2 += d * d / 5;
      float a = atan2f(p->v[i].y - my, p->v[i].x - mx) * 5;
      sc += cosf(a), ss += sinf(a);
    } else {
      r += d / 5;
    }
  }
  if(sqrtf(fmaxf(0, R2 - R * R)) > 0.15f * R || r > 0.75f * R) return;
  // The middle: the points' own (the dents are blunter in the picture).
  float px = 0, py = 0;
  for(int i = 0; i < 10; i++)
    if(cv[i]) px += p->v[i].x / 5, py += p->v[i].y / 5;
  float a0 = atan2f(ss, sc) / 5;
  for(int i = 0; i < 10; i++) {
    float a = a0 + (float) (i * 3.14159265 / 5);
    float d = i & 1 ? r : R;
    p->v[i] = (P2) {px + cosf(a) * d, py + sinf(a) * d};
  }
  p->star = true;
  // (clockwise on the screen, like the path: angles grow clockwise with y down)
}

// --- 3. Fill -------------------------------------------------------------------------------
typedef struct {
  P2* p;
  int n, cap;
} Ring;

// A hole bridged into the ring (Eberly): from its rightmost corner to a corner of the ring
// it can see to its right.
static void bridge(Ring* ring, const P2* h, int nh) {
  int mi = 0;
  for(int i = 1; i < nh; i++)
    if(h[i].x > h[mi].x) mi = i;
  P2 M = h[mi];
  float bx = 1e30f;
  int pi = -1;
  for(int i = 0; i < ring->n; i++) {
    P2 a = ring->p[i], b = ring->p[(i + 1) % ring->n];
    if((a.y > M.y) == (b.y > M.y) && a.y != M.y && b.y != M.y) continue;
    if(a.y == b.y) {
      if(a.y != M.y) continue;
      float x = fminf(a.x, b.x);
      if(x >= M.x && x < bx) bx = x, pi = a.x < b.x ? i : (i + 1) % ring->n;
      continue;
    }
    float x = a.x + (M.y - a.y) * (b.x - a.x) / (b.y - a.y);
    if(x >= M.x && x < bx) bx = x, pi = a.x > b.x ? i : (i + 1) % ring->n;
  }
  if(pi < 0) return;
  // Corners of the ring inside the triangle M, I, P: the one nearest the ray's angle.
  P2 I = {bx, M.y}, P = ring->p[pi];
  float bestAng = 1e30f, bestD = 1e30f;
  int best = pi;
  for(int i = 0; i < ring->n; i++) {
    P2 q = ring->p[i];
    if(i == pi || q.x < M.x) continue;
    // inside triangle (M, I, P), either way round
    float d1 = cross2(sub2(I, M), sub2(q, M)), d2 = cross2(sub2(P, I), sub2(q, I)), d3 = cross2(sub2(M, P), sub2(q, P));
    bool neg = d1 < 0 || d2 < 0 || d3 < 0, pos = d1 > 0 || d2 > 0 || d3 > 0;
    if(neg && pos) continue;
    P2 a = ring->p[(i + ring->n - 1) % ring->n], c = ring->p[(i + 1) % ring->n];
    if(cross2(sub2(q, a), sub2(c, q)) > 0) continue;  // convex: not in the way
    float ang = fabsf(atan2f(q.y - M.y, q.x - M.x)), d = len2(sub2(q, M));
    if(ang < bestAng - 1e-6f || (fabsf(ang - bestAng) < 1e-6f && d < bestD)) bestAng = ang, bestD = d, best = i;
  }
  pi = best;
  Ring out = {0};
  for(int i = 0; i <= pi; i++) PUSH(out.p, out.n, out.cap, ring->p[i]);
  for(int i = 0; i <= nh; i++) PUSH(out.p, out.n, out.cap, h[(mi + i) % nh]);
  PUSH(out.p, out.n, out.cap, ring->p[pi]);
  for(int i = pi + 1; i < ring->n; i++) PUSH(out.p, out.n, out.cap, ring->p[i]);
  free(ring->p);
  *ring = out;
}

typedef struct {
  P2 a, b, c;
} Tri2;

static bool same(P2 a, P2 b) { return fabsf(a.x - b.x) < 1e-4f && fabsf(a.y - b.y) < 1e-4f; }

static void earclip(const Ring* r, Tri2** tris, int* nt, int* ct) {
  int n = r->n;
  if(n < 3) return;
  int* nx = malloc(sizeof(int) * (size_t) n * 2);
  if(!nx) return;
  int* pv = nx + n;
  for(int i = 0; i < n; i++) nx[i] = (i + 1) % n, pv[i] = (i + n - 1) % n;
  int left = n, i = 0, guard = 0;
  while(left > 3) {
    int a = pv[i], c = nx[i];
    P2 A = r->p[a], B = r->p[i], C = r->p[c];
    bool ear = cross2(sub2(B, A), sub2(C, B)) > 1e-7f;
    if(ear)
      for(int j = nx[c]; j != a; j = nx[j]) {
        P2 q = r->p[j];
        if(same(q, A) || same(q, B) || same(q, C)) continue;
        if(cross2(sub2(B, A), sub2(q, A)) >= 0 && cross2(sub2(C, B), sub2(q, B)) >= 0 && cross2(sub2(A, C), sub2(q, C)) >= 0) {
          ear = false;
          break;
        }
      }
    if(ear || guard > left) {  // (none left that is clean: the closest it gets)
      if(cross2(sub2(B, A), sub2(C, B)) > 1e-7f) PUSH(*tris, *nt, *ct, ((Tri2) {A, B, C}));
      nx[a] = c, pv[c] = a;
      left--;
      i = c;
      guard = 0;
    } else {
      i = nx[i];
      guard++;
    }
  }
  int a = pv[i], c = nx[i];
  if(cross2(sub2(r->p[i], r->p[a]), sub2(r->p[c], r->p[i])) > 1e-7f) PUSH(*tris, *nt, *ct, ((Tri2) {r->p[a], r->p[i], r->p[c]}));
  free(nx);
}

// The face's colour: the picture's face colours averaged over a box (a summed table).
typedef struct {
  uint32_t* sum;  // (W + 1) x (H + 1) x 4: r, g, b, count
  int W, H, rx, ry;
  float* img;     // W x H x 4: the box's average at each pixel (and whether it is worked out)
} Field;

static int box_at(const Field* f, int cx, int cy, int rx, int ry, float out[3]) {
  int x0 = cx - rx, x1 = cx + rx + 1, y0 = cy - ry, y1 = cy + ry + 1;
  x0 = x0 < 0 ? 0 : x0 > f->W ? f->W : x0, x1 = x1 < 0 ? 0 : x1 > f->W ? f->W : x1;
  y0 = y0 < 0 ? 0 : y0 > f->H ? f->H : y0, y1 = y1 < 0 ? 0 : y1 > f->H ? f->H : y1;
  int S = f->W + 1;
  uint32_t s[4];
  for(int k = 0; k < 4; k++) s[k] = f->sum[(y1 * S + x1) * 4 + k] - f->sum[(y0 * S + x1) * 4 + k] - f->sum[(y1 * S + x0) * 4 + k] + f->sum[(y0 * S + x0) * 4 + k];
  if(!s[3]) return 0;
  for(int k = 0; k < 3; k++) out[k] = (float) s[k] / s[3];
  return (int) s[3];
}

// A pixel's average, worked out the first time it is asked for (only those near the face are).
static const float* field_px(Field* f, int x, int y) {
  float* o = &f->img[(y * f->W + x) * 4];
  if(o[3] != 0) return o;
  int rx = f->rx, ry = f->ry, got = 0;
  for(int t = 0; t < 6 && !got; t++, rx = rx * 2 + 1, ry = ry * 2 + 1) got = box_at(f, x, y, rx, ry, o);
  if(!got) o[0] = 240, o[1] = 128, o[2] = 48;
  o[3] = 1;
  return o;
}

// The colour at a point: the baked averages, bilinear between the pixels' middles.
static uint32_t field_at(Field* f, float x, float y) {
  float fx = x - 0.5f, fy = y - 0.5f;
  int x0 = (int) floorf(fx), y0 = (int) floorf(fy);
  float tx = fx - x0, ty = fy - y0, acc[3] = {0, 0, 0};
  for(int k = 0; k < 4; k++) {
    int cx = x0 + (k & 1), cy = y0 + (k >> 1);
    cx = cx < 0 ? 0 : cx >= f->W ? f->W - 1 : cx, cy = cy < 0 ? 0 : cy >= f->H ? f->H - 1 : cy;
    float w = ((k & 1) ? tx : 1 - tx) * ((k >> 1) ? ty : 1 - ty);
    const float* c = field_px(f, cx, cy);
    for(int j = 0; j < 3; j++) acc[j] += c[j] * w;
  }
  uint32_t out = 0xFF000000u;
  for(int j = 0; j < 3; j++) {
    int v = (int) (acc[j] + 0.5f);
    out |= (uint32_t) (v > 255 ? 255 : v < 0 ? 0 : v) << (16 - 8 * j);
  }
  return out;
}

static int clip_axis(const P2* in, int n, P2* out, int axis, float v, bool keepGreater) {
  int m = 0;
  for(int i = 0; i < n; i++) {
    P2 a = in[i], b = in[(i + 1) % n];
    float da = (axis ? a.y : a.x) - v, db = (axis ? b.y : b.x) - v;
    if(!keepGreater) da = -da, db = -db;
    if(da >= 0) out[m++] = a;
    if((da >= 0) != (db >= 0)) {
      float t = da / (da - db);
      out[m++] = (P2) {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
    }
  }
  return m;
}

// A fill from top to bottom (the word under the splash's logo), while it is built.
typedef struct {
  float y0, y1;
  uint32_t c0, c1;
} Grad;
static const Grad* g_grad;

static uint32_t paint(Field* f, uint32_t flat, float x, float y) {
  if(f) return field_at(f, x, y);
  if(!g_grad) return flat;
  float t = (y - g_grad->y0) / (g_grad->y1 - g_grad->y0);
  t = t < 0 ? 0 : t > 1 ? 1 : t;
  uint32_t out = 0xFF000000u;
  for(int sh = 0; sh < 24; sh += 8) {
    float a = (float) (g_grad->c0 >> sh & 255), b = (float) (g_grad->c1 >> sh & 255);
    out |= (uint32_t) (a + (b - a) * t + 0.5f) << sh;
  }
  return out;
}

static void push_poly(LvMesh* m, const LvPoly* q) { PUSH(m->p, m->np, m->cap, *q); }

// A front: the triangles cut into cells, each corner its colour.
static void front(LvMesh* m, const Tri2* t, int nt, float z, int mat, Field* f, uint32_t flat, float cell) {
  for(int i = 0; i < nt; i++) {
    P2 tri[3] = {t[i].a, t[i].b, t[i].c};
    float minx = fminf(tri[0].x, fminf(tri[1].x, tri[2].x)), maxx = fmaxf(tri[0].x, fmaxf(tri[1].x, tri[2].x));
    float miny = fminf(tri[0].y, fminf(tri[1].y, tri[2].y)), maxy = fmaxf(tri[0].y, fmaxf(tri[1].y, tri[2].y));
    for(int cy = (int) floorf(miny / cell); cy * cell < maxy; cy++)
      for(int cx = (int) floorf(minx / cell); cx * cell < maxx; cx++) {
        P2 a[LV_MAXV + 2], c[LV_MAXV + 2], *src = a, *dst = c, *tmp;
        int n = 3;
        memcpy(a, tri, sizeof(tri));
        float X0 = cx * cell, X1 = X0 + cell, Y0 = cy * cell, Y1 = Y0 + cell;
        // (only the cell's sides that cross the triangle)
        if(minx < X0) n = clip_axis(src, n, dst, 0, X0, true), tmp = src, src = dst, dst = tmp;
        if(maxx > X1 && n) n = clip_axis(src, n, dst, 0, X1, false), tmp = src, src = dst, dst = tmp;
        if(miny < Y0 && n) n = clip_axis(src, n, dst, 1, Y0, true), tmp = src, src = dst, dst = tmp;
        if(maxy > Y1 && n) n = clip_axis(src, n, dst, 1, Y1, false), tmp = src, src = dst, dst = tmp;
        P2* b = src;
        if(n < 3 || n > LV_MAXV || ring_area(b, n) < 1e-4f) continue;
        LvPoly q = {(uint8_t) mat, (uint8_t) n, 0, 0, -1, {{0}}};
        for(int k = 0; k < n; k++) q.v[k] = (LvVert) {b[k].x, b[k].y, z, paint(f, flat, b[k].x, b[k].y)};
        push_poly(m, &q);
      }
  }
}

// --- 4. Solid ------------------------------------------------------------------------------
// Each corner's way out (the bisector, long enough that the edges move by 1).
static void offsets(const P2* v, int n, P2* off) {
  for(int i = 0; i < n; i++) {
    P2 a = v[(i + n - 1) % n], b = v[i], c = v[(i + 1) % n];
    P2 e1 = sub2(b, a), e2 = sub2(c, b);
    float l1 = len2(e1), l2 = len2(e2);
    P2 n1 = l1 > 1e-6f ? (P2) {e1.y / l1, -e1.x / l1} : (P2) {0, 0}, n2 = l2 > 1e-6f ? (P2) {e2.y / l2, -e2.x / l2} : (P2) {0, 0};
    P2 mm = {n1.x + n2.x, n1.y + n2.y};
    float ml = len2(mm);
    if(ml < 1e-3f) {
      off[i] = n1;
      continue;
    }
    mm.x /= ml, mm.y /= ml;
    float d = mm.x * n1.x + mm.y * n1.y;
    float k = 1 / (d > 0.4f ? d : 0.4f);
    off[i] = (P2) {mm.x * k, mm.y * k};
  }
}

static void quad(LvMesh* m, int mat, LvVert a, LvVert b, LvVert c, LvVert d, float nx, float ny, float nz) {
  float l = sqrtf(nx * nx + ny * ny + nz * nz);
  LvPoly q = {(uint8_t) mat, 4, nx / l, ny / l, nz / l, {a, b, c, d}};
  push_poly(m, &q);
}

static void walls(LvMesh* m, const P2* v, int n, bool face, const LvStyle* st, Field* f) {
  P2* off = malloc(sizeof(P2) * (size_t) n);
  if(!off) return;
  offsets(v, n, off);
  for(int i = 0; i < n; i++) {
    int j = (i + 1) % n;
    P2 a = v[i], b = v[j], e = sub2(b, a);
    float el = len2(e);
    if(el < 1e-4f) continue;
    float ox = e.y / el, oy = -e.x / el;  // out
    if(face) {
      float bf = st->face_bevel, z0 = -st->raise;
      uint32_t ca = paint(f, 0, a.x, a.y), cb = paint(f, 0, b.x, b.y);
      quad(m, LV_M_FBEVEL, (LvVert) {a.x, a.y, z0, ca}, (LvVert) {b.x, b.y, z0, cb},
           (LvVert) {b.x + off[j].x * bf, b.y + off[j].y * bf, 0, cb}, (LvVert) {a.x + off[i].x * bf, a.y + off[i].y * bf, 0, ca},
           ox * st->raise, oy * st->raise, -bf);
    } else {
      float bs = st->side_bevel, zl = st->depth * st->line_at, zd = st->depth;
      P2 A = {a.x + off[i].x * bs, a.y + off[i].y * bs}, B = {b.x + off[j].x * bs, b.y + off[j].y * bs};
      quad(m, LV_M_RBEVEL, (LvVert) {a.x, a.y, 0, st->rim}, (LvVert) {b.x, b.y, 0, st->rim}, (LvVert) {B.x, B.y, bs, st->rim},
           (LvVert) {A.x, A.y, bs, st->rim}, ox, oy, -1);
      quad(m, LV_M_SIDE, (LvVert) {A.x, A.y, bs, st->side_front}, (LvVert) {B.x, B.y, bs, st->side_front},
           (LvVert) {B.x, B.y, zl, st->side_back}, (LvVert) {A.x, A.y, zl, st->side_back}, ox, oy, 0);
      quad(m, LV_M_LINE, (LvVert) {A.x, A.y, zl, st->line}, (LvVert) {B.x, B.y, zl, st->line}, (LvVert) {B.x, B.y, zd, st->line},
           (LvVert) {A.x, A.y, zd, st->line}, ox, oy, 0);
    }
  }
  free(off);
}

// --- The build ---------------------------------------------------------------------------
static uint64_t key_of(const uint8_t* cls, const uint32_t* col, int W, int H, const LvStyle* st) {
  uint64_t h = 0xcbf29ce484222325ull;
#define MIX(v) h = (h ^ (uint64_t) (v)) * 0x100000001b3ull
  MIX(W), MIX(H);
  float fs[8] = {st->raise, st->depth, st->face_bevel, st->side_bevel, st->line_at, st->cell, st->tol, 0};
  for(int i = 0; i < 7; i++) {
    uint32_t u;
    memcpy(&u, &fs[i], 4);
    MIX(u);
  }
  MIX(st->blur_x), MIX(st->blur_y), MIX(st->straighten), MIX(st->rim), MIX(st->side_front), MIX(st->side_back), MIX(st->line);
  for(int i = 0; i < W * H; i++) {
    MIX(cls[i]);
    if(cls[i] == LV_FACE || cls[i] == LV_HI) MIX(col[i]);
  }
#undef MIX
  return h;
}

void lv_free(LvMesh* m) {
  free(m->p);
  memset(m, 0, sizeof(*m));
}

static void build_grid(LvMesh* m, const uint8_t* cls, const uint32_t* col, int W, int H, const LvStyle* st) {
  double t0 = now_ms();
  // (scratch kept between builds: fresh pages each frame cost more than the work)
  static uint8_t* mask;
  static uint32_t* sum;
  static float* img;
  static int scap;
  if(W * H > scap) {
    free(mask), free(sum), free(img);
    scap = W * H;
    mask = malloc((size_t) scap);
    sum = malloc(sizeof(uint32_t) * 4 * (size_t) (W + 1) * (H + 1) + sizeof(uint32_t) * 4 * (size_t) (W + H + 2));
    img = malloc(sizeof(float) * 4 * (size_t) scap);
    if(!mask || !sum || !img) {
      free(mask), free(sum), free(img);
      mask = NULL, sum = NULL, img = NULL, scap = 0;
      return;
    }
  }
  memset(sum, 0, sizeof(uint32_t) * 4 * (size_t) (W + 1) * (H + 1));
  memset(img, 0, sizeof(float) * 4 * (size_t) (W * H));
  Field f = {sum, W, H, st->blur_x, st->blur_y, img};
  int S = W + 1;
  for(int y = 1; y <= H; y++)
    for(int x = 1; x <= W; x++) {
      uint8_t k = cls[(y - 1) * W + x - 1];
      uint32_t c = col[(y - 1) * W + x - 1];
      bool in = k == LV_FACE || k == LV_HI;
      uint32_t add[4] = {in ? c >> 16 & 255 : 0, in ? c >> 8 & 255 : 0, in ? c & 255 : 0, in};
      for(int j = 0; j < 4; j++) f.sum[(y * S + x) * 4 + j] = add[j] + f.sum[((y - 1) * S + x) * 4 + j] + f.sum[(y * S + x - 1) * 4 + j] - f.sum[((y - 1) * S + x - 1) * 4 + j];
    }
  double Ta = now_ms();
  // The paths of both masks.
  Path* ps = NULL;
  int np = 0, cp = 0;
  for(int mk = 0; mk < 2; mk++) {
    for(int i = 0; i < W * H; i++) mask[i] = mk ? cls[i] != LV_NONE : cls[i] == LV_FACE || cls[i] == LV_HI;
    // (the outline's top and bottom edges closed over nicks a pixel or two wide: the
    // original's drop lines start there, the edge itself is straight)
    for(int y = 1; mk && y < H - 1; y++)
      for(int x = 2; x < W - 2; x++) {
        const uint8_t* q = &mask[y * W + x];
        if(q[0] || (q[-W] && q[W]) || (!q[-W] && !q[W])) continue;
        // (on an edge: the pixels either side are on the same edge, not inside a letter)
        int out = q[W] ? -W : W;
        int l = q[-1] ? -1 : -2, r = q[1] ? 1 : 2;
        if(!q[l] || !q[r] || r - l > 3) continue;  // (a nick two pixels wide at most)
        if(q[l + out] || q[r + out]) continue;
        mask[y * W + x] = 2;
      }
    for(int i = 0; mk && i < W * H; i++)
      if(mask[i] == 2) mask[i] = 1;
    // (and a one-pixel line of sky between two pieces of outline, one over the other, closed:
    // the original's thin seam, which the bevels would only leave as dots; a slot inside a
    // shape, not reached from outside, stays)
    if(mk) {
      static int* stack;
      static uint8_t* outside;
      static int cap;
      if(W * H > cap) {
        free(stack), free(outside);
        cap = W * H;
        stack = malloc(sizeof(int) * (size_t) cap), outside = malloc((size_t) cap);
        if(!stack || !outside) free(stack), free(outside), stack = NULL, outside = NULL, cap = 0;
      }
      if(stack && outside) {
        memset(outside, 0, (size_t) (W * H));
        int sp = 0;
        for(int x = 0; x < W; x++) stack[sp++] = x, stack[sp++] = (H - 1) * W + x;
        for(int y = 1; y < H - 1; y++) stack[sp++] = y * W, stack[sp++] = y * W + W - 1;
        while(sp > 0) {
          int i = stack[--sp];
          if(outside[i] || mask[i]) continue;
          outside[i] = 1;
          int x = i % W, y = i / W;
          if(x > 0 && sp < cap) stack[sp++] = i - 1;
          if(x < W - 1 && sp < cap) stack[sp++] = i + 1;
          if(y > 0 && sp < cap) stack[sp++] = i - W;
          if(y < H - 1 && sp < cap) stack[sp++] = i + W;
        }
        // (only along a seam between two pieces: a run of such pixels at least 8 long, the
        // pieces over and under it not one; a slot cut into one piece stays)
        static int* label;
        static int lcap;
        if(W * H > lcap) {
          free(label);
          lcap = W * H;
          label = malloc(sizeof(int) * (size_t) lcap);
          if(!label) lcap = 0;
        }
        if(label) {
          memset(label, 0, sizeof(int) * (size_t) (W * H));
          int nl = 0;
          for(int i0 = 0; i0 < W * H; i0++) {
            if(!mask[i0] || label[i0]) continue;
            nl++;
            sp = 0;
            stack[sp++] = i0;
            label[i0] = nl;
            while(sp > 0) {
              int i = stack[--sp], x = i % W, y = i / W;
              int nb[4] = {x > 0 ? i - 1 : -1, x < W - 1 ? i + 1 : -1, y > 0 ? i - W : -1, y < H - 1 ? i + W : -1};
              for(int k = 0; k < 4; k++)
                if(nb[k] >= 0 && mask[nb[k]] && !label[nb[k]] && sp < cap) label[nb[k]] = nl, stack[sp++] = nb[k];
            }
          }
        }
        // (each piece's first and last rows: a seam is under one piece's last and over the
        // other's first)
        static int ytop[4096], ybot[4096];
        for(int k = 0; k < 4096; k++) ytop[k] = 1 << 30, ybot[k] = -1;
        for(int i = 0; label && i < W * H; i++)
          if(label[i] > 0 && label[i] < 4096) ytop[label[i]] = ytop[label[i]] < i / W ? ytop[label[i]] : i / W, ybot[label[i]] = ybot[label[i]] > i / W ? ybot[label[i]] : i / W;
        for(int y = 1; label && y < H - 1; y++)
          for(int x = 0; x < W;) {
            int e = x;
            while(e < W && outside[y * W + e] && mask[(y - 1) * W + e] && mask[(y + 1) * W + e]) e++;
            int mid = y * W + (x + e) / 2;
            int la = label[mid - W], lb = label[mid + W];
            if(e - x >= 8 && la != lb && la < 4096 && lb < 4096 && ybot[la] == y - 1 && ytop[lb] == y + 1) {
              for(int k = x; k < e; k++) mask[y * W + k] = 1;
              if(getenv("TG_LV_DUMP")) fprintf(stderr, "seam %d: %d-%d\n", y, x, e);
            }
            x = e > x ? e : x + 1;
          }
      }
    }
    // (the outline's spurs pruned: stray pixels touching it would trace as thorns)
    for(int pass = 0, cut = 1; mk && pass < 6 && cut; pass++) {
      cut = 0;
      for(int y = 0; y < H; y++)
        for(int x = 0; x < W; x++) {
          if(cls[y * W + x] != LV_RIM || !mask[y * W + x]) continue;
          int nb = (x > 0 && mask[y * W + x - 1]) + (x < W - 1 && mask[y * W + x + 1]) + (y > 0 && mask[(y - 1) * W + x]) + (y < H - 1 && mask[(y + 1) * W + x]);
          if(nb <= 1) mask[y * W + x] = 0, cut = 1;
        }
    }
    if(getenv("TG_LV_MASK") && W > 150)
      for(int y = 0; y < H; y++) {
        fprintf(stderr, "mask%d %3d ", mk, y);
        for(int x = 0; x < W && x < 140; x++) fputc(mask[y * W + x] ? (cls[y * W + x] ? '#' : '+') : '.', stderr);
        fputc('\n', stderr);
      }
    Chain* ch = NULL;
    int nch = trace_mask(mask, W, H, &ch);
    for(int c = 0; c < nch; c++) {
      P2* pts = malloc(sizeof(P2) * (size_t) ch[c].n);
      float a = 0;
      for(int i = 0; i < ch[c].n; i++) {
        int j = (i + 1) % ch[c].n;
        float x0 = (float) ch[c].xy[2 * i], y0 = (float) ch[c].xy[2 * i + 1], x1 = (float) ch[c].xy[2 * j], y1 = (float) ch[c].xy[2 * j + 1];
        a += x0 * y1 - x1 * y0;
        if(pts) pts[i] = (P2) {(x0 + x1) / 2, (y0 + y1) / 2};
      }
      a /= 2;
      free(ch[c].xy);
      // (specks dropped: a few stray pixels of the picture's)
      if(!pts || ch[c].n < 6 || (a > 0 && a < 4) || (a < 0 && a > (mk ? -6 : -3))) {
        free(pts);
        continue;
      }
      Path p = {pts, ch[c].n, a, NULL, 0, 0, NULL, 0, 0, -1, mk, false};
      fit_path(&p, fminf(st->tol, 0.3f * 2 * fabsf(a) / p.ns + 0.15f));  // (a thin shape, a slot: held closer)
      PUSH(ps, np, cp, p);
    }
    free(ch);
  }
  double Tb = now_ms();
  // The italic slant: the angle most of the slanted lines have (by length).
  float slant = -1;
  if(st->straighten) {
    float hist[181] = {0};
    for(int i = 0; i < np; i++)
      for(int k = 0; k < ps[i].nl; k++) {
        float a = line_angle(&ps[i].ln[k]);
        if(ps[i].ln[k].len >= 3 && a > 95 && a < 150) hist[(int) a] += ps[i].ln[k].len;
      }
    int bi = 0;
    float bw = 0;
    for(int a = 97; a < 148; a++) {
      float w = hist[a - 2] * 0.5f + hist[a - 1] + hist[a] + hist[a + 1] + hist[a + 2] * 0.5f;
      if(w > bw) bw = w, bi = a;
    }
    if(bw > 0) {
      float sw = 0, sa = 0;
      for(int i = 0; i < np; i++)
        for(int k = 0; k < ps[i].nl; k++) {
          float a = line_angle(&ps[i].ln[k]);
          if(ps[i].ln[k].len >= 3 && fabsf(a - bi) <= 4) sa += a * ps[i].ln[k].len, sw += ps[i].ln[k].len;
        }
      slant = sa / sw;
    }
    for(int i = 0; i < np; i++) {
      float snapped[512];
      if(ps[i].nl > 512) continue;
      for(int k = 0; k < ps[i].nl; k++) snapped[k] = ps[i].ln[k].len < 1.4f ? -1 : snap_line(&ps[i].ln[k], ps[i].s, ps[i].ns, slant);
      // Neighbours turned onto the same angle: one line, when its samples allow it (a corner
      // the pixels drew in two steps).
      for(int k = 0; k + 1 < ps[i].nl && ps[i].nl > 3; k++) {
        if(snapped[k] < 0 || snapped[k] != snapped[k + 1]) continue;
        Line* A = &ps[i].ln[k];
        float off, dev = angle_dev(ps[i].s, ps[i].ns, A->a, ps[i].ln[k + 1].b, snapped[k], &off);
        if(dev > 0.6f) continue;
        A->b = ps[i].ln[k + 1].b;
        P2 pa = ps[i].s[A->a % ps[i].ns], pb = ps[i].s[A->b % ps[i].ns];
        A->cx = (pa.x + pb.x) / 2, A->cy = (pa.y + pb.y) / 2, A->len = len2(sub2(pb, pa));
        set_line(A, snapped[k], off);
        memmove(&ps[i].ln[k + 1], &ps[i].ln[k + 2], sizeof(Line) * (size_t) (ps[i].nl - k - 2));
        memmove(&snapped[k + 1], &snapped[k + 2], sizeof(float) * (size_t) (ps[i].nl - k - 2));
        ps[i].nl--;
        k--;
      }
    }
  }
  for(int i = 0; i < np; i++) {
    corners(&ps[i]);
    if(self_crossing(ps[i].v, ps[i].nv)) {  // (lines that meet badly: the pieces' own ends instead)
      ps[i].nv = 0;
      for(int k = 0; k < ps[i].nl; k++) PUSH(ps[i].v, ps[i].nv, ps[i].cv, ps[i].s[ps[i].ln[k].a % ps[i].ns]);
    }
    for(int it = 0; it < 1; it++) refine(&ps[i], st->straighten ? slant : -2);
    // A corner drawn by the pixels as a gentle bend (two short sides turning a little, the
    // shape's outside): one straight chamfer instead.
    for(bool again = true; again && ps[i].nv > 4;) {
      again = false;
      for(int k = 0; k < ps[i].nv && ps[i].nv > 4; k++) {
        int n = ps[i].nv;
        P2 a = ps[i].v[(k + n - 1) % n], b = ps[i].v[k], c = ps[i].v[(k + 1) % n];
        P2 e1 = sub2(b, a), e2 = sub2(c, b);
        float l1 = len2(e1), l2 = len2(e2);
        if(l1 < 1e-4f || l2 < 1e-4f || l1 > 13 || l2 > 13) continue;
        float cr = cross2(e1, e2) / (l1 * l2), dt = (e1.x * e2.x + e1.y * e2.y) / (l1 * l2);
        if(cr <= 0 || dt < 0.81f) continue;  // (convex, under 36 degrees)
        // only between sides that turn the same way (a bend, not a step)
        P2 z = ps[i].v[(k + n - 2) % n], d = ps[i].v[(k + 2) % n];
        if(cross2(sub2(a, z), e1) <= 0 || cross2(e2, sub2(d, c)) <= 0) continue;
        memmove(&ps[i].v[k], &ps[i].v[k + 1], sizeof(P2) * (size_t) (n - k - 1));
        ps[i].nv--;
        again = true;
      }
    }
    star_fix(&ps[i]);
    // (corners on top of each other, or on the line between their neighbours: one less)
    for(bool again = true; again && ps[i].nv > 3;) {
      again = false;
      for(int k = 0; k < ps[i].nv && ps[i].nv > 3; k++) {
        int n = ps[i].nv;
        P2 a = ps[i].v[(k + n - 1) % n], b = ps[i].v[k], c = ps[i].v[(k + 1) % n];
        float l1 = len2(sub2(b, a)), l2 = len2(sub2(c, b));
        bool dup = l2 < 0.05f, straight = l1 > 1e-4f && l2 > 1e-4f && fabsf(cross2(sub2(b, a), sub2(c, b))) / (l1 * l2) < 0.01f &&
                                          (b.x - a.x) * (c.x - b.x) + (b.y - a.y) * (c.y - b.y) > 0;
        if(!dup && !straight) continue;
        memmove(&ps[i].v[k], &ps[i].v[k + 1], sizeof(P2) * (size_t) (n - k - 1));
        ps[i].nv--;
        again = true;
      }
    }
    float a = ring_area(ps[i].v, ps[i].nv);
    if(ps[i].nv < 3 || (a > 0) != (ps[i].area > 0)) ps[i].nv = 0;  // (turned inside out: too small to keep)
    m->corners += ps[i].nv;
    if(getenv("TG_LV_DUMP")) {
      fprintf(stderr, "path %d mask %d area %.1f lines %d:", i, ps[i].mask, ps[i].area, ps[i].nl);
      for(int k = 0; k < ps[i].nv; k++) fprintf(stderr, " (%.2f,%.2f)", ps[i].v[k].x, ps[i].v[k].y);
      fprintf(stderr, "\n");
      if(getenv("TG_LV_DUMP")[0] == '2')
        for(int k = 0; k < ps[i].nl; k++) {
          fprintf(stderr, "   line %d: %.1f deg len %.2f samples %d-%d:", k, line_angle(&ps[i].ln[k]), ps[i].ln[k].len, ps[i].ln[k].a, ps[i].ln[k].b);
          for(int j = ps[i].ln[k].a; j <= ps[i].ln[k].b; j++) fprintf(stderr, " %.1f,%.1f", ps[i].s[j % ps[i].ns].x, ps[i].s[j % ps[i].ns].y);
          fprintf(stderr, "\n");
        }
    }
  }
  double Tc = now_ms();
  // A star's outline: the star itself pushed out by the outline's width (its sides parallel,
  // its points sharp), not traced on its own.
  for(int i = 0; i < np; i++) {
    if(!ps[i].star || ps[i].mask != 0) continue;
    P2 c = {0, 0};
    float per = 0, af = ring_area(ps[i].v, 10);
    for(int k = 0; k < 10; k++) c.x += ps[i].v[k].x / 10, c.y += ps[i].v[k].y / 10, per += len2(sub2(ps[i].v[(k + 1) % 10], ps[i].v[k]));
    int best = -1;
    float ba = 1e30f;
    for(int j = 0; j < np; j++) {
      if(ps[j].mask != 1 || ps[j].area <= 0 || ps[j].nv < 3 || ps[j].area >= ba || ps[j].area > af * 3) continue;
      bool in = false;
      for(int k = 0, l = ps[j].nv - 1; k < ps[j].nv; l = k++) {
        P2 a = ps[j].v[k], b = ps[j].v[l];
        if((a.y > c.y) != (b.y > c.y) && c.x < a.x + (c.y - a.y) * (b.x - a.x) / (b.y - a.y)) in = !in;
      }
      if(in) ba = ps[j].area, best = j;
    }
    if(best < 0) continue;
    float w = (ring_area(ps[best].v, ps[best].nv) - af) / per;
    if(w <= 0.2f || w > 4) continue;
    P2 out[10];
    for(int k = 0; k < 10; k++) {  // each corner: where its two sides, pushed out by w, meet
      P2 a = ps[i].v[(k + 9) % 10], b = ps[i].v[k], d = ps[i].v[(k + 1) % 10];
      P2 e1 = sub2(b, a), e2 = sub2(d, b);
      float l1 = len2(e1), l2 = len2(e2);
      P2 n1 = {e1.y / l1, -e1.x / l1}, n2 = {e2.y / l2, -e2.x / l2};
      P2 p1 = {a.x + n1.x * w, a.y + n1.y * w}, p2 = {b.x + n2.x * w, b.y + n2.y * w};
      float den = cross2(e1, e2);
      float t = cross2(sub2(p2, p1), e2) / den;
      out[k] = (P2) {p1.x + e1.x * t, p1.y + e1.y * t};
    }
    ps[best].nv = 0;
    for(int k = 0; k < 10; k++) PUSH(ps[best].v, ps[best].nv, ps[best].cv, out[k]);
  }
  // Holes to their outer paths: the smallest of the same mask round them.
  for(int i = 0; i < np; i++) {
    if(ps[i].area >= 0 || !ps[i].nv) continue;
    float best = 1e30f;
    P2 q = ps[i].s[0];
    for(int j = 0; j < np; j++) {
      if(ps[j].area <= 0 || ps[j].mask != ps[i].mask || !ps[j].nv || ps[j].area >= best) continue;
      bool in = false;
      for(int k = 0, l = ps[j].nv - 1; k < ps[j].nv; l = k++) {
        P2 a = ps[j].v[k], b = ps[j].v[l];
        if((a.y > q.y) != (b.y > q.y) && q.x < a.x + (q.y - a.y) * (b.x - a.x) / (b.y - a.y)) in = !in;
      }
      if(in) best = ps[j].area, ps[i].outer = j;
    }
  }
  double Td = now_ms();
  // Each shape filled, its walls.
  Tri2* tris = NULL;
  int nt = 0, ct = 0;
  for(int i = 0; i < np; i++) {
    if(ps[i].area <= 0 || !ps[i].nv) continue;
    m->shapes++;
    Ring r = {0};
    for(int k = 0; k < ps[i].nv; k++) PUSH(r.p, r.n, r.cap, ps[i].v[k]);
    // holes, the rightmost first
    for(;;) {
      int hb = -1;
      float hx = -1e30f;
      for(int j = 0; j < np; j++) {
        if(ps[j].outer != i || !ps[j].nv) continue;
        float mx = -1e30f;
        for(int k = 0; k < ps[j].nv; k++) mx = fmaxf(mx, ps[j].v[k].x);
        if(mx > hx) hx = mx, hb = j;
      }
      if(hb < 0) break;
      bridge(&r, ps[hb].v, ps[hb].nv);
      ps[hb].outer = -2;  // (done)
    }
    nt = 0;
    earclip(&r, &tris, &nt, &ct);
    free(r.p);
    bool face = ps[i].mask == 0;
    front(m, tris, nt, face ? -st->raise : 0, face ? LV_M_FACE : LV_M_RIM, face ? &f : NULL, st->rim, face ? st->cell : st->cell * 2.5f);
  }
  for(int i = 0; i < np; i++)
    if(ps[i].nv && (ps[i].area > 0 || ps[i].outer == -2)) walls(m, ps[i].v, ps[i].nv, ps[i].mask == 0, st, &f);
  double Te = now_ms();
  free(tris);
  for(int i = 0; i < np; i++) free(ps[i].s), free(ps[i].ln), free(ps[i].v);
  free(ps);

  if(getenv("TG_LV_STATS") && m->builds % 50 == 1) fprintf(stderr, "lv stages: prep %.2f trace+fit %.2f snap+refine %.2f holes %.2f fill %.2f\n", Ta - t0, Tb - Ta, Tc - Tb, Td - Tc, Te - Td);
}

bool lv_build(LvMesh* m, const uint8_t* cls, const uint32_t* col, int W, int H, const LvStyle* st) {
  uint64_t key = key_of(cls, col, W, H, st);
  if(m->valid && m->key == key) return false;
  double t0 = now_ms();
  m->np = 0, m->key = key, m->valid = true, m->shapes = 0, m->corners = 0, m->builds++;
  // Only the box round what is there (and a margin), its own grid.
  int x0 = W, x1 = -1, y0 = H, y1 = -1;
  for(int y = 0; y < H; y++)
    for(int x = 0; x < W; x++)
      if(cls[y * W + x]) x0 = x < x0 ? x : x0, x1 = x > x1 ? x : x1, y0 = y < y0 ? y : y0, y1 = y > y1 ? y : y1;
  if(x1 >= 0) {
    x0 = x0 - 2 < 0 ? 0 : x0 - 2, y0 = y0 - 2 < 0 ? 0 : y0 - 2, x1 = x1 + 2 >= W ? W - 1 : x1 + 2, y1 = y1 + 2 >= H ? H - 1 : y1 + 2;
    int w = x1 - x0 + 1, h = y1 - y0 + 1;
    static uint8_t* c2;
    static uint32_t* k2;
    static int cap2;
    if(w * h > cap2) {
      free(c2), free(k2);
      cap2 = w * h;
      c2 = malloc((size_t) cap2), k2 = malloc(sizeof(uint32_t) * (size_t) cap2);
      if(!c2 || !k2) free(c2), free(k2), c2 = NULL, k2 = NULL, cap2 = 0;
    }
    if(c2 && k2) {
      for(int y = 0; y < h; y++) {
        memcpy(&c2[y * w], &cls[(y + y0) * W + x0], (size_t) w);
        memcpy(&k2[y * w], &col[(y + y0) * W + x0], sizeof(uint32_t) * (size_t) w);
      }
      build_grid(m, c2, k2, w, h, st);
      for(int i = 0; i < m->np; i++)
        for(int k = 0; k < m->p[i].n; k++) m->p[i].v[k].x += (float) x0, m->p[i].v[k].y += (float) y0;
    }
  }
  m->ms = now_ms() - t0;
  return true;
}

// --- The publisher's splash ----------------------------------------------------------------
// Its logo: letters in a chrome gradient (blue to white, row by row), their darker blue parts
// (the shading painted in) the outline, the white line under it a face of its own; the dark
// drop under the white line left out (the solid's depth takes its place).
void lv_splash_capture(LvSplash* s, const uint32_t* raw) {
  int mx = 0;
  for(int i = 0; i < 224 * 256; i++) {
    uint32_t c = raw[i];
    int r = c >> 16 & 255, g = c >> 8 & 255, b = c & 255;
    int m = r > g ? r : g;
    m = m > b ? m : b;
    mx = m > mx ? m : mx;
  }
  s->bright = mx / 255.0f;
  // The word: neutral (white to grey) pixels under the blue logo; its letters by columns.
  int ly1 = -1, wx0 = 256, wx1 = -1, wy0 = 224, wy1 = -1, wmax = 0;
  for(int y = 0; y < 224; y++)
    for(int x = 0; x < 256; x++) {
      uint32_t c = raw[y * 256 + x];
      int r = c >> 16 & 255, b = c & 255;
      if(b > 16 && b > r + r / 3 + 8 && b * 255 >= 0x70 * mx) ly1 = y;  // (the logo's blue, not the word's dark drop)
    }
  static uint8_t isword[224 * 256];
  memset(isword, 0, sizeof(isword));
  for(int y = ly1 + 2; ly1 >= 0 && y < 224; y++)
    for(int x = 0; x < 256; x++) {
      uint32_t c = raw[y * 256 + x];
      int r = c >> 16 & 255, g = c >> 8 & 255, b = c & 255;
      if(r < 6 || r * 5 < b * 4 || g * 5 < b * 4) continue;
      isword[y * 256 + x] = 1;
      wx0 = x < wx0 ? x : wx0, wx1 = x > wx1 ? x : wx1, wy0 = y < wy0 ? y : wy0, wy1 = y > wy1 ? y : wy1;
      wmax = r > wmax ? r : wmax;
    }
  s->word_bright = wmax / 255.0f;
  if(wx1 >= 0) {
    float box[8][4];
    int n = 0;
    for(int x = wx0; x <= wx1 + 1 && n <= 8; x++) {
      bool col = false;
      for(int y = wy0; y <= wy1 && x <= wx1; y++) col |= isword[y * 256 + x];
      if(col && (n == 0 || box[n - 1][2] >= 0)) {
        if(n == 8) {
          n = 9;
          break;
        }
        box[n][0] = (float) x, box[n][1] = 1e9f, box[n][2] = -1, box[n][3] = -1e9f, n++;
      }
      if(col)
        for(int y = wy0; y <= wy1; y++)
          if(isword[y * 256 + x]) box[n - 1][1] = fminf(box[n - 1][1], (float) y), box[n - 1][3] = fmaxf(box[n - 1][3], (float) y + 1);
      if(!col && n && box[n - 1][2] < 0) box[n - 1][2] = (float) x;
    }
    if(n == 8 && box[7][2] >= 0) {  // (all of its letters seen: kept, the fade does not change them)
      memcpy(s->word, box, sizeof(box));
      s->nword = 8;
    }
  }
  if(mx < 32) return;  // too dark to tell its parts: as last seen
  uint32_t rr = 0, rg = 0, rb = 0, rn = 0;
  int x0 = 256, x1 = -1, y0 = 224, y1 = -1;
  float k = 255.0f / mx;
  for(int y = 0; y < 224; y++)
    for(int x = 0; x < 256; x++) {
      uint32_t c = raw[y * 256 + x];
      int r = (int) ((c >> 16 & 255) * k), g = (int) ((c >> 8 & 255) * k), b = (int) ((c & 255) * k);
      r = r > 255 ? 255 : r, g = g > 255 ? 255 : g, b = b > 255 ? 255 : b;
      uint8_t cl = LV_NONE;
      if((b >= 0x70 || r >= 0x80) && !isword[y * 256 + x]) cl = r >= 0x20 ? LV_FACE : LV_RIM;  // (the word apart)
      s->cls[y * 256 + x] = cl;
      s->col[y * 256 + x] = 0xFF000000u | (uint32_t) r << 16 | (uint32_t) g << 8 | (uint32_t) b;
      if(cl == LV_RIM) rr += r, rg += g, rb += b, rn++;
      if(cl) x0 = x < x0 ? x : x0, x1 = x > x1 ? x : x1, y0 = y < y0 ? y : y0, y1 = y > y1 ? y : y1;
    }
  // The face opened (a pixel of it stays in a 2 x 2 block of it): the one-pixel slivers of
  // the painted shading at the letters' feet go to the outline.
  static uint8_t keep[224 * 256];
  memset(keep, 0, sizeof(keep));
  for(int y = 0; y < 223; y++)
    for(int x = 0; x < 255; x++) {
      const uint8_t* c = &s->cls[y * 256 + x];
      if(c[0] == LV_FACE && c[1] == LV_FACE && c[256] == LV_FACE && c[257] == LV_FACE) keep[y * 256 + x] = keep[y * 256 + x + 1] = keep[(y + 1) * 256 + x] = keep[(y + 1) * 256 + x + 1] = 1;
    }
  for(int i = 0; i < 224 * 256; i++)
    if(s->cls[i] == LV_FACE && !keep[i]) s->cls[i] = LV_RIM;
  // The letters' top edge (their first three rows) is the chrome's dark top, not shading: face.
  int top = -1;
  for(int i = 0; i < 224 * 256 && top < 0; i++)
    if(s->cls[i]) top = i / 256;
  // (but not a line of shading that goes on down: the thin dark line between two letters)
  for(int x = 0; top >= 0 && top + 7 < 224 && x < 256; x++) {
    const uint8_t* c = &s->cls[(top + 3) * 256];
    int l = x, r = x;
    while(l > 0 && c[l - 1] == LV_RIM) l--;
    while(r < 255 && c[r + 1] == LV_RIM) r++;
    bool line = c[x] == LV_RIM && r - l < 2;  // (thin, and going on down)
    for(int y = top + 4; line && y < top + 7; y++) line = s->cls[y * 256 + x] == LV_RIM || (x > 0 && s->cls[y * 256 + x - 1] == LV_RIM) || (x < 255 && s->cls[y * 256 + x + 1] == LV_RIM);
    if(line) continue;
    for(int y = top; y < top + 3; y++)
      if(s->cls[y * 256 + x] == LV_RIM) s->cls[y * 256 + x] = LV_FACE;
  }
  s->valid = x1 >= 0;
  if(!s->valid) return;
  s->cx = (x0 + x1 + 1) / 2.0f, s->cy = (y0 + y1 + 1) / 2.0f;
  s->rim = rn ? 0xFF000000u | (rr / rn) << 16 | (rg / rn) << 8 | (rb / rn) : 0xFF101090;
}

// --- The word from a font ---------------------------------------------------------------
bool lv_build_word(LvMesh* m, const LvSplash* s, const LvStyle* st, uint32_t top, uint32_t bottom) {
  const char* word = "PRESENTS";
  uint64_t key = 0xcbf29ce484222325ull;
  const uint8_t* kb = (const uint8_t*) s->word;
  for(size_t i = 0; i < sizeof(s->word); i++) key = (key ^ kb[i]) * 0x100000001b3ull;
  key = (key ^ top) * 0x100000001b3ull, key = (key ^ bottom) * 0x100000001b3ull;
  if(s->nword != 8) return false;
  if(m->valid && m->key == key) return false;
  double t0 = now_ms();
  m->np = 0, m->key = key, m->valid = true, m->shapes = 0, m->corners = 0, m->builds++;
  Grad gr = {1e9f, -1e9f, top, bottom};
  for(int l = 0; l < 8; l++) gr.y0 = fminf(gr.y0, s->word[l][1]), gr.y1 = fmaxf(gr.y1, s->word[l][3]);
  g_grad = &gr;
  Tri2* tris = NULL;
  int nt = 0, ct = 0;
  for(int l = 0; l < 8; l++) {
    int gi = -1;
    for(int k = 0; k < (int) (sizeof(kLogoGlyphs) / sizeof(kLogoGlyphs[0])); k++)
      if(kLogoGlyphs[k].ch == word[l]) gi = k;
    if(gi < 0) continue;
    const float* b = s->word[l];
    int nc = kLogoGlyphs[gi].n;
    P2 c[4][96];
    int cn[4];
    float ar[4];
    for(int k = 0; k < nc; k++) {
      cn[k] = kLogoGlyphs[gi].count[k] > 96 ? 96 : kLogoGlyphs[gi].count[k];
      for(int j = 0; j < cn[k]; j++) {
        const float* q = &kLogoGlyphPts[(kLogoGlyphs[gi].first[k] + j) * 2];
        c[k][j] = (P2) {b[0] + q[0] * (b[2] - b[0]), b[1] + q[1] * (b[3] - b[1])};
      }
    }
    // Outer contours (in no other) clockwise on the screen, holes the other way.
    bool hole[4];
    for(int k = 0; k < nc; k++) {
      hole[k] = false;
      for(int o = 0; o < nc; o++) {
        if(o == k) continue;
        bool in = false;
        P2 q = c[k][0];
        for(int i = 0, j = cn[o] - 1; i < cn[o]; j = i++) {
          P2 A = c[o][i], B = c[o][j];
          if((A.y > q.y) != (B.y > q.y) && q.x < A.x + (q.y - A.y) * (B.x - A.x) / (B.y - A.y)) in = !in;
        }
        hole[k] |= in;
      }
      ar[k] = ring_area(c[k], cn[k]);
      if((ar[k] > 0) == hole[k])
        for(int i = 0; i < cn[k] / 2; i++) {
          P2 t = c[k][i];
          c[k][i] = c[k][cn[k] - 1 - i], c[k][cn[k] - 1 - i] = t;
        }
    }
    for(int k = 0; k < nc; k++) {
      if(hole[k]) continue;
      m->shapes++;
      Ring r = {0};
      for(int j = 0; j < cn[k]; j++) PUSH(r.p, r.n, r.cap, c[k][j]);
      for(int h = 0; h < nc; h++)
        if(hole[h]) bridge(&r, c[h], cn[h]);
      nt = 0;
      earclip(&r, &tris, &nt, &ct);
      free(r.p);
      front(m, tris, nt, -st->raise, LV_M_FACE, NULL, top, st->cell);
    }
    for(int k = 0; k < nc; k++) {
      m->corners += cn[k];
      walls(m, c[k], cn[k], true, st, NULL);
      // the sides from the bevel's foot
      P2 off[96], foot[96];
      offsets(c[k], cn[k], off);
      for(int j = 0; j < cn[k]; j++) foot[j] = (P2) {c[k][j].x + off[j].x * st->face_bevel, c[k][j].y + off[j].y * st->face_bevel};
      walls(m, foot, cn[k], false, st, NULL);
    }
  }
  g_grad = NULL;
  free(tris);
  m->ms = now_ms() - t0;
  return true;
}
