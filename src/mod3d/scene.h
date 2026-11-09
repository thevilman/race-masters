#ifndef TG_SCENE_H
#define TG_SCENE_H

// Everything the 3D renderer draws, read from the game after each race update (3D mod,
// docs/3D-MOD.md). Read-only: building a scene never changes the game.
//
// Every car in the race is here, the players' and the computer's, each with its place on
// the course (track and side position, lap) and in the world (position and orientation on
// the 3D circuit of track3d.h).

#include <stdbool.h>
#include <stdint.h>

#include "track3d.h"

#define TG_SCENE_MAX_CARS 20

typedef struct {
  int slot;          // record in the car list ($0200 + $12 + slot * $12)
  int id;            // the car's id (bits 0-4 of the record), unique: player 2's car,
                     // number 0 like player 1's, is 0x1F (as the game's sprite tags have it)
  int view;          // 1 or 2 if a view follows this car (the top and bottom halves of the
                     // original screen; in 1-player races the bottom one follows a computer
                     // rival), else 0
  float track_pos;   // track units along the lap, 0 at the start line
  float side;        // side-to-side units, positive right, road edges near +-TG_TRACK3D_SIDE_EDGE
  int lap;           // laps completed
  uint16_t speed;    // the game's speed word
  uint16_t kind;     // its record's sprite kind (word 0, bits 9-15: the game's picture set for
                     // it, which carries its palette)
  TgTrackFrame world;  // position (centre of the car on the road) and orientation, metres
} TgSceneCar;

typedef struct {
  bool in_race;
  int views;         // views with a car in them, 1 or 2
  int car_count;
  TgSceneCar cars[TG_SCENE_MAX_CARS];
  int view_car[2];   // index into cars of the car each view follows, or -1
  uint16_t race_time;  // the game's race clock
} TgScene;

// Reads the race from work RAM. track is brought up to date for the course being raced.
// Returns scene->in_race.
bool tg_scene_update(TgScene* scene, TgTrack3D* track, const uint8_t* wram);

#endif
