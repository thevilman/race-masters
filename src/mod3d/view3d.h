#ifndef TG_VIEW3D_H
#define TG_VIEW3D_H

// The race in real 3D (3D mod, docs/3D-MOD.md).
//
// Everything comes from the game itself: the course and the cars from work RAM (scene.h,
// track3d.h), the camera from the game's own camera (the track position RaceRenderViews
// draws from, $46, and the player's side position), and the picture of every car and
// roadside object from the sprites the game chose this frame (steering frames, brake
// lights, size steps), found by the object tags its sprite builder leaves on OAM entries.
// The game is never changed by this: it only reads.
//
// Two looks: the game's own pictures standing in the 3D world (billboards), or simple
// models made from them. Drawn as a list of 2D triangles, far to near, that any renderer
// can draw (SDL_RenderGeometry in the frontend, tg_v3_raster in tests).

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "game.h"
#include "scene.h"

// The game's own pictures standing in 3D; models built from them; or the modern look: new
// models and materials made here (none of the game's pictures but its HUD and skylines).
typedef enum { TG_V3_LOOK_SPRITES, TG_V3_LOOK_MODELS, TG_V3_LOOK_MODERN, TG_V3_LOOK_COUNT } TgV3Look;
typedef enum { TG_V3_CAM_GAME, TG_V3_CAM_HIGH, TG_V3_CAM_BUMPER, TG_V3_CAM_COUNT } TgV3Camera;

// A picture: w x h, ARGB8888 (alpha 0 or 255). version changes when the pixels do.
typedef struct {
  int w, h;
  uint32_t* px;
  uint32_t version;
  bool replaced;  // a picture from the art folder: kept, whatever the game shows
  bool smooth;    // drawn filtered (the made textures), not as sharp pixels
  // The last change: from version dirty_from to version only rows dirty_y0 to dirty_y1 - 1
  // changed (a renderer holding dirty_from sends only those).
  uint32_t dirty_from;
  int dirty_y0, dirty_y1;
  // A car model's texture (its atlas, tgcar version 3): mips smaller levels follow the picture
  // in px, each half the last (to 1 x 1), drawn trilinear; and mask: its alpha is not coverage
  // but a tint mask - the surface's colour (albedo) is rgb x mix(1, the vertex colour, alpha),
  // so the paint (and the lamps' light) comes from the vertex: a rival's paint is the game's.
  int mips;
  bool mask;
} TgV3Pic;

typedef struct {
  float x, y;     // screen pixels
  float u, v;     // 0-1 in the picture
  uint32_t argb;  // multiplied with the picture (or the colour without one); alpha blends
                  // (a mask picture: see TgV3Pic.mask)
  float w;        // > 0: in the world, its distance ahead of the camera (metres), for a depth
                  // buffer and perspective-correct pictures; 0: background (behind all the
                  // world); < 0: overlay (the HUD, over everything)
  // A shaded material (the car models; 0 for everything else, drawn in argb as it is). Then
  // argb is the surface's own colour (albedo) and the renderer lights it per pixel
  // (shade_mat in view3d.c; the same lines in gl3d.c's shader, tg_v3_raster per pixel):
  uint32_t nrm;   // bytes x, y, z: the world normal (snorm8: (v * 127 + 128)); byte 3: the
                  // material (bits 0-2, TG_V3_MAT_*) and its ambient occlusion (bits 3-7, 0-31)
  uint32_t eye;   // bytes x, y, z: the world direction to the eye (snorm8); byte 3: bits 0-6
                  // the distance haze towards TgV3List.haze (0-127), bit 7 night
} TgV3Vertex;

enum { TG_V3_MAT_NONE, TG_V3_MAT_PAINT, TG_V3_MAT_GLASS, TG_V3_MAT_METAL, TG_V3_MAT_RUBBER, TG_V3_MAT_LAMP,
       TG_V3_MAT_SATIN };

typedef struct {
  int pic;  // index for tg_view3d_pic, or -1 for plain colour
  int first, count;  // vertices, three per triangle
  // Screen-door transparency (a rival between the camera and the player's car, seen through):
  // of every 4 x 4 pixels, this many are left out by an ordered dither, the same cells in every
  // renderer (TG_V3_BAYER4: a pixel is left out where its cell's value is below door, columns
  // from the left, rows from the bottom as GL counts them); 0: drawn whole. What is drawn is
  // solid (it writes depth), so a closed model shows only its own near side, in any order.
  int door;
} TgV3Batch;

// The 4 x 4 ordered-dither (Bayer) matrix of TgV3Batch.door, row by row (gl3d.c's shader
// has the same numbers).
#define TG_V3_BAYER4 {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5}

typedef struct {
  TgV3Vertex* v;
  int nv, capv;
  TgV3Batch* b;
  int nb, capb;
  bool overlay;  // screen-space vertices pushed now are the HUD (else the background)
  uint32_t haze; // the colour shaded materials fade to with distance (TgV3Vertex.eye)
} TgV3List;

// A vertex's colour as drawn: argb, or for a shaded material its light worked out at the
// vertex (for renderers that only take vertex colours: the frontend's SDL fallback).
uint32_t tg_v3_vertex_colour(const TgV3Vertex* v, uint32_t haze);

typedef struct TgView3D TgView3D;

TgView3D* tg_view3d_create(void);
// A folder (ending in a path separator) to keep what the 3D view learns between sessions:
// the skylines and roadside pictures of each course, the cars' pictures. They come from
// the player's ROM, so the folder should be theirs (the settings folder).
void tg_view3d_set_cache_dir(TgView3D* v, const char* dir);

// A folder (ending in a path separator) of replacement pictures, BMP (32-bit with alpha, or
// 24-bit with magenta (255, 0, 255) see-through), any size: each keeps the original's place
// and size in the world, only its detail changes.
//   art-<course>-<n>.bmp  roadside object n of a course     sky-<course>.bmp  its skyline
//   car-<id>.bmp          car id seen from behind (the cars of the models look)
// tg_view3d_export writes the pictures the 3D view has under these names (the files to
// draw over or upscale); returns how many.
void tg_view3d_set_art_dir(TgView3D* v, const char* dir);
int tg_view3d_export(TgView3D* v, const char* dir);
void tg_view3d_destroy(TgView3D* v);

// After each frame. Needs the 1-player view over the whole screen (tg_game_set_full_view),
// so the sprites at the end of the frame are the player's view, and high frame rate on
// (tg_game_set_interp), so they carry their object tags. pixels: the game's picture,
// XRGB8888, TG_FB_WIDTH x TG_FB_HEIGHT (for the sky, the ground colours and the skyline).
// Returns whether a race is on (and so whether there is anything to draw).
bool tg_view3d_capture(TgView3D* v, const uint8_t* wram, const TgSpriteState* sprites, const uint32_t* pixels);

// The picture at w x h. Cars and camera move smoothly between the game's race updates
// (every third frame), a race update behind.
void tg_view3d_draw(TgView3D* v, int w, int h, TgV3Look look, TgV3Camera cam, TgV3List* out);
// 2-player races have two views (player 1's, then player 2's), each drawn on its own.
int tg_view3d_views(const TgView3D* v);
void tg_view3d_draw_view(TgView3D* v, int view, int w, int h, TgV3Look look, TgV3Camera cam, TgV3List* out);

int tg_view3d_pic_count(const TgView3D* v);
const TgV3Pic* tg_view3d_pic(const TgView3D* v, int i);
// Whether picture i is one a race draws whatever happens (its materials, font, skyline,
// roadside pictures): a renderer may send it ahead (gl3d's warm-up on the loading card).
bool tg_view3d_pic_warm(const TgView3D* v, int i);
// The loading card before a race is up (the race's course is about to be drawn).
bool tg_view3d_loading_up(const TgView3D* v);

void tg_v3_list_clear(TgV3List* l);
void tg_v3_list_free(TgV3List* l);
// For frame-time logs: the bytes a renderer keeping one texture per picture uploads to draw
// l (the pictures whose pixels it hasn't seen yet). seen: tg_view3d_pic_count entries,
// zeroed at first, kept between calls.
size_t tg_v3_list_upload_bytes(const TgView3D* v, const TgV3List* l, uint32_t* seen);

// Draws a list into an XRGB8888 buffer (tests and tg_headless).
void tg_v3_raster(const TgView3D* v, const TgV3List* l, uint32_t* out, int w, int h);

// What the HUD shows, read from the game.
typedef struct {
  int speed;  // as the game shows it
  bool kph;
  int position, laps, lap;
  int minutes, seconds, hundredths;
  int gear;
  int nitros;
  int fuel;  // percent
} TgV3Hud;
void tg_view3d_hud(const uint8_t* wram, TgV3Hud* hud);
// How far (0-1) the display is between this frame and the next, when it shows more pictures
// than the game's 60 a second: the cars and the camera move on smoothly.
void tg_view3d_set_subframe(TgView3D* v, float t);
// Whether the game's own HUD is drawn with the picture (else draw tg_view3d_hud's values).
// One car on its own (type 0-3, the select screen's; 4 the rivals' amber), seen from yaw
// degrees round it (0: from behind) and pitch degrees above, for looking at the model.
void tg_view3d_showcase(TgView3D* v, int type, float yaw, float pitch, int w, int h, TgV3List* out);
// The car select screen redrawn in the modern look: whether it is up now, and its picture
// (seconds: a clock for the turning car).
bool tg_view3d_select_up(const TgView3D* v);
// The "Country select" option (src/country_input.h): free, the frontend types a locked
// country's password; busy, it is doing so now. Shown on the country and password screens.
void tg_view3d_set_country_select(TgView3D* v, bool free, bool busy);
void tg_view3d_draw_select(TgView3D* v, int w, int h, float seconds, TgV3List* out);
// The title screen redrawn in the modern look (its 3D logo, a car, PRESS START).
bool tg_view3d_title_up(const TgView3D* v);
void tg_view3d_draw_title(TgView3D* v, int w, int h, float seconds, TgV3List* out);
// Which title: the modern one (chrome logo, a car), or the classic one (the original's own
// logo, read off the game's picture, rebuilt as a solid 3D object over its sky).
enum { TG_V3_TITLE_MODERN, TG_V3_TITLE_CLASSIC };
void tg_view3d_set_title_style(TgView3D* v, int style);
// Any other menu redrawn in the modern look (its text read off the game's screen).
bool tg_view3d_menu_up(const TgView3D* v);
void tg_view3d_menu_dump(const TgView3D* v);
void tg_view3d_draw_menu(TgView3D* v, int w, int h, float seconds, TgV3List* out);
bool tg_view3d_has_hud(const TgView3D* v);

#endif
