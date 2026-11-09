#ifndef TG_NOW_MS_H
#define TG_NOW_MS_H

// Milliseconds on a steady clock (from an arbitrary start), for timing logs only: the game's
// own timing never reads it. Works on every compiler the project builds with (MSVC, MinGW
// with either C runtime, GCC, Clang), unlike C11's timespec_get.
double tg_now_ms(void);

#endif
