#ifndef TG_USERDIR_H
#define TG_USERDIR_H

// The per-user data folder the launcher, the game and tools/music share for what is too big
// for the settings folder: the music tools (music-tools/) and, when the game's own folder
// can't be written to, the modern music (audio-modern/). Windows: %LOCALAPPDATA%\race-masters\,
// elsewhere $XDG_DATA_HOME/race-masters/ (~/.local/share/race-masters/); with the separator at
// the end. tools/music/generate.py and setup_tools.py (data_root) find the same folder.

#include <stdbool.h>
#include <stdio.h>

#include <SDL3/SDL.h>

#include "brand.h"

static inline bool tg_user_data_dir(char* out, size_t n) {
#ifdef _WIN32
  const char* base = SDL_getenv("LOCALAPPDATA");
  if(base && base[0]) {
    snprintf(out, n, "%s\\%s\\", base, TG_PREF_ORG);
    return true;
  }
#else
  const char* xdg = SDL_getenv("XDG_DATA_HOME");
  if(xdg && xdg[0] == '/') {
    snprintf(out, n, "%s/%s/", xdg, TG_PREF_ORG);
    return true;
  }
  const char* home = SDL_getenv("HOME");
  if(home && home[0]) {
    snprintf(out, n, "%s/.local/share/%s/", home, TG_PREF_ORG);
    return true;
  }
#endif
  if(n) out[0] = 0;
  return false;
}

#endif
