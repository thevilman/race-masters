#ifndef TG_TRANSITION_H
#define TG_TRANSITION_H

// F7's change between the original picture (2D) and the 3D view, as time passing: a clock's
// hand of light sweeps once round the screen from 12 o'clock. Going to 3D it turns clockwise
// (forward in time) and the 3D view is behind it, the original still ahead; going back it
// turns anticlockwise (rewinding) and the original returns behind it. A clock face at the
// centre shows the year rolling 1992 -> 2026 (or back), its rim lit in the two eras' colours
// (amber: the original's share of the screen, cyan: the 3D view's), fast-forward or rewind
// marks under the year, and the hand leaves a short afterglow, as a radar sweep's phosphor.
//
// Presentation only: the game runs on underneath; the frontend draws both pictures and puts
// the 3D one inside tg_transition_region. A new press during a transition turns the hand back
// from where it is (the picture at hand angle a is the same whichever way it turns).
// 0.40 s for the turn, 0.10 s for the face to fade: 0.5 s from a press to the plain picture.

#include <stdbool.h>

#include "view3d.h"

#define TG_TRANSITION_SWEEP 0.40f     // seconds for the hand's full turn
#define TG_TRANSITION_FADE_IN 0.05f   // the face and hand appearing
#define TG_TRANSITION_FADE_OUT 0.10f  // and going, once the hand is home
#define TG_TRANSITION_YEAR_FROM 1992
#define TG_TRANSITION_YEAR_TO 2026

typedef struct {
  bool active;
  bool to3d;     // where the hand is going
  float x;       // the hand: 0 at 12 o'clock with all the original, 1 a full turn on with all 3D
  float alpha;   // the face's and the hand's opacity (0-1)
  float trail;   // the afterglow, radians: > 0 behind a clockwise hand (on the 3D side)
} TgTransition;

// A press. Not active: starts from the picture that is up (from3d: the 3D view, else the
// original). Active: turns round towards to3d from where the hand is.
void tg_transition_start(TgTransition* t, bool from3d, bool to3d);
// dt seconds later; active turns false when the hand is home and the face has faded.
void tg_transition_step(TgTransition* t, float dt);
// The hand's angle, radians clockwise from 12 o'clock (0 - 2 pi): the 3D view is in [0, angle).
float tg_transition_angle(const TgTransition* t);
// The year on the face, and how far it is from the first to the last (0 - 1).
int tg_transition_year(const TgTransition* t, float* along);

// The picture's share of a w x h screen as a fan of triangles from its centre (screen pixels,
// TgV3Vertex.w -1: over everything; u, v the position in the screen 0 - 1, to draw a picture of
// the whole screen through it; opaque white): threeD the 3D view's [0, angle), else the
// original's [angle, 2 pi). Appended to out (pic -1); nothing when the share is empty.
void tg_transition_region(const TgTransition* t, int w, int h, bool threeD, TgV3List* out);
// The hand, its afterglow and the clock face with the year, for w x h (pic -1, w -1, with
// alpha), appended to out.
void tg_transition_overlay(const TgTransition* t, int w, int h, TgV3List* out);

#endif
