// The modern music: which song the game starts (from its writes to the sound ports),
// which of the player's files plays for it, and the files streamed and mixed in.
// See music.h and docs/AUDIO.md.

#include "music.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

// Paths are UTF-8 (SDL's). Windows' fopen reads them in the system's code page, so a folder
// under a user name with accents would not open: there the path goes through _wfopen.
static FILE* open_utf8(const char* path, const char* mode) {
#ifdef _WIN32
  wchar_t wpath[1400], wmode[8];
  if(MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, (int) (sizeof(wpath) / sizeof(wpath[0]))) == 0) return NULL;
  if(MultiByteToWideChar(CP_UTF8, 0, mode, -1, wmode, (int) (sizeof(wmode) / sizeof(wmode[0]))) == 0) return NULL;
  return _wfopen(wpath, wmode);
#else
  return fopen(path, mode);
#endif
}

// --- The songs, the sets and the courses --------------------------------------------------

// By the value the game writes to port 0 (upload set + 1). The gain brings the remakes
// (all mastered to -14 LUFS) to the original song's own loudness, so the music sits under
// the engine and effects as it did on the sound chip (measured on the sound chip's output,
// ITU-R BS.1770: title -17.2, Frankfurt -17.4, Bordeaux -20.3, Hiroshima -19.0,
// Las Vegas -12.9, Qualified -11.2, Ending -11.6 LUFS), at most +1.5 dB (the files peak
// at -2.4 to -5 dBTP).
static const struct {
  const char* name;
  float gainDb;
} kSongs[8] = {
  {NULL, 0},
  {"01_title", -3.2f},
  {"05_frankfurt", -3.4f},
  {"04_bordeaux", -6.3f},
  {"03_hiroshima", -5.0f},
  {"02_las_vegas", 1.1f},
  {"06_qualified", 1.5f},
  {"07_ending", 1.5f},
};

static bool race_song(int song) { return song >= 2 && song <= 5; }

static const char* const kSetNames[TG_MUSIC_SET_COUNT] = {
  "ORIGINAL", "FAITHFUL", "MODERN", "CITY BAND", "LOCAL BAND", "POSTCARD", "SYNTHWAVE", "ORCHESTRAL",
};
static const char* const kSetKeys[TG_MUSIC_SET_COUNT] = {
  "original", "faithful", "modern", "band", "local", "postcard", "synthwave", "orchestral",
};

// The courses in the game's order ($1F06 bits 0-4): four a country, as the cities are
// named in the player's stages_manifest.json (and in the ROM's course names).
static const char* const kCountries[8] = {"usa", "south_america", "japan", "germany", "scandinavia", "france", "italy", "uk"};
static const char* const kCities[32] = {
  "las_vegas", "los_angeles", "new_york", "san_francisco",
  "rio", "machu_picchu", "chichen_itza", "rain_forest",
  "tokyo", "hiroshima", "yokohama", "kyoto",
  "munich", "cologne", "black_forest", "frankfurt",
  "stockholm", "copenhagen", "helsinki", "oslo",
  "paris", "nice", "bordeaux", "monaco",
  "pisa", "rome", "sicily", "florence",
  "london", "sheffield", "loch_ness", "stonehenge",
};

const char* tg_music_set_name(int set) { return set >= 0 && set < TG_MUSIC_SET_COUNT ? kSetNames[set] : "?"; }
const char* tg_music_set_key(int set) { return set >= 0 && set < TG_MUSIC_SET_COUNT ? kSetKeys[set] : "?"; }

int tg_music_set_from_key(const char* key) {
  for(int i = 0; i < TG_MUSIC_SET_COUNT; i++) if(strcmp(key, kSetKeys[i]) == 0) return i;
  return -1;
}

const char* tg_music_song_name(int song) { return song >= 1 && song <= 7 ? kSongs[song].name : NULL; }

void tg_music_course_city(int course, const char** country, const char** city) {
  *country = kCountries[(course & 31) >> 2];
  *city = kCities[course & 31];
}

// --- Streams ------------------------------------------------------------------------------

#define BLOCK 1024

typedef struct {
  bool active;
  stb_vorbis* vorbis;
  FILE* wav;
  long wavData;  // where the samples start
  int wavBits, wavFloat;
  int channels, rate;
  int64_t pos, length;         // in the file's frames (length -1: unknown, to the end)
  int64_t loopStart, loopEnd;  // loopEnd exclusive
  bool loop, ended;
  float gain;                  // linear
  float fade, fadeStep;        // fadeStep > 0: fading in to 1, < 0: out to 0 (then closed)
  // The file's frames, buffered, and the resampler's place between frames a and b.
  float buf[BLOCK * 2];
  int bufPos, bufLen;
  double phase;
  float a[2], b[2];
} Stream;

static void stream_close(Stream* s) {
  if(s->vorbis) stb_vorbis_close(s->vorbis);
  if(s->wav) fclose(s->wav);
  memset(s, 0, sizeof(*s));
}

static uint32_t le32(const uint8_t* p) { return (uint32_t) (p[0] | p[1] << 8 | p[2] << 16 | (uint32_t) p[3] << 24); }

// A RIFF WAVE file: PCM 16/24/32-bit or 32-bit float, its loop from a 'smpl' chunk.
static bool wav_open(Stream* s, const char* path) {
  FILE* f = open_utf8(path, "rb");
  if(f == NULL) return false;
  uint8_t h[12];
  bool fmt = false;
  int format = 0;
  int64_t dataSize = -1;
  if(fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) != 0 || memcmp(h + 8, "WAVE", 4) != 0) goto fail;
  for(;;) {
    uint8_t c[8];
    if(fread(c, 1, 8, f) != 8) break;
    uint32_t size = le32(c + 4);
    long body = ftell(f);
    if(memcmp(c, "fmt ", 4) == 0 && size >= 16) {
      uint8_t b[40] = {0};
      if(fread(b, 1, size < 40 ? size : 40, f) < 16) goto fail;
      format = b[0] | b[1] << 8;
      s->channels = b[2] | b[3] << 8;
      s->rate = (int) le32(b + 4);
      s->wavBits = b[14] | b[15] << 8;
      if(format == 0xFFFE && size >= 26) format = b[24] | b[25] << 8;  // extensible: the sub-format
      fmt = true;
    } else if(memcmp(c, "data", 4) == 0) {
      s->wavData = body;
      dataSize = size;
    } else if(memcmp(c, "smpl", 4) == 0 && size >= 60) {
      uint8_t b[60];
      if(fread(b, 1, 60, f) == 60 && le32(b + 28) >= 1) {
        s->loopStart = le32(b + 44);
        s->loopEnd = (int64_t) le32(b + 48) + 1;  // the chunk's end is inclusive
        s->loop = s->loopEnd > s->loopStart;
      }
    }
    if(fseek(f, body + size + (size & 1), SEEK_SET) != 0) break;
  }
  s->wavFloat = format == 3;
  if(!fmt || dataSize < 0 || s->channels < 1 || s->channels > 8 || s->rate < 8000 ||
     !((format == 1 && (s->wavBits == 16 || s->wavBits == 24 || s->wavBits == 32)) || (format == 3 && s->wavBits == 32)))
    goto fail;
  s->length = dataSize / (s->channels * (s->wavBits / 8));
  s->wav = f;
  return fseek(f, s->wavData, SEEK_SET) == 0;
fail:
  fclose(f);
  return false;
}

// Ogg Vorbis: the loop from LOOPSTART / LOOPLENGTH (or LOOPEND) comments, in frames.
static bool ogg_open(Stream* s, const char* path) {
  int err = 0;
  FILE* f = open_utf8(path, "rb");
  if(f == NULL) return false;
  s->vorbis = stb_vorbis_open_file(f, 1, &err, NULL);  // closes f when done (or on failure)
  if(s->vorbis == NULL) return false;
  stb_vorbis_info info = stb_vorbis_get_info(s->vorbis);
  s->channels = info.channels;
  s->rate = (int) info.sample_rate;
  s->length = -1;
  stb_vorbis_comment c = stb_vorbis_get_comment(s->vorbis);
  int64_t start = -1, len = -1, end = -1;
  for(int i = 0; i < c.comment_list_length; i++) {
    const char* t = c.comment_list[i];
    if(strncmp(t, "LOOPSTART=", 10) == 0) start = strtoll(t + 10, NULL, 10);
    else if(strncmp(t, "LOOPLENGTH=", 11) == 0) len = strtoll(t + 11, NULL, 10);
    else if(strncmp(t, "LOOPEND=", 8) == 0) end = strtoll(t + 8, NULL, 10);
  }
  if(start >= 0 && (len > 0 || end > start)) {
    s->loopStart = start;
    s->loopEnd = len > 0 ? start + len : end;
    s->loop = true;
  }
  return s->channels >= 1 && s->rate >= 8000;
}

static bool stream_seek(Stream* s, int64_t frame) {
  s->pos = frame;
  if(s->vorbis) return frame == 0 ? stb_vorbis_seek_start(s->vorbis) != 0 : stb_vorbis_seek(s->vorbis, (unsigned) frame) != 0;
  return fseek(s->wav, s->wavData + (long) (frame * s->channels * (s->wavBits / 8)), SEEK_SET) == 0;
}

// Up to n frames of the file as stereo floats; 0 at its end.
static int decode(Stream* s, float* out, int n) {
  if(s->vorbis) {
    int got = stb_vorbis_get_samples_float_interleaved(s->vorbis, 2, out, n * 2);
    if(s->channels == 1) for(int i = 0; i < got; i++) out[2 * i + 1] = out[2 * i];
    return got;
  }
  int bytes = s->wavBits / 8, frame = bytes * s->channels;
  static uint8_t raw[BLOCK * 8 * 4];
  if(n > BLOCK) n = BLOCK;
  int got = (int) fread(raw, (size_t) frame, (size_t) n, s->wav);
  for(int i = 0; i < got; i++) {
    for(int c = 0; c < 2; c++) {
      const uint8_t* p = raw + i * frame + (c < s->channels ? c : 0) * bytes;
      float v;
      if(s->wavFloat) {
        uint32_t u = le32(p);
        memcpy(&v, &u, 4);
      } else if(bytes == 2) {
        v = (int16_t) (p[0] | p[1] << 8) / 32768.0f;
      } else if(bytes == 3) {
        int32_t x = (int32_t) ((uint32_t) p[0] << 8 | (uint32_t) p[1] << 16 | (uint32_t) p[2] << 24) >> 8;
        v = x / 8388608.0f;
      } else {
        v = (int32_t) le32(p) / 2147483648.0f;
      }
      out[2 * i + c] = v;
    }
  }
  return got;
}

// Up to n frames from where the stream is, round its loop; fewer only at the end of a file
// that does not loop.
static int read_raw(Stream* s, float* out, int n) {
  int done = 0;
  while(done < n && !s->ended) {
    int64_t end = s->loop ? s->loopEnd : s->length >= 0 ? s->length : INT64_MAX;
    if(s->pos >= end) {
      if(!s->loop || !stream_seek(s, s->loopStart)) s->ended = true;
      continue;
    }
    int64_t want = n - done;
    if(want > end - s->pos) want = end - s->pos;
    int got = decode(s, out + 2 * done, (int) want);
    if(got <= 0) {
      // The file ends before its loop end: loop from where it ends.
      if(s->loop && s->pos > s->loopStart) {
        s->loopEnd = s->pos;
        if(stream_seek(s, s->loopStart)) continue;
      }
      s->ended = true;
      break;
    }
    s->pos += got;
    done += got;
  }
  return done;
}

static bool next_frame(Stream* s, float f[2]) {
  if(s->bufPos >= s->bufLen) {
    s->bufLen = read_raw(s, s->buf, BLOCK);
    s->bufPos = 0;
    if(s->bufLen == 0) return false;
  }
  f[0] = s->buf[2 * s->bufPos];
  f[1] = s->buf[2 * s->bufPos + 1];
  s->bufPos++;
  return true;
}

// n frames at the output rate (linear interpolation when the file's rate differs); the
// frames after a file's end are silence. Returns false when the stream has nothing left.
static bool stream_read(Stream* s, float* out, int n, int rate) {
  if(s->rate == rate) {
    int got = 0;
    while(got < n) {
      if(s->bufPos >= s->bufLen) {
        s->bufLen = read_raw(s, s->buf, BLOCK);
        s->bufPos = 0;
        if(s->bufLen == 0) break;
      }
      int k = s->bufLen - s->bufPos;
      if(k > n - got) k = n - got;
      memcpy(out + 2 * got, s->buf + 2 * s->bufPos, (size_t) k * 2 * sizeof(float));
      s->bufPos += k;
      got += k;
    }
    for(int i = got; i < n; i++) out[2 * i] = out[2 * i + 1] = 0;
    return got == n;
  }
  double step = (double) s->rate / rate;
  bool any = false;
  for(int i = 0; i < n; i++) {
    while(s->phase >= 1.0) {
      s->a[0] = s->b[0], s->a[1] = s->b[1];
      if(!next_frame(s, s->b)) s->b[0] = s->b[1] = 0;
      s->phase -= 1.0;
    }
    float t = (float) s->phase;
    out[2 * i] = s->a[0] + (s->b[0] - s->a[0]) * t;
    out[2 * i + 1] = s->a[1] + (s->b[1] - s->a[1]) * t;
    s->phase += step;
    any = true;
  }
  return any && !(s->ended && s->bufPos >= s->bufLen);
}

static bool stream_open(Stream* s, const char* path, bool loopByDefault) {
  memset(s, 0, sizeof(*s));
  const char* dot = strrchr(path, '.');
  bool ok = dot && strcmp(dot, ".ogg") == 0 ? ogg_open(s, path) : wav_open(s, path);
  if(!ok) {
    stream_close(s);
    return false;
  }
  if(!s->loop && loopByDefault) {
    // No loop in the file: the whole file loops (the game's songs play again and again).
    if(s->length < 0 && s->vorbis) s->length = stb_vorbis_stream_length_in_samples(s->vorbis);
    s->loopStart = 0;
    s->loopEnd = s->length;
    s->loop = s->loopEnd > 0;
  }
  if(s->loop && s->length >= 0 && s->loopEnd > s->length) s->loopEnd = s->length;
  s->active = true;
  s->fade = 1;
  s->phase = 1.0;  // the resampler reads its first frames at once
  return true;
}

// --- The director --------------------------------------------------------------------------

// Port 0 gets the song number only after the upload's last byte and a delay loop of about
// 65,536 iterations (seven frames); the IPL counters it carries during the upload come a
// few microseconds apart. A gap of a million master cycles (about three frames) tells them
// apart.
#define SONG_GAP 1000000u

struct TgMusic {
  char dir[1024];
  bool available;  // the folder has the player's music
  int rate;
  int set;
  FILE* log;
  bool dry;
  TgGame* game;
  uint32_t frame;
  // What the game did.
  bool uploading;  // after $18 (and at power-on) until a song starts
  int lastPort0;
  uint64_t lastPort0Cycles;
  bool paused;
  int song, course;
  // What plays.
  char file[1400];  // "" when the original plays
  bool openPending;
  float openFade;   // the new file's fade in, frames (0: at once)
  Stream cur, old;
  uint8_t mask;
};

static void logf_(TgMusic* m, const char* fmt, const char* a, const char* b) {
  if(m->log == NULL) return;
  fprintf(m->log, "frame %u ", m->frame);
  fprintf(m->log, fmt, a, b);
  fputc('\n', m->log);
  fflush(m->log);
}

static bool file_exists(const char* path) {
  FILE* f = open_utf8(path, "rb");
  if(f) fclose(f);
  return f != NULL;
}

// dir/rel.ogg or dir/rel.wav, if there.
static bool find(const TgMusic* m, const char* rel, char* out, size_t n) {
  static const char* const kExt[] = {".ogg", ".wav"};
  for(int i = 0; i < 2; i++) {
    snprintf(out, n, "%s/%s%s", m->dir, rel, kExt[i]);
    if(file_exists(out)) return true;
  }
  out[0] = 0;
  return false;
}

// The file for song in the set; why says which and how it was chosen.
static void resolve(const TgMusic* m, int set, int song, int course, char* out, size_t n, char* why, size_t wn) {
  out[0] = 0;
  snprintf(why, wn, "original");
  if(set == TG_MUSIC_ORIGINAL || !m->dir[0] || song < 1 || song > 7) return;
  const char* name = kSongs[song].name;
  char rel[256];
  static const char kLetter[TG_MUSIC_SET_COUNT] = {0, 0, 'D', 'C', 'B', 'A', 0, 0};
  static const char* const kFolder[TG_MUSIC_SET_COUNT] = {NULL, NULL, "stages-race", "stages-band", "stages", "stages"};
  if(kLetter[set]) {
    const char *country, *city;
    tg_music_course_city(course, &country, &city);
    // The city's own take on its race tune: only for the tune the game plays on that
    // course (the course's place in its country picks the tune).
    if(race_song(song) && ((course & 3) ^ 3) + 2 == song) {
      snprintf(rel, sizeof(rel), "%s/%s/%s__%s__%c", kFolder[set], country, country, city, kLetter[set]);
      if(find(m, rel, out, n)) {
        snprintf(why, wn, "%s %c %s/%s", tg_music_set_key(set), kLetter[set], country, city);
        return;
      }
      snprintf(why, wn, "no %c for %s/%s: faithful", kLetter[set], country, city);
    } else {
      snprintf(why, wn, race_song(song) ? "course %d plays another tune: faithful" : "no city version: faithful", course & 31);
    }
    set = TG_MUSIC_FAITHFUL;
  }
  static const char* const kStyle[TG_MUSIC_SET_COUNT] = {NULL, "faithful", NULL, NULL, NULL, NULL, "synthwave", "orchestral"};
  snprintf(rel, sizeof(rel), "variations/%s/%s__%s", name, name, kStyle[set]);
  if(find(m, rel, out, n)) {
    if(set != m->set) return;  // why already says it fell back to faithful
    snprintf(why, wn, "%s", kStyle[set]);
    return;
  }
  if(set != TG_MUSIC_FAITHFUL) {
    snprintf(rel, sizeof(rel), "variations/%s/%s__faithful", name, name);
    if(find(m, rel, out, n)) {
      snprintf(why, wn, "no %s: faithful", kStyle[set]);
      return;
    }
  }
  snprintf(why, wn, "no file: original");
}

static void set_mask(TgMusic* m, uint8_t mask) {
  m->mask = mask;
  if(m->game) tg_game_set_voice_mute(m->game, mask);
}

// The current file fades out (frames; 0: stops at once).
static void fade_out(TgMusic* m, float frames) {
  stream_close(&m->old);
  if(m->cur.active && frames > 0) {
    m->old = m->cur;
    m->old.fadeStep = -m->old.fade / frames;
    memset(&m->cur, 0, sizeof(m->cur));
  } else {
    stream_close(&m->cur);
  }
}

// Picks the file for the song now playing and leaves the chip's music voices out when
// there is one. fadeIn: frames (a change of set mid-song), 0 at a song's start.
static void choose(TgMusic* m, float fadeIn) {
  char why[160];
  resolve(m, m->set, m->song, m->course, m->file, sizeof(m->file), why, sizeof(why));
  set_mask(m, m->file[0] ? (race_song(m->song) ? 0x0F : 0xFF) : 0);
  m->openPending = m->file[0] != 0;
  m->openFade = fadeIn;
  if(m->log) {
    const char *country, *city;
    tg_music_course_city(m->course, &country, &city);
    char what[200];
    if(race_song(m->song)) snprintf(what, sizeof(what), "song %d %s course %d %s/%s", m->song, kSongs[m->song].name, m->course, country, city);
    else snprintf(what, sizeof(what), "song %d %s", m->song, kSongs[m->song].name);
    char line[1800];
    snprintf(line, sizeof(line), "[%s] %s -> %s", tg_music_set_key(m->set), why, m->file[0] ? m->file : "original (sound chip)");
    logf_(m, "start %s %s", what, line);
  }
}

static void on_port(void* ctx, int port, uint8_t val, uint64_t cycles) {
  TgMusic* m = ctx;
  if(port == 1) {
    if(val == 0x18) {
      // The driver mutes and returns to the loader: an upload follows.
      if(m->song) logf_(m, "stop (song %s)%s", kSongs[m->song].name, "");
      if(!m->uploading) fade_out(m, m->rate * 0.04f);
      m->uploading = true;
      m->song = 0;
      m->paused = false;
      m->openPending = false;
      m->file[0] = 0;
    } else if(!m->uploading && m->song) {
      bool p = val == 0x19;  // the driver halts while port 1 holds $19
      if(p != m->paused) logf_(m, "%s%s", p ? "pause" : "resume", "");
      m->paused = p;
    }
    return;
  }
  if(port != 0) return;
  uint64_t gap = cycles >= m->lastPort0Cycles ? cycles - m->lastPort0Cycles : 0;
  if(m->uploading && val >= 1 && val <= 7 && m->lastPort0 == 0 && gap >= SONG_GAP) {
    m->uploading = false;
    m->song = val;
    m->paused = false;
    m->course = m->game ? tg_game_wram(m->game)[0x1F06] & 31 : 0;
    stream_close(&m->cur);
    choose(m, 0);
  }
  m->lastPort0 = val;
  m->lastPort0Cycles = cycles;
}

bool tg_music_dir_has_music(const char* dir) {
  static const char* const kExt[] = {".ogg", ".wav"};
  char path[1400];
  for(int i = 0; i < 2; i++) {
    snprintf(path, sizeof(path), "%s/variations/01_title/01_title__faithful%s", dir, kExt[i]);
    if(file_exists(path)) return true;
  }
  return false;
}

TgMusic* tg_music_create(const char* dir, int rate) {
  TgMusic* m = calloc(1, sizeof(TgMusic));
  if(m == NULL) return NULL;
  if(dir) {
    snprintf(m->dir, sizeof(m->dir), "%s", dir);
    size_t n = strlen(m->dir);
    while(n > 0 && (m->dir[n - 1] == '/' || m->dir[n - 1] == '\\')) m->dir[--n] = 0;
  }
  char path[1400];
  m->available = m->dir[0] && find(m, "variations/01_title/01_title__faithful", path, sizeof(path));
  m->rate = rate;
  m->set = TG_MUSIC_DEFAULT;
  m->uploading = true;  // power-on: the loader uploads the driver first
  m->lastPort0 = -1;
  return m;
}

void tg_music_destroy(TgMusic* m) {
  if(m == NULL) return;
  tg_music_attach(m, NULL);
  stream_close(&m->cur);
  stream_close(&m->old);
  free(m);
}

void tg_music_attach(TgMusic* m, TgGame* game) {
  if(m->game) {
    tg_game_set_apu_observer(m->game, NULL, NULL);
    tg_game_set_voice_mute(m->game, 0);
  }
  m->game = game;
  if(game) {
    tg_game_set_apu_observer(game, on_port, m);
    tg_game_set_voice_mute(game, m->mask);
  }
}

void tg_music_set_set(TgMusic* m, int set) {
  if(set < 0 || set >= TG_MUSIC_SET_COUNT || set == m->set) return;
  m->set = set;
  if(m->song == 0) return;
  float fade = m->rate * 0.15f;
  fade_out(m, fade);
  choose(m, fade);
}

int tg_music_get_set(const TgMusic* m) { return m->set; }

bool tg_music_available(const TgMusic* m) { return m->available; }
void tg_music_set_log(TgMusic* m, FILE* log) { m->log = log; }
void tg_music_set_dry(TgMusic* m, bool dry) { m->dry = dry; }
int tg_music_song(const TgMusic* m) { return m->song; }
const char* tg_music_file(const TgMusic* m) { return m->file[0] ? m->file : NULL; }

void tg_music_end_frame(TgMusic* m, uint32_t frame) {
  m->frame = frame;
  if(!m->openPending) return;
  m->openPending = false;
  stream_close(&m->cur);
  bool loop = m->song != 6;  // Qualified plays once
  if(!stream_open(&m->cur, m->file, loop)) {
    logf_(m, "cannot play %s: original%s", m->file, "");
    m->file[0] = 0;
    set_mask(m, 0);
    return;
  }
  m->cur.gain = powf(10, kSongs[m->song].gainDb / 20);
  if(m->openFade > 0) {
    m->cur.fade = 0;
    m->cur.fadeStep = 1 / m->openFade;
  }
  if(m->log) {
    char info[200];
    if(m->cur.loop) snprintf(info, sizeof(info), "%d Hz %d ch, loop %lld-%lld (%.3f s), gain %.1f dB", m->cur.rate, m->cur.channels,
                             (long long) m->cur.loopStart, (long long) m->cur.loopEnd,
                             (double) (m->cur.loopEnd - m->cur.loopStart) / m->cur.rate, kSongs[m->song].gainDb);
    else snprintf(info, sizeof(info), "%d Hz %d ch, plays once, gain %.1f dB", m->cur.rate, m->cur.channels, kSongs[m->song].gainDb);
    logf_(m, "  opened: %s%s", info, "");
  }
  if(m->dry) stream_close(&m->cur);
}

static void mix_stream(TgMusic* m, Stream* s, float* acc, int n) {
  static float tmp[BLOCK * 2];
  bool more = stream_read(s, tmp, n, m->rate);
  for(int i = 0; i < n; i++) {
    float g = s->gain * s->fade;
    acc[2 * i] += tmp[2 * i] * g;
    acc[2 * i + 1] += tmp[2 * i + 1] * g;
    if(s->fadeStep != 0) {
      s->fade += s->fadeStep;
      if(s->fade >= 1) s->fade = 1, s->fadeStep = 0;
      if(s->fade <= 0) {
        s->fade = 0;
        more = false;
        break;
      }
    }
  }
  if(!more) stream_close(s);
}

void tg_music_mix(TgMusic* m, int16_t* samples, int frames) {
  if(m->paused || m->dry || (!m->cur.active && !m->old.active)) return;
  static float acc[BLOCK * 2];
  for(int at = 0; at < frames; at += BLOCK) {
    int n = frames - at < BLOCK ? frames - at : BLOCK;
    memset(acc, 0, (size_t) n * 2 * sizeof(float));
    if(m->old.active) mix_stream(m, &m->old, acc, n);
    if(m->cur.active) mix_stream(m, &m->cur, acc, n);
    int16_t* out = samples + 2 * at;
    for(int i = 0; i < 2 * n; i++) {
      float v = out[i] + acc[i] * 32767.0f;
      out[i] = (int16_t) (v > 32767 ? 32767 : v < -32768 ? -32768 : lrintf(v));
    }
  }
}
