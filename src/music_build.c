// The launcher's "Build music": progress lines, the build's state, the launcher's own
// settings and consent.txt. See music_build.h.

#include "music_build.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char* const kTgMbStyleKeys[TG_MB_STYLE_COUNT] = {"faithful", "synthwave", "orchestral", "A", "B", "C", "D"};
const char* const kTgMbStyleNames[TG_MB_STYLE_COUNT] = {"Faithful", "Synthwave", "Orchestral", "Postcard",
                                                        "Local band", "City band", "Race"};

int tg_mb_style_tracks(unsigned mask) {
  int n = 0;
  for(int i = 0; i < TG_MB_STYLE_COUNT; i++)
    if(mask & (1u << i)) n += i < 3 ? 7 : 32;
  if(!(mask & 1u)) n += 1;  // the faithful title, which the game needs
  return n;
}

void tg_mb_styles_arg(unsigned mask, char* out, size_t n) {
  if(n == 0) return;
  out[0] = 0;
  if((mask & TG_MB_STYLES_ALL) == TG_MB_STYLES_ALL || (mask & TG_MB_STYLES_ALL) == 0) {
    snprintf(out, n, "all");
    return;
  }
  size_t used = 0;
  for(int i = 0; i < TG_MB_STYLE_COUNT; i++) {
    if(!(mask & (1u << i))) continue;
    int w = snprintf(out + used, n - used, "%s%s", used ? "," : "", kTgMbStyleKeys[i]);
    if(w < 0 || (size_t) w >= n - used) break;
    used += (size_t) w;
  }
}

// --- One progress line ----------------------------------------------------------------------

static const char* skipSpace(const char* p) {
  while(*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
  return p;
}

static int hexval(char c) {
  if(c >= '0' && c <= '9') return c - '0';
  if(c >= 'a' && c <= 'f') return c - 'a' + 10;
  if(c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// A JSON string at p (after its opening quote) into out; returns the character after its
// closing quote, or NULL.
static const char* parseString(const char* p, char* out, size_t n) {
  size_t k = 0;
  while(*p && *p != '"') {
    char c = *p++;
    if(c == '\\') {
      char e = *p++;
      switch(e) {
        case '"': c = '"'; break;
        case '\\': c = '\\'; break;
        case '/': c = '/'; break;
        case 'b': c = '\b'; break;
        case 'f': c = '\f'; break;
        case 'n': c = '\n'; break;
        case 'r': c = '\r'; break;
        case 't': c = '\t'; break;
        case 'u': {
          int v = 0;
          for(int i = 0; i < 4; i++) {
            int h = hexval(p[i]);
            if(h < 0) return NULL;
            v = v * 16 + h;
          }
          p += 4;
          c = v < 128 ? (char) v : '?';
          break;
        }
        default: return NULL;
      }
    } else if((unsigned char) c >= 128) {
      c = '?';
      while(((unsigned char) *p & 0xC0) == 0x80) p++;  // the rest of a UTF-8 character
    }
    if(k + 1 < n) out[k++] = c;
  }
  if(n) out[k < n ? k : n - 1] = 0;
  return *p == '"' ? p + 1 : NULL;
}

bool tg_mb_parse_line(const char* line, TgMbLine* out) {
  memset(out, 0, sizeof(*out));
  const char* p = skipSpace(line);
  if(*p++ != '{') return false;
  p = skipSpace(p);
  if(*p == '}') return true;
  for(;;) {
    if(*p++ != '"') return false;
    TgMbField tmp, *f = out->n < (int) (sizeof(out->f) / sizeof(out->f[0])) ? &out->f[out->n] : &tmp;
    memset(f, 0, sizeof(*f));
    p = parseString(p, f->key, sizeof(f->key));
    if(p == NULL) return false;
    p = skipSpace(p);
    if(*p++ != ':') return false;
    p = skipSpace(p);
    if(*p == '"') {
      p = parseString(p + 1, f->str, sizeof(f->str));
      if(p == NULL) return false;
    } else if(*p == '-' || (*p >= '0' && *p <= '9')) {
      char* end;
      f->num = strtod(p, &end);
      if(end == p) return false;
      snprintf(f->str, sizeof(f->str), "%.*s", (int) (end - p), p);
      f->isNum = true;
      p = end;
    } else if(strncmp(p, "true", 4) == 0) {
      snprintf(f->str, sizeof(f->str), "true");
      f->num = 1, f->isNum = true, p += 4;
    } else if(strncmp(p, "false", 5) == 0) {
      snprintf(f->str, sizeof(f->str), "false");
      f->isNum = true, p += 5;
    } else if(strncmp(p, "null", 4) == 0) {
      p += 4;
      if(f != &tmp) f->key[0] = 0;  // as if absent
    } else {
      return false;  // nested values aren't part of the protocol
    }
    if(f != &tmp && f->key[0]) out->n++;
    p = skipSpace(p);
    if(*p == ',') {
      p = skipSpace(p + 1);
      continue;
    }
    return *p == '}';
  }
}

const char* tg_mb_str(const TgMbLine* l, const char* key) {
  for(int i = 0; i < l->n; i++) if(strcmp(l->f[i].key, key) == 0) return l->f[i].str;
  return NULL;
}

double tg_mb_num(const TgMbLine* l, const char* key, double def) {
  for(int i = 0; i < l->n; i++) if(strcmp(l->f[i].key, key) == 0) return l->f[i].isNum ? l->f[i].num : def;
  return def;
}

void tg_mb_lines_push(TgMbLines* s, const char* data, size_t n, void (*fn)(const char* line, void* user), void* user) {
  for(size_t i = 0; i < n; i++) {
    char c = data[i];
    if(c == '\n') {
      if(!s->overlong) {
        while(s->len && s->buf[s->len - 1] == '\r') s->len--;
        s->buf[s->len] = 0;
        fn(s->buf, user);
      }
      s->len = 0;
      s->overlong = false;
    } else if(!s->overlong) {
      if(s->len + 1 < sizeof(s->buf)) s->buf[s->len++] = c;
      else s->overlong = true, s->len = 0;
    }
  }
}

// --- A build --------------------------------------------------------------------------------

void tg_mb_reset(TgMbState* s) {
  memset(s, 0, sizeof(*s));
  s->fraction = -1;
  s->etaS = -1;
  s->downloadMb = s->downloadMaxMb = -1;
}

void tg_mb_begin(TgMbState* s, double etaS) {
  tg_mb_reset(s);
  s->phase = TG_MB_RUNNING;
  s->step = TG_MB_STEP_TOOLS;
  s->etaS = etaS;
  snprintf(s->stepText, sizeof(s->stepText), "Setting up the music tools");
}

static void copy(char* dst, size_t n, const char* src) { snprintf(dst, n, "%s", src ? src : ""); }

static TgMbStep stepNamed(const char* name) {
  if(name == NULL) return TG_MB_STEP_NONE;
  if(strcmp(name, "tools") == 0) return TG_MB_STEP_TOOLS;
  if(strcmp(name, "notes") == 0) return TG_MB_STEP_NOTES;
  if(strcmp(name, "plan") == 0) return TG_MB_STEP_PLAN;
  if(strcmp(name, "libraries") == 0) return TG_MB_STEP_LIBRARIES;
  if(strcmp(name, "render") == 0) return TG_MB_STEP_RENDER;
  return TG_MB_STEP_NONE;
}

void tg_mb_feed(TgMbState* s, const char* line) {
  TgMbLine l;
  if(!tg_mb_parse_line(line, &l) || tg_mb_str(&l, "ev") == NULL) {
    const char* p = skipSpace(line);
    if(*p) copy(s->lastPlain, sizeof(s->lastPlain), p);
    return;
  }
  const char* ev = tg_mb_str(&l, "ev");
  double eta = tg_mb_num(&l, "eta_s", -1);
  if(eta >= 0) s->etaS = eta;
  if(strcmp(ev, "stage") == 0) {
    TgMbStep st = stepNamed(tg_mb_str(&l, "stage"));
    if(st != TG_MB_STEP_NONE) s->step = st;
    copy(s->stepText, sizeof(s->stepText), tg_mb_str(&l, "text"));
    s->detail[0] = 0;
    s->fraction = st == TG_MB_STEP_RENDER && s->tracksTotal > 0 ? (double) s->tracksDone / s->tracksTotal
                  : st == TG_MB_STEP_NOTES || st == TG_MB_STEP_PLAN ? 0 : -1;
  } else if(strcmp(ev, "tools") == 0) {
    s->step = TG_MB_STEP_TOOLS;
    copy(s->detail, sizeof(s->detail), tg_mb_str(&l, "text"));
    double count = tg_mb_num(&l, "count", 0), k = tg_mb_num(&l, "n", -1);
    double i = tg_mb_num(&l, "i", 0), of = tg_mb_num(&l, "of", 0);
    if(count > 0 && k >= 0) s->fraction = 0.3 + 0.6 * k / count;  // the packages: most of the step
    else if(of > 0) s->fraction = (i - 1) / of;
  } else if(strcmp(ev, "download") == 0) {
    double got = tg_mb_num(&l, "got", 0), total = tg_mb_num(&l, "total", 0);
    double allGot = tg_mb_num(&l, "all_got", -1), allTotal = tg_mb_num(&l, "all_total", 0);
    const char* name = tg_mb_str(&l, "name");
    const char* lib = tg_mb_str(&l, "lib");
    char a[32], b[32];
    if(allGot >= 0 && allTotal > 0) {
      s->step = TG_MB_STEP_LIBRARIES;
      s->fraction = allGot / allTotal;
      double rate = tg_mb_num(&l, "rate", 0);
      tg_mb_format_size(allGot, a, sizeof(a));
      tg_mb_format_size(allTotal, b, sizeof(b));
      char speed[32] = "";
      if(rate > 0) snprintf(speed, sizeof(speed), ", %.1f MB/s", rate / 1e6);
      snprintf(s->detail, sizeof(s->detail), "%s of %s%s - now %s (%d of %d)", a, b, speed, name ? name : lib ? lib : "",
               (int) tg_mb_num(&l, "i", 0), (int) tg_mb_num(&l, "of", 0));
    } else if(total > 0) {
      // setup_tools.py: pip or Python itself.
      s->fraction = 0.3 * got / total;
      tg_mb_format_size(got, a, sizeof(a));
      tg_mb_format_size(total, b, sizeof(b));
      snprintf(s->detail, sizeof(s->detail), "Downloading %s: %s of %s", lib ? lib : "", a, b);
    }
  } else if(strcmp(ev, "unpack") == 0) {
    const char* name = tg_mb_str(&l, "name");
    snprintf(s->detail, sizeof(s->detail), "Unpacking %s (%d of %d)", name ? name : "", (int) tg_mb_num(&l, "i", 0),
             (int) tg_mb_num(&l, "of", 0));
  } else if(strcmp(ev, "notes") == 0) {
    s->step = TG_MB_STEP_NOTES;
    double e = tg_mb_num(&l, "elapsed_s", 0);
    s->fraction = e / 660.0 > 0.97 ? 0.97 : e / 660.0;
    snprintf(s->detail, sizeof(s->detail), "Playing every song of your ROM on the emulated sound chip");
  } else if(strcmp(ev, "planning") == 0) {
    s->step = TG_MB_STEP_PLAN;
    double i = tg_mb_num(&l, "i", 0), of = tg_mb_num(&l, "of", 0);
    if(of > 0) {
      s->fraction = i / of;
      snprintf(s->detail, sizeof(s->detail), "What each instrument plays: %d of %d tracks", (int) i, (int) of);
    }
  } else if(strcmp(ev, "plan") == 0) {
    s->tracksTotal = (int) tg_mb_num(&l, "tracks", s->tracksTotal);
    s->tracksDone = (int) tg_mb_num(&l, "done", s->tracksDone);
    s->downloadMb = (int) tg_mb_num(&l, "download_mb", s->downloadMb);
    s->downloadMaxMb = (int) tg_mb_num(&l, "download_max_mb", s->downloadMaxMb);
    if(tg_mb_num(&l, "planned", 0) != 0) {
      s->planned = true;
      char a[32];
      tg_mb_format_size(s->downloadMb * 1e6, a, sizeof(a));
      if(s->downloadMb > 0) snprintf(s->detail, sizeof(s->detail), "%s to download: only the samples your tracks play", a);
      else snprintf(s->detail, sizeof(s->detail), "Nothing more to download");
    }
  } else if(strcmp(ev, "track") == 0) {
    s->step = TG_MB_STEP_RENDER;
    s->tracksDone = (int) tg_mb_num(&l, "i", s->tracksDone);
    s->tracksTotal = (int) tg_mb_num(&l, "of", s->tracksTotal);
    const char* st = tg_mb_str(&l, "status");
    if(st && strcmp(st, "failed") == 0) s->tracksFailed++;
    if(s->tracksTotal > 0) s->fraction = (double) s->tracksDone / s->tracksTotal;
  } else if(strcmp(ev, "render") == 0) {
    s->step = TG_MB_STEP_RENDER;
    s->tracksDone = (int) tg_mb_num(&l, "done", s->tracksDone);
    s->tracksTotal = (int) tg_mb_num(&l, "of", s->tracksTotal);
    if(s->tracksTotal > 0) s->fraction = (double) s->tracksDone / s->tracksTotal;
    const char* running = tg_mb_str(&l, "running");
    if(running && running[0]) snprintf(s->detail, sizeof(s->detail), "Now: %s", running);
  } else if(strcmp(ev, "ready") == 0) {
    copy(s->python, sizeof(s->python), tg_mb_str(&l, "python"));
    s->sawReady = true;
  } else if(strcmp(ev, "error") == 0) {
    copy(s->errorCode, sizeof(s->errorCode), tg_mb_str(&l, "code") ? tg_mb_str(&l, "code") : "other");
    copy(s->errorText, sizeof(s->errorText), tg_mb_str(&l, "text"));
    copy(s->logPath, sizeof(s->logPath), tg_mb_str(&l, "log"));
  } else if(strcmp(ev, "cancelled") == 0) {
    s->sawCancelled = true;
  } else if(strcmp(ev, "done") == 0) {
    s->sawDone = true;
    s->fraction = 1;
    s->etaS = 0;
  }
}

void tg_mb_cancel(TgMbState* s) {
  if(s->phase == TG_MB_RUNNING) s->phase = TG_MB_CANCELLING;
}

void tg_mb_fail(TgMbState* s, const char* code, const char* text) {
  s->phase = TG_MB_FAILED;
  copy(s->errorCode, sizeof(s->errorCode), code);
  copy(s->errorText, sizeof(s->errorText), text);
}

bool tg_mb_child_exited(TgMbState* s, int exitCode, bool last) {
  if(s->phase == TG_MB_CANCELLING || s->sawCancelled || exitCode == 3) {
    s->phase = TG_MB_STOPPED;
    return false;
  }
  bool said = last ? s->sawDone : s->sawReady;
  if(said && !s->errorText[0] && (exitCode == 0 || exitCode == TG_MB_EXIT_UNKNOWN)) {
    if(!last) return s->phase == TG_MB_RUNNING;
    s->phase = TG_MB_FINISHED;
    s->fraction = 1;
    s->etaS = 0;
    return false;
  }
  s->phase = TG_MB_FAILED;
  if(!s->errorCode[0]) copy(s->errorCode, sizeof(s->errorCode), exitCode == 4 ? "tracks" : "other");
  if(!s->errorText[0]) {
    char code[48] = "";
    if(exitCode != TG_MB_EXIT_UNKNOWN) snprintf(code, sizeof(code), " (exit code %d)", exitCode);
    snprintf(s->errorText, sizeof(s->errorText), "The music build stopped unexpectedly%s%s%s", code,
             s->lastPlain[0] ? ": " : ".", s->lastPlain);
  }
  return false;
}

int tg_mb_step_number(TgMbStep step) {
  switch(step) {
    case TG_MB_STEP_NOTES: return 2;
    case TG_MB_STEP_PLAN: return 3;
    case TG_MB_STEP_LIBRARIES: return 4;
    case TG_MB_STEP_RENDER: return 5;
    default: return 1;
  }
}

// --- Words and numbers ----------------------------------------------------------------------

void tg_mb_format_eta(double seconds, char* out, size_t n) {
  if(n == 0) return;
  if(seconds < 0) {
    out[0] = 0;
    return;
  }
  long s = (long) (seconds + 0.5);
  if(s < 60) {
    snprintf(out, n, "under a minute left");
    return;
  }
  long m = (s + 30) / 60;
  if(m < 60) snprintf(out, n, "about %ld min left", m);
  else snprintf(out, n, "about %ld h %02ld min left", m / 60, m % 60);
}

void tg_mb_format_size(double bytes, char* out, size_t n) {
  if(bytes >= 1e9) snprintf(out, n, "%.1f GB", bytes / 1e9);
  else if(bytes >= 1e6) snprintf(out, n, "%.0f MB", bytes / 1e6);
  else snprintf(out, n, "%.0f KB", bytes / 1e3);
}

void tg_mb_format_hours(double seconds, char* out, size_t n) {
  double h = seconds / 3600;
  if(h >= 1.5) snprintf(out, n, "about %.0f hours", h);
  else if(h >= 0.75) snprintf(out, n, "about an hour");
  else snprintf(out, n, "about %.0f minutes", seconds / 60 < 1 ? 1 : seconds / 60);
}

double tg_mb_estimate_seconds(int tracks, int jobs, double downloadMb, bool notes, int trackS, int notesS, int rateMbs) {
  if(jobs < 1) jobs = 1;
  if(trackS <= 0) trackS = 150;
  if(notesS <= 0) notesS = 660;
  if(rateMbs <= 0) rateMbs = 8;
  double setup = 180;  // Python and its packages
  double plan = tracks * 1.0;  // planning: about 1.5 s a track, a few at once
  return setup + downloadMb / rateMbs + downloadMb / 1000 * 40 + (notes ? notesS : 0) + plan + (double) tracks * trackS / jobs;
}

int tg_mb_auto_jobs(int ramMb, int cpus) {
  // As generate.py's --jobs auto (tgmusic/progress.py concurrency): half the logical processors
  // (the other half for the parts each track plays at once; the generator runs below normal
  // priority, so the game comes first), at most as many as the memory holds (a track takes up
  // to about 1.6 GB, 1.5 GB is left for the rest). Beyond a third of that, the generator starts
  // a track only when the memory available then holds it, too.
  int byCpu = cpus / 2;
  if(byCpu < 1) byCpu = 1;
  int byMem = (ramMb - 1500) / 1600;
  if(byMem < 1) byMem = 1;
  return byCpu < byMem ? byCpu : byMem;
}

// --- launcher.txt ---------------------------------------------------------------------------

void tg_prefs_defaults(TgLauncherPrefs* p) {
  memset(p, 0, sizeof(*p));
  p->offer = TG_MB_OFFER_ASK;
  p->styles = TG_MB_STYLES_ALL;
  p->cleanAfter = false;
}

void tg_prefs_parse(TgLauncherPrefs* p, const char* text) {
  const char* line = text;
  while(line && *line) {
    const char* end = strpbrk(line, "\r\n");
    size_t len = end ? (size_t) (end - line) : strlen(line);
    char buf[2200];
    if(len >= sizeof(buf)) len = sizeof(buf) - 1;
    memcpy(buf, line, len);
    buf[len] = 0;
    char* sp = strchr(buf, ' ');
    if(sp) {
      *sp = 0;
      const char* v = sp + 1;
      if(strcmp(buf, "rom") == 0) snprintf(p->rom, sizeof(p->rom), "%s", v);
      else if(strcmp(buf, "music_offer") == 0) {
        int o = atoi(v);
        if(o >= TG_MB_OFFER_ASK && o <= TG_MB_OFFER_NEVER) p->offer = o;
      } else if(strcmp(buf, "music_styles") == 0) {
        unsigned m = (unsigned) strtoul(v, NULL, 10) & TG_MB_STYLES_ALL;
        if(m) p->styles = m;
      } else if(strcmp(buf, "music_clean") == 0) {
        p->cleanAfter = atoi(v) != 0;
      }
    }
    if(!end) break;
    line = end + 1;
  }
}

size_t tg_prefs_write(const TgLauncherPrefs* p, char* out, size_t n) {
  int w = snprintf(out, n, "rom %s\nmusic_offer %d\nmusic_styles %u\nmusic_clean %d\n", p->rom, p->offer,
                   p->styles & TG_MB_STYLES_ALL, p->cleanAfter ? 1 : 0);
  return w < 0 ? 0 : (size_t) w;
}

bool tg_mb_should_offer(bool romOk, bool toolsFound, int tracksBuilt, bool running, int offer, bool shownThisSession) {
  return romOk && toolsFound && tracksBuilt == 0 && !running && offer != TG_MB_OFFER_NEVER && !shownThisSession;
}

// --- consent.txt ----------------------------------------------------------------------------

// The next tab-separated field of a line (in place); NULL when there is none.
static char* field(char** p) {
  if(*p == NULL) return NULL;
  char* f = *p;
  char* tab = strchr(f, '\t');
  if(tab) *tab = 0, *p = tab + 1;
  else *p = NULL;
  return f;
}

bool tg_mb_parse_consent(const char* text, TgMbConsent* c) {
  memset(c, 0, sizeof(*c));
  for(int i = 0; i < 16; i++)
    for(int k = 0; k < TG_MB_STYLE_COUNT; k++) c->typical[i][k] = -1;
  const char* line = text;
  while(line && *line) {
    const char* end = strpbrk(line, "\r\n");
    size_t len = end ? (size_t) (end - line) : strlen(line);
    char buf[1024];
    if(len >= sizeof(buf)) len = sizeof(buf) - 1;
    memcpy(buf, line, len);
    buf[len] = 0;
    char* p = buf;
    char* kind = buf[0] == '#' ? NULL : field(&p);
    if(kind && strcmp(kind, "tracks") == 0) {
      char* v = field(&p);
      c->tracks = v ? atoi(v) : 0;
    } else if(kind && (strcmp(kind, "lib") == 0 || strcmp(kind, "tool") == 0)) {
      bool lib = kind[0] == 'l';
      TgMbItem* it = lib ? (c->nLibs < 16 ? &c->libs[c->nLibs] : NULL) : &c->tools;
      if(it) {
        char* f[6];
        int k = 0;
        if(lib) {
          char* id = field(&p);
          snprintf(it->id, sizeof(it->id), "%s", id ? id : "");
        } else {
          snprintf(it->id, sizeof(it->id), "tools");
        }
        for(; k < 6 && (f[k] = field(&p)) != NULL; k++) {}
        if(k >= 5) {
          snprintf(it->name, sizeof(it->name), "%s", f[0]);
          snprintf(it->licence, sizeof(it->licence), "%s", f[1]);
          snprintf(it->homepage, sizeof(it->homepage), "%s", f[2]);
          it->mbWindows = atoi(f[3]);
          it->mbLinux = atoi(f[4]);
          if(lib) c->nLibs++;
        }
      }
    } else if(kind && strcmp(kind, "style") == 0) {
      char* key = field(&p);
      char* list = field(&p);
      int st = -1;
      for(int i = 0; key && i < TG_MB_STYLE_COUNT; i++) if(strcmp(key, kTgMbStyleKeys[i]) == 0) st = i;
      while(st >= 0 && list && *list) {
        char* comma = strchr(list, ',');
        if(comma) *comma = 0;
        for(int i = 0; i < c->nLibs; i++) if(strcmp(c->libs[i].id, list) == 0) c->styleLibs[st] |= 1u << i;
        list = comma ? comma + 1 : NULL;
      }
    } else if(kind && strcmp(kind, "typical") == 0) {
      char* id = field(&p);
      char* key = field(&p);
      char* v = field(&p);
      int lib = -1, st = -1;
      for(int i = 0; id && i < c->nLibs; i++) if(strcmp(c->libs[i].id, id) == 0) lib = i;
      for(int i = 0; key && i < TG_MB_STYLE_COUNT; i++) if(strcmp(key, kTgMbStyleKeys[i]) == 0) st = i;
      if(lib >= 0 && st >= 0 && v && *v >= '0' && *v <= '9') c->typical[lib][st] = atoi(v);
    } else if(kind && (strcmp(kind, "disk") == 0 || strcmp(kind, "time") == 0)) {
      char* key = field(&p);
      char* v = field(&p);
      int x = v ? atoi(v) : 0;
      if(key == NULL) {
      } else if(strcmp(key, "libs") == 0) c->libsMb = x;
      else if(strcmp(key, "tools") == 0) c->toolsMb = x;
      else if(strcmp(key, "music") == 0) c->musicMb = x;
      else if(strcmp(key, "peak") == 0) c->peakMb = x;
      else if(strcmp(key, "track_s") == 0) c->trackS = x;
      else if(strcmp(key, "notes_s") == 0) c->notesS = x;
      else if(strcmp(key, "rate_mbs") == 0) c->rateMbs = x;
    }
    if(!end) break;
    line = end + 1;
  }
  return c->tracks > 0 && c->nLibs > 0 && c->tools.name[0];
}

unsigned tg_mb_consent_libs(const TgMbConsent* c, unsigned styles) {
  unsigned libs = c->styleLibs[0];  // the faithful title is always made
  for(int i = 0; i < TG_MB_STYLE_COUNT; i++) if(styles & (1u << i)) libs |= c->styleLibs[i];
  return libs;
}

int tg_mb_consent_download_mb(const TgMbConsent* c, unsigned libs, bool tools, bool windows) {
  int mb = tools ? (windows ? c->tools.mbWindows : c->tools.mbLinux) : 0;
  for(int i = 0; i < c->nLibs; i++)
    if(libs & (1u << i)) mb += windows ? c->libs[i].mbWindows : c->libs[i].mbLinux;
  return mb;
}

int tg_mb_consent_lib_typical_mb(const TgMbConsent* c, int lib, unsigned styles, bool windows) {
  if(lib < 0 || lib >= c->nLibs || !(tg_mb_consent_libs(c, styles) & (1u << lib))) return 0;
  unsigned made = styles | 1u;  // the faithful title is always made
  int all = windows ? c->libs[lib].mbWindows : c->libs[lib].mbLinux, most = -1;
  for(int k = 0; k < TG_MB_STYLE_COUNT; k++)
    if((made & (1u << k)) && c->typical[lib][k] > most) most = c->typical[lib][k];
  return most < 0 || most > all ? all : most;
}

int tg_mb_consent_typical_mb(const TgMbConsent* c, unsigned styles, bool tools, bool windows) {
  int mb = tools ? (windows ? c->tools.mbWindows : c->tools.mbLinux) : 0;
  for(int i = 0; i < c->nLibs; i++) mb += tg_mb_consent_lib_typical_mb(c, i, styles, windows);
  return mb;
}

void tg_mb_consent_disk(const TgMbConsent* c, unsigned styles, bool windows, int* keptMb, int* peakMb, int* musicMb) {
  unsigned all = (1u << c->nLibs) - 1, libs = tg_mb_consent_libs(c, styles);
  int allMb = tg_mb_consent_download_mb(c, all, false, windows), mb = tg_mb_consent_download_mb(c, libs, false, windows);
  int unpacked = allMb > 0 ? (int) ((double) c->libsMb * mb / allMb) : 0;
  int usual = allMb > 0 ? (int) ((double) c->libsMb * tg_mb_consent_typical_mb(c, styles, false, windows) / allMb) : 0;
  int biggest = 100;  // the biggest archive while it unpacks: a plug-in's (the samples come file by file)
  int music = c->tracks > 0 ? (int) ((double) c->musicMb * tg_mb_style_tracks(styles) / c->tracks) : 0;
  if(musicMb) *musicMb = music;
  if(keptMb) *keptMb = usual + c->toolsMb + music;
  // At most: every listed file of the libraries plus the biggest archive while it unpacks; for
  // all styles, the measured figure.
  int peak = unpacked + biggest + c->toolsMb + music;
  if(libs == all && c->peakMb > peak) peak = c->peakMb;
  if(peakMb) *peakMb = peak;
}
