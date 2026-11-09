#include "now_ms.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

double tg_now_ms(void) {
  static LARGE_INTEGER freq;
  LARGE_INTEGER c;
  if(!freq.QuadPart) QueryPerformanceFrequency(&freq);
  QueryPerformanceCounter(&c);
  return (double) c.QuadPart * 1e3 / (double) freq.QuadPart;
}
#else
#include <time.h>

double tg_now_ms(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}
#endif
