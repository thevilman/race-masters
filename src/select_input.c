#include "select_input.h"

#include "game.h"

#define DPAD (TG_BTN_UP | TG_BTN_DOWN | TG_BTN_LEFT | TG_BTN_RIGHT)
#define PRESS_FRAMES 2   // a press long enough for the game's once-a-frame read
#define RELEASE_FRAMES 2 // then nothing, so it sees the release
#define WAIT_FRAMES 24   // a press the game missed (it was redrawing) is made again
#define MAX_TRIES 8

void tg_select_input_reset(TgSelectInput* s) {
  uint16_t prev = s->prev;
  *s = (TgSelectInput) {0};
  s->target = -1;
  s->prev = prev;
}

uint16_t tg_select_input(TgSelectInput* s, uint16_t raw, int cursor, bool active) {
  uint16_t pressed = raw & ~s->prev;
  s->prev = raw;
  cursor &= 3;
  if(!active) {
    tg_select_input_reset(s);
    return raw;
  }
  // A car was taken: the screen fades out and the next menu (which reuses $5A) gets the
  // buttons as they are.
  if(s->chosen) {
    uint16_t out = raw;
    if(s->heldFrames > 0) {
      out |= s->held;
      if(--s->heldFrames == 0) s->held = 0;
    }
    return out;
  }
  if(pressed & (TG_BTN_LEFT | TG_BTN_RIGHT)) {
    // One step along the line from where the cursor is going (or is).
    int from = s->target >= 0 ? s->target : cursor;
    s->target = (from + (pressed & TG_BTN_RIGHT ? 1 : 3)) & 3;
    s->tries = 0;
  }
  uint16_t other = raw & ~DPAD;
  if(s->target >= 0 && s->phase == 0 && s->target == cursor) {
    s->target = -1;
    s->heldFrames = s->held ? PRESS_FRAMES + 1 : 0;
  }
  if(s->target < 0) {
    uint16_t out = other;
    if(s->heldFrames > 0) {
      out |= s->held;
      if(--s->heldFrames == 0) s->held = 0;
    }
    if(out & ~TG_BTN_SELECT) s->chosen = true;  // any button but the D-pad and Select takes the car ($DFF0)
    return out;
  }
  s->held |= pressed & ~DPAD;
  switch(s->phase) {
    case 0: {
      if(++s->tries > MAX_TRIES) {  // the game is not taking presses: give up
        s->target = -1;
        s->held = 0;
        return other;
      }
      // Column first (Left / Right flip it), then the row (Up / Down).
      int diff = cursor ^ s->target;
      s->button = diff & 1 ? ((s->target & 1) ? TG_BTN_RIGHT : TG_BTN_LEFT) : ((s->target & 2) ? TG_BTN_DOWN : TG_BTN_UP);
      s->from = cursor;
      s->phase = 1;
      s->timer = PRESS_FRAMES;
    }
      /* fall through */
    case 1:
      if(s->timer-- > 0) return s->button;
      s->phase = 2;
      s->timer = 0;
      /* fall through */
    default:
      s->timer++;
      if((cursor != s->from && s->timer > RELEASE_FRAMES) || s->timer > WAIT_FRAMES) s->phase = 0;
      return 0;
  }
}
