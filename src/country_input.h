#ifndef TG_COUNTRY_INPUT_H
#define TG_COUNTRY_INPUT_H

#include <stdbool.h>
#include <stdint.h>

// Country select without typing the password (the "Country select: FREE" option).
//
// The game opens a country beyond the ones reached only through its password screen
// ($0F:929E MenuCountrySelect -> screen 8 -> $0F:93DC MenuCheckPasswords, which compares
// the 8 letters typed at $68 with the table at $0D:C513: $38 bytes per skill level, 8 per
// country 1-7). With the option on, when the player picks a locked country the frontend
// types that country's password for the current skill level ($1F10) into the game's own
// keyboard, as presses: it moves the keyboard's cursor ($48: row, column) to each letter,
// presses A, checks the letter landed ($5E, $68), ends on END, and when the game is back on
// the country screen with the country now open it presses A on it once more. The password
// is read from the ROM at run time. The game is unchanged: it receives ordinary presses (and
// a recording keeps them, so a replay needs no frontend help); the player's own buttons are
// held back while it types. If anything is not as expected it stops and the player has the
// keyboard.
//
// Separately, on the modern country screen (two columns of four) Left and Right move across,
// as the four Down presses that bring the game's cursor ($5A, wrapping 0-7) there; in the
// original Left and Right take the country, which the modern screen would not suggest.

typedef struct {
  int phase;         // 0: next press due, 1: pressing, 2: released, waiting for the change
  int timer;
  int tries;
  uint16_t button;   // what is being pressed
  uint32_t mark;     // the game's state when the press began ($48, $5E, $46)
  int confirm;       // the country to take on the country screen once it is open, -1 none
  int country;       // the country whose password is being typed, -1 none
  bool gaveUp;       // stopped: the player types
  int typed;         // letters typed by the frontend (for the screen)
  int moves;         // Down presses still to make (Left / Right on the modern screen)
  uint16_t prev;     // the player's buttons last frame
} TgCountryInput;

void tg_country_input_reset(TgCountryInput* s);
// raw: what player 1 holds; modern: the modern country screen is shown (two columns: Left
// and Right go across, as four Down presses); wram: the game's memory; free: the option is
// on. Returns what the game gets this frame.
uint16_t tg_country_input(TgCountryInput* s, uint16_t raw, bool modern, const uint8_t* wram, bool free);
// The frontend is typing a password or taking the country (the player's buttons wait).
bool tg_country_input_busy(const TgCountryInput* s);
// Country c's (1-7) password at a skill level (0-2), from the ROM: 8 characters and a 0.
// False when it is not letters and spaces.
bool tg_country_password(int country, int skill, char out[9]);
// The game is in its menus (the menus' NMI trampoline, JML $0F:8000, at $0000).
bool tg_country_in_menus(const uint8_t* wram);

#endif
