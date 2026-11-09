// The 3D races drawn with OpenGL and a depth buffer (3D mod): surfaces hide what is behind
// them however the scene is sorted, pictures are drawn in perspective, edges are smoothed.
// Its own GL context in the game's window, so the game's 2D renderer keeps its state.
#ifndef TG_GL3D_H
#define TG_GL3D_H

#include <SDL3/SDL.h>
#include <stdbool.h>

#include "mod3d/view3d.h"

// Before the window is made: a depth buffer and multisampling for it.
void tg_gl3d_prepare(void);
// After the renderer is made (it must be SDL's "opengl" one): false if this can't be used.
bool tg_gl3d_init(SDL_Window* window, SDL_Renderer* renderer);
// One view's list into the rectangle (x, y, w, h) of the window (pixels from the top left).
void tg_gl3d_draw(const TgView3D* v, const TgV3List* l, int x, int y, int w, int h, int windowH);
// A picture of several views (the race: one per player): tg_gl3d_begin, tg_gl3d_draw for each
// view, tg_gl3d_end. In between this file's context stays current and the whole window's
// depth is cleared once, so a 2-player picture reaches the driver as a 1-player one does:
// the renderer's clear, this context's drawing, the renderer's present, nothing in between.
void tg_gl3d_begin(int windowW, int windowH);
void tg_gl3d_end(void);
// In a picture, F7's transition (transition.h) with the views drawn in one go as ever: the
// renderer's drawing under it (the original picture) stays inside keep's triangles (as depth 0,
// nearer than all the views draw), the rest of the window is black for the views; the views
// drawn until tg_gl3d_unmask show only there. What is drawn after it (the clock) goes anywhere.
void tg_gl3d_mask(const TgV3List* keep, int windowW, int windowH);
void tg_gl3d_unmask(void);
void tg_gl3d_shutdown(void);
// What drew the 3D views ("Intel(R) Iris(R) Xe Graphics, 4.6.0 - Build ..., 4x MSAA"), for the
// session's recording; empty when this isn't used.
const char* tg_gl3d_info(void);
// The first OpenGL error the 3D views raised since the last call (0: none).
unsigned tg_gl3d_take_error(void);

// For the frame-time log (TG_PERF_LOG): what was sent and drawn since the last take.
typedef struct {
  size_t upload_bytes;  // texture pixels sent
  int uploads;          // pictures sent
  size_t vertex_bytes;  // vertices sent
  int triangles, draws;
  double gpu_ms;        // GPU time of the views whose timer results came back since (lags a frame or two)
  int gpu_views;
} TgGl3dStats;
void tg_gl3d_set_perf(bool on);  // GPU timer queries on (only while logging)
void tg_gl3d_take_stats(TgGl3dStats* out);
// Sends the pictures a race draws ahead of it (the loading card): at most budget bytes a
// call; true when all are sent.
bool tg_gl3d_warm(const TgView3D* v, size_t budget);

#endif
