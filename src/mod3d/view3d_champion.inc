// The championship's end: the game's MenuEndScreen after the last country ("CONGRATULATIONS
// ON COMPLETING ..."), drawn as its own modern screen. The original shows its driver on the
// podium, a garland round his neck, a fist up and champagne in the other hand, in front of a
// sponsors' wall. This is our own drawing of that moment, made of shapes here (nothing of the
// original's picture): the driver behind the top step of a podium under a waving chequered
// flag, his fist pumping, champagne spraying, confetti falling and gold light; beside him
// WORLD CHAMPION (TOUR COMPLETE when a computer driver tops the last country's standings),
// the player's name, those standings and the game's own message. It plays from when the
// screen comes up (TgView3D.champ_frames). Included by view3d.c (its 2D helpers).

static const uint32_t kChGold = 0xFFFFC94A, kChSilver = 0xFFC9D3DD, kChBronze = 0xFFD08A55, kChWhite = 0xFFF0F5F7,
                      kChCyan = 0xFF62F2DC, kChMuted = 0xFF8A9AAA, kChNavy = 0xFF14213D;

// 0-1 from an index, the same every frame (the confetti and the spray are made of these).
static float ch_rand(int i, int salt) {
  return (float) (hash32((uint32_t) i * 0x9E3779B1u + (uint32_t) salt * 0x85EBCA77u) & 0xFFFF) / 65535.0f;
}

// 0 until `start`, then easing to 1 over `len` seconds (cubic out).
static float ch_in(float t, float start, float len) {
  float x = (t - start) / len;
  x = x < 0 ? 0 : x > 1 ? 1 : x;
  return 1 - (1 - x) * (1 - x) * (1 - x);
}

// A colour with its alpha scaled by a (0-1).
static uint32_t ch_alpha(uint32_t c, float a) {
  a = a < 0 ? 0 : a > 1 ? 1 : a;
  return (c & 0xFFFFFF) | (uint32_t) ((float) (c >> 24) * a + 0.5f) << 24;
}

// A colour's red, green and blue scaled by k (shading).
static uint32_t ch_shade(uint32_t c, float k) {
  int r = (int) ((c >> 16 & 255) * k), g = (int) ((c >> 8 & 255) * k), b = (int) ((c & 255) * k);
  r = r > 255 ? 255 : r, g = g > 255 ? 255 : g, b = b > 255 ? 255 : b;
  return (c & 0xFF000000) | (uint32_t) r << 16 | (uint32_t) g << 8 | (uint32_t) b;
}

static void ch_tri(TgV3List* l, float x0, float y0, uint32_t c0, float x1, float y1, uint32_t c1, float x2, float y2, uint32_t c2) {
  TgV3Vertex t[3] = {{x0, y0, 0, 0, c0}, {x1, y1, 0, 0, c1}, {x2, y2, 0, 0, c2}};
  list_push(l, t, 3, -1);
}

// An ellipse (radii rx, ry, turned by a) as a fan: c0 at the centre, c1 at the rim.
static void ch_ellipse(TgV3List* l, float cx, float cy, float rx, float ry, float a, uint32_t c0, uint32_t c1) {
  enum { N = 28 };
  float ca = cosf(a), sa = sinf(a), px = cx + ca * rx, py = cy + sa * rx;
  for(int i = 1; i <= N; i++) {
    float e = i * 2 * PI / N, ex = cosf(e) * rx, ey = sinf(e) * ry;
    float x = cx + ex * ca - ey * sa, y = cy + ex * sa + ey * ca;
    ch_tri(l, cx, cy, c0, px, py, c1, x, y, c1);
    px = x, py = y;
  }
}

static void ch_disc(TgV3List* l, float cx, float cy, float r, uint32_t c) { ch_ellipse(l, cx, cy, r, r, 0, c, c); }

// A limb: (x0, y0) to (x1, y1), r either side, round at both ends.
static void ch_limb(TgV3List* l, float x0, float y0, float x1, float y1, float r, uint32_t c) {
  float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
  if(len > 0.01f) {
    float nx = -dy / len * r, ny = dx / len * r;
    sq(l, x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, c);
  }
  ch_disc(l, x0, y0, r, c);
  ch_disc(l, x1, y1, r, c);
}

// A soft round light (the glow picture).
static void ch_glow(TgV3List* l, float cx, float cy, float r, uint32_t c) {
  screen_rect(l, cx - r, cy - r, cx + r, cy + r, 0, 1, c, c, TEX_GLOW);
}

// A five-pointed star (r its points, turned by rot).
static void ch_star(TgV3List* l, float cx, float cy, float r, float rot, uint32_t c) {
  for(int i = 0; i < 10; i++) {
    float a0 = rot + i * PI / 5 - PI / 2, a1 = rot + (i + 1) * PI / 5 - PI / 2;
    float r0 = i & 1 ? r * 0.45f : r, r1 = i & 1 ? r : r * 0.45f;
    ch_tri(l, cx, cy, c, cx + cosf(a0) * r0, cy + sinf(a0) * r0, c, cx + cosf(a1) * r1, cy + sinf(a1) * r1, c);
  }
}

// The drawing's units: x right and y down from the driver's waist (at the podium's top), u
// pixels each.
typedef struct {
  float x, y, u;
} ChAt;

static float chx(const ChAt* a, float x) { return a->x + x * a->u; }
static float chy(const ChAt* a, float y) { return a->y + y * a->u; }

// A convex shape of n points (the drawing's units).
static void ch_shape(TgV3List* l, const ChAt* a, const float (*p)[2], int n, uint32_t c) {
  for(int i = 1; i + 1 < n; i++)
    ch_tri(l, chx(a, p[0][0]), chy(a, p[0][1]), c, chx(a, p[i][0]), chy(a, p[i][1]), c, chx(a, p[i + 1][0]), chy(a, p[i + 1][1]), c);
}

static void ch_limb_u(TgV3List* l, const ChAt* a, float x0, float y0, float x1, float y1, float r, uint32_t c) {
  ch_limb(l, chx(a, x0), chy(a, y0), chx(a, x1), chy(a, y1), r * a->u, c);
}

static void ch_ellipse_u(TgV3List* l, const ChAt* a, float cx, float cy, float rx, float ry, float rot, uint32_t c0, uint32_t c1) {
  ch_ellipse(l, chx(a, cx), chy(a, cy), rx * a->u, ry * a->u, rot, c0, c1);
}

// The chequered flag behind the driver, waving.
static void ch_flag(TgV3List* l, const ChAt* a, float t) {
  enum { NX = 14, NY = 9 };
  const float x0 = -5.6f, x1 = 5.6f, y0 = -8.4f, y1 = -0.2f;
  for(int j = 0; j < NY; j++)
    for(int i = 0; i < NX; i++) {
      float px[4], py[4], slope = 0;
      for(int k = 0; k < 4; k++) {
        float x = x0 + (x1 - x0) * (float) (i + (k == 1 || k == 2)) / NX, y = y0 + (y1 - y0) * (float) (j + (k >= 2)) / NY;
        float ph = x * 0.62f + y * 0.18f - t * 2.4f;
        px[k] = chx(a, x), py[k] = chy(a, y + sinf(ph) * 0.24f);
        slope += cosf(ph);
      }
      // Lit where the cloth turns to the light; fading out towards the top.
      float lit = 0.75f + slope / 4 * 0.25f, fade = 0.15f + 0.85f * (float) (j + 1) / NY;
      uint32_t c = (i + j) & 1 ? 0xFF0B1222 : 0xFFB8C4D2;
      sq(l, px[0], py[0], px[1], py[1], px[2], py[2], px[3], py[3], ch_alpha(ch_shade(c, lit), 0.24f * fade));
    }
}

// A podium step: its top face, its front from top to bottom, its edge in its medal's colour,
// its number.
static void ch_step(TgV3List* l, const ChAt* a, float x0, float x1, float top, float bottom, uint32_t medal, const char* n) {
  sq(l, chx(a, x0), chy(a, top), chx(a, x1), chy(a, top), chx(a, x1 - 0.3f), chy(a, top - 0.36f), chx(a, x0 + 0.3f), chy(a, top - 0.36f),
     0xFF3B4D68);
  screen_rect(l, chx(a, x0), chy(a, top), chx(a, x1), chy(a, bottom), 0, 1, 0xFF2A3A52, 0xFF111A28, -1);
  rect(l, chx(a, x0), chy(a, top), chx(a, x1), chy(a, top + 0.12f), medal);
  ch_glow(l, chx(a, (x0 + x1) / 2), chy(a, top), (x1 - x0) * 0.5f * a->u, ch_alpha(medal, 0.18f));
  float size = fminf(1.15f, (bottom - top) * 0.62f);
  ftext(l, n, chx(a, (x0 + x1) / 2), chy(a, (top + bottom) / 2 + size / 2 + 0.04f), size * a->u, medal, 0, 2, 0);
}

// The laurel round his neck: two rows of leaves on a band, a gold bow at the bottom.
static void ch_wreath(TgV3List* l, const ChAt* a, float sway) {
  const float cx = 0, cy = -3.06f, rx = 1.04f, ry = 1.08f;
  for(int i = 0; i < 16; i++) {
    float a0 = (16 + i * 148.0f / 16) * PI / 180 + sway, a1 = (16 + (i + 1) * 148.0f / 16) * PI / 180 + sway;
    bar(l, chx(a, cx + cosf(a0) * rx), chy(a, cy + sinf(a0) * ry), chx(a, cx + cosf(a1) * rx), chy(a, cy + sinf(a1) * ry), 0.16f * a->u,
        0xFF1E5E2A);
  }
  for(int i = 0; i <= 16; i++) {
    float an = (18 + i * 144.0f / 16) * PI / 180 + sway;
    float x = cx + cosf(an) * rx, y = cy + sinf(an) * ry, tang = atan2f(cosf(an) * ry, -sinf(an) * rx);
    // (both sides' leaves run down to the bow)
    if(i > 8) tang += PI;
    for(int side = -1; side <= 1; side += 2) {
      float lx = x + cosf(an) * 0.13f * side, ly = y + sinf(an) * 0.13f * side;
      uint32_t c = side > 0 ? 0xFF2C7C38 : 0xFF4DB45B;
      ch_ellipse_u(l, a, lx, ly, 0.22f, 0.085f, tang + side * 0.5f, ch_shade(c, 1.12f), ch_shade(c, 0.78f));
    }
  }
  float bx = chx(a, cx + cosf(PI / 2 + sway) * rx), by = chy(a, cy + sinf(PI / 2 + sway) * ry) + 0.04f * a->u, u = a->u;
  sq(l, bx - 0.05f * u, by, bx - 0.24f * u, by + 0.5f * u, bx - 0.12f * u, by + 0.52f * u, bx + 0.02f * u, by + 0.04f * u, 0xFFC08A1E);
  sq(l, bx + 0.05f * u, by, bx + 0.22f * u, by + 0.48f * u, bx + 0.1f * u, by + 0.5f * u, bx - 0.02f * u, by + 0.04f * u, 0xFFC08A1E);
  ch_tri(l, bx, by, kChGold, bx - 0.34f * u, by - 0.17f * u, kChGold, bx - 0.34f * u, by + 0.17f * u, 0xFFC08A1E);
  ch_tri(l, bx, by, kChGold, bx + 0.34f * u, by - 0.17f * u, kChGold, bx + 0.34f * u, by + 0.17f * u, 0xFFC08A1E);
  ch_disc(l, bx, by, 0.09f * u, 0xFFFFDE85);
}

// The champagne's spray, t seconds after the cork: droplets thrown from the mouth (mx, my)
// along the bottle (dx, dy), falling.
static void ch_spray(TgV3List* l, const ChAt* a, float mx, float my, float dx, float dy, float t) {
  enum { N = 120 };
  const float life = 1.3f, g = 8.5f;
  float u = a->u;
  for(int k = 0; k < N; k++) {
    float birth = k * life / N;
    if(t < birth) continue;
    float age = fmodf(t - birth, life), f = age / life;
    float spread = (ch_rand(k, 11) - 0.5f) * 0.6f, sp = 6.0f + 4.0f * ch_rand(k, 12);
    float vx = (dx * cosf(spread) - dy * sinf(spread)) * sp, vy = (dx * sinf(spread) + dy * cosf(spread)) * sp;
    float x = mx + vx * age, y = my + vy * age + 0.5f * g * age * age;
    float r = (0.05f + 0.07f * ch_rand(k, 13)) * (1 + 1.5f * f) * u, al = (1 - f) * (1 - f);
    uint32_t c = k % 3 == 0 ? 0xFFFFFFFF : k % 3 == 1 ? 0xFFFFF3D2 : 0xFFFFE39A;
    ch_glow(l, chx(a, x), chy(a, y), r * 2.6f, ch_alpha(c, al * 0.7f));
    ch_disc(l, chx(a, x), chy(a, y), r * 0.5f, ch_alpha(c, al));
  }
  ch_glow(l, chx(a, mx), chy(a, my), (0.7f + 0.12f * sinf(t * 19)) * u, 0xD8FFFFFF);
}

// The driver, waist up behind the top step: raise 0-1 his arm going up, beat 0-1 the fist's
// pump. Returns the champagne's mouth and direction (the drawing's units) for the spray.
static void ch_driver(TgV3List* l, const ChAt* a, float raise, float beat, float* mouth) {
  const uint32_t suit = 0xFFE9EEF4, suitShade = 0xFFB4C0CF, skin = 0xFFD9A27A, skinShade = 0xFFB27A53, glove = 0xFF1B2846;
  // Torso, its shaded side, a cyan stripe and gold piping, the zip.
  static const float kTorso[6][2] = {{-1.08f, 0.4f}, {1.08f, 0.4f}, {1.52f, -2.62f}, {1.18f, -3.12f}, {-1.18f, -3.12f}, {-1.52f, -2.62f}};
  static const float kShade[5][2] = {{-1.08f, 0.4f}, {-0.62f, 0.4f}, {-0.8f, -3.12f}, {-1.18f, -3.12f}, {-1.52f, -2.62f}};
  static const float kStripe[4][2] = {{0.62f, 0.4f}, {0.86f, 0.4f}, {1.36f, -2.6f}, {1.12f, -2.6f}};
  static const float kPiping[4][2] = {{0.86f, 0.4f}, {0.93f, 0.4f}, {1.43f, -2.6f}, {1.36f, -2.6f}};
  static const float kCollar[4][2] = {{-0.46f, -3.12f}, {0.46f, -3.12f}, {0.34f, -3.42f}, {-0.34f, -3.42f}};
  ch_shape(l, a, kTorso, 6, suit);
  ch_shape(l, a, kShade, 5, suitShade);
  ch_shape(l, a, kStripe, 4, kChCyan);
  ch_shape(l, a, kPiping, 4, kChGold);
  bar(l, chx(a, 0), chy(a, -3.1f), chx(a, 0), chy(a, 0.4f), 0.05f * a->u, 0xFFA7B3C3);
  rect(l, chx(a, 0.3f), chy(a, -1.72f), chx(a, 0.9f), chy(a, -1.3f), kChNavy);
  ch_star(l, chx(a, 0.6f), chy(a, -1.51f), 0.16f * a->u, 0, kChGold);
  ch_shape(l, a, kCollar, 4, kChNavy);
  rect(l, chx(a, -0.46f), chy(a, -3.16f), chx(a, 0.46f), chy(a, -3.1f), kChCyan);
  // The arm with the bottle (his left, on our right of him: our left).
  float s2x = -1.25f, s2y = -2.85f, e2x = -1.98f, e2y = -1.5f, hx = -0.95f, hy = -1.0f;
  ch_limb_u(l, a, s2x, s2y, e2x, e2y, 0.37f, suitShade);
  ch_limb_u(l, a, s2x + 0.04f, s2y - 0.04f, e2x + 0.04f, e2y - 0.04f, 0.3f, suit);
  ch_limb_u(l, a, e2x, e2y, hx, hy, 0.33f, suitShade);
  ch_limb_u(l, a, e2x + 0.03f, e2y - 0.05f, hx + 0.03f, hy - 0.05f, 0.27f, suit);
  // The bottle: up and out, its spray away from him.
  float dx = -0.58f, dy = -0.81f, nx = -dy, ny = dx;
  float bx = hx - dx * 0.75f, by = hy - dy * 0.75f;
  const uint32_t glass = 0xFF1F5A2E;
  ch_limb_u(l, a, bx + dx * 0.3f, by + dy * 0.3f, bx + dx * 1.2f, by + dy * 1.2f, 0.3f, glass);
  float q[4][2] = {{bx + dx * 1.2f + nx * 0.3f, by + dy * 1.2f + ny * 0.3f}, {bx + dx * 1.2f - nx * 0.3f, by + dy * 1.2f - ny * 0.3f},
                   {bx + dx * 1.62f - nx * 0.12f, by + dy * 1.62f - ny * 0.12f}, {bx + dx * 1.62f + nx * 0.12f, by + dy * 1.62f + ny * 0.12f}};
  ch_shape(l, a, (const float (*)[2]) q, 4, glass);
  ch_limb_u(l, a, bx + dx * 1.6f, by + dy * 1.6f, bx + dx * 2.15f, by + dy * 2.15f, 0.12f, glass);
  ch_limb_u(l, a, bx + dx * 1.72f, by + dy * 1.72f, bx + dx * 2.2f, by + dy * 2.2f, 0.135f, 0xFFE3B341);
  float lb[4][2] = {{bx + dx * 0.55f + nx * 0.3f, by + dy * 0.55f + ny * 0.3f}, {bx + dx * 0.55f - nx * 0.3f, by + dy * 0.55f - ny * 0.3f},
                    {bx + dx * 0.98f - nx * 0.3f, by + dy * 0.98f - ny * 0.3f}, {bx + dx * 0.98f + nx * 0.3f, by + dy * 0.98f + ny * 0.3f}};
  ch_shape(l, a, (const float (*)[2]) lb, 4, 0xFFF2E6C4);
  ch_limb_u(l, a, bx + dx * 0.35f + nx * 0.15f, by + dy * 0.35f + ny * 0.15f, bx + dx * 1.1f + nx * 0.15f, by + dy * 1.1f + ny * 0.15f,
            0.045f, 0x9068C584);
  ch_ellipse_u(l, a, hx, hy, 0.36f, 0.31f, atan2f(dy, dx), 0xFF2C3D63, glove);
  ch_ellipse_u(l, a, hx + nx * 0.2f + dx * 0.15f, hy + ny * 0.2f + dy * 0.15f, 0.16f, 0.1f, atan2f(dy, dx), 0xFF2C3D63, glove);
  mouth[0] = bx + dx * 2.25f, mouth[1] = by + dy * 2.25f, mouth[2] = dx, mouth[3] = dy;
  // The garland over his shoulders.
  ch_wreath(l, a, (beat - 0.5f) * 0.035f);
  // The raised arm (his right: our right): from hanging to up, then pumping.
  float sx = 1.25f, sy = -2.85f, phi = (1 - raise) * 2.3f + 0.08f * (1 - beat), psi = 0.28f * (1 - beat) * raise;
  float ux = 0.95f, uy = -1.25f, fx = -0.15f, fy = -1.55f;
  float ex = sx + ux * cosf(phi) - uy * sinf(phi), ey = sy + ux * sinf(phi) + uy * cosf(phi);
  float px = ex + fx * cosf(phi + psi) - fy * sinf(phi + psi), py = ey + fx * sinf(phi + psi) + fy * cosf(phi + psi);
  float wx = ex + (px - ex) * 0.78f, wy = ey + (py - ey) * 0.78f;
  ch_limb_u(l, a, sx, sy, ex, ey, 0.37f, suitShade);
  ch_limb_u(l, a, sx - 0.04f, sy - 0.04f, ex - 0.04f, ey - 0.04f, 0.3f, suit);
  ch_limb_u(l, a, ex, ey, wx, wy, 0.33f, suitShade);
  ch_limb_u(l, a, ex - 0.04f, ey - 0.03f, wx - 0.04f, wy - 0.03f, 0.27f, suit);
  ch_limb_u(l, a, wx - (px - ex) * 0.1f, wy - (py - ey) * 0.1f, wx, wy, 0.35f, kChCyan);
  float fa = atan2f(py - ey, px - ex);
  ch_ellipse_u(l, a, px, py, 0.44f, 0.4f, fa, 0xFF2C3D63, glove);
  for(int k = -1; k <= 1; k++) {
    float kx = px + cosf(fa) * 0.3f + cosf(fa + PI / 2) * 0.15f * k, ky = py + sinf(fa) * 0.3f + sinf(fa + PI / 2) * 0.15f * k;
    ch_ellipse_u(l, a, kx, ky, 0.09f, 0.07f, fa, 0xFF3A4E7A, 0xFF2C3D63);
  }
  // Neck, head, ears; the cap (its peak, a gold star); sunglasses; a grin.
  rect(l, chx(a, -0.27f), chy(a, -3.66f), chx(a, 0.27f), chy(a, -3.1f), skinShade);
  ch_ellipse_u(l, a, -0.61f, -4.12f, 0.14f, 0.16f, 0, skinShade, skinShade);
  ch_ellipse_u(l, a, 0.61f, -4.12f, 0.14f, 0.16f, 0, skinShade, skinShade);
  ch_ellipse_u(l, a, 0, -4.18f, 0.62f, 0.7f, 0, skin, skinShade);
  for(int i = 0; i < 16; i++) {  // the crown, round as a cap's
    float a0 = PI + i * PI / 16, a1 = PI + (i + 1) * PI / 16;
    ch_tri(l, chx(a, 0), chy(a, -4.44f), 0xFF22345A, chx(a, cosf(a0) * 0.65f), chy(a, -4.44f + sinf(a0) * 0.62f), 0xFF0F1A31,
           chx(a, cosf(a1) * 0.65f), chy(a, -4.44f + sinf(a1) * 0.62f), 0xFF0F1A31);
  }
  bar(l, chx(a, 0), chy(a, -5.04f), chx(a, 0), chy(a, -4.46f), 0.035f * a->u, 0xFF0B1427);  // a seam
  ch_ellipse_u(l, a, 0, -4.43f, 0.68f, 0.11f, 0, 0xFF2A3D66, 0xFF0B1427);  // the peak
  rect(l, chx(a, -0.62f), chy(a, -4.56f), chx(a, 0.62f), chy(a, -4.51f), kChCyan);
  ch_star(l, chx(a, 0), chy(a, -4.76f), 0.14f * a->u, 0, kChGold);
  for(int side = -1; side <= 1; side += 2) {
    ch_ellipse_u(l, a, 0.27f * side, -4.16f, 0.23f, 0.13f, 0, 0xFF2A3B57, 0xFF0B0F16);
    bar(l, chx(a, 0.49f * side), chy(a, -4.18f), chx(a, 0.63f * side), chy(a, -4.13f), 0.05f * a->u, 0xFF0B0F16);
    ch_ellipse_u(l, a, 0.31f * side, -4.2f, 0.07f, 0.028f, -0.3f, 0xC0FFFFFF, 0x40FFFFFF);
  }
  bar(l, chx(a, -0.07f), chy(a, -4.18f), chx(a, 0.07f), chy(a, -4.18f), 0.05f * a->u, 0xFF0B0F16);
  ch_ellipse_u(l, a, 0, -3.97f, 0.07f, 0.05f, 0, skinShade, skinShade);
  ch_ellipse_u(l, a, 0, -3.78f, 0.24f, 0.11f, 0, 0xFF7A2E2A, 0xFF5E2220);
  ch_ellipse_u(l, a, 0, -3.81f, 0.2f, 0.05f, 0, 0xFFFFFFFF, 0xFFE8E8E8);
  ch_ellipse_u(l, a, 0, -3.875f, 0.27f, 0.06f, 0, skin, skin);
}

// Confetti from index `from` to `to`, falling from the top since t = 0.55 s.
static void ch_confetti(TgV3List* l, int w, int h, float s, float t, int from, int to) {
  static const uint32_t kCol[5] = {0xFFFFC94A, 0xFF62F2DC, 0xFFF0F5F7, 0xFFFF5A48, 0xFFB98CFF};
  float since = t - 0.55f;
  if(since <= 0) return;
  for(int i = from; i < to; i++) {
    float speed = (140 + 170 * ch_rand(i, 1)) * s, y = -h * (0.05f + 1.1f * ch_rand(i, 2)) + speed * since;
    if(y > h * 1.05f) y = -h * 0.2f + fmodf(y - h * 1.05f, h * 1.25f);
    float x = w * ch_rand(i, 3) + sinf(t * (1.1f + ch_rand(i, 4)) + i) * 36 * s;
    float rot = t * (1.5f + 3 * ch_rand(i, 5)) + i, flip = cosf(t * (3 + 4 * ch_rand(i, 6)) + i * 1.7f);
    float hw = (7 + 6 * ch_rand(i, 7)) * s * (fabsf(flip) * 0.85f + 0.15f), hh = (3.5f + 2.5f * ch_rand(i, 8)) * s;
    float ca = cosf(rot), sa = sinf(rot), ox[4] = {-hw, hw, hw, -hw}, oy[4] = {-hh, -hh, hh, hh}, px[4], py[4];
    for(int k = 0; k < 4; k++) px[k] = x + ox[k] * ca - oy[k] * sa, py[k] = y + ox[k] * sa + oy[k] * ca;
    sq(l, px[0], py[0], px[1], py[1], px[2], py[2], px[3], py[3], ch_shade(kCol[i % 5], flip > 0 ? 1.0f : 0.68f));
  }
}

// Text in the black italic, a light running across it (band: its centre's x).
static void ch_shimmer(TgV3List* l, const char* str, float x, float y, float size, uint32_t col, float band) {
  char one[2] = {0, 0};
  for(const char* p = str; *p; p++) {
    one[0] = *p;
    float cw = ftext_w(one, size, 2, 0), k = 1 - fabsf(x + cw / 2 - band) / (size * 1.3f);
    ftext(l, one, x, y, size, k > 0 ? lerp_colour(col, 0xFFFFFFFF, k * 0.8f) : col, -1, 2, 0);
    x += cw;
  }
}

static void draw_champion(TgView3D* v, int w, int h, TgV3List* out) {
  float s = h / 1080.0f, t = ((float) v->champ_frames - 1 + v->subframe) / 60.0f;
  // Narrower screens than 16:9: the drawing and the text smaller.
  float ts = s * fminf(1.0f, (float) w / h / (16.0f / 9)), u = 70 * ts;
  // The standings as last seen with points (the game has cleared them by now).
  bool kept = false;
  for(int d = 0; d < 20; d++) kept |= v->champ_pts[d] != 0;
  const uint8_t* rank = kept ? v->champ_rank : v->res.rank;
  int players = v->res.two ? 2 : 1, best = 0, bestPos = 99;
  for(int p = 0; p < players; p++)
    for(int i = 0; i < 20; i++)
      if(rank[i] == p && i < bestPos) bestPos = i, best = p;
  bool champion = bestPos == 0;
  const char* name = v->res.name[best][0] ? v->res.name[best] : (best ? "PLAYER 2" : "PLAYER 1");

  // Night blue; behind the driver the light turning, a gold glow, the far confetti.
  screen_rect(out, 0, 0, (float) w, (float) h, 0, 1, 0xFF0C1630, 0xFF04060C, -1);
  out->overlay = true;
  ChAt at = {fmaxf(w * 0.27f, 78 * s + 6.3f * u), h - 285 * s, u};
  float cx = at.x, cy = chy(&at, -5.0f), rays = ch_in(t, 0.0f, 0.8f);
  for(int i = 0; i < 18; i++) {
    float a0 = i * 2 * PI / 18 + t * 0.06f, half = 0.06f, r = h * 0.95f * (0.4f + 0.6f * rays);
    ch_tri(out, cx, cy, ch_alpha(0x1EFFD27A, rays), cx + cosf(a0 - half) * r, cy + sinf(a0 - half) * r, 0x00FFD27A,
           cx + cosf(a0 + half) * r, cy + sinf(a0 + half) * r, 0x00FFD27A);
  }
  ch_glow(out, cx, cy, h * 0.75f, ch_alpha(0x50FFC94A, rays));
  ch_glow(out, w * 0.75f, h * 0.42f, h * 0.6f, ch_alpha(0x2462F2DC, rays));
  ch_confetti(out, w, h, s * 0.7f, t * 0.8f, 0, 60);

  // The drawing: the flag, the side steps, the driver, the top step in front of him, the
  // spray and the cork; rising into place.
  int from = out->nv;
  ch_flag(out, &at, t);
  ch_step(out, &at, -6.2f, -2.5f, 1.0f, 2.3f, kChSilver, "2");
  ch_step(out, &at, 2.5f, 6.2f, 1.45f, 2.3f, kChBronze, "3");
  float beat = 0.5f - 0.5f * cosf(fmaxf(0, t - 0.9f) * 2 * PI * 1.25f), mouth[4];
  ChAt body = at;
  body.y -= 0.07f * u * beat;
  ch_driver(out, &body, ch_in(t, 0.45f, 0.45f), beat, mouth);
  ch_step(out, &at, -2.5f, 2.5f, 0.0f, 2.3f, kChGold, "1");
  float pop = t - 1.0f;
  if(pop > 0) {
    ch_spray(out, &body, mouth[0], mouth[1], mouth[2], mouth[3], pop);
    if(pop < 1.5f) {
      float kx = mouth[0] - 3.4f * pop, ky = mouth[1] - 6.8f * pop + 0.5f * 9 * pop * pop, kr = pop * 14;
      ch_limb(out, chx(&body, kx), chy(&body, ky), chx(&body, kx) + cosf(kr) * 0.22f * u, chy(&body, ky) + sinf(kr) * 0.22f * u,
              0.11f * u, 0xFFB07A4A);
    }
  }
  list_fade(out, from, ch_in(t, 0.1f, 0.5f), (1 - ch_in(t, 0.1f, 0.7f)) * 90 * s);

  // The words: who, WORLD CHAMPION, the standings, the game's message.
  float x0 = w * 0.53f, slide;
  ch_glow(out, x0 + 320 * ts, 420 * s, 460 * ts, ch_alpha(0x28FFC94A, rays));
  from = out->nv;
  if(v->menu_n > 0) ftext(out, v->menu_item[0].text, x0 + 4 * ts, 196 * s, 22 * ts, kChCyan, -1, 0, 8 * ts);
  list_fade(out, from, ch_in(t, 0.4f, 0.5f), 0);
  float band = x0 - 300 * ts + fmodf(fmaxf(0, t - 1.3f), 3.4f) * 1400 * ts;
  const char* line1 = champion ? "WORLD" : "TOUR";
  const char* line2 = champion ? "CHAMPION" : "COMPLETE";
  slide = (1 - ch_in(t, 0.5f, 0.6f)) * 140 * ts;
  from = out->nv;
  ch_shimmer(out, line1, x0 + slide, 362 * s, 150 * ts, kChGold, band);
  list_fade(out, from, ch_in(t, 0.5f, 0.4f), 0);
  slide = (1 - ch_in(t, 0.62f, 0.6f)) * 140 * ts;
  from = out->nv;
  ch_shimmer(out, line2, x0 + slide, 512 * s, 150 * ts, kChGold, band - 120 * ts);
  rect(out, x0 + 6 * ts, 540 * s, x0 + 6 * ts + 200 * ts * ch_in(t, 0.8f, 0.5f), 546 * s, kChGold);
  list_fade(out, from, ch_in(t, 0.62f, 0.4f), 0);
  from = out->nv;
  float nw = ftext_w(name, 60 * ts, 2, 0);
  ftext(out, name, x0 + 4 * ts, 626 * s, 60 * ts, kChWhite, -1, 2, 0);
  const char* skill = kSkillName[v->menu_skill >= 0 && v->menu_skill < 3 ? v->menu_skill : 0];
  char chip[40];
  snprintf(chip, sizeof(chip), "%s LEVEL", skill);
  float cw = ftext_w(chip, 15 * ts, 0, 3 * ts);
  rpanel(out, x0 + nw + 28 * ts, 594 * s, x0 + nw + 52 * ts + cw, 626 * s, 6 * ts, 0xC0101C28);
  ftext(out, chip, x0 + nw + 40 * ts, 616 * s, 15 * ts, kChMuted, -1, 0, 3 * ts);
  list_fade(out, from, ch_in(t, 0.9f, 0.4f), (1 - ch_in(t, 0.9f, 0.5f)) * 20 * s);
  // The last country's standings: the first five (who go on, in the game).
  char head[64];
  snprintf(head, sizeof(head), "FINAL STANDINGS  /  %s", kCountry[(v->champ_course >> 2) & 7].name);
  from = out->nv;
  ftext(out, head, x0 + 4 * ts, 766 * s, 15 * ts, kChMuted, -1, 0, 4 * ts);
  list_fade(out, from, ch_in(t, 1.15f, 0.4f), 0);
  for(int i = 0; i < 5; i++) {
    int d = rank[i] % 20;
    bool mine = d < players;
    float y = 806 * s + i * 38 * s, rx1 = x0 + 600 * ts;
    from = out->nv;
    if(mine) rpanel(out, x0 - 6 * ts, y - 28 * s, rx1 + 12 * ts, y + 8 * s, 6 * ts, 0x38FFC94A);
    uint32_t medal = i == 0 ? kChGold : i == 1 ? kChSilver : i == 2 ? kChBronze : 0xFF2A3A52;
    char num[4];
    snprintf(num, sizeof(num), "%d", i + 1);
    ch_disc(out, x0 + 16 * ts, y - 10 * s, 15 * ts, medal);
    ftext(out, num, x0 + 16 * ts, y - 2 * s, 17 * ts, i < 3 ? kChNavy : kChWhite, 0, 0, 0);
    ftext(out, v->res.name[d], x0 + 46 * ts, y, 24 * ts, mine ? kChWhite : 0xFFC4D0DC, -1, 0, 1 * ts);
    if(kept) {
      char pts[16];
      snprintf(pts, sizeof(pts), "%d PTS", v->champ_pts[d]);
      ftext(out, pts, rx1, y, 22 * ts, mine ? kChGold : kChMuted, 1, 0, 2 * ts);
    }
    list_fade(out, from, ch_in(t, 1.2f + 0.07f * i, 0.35f), (1 - ch_in(t, 1.2f + 0.07f * i, 0.4f)) * 16 * s);
  }
  // The game's own words after its first line ("ON COMPLETING ...", and on the first two
  // levels its challenge for the next), on two lines.
  char msg[2][96] = {"", ""};
  for(int i = 1; i < v->menu_n; i++) {
    char* m = msg[i == 1 ? 0 : 1];
    if(m[0]) strncat(m, " ", sizeof(msg[0]) - strlen(m) - 1);
    strncat(m, v->menu_item[i].text, sizeof(msg[0]) - strlen(m) - 1);
  }
  from = out->nv;
  ftext(out, msg[0], x0 + 4 * ts, 676 * s, 22 * ts, 0xFFC4D0DC, -1, 1, 2 * ts);
  ftext(out, msg[1], x0 + 4 * ts, 708 * s, 22 * ts, kChCyan, -1, 1, 2 * ts);
  list_fade(out, from, ch_in(t, 1.0f, 0.5f), 0);

  // The near confetti, over everything; the frame round it.
  ch_confetti(out, w, h, s, t, 60, 150);
  ftext(out, brand_caps(), 76 * s, 74 * s, 22 * s, kChCyan, -1, 0, 9 * s);
  ftext(out, "WORLD TOUR", w - 76 * s, 72 * s, 15 * s, kChMuted, 1, 0, 3 * s);
  rect(out, 74 * s, 100 * s, w - 74 * s, 101.5f * s, 0x30A9D2E0);
  static const char* const hints[1][2] = {{"ANY BUTTON", "CONTINUE"}};  // MenuWaitButton: any button
  key_hints(out, w, h, s, hints, 1);
  // In from black.
  if(t < 0.45f) rect(out, 0, 0, (float) w, (float) h, ch_alpha(0xFF000000, 1 - ch_in(t, 0, 0.45f)));
}
