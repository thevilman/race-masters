// The classic title's logos as vector shapes (docs/3D-MOD.md): a picture of the game's logo,
// read off the screen and sorted into parts (its face, its outline), is traced into clean
// outlines, fitted with straight lines that keep the corners (the pixel stairs gone),
// filled and extruded into a solid with a bevel. Built at run time from the screen only;
// nothing of the game's is kept beyond the title.
#ifndef TG_LOGO_VECTOR_H
#define TG_LOGO_VECTOR_H

#include <stdbool.h>
#include <stdint.h>

// The parts of a pixel (as the title's capture sorts them).
enum { LV_NONE = 0, LV_FACE = 1, LV_RIM = 2, LV_HI = 3 };

// What a polygon is: the front of the face, the front of the outline, the bevel round the
// face, the bevel round the outline, the side, the pale line at the side's back.
enum { LV_M_FACE, LV_M_RIM, LV_M_FBEVEL, LV_M_RBEVEL, LV_M_SIDE, LV_M_LINE };

#define LV_MAXV 8

typedef struct {
  float x, y, z;  // grid pixels: x right, y down, z back (the face is in front: z < 0)
  uint32_t c;     // its colour, unlit
} LvVert;

typedef struct {
  uint8_t mat, n;      // what it is, its corners (a convex polygon)
  float nx, ny, nz;    // its outward normal (the grid's axes)
  LvVert v[LV_MAXV];
} LvPoly;

typedef struct {
  float raise;        // the face in front of the outline (pixels)
  float depth;        // the solid's depth behind the outline
  float face_bevel;   // the face's bevel: how far out it reaches the outline's plane
  float side_bevel;   // the outline's bevel (45 degrees)
  float line_at;      // where the pale line starts (0-1 of the depth)
  int blur_x, blur_y; // the face's colour: averaged over this box round each point
  float cell;         // the fronts are cut into cells this big (lighting per corner)
  float tol;          // the fit's tolerance (pixels)
  bool straighten;    // snap edges near the italic slant, the horizontal, the vertical
  uint32_t rim, side_front, side_back, line;
} LvStyle;

typedef struct {
  LvPoly* p;
  int np, cap;
  uint64_t key;
  bool valid;
  // The last build: its shapes, their corners, the time it took (ms).
  int shapes, corners, builds;
  double ms;
} LvMesh;

// The mesh of a grid of parts (W x H, cls: LV_*) and colours (col, 0xAARRGGBB, for the face).
// Rebuilt only when the grid (or the style) changed; returns whether it was rebuilt.
bool lv_build(LvMesh* m, const uint8_t* cls, const uint32_t* col, int W, int H, const LvStyle* st);
void lv_free(LvMesh* m);

// The publisher's splash: its logo read off the screen (a 256 x 224 picture), its parts
// sorted at full brightness (the colours as they are without the fade), the fade apart.
typedef struct {
  uint8_t cls[224 * 256];
  uint32_t col[224 * 256];
  bool valid;
  float bright;          // the picture's brightness now (0-1)
  float cx, cy;          // the logo's centre (pixels)
  uint32_t rim;          // the outline's colour
  // The word under the logo (PRESENTS): not traced (too small to read as pixels), drawn
  // from a font's outlines, each letter in the box of the original's (pixels).
  float word[8][4];      // its letters' boxes: x0, y0, x1, y1
  int nword;             // 8 when found
  float word_bright;     // its brightness now (it fades in on its own)
} LvSplash;

void lv_splash_capture(LvSplash* s, const uint32_t* raw);

// The word's mesh (logo_glyphs.h's letters fitted to the boxes): a white face shaded to blue
// (top, bottom), a bevel, the sides. Rebuilt only when the boxes change.
bool lv_build_word(LvMesh* m, const LvSplash* s, const LvStyle* st, uint32_t top, uint32_t bottom);

#endif
