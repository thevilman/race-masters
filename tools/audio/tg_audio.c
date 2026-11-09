// tg_audio: rips Top Gear (USA)'s music and sound effects with the LakeSnes core.
//
// The game's own code starts every song: the tool boots the ROM, then calls the menu
// routine MenuStartMusic ($0F:8133) with A = the song number, exactly as the game does
// (it sends $18 on port 1, runs UploadSoundDriver ($07:8000) with that number, then writes
// song + 1 to port 0). When port 0 gets song + 1 the APU state is saved as an .spc file,
// and the APU is then run on its own (the CPU stays stopped, the ports keep their
// values) to render 32 kHz stereo audio straight from the DSP, one sample per DSP tick.
//
// Sound effects: the race upload is made the same way but no song is started (port 0
// stays 0), then each sound command is written to port 1 the way the race IRQ
// (RaceIrqTop, $00:B850) sends them.
//
// Nothing here changes the game: it only reads the ROM and writes files.
//
//   tg_audio --rom ROM --out DIR songs [--seconds S]
//       song_U.spc (snapshot as the song starts) and song_U_raw.wav (S seconds, default
//       420) for the 7 song uploads U = 0..6, plus song_U_srcn.txt (sample numbers used,
//       pattern loads) and song_U_notes.txt (every key-on: sample, voice, sample number,
//       pitch, volumes; pitch / volume changes; every key-off; DSP envelope, echo and
//       noise registers and the driver's tempo when they change). --only U renders one
//       upload; --keep MASK keeps only those voices audible (stems; no logs then).
//   tg_audio --rom ROM probe
//       which port-0 values make sound after each upload (looks for hidden songs).
//   tg_audio --rom ROM --out DIR sfx [--upload U] [--first A] [--last B] [--seconds S]
//       sfx_CC.wav for each port-1 command CC, after upload U (default 1) with no song.
//   tg_audio --rom ROM --out DIR engine [--upload U]
//       engine.wav: the engine command ($02, then $03 with port 2/3 = RPM + $400)
//       with the RPM swept up and down.
//   tg_audio --rom ROM log [--input FILE] --frames N
//       runs the original game with an input recording and prints every CPU write to the
//       APU ports: frame, PC, port, value. --wav FILE [--wav-from FRAME] also records
//       the game's audio (music and effects together) as it plays.
//   tg_audio render-spc FILE.spc OUT.wav SECONDS
//       plays an .spc file on a fresh LakeSnes APU (used to check the .spc files).
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#include "apu.h"
#include "cpu.h"
#include "dsp.h"
#include "input_script.h"
#include "snes.h"
#include "spc.h"

#define RATE 32000
#define NUM_UPLOADS 7

static const char* kSongTitles[NUM_UPLOADS] = {
  "Title", "Frankfurt", "Bordeaux", "Hiroshima", "Las Vegas", "Qualified", "Ending Theme",
};

// ---------------------------------------------------------------- files

static uint8_t* read_file(const char* path, long* size) {
  FILE* f = fopen(path, "rb");
  if(f == NULL) return NULL;
  fseek(f, 0, SEEK_END);
  *size = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t* data = malloc((size_t) *size);
  if(fread(data, 1, (size_t) *size, f) != (size_t) *size) {
    fclose(f);
    free(data);
    return NULL;
  }
  fclose(f);
  return data;
}

static void put16(uint8_t* p, uint16_t v) { p[0] = (uint8_t) v; p[1] = (uint8_t) (v >> 8); }
static void put32(uint8_t* p, uint32_t v) { put16(p, (uint16_t) v); put16(p + 2, (uint16_t) (v >> 16)); }

static bool write_wav(const char* path, const int16_t* samples, long frames) {
  FILE* f = fopen(path, "wb");
  if(f == NULL) return false;
  uint8_t h[44];
  uint32_t bytes = (uint32_t) frames * 4;
  memcpy(h, "RIFF", 4);
  put32(h + 4, 36 + bytes);
  memcpy(h + 8, "WAVEfmt ", 8);
  put32(h + 16, 16);
  put16(h + 20, 1);
  put16(h + 22, 2);
  put32(h + 24, RATE);
  put32(h + 28, RATE * 4);
  put16(h + 32, 4);
  put16(h + 34, 16);
  memcpy(h + 36, "data", 4);
  put32(h + 40, bytes);
  fwrite(h, 1, 44, f);
  for(long i = 0; i < frames * 2; i++) {  // little-endian whatever the host
    uint8_t b[2];
    put16(b, (uint16_t) samples[i]);
    fwrite(b, 1, 2, f);
  }
  fclose(f);
  return true;
}

// ---------------------------------------------------------------- the APU

typedef struct {
  int16_t* data;
  long frames, capacity;
  uint8_t srcnUsed[256];  // sample numbers heard (a voice with a non-zero envelope)
  long marks[64];  // sample numbers at which the driver loaded the next pattern
  int nmarks;
  FILE* notes;  // when set: every key-on / key-off (a transcription of the song)
  bool kon[8], koff[8];
  uint16_t lastPitch[8];
  int8_t lastVolL[8], lastVolR[8];
  uint8_t lastReg[0x80];
  int lastTempo;
  uint8_t keep;  // voices heard (bit per voice); the others are silenced (for stems)
} Render;

// DSP registers that are not per-voice note data, logged when they change: main and
// echo volumes, echo feedback, echo/noise/pitch-modulation enables, FLG (noise clock),
// echo delay, the echo FIR taps, and each voice's ADSR/GAIN.
static bool logged_reg(int a) {
  int lo = a & 0x0F;
  if(lo == 0x0F) return true;  // FIR
  if(lo >= 5 && lo <= 7) return true;  // ADSR1, ADSR2, GAIN
  switch(a) {
    case 0x0C: case 0x1C: case 0x2C: case 0x3C: case 0x0D: case 0x2D: case 0x3D: case 0x4D:
    case 0x6C: case 0x6D: case 0x7D: return true;
  }
  return false;
}

// The driver (an N-SPC-style engine at $0800) loads the next pattern of the playlist at
// $0A94 (MOVW $16,YA). Every song's playlist is one pattern repeated, so the distance
// between two marks is the loop length.
#define PATTERN_PC 0x0A94

// Runs the APU on its own until frames more DSP samples have come out.
static void apu_render_from(Apu* apu, Render* r, long frames, uint16_t last);
static void apu_render(Apu* apu, Render* r, long frames) {
  apu_render_from(apu, r, frames, apu->dsp->sampleOffset);
}

// From DSP sample number last on (samples already made are taken from the DSP's
// 1024-sample ring).
static void apu_render_from(Apu* apu, Render* r, long frames, uint16_t last) {
  if(r->frames + frames > r->capacity) {
    r->capacity = r->frames + frames;
    r->data = realloc(r->data, (size_t) r->capacity * 2 * sizeof(int16_t));
  }
  Dsp* dsp = apu->dsp;
  long target = r->frames + frames;
  if(r->keep == 0) r->keep = 0xFF;
  if(r->notes && r->lastTempo == 0) {
    r->lastTempo = -1;
    memset(r->lastReg, 0xFF, sizeof(r->lastReg));  // log every register once at the start
    for(int a = 0; a < 0x80; a++) if(logged_reg(a)) r->lastReg[a] = (uint8_t) ~dsp->ram[a];
  }
  for(;;) {
    while(last != dsp->sampleOffset && r->frames < target) {
      r->data[r->frames * 2] = dsp->sampleBuffer[(last & 0x3ff) * 2];
      r->data[r->frames * 2 + 1] = dsp->sampleBuffer[(last & 0x3ff) * 2 + 1];
      last++;
      r->frames++;
    }
    if(r->frames >= target) break;
    if(apu->spc->pc == PATTERN_PC && r->nmarks < 64) r->marks[r->nmarks++] = r->frames;
    spc_runOpcode(apu->spc);
    if(r->keep != 0xFF) {  // stems: silence the other voices (the driver never reads these back)
      for(int v = 0; v < 8; v++) {
        if(!(r->keep & (1 << v))) {
          dsp->channel[v].volumeL = 0;
          dsp->channel[v].volumeR = 0;
        }
      }
    }
    if(r->notes) {
      FILE* f = r->notes;
      for(int a = 0; a < 0x80; a++) {
        if(logged_reg(a) && dsp->ram[a] != r->lastReg[a]) {
          r->lastReg[a] = dsp->ram[a];
          fprintf(f, "reg %ld %02X %02X\n", r->frames, a, dsp->ram[a]);
        }
      }
      if(apu->ram[0x3D] != r->lastTempo) {  // the driver's tempo (timer-0 ticks are scaled by it)
        r->lastTempo = apu->ram[0x3D];
        fprintf(f, "tempo %ld %d\n", r->frames, r->lastTempo);
      }
      for(int v = 0; v < 8; v++) {
        DspChannel* ch = &dsp->channel[v];
        const uint8_t* vr = &dsp->ram[v << 4];
        if(ch->keyOn && !r->kon[v]) {
          fprintf(f, "on %ld %d %d %d %d %d\n", r->frames, v, ch->srcn, ch->pitch, (int8_t) vr[0], (int8_t) vr[1]);
          r->lastPitch[v] = ch->pitch;
          r->lastVolL[v] = (int8_t) vr[0];
          r->lastVolR[v] = (int8_t) vr[1];
        } else {
          if(ch->pitch != r->lastPitch[v]) {
            r->lastPitch[v] = ch->pitch;
            fprintf(f, "pitch %ld %d %d\n", r->frames, v, ch->pitch);
          }
          if((int8_t) vr[0] != r->lastVolL[v] || (int8_t) vr[1] != r->lastVolR[v]) {
            r->lastVolL[v] = (int8_t) vr[0];
            r->lastVolR[v] = (int8_t) vr[1];
            fprintf(f, "vol %ld %d %d %d\n", r->frames, v, r->lastVolL[v], r->lastVolR[v]);
          }
        }
        if(ch->keyOff && !r->koff[v]) fprintf(f, "off %ld %d\n", r->frames, v);
        r->kon[v] = ch->keyOn;
        r->koff[v] = ch->keyOff;
      }
    }
    for(int v = 0; v < 8; v++) {
      if(dsp->channel[v].gain != 0) r->srcnUsed[dsp->channel[v].srcn] = 1;
    }
  }
}

static void write_srcn(const char* path, const Render* r, const Apu* apu) {
  FILE* f = fopen(path, "w");
  if(f == NULL) return;
  fprintf(f, "dir %02X\n", apu->dsp->ram[0x5D]);
  for(int i = 0; i < r->nmarks; i++) fprintf(f, "pattern %ld\n", r->marks[i]);
  for(int i = 0; i < 256; i++) {
    if(r->srcnUsed[i]) fprintf(f, "%d\n", i);
  }
  fclose(f);
}

// ---------------------------------------------------------------- .spc files

static uint8_t apu_psw(const Spc* s) {
  return (uint8_t) ((s->n << 7) | (s->v << 6) | (s->p << 5) | (s->b << 4) | (s->h << 3) | (s->i << 2) |
                    (s->z << 1) | (uint8_t) s->c);
}

static void field(uint8_t* p, int size, const char* text) {
  memset(p, 0, (size_t) size);
  size_t n = strlen(text);
  memcpy(p, text, n < (size_t) size ? n : (size_t) size);
}

// SPC v0.30 with a text ID666 tag (see tools/audio/README.md for the layout).
static bool write_spc(const char* path, const Apu* apu, const char* title, const char* comment,
                      int seconds, int fadeMs) {
  static uint8_t f[0x10200];
  memset(f, 0, sizeof(f));
  memcpy(f, "SNES-SPC700 Sound File Data v0.30", 33);
  f[0x21] = 26;
  f[0x22] = 26;
  f[0x23] = 26;  // has an ID666 tag
  f[0x24] = 30;
  put16(f + 0x25, apu->spc->pc);
  f[0x27] = apu->spc->a;
  f[0x28] = apu->spc->x;
  f[0x29] = apu->spc->y;
  f[0x2A] = apu_psw(apu->spc);
  f[0x2B] = apu->spc->sp;
  field(f + 0x2E, 32, title);
  field(f + 0x4E, 32, "Top Gear");
  field(f + 0x6E, 16, "tg_audio");
  field(f + 0x7E, 32, comment);
  time_t now = time(NULL);
  struct tm* tm = localtime(&now);
  char buf[48];
  snprintf(buf, sizeof(buf), "%02d/%02d/%04d", tm->tm_mon + 1, tm->tm_mday, tm->tm_year + 1900);
  field(f + 0x9E, 11, buf);
  snprintf(buf, sizeof(buf), "%d", seconds);
  field(f + 0xA9, 3, buf);
  snprintf(buf, sizeof(buf), "%d", fadeMs);
  field(f + 0xAC, 5, buf);
  field(f + 0xB1, 32, "Barry Leitch");
  f[0xD2] = 0;
  // RAM, with the I/O registers as the CPU and the driver left them.
  uint8_t* ram = f + 0x100;
  memcpy(ram, apu->ram, 0x10000);
  uint8_t ctrl = (uint8_t) (apu->romReadable ? 0x80 : 0);
  for(int i = 0; i < 3; i++) {
    if(apu->timer[i].enabled) ctrl |= (uint8_t) (1 << i);
  }
  ram[0xF1] = ctrl;
  ram[0xF2] = apu->dspAdr;
  ram[0xF3] = apu->dsp->ram[apu->dspAdr & 0x7F];
  for(int i = 0; i < 4; i++) ram[0xF4 + i] = apu->inPorts[i];  // what the CPU wrote
  ram[0xF8] = apu->inPorts[4];
  ram[0xF9] = apu->inPorts[5];
  for(int i = 0; i < 3; i++) {
    ram[0xFA + i] = apu->timer[i].target;
    ram[0xFD + i] = apu->timer[i].counter;
  }
  memcpy(f + 0x10100, apu->dsp->ram, 0x80);
  memcpy(f + 0x101C0, apu->ram + 0xFFC0, 0x40);  // the RAM under the IPL ROM
  FILE* out = fopen(path, "wb");
  if(out == NULL) return false;
  fwrite(f, 1, sizeof(f), out);
  fclose(out);
  return true;
}

// Loads an .spc file into a fresh APU, the way SPC players do.
static Apu* load_spc(const char* path) {
  long size;
  uint8_t* f = read_file(path, &size);
  if(f == NULL || size < 0x10180 || memcmp(f, "SNES-SPC700 Sound File Data", 27) != 0) {
    fprintf(stderr, "%s: not an .spc file\n", path);
    free(f);
    return NULL;
  }
  Apu* apu = apu_init(NULL);
  apu_reset(apu);
  const uint8_t* ram = f + 0x100;
  memcpy(apu->ram, ram, 0x10000);
  if(size >= 0x10200) memcpy(apu->ram + 0xFFC0, f + 0x101C0, 0x40);
  Spc* s = apu->spc;
  s->pc = (uint16_t) (f[0x25] | (f[0x26] << 8));
  s->a = f[0x27];
  s->x = f[0x28];
  s->y = f[0x29];
  uint8_t p = f[0x2A];
  s->n = p & 0x80; s->v = p & 0x40; s->p = p & 0x20; s->b = p & 0x10;
  s->h = p & 0x08; s->i = p & 0x04; s->z = p & 0x02; s->c = p & 0x01;
  s->sp = f[0x2B];
  s->resetWanted = false;
  s->stopped = false;
  uint8_t ctrl = ram[0xF1];
  for(int i = 0; i < 3; i++) {
    apu->timer[i].enabled = ctrl & (1 << i);
    apu->timer[i].target = ram[0xFA + i];
    apu->timer[i].counter = ram[0xFD + i] & 0x0F;
  }
  apu->romReadable = ctrl & 0x80;
  apu->dspAdr = ram[0xF2];
  for(int i = 0; i < 4; i++) apu->inPorts[i] = ram[0xF4 + i];
  apu->inPorts[4] = ram[0xF8];
  apu->inPorts[5] = ram[0xF9];
  for(int i = 0; i < 0x80; i++) {
    if(i == 0x4C) continue;  // KON last, as players do
    dsp_write(apu->dsp, (uint8_t) i, f[0x10100 + i]);
  }
  dsp_write(apu->dsp, 0x4C, 0);
  free(f);
  return apu;
}

// ---------------------------------------------------------------- the game

typedef struct {
  Snes* snes;
  bool hijack;  // calling MenuStartMusic: stop the CPU when it returns to the sentinel
  bool done;
  int watchPort0;  // the port-0 value that starts the song (-1: none)
  bool portWritten;  // watchPort0 was just written: snapshot before the next instruction
  bool snapshotTaken;
  uint16_t snapOffset;  // the DSP sample counter at the snapshot
  const char* spcPath;
  const char* spcTitle;
  const char* spcComment;
  bool logWrites;
  long frame;
} Game;

static Game g_game;

#define SENTINEL 0x8000  // $0F:8000 (NmiHandler); NMIs are off while the hook is set

static bool call_hook(void* ctx, Cpu* cpu) {
  Game* game = ctx;
  if(game->portWritten) {
    // The CPU has just written song + 1 to port 0 (the APU caught up before the write):
    // this is the state the song starts from.
    game->portWritten = false;
    game->snapshotTaken = true;
    game->snapOffset = game->snes->apu->dsp->sampleOffset;
    if(game->spcPath && !write_spc(game->spcPath, game->snes->apu, game->spcTitle, game->spcComment, 180, 10000)) {
      fprintf(stderr, "cannot write %s\n", game->spcPath);
      exit(1);
    }
  }
  if(game->hijack && cpu->k == 0x0F && cpu->pc == SENTINEL) {
    cpu->stopped = true;
    game->done = true;
    return true;
  }
  return false;
}

static bool is_apu_port(uint32_t adr) {
  uint8_t bank = (uint8_t) (adr >> 16);
  return (bank < 0x40 || (bank >= 0x80 && bank < 0xC0)) && (adr & 0xFFFC) == 0x2140;
}

static void write_hook(void* ctx, uint32_t adr, uint8_t val) {
  Game* game = ctx;
  if(!is_apu_port(adr)) return;
  if(game->hijack && (adr & 3) == 0 && game->watchPort0 >= 0 && val == game->watchPort0) game->portWritten = true;
  if(game->logWrites) {
    Cpu* cpu = game->snes->cpu;
    printf("%ld %02X:%04X %d %02X\n", game->frame, cpu->k, cpu->pc, (int) (adr & 3), val);
  }
}

static Snes* boot(const char* romPath) {
  long size;
  uint8_t* rom = read_file(romPath, &size);
  if(rom == NULL) {
    fprintf(stderr, "cannot read %s\n", romPath);
    exit(1);
  }
  const uint8_t* data = rom;
  if(size % 0x8000 == 512) {  // copier header
    data += 512;
    size -= 512;
  }
  Snes* snes = snes_init();
  if(!snes_loadRom(snes, data, (int) size)) {
    fprintf(stderr, "LakeSnes cannot load %s\n", romPath);
    exit(1);
  }
  free(rom);
  memset(&g_game, 0, sizeof(g_game));
  g_game.snes = snes;
  g_game.watchPort0 = -1;
  snes->readHookCtx = &g_game;
  snes->writeHook = write_hook;
  snes->cpu->callHook = call_hook;
  snes->cpu->callCtx = &g_game;
  return snes;
}

static void run_frames(Snes* snes, int n) {
  for(int i = 0; i < n; i++) {
    snes_runFrame(snes);
    g_game.frame++;
  }
}

// Calls MenuStartMusic ($0F:8133) with A = upload, as the menus do (JSR from bank $0F,
// 16-bit A and X). With startSong false the routine stops when it writes 0 to port 0:
// the upload is done and no song is playing (for the sound effects). Returns after the
// routine has returned and the APU has caught up.
static void start_music(Snes* snes, int upload) {
  snes_write(snes, 0x004200, 0x00);  // no NMI/IRQ while the routine runs on its own
  Cpu* cpu = snes->cpu;
  cpu->e = false;
  cpu->mf = false;
  cpu->xf = false;
  cpu->d = false;
  cpu->i = true;
  cpu->waiting = false;
  cpu->stopped = false;
  cpu->nmiWanted = false;
  cpu->irqWanted = false;
  cpu->intWanted = false;
  cpu->k = 0x0F;
  cpu->db = 0x0F;
  cpu->dp = 0;
  cpu->a = (uint16_t) upload;
  cpu->sp = 0x01FD;
  uint16_t ret = SENTINEL - 1;  // RTS adds 1
  snes->ram[0x01FE] = (uint8_t) ret;
  snes->ram[0x01FF] = (uint8_t) (ret >> 8);
  cpu->pc = 0x8133;
  g_game.hijack = true;
  g_game.done = false;
  for(int i = 0; i < 600 && !g_game.done; i++) run_frames(snes, 1);
  g_game.hijack = false;
  if(!g_game.done) {
    fprintf(stderr, "MenuStartMusic(%d) did not return\n", upload);
    exit(1);
  }
}

static Snes* boot_to_title(const char* romPath) {
  Snes* snes = boot(romPath);
  run_frames(snes, 240);  // boot: the driver upload and the title song have started
  return snes;
}

// ---------------------------------------------------------------- commands

static double rms(const int16_t* d, long frames) {
  double s = 0;
  for(long i = 0; i < frames * 2; i++) s += (double) d[i] * d[i];
  return frames ? sqrt(s / (frames * 2)) : 0;
}

static int cmd_songs(const char* romPath, const char* outDir, int seconds, int only, int keep) {
  for(int u = 0; u < NUM_UPLOADS; u++) {
    if(only >= 0 && u != only) continue;
    Snes* snes = boot_to_title(romPath);
    char path[1024], title[64], comment[64];
    snprintf(title, sizeof(title), "%s", kSongTitles[u]);
    snprintf(comment, sizeof(comment), "upload %d, port0=%d via $0F:8133", u, u + 1);
    snprintf(path, sizeof(path), "%s/song_%d.spc", outDir, u);
    // MenuStartMusic saves the .spc as it writes port 0 = u + 1 (call_hook).
    g_game.watchPort0 = u + 1;
    g_game.spcPath = path;
    g_game.spcTitle = title;
    g_game.spcComment = comment;
    start_music(snes, u);
    g_game.watchPort0 = -1;
    g_game.spcPath = NULL;
    if(!g_game.snapshotTaken) {
      fprintf(stderr, "song %d: port 0 never got %d\n", u, u + 1);
      return 1;
    }
    Apu* apu = snes->apu;
    // The audio from the snapshot on: the samples made while the frame finished are
    // still in the DSP's ring, then the APU runs on its own.
    Render r = {0};
    r.keep = (uint8_t) keep;
    snprintf(path, sizeof(path), "%s/song_%d_notes.txt", outDir, u);
    if(keep == 0xFF) r.notes = fopen(path, "w");
    apu_render_from(apu, &r, (long) seconds * RATE, g_game.snapOffset);
    if(r.notes) fclose(r.notes);
    snprintf(path, sizeof(path), "%s/song_%d_raw.wav", outDir, u);
    write_wav(path, r.data, r.frames);
    snprintf(path, sizeof(path), "%s/song_%d_srcn.txt", outDir, u);
    write_srcn(path, &r, apu);
    printf("song %d %s: %ld frames, rms %.1f, port0=%d, apu pc %04X\n", u, kSongTitles[u], r.frames,
           rms(r.data, r.frames), apu->inPorts[0], apu->spc->pc);
    free(r.data);
    snes_free(snes);
  }
  return 0;
}

// Saves the whole emulator after an upload with no song, to try commands from.
static uint8_t* save_state(Snes* snes, int* size) {
  *size = snes_saveState(snes, NULL);
  uint8_t* data = malloc((size_t) *size);
  snes_saveState(snes, data);
  return data;
}

// The upload with port 0 left at 0 (no song): runs MenuStartMusic, then puts port 0 back
// to 0 and lets the driver settle. Same as start_music then "stop": used for the effects.
static Snes* upload_no_song(const char* romPath, int upload) {
  Snes* snes = boot_to_title(romPath);
  start_music(snes, upload);
  snes->apu->inPorts[0] = 0;  // as between $0F:8145 (STZ $2140) and $0F:8154
  return snes;
}

static int cmd_probe(const char* romPath) {
  for(int u = 0; u < NUM_UPLOADS; u++) {
    Snes* snes = upload_no_song(romPath, u);
    int size;
    uint8_t* state = save_state(snes, &size);
    printf("upload %d:", u);
    for(int p = 0; p <= 12; p++) {
      snes_loadState(snes, state, size);
      g_game.hijack = false;
      snes->apu->inPorts[0] = (uint8_t) p;
      Render r = {0};
      apu_render(snes->apu, &r, 8L * RATE);
      printf(" %d:%.0f", p, rms(r.data, r.frames));
      fflush(stdout);
      free(r.data);
    }
    printf("\n");
    free(state);
    snes_free(snes);
  }
  return 0;
}

// The driver runs whatever is on port 1 at each of its ticks and echoes it on port 1;
// RaceIrqTop ($00:B850) sends the next command only once the last one is echoed. This
// does the same: cmd goes on port 1, the APU runs until the echo, then port 1 gets $00
// (a command that does nothing), as the game would send its next command.
static void send_command(Apu* apu, Render* r, uint8_t cmd) {
  apu->inPorts[1] = cmd;
  for(int i = 0; i < RATE && apu->outPorts[1] != cmd; i++) apu_render(apu, r, 1);
  apu->inPorts[1] = 0;
}

// Renders each sound command, alone (after the race upload, no song playing).
static int cmd_sfx(const char* romPath, const char* outDir, int upload, int first, int last, int seconds) {
  Snes* snes = upload_no_song(romPath, upload);
  Render warm = {0};
  apu_render(snes->apu, &warm, RATE / 4);  // let the driver settle with no command
  free(warm.data);
  int size;
  uint8_t* state = save_state(snes, &size);
  for(int c = first; c <= last; c++) {
    snes_loadState(snes, state, size);
    Apu* apu = snes->apu;
    Render r = {0};
    send_command(apu, &r, (uint8_t) c);
    long latency = r.frames;  // samples until the driver took the command
    r.frames = 0;
    char npath[1024];
    snprintf(npath, sizeof(npath), "%s/sfx_%02X_notes.txt", outDir, c);
    r.notes = fopen(npath, "w");
    apu_render(apu, &r, (long) seconds * RATE);
    if(r.notes) fclose(r.notes);
    char path[1024];
    snprintf(path, sizeof(path), "%s/sfx_%02X.wav", outDir, c);
    write_wav(path, r.data, r.frames);
    snprintf(path, sizeof(path), "%s/sfx_%02X_srcn.txt", outDir, c);
    write_srcn(path, &r, apu);
    printf("cmd %02X: rms %.1f taken after %ld samples\n", c, rms(r.data, r.frames), latency);
    fflush(stdout);
    free(r.data);
  }
  free(state);
  snes_free(snes);
  return 0;
}

// The engine: the start command ($02), then every frame the RPM command ($03, ports 2/3
// = RPM + $400) alternating with another command, as RaceIrqTop does, with the RPM swept.
static int cmd_engine(const char* romPath, const char* outDir, int upload, int startCmd, int rpmCmd,
                      const char* name) {
  Snes* snes = upload_no_song(romPath, upload);
  Apu* apu = snes->apu;
  Render r = {0};
  apu_render(apu, &r, RATE / 4);
  r.frames = 0;
  apu->inPorts[2] = 0x00;
  apu->inPorts[3] = 0x04;  // idle: RPM 0
  send_command(apu, &r, (uint8_t) startCmd);
  r.frames = 0;
  char npath[1024];
  snprintf(npath, sizeof(npath), "%s/%s_notes.txt", outDir, name);
  r.notes = fopen(npath, "w");
  const int frameLen = RATE / 60;
  const int total = 60 * 16;  // 16 s: idle, up to the top RPM seen in races ($0E00 on the port), hold, down, idle
  uint8_t prev = 0;
  for(int i = 0; i < total; i++) {
    double t = i / 60.0;
    double rpm;
    if(t < 2) rpm = 0;
    else if(t < 8) rpm = (t - 2) / 6 * 0x0A00;
    else if(t < 10) rpm = 0x0A00;
    else if(t < 14) rpm = 0x0A00 * (1 - (t - 10) / 4);
    else rpm = 0;
    uint16_t v = (uint16_t) ((int) rpm + 0x400);
    long end = r.frames + frameLen;
    if(apu->outPorts[1] == prev) {  // the driver has taken the last command
      uint8_t cmd = prev == rpmCmd ? 0 : (uint8_t) rpmCmd;
      apu->inPorts[2] = (uint8_t) v;
      apu->inPorts[3] = (uint8_t) (v >> 8);
      apu->inPorts[1] = cmd;
      prev = cmd;
    }
    apu_render(apu, &r, end - r.frames);
  }
  if(r.notes) fclose(r.notes);
  char path[1024];
  snprintf(path, sizeof(path), "%s/%s.wav", outDir, name);
  write_wav(path, r.data, r.frames);
  snprintf(path, sizeof(path), "%s/%s_srcn.txt", outDir, name);
  write_srcn(path, &r, apu);
  printf("%s: %ld frames rms %.1f\n", name, r.frames, rms(r.data, r.frames));
  free(r.data);
  snes_free(snes);
  return 0;
}

// Appends the DSP samples made since *last (the ring holds 1024, a frame makes ~534).
static void harvest(Apu* apu, Render* r, uint16_t* last) {
  Dsp* dsp = apu->dsp;
  long n = (uint16_t) (dsp->sampleOffset - *last);
  if(r->frames + n > r->capacity) {
    r->capacity = (r->frames + n) * 2;
    r->data = realloc(r->data, (size_t) r->capacity * 2 * sizeof(int16_t));
  }
  while(*last != dsp->sampleOffset) {
    r->data[r->frames * 2] = dsp->sampleBuffer[(*last & 0x3ff) * 2];
    r->data[r->frames * 2 + 1] = dsp->sampleBuffer[(*last & 0x3ff) * 2 + 1];
    (*last)++;
    r->frames++;
  }
}

static int cmd_log(const char* romPath, const char* inputPath, long frames, const char* wavPath, long wavFrom) {
  Snes* snes = boot(romPath);
  Render audio = {0};
  uint16_t last = snes->apu->dsp->sampleOffset;
  TgInputScript script;
  bool haveScript = false;
  if(inputPath) {
    char err[256];
    if(!tg_input_script_load(inputPath, &script, err, sizeof(err))) {
      fprintf(stderr, "%s\n", err);
      return 1;
    }
    haveScript = true;
  }
  g_game.logWrites = true;
  for(long f = 0; f < frames; f++) {
    if(haveScript) {
      tg_input_script_advance(&script, (uint32_t) f);
      if(script.resetPending) {
        snes_reset(snes, false);
        script.resetPending = false;
      }
      for(int p = 1; p <= 2; p++) {
        for(int b = 0; b < 12; b++) snes_setButtonState(snes, p, b, (script.state[p] >> b) & 1);
      }
    }
    g_game.frame = f;
    snes_runFrame(snes);
    if(wavPath) {
      if(f < wavFrom) last = snes->apu->dsp->sampleOffset;
      else harvest(snes->apu, &audio, &last);
    }
  }
  if(wavPath) {
    write_wav(wavPath, audio.data, audio.frames);
    fprintf(stderr, "%s: %ld frames\n", wavPath, audio.frames);
  }
  free(audio.data);
  snes_free(snes);
  return 0;
}

static int cmd_render_spc(const char* spcPath, const char* wavPath, int seconds) {
  Apu* apu = load_spc(spcPath);
  if(apu == NULL) return 1;
  Render r = {0};
  apu_render(apu, &r, (long) seconds * RATE);
  write_wav(wavPath, r.data, r.frames);
  printf("%s: %ld frames rms %.1f\n", wavPath, r.frames, rms(r.data, r.frames));
  free(r.data);
  apu_free(apu);
  return 0;
}

int main(int argc, char** argv) {
  const char* romPath = NULL;
  const char* outDir = ".";
  const char* inputPath = NULL;
  int seconds = -1, upload = 1, first = 1, last = 0x3F, only = -1, keep = 0xFF;
  long frames = 3600, wavFrom = 0;
  const char* wavPath = NULL;
  const char* cmd = NULL;
  const char* args[4] = {0};
  int nargs = 0;
  for(int i = 1; i < argc; i++) {
    const char* next = i + 1 < argc ? argv[i + 1] : NULL;
    if(strcmp(argv[i], "--rom") == 0 && next) romPath = argv[++i];
    else if(strcmp(argv[i], "--out") == 0 && next) outDir = argv[++i];
    else if(strcmp(argv[i], "--input") == 0 && next) inputPath = argv[++i];
    else if(strcmp(argv[i], "--seconds") == 0 && next) seconds = atoi(argv[++i]);
    else if(strcmp(argv[i], "--upload") == 0 && next) upload = atoi(argv[++i]);
    else if(strcmp(argv[i], "--only") == 0 && next) only = atoi(argv[++i]);
    else if(strcmp(argv[i], "--keep") == 0 && next) keep = (int) strtol(argv[++i], NULL, 0) & 0xFF;
    else if(strcmp(argv[i], "--first") == 0 && next) first = (int) strtol(argv[++i], NULL, 0);
    else if(strcmp(argv[i], "--last") == 0 && next) last = (int) strtol(argv[++i], NULL, 0);
    else if(strcmp(argv[i], "--frames") == 0 && next) frames = atol(argv[++i]);
    else if(strcmp(argv[i], "--wav") == 0 && next) wavPath = argv[++i];
    else if(strcmp(argv[i], "--wav-from") == 0 && next) wavFrom = atol(argv[++i]);
    else if(cmd == NULL) cmd = argv[i];
    else if(nargs < 4) args[nargs++] = argv[i];
  }
  if(cmd == NULL) {
    fprintf(stderr, "usage: see the top of tools/audio/tg_audio.c or tools/audio/README.md\n");
    return 2;
  }
  if(strcmp(cmd, "render-spc") == 0) {
    if(nargs < 3) return 2;
    return cmd_render_spc(args[0], args[1], atoi(args[2]));
  }
  if(romPath == NULL) {
    fprintf(stderr, "--rom is required\n");
    return 2;
  }
  if(strcmp(cmd, "songs") == 0) return cmd_songs(romPath, outDir, seconds > 0 ? seconds : 420, only, keep);
  if(strcmp(cmd, "probe") == 0) return cmd_probe(romPath);
  if(strcmp(cmd, "sfx") == 0) return cmd_sfx(romPath, outDir, upload, first, last, seconds > 0 ? seconds : 4);
  if(strcmp(cmd, "engine") == 0) {
    int startCmd = first, rpmCmd = last;
    if(nargs >= 3) {
      startCmd = (int) strtol(args[0], NULL, 0);
      rpmCmd = (int) strtol(args[1], NULL, 0);
      return cmd_engine(romPath, outDir, upload, startCmd, rpmCmd, args[2]);
    }
    return cmd_engine(romPath, outDir, upload, 0x02, 0x03, "engine_p1");
  }
  if(strcmp(cmd, "log") == 0) return cmd_log(romPath, inputPath, frames, wavPath, wavFrom);
  fprintf(stderr, "unknown command %s\n", cmd);
  return 2;
}
