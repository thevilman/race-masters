// The pit lane and the pit stop (included by view3d.c, before view3d_stages.inc).
//
// The original knows a course's pits by a few roadside objects in its table at $7E:6400 (one
// word a segment, read into TgTrack3D.tables.object): the pit sign (kind $3A: word $74xx, bit 8
// its side, bits 0-7 its distance from the middle) and a line of kind-$15 boards along the
// divider the original draws. A car passing the sign's segment further out than the sign starts
// its pit stop (drive.c track_object: $1E86 / $1E88 = $FFFF); from then the game holds the car's
// place on the course (its view is off, RaceDrivePlayers does not move it) and runs its own pit
// scene (race.c pit_stop, irq.c pit_lane): the view fades out, the scene's car drives in by the
// scroll $A8 / $BC and stops ($FFFA), the fuel ($1E62 / $1E64) goes up by $7F an update to $A7FF
// ($FFF9: or the player leaves early with accelerate), it drives out, the view fades out and back
// in, and the car goes on from where it was held. 13 of the 32 courses have pits; all of them on
// a straight, their sign 60 (3 m, on the road) or 124 (6.2 m, just past the kerb) from the middle.
//
// Here (presentation only, never the game's state):
// - The pit lane, laid out once per course from the sign (the same every lap): the line the game
//   checks painted on the road before the sign (cross it and you are in), the lane splitting off
//   the road after it behind a wall with catch fencing (at the road's edge, or the game's own
//   divider where that is further out, so the track keeps its width: the game's cars drive the
//   whole road there), a fast lane and a working lane with painted boxes, the speed-limit lines and
//   boards, PIT IN / PIT OUT painted, the exit merging back onto the road. The game's own pit-lane
//   boards, and roadside pictures standing where the lane is, make way for it; nothing else is
//   placed on it.
// - The garages: a row of eight under a canopy, a pit building's glass floor over them; two are the
//   players' (player 1's, and player 2's or the computer rival's), in their colours with their
//   names; the others invented teams', a mechanic or two in their open doors.
// - The stop, from the game's state each frame (pit_step): the car brakes from where it crossed
//   into its box, arriving when the game's own scene stops its car (the scene's frames to the stop
//   are worked out from its scroll and speed, as pit_lane moves them); the crew walks out as it
//   comes in, the jacks lift it, the wheel guns turn, the fuel hose is on while the game's fuel goes
//   up; the lollipop shows BRAKE, then GO once the game has let it go and the crew is done (at
//   least ~1.7 s of service); it drives off down the lane at the limit and back onto the road, and
//   is brought onto the game's place again (the game holds the car still while its scene drives in
//   and out, so the car as drawn is ahead of the game's place by about the lane's length when the
//   game lets it go; the gap closes as the game's car comes through at speed, the car as drawn
//   accelerating to meet it as a car would - as rivals pass it after a real stop). The car is drawn
//   as an offset from the game's place, zero at the start and the end.

#define PR_ENT0 (-24)            // the pit apron starts to widen off the road (segments from the sign)
#define PR_ENT1 (-6)             // ... at its full width
#define PR_WALL0 10              // the pit wall's nose
#define PR_LIM0 12               // the pit speed limit's line
#define PR_GAR0 14               // the first garage
#define PR_NGAR 8                // garages, 4 segments (10 m) each
#define PR_GAR1 (PR_GAR0 + 4 * PR_NGAR)
#define PR_LIM1 (PR_GAR1 + 2)    // the end of the limit
#define PR_WALL1 (PR_GAR1 + 6)   // the wall's end
#define PR_EXIT1 (PR_GAR1 + 24)  // the lane is back on the road
#define PR_BOX 2                 // player 1's garage (the next: player 2's, or the computer rival's)

typedef struct {
  bool on;
  uint32_t key;
  int nseg, S, sgn, divkind;
  // Metres out from the middle on the pits' side: the game's line (the sign's distance), the
  // kerb's outer edge, the wall's track face, the fast lane's middle, the dashed line between the
  // lanes, a car standing in its box (its middle), the garages' fronts.
  float div, kerb, wall, fast, lanediv, boxc, gar;
} PitLane;
static PitLane g_pit;

static float pit_smooth(float t) {
  t = t < 0 ? 0 : t > 1 ? 1 : t;
  return t * t * (3 - 2 * t);
}

static int g_pit_ver;  // the layout's changes (the stages' placement is redone after one)

// The lane laid out from a course's object table (obj: a word a segment).
static void pit_set(const TgTrack3D* tr, const uint16_t* obj) {
  int n = tr->tables.segments;
  PitLane was = g_pit;
  memset(&g_pit, 0, sizeof(g_pit));
  g_pit.key = tr->key, g_pit.nseg = n, g_pit.divkind = -1;
  float hw = tr->scale.half_width;
  for(int i = 0; i < n && n > PR_EXIT1 * 4; i++) {
    uint16_t w = obj[i];
    if((w & 0xFE00) != 0x7400) continue;
    g_pit.S = i, g_pit.sgn = (w & 0x100) ? -1 : 1;
    g_pit.div = (float) ((w & 0xFF) << 7) / TG_TRACK3D_SIDE_EDGE * hw;
    g_pit.kerb = 1.12f * hw;
    g_pit.wall = fmaxf(g_pit.div, g_pit.kerb + 0.6f);
    g_pit.fast = g_pit.wall + 0.45f + 1.8f;
    g_pit.lanediv = g_pit.wall + 4.05f;
    g_pit.boxc = g_pit.wall + 6.15f;
    g_pit.gar = g_pit.wall + 8.65f;
    // The game's own divider: the kind of board just past the sign on its side (the original's line).
    for(int d = 1; d <= 3 && g_pit.divkind < 0; d++) {
      uint16_t o = obj[(i + d) % n];
      if(o && !(o & 0x8000) && ((o & 0x100) ? -1 : 1) == g_pit.sgn) g_pit.divkind = (o >> 9) & 0x3F;
    }
    g_pit.on = true;
    break;
  }
  if(was.on != g_pit.on || was.S != g_pit.S || was.sgn != g_pit.sgn || was.div != g_pit.div || was.key != g_pit.key) g_pit_ver++;
}

static const PitLane* pit_layout(const TgTrack3D* tr) {
  if(g_pit.key != tr->key || g_pit.nseg != tr->tables.segments) pit_set(tr, tr->tables.object);
  return &g_pit;
}

// Segment seg's place from the sign, round the lap's shorter way.
static int pit_rel(int seg) {
  int n = g_pit.nseg, r = ((seg - g_pit.S) % n + n) % n;
  return r >= n / 2 ? r - n : r;
}

// The pit apron's outer edge at r segments from the sign (metres out); the kerb where there is none.
static float pit_outer(float r) {
  const PitLane* pl = &g_pit;
  float full = pl->gar + 0.4f;
  if(r < PR_ENT0 || r > PR_EXIT1) return pl->kerb;
  if(r < PR_ENT1) return pl->kerb + (full - pl->kerb) * pit_smooth((r - PR_ENT0) / (float) (PR_ENT1 - PR_ENT0));
  if(r <= PR_WALL1) return full;
  return pl->kerb + (full - pl->kerb) * (1 - pit_smooth((r - PR_WALL1) / (float) (PR_EXIT1 - PR_WALL1)));
}

// The ground r segments from the sign, x metres out on the pits' side; and the level directions
// there: along the road, and out towards the pits.
static V3 pit_at(const TgTrack3D* tr, float r, float x) {
  float len = (float) tr->tables.segments * TG_TRACK3D_SEG_UNITS;
  float pos = fmodf(((float) g_pit.S + r) * TG_TRACK3D_SEG_UNITS, len);
  if(pos < 0) pos += len;
  return tv(tg_track3d_frame(tr, pos, g_pit.sgn * x / tr->scale.half_width * TG_TRACK3D_SIDE_EDGE).pos);
}

static void pit_dirs(const TgTrack3D* tr, float r, V3* f, V3* n) {
  float len = (float) tr->tables.segments * TG_TRACK3D_SEG_UNITS;
  float pos = fmodf(((float) g_pit.S + r) * TG_TRACK3D_SEG_UNITS, len);
  if(pos < 0) pos += len;
  TgTrackFrame fr = tg_track3d_frame(tr, pos, 0);
  *f = vnorm(v3(fr.forward.x, 0, fr.forward.z));
  *n = vmul(vnorm(v3(fr.right.x, 0, fr.right.z)), (float) g_pit.sgn);
}

static bool pit_blocks(const TgTrack3D* tr, int seg, float side) {
  const PitLane* pl = pit_layout(tr);
  if(!pl->on || side * pl->sgn <= 0) return false;
  int r = pit_rel(seg);
  if(r < PR_ENT0 - 2 || r > PR_EXIT1 + 2) return false;
  float x = fabsf(side) * tr->scale.half_width;
  float out = fmaxf(pit_outer((float) r), pit_outer((float) r + 1)) + (r >= PR_GAR0 - 3 && r <= PR_GAR1 + 3 ? 13.0f : 1.5f);
  return x > pl->kerb - 0.3f && x < out;
}

static bool pit_hides(const TgTrack3D* tr, int seg, uint16_t ow) {
  if(ow == 0 || (ow & 0x8000) || (ow & 0xFE00) == 0x7400) return false;
  const PitLane* pl = pit_layout(tr);
  if(!pl->on) return false;
  int r = pit_rel(seg);
  if(r < PR_ENT0 - 20 || r > PR_EXIT1) return false;
  if(((ow & 0x100) ? -1 : 1) == pl->sgn && (int) ((ow >> 9) & 0x3F) == pl->divkind) return true;  // the game's line of boards
  return pit_blocks(tr, seg, object_side(ow));
}

static float pit_sign_side(const TgTrack3D* tr, float side) {
  const PitLane* pl = pit_layout(tr);
  if(!pl->on || side * pl->sgn <= 0) return side;
  float x = pl->div < pl->kerb ? pl->kerb + 0.9f : (pl->kerb + pl->div) / 2;  // in the gore, by the road
  return pl->sgn * x / tr->scale.half_width;
}

// --- Signs and boards ------------------------------------------------------------------------

// The other teams and the sponsors (all invented: no real brands).
static const struct { const char* name; uint32_t col, trim; } kTeams[10] = {
  {"KESTREL", 0xFF1E5AA8, 0xFFF2C230}, {"HALCYON", 0xFF16A08C, 0xFFF4F4F0}, {"ORBITAL", 0xFF7A2E9A, 0xFFE8E8EC},
  {"MERIDIAN", 0xFFE06A1A, 0xFF1A1C22}, {"ARGENT", 0xFFB4BAC4, 0xFF1E2A44}, {"NIMBUS", 0xFF2C9A3C, 0xFFF0F0E8},
  {"PEREGRINE", 0xFFC8202C, 0xFFF4F4F4}, {"SOLSTICE", 0xFFF0B820, 0xFF202428}, {"BOREAL", 0xFF2A3A5C, 0xFF6AD0F0},
  {"CASCADE", 0xFF0098D0, 0xFFFFFFFF},
};

// A round board facing face (seen from that side only), radius rad.
static void pit_disc(Gather* g, V3 c, V3 face, V3 up, float rad, uint32_t col, int sides) {
  if(vdot(face, vsub(g->cam->eye, c)) <= 0) return;
  V3 rt = vnorm(vcross(up, face));
  CV q[16];
  if(sides > 16) sides = 16;
  for(int i = 0; i < sides; i++) {
    float a = 2 * PI * i / sides;
    q[i] = (CV) {to_cam(g->cam, vadd(c, vadd(vmul(rt, cosf(a) * rad), vmul(up, sinf(a) * rad)))), 0, 0, 0};
  }
  emit_poly(g, q, sides, col, -1);
}

// Words on an upright face (a board, a sign), centred at c, facing face, letters size m tall.
static void pit_sign_words(Gather* g, const char* s, V3 c, V3 face, float size, uint32_t col) {
  if(vdot(face, vsub(g->cam->eye, c)) <= 0) return;
  V3 up = v3(0, 1, 0), rt = vnorm(vcross(up, face));
  float w = ftext_w(s, size, 2, 0), cap = -kFontGlyphs[glyph_of(2, 'E')].top * (size / FONT_SIZE);
  V3 at = vadd(vadd(c, vmul(rt, -w / 2)), vadd(vmul(up, -cap / 2), vmul(face, 0.012f)));
  wtext(g, s, at, rt, vmul(up, -1), size, col, 2, 0);
}

// A speed-limit roundel (a red ring, the number) or its end (white, a black bar), facing face.
static void pit_roundel(Gather* g, V3 c, V3 face, float rad, bool end, float depth) {
  V3 up = v3(0, 1, 0);
  begin_item(g, to_cam(g->cam, c).z - STAND_BIAS - 0.2f);
  pit_disc(g, c, face, up, rad, fogged(end ? 0xFF1A1C20 : 0xFFD8262E, depth), 16);
  pit_disc(g, vadd(c, vmul(face, 0.006f)), face, up, rad * (end ? 0.93f : 0.8f), fogged(0xFFF6F6F2, depth), 16);
  pit_disc(g, vadd(c, vmul(face, -0.01f)), vmul(face, -1), up, rad, fogged(0xFF6A7078, depth), 16);
  if(end) {
    if(vdot(face, vsub(g->cam->eye, c)) > 0) {
      V3 rt = vnorm(vcross(up, face)), d = vnorm(vadd(rt, up)), e = vnorm(vsub(rt, up));
      V3 o = vadd(c, vmul(face, 0.012f));
      quad(g, vadd(o, vmul(d, -rad * 0.85f)), vadd(vadd(o, vmul(d, -rad * 0.85f)), vmul(e, rad * 0.14f)),
           vadd(vadd(o, vmul(d, rad * 0.85f)), vmul(e, rad * 0.14f)), vadd(o, vmul(d, rad * 0.85f)), fogged(0xFF1A1C20, depth));
    }
  } else {
    pit_sign_words(g, "60", vadd(c, vmul(face, 0.012f)), face, rad * 0.75f, fogged(0xFF101214, depth));
  }
}

// --- The ground: the apron and its paint (in the road's segment loop) -------------------------

// A line painted on the apron: x0 to x1 metres out, from r0 to r1 segments from the sign.
static void pit_paint(Gather* g, const TgTrack3D* tr, float r0, float r1, float x0a, float x1a, float x0b, float x1b, uint32_t col) {
  V3 up = v3(0, 0.022f, 0);
  quad(g, vadd(pit_at(tr, r0, x0a), up), vadd(pit_at(tr, r0, x1a), up), vadd(pit_at(tr, r1, x1b), up), vadd(pit_at(tr, r1, x0b), up), col);
}

// Words painted flat on the ground, read from a car coming along the road: centred at r
// segments from the sign and x metres out, letters size m tall, drawn out stretch times along.
static void pit_words(Gather* g, const TgTrack3D* tr, const char* s, float r, float x, float size, float stretch, uint32_t col) {
  V3 f, n;
  pit_dirs(tr, r, &f, &n);
  V3 right = vmul(n, (float) g_pit.sgn);  // (the driver's right)
  float w = ftext_w(s, size, 2, size * 0.06f);
  V3 at = vadd(vadd(pit_at(tr, r, x), vmul(right, -w / 2)), v3(0, 0.026f, 0));
  wtext(g, s, at, right, vmul(f, -stretch), size, col, 2, size * 0.06f);
}

// Diagonal hatching between x0(r) and x1(r) over one segment (a gore no car crosses).
static void pit_hatch(Gather* g, const TgTrack3D* tr, int r, float x0, float x1a, float x1b, uint32_t col) {
  V3 up = v3(0, 0.022f, 0);
  for(int k = 0; k < 2; k++) {
    float a = r + k * 0.5f, xo = x1a + (x1b - x1a) * (k * 0.5f);
    if(xo - x0 < 0.3f) continue;
    float sl = (xo - x0) / tr->scale.seg_len * 0.7f;  // (slanted forward, ~35 degrees)
    quad(g, vadd(pit_at(tr, a, x0), up), vadd(pit_at(tr, a + 0.14f, x0), up), vadd(pit_at(tr, a + 0.14f + sl, xo), up),
         vadd(pit_at(tr, a + sl, xo), up), col);
  }
}

static void pit_ground(Gather* g, const TgTrack3D* tr, int s0, float p0, float depth, float light) {
  (void) p0;
  const PitLane* pl = pit_layout(tr);
  if(!pl->on || depth > 420) return;
  int r = pit_rel(s0);
  if(r < PR_ENT0 - 1 || r > PR_EXIT1 + 1) return;
  float rf = (float) r;
  // The apron: a smoother, paler asphalt from the kerb out.
  float o0 = pit_outer(rf), o1 = pit_outer(rf + 1);
  uint32_t apron = fogged(shade(g_night ? 0xFF3E434A : 0xFF73787F, light), depth);
  if(o0 > pl->kerb + 0.05f || o1 > pl->kerb + 0.05f) {
    V3 lift = v3(0, 0.01f, 0);
    quad(g, vadd(pit_at(tr, rf, pl->kerb), lift), vadd(pit_at(tr, rf, o0), lift), vadd(pit_at(tr, rf + 1, o1), lift),
         vadd(pit_at(tr, rf + 1, pl->kerb), lift), apron);
  }
  uint32_t white = fogged(0xFFEEF0EE, depth), yellow = fogged(0xFFF2C21E, depth);
  float lw = 0.075f;
  // The game's line: past it at the sign, a car is in the pits.
  if(r >= PR_ENT0 && r < 0 && (pl->div < pl->kerb || o0 > pl->div + 0.4f))
    pit_paint(g, tr, rf, rf + 1, pl->div - lw, pl->div + lw, pl->div - lw, pl->div + lw, white);
  // The lane's inner line: from the game's line out to the wall's foot, along it, back to the road's edge.
  float in0 = pl->wall + 0.6f, edge = 0.93f * tr->scale.half_width;
  if(r >= 0 && r < PR_WALL0) {
    float xa = pl->div + (in0 - pl->div) * (rf / PR_WALL0), xb = pl->div + (in0 - pl->div) * ((rf + 1) / PR_WALL0);
    pit_paint(g, tr, rf, rf + 1, xa - lw, xa + lw, xb - lw, xb + lw, white);
    if(r >= PR_ENT1 + 6) pit_hatch(g, tr, r, pl->kerb + 0.15f, xa - 0.25f, xb - 0.25f, yellow);
  }
  if(r >= PR_ENT1 && r < 0 && pl->div > pl->kerb + 0.6f) pit_hatch(g, tr, r, pl->kerb + 0.15f, pl->div - 0.25f, pl->div - 0.25f, yellow);
  if(r >= PR_WALL0 && r < PR_WALL1) pit_paint(g, tr, rf, rf + 1, in0 - lw, in0 + lw, in0 - lw, in0 + lw, white);
  if(r >= PR_WALL1 && r < PR_WALL1 + 14) {
    float xa = in0 + (edge - in0) * ((rf - PR_WALL1) / 14), xb = in0 + (edge - in0) * ((rf + 1 - PR_WALL1) / 14);
    pit_paint(g, tr, rf, rf + 1, xa - lw, xa + lw, xb - lw, xb + lw, white);
    pit_hatch(g, tr, r, pl->kerb + 0.15f, xa - 0.25f, xb - 0.25f, yellow);
  }
  // The garages' line, the dashed line between the fast and the working lane.
  float gl = pl->gar - 0.15f;
  if(r >= PR_WALL0 && r < PR_WALL1) pit_paint(g, tr, rf, rf + 1, gl - lw, gl + lw, gl - lw, gl + lw, white);
  if(r >= PR_LIM0 && r < PR_LIM1 && ((r - PR_LIM0) & 1) == 0)
    pit_paint(g, tr, rf, rf + 0.6f, pl->lanediv - lw, pl->lanediv + lw, pl->lanediv - lw, pl->lanediv + lw, white);
  // The speed limit's lines across the lane, start and end.
  if(r == PR_LIM0 || r == PR_LIM1) pit_paint(g, tr, rf, rf + 0.16f, in0, gl, in0, gl, white);
  // The boxes: an outline before each garage (the players' in yellow, filled in their colour), the
  // front wheels' marks, the box's number in front.
  if(r >= PR_GAR0 && r < PR_GAR1) {
    int gi = (r - PR_GAR0) / 4;
    float b0 = PR_GAR0 + 4.0f * gi + 0.35f, b1 = b0 + 3.3f, xa = pl->lanediv + 0.25f, xb = pl->gar - 0.45f;
    bool mine = gi == PR_BOX || gi == PR_BOX + 1;
    uint32_t col = mine ? yellow : fogged(0xFFD8DCDE, depth);
    float a = fmaxf(rf, b0), b = fminf(rf + 1, b1);
    if(b > a) {
      if(mine) pit_paint(g, tr, a, b, xa, xb, xa, xb, with_alpha(fogged(gi == PR_BOX ? 0xFF2A3A52 : 0xFF3A2A4A, depth), 90));
      pit_paint(g, tr, a, b, xa, xa + 0.12f, xa, xa + 0.12f, col);
      pit_paint(g, tr, a, b, xb - 0.12f, xb, xb - 0.12f, xb, col);
    }
    if(b0 >= rf && b0 < rf + 1) pit_paint(g, tr, b0, b0 + 0.05f, xa, xb, xa, xb, col);
    if(b1 >= rf && b1 < rf + 1) pit_paint(g, tr, b1 - 0.05f, b1, xa, xb, xa, xb, col);
    float wm = b0 - 0.35f + 2 + 1.35f / tr->scale.seg_len;  // where its front wheels stop
    if(wm >= rf && wm < rf + 1)
      for(int k = -1; k <= 1; k += 2) pit_paint(g, tr, wm - 0.02f, wm + 0.02f, pl->boxc + k * 0.93f - 0.3f, pl->boxc + k * 0.93f + 0.3f,
                                              pl->boxc + k * 0.93f - 0.3f, pl->boxc + k * 0.93f + 0.3f, col);
    if(r == PR_GAR0 + 4 * gi + 3 && depth < 140) {
      font_ready(g_view);
      char num[8];
      int nb = gi == PR_BOX ? 1 : gi == PR_BOX + 1 ? 2 : gi + 3;
      snprintf(num, sizeof(num), "%d", nb);
      pit_words(g, tr, num, b1 - 0.62f, pl->boxc, 0.95f, 1.5f, col);
    }
  }
  // PIT IN as the lane begins, PIT OUT as it leaves.
  if(depth < 170) {
    font_ready(g_view);
    if(r == 2) pit_words(g, tr, "PIT IN", 2.1f, pl->div + 0.25f * (in0 - pl->div) + 2.6f, 1.15f, 2.2f, white);
    if(r == PR_LIM0 + 1) pit_words(g, tr, "60", 13.2f, pl->fast, 1.3f, 2.0f, white);
    if(r == PR_WALL1 - 5) pit_words(g, tr, "PIT OUT", PR_WALL1 - 4.9f, pl->fast, 1.15f, 2.2f, white);
  }
}

// --- The garages -----------------------------------------------------------------------------

// A block standing on the pit lane's ground: r0 to r1 segments from the sign, x0 to x1 metres out,
// y0 to y1 metres over the ground under it. It follows the ground along the lane (pits are on
// climbs too: London's rises 17 %), its sides upright.
static void pit_block(Gather* g, const TgTrack3D* tr, float r0, float r1, float x0, float x1, float y0, float y1, uint32_t col, float bias) {
  V3 c[8], up = v3(0, 1, 0);
  V3 p[4] = {pit_at(tr, r0, x0), pit_at(tr, r0, x1), pit_at(tr, r1, x1), pit_at(tr, r1, x0)};
  for(int k = 0; k < 4; k++) c[k] = vadd(p[k], vmul(up, y0)), c[k + 4] = vadd(p[k], vmul(up, y1));
  hexa(g, c, col, bias);
}

// The players' garages: their car's colours (Rosso, Glacier, Violet, Azure).
static const uint32_t kPlayerTeam[4][2] = {{0xFFC8222A, 0xFFF4F4F4}, {0xFFE4E8EC, 0xFF2A3A5A}, {0xFF6A32AE, 0xFFF2F2F2}, {0xFF2364CC, 0xFFF4F4F4}};

// Garage gi's colours and name: the players' own (their name as entered; the computer rival's in a
// 1-player race, in its paint), or an invented team's.
static void pit_team(const TgView3D* v, int gi, uint32_t* col, uint32_t* trim, const char** name, char* buf, size_t n) {
  if(gi == PR_BOX || gi == PR_BOX + 1) {
    int b = gi - PR_BOX;
    bool player = b == 0 || v->two;
    if(player) {
      int t = v->ptype[b] & 3;
      *col = kPlayerTeam[t][0], *trim = kPlayerTeam[t][1];
      snprintf(buf, n, "%s", g_player_name[b][0] ? g_player_name[b] : b ? "PLAYER 2" : "PLAYER 1");
    } else {  // the computer rival: its own paint
      int k1 = v->cur.view_car[1], id = k1 >= 0 ? v->cur.cars[k1].id & 31 : 1;
      *col = v->car_paint[id & 31] ? v->car_paint[id & 31] : 0xFFE09A20, *trim = 0xFF1A1C22;
      snprintf(buf, n, "%s", v->res.name[1][0] ? v->res.name[1] : "RIVAL");
    }
    *name = buf;
    return;
  }
  int k = (gi + v->course * 3) % 10;
  *col = kTeams[k].col, *trim = kTeams[k].trim, *name = kTeams[k].name;
}

// A box between two points (a limb, a pole), w wide along side, d deep the other way.
static void pit_limb(Gather* g, V3 a, V3 b, float w, float d, V3 side, uint32_t col) {
  V3 ax = vsub(b, a);
  float L = sqrtf(vdot(ax, ax));
  if(L < 1e-3f) return;
  ax = vmul(ax, 1 / L);
  V3 s = vsub(side, vmul(ax, vdot(side, ax)));
  float sl = sqrtf(vdot(s, s));
  s = sl > 1e-3f ? vmul(s, 1 / sl) : vnorm(vcross(ax, fabsf(ax.y) < 0.9f ? v3(0, 1, 0) : v3(1, 0, 0)));
  mbox(g, a, vcross(ax, s), s, ax, d / 2, w / 2, 0, L, col, 0.002f);
}

// The middle joint of a two-bone limb (knee, elbow) from a to b, bent towards bend.
static V3 pit_joint(V3 a, V3 b, float l1, float l2, V3 bend) {
  V3 d = vsub(b, a);
  float L = sqrtf(vdot(d, d));
  if(L < 1e-4f) return vadd(a, vmul(bend, l1));
  V3 dn = vmul(d, 1 / L);
  if(L >= l1 + l2 - 1e-3f) return vadd(a, vmul(dn, l1));
  float x = (L * L + l1 * l1 - l2 * l2) / (2 * L), y = sqrtf(fmaxf(l1 * l1 - x * x, 0));
  V3 bp = vsub(bend, vmul(dn, vdot(bend, dn)));
  float bl = sqrtf(vdot(bp, bp));
  bp = bl > 1e-4f ? vmul(bp, 1 / bl) : v3(0, 1, 0);
  return vadd(a, vadd(vmul(dn, x), vmul(bp, y)));
}

// How a crew member stands: feet and hands (world), the hips' height, the lean forward, kneeling.
typedef struct {
  V3 foot[2], hand[2];
  float hip, lean;
  bool kneel;
} Pose;

// A crew member: overalls in the team's colour with a band of its trim, a helmet with a dark
// visor, gloves and boots; boxes between joints worked out from the pose (two-bone legs and arms).
// lod 1: fewer, plainer parts (far away).
static void crew_member(Gather* g, V3 base, V3 face, const Pose* p, uint32_t suit, uint32_t trim, uint32_t helm, int lod) {
  V3 up = v3(0, 1, 0), f = face, r = vnorm(vcross(f, up));
  V3 hipC = vadd(base, vmul(up, p->hip));
  V3 tup = vnorm(vadd(vmul(up, cosf(p->lean)), vmul(f, sinf(p->lean))));
  V3 neck = vadd(hipC, vmul(tup, 0.56f));
  uint32_t dark = 0xFF1C1F24, legC = mix(suit, 0xFF1E222A, 0.72f);  // (dark trousers, the team's top)
  for(int i = 0; i < 2; i++) {
    float sd = i ? 0.1f : -0.1f;
    V3 hp = vadd(hipC, vmul(r, sd)), ankle = vadd(p->foot[i], vmul(up, 0.07f));
    V3 knee;
    if(p->kneel && i == 1) knee = vadd(vadd(base, vmul(f, 0.12f)), vadd(vmul(r, sd), vmul(up, 0.07f))), ankle = vadd(knee, vmul(f, -0.42f));
    else knee = pit_joint(hp, ankle, 0.46f, 0.43f, vadd(f, vmul(up, 0.1f)));
    if(lod) {
      pit_limb(g, hp, ankle, 0.15f, 0.16f, r, legC);
    } else {
      pit_limb(g, hp, knee, 0.16f, 0.17f, r, legC);
      pit_limb(g, knee, ankle, 0.13f, 0.15f, r, legC);
      mbox(g, vadd(ankle, vmul(up, -0.07f)), f, r, up, 0.14f, 0.065f, 0, 0.1f, dark, 0.003f);  // boot
    }
  }
  pit_limb(g, vadd(hipC, vmul(tup, -0.06f)), neck, 0.42f, 0.25f, r, suit);
  if(!lod) pit_limb(g, vadd(hipC, vmul(tup, 0.3f)), vadd(hipC, vmul(tup, 0.38f)), 0.43f, 0.26f, r, trim);
  V3 h0 = vadd(neck, vmul(tup, 0.03f)), h1 = vadd(neck, vmul(tup, 0.31f));
  pit_limb(g, h0, h1, 0.26f, 0.28f, r, helm);
  if(!lod) pit_limb(g, vadd(vadd(neck, vmul(tup, 0.15f)), vmul(f, 0.1f)), vadd(vadd(neck, vmul(tup, 0.25f)), vmul(f, 0.1f)), 0.22f, 0.1f, r, 0xFF20262E);
  for(int i = 0; i < 2; i++) {
    float sd = i ? 0.24f : -0.24f;
    V3 sh = vadd(vadd(neck, vmul(tup, -0.06f)), vmul(r, sd)), hand = p->hand[i];
    if(lod) {
      pit_limb(g, sh, hand, 0.1f, 0.1f, f, suit);
      continue;
    }
    V3 el = pit_joint(sh, hand, 0.3f, 0.3f, vadd(vmul(up, -1), vadd(vmul(f, -0.4f), vmul(r, sd * 2.5f))));
    pit_limb(g, sh, el, 0.11f, 0.12f, f, suit);
    pit_limb(g, el, hand, 0.1f, 0.1f, f, suit);
    mbox(g, vadd(hand, vmul(up, -0.05f)), f, r, up, 0.05f, 0.05f, 0, 0.1f, dark, 0.004f);  // glove
  }
}

// Standing: feet a stride apart, hands by the hips; walking: a sine stride (legs opposite, arms
// opposite to the legs, a little bob) by the distance walked, so the feet never slide.
static Pose pit_pose_stand(V3 base, V3 f, float walk, float phase) {
  V3 up = v3(0, 1, 0), r = vnorm(vcross(f, up));
  Pose p;
  float sw = sinf(phase) * 0.32f * walk, lift0 = fmaxf(0, cosf(phase)) * 0.12f * walk, lift1 = fmaxf(0, -cosf(phase)) * 0.12f * walk;
  p.foot[0] = vadd(base, vadd(vmul(r, -0.11f), vadd(vmul(f, sw), vmul(up, lift0))));
  p.foot[1] = vadd(base, vadd(vmul(r, 0.11f), vadd(vmul(f, -sw), vmul(up, lift1))));
  p.hip = 0.92f - 0.03f * walk * fabsf(sinf(phase));
  p.lean = 0.08f * walk;
  for(int i = 0; i < 2; i++) {
    float sd = i ? 0.3f : -0.3f, swing = (i ? sw : -sw) * 0.7f;
    p.hand[i] = vadd(base, vadd(vmul(r, sd), vadd(vmul(f, 0.05f + swing), vmul(up, p.hip - 0.05f))));
  }
  p.kneel = false;
  return p;
}

// A tyre (a dark drum), axis along ax, at c.
static void pit_tyre(Gather* g, V3 c, V3 ax, V3 a1, V3 a2, uint32_t band) {
  frustum(g, vadd(c, vmul(ax, -0.15f)), ax, a1, a2, 0.33f, 0.33f, 0.3f, 8, 0xFF16181C, 0.01f);
  if(band) frustum(g, vadd(c, vmul(ax, -0.155f)), ax, a1, a2, 0.2f, 0.2f, 0.31f, 8, band, 0.012f);
}

static void pit_garage(Gather* g, TgView3D* v, const TgTrack3D* tr, int gi) {
  const PitLane* pl = &g_pit;
  float rg = PR_GAR0 + 4.0f * gi, rc = rg + 2, sl = tr->scale.seg_len;
  V3 f, n, up = v3(0, 1, 0);
  pit_dirs(tr, rc, &f, &n);
  // (a: metres along from its middle, b: out from its front, y: up from the ground there)
#define GP(a, b, y) vadd(pit_at(tr, rc + (a) / sl, pl->gar + (b)), vmul(up, (y)))
#define GB(a0, a1, b0, b1, y0, y1, c, bias) pit_block(g, tr, rc + (a0) / sl, rc + (a1) / sl, pl->gar + (b0), pl->gar + (b1), y0, y1, c, bias)
  float depth = to_cam(g->cam, GP(0, 4.75f, 2.5f)).z;
  if(depth < -20 || depth > 430) return;
  uint32_t col, trim;
  const char* name;
  char buf[24];
  pit_team(v, gi, &col, &trim, &name, buf, sizeof(buf));
  uint32_t h = hash32((uint32_t) gi * 0x9E3779B1u ^ (uint32_t) v->course * 0x85EBCA6Bu);
  bool mine = gi == PR_BOX || gi == PR_BOX + 1;
  int door = mine ? 0 : (h & 3) == 0 ? 2 : (h & 3) == 1 ? 1 : 0;  // 0 open, 1 half, 2 shut
  // (faded, as a prop by the road, if the camera comes into it: a car run far out past the pits)
  static FadeMem mem[2][PR_NGAR];
  g_alpha = fade_mem(&mem[g_view_k][gi], near_fade(g->cam, GP(0, 4.75f, 0), 5.5f, 5.5f, false));
  if(g_alpha < 0.02f) {
    g_alpha = 1;
    return;
  }
  uint32_t wallC = 0xFFE6E8EA, roofC = 0xFFB8BEC6;
  for(int s = -1; s <= 1; s += 2) GB(s * 4.86f - 0.09f, s * 4.86f + 0.09f, 0, 9.5f, 0, 5.3f, wallC, 0);
  GB(-4.95f, 4.95f, 9.35f, 9.55f, 0, 5.3f, wallC, 0);
  GB(-4.95f, 4.95f, -0.05f, 9.55f, 5.15f, 5.4f, roofC, 0);
  GB(-4.95f, 4.95f, 0, 0.3f, 3.95f, 5.4f, col, 0.01f);  // the fascia, in its colour
  GB(-4.96f, 4.96f, -0.01f, 0.05f, 3.95f, 4.12f, trim, 0.02f);  // its trim
  if(gi == 0 || gi == PR_NGAR - 1) {  // the row's ends: its band round the corner, a dark plinth
    float a = gi == 0 ? -4.98f : 4.98f;
    GB(a - 0.03f, a + 0.03f, 0, 9.6f, 3.95f, 5.4f, col, 0.01f);
    GB(a - 0.03f, a + 0.03f, 0, 9.6f, 0, 0.6f, 0xFF3A4048, 0.01f);
  }
  if(depth < 260) {
    // Inside: the floor, its colour on the back wall, benches, tyres in sets, lights.
    bool on = g_night || mine;
    begin_item(g, depth + 3);
    V3 lift = v3(0, 0.02f, 0);
    quad(g, vadd(GP(-4.77f, 0.3f, 0), lift), vadd(GP(4.77f, 0.3f, 0), lift), vadd(GP(4.77f, 9.35f, 0), lift), vadd(GP(-4.77f, 9.35f, 0), lift),
         fogged(on ? 0xFFB4BAC2 : 0xFF8C929A, depth));
    quad(g, GP(-4.77f, 9.33f, 1.3f), GP(4.77f, 9.33f, 1.3f), GP(4.77f, 9.33f, 2.1f), GP(-4.77f, 9.33f, 2.1f), fogged(col, depth));
    for(int s = -1; s <= 1; s += 2)  // (and along its side walls, over a grey dado)
      quad(g, GP(s * 4.76f, 0.4f, 1.3f), GP(s * 4.76f, 9.3f, 1.3f), GP(s * 4.76f, 9.3f, 2.1f), GP(s * 4.76f, 0.4f, 2.1f), fogged(col, depth)),
      quad(g, GP(s * 4.76f, 0.4f, 0.02f), GP(s * 4.76f, 9.3f, 0.02f), GP(s * 4.76f, 9.3f, 1.3f), GP(s * 4.76f, 0.4f, 1.3f), fogged(0xFF9AA0A8, depth));
    for(int s = -1; s <= 1; s += 2) quad(g, GP(s * 2.0f - 0.9f, 2.0f, 5.13f), GP(s * 2.0f + 0.9f, 2.0f, 5.13f), GP(s * 2.0f + 0.9f, 7.0f, 5.13f),
                                         GP(s * 2.0f - 0.9f, 7.0f, 5.13f), on ? 0xFFFFF8E8 : 0xFFD8DCE0);
    for(int k = -1; k <= 1; k++) mbox(g, GP(k * 3.0f, 8.95f, 0), f, n, up, 1.25f, 0.35f, 0, 1.05f, 0xFF4A505A, 0.02f);
    if(depth < 140) {
      for(int s = -1; s <= 1; s += 2)
        for(int k = 0; k < 4; k++) pit_tyre(g, GP(s * 3.75f, 1.3f + (k & 1) * 0.75f, 0.17f + (k >> 1) * 0.31f), up, f, n, k >= 2 ? trim : 0);
    }
  }
  // The door: rolled up, half down, or shut (ribbed, in its colour).
  uint32_t doorC = mix(col, 0xFFD8DCE0, 0.35f);
  if(door == 0) GB(-4.7f, 4.7f, 0.18f, 0.62f, 3.62f, 3.95f, doorC, 0.02f);
  else {
    float y0 = door == 2 ? 0 : 2.1f;
    GB(-4.74f, 4.74f, 0.32f, 0.4f, y0, 3.95f, doorC, 0.02f);
    if(depth < 160)
      for(float y = y0 + 0.25f; y < 3.9f; y += 0.42f) GB(-4.72f, 4.72f, 0.29f, 0.32f, y, y + 0.05f, shade(doorC, 0.8f), 0.03f);
  }
  // The team's name across its front (along its slope), the box's number on a light box under
  // the canopy.
  if(depth < 170 && vdot(vsub(g->cam->eye, GP(0, 0, 4.7f)), n) < 0) {
    begin_item(g, depth - STAND_BIAS - 0.6f);
    float size = 0.62f, w = ftext_w(name, size, 2, 0.04f);
    if(w > 8.6f) size *= 8.6f / w, w = 8.6f;
    V3 rl = vnorm(vcross(n, up)), along = vnorm(vsub(GP(1, 0, 0), GP(-1, 0, 0)));
    V3 rt = vdot(along, rl) >= 0 ? along : vmul(along, -1);
    V3 at = vadd(GP(0, -0.012f, 4.42f), vmul(rt, -w / 2));
    wtext(g, name, at, rt, vmul(up, -1), size, fogged(trim, depth), 2, 0.04f);
  }
  if(mine && depth < 220) {
    V3 bx = GP(0, -2.1f, 0);
    mbox(g, bx, f, n, up, 0.06f, 0.75f, 4.15f, 4.85f, 0xFF14181E, 0.02f);
    pit_limb(g, vadd(bx, v3(0, 4.85f, 0)), vadd(bx, v3(0, 5.05f, 0)), 0.04f, 0.04f, n, 0xFF6A7078);
    char num[4];
    snprintf(num, sizeof(num), "%d", gi - PR_BOX + 1);
    begin_item(g, to_cam(g->cam, bx).z - STAND_BIAS - 0.4f);
    for(int s = -1; s <= 1; s += 2) {
      V3 fc = vmul(f, (float) s);
      V3 c = vadd(vadd(bx, vmul(fc, 0.07f)), v3(0, 4.5f, 0));
      if(vdot(fc, vsub(g->cam->eye, c)) <= 0) continue;
      quad(g, vadd(c, vadd(vmul(n, -0.68f), v3(0, 0.3f, 0))), vadd(c, vadd(vmul(n, 0.68f), v3(0, 0.3f, 0))),
           vadd(c, vadd(vmul(n, 0.68f), v3(0, -0.3f, 0))), vadd(c, vadd(vmul(n, -0.68f), v3(0, -0.3f, 0))), fogged(col, depth));
      pit_sign_words(g, num, vadd(c, vmul(fc, 0.005f)), fc, 0.42f, fogged(trim, depth));
    }
  }
  // The other teams: a mechanic or two in an open garage's mouth.
  if(!mine && door != 2 && depth < 120) {
    for(int k = 0; k < 1 + (int) (h >> 8 & 1); k++) {
      V3 at = GP(-2.0f + k * 2.6f + ((h >> (12 + k * 3)) & 7) * 0.15f, 1.0f + k * 0.6f, 0);
      V3 face = vnorm(vadd(vmul(n, -1), vmul(f, ((h >> (20 + k)) & 1) ? 0.4f : -0.4f)));
      Pose p = pit_pose_stand(at, face, 0, 0);
      crew_member(g, at, face, &p, col, trim, trim, depth < 50 ? 0 : 1);
    }
  }
  g_alpha = 1;
#undef GP
#undef GB
}

// --- The stop, stepped once a game frame -----------------------------------------------------

#define PIT_LIMIT 23.4f          // the lane's speed limit (m/s): 60 on the HUD (the game's km/h for a
                                 // car's world m/s x 2.56), the 60 painted and on the boards
#define PIT_SERVICE 100          // the crew's shortest service (game frames): jacks, wheel guns
#define PIT_LAUNCH 16            // frames from GO to the car moving (jacks down, lollipop up)

// The garage of box b (0: player 1's) and where a car's back is when it stands in it (metres
// from the sign), and its middle (segments from the sign).
static float pit_box_r(int b) { return PR_GAR0 + 4.0f * (PR_BOX + b) + 2; }
static double pit_box_s(const TgTrack3D* tr, int b) { return pit_box_r(b) * tr->scale.seg_len - 2.15; }

// Metres along from the sign's segment to track position pos (round the shorter way).
static double pit_metres(const TgTrack3D* tr, float pos) {
  float len = (float) tr->tables.segments * TG_TRACK3D_SEG_UNITS;
  float d = fmodf(pos - (float) g_pit.S * TG_TRACK3D_SEG_UNITS, len);
  if(d < 0) d += len;
  if(d >= len / 2) d -= len;
  return d / TG_TRACK3D_SEG_UNITS * tr->scale.seg_len;
}

// While no pits are found, the game's object table is looked at again straight from work RAM now
// and then: the track's copy is taken when the course's road is first complete, and in a replay of
// the original's own race code (verify mode) the objects can come in after it (chris_session_03's
// Tokyo stop showed no pit lane).
static void pit_refresh(const TgTrack3D* tr, const uint8_t* wram) {
  static uint32_t tick;
  if(pit_layout(tr)->on || tick++ % 30 || tr->tables.segments <= 0) return;
  static uint16_t obj[TG_TRACK3D_MAX_SEGMENTS];
  for(int i = 0; i < tr->tables.segments; i++) obj[i] = r16(wram, 0x6400 + 2 * (uint32_t) i);
  pit_set(tr, obj);
}

// Game frames until the game's own pit scene stops player p's car ($FFFA), from its state: the
// view's fade out, the scene's set-up and tiles, then the scene's car braking in by its scroll as
// irq.c pit_lane moves it (the same steps, run forward here on copies).
static int pit_frames_to_stop(const uint8_t* m, int p, uint16_t st) {
  int pre = 0;
  uint16_t scroll = r16(m, p ? 0xBC : 0xA8), speed = r16(m, p ? 0x1E5C : 0x1E5A);
  switch((uint16_t) ~st) {
    case 0: {
      uint8_t br = m[p ? 0x65 : 0x52];
      pre = ((br & 0x80) ? 0 : (br & 15)) + 11, scroll = 0x0280, speed = 0x1700;
      break;
    }
    case 1: case 2: pre = 9, scroll = 0x0280, speed = 0x1700; break;
    case 3: pre = 1, scroll = 0x0280, speed = 0x1700; break;
    case 4: break;
    default: return 0;
  }
  int k = 0;
  for(; k < 900; k++) {
    uint16_t a = scroll & 0x03FF;
    if(a >= 0x0188) {
      speed = (uint16_t) (speed + 0x0029);
    } else {
      bool stop = a < 0x0080;
      uint16_t vv = 0;
      if(!stop) {
        vv = (uint16_t) (((uint16_t) (((uint16_t) (a - 0x0200)) >> 2) | 0xC000) + speed);
        stop = (vv & 0x8000) != 0;
      }
      if(stop) break;
      speed = vv;
    }
    scroll = (uint16_t) (scroll - ((speed >> 8) >> 3));
  }
  return pre + k + 1;
}

static void pit_step(TgView3D* v, const uint8_t* wram) {
  const TgTrack3D* tr = &v->track;
  pit_refresh(tr, wram);
  const PitLane* pl = pit_layout(tr);
  float seg = tr->scale.seg_len, hw = tr->scale.half_width, dt = 1 / 60.0f;
  float t = v->period > 0 ? (float) v->since / (float) v->period : 1;
  if(t > 1) t = 1;
  for(int p = 0; p < 2; p++) {
    PitCar* c = &v->pitc[p];
    c->off[0][0] = c->off[1][0], c->off[0][1] = c->off[1][1], c->yaw[0] = c->yaw[1], c->lift[0] = c->lift[1];
    int k = v->cur.view_car[p];
    if(k < 0 || !pl->on || tr->tables.segments <= 0) {
      if(c->ref_ok) memset(c, 0, sizeof(*c));
      continue;
    }
    int id = v->cur.cars[k].id & 31;
    if(c->ref_ok && c->id != id) memset(c, 0, sizeof(*c));  // another car in this view
    uint16_t st = r16(wram, p ? 0x1E88 : 0x1E86);
    bool in = (st & 0x8000) != 0;
    int step = in ? (uint16_t) ~st : -1;  // 0 the stop begun ($FFFF) ... 4 driving in, 5 refuelling, 6+ out
    float pos, side;
    car_now_raw(v, k, t, &pos, &side);
    double rs = pit_metres(tr, pos);
    float rxprev = c->ref_ok ? c->ref_x : 0;
    float rx = pl->sgn * side / TG_TRACK3D_SIDE_EDGE * hw;
    if(c->ref_ok) {
      float dv = (float) ((rs - c->ref_s) / dt);
      if(fabsf(dv) < 150) c->ref_v += (dv - c->ref_v) * 0.3f;
    } else {
      c->ref_v = v->car_ms[id];
    }
    c->ref_s = rs, c->ref_x = rx, c->ref_ok = true, c->id = id, c->st = st;
    double before = c->s;
    float xbefore = c->x;
    if(c->phase == PIT_OFF) {
      if(c->stops > 0 && c->t_go < 100000) c->t_go++;  // (the crew walking back keeps its clock)
      if(!(in && step <= 4 && fabs(rs) < 150)) {
        c->off[1][0] = c->off[1][1] = 0, c->yaw[1] = 0, c->lift[1] = 0;
        spring_step(c->cam, 0, 2.5f, dt);
        continue;
      }
      // The stop begins: from the game's place, at the game's speed (its speed word: a car run into
      // the pit boards at the sign is slowed there and then), and no faster than can still stop in
      // its box (the game's cars reach ~100 m/s in the world's metres).
      c->phase = PIT_IN, c->box = p, c->t = c->t_box = c->t_go = c->t_fill = c->t_in = 0, c->go = c->full = false;
      uint16_t sw = v->pspeed[p];
      float vw = (sw & 0x8000) ? 0 : sw * (60.0f / 512.0f / TG_TRACK3D_SEG_UNITS * seg);
      c->vel = fminf(vw, sqrtf(2 * 40.0f * fmaxf((float) (pit_box_s(tr, p) - rs), 1.0f)));
      c->s = before = rs, c->x = xbefore = c->x0 = rx, c->s0 = (float) rs, c->acc = 0;
      c->fuel_start = v->fuel_word[p] * 100.0f / 0xA7FF;
      c->yaw[0] = c->yaw[1] = v->car_yaw[id];
      c->stops++;
    }
    double sBox = pit_box_s(tr, c->box);
    if(step == 5) c->t_fill++;
    if(c->phase == PIT_IN) {
      c->t++;
      if(!in || step >= 6) {
        // Let go before it stood (the player drove through): on, no stop, back over to the fast
        // lane from where it is (the crew back to the garage).
        c->phase = PIT_OUT, c->go = c->full = true, c->t_go = PIT_LAUNCH, c->t_in = c->t;
        c->s0 = (float) c->s, c->x0 = c->x;
      } else {
        // Arrive at the box with no speed when the game's scene stops its car: on the cubic that
        // starts at this place and speed and ends there (recomputed each frame); braking to stop
        // early and wait if that would overshoot (a fast entry).
        float d = (float) (sBox - c->s);
        int T = step >= 5 ? 1 : pit_frames_to_stop(wram, p, st);
        float Ts = (float) (T > 1 ? T : 1) * dt, vv = c->vel, a;
        if(d <= 0.02f) a = 0;
        else if(Ts < 0.12f || d < vv * Ts / 3) a = -vv * vv / (2 * d);
        else a = 2 * (3 * d - 2 * vv * Ts) / (Ts * Ts);
        a = clampf(a, -40, 7);
        if(vv > 24 && a > 0) a = 0;
        c->vel = fmaxf(vv + a * dt, 0);
        if(d > 0.05f && c->vel < 0.8f) c->vel = fminf(0.8f, sqrtf(6 * d));  // (never short of the box)
        c->s += c->vel * dt;
        c->acc = a;
        if(c->s >= sBox - 0.01) c->s = sBox, c->vel = 0, c->acc = 0, c->phase = PIT_BOX, c->t_box = 0;
        // Across: out over the line into the fast lane before the wall, then into the box.
        float sw = (PR_WALL0 - 0.5f) * seg, L1 = fmaxf(16.0f, sw - c->s0);
        float u1 = pit_smooth(((float) c->s - c->s0) / L1), u2 = pit_smooth(((float) c->s - ((float) sBox - 13)) / 13);
        c->x = c->x0 + (pl->fast - c->x0) * u1 + (pl->boxc - pl->fast) * u2;
      }
    } else if(c->phase == PIT_BOX) {
      c->t++, c->t_box++;
      c->s = sBox, c->vel = 0, c->acc = 0, c->x = pl->boxc;
      bool left = !in || step >= 6;  // the game is done with it: refuelled, or let go
      if(left) c->full = true;
      if((left && c->t_box >= PIT_SERVICE) || !in) c->phase = PIT_OUT, c->go = true, c->t_go = 0, c->s0 = (float) sBox, c->x0 = pl->boxc;
    } else {
      c->t++, c->t_go++;
      if(c->phase == PIT_OUT && !in) c->phase = PIT_JOIN, c->t_join = 0, c->jlock = false;
      if(c->phase == PIT_OUT) {
        // After the jacks are down and the lollipop is up: off at the limit, then away.
        if(c->t_go > PIT_LAUNCH) {
          float vt = c->s < PR_LIM1 * seg ? PIT_LIMIT : 45;
          float a = clampf((vt - c->vel) / dt, -6, 6);
          c->vel += a * dt, c->s += c->vel * dt, c->acc = a;
        }
      } else {
        // Back onto the game's place (the game has given the car back: it moves on from where it was
        // held, at its pit scene's speed). The car as drawn closes on it as a car driving: the gap
        // (D, + ahead) is made to close no faster than a steady 4 m/s^2 would stop it (the "arrive"
        // steering behaviour, on the gap and the two speeds - the game's from its speed word, at
        // once, a car speed / 512 track units a frame - with the game's own acceleration fed
        // forward), its acceleration within -30..+25 m/s^2 (the game's cars accelerate up to ~25 in
        // the world's metres), never backwards; once close and as fast, what is left of the gap is
        // let go in a few frames.
        c->t_join++;
        uint16_t sw = v->pspeed[p];
        float vg0 = c->vg;
        c->vg = (sw & 0x8000) ? 0 : sw * (60.0f / 512.0f / TG_TRACK3D_SEG_UNITS * seg);
        c->ag = c->t_join > 1 ? c->ag + (clampf((c->vg - vg0) / dt, -40, 40) - c->ag) * 0.3f : 0;
        float D = (float) (c->s - rs), dD = c->vel - c->vg;
        if(!c->jlock && fabsf(D) < 0.3f && fabsf(dD) < 1.5f) c->jlock = true, c->jdel = D;
        if(c->jlock) {
          c->jdel *= 0.82f;
          double ns = rs + c->jdel;
          float nv = fmaxf(0, (float) ((ns - c->s) / dt));
          c->acc += (clampf((nv - c->vel) / dt, -30, 25) - c->acc) * 0.3f;
          c->vel = nv, c->s = ns;
        } else {
          float want = (D > 0 ? -1.0f : 1.0f) * fminf(30.0f, sqrtf(2 * 4.0f * fabsf(D)));
          float a = clampf(c->ag + 3.0f * (want - dD), -30, 25);
          c->vel = fmaxf(0, c->vel + a * dt), c->s += c->vel * dt, c->acc = a;
        }
      }
      // Across: out of the box (or from where a drive-through was let go) into the fast lane, along
      // it, down the exit onto the road; out on the road, over to where the game has the car.
      float s = (float) c->s, e0 = (PR_WALL1 - 4) * seg, e1 = (PR_EXIT1 - 2) * seg, xe = 0.7f * hw;
      float x = pl->fast + (c->x0 - pl->fast) * (1 - pit_smooth((s - c->s0) / 12));
      x += (xe - pl->fast) * pit_smooth((s - e0) / (e1 - e0));
      if(c->phase == PIT_JOIN && s > e1) c->x = rx + (c->x - rxprev) * 0.93f;  // (the offset across let go, the game's steering followed)
      else c->x = x;
      if(c->phase == PIT_JOIN && c->jlock && fabsf(c->jdel) < 0.01f && s > e1 && fabsf(c->x - rx) < 0.03f) c->phase = PIT_OFF;
      if(c->t > 60 * 90) c->phase = PIT_OFF;  // (never held for ever)
    }
    // Drawn as the offset from the game's place; turned the way it moves; on the jacks in the box.
    if(c->phase == PIT_OFF) {
      c->off[1][0] = c->off[1][1] = 0, c->yaw[1] = 0, c->lift[1] = 0;
    } else {
      c->off[1][0] = (float) ((c->s - rs) / seg * TG_TRACK3D_SEG_UNITS);
      c->off[1][1] = pl->sgn * (c->x - rx) / hw * TG_TRACK3D_SIDE_EDGE;
      float ds = (float) (c->s - before), dx = c->x - xbefore;
      float yt = ds > 0.004f ? clampf(atan2f(pl->sgn * dx, ds), -0.45f, 0.45f) : 0;
      c->yaw[1] += (yt - c->yaw[1]) * (ds > 0.004f ? 0.3f : 0.1f);
      float lift = 0;
      if(c->phase == PIT_BOX) lift = 0.075f * pit_smooth((c->t_box - 8) / 6.0f);
      else if(c->phase == PIT_OUT && c->t_go < 6 && c->t_box > 0) lift = 0.075f * (1 - c->t_go / 6.0f);
      c->lift[1] = lift;
      v->car_yaw[id] = v->car_yaw_to[id] = c->yaw[1];
      v->car_brake[id] = c->acc < -2.5f ? 3 : 0;
    }
    // The camera's framing while it stands in the box (and until it moves off).
    bool still = c->phase == PIT_BOX || (c->phase == PIT_OUT && c->t_go <= PIT_LAUNCH && c->t_box > 0);
    spring_step(c->cam, still ? 1.0f : 0.0f, 2.2f, dt);
    static int trace = -1;
    if(trace < 0) trace = getenv("TG_PIT_TRACE") != NULL;
    if(trace && c->phase != PIT_OFF)  // PT frame car phase state: drawn along, speed, across; the game's along, across
      fprintf(stderr, "PT %u %d ph %d st %04X s %.2f v %.2f a %.1f x %.2f ref %.2f %.2f rv %.1f tb %d tg %d\n", v->frames, p, c->phase,
              st, c->s, c->vel, c->acc, c->x, rs, rx, c->ref_v, c->t_box, c->t_go);
  }
}

// The chase camera's pit framing for car id (0-1).
static float pit_framing(const TgView3D* v, int id) {
  for(int p = 0; p < 2; p++)
    if(v->pitc[p].ref_ok && v->pitc[p].id == (id & 31)) return clampf(v->pitc[p].cam[0], 0, 1);
  return 0;
}

// The jacks' lift for car id (m); its wheels turned by the wheel guns (metres rolled added).
static float pit_lift(const TgView3D* v, int id, float* metres) {
  const PitCar* c = pit_car(v, id);
  if(!c) return 0;
  float u = v->pit_sub, l = c->lift[1] + (c->lift[1] - c->lift[0]) * u;
  if(c->t_box > 0) {
    float tb = c->phase == PIT_BOX ? c->t_box + u : (float) c->t_box;
    // The guns off (frames 18-34) and on again (64-80): the wheels spin.
    float spin = clampf(tb - 18, 0, 16) + clampf(tb - 64, 0, 16);
    *metres += spin * 0.27f;
  }
  return l < 0 ? 0 : l;
}

// Whether a thing standing at p (radius r) comes between the chase camera and the view's car: in
// the cone the car is seen in from the eye, nearer than its tail (occluder_setup's line and body).
static bool pit_covers_car(V3 p, float r) {
  if(!g_occ_on) return false;
  float dx = p.x - g_occ_eye.x, dz = p.z - g_occ_eye.z;
  float a = dx * g_occ_ux + dz * g_occ_uz;
  if(a < 0.5f || a > g_occ_mid - 2.0f) return false;
  float b = dx * -g_occ_uz + dz * g_occ_ux, k = a / g_occ_mid;
  return b + r > g_occ_b0 * k && b - r < g_occ_b1 * k;
}

enum { CR_LOLLI, CR_JACKF, CR_JACKR, CR_GUN, CR_TYRE, CR_FUEL };
// Each member's place round a car standing in the box (metres along from its middle, out towards
// the garage) and job.
static const struct { float a, n; int role; } kCrew[12] = {
  {4.4f, 1.0f, CR_LOLLI}, {3.3f, 0, CR_JACKF}, {-3.35f, 0, CR_JACKR},
  {1.35f, -1.45f, CR_GUN}, {1.35f, 1.45f, CR_GUN}, {-1.35f, -1.45f, CR_GUN}, {-1.35f, 1.45f, CR_GUN},
  {2.15f, -1.95f, CR_TYRE}, {2.15f, 1.95f, CR_TYRE}, {-2.2f, -1.95f, CR_TYRE}, {-2.2f, 1.95f, CR_TYRE},
  {-0.35f, 1.5f, CR_FUEL},
};

// Box b's crew, where the stop has got to.
static void pit_crew(Gather* g, TgView3D* v, const TgTrack3D* tr, int b) {
  const PitLane* pl = &g_pit;
  const PitCar* c = &v->pitc[b];
  float rc = pit_box_r(b);
  V3 C = pit_at(tr, rc, pl->boxc), f, n, up = v3(0, 1, 0);
  pit_dirs(tr, rc, &f, &n);
  float depth = to_cam(g->cam, C).z;
  if(depth < -25 || depth > 170) return;
  int lod = depth < 55 ? 0 : 1;
  uint32_t col, trim;
  const char* nm;
  char buf[24];
  pit_team(v, PR_BOX + b, &col, &trim, &nm, buf, sizeof(buf));
  float u = v->pit_sub;
  // Where the stop is: frames into the drive in (crew walking out), in the box, since GO.
  bool active = c->phase != PIT_OFF || c->stops > 0;
  float tIn = -1, tBox = -1, tGo = -1;
  if(c->phase == PIT_IN) tIn = c->t + u;
  else if(c->phase == PIT_BOX) tIn = 1e4f, tBox = c->t_box + u;
  else if(active) tIn = 1e4f, tBox = c->t_box > 0 ? (float) c->t_box : -1, tGo = c->t_go + u;
  bool served = tBox >= 0;
  bool fuelling = c->phase == PIT_BOX && !c->full && tBox > 4;
  float gar = pl->gar - pl->boxc;  // the garage's front, out from the car's middle
  // (a place round the box: on the ground there - the lane may climb)
  float sl = tr->scale.seg_len;
#define CP(a, nn) pit_at(tr, rc + (a) / sl, pl->boxc + (nn))
  V3 tank = CP(-3.4f, gar + 0.55f);
  for(int i = 0; i < 12; i++) {
    int role = kCrew[i].role;
    float sa = kCrew[i].a, sn = kCrew[i].n;
    if(role == CR_JACKR && tBox < 0) sn = -1.3f;  // (beside the box till the car is in)
    V3 station = CP(sa, sn);
    V3 sface = role == CR_LOLLI || role == CR_JACKF ? vmul(f, -1) : role == CR_JACKR ? f : role == CR_FUEL ? vmul(n, -1) : vmul(n, sn > 0 ? -1.0f : 1.0f);
    if(role == CR_JACKR && tBox >= 0 && tBox < 10 && tGo < 0) station = CP(sa, -1.3f * (1 - tBox / 10));
    V3 idle = CP(-3.6f + (i % 6) * 1.45f, gar + 0.9f + (i / 6) * 1.1f), iface = vmul(n, -1);
    V3 pos = idle, face = iface;
    float walk = 0, phase = 0;
    if(tGo >= 0 && !served) {  // let go before the box (a drive-through): from where it had got, back in at once
      float t0 = 12 + 3.0f * i, dist = sqrtf(vdot(vsub(station, idle), vsub(station, idle))), tw = dist / 2.2f * 60;
      float k0 = clampf((c->t_in - t0) / fmaxf(tw, 1), 0, 1), d0 = k0 * dist;
      V3 from = vadd(idle, vmul(vsub(station, idle), k0));
      float k = clampf((tGo - PIT_LAUNCH) / fmaxf(d0 / 1.8f * 60, 1), 0, 1);
      pos = vadd(from, vmul(vsub(idle, from), k));
      if(k < 1 && d0 > 0.05f) walk = 1, phase = k * d0 / 0.75f * PI, face = vnorm(vsub(idle, from));
    } else if(tGo >= 0) {  // after GO: a step back from the car, then back to the garage
      V3 away = vsub(station, C);
      away.y = 0;
      away = vnorm(role == CR_JACKF ? f : role == CR_JACKR ? vmul(f, -1) : away);
      V3 back = vadd(station, vmul(away, 0.7f));
      float k1 = pit_smooth(tGo / 14);
      pos = vadd(station, vmul(away, 0.7f * k1)), face = sface;
      float t0 = 60 + 2.0f * i, dist = sqrtf(vdot(vsub(idle, back), vsub(idle, back))), tw = dist / 1.5f * 60;
      if(tGo > t0) {
        float k = clampf((tGo - t0) / fmaxf(tw, 1), 0, 1);
        pos = vadd(back, vmul(vsub(idle, back), k));
        if(k < 1) walk = 1, phase = k * dist / 0.75f * PI, face = vnorm(vsub(idle, back));
        else face = iface;
      }
    } else if(tIn >= 0) {  // the car coming in: out to the stations
      float t0 = 12 + 3.0f * i, dist = sqrtf(vdot(vsub(station, idle), vsub(station, idle))), tw = dist / 2.2f * 60;
      float k = clampf((tIn - t0) / fmaxf(tw, 1), 0, 1);
      pos = vadd(idle, vmul(vsub(station, idle), k));
      if(k > 0 && k < 1) walk = 1, phase = k * dist / 0.75f * PI, face = vnorm(vsub(station, idle));
      else if(k >= 1) face = sface;
    }
    if(walk == 0 && tIn < 0 && tGo < 0) {  // idle: a little sway
      float tt = (v->frames + u) / 60.0f + i * 1.7f;
      pos = vadd(pos, vmul(n, 0.04f * sinf(tt * 0.9f)));
    }
    face.y = 0;
    face = vnorm(face);
    V3 r = vnorm(vcross(face, up));
    // (one standing between the chase camera and the view's car is seen through, as a rival is)
    g_door = pit_covers_car(pos, 0.4f) ? 9 : 0;
    Pose p = pit_pose_stand(pos, face, walk, phase);
    bool atWork = tIn >= 1e4f - 1 && (tGo < 0 || tGo < 14) && walk == 0;  // at the station with the car in
    bool atStation = walk == 0 && tIn >= 0 && (tGo < 0 || tGo < 60);
    // The job.
    if(role == CR_GUN) {
      V3 hub = CP(sa, sn > 0 ? 0.93f : -0.93f);
      hub.y += 0.34f + (c->phase == PIT_BOX ? c->lift[1] : 0);
      float kn = atWork && served ? pit_smooth((tBox - 2) / 6) * (tGo >= 0 ? 1 - pit_smooth(tGo / 8) : 1) : 0;
      if(kn > 0.01f) {
        p.kneel = kn > 0.5f, p.hip = 0.92f - 0.42f * kn, p.lean = 0.3f * kn;
        p.foot[0] = vadd(pos, vadd(vmul(face, 0.4f * kn), vmul(r, -0.13f)));
        V3 gun = vadd(hub, vmul(face, -0.22f));
        p.hand[0] = vadd(gun, vadd(vmul(r, -0.07f), vmul(face, -0.12f)));
        p.hand[1] = vadd(gun, vadd(vmul(r, 0.07f), vmul(face, -0.12f)));
        if(!lod) {
          pit_limb(g, gun, vadd(gun, vmul(face, -0.32f)), 0.11f, 0.11f, r, 0xFF30343A);
          pit_limb(g, vadd(gun, vmul(face, -0.32f)), vadd(vadd(pos, vmul(face, -0.9f)), v3(0, 0.03f, 0)), 0.04f, 0.04f, r, 0xFF101214);
        }
      } else if(atStation && !lod) {  // the gun ready in its hands
        V3 gun = vadd(pos, vadd(vmul(face, 0.35f), vmul(up, 0.85f)));
        p.hand[0] = vadd(gun, vmul(r, -0.08f)), p.hand[1] = vadd(gun, vmul(r, 0.08f));
        pit_limb(g, gun, vadd(gun, vmul(face, 0.28f)), 0.11f, 0.11f, r, 0xFF30343A);
      }
    } else if(role == CR_TYRE) {
      float in = atWork && served && tGo < 0 ? pit_smooth((tBox - 26) / 10) * (1 - pit_smooth((tBox - 60) / 10)) : 0;
      V3 at = vadd(pos, vmul(face, 0.35f * in));
      p = pit_pose_stand(at, face, walk, phase);
      V3 tc = vadd(at, vadd(vmul(face, 0.42f), vmul(up, 0.72f)));
      p.hand[0] = vadd(tc, vmul(r, -0.24f)), p.hand[1] = vadd(tc, vmul(r, 0.24f));
      p.lean = 0.12f;
      if(depth < 120) pit_tyre(g, tc, r, face, up, lod ? 0 : trim);
    } else if(role == CR_JACKF || role == CR_JACKR) {
      // The jack: its head under the car's nose (or tail) once it is in, its handle to the hands.
      float reach = 1.05f;
      if(!(atWork && served)) reach = atStation ? 0.55f : 0.7f;
      else if(tGo >= 0) reach = 1.05f - 0.5f * pit_smooth(tGo / 8);
      else reach = 0.55f + 0.5f * pit_smooth(tBox / 5);
      V3 head = vadd(pos, vmul(face, reach));
      head.y += 0.06f + (atWork && served && c->phase == PIT_BOX ? c->lift[1] : 0);
      V3 grip = vadd(pos, vadd(vmul(face, 0.3f), vmul(up, 0.95f)));
      p.hand[0] = vadd(grip, vmul(r, -0.12f)), p.hand[1] = vadd(grip, vmul(r, 0.12f));
      p.lean = 0.22f;
      if(depth < 120) {
        mbox(g, vadd(head, v3(0, -0.06f, 0)), face, r, up, 0.28f, 0.2f, 0, 0.1f, 0xFF2A2E34, 0.01f);
        mbox(g, vadd(head, v3(0, -0.06f, 0)), face, r, up, 0.1f, 0.22f, 0.1f, 0.14f, col, 0.012f);
        pit_limb(g, vadd(head, vmul(face, -0.2f)), grip, 0.06f, 0.06f, r, 0xFF8E949C);
      }
    } else if(role == CR_FUEL) {
      V3 filler = CP(-0.35f, 0.92f);
      filler.y += 0.85f + (c->phase == PIT_BOX ? c->lift[1] : 0);
      V3 nozzle;
      if(fuelling && atWork) {
        nozzle = vadd(filler, vmul(n, 0.08f));
        p.hand[0] = vadd(nozzle, vadd(vmul(r, -0.06f), vmul(face, -0.15f)));
        p.hand[1] = vadd(nozzle, vadd(vmul(r, 0.06f), vmul(face, -0.25f)));
        p.lean = 0.2f;
      } else {
        nozzle = vadd(pos, vadd(vmul(face, 0.4f), vmul(up, 1.0f)));
        p.hand[0] = vadd(nozzle, vmul(r, -0.08f)), p.hand[1] = vadd(nozzle, vmul(r, 0.08f));
      }
      if(depth < 130) {
        // The hose: from the rig's boom in the garage mouth, sagging, to the nozzle.
        V3 top = vadd(tank, v3(0, 2.6f, 0));
        V3 c1 = vadd(top, vadd(vmul(vsub(nozzle, top), 0.35f), v3(0, -0.2f, 0))), c2 = vadd(nozzle, v3(0, 0.9f, 0));
        V3 prev = top;
        for(int s2 = 1; s2 <= 7; s2++) {
          float tt = s2 / 7.0f, it = 1 - tt;
          V3 q = vadd(vadd(vmul(top, it * it * it), vmul(c1, 3 * it * it * tt)), vadd(vmul(c2, 3 * it * tt * tt), vmul(nozzle, tt * tt * tt)));
          pit_limb(g, prev, q, 0.07f, 0.07f, r, 0xFF1A1C20);
          prev = q;
        }
        pit_limb(g, nozzle, vadd(nozzle, vmul(face, -0.3f)), 0.1f, 0.1f, r, 0xFF8A9098);
      }
    } else if(role == CR_LOLLI) {
      // The lollipop: BRAKE over the car's nose while it stands, GO - lifted - once it may go.
      bool goNow = c->go && tGo >= 0;
      float lift = goNow ? pit_smooth(tGo / 10) : 0;
      V3 grip = vadd(pos, vadd(vmul(face, 0.32f), vmul(up, 1.18f)));
      // (held out over the car's nose, in front of the driver, high enough to be seen over the car)
      V3 over = CP(2.75f, 0.05f);
      over.y += 1.75f;
      V3 dir = vnorm(vsub(over, grip));
      dir = vnorm(vadd(vmul(dir, 1 - lift), vmul(up, lift * 1.4f)));
      bool idle0 = tIn < 0 || (tGo >= 0 && (!served || tGo > 60 + 2.0f * i));
      if(idle0 || walk > 0) dir = up, grip = vadd(pos, vadd(vmul(r, 0.3f), vmul(up, 1.0f)));
      V3 board = vadd(grip, vmul(dir, 1.7f));
      p.hand[0] = vadd(grip, vmul(dir, -0.25f)), p.hand[1] = vadd(grip, vmul(dir, 0.15f));
      if(idle0 || walk > 0) p.hand[0] = vadd(pos, vadd(vmul(r, -0.3f), vmul(up, 0.85f))), p.hand[1] = grip;
      if(depth < 130) {
        pit_limb(g, vadd(grip, vmul(dir, -0.3f)), board, 0.045f, 0.045f, r, 0xFFD8DCE0);
        V3 bface = idle0 || walk > 0 ? vmul(face, -1) : vmul(f, -1);  // (its words to the driver)
        begin_item(g, to_cam(g->cam, board).z - STAND_BIAS - 0.3f);
        uint32_t bc = goNow ? 0xFF1EA84A : 0xFFD8262E;
        pit_disc(g, board, bface, up, 0.36f, fogged(0xFFF4F4F4, depth), 14);
        pit_disc(g, vadd(board, vmul(bface, 0.006f)), bface, up, 0.32f, fogged(bc, depth), 14);
        pit_disc(g, vadd(board, vmul(bface, -0.01f)), vmul(bface, -1), up, 0.36f, fogged(0xFF2A2E34, depth), 14);
        if(!lod) pit_sign_words(g, goNow ? "GO" : "BRAKE", vadd(board, vmul(bface, 0.01f)), bface, goNow ? 0.26f : 0.15f, 0xFFFFFFFF);
      }
    }
    crew_member(g, pos, face, &p, col, trim, trim, lod);
    g_door = 0;
  }
  // The fuel rig: a tank in the garage's mouth with a boom over the lane.
  if(depth < 140) {
    frustum(g, tank, up, f, n, 0.32f, 0.3f, 1.7f, 8, col, 0.01f);
    frustum(g, vadd(tank, v3(0, 1.1f, 0)), up, f, n, 0.33f, 0.33f, 0.22f, 8, trim, 0.012f);
    pit_limb(g, vadd(tank, v3(0, 1.7f, 0)), vadd(tank, v3(0, 2.65f, 0)), 0.08f, 0.08f, f, 0xFF8E949C);
  }
#undef CP
}

// --- The pit lane drawn: wall, signs, garages, crews -----------------------------------------

static void pit_props(Gather* g, TgView3D* v, const TgTrack3D* tr) {
  const PitLane* pl = pit_layout(tr);
  if(!pl->on) return;
  const Cam* cam = g->cam;
  float tanx = cam->w * 0.5f / cam->f;
  V3 up = v3(0, 1, 0);
  font_ready(v);
  bool any = false;
  for(int r = PR_ENT0; r <= PR_EXIT1; r++) {
    V3 mid = pit_at(tr, r + 0.5f, (pl->wall + pl->gar) / 2);
    V3 cc = to_cam(cam, mid);
    if(cc.z < -40 || cc.z > 430 || fabsf(cc.x) > cc.z * tanx + 34) continue;
    any = true;
    float depth = cc.z, rf = (float) r;
    V3 f, n;
    pit_dirs(tr, rf + 0.5f, &f, &n);
    if(r >= PR_WALL0 && r < PR_WALL1) {
      // The pit wall: concrete, its coping red and white, sponsors on its track side; catch
      // fencing on it (posts every 5 m, the mesh seen through).
      V3 c[8];
      float xs[2] = {pl->wall, pl->wall + 0.45f};
      c[0] = pit_at(tr, rf, xs[0]), c[1] = pit_at(tr, rf, xs[1]), c[2] = pit_at(tr, rf + 1, xs[1]), c[3] = pit_at(tr, rf + 1, xs[0]);
      for(int k = 0; k < 4; k++) c[k + 4] = vadd(c[k], vmul(up, 1.0f));
      hexa(g, c, 0xFFC4C8CC, 0);
      V3 t[8];
      for(int k = 0; k < 4; k++) t[k] = c[k + 4], t[k + 4] = vadd(c[k + 4], vmul(up, 0.1f));
      t[0] = vadd(t[0], vmul(n, -0.03f)), t[3] = vadd(t[3], vmul(n, -0.03f)), t[4] = vadd(t[4], vmul(n, -0.03f)), t[7] = vadd(t[7], vmul(n, -0.03f));
      hexa(g, t, (r & 1) ? 0xFFD8262E : 0xFFF2F2F2, 0.01f);
      if(depth < 260) {
        int q = (r - PR_WALL0) % 4;
        if(q < 2 && vdot(vsub(cam->eye, c[0]), n) < 0) {  // a sponsor's panel over two segments, facing the track
          int sp = ((r - PR_WALL0) / 4 + v->course) % 10;
          begin_item(g, depth - STAND_BIAS - 0.4f);
          V3 o = vmul(n, -0.012f);
          V3 a0 = vadd(pit_at(tr, rf, pl->wall), o), a1 = vadd(pit_at(tr, rf + 1, pl->wall), o);
          quad(g, vadd(a0, vmul(up, 0.15f)), vadd(a1, vmul(up, 0.15f)), vadd(a1, vmul(up, 0.88f)), vadd(a0, vmul(up, 0.88f)),
               fogged(kTeams[sp].col, depth));
          if(q == 1 && depth < 120) {
            V3 at = vadd(pit_at(tr, rf, pl->wall), vadd(o, vmul(up, 0.51f)));
            pit_sign_words(g, kTeams[sp].name, at, vmul(n, -1), 0.4f, fogged(kTeams[sp].trim, depth));
          }
        }
        if(((r - PR_WALL0) & 1) == 0) mbox(g, vadd(pit_at(tr, rf, pl->wall + 0.22f), vmul(up, 1.1f)), f, n, up, 0.05f, 0.05f, 0, 2.6f, 0xFF7A8088, 0.01f);
        begin_item(g, depth - 0.3f);
        V3 m0 = pit_at(tr, rf, pl->wall + 0.2f), m1 = pit_at(tr, rf + 1, pl->wall + 0.2f);
        quad(g, vadd(m0, vmul(up, 1.12f)), vadd(m1, vmul(up, 1.12f)), vadd(m1, vmul(up, 3.65f)), vadd(m0, vmul(up, 3.65f)),
             with_alpha(fogged(0xFFB4BCC4, depth), 64));
        V3 rl[8];
        rl[0] = vadd(m0, vmul(up, 3.62f)), rl[1] = vadd(rl[0], vmul(n, 0.06f)), rl[3] = vadd(m1, vmul(up, 3.62f)), rl[2] = vadd(rl[3], vmul(n, 0.06f));
        for(int k = 0; k < 4; k++) rl[k + 4] = vadd(rl[k], vmul(up, 0.06f));
        hexa(g, rl, 0xFF7A8088, 0.02f);
      }
    }
    // The wall's noses: crash cushions in yellow and black.
    if(r == PR_WALL0 || r == PR_WALL1) {
      float s = r == PR_WALL0 ? -1.0f : 1.0f;
      for(int k = 0; k < 3; k++) {
        V3 b = pit_at(tr, rf + s * (0.12f + k * 0.17f), pl->wall + 0.22f);
        mbox(g, b, f, n, up, 0.2f, 0.34f, 0, 0.95f, (k & 1) ? 0xFF1A1C20 : 0xFFF2C21E, 0.01f * k);
      }
    }
    // The canopy over the boxes' garage side, down-lights under it; faded if the chase camera comes
    // up into it (on a climb the camera rides high over the lane behind the car).
    if(r >= PR_GAR0 && r < PR_GAR1) {
      static FadeMem canopyMem[2][PR_NGAR * 4];
      float x0 = pl->gar - 2.4f, x1 = pl->gar + 0.3f;
      V3 cm = vadd(pit_at(tr, rf + 0.5f, (x0 + x1) / 2), vmul(up, 5.2f));
      g_alpha = fade_mem(&canopyMem[g_view_k][(r - PR_GAR0) % (PR_NGAR * 4)], near_fade(cam, vadd(cm, v3(0, -0.5f, 0)), 1.6f, 1.0f, false));
      if(g_alpha > 0.02f) {
        V3 c[8];
        c[0] = pit_at(tr, rf, x0), c[1] = pit_at(tr, rf, x1), c[2] = pit_at(tr, rf + 1, x1), c[3] = pit_at(tr, rf + 1, x0);
        for(int k = 0; k < 4; k++) c[k + 4] = vadd(c[k], vmul(up, 5.38f)), c[k] = vadd(c[k], vmul(up, 5.12f));
        hexa(g, c, 0xFFE8ECF0, 0.01f);
        V3 e[8];
        e[0] = pit_at(tr, rf, x0 - 0.12f), e[1] = pit_at(tr, rf, x0 + 0.06f), e[2] = pit_at(tr, rf + 1, x0 + 0.06f), e[3] = pit_at(tr, rf + 1, x0 - 0.12f);
        for(int k = 0; k < 4; k++) e[k + 4] = vadd(e[k], vmul(up, 5.42f)), e[k] = vadd(e[k], vmul(up, 4.92f));
        hexa(g, e, 0xFF26303C, 0.02f);
        if(depth < 200) {
          V3 lc = vadd(pit_at(tr, rf + 0.5f, pl->gar - 1.2f), vmul(up, 5.1f));
          begin_item(g, depth - 0.5f);
          quad(g, vadd(lc, vmul(f, -0.5f)), vadd(lc, vmul(n, 0.25f)), vadd(lc, vmul(f, 0.5f)), vadd(lc, vmul(n, -0.25f)), g_night ? 0xFFFFF6D8 : 0xFFF4F4EC);
          if(g_night && (r & 1)) billboard_tint(g, vadd(lc, v3(0, -2.6f, 0)), 3.0f, 2.4f, TEX_GLOW, 0x70FFF0C8);
        }
      }
      g_alpha = 1;
    }
    if(r >= PR_GAR0 && r < PR_GAR1 && ((r - PR_GAR0) & 3) == 0) pit_garage(g, v, tr, (r - PR_GAR0) / 4);
    // Over the garages, the pit building's upper floor: a band of dark glass set back, a roof edge.
    if(r >= PR_GAR0 && r < PR_GAR1) {
      V3 c[8];
      float x0 = pl->gar + 1.2f, x1 = pl->gar + 9.4f;
      c[0] = pit_at(tr, rf, x0), c[1] = pit_at(tr, rf, x1), c[2] = pit_at(tr, rf + 1, x1), c[3] = pit_at(tr, rf + 1, x0);
      for(int k = 0; k < 4; k++) c[k + 4] = vadd(c[k], vmul(up, 7.9f)), c[k] = vadd(c[k], vmul(up, 5.4f));
      hexa(g, c, g_night ? 0xFF3A4A62 : 0xFF2A4058, 0.01f);
      V3 e[8];
      e[0] = pit_at(tr, rf, x0 - 0.25f), e[1] = pit_at(tr, rf, x1), e[2] = pit_at(tr, rf + 1, x1), e[3] = pit_at(tr, rf + 1, x0 - 0.25f);
      for(int k = 0; k < 4; k++) e[k + 4] = vadd(e[k], vmul(up, 8.3f)), e[k] = vadd(e[k], vmul(up, 7.9f));
      hexa(g, e, 0xFFE8ECF0, 0.02f);
      if(g_night && depth < 300 && (r & 1)) {  // its lights on at night
        begin_item(g, depth - STAND_BIAS - 0.2f);
        V3 w0 = vadd(pit_at(tr, rf + 0.2f, x0 - 0.02f), vmul(up, 6.0f)), w1 = vadd(pit_at(tr, rf + 0.8f, x0 - 0.02f), vmul(up, 6.0f));
        quad(g, w0, w1, vadd(w1, vmul(up, 1.3f)), vadd(w0, vmul(up, 1.3f)), 0xFFFFE6B0);
      }
    }
    // The entry's gantry: PIT LANE and the limit over the lane, high (a chase camera on a climb
    // rides high), faded if the camera still comes up to it.
    if(r == PR_LIM0 - 1) {
      V3 p0 = pit_at(tr, rf + 0.5f, pl->wall + 0.22f), p1 = pit_at(tr, rf + 0.5f, pl->gar + 0.35f);
      V3 m = vmul(vadd(p0, p1), 0.5f);
      float half = sqrtf(vdot(vsub(p1, p0), vsub(p1, p0))) / 2;
      static FadeMem gantryMem[2];
      g_alpha = fade_mem(&gantryMem[g_view_k], near_fade(cam, vadd(m, vmul(up, 6.5f)), half, 1.2f, false));
      if(g_alpha > 0.02f) {
        mbox(g, p0, f, n, up, 0.13f, 0.13f, 1.0f, 7.5f, 0xFF8E949C, 0);
        mbox(g, p1, f, n, up, 0.13f, 0.13f, 0, 7.5f, 0xFF8E949C, 0);
        mbox(g, m, f, n, up, 0.16f, half + 0.1f, 6.6f, 7.5f, 0xFF16263E, 0.01f);
        if(depth < 260) {
          V3 face = vmul(f, -1), rt = vnorm(vcross(up, face));
          begin_item(g, depth - STAND_BIAS - 0.5f);
          pit_sign_words(g, "PIT LANE", vadd(vadd(m, vmul(face, 0.17f)), vadd(vmul(up, 7.05f), vmul(rt, -0.6f))), face, 0.48f, fogged(0xFFF4F6F8, depth));
          pit_roundel(g, vadd(vadd(m, vmul(face, 0.18f)), vadd(vmul(up, 7.05f), vmul(rt, half - 0.55f))), face, 0.4f, false, depth);
        }
      }
      g_alpha = 1;
      if(depth < 260) pit_roundel(g, vadd(vadd(p1, vmul(vmul(f, -1), 0.15f)), vmul(up, 2.6f)), vmul(f, -1), 0.45f, false, depth);
    }
    // The limit's end, and the pit exit's light (green) at the wall's end.
    if(r == PR_LIM1 && depth < 260) {
      V3 p1 = pit_at(tr, rf, pl->gar + 0.3f);
      mbox(g, p1, f, n, up, 0.05f, 0.05f, 0, 2.6f, 0xFF8E949C, 0);
      pit_roundel(g, vadd(vadd(p1, vmul(f, -0.08f)), vmul(up, 2.7f)), vmul(f, -1), 0.42f, true, depth);
    }
    if(r == PR_WALL1 - 3 && depth < 300) {  // (on the garages' side, out of the cars' way)
      V3 b = pit_at(tr, rf + 0.5f, pl->gar + 0.25f);
      static FadeMem lightMem[2];
      g_alpha = fade_mem(&lightMem[g_view_k], near_fade(cam, b, 0.4f, 4.0f, false));
      if(g_alpha > 0.02f) {
        mbox(g, b, f, n, up, 0.06f, 0.06f, 0, 3.1f, 0xFF6A7078, 0);
        mbox(g, b, f, n, up, 0.14f, 0.18f, 3.1f, 3.95f, 0xFF16181C, 0.01f);
        V3 face = vmul(f, -1);
        begin_item(g, depth - STAND_BIAS - 0.3f);
        pit_disc(g, vadd(b, vadd(vmul(face, 0.15f), vmul(up, 3.75f))), face, up, 0.11f, 0xFF4A1210, 10);
        pit_disc(g, vadd(b, vadd(vmul(face, 0.15f), vmul(up, 3.35f))), face, up, 0.11f, 0xFF3AF07A, 10);
        if(vdot(face, vsub(cam->eye, b)) > 0) billboard_tint(g, vadd(b, vadd(vmul(face, 0.2f), vmul(up, 3.12f))), 0.55f, 0.45f, TEX_GLOW, 0xA03AF07A);
      }
      g_alpha = 1;
    }
  }
  if(!any) return;
  for(int b = 0; b < 2; b++) pit_crew(g, v, tr, b);
}

// --- Where the pit entry is, for the HUD ------------------------------------------------------

// This picture's pit entry cue for each view: on when the view's car is coming up to the pits on
// their side of the road (or short of fuel); the entry's place on the screen (or off it), how far.
static struct {
  bool on, seen, committed;
  float sx, sy, metres;
  int sgn;
} g_pmark[2];

static void pit_marker(TgView3D* v, int view, const Cam* cam, float carPos, float pside) {
  const TgTrack3D* tr = &v->track;
  const PitLane* pl = pit_layout(tr);
  g_pmark[view].on = false;
  int pk = v->cur.view_car[view];
  if(!pl->on || pk < 0 || pit_car(v, v->cur.cars[pk].id)) return;
  float toSign = (float) -pit_metres(tr, carPos), x = pl->sgn * pside / TG_TRACK3D_SIDE_EDGE * tr->scale.half_width;
  if(toSign < 0 || toSign > 320) return;
  int pl_ = view < 2 ? view : 0;
  bool low = v->hudv[pl_].fuel < 30;
  if(!(low || x > pl->div - 0.8f)) return;  // (heading for the line on the pits' side, or short of fuel)
  V3 at = vadd(pit_at(tr, 0, fmaxf(pl->div + 1.2f, pl->kerb + 1.0f)), v3(0, 3.0f, 0));
  V3 q = to_cam(cam, at);
  g_pmark[view].on = true;
  g_pmark[view].seen = q.z > 2 && fabsf(q.x / q.z * cam->f) < cam->w * 0.47f;
  g_pmark[view].sx = q.z > 0.1f ? cam->cx + q.x / q.z * cam->f : (pl->sgn > 0 ? cam->w : 0);
  g_pmark[view].sy = q.z > 0.1f ? cam->cy - q.y / q.z * cam->f : cam->cy;
  g_pmark[view].metres = toSign;
  g_pmark[view].committed = x > pl->div;
  g_pmark[view].sgn = pl->sgn;
}
