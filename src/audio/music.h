#ifndef TG_MUSIC_H
#define TG_MUSIC_H

// The modern music (docs/AUDIO.md): the player's remade songs, played in place of the
// sound chip's music. Presentation only: the game runs exactly as before.
//
// Song changes are seen where the game writes to the sound ports (tg_game_set_apu_observer):
// $18 on port 1 stops the music before every upload, port 0 then gets the song number
// (1-7) once the upload is done, $19 on port 1 pauses everything (the driver halts until
// port 1 changes). When a file for the chosen set exists, the sound chip's music voices
// are left out of the output (tg_game_set_voice_mute: 0-3 in races, keeping the engine and
// effects on 4-7; all eight for the title, Qualified and Ending, which have no effects)
// and the file is streamed instead, looping at its loop points; otherwise the original
// plays. Files stay in the player's folder: audio-modern/ next to the game, or, when the
// launcher could not write there, audio-modern/ in the per-user data folder (src/userdir.h).

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "game.h"

// The sets, in the options' order (settings.txt keeps the number).
typedef enum {
  TG_MUSIC_ORIGINAL,    // the sound chip's music, unchanged
  TG_MUSIC_FAITHFUL,    // variations/NN_song/NN_song__faithful (the default)
  TG_MUSIC_MODERN,      // races: the city's "D" (stages-race/); other songs faithful
  TG_MUSIC_CITY_BAND,   // races: the city's "C" (stages-band/); other songs faithful
  TG_MUSIC_LOCAL_BAND,  // races: the city's "B" (stages/ ..._B); other songs faithful
  TG_MUSIC_POSTCARD,    // races: the city's "A" (stages/ ..._A); other songs faithful
  TG_MUSIC_SYNTHWAVE,   // variations/NN_song/NN_song__synthwave
  TG_MUSIC_ORCHESTRAL,  // variations/NN_song/NN_song__orchestral
  TG_MUSIC_SET_COUNT
} TgMusicSet;

#define TG_MUSIC_DEFAULT TG_MUSIC_FAITHFUL

const char* tg_music_set_name(int set);  // for menus: "ORIGINAL", "FAITHFUL", "MODERN", ...
const char* tg_music_set_key(int set);   // for the command line: "original", "faithful", ...
int tg_music_set_from_key(const char* key);  // -1 if unknown

// The song a port-0 value starts ("01_title" ... "07_ending"; NULL outside 1-7).
const char* tg_music_song_name(int song);
// The course's city as the player's files name it ("usa", "las_vegas"), course 0-31.
void tg_music_course_city(int course, const char** country, const char** city);

typedef struct TgMusic TgMusic;

// dir: the folder with variations/, stages*/ (NULL or missing: the original plays).
// rate: the output's sample rate.
TgMusic* tg_music_create(const char* dir, int rate);
void tg_music_destroy(TgMusic* m);
// Watches game (NULL: stops watching). The game must outlive the watch.
void tg_music_attach(TgMusic* m, TgGame* game);
// The set; a song playing changes to the new set's file at once (a short crossfade).
void tg_music_set_set(TgMusic* m, int set);
int tg_music_get_set(const TgMusic* m);
// One line per song change (start, stop, pause, resume, the file chosen and why).
void tg_music_set_log(TgMusic* m, FILE* log);
// Without decoding: files are opened to check them and closed (tg_headless --music-log).
void tg_music_set_dry(TgMusic* m, bool dry);
// After each game frame (frame: the frames run so far): opens the files due.
void tg_music_end_frame(TgMusic* m, uint32_t frame);
// Adds the music to frames stereo frames of the game's samples (int16, interleaved).
void tg_music_mix(TgMusic* m, int16_t* samples, int frames);
// Whether the folder has the player's music (the faithful title, .ogg or .wav).
bool tg_music_available(const TgMusic* m);
// The same for a folder not opened yet (the game picks between audio-modern next to it and
// the one in the per-user data folder, src/userdir.h).
bool tg_music_dir_has_music(const char* dir);
// What plays now: the song number (0: none) and the file (NULL: the original).
int tg_music_song(const TgMusic* m);
const char* tg_music_file(const TgMusic* m);

#endif
