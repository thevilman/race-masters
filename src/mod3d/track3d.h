#ifndef TG_TRACK3D_H
#define TG_TRACK3D_H

// The course as a real 3D circuit (3D mod, docs/3D-MOD.md).
//
// The original only knows a course as per-segment tables in work RAM, one word per 32
// track units: how fast the road turns ($4C00), its gradient ($4000), its stripe and
// colour phase ($5800) and its roadside object ($6400). Its renderer never places the
// road in the world; it bends and lifts it on screen relative to the camera. Here the
// tables are integrated once into a closed loop with a position for every point:
//
// - Plan: the curve values are turn rates. One lap is scaled to turn exactly one full
//   circle, and the small gap left where the loop meets its start is spread over the lap.
// - Height: the gradient is integrated. Extreme gradients are capped (the original uses
//   them for drops the camera loses over a crest, never climbed back), and the climbs or
//   the descents are scaled so the lap ends where it starts: flat road stays flat.
//
// World axes: x east, y up, z south (right-handed, as OpenGL and SDL GPU expect), in
// metres. A track position is in the game's track units (32 per segment); a side position
// is the game's side-to-side units, positive to the right, the road's edges at about
// +-TG_TRACK3D_SIDE_EDGE.

#include <stdbool.h>
#include <stdint.h>

#define TG_TRACK3D_MAX_SEGMENTS 2048  // the longest courses have about 1250
#define TG_TRACK3D_SEG_UNITS 32
#define TG_TRACK3D_SIDE_EDGE 0x27F5  // the usual road edge, from the limits at $017B/$017D

typedef struct { float x, y, z; } TgVec3;

// What the course is made of: the per-segment tables, copied out of work RAM.
typedef struct {
  int segments;
  int16_t curve[TG_TRACK3D_MAX_SEGMENTS];
  int16_t slope[TG_TRACK3D_MAX_SEGMENTS];
  uint16_t stripe[TG_TRACK3D_MAX_SEGMENTS];
  uint16_t object[TG_TRACK3D_MAX_SEGMENTS];
} TgTrackTables;

// How the tables become metres. tg_track3d_default_scale gives the values in use.
typedef struct {
  float seg_len;      // metres per segment
  float half_width;   // metres from the centre line to the road edge
  float rise;         // metres of height per segment per unit of gradient
  int16_t max_slope;  // gradients beyond this (either way) are capped
} TgTrackScale;

typedef struct {
  TgTrackTables tables;
  TgTrackScale scale;
  uint32_t key;       // identifies the course the tables came from
  // Per segment start, closed: index segments is the same point as index 0.
  TgVec3 centre[TG_TRACK3D_MAX_SEGMENTS + 1];
  float heading[TG_TRACK3D_MAX_SEGMENTS + 1];  // radians, counter-clockwise from east
  float grade[TG_TRACK3D_MAX_SEGMENTS + 1];    // rise over run along the road
  TgVec3 min, max;    // bounds of the centre line
} TgTrack3D;

// A point's frame on the road: where it is, and which way is ahead, right and up.
typedef struct {
  TgVec3 pos, forward, right, up;
} TgTrackFrame;

TgTrackScale tg_track3d_default_scale(void);

// Copies the tables of the course in a race; false outside a race.
bool tg_track3d_read_tables(const uint8_t* wram, TgTrackTables* out);

// Lays the course out. Returns false if the tables are empty.
bool tg_track3d_build(TgTrack3D* t, const TgTrackTables* tables, TgTrackScale scale);

// Builds from work RAM unless t already holds this course. Returns whether t is valid.
bool tg_track3d_update(TgTrack3D* t, const uint8_t* wram);

// The frame at track position pos (track units, fractions allowed, wrapping) and side
// position side (the game's units, positive right).
TgTrackFrame tg_track3d_frame(const TgTrack3D* t, float pos, float side);

// Lap length in metres.
float tg_track3d_length(const TgTrack3D* t);

// Writes the road, its verges and the start line as a Wavefront OBJ (with an .mtl next
// to it) to look at in any 3D viewer. Returns false if the file can't be written.
bool tg_track3d_write_obj(const TgTrack3D* t, const char* path);

#endif
