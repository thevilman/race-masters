// The launcher's "Build music" without the launcher: progress lines, the build's states,
// launcher.txt, the first-run offer, consent.txt (src/music_build.h), and the unzipping of
// the Windows Python (src/unzip.h).
//   test_music_build [path/to/tools/music/tgmusic/data/consent.txt]
//   test_music_build --unzip FILE.zip     lists what a zip unpacks to (name, size, CRC-32)

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "music_build.h"
#include "unzip.h"
#include "unzip_vector.h"  // made by tests/make_unzip_vector.py

static int failures, checks;

#define CHECK(cond)                                                   \
  do {                                                                \
    checks++;                                                         \
    if(!(cond)) {                                                     \
      failures++;                                                     \
      fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
    }                                                                 \
  } while(0)

#define CHECK_STR(a, b)                                                                     \
  do {                                                                                      \
    checks++;                                                                               \
    const char* a_ = (a);                                                                   \
    const char* b_ = (b);                                                                   \
    if(a_ == NULL || strcmp(a_, b_) != 0) {                                                  \
      failures++;                                                                           \
      fprintf(stderr, "%s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, a_ ? a_ : "(null)", b_); \
    }                                                                                       \
  } while(0)

static void test_parse_line(void) {
  TgMbLine l;
  CHECK(tg_mb_parse_line("{\"ev\":\"track\",\"i\":12,\"of\":149,\"label\":\"rio D (race (modern))\",\"status\":\"done\",\"secs\":150.25}", &l));
  CHECK_STR(tg_mb_str(&l, "ev"), "track");
  CHECK(tg_mb_num(&l, "i", -1) == 12);
  CHECK(tg_mb_num(&l, "of", -1) == 149);
  CHECK(fabs(tg_mb_num(&l, "secs", -1) - 150.25) < 1e-9);
  CHECK_STR(tg_mb_str(&l, "label"), "rio D (race (modern))");
  CHECK(tg_mb_str(&l, "missing") == NULL);
  CHECK(tg_mb_num(&l, "label", 7) == 7);  // a string is not a number
  // Escapes, booleans, null, spaces, negative and exponent numbers.
  CHECK(tg_mb_parse_line(" { \"ev\" : \"error\" , \"text\" : \"a \\\"b\\\"\\nc\\\\d \\u00e9\\u0041\", \"ok\": true, \"x\": null,"
                         " \"n\": -1.5e3, \"f\": false } ", &l));
  CHECK_STR(tg_mb_str(&l, "text"), "a \"b\"\nc\\d ?A");
  CHECK(tg_mb_num(&l, "ok", 0) == 1);
  CHECK(tg_mb_num(&l, "f", 9) == 0);
  CHECK(tg_mb_str(&l, "x") == NULL);
  CHECK(tg_mb_num(&l, "n", 0) == -1500);
  // UTF-8 in a string becomes '?' (the launcher's font is ASCII).
  CHECK(tg_mb_parse_line("{\"p\":\"C:\\\\Users\\\\Zo\xc3\xab\\\\x\"}", &l));
  CHECK_STR(tg_mb_str(&l, "p"), "C:\\Users\\Zo?\\x");
  CHECK(tg_mb_parse_line("{}", &l) && l.n == 0);
  // Not progress lines.
  CHECK(!tg_mb_parse_line("  [3/149] rio D: done in 150 s", &l));
  CHECK(!tg_mb_parse_line("{\"a\":{\"b\":1}}", &l));
  CHECK(!tg_mb_parse_line("{\"a\":[1,2]}", &l));
  CHECK(!tg_mb_parse_line("{\"a\":\"unterminated}", &l));
  CHECK(!tg_mb_parse_line("{\"a\" 1}", &l));
  CHECK(!tg_mb_parse_line("", &l));
  // More fields than kept: the rest is skipped, the line still parses.
  char big[2048] = "{";
  for(int i = 0; i < 40; i++) {
    char f[32];
    snprintf(f, sizeof(f), "%s\"k%d\":%d", i ? "," : "", i, i);
    strcat(big, f);
  }
  strcat(big, "}");
  CHECK(tg_mb_parse_line(big, &l));
  CHECK(l.n == 24);
  CHECK(tg_mb_num(&l, "k23", -1) == 23);
}

typedef struct {
  char lines[8][64];
  int n;
} Collected;

static void collect(const char* line, void* user) {
  Collected* c = user;
  if(c->n < 8) snprintf(c->lines[c->n++], sizeof(c->lines[0]), "%s", line);
}

static void test_lines(void) {
  TgMbLines s = {0};
  Collected c = {0};
  tg_mb_lines_push(&s, "{\"ev\":", 6, collect, &c);
  CHECK(c.n == 0);
  tg_mb_lines_push(&s, "\"plan\"}\r\nsecond\nthi", 19, collect, &c);
  CHECK(c.n == 2);
  CHECK_STR(c.lines[0], "{\"ev\":\"plan\"}");
  CHECK_STR(c.lines[1], "second");
  tg_mb_lines_push(&s, "rd\n", 3, collect, &c);
  CHECK(c.n == 3);
  CHECK_STR(c.lines[2], "third");
  // A line longer than the buffer is dropped, the next one is fine.
  char* longLine = malloc(10000);
  memset(longLine, 'x', 9999);
  longLine[9999] = '\n';
  tg_mb_lines_push(&s, longLine, 10000, collect, &c);
  tg_mb_lines_push(&s, "ok\n", 3, collect, &c);
  free(longLine);
  CHECK(c.n == 4);
  CHECK_STR(c.lines[3], "ok");
}

static void test_build_states(void) {
  TgMbState s;
  tg_mb_begin(&s, 20000);
  CHECK(s.phase == TG_MB_RUNNING);
  CHECK(s.step == TG_MB_STEP_TOOLS);
  CHECK(s.etaS == 20000);
  // setup_tools.py
  tg_mb_feed(&s, "{\"ev\":\"stage\",\"stage\":\"tools\",\"n\":1,\"of\":1,\"text\":\"Setting up the music tools\"}");
  tg_mb_feed(&s, "{\"ev\":\"download\",\"lib\":\"pip\",\"got\":500000,\"total\":1000000}");
  CHECK(s.fraction > 0.14 && s.fraction < 0.16);
  tg_mb_feed(&s, "{\"ev\":\"tools\",\"step\":\"packages\",\"i\":4,\"of\":5,\"pkg\":\"scipy\",\"n\":2,\"count\":21,"
                 "\"text\":\"Getting Python packages (2 of 21: scipy)\"}");
  CHECK_STR(s.detail, "Getting Python packages (2 of 21: scipy)");
  CHECK(s.fraction > 0.3 && s.fraction < 0.4);
  tg_mb_feed(&s, "$ python -m pip install ...");  // a plain line
  CHECK_STR(s.lastPlain, "$ python -m pip install ...");
  tg_mb_feed(&s, "{\"ev\":\"ready\",\"python\":\"/data/race-masters/music-tools/venv/bin/python\"}");
  CHECK(s.sawReady);
  CHECK_STR(s.python, "/data/race-masters/music-tools/venv/bin/python");
  CHECK(tg_mb_child_exited(&s, 0, false));  // on to generate.py
  CHECK(s.phase == TG_MB_RUNNING);
  // generate.py: the notes, the plan, the libraries, the tracks (tools/music/tgmusic/progress.py)
  CHECK(s.downloadMb == -1 && s.downloadMaxMb == -1 && !s.planned);
  tg_mb_feed(&s, "{\"ev\":\"plan\",\"tracks\":149,\"done\":10,\"todo\":139,\"download_mb\":1100,\"download_max_mb\":3168,"
                 "\"libs\":9,\"jobs\":2,\"eta_s\":24000}");
  CHECK(s.tracksTotal == 149 && s.tracksDone == 10);
  CHECK(s.etaS == 24000);
  CHECK(s.downloadMb == 1100 && s.downloadMaxMb == 3168 && !s.planned);
  tg_mb_feed(&s, "{\"ev\":\"stage\",\"stage\":\"notes\",\"n\":1,\"of\":4,\"text\":\"Reading the notes from your ROM\"}");
  CHECK(s.step == TG_MB_STEP_NOTES && s.fraction == 0);
  CHECK(tg_mb_step_number(s.step) == 2);
  tg_mb_feed(&s, "{\"ev\":\"notes\",\"elapsed_s\":330,\"eta_s\":19000}");
  CHECK(fabs(s.fraction - 0.5) < 1e-9);
  tg_mb_feed(&s, "{\"ev\":\"notes\",\"elapsed_s\":5000}");
  CHECK(s.fraction <= 0.97);
  tg_mb_feed(&s, "{\"ev\":\"stage\",\"stage\":\"plan\",\"n\":2,\"of\":4,\"text\":\"Working out what to download\"}");
  CHECK(s.step == TG_MB_STEP_PLAN && tg_mb_step_number(s.step) == 3 && s.fraction == 0);
  CHECK_STR(s.stepText, "Working out what to download");
  tg_mb_feed(&s, "{\"ev\":\"planning\",\"i\":40,\"of\":139,\"eta_s\":18900}");
  CHECK(fabs(s.fraction - 40.0 / 139) < 1e-9);
  CHECK_STR(s.detail, "What each instrument plays: 40 of 139 tracks");
  CHECK(s.etaS == 18900);
  tg_mb_feed(&s, "{\"ev\":\"stage\",\"stage\":\"libraries\",\"n\":3,\"of\":4,\"text\":\"Getting the instrument libraries\",\"eta_s\":18800}");
  CHECK(s.step == TG_MB_STEP_LIBRARIES);
  CHECK(tg_mb_step_number(s.step) == 4);
  CHECK_STR(s.stepText, "Getting the instrument libraries");
  // Planned: the exact download.
  tg_mb_feed(&s, "{\"ev\":\"plan\",\"tracks\":149,\"done\":10,\"todo\":139,\"download_mb\":1040,\"download_max_mb\":3168,"
                 "\"libs\":7,\"jobs\":2,\"eta_s\":18700,\"planned\":true}");
  CHECK(s.planned && s.downloadMb == 1040 && s.downloadMaxMb == 3168);
  CHECK_STR(s.detail, "1.0 GB to download: only the samples your tracks play");
  CHECK(s.tracksTotal == 149 && s.tracksDone == 10);
  tg_mb_feed(&s, "{\"ev\":\"download\",\"lib\":\"vcsl\",\"name\":\"VCSL\",\"i\":6,\"of\":7,\"got\":190000000,\"total\":323000000,"
                 "\"all_got\":626000000,\"all_total\":1040000000,\"rate\":9600000,\"eta_s\":17000}");
  CHECK(fabs(s.fraction - 626.0 / 1040) < 1e-6);
  CHECK_STR(s.detail, "626 MB of 1.0 GB, 9.6 MB/s - now VCSL (6 of 7)");
  CHECK(s.etaS == 17000);
  tg_mb_feed(&s, "{\"ev\":\"unpack\",\"lib\":\"surge-xt\",\"name\":\"Surge XT\",\"i\":7,\"of\":7}");
  CHECK_STR(s.detail, "Unpacking Surge XT (7 of 7)");
  tg_mb_feed(&s, "{\"ev\":\"stage\",\"stage\":\"render\",\"n\":4,\"of\":4,\"text\":\"Rendering 139 tracks\"}");
  CHECK(s.step == TG_MB_STEP_RENDER && tg_mb_step_number(s.step) == TG_MB_STEPS);
  CHECK(fabs(s.fraction - 10.0 / 149) < 1e-9);
  tg_mb_feed(&s, "{\"ev\":\"track\",\"i\":11,\"of\":149,\"label\":\"01_title faithful\",\"status\":\"done\",\"secs\":150,\"eta_s\":10000}");
  tg_mb_feed(&s, "{\"ev\":\"track\",\"i\":12,\"of\":149,\"label\":\"rio D\",\"status\":\"failed\",\"secs\":10}");
  CHECK(s.tracksDone == 12 && s.tracksFailed == 1);
  tg_mb_feed(&s, "{\"ev\":\"render\",\"done\":12,\"of\":149,\"running\":\"las_vegas D, rome C\",\"eta_s\":9900}");
  CHECK_STR(s.detail, "Now: las_vegas D, rome C");
  CHECK(s.etaS == 9900);
  tg_mb_feed(&s, "{\"ev\":\"done\",\"tracks\":149,\"failed\":0,\"mb\":640,\"minutes\":380,\"late_samples\":0}");
  CHECK(s.sawDone);
  CHECK(!tg_mb_child_exited(&s, 0, true));
  CHECK(s.phase == TG_MB_FINISHED);
  CHECK(s.etaS == 0);
  // The steps, in the order a build takes them.
  CHECK(tg_mb_step_number(TG_MB_STEP_TOOLS) == 1 && tg_mb_step_number(TG_MB_STEP_NOTES) == 2 &&
        tg_mb_step_number(TG_MB_STEP_PLAN) == 3 && tg_mb_step_number(TG_MB_STEP_LIBRARIES) == 4 &&
        tg_mb_step_number(TG_MB_STEP_RENDER) == 5 && TG_MB_STEPS == 5);
  // Resumed after a cancel: the notes and the plan are kept, so the plan's stage passes at once
  // (all of it done) and the exact download (what is left) comes straight away.
  tg_mb_begin(&s, -1);
  tg_mb_feed(&s, "{\"ev\":\"stage\",\"stage\":\"plan\",\"n\":2,\"of\":4,\"text\":\"Working out what to download\"}");
  tg_mb_feed(&s, "{\"ev\":\"planning\",\"i\":139,\"of\":139}");
  CHECK(s.fraction == 1);
  tg_mb_feed(&s, "{\"ev\":\"plan\",\"tracks\":149,\"done\":10,\"todo\":139,\"download_mb\":0,\"planned\":true}");
  CHECK(s.planned && s.downloadMb == 0);
  CHECK_STR(s.detail, "Nothing more to download");
}

static void test_cancel_and_errors(void) {
  TgMbState s;
  // Cancelled by the launcher.
  tg_mb_begin(&s, -1);
  tg_mb_cancel(&s);
  CHECK(s.phase == TG_MB_CANCELLING);
  tg_mb_feed(&s, "{\"ev\":\"cancelled\"}");
  CHECK(!tg_mb_child_exited(&s, 3, true));
  CHECK(s.phase == TG_MB_STOPPED);
  // Killed after the timeout (no "cancelled" line): still stopped, not failed.
  tg_mb_begin(&s, -1);
  tg_mb_cancel(&s);
  CHECK(!tg_mb_child_exited(&s, 1, false));
  CHECK(s.phase == TG_MB_STOPPED);
  // Stopped from elsewhere (end of input): exit code 3.
  tg_mb_begin(&s, -1);
  CHECK(!tg_mb_child_exited(&s, 3, true));
  CHECK(s.phase == TG_MB_STOPPED);
  // A reported error.
  tg_mb_begin(&s, -1);
  tg_mb_feed(&s, "{\"ev\":\"error\",\"code\":\"network\",\"text\":\"Couldn't download it\",\"log\":\"/x/build.log\"}");
  CHECK(!tg_mb_child_exited(&s, 1, true));
  CHECK(s.phase == TG_MB_FAILED);
  CHECK_STR(s.errorCode, "network");
  CHECK_STR(s.errorText, "Couldn't download it");
  CHECK_STR(s.logPath, "/x/build.log");
  // Windows: no exit codes; the last lines say it.
  tg_mb_begin(&s, -1);
  tg_mb_feed(&s, "{\"ev\":\"ready\",\"python\":\"C:\\\\x\\\\python.exe\"}");
  CHECK(tg_mb_child_exited(&s, TG_MB_EXIT_UNKNOWN, false));
  tg_mb_feed(&s, "{\"ev\":\"done\",\"tracks\":149}");
  CHECK(!tg_mb_child_exited(&s, TG_MB_EXIT_UNKNOWN, true));
  CHECK(s.phase == TG_MB_FINISHED);
  tg_mb_begin(&s, -1);  // ended without a word: failed, not finished
  CHECK(!tg_mb_child_exited(&s, TG_MB_EXIT_UNKNOWN, true));
  CHECK(s.phase == TG_MB_FAILED);
  CHECK_STR(s.errorText, "The music build stopped unexpectedly.");
  tg_mb_begin(&s, -1);  // setup_tools.py ended with 0 but never said "ready"
  CHECK(!tg_mb_child_exited(&s, 0, false));
  CHECK(s.phase == TG_MB_FAILED);
  // An error line then exit 0 still fails (setup_tools.py never does that; be safe).
  tg_mb_begin(&s, -1);
  tg_mb_feed(&s, "{\"ev\":\"error\",\"code\":\"vcredist\",\"text\":\"Visual C++\"}");
  CHECK(!tg_mb_child_exited(&s, 0, false));
  CHECK(s.phase == TG_MB_FAILED);
  // A crash: no error line; the last plain line is shown.
  tg_mb_begin(&s, -1);
  tg_mb_feed(&s, "Traceback (most recent call last):");
  tg_mb_feed(&s, "MemoryError");
  CHECK(!tg_mb_child_exited(&s, 1, true));
  CHECK(s.phase == TG_MB_FAILED);
  CHECK_STR(s.errorCode, "other");
  CHECK_STR(s.errorText, "The music build stopped unexpectedly (exit code 1): MemoryError");
  // Some tracks failed.
  tg_mb_begin(&s, -1);
  CHECK(!tg_mb_child_exited(&s, 4, true));
  CHECK_STR(s.errorCode, "tracks");
  // The launcher's own failure.
  tg_mb_begin(&s, -1);
  tg_mb_fail(&s, "hash", "The Python download is damaged.");
  CHECK(s.phase == TG_MB_FAILED);
  CHECK_STR(s.errorCode, "hash");
}

static void test_words(void) {
  char b[64];
  tg_mb_format_eta(-1, b, sizeof(b));
  CHECK_STR(b, "");
  tg_mb_format_eta(20, b, sizeof(b));
  CHECK_STR(b, "under a minute left");
  tg_mb_format_eta(12 * 60 + 10, b, sizeof(b));
  CHECK_STR(b, "about 12 min left");
  tg_mb_format_eta(3 * 3600 + 10 * 60 + 5, b, sizeof(b));
  CHECK_STR(b, "about 3 h 10 min left");
  tg_mb_format_size(1.9e9, b, sizeof(b));
  CHECK_STR(b, "1.9 GB");
  tg_mb_format_size(350e6, b, sizeof(b));
  CHECK_STR(b, "350 MB");
  tg_mb_format_hours(6.2 * 3600, b, sizeof(b));
  CHECK_STR(b, "about 6 hours");
  tg_mb_format_hours(50 * 60, b, sizeof(b));
  CHECK_STR(b, "about an hour");
  tg_mb_format_hours(20 * 60, b, sizeof(b));
  CHECK_STR(b, "about 20 minutes");
  // The whole build, all styles, one job: tools 3 min, 10.1 GB at 8 MB/s (21 min) + unpack
  // (7 min), notes 11 min, the plan 2.5 min, 149 tracks x 150 s (6.2 h): about 7 hours.
  double t = tg_mb_estimate_seconds(149, 1, 10101, true, 150, 660, 8);
  CHECK(t > 6.8 * 3600 && t < 7.0 * 3600);
  CHECK(tg_mb_estimate_seconds(149, 2, 10101, true, 150, 660, 8) < 4.2 * 3600);
  CHECK(tg_mb_estimate_seconds(149, 1, 1100, true, 150, 660, 8) < t - 1000);  // the usual download: less time
  CHECK(tg_mb_auto_jobs(8000, 8) == 4);
  CHECK(tg_mb_auto_jobs(16000, 12) == 6);  // 2 performance + 8 efficiency cores, 16 GB
  CHECK(tg_mb_auto_jobs(16000, 4) == 2);   // a 4-thread CPU: two at a time
  CHECK(tg_mb_auto_jobs(6500, 16) == 3);   // little memory: what it holds
  CHECK(tg_mb_auto_jobs(2000, 2) == 1);
  CHECK(tg_mb_auto_jobs(64000, 16) == 8);
}

static void test_styles(void) {
  char b[64];
  CHECK(tg_mb_style_tracks(TG_MB_STYLES_ALL) == TG_MB_TRACKS);
  CHECK(tg_mb_style_tracks(1u | 1u << 6) == 7 + 32);  // faithful + D (the generator's default)
  CHECK(tg_mb_style_tracks(1u << 6) == 32 + 1);      // D alone still makes the faithful title
  tg_mb_styles_arg(TG_MB_STYLES_ALL, b, sizeof(b));
  CHECK_STR(b, "all");
  tg_mb_styles_arg(1u | 1u << 6, b, sizeof(b));
  CHECK_STR(b, "faithful,D");
  tg_mb_styles_arg(1u << 1 | 1u << 3 | 1u << 5, b, sizeof(b));
  CHECK_STR(b, "synthwave,A,C");
}

static void test_prefs(void) {
  TgLauncherPrefs p;
  // An earlier launcher's file: just the ROM, with spaces in its path.
  tg_prefs_defaults(&p);
  tg_prefs_parse(&p, "rom /games/My Games/Top Gear (USA).sfc\n");
  CHECK_STR(p.rom, "/games/My Games/Top Gear (USA).sfc");
  CHECK(p.offer == TG_MB_OFFER_ASK);
  CHECK(p.styles == TG_MB_STYLES_ALL);
  CHECK(!p.cleanAfter);
  // Round trip; "rom" stays the first line (earlier launchers read only that).
  p.offer = TG_MB_OFFER_NEVER;
  p.styles = 1u | 1u << 6;
  p.cleanAfter = true;
  char text[4096];
  size_t n = tg_prefs_write(&p, text, sizeof(text));
  const char* first = "rom /games/My Games/Top Gear (USA).sfc\n";
  CHECK(n > 0 && strncmp(text, first, strlen(first)) == 0);
  TgLauncherPrefs q;
  tg_prefs_defaults(&q);
  tg_prefs_parse(&q, text);
  CHECK_STR(q.rom, p.rom);
  CHECK(q.offer == TG_MB_OFFER_NEVER && q.styles == p.styles && q.cleanAfter);
  // Windows line ends, unknown keys, bad values: ignored.
  tg_prefs_defaults(&q);
  tg_prefs_parse(&q, "rom C:\\Games\\tg.sfc\r\nmusic_offer 9\r\nmusic_styles 0\r\nsomething else\r\nmusic_clean 1\r\n");
  CHECK_STR(q.rom, "C:\\Games\\tg.sfc");
  CHECK(q.offer == TG_MB_OFFER_ASK);
  CHECK(q.styles == TG_MB_STYLES_ALL);
  CHECK(q.cleanAfter);
  tg_prefs_defaults(&q);
  tg_prefs_parse(&q, "");
  CHECK(q.rom[0] == 0);
}

static void test_offer(void) {
  // Once a session, with a verified ROM, the tools found, nothing built or building.
  CHECK(tg_mb_should_offer(true, true, 0, false, TG_MB_OFFER_ASK, false));
  CHECK(tg_mb_should_offer(true, true, 0, false, TG_MB_OFFER_LATER, false));  // "Later": next time
  CHECK(!tg_mb_should_offer(true, true, 0, false, TG_MB_OFFER_NEVER, false));
  CHECK(!tg_mb_should_offer(true, true, 0, false, TG_MB_OFFER_ASK, true));
  CHECK(!tg_mb_should_offer(false, true, 0, false, TG_MB_OFFER_ASK, false));
  CHECK(!tg_mb_should_offer(true, false, 0, false, TG_MB_OFFER_ASK, false));
  CHECK(!tg_mb_should_offer(true, true, 39, false, TG_MB_OFFER_ASK, false));
  CHECK(!tg_mb_should_offer(true, true, 0, true, TG_MB_OFFER_ASK, false));
}

static void test_consent(const char* path) {
  TgMbConsent c;
  const char* sample =
    "# comment\ntracks\t149\n"
    "lib\tsurge-xt\tSurge XT 1.3.4\tGPL-3.0-or-later\thttps://surge-synthesizer.github.io\t302\t350\n"
    "lib\tsso\tSonatina\tCC Sampling Plus 1.0\thttps://github.com/peastman/sso\t1380\t1380\n"
    "style\tfaithful\tsurge-xt,sso\nstyle\torchestral\tsso,nonexistent\nstyle\tpolka\tsso\n"
    "typical\tsso\tfaithful\t12\ntypical\tsso\torchestral\t130\ntypical\tsso\tpolka\t1\ntypical\tnope\tfaithful\t5\n"
    "typical\tsso\tA\tx\n"
    "tool\tPython and packages\tPSF-2.0, MIT\thttps://pypi.org\t145\t150\n"
    "disk\tlibs\t9000\r\ndisk\tpeak\t14000\ntime\ttrack_s\t150\n";
  CHECK(tg_mb_parse_consent(sample, &c));
  CHECK(c.tracks == 149 && c.nLibs == 2);
  CHECK_STR(c.libs[0].id, "surge-xt");
  CHECK_STR(c.libs[1].licence, "CC Sampling Plus 1.0");
  CHECK(c.libs[0].mbWindows == 302 && c.libs[0].mbLinux == 350);
  CHECK(c.libsMb == 9000 && c.peakMb == 14000 && c.trackS == 150);
  CHECK(c.styleLibs[0] == 3u && c.styleLibs[2] == 2u && c.styleLibs[1] == 0);
  CHECK(c.typical[1][0] == 12 && c.typical[1][2] == 130 && c.typical[1][3] == -1 && c.typical[0][0] == -1);
  CHECK(tg_mb_consent_libs(&c, 1u << 1) == 3u);       // synthwave alone: the faithful title's libraries
  CHECK(tg_mb_consent_libs(&c, 1u << 2) == 3u);
  CHECK(tg_mb_consent_download_mb(&c, 3u, true, true) == 302 + 1380 + 145);
  CHECK(tg_mb_consent_download_mb(&c, 1u, false, false) == 350);
  // Usually: of a library with typical sizes, the most one style takes (the faithful title's
  // counted); of one without, all of it (a plug-in, a SoundFont).
  CHECK(tg_mb_consent_typical_mb(&c, 1u << 2, false, true) == 302 + 130);
  CHECK(tg_mb_consent_typical_mb(&c, 1u << 1, true, false) == 350 + 12 + 150);
  CHECK(tg_mb_consent_typical_mb(&c, 1u, false, true) == 302 + 12);
  int kept, peak, music;
  tg_mb_consent_disk(&c, 1u << 2, true, &kept, &peak, &music);
  CHECK(kept < peak);
  CHECK(!tg_mb_parse_consent("# nothing\n", &c));
  if(path == NULL) return;
  // The real one, as the release ships it.
  FILE* f = fopen(path, "rb");
  CHECK(f != NULL);
  if(f == NULL) return;
  static char text[16384];
  size_t n = fread(text, 1, sizeof(text) - 1, f);
  fclose(f);
  text[n] = 0;
  CHECK(tg_mb_parse_consent(text, &c));
  CHECK(c.tracks == TG_MB_TRACKS);
  CHECK(c.nLibs == 9);
  unsigned all = tg_mb_consent_libs(&c, TG_MB_STYLES_ALL);
  CHECK(all == (1u << c.nLibs) - 1);  // every library is played by some style
  int win = tg_mb_consent_download_mb(&c, all, true, true), lin = tg_mb_consent_download_mb(&c, all, true, false);
  CHECK(win > 2800 && win < 4000);  // at most: every file tgmusic/data/samples.tsv lists, about 3.3 GB
  CHECK(lin > 2800 && lin < 4000);
  int usual = tg_mb_consent_typical_mb(&c, TG_MB_STYLES_ALL, true, true);
  CHECK(usual > 900 && usual < 1500);  // usually: the samples the notes play, about 1.2 GB
  CHECK(tg_mb_consent_typical_mb(&c, TG_MB_STYLES_ALL, true, false) > 900);
  CHECK(tg_mb_consent_typical_mb(&c, 1u, false, true) < 200);  // faithful alone: plug-ins and a few samples
  // The consent screen's list (each library's usual size) adds up to its total.
  for(unsigned styles = 1; styles <= TG_MB_STYLES_ALL; styles = styles * 2 + 1) {
    int sum = 0;
    for(int i = 0; i < c.nLibs; i++) sum += tg_mb_consent_lib_typical_mb(&c, i, styles, true);
    CHECK(sum == tg_mb_consent_typical_mb(&c, styles, false, true));
  }
  CHECK(tg_mb_consent_lib_typical_mb(&c, c.nLibs, TG_MB_STYLES_ALL, true) == 0);
  CHECK(c.libsMb > 0 && c.toolsMb > 0 && c.musicMb > 0 && c.peakMb > c.libsMb);
  tg_mb_consent_disk(&c, TG_MB_STYLES_ALL, true, &kept, &peak, &music);
  CHECK(kept < c.libsMb && kept > c.toolsMb + c.musicMb);
  CHECK(peak >= c.peakMb && music == c.musicMb);
  unsigned synth = tg_mb_consent_libs(&c, 1u << 1);  // synthwave: Surge XT, Dragonfly + the faithful title's
  CHECK(synth != all && (synth & ~c.styleLibs[0]) == 0);
  tg_mb_consent_disk(&c, 1u << 1, true, &kept, &peak, &music);
  CHECK(kept < c.libsMb && peak < c.peakMb && music < 100);
  for(int i = 0; i < c.nLibs; i++) CHECK(c.libs[i].name[0] && c.libs[i].licence[0] && c.libs[i].homepage[0]);
  // Every library that has typical sizes has them for every style that plays on it, never
  // more than all of it.
  for(int i = 0; i < c.nLibs; i++) {
    bool any = false;
    for(int k = 0; k < TG_MB_STYLE_COUNT; k++) any = any || c.typical[i][k] >= 0;
    for(int k = 0; any && k < TG_MB_STYLE_COUNT; k++) {
      if(c.styleLibs[k] & (1u << i)) CHECK(c.typical[i][k] >= 0 && c.typical[i][k] <= c.libs[i].mbWindows);
    }
  }
}

typedef struct {
  int n;
  bool ok[16];
  bool stopAfterOne;
} Unzipped;

static bool checkFile(const char* name, const uint8_t* data, size_t size, void* user) {
  Unzipped* u = user;
  for(size_t i = 0; i < sizeof(kZipFiles) / sizeof(kZipFiles[0]); i++)
    if(strcmp(kZipFiles[i].name, name) == 0) u->ok[i] = size == kZipFiles[i].size && tg_crc32(data, size) == kZipFiles[i].crc;
  u->n++;
  return !u->stopAfterOne;
}

static void test_unzip(void) {
  Unzipped u = {0};
  char err[200] = "";
  CHECK(tg_unzip(kZip, sizeof(kZip), checkFile, &u, err, sizeof(err)));
  CHECK(u.n == (int) (sizeof(kZipFiles) / sizeof(kZipFiles[0])));  // the folder is not a file
  for(size_t i = 0; i < sizeof(kZipFiles) / sizeof(kZipFiles[0]); i++) CHECK(u.ok[i]);
  CHECK(tg_crc32((const uint8_t*) "123456789", 9) == 0xCBF43926u);
  // A damaged byte in a file's data: refused (bad code or CRC).
  uint8_t* bad = malloc(sizeof(kZip));
  memcpy(bad, kZip, sizeof(kZip));
  bad[200] ^= 0x55;
  memset(&u, 0, sizeof(u));
  CHECK(!tg_unzip(bad, sizeof(kZip), checkFile, &u, err, sizeof(err)));
  CHECK(strstr(err, "damaged") != NULL);
  // Cut short, or not a zip.
  CHECK(!tg_unzip(kZip, sizeof(kZip) / 2, checkFile, &u, err, sizeof(err)));
  CHECK(!tg_unzip((const uint8_t*) "PK", 2, checkFile, &u, err, sizeof(err)));
  // A path that would leave the folder.
  memcpy(bad, kZip, sizeof(kZip));
  for(size_t i = 0; i + 11 <= sizeof(kZip); i++)  // the central directory's "a/hello.txt"
    if(memcmp(bad + i, "a/hello.txt", 11) == 0 && i > sizeof(kZip) / 2) memcpy(bad + i, "../hello.tx", 11);
  memset(&u, 0, sizeof(u));
  CHECK(!tg_unzip(bad, sizeof(kZip), checkFile, &u, err, sizeof(err)));
  CHECK(strstr(err, "unsafe") != NULL);
  free(bad);
  // The writer can stop it.
  memset(&u, 0, sizeof(u));
  u.stopAfterOne = true;
  CHECK(!tg_unzip(kZip, sizeof(kZip), checkFile, &u, err, sizeof(err)) && u.n == 1);
  // Raw DEFLATE: wrong expected size.
  uint8_t out[8];
  static const uint8_t fixedHi[] = {0xcb, 0x48, 0xcd, 0xc9, 0xc9, 0x07, 0x00};  // "hello", fixed Huffman
  CHECK(tg_inflate(fixedHi, sizeof(fixedHi), out, 5) && memcmp(out, "hello", 5) == 0);
  CHECK(!tg_inflate(fixedHi, sizeof(fixedHi), out, 4));
  CHECK(!tg_inflate(fixedHi, 3, out, 5));
}

static bool listFile(const char* name, const uint8_t* data, size_t size, void* user) {
  (void) user;
  printf("%10zu  %08x  %s\n", size, (unsigned) tg_crc32(data, size), name);
  return true;
}

static int unzipList(const char* path) {
  FILE* f = fopen(path, "rb");
  if(f == NULL) return 2;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t* zip = malloc((size_t) n);
  size_t got = fread(zip, 1, (size_t) n, f);
  fclose(f);
  char err[200] = "";
  bool ok = got == (size_t) n && tg_unzip(zip, got, listFile, NULL, err, sizeof(err));
  free(zip);
  if(!ok) fprintf(stderr, "%s\n", err);
  return ok ? 0 : 1;
}

int main(int argc, char** argv) {
  if(argc > 2 && strcmp(argv[1], "--unzip") == 0) return unzipList(argv[2]);
  test_unzip();
  test_parse_line();
  test_lines();
  test_build_states();
  test_cancel_and_errors();
  test_words();
  test_styles();
  test_prefs();
  test_offer();
  test_consent(argc > 1 ? argv[1] : NULL);
  printf("%d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}
