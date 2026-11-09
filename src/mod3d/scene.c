// The race as a 3D scene: see scene.h.

#include "scene.h"

#include <string.h>

static uint16_t r16(const uint8_t* m, uint32_t a) { return (uint16_t) (m[a] | m[a + 1] << 8); }

// The car list (src/port/cars.c): records of $12 bytes after a header record at $0200.
#define CARS 0x0200
#define CAR_SIZE 0x12
#define CAR(slot, f) (CARS + CAR_SIZE + (uint32_t) (slot) * CAR_SIZE + (f))

// The cars the two views follow (drive.c): their record, their own copies of position and
// side (a 24-bit track position, fraction byte first, and the side word, updated before
// the car list is), and the view's off flag.
static const struct { uint16_t car, pos, side, view_off; } kViews[2] = {
  {0x0204, 0x1ED6, 0x1E8A, 0x0040},
  {0x0206, 0x1ED9, 0x1E8C, 0x0053},
};

bool tg_scene_update(TgScene* s, TgTrack3D* track, const uint8_t* m) {
  memset(s, 0, sizeof(*s));
  s->view_car[0] = s->view_car[1] = -1;
  if(!tg_track3d_update(track, m)) return false;
  s->in_race = true;
  s->race_time = r16(m, 0x1EAE);
  s->views = (r16(m, kViews[1].view_off) & 0x8000) ? 1 : 2;

  int viewSlot[2] = {-1, -1};
  for(int v = 0; v < s->views; v++) viewSlot[v] = r16(m, kViews[v].car) / CAR_SIZE;

  for(int slot = 0; slot < TG_SCENE_MAX_CARS; slot++) {
    TgSceneCar* c = &s->cars[s->car_count];
    c->slot = slot;
    // Both players' cars are number 0; player 2's is told apart as the game's sprite tags
    // tell it (TG_P2_CAR, visible.c).
    c->id = slot == r16(m, kViews[1].car) / CAR_SIZE ? 0x1F : m[CAR(slot, 0x00)] & 0x1F;
    c->view = slot == viewSlot[0] ? 1 : slot == viewSlot[1] ? 2 : 0;
    if(c->view) {
      uint32_t a = kViews[c->view - 1].pos;
      c->track_pos = (float) r16(m, a + 1) + m[a] / 256.0f;
      c->side = (float) (int16_t) r16(m, kViews[c->view - 1].side);
      s->view_car[c->view - 1] = s->car_count;
    } else {
      c->track_pos = (float) r16(m, CAR(slot, 0x07)) + m[CAR(slot, 0x06)] / 256.0f;
      c->side = (float) (int16_t) r16(m, CAR(slot, 0x02));
    }
    c->lap = m[CAR(slot, 0x09)];
    c->speed = r16(m, CAR(slot, 0x0A));
    c->kind = r16(m, CAR(slot, 0x00)) & 0xFE00;
    c->world = tg_track3d_frame(track, c->track_pos, c->side);
    s->car_count++;
  }
  return true;
}
