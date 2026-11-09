#include "country_input.h"

#include <string.h>

#include "game.h"
#include "port/port.h"

#define PRESS_FRAMES 2    // a press long enough for the game's once-a-frame read
#define RELEASE_FRAMES 2  // then nothing, so it sees the release (it waits for one)
#define WAIT_FRAMES 24    // a press the game missed (it was fading or drawing) is made again
#define END_WAIT 150      // after END: the fade out and the check before the country screen
#define MAX_TRIES 12

#define CELL_SPACE 0x1310  // the keyboard's space key; $130C is back, $1314 and on END
#define CELL_END 0x1314

static uint16_t w16(const uint8_t* w, uint16_t a) { return (uint16_t) (w[a] | w[(uint16_t) (a + 1)] << 8); }

bool tg_country_in_menus(const uint8_t* w) { return w[0] == 0x5C && w[1] == 0x00 && w[2] == 0x80 && w[3] == 0x0F; }

bool tg_country_password(int country, int skill, char out[9]) {
  out[0] = 0;
  if(country < 1 || country > 7 || skill < 0 || skill > 2) return false;
  // $0F:93DC: ($5C - 1) * 8 + level * $38 into $0D:C513.
  const uint8_t* p = tg_rom(0x0DC513 + (uint32_t) ((country - 1) * 8 + skill * 0x38));
  for(int k = 0; k < 8; k++) {
    if(!((p[k] >= 'A' && p[k] <= 'Z') || p[k] == ' ')) return false;
    out[k] = (char) p[k];
  }
  out[8] = 0;
  return out[0] != ' ';
}

// The keyboard's cell (row << 8 | column) for a letter, as $0F:943D reads the keys: rows
// $0B-$13 two apart, columns 4-$18 four apart, a row's first letter from $0F:95A2.
static uint16_t cell_of(char c) {
  if(c == ' ') return CELL_SPACE;
  for(int ri = 0; ri < 5; ri++)
    for(int ci = 0; ci < (ri == 4 ? 2 : 6); ci++)
      if((char) (rom16(0x0F95A2 + (uint32_t) (ri * 2)) + ci + 0x41) == c) return (uint16_t) ((0x0B + 2 * ri) << 8 | (4 + 4 * ci));
  return 0;
}

// The direction that brings the cursor at cur nearer target (END: any cell from $1314).
static uint16_t step_to(uint16_t cur, uint16_t target) {
  bool atEnd = cur >= CELL_END, toEnd = target >= CELL_END;
  if(atEnd) return toEnd ? 0 : TG_BTN_UP;  // up from END: back to the letters
  int row = cur >> 8, col = cur & 0xFF, trow = target >> 8, tcol = toEnd ? 0x14 : target & 0xFF;
  // On the last row Left / Right run into END and back key; leave it upwards unless the key
  // is on it.
  if(row == 0x13 && trow != 0x13) return TG_BTN_UP;
  if(col != tcol) return col < tcol ? TG_BTN_RIGHT : TG_BTN_LEFT;
  if(row != trow) return row < trow ? TG_BTN_DOWN : TG_BTN_UP;
  return 0;
}

void tg_country_input_reset(TgCountryInput* s) {
  *s = (TgCountryInput) {0};
  s->confirm = s->country = -1;
}

bool tg_country_input_busy(const TgCountryInput* s) { return s->country >= 0 || s->confirm >= 0; }

static uint32_t snapshot(const uint8_t* w) {
  return (uint32_t) w16(w, 0x48) | (uint32_t) (w[0x5E] & 0x0F) << 16 | (uint32_t) (w[0x46] & 0x0F) << 20 | (uint32_t) (w[0x5A] & 0x0F) << 24;
}

// One press of button, checked: 0 while it runs, 1 when the game took it, -1 when it gave up.
static uint16_t press(TgCountryInput* s, uint16_t button, const uint8_t* w, int wait, int* result) {
  *result = 0;
  switch(s->phase) {
    case 0:
      if(++s->tries > MAX_TRIES) {
        *result = -1;
        return 0;
      }
      s->button = button;
      s->mark = snapshot(w);
      s->phase = 1;
      s->timer = PRESS_FRAMES;
      /* fall through */
    case 1:
      if(s->timer-- > 0) return s->button;
      s->phase = 2;
      s->timer = 0;
      /* fall through */
    default:
      s->timer++;
      if(snapshot(w) != s->mark && s->timer > RELEASE_FRAMES) {
        s->phase = 0;
        s->tries = 0;
        *result = 1;
      } else if(s->timer > wait) {
        s->phase = 0;  // missed: again
      }
      return 0;
  }
}

uint16_t tg_country_input(TgCountryInput* s, uint16_t raw, bool modern, const uint8_t* w, bool free) {
  int screen = w[0x46];
  uint16_t pressed = raw & ~s->prev;
  s->prev = raw;
  if(!tg_country_in_menus(w) || (screen != 7 && screen != 8) || (!free && !modern)) {
    uint16_t prev = s->prev;
    tg_country_input_reset(s);
    s->prev = prev;
    return raw;
  }
  int r;
  if(screen == 8 && free) {
    // The password screen, for the country $5C.
    if(s->gaveUp) return raw;
    int country = w[0x5C], skill = w[0x1F10];
    char pw[9];
    if(!tg_country_password(country, skill, pw)) {
      s->gaveUp = true;
      return raw;
    }
    if(s->country != country) s->country = country, s->phase = 0, s->tries = 0, s->typed = 0, s->confirm = -1;
    int typed = w[0x5E] & 0x0F, len = 8;
    while(len > 0 && pw[len - 1] == ' ') len--;
    // What is typed must be the password so far (a letter the player typed: the player types).
    bool ok = typed <= 8;
    for(int k = 0; k < typed && ok; k++) ok = w[0x68 + k] == (uint8_t) pw[k];
    if(!ok) {
      s->gaveUp = true, s->country = -1;
      return raw;
    }
    s->typed = typed;
    uint16_t target = typed < len ? cell_of(pw[typed]) : CELL_END, cur = w16(w, 0x48);
    if(target == 0) {
      s->gaveUp = true, s->country = -1;
      return raw;
    }
    uint16_t b = s->phase ? s->button : step_to(cur, target);
    bool end = target >= CELL_END && cur >= CELL_END;
    uint16_t out = press(s, b ? b : TG_BTN_A, w, end ? END_WAIT : WAIT_FRAMES, &r);
    if(r < 0) s->gaveUp = true, s->country = -1;
    if(end && s->phase == 1) s->confirm = country;  // A on END: then take the country
    return out;
  }
  if(screen == 8) return raw;
  // The country screen. In the modern look's two columns Left / Right go across: four
  // steps on (the game's Left / Right would take the country).
  if(s->moves > 0) {
    uint16_t out = press(s, TG_BTN_DOWN, w, WAIT_FRAMES, &r);
    if(r != 0 && --s->moves > 0) s->phase = 0;
    if(r < 0) s->moves = 0;
    return out;
  }
  if(modern && s->confirm < 0 && (pressed & (TG_BTN_LEFT | TG_BTN_RIGHT))) {
    s->moves = 4, s->phase = 0, s->tries = 0;
    return press(s, TG_BTN_DOWN, w, WAIT_FRAMES, &r);
  }
  if(modern && s->confirm < 0) return raw & ~(TG_BTN_LEFT | TG_BTN_RIGHT);
  // After the password, the country is open; take it.
  if(s->confirm < 0) return raw;
  if(s->country >= 0) s->country = -1, s->phase = 0, s->tries = 0;  // back from the password
  if((w16(w, 0x1F06) >> 2 & 7) == s->confirm || (w[0x5A] != s->confirm && s->phase == 0)) {
    // Taken (the course is its first), or the password was not taken: the player goes on.
    s->confirm = -1;
    s->phase = 0;
    return raw;
  }
  uint16_t out = press(s, TG_BTN_A, w, WAIT_FRAMES, &r);
  if(r != 0) s->confirm = -1;
  return out;
}
