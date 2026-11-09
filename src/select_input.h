#ifndef TG_SELECT_INPUT_H
#define TG_SELECT_INPUT_H

#include <stdbool.h>
#include <stdint.h>

// The car select in the modern look shows the four cars in one line; the game moves its
// cursor ($5A, 0-3) on a 2 x 2 grid: Left and Right flip the column (car ^ 1), Up and Down
// the row (car ^ 2), wrapping, one move per press (it waits for every button to be
// released between moves). While the modern screen is up, the frontend turns Left / Right
// into the presses the grid needs to reach the next or previous car in the line (wrapping
// from the fourth to the first), over a few frames, checking the cursor after each; Up and
// Down do nothing there. The game itself is unchanged and receives (and a recording keeps)
// ordinary presses. Other buttons pressed during a move are held back and given after it,
// so a quick A does not take a car on the way.

typedef struct {
  int target;          // the car being moved to, -1 when none
  int phase;           // 0: next press due, 1: pressing, 2: released, waiting for the cursor
  int timer;
  int tries;
  int from;            // the cursor when the press started
  uint16_t button;     // the direction being pressed
  uint16_t prev;       // the player's buttons last frame
  uint16_t held;       // other buttons pressed during a move
  int heldFrames;      // frames left to give them after it
  bool chosen;         // a car was taken: buttons pass as they are until the screen goes
} TgSelectInput;

void tg_select_input_reset(TgSelectInput* s);
// raw: what the player holds this frame; cursor: the game's car under the cursor (0-3);
// active: the modern car select is shown. Returns what the game gets this frame.
uint16_t tg_select_input(TgSelectInput* s, uint16_t raw, int cursor, bool active);

#endif
