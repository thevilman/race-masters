// The race in real 3D: see view3d.h.

#include "view3d.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>  // _findfirst: the art folder's listing
#else
#include <dirent.h>
#endif

#include "brand.h"  // TG_APP_NAME
#include "port/port.h"  // tg_port_race_updates
#include "country_input.h"  // the countries' passwords, for the country screen
#include "font_atlas.h"
#include "logo_vector.h"

static char g_player_name[2][9];  // the players' names, as entered (for the HUD)
static char g_banner_name[2][9];  // the names the race's start banners show ($054E, $055E)
static int g_msg_player;          // whose message message_text reads

#define PI 3.14159265358979f

static uint16_t r16(const uint8_t* m, uint32_t a) { return (uint16_t) (m[a] | m[a + 1] << 8); }

// --- Pictures ---------------------------------------------------------------------------

// Fixed slots: a car's picture as the game shows it now, by the car's id; the same car
// seen straight from behind and close (for its model); a roadside object's picture by its
// art and side; the skyline panorama.
#define CAR_PIC 0
#define REAR_PIC 32
#define ART_PIC 64
#define ART_COUNT (256 + 16)  // by art and side; then the groups (the start banner...)
#define SKY_PIC (ART_PIC + ART_COUNT)
#define HUD_PIC (SKY_PIC + 1)
// The modern look's materials, made here: fine grain (ground), asphalt, chevron boards
// (pointing left, right), a warning board, a soft round glow.
#define TEX_GRAIN (HUD_PIC + 1)
#define TEX_ASPHALT (TEX_GRAIN + 1)
#define TEX_CHEVRON_L (TEX_ASPHALT + 1)
#define TEX_CHEVRON_R (TEX_CHEVRON_L + 1)
#define TEX_WARNING (TEX_CHEVRON_R + 1)
#define TEX_GLOW (TEX_WARNING + 1)
#define TEX_CLOUD (TEX_GLOW + 1)   // two soft clouds
#define TEX_CLOUD2 (TEX_CLOUD + 1)
#define TEX_ATLAS (TEX_CLOUD2 + 1)  // all the made textures and a white patch, in one
#define SKY_SMOOTH (TEX_ATLAS + 1)  // the skyline doubled with Scale2x, drawn filtered (modern)
#define SIDE_SRC (SKY_SMOOTH + 1)  // the four cars seen from the side (the car select screen)
#define SIDE_PIC (SIDE_SRC + 4)    // each car's side, in its own paint
#define SIDE_HI (SIDE_PIC + 32)    // each car's side, upscaled 4x (the model's flanks)
#define HUD_HI (SIDE_HI + 32)      // the HUD upscaled 4x (smooth fonts)
#define REAR_HI (HUD_HI + 1)      // each car's rear picture, upscaled 4x (the model's tail)
#define FRONT_PIC (REAR_HI + 32)   // each car's front, made in its paint (the model's nose)
#define LIVERY_PIC (FRONT_PIC + 32) // each car's body texture: paint, seams, stripes, its number
#define FONT_PIC (LIVERY_PIC + 32)  // the HUD's font (Lato), all faces in one
#define MENU_PIC (FONT_PIC + 1)     // a menu's pictures and sprites (all but the cursor), 256x224
#define MENU_HI (MENU_PIC + 1)      // the same upscaled
#define MENU_BG (MENU_HI + 1)       // the menu's photograph (BG2), softened
#define CAR_TEX (MENU_BG + 1)       // the five car models' textures (tgcar version 3), mipmapped: with
                                    // white decals, then with black ones (CarMesh.texDark)
#define PIC_COUNT (CAR_TEX + 10)
#define HUD_H 104       // a view's HUD in BG1: the top view's lines 0-103, the bottom's from 112
#define HUD_ROWS 216    // both views' HUD lines
#define HUD_VIEW2 112

#define CAR_WIDTH 1.9f  // metres, for every car
#define DRAW_SEGS 320   // segments drawn ahead (800 m)
#define SKY_W 512       // the skyline is a BG2 panorama 512 pixels round ($B0 scrolls it)
#define SKY_H 48        // lines of it kept above the horizon
#define VERGE 12.0f     // ground bands each side, in road half widths (narrower than the
                        // tightest bend, so a near band never lies over road further on)

typedef struct {
  float sum_h;    // its height in metres (picture lines x depth x k), summed over sightings
  int n;
  int area;       // pixels in the kept picture: a bigger one replaces it
} ArtInfo;  // kept in the course cache as it is: don't change

typedef struct {
  float mpp;       // metres per pixel of the picture in CAR_PIC (its size in the world)
  float rear_mpp;  // the same for REAR_PIC
  int rear_area;
  float rear_aspect;
  uint32_t colour; // its main colour
} CarInfo;  // kept in the cars cache as it is: don't change

#define GROUND_STRETCH 16  // segments to a stretch of ground colour (40 m)
#define GROUND_BUCKETS (TG_TRACK3D_MAX_SEGMENTS / GROUND_STRETCH)
#define GROUND_MAGIC 0x444E5247u  // "GRND": the stretches, after a course file's pictures

// A car's shape for the 3D model, from its side view on the select screen: its outline
// along the length (heights in metres at stations from nose to tail), the belt line, the
// wheels, a rear wing if it has one.
#define CAR_STATIONS 26
typedef struct {
  uint32_t version;              // the side view it was made from (+1; 0 not made)
  float top[CAR_STATIONS];       // the body's top (the roof over the cabin), metres
  float belt;                    // where the glasshouse starts, metres
  float wheel_z[2];              // front and rear axles, metres from the middle (+ ahead)
  float wing_top, wing_z0, wing_z1;  // the rear wing (wing_top 0: none)
  float yscale;                  // metres per pixel up the side view
} CarShape;

// A car's pit stop as the modern look draws it (view3d_pits.inc): the game holds the car's place
// on the course for the stop and shows its own pit scene; here the car drives into its box on a
// real pit lane, stands while the crew works and the game refuels it, and drives out, then is
// eased back onto the game's place. Stepped once a game frame from the game's state; drawn as an
// offset from the game's place (so it starts and ends exactly on it).
enum { PIT_OFF, PIT_IN, PIT_BOX, PIT_OUT, PIT_JOIN };
typedef struct {
  int phase, id, box;
  uint16_t st;              // the game's pit state ($1E86 / $1E88) this frame
  int t, t_box, t_go;       // game frames: since the stop began, stopped in the box, since GO
  int t_fill;               // frames the game's fuel has been going up in this stop
  int t_in;                 // frames the drive in lasted (let go before the box: how far out the crew got)
  bool go;                 // sent off (the lollipop up, GO on the HUD)
  bool full;                // the game has refuelled it to full / let it go ($FFF9 on)
  double s;                 // the drawn car's back: metres along from the pit sign's segment
  float vel, acc, x;        // its speed (m/s), acceleration, and place across (m out on the pits' side)
  float s0, x0;             // where the drive in started (driving out: where it moved off)
  float jdel;               // joining: what is left of the along offset (m) once close
  bool jlock;               // ... close and as fast: the rest let go
  int t_join;               // frames since the game gave the car back
  float vg, ag;             // the game's car's speed as it moves off again (m/s, from its speed word), its change
  double ref_s;             // the game's car as drawn (metres from the sign), its speed, across
  float ref_v, ref_x;
  bool ref_ok;
  float off[2][2];          // the drawn car's offset from the game's: [last frame, this frame][along
                            // in track units, across in side units]
  float yaw[2], lift[2];    // its heading off the road's (radians), the jacks' lift (m)
  float cam[2];             // the camera's pit framing, 0-1 (a spring: value, rate)
  float fuel_start;         // fuel (%) when the stop began
  int stops;                // stops this race (the crew's walk back is kept between them)
} PitCar;

// Colours seen over time: the commonest win, so a sprite passing doesn't change them.
#define HIST 24
typedef struct {
  uint32_t c[HIST];
  float n[HIST];
} Hist;

struct TgView3D {
  TgTrack3D track;
  TgScene prev, cur;
  bool have, in_race;
  uint32_t updates;
  int since, period;   // frames since the last race update, frames between the last two
  uint16_t sky_scroll; // the game's skyline scroll ($B0), 0-511
  // Each view's sky position, the game's own ($1E96 / $1E98: panorama pixels x 128; the BG2
  // scroll is its top 9 bits), unwrapped (it only adds up), and its rate a frame ($1E9A / $1E9C).
  double sky_acc[2];
  float sky_rate[2];
  uint16_t sky_raw[2];
  bool sky_have;
  double head_acc[2];  // each view's camera heading (radians), unwrapped, as last drawn
  float head_raw[2];
  bool head_have[2];
  uint16_t cam_prev[2], cam_cur[2];  // each view's camera ($46, $59: track positions), last two updates
  float cam_side[2];   // each view's camera side position, eased
  bool two;            // a 2-player race ($1F04): two views
  bool demo;           // the attract demo race ($1FF6 = $FFFF): two views too, as the original's split
  float subframe;      // how far the display is into the next frame (0-1), for smooth motion
  TgV3Pic pics[PIC_COUNT];
  ArtInfo art[ART_COUNT];
  CarInfo car[32];
  float k;             // metres per (picture pixel x projection depth), from the player's car
  int player_min_w;    // the narrowest picture of the player's car (straight ahead)...
  int player_max_w;    // ...of those at least 3/4 as wide as its widest (not cut off)
  int horizon, last_horizon;  // the original's horizon line, 0-223
  uint32_t* sky_px;    // the panorama, SKY_W x SKY_H, built up as the game shows it
  uint32_t* sky_seen;  // the last colour seen at each panorama pixel: taken when seen twice
  Hist sky_top, sky_low, ground, road;
  uint32_t art_colour[ART_COUNT];
  float art_side[ART_COUNT];         // groups: where across the road (side units), summed
  int art_side_n[ART_COUNT];
  uint32_t car_paint[32];            // the modern look's body colours (0: not yet)
  float car_adv[32];                 // track units each car moved in the last update
  int car_brake[32];                 // updates its brake lights stay on
  float car_yaw_to[32], car_yaw[32];
  uint16_t pspeed[2];                // the players' speed words ($1E5A, $1E5C), this frame
  float car_adv_ms[32];              // the rivals' speed from their motion (m/s), smoothed
  float car_ms[32], car_accel[32];   // each car's speed (m/s) and its change (m/s^2), smoothed
  // Each car's body on its springs (the modern look's suspension, visual only): roll (+ leans
  // right), pitch (+ nose down), heave (m up), the front wheels' steer (+ right); [0] now, [1]
  // its rate. Stepped once a game frame in tg_view3d_capture; prev for the display's in-between.
  struct CarBody { float roll[2], pitch[2], heave[2], steer[2]; } body[32], body_prev[32];
  // Drawn-only room between cars (step_separation, sep_room), never the game's positions: each
  // car's offset along the track (track units, [0]) and across (side units, [1]) that the
  // constraints asked for at the last game frame; what is left of a jump in it, eased out
  // (value, rate), and of a change too fast to show at once; the offset as drawn at the last
  // game frame; a turn into a sideways move
  // (radians); the side each pair of rivals keeps apart on and which of a held pair is ahead
  // (0: none yet).
  float sep_tgt[32][2], sep_ease[32][2][2], sep_slew[32][2], sep_last[32][2], sep_yaw[32];
  int8_t sep_dir[32][32];
  // A knock between a player's car and a rival ($1EF5 / $1EF6 bit 7 rising with a rival in
  // reach): the rival's id + 1 (0: none), the game frame it came, the bit last frame.
  int knock_id[2];
  uint32_t knock_at[2];
  bool knock_bit[2];
  float night_lum;                    // the sky's brightness, smoothed (-1: a new course)
  bool night;                        // this course is lit as night (decided with hysteresis)
  uint32_t sky_show[2];              // the sky's colours as shown, eased towards the learned ones (0: none yet)
  int ticks;                         // pictures drawn (flames flicker by it)  // the modern cars' heading off the road's (radians): aim, eased
  bool nitro[2];                     // players 1 and 2's nitro burning ($1E6C/$1E6E bit 15)
  uint16_t rpm[2];                   // their revs ($1E5E/$1E60)
  int course;                        // $1F06: country (bits 2-4) and race (bits 0-1)
  int bright;                        // the game's screen brightness (INIDISP's, from $52): 0-15
  int lit_frames;                    // frames since it last was under 15 (capped)
  uint32_t frames;                   // game frames seen (tg_view3d_capture calls)
  uint32_t eased_at;                 // ... when the shown colours and fades last moved
  float ease_n;                      // game frames they move by in this picture
  int portraits_wait;                // frames to the next look for the car select screen
  int select_up;                     // frames the car select screen counts as up (0: not)
  int title_up, title_frames;        // the title screen: frames it counts as up; frames since it came
  int splash_up, splash_frames;      // the publisher's splash before it
  // The classic title (tg_view3d_set_title_style): the original's logo as it is on the screen
  // this frame, read off its picture (never kept): each pixel's part (TL_FACE, TL_RIM, TL_HI)
  // and the face's colour smoothed (the dither averaged out); whether the original shows its
  // PRESS START and licence lines now, and since when the licence lines are up.
  int title_style;
  uint8_t tl_cls[224][256];
  uint32_t tl_col[224][256];
  uint32_t tl_rim;
  LvMesh lv_logo, lv_badge, lv_kemco, lv_word;  // the logos as vector shapes (logo_vector.c)
  LvSplash lv_splash;                  // the publisher's splash as it is on the screen
  int tl_n;
  bool tl_press, tl_lic;
  int tl_lic_frames;
  // The original's credit lines under its logo, as its picture shows them (white pixels, read
  // off the screen while they are up, never kept beyond the title): the title draws them as
  // they are, so the notices are the game's own, from the player's ROM.
  uint8_t tl_credit[48][256];
  int tl_credit_box[4];              // x0, y0, x1, y1 (inclusive) in tl_credit; x1 <= x0: none yet
  int menu_up;                       // a menu screen (its text read off BG1) counts as up
  bool menu_to_race;                 // the last menu leads into a race (PLAY GAME, NEXT RACE)
  int loading_up, loading_frames;    // between that menu and the race: a loading card (frames)
  int menu_n, menu_sel;              // its lines, the one under the cursor (-1 none)
  struct { char text[33]; int col, row, big; } menu_item[48];
  int menu_box[4];                   // its frame: columns and rows, -1 none
  bool menu_pics;                    // it has pictures (MENU_PIC)
  bool menu_has_bg;                  // it has a photograph behind (MENU_BG)
  uint32_t menu_bg_key;              // which photograph (a hash)
  bool menu_bg_hi;                   // MENU_BG is an upscaled replacement from the art folder
  uint32_t menu_bg_seen[8];          // photographs seen, for --export-art
  uint32_t menu_bg_px[8][256 * 224];
  double menu_bg_light[8];
  int menu_cur[4];                   // the cursor's box when it is on a picture (-1: none)
  int menu_course;                   // $1F06 while in the menus (the course being previewed)
  uint16_t menu_record;              // its lap record ($1FB2 + course * 2, frames)
  bool menu_kph;                     // $1FB0
  // The country screen (screen 7) and the password (8): the game's own cursor and state.
  int menu_screen;                   // $46 while the menus' NMI trampoline is in place, else -1
  int menu_cursor;                   // $5A
  int menu_skill;                    // $1F10
  int menu_open;                     // $1F0A[skill]: the highest country open
  int menu_pw_country;               // $5C: the country whose password is asked
  bool country_free;                 // the frontend types the passwords (country_input.h)
  bool country_busy;                 // ... and is doing so now
  float ctry_hx, ctry_hy, ctry_t;    // the eased highlight on the country cards
  int menu_pbox[4];                  // the pictures' box (pixels of the game's screen)
  float menu_cx, menu_cy;            // the cursor's centre (game pixels; -1 none)
  bool pit[2];                       // a player is in the pit stop (RaceP1PitState $1E86 / $1E88 negative)
  PitCar pitc[2];                    // the view cars' pit stops as drawn (player 1; player 2 or the rival)
  float pit_sub;                     // how far the drawn pit offsets run on past the last game frame (0-1)
  int bright2;                       // the bottom view's brightness ($65), as bright
  uint16_t fuel_word[2];             // the views' cars' fuel ($1E62 / $1E64; full $A7FF), for the pit panel
  int pit_hold[2];                   // frames a view's fades are left out (its car's pit stop, and just after)
  int menu_scroll;                   // BG1's vertical scroll in pixels (the results scroll to the standings)
  uint16_t menu_tiles;               // BG1's tile base (the menus' font: $1000)
  // The results and standings as the game keeps them (read from WRAM, not off the screen):
  // finishing order $1F12, times $1F26 + 2*driver (frames), championship points $1F4E,
  // championship order $1F62; names of the 20 drivers (the players' or $0D:C47B).
  struct {
    uint8_t order[10], rank[20], pts[20];
    uint16_t time[20];
    char name[20][9];
    bool two;
  } res;
  // The championship's end screen (draw_champion): the frames it has been up (its animation's
  // clock), and the country's standings with their points as last seen (the game clears the
  // points, MenuAdvanceCourse, before that screen comes up).
  int champ_frames;
  uint8_t champ_rank[20], champ_pts[20];
  int champ_course;
  int select_cursor;                 // the car under its cursor (0-3)
  char car_stat[4][4][12];           // each car's stats as the screen shows them (speed, 0-100, grip, fuel)
  CarShape shape[5];                 // the four cars' shapes from their side views; [4] a default
  TgV3Hud hudv[2];                   // each player's HUD values (the modern look draws its own HUD)
  float sky_prof[SKY_W];             // the skyline's height per panorama column (pixels; 0 none)
  uint32_t sky_ctop[SKY_W], sky_cbot[SKY_W];  // its colour high up and low down, per column
  uint8_t sky_fade[SKY_W];           // how far each column has faded in since it was learned
  float sky_hshow[SKY_W];            // each column's outline height as drawn, eased to the learned one (QA-009)
  uint32_t sky_cshow[SKY_W][2];      // and its colours (top, foot) as drawn, eased likewise
  float sky_tw[SKY_W];               // and how much it shows a tower's windows (0..1), eased likewise
  uint8_t landmark_fade;             // the country's landmark, likewise
  uint32_t sky_prof_ver;             // the SKY_PIC version it was made from (+1)
  uint8_t sky_known[SKY_W];          // the game has shown this panorama column (skyline or bare sky)
  float ridge[SKY_W];                // the far ridge's height per column as drawn, eased
  bool ridge_set;                    // ridge[] holds this course's (else it is set at once)
  bool colours_seen;                 // this course's colours are known (learned, or from its cache)
  uint8_t art_fade[ART_COUNT];       // how far each roadside picture's objects have faded in since it was learned
  uint16_t msg_id[2], msg_delay[2], msg_blink[2], msg_flags[2], msg_age_id[2];
  uint8_t msg_age[2];  // frames each player's message has been showing  // each player's race message ($0118, $012A: messages.c)
  uint8_t ptype[2];                  // the players' cars (0-3: which side view) ($1F8F, $1FAC)
  uint32_t side_made[32];            // what each car's side picture was made from (paint, version)
  Hist seg_ground[GROUND_BUCKETS];   // the ground's colours stretch by stretch of the course
  uint32_t ground_now;               // the open ground's colour where the player is, eased
  uint32_t ground_show[2];           // the course's two ground colours as drawn, eased (0: none yet)
  uint32_t seg_show[GROUND_BUCKETS][2];  // each stretch's two ground colours as drawn, eased
  uint32_t seg_pick[GROUND_BUCKETS][2];  // ... and as last picked from what was learned
  uint32_t ground_pick[2];
  uint32_t car_paint_version[32];
  // The rivals' paint, the game's own: each car's record names its picture set (its kind,
  // bits 13-15 the sprite palette it is drawn with: TgSceneCar.kind), and the palette's body
  // ramp in CGRAM gives the colour. Taken once per race (when the palette first holds colours,
  // let go when it goes black between races), never from the learned pictures, whose mix of
  // shades changes with their size and angle (player: "cars are still blinking and changing
  // colours").
  uint32_t pal_paint[8];
  uint8_t pal_locked;  // bit p: pal_paint[p] is this race's
  // Each rival's place on the grid among the cars of its palette (front first), taken at the
  // race's first frame: which of its colour's vivid paints it wears (vivid_paint).
  uint8_t vivid_rank[32];
  uint8_t art_kinds[ART_COUNT];      // the modern look's model for each roadside picture (+1; 0 unknown)
  uint32_t art_kind_version[ART_COUNT];
  char cache_dir[1024];  // where what was learned is kept between sessions ("" for nowhere)
  char art_dir[1024];    // replacement pictures ("" for none)
  int frames_to_save;
  uint32_t hud_hi_of;    // the HUD_PIC version HUD_HI was made from (+1)
  uint32_t saved_key;    // the course kept when its race ended (no need to keep it again)
  // The chase camera's smoothed parts, per view (tg_view3d_draw_view): the eye's height over
  // the car and the framing pitch, each a critically damped spring [value, rate], stepped by
  // the game time (frames + subframe) since the last drawn picture.
  float rig_h[2][2], rig_p[2][2];
  double rig_t[2];
  bool rig_on[2];
};

static void make_textures(TgView3D* v);
// TG_CAR_TRACE=1: each car drawn (CT: frame, id, paint, depth, palette, level, brake) and each
// rear picture learned (CR), on stderr, for checking that cars keep their colour and level.
static bool car_trace(void) {
  static int on = -1;
  if(on < 0) on = getenv("TG_CAR_TRACE") != NULL;
  return on;
}
static int g_viewIdx;  // the view being drawn (0, 1): per-view state such as the cars' levels
static uint32_t paint_colour(const TgV3Pic* p);
static void preload_car_meshes(TgView3D* v);

TgView3D* tg_view3d_create(void) {
  TgView3D* v = calloc(1, sizeof(TgView3D));
  if(v == NULL) return NULL;
  v->period = 3;
  v->horizon = v->last_horizon = 112;
  v->night_lum = -1;
  v->sky_px = calloc(SKY_W * SKY_H, 4);
  v->sky_seen = calloc(SKY_W * SKY_H, 4);
  make_textures(v);
  return v;
}

static void save_course(const TgView3D* v, uint32_t key);
static void save_cars(const TgView3D* v);

void tg_view3d_destroy(TgView3D* v) {
  if(v == NULL) return;
  if(v->track.key) save_course(v, v->track.key);
  save_cars(v);
  for(int i = 0; i < PIC_COUNT; i++) free(v->pics[i].px);
  free(v->sky_px);
  free(v->sky_seen);
  free(v);
}

int tg_view3d_pic_count(const TgView3D* v) { (void) v; return PIC_COUNT; }
bool tg_view3d_pic_warm(const TgView3D* v, int i) {
  (void) v;
  return (i >= ART_PIC && i < ART_PIC + ART_COUNT) || (i >= SKY_PIC && i <= SKY_SMOOTH && i != HUD_PIC) || i == FONT_PIC ||
         (i >= CAR_TEX && i < CAR_TEX + 10);
}
bool tg_view3d_loading_up(const TgView3D* v) { return v->loading_up > 0; }
const TgV3Pic* tg_view3d_pic(const TgView3D* v, int i) { return i >= 0 && i < PIC_COUNT ? &v->pics[i] : NULL; }

static void set_pic(TgV3Pic* p, int w, int h, const uint32_t* px, int stride) {
  if(p->replaced) return;  // the art folder's picture stays
  if(p->w == w && p->h == h && p->px) {
    // Only the rows that changed are copied, and said (a renderer sends only those).
    int y0 = -1, y1 = -1;
    for(int y = 0; y < h; y++)
      if(memcmp(&p->px[y * w], &px[y * stride], (size_t) w * 4) != 0) {
        if(y0 < 0) y0 = y;
        y1 = y + 1;
      }
    if(y0 < 0) return;
    for(int y = y0; y < y1; y++) memcpy(&p->px[y * w], &px[y * stride], (size_t) w * 4);
    p->dirty_from = p->version;
    p->dirty_y0 = y0, p->dirty_y1 = y1;
    p->version++;
    return;
  } else {
    free(p->px);
    p->px = malloc((size_t) w * h * 4);
    if(p->px == NULL) {
      p->w = p->h = 0;
      return;
    }
    p->w = w;
    p->h = h;
  }
  for(int y = 0; y < h; y++) memcpy(&p->px[y * w], &px[y * stride], (size_t) w * 4);
  p->dirty_from = p->version;
  p->dirty_y0 = 0, p->dirty_y1 = h;
  p->version++;
}

static uint32_t snes_colour(uint16_t c) {
  uint32_t r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
  return 0xFF000000u | (r << 3 | r >> 2) << 16 | (g << 3 | g >> 2) << 8 | (b << 3 | b >> 2);
}

static void hist_add(Hist* h, uint32_t c, float w) {
  int slot = -1, low = 0;
  for(int i = 0; i < HIST; i++) {
    if(h->n[i] > 0 && h->c[i] == c) slot = i;
    if(h->n[i] < h->n[low]) low = i;
  }
  if(slot < 0) {
    slot = low;
    h->c[slot] = c;
    h->n[slot] = 0;
  }
  h->n[slot] += w;
}

static void hist_decay(Hist* h, float k) {
  for(int i = 0; i < HIST; i++) h->n[i] *= k;
}

// The commonest colour (rank 0) or the next (rank 1); fallback if none yet.
static uint32_t hist_top(const Hist* h, int rank, uint32_t fallback) {
  int a = -1, b = -1;
  for(int i = 0; i < HIST; i++) {
    if(h->n[i] <= 0) continue;
    if(a < 0 || h->n[i] > h->n[a]) {
      b = a;
      a = i;
    } else if(b < 0 || h->n[i] > h->n[b]) {
      b = i;
    }
  }
  if(rank == 1 && b >= 0 && h->n[b] > h->n[a] * 0.15f) return h->c[b];
  return a >= 0 ? h->c[a] : fallback;
}

// All that was seen, summed.
static float hist_weight(const Hist* h) {
  float n = 0;
  for(int i = 0; i < HIST; i++) n += h->n[i];
  return n;
}

// --- The modern look's materials ---------------------------------------------------------

static uint32_t hash32(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352d;
  x ^= x >> 15;
  x *= 0x846ca68b;
  x ^= x >> 16;
  return x;
}

// Smooth value noise that repeats every `period` pixels, 0-1.
static float noise2(int x, int y, int period, uint32_t seed) {
  int cell = period / 8;
  int x0 = x / cell, y0 = y / cell;
  float fx = (float) (x % cell) / cell, fy = (float) (y % cell) / cell;
  int n = period / cell;
  float c[4];
  for(int k = 0; k < 4; k++) {
    int cx = (x0 + (k & 1)) % n, cy = (y0 + (k >> 1)) % n;
    c[k] = (hash32((uint32_t) (cx * 73856093) ^ (uint32_t) (cy * 19349663) ^ seed) & 0xFFFF) / 65535.0f;
  }
  fx = fx * fx * (3 - 2 * fx);
  fy = fy * fy * (3 - 2 * fy);
  return (c[0] + (c[1] - c[0]) * fx) + ((c[2] + (c[3] - c[2]) * fx) - (c[0] + (c[1] - c[0]) * fx)) * fy;
}

static uint32_t grey(float k) {
  int g = (int) (k * 255 + 0.5f);
  g = g < 0 ? 0 : g > 255 ? 255 : g;
  return 0xFF000000u | (uint32_t) g << 16 | (uint32_t) g << 8 | (uint32_t) g;
}

// The made textures in one picture, so the modern look's road and models (and plain colours,
// on its white patch) go to the screen in few batches.
#define ATLAS_W 512
#define ATLAS_H 256
static const struct { int pic, x, y; } kAtlas[] = {
  {TEX_GRAIN, 0, 0}, {TEX_ASPHALT, 128, 0}, {TEX_CHEVRON_L, 0, 128}, {TEX_CHEVRON_R, 128, 128},
  {TEX_WARNING, 0, 192}, {TEX_GLOW, 64, 192}, {TEX_CLOUD, 256, 0}, {TEX_CLOUD2, 256, 64},
};
#define ATLAS_WHITE_X 200  // a white patch at (200-215, 200-215)
static bool g_atlas;
static int g_atlas_w[PIC_COUNT], g_atlas_h[PIC_COUNT];

static void make_atlas(TgView3D* v) {
  static uint32_t px[ATLAS_W * ATLAS_H];
  memset(px, 0, sizeof(px));
  for(size_t i = 0; i < sizeof(kAtlas) / sizeof(kAtlas[0]); i++) {
    const TgV3Pic* p = &v->pics[kAtlas[i].pic];
    if(!p->px) return;
    for(int y = 0; y < p->h; y++) memcpy(&px[(kAtlas[i].y + y) * ATLAS_W + kAtlas[i].x], &p->px[y * p->w], (size_t) p->w * 4);
    g_atlas_w[kAtlas[i].pic] = p->w;
    g_atlas_h[kAtlas[i].pic] = p->h;
  }
  for(int y = 0; y < 16; y++)
    for(int x = 0; x < 16; x++) px[(ATLAS_WHITE_X + y) * ATLAS_W + ATLAS_WHITE_X + x] = 0xFFFFFFFF;
  set_pic(&v->pics[TEX_ATLAS], ATLAS_W, ATLAS_H, px, ATLAS_W);
  v->pics[TEX_ATLAS].smooth = true;
  g_atlas = true;
}

static void make_textures(TgView3D* v) {
  static uint32_t px[128 * 128];
  // Grain: soft patches and fine speckle around light grey (tinted by the vertex colour).
  for(int y = 0; y < 128; y++)
    for(int x = 0; x < 128; x++) {
      float k = 0.82f + 0.12f * noise2(x, y, 128, 7) + 0.06f * ((hash32((uint32_t) (y * 128 + x)) & 255) / 255.0f);
      px[y * 128 + x] = grey(k);
    }
  set_pic(&v->pics[TEX_GRAIN], 128, 128, px, 128);
  // Asphalt: finer, with dark flecks and light stones.
  for(int y = 0; y < 128; y++)
    for(int x = 0; x < 128; x++) {
      uint32_t h = hash32((uint32_t) (y * 128 + x) ^ 0x9e3779b9u);
      float k = 0.86f + 0.07f * noise2(x, y, 128, 11) + 0.05f * ((h & 255) / 255.0f);
      if((h >> 8 & 63) == 0) k -= 0.18f;
      if((h >> 14 & 127) == 0) k += 0.12f;
      px[y * 128 + x] = grey(k);
    }
  set_pic(&v->pics[TEX_ASPHALT], 128, 128, px, 128);
  // Chevron boards: white, a dark rim, two red chevrons pointing left (and mirrored, right).
  for(int dir = 0; dir < 2; dir++) {
    for(int y = 0; y < 64; y++)
      for(int x = 0; x < 128; x++) {
        uint32_t c = 0xFFF6F6F2;
        if(x < 4 || x >= 124 || y < 4 || y >= 60) c = 0xFF1C1C22;
        else {
          int xx = dir ? 127 - x : x;
          float dy = fabsf(y - 31.5f);
          for(int k = 0; k < 2; k++) {
            float tip = 22 + k * 44, d = xx - (tip + dy * 0.9f);
            if(d >= 0 && d < 16) c = 0xFFD8202A;
          }
        }
        px[y * 128 + x] = c;
      }
    set_pic(&v->pics[dir ? TEX_CHEVRON_R : TEX_CHEVRON_L], 128, 64, px, 128);
  }
  // Warning board: a yellow triangle, red rim, black "!".
  for(int y = 0; y < 64; y++)
    for(int x = 0; x < 64; x++) {
      float u = (x + 0.5f) / 64, w = (y + 0.5f) / 64;  // apex at the top
      float half = w * 0.5f, d = fabsf(u - 0.5f);
      uint32_t c = 0;
      if(d <= half && w <= 0.96f) {
        c = (d > half - 0.07f || w > 0.88f) ? 0xFFD0202A : 0xFFF5C518;
        if(d < 0.045f && ((w > 0.32f && w < 0.66f) || (w > 0.72f && w < 0.8f))) c = 0xFF141414;
      }
      px[y * 64 + x] = c;
    }
  set_pic(&v->pics[TEX_WARNING], 64, 64, px, 64);
  // Glow: a soft white disc (its alpha falls off), for the sun.
  for(int y = 0; y < 64; y++)
    for(int x = 0; x < 64; x++) {
      float dx = (x + 0.5f) / 32 - 1, dy = (y + 0.5f) / 32 - 1, r = sqrtf(dx * dx + dy * dy);
      float a = r >= 1 ? 0 : (1 - r) * (1 - r);
      px[y * 64 + x] = (uint32_t) (a * 255) << 24 | 0xFFFFFF;
    }
  v->pics[TEX_GLOW].replaced = false;
  free(v->pics[TEX_GLOW].px);
  v->pics[TEX_GLOW].px = malloc(64 * 64 * 4);
  if(v->pics[TEX_GLOW].px) {
    memcpy(v->pics[TEX_GLOW].px, px, 64 * 64 * 4);
    v->pics[TEX_GLOW].w = v->pics[TEX_GLOW].h = 64;
    v->pics[TEX_GLOW].dirty_from = v->pics[TEX_GLOW].version, v->pics[TEX_GLOW].dirty_y0 = 0, v->pics[TEX_GLOW].dirty_y1 = 64;
    v->pics[TEX_GLOW].version++;
  }
  // Clouds: soft heaps of round puffs, flatter at the bottom, see-through at the edges.
  for(int kind = 0; kind < 2; kind++) {
    static float a[64 * 128];
    memset(a, 0, sizeof(a));
    uint32_t seed = 0xC10D + kind * 977;
    for(int b = 0; b < 9; b++) {
      seed = hash32(seed + (uint32_t) b);
      float cx = 22 + (seed & 1023) / 1023.0f * 84, cy = 34 + ((seed >> 10) & 255) / 255.0f * 10 - (b % 3) * 4;
      float r = 10 + ((seed >> 18) & 255) / 255.0f * (kind ? 10 : 14);
      for(int y = 0; y < 64; y++)
        for(int x = 0; x < 128; x++) {
          float d = ((x - cx) * (x - cx) + (y - cy) * (y - cy) * 1.6f) / (r * r);
          if(d < 1) a[y * 128 + x] += (1 - d) * (1 - d);
        }
    }
    for(int y = 0; y < 64; y++)
      for(int x = 0; x < 128; x++) {
        float k = a[y * 128 + x] * 1.6f * (0.85f + 0.3f * noise2(x, y, 64, 31 + kind));
        if(y > 44) k *= 1 - (y - 44) / 20.0f;  // a flat underside
        k = k > 1 ? 1 : k < 0 ? 0 : k;
        float shadeK = 0.82f + 0.18f * (1 - y / 64.0f);  // lit from above
        uint32_t g8 = (uint32_t) (shadeK * 255);
        uint32_t b8 = (uint32_t) (g8 + (255 - g8) * 0.4f);
        px[y * 128 + x] = (uint32_t) (k * 255) << 24 | g8 << 16 | g8 << 8 | b8;
      }
    set_pic(&v->pics[kind ? TEX_CLOUD2 : TEX_CLOUD], 128, 64, px, 128);
  }
  make_atlas(v);
}

// The ground's colours at a segment: its stretch's, once seen there, else the course's.
// The modern look's grading: the SNES's loud greens and sands toned down (less saturated,
// a little darker), as in the concept art.
static uint32_t calm(uint32_t c) {
  float r = (c >> 16 & 255), g = (c >> 8 & 255), b = (c & 255), l = r * 0.3f + g * 0.59f + b * 0.11f;
  float k = 0.48f;  // saturation kept
  r = (l + (r - l) * k) * 0.86f, g = (l + (g - l) * k) * 0.84f, b = (l + (b - l) * k) * 0.88f;
  return (c & 0xFF000000u) | (uint32_t) fminf(r, 255) << 16 | (uint32_t) fminf(g, 255) << 8 | (uint32_t) fminf(b, 255);
}

static int nseg_of(const TgView3D* v, float pos) {
  int n = v->track.tables.segments;
  if(n <= 0) return 0;
  int s = (int) floorf(pos / TG_TRACK3D_SEG_UNITS) % n;
  return s < 0 ? s + n : s;
}

// The part of the way an easing of t per game frame goes in this picture: the shown
// colours and fades move by the game's frames, not by pictures drawn (a headless render every
// 100 frames, or high frame rate, would otherwise ease at another speed).
static float ease_k(const TgView3D* v, float t) { return v->ease_n <= 0 ? 0 : 1 - powf(1 - t, v->ease_n); }
static uint8_t fade_up(const TgView3D* v, uint8_t f, float per_frame) {
  float n = f + per_frame * v->ease_n;
  return (uint8_t) (n > 255 ? 255 : n);
}

// a eased towards b by t of the way, at least one step per channel (it gets there).
static uint32_t ease_colour(uint32_t a, uint32_t b, float t) {
  if(t <= 0) return a;
  uint32_t out = 0xFF000000u;
  for(int sh = 0; sh <= 16; sh += 8) {
    int x = a >> sh & 255, y = b >> sh & 255, d = (int) ((y - x) * t);
    if(d == 0 && y != x) d = y > x ? 1 : -1;
    out |= (uint32_t) (x + d) << sh;
  }
  return out;
}

static int lum3(uint32_t c) { return (c >> 16 & 255) + (c >> 8 & 255) + (c & 255); }

static float hist_count(const Hist* h, uint32_t c) {
  for(int i = 0; i < HIST; i++)
    if(h->n[i] > 0 && h->c[i] == c) return h->n[i];
  return 0;
}

// The two commonest colours of a histogram as a pair, kept as they were picked until another
// is clearly commoner. The original's ground stripes alternate two colours seen about
// equally often: ranked afresh each picture they swapped places every few frames, so every
// verge and field of a stretch changed shade at once (QA-004). pick: the last pair (0 none).
static void hist_pair(const Hist* h, uint32_t f0, uint32_t pick[2]) {
  uint32_t a = hist_top(h, 0, f0);
  float na = hist_count(h, a);
  if(pick[0] && pick[0] != a && hist_count(h, pick[0]) >= na * 0.75f) a = pick[0];
  na = hist_count(h, a);
  // The next commonest other than a, if it is common enough (as hist_top's rank 1).
  int bi = -1;
  for(int i = 0; i < HIST; i++)
    if(h->n[i] > 0 && h->c[i] != a && (bi < 0 || h->n[i] > h->n[bi])) bi = i;
  uint32_t b = bi >= 0 && h->n[bi] > na * 0.15f ? h->c[bi] : a;
  if(pick[1] && pick[1] != a && pick[1] != b && hist_count(h, pick[1]) >= hist_count(h, b) * 0.75f && hist_count(h, pick[1]) > na * 0.15f)
    b = pick[1];
  pick[0] = a, pick[1] = b;
}

// A stretch's ground colours as learned (the course's where the stretch isn't known yet).
static void ground_learned(TgView3D* v, int bucket, uint32_t g0, uint32_t g1, uint32_t* a, uint32_t* b) {
  const Hist* h = &v->seg_ground[bucket % GROUND_BUCKETS];
  uint32_t* pick = v->seg_pick[bucket % GROUND_BUCKETS];
  if(hist_weight(h) < 12) {
    *a = g0, *b = g1;
    return;
  }
  hist_pair(h, g0, pick);
  *a = pick[0], *b = pick[1];
  // Much darker than the course's ground: a shadow or the pit lane was seen, not the ground.
  // Much brighter: road lines, posts and kerbs, all that a night's dark ground leaves to be
  // seen there (the "white snow" on night courses once day broke: retro audit R-06).
  int l0 = lum3(g0);
  if(lum3(*a) < l0 * 6 / 10 || lum3(*a) > l0 * 9 / 5 + 60) *a = g0;
  if(lum3(*b) < l0 * 6 / 10 || lum3(*b) > l0 * 9 / 5 + 60) *b = *a;
}

// Once per picture: what each stretch shows drifts to what was learned (about half a
// second), so a stretch whose colours are learned, or change, never switches at once.
static void ground_ease(TgView3D* v, uint32_t g0, uint32_t g1) {
  for(int k = 0; k < GROUND_BUCKETS; k++) {
    uint32_t want[2];
    ground_learned(v, k, g0, g1, &want[0], &want[1]);
    for(int j = 0; j < 2; j++)
      v->seg_show[k][j] = v->seg_show[k][j] ? ease_colour(v->seg_show[k][j], want[j], ease_k(v, 0.06f)) : want[j];
  }
}

static void ground_at(const TgView3D* v, int seg, uint32_t g0, uint32_t g1, uint32_t* a, uint32_t* b) {
  const uint32_t* sh = v->seg_show[(seg / GROUND_STRETCH) % GROUND_BUCKETS];
  if(!sh[0]) {  // (not eased yet: the course's)
    *a = g0, *b = g1;
    return;
  }
  *a = sh[0], *b = sh[1];
}

// --- The game's sprites, put together by object ----------------------------------------

#define MAP_DOTS 3  // OAM entries 0-2: the cars' dots on the HUD's map

static const uint8_t kSpriteSizes[8][2] = {{8, 16}, {8, 32}, {8, 64}, {16, 32}, {16, 64}, {32, 64}, {16, 32}, {16, 32}};

typedef struct {
  int x, y, size;
  int index;
} Spr;

static Spr sprite_at(const TgSpriteState* s, int i) {
  int idx = i * 2;
  Spr sp = {0, 0, 0, i};
  int y = s->oam[idx] >> 8;
  int x = (s->oam[idx] & 0xFF) | ((s->high_oam[idx >> 3] >> (idx & 7)) & 1) << 8;
  if(x >= 256) x -= 512;
  int size = kSpriteSizes[s->obj_size & 7][(s->high_oam[idx >> 3] >> ((idx & 7) + 1)) & 1];
  if(y >= 224 && y <= 256 - size) return sp;  // parked below the screen
  if(y >= 224) y -= 256;
  sp.x = x;
  sp.y = y;
  sp.size = size;
  return sp;
}

static void draw_sprite(const TgSpriteState* s, const Spr* sp, uint32_t* buf, int bw, int bh, int ox, int oy) {
  uint16_t attr = s->oam[sp->index * 2 + 1];
  int tile = attr & 0xFF, pal = (attr >> 9) & 7;
  uint16_t base = (attr & 0x100) ? s->obj_adr2 : s->obj_adr1;
  bool hf = attr & 0x4000, vf = attr & 0x8000;
  for(int row = 0; row < sp->size; row++) {
    for(int col = 0; col < sp->size; col++) {
      int t = (((tile >> 4) + row / 8) << 4) | (((tile & 15) + col / 8) & 15);
      uint16_t p1 = s->vram[(base + t * 16 + (row & 7)) & 0x7FFF];
      uint16_t p2 = s->vram[(base + t * 16 + 8 + (row & 7)) & 0x7FFF];
      int sh = 7 - (col & 7);
      int c = (p1 >> sh & 1) | (p1 >> (8 + sh) & 1) << 1 | (p2 >> sh & 1) << 2 | (p2 >> (8 + sh) & 1) << 3;
      if(c == 0) continue;
      int dx = (hf ? sp->size - 1 - col : col) + sp->x - ox;
      int dy = (vf ? sp->size - 1 - row : row) + sp->y - oy;
      if(dx >= 0 && dy >= 0 && dx < bw && dy < bh) buf[dy * bw + dx] = snes_colour(s->cgram[0x80 + 16 * pal + c]);
    }
  }
}

static bool touch(const Spr* a, const Spr* b) {
  return a->x <= b->x + b->size && b->x <= a->x + a->size && a->y <= b->y + b->size && b->y <= a->y + a->size;
}

// An object's picture: its sprites put together, cropped. Only the biggest set of touching
// sprites counts (a tag also covers the odd sprite drawn next, such as the map marker).
// Returns false if nothing is left; *edge is set if it is cut by the screen's edge.
static float g_compose_x;  // the screen x of the middle of the last picture composed

static bool compose(const TgSpriteState* s, const Spr* sp, int n, uint32_t* buf, int* w, int* h, bool* edge) {
  int cluster[128], best = -1, bestArea = 0;
  for(int i = 0; i < n; i++) cluster[i] = i;
  for(bool changed = true; changed;) {
    changed = false;
    for(int i = 0; i < n; i++)
      for(int j = 0; j < n; j++)
        if(cluster[j] < cluster[i] && touch(&sp[i], &sp[j])) {
          cluster[i] = cluster[j];
          changed = true;
        }
  }
  for(int c = 0; c < n; c++) {
    int area = 0;
    for(int i = 0; i < n; i++) if(cluster[i] == c) area += sp[i].size * sp[i].size;
    if(area > bestArea) {
      bestArea = area;
      best = c;
    }
  }
  if(best < 0) return false;
  int x0 = 9999, y0 = 9999, x1 = -9999, y1 = -9999;
  for(int i = 0; i < n; i++) {
    if(cluster[i] != best) continue;
    if(sp[i].x < x0) x0 = sp[i].x;
    if(sp[i].y < y0) y0 = sp[i].y;
    if(sp[i].x + sp[i].size > x1) x1 = sp[i].x + sp[i].size;
    if(sp[i].y + sp[i].size > y1) y1 = sp[i].y + sp[i].size;
  }
  int bw = x1 - x0, bh = y1 - y0;
  if(bw <= 0 || bh <= 0 || bw > 256 || bh > 256) return false;
  static uint32_t tmp[256 * 256];
  memset(tmp, 0, (size_t) bw * bh * 4);
  for(int i = n - 1; i >= 0; i--)  // lower OAM index in front
    if(cluster[i] == best) draw_sprite(s, &sp[i], tmp, bw, bh, x0, y0);
  int cx0 = bw, cy0 = bh, cx1 = -1, cy1 = -1;
  for(int y = 0; y < bh; y++)
    for(int x = 0; x < bw; x++)
      if(tmp[y * bw + x]) {
        if(x < cx0) cx0 = x;
        if(x > cx1) cx1 = x;
        if(y < cy0) cy0 = y;
        if(y > cy1) cy1 = y;
      }
  if(cx1 < 0) return false;
  *w = cx1 - cx0 + 1;
  *h = cy1 - cy0 + 1;
  if(*w * *h < 24) return false;
  for(int y = 0; y < *h; y++) memcpy(&buf[y * *w], &tmp[(y + cy0) * bw + cx0], (size_t) *w * 4);
  *edge = x0 + cx0 <= 0 || x0 + cx0 + *w >= 256 || y0 + cy0 + *h >= 224;
  g_compose_x = x0 + cx0 + *w * 0.5f;
  return true;
}

static uint32_t main_colour(const uint32_t* px, int n) {
  // The commonest colour that isn't nearly black, white or grey (counted at 4 bits a
  // channel).
  static uint16_t count[4096];
  memset(count, 0, sizeof(count));
  uint32_t best = 0xFF808080;
  int bestN = 0;
  for(int i = 0; i < n; i++) {
    uint32_t c = px[i];
    if(!(c >> 24)) continue;
    int r = c >> 16 & 255, g = c >> 8 & 255, b = c & 255;
    int mx = r > g ? (r > b ? r : b) : (g > b ? g : b), mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    if(mx < 48 || mx - mn < 40) continue;
    int k = ++count[(r >> 4) << 8 | (g >> 4) << 4 | (b >> 4)];
    if(k > bestN) {
      bestN = k;
      best = c;
    }
  }
  return best;
}

// A segment's roadside object word ($6400) as a picture slot: the art and the side, or for
// a group (bit 15; the start banner is one) its number. -1: nothing to draw (none, or
// bit 14: the markers that turn the road edges' hold on and off).
// Where a segment's roadside object stands across the road, in road half-widths (the frame's
// side / TG_TRACK3D_SIDE_EDGE), on its side. The game's own unit (its collision test, $D6BD
// in src/port/drive.c): the word's low byte x 128 in the car's side units, so 124 (the most
// used) stands just past the kerb and 255 at the car's furthest out ($7FFF). It used to be
// guessed as 1.25 + n / 96, a road's width too far out: a car on the verge drove through
// boards the original shows between it and the road (QA: Rome 3900).
static float object_side(uint16_t ow) {
  float s = (float) ((ow & 0xFF) << 7) / TG_TRACK3D_SIDE_EDGE;
  return (ow & 0x0100) ? -s : s;
}

static int art_key(uint16_t ow) {
  if(ow == 0 || (ow & 0x4000)) return -1;
  if(ow & 0x8000) return 256 + (ow & 15);
  return ((ow >> 9) & 0x7F) << 1 | ((ow >> 8) & 1);
}

// Track units from a to b, forwards round the lap.
static int ahead(int a, int b, int len) {
  int d = b - a;
  d %= len;
  if(d < 0) d += len;
  return d;
}

static void capture_objects(TgView3D* v, const uint8_t* m, const TgSpriteState* s) {
  int len = r16(m, 0x0200);
  // The game's projection: depth is track units from its camera ($46, the player's track
  // position), plus $90 (its car list starts $10 behind the player, at depth $80). A
  // picture's pixels times its depth is its size in the world, up to one factor (k).
  static const uint16_t kCams[2] = {0x0046, 0x0059};
  Spr sp[128];
  uint16_t ids[128];
  int n = 0;
  // A 1-player race drawn split (the original's own drawing, as --port verify shows it, with
  // the COMPUTER rival's view below) has its OAM from the original's code, while the object
  // tags are the C sprite builder's for the full-screen view: they don't match, and cars came
  // out learned as roadside pictures (QA-001). Nothing is learned from such a frame.
  bool full = false;
  for(int i = 0; i < 224 && !full; i++) full = s->line_at[i] != i;
  if(!v->two && !full) return;
  // OAM entries 0-2 are the map's dots (the sprite builder starts at entry 3).
  for(int i = MAP_DOTS; i < 128; i++) {
    Spr a = sprite_at(s, i);
    uint16_t id = s->obj_ident[i];
    // No object, or the bottom view's in a 1-player race (the full-screen view's are the top's).
    if(a.size == 0 || id == 0 || ((id & 0x4000) && !v->two)) continue;
    sp[n] = a;
    ids[n] = id;
    n++;
  }
  static uint32_t buf[256 * 256];
  bool done[128] = {false};
  int playerIds[2];
  for(int vw = 0; vw < 2; vw++) playerIds[vw] = v->cur.view_car[vw] >= 0 ? v->cur.cars[v->cur.view_car[vw]].id : -1;
  for(int i = 0; i < n; i++) {
    if(done[i]) continue;
    Spr group[128];
    int gn = 0;
    for(int j = i; j < n; j++)
      if(ids[j] == ids[i]) {
        group[gn++] = sp[j];
        done[j] = true;
      }
    int w, h;
    bool edge;
    if(!compose(s, group, gn, buf, &w, &h, &edge)) continue;
    uint16_t id = ids[i];
    int vw = (id & 0x4000) ? 1 : 0;  // which view's sprite: its camera and its player
    int cam = r16(m, kCams[vw]), playerId = playerIds[vw];
    id &= (uint16_t) ~0x4000;
    if(id & 0x8000) {
      int car = id & 0x7F;
      if(car >= 32) continue;  // the ghost
      // One car is about twice as wide as it is tall; wider is two cars together.
      if(w > h * 4 || w < h) continue;
      int depth = -1;
      for(int k = 0; k < v->cur.car_count; k++)
        if(v->cur.cars[k].id == car) depth = ahead(cam, (int) v->cur.cars[k].track_pos, len) + 0x90;
      if(car == playerId) {
        depth = 0x90;
        // The player's car straight ahead is the narrowest of its full pictures (turning
        // shows some of its side): CAR_WIDTH. Pictures cut short don't count.
        if(!edge && w > v->player_max_w) {
          v->player_max_w = w;
          if(v->player_min_w < w * 3 / 4) v->player_min_w = 0;
        }
        if(!edge && w >= v->player_max_w * 3 / 4 && (v->player_min_w == 0 || w < v->player_min_w)) {
          v->player_min_w = w;
          v->k = CAR_WIDTH / ((float) w * 0x90);
        }
      }
      if(depth < 0x80 || depth > 0x400 || v->k == 0) continue;
      CarInfo* ci = &v->car[car];
      float mpp = depth * v->k;
      if(mpp * w > CAR_WIDTH * 1.6f || mpp * w < CAR_WIDTH * 0.6f) continue;  // a part of it, or another thing
      set_pic(&v->pics[CAR_PIC + car], w, h, buf, w);
      ci->mpp = mpp;
      ci->colour = main_colour(buf, w * h);
      // Its model's back: the biggest picture seen from straight behind (the narrowest
      // for its height).
      float aspect = (float) w / h;
      if(!edge && (w * h > ci->rear_area * 1.2f || (w * h * 1.2f >= ci->rear_area && aspect < ci->rear_aspect))) {
        ci->rear_area = w * h;
        ci->rear_aspect = aspect;
        ci->rear_mpp = depth * v->k;
        set_pic(&v->pics[REAR_PIC + car], w, h, buf, w);
        if(car_trace()) {
          int pals = 0;
          for(int q = 0; q < gn; q++) pals |= 1 << ((s->oam[group[q].index * 2 + 1] >> 9) & 7);
          fprintf(stderr, "CR %u car %d %dx%d pals %02x paint %08x", v->frames, car, w, h, pals, paint_colour(&v->pics[REAR_PIC + car]));
          for(int pl = 0; pl < 8; pl++)
            if(pals >> pl & 1) {
              fprintf(stderr, " P%d:", pl);
              for(int ci = 1; ci < 16; ci++) fprintf(stderr, " %06x", snes_colour(s->cgram[0x80 + 16 * pl + ci]) & 0xFFFFFF);
            }
          int hist[16] = {0}, low = 0;  // the lowest palette used
          while(low < 7 && !(pals >> low & 1)) low++;
          for(int q = 0; q < w * h; q++)
            for(int ci = 1; ci < 16; ci++)
              if(buf[q] == snes_colour(s->cgram[0x80 + 16 * low + ci])) { hist[ci]++; break; }
          fprintf(stderr, " H");
          for(int ci = 1; ci < 16; ci++) fprintf(stderr, " %d", hist[ci]);
          fprintf(stderr, "\n");
        }
      }
    } else {
      int seg = (id - 1) >> 2;
      if(((id - 1) & 3) != 0 || seg >= v->track.tables.segments) continue;  // a window object
      int key = art_key(v->track.tables.object[seg]);
      if(key < 0) continue;
      ArtInfo* a = &v->art[key];
      int depth = ahead(cam, seg * TG_TRACK3D_SEG_UNITS + 16, len) + 0x90;
      if(edge || v->k == 0 || depth < 0x90 || depth > 0x400) continue;
      // A group (bit 15) is drawn round the road's middle, wherever its sprites put it: where
      // across the road it stands, from the player's place and the picture's on the screen
      // (the game keeps the player's car in the middle of the screen).
      if(key >= 256 && playerId >= 0) {
        float side = 0;
        for(int k = 0; k < v->cur.car_count; k++)
          if(v->cur.cars[k].id == playerId) side = v->cur.cars[k].side;
        float metres = (g_compose_x - 128) * depth * v->k;
        v->art_side[key] += side + metres / v->track.scale.half_width * TG_TRACK3D_SIDE_EDGE;
        v->art_side_n[key]++;
      }
      // Its size: picture lines x depth is the same at every distance (the game's
      // projection); close-up pictures (12 lines or more) measure it best.
      float hm = h * depth * v->k;
      if(hm > 12) continue;  // taller than any roadside thing: a misreading
      if(h >= 12 || a->n == 0) {
        if(h >= 12 && a->n > 0 && a->area < 12 * 12) a->sum_h = 0, a->n = 0;  // better ones now
        a->sum_h += hm;
        a->n++;
      }
      if(w * h > a->area) {
        a->area = w * h;
        set_pic(&v->pics[ART_PIC + key], w, h, buf, w);
        v->art_colour[key] = main_colour(buf, w * h);
      }
    }
  }
}

// --- The skyline and colours, from the original picture ---------------------------------

static uint32_t px_at(const uint32_t* p, int x, int row) { return p[(16 + row * 2) * TG_FB_WIDTH + x * 2] | 0xFF000000u; }

static bool road_grey(uint32_t c) {
  int r = c >> 16 & 255, g = c >> 8 & 255, b = c & 255;
  return abs(r - g) < 20 && abs(g - b) < 20 && r > 40 && r < 140;
}

// Is c the sky (the original's sky colours, or blue)? The panorama leaves the sky out, so
// the 3D sky's own gradient shows through it.
static bool sky_colour(const TgView3D* v, uint32_t c) {
  int r = c >> 16 & 255, g = c >> 8 & 255, b = c & 255;
  // Clearly sky blue (pale or deep); lavender and blue-grey buildings (Japan's) are not.
  if(b > g + 8 && (b > r + 100 || (b > r + 48 && r < 110))) return true;
  return c == hist_top(&v->sky_top, 0, 1) || c == hist_top(&v->sky_low, 0, 1);
}

// The panorama as drawn: the sky left out, and what the game hasn't shown yet filled with
// the commonest colour of its line (the ground under the skyline, say), so there are no
// gaps.
// Scale2x: twice the size, edges kept sharp and their steps halved. out is 2w x 2h. wrap:
// the left and right edges meet (a panorama).
static void scale2x(const uint32_t* in, int w, int h, uint32_t* out, bool wrap) {
  for(int y = 0; y < h; y++)
    for(int x = 0; x < w; x++) {
      uint32_t P = in[y * w + x];
      uint32_t A = y > 0 ? in[(y - 1) * w + x] : P, D = y + 1 < h ? in[(y + 1) * w + x] : P;
      uint32_t C = x > 0 ? in[y * w + x - 1] : wrap ? in[y * w + w - 1] : P;
      uint32_t B = x + 1 < w ? in[y * w + x + 1] : wrap ? in[y * w] : P;
      uint32_t e0 = P, e1 = P, e2 = P, e3 = P;
      if(A != D && C != B) {
        e0 = C == A ? A : P;
        e1 = A == B ? B : P;
        e2 = C == D ? C : P;
        e3 = D == B ? D : P;
      }
      out[(2 * y) * 2 * w + 2 * x] = e0;
      out[(2 * y) * 2 * w + 2 * x + 1] = e1;
      out[(2 * y + 1) * 2 * w + 2 * x] = e2;
      out[(2 * y + 1) * 2 * w + 2 * x + 1] = e3;
    }
}

// A picture four times the size (Scale2x twice), to be drawn filtered: smooth edges on
// pixel art (fonts, the cars' sides).
static void upscale4(TgV3Pic* dst, const TgV3Pic* src) {
  if(!src->w || !src->px) return;
  size_t n = (size_t) src->w * src->h;
  uint32_t* a = malloc(n * 4 * 4);
  uint32_t* b = malloc(n * 16 * 4);
  if(a && b) {
    scale2x(src->px, src->w, src->h, a, false);
    scale2x(a, src->w * 2, src->h * 2, b, false);
    set_pic(dst, src->w * 4, src->h * 4, b, src->w * 4);
    dst->smooth = true;
  }
  free(a);
  free(b);
}

// The skyline for the modern look: twice the size by Scale2x (edges stay sharp, steps on
// slopes are halved), then drawn filtered.
static void smooth_sky(TgView3D* v) {
  const TgV3Pic* p = &v->pics[SKY_PIC];
  if(!p->w) return;
  int w = p->w, h = p->h;
  static uint32_t out[SKY_W * 2 * SKY_H * 2];
  if(w > SKY_W || h > SKY_H) return;
  for(int y = 0; y < h; y++)
    for(int x = 0; x < w; x++) {
      uint32_t P = p->px[y * w + x];
      uint32_t A = y > 0 ? p->px[(y - 1) * w + x] : P, D = y + 1 < h ? p->px[(y + 1) * w + x] : P;
      uint32_t C = p->px[y * w + (x + w - 1) % w], B = p->px[y * w + (x + 1) % w];  // it wraps round
      uint32_t e0 = P, e1 = P, e2 = P, e3 = P;
      if(A != D && C != B) {
        e0 = C == A ? A : P;
        e1 = A == B ? B : P;
        e2 = C == D ? C : P;
        e3 = D == B ? D : P;
      }
      out[(2 * y) * 2 * w + 2 * x] = e0;
      out[(2 * y) * 2 * w + 2 * x + 1] = e1;
      out[(2 * y + 1) * 2 * w + 2 * x] = e2;
      out[(2 * y + 1) * 2 * w + 2 * x + 1] = e3;
    }
  set_pic(&v->pics[SKY_SMOOTH], 2 * w, 2 * h, out, 2 * w);
  v->pics[SKY_SMOOTH].smooth = true;
}

static void sky_picture(TgView3D* v) {
  static uint32_t out[SKY_W * SKY_H];
  for(int y = 0; y < SKY_H; y++) {
    uint32_t* row = &v->sky_px[y * SKY_W];
    Hist h = {0};
    for(int x = 0; x < SKY_W; x += 2)
      if(row[x] >> 24) hist_add(&h, row[x], 1);
    uint32_t fill = hist_top(&h, 0, 0);
    if(fill && sky_colour(v, fill)) fill = 0;
    // Only small gaps are filled: a stretch not seen yet stays empty until it is (rather
    // than a streak of one colour).
    for(int x = 0; x < SKY_W; x++) {
      uint32_t c = row[x];
      if(!(c >> 24)) {
        int run = 1;
        for(int l = 1; l < 24 && !(row[(x - l) & (SKY_W - 1)] >> 24); l++) run++;
        for(int r = 1; r < 24 && !(row[(x + r) & (SKY_W - 1)] >> 24); r++) run++;
        c = run < 24 ? fill : 0;
      }
      out[y * SKY_W + x] = c && !sky_colour(v, c) ? c : 0;
    }
  }
  set_pic(&v->pics[SKY_PIC], SKY_W, SKY_H, out, SKY_W);
  smooth_sky(v);
}

// A colour of the original's picture as it is at full brightness. While the screen fades,
// the PPU draws each 5-bit channel e as e * b / 15: that is undone exactly (for b >= 2 every
// value comes from one colour). 0 when c can't have come from brightness b (the picture
// was drawn at another brightness than the one read).
static uint32_t unfade(uint32_t c, int b) {
  if(b >= 15) return c | 0xFF000000u;
  if(b < 2) return 0;
  static int16_t inv[16][256];
  static bool made;
  if(!made) {
    for(int bb = 0; bb < 16; bb++)
      for(int o = 0; o < 256; o++) inv[bb][o] = -1;
    for(int bb = 2; bb < 15; bb++)
      for(int c5 = 0; c5 < 32; c5++) {
        int e = c5 << 3 | c5 >> 2;
        inv[bb][e * bb / 15] = (int16_t) e;
      }
    made = true;
  }
  uint32_t out = 0xFF000000u;
  for(int sh = 0; sh <= 16; sh += 8) {
    int e = inv[b][c >> sh & 255];
    if(e < 0) return 0;
    out |= (uint32_t) e << sh;
  }
  return out;
}

// Blue the strongest (or dark): sky, not grass or road.
static bool sky_like(uint32_t c) {
  int R = c >> 16 & 255, G = c >> 8 & 255, B = c & 255;
  return c && B >= R && B + 16 >= G;
}

static void capture_picture(TgView3D* v, const uint8_t* m, const uint32_t* p, const TgSpriteState* s) {
  // The horizon: the line where the game's sky stage ends and its road band begins ($AE),
  // where the full-screen view put it.
  if(s == NULL) return;
  // 1 player: only from the full-screen view the 3D mode turns on. A split picture (the
  // original's own drawing, as tg_headless --port verify shows it) has other lines.
  bool full = false;
  for(int i = 0; i < 224 && !full; i++) full = s->line_at[i] != i;
  if(!v->two && !full) return;
  int ae = r16(m, 0x00AE) & 0xFF;
  if(ae >= 224) return;
  int hz = s->line_at[ae];
  if(hz < 48 || hz > 200) return;
  // Only in the ordinary race view (the game's camera on the player's car, $46 is $1ED7,
  // not flying in at the start) with the horizon still (a hill moving it would smear).
  bool steady = hz == v->last_horizon && r16(m, 0x0046) == r16(m, 0x1ED7);
  // The skyline is only taken while racing ($1E58 is 0 in the race's main loop) and moving
  // (speed $1E5A): then every sprite moves against the scenery and is never seen twice in
  // the same place; standing still on the grid, the start gantry would be.
  bool moving = r16(m, 0x1E58) == 0 && !(r16(m, 0x1E5A) & 0x8000) && r16(m, 0x1E5A) > 0x100;
  // Nor near a group object (the start gantry): the game draws its banner in the
  // background, in the skyline's lines. Courses lined with them (Japan's gantries) look
  // only a little way ahead, or they would never show their skyline.
  {
    int n = v->track.tables.segments, cs = r16(m, 0x0046) / TG_TRACK3D_SEG_UNITS, groups = 0;
    for(int i = 0; i < n; i++) groups += (v->track.tables.object[i] & 0x8000) != 0;
    int ahead = groups > 3 ? 10 : 50;
    for(int i = -4; i < ahead && n > 0; i++)
      if(v->track.tables.object[((cs + i) % n + n) % n] & 0x8000) moving = false;
  }
  v->last_horizon = hz;
  v->horizon = hz;
  // Where sprites are on screen: the skyline isn't taken from under them.
  static uint8_t covered[224][256];
  memset(covered, 0, sizeof(covered));
  if(s) {
    for(int i = 0; i < 128; i++) {
      Spr a = sprite_at(s, i);
      if(a.size == 0) continue;
      // OAM has the lines as the game drew them; the full-screen view moves them.
      for(int y = a.y < 0 ? 0 : a.y; y < a.y + a.size && y < 224; y++) {
        int y0 = s->line_at[y], y1 = y + 1 < 224 ? s->line_at[y + 1] : y0 + 1;
        for(int yy = y0; yy < y1 + 1 && yy < 224; yy++)
          if(yy >= 0)
            for(int x = a.x < 0 ? 0 : a.x; x < a.x + a.size && x < 256; x++) covered[yy][x] = 1;
      }
    }
  }
  // The skyline: the original shows 256 of the panorama's 512 pixels, from its scroll.
  // Lines above the horizon are kept by their height above it; only a steady horizon is
  // taken (a hill moving it would smear the panorama).
  int scroll = r16(m, 0x00B0) & (SKY_W - 1);  // the top view's BG2 scroll (irq.c, sky)
  v->sky_scroll = (uint16_t) scroll;
  int top = hz - SKY_H < 40 ? 40 : hz - SKY_H;
  if(steady && moving && v->bright == 15) {  // (not from a fading picture)
    bool changed = false;
    // Only columns with sky above the band: over a crest the original's ground can reach
    // above the horizon line, and that is not skyline.
    static bool skyOver[256];
    for(int x = 0; x < 256; x++) skyOver[x] = top >= 2 && sky_colour(v, px_at(p, x, top - 2));
    for(int row = top; row < hz; row++) {
      int py = SKY_H - (hz - row);
      for(int x = 0; x < 256; x++) {
        if(covered[row][x] || !skyOver[x]) continue;
        uint32_t c = px_at(p, x, row);
        if((c & 0xFFFFFF) == 0) continue;  // black: a HUD message's box, not the skyline
        // Taken when seen twice in a row at the same place in the panorama: the scenery
        // is; a car or roadside object passing in front of it moves on.
        int at = py * SKY_W + ((x + scroll) & (SKY_W - 1));
        bool twice = v->sky_seen[at] == c;
        v->sky_seen[at] = c;
        if(!twice) continue;
        uint32_t* d = &v->sky_px[at];
        if(*d != c) {
          *d = c;
          changed = true;
        }
      }
    }
    if(changed || v->pics[SKY_PIC].w == 0) sky_picture(v);
  }
  // Colours, over time: the sky above the skyline, the ground stripes, the road. From the
  // ordinary race view (not the fly-in at the start, or over a crest); but a course whose
  // colours aren't known yet takes them from its first lit pictures, while the screen
  // fades in and the camera flies in, so the race never starts in another course's (or a
  // made-up) look (QA-005). A fading picture's colours are taken back to full brightness:
  // a fade-in never teaches a dark sky (Los Angeles lit as night).
  int b = v->bright;
  bool fresh = hist_weight(&v->ground) < 40 || hist_weight(&v->sky_top) < 4;
  if(b < 4 || !(steady || fresh)) return;
  {  // the brightness read must be the one the picture was drawn with
    int ok = 0, n = 0;
    for(int x = 8; x < 248; x += 16, n += 2) ok += unfade(px_at(p, x, 4), b) != 0, ok += unfade(px_at(p, x, hz + 8 < 224 ? hz + 8 : 223), b) != 0;
    if(ok * 10 < n * 9) return;
  }
#define px_at(p, x, row) unfade(px_at(p, x, row), b)
  // The sky follows the game within about half a second: a course's scripted change (night
  // falling in Cologne, day breaking in the Black Forest) comes through; the night
  // threshold's margins below keep it from blinking.
  hist_decay(&v->sky_top, 0.98f);
  hist_decay(&v->sky_low, 0.98f);
  hist_decay(&v->ground, 0.97f);  // (follows the day/night script too; the pair's pick has hysteresis)
  hist_decay(&v->road, 0.99f);
  // The sky: at the top in the middle (no HUD there in the full view), and above the
  // skyline.
  // Only when there is sky up there: the horizon low enough that the skyline's band is below
  // row 4 (over a crest the original's road band can fill the picture to the top).
  uint32_t groundNow = hist_top(&v->ground, 0, 0);
  bool skyAbove = hz > SKY_H + 12;
  for(int x = 64; x < 136; x += 8) {  // clear of the speedometer's rev bar
    uint32_t c4 = px_at(p, x, 4), cl = px_at(p, x, top);
    if(!steady && !sky_like(c4)) c4 = 0;  // flying in, or over a crest: only what is surely sky
    if(!steady && !sky_like(cl)) cl = 0;
    if(c4 && skyAbove && !covered[4][x] && c4 != groundNow && !road_grey(c4)) hist_add(&v->sky_top, c4, 1);
    if(cl && top > 8 && !covered[top][x] && !road_grey(cl)) hist_add(&v->sky_low, cl, 1);
  }
  // The ground just ahead of the player, for its stretch of the course.
  {
    int pk = v->cur.view_car[0];
    if(pk >= 0 && v->track.tables.segments > 0 && steady) {
      // The screen's edges a little below the horizon (clear of the HUD): about 20 m ahead.
      int seg = ((int) (v->cur.cars[pk].track_pos / TG_TRACK3D_SEG_UNITS) + 8) % v->track.tables.segments;
      Hist* gh = &v->seg_ground[(seg / GROUND_STRETCH) % GROUND_BUCKETS];
      hist_decay(gh, 0.995f);
      for(int row = hz + 16; row < hz + 56 && row < 224; row += 2)
        for(int x = 0; x < 256; x += 4) {
          if(covered[row][x] || (x >= 24 && x <= 232)) continue;
          uint32_t c = px_at(p, x, row);
          int sum = (c >> 16 & 255) + (c >> 8 & 255) + (c & 255);
          if(c && !road_grey(c) && sum > 90) hist_add(gh, c, 1);
        }
    }
  }
  for(int row = hz + 4; row < hz + 80 && row < 224; row++) {
    for(int x = 0; x < 256; x += 4) {
      if(covered[row][x]) continue;
      uint32_t c = px_at(p, x, row);
      if(!c) continue;
      if(road_grey(c)) hist_add(&v->road, c, 1);
      else if(x < 24 || x > 232) hist_add(&v->ground, c, 1);
    }
  }
#undef px_at
  // The first time this course's colours are known while the race is still fading in, what
  // is shown goes straight to them (it was the made-up default, under the fade): no drifting
  // over from a desert (QA-005). Learned later, in full view, they ease there instead.
  if(!v->colours_seen && hist_weight(&v->ground) > 0 && hist_weight(&v->sky_top) > 0) {
    v->colours_seen = true;
    if(v->lit_frames <= 2) {
      v->sky_show[0] = v->sky_show[1] = 0;
      memset(v->ground_show, 0, sizeof(v->ground_show));
      memset(v->seg_show, 0, sizeof(v->seg_show));
      v->ground_now = 0;
      v->night_lum = -1;
    }
  }
}

// The car select screen: its four cars seen from the side, on BG1 (red, white, purple, blue:
// the players' choices 0-3). Each is a patch of picture about 100 by 30 pixels; they are
// cut out by finding those patches.
static float pic_light(const uint32_t* px, int n) {
  double sum = 0;
  int k = 0;
  for(int i = 0; i < n; i++)
    if(px[i] >> 24) sum += (px[i] >> 16 & 255) + (px[i] >> 8 & 255) + (px[i] & 255), k++;
  return k ? (float) (sum / k) : 0;
}

static bool capture_portraits(TgView3D* v, const TgSpriteState* s) {
  static uint32_t scr[256 * 224];
  for(int ty = 0; ty < 28; ty++)
    for(int tx = 0; tx < 32; tx++) {
      uint16_t e = s->vram[(s->bg1_map + ty * 32 + tx) & 0x7FFF];
      int tile = e & 0x3FF, pal = (e >> 10) & 7;
      bool hf = e & 0x4000, vf = e & 0x8000;
      for(int r = 0; r < 8; r++) {
        int rr = vf ? 7 - r : r;
        uint16_t p1 = s->vram[(s->bg1_tiles + tile * 16 + rr) & 0x7FFF];
        uint16_t p2 = s->vram[(s->bg1_tiles + tile * 16 + 8 + rr) & 0x7FFF];
        for(int c = 0; c < 8; c++) {
          int sh = hf ? c : 7 - c;
          int k = (p1 >> sh & 1) | (p1 >> (8 + sh) & 1) << 1 | (p2 >> sh & 1) << 2 | (p2 >> (8 + sh) & 1) << 3;
          scr[(ty * 8 + r) * 256 + tx * 8 + c] = k ? snes_colour(s->cgram[pal * 16 + k]) : 0;
        }
      }
    }
  // Patches of picture: flood fill.
  static int16_t lab[256 * 224];
  static int stack[256 * 224];
  memset(lab, 0, sizeof(lab));
  typedef struct { int x0, y0, x1, y1, n; } Patch;
  Patch found[8];
  int nf = 0, next = 0;
  for(int i = 0; i < 256 * 224; i++) {
    if(!scr[i] || lab[i]) continue;
    int sp = 0, x0 = 999, y0 = 999, x1 = -1, y1 = -1, n = 0;
    lab[i] = (int16_t) ++next;
    stack[sp++] = i;
    while(sp) {
      int j = stack[--sp], x = j % 256, y = j / 256;
      n++;
      x0 = x < x0 ? x : x0, x1 = x > x1 ? x : x1, y0 = y < y0 ? y : y0, y1 = y > y1 ? y : y1;
      static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
      for(int d = 0; d < 4; d++) {
        int xx = x + dx[d], yy = y + dy[d];
        if(xx < 0 || xx >= 256 || yy < 0 || yy >= 224) continue;
        int q = yy * 256 + xx;
        if(scr[q] && !lab[q]) lab[q] = (int16_t) next, stack[sp++] = q;
      }
    }
    int w = x1 - x0 + 1, h = y1 - y0 + 1;
    if(w >= 60 && w <= 140 && h >= 16 && h <= 48 && n > w * h * 2 / 5 && nf < 8)
      found[nf++] = (Patch) {x0, y0, x1, y1, n};
  }
  if(nf != 4) return false;
  // In reading order: top left, top right, bottom left, bottom right.
  for(int a = 0; a < 4; a++)
    for(int b = a + 1; b < 4; b++) {
      bool swap = found[b].y0 / 24 < found[a].y0 / 24 || (found[b].y0 / 24 == found[a].y0 / 24 && found[b].x0 < found[a].x0);
      if(swap) {
        Patch t = found[a];
        found[a] = found[b];
        found[b] = t;
      }
    }
  // Two by two, the same rows and columns: the select screen, not something else.
  for(int c = 0; c < 4; c += 2)
    if(abs(found[c].y0 - found[c + 1].y0) > 12 || found[c + 1].x0 - found[c].x0 < 80) return false;
  if(found[2].y0 - found[0].y0 < 24 || abs(found[0].x0 - found[2].x0) > 24) return false;
  // The cursor: the car under it is drawn bigger than the others.
  {
    int best = -1, bw = 0;
    for(int c = 0; c < 4; c++)
      if(found[c].x1 - found[c].x0 > bw) bw = found[c].x1 - found[c].x0, best = c;
    if(best >= 0) v->select_cursor = best;
    // Its stats, from the text under the cars (BG1 rows 21-24, columns 19-27: tiles 1-26 are
    // A-Z, 27-36 the digits, 40 a point).
    for(int row = 0; row < 4; row++) {
      char* o = v->car_stat[v->select_cursor][row];
      int n = 0;
      for(int tx = 18; tx < 28 && n < 11; tx++) {
        int t = s->vram[(s->bg1_map + (21 + row) * 32 + tx) & 0x7FFF] & 0x3FF;
        char ch = t >= 1 && t <= 26 ? (char) ('A' + t - 1) : t >= 27 && t <= 36 ? (char) ('0' + t - 27) : t == 40 ? '.' : ' ';
        if(ch != ' ' || (n && o[n - 1] != ' ')) o[n++] = ch;
      }
      while(n && o[n - 1] == ' ') n--;
      o[n] = 0;
      if(row >= 2) {  // grip, fuel: one word (the label's end can reach these columns)
        char* sp = strrchr(o, ' ');
        if(sp) memmove(o, sp + 1, strlen(sp + 1) + 1);
      }
    }
  }
  // The screen fades in and out by its colours: the brightest sight of each is kept.
  static uint32_t pic[140 * 48];
  for(int c = 0; c < 4; c++) {
    int w = found[c].x1 - found[c].x0 + 1, h = found[c].y1 - found[c].y0 + 1;
    for(int y = 0; y < h; y++)
      for(int x = 0; x < w; x++) pic[y * w + x] = scr[(found[c].y0 + y) * 256 + found[c].x0 + x];
    TgV3Pic* old = &v->pics[SIDE_SRC + c];
    if(old->w && pic_light(old->px, old->w * old->h) > pic_light(pic, w * h) * 1.02f) continue;
    set_pic(old, w, h, pic, w);
  }
  return true;
}

// The game's own HUD: the top view's part of BG1 (4 bits a pixel, colour 0 see-through).
static void capture_hud(TgView3D* v, const TgSpriteState* s) {
  static uint32_t hud[256 * HUD_ROWS];
  for(int ty = 0; ty < HUD_ROWS / 8; ty++)
    for(int tx = 0; tx < 32; tx++) {
      uint16_t e = s->vram[(s->bg1_map + ty * 32 + tx) & 0x7FFF];
      int tile = e & 0x3FF, pal = (e >> 10) & 7;
      bool hf = e & 0x4000, vf = e & 0x8000;
      for(int r = 0; r < 8; r++) {
        int rr = vf ? 7 - r : r;
        uint16_t p1 = s->vram[(s->bg1_tiles + tile * 16 + rr) & 0x7FFF];
        uint16_t p2 = s->vram[(s->bg1_tiles + tile * 16 + 8 + rr) & 0x7FFF];
        for(int c = 0; c < 8; c++) {
          int sh = hf ? c : 7 - c;
          int k = (p1 >> sh & 1) | (p1 >> (8 + sh) & 1) << 1 | (p2 >> sh & 1) << 2 | (p2 >> (8 + sh) & 1) << 3;
          hud[(ty * 8 + r) * 256 + tx * 8 + c] = k ? snes_colour(s->cgram[pal * 16 + k]) : 0;
        }
      }
    }
  // The cars' dots on the map: OAM entries 0-2, in the same lines as the HUD (the full-screen
  // view moves both together when it composes the picture).
  for(int i = 0; i < MAP_DOTS; i++) {
    Spr d = sprite_at(s, i);
    if(d.size && d.y < HUD_ROWS) draw_sprite(s, &d, hud, 256, HUD_ROWS, 0, 0);
  }
  // (HUD_HI, its smooth upscale, is made when it is drawn: hud_hi_ready)
  set_pic(&v->pics[HUD_PIC], 256, HUD_ROWS, hud, 256);
}

// The HUD upscaled, made from HUD_PIC as it is now (only when a message from it is drawn:
// the HUD changes nearly every frame, and the upscale is 3.5 MB to make and send).
static void hud_hi_ready(TgView3D* v) {
  if(!v->pics[HUD_PIC].w || (v->pics[HUD_HI].w && v->hud_hi_of == v->pics[HUD_PIC].version + 1)) return;
  upscale4(&v->pics[HUD_HI], &v->pics[HUD_PIC]);
  v->hud_hi_of = v->pics[HUD_PIC].version + 1;
}

// --- Kept between sessions ---------------------------------------------------------------
// What the 3D view learns as the game shows things (the skyline, the pictures and sizes of
// the roadside objects of each course; the cars' pictures) is kept in the settings folder,
// so a course looks complete from the start the next time. It comes from the player's ROM:
// it stays on their machine.

#define CACHE_MAGIC 0x54473344u  // "TG3D"
#define CACHE_VERSION 1
#define SIDE_MAGIC 0x45444953u  // "SIDE": the cars' side views, after the cars file's pictures

static void put_pic(FILE* f, const TgV3Pic* p) {
  // The art folder's pictures aren't kept: they are read from there each time.
  int32_t wh[2] = {p->px && !p->replaced ? p->w : 0, p->px && !p->replaced ? p->h : 0};
  fwrite(wh, sizeof(wh), 1, f);
  if(wh[0] > 0) fwrite(p->px, 4, (size_t) wh[0] * wh[1], f);
}

static bool get_pic(FILE* f, TgV3Pic* p) {
  int32_t wh[2];
  if(fread(wh, sizeof(wh), 1, f) != 1 || wh[0] < 0 || wh[1] < 0 || wh[0] > 512 || wh[1] > 512) return false;
  if(wh[0] == 0) return true;
  static uint32_t buf[512 * 512];
  if(fread(buf, 4, (size_t) wh[0] * wh[1], f) != (size_t) wh[0] * wh[1]) return false;
  set_pic(p, wh[0], wh[1], buf, wh[0]);
  return true;
}

static FILE* cache_open(const TgView3D* v, const char* name, const char* mode) {
  if(!v->cache_dir[0]) return NULL;
  char path[1200];
  snprintf(path, sizeof(path), "%s%s", v->cache_dir, name);
  return fopen(path, mode);
}

static void save_course(const TgView3D* v, uint32_t key) {
  char name[64];
  snprintf(name, sizeof(name), "course3d-%08x.bin", key);
  FILE* f = cache_open(v, name, "wb");
  if(f == NULL) return;
  uint32_t head[2] = {CACHE_MAGIC, CACHE_VERSION};
  fwrite(head, sizeof(head), 1, f);
  fwrite(v->sky_px, 4, SKY_W * SKY_H, f);
  fwrite(v->art, sizeof(v->art), 1, f);
  fwrite(v->art_colour, sizeof(v->art_colour), 1, f);
  fwrite(&v->sky_top, sizeof(Hist), 4, f);
  for(int i = 0; i < ART_COUNT; i++) put_pic(f, &v->pics[ART_PIC + i]);
  uint32_t gm = GROUND_MAGIC;  // after the pictures: earlier versions read no further
  fwrite(&gm, 4, 1, f);
  fwrite(v->seg_ground, sizeof(v->seg_ground), 1, f);
  fclose(f);
}

static void load_course(TgView3D* v, uint32_t key) {
  char name[64];
  snprintf(name, sizeof(name), "course3d-%08x.bin", key);
  FILE* f = cache_open(v, name, "rb");
  if(f == NULL) return;
  uint32_t head[2];
  bool ok = fread(head, sizeof(head), 1, f) == 1 && head[0] == CACHE_MAGIC && head[1] == CACHE_VERSION &&
            fread(v->sky_px, 4, SKY_W * SKY_H, f) == SKY_W * SKY_H && fread(v->art, sizeof(v->art), 1, f) == 1 &&
            fread(v->art_colour, sizeof(v->art_colour), 1, f) == 1 && fread(&v->sky_top, sizeof(Hist), 4, f) == 4;
  for(int i = 0; ok && i < ART_COUNT; i++) ok = get_pic(f, &v->pics[ART_PIC + i]);
  uint32_t gm = 0;
  if(!(ok && fread(&gm, 4, 1, f) == 1 && gm == GROUND_MAGIC && fread(v->seg_ground, sizeof(v->seg_ground), 1, f) == 1))
    memset(v->seg_ground, 0, sizeof(v->seg_ground));
  fclose(f);
  if(!ok) {  // a damaged file: start this course afresh
    memset(v->sky_px, 0, SKY_W * SKY_H * 4);
    memset(v->art, 0, sizeof(v->art));
    memset(&v->sky_top, 0, sizeof(Hist) * 4);
    for(int i = ART_PIC; i < ART_PIC + ART_COUNT; i++) v->pics[i].w = 0;
    return;
  }
  // The panorama as drawn.
  bool any = false;
  for(int i = 0; i < SKY_W * SKY_H && !any; i++) any = v->sky_px[i] != 0;
  if(any) sky_picture(v);
}

static void save_cars(const TgView3D* v) {
  FILE* f = cache_open(v, "cars3d.bin", "wb");
  if(f == NULL) return;
  uint32_t head[2] = {CACHE_MAGIC, CACHE_VERSION};
  fwrite(head, sizeof(head), 1, f);
  fwrite(v->car, sizeof(v->car), 1, f);
  fwrite(&v->k, sizeof(v->k), 1, f);
  fwrite(&v->player_min_w, sizeof(int), 1, f);
  fwrite(&v->player_max_w, sizeof(int), 1, f);
  for(int i = 0; i < 64; i++) put_pic(f, &v->pics[CAR_PIC + i]);
  uint32_t sm = SIDE_MAGIC;  // after the pictures: earlier versions read no further
  fwrite(&sm, 4, 1, f);
  for(int i = 0; i < 4; i++) put_pic(f, &v->pics[SIDE_SRC + i]);
  fclose(f);
}

static void load_cars(TgView3D* v) {
  FILE* f = cache_open(v, "cars3d.bin", "rb");
  if(f == NULL) return;
  uint32_t head[2];
  bool ok = fread(head, sizeof(head), 1, f) == 1 && head[0] == CACHE_MAGIC && head[1] == CACHE_VERSION &&
            fread(v->car, sizeof(v->car), 1, f) == 1 && fread(&v->k, sizeof(v->k), 1, f) == 1 &&
            fread(&v->player_min_w, sizeof(int), 1, f) == 1 && fread(&v->player_max_w, sizeof(int), 1, f) == 1;
  for(int i = 0; ok && i < 64; i++) ok = get_pic(f, &v->pics[CAR_PIC + i]);
  uint32_t sm = 0;
  if(ok && fread(&sm, 4, 1, f) == 1 && sm == SIDE_MAGIC)
    for(int i = 0; i < 4; i++)
      if(!get_pic(f, &v->pics[SIDE_SRC + i])) v->pics[SIDE_SRC + i].w = 0;
  fclose(f);
  if(!ok) {
    memset(v->car, 0, sizeof(v->car));
    v->k = 0;
    v->player_min_w = v->player_max_w = 0;
    for(int i = 0; i < 64; i++) v->pics[CAR_PIC + i].w = 0;
  }
}

// --- Replacement pictures (the art folder) ----------------------------------------------

static uint32_t le32(const uint8_t* p) { return (uint32_t) p[0] | p[1] << 8 | p[2] << 16 | (uint32_t) p[3] << 24; }

// An uncompressed 24- or 32-bit BMP as ARGB (magenta see-through in 24-bit). NULL if not.
static uint32_t* read_bmp(const char* path, int* w, int* h) {
  FILE* f = fopen(path, "rb");
  if(f == NULL) return NULL;
  uint8_t hd[54];
  uint32_t* out = NULL;
  if(fread(hd, 1, 54, f) == 54 && hd[0] == 'B' && hd[1] == 'M') {
    uint32_t off = le32(hd + 10), comp = le32(hd + 30);
    int32_t bw = (int32_t) le32(hd + 18), bh = (int32_t) le32(hd + 22);
    int bpp = hd[28] | hd[29] << 8;
    bool topDown = bh < 0;
    if(bh < 0) bh = -bh;
    if(bw > 0 && bh > 0 && bw <= 4096 && bh <= 4096 && (bpp == 24 || bpp == 32) && (comp == 0 || comp == 3)) {
      int stride = (bw * (bpp / 8) + 3) & ~3;
      uint8_t* row = malloc((size_t) stride);
      out = malloc((size_t) bw * bh * 4);
      if(row && out && fseek(f, (long) off, SEEK_SET) == 0) {
        for(int y = 0; y < bh; y++) {
          if(fread(row, 1, (size_t) stride, f) != (size_t) stride) {
            free(out);
            out = NULL;
            break;
          }
          uint32_t* d = &out[(topDown ? y : bh - 1 - y) * bw];
          for(int x = 0; x < bw; x++) {
            const uint8_t* p = &row[x * (bpp / 8)];
            uint32_t a = bpp == 32 ? p[3] : 255;
            if(bpp == 24 && p[2] == 255 && p[1] == 0 && p[0] == 255) a = 0;
            d[x] = a >= 128 ? 0xFF000000u | (uint32_t) p[2] << 16 | (uint32_t) p[1] << 8 | p[0] : 0;
          }
        }
        *w = bw;
        *h = bh;
      } else {
        free(out);
        out = NULL;
      }
      free(row);
    }
  }
  fclose(f);
  return out;
}

static bool write_bmp(const char* path, const TgV3Pic* p) {
  FILE* f = fopen(path, "wb");
  if(f == NULL) return false;
  uint32_t size = 54 + (uint32_t) p->w * p->h * 4;
  uint8_t hd[54] = {'B', 'M'};
  uint32_t vals[][2] = {{2, size}, {10, 54}, {14, 40}, {18, (uint32_t) p->w}, {22, (uint32_t) -p->h}, {34, size - 54}};
  for(size_t i = 0; i < sizeof(vals) / sizeof(vals[0]); i++)
    for(int k = 0; k < 4; k++) hd[vals[i][0] + k] = (uint8_t) (vals[i][1] >> (8 * k));
  hd[26] = 1;   // planes
  hd[28] = 32;  // bits per pixel (top-down rows, alpha in the fourth byte)
  fwrite(hd, 1, 54, f);
  for(int i = 0; i < p->w * p->h; i++) {
    uint32_t c = p->px[i];
    uint8_t bgra[4] = {(uint8_t) c, (uint8_t) (c >> 8), (uint8_t) (c >> 16), (uint8_t) (c >> 24)};
    fwrite(bgra, 1, 4, f);
  }
  fclose(f);
  return true;
}

// The art folder's file names, listed once as it is set: a new course looks for some 300
// replacement pictures, nearly all absent, and asking the disk for each (inside the race's
// first frame) was a stall on a slow disk. -1: not listed (every name is tried).
static char (*g_art_names)[64];
static int g_art_n = -1;

static int cmp_name(const void* a, const void* b) { return strcmp((const char*) a, (const char*) b); }

static void list_art_dir(const char* dir) {
  free(g_art_names);
  g_art_names = NULL;
  g_art_n = -1;
  if(!dir[0]) return;
  int cap = 0, n = 0;
  char (*names)[64] = NULL;
  bool ok = false;
#ifdef _WIN32
  char pat[1200];
  snprintf(pat, sizeof(pat), "%s*", dir);
  struct _finddata_t fd;
  intptr_t h = _findfirst(pat, &fd);
  if(h != -1) {
    ok = true;
    do {
      const char* nm = fd.name;
#else
  DIR* d = opendir(dir);
  if(d) {
    ok = true;
    for(struct dirent* e; (e = readdir(d)) != NULL;) {
      const char* nm = e->d_name;
#endif
      if(strlen(nm) >= 64) continue;
      if(n == cap) {
        cap = cap ? cap * 2 : 64;
        char (*more)[64] = realloc(names, (size_t) cap * 64);
        if(!more) {
          ok = false;
          break;
        }
        names = more;
      }
      snprintf(names[n++], 64, "%s", nm);
#ifdef _WIN32
    } while(_findnext(h, &fd) == 0);
    _findclose(h);
  }
#else
    }
    closedir(d);
  }
#endif
  if(!ok) {  // couldn't list it: every name is tried, as before
    free(names);
    return;
  }
  if(n) qsort(names, (size_t) n, 64, cmp_name);
  g_art_names = names;
  g_art_n = n;
}

static bool art_has(const char* name) {
  if(g_art_n < 0) return true;
#ifdef _WIN32
  for(int i = 0; i < g_art_n; i++)  // Windows names: any case
    if(_stricmp(g_art_names[i], name) == 0) return true;
  return false;
#else
  char key[64];
  snprintf(key, sizeof(key), "%s", name);
  return g_art_n > 0 && bsearch(key, g_art_names, (size_t) g_art_n, 64, cmp_name) != NULL;
#endif
}

static void replace_pic(TgView3D* v, TgV3Pic* p, const char* name) {
  if(!v->art_dir[0] || !art_has(name)) return;
  char path[1200];
  snprintf(path, sizeof(path), "%s%s", v->art_dir, name);
  int w, h;
  uint32_t* px = read_bmp(path, &w, &h);
  if(px == NULL) return;
  p->replaced = false;
  set_pic(p, w, h, px, w);
  p->replaced = true;
  p->smooth = true;  // drawn filtered: they are the detailed ones
  free(px);
}

// The menus' upscaled photographs (menubg-KEY.bmp), read once when the art folder is set:
// read on the first menu frame, a 3.6 MB picture froze the step from the title to the menus
// for 0.5-0.9 s (6 s on a cold disk; QA-025). At start-up it is part of the KEMCO splash.
static struct {
  uint32_t key;
  int w, h;
  uint32_t* px;
} g_menubg[8];
static int g_menubg_n;

static void preload_menu_photos(const TgView3D* v) {
  for(int i = 0; i < g_menubg_n; i++) free(g_menubg[i].px);
  g_menubg_n = 0;
  if(!v->art_dir[0] || g_art_n <= 0) return;
  for(int i = 0; i < g_art_n && g_menubg_n < 8; i++) {
    unsigned key;
    char tail[8];
    if(sscanf(g_art_names[i], "menubg-%8x.%3s", &key, tail) != 2 || strcmp(tail, "bmp") != 0) continue;
    char path[1200];
    snprintf(path, sizeof(path), "%s%s", v->art_dir, g_art_names[i]);
    int w, h;
    uint32_t* px = read_bmp(path, &w, &h);
    if(px) g_menubg[g_menubg_n].key = key, g_menubg[g_menubg_n].w = w, g_menubg[g_menubg_n].h = h, g_menubg[g_menubg_n++].px = px;
  }
}

// A menu photograph's upscaled picture: from memory when it was read at start-up.
static void replace_menu_photo(TgView3D* v, TgV3Pic* p, uint32_t key, const char* name) {
  for(int i = 0; i < g_menubg_n; i++)
    if(g_menubg[i].key == key) {
      p->replaced = false;
      set_pic(p, g_menubg[i].w, g_menubg[i].h, g_menubg[i].px, g_menubg[i].w);
      p->replaced = true;
      p->smooth = true;
      return;
    }
  if(g_art_n < 0) replace_pic(v, p, name);  // the folder couldn't be listed: as before
}

// The art folder's pictures for the course being raced (and the cars).
static void load_replacements(TgView3D* v) {
  char name[96];
  for(int i = 0; i < ART_COUNT; i++) {
    snprintf(name, sizeof(name), "art-%08x-%d.bmp", v->track.key, i);
    replace_pic(v, &v->pics[ART_PIC + i], name);
  }
  snprintf(name, sizeof(name), "sky-%08x.bmp", v->track.key);
  replace_pic(v, &v->pics[SKY_PIC], name);
  for(int i = 0; i < 32; i++) {
    snprintf(name, sizeof(name), "car-%d.bmp", i);
    replace_pic(v, &v->pics[REAR_PIC + i], name);
  }
}

void tg_view3d_set_art_dir(TgView3D* v, const char* dir) {
  snprintf(v->art_dir, sizeof(v->art_dir), "%s", dir ? dir : "");
  size_t n = strlen(v->art_dir);  // a folder given without its separator: add one
  if(n && n + 1 < sizeof(v->art_dir) && v->art_dir[n - 1] != '/' && v->art_dir[n - 1] != '\\') v->art_dir[n] = '/', v->art_dir[n + 1] = 0;
  list_art_dir(v->art_dir);
  load_replacements(v);
  preload_car_meshes(v);
  preload_menu_photos(v);
}

int tg_view3d_export(TgView3D* v, const char* dir) {
  char path[1200];
  int n = 0;
  for(int i = 0; i < ART_COUNT; i++) {
    if(v->pics[ART_PIC + i].w == 0) continue;
    snprintf(path, sizeof(path), "%sart-%08x-%d.bmp", dir, v->track.key, i);
    n += write_bmp(path, &v->pics[ART_PIC + i]);
  }
  if(v->pics[SKY_PIC].w) {
    snprintf(path, sizeof(path), "%ssky-%08x.bmp", dir, v->track.key);
    n += write_bmp(path, &v->pics[SKY_PIC]);
  }
  for(int i = 0; i < 32; i++) {
    if(v->pics[REAR_PIC + i].w == 0) continue;
    snprintf(path, sizeof(path), "%scar-%d.bmp", dir, i);
    n += write_bmp(path, &v->pics[REAR_PIC + i]);
  }
  for(int i = 0; i < 4; i++) {
    if(v->pics[SIDE_SRC + i].w == 0) continue;
    snprintf(path, sizeof(path), "%sside-%d.bmp", dir, i);
    n += write_bmp(path, &v->pics[SIDE_SRC + i]);
  }
  for(int i = 0; i < 8; i++) {
    if(!v->menu_bg_seen[i]) continue;
    TgV3Pic tmp = {256, 224, v->menu_bg_px[i], 1, false, false};
    snprintf(path, sizeof(path), "%smenubg-%08x.bmp", dir, v->menu_bg_seen[i]);
    n += write_bmp(path, &tmp);
  }
  if(getenv("TG_EXPORT_HUD") && v->pics[HUD_PIC].w) {  // for working on the HUD's zones
    snprintf(path, sizeof(path), "%shud.bmp", dir);
    n += write_bmp(path, &v->pics[HUD_PIC]);
  }
  return n;
}

void tg_view3d_set_cache_dir(TgView3D* v, const char* dir) {
  snprintf(v->cache_dir, sizeof(v->cache_dir), "%s", dir ? dir : "");
  load_cars(v);
}

// Whether any car is somewhere else: a race update, also when the race loop runs as the
// original's own code (tg_port_race_updates counts the C loop's updates).
static bool moved(const TgScene* a, const TgScene* b) {
  if(a->car_count != b->car_count) return true;
  for(int i = 0; i < a->car_count; i++)
    if(a->cars[i].id != b->cars[i].id || a->cars[i].track_pos != b->cars[i].track_pos || a->cars[i].side != b->cars[i].side)
      return true;
  return false;
}

static void hud_of(const uint8_t* m, int pl, TgV3Hud* hud);

// A menu screen: its text read off BG1 (the small font, tiles 1-36; the big font, tiles
// $80-$FF two by two, as MenuDrawBigText lays them out from $0D:C5BB), its frame (tiles
// $2B-$30), its pictures (tiles $100 and up) and its sprites but the cursor.
// BG1's map entry seen at screen tile (tx, ty), with its scroll (the results page scrolls
// down its 32x64 map to the standings) and size. Screen line y shows BG line y + 1 + vs.
static uint16_t bg1_at(const TgSpriteState* s, int tx, int ty) {
  int X = (tx * 8 + s->bg1_hs) & (s->bg1_wide ? 511 : 255), Y = (ty * 8 + 1 + s->bg1_vs) & (s->bg1_high ? 511 : 255);
  int cx = X >> 3, cy = Y >> 3;
  int m = s->bg1_map + (cy & 31) * 32 + (cx & 31) + (cx >= 32 ? 0x400 : 0) + (cy >= 32 ? (s->bg1_wide ? 0x800 : 0x400) : 0);
  return s->vram[m & 0x7FFF];
}

static bool capture_menu(TgView3D* v, const TgSpriteState* s) {
  // Every menu draws its text with the font at BG1 tile base $1000; while a race loads BG1
  // holds race tiles at $0000, whose numbers 1-36 would read as letters ("KLM", "NO").
  if(s->bg1_tiles != 0x1000) return false;
  static char big[256];
  static bool bigReady;
  if(!bigReady) {
    memset(big, 0, sizeof(big));
    static const char order[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.,'!?-:/\"";  // letters win shared tiles
    for(const char* o = order; *o; o++) {
      int c = *o, i = c < 0x3B ? c - 0x15 : c - 0x40;
      int t = (tg_rom(0x0DC5BB + i)[0] + 0x80) & 0xFF;
      if(t >= 0x80 && !big[t]) big[t] = (char) c;
    }
    big[(tg_rom(0x0DC5BB)[0] + 0x80) & 0xFF] = 0;  // the blank: no character
    bigReady = true;
  }
  char grid[28][33];
  int kind[28][32];  // 0 none, 1 small, 2 big (top left), 3 frame, 4 picture, 5 big (other part)
  int box[4] = {99, 99, -1, -1}, pics = 0;
  memset(kind, 0, sizeof(kind));
  for(int ty = 0; ty < 28; ty++) {
    for(int tx = 0; tx < 32; tx++) {
      int t = bg1_at(s, tx, ty) & 0x3FF;
      char c = ' ';
      if(t >= 1 && t <= 26) c = (char) ('A' + t - 1), kind[ty][tx] = 1;
      else if(t >= 27 && t <= 36) c = (char) ('0' + t - 27), kind[ty][tx] = 1;
      else if(t >= 0x2B && t <= 0x30) {
        kind[ty][tx] = 3;
        box[0] = tx < box[0] ? tx : box[0], box[1] = ty < box[1] ? ty : box[1];
        box[2] = tx > box[2] ? tx : box[2], box[3] = ty > box[3] ? ty : box[3];
      } else if(t >= 0x80 && t < 0x100) {
        if(!(t & 1) && !(t & 0x10) && big[t]) c = big[t], kind[ty][tx] = 2;
        else kind[ty][tx] = 5;
      } else if(t >= 0x100) {
        kind[ty][tx] = 4, pics++;
      }
      grid[ty][tx] = c;
    }
    grid[ty][32] = 0;
  }
  // Lines: runs of characters on a row, a gap of up to one character (two tiles in the big
  // font) inside them.
  int n = 0, letters = 0;
  for(int ty = 0; ty < 28 && n < 48; ty++)
    for(int tx = 0; tx < 32 && n < 48;) {
      if(kind[ty][tx] != 1 && kind[ty][tx] != 2) {
        tx++;
        continue;
      }
      int bigRun = kind[ty][tx] == 2, step = bigRun ? 2 : 1, k = 0, gap = 0, x = tx;
      char* o = v->menu_item[n].text;
      v->menu_item[n].col = tx, v->menu_item[n].row = ty, v->menu_item[n].big = bigRun;
      while(x < 32 && k < 32) {
        if(kind[ty][x] == (bigRun ? 2 : 1)) {
          while(gap > 0 && k < 32) o[k++] = ' ', gap--;
          o[k++] = grid[ty][x];
          if(isalpha((unsigned char) grid[ty][x])) letters++;
          x += step;
        } else if(gap < 1 && (kind[ty][x] == 0 || (bigRun && kind[ty][x] == 5))) {
          gap++;
          x += step;
        } else {
          break;
        }
      }
      o[k] = 0;
      tx = x;
      // A row of single letters with gaps (the name entry's letters): each one on its own.
      int kk = (int) strlen(o);
      // The keyboard's last row ends in END: its keys before it (Y Z and the two arrows, which
      // read as 3 and 4) each on their own too.
      const char* endp = strstr(o, "END");
      if(endp && endp - o >= 2 && endp[3] == 0) {
        int ke = (int) (endp - o);
        bool keys = true;
        for(int q = 0; q < ke && keys; q++) keys = (q & 1) ? o[q] == ' ' : o[q] != ' ';
        if(keys) {
          int col0 = v->menu_item[n].col, row0 = v->menu_item[n].row;
          char tmp[33];
          strcpy(tmp, o);
          for(int q = 0; q <= ke && n < 48; q += 2, n++) {
            char* t = v->menu_item[n].text;
            if(q == ke) strcpy(t, "END");
            else t[0] = tmp[q] == '3' ? '<' : tmp[q] == '4' ? '>' : tmp[q], t[1] = 0;
            v->menu_item[n].col = col0 + q * step, v->menu_item[n].row = row0, v->menu_item[n].big = bigRun;
          }
          continue;
        }
      }
      bool grid1 = kk >= 5;
      for(int q = 0; q < kk && grid1; q++) grid1 = (q & 1) ? o[q] == ' ' : o[q] != ' ';
      if(grid1) {
        int col0 = v->menu_item[n].col, row0 = v->menu_item[n].row;
        char tmp[33];
        strcpy(tmp, o);
        for(int q = 0; q < kk && n < 48; q += 2, n++) {
          v->menu_item[n].text[0] = tmp[q], v->menu_item[n].text[1] = 0;
          v->menu_item[n].col = col0 + q * step, v->menu_item[n].row = row0, v->menu_item[n].big = bigRun;
        }
        continue;
      }
      n++;
    }
  if(n < 2 || letters < 6) return false;
  for(int i = 0; i < n; i++)  // a font being loaded (the alphabet in a row), not a menu
    if(strstr(v->menu_item[i].text, "ABCDEFG") || strstr(v->menu_item[i].text, "HIJKLMN")) return false;
  v->menu_n = n;
  v->menu_scroll = (s->bg1_vs + 1) & (s->bg1_high ? 511 : 255);
  v->menu_tiles = s->bg1_tiles;
  memcpy(v->menu_box, box, sizeof(box));
  // The sprites: flat ones (mostly one colour: the cursor's pill, circle, panel) mark the
  // line under the cursor and aren't drawn; the others (cars, flags, pointers) are kept.
  static Spr sp[128];
  static bool flat[128];
  static uint32_t tmp[64 * 64];
  int ns = 0;
  for(int i = 0; i < 128; i++) {
    Spr a = sprite_at(s, i);
    if(!a.size || a.y >= 224) continue;
    memset(tmp, 0, sizeof(tmp));
    draw_sprite(s, &a, tmp, 64, 64, a.x, a.y);
    Hist hh = {0};
    int cnt = 0;
    for(int q = 0; q < 64 * 64; q++)
      if(tmp[q] >> 24) hist_add(&hh, tmp[q], 1), cnt++;
    if(!cnt) continue;
    uint32_t top = hist_top(&hh, 0, 0);
    float topN = 0;
    for(int q = 0; q < HIST; q++)
      if(hh.c[q] == top) topN = hh.n[q];
    flat[ns] = topN > cnt * 0.72f;
    sp[ns++] = a;
  }
  v->menu_sel = -1;
  float best = 1e9f;
  for(int i = 0; i < ns; i++) {
    if(!flat[i]) continue;
    float cy = sp[i].y + sp[i].size / 2.0f, cx = sp[i].x + sp[i].size / 2.0f;
    for(int k = 0; k < n; k++) {
      if(v->menu_item[k].big && v->menu_item[k].row <= 5) continue;  // the title isn't chosen
      float iy = v->menu_item[k].row * 8 + (v->menu_item[k].big ? 8 : 4), ix = v->menu_item[k].col * 8;
      float iw = strlen(v->menu_item[k].text) * (v->menu_item[k].big ? 16 : 8);
      float dx = cx < ix ? ix - cx : cx > ix + iw ? cx - ix - iw : 0, dy = fabsf(cy - iy);
      float d = dy * 3 + dx;
      if(d < best) best = d, v->menu_sel = k;
    }
  }
  if(best > 120) v->menu_sel = -1;  // a cursor far from any line (on a picture)
  v->menu_cur[0] = v->menu_cur[1] = -1;
  {
    float sx = 0, sy = 0, sn = 0;
    for(int i = 0; i < ns; i++)
      if(flat[i]) sx += (sp[i].x + sp[i].size / 2.0f) * sp[i].size, sy += (sp[i].y + sp[i].size / 2.0f) * sp[i].size, sn += sp[i].size;
    v->menu_cx = sn ? sx / sn : -1, v->menu_cy = sn ? sy / sn : -1;
  }
  // The picture layer: BG1's pictures, and the sprites that aren't the cursor.
  static uint32_t buf[256 * 224];
  memset(buf, 0, sizeof(buf));
  bool any = false;
  if(pics)
    for(int ty = 0; ty < 28; ty++)
      for(int tx = 0; tx < 32; tx++) {
        if(kind[ty][tx] != 4) continue;
        uint16_t e = bg1_at(s, tx, ty);
        int tile = e & 0x3FF, pal = (e >> 10) & 7;
        bool hf = e & 0x4000, vf = e & 0x8000;
        for(int r = 0; r < 8; r++) {
          int rr = vf ? 7 - r : r;
          uint16_t p1 = s->vram[(s->bg1_tiles + tile * 16 + rr) & 0x7FFF], p2 = s->vram[(s->bg1_tiles + tile * 16 + 8 + rr) & 0x7FFF];
          for(int c = 0; c < 8; c++) {
            int sh = hf ? c : 7 - c;
            int k = (p1 >> sh & 1) | (p1 >> (8 + sh) & 1) << 1 | (p2 >> sh & 1) << 2 | (p2 >> (8 + sh) & 1) << 3;
            if(k) buf[(ty * 8 + r) * 256 + tx * 8 + c] = snes_colour(s->cgram[pal * 16 + k]), any = true;
          }
        }
      }
  // A flat sprite behind a picture (the gear select's) is the cursor there: its picture is
  // outlined instead (menu_cur: the cursor's box, in screen pixels of the game).
  {
    int x0 = 999, y0 = 999, x1 = -1, y1 = -1;
    for(int i = 0; i < ns; i++)
      if(flat[i] && v->menu_sel < 0) {
        x0 = sp[i].x < x0 ? sp[i].x : x0, y0 = sp[i].y < y0 ? sp[i].y : y0;
        x1 = sp[i].x + sp[i].size > x1 ? sp[i].x + sp[i].size : x1, y1 = sp[i].y + sp[i].size > y1 ? sp[i].y + sp[i].size : y1;
      }
    if(x1 > x0) v->menu_cur[0] = x0, v->menu_cur[1] = y0, v->menu_cur[2] = x1, v->menu_cur[3] = y1;
  }
  for(int i = ns - 1; i >= 0; i--) {
    if(flat[i]) continue;
    draw_sprite(s, &sp[i], buf, 256, 224, 0, 0);
    any = true;
  }
  v->menu_pics = any;
  // The photograph behind (BG2, 4 bits a pixel): kept softened, a little blurred, for the
  // backdrop.
  {
    static uint32_t bg[256 * 224], tmp2[128 * 112];
    int lit = 0;
    for(int y = 0; y < 224; y++)
      for(int x = 0; x < 256; x++) {
        int X = (x + s->bg2_hs) & (s->bg2_wide ? 511 : 255), Y = (y + s->bg2_vs) & (s->bg2_high ? 511 : 255);
        int tx = X >> 3, ty = Y >> 3;
        int m = s->bg2_map + (ty & 31) * 32 + (tx & 31) + (tx >= 32 ? 0x400 : 0) + (ty >= 32 ? (s->bg2_wide ? 0x800 : 0x400) : 0);
        uint16_t e = s->vram[m & 0x7FFF];
        int tile = e & 0x3FF, pal = (e >> 10) & 7, r = Y & 7, c = X & 7;
        if(e & 0x8000) r = 7 - r;
        if(e & 0x4000) c = 7 - c;
        uint16_t p1 = s->vram[(s->bg2_tiles + tile * 16 + r) & 0x7FFF], p2 = s->vram[(s->bg2_tiles + tile * 16 + 8 + r) & 0x7FFF];
        int sh = 7 - c;
        int k = (p1 >> sh & 1) | (p1 >> (8 + sh) & 1) << 1 | (p2 >> sh & 1) << 2 | (p2 >> (8 + sh) & 1) << 3;
        bg[y * 256 + x] = k ? snes_colour(s->cgram[pal * 16 + k]) : 0xFF000000u;
        lit += k != 0;
      }
    if(lit > 256 * 224 / 3) {
      // Which photograph: a hash of it. An upscaled one in the art folder replaces it.
      // (By its tiles, not its colours: the menus fade by their colours.)
      uint32_t key = 2166136261u;
      for(int i = 0; i < 32 * 28; i++) key = (key ^ s->vram[(s->bg2_map + i) & 0x7FFF]) * 16777619u;
      key = (key ^ s->bg2_hs) * 16777619u, key = (key ^ s->bg2_vs) * 16777619u;
      {  // the brightest sight of it is the one exported
        double sum = 0;
        for(int i = 0; i < 256 * 224; i += 5) sum += (bg[i] >> 16 & 255) + (bg[i] >> 8 & 255) + (bg[i] & 255);
        for(int i = 0; i < 8; i++)
          if(v->menu_bg_seen[i] == key && sum > v->menu_bg_light[i]) {
            v->menu_bg_light[i] = sum;
            memcpy(v->menu_bg_px[i], bg, sizeof(bg));
          }
      }
      if(key != v->menu_bg_key) {
        v->menu_bg_key = key;
        v->menu_bg_hi = false;
        char name[64];
        snprintf(name, sizeof(name), "menubg-%08x.bmp", key);
        TgV3Pic* hp = &v->pics[MENU_BG];
        hp->replaced = false;
        replace_menu_photo(v, hp, key, name);
        v->menu_bg_hi = hp->replaced;
        if(!v->menu_bg_hi) {  // remember it, for --export-art (to be upscaled)
          int slot = -1;
          for(int i = 0; i < 8; i++)
            if(v->menu_bg_seen[i] == key || (slot < 0 && !v->menu_bg_seen[i])) slot = slot < 0 ? i : slot;
          if(slot >= 0 && v->menu_bg_seen[slot] != key) {
            v->menu_bg_seen[slot] = key;
            memcpy(v->menu_bg_px[slot], bg, sizeof(bg));
          }
        }
      }
      v->menu_has_bg = true;
      if(v->menu_bg_hi) goto bg_done;
      // Half size, averaged (the blur), then into the picture at that size.
      for(int y = 0; y < 112; y++)
        for(int x = 0; x < 128; x++) {
          int R = 0, G = 0, B2 = 0;
          for(int q = 0; q < 4; q++) {
            uint32_t c = bg[(y * 2 + (q >> 1)) * 256 + x * 2 + (q & 1)];
            R += c >> 16 & 255, G += c >> 8 & 255, B2 += c & 255;
          }
          tmp2[y * 128 + x] = 0xFF000000u | (uint32_t) (R / 4) << 16 | (uint32_t) (G / 4) << 8 | (uint32_t) (B2 / 4);
        }
      set_pic(&v->pics[MENU_BG], 128, 112, tmp2, 128);
      v->pics[MENU_BG].smooth = true;
    bg_done:;
    } else {
      v->menu_has_bg = false;
    }
  }
  {
    int x0 = 256, y0 = 224, x1 = -1, y1 = -1;
    for(int y = 0; y < 224; y++)
      for(int x = 0; x < 256; x++)
        if(buf[y * 256 + x] >> 24) x0 = x < x0 ? x : x0, x1 = x > x1 ? x : x1, y0 = y < y0 ? y : y0, y1 = y > y1 ? y : y1;
    v->menu_pbox[0] = x0, v->menu_pbox[1] = y0, v->menu_pbox[2] = x1 + 1, v->menu_pbox[3] = y1 + 1;
  }
  if(any) {
    uint32_t before = v->pics[MENU_PIC].version;
    set_pic(&v->pics[MENU_PIC], 256, 224, buf, 256);
    if(v->pics[MENU_PIC].version != before || !v->pics[MENU_HI].w) upscale4(&v->pics[MENU_HI], &v->pics[MENU_PIC]);
  }
  // Does this screen lead straight into a race? OPTIONS with PLAY GAME under the cursor, or
  // the next race's preview.
  {
    bool toRace = v->menu_sel >= 0 && strstr(v->menu_item[v->menu_sel].text, "PLAY GAME");
    for(int i = 0; i < n; i++) toRace |= strstr(v->menu_item[i].text, "LOCATION") || strstr(v->menu_item[i].text, "LAP RECORD");
    v->menu_to_race = toRace;
  }
  return true;
}

// The classic title: the original's logo read off its picture, frame by frame (its pieces
// fly in, then it stays). Its parts by colour: the orange-to-yellow face, the purple rim
// round it, and the bright yellow edge pixels on the face; the painted drop under it (black,
// pale purple lines) is left out, the 3D extrusion takes its place. Nothing is kept beyond
// the title.
enum { TL_NONE, TL_FACE, TL_RIM, TL_HI };

static int tl_lum(uint32_t c) { return (int) (c >> 16 & 255) * 3 + (int) (c >> 8 & 255) * 6 + (int) (c & 255); }

// Each face pixel's colour averaged over the face pixels round it (radius 4), so the
// original's dithered gradient becomes a smooth one.
static void tl_smooth(TgView3D* v, const uint32_t* raw, bool skip_hi) {
  static uint32_t sum[225][257][4];  // a summed-area table of r, g, b and the count
  for(int y = 0; y <= 224; y++)
    for(int x = 0; x <= 256; x++) {
      if(!x || !y) {
        memset(sum[y][x], 0, sizeof(sum[y][x]));
        continue;
      }
      uint8_t k = v->tl_cls[y - 1][x - 1];
      bool in = k == TL_FACE || (!skip_hi && k == TL_HI);
      uint32_t c = raw[(y - 1) * 256 + x - 1];
      uint32_t add[4] = {in ? c >> 16 & 255 : 0, in ? c >> 8 & 255 : 0, in ? c & 255 : 0, in};
      for(int i = 0; i < 4; i++) sum[y][x][i] = add[i] + sum[y - 1][x][i] + sum[y][x - 1][i] - sum[y - 1][x - 1][i];
    }
  for(int y = 0; y < 224; y++)
    for(int x = 0; x < 256; x++) {
      if(v->tl_cls[y][x] != TL_FACE && v->tl_cls[y][x] != TL_HI) continue;
      int x0 = x - 4 < 0 ? 0 : x - 4, x1 = x + 5 > 256 ? 256 : x + 5, y0 = y - 4 < 0 ? 0 : y - 4, y1 = y + 5 > 224 ? 224 : y + 5;
      uint32_t s[4];
      for(int i = 0; i < 4; i++) s[i] = sum[y1][x1][i] - sum[y0][x1][i] - sum[y1][x0][i] + sum[y0][x0][i];
      if(!s[3]) {
        v->tl_col[y][x] = raw[y * 256 + x] | 0xFF000000u;
        continue;
      }
      v->tl_col[y][x] = 0xFF000000u | (s[0] / s[3]) << 16 | (s[1] / s[3]) << 8 | (s[2] / s[3]);
    }
}

static void capture_title_logo(TgView3D* v, const uint32_t* pixels) {
  static uint32_t raw[224 * 256];
  static uint8_t face[224][256], rim[224][256];
  int n = 0;
  uint32_t rr = 0, rg = 0, rb = 0, rn = 0;
  for(int y = 0; y < 224; y++)
    for(int x = 0; x < 256; x++) {
      uint32_t c = px_at(pixels, x, y);
      raw[y * 256 + x] = c;
      int r = c >> 16 & 255, g = c >> 8 & 255, b = c & 255;
      face[y][x] = r >= 0x50 && g >= r / 8 && r > b + r / 4 && g < r + 8;  // orange to yellow (the sky has no red)
      rim[y][x] = r >= 0x30 && b > r + r / 3 && r > g + g / 4;  // the purple (not the pale purple, not the sky)
    }
  // (not a lone pixel: the licence lines have a few red ones)
  for(int y = 0; y < 224; y++)
    for(int x = 0; x < 256; x++) {
      if(!face[y][x]) continue;
      int nb = 0;
      for(int dy = -1; dy <= 1; dy++)
        for(int dx = -1; dx <= 1; dx++) nb += y + dy >= 0 && y + dy < 224 && x + dx >= 0 && x + dx < 256 && (dx || dy) && (face[y + dy][x + dx] & 1);
      if(nb < 2) face[y][x] = 2;  // (2: dropped, still counted as a neighbour in this pass)
    }
  for(int y = 0; y < 224; y++)
    for(int x = 0; x < 256; x++) {
      if(face[y][x] == 2) face[y][x] = 0;
      n += face[y][x];
    }
  // The text under the logo (white): PRESS START blinks, the licence lines come with the logo.
  int press = 0, lic = 0;
  for(int y = 160; y < 169; y++)
    for(int x = 84; x < 182; x++) press += (int) ((raw[y * 256 + x] & 0xC0C0C0) == 0xC0C0C0);
  for(int y = 180; y < 189; y++)
    for(int x = 50; x < 220; x++) lic += (int) ((raw[y * 256 + x] & 0xC0C0C0) == 0xC0C0C0);
  v->tl_press = press > 60;
  v->tl_lic = lic > 150;
  if(v->tl_lic) v->tl_lic_frames++;
  // The credit lines themselves (lines 174-221): their white pixels and their box.
  if(v->tl_lic) {
    int box[4] = {256, 48, -1, -1};
    for(int y = 0; y < 48; y++)
      for(int x = 0; x < 256; x++) {
        bool on = (raw[(174 + y) * 256 + x] & 0xC0C0C0) == 0xC0C0C0;
        v->tl_credit[y][x] = on;
        if(on) box[0] = x < box[0] ? x : box[0], box[1] = y < box[1] ? y : box[1], box[2] = x > box[2] ? x : box[2], box[3] = y > box[3] ? y : box[3];
      }
    memcpy(v->tl_credit_box, box, sizeof(box));
  }
  // The rim: purple within two pixels of the face (the drop's purple lines are further).
  for(int y = 0; y < 224; y++)
    for(int x = 0; x < 256; x++) {
      uint8_t k = TL_NONE;
      if(face[y][x]) {
        k = TL_FACE;
      } else if(rim[y][x]) {
        for(int dy = -2; dy <= 2 && !k; dy++)
          for(int dx = -2; dx <= 2; dx++) {
            int yy = y + dy, xx = x + dx;
            if(yy >= 0 && yy < 224 && xx >= 0 && xx < 256 && face[yy][xx]) {
              k = TL_RIM;
              break;
            }
          }
        if(k) rr += raw[y * 256 + x] >> 16 & 255, rg += raw[y * 256 + x] >> 8 & 255, rb += raw[y * 256 + x] & 255, rn++;
      }
      v->tl_cls[y][x] = k;
    }
  v->tl_n = n;
  v->tl_rim = rn ? 0xFF000000u | (rr / rn) << 16 | (rg / rn) << 8 | (rb / rn) : 0xFF6B39DE;
  if(!n) return;
  // The bright edge: face pixels well over the smoothed colour, next to the rim.
  tl_smooth(v, raw, false);
  for(int y = 1; y < 223; y++)
    for(int x = 1; x < 255; x++) {
      if(v->tl_cls[y][x] != TL_FACE) continue;
      bool edge = v->tl_cls[y - 1][x] == TL_RIM || v->tl_cls[y + 1][x] == TL_RIM || v->tl_cls[y][x - 1] == TL_RIM || v->tl_cls[y][x + 1] == TL_RIM;
      if(edge && tl_lum(raw[y * 256 + x]) > tl_lum(v->tl_col[y][x]) + 300) v->tl_cls[y][x] = TL_HI;
    }
  tl_smooth(v, raw, true);
}

static void step_bodies(TgView3D* v);
static void step_separation(TgView3D* v, const uint8_t* wram);
static void pit_step(TgView3D* v, const uint8_t* wram);  // view3d_pits.inc
static const PitCar* pit_car(const TgView3D* v, int id);

static bool message_showing(const TgView3D* v, int pl);

// The car palettes' body colours (OBJ palettes 0-7, CGRAM $80 + 16p): the ramp's two
// deepest body shades (entries 11 and 12, as the original's near pictures are mostly
// painted), taken once a race; see pal_paint.
static void lock_rival_paint(TgView3D* v, const TgSpriteState* s) {
  for(int p = 0; p < 8; p++) {
    int sum[3] = {0, 0, 0}, n = 0, lit = 0;
    for(int ci = 8; ci <= 12; ci++) {
      uint32_t c = snes_colour(s->cgram[0x80 + 16 * p + ci]);
      int r = c >> 16 & 255, g = c >> 8 & 255, b = c & 255, mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
      if(mx >= 24) lit++;
      if(ci >= 11 && mx >= 40) sum[0] += r, sum[1] += g, sum[2] += b, n++;
    }
    if(!lit) {
      v->pal_locked &= (uint8_t) ~(1u << p);  // black: between races
      continue;
    }
    if(v->pal_locked >> p & 1) continue;
    if(!n) {  // no deep shades: the ramp's middle
      uint32_t c = snes_colour(s->cgram[0x80 + 16 * p + 10]);
      sum[0] = c >> 16 & 255, sum[1] = c >> 8 & 255, sum[2] = c & 255, n = 1;
    }
    v->pal_paint[p] = 0xFF000000u | (uint32_t) (sum[0] / n) << 16 | (uint32_t) (sum[1] / n) << 8 | (uint32_t) (sum[2] / n);
    v->pal_locked |= (uint8_t) (1u << p);
  }
}

bool tg_view3d_capture(TgView3D* v, const uint8_t* wram, const TgSpriteState* sprites, const uint32_t* pixels) {
  static TgScene now;
  uint32_t key = v->track.key;
  // Out of the races, now and then: is the car select screen up? Its side views are taken.
  // The splash: black all round, a blue-white logo in the middle rows.
  if(v->splash_up > 0) v->splash_up--;
  if(pixels && !v->in_race) {
    bool black = !(px_at(pixels, 6, 6) & 0xFFFFFF) && !(px_at(pixels, 249, 6) & 0xFFFFFF) && !(px_at(pixels, 6, 214) & 0xFFFFFF) &&
                 !(px_at(pixels, 249, 214) & 0xFFFFFF) && !(px_at(pixels, 128, 20) & 0xFFFFFF) && !(px_at(pixels, 128, 180) & 0xFFFFFF);
    int lit = 0, blue = 0;
    for(int y = 90; y < 140; y += 2)
      for(int x = 60; x < 200; x += 3) {
        uint32_t c = px_at(pixels, x, y);
        if(c & 0xFFFFFF) lit++;
        if((c & 255) > (c >> 16 & 255) + 30) blue++;
      }
    if(black && lit > 40 && blue > lit / 3) {
      if(v->splash_up == 0) v->splash_frames = 0;
      v->splash_up = 4;
    }
    if(v->splash_up) v->splash_frames++;
    if(v->splash_up && v->title_style == TG_V3_TITLE_CLASSIC) {
      static uint32_t raw[224 * 256];
      for(int y = 0; y < 224; y++)
        for(int x = 0; x < 256; x++) raw[y * 256 + x] = px_at(pixels, x, y);
      lv_splash_capture(&v->lv_splash, raw);
    }
  }
  // The title screen: its blue sky, dark at the top and pale cyan at the bottom.
  if(v->title_up > 0) v->title_up--;
  if(pixels && !v->in_race) {
    uint32_t a = px_at(pixels, 6, 6), b = px_at(pixels, 249, 6), c = px_at(pixels, 6, 214), d = px_at(pixels, 249, 214);
    // By the colours' proportions, not their brightness: it is the title while it fades in too.
    int aR = a >> 16 & 255, aB = a & 255, cR = c >> 16 & 255, cG = c >> 8 & 255, cB = c & 255;
    bool dark = (a & 0xFFFFFF) == (b & 0xFFFFFF) && aR < 30 && aB < 110 && aB >= aR + 3;
    bool cyanLow = (c & 0xFFFFFF) == (d & 0xFFFFFF) && cB > 20 && cG > cR + cB / 4 && cB > cR + cB / 3 && cB > aB + 12;
    if(dark && cyanLow) {
      if(v->title_up == 0) v->title_frames = 0, v->tl_lic_frames = 0;
      v->title_up = 4;
    }
    if(v->title_up) v->title_frames++;
    if(v->title_up) capture_title_logo(v, pixels);
  }
  v->menu_course = r16(wram, 0x1F06) & 0x1F;
  {
    bool menus = wram[0] == 0x5C && wram[1] == 0x00 && wram[2] == 0x80 && wram[3] == 0x0F;
    v->menu_screen = menus ? wram[0x46] : -1;
    v->menu_cursor = wram[0x5A] & 7;
    v->menu_skill = wram[0x1F10] % 3;
    v->menu_open = wram[0x1F0A + v->menu_skill * 2] & 7;
    v->menu_pw_country = wram[0x5C] & 7;
  }
  for(int pl = 0; pl < 2; pl++) {  // the players' names ($1F86, $1FA3: 8 characters), for the HUD
    int n = 0;
    for(int i = 0; i < 8; i++) {
      uint8_t ch = wram[(pl ? 0x1FA3 : 0x1F86) + i];
      // ('=' pads a name while the results are up: MenuMarkNameEnds' dotted leader)
      g_player_name[pl][i] = ch > 0x20 && ch < 0x7F && ch != '=' ? (char) ch : ' ';
      if(g_player_name[pl][i] != ' ') n = i + 1;
    }
    g_player_name[pl][n] = 0;
    n = 0;
    for(int i = 0; i < 8; i++) {
      uint8_t ch = wram[(pl ? 0x055E : 0x054E) + i];
      g_banner_name[pl][i] = ch > 0x20 && ch < 0x7F ? (char) ch : ' ';
      if(g_banner_name[pl][i] != ' ') n = i + 1;
    }
    g_banner_name[pl][n] = 0;
  }
  v->menu_record = r16(wram, 0x1FB2 + (v->menu_course << 1));
  // The race's results and the championship (for the results and standings pages).
  v->res.two = wram[0x1F04] & 1;
  for(int i = 0; i < 10; i++) v->res.order[i] = wram[0x1F12 + i] % 20;
  for(int d = 0; d < 20; d++) {
    v->res.rank[d] = wram[0x1F62 + d] % 20;
    v->res.pts[d] = wram[0x1F4E + d];
    v->res.time[d] = r16(wram, 0x1F26 + 2 * d);
    char* nm = v->res.name[d];
    if(d == 0 || (d == 1 && v->res.two)) {
      snprintf(nm, 9, "%s", g_player_name[d]);
    } else {
      const uint8_t* rn = tg_rom(0x0DC47B + (uint32_t) ((d == 1 ? 0x12 : d - 2) << 3));
      int n = 0;
      for(int k = 0; k < 8; k++) {
        nm[k] = rn[k] > 0x20 && rn[k] < 0x7F && rn[k] != '=' ? (char) rn[k] : ' ';
        if(nm[k] != ' ') n = k + 1;
      }
      nm[n] = 0;
    }
  }
  bool champPts = false;  // (kept while there are points: the end screen comes after they are cleared)
  for(int d = 0; d < 20; d++) champPts |= v->res.pts[d] != 0;
  if(champPts) {
    memcpy(v->champ_rank, v->res.rank, sizeof(v->champ_rank));
    memcpy(v->champ_pts, v->res.pts, sizeof(v->champ_pts));
    v->champ_course = v->menu_course;
  }
  v->menu_kph = wram[0x1FB0] != 0;
  // Any other menu: its text, cursor and pictures, every frame.
  if(v->menu_up > 0) v->menu_up--;
  if(sprites && !v->in_race && !v->title_up && !v->select_up && capture_menu(v, sprites)) v->menu_up = 3;
  // The championship's end (MenuEndScreen after the last country: "... ON COMPLETING ..."):
  // its own screen, which plays from when it comes up.
  bool champ = false;
  if(v->menu_up > 0 && !v->in_race)
    for(int i = 0; i < v->menu_n; i++) champ |= strstr(v->menu_item[i].text, "COMPLETING") != NULL;
  v->champ_frames = champ ? v->champ_frames + 1 : 0;
  // From a menu that starts a race until the race is drawn, the screens in between (the
  // menu's photograph fading with its text cleared, the black while the race loads) are a
  // loading card, not the original's leftovers (QA-003, QA-010).
  if(v->in_race || v->title_up || v->select_up) v->menu_to_race = false;
  if(v->menu_up > 0 || !v->menu_to_race) {
    v->loading_up = 0;
  } else {
    if(!v->loading_up) v->loading_up = 1, v->loading_frames = 0;
    if(++v->loading_frames > 60 * 12) v->loading_up = 0, v->menu_to_race = false;  // never for long
  }
  // (Every few frames: the modern look draws its own select screen while it is up.)
  if(v->select_up > 0) v->select_up--;
  if(sprites && !v->in_race && ++v->portraits_wait >= 6) {
    v->portraits_wait = 0;
    if(capture_portraits(v, sprites)) v->select_up = 9;
  }
  for(int pl = 0; pl < 2; pl++) {
    hud_of(wram, pl, &v->hudv[pl]);
    uint16_t mb = pl ? 0x012A : 0x0118;
    uint16_t id = r16(wram, mb + 0x0C);
    // Player 2's messages have their own ids: as player 1's.
    if(pl && id != 0xFFFF) {
      if(id >= 0x3F && id <= 0x52) id = (uint16_t) (id - 0x3F);
      else if(id >= 0x53 && id <= 0x5B) id = (uint16_t) (id - 0x53 + 0x14);
      else if(id == 0x5C) id = 0x1D;
      else if(id >= 0x5D && id <= 0x61) id = (uint16_t) (id - 0x5D + 0x1E);
      else if(id == 0x80 || id == 0x81) id = (uint16_t) (id - 2);
      else if(id >= 0x76 && id <= 0x7D) id = (uint16_t) (id - 0x76 + 0x37);  // the name banner
      else id = 0xFFFE;  // a picture: the game's drawing
    }
    v->msg_id[pl] = id;
    v->msg_delay[pl] = r16(wram, mb + 0x08);
    v->msg_blink[pl] = r16(wram, mb + 0x0E);
    v->msg_flags[pl] = r16(wram, mb + 0x10);
    // How long it has been up (a bubble grows in over a few frames, as modern UI does).
    v->msg_age[pl] = id != v->msg_age_id[pl] || !message_showing(v, pl) ? 0 : v->msg_age[pl] < 255 ? v->msg_age[pl] + 1 : 255;
    v->msg_age_id[pl] = id;
  }
  v->ptype[0] = wram[0x1F8F] & 3;
  v->ptype[1] = wram[0x1FAC] & 3;
  v->two = wram[0x1F04] & 1;
  // The attract demo is a 1-player race to the game ($1F04 = 0, so the full-screen view is
  // armed and only the top view's sprites are tagged), but the original shows both demo cars
  // split: the modern look draws both views from the game's state (QA-001). What is learned
  // from the screen still follows $1F04 (the top view's, full screen).
  v->demo = (r16(wram, 0x1FF6) & 0x8000) != 0;
  v->nitro[0] = wram[0x1E6D] & 0x80;
  v->nitro[1] = wram[0x1E6F] & 0x80;
  v->rpm[0] = r16(wram, 0x1E5E);
  v->course = r16(wram, 0x1F06) & 0x1F;
  v->bright = (wram[0x0052] & 0x80) ? 0 : wram[0x0052] & 0x0F;  // the race's INIDISP ($52, irq.c)
  v->bright2 = (wram[0x0065] & 0x80) ? 0 : wram[0x0065] & 0x0F;  // the bottom view's ($65)
  v->pit_sub = 0;  // (the drawn pit stops at this game frame, not run on: tg_view3d_draw_view sets it)
  v->lit_frames = v->bright < 15 ? 0 : v->lit_frames < 1000 ? v->lit_frames + 1 : 1000;
  v->frames++;
  v->rpm[1] = r16(wram, 0x1E60);
  v->pspeed[0] = r16(wram, 0x1E5A), v->pspeed[1] = r16(wram, 0x1E5C);
  v->fuel_word[0] = r16(wram, 0x1E62), v->fuel_word[1] = r16(wram, 0x1E64);
  // The views' sky positions (the parallax's reference): every frame, both views.
  for(int vw = 0; vw < 2; vw++) {
    uint16_t raw = r16(wram, 0x1E96 + 2 * vw);
    if(v->sky_have) v->sky_acc[vw] += (int16_t) (uint16_t) (raw - v->sky_raw[vw]) / 128.0;
    else v->sky_acc[vw] = raw / 128.0;
    v->sky_raw[vw] = raw;
    v->sky_rate[vw] = (int16_t) r16(wram, 0x1E9A + 2 * vw) / 128.0f;
  }
  v->sky_have = true;
  // A pit stop ($1E86 / $1E88 negative) is still the race: the modern look keeps drawing it (the
  // stop on its pit lane, view3d_pits.inc, and its panel) instead of the original's pit scene,
  // which it neither shows nor learns from.
  for(int pl = 0; pl < 2; pl++) {
    // The game fades a view out and in round its own pit scene ($52 / $65, at the stop's start and
    // end): the modern look draws the race through it, so those fades are left out of that view
    // (the stop, and the fade back in after it).
    if(r16(wram, pl ? 0x1E88 : 0x1E86) & 0x8000) v->pit_hold[pl] = 50;
    else if(v->pit_hold[pl] > 0) v->pit_hold[pl]--;
    v->pit[pl] = (pl == 0 || v->two) && (r16(wram, pl ? 0x1E88 : 0x1E86) & 0x8000);
  }
  bool pitting = v->pit[0] || v->pit[1];
  v->in_race = tg_scene_update(&now, &v->track, wram);
  if(!v->in_race) {
    // A race just ended: what it learned is kept now, on the results screens, not in the
    // first frame of the next race (file writes there were a stall at the race's start).
    if(v->have && key) {
      save_course(v, key);
      save_cars(v);
      v->saved_key = key;
    }
    v->have = false;
    v->rig_on[0] = v->rig_on[1] = false;  // the next race's camera starts fresh
    v->pal_locked = 0;  // the next race takes its own paint
    return false;
  }
  if(v->track.key != key) {
    v->pal_locked = 0;
    // A new course: its own skyline, colours and objects, as far as they were learned before.
    if(key && key != v->saved_key) save_course(v, key);
    v->saved_key = 0;
    v->frames_to_save = 60 * 60;  // (just read: kept again a minute on)
    memset(v->sky_px, 0, SKY_W * SKY_H * 4);
    memset(v->sky_seen, 0, SKY_W * SKY_H * 4);
    v->pics[SKY_PIC].w = 0;
    v->pics[SKY_PIC].replaced = v->pics[SKY_PIC].smooth = false;
    memset(v->sky_fade, 0, sizeof(v->sky_fade));
    memset(v->sky_hshow, 0, sizeof(v->sky_hshow));
    memset(v->sky_cshow, 0, sizeof(v->sky_cshow));
    memset(v->sky_tw, 0, sizeof(v->sky_tw));
    memset(v->sky_prof, 0, sizeof(v->sky_prof));
    memset(v->sky_known, 0, sizeof(v->sky_known));
    v->sky_prof_ver = 0;
    v->ridge_set = false;
    v->colours_seen = false;
    v->night_lum = -1;
    v->sky_show[0] = v->sky_show[1] = 0;
    v->landmark_fade = 0;
    v->pics[SKY_SMOOTH].w = 0;
    memset(v->art, 0, sizeof(v->art));
    memset(v->art_side, 0, sizeof(v->art_side));
    memset(v->art_side_n, 0, sizeof(v->art_side_n));
    for(int i = ART_PIC; i < ART_PIC + ART_COUNT; i++) v->pics[i].w = 0, v->pics[i].replaced = v->pics[i].smooth = false;
    memset(&v->sky_top, 0, sizeof(Hist) * 4);
    memset(v->seg_ground, 0, sizeof(v->seg_ground));
    v->ground_now = 0;
    memset(v->ground_show, 0, sizeof(v->ground_show));
    memset(v->seg_show, 0, sizeof(v->seg_show));
    memset(v->seg_pick, 0, sizeof(v->seg_pick));
    memset(v->ground_pick, 0, sizeof(v->ground_pick));
    load_course(v, v->track.key);
    load_replacements(v);
    // What this course's file already knew is there from the start; what is learned now fades in.
    for(int i = 0; i < ART_COUNT; i++) v->art_fade[i] = v->art[i].n > 0 && v->pics[ART_PIC + i].w ? 255 : 0;
  }
  // Now and then, what was learned is kept (a crash would otherwise lose it).
  if(--v->frames_to_save <= 0) {
    v->frames_to_save = 60 * 60;
    save_course(v, v->track.key);
    save_cars(v);
  }
  if(!v->have) {
    v->prev = v->cur = now;
    // (a new race: no room between cars carried over, no knock)
    memset(v->sep_tgt, 0, sizeof(v->sep_tgt)), memset(v->sep_ease, 0, sizeof(v->sep_ease));
    memset(v->sep_last, 0, sizeof(v->sep_last)), memset(v->sep_yaw, 0, sizeof(v->sep_yaw));
    memset(v->sep_slew, 0, sizeof(v->sep_slew));
    memset(v->sep_dir, 0, sizeof(v->sep_dir));
    v->knock_id[0] = v->knock_id[1] = 0;
    memset(v->pitc, 0, sizeof(v->pitc));  // (no pit stop carried over)
    // (the rivals by palette, ranked by their place on the grid: front first, ties by id)
    {
      int len = v->track.tables.segments * TG_TRACK3D_SEG_UNITS;
      memset(v->vivid_rank, 0, sizeof(v->vivid_rank));
      for(int a = 0; a < now.car_count; a++) {
        const TgSceneCar* ca = &now.cars[a];
        float ta = len > 0 && ca->track_pos > len / 2 ? ca->track_pos - len : ca->track_pos;
        int rank = 0;
        for(int b2 = 0; b2 < now.car_count; b2++) {
          const TgSceneCar* cb = &now.cars[b2];
          if(b2 == a || (cb->kind >> 13 & 7) != (ca->kind >> 13 & 7)) continue;
          float tb = len > 0 && cb->track_pos > len / 2 ? cb->track_pos - len : cb->track_pos;
          rank += tb > ta || (tb == ta && cb->id < ca->id);
        }
        v->vivid_rank[ca->id & 31] = (uint8_t) rank;
      }
    }
    v->cam_prev[0] = v->cam_cur[0] = r16(wram, 0x0046);
    v->cam_prev[1] = v->cam_cur[1] = r16(wram, 0x0059);
    v->have = true;
    v->updates = tg_port_race_updates;
    v->since = 0;
  } else if(tg_port_race_updates != v->updates || moved(&v->cur, &now) || r16(wram, 0x0046) != v->cam_cur[0] ||
            r16(wram, 0x0059) != v->cam_cur[1]) {
    // (the game's camera moving counts too: in a replay that runs the original's race code
    // - verify mode - the update count does not tick, and with the cars still on the grid the
    // view kept the fly-in's first place through the countdown, then cut to the grid at GO:
    // QA-032, country_uk 6540)
    // The game drew a new race picture: the cars and its camera are where it shows them.
    v->prev = v->cur;
    v->cur = now;
    // Who is braking: a car slowing (moving less this update than the last) more than
    // coasting would; it shows for a few updates.
    {
      int len = v->track.tables.segments * TG_TRACK3D_SEG_UNITS;
      for(int k = 0; k < v->cur.car_count; k++)
        for(int k2 = 0; k2 < v->prev.car_count; k2++) {
          if(v->prev.cars[k2].id != v->cur.cars[k].id) continue;
          int id = v->cur.cars[k].id & 31;
          float adv = v->cur.cars[k].track_pos - v->prev.cars[k2].track_pos;
          if(len > 0 && adv < -len / 2) adv += len;
          if(len > 0 && adv > len / 2) adv -= len;
          if(adv > 1 && v->car_adv[id] - adv > 0.35f) v->car_brake[id] = 3;
          else if(v->car_brake[id] > 0) v->car_brake[id]--;
          // Where it is heading: its sideways move against its move along the road.
          float sideM = (v->cur.cars[k].side - v->prev.cars[k2].side) / TG_TRACK3D_SIDE_EDGE * v->track.scale.half_width;
          float alongM = adv / TG_TRACK3D_SEG_UNITS * v->track.scale.seg_len;
          // (gently: the game nudges cars sideways in steps; a car keeps to the road's line)
          float yaw = alongM > 0.3f ? atan2f(sideM, alongM) : 0;
          v->car_yaw_to[id] = yaw > 0.18f ? 0.18f : yaw < -0.18f ? -0.18f : yaw;
          v->car_adv[id] = adv;
          // Its speed from how far it moved (the rivals' speed word is not their pace).
          if(adv >= 0 && adv < 0x200) {
            float per = (float) (v->since + 1 > 6 ? 6 : v->since + 1) / 60.0f;
            v->car_adv_ms[id] += (alongM / per - v->car_adv_ms[id]) * 0.25f;
          }
        }
    }
    for(int vw = 0; vw < 2; vw++) v->cam_prev[vw] = v->cam_cur[vw];
    v->cam_cur[0] = r16(wram, 0x0046);
    v->cam_cur[1] = r16(wram, 0x0059);
    v->period = v->since + 1 < 1 ? 1 : v->since + 1 > 6 ? 6 : v->since + 1;
    v->since = 0;
    v->updates = tg_port_race_updates;
  } else {
    v->since++;
  }
  // The cars' pit stops as drawn (view3d_pits.inc), once a game frame.
  pit_step(v, wram);
  // The camera follows the player's side position smoothly (the game's view scrolls part
  // of the way with it).
  for(int vw = 0; vw < 2; vw++)
    if(v->cur.view_car[vw] >= 0) {
      // (the camera stays no further from the car than at the road's edge: off the road, far
      // out on the verge, the car would otherwise slide to the picture's side - the original
      // keeps it framed)
      float cs = v->cur.cars[v->cur.view_car[vw]].side, lag = cs * 0.2f;
      const PitCar* pc = pit_car(v, v->cur.cars[v->cur.view_car[vw]].id);
      if(pc) cs += pc->off[1][1], lag = cs * 0.2f;  // (in its pit stop: behind the car as drawn)
      lag = lag > 0.2f * TG_TRACK3D_SIDE_EDGE ? 0.2f * TG_TRACK3D_SIDE_EDGE : lag < -0.2f * TG_TRACK3D_SIDE_EDGE ? -0.2f * TG_TRACK3D_SIDE_EDGE : lag;
      float side = cs - lag;
      v->cam_side[vw] += (side - v->cam_side[vw]) * 0.2f;
    }
  step_bodies(v);  // the cars' bodies on their springs, once a game frame
  step_separation(v, wram);  // room between the drawn cars, once a game frame
  if(pitting) return true;  // the screen shows the pit lane: nothing to learn from it
  if(sprites) lock_rival_paint(v, sprites);
  if(sprites) capture_objects(v, wram, sprites);
  if(pixels && !v->two) capture_picture(v, wram, pixels, sprites);  // the full-screen view's picture
  if(sprites) capture_hud(v, sprites);
  return true;
}

// --- Drawing ----------------------------------------------------------------------------

typedef struct { float x, y, z; } V3;
static V3 v3(float x, float y, float z) { return (V3) {x, y, z}; }
static V3 vadd(V3 a, V3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static V3 vsub(V3 a, V3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static V3 vmul(V3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static float vdot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static V3 vcross(V3 a, V3 b) { return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static V3 vnorm(V3 a) {
  float l = sqrtf(vdot(a, a));
  return l > 0 ? vmul(a, 1 / l) : a;
}
static V3 tv(TgVec3 a) { return v3(a.x, a.y, a.z); }

typedef struct {
  V3 eye, fwd, right, up;
  float f, cx, cy, w, h;
} Cam;

// A point in camera space: x right, y up, z ahead.
static V3 to_cam(const Cam* c, V3 p) {
  V3 d = vsub(p, c->eye);
  return v3(vdot(d, c->right), vdot(d, c->up), vdot(d, c->fwd));
}

// Triangles are gathered by item (a road segment, a car, a box of a model...), then the
// items are drawn far to near.
typedef struct {
  TgV3Vertex v[3];
  int pic;
  int door;  // screen-door transparency (TgV3Batch.door)
} Tri;

typedef struct {
  float depth;
  int first, count;
} Item;

typedef struct {
  Tri* t;
  int nt, capt;
  Item* it;
  int ni, capi;
  const Cam* cam;
} Gather;

static void begin_item(Gather* g, float depth) {
  if(g->ni == g->capi) {
    int cap = g->capi ? g->capi * 2 : 1024;
    Item* it = cap <= 1000000 ? realloc(g->it, sizeof(Item) * (size_t) cap) : NULL;
    if(it == NULL) return;  // full: what follows joins the last item
    g->it = it;
    g->capi = cap;
  }
  g->it[g->ni++] = (Item) {depth, g->nt, 0};
}

// Everything pushed is this opaque (1: as it is): an object fading in or out (QA-009).
static float g_alpha = 1;
// Everything pushed is seen through by screen door (TgV3Batch.door: of 16 pixels, this many
// left out; 0 whole): a rival hiding the player's car (car_cover). Its solid triangles keep
// solid and are dithered; its see-through ones (shadow, glow) are faded by as much instead.
static int g_door;

static void push_tri(Gather* g, const TgV3Vertex* a, const TgV3Vertex* b, const TgV3Vertex* c, int pic) {
  if(g->ni == 0) return;  // a triangle belongs to an item
  if(g->nt == g->capt) {
    int cap = g->capt ? g->capt * 2 : 8192;
    Tri* nt = cap <= 2000000 ? realloc(g->t, sizeof(Tri) * (size_t) cap) : NULL;
    if(nt == NULL) return;
    g->t = nt;
    g->capt = cap;
  }
  Tri* t = &g->t[g->nt++];
  t->v[0] = *a;
  t->v[1] = *b;
  t->v[2] = *c;
  if(g_alpha < 1)
    for(int k = 0; k < 3; k++) t->v[k].argb = (t->v[k].argb & 0xFFFFFF) | (uint32_t) ((t->v[k].argb >> 24) * g_alpha) << 24;
  t->door = 0;
  if(g_door > 0) {
    if((t->v[0].argb >> 24) == 255 && (t->v[1].argb >> 24) == 255 && (t->v[2].argb >> 24) == 255) t->door = g_door;
    else
      for(int k = 0; k < 3; k++) t->v[k].argb = (t->v[k].argb & 0xFFFFFF) | ((t->v[k].argb >> 24) * (uint32_t) (16 - g_door) / 16) << 24;
  }
  t->pic = pic;
  g->it[g->ni - 1].count++;
}

#define NEAR 0.25f

typedef struct {
  V3 p;  // camera space
  float u, v;
  uint32_t c;  // the colour at this corner (0: the polygon's)
} CV;

static int glyph_of(int face, char ch);
static float ftext_w(const char* str, float size, int face, float track);
static void font_ready(TgView3D* v);
static void wtext(Gather* g, const char* str, V3 at, V3 rx, V3 dn, float size, uint32_t col, int face, float track);

static uint32_t lerp_colour(uint32_t a, uint32_t b, float t) {
  uint32_t out = 0;
  for(int sh = 0; sh < 32; sh += 8) {
    float x = (float) (a >> sh & 255) + ((float) (b >> sh & 255) - (float) (a >> sh & 255)) * t;
    out |= (uint32_t) (x + 0.5f) << sh;
  }
  return out;
}

// A convex polygon in camera space: clipped to the near plane, projected, as a fan.
// Textured polygons are cut finer near the camera, so the picture bends less (the
// renderer interpolates texture coordinates on the screen, not in depth).
static void emit_poly(Gather* g, const CV* in, int n, uint32_t argb, int pic) {
  CV out[28];
  int m = 0;
  for(int i = 0; i < n && m < 26; i++) {
    const CV* a = &in[i];
    const CV* b = &in[(i + 1) % n];
    bool ain = a->p.z >= NEAR, bin = b->p.z >= NEAR;
    if(ain) out[m++] = *a;
    if(ain != bin) {
      float t = (NEAR - a->p.z) / (b->p.z - a->p.z);
      out[m++] = (CV) {vadd(a->p, vmul(vsub(b->p, a->p), t)), a->u + (b->u - a->u) * t, a->v + (b->v - a->v) * t,
                       a->c && b->c ? lerp_colour(a->c, b->c, t) : 0};
    }
  }
  if(m < 3) return;
  const Cam* c = g->cam;
  TgV3Vertex sv[28];
  for(int i = 0; i < m; i++) {
    float iz = 1 / out[i].p.z;
    sv[i] = (TgV3Vertex) {c->cx + out[i].p.x * c->f * iz, c->cy - out[i].p.y * c->f * iz, out[i].u, out[i].v,
                          out[i].c ? out[i].c : argb, out[i].p.z};
  }
  for(int i = 1; i + 1 < m; i++) push_tri(g, &sv[0], &sv[i], &sv[i + 1], pic);
}

static void quad(Gather* g, V3 a, V3 b, V3 c, V3 d, uint32_t argb) {
  CV q[4] = {{to_cam(g->cam, a), 0, 0, 0}, {to_cam(g->cam, b), 0, 0, 0}, {to_cam(g->cam, c), 0, 0, 0}, {to_cam(g->cam, d), 0, 0, 0}};
  emit_poly(g, q, 4, argb, -1);
}

// A quad shaded from corner to corner (a, b, c, d its corners' colours).
static void quad_shaded(Gather* g, V3 a, V3 b, V3 c, V3 d, uint32_t ca, uint32_t cb, uint32_t cc, uint32_t cd) {
  CV q[4] = {{to_cam(g->cam, a), 0, 0, ca}, {to_cam(g->cam, b), 0, 0, cb}, {to_cam(g->cam, c), 0, 0, cc}, {to_cam(g->cam, d), 0, 0, cd}};
  emit_poly(g, q, 4, ca, -1);
}

// A picture on a quad in the world: a top left, b top right, c bottom right, d bottom left.
// Split into a grid so it bends correctly in perspective.
static void tex_quad_uv(Gather* g, V3 a, V3 b, V3 c, V3 d, int pic, uint32_t argb, int cells, float pu0, float pu1,
                        float pv0, float pv1) {
  for(int j = 0; j < cells; j++)
    for(int i = 0; i < cells; i++) {
      float u0 = (float) i / cells, u1 = (float) (i + 1) / cells, v0 = (float) j / cells, v1 = (float) (j + 1) / cells;
      V3 p[4];
      float uu[4] = {u0, u1, u1, u0}, vv[4] = {v0, v0, v1, v1};
      for(int k = 0; k < 4; k++) {
        V3 top = vadd(a, vmul(vsub(b, a), uu[k])), bot = vadd(d, vmul(vsub(c, d), uu[k]));
        p[k] = vadd(top, vmul(vsub(bot, top), vv[k]));
      }
      CV q[4];
      for(int k = 0; k < 4; k++) q[k] = (CV) {to_cam(g->cam, p[k]), pu0 + (pu1 - pu0) * uu[k], pv0 + (pv1 - pv0) * vv[k], 0};
      emit_poly(g, q, 4, argb, pic);
    }
}

static void tex_quad(Gather* g, V3 a, V3 b, V3 c, V3 d, int pic, uint32_t argb, int cells) {
  tex_quad_uv(g, a, b, c, d, pic, argb, cells, 0, 1, 0, 1);
}

static uint32_t shade(uint32_t c, float k) {
  int r = (int) ((c >> 16 & 255) * k), gg = (int) ((c >> 8 & 255) * k), b = (int) ((c & 255) * k);
  r = r > 255 ? 255 : r;
  gg = gg > 255 ? 255 : gg;
  b = b > 255 ? 255 : b;
  return 0xFF000000u | (uint32_t) r << 16 | (uint32_t) gg << 8 | (uint32_t) b;
}

static uint32_t mix(uint32_t a, uint32_t b, float t) {
  int r = (int) ((a >> 16 & 255) + ((int) (b >> 16 & 255) - (int) (a >> 16 & 255)) * t);
  int g = (int) ((a >> 8 & 255) + ((int) (b >> 8 & 255) - (int) (a >> 8 & 255)) * t);
  int bl = (int) ((a & 255) + ((int) (b & 255) - (int) (a & 255)) * t);
  return 0xFF000000u | (uint32_t) r << 16 | (uint32_t) g << 8 | (uint32_t) bl;
}

static const V3 kLight = {0.33f, 0.82f, 0.46f};

static uint32_t lit(uint32_t col, V3 n) {
  float l = vdot(n, vnorm(kLight));
  return shade(col, 0.78f + 0.3f * (l > 0 ? l : 0));
}

// A box: centre of its base, axes (unit), half sizes, one colour; the faces turned away are
// left out. rear_pic, if not -1, is drawn on the face at -fwd (a car's back).
// Things standing on the road are drawn after the stretch of road they stand on (its
// depth is the middle of a 2.5 m segment), never in a tie with it that could flip from one
// picture to the next.
#define STAND_BIAS 1.6f

static void box(Gather* g, V3 base, V3 fwd, V3 right, V3 up, float hl, float hw, float hh, uint32_t col, int rear_pic) {
  V3 c = vadd(base, vmul(up, hh));
  begin_item(g, to_cam(g->cam, c).z - STAND_BIAS);
  V3 ax[3] = {fwd, right, up};
  float hs[3] = {hl, hw, hh};
  for(int a = 0; a < 3; a++) {
    for(int s = -1; s <= 1; s += 2) {
      V3 n = vmul(ax[a], (float) s);
      V3 fc = vadd(c, vmul(n, hs[a]));
      if(vdot(n, vsub(fc, g->cam->eye)) >= 0) continue;
      if(a == 0 && s < 0 && rear_pic >= 0) {
        // Seen from behind, as the game draws the car: its own picture.
        V3 tl = vadd(fc, vadd(vmul(right, -hw), vmul(up, hh))), tr = vadd(fc, vadd(vmul(right, hw), vmul(up, hh)));
        V3 br = vadd(fc, vadd(vmul(right, hw), vmul(up, -hh))), bl = vadd(fc, vadd(vmul(right, -hw), vmul(up, -hh)));
        tex_quad(g, tl, tr, br, bl, rear_pic, lit(0xFFFFFFFF, n), 2);
        continue;
      }
      V3 u = ax[(a + 1) % 3], w = ax[(a + 2) % 3];
      float hu = hs[(a + 1) % 3], hv = hs[(a + 2) % 3];
      V3 p0 = vadd(fc, vadd(vmul(u, -hu), vmul(w, -hv)));
      V3 p1 = vadd(fc, vadd(vmul(u, hu), vmul(w, -hv)));
      V3 p2 = vadd(fc, vadd(vmul(u, hu), vmul(w, hv)));
      V3 p3 = vadd(fc, vadd(vmul(u, -hu), vmul(w, hv)));
      quad(g, p0, p1, p2, p3, lit(col, n));
    }
  }
}

// A picture standing on the ground at base, facing the camera, w x h metres.
static void billboard_tint(Gather* g, V3 base, float w, float h, int pic, uint32_t argb) {
  V3 r = vnorm(v3(g->cam->right.x, 0, g->cam->right.z));
  V3 up = v3(0, 1, 0);
  V3 a = vadd(base, vmul(r, -w / 2)), b = vadd(base, vmul(r, w / 2));
  begin_item(g, to_cam(g->cam, base).z - STAND_BIAS);
  tex_quad(g, vadd(a, vmul(up, h)), vadd(b, vmul(up, h)), b, a, pic, argb, 1);
}

static void billboard(Gather* g, V3 base, float w, float h, int pic) { billboard_tint(g, base, w, h, pic, 0xFFFFFFFF); }

static void frustum(Gather* g, V3 base, V3 axis, V3 a1, V3 a2, float r0, float r1, float len, int n, uint32_t col, float bias);

// A nitro flame from an exhaust at at, pointing back: blue-violet round a white-hot core, and
// the glow it throws.
static uint32_t with_alpha(uint32_t c, int a);
static void nitro_flame(Gather* g, V3 at, V3 back, V3 r, V3 u, float len) {
  (void) r;
  // Soft glowing puffs along the flame, shrinking and fading out, white-hot at the pipe.
  for(int i = 5; i >= 0; i--) {
    float t = i / 5.0f, sz = 0.75f - 0.5f * t;
    V3 p = vadd(at, vmul(back, len * t));
    int a = (int) (255 - 170 * t);
    uint32_t col = t < 0.2f ? with_alpha(0xFFE8F2FF, a) : with_alpha(t < 0.55f ? 0xFF7AA8FF : 0xFF5A62FF, a);
    billboard_tint(g, vadd(p, vmul(u, -sz / 2)), sz, sz, TEX_GLOW, col);
  }
  billboard_tint(g, vadd(vadd(at, vmul(back, len * 0.3f)), vmul(u, -0.5f)), 1.6f, 1.0f, TEX_GLOW, 0x8070A8FF);
}

// Interpolated car k: a race update behind, moving smoothly between the last two. The car
// list is re-sorted as cars overtake, so the car is found by its id.
static void car_now_raw(const TgView3D* v, int k, float t, float* pos, float* side) {
  const TgSceneCar* b = &v->cur.cars[k];
  const TgSceneCar* a = b;
  for(int i = 0; i < v->prev.car_count; i++)
    if(v->prev.cars[i].id == b->id) a = &v->prev.cars[i];
  float len = (float) v->track.tables.segments * TG_TRACK3D_SEG_UNITS;
  float d = b->track_pos - a->track_pos;
  if(d > len / 2) d -= len;
  if(d < -len / 2) d += len;
  if(fabsf(d) > 0x200) t = 1;  // a jump (the start of a race): no sliding
  *pos = a->track_pos + d * t;
  if(*pos < 0) *pos += len;
  if(*pos >= len) *pos -= len;
  *side = a->side + (b->side - a->side) * t;
}

// The pit stop being drawn for car id (view3d_pits.inc), or NULL.
static const PitCar* pit_car(const TgView3D* v, int id) {
  for(int p = 0; p < 2; p++)
    if(v->pitc[p].phase != PIT_OFF && v->pitc[p].id == (id & 31)) return &v->pitc[p];
  return NULL;
}

// Where car k is drawn: the game's place, eased between race updates, and for a car in its pit
// stop the drawn stop's offset from it (run on by the display's part of a frame).
static void car_now(const TgView3D* v, int k, float t, float* pos, float* side) {
  car_now_raw(v, k, t, pos, side);
  const PitCar* pc = pit_car(v, v->cur.cars[k].id);
  if(pc) {
    float len = (float) v->track.tables.segments * TG_TRACK3D_SEG_UNITS, u = v->pit_sub;
    *pos += pc->off[1][0] + (pc->off[1][0] - pc->off[0][0]) * u;
    *side += pc->off[1][1] + (pc->off[1][1] - pc->off[0][1]) * u;
    *pos = fmodf(*pos, len);
    if(*pos < 0) *pos += len;
  }
}

// --- The cars' bodies on their springs (visual only) -------------------------------------
// What a modern arcade racer shows (OutRun 2, Forza Horizon, Need for Speed): the body is a
// separate thing from the wheels, on springs, driven by the forces the car feels; the wheels
// stay on the road. The forces come from the road and the car's motion as the game moves it:
// lateral acceleration (the road's curvature times the speed squared) rolls the body out of
// the turn, braking dips the nose and accelerating squats it, a dip in the road compresses
// the springs and a crest unloads them (heave), and the body's pitch lags the road's grade.
// Each is a critically damped spring (no overshoot, never unstable), stepped once a game
// frame at a fixed 1/60 s so it is the same at any display rate; the display eases between
// the last two steps. Small: a few degrees at most, the heading still clamped to the road's.

static float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }

// One critically damped spring step: s[0] the value, s[1] its rate, towards target.
static void spring_step(float s[2], float target, float omega, float dt) {
  float d = s[0] - target, tmp = (s[1] + omega * d) * dt, e = expf(-omega * dt);
  s[0] = target + (d + tmp) * e;
  s[1] = (s[1] - omega * tmp) * e;
}

static float sat01(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
static float smooth01(float a, float b, float x) {
  float t = sat01((x - a) / (b - a));
  return t * t * (3 - 2 * t);
}

// --- Room between the drawn cars (visual only) --------------------------------------------
// The game's cars touch at zero track gap, within 0x800 side units (~0.8 m) of a rival
// pushing a player's car from behind and 0x1000 (~1.6 m) of one the player runs into - a rear
// push leaves the pusher 0.04 m behind - but our models are 4.3 m by 1.9 m: a rival the game
// has right behind or beside the player was drawn inside its car (693 frames of one session).
// So rivals are drawn apart; the game's positions are never touched, nor the players' cars,
// nor the camera (it follows a player's car).
// - Along (sep_room): in a player's lane, where the game never lets a rival through (it
//   bumps), the rival is drawn at least a car's length (4.45 m) behind or ahead of it, on the
//   side the game's car list says (the list is the truth about who is ahead; after a rear
//   push at gap 0 the pusher is behind); out of the lane only as far as the move across can't
//   clear the bodies. A rival held behind a held one keeps behind it too (a queue, placed in
//   the list's order out from the player). A smooth maximum (a quadratic knee), not a spring:
//   no lag, so a car run into at 40 m/s is shoved as it is touched, not sunk into.
// - Across: rounds of pushing apart each pair that overlaps along (position-based), at most
//   0.6 m a car and within the road, begun ahead of the bodies meeting by 2.5 m and 0.3 s of
//   the two's difference in speed; two rivals share the move, less as they come level across
//   (where they cross over); a rival beside a player's car is never pushed into it. Never
//   along between two rivals that pass each other (the side to hold one to would flip: a
//   two-car-length jump).
// All weights are smooth, so the room changes continuously; where it can't (a held pair
// changing which is ahead: one passing the other beside it) the jump is taken out and eased
// back in on a critically damped spring (~0.15 s: inertialization, as animation blending
// does), and any other change is shown at no more than 1.5 m along (0.2 m across) a game
// frame (step_separation). A car moved across turns into the move a little. The game's knock
// ($1EF5 / $1EF6 bit 7 rising with a rival in reach) jolts both bodies on their springs and
// throws sparks from the contact (modern_knock).

// x, but no less than m: a quadratic knee of half width k between (smooth: no kink).
static float smooth_max(float x, float m, float k) {
  float d = x - m;
  return d >= k ? x : d <= -k ? m : m + (d + k) * (d + k) / (4 * k);
}

static float lap_gap(float d, float len) { return d > len / 2 ? d - len : d < -len / 2 ? d + len : d; }

// Record sb ahead of record sa in the game's car list (a ring of records in track order, cars.c).
static bool list_ahead(int sa, int sb) {
  int ring = ((sb - sa) % TG_SCENE_MAX_CARS + TG_SCENE_MAX_CARS) % TG_SCENE_MAX_CARS;
  return ring > 0 && ring < TG_SCENE_MAX_CARS / 2;
}

// The players' cars (the views': they and the camera are never moved).
static bool sep_fixed(const TgView3D* v, int k) {
  return k == v->cur.view_car[0] || (tg_view3d_views(v) > 1 && k == v->cur.view_car[1]);
}

// The room each car k needs at t (the cars' places as car_now has them): along[k] in track
// units, across[k] in side units, 0 for the players' cars. A function of the places (and,
// when keep - once a game frame - it notes which of each held pair is ahead), so the pictures
// drawn between two game frames are apart too, without a frame's lag. flip[k] (when keep): a
// pair car k is held in changed which is ahead (its room jumps: to be eased).
static void sep_room(TgView3D* v, float t, bool keep, float* along, float* across, bool* flip) {
  const TgTrack3D* tr = &v->track;
  enum { N = TG_SCENE_MAX_CARS };
  int n = v->cur.car_count < N ? v->cur.car_count : N;
  for(int k = 0; k < n; k++) along[k] = across[k] = 0;
  if(tr->tables.segments <= 0 || tr->scale.seg_len <= 0 || tr->scale.half_width <= 0) return;
  float len = (float) tr->tables.segments * TG_TRACK3D_SEG_UNITS;
  float mt = tr->scale.seg_len / TG_TRACK3D_SEG_UNITS, ms = tr->scale.half_width / TG_TRACK3D_SIDE_EDGE;  // metres per unit
  const float kLen = 4.45f, kWide = 1.95f, kKnee = 0.6f, kNudge = 0.6f, kFade = 2.5f;
  const float edge = TG_TRACK3D_SIDE_EDGE - 0.3f / ms;  // (a moved car's middle stays 0.3 m in from the road's edge)
  float pos[N], side[N], al[N];  // al: along (m, + ahead)
  int slot[N], order[N], dist[N], nr = 0;
  bool fixed[N];
  for(int k = 0; k < n; k++) {
    car_now(v, k, t, &pos[k], &side[k]);
    slot[k] = v->cur.cars[k].slot;
    fixed[k] = sep_fixed(v, k);
    al[k] = 0;
  }
  // The rivals out from the players' cars in the list's order, nearest first.
  for(int k = 0; k < n; k++) {
    if(fixed[k]) continue;
    int best = N;
    for(int f = 0; f < n; f++)
      if(fixed[f]) {
        int r = ((slot[k] - slot[f]) % N + N) % N;
        r = r < N - r ? r : N - r;
        best = r < best ? r : best;
      }
    dist[k] = best * 32 + (v->cur.cars[k].id & 31);  // (ties by id: not by the list's order)
    int i = nr++;
    while(i > 0 && dist[order[i - 1]] > dist[k]) order[i] = order[i - 1], i--;
    order[i] = k;
  }
  // Along: a rival held a car's length from a player's car, or from a rival held itself (a queue).
  for(int i = 0; i < nr; i++) {
    int r = order[i], ir = v->cur.cars[r].id & 31;
    float up = 0, down = 0;
    for(int q = 0; q < n; q++) {
      if(q == r) continue;
      int iq = v->cur.cars[q].id & 31;
      int8_t* mem = &v->sep_dir[ir][iq];
      float wq = fixed[q] ? 1 : smooth01(0.3f, 1.2f, fabsf(al[q]));  // (a rival only while held itself)
      float g = lap_gap(pos[r] - pos[q], len) * mt - al[q], dir = list_ahead(slot[q], slot[r]) ? 1.0f : -1.0f, dg = dir * g;
      float s = (side[r] - side[q]) * ms, as = fabsf(s), w = 0;
      bool near = wq > 0 && dg < kLen + kKnee && dg > -14;
      if(keep && near) {  // (a pair that changed which is ahead: its room jumps)
        if(*mem && *mem != (int8_t) dir && flip) flip[r] = true;
        *mem = (int8_t) dir;
      } else if(keep) {
        *mem = 0;
      }
      if(near) {
        if(fixed[q]) {
          // The game's own lanes, where it never lets one through the other: a player's car
          // runs into a rival ahead within 0x1000 side units (their halved sides 0x800 apart,
          // drive.c bump_cars), a rival behind pushes it within 0x800 (cars.c RaceMoveCars).
          float lane = dir > 0 ? 1 - smooth01(1.35f, 1.6f, as) : 1 - smooth01(0.62f, 0.8f, as);
          // Out of them, held too as far as the move across (below) leaves the bodies
          // overlapping (beside it by less than 1.35 m, the road's edge in the way, or easing
          // out of the lane): one passing the other there flips which is ahead, and that jump
          // is eased (step_separation).
          float need = kWide - as, room = (s > 0 ? edge - side[r] : edge + side[r]) * ms;
          float fade = kFade + 0.3f * fabsf(v->car_ms[ir] - v->car_ms[iq]);  // (as below)
          float across = fminf(need, kNudge) * (1 - smooth01(kLen, kLen + fade, fabsf(g))) * (1 - lane);
          float rest = need - fminf(across > 0 ? across : 0, room > 0 ? room : 0);
          w = fmaxf(lane, smooth01(0.05f, 0.35f, rest));
        } else {
          w = 1 - smooth01(0.62f, 0.8f, as);
        }
        w *= wq * (1 - smooth01(10, 14, -dg));  // (the list and the places far apart: not a pair)
      }
      if(w <= 0) continue;
      float push = w * (dir * smooth_max(dg, kLen, kKnee) - g);
      up = push > up ? push : up;
      down = push < down ? push : down;
    }
    al[r] = up + down;
  }
  // Across: rounds of pushing apart each pair whose bodies overlap along, each by what it lacks
  // (position-based, as crowd and cloth solvers do it), so a car pushed by one neighbour is
  // pushed back by the next instead of into it; every rival's move at most 0.6 m and within
  // the road, the players' cars never moved. A pair's aim: kWide apart, from as far as the
  // game has them once a car's length apart along (and no nearer than that until then) - a
  // rival beside a player's car only out of its lane, there the rival is held along. Two
  // rivals share the move, less as the game has them level across (where they cross over).
  enum { MAXP = N * (N - 1) / 2 };
  struct SepPair { int8_t a, b, sd; float s, aim; } pr[MAXP];
  int np = 0;
  float mv[N], mlo[N], mhi[N];  // the moves across (m, + right) and their limits
  for(int k = 0; k < n; k++) {
    mv[k] = 0;
    mhi[k] = fixed[k] ? 0 : clampf((edge - side[k]) * ms, 0, kNudge);
    mlo[k] = fixed[k] ? 0 : -clampf((edge + side[k]) * ms, 0, kNudge);
  }
  int byId[32];  // (the pairs in the order of the cars' ids, not the list's, which changes as
                 // they pass: the rounds' result must not depend on it)
  for(int i = 0; i < 32; i++) byId[i] = -1;
  for(int k = 0; k < n; k++) byId[v->cur.cars[k].id & 31] = k;
  for(int ia = 0; ia < 32; ia++)
    for(int ib = ia + 1; ib < 32; ib++) {
      int a = byId[ia], b = byId[ib];
      if(a < 0 || b < 0 || (fixed[a] && fixed[b])) continue;
      bool player = fixed[a] || fixed[b];
      float s = (side[b] - side[a]) * ms, g = lap_gap(pos[b] - pos[a], len) * mt + (player ? 0 : al[b] - al[a]);  // (two rivals: as held)
      // (begun 2.5 m before the bodies meet, and 0.3 s of the two's difference in speed: a car
      // passing fast doesn't swerve in a blink)
      float wg = 1 - smooth01(kLen, kLen + kFade + 0.3f * fabsf(v->car_ms[ia] - v->car_ms[ib]), fabsf(g));
      if(wg <= 0 || fabsf(s) >= kWide + 2 * kNudge) continue;
      int8_t sd = s > 0 ? 1 : -1;
      float w = wg;
      if(player) {
        bool ahead = fixed[a] ? list_ahead(slot[a], slot[b]) : list_ahead(slot[b], slot[a]);  // (the rival)
        float as = fabsf(s), lane = ahead ? 1 - smooth01(1.35f, 1.6f, as) : 1 - smooth01(0.62f, 0.8f, as);
        w *= 1 - lane;
      } else {
        // (two rivals the game has level across get less: they cross over there, and a push
        // to either side would flip as they do)
        w *= smooth01(0.1f, 0.45f, fabsf(s));
      }
      // (beside a player's car, no nearer than the game has it while they overlap along)
      float sp = s * sd, aim = sp + fminf(kWide - sp, player ? kNudge : 2 * kNudge) * w - (1 - (player ? wg : w)) * 2 * kNudge;
      if(np < MAXP) pr[np++] = (struct SepPair) {(int8_t) a, (int8_t) b, sd, s, aim};
    }
  // (the pairs with a player's car last in each round, and once more at the end: they are
  // the ones in sight, and they win)
  struct SepPair tmp[MAXP];
  int nt = 0;
  for(int i = 0; i < np; i++)
    if(!fixed[pr[i].a] && !fixed[pr[i].b]) tmp[nt++] = pr[i];
  int first = nt;
  for(int i = 0; i < np; i++)
    if(fixed[pr[i].a] || fixed[pr[i].b]) tmp[nt++] = pr[i];
  memcpy(pr, tmp, sizeof(struct SepPair) * (size_t) np);
  for(int it = 0; it < 7; it++)
    for(int i = it < 6 ? 0 : first; i < np; i++) {
      int a = pr[i].a, b = pr[i].b;
      float lack = pr[i].aim - pr[i].sd * (pr[i].s + mv[b] - mv[a]);
      if(lack <= 0) continue;
      float mb = fixed[a] ? 1 : fixed[b] ? 0 : 0.5f;  // (b's share)
      mv[b] = clampf(mv[b] + pr[i].sd * lack * mb, mlo[b], mhi[b]);
      mv[a] = clampf(mv[a] - pr[i].sd * lack * (1 - mb), mlo[a], mhi[a]);
    }
  for(int k = 0; k < n; k++)
    if(!fixed[k]) along[k] = al[k] / mt, across[k] = mv[k] / ms;
}

// Once a game frame: the room's jumps taken out and eased back in, the turn into a move
// across, the pairs kept, a knock.
static void step_separation(TgView3D* v, const uint8_t* wram) {
  const TgTrack3D* tr = &v->track;
  if(tr->tables.segments <= 0 || tr->scale.seg_len <= 0 || tr->scale.half_width <= 0) return;
  float t = v->period > 0 ? (float) v->since / (float) v->period : 1;
  if(t > 1) t = 1;
  enum { N = TG_SCENE_MAX_CARS };
  int n = v->cur.car_count < N ? v->cur.car_count : N;
  float along[N], across[N];
  bool flip[N] = {false};
  sep_room(v, t, true, along, across, flip);
  float len = (float) tr->tables.segments * TG_TRACK3D_SEG_UNITS;
  float mt = tr->scale.seg_len / TG_TRACK3D_SEG_UNITS, ms = tr->scale.half_width / TG_TRACK3D_SIDE_EDGE;
  // A flip's jump eased out on a critically damped spring; any other change shown at no more
  // than 1.5 m along (0.2 m across) a game frame, the rest caught up at that rate (a shove,
  // when a car thrown sideways meets one level with it: an impact, not a leap).
  const float omega = 2 / 0.15f, dt = 1 / 60.0f;
  for(int k = 0; k < n; k++) {
    int id = v->cur.cars[k].id & 31;
    float to[2] = {along[k], across[k]}, last = v->sep_last[id][1];
    for(int x = 0; x < 2; x++) {
      float d = to[x] - v->sep_tgt[id][x], rate = x ? 0.2f / ms : 1.5f / mt, *sl = &v->sep_slew[id][x];
      if(flip[k]) v->sep_ease[id][x][0] -= d;  // (the drawn place stays where it was)
      else *sl -= d;
      *sl = *sl > rate ? *sl - rate : *sl < -rate ? *sl + rate : 0;
      spring_step(v->sep_ease[id][x], 0, omega, dt);
      v->sep_tgt[id][x] = to[x];
      v->sep_last[id][x] = to[x] + v->sep_ease[id][x][0] + *sl;
    }
    float rate = (v->sep_last[id][1] - last) * ms / dt, sp = v->car_ms[id] > 8 ? v->car_ms[id] : 8;
    v->sep_yaw[id] += (clampf(atanf(rate / sp), -0.12f, 0.12f) - v->sep_yaw[id]) * 0.3f;
  }
  float pos[N], side[N];
  bool fixed[N];
  for(int k = 0; k < n; k++) car_now(v, k, t, &pos[k], &side[k]), fixed[k] = sep_fixed(v, k);
  // A knock: the rival in reach in the player's lane, nearest along. The one hit from behind
  // pitches back, the one running into it dips its nose; both jump a little on their springs.
  for(int pl = 0; pl < 2; pl++) {
    int pk = v->cur.view_car[pl];
    bool bit = wram && pk >= 0 && pk < n && (pl == 0 || tg_view3d_views(v) > 1) && (wram[0x1EF5 + pl] & 0x80);
    if(bit && !v->knock_bit[pl]) {
      int best = -1;
      float bg = 3.0f;
      for(int k = 0; k < n; k++) {
        float g = fabsf(lap_gap(pos[k] - pos[pk], len)) * mt;
        if(!fixed[k] && fabsf(side[k] - side[pk]) * ms < 1.6f && g < bg) bg = g, best = k;
      }
      if(best >= 0) {
        int id = v->cur.cars[best].id & 31, pid = v->cur.cars[pk].id & 31;
        bool behind = !list_ahead(v->cur.cars[pk].slot, v->cur.cars[best].slot);
        v->knock_id[pl] = id + 1, v->knock_at[pl] = v->frames;
        if(car_trace()) fprintf(stderr, "CK %u player %d rival %d %s\n", v->frames, pl, id, behind ? "behind" : "ahead");
        v->body[behind ? pid : id].pitch[1] -= 0.45f;
        v->body[behind ? id : pid].pitch[1] += 0.45f;
        v->body[pid].heave[1] += 0.25f, v->body[id].heave[1] += 0.25f;
      }
    }
    v->knock_bit[pl] = bit;
  }
  if(car_trace())  // CS frame id: gap along and across to a player's car (m), as the game has it and as drawn
    for(int a = 0; a < n; a++)
      for(int b = 0; b < n; b++) {
        if(!fixed[a] || fixed[b]) continue;
        int ia = v->cur.cars[a].id & 31, ib = v->cur.cars[b].id & 31;
        float gm = lap_gap(pos[b] - pos[a], len) * mt, sm = (side[b] - side[a]) * ms;
        if(fabsf(gm) < 8 && fabsf(sm) < 3)
          fprintf(stderr, "CS %u %d game %.2f %.2f drawn %.2f %.2f\n", v->frames, ib, gm, sm,
                  gm + (v->sep_last[ib][0] - v->sep_last[ia][0]) * mt, sm + (v->sep_last[ib][1] - v->sep_last[ia][1]) * ms);
      }
  if(car_trace())  // CP frame id id: the same between two rivals
    for(int a = 0; a < n; a++)
      for(int b = a + 1; b < n; b++) {
        if(fixed[a] || fixed[b]) continue;
        int ia = v->cur.cars[a].id & 31, ib = v->cur.cars[b].id & 31;
        float gm = lap_gap(pos[b] - pos[a], len) * mt, sm = (side[b] - side[a]) * ms;
        if(fabsf(gm) < 8 && fabsf(sm) < 3)
          fprintf(stderr, "CP %u %d %d game %.2f %.2f drawn %.2f %.2f\n", v->frames, ia, ib, gm, sm,
                  gm + (v->sep_last[ib][0] - v->sep_last[ia][0]) * mt, sm + (v->sep_last[ib][1] - v->sep_last[ia][1]) * ms);
      }
}

// Sparks where a player's car and a rival knocked (step_separation): hot specks thrown out
// from the contact between the two bodies as drawn, out to the sides, falling back and down,
// white to orange, for up to 0.55 s, and a flash in the first instant.
static void modern_knock(Gather* g, const TgView3D* v, float t, const float* sepA, const float* sepS) {
  const TgTrack3D* tr = &v->track;
  float len = (float) tr->tables.segments * TG_TRACK3D_SEG_UNITS;
  for(int pl = 0; pl < 2; pl++) {
    if(!v->knock_id[pl]) continue;
    float age = ((float) (v->frames - v->knock_at[pl]) + v->subframe) / 60.0f;
    if(age > 0.55f) continue;
    int pk = v->cur.view_car[pl], rk = -1, id = v->knock_id[pl] - 1;
    for(int k = 0; k < v->cur.car_count; k++)
      if((v->cur.cars[k].id & 31) == id) rk = k;
    if(pk < 0 || rk < 0) continue;
    float pp, ps, rp, rs;
    car_now(v, pk, t, &pp, &ps);
    car_now(v, rk, t, &rp, &rs);
    rp += sepA[rk] + v->sep_ease[id][0][0] + v->sep_slew[id][0], rs += sepS[rk] + v->sep_ease[id][1][0] + v->sep_slew[id][1];
    float mid = pp + lap_gap(rp - pp, len) / 2;
    mid = mid < 0 ? mid + len : mid >= len ? mid - len : mid;
    TgTrackFrame cf = tg_track3d_frame(tr, mid, (ps + rs) / 2);
    V3 f = tv(cf.forward), r = tv(cf.right), u = tv(cf.up);
    V3 c = vadd(vadd(tv(cf.pos), vmul(f, 2.15f)), vmul(u, 0.38f));  // (a car's middle is 2.15 m ahead of its place)
    if(age < 0.1f) {
      float k = 1 - age * 10;
      billboard_tint(g, vadd(c, vmul(u, -0.6f * k)), 1.5f * k, 1.2f * k, TEX_GLOW, with_alpha(0xFFFFE8B0, (int) (220 * k)));
    }
    for(int i = 0; i < 24; i++) {
      // (from across the contact, out to the sides and up past the bodies, so they are seen
      // from behind the cars; trailing back as the cars run on)
      uint32_t h = hash32(v->knock_at[pl] * 31u + (uint32_t) i * 7919u + (uint32_t) pl), h2 = hash32(h);
      float sx = ((h & 255) / 255.0f - 0.5f) * 2, sy = (h >> 8 & 255) / 255.0f, sb = (h >> 16 & 255) / 255.0f;
      float life = 0.25f + 0.3f * ((h >> 24) / 255.0f), q = age / life, x0 = sx * 0.85f, vx = (sx > 0 ? 3 : -3) + sx * 7;
      if(q >= 1) continue;
      V3 at = vadd(c, vadd(vmul(r, x0 + vx * age), vadd(vmul(u, (1.5f + 3.5f * sy) * age - 4.9f * age * age),
                                                        vmul(f, -(2 + 10 * sb) * age + ((h2 & 255) / 255.0f - 0.5f) * 0.6f))));
      uint32_t col = q < 0.3f ? 0xFFFFF6D8 : q < 0.65f ? 0xFFFFC060 : 0xFFFF7A28;
      float sz = 0.2f + 0.14f * (1 - q);
      billboard_tint(g, vadd(at, vmul(u, -sz / 2)), sz, sz, TEX_GLOW, with_alpha(col, (int) (255 * (1 - q * q))));
    }
  }
}

static void step_bodies(TgView3D* v) {
  const TgTrack3D* tr = &v->track;
  if(tr->tables.segments <= 0 || tr->scale.seg_len <= 0) return;
  memcpy(v->body_prev, v->body, sizeof(v->body));
  float t = v->period > 0 ? (float) v->since / (float) v->period : 1;
  if(t > 1) t = 1;
  const float span = 4.0f, dt = 1 / 60.0f;  // the road's bend measured over 4 m
  float ds = span / tr->scale.seg_len * TG_TRACK3D_SEG_UNITS;
  for(int k = 0; k < v->cur.car_count; k++) {
    int id = v->cur.cars[k].id & 31;
    float pos, side;
    car_now(v, k, t, &pos, &side);
    TgTrackFrame a = tg_track3d_frame(tr, pos, side), b = tg_track3d_frame(tr, pos + ds, side);
    V3 df = vsub(tv(b.forward), tv(a.forward));
    float lat = vdot(df, tv(a.right)) / span;  // curvature across, 1/m (+ bending right)
    float vert = vdot(df, tv(a.up)) / span;    // and up (+ a dip: the road bends upward)
    // Its speed (a player's: the game's speed word, the HUD shows it x 24/1024 as km/h) and
    // its change, for the pitch.
    {
      const TgSceneCar* cc = &v->cur.cars[k];
      // (the players' cars: their own speed words, every frame; the rivals': their motion)
      float now = v->car_adv_ms[id];
      if(cc->view) {
        uint16_t sw = v->pspeed[cc->view - 1];
        now = (sw & 0x8000) ? 0 : sw * (24.0f / 1024.0f / 3.6f);
      }
      // (in its pit stop the speed word is the game's pit scene's: the car as drawn instead)
      const PitCar* pc = pit_car(v, id);
      if(pc) now = pc->vel;
      float acc = clampf((now - v->car_ms[id]) / dt, -30, 30);
      v->car_accel[id] += (acc - v->car_accel[id]) * 0.12f;
      v->car_ms[id] = now;
    }
    float ms = v->car_ms[id], v2 = ms * ms;
    // Out of the turn: in a left bend the body leans right.
    float roll = clampf(-lat * v2 * 0.003f, -0.06f, 0.06f);
    // Nose down braking, up accelerating; the body lags the grade (the road under its middle).
    float acc = v->car_accel[id], pitch = clampf(acc < 0 ? -acc * 0.0045f : -acc * 0.0012f, -0.015f, 0.05f) + clampf(-vert * 2.15f, -0.04f, 0.04f);
    // A dip presses it down, a crest lets it rise.
    float heave = clampf(-vert * v2 * 0.003f, -0.07f, 0.05f);
    // The front wheels follow the bend (its curvature over a 2.6 m wheelbase, shown larger),
    // for every car, as well as the turn the car's own heading makes.
    float steer = clampf(lat * 2.6f * 3.0f + v->car_yaw[id] * 1.5f, -0.4f, 0.4f);
    // Its heading eases towards the one the game's motion gives (at a fixed rate a game frame,
    // whatever the display's).
    v->car_yaw[id] += (v->car_yaw_to[id] - v->car_yaw[id]) * 0.06f;
    struct CarBody* bd = &v->body[id];
    spring_step(bd->roll, roll, 6.5f, dt);
    spring_step(bd->pitch, pitch, 8.0f, dt);
    spring_step(bd->heave, heave, 9.0f, dt);
    spring_step(bd->steer, steer, 12.0f, dt);
  }
}

// A car built from the game's own picture of it: a body and cabin in its colours, with the
// picture of it from behind on its back, so from the game's camera it looks as the game
// draws it, and from any other angle it has a body.
// One slab of a car built from its picture: a box from height y0 to y1, its back showing
// lines v0-v1 and columns u0-u1 of the picture, its sides and top in col.
static void slab(Gather* g, V3 p, V3 f, V3 r, V3 u, float fc, float lx, float y0, float y1, float hl, float hw,
                 uint32_t col, int pic, float u0, float u1, float v0, float v1) {
  float hh = (y1 - y0) / 2;
  V3 c = vadd(vadd(vadd(p, vmul(f, fc)), vmul(r, lx)), vmul(u, y0 + hh));
  // Higher slabs a hair later, so stacked ones never tie.
  begin_item(g, to_cam(g->cam, c).z - STAND_BIAS - y0 * 0.01f);
  V3 ax[3] = {f, r, u};
  float hs[3] = {hl, hw, hh};
  for(int a = 0; a < 3; a++) {
    for(int sgn = -1; sgn <= 1; sgn += 2) {
      V3 n = vmul(ax[a], (float) sgn);
      V3 fc3 = vadd(c, vmul(n, hs[a]));
      if(vdot(n, vsub(fc3, g->cam->eye)) >= 0) continue;
      if(a == 0 && sgn < 0 && pic >= 0) {
        V3 tl = vadd(fc3, vadd(vmul(r, -hw), vmul(u, hh))), tr = vadd(fc3, vadd(vmul(r, hw), vmul(u, hh)));
        V3 br = vadd(fc3, vadd(vmul(r, hw), vmul(u, -hh))), bl = vadd(fc3, vadd(vmul(r, -hw), vmul(u, -hh)));
        tex_quad_uv(g, tl, tr, br, bl, pic, 0xFFFFFFFF, 1, u0, u1, v0, v1);
        continue;
      }
      V3 au = ax[(a + 1) % 3], aw = ax[(a + 2) % 3];
      float hu = hs[(a + 1) % 3], hv = hs[(a + 2) % 3];
      quad(g, vadd(fc3, vadd(vmul(au, -hu), vmul(aw, -hv))), vadd(fc3, vadd(vmul(au, hu), vmul(aw, -hv))),
           vadd(fc3, vadd(vmul(au, hu), vmul(aw, hv))), vadd(fc3, vadd(vmul(au, -hu), vmul(aw, hv))), lit(col, n));
    }
  }
}

// A car built from the game's own picture of it from behind: the picture is cut into bands
// of lines, and each band becomes a slab as wide as the car is there, showing that band on
// its back and its colours on its sides and top. The lower bands run the car's length, the
// upper ones (the cabin) are shorter and set back. From behind it is the game's picture;
// from anywhere else it has the picture's shape and colours.
static void car_model(Gather* g, const TgView3D* v, TgTrackFrame fr, int id) {
  const TgV3Pic* rear = &v->pics[REAR_PIC + (id & 31)];
  V3 f = tv(fr.forward), r = tv(fr.right), u = tv(fr.up);
  V3 p = vadd(tv(fr.pos), vmul(f, 2.1f));  // its back where the game draws the car
  if(rear->w == 0 || rear->h == 0) {
    uint32_t body = v->car[id & 31].colour ? v->car[id & 31].colour : 0xFFC03030;
    box(g, vadd(p, vmul(u, 0.1f)), f, r, u, 2.1f, CAR_WIDTH / 2, 0.3f, body, -1);
    return;
  }
  float W = CAR_WIDTH, H = CAR_WIDTH * rear->h / rear->w;
  if(H > 1.4f) H = 1.4f;
  const int bands = 8;
  for(int b = 0; b < bands; b++) {
    int r0 = rear->h * b / bands, r1 = rear->h * (b + 1) / bands;
    if(r1 <= r0) continue;
    int x0 = rear->w, x1 = -1;
    long sr = 0, sg = 0, sb = 0, n = 0;
    for(int y = r0; y < r1; y++)
      for(int x = 0; x < rear->w; x++) {
        uint32_t c = rear->px[y * rear->w + x];
        if(!(c >> 24)) continue;
        if(x < x0) x0 = x;
        if(x > x1) x1 = x;
        sr += c >> 16 & 255;
        sg += c >> 8 & 255;
        sb += c & 255;
        n++;
      }
    if(x1 < x0 || n == 0) continue;
    uint32_t col = 0xFF000000u | (uint32_t) (sr / n) << 16 | (uint32_t) (sg / n) << 8 | (uint32_t) (sb / n);
    float yTop = H * (1 - (float) r0 / rear->h), yBot = H * (1 - (float) r1 / rear->h);
    float lx = ((x0 + x1 + 1) / 2.0f / rear->w - 0.5f) * W, hw = (x1 - x0 + 1) / 2.0f / rear->w * W;
    bool cabin = b < bands * 2 / 5;
    float hl = cabin ? 1.0f : 2.1f, fc = cabin ? -0.5f : 0;
    slab(g, p, f, r, u, fc, lx, yBot, yTop, hl, hw, col, REAR_PIC + (id & 31),
         (float) x0 / rear->w, (float) (x1 + 1) / rear->w, (float) r0 / rear->h, (float) r1 / rear->h);
  }
}

// Roadside objects as models: the game's own picture on two crossed boards (it looks the
// same from every side), or for a sign one board facing the road, on a post.
static void art_model(Gather* g, V3 base, V3 f, V3 r, float w, float h, int pic, uint32_t col) {
  V3 up = v3(0, 1, 0);
  int R = col >> 16 & 255, G = col >> 8 & 255, B = col & 255;
  bool sign = R > G + 40 && R > B + 40;
  if(sign) {
    V3 a = vadd(base, vmul(r, -w / 2)), b = vadd(base, vmul(r, w / 2));
    begin_item(g, to_cam(g->cam, base).z - STAND_BIAS);
    tex_quad(g, vadd(a, vmul(up, h)), vadd(b, vmul(up, h)), b, a, pic, 0xFFFFFFFF, 2);
    return;
  }
  for(int k = 0; k < 2; k++) {
    V3 d = vnorm(k == 0 ? vadd(f, r) : vsub(f, r));
    V3 a = vadd(base, vmul(d, -w / 2)), b = vadd(base, vmul(d, w / 2));
    begin_item(g, to_cam(g->cam, base).z - STAND_BIAS + (k ? 0.01f : 0));
    tex_quad(g, vadd(a, vmul(up, h)), vadd(b, vmul(up, h)), b, a, pic, k ? 0xFFE8E8E8 : 0xFFFFFFFF, 2);
  }
}

// The start/finish gantry (a group object the game draws partly in the background): posts
// at the road edges, a white banner across the road with checkered ends.
static const TgTrack3D* g_track;
static TgView3D* g_view;  // the view being drawn (models that write text need its font)
static int g_view_k;      // which of the views (0, or 1 in a 2-player race)

// A fade with memory (QA-009): what fades a thing near the camera, or one hiding the player's
// car, is worked out from where it is in this picture, so it went from solid to see-through
// (or back) within a frame or two as the camera swung past a big tree: a pop. Each thing keeps
// the alpha it was drawn with and moves towards the new one by the game frames since, ~0.25 s
// to settle; one not drawn for half a second starts at its target (no stale fade).
typedef struct { float a; uint32_t at; } FadeMem;
// (out and in: the share of the way to a lower / higher target gone a game frame)
static float fade_mem_rates(FadeMem* m, float target, float out, float in) {
  uint32_t now = g_view->frames, dn = now - m->at;
  if(m->at == 0 || dn > 30) m->a = target;
  else if(dn > 0) m->a += (target - m->a) * (1 - powf(1 - (target < m->a ? out : in), (float) dn));
  m->at = now ? now : 1;
  return m->a;
}
// (out faster than in: something that comes over the car or the camera is down to 6 % in
// three frames, not a ghost for a quarter second; in slowly, so nothing pops in)
static float fade_mem(FadeMem* m, float target) { return fade_mem_rates(m, target, 0.6f, 0.18f); }

static void gantry(Gather* g, float pos) {
  // The original's START gantry (retro R-11): lattice legs, a banner reading START with
  // chequered ends, and a row of start-light pods over the middle.
  const TgTrack3D* tr = g_track;
  TgTrackFrame c = tg_track3d_frame(tr, pos, 0);
  V3 f = tv(c.forward), r = tv(c.right), up = v3(0, 1, 0);
  float hw = tr->scale.half_width * 1.35f;  // legs just off the road
  for(int sgn = -1; sgn <= 1; sgn += 2) {
    V3 base = vadd(tv(c.pos), vmul(r, hw * sgn));
    for(int k = -1; k <= 1; k += 2)  // two uprights a leg, braced: a lattice tower
      box(g, vadd(base, vmul(f, k * 0.32f)), f, r, up, 0.07f, 0.07f, 3.4f, 0xFFB8C0C8, -1);
    for(int k = 0; k < 6; k++)
      box(g, vadd(base, vmul(up, 0.4f + k * 1.1f)), f, r, up, 0.36f, 0.05f, 0.04f, 0xFF98A0A8, -1);
    box(g, vadd(base, vmul(up, 6.8f)), f, r, up, 0.4f, 0.4f, 0.12f, 0xFF98A0A8, -1);
  }
  V3 mid = vadd(tv(c.pos), vmul(up, 5.0f));
  box(g, vadd(mid, vmul(up, 0.2f)), f, r, up, 0.1f, hw, 0.08f, 0xFF98A0A8, -1);   // the truss under the banner
  box(g, mid, f, r, up, 0.12f, hw - 0.3f, 0.8f, 0xFFF4F4F4, -1);                   // the banner
  box(g, vadd(mid, vmul(up, 1.42f)), f, r, up, 0.13f, hw - 0.3f, 0.1f, 0xFFD82828, -1);  // red top edge
  box(g, vadd(mid, vmul(up, 0.08f)), f, r, up, 0.13f, hw - 0.3f, 0.08f, 0xFFD82828, -1); // and bottom
  for(int sgn = -1; sgn <= 1; sgn += 2)  // chequered ends: 2 rows of 4
    for(int k = 0; k < 8; k++) {
      int col = k & 3, row = k >> 2;
      V3 at = vadd(vadd(mid, vmul(r, sgn * (hw - 0.6f - col * 0.42f))), vmul(up, 0.3f + row * 0.5f));
      box(g, at, f, r, up, 0.14f, 0.21f, 0.25f, ((col + row) & 1) ? 0xFF101010 : 0xFFF8F8F8, -1);
    }
  // START on the side the field drives toward, in the HUD's italic, centred on the white
  // between the red edges (0.24-1.42 m up the banner). wtext's at is the baseline and the
  // letters rise from it: their height (the T's top) halves either side of the middle.
  {
    font_ready(g_view);
    float size = 1.25f, wdt = ftext_w("START", size, 2, 0.1f);
    float cap = -kFontGlyphs[glyph_of(2, 'T')].top * (size / FONT_SIZE);
    V3 face = vadd(mid, vmul(f, -0.14f));
    V3 at = vadd(vadd(face, vmul(r, -wdt / 2)), vmul(up, 0.83f - cap / 2));
    if(vdot(f, vsub(face, g->cam->eye)) > 0) wtext(g, "START", at, r, vmul(up, -1), size, 0xFF101828, 2, 0.1f);
  }
  // The start lights: five pods of two red lamps over the middle.
  V3 lp = vadd(mid, vmul(up, 2.2f));
  box(g, vadd(lp, vmul(up, 0.55f)), f, r, up, 0.06f, 1.9f, 0.05f, 0xFF606870, -1);
  for(int k = -2; k <= 2; k++) {
    V3 pod = vadd(lp, vmul(r, k * 0.75f));
    box(g, pod, f, r, up, 0.18f, 0.26f, 0.5f, 0xFF16181C, -1);
    for(int j = 0; j < 2; j++)
      box(g, vadd(vadd(pod, vmul(up, 0.18f + j * 0.42f)), vmul(f, -0.19f)), f, r, up, 0.02f, 0.13f, 0.13f, j ? 0xFFE82418 : 0xFF6A1410, -1);
  }
}

// --- The modern look: models and materials made here --------------------------------------

static uint32_t g_haze;
static uint32_t g_horizon;  // the sky's colour along the horizon (this picture's: modern_sky)
static uint32_t g_land;  // the open ground's colour where the camera is (the skyline's near hills take it)
static bool g_night;  // a night course (its sky is dark): the modern look lights it as night  // the colour far things fade to (this picture's)

// Distance haze: near things keep their colour, far ones fade into the sky's horizon colour.
static uint32_t fogged(uint32_t c, float depth) {
  float f = (depth - 60) / 620;
  f = f < 0 ? 0 : f > 1 ? 1 : f;
  f = f * f * (3 - 2 * f) * 0.7f;
  return (c & 0xFF000000u) | (mix(c, g_haze, f) & 0xFFFFFF);
}

static uint32_t with_alpha(uint32_t c, int a) { return (c & 0xFFFFFF) | (uint32_t) a << 24; }

// A solid from 8 corners: 0-3 its bottom, 4-7 its top above them. Faces turned away are left
// out; each face lit by its own direction and hazed by distance.
static void hexa(Gather* g, const V3 c[8], uint32_t col, float bias) {
  static const int faces[6][4] = {{0, 1, 2, 3}, {4, 5, 6, 7}, {0, 1, 5, 4}, {1, 2, 6, 5}, {2, 3, 7, 6}, {3, 0, 4, 7}};
  V3 centre = v3(0, 0, 0);
  for(int i = 0; i < 8; i++) centre = vadd(centre, vmul(c[i], 0.125f));
  float depth = to_cam(g->cam, centre).z;
  begin_item(g, depth - STAND_BIAS - bias);
  for(int f = 0; f < 6; f++) {
    V3 a = c[faces[f][0]], b = c[faces[f][1]], d = c[faces[f][2]], e = c[faces[f][3]];
    V3 fc = vmul(vadd(vadd(a, b), vadd(d, e)), 0.25f);
    V3 n = vnorm(vcross(vsub(b, a), vsub(e, a)));
    if(vdot(n, vsub(fc, centre)) < 0) n = vmul(n, -1);
    if(vdot(n, vsub(fc, g->cam->eye)) >= 0) continue;
    quad(g, a, b, d, e, fogged(lit(col, n), depth));
  }
}

// A box as a hexa: centre of its base, axes, half sizes.
static void mbox(Gather* g, V3 base, V3 f, V3 r, V3 u, float hl, float hw, float y0, float y1, uint32_t col, float bias) {
  V3 c[8];
  static const float sx[4] = {-1, -1, 1, 1}, sz[4] = {-1, 1, 1, -1};
  for(int k = 0; k < 4; k++) {
    c[k] = vadd(base, vadd(vadd(vmul(f, sz[k] * hl), vmul(r, sx[k] * hw)), vmul(u, y0)));
    c[k + 4] = vadd(base, vadd(vadd(vmul(f, sz[k] * hl), vmul(r, sx[k] * hw)), vmul(u, y1)));
  }
  hexa(g, c, col, bias);
}

// A frustum (a cylinder, or a cone if r1 is 0) of n sides along axis from base: radii r0 at
// the base and r1 at length len.
static void frustum(Gather* g, V3 base, V3 axis, V3 a1, V3 a2, float r0, float r1, float len, int n, uint32_t col, float bias) {
  if(n > 10) n = 10;
  V3 ring0[10], ring1[10];
  V3 top = vadd(base, vmul(axis, len));
  for(int i = 0; i < n; i++) {
    float ang = 2 * PI * (i + 0.5f) / n;
    V3 dir = vadd(vmul(a1, cosf(ang)), vmul(a2, sinf(ang)));
    ring0[i] = vadd(base, vmul(dir, r0));
    ring1[i] = vadd(top, vmul(dir, r1));
  }
  V3 centre = vadd(base, vmul(axis, len / 2));
  float depth = to_cam(g->cam, centre).z;
  begin_item(g, depth - STAND_BIAS - bias);
  for(int i = 0; i < n; i++) {
    int j = (i + 1) % n;
    V3 fc = vmul(vadd(vadd(ring0[i], ring0[j]), vadd(ring1[i], ring1[j])), 0.25f);
    V3 nrm = vnorm(vsub(fc, vadd(base, vmul(axis, vdot(vsub(fc, base), axis)))));
    if(vdot(nrm, vsub(fc, g->cam->eye)) >= 0) continue;
    if(r1 > 0.001f) quad(g, ring0[i], ring0[j], ring1[j], ring1[i], fogged(lit(col, nrm), depth));
    else {
      CV q[3] = {{to_cam(g->cam, ring0[i]), 0, 0, 0}, {to_cam(g->cam, ring0[j]), 0, 0, 0}, {to_cam(g->cam, top), 0, 0, 0}};
      emit_poly(g, q, 3, fogged(lit(col, nrm), depth), -1);
    }
  }
  // The caps that face the camera.
  for(int cap = 0; cap < 2; cap++) {
    V3 nrm = cap ? axis : vmul(axis, -1);
    V3 cc = cap ? top : base;
    if((cap && r1 <= 0.001f) || vdot(nrm, vsub(cc, g->cam->eye)) >= 0) continue;
    CV q[10];
    for(int i = 0; i < n; i++) q[i] = (CV) {to_cam(g->cam, cap ? ring1[i] : ring0[i]), 0, 0, 0};
    emit_poly(g, q, n, fogged(lit(col, nrm), depth), -1);
  }
}

// A soft shadow on the ground: a dark, see-through ellipse, drawn just after the road.
static void shadow(Gather* g, V3 c, V3 f, V3 r, float rf, float rr, float alpha) {
  begin_item(g, to_cam(g->cam, c).z - 0.6f);
  CV q[10];
  for(int i = 0; i < 10; i++) {
    float a = 2 * PI * i / 10;
    q[i] = (CV) {to_cam(g->cam, vadd(vadd(c, v3(0, 0.02f, 0)), vadd(vmul(f, cosf(a) * rf), vmul(r, sinf(a) * rr)))), 0, 0, 0};
  }
  emit_poly(g, q, 10, with_alpha(0xFF101418, (int) (alpha * 255)), -1);
}

// A car's: a darker core under the body (the contact shadow) fading out past its outline.
static void car_shadow(Gather* g, V3 c, V3 f, V3 r, float rf, float rr, float alpha) {
  begin_item(g, to_cam(g->cam, c).z - 0.6f);
  enum { S = 16 };
  V3 ring[3][S];
  static const float kScale[3] = {0.55f, 0.85f, 1.12f};
  for(int k = 0; k < 3; k++)
    for(int i = 0; i < S; i++) {
      float a = 2 * PI * i / S;
      ring[k][i] = to_cam(g->cam, vadd(vadd(c, v3(0, 0.02f, 0)), vadd(vmul(f, cosf(a) * rf * kScale[k]), vmul(r, sinf(a) * rr * kScale[k]))));
    }
  uint32_t core = with_alpha(0xFF0C0F12, (int) (alpha * 1.25f * 255 > 255 ? 255 : alpha * 1.25f * 255));
  uint32_t mid = with_alpha(0xFF101418, (int) (alpha * 0.8f * 255)), out = 0x00101418;
  CV q[S];
  for(int i = 0; i < S; i++) q[i] = (CV) {ring[0][i], 0, 0, core};
  emit_poly(g, q, S, core, -1);
  for(int k = 0; k < 2; k++)
    for(int i = 0; i < S; i++) {
      int j = (i + 1) % S;
      uint32_t ci = k ? mid : core, co = k ? out : mid;
      CV qq[4] = {{ring[k][i], 0, 0, ci}, {ring[k + 1][i], 0, 0, co}, {ring[k + 1][j], 0, 0, co}, {ring[k][j], 0, 0, ci}};
      emit_poly(g, qq, 4, ci, -1);
    }
}


// A car's paint: the colour covering most of its rear picture, greys and whites included
// (silver cars stay silver), leaving out the dark shadows, tyres and glass.
static uint32_t paint_colour(const TgV3Pic* p) {
  static uint16_t count[512];
  static uint32_t sum[512][3];
  memset(count, 0, sizeof(count));
  memset(sum, 0, sizeof(sum));
  // A coloured car: its commonest colour, if it covers a fair part of the picture. Otherwise a
  // silver or white one: its commonest light grey (the red of the lights is too small to win).
  int best = -1, bestGrey = -1, total = 0, coloured = 0;
  for(int i = 0; i < p->w * p->h; i++) {
    uint32_t c = p->px[i];
    if(!(c >> 24)) continue;
    int r = c >> 16 & 255, g = c >> 8 & 255, b = c & 255;
    int mx = r > g ? (r > b ? r : b) : (g > b ? g : b), mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    if(mx < 60) continue;
    total++;
    int k = (r >> 5) << 6 | (g >> 5) << 3 | (b >> 5);
    count[k]++;
    sum[k][0] += r, sum[k][1] += g, sum[k][2] += b;
    if(mx - mn >= 50) {
      coloured++;
      if(best < 0 || count[k] > count[best]) best = k;
    } else if(mx >= 110 && (bestGrey < 0 || count[k] > count[bestGrey])) bestGrey = k;
  }
  if(best < 0 || coloured * 4 < total) best = bestGrey;
  if(best < 0) return 0;
  int n = count[best];
  return 0xFF000000u | (sum[best][0] / n) << 16 | (sum[best][1] / n) << 8 | (sum[best][2] / n);
}

// A car's side view in its own paint: the players' as the select screen shows them, the
// others recoloured from one of the four (their body colour replaced by their paint,
// keeping its light and shade).
static const TgV3Pic* side_picture(TgView3D* v, int id, uint32_t paint) {
  int type = id == 0 ? v->ptype[0] : id == 0x1F ? v->ptype[1] : (id * 7 + 1) % 4;
  const TgV3Pic* src = &v->pics[SIDE_SRC + type];
  if(!src->w) return NULL;
  TgV3Pic* dst = &v->pics[SIDE_PIC + (id & 31)];
  bool player = id == 0 || id == 0x1F;
  uint32_t made = hash32(src->version * 2654435761u ^ (player ? 0x5EEDu : paint) ^ (uint32_t) type << 28);
  if(dst->w && v->side_made[id & 31] == made) return dst;
  static uint32_t px[140 * 48];
  int n = src->w * src->h;
  if(n > 140 * 48) return NULL;
  memcpy(px, src->px, (size_t) n * 4);
  if(!player && paint) {
    // The body: the commonest colour that isn't dark (tyres, glass) and its like.
    uint32_t bodyC = paint_colour(src);
    int br = bodyC >> 16 & 255, bg = bodyC >> 8 & 255, bb = bodyC & 255, bs = br + bg + bb + 1;
    float lumB = (br * 0.3f + bg * 0.59f + bb * 0.11f) + 1;
    for(int i = 0; i < n; i++) {
      uint32_t c = px[i];
      if(!(c >> 24)) continue;
      int r = c >> 16 & 255, g = c >> 8 & 255, b = c & 255, sum = r + g + b + 1;
      float d = fabsf((float) r / sum - (float) br / bs) + fabsf((float) g / sum - (float) bg / bs) + fabsf((float) b / sum - (float) bb / bs);
      float lum = r * 0.3f + g * 0.59f + b * 0.11f;
      if(d > 0.12f || lum < lumB * 0.35f) continue;
      float k = lum / lumB;
      int R = (int) ((paint >> 16 & 255) * k), G = (int) ((paint >> 8 & 255) * k), B = (int) ((paint & 255) * k);
      px[i] = 0xFF000000u | (uint32_t) (R > 255 ? 255 : R) << 16 | (uint32_t) (G > 255 ? 255 : G) << 8 | (uint32_t) (B > 255 ? 255 : B);
    }
  }
  set_pic(dst, src->w, src->h, px, src->w);
  dst->smooth = false;
  v->side_made[id & 31] = made;
  return dst;
}

// A face of the side-view car: a convex polygon in the world with picture coordinates.
static void side_face(Gather* g, const V3* p, const float* u, const float* vv, int n, uint32_t col, int pic, V3 centre) {
  V3 nrm = vnorm(vcross(vsub(p[1], p[0]), vsub(p[n - 1], p[0])));
  V3 fc = v3(0, 0, 0);
  for(int i = 0; i < n; i++) fc = vadd(fc, vmul(p[i], 1.0f / n));
  if(vdot(nrm, vsub(fc, centre)) < 0) nrm = vmul(nrm, -1);
  if(vdot(nrm, vsub(fc, g->cam->eye)) >= 0) return;
  CV q[6];
  for(int i = 0; i < n; i++) q[i] = (CV) {to_cam(g->cam, p[i]), u ? u[i] : 0, vv ? vv[i] : 0, 0};
  emit_poly(g, q, n, lit(col, nrm), pic);
}

// The car as the select screen shows it from the side, made solid: its outline drawn out
// across the car's width in slices, the picture on both flanks, its own rear picture on the
// back, the roof and bonnet in the colours along the picture's top edge.
static bool side_car(Gather* g, TgView3D* v, int id, V3 c, V3 f, V3 r, V3 u, float depth, bool braking, bool nitro, int tick,
                     uint32_t paint) {
  const TgV3Pic* sp = side_picture(v, id, paint);
  if(!sp || sp->w < 40) return false;
  int w = sp->w, h = sp->h;
  const float L = 4.3f, HW = 0.93f, mpp = L / w, hs = mpp * 0.85f;
  // The outline: per column, its top and bottom.
  static int top[140], bot[140];
  int highest = h;
  for(int x = 0; x < w; x++) {
    top[x] = h, bot[x] = -1;
    for(int y = 0; y < h; y++)
      if(sp->px[y * w + x] >> 24) {
        if(top[x] == h) top[x] = y;
        bot[x] = y;
      }
    if(top[x] < highest) highest = top[x];
  }
  // The belt line, where the body ends and the glasshouse begins: 45% of the way up from the
  // roof's height to the ground.
  int belt = highest + (h - highest) * 45 / 100;
  int pic = SIDE_PIC + (id & 31);
  int slices = depth < 60 ? 16 : 8;
  uint32_t white = fogged(0xFFFFFFFF, depth);
  float ymax = (h - highest) * hs;
  for(int si = 0; si < slices; si++) {
    int xa = si * w / slices, xb = (si + 1) * w / slices;
    if(xb >= w) xb = w - 1;
    if(xb <= xa || top[xa] >= h || top[xb] >= h) continue;
    float za = L / 2 - xa * mpp, zb = L / 2 - xb * mpp;
    float ua = (float) xa / w, ub = (float) xb / w;
    // Two parts: the body up to the belt line, the full width; the glasshouse above it,
    // narrower. A column whose top is below the belt (bonnet, boot) is body only.
    for(int part = 0; part < 2; part++) {
      int ta = part ? top[xa] : (top[xa] > belt ? top[xa] : belt), tb = part ? top[xb] : (top[xb] > belt ? top[xb] : belt);
      int ba = part ? belt : bot[xa], bb = part ? belt : bot[xb];
      if(part && (top[xa] >= belt || top[xb] >= belt)) continue;
      float yta = (h - ta) * hs, ytb = (h - tb) * hs, yba = (h - 1 - ba) * hs, ybb = (h - 1 - bb) * hs;
      if(part) yba = ybb = (h - belt) * hs;
      float wb = part ? HW * 0.74f : HW, wt = part ? HW * 0.6f : HW * 0.94f;
      V3 q[8];
      for(int k = 0; k < 2; k++) {
        float sd = k ? 1.0f : -1.0f;
        q[k * 4 + 0] = vadd(c, vadd(vmul(f, za), vadd(vmul(r, sd * wb), vmul(u, yba))));
        q[k * 4 + 1] = vadd(c, vadd(vmul(f, za), vadd(vmul(r, sd * wt), vmul(u, yta))));
        q[k * 4 + 2] = vadd(c, vadd(vmul(f, zb), vadd(vmul(r, sd * wt), vmul(u, ytb))));
        q[k * 4 + 3] = vadd(c, vadd(vmul(f, zb), vadd(vmul(r, sd * wb), vmul(u, ybb))));
      }
      V3 centre = vadd(c, vadd(vmul(f, (za + zb) / 2), vmul(u, (yta + yba) / 2)));
      begin_item(g, to_cam(g->cam, centre).z - STAND_BIAS - part * 0.01f);
      float uu[4] = {ua, ua, ub, ub}, vvs[4] = {(ba + 1.0f) / h, (float) ta / h, (float) tb / h, (bb + 1.0f) / h};
      if(part) vvs[0] = vvs[3] = (float) belt / h;
      for(int k = 0; k < 2; k++) side_face(g, &q[k * 4], uu, vvs, 4, white, pic, centre);
      // The top: the colour just inside the outline there (bonnet, roof, boot), or glass
      // where the glasshouse slopes.
      int xm = (xa + xb) / 2, ty = (part ? top[xm] : (top[xm] > belt ? top[xm] : belt)) + 2;
      uint32_t topC = sp->px[(ty < h ? ty : h - 1) * w + xm];
      if(!(topC >> 24)) topC = paint ? paint : 0xFF808080;
      bool slope = part && abs(ta - tb) > 2;
      if(slope) topC = 0xFF2A3440;  // windscreen, rear window
      V3 tq[4] = {q[1], q[2], q[6], q[5]};
      side_face(g, tq, NULL, NULL, 4, fogged(topC | 0xFF000000u, depth), -1, centre);
      if(si == 0) {  // the nose
        V3 nq[4] = {q[0], q[1], q[5], q[4]};
        side_face(g, nq, NULL, NULL, 4, fogged(shade(paint ? paint : topC, 0.9f), depth), -1, centre);
      }
      if(si == slices - 1) {  // the tail, behind the picture of the back
        V3 bq[4] = {q[7], q[6], q[2], q[3]};
        side_face(g, bq, NULL, NULL, 4, fogged(shade(paint ? paint : topC, 0.7f), depth), -1, centre);
      }
    }
  }
  // The back: the car's own picture from behind, over the whole tail.
  {
    const TgV3Pic* rp = &v->pics[REAR_PIC + (id & 31)];
    float zr = -L / 2 - 0.02f, rh = ymax * 0.98f;
    V3 bl = vadd(c, vadd(vmul(f, zr), vmul(r, -HW))), br = vadd(c, vadd(vmul(f, zr), vmul(r, HW)));
    V3 tl = vadd(bl, vmul(u, rh)), trr = vadd(br, vmul(u, rh));
    V3 centre = vadd(c, vmul(f, zr + 0.3f));
    begin_item(g, to_cam(g->cam, vadd(c, vmul(f, zr))).z - STAND_BIAS - 0.05f);
    if(rp->w) {
      V3 bq[4] = {br, trr, tl, bl};
      float ru[4] = {1, 1, 0, 0}, rv[4] = {1, 0, 0, 1};
      side_face(g, bq, ru, rv, 4, white, REAR_PIC + (id & 31), centre);
    }
    float yl = rh * 0.42f;
    if(braking)
      for(int k = -1; k <= 1; k += 2)
        mbox(g, vadd(c, vadd(vmul(f, zr - 0.02f), vmul(r, k * 0.62f))), f, r, u, 0.02f, 0.22f, yl - 0.06f, yl + 0.06f, 0xFFFF4030, 0.2f);
    if(nitro)
      for(int k = -1; k <= 1; k += 2) {
        uint32_t hh = hash32((uint32_t) (tick * 31 + k + id * 7));
        float len = 1.1f + (hh & 255) / 255.0f * 0.9f;
        V3 at = vadd(c, vadd(vmul(f, zr - 0.05f), vadd(vmul(r, k * 0.45f), vmul(u, rh * 0.22f))));
        V3 back = vmul(f, -1);
        nitro_flame(g, at, back, r, u, len);
      }
  }
  return true;
}

#define CAR_LEN 4.3f
#define CAR_HW 0.93f

static void make_shape(CarShape* cs, const TgV3Pic* sp) {
  memset(cs, 0, sizeof(*cs));
  int w = sp->w, h = sp->h;
  static int top[160], bot[160], body[160];
  float mpp = CAR_LEN / w, ys = mpp * 0.86f;
  cs->yscale = ys;
  int wingCols = 0, wingTopRow = h, wx0 = w, wx1 = -1;
  for(int x = 0; x < w; x++) {
    top[x] = h, bot[x] = -1, body[x] = h;
    for(int y = 0; y < h; y++)
      if(sp->px[y * w + x] >> 24) {
        if(top[x] == h) top[x] = y;
        bot[x] = y;
      }
    body[x] = top[x];
    // A wing: in the last quarter, a piece over a gap over the body.
    if(x > w * 3 / 4 && top[x] < h) {
      int y = top[x];
      while(y < h && (sp->px[y * w + x] >> 24)) y++;
      int gap = y;
      while(y < h && !(sp->px[y * w + x] >> 24)) y++;
      if(y < h && y - gap >= 2 && gap - top[x] <= 6) {
        body[x] = y;
        wingCols++;
        wingTopRow = top[x] < wingTopRow ? top[x] : wingTopRow;
        wx0 = x < wx0 ? x : wx0, wx1 = x > wx1 ? x : wx1;
      }
    }
  }
  if(wingCols >= 3) {
    cs->wing_top = (h - wingTopRow) * ys;
    cs->wing_z0 = CAR_LEN / 2 - wx0 * mpp, cs->wing_z1 = CAR_LEN / 2 - wx1 * mpp;
  }
  // The outline at each station, smoothed over a few columns.
  float roof = 0;
  for(int i = 0; i < CAR_STATIONS; i++) {
    float xf = (float) i / (CAR_STATIONS - 1) * (w - 1);
    float sum = 0, n = 0;
    for(int d = -2; d <= 2; d++) {
      int x = (int) (xf + 0.5f) + d;
      if(x < 0 || x >= w || body[x] >= h) continue;
      sum += (h - body[x]) * ys, n++;
    }
    cs->top[i] = n ? sum / n : 0.3f;
    roof = cs->top[i] > roof ? cs->top[i] : roof;
  }
  cs->belt = roof * 0.6f;
  // The wheels: where the outline reaches the ground, in the front and rear halves.
  float zs[2] = {0, 0}, ns[2] = {0, 0};
  for(int x = 0; x < w; x++)
    if(bot[x] >= h - 2) {
      int half = x < w / 2 ? 0 : 1;
      zs[half] += CAR_LEN / 2 - x * mpp, ns[half]++;
    }
  cs->wheel_z[0] = ns[0] ? zs[0] / ns[0] : 1.35f;
  cs->wheel_z[1] = ns[1] ? zs[1] / ns[1] : -1.35f;
  cs->version = sp->version + 1;
}

// A sports car without a side view yet: a generic outline.
static void default_shape(CarShape* cs) {
  static const float t[CAR_STATIONS] = {0.42f, 0.5f, 0.56f, 0.6f, 0.63f, 0.66f, 0.7f, 0.8f, 0.95f, 1.08f, 1.15f, 1.18f, 1.19f,
                                        1.19f, 1.18f, 1.15f, 1.08f, 0.98f, 0.88f, 0.8f, 0.76f, 0.74f, 0.73f, 0.72f, 0.7f, 0.62f};
  memcpy(cs->top, t, sizeof(t));
  cs->belt = 0.72f;
  cs->wheel_z[0] = 1.35f, cs->wheel_z[1] = -1.35f;
  cs->wing_top = 0;
  cs->version = 1;
}

// The model's width at a station (0 nose .. 1 tail): rounded off at both ends.
static float car_width(float t) {
  float a = t < 0.14f ? t / 0.14f : 1, b = t > 0.9f ? (1 - t) / 0.1f : 1;
  return CAR_HW * (0.6f + 0.4f * sqrtf(a)) * (0.82f + 0.18f * sqrtf(b < 0 ? 0 : b));
}

// How far the underside lifts at the nose and the tail (0 .. 1).
static float car_lift(float t) {
  float a = t < 0.1f ? 1 - t / 0.1f : 0, b = t > 0.92f ? (t - 0.92f) / 0.08f : 0;
  return a > b ? a : b;
}

// A face of the model: smooth (each corner lit by its own normal), culled when turned away.
// pic -1 for plain paint; else (u, v) per corner.
static void car_face(Gather* g, V3 a, V3 b, V3 c, V3 d, V3 na, V3 nb, V3 nc, V3 nd, uint32_t col, float depth, int pic,
                     const float* uv) {
  V3 n = vcross(vsub(b, a), vsub(d, a));
  if(vdot(n, vdot(n, vadd(vadd(na, nb), vadd(nc, nd))) < 0 ? vmul(n, -1) : n) == 0) return;
  V3 avg = vadd(vadd(na, nb), vadd(nc, nd));
  if(vdot(n, avg) < 0) n = vmul(n, -1);
  V3 fc = vmul(vadd(vadd(a, b), vadd(c, d)), 0.25f);
  if(vdot(n, vsub(fc, g->cam->eye)) >= 0) return;
  begin_item(g, to_cam(g->cam, fc).z - STAND_BIAS);  // each face sorted on its own
  uint32_t base = pic < 0 ? col : 0xFFFFFFFF;
  CV q[4] = {{to_cam(g->cam, a), uv ? uv[0] : 0, uv ? uv[1] : 0, fogged(lit(base, vnorm(na)), depth)},
             {to_cam(g->cam, b), uv ? uv[2] : 0, uv ? uv[3] : 0, fogged(lit(base, vnorm(nb)), depth)},
             {to_cam(g->cam, c), uv ? uv[4] : 0, uv ? uv[5] : 0, fogged(lit(base, vnorm(nc)), depth)},
             {to_cam(g->cam, d), uv ? uv[6] : 0, uv ? uv[7] : 0, fogged(lit(base, vnorm(nd)), depth)}};
  emit_poly(g, q, 4, q[0].c, pic);
}

// A car's front, made: its paint, a dark grille and intake low down, two headlights with
// chrome rims, a plate; shaded darker towards the bottom.
static void make_front(TgV3Pic* dst, uint32_t paint) {
  enum { FW = 128, FH = 48 };
  static uint32_t px[FW * FH];
  for(int y = 0; y < FH; y++)
    for(int x = 0; x < FW; x++) {
      float fy = (float) y / FH, fx = (float) x / FW;
      uint32_t c = shade(paint, 1.08f - fy * 0.45f);
      // Headlights: a slanted pod each side, bright with a chrome rim.
      for(int k = 0; k < 2; k++) {
        float cx = k ? 0.8f : 0.2f, dx = (fx - cx) / 0.14f, dy = (fy - 0.3f) / 0.11f;
        float d = dx * dx + dy * dy + dx * dy * (k ? -0.6f : 0.6f);
        if(d < 1.25f) c = 0xFFC8CED6;
        if(d < 1) c = d < 0.4f ? 0xFFFFFFFF : 0xFFF2F0E0;
      }
      // The grille between them, and the intake below.
      if(fy > 0.24f && fy < 0.4f && fx > 0.36f && fx < 0.64f) c = ((y + x / 3) & 2) ? 0xFF16181C : 0xFF2C3036;
      if(fy > 0.58f && fy < 0.82f && fx > 0.12f && fx < 0.88f) c = (y & 2) ? 0xFF101214 : 0xFF22262A;
      // The plate.
      if(fy > 0.62f && fy < 0.78f && fx > 0.42f && fx < 0.58f) c = (fx > 0.44f && fx < 0.56f && fy > 0.65f && fy < 0.75f && (x & 3) == 1) ? 0xFF303030 : 0xFFF2F2E8;
      // A splitter along the bottom.
      if(fy > 0.9f) c = 0xFF141416;
      px[y * FW + x] = c;
    }
  set_pic(dst, FW, FH, px, FW);
  dst->smooth = true;
}

// The car, modelled: a body lofted through rounded sections along its outline, the
// glasshouse on it, the select screen's picture of its side on its flanks (upscaled), wheels,
// lights, mirrors, a wing if it has one.
static bool model_car(Gather* g, TgView3D* v, int id, V3 c, V3 f, V3 r, V3 u, float depth, float metres, float yaw,
                      bool braking, bool nitro, int tick, uint32_t paint) {
  int type = id == 0 ? v->ptype[0] : id == 0x1F ? v->ptype[1] : (id * 7 + 1) % 4;
  const TgV3Pic* src = &v->pics[SIDE_SRC + type];
  CarShape* cs = &v->shape[4];
  if(src->w) {
    cs = &v->shape[type];
    if(cs->version != src->version + 1) make_shape(cs, src);
  } else if(!cs->version) {
    default_shape(cs);
  }
  // The flanks' picture: the side view in this car's paint, upscaled.
  int pic = -1;
  if(src->w) {
    const TgV3Pic* sp = side_picture(v, id, paint);
    TgV3Pic* hi = &v->pics[SIDE_HI + (id & 31)];
    if(sp && (!hi->w || hi->version == 0 || v->side_made[id & 31] != hi->replaced + 0u)) {
      // (re)made when the side picture changes: tracked by its version
    }
    if(sp) {
      static uint32_t hiOf[32];
      if(!hi->w || hiOf[id & 31] != sp->version) {
        upscale4(hi, sp);
        hiOf[id & 31] = sp->version;
      }
      pic = hi->w ? SIDE_HI + (id & 31) : -1;
    }
  }
  // The players' cars in the select screen's colours; the others in their own.
  uint32_t body = paint ? paint : 0xFFB0B0B8;
  if(src->w && (id == 0 || id == 0x1F)) {
    uint32_t pc = paint_colour(src);
    if(pc) body = pc;
  }
  // The tail's and nose's pictures.
  int rearPic = -1, frontPic = -1;
  {
    static uint32_t rearOf[32], frontOf[32];
    const TgV3Pic* rp = &v->pics[REAR_PIC + (id & 31)];
    TgV3Pic* rh = &v->pics[REAR_HI + (id & 31)];
    if(rp->w && rearOf[id & 31] != rp->version) {
      upscale4(rh, rp);
      rearOf[id & 31] = rp->version;
    }
    if(rp->w && rh->w) rearPic = REAR_HI + (id & 31);
    TgV3Pic* fp = &v->pics[FRONT_PIC + (id & 31)];
    if(!fp->w || frontOf[id & 31] != body) {
      make_front(fp, body);
      frontOf[id & 31] = body;
    }
    if(fp->w) frontPic = FRONT_PIC + (id & 31);
  }
  const float L = CAR_LEN, sill = 0.17f;
  float pich = src->w ? src->h * cs->yscale : 1.4f;  // the side view's height in metres (for its v)
  // Points of a section at station i: lower body (8) and glasshouse (6), in car space.
  #define SECT 11
  V3 lb[CAR_STATIONS][SECT], ln[CAR_STATIONS][SECT], gh[CAR_STATIONS][6], gn[CAR_STATIONS][6];
  float zt[CAR_STATIONS];
  bool cabin[CAR_STATIONS];
  for(int i = 0; i < CAR_STATIONS; i++) {
    float t = (float) i / (CAR_STATIONS - 1), z = L / 2 - t * L, wz = car_width(t);
    zt[i] = z;
    float top = cs->top[i], yt = top < cs->belt ? top : cs->belt;
    if(yt < sill + 0.12f) yt = sill + 0.12f;
    float slope = 0;
    if(i > 0 && i < CAR_STATIONS - 1) slope = (cs->top[i - 1] - cs->top[i + 1]) / (2 * L / (CAR_STATIONS - 1));
    // A rounded section (a superellipse): from under the left sill, up over the top, down to
    // under the right one.
    float sl = sill + car_lift(t) * 0.14f;
    if(yt < sl + 0.1f) yt = sl + 0.1f;
    float yc = sl + (yt - sl) * 0.4f, up = yt - yc, dn = yc - sl;
    bool onTop = top <= cs->belt;  // bonnet or boot: the top follows the outline's slope
    for(int k = 0; k < SECT; k++) {
      float th = -PI / 2 + PI * k / (SECT - 1);  // -90 (left, low) .. 90 (right, low) through the top
      float ang = th < 0 ? -PI / 2 - th : PI / 2 - th;  // 0 at the side's middle, +-90 at top or bottom
      (void) ang;
      // Walk round: left side bottom -> top -> right side bottom.
      float phi = PI * k / (SECT - 1);  // 0 left .. pi right, via the top (pi/2)
      float cx = -cosf(phi), cyy = sinf(phi);
      float ex = 0.38f;  // superellipse exponent 2/n: a squarish but rounded section
      float x = wz * (cx < 0 ? -1 : 1) * powf(fabsf(cx), ex), y = yc + up * powf(cyy, ex);
      if(k == 0 || k == SECT - 1) y = sl;  // down to the sill
      lb[i][k] = v3(x, y, z);
      float gx = (cx < 0 ? -1 : 1) * powf(fabsf(cx), 2 - ex) / wz, gy = powf(cyy, 2 - ex) / (up > 0.05f ? up : 0.05f);
      if(k == 0 || k == SECT - 1) gy = -0.15f / (dn > 0.05f ? dn : 0.05f);
      V3 n = vnorm(v3(gx, gy, 0));
      if(onTop) n = vnorm(vadd(n, v3(0, 0, slope * n.y)));
      ln[i][k] = n;
    }
    cabin[i] = top > cs->belt + 0.06f;
    float wb = wz * 0.84f, wr = wz * 0.64f, yr = cabin[i] ? top : cs->belt + 0.02f;
    float gx[6] = {-wb, -wr, -wr * 0.9f, wr * 0.9f, wr, wb}, gy[6] = {cs->belt, yr - 0.07f, yr, yr, yr - 0.07f, cs->belt};
    static const float gnx[6] = {-0.8f, -0.6f, -0.2f, 0.2f, 0.6f, 0.8f}, gny[6] = {0.6f, 0.75f, 1, 1, 0.75f, 0.6f};
    for(int k = 0; k < 6; k++) {
      gh[i][k] = v3(gx[k], gy[k], z);
      gn[i][k] = vnorm(v3(gnx[k], gny[k], k >= 1 && k <= 4 ? slope * 1.2f : 0));
    }
  }
  // Into the world.
  #define W(p) vadd(c, vadd(vmul(r, (p).x), vadd(vmul(u, (p).y), vmul(f, (p).z))))
  #define WN(n) vadd(vmul(r, (n).x), vadd(vmul(u, (n).y), vmul(f, (n).z)))
  uint32_t glass = 0xFF1A2430, skirt = shade(body, 0.45f);
  for(int i = 0; i + 1 < CAR_STATIONS; i++) {
    float u0 = (float) i / (CAR_STATIONS - 1), u1 = (float) (i + 1) / (CAR_STATIONS - 1);
    for(int k = 0; k < SECT - 1; k++) {
      V3 a = W(lb[i][k]), b = W(lb[i + 1][k]), cc = W(lb[i + 1][k + 1]), d = W(lb[i][k + 1]);
      V3 na = WN(ln[i][k]), nb = WN(ln[i + 1][k]), nc = WN(ln[i + 1][k + 1]), nd = WN(ln[i][k + 1]);
      bool flank = k <= 2 || k >= SECT - 4;  // the sides (the picture); the rest is the top
      uint32_t col = k == 0 || k == SECT - 2 ? skirt : body;
      if(flank && pic >= 0) {
        // The flank: the side view, by height and length (its nose to the left).
        float va = 1 - lb[i][k].y / pich, vb = 1 - lb[i + 1][k].y / pich, vc = 1 - lb[i + 1][k + 1].y / pich, vd = 1 - lb[i][k + 1].y / pich;
        float uv[8] = {u0, va, u1, vb, u1, vc, u0, vd};
        car_face(g, a, b, cc, d, na, nb, nc, nd, col, depth, pic, uv);
      } else {
        car_face(g, a, b, cc, d, na, nb, nc, nd, col, depth, -1, NULL);
      }
    }
    if(cabin[i] || cabin[i + 1]) {
      for(int k = 0; k < 5; k++) {
        V3 a = W(gh[i][k]), b = W(gh[i + 1][k]), cc = W(gh[i + 1][k + 1]), d = W(gh[i][k + 1]);
        V3 na = WN(gn[i][k]), nb = WN(gn[i + 1][k]), nc = WN(gn[i + 1][k + 1]), nd = WN(gn[i][k + 1]);
        bool side = k == 0 || k == 4;
        float rise = fabsf(gh[i][2].y - gh[i + 1][2].y) / (zt[i] - zt[i + 1]);
        bool screen = !side && rise > 0.32f;  // a windscreen or rear window
        if(side && pic >= 0) {
          float va = 1 - gh[i][k].y / pich, vb = 1 - gh[i + 1][k].y / pich, vc = 1 - gh[i + 1][k + 1].y / pich, vd = 1 - gh[i][k + 1].y / pich;
          float uv[8] = {u0, va, u1, vb, u1, vc, u0, vd};
          car_face(g, a, b, cc, d, na, nb, nc, nd, glass, depth, pic, uv);
        } else {
          car_face(g, a, b, cc, d, na, nb, nc, nd, side || screen ? glass : body, depth, -1, NULL);
        }
      }
    }
    // The nose and the tail.
    if(i == 0 || i + 2 == CAR_STATIONS) {
      int s = i == 0 ? 0 : CAR_STATIONS - 1;
      CV q[SECT];
      V3 nrm = i == 0 ? f : vmul(f, -1);
      V3 cen = W(v3(0, 0.4f, zt[s]));
      if(vdot(nrm, vsub(cen, g->cam->eye)) < 0) {
        begin_item(g, to_cam(g->cam, cen).z - STAND_BIAS);
        // The tail: the car's own picture from behind (upscaled); the nose: its made front.
        int capPic = i == 0 ? frontPic : rearPic;
        float w0 = car_width(i == 0 ? 0 : 1), y0 = lb[s][0].y, y1 = y0;
        for(int k = 0; k < SECT; k++) y1 = lb[s][k].y > y1 ? lb[s][k].y : y1;
        for(int k = 0; k < SECT; k++) {
          float uu = (lb[s][k].x / w0 + 1) / 2, vv = 1 - (lb[s][k].y - y0) / (y1 - y0 > 0.05f ? y1 - y0 : 0.05f);
          if(i != 0) uu = 1 - uu;  // seen from behind: the car's left on the left
          q[k] = (CV) {to_cam(g->cam, W(lb[s][k])), uu, i != 0 ? 0.08f + vv * 0.62f : vv, 0};
        }
        uint32_t capC = capPic >= 0 ? fogged(lit(0xFFFFFFFF, nrm), depth) : fogged(lit(i == 0 ? body : shade(body, 0.6f), nrm), depth);
        emit_poly(g, q, SECT, capC, capPic);
      }
    }
  }
  // The tail's lights and the nose's lamps, the exhausts.
  float zr = -L / 2 - 0.02f, ytail = cs->top[CAR_STATIONS - 1] < cs->belt ? cs->top[CAR_STATIONS - 1] : cs->belt;
  float yl = sill + (ytail - sill) * 0.7f;
  uint32_t tail = braking ? 0xFFFF4030 : 0xFFB0140C;
  // The rear picture has its own lights: only braking adds a glow over them.
  if(rearPic < 0) mbox(g, W(v3(0, 0, zr)), f, r, u, 0.02f, car_width(1) * 0.86f, yl - 0.05f, yl + 0.04f, shade(tail, 0.7f), 0.2f);
  for(int k = -1; k <= 1; k += 2) {
    if(rearPic < 0 || braking)
      mbox(g, W(v3(k * car_width(1) * 0.62f, 0, zr - 0.01f)), f, r, u, 0.02f, 0.2f, yl - 0.07f, yl + 0.06f, tail, 0.21f);
    mbox(g, W(v3(k * 0.4f, 0, zr - 0.02f)), f, r, u, 0.06f, 0.05f, sill + 0.06f, sill + 0.14f, 0xFF2A2A2E, 0.21f);
    float yh = cs->top[0] * 0.8f;
    mbox(g, W(v3(k * car_width(0) * 0.6f, 0, L / 2 - 0.05f)), f, r, u, 0.03f, 0.18f, yh - 0.05f, yh + 0.03f, 0xFFFFF6D8, 0.2f);
    // Mirrors.
    mbox(g, W(v3(k * car_width(0.4f) * 0.98f, 0, L / 2 - L * 0.36f)), f, r, u, 0.07f, 0.06f, cs->belt, cs->belt + 0.1f, body, 0.06f);
  }
  // A wing.
  if(cs->wing_top > 0) {
    float zm = (cs->wing_z0 + cs->wing_z1) / 2, hl = fabsf(cs->wing_z0 - cs->wing_z1) / 2 + 0.08f;
    mbox(g, W(v3(0, 0, zm)), f, r, u, hl, car_width(0.9f) * 0.96f, cs->wing_top - 0.06f, cs->wing_top, shade(body, 0.85f), 0.15f);
    for(int k = -1; k <= 1; k += 2)
      mbox(g, W(v3(k * 0.55f, 0, zm)), f, r, u, 0.05f, 0.03f, ytail - 0.05f, cs->wing_top - 0.06f, 0xFF202024, 0.14f);
  }
  // Wheels: tyres, rims and turning spokes; the front ones steer.
  float spin = metres / 0.33f;
  for(int ax = 0; ax < 2; ax++)
    for(int j = -1; j <= 1; j += 2) {
      float t = (L / 2 - cs->wheel_z[ax]) / L;
      V3 wc = W(v3(j * (car_width(t) - 0.1f), 0.33f, cs->wheel_z[ax]));
      float st = ax == 0 ? yaw * 1.5f : 0;
      V3 wf = vadd(vmul(f, cosf(st)), vmul(r, sinf(st))), wr = vnorm(vcross(wf, u));
      V3 out = vmul(wr, (float) j);
      frustum(g, vadd(wc, vmul(out, -0.12f)), out, wf, u, 0.33f, 0.33f, 0.24f, 12, 0xFF141416, 0.03f);
      frustum(g, vadd(wc, vmul(out, 0.12f)), out, wf, u, 0.23f, 0.21f, 0.012f, 12, 0xFFC4CAD2, 0.04f);
      if(depth < 60)
        for(int sp = 0; sp < 5; sp++) {
          float a = spin + sp * 2 * PI / 5;
          V3 d1 = vadd(vmul(wf, cosf(a)), vmul(u, sinf(a)));
          V3 d2 = vnorm(vcross(out, d1));
          mbox(g, vadd(vadd(wc, vmul(out, 0.14f)), vmul(d1, 0.1f)), d1, d2, out, 0.1f, 0.02f, -0.006f, 0.006f, 0xFF707680, 0.05f);
        }
    }
  if(nitro)
    for(int k = -1; k <= 1; k += 2) {
      uint32_t hh = hash32((uint32_t) (tick * 31 + k + id * 7));
      float len = 1.1f + (hh & 255) / 255.0f * 0.9f;
      V3 at = W(v3(k * 0.4f, sill + 0.1f, zr - 0.05f));
      V3 back = vmul(f, -1);
      nitro_flame(g, at, back, r, u, len);
    }
  #undef W
  #undef WN
  #undef SECT
  return true;
}

// --- The cars, modelled ----------------------------------------------------------------
// One continuous body per car: a ring of points at each station along it, from under the
// left sill, up the side, over the shoulder, the glasshouse and the roof, and down the
// right; the same points at every station, so the surface is one piece and shades
// smoothly. The proportions are the select screen's car's; the surfaces are clean paint
// and glass with a highlight (no pixel art on them).
#define RING 18
#define STA 30

static uint32_t clean_paint(uint32_t c) {
  // A car paint from a picture's colour: as saturated, a little brighter.
  float r = (c >> 16 & 255) / 255.0f, g = (c >> 8 & 255) / 255.0f, b = (c & 255) / 255.0f;
  float mx = fmaxf(r, fmaxf(g, b)), mn = fminf(r, fminf(g, b));
  if(mx < 0.01f) return 0xFF202024;
  float k = 0.74f / mx;  // bring the brightest channel to 0.74
  if(mx - mn < 0.12f) k = (0.8f + 0.1f * mx) / mx;  // greys and silvers stay grey
  r *= k, g *= k, b *= k;
  return 0xFF000000u | (uint32_t) (fminf(r, 1) * 255) << 16 | (uint32_t) (fminf(g, 1) * 255) << 8 | (uint32_t) (fminf(b, 1) * 255);
}

// Paint lit: diffuse by the sun, a sky reflection from above, a sharp highlight.
static uint32_t paint_lit(uint32_t col, V3 n, V3 pos, V3 eye, float gloss) {
  V3 L = vnorm(kLight), E = vnorm(vsub(eye, pos));
  float d = vdot(n, L), sky = 0.5f + 0.5f * n.y;
  if(g_night) d *= 0.35f, sky *= 0.6f;
  float k = 0.42f + 0.5f * (d > 0 ? d : 0) + 0.18f * sky;
  V3 hlf = vnorm(vadd(L, E));
  float sp = vdot(n, hlf);
  sp = sp > 0 ? powf(sp, 40) * gloss : 0;
  float fres = 1 - fabsf(vdot(n, E));
  fres = fres * fres * fres * 0.25f * gloss;
  int out[3];
  for(int ch = 0; ch < 3; ch++) {
    float c = (col >> (16 - ch * 8) & 255) * k + 255 * (sp + fres);
    out[ch] = c > 255 ? 255 : (int) c;
  }
  return 0xFF000000u | (uint32_t) out[0] << 16 | (uint32_t) out[1] << 8 | (uint32_t) out[2];
}

static void clean_face(Gather* g, const V3* p, const V3* n, const uint32_t* col, float depth) {
  V3 fn = vcross(vsub(p[1], p[0]), vsub(p[3], p[0]));
  V3 avg = vadd(vadd(n[0], n[1]), vadd(n[2], n[3]));
  if(vdot(fn, avg) < 0) fn = vmul(fn, -1);
  V3 fc = vmul(vadd(vadd(p[0], p[1]), vadd(p[2], p[3])), 0.25f);
  if(vdot(fn, vsub(fc, g->cam->eye)) >= 0) return;
  begin_item(g, to_cam(g->cam, fc).z - STAND_BIAS);
  CV q[4];
  for(int k = 0; k < 4; k++) q[k] = (CV) {to_cam(g->cam, p[k]), 0, 0, fogged(col[k], depth)};
  emit_poly(g, q, 4, q[0].c, -1);
}

// --- Car models from files (art3d/cars/car-<name>.tgcar, made in Blender) ------------------
// Levels of detail of triangles; per corner its place (x right, y up, z forward, metres, the
// footprint's middle at 0), normal, material class and colour; from version 3 its place in
// the car's texture (tools/cars/, docs/CARS.md).
typedef struct {
  float p[3], n[3];
  uint8_t k, r, g, b;
  uint8_t ao;  // ambient occlusion 0-31 (finish_corners)
  uint16_t u, v;  // in the texture (0-65535)
} CarCorner;

typedef struct {
  bool tried;
  int levels, tris[3];
  CarCorner* c[3];
  uint32_t flags[3];  // per level (version 3): bit 0 it has an interior (the glass is see-through)
  int wheelTris;      // the wheel (version 2): one, around its hub, as the left front one
  CarCorner* wheel;
  int wheelFixedTris, wheelRTris;  // version 3: the caliper (steers, doesn't spin); the right
  CarCorner* wheelFixed;           // wheel as its own (its lettering reads the right way round)
  CarCorner* wheelR;
  float hub[4][3];    // front left, front right, rear left, rear right
  float side;         // the body's half width at the doors (for the decals)
  // Version 3: where the race number goes (along, up, out, radius; its shape - 0 a roundel,
  // 1 a panel - and the digits' colour are painted in the texture, the digits are drawn here:
  // every car its own number), the texture's white block (plain-coloured corners: u, v), the
  // texture with its mip levels.
  bool num;
  float numAt[4];
  uint32_t numColour;
  uint16_t white[2];
  uint32_t* tex;       // the texture with its contrasting decals in white (for dark paint)...
  uint32_t* texDark;   // ...and in black (for light paint; NULL: the file has no ink mask)
  int texW, texH, texMips;
  uint32_t ownPaint;   // the model's own paint (the players' cars)
  // Lighter levels made here for cars small on screen (a grid of 20 cars is the race's
  // heaviest picture: it slowed the start of each race): the file's lightest level and the
  // wheel simplified by vertex clustering.
  int farTris[2], wheelFarTris[3];
  CarCorner* far[2];
  CarCorner* wheelFar[3];  // [2]: a plain tyre and disc (cars far away or small on screen)
} CarMesh;

// The model files are read whole, in one go, then taken apart in memory (a read per corner
// was thousands of small reads: seconds on a slow or network disk, inside a race frame).
typedef struct {
  const uint8_t* p;
  size_t left;
} Bytes;

static bool take(Bytes* b, void* out, size_t n) {
  if(b->left < n) return false;
  memcpy(out, b->p, n);
  b->p += n, b->left -= n;
  return true;
}

// n triangles' corners: 28 bytes each (versions 1, 2), 32 with the texture's u, v (version 3).
static CarCorner* read_corners(Bytes* b, uint32_t n, uint32_t ver) {
  size_t rs = ver >= 3 ? 32 : 28;
  if(b->left < (size_t) n * 3 * rs) return NULL;
  CarCorner* c = calloc((size_t) n * 3 + 1, sizeof(CarCorner));
  if(!c) return NULL;
  for(uint32_t i = 0; i < n * 3; i++) {
    const uint8_t* rec = b->p + (size_t) i * rs;
    memcpy(c[i].p, rec, 12);
    memcpy(c[i].n, rec + 12, 12);
    c[i].k = rec[24], c[i].r = rec[25], c[i].g = rec[26], c[i].b = rec[27];
    if(rs == 32) c[i].u = (uint16_t) (rec[28] | rec[29] << 8), c[i].v = (uint16_t) (rec[30] | rec[31] << 8);
  }
  b->p += (size_t) n * 3 * rs, b->left -= (size_t) n * 3 * rs;
  return c;
}

// A texture's mip levels after it (each half the last, a 2 x 2 box, to 1 x 1): *mips how many.
// The alpha (the tint mask) averages like the colour. NULL if out of memory.
static uint32_t* with_mips(const uint32_t* px, int w, int h, int* mips) {
  size_t total = 0;
  int n = 0;
  for(int lw = w, lh = h;; lw = lw > 1 ? lw / 2 : 1, lh = lh > 1 ? lh / 2 : 1) {
    total += (size_t) lw * lh;
    if(lw == 1 && lh == 1) break;
    n++;
  }
  uint32_t* out = malloc(total * 4);
  if(!out) return NULL;
  memcpy(out, px, (size_t) w * h * 4);
  uint32_t* src = out;
  int sw = w, sh = h;
  for(int l = 0; l < n; l++) {
    int dw = sw > 1 ? sw / 2 : 1, dh = sh > 1 ? sh / 2 : 1;
    uint32_t* dst = src + (size_t) sw * sh;
    for(int y = 0; y < dh; y++)
      for(int x = 0; x < dw; x++) {
        int x0 = x * 2 < sw ? x * 2 : sw - 1, x1 = x * 2 + 1 < sw ? x * 2 + 1 : sw - 1;
        int y0 = y * 2 < sh ? y * 2 : sh - 1, y1 = y * 2 + 1 < sh ? y * 2 + 1 : sh - 1;
        uint32_t q[4] = {src[y0 * sw + x0], src[y0 * sw + x1], src[y1 * sw + x0], src[y1 * sw + x1]}, o = 0;
        for(int s = 0; s < 32; s += 8)
          o |= (((q[0] >> s & 255) + (q[1] >> s & 255) + (q[2] >> s & 255) + (q[3] >> s & 255) + 2) / 4) << s;
        dst[y * dw + x] = o;
      }
    src = dst, sw = dw, sh = dh;
  }
  *mips = n;
  return out;
}

// A whole file in memory (NULL if it can't be read); *n its size.
static uint8_t* read_file(const char* path, size_t* n) {
  FILE* f = fopen(path, "rb");
  if(!f) return NULL;
  uint8_t* buf = NULL;
  if(fseek(f, 0, SEEK_END) == 0) {
    long sz = ftell(f);
    if(sz > 0 && sz < (64L << 20) && fseek(f, 0, SEEK_SET) == 0 && (buf = malloc((size_t) sz)) != NULL) {
      if(fread(buf, 1, (size_t) sz, f) != (size_t) sz) {
        free(buf);
        buf = NULL;
      } else {
        *n = (size_t) sz;
      }
    }
  }
  fclose(f);
  return buf;
}

// A mesh simplified by vertex clustering (Rossignac & Borrel): every corner moves to the
// middle of the corners in its cell of a grid `cell` metres wide; triangles that lose an
// edge go. Each corner keeps its normal, material and colour. *outN: the triangles left.
static CarCorner* cluster_corners(const CarCorner* in, int n, float cell, int* outN) {
  *outN = 0;
  int nc3 = n * 3, cap = 1;
  while(cap < nc3 * 2) cap <<= 1;
  uint32_t* keys = malloc(sizeof(uint32_t) * (size_t) cap);
  int* slot = malloc(sizeof(int) * (size_t) cap);
  float* sum = calloc((size_t) nc3 * 4, sizeof(float));
  int* map = malloc(sizeof(int) * (size_t) nc3);
  CarCorner* out = malloc(sizeof(CarCorner) * (size_t) (nc3 ? nc3 : 1));
  if(!keys || !slot || !sum || !map || !out) {
    free(keys), free(slot), free(sum), free(map), free(out);
    return NULL;
  }
  memset(keys, 0xFF, sizeof(uint32_t) * (size_t) cap);
  int nc = 0;
  for(int i = 0; i < nc3; i++) {
    uint32_t key = 0;
    for(int a = 0; a < 3; a++) {
      int q = (int) floorf(in[i].p[a] / cell) + 512;
      q = q < 0 ? 0 : q > 1023 ? 1023 : q;
      key |= (uint32_t) q << (10 * a);
    }
    uint32_t h = hash32(key) & (uint32_t) (cap - 1);
    while(keys[h] != 0xFFFFFFFFu && keys[h] != key) h = (h + 1) & (uint32_t) (cap - 1);
    if(keys[h] == 0xFFFFFFFFu) keys[h] = key, slot[h] = nc++;
    int c = slot[h];
    for(int a = 0; a < 3; a++) sum[c * 4 + a] += in[i].p[a];
    sum[c * 4 + 3] += 1;
    map[i] = c;
  }
  int m = 0;
  for(int t = 0; t < n; t++) {
    int a = map[t * 3], b = map[t * 3 + 1], c = map[t * 3 + 2];
    if(a == b || b == c || a == c) continue;
    for(int k = 0; k < 3; k++) {
      CarCorner o = in[t * 3 + k];
      int cl = map[t * 3 + k];
      for(int x = 0; x < 3; x++) o.p[x] = sum[cl * 4 + x] / sum[cl * 4 + 3];
      out[m * 3 + k] = o;
    }
    m++;
  }
  free(keys), free(slot), free(sum), free(map);
  *outN = m;
  return out;
}

// --- Shaded materials: the cars' paint, glass, metal, rubber and lamps ---------------------
// One lighting model for every renderer: tg_v3_raster and the SDL fallback run shade_mat
// below, gl3d.c's fragment shader is the same lines in GLSL (keep the two in step). The car
// models send each corner's own colour (albedo), its world normal and direction to the eye
// (TgV3Vertex.nrm / eye), and the light is worked out per pixel.
// Research (2026-11-03): car paint as a clear coat over a coloured base - the coat's
// reflection weighted by Fresnel (Schlick's approximation, ~4-5 % head on, all of it at
// grazing angles), the base dimmed by what the coat reflects (energy compensation), a sharp
// sun highlight on the coat and a broad one in the base (Autodesk/mental ray car paint,
// Maxon Redshift Car Paint, RenderMan PxrSurface clearcoat); the environment as a sky dome
// gradient sampled along the reflected ray (the matcap / sky-gradient trick of stylised
// racers, no cube map needed); glass a dark tint that is mostly reflection.

// What the coat mirrors along a ray whose height is ry (-1 down .. 1 up): a sky pale at the
// horizon and deep blue overhead with a bright band just above the horizon line, over a
// darker ground fading to near black straight down.
static V3 env_light(float ry, bool night) {
  V3 c;
  if(ry >= 0) {
    float t = sqrtf(ry), band = 1 - smooth01(0, 0.1f, ry);
    c = night ? vadd(vmul(v3(0.13f, 0.15f, 0.24f), 1 - t), vmul(v3(0.02f, 0.03f, 0.08f), t))
              : vadd(vmul(v3(0.92f, 0.95f, 1.0f), 1 - t), vmul(v3(0.27f, 0.47f, 0.86f), t));
    c = vadd(c, vmul(v3(1, 1, 1), band * (night ? 0.03f : 0.16f)));
  } else {
    float t = sat01(-ry * 1.6f), band = 1 - smooth01(0, 0.06f, -ry);  // the far ground's haze: a light line under the horizon
    c = night ? vadd(vmul(v3(0.06f, 0.06f, 0.07f), 1 - t), vmul(v3(0.02f, 0.02f, 0.02f), t))
              : vadd(vmul(v3(0.66f, 0.64f, 0.60f), 1 - t), vmul(v3(0.17f, 0.17f, 0.18f), t));
    c = vadd(c, vmul(v3(1, 1, 1), band * (night ? 0.02f : 0.12f)));
  }
  return c;
}

// c: the albedo (0-1) in, the lit colour out. N, E: unit, world (E towards the eye). spark: a
// metallic flake in the paint here (0-1, paint_spark; 0 without a texture).
static void shade_mat(float c[3], int mat, float ao, bool night, V3 N, V3 E, float spark) {
  static V3 L;
  if(L.y == 0) L = vnorm(kLight);
  float ne = vdot(N, E);
  if(ne < 0) N = vmul(N, -1), ne = -ne;  // seen from behind (two-sided glass)
  // (the ambient as bright as the flat-lit cars' was on a face turned from the sun - the chase
  // camera mostly sees the cars' backs - its occlusion soft; the reflections take it whole)
  float nl = vdot(N, L), amb = (0.36f + 0.24f * (0.5f + 0.5f * N.y)) * (0.7f + 0.3f * ao), dif = 0.62f * (nl > 0 ? nl : 0) * (0.55f + 0.45f * ao);
  if(night) amb *= 0.55f, dif *= 0.3f;
  float k = amb + dif;
  V3 env = env_light(2 * ne * N.y - E.y, night);  // along the reflected ray
  float fr = 1 - ne, fr3 = fr * fr * fr, fr5 = fr3 * fr * fr;
  V3 H = vnorm(vadd(L, E));
  float nh = vdot(N, H);
  nh = nh > 0 ? nh : 0;
  float p8 = nh * nh, p16, p32, p256;
  p8 *= p8, p8 *= p8, p16 = p8 * p8, p32 = p16 * p16, p256 = p32 * p32, p256 *= p256, p256 *= p256;
  float sun = night ? 0.25f : 1, o[3];
  for(int i = 0; i < 3; i++) {
    float e = i == 0 ? env.x : i == 1 ? env.y : env.z, a = c[i], F;
    switch(mat) {
      case TG_V3_MAT_PAINT:  // clear coat over the colour
        // (Schlick's fifth power, 0.7 at grazing: from the chase camera, decks and roofs are
        // seen at fr ~0.7 - more sky there turned the game's paints pastel; the rim stays bright)
        F = 0.04f + 0.7f * fr5;
        o[i] = a * k * 1.08f * (1 - F) + e * (F + 0.03f) * ao + (p256 * 2.4f + p32 * 0.12f) * sun + a * p16 * 0.25f * sun;
        o[i] += spark * (p16 * 1.5f * sun + e * 0.025f) * (0.6f + 0.4f * a);  // a flake catching the light
        if(night) o[i] += a * 0.1f + 0.012f;  // (the street lamps and the field's lights: the paint keeps its colour at night)
        break;
      case TG_V3_MAT_GLASS:  // a dark tint, mostly what it mirrors
        F = 0.1f + 0.9f * fr5;
        o[i] = a * 0.35f * k * (1 - F) + e * (F * 0.9f + 0.06f) + p256 * 3 * sun;
        break;
      case TG_V3_MAT_METAL:  // chrome: the surroundings in its own tint
        o[i] = a * k * 0.55f + e * (0.4f + 0.5f * fr5) * (0.5f + 0.5f * a) * ao + p32 * p8 * 1.8f * sun;
        break;
      case TG_V3_MAT_RUBBER:  // dull, darker underneath, a faint sheen at the sidewall's edge
        o[i] = a * k * (0.7f + 0.3f * sat01(N.y + 0.7f)) + e * fr5 * 0.12f * ao + p8 * 0.035f * sun;
        break;
      case TG_V3_MAT_LAMP:  // lit from inside, under a glass cover
        o[i] = a * (night ? 1.15f : 1.0f) + e * (0.04f + 0.5f * fr5) + p256 * 2 * sun;
        break;
      default:  // satin trim and plastic
        F = 0.04f + 0.96f * fr5;
        o[i] = a * k * (1 - F * 0.5f) + e * F * 0.35f * ao + p32 * 0.25f * sun;
        break;
    }
  }
  for(int i = 0; i < 3; i++) c[i] = o[i] > 1 ? 1 : o[i];
}

// The paint's metallic flake (owner: vivid rivals, "metallic flake sparkle if cheap"): one
// flake to a texel of the car's texture, one in twenty of them catching the light (an integer
// hash of the cell: the same in gl3d.c's shader), shown only while a texel covers more than
// about two thirds of a pixel (fp: texels per pixel) - smaller, flakes would twinkle as noise.
static float paint_spark(float u, float v, float fp) {
  float fade = 1 - smooth01(0.5f, 1.5f, fp);
  if(fade <= 0) return 0;
  uint32_t h = (uint32_t) (u * 1024) * 1597334677u ^ (uint32_t) (v * 1024) * 3812015801u;
  h ^= h >> 15, h *= 2246822519u, h ^= h >> 13;
  return (h & 255u) < 13u ? fade : 0;
}

static uint32_t snorm8(float x) {
  int q = (int) (x * 127 + 128.5f);  // (x >= -1: no floor needed)
  return (uint32_t) (q < 1 ? 1 : q > 255 ? 255 : q);
}
static V3 unsnorm8(uint32_t w) {
  return v3(((float) (w & 255) - 128) / 127, ((float) (w >> 8 & 255) - 128) / 127, ((float) (w >> 16 & 255) - 128) / 127);
}

// A shaded vertex's colour: its material lit at the vertex, hazed.
static uint32_t shaded_colour(uint32_t argb, uint32_t nrm, uint32_t eye, uint32_t haze) {
  float c[3] = {(argb >> 16 & 255) / 255.0f, (argb >> 8 & 255) / 255.0f, (argb & 255) / 255.0f};
  shade_mat(c, nrm >> 24 & 7, (float) (nrm >> 27) / 31, eye >> 31, vnorm(unsnorm8(nrm)), vnorm(unsnorm8(eye)), 0);
  float fog = (float) (eye >> 24 & 127) / 127;
  uint32_t out = argb & 0xFF000000u;
  for(int i = 0; i < 3; i++) {
    float h = (float) (haze >> (16 - 8 * i) & 255) / 255;
    int q = (int) ((c[i] + (h - c[i]) * fog) * 255 + 0.5f);
    out |= (uint32_t) (q < 0 ? 0 : q > 255 ? 255 : q) << (16 - 8 * i);
  }
  return out;
}

uint32_t tg_v3_vertex_colour(const TgV3Vertex* v, uint32_t haze) {
  return (v->nrm >> 24 & 7) ? shaded_colour(v->argb, v->nrm, v->eye, haze) : v->argb;
}

// The car models' material classes (export_cars_specialist_blender.py, tools/cars/) as shaded
// materials: light, unsaturated trim (rims, mirrors' arms, grilles' frames) is chrome, the rest
// satin; version 3 names the interior and chrome itself.
static int car_material(const CarCorner* k) {
  switch(k->k) {
    case 1: case 6: return TG_V3_MAT_PAINT;
    case 2: return TG_V3_MAT_GLASS;
    case 3: case 4: return TG_V3_MAT_LAMP;
    case 5: return TG_V3_MAT_RUBBER;
    case 7: return TG_V3_MAT_SATIN;  // (version 3) the interior
    case 8: return TG_V3_MAT_METAL;  // (version 3) chrome: rims, exhaust tips
    default: {
      int mx = k->r > k->g ? (k->r > k->b ? k->r : k->b) : (k->g > k->b ? k->g : k->b);
      int mn = k->r < k->g ? (k->r < k->b ? k->r : k->b) : (k->g < k->b ? k->g : k->b);
      float lum = (k->r * 0.3f + k->g * 0.59f + k->b * 0.11f) / 255;
      return lum > 0.42f && lum < 0.86f && mx - mn < 40 ? TG_V3_MAT_METAL : TG_V3_MAT_SATIN;
    }
  }
}

// Smooth normals (weld and smooth with a crease angle, as Blender's auto smooth, Java3D's
// NormalGenerator, Model I/O's addNormals): corners at the same place (0.5 mm) and of the same
// class share the area-weighted normals of their triangles that are within the crease angle
// of their own triangle's; across a sharper fold (a panel's edge, the tyre's shoulder) the
// edge stays hard. The decimated and clustered levels had faceted (or, once moved, wrong)
// normals: the light then jumped from level to level and the paint looked faceted. Then each
// corner's ambient occlusion: lower on the body and under it, darker.
typedef struct {
  uint64_t key;
  int i;
} CornerKey;
static int cmp_corner_key(const void* a, const void* b) {
  uint64_t x = ((const CornerKey*) a)->key, y = ((const CornerKey*) b)->key;
  return x < y ? -1 : x > y;
}

static void finish_corners(CarCorner* c, int n, bool wheel) {
  if(!c || n <= 0) return;
  int nc = n * 3;
  V3* fw = malloc(sizeof(V3) * (size_t) n);   // area-weighted face normals, turned outwards
  V3* fu = malloc(sizeof(V3) * (size_t) n);   // the same, unit
  CornerKey* ks = malloc(sizeof(CornerKey) * (size_t) nc);
  V3* nn = malloc(sizeof(V3) * (size_t) nc);
  if(!fw || !fu || !ks || !nn) {
    free(fw), free(fu), free(ks), free(nn);
    return;
  }
  for(int t = 0; t < n; t++) {
    const CarCorner* q = &c[t * 3];
    V3 a = v3(q[0].p[0], q[0].p[1], q[0].p[2]), b = v3(q[1].p[0], q[1].p[1], q[1].p[2]), d = v3(q[2].p[0], q[2].p[1], q[2].p[2]);
    V3 g = vcross(vsub(b, a), vsub(d, a));
    V3 s = v3(q[0].n[0] + q[1].n[0] + q[2].n[0], q[0].n[1] + q[1].n[1] + q[2].n[1], q[0].n[2] + q[1].n[2] + q[2].n[2]);
    if(vdot(g, s) < 0) g = vmul(g, -1);
    fw[t] = g;
    fu[t] = vnorm(g);
  }
  for(int i = 0; i < nc; i++) {
    uint64_t key = (uint64_t) (c[i].k & 15) << 60;
    for(int a = 0; a < 3; a++) {
      int64_t q = (int64_t) floorf(c[i].p[a] / 0.0005f + 0.5f) + (1 << 19);
      key |= (uint64_t) (q & 0xFFFFF) << (20 * a);
    }
    ks[i] = (CornerKey) {key, i};
  }
  qsort(ks, (size_t) nc, sizeof(CornerKey), cmp_corner_key);
  for(int s = 0; s < nc;) {
    int e = s + 1;
    while(e < nc && ks[e].key == ks[s].key) e++;
    for(int x = s; x < e; x++) {
      int i = ks[x].i, ti = i / 3;
      int k = c[i].k;
      float crease = k == 1 || k == 6 ? 0.766f : k == 5 ? 0.64f : k == 2 ? 0.866f : 0.82f;  // cos 40, 50, 30, 35 deg
      V3 sum = v3(0, 0, 0);
      for(int y = s; y < e; y++) {
        int tj = ks[y].i / 3;
        bool same = tj == ti;
        if(same) {  // each triangle once
          bool first = true;
          for(int z = s; z < y; z++) first &= ks[z].i / 3 != tj;
          if(!first) continue;
        } else {
          bool seen = false;
          for(int z = s; z < y; z++) seen |= ks[z].i / 3 == tj;
          if(seen) continue;
        }
        if(same || vdot(fu[ti], fu[tj]) > crease) sum = vadd(sum, fw[tj]);
      }
      nn[i] = vdot(sum, sum) > 1e-20f ? vnorm(sum) : v3(c[i].n[0], c[i].n[1], c[i].n[2]);
    }
    s = e;
  }
  for(int i = 0; i < nc; i++) {
    c[i].n[0] = nn[i].x, c[i].n[1] = nn[i].y, c[i].n[2] = nn[i].z;
    float ao = wheel ? 0.9f : 0.55f + 0.45f * smooth01(0.12f, 0.62f, c[i].p[1]);
    if(!wheel && nn[i].y < -0.4f) ao *= 0.75f;
    c[i].ao = (uint8_t) (ao * 31 + 0.5f);
  }
  free(fw), free(fu), free(ks), free(nn);
}

// A plain wheel for cars far away: a tyre (10 sides) and its outer face, a dark rubber ring
// round a grey disc, sized from the wheel model (left front: the axle along x, the outside
// at -x). 50 triangles instead of the model's 350-1300.
static CarCorner* plain_wheel(const CarCorner* in, int n, int* outN, const uint16_t white[2]) {
  float rad = 0, x0 = 1e9f, x1 = -1e9f;
  for(int i = 0; i < n * 3; i++) {
    float rr = sqrtf(in[i].p[1] * in[i].p[1] + in[i].p[2] * in[i].p[2]);
    if(rr > rad) rad = rr;
    if(in[i].p[0] < x0) x0 = in[i].p[0];
    if(in[i].p[0] > x1) x1 = in[i].p[0];
  }
  enum { S = 10 };
  CarCorner* out = malloc(sizeof(CarCorner) * S * 5 * 3);
  if(!out || rad <= 0) {
    free(out);
    *outN = 0;
    return NULL;
  }
  int m = 0;
  #define PUT(px, py, pz, nx, ny, nz, kk, cr, cg, cb) \
    out[m++] = (CarCorner) {{px, py, pz}, {nx, ny, nz}, kk, cr, cg, cb, 0, white[0], white[1]}  // (the texture's white: plain colour)
  for(int i = 0; i < S; i++) {
    float a0 = 2 * PI * i / S, a1 = 2 * PI * (i + 1) / S;
    float c0 = cosf(a0), s0 = sinf(a0), c1 = cosf(a1), s1 = sinf(a1), ri = rad * 0.62f;
    // the tread: two triangles, normals out from the axle
    PUT(x0, rad * c0, rad * s0, 0, c0, s0, 5, 30, 31, 34), PUT(x1, rad * c0, rad * s0, 0, c0, s0, 5, 30, 31, 34),
        PUT(x1, rad * c1, rad * s1, 0, c1, s1, 5, 30, 31, 34);
    PUT(x0, rad * c0, rad * s0, 0, c0, s0, 5, 30, 31, 34), PUT(x1, rad * c1, rad * s1, 0, c1, s1, 5, 30, 31, 34),
        PUT(x0, rad * c1, rad * s1, 0, c1, s1, 5, 30, 31, 34);
    // the sidewall: a ring on the outer face
    PUT(x0, rad * c0, rad * s0, -1, 0, 0, 5, 30, 31, 34), PUT(x0, ri * c1, ri * s1, -1, 0, 0, 5, 30, 31, 34),
        PUT(x0, rad * c1, rad * s1, -1, 0, 0, 5, 30, 31, 34);
    PUT(x0, rad * c0, rad * s0, -1, 0, 0, 5, 30, 31, 34), PUT(x0, ri * c0, ri * s0, -1, 0, 0, 5, 30, 31, 34),
        PUT(x0, ri * c1, ri * s1, -1, 0, 0, 5, 30, 31, 34);
    // the wheel's disc, a little inside
    PUT(x0 + 0.02f, 0, 0, -1, 0, 0, 0, 150, 156, 164), PUT(x0 + 0.02f, ri * c1, ri * s1, -1, 0, 0, 0, 150, 156, 164),
        PUT(x0 + 0.02f, ri * c0, ri * s0, -1, 0, 0, 0, 150, 156, 164);
  }
  #undef PUT
  *outN = m / 3;
  return out;
}

static CarMesh g_carMesh[5];
static const char* kCarFiles[5] = {"rosso", "glacier", "violet", "azure", "amber"};

static const CarMesh* car_mesh(const TgView3D* v, int m) {
  CarMesh* cm = &g_carMesh[m];
  if(cm->tried) return cm->levels ? cm : NULL;
  cm->tried = true;
  if(!v->art_dir[0]) return NULL;
  char path[1200];
  snprintf(path, sizeof(path), "%scars/car-%s.tgcar", v->art_dir, kCarFiles[m]);
  size_t size = 0;
  uint8_t* file = read_file(path, &size);
  if(!file) return NULL;
  Bytes b = {file, size};
  char magic[4];
  uint32_t ver = 0, levels = 0;
  if(take(&b, magic, 4) && memcmp(magic, "TGCR", 4) == 0 && take(&b, &ver, 4) && ver >= 1 && ver <= 3 &&
     take(&b, &levels, 4) && levels >= 1 && levels <= 3) {
    int ok = 0;
    for(uint32_t l = 0; l < levels; l++) {
      uint32_t n = 0, fl = 0;
      if((ver >= 3 && !take(&b, &fl, 4)) || !take(&b, &n, 4) || n > 200000) break;
      CarCorner* c = read_corners(&b, n, ver);
      if(!c) break;
      cm->c[l] = c, cm->tris[l] = (int) n, cm->flags[l] = fl;
      ok++;
    }
    cm->levels = ok;
    uint32_t wn = 0;
    if(ver >= 2 && ok == (int) levels && take(&b, &wn, 4) && wn > 0 && wn < 50000) {
      cm->wheel = read_corners(&b, wn, ver);
      bool parts = cm->wheel != NULL;
      if(ver >= 3 && parts) {  // the caliper, the right wheel
        uint32_t fn = 0, rn = 0;
        parts = take(&b, &fn, 4) && fn < 50000 && (cm->wheelFixed = read_corners(&b, fn, ver)) != NULL;
        parts = parts && take(&b, &rn, 4) && rn < 50000 && (cm->wheelR = read_corners(&b, rn, ver)) != NULL;
        if(parts) cm->wheelFixedTris = (int) fn, cm->wheelRTris = (int) rn;
      }
      if(parts && take(&b, cm->hub, sizeof(cm->hub))) cm->wheelTris = (int) wn;
    }
    // Version 3: the number's place, the white block, the texture (every level's corners
    // point into it).
    float white[2];
    uint32_t shape = 0, tw = 0, th = 0;
    if(ver >= 3 && cm->wheelTris && take(&b, cm->numAt, sizeof(cm->numAt)) && take(&b, &shape, 4) && take(&b, &cm->numColour, 4) &&
       take(&b, white, sizeof(white)) && take(&b, &tw, 4) && take(&b, &th, 4) && tw > 0 && th > 0 && tw <= 4096 && th <= 4096 &&
       b.left >= (size_t) tw * th * 4) {
      cm->num = true;
      for(int i = 0; i < 2; i++) cm->white[i] = (uint16_t) (white[i] < 0 ? 0 : white[i] > 1 ? 65535 : white[i] * 65535 + 0.5f);
      size_t np = (size_t) tw * th;
      uint32_t* base = malloc(np * 4);
      uint32_t inkOn = 0;
      if(base) memcpy(base, b.p, np * 4);
      b.p += np * 4, b.left -= np * 4;
      // The ink mask: the decals that must stand out from whatever paint the car wears, made
      // up here in white and in black (mesh_car picks by the paint's lightness).
      const uint8_t* ink = take(&b, &inkOn, 4) && inkOn == 1 && b.left >= np ? b.p : NULL;
      for(int k = 0; base && k < (ink ? 2 : 1); k++) {
        uint32_t* t = malloc(np * 4);
        if(!t) break;
        for(size_t i = 0; i < np; i++) {
          uint32_t c = base[i], q = ink ? ink[i] : 0;
          if(q) {  // rgb towards the ink, the paint's share (alpha) away
            uint32_t o = (q * (k ? 15 : 242) + 127) / 255, a = ((c >> 24) * (255 - q) + 127) / 255;
            uint32_t r = ((c >> 16 & 255) * (255 - q) + 127) / 255 + o, gg = ((c >> 8 & 255) * (255 - q) + 127) / 255 + o;
            uint32_t bb = ((c & 255) * (255 - q) + 127) / 255 + (q * (k ? 18 : 242) + 127) / 255;
            c = a << 24 | (r > 255 ? 255 : r) << 16 | (gg > 255 ? 255 : gg) << 8 | (bb > 255 ? 255 : bb);
          }
          t[i] = c;
        }
        uint32_t* m = with_mips(t, (int) tw, (int) th, &cm->texMips);
        free(t);
        if(k) cm->texDark = m;
        else cm->tex = m;
      }
      free(base);
      if(cm->tex) cm->texW = (int) tw, cm->texH = (int) th;
    }
  }
  free(file);
  if(cm->levels) {
    cm->side = 0;
    for(int i = 0; i < cm->tris[0] * 3; i++) {
      const CarCorner* k = &cm->c[0][i];
      if(k->k == 1 && fabsf(k->p[2]) < 0.6f && k->p[1] > 0.3f && k->p[1] < 0.7f && fabsf(k->p[0]) > cm->side) cm->side = fabsf(k->p[0]);
    }
    if(cm->side < 0.5f) cm->side = 0.9f;
    for(int i = 0; i < cm->tris[0] * 3 && !cm->ownPaint; i++)
      if(cm->c[0][i].k == 1) cm->ownPaint = 0xFF000000u | (uint32_t) cm->c[0][i].r << 16 | (uint32_t) cm->c[0][i].g << 8 | cm->c[0][i].b;
    static const float kCell[2] = {0.05f, 0.10f}, kWheelCell[2] = {0.05f, 0.11f};
    for(int f = 0; f < 2; f++) {
      cm->far[f] = cluster_corners(cm->c[cm->levels - 1], cm->tris[cm->levels - 1], kCell[f], &cm->farTris[f]);
      if(cm->wheelTris) cm->wheelFar[f] = cluster_corners(cm->wheel, cm->wheelTris, kWheelCell[f], &cm->wheelFarTris[f]);
    }
    if(cm->wheelTris) cm->wheelFar[2] = plain_wheel(cm->wheel, cm->wheelTris, &cm->wheelFarTris[2], cm->white);
    for(int l = 0; l < cm->levels; l++) finish_corners(cm->c[l], cm->tris[l], false);
    for(int f = 0; f < 2; f++) finish_corners(cm->far[f], cm->farTris[f], false);
    finish_corners(cm->wheel, cm->wheelTris, true);
    finish_corners(cm->wheelFixed, cm->wheelFixedTris, true);
    finish_corners(cm->wheelR, cm->wheelRTris, true);
    for(int f = 0; f < 3; f++) finish_corners(cm->wheelFar[f], cm->wheelFarTris[f], true);
    if(getenv("TG_CAR_LOD_INFO"))
      fprintf(stderr, "car %s: %d %d %d, far %d %d; wheel %d (+%d fixed), far %d %d; texture %dx%d, %d mips\n", kCarFiles[m], cm->tris[0],
              cm->tris[1], cm->tris[2], cm->farTris[0], cm->farTris[1], cm->wheelTris, cm->wheelFixedTris, cm->wheelFarTris[0],
              cm->wheelFarTris[1], cm->texW, cm->texH, cm->texMips);
  }
  return cm->levels ? cm : NULL;
}

// A model's textures as this view's pictures (copies: the view frees its own): with white
// decals at CAR_TEX + m, with black ones at CAR_TEX + 5 + m.
static void car_pic(TgView3D* v, int m) {
  const CarMesh* cm = car_mesh(v, m);
  for(int k = 0; cm && k < 2; k++) {
    const uint32_t* src = k ? cm->texDark : cm->tex;
    TgV3Pic* p = &v->pics[CAR_TEX + 5 * k + m];
    if(!src || p->px) continue;
    size_t n = 0;
    for(int l = 0, w = cm->texW, h = cm->texH; l <= cm->texMips; l++, w = w > 1 ? w / 2 : 1, h = h > 1 ? h / 2 : 1) n += (size_t) w * h;
    p->px = malloc(n * 4);
    if(!p->px) continue;
    memcpy(p->px, src, n * 4);
    p->w = cm->texW, p->h = cm->texH, p->mips = cm->texMips, p->mask = true, p->smooth = true;
    p->dirty_from = p->version, p->dirty_y0 = 0, p->dirty_y1 = p->h;
    p->version++;
  }
}

// All the car models, read now (as the art folder is set, before any race) rather than the
// first time a car is drawn: that read stalled the first frames of a race (QA-002); and their
// textures made this view's pictures (sent ahead on the loading card, tg_view3d_pic_warm).
static void preload_car_meshes(TgView3D* v) {
  for(int m = 0; m < 5; m++) car_pic(v, m);
}

// A car drawn from its model: each corner lit by its material (paint with a highlight,
// glass with a sharper one, rubber dull, the lights glowing; the tail lights brighter when
// braking). false if there is no model (the made one is drawn instead).
static bool g_showcase;  // the showcase: the most detailed models
static float g_meshCut = 45;  // cars nearer than this get their model (the nearest few)
static uint32_t g_paint;       // the car being drawn: its paint (the game's colour for it; 0: the model's own)
static int g_number;           // its race number (0: from its id)
static int g_traceLvl;          // (TG_CAR_TRACE: the level last chosen)
static int g_traceTot, g_traceKept;  // (TG_CAR_TRACE: model triangles seen, kept facing)

static float g_wheelMetres, g_wheelSteer;  // the car being drawn: how far it has rolled, its steering
static bool g_wheel_on;  // the wheels have their own frame (on the road; the body on its springs)
static float g_car_lift;  // the car being drawn stands this high off the road (its pit stop's jacks, m)
static V3 g_wheel_c, g_wheel_f, g_wheel_r, g_wheel_u;

// A set of model corners into the gather, in camera space: origin cc0, axes rx, uy, fz. pic: the
// model's texture (-1 none); glassA: the glass's opacity (255: none to see through).
static void mesh_corners(Gather* g, const CarCorner* cc, int n, V3 cc0, V3 rx, V3 uy, V3 fz, V3 L, V3 E, V3 hlf, float depth,
                         bool braking, bool wheel, int pic, uint32_t glassA) {
  const Cam* cam = g->cam;
  (void) L, (void) E, (void) hlf;
  // Each corner: its own colour, material, world normal and direction to the eye; the
  // renderer lights it (shade_mat). The haze is the same over a car: once.
  float fog = (depth - 60) / 620;
  fog = fog < 0 ? 0 : fog > 1 ? 1 : fog;
  fog = fog * fog * (3 - 2 * fog) * 0.7f;
  uint32_t eyeHi = ((uint32_t) (fog * 127 + 0.5f) | (g_night ? 0x80u : 0)) << 24;
  for(int t = 0; t < n; t++) {
    const CarCorner* tri = &cc[t * 3];
    V3 cp[3], cn[3];
    for(int k = 0; k < 3; k++) {
      cp[k] = vadd(cc0, vadd(vmul(rx, tri[k].p[0]), vadd(vmul(uy, tri[k].p[1]), vmul(fz, tri[k].p[2]))));
      cn[k] = vadd(vmul(rx, tri[k].n[0]), vadd(vmul(uy, tri[k].n[1]), vmul(fz, tri[k].n[2])));
    }
    V3 avg = vadd(vadd(cn[0], cn[1]), cn[2]);
    // Facing: the triangle's own plane, turned to the side its corners' normals say is out.
    // (On the clustered levels a triangle's corners have moved; judged by the normals alone
    // triangles still facing the camera were dropped: see-through cracks along the body -
    // player: "the models seem not continuous".) A sliver too thin to have a plane goes by
    // the normals.
    V3 gn = vcross(vsub(cp[1], cp[0]), vsub(cp[2], cp[0]));
    if(vdot(gn, gn) > 1e-10f) {
      if(vdot(gn, avg) < 0) gn = vmul(gn, -1);
    } else {
      gn = avg;
    }
    g_traceTot++;
    if(vdot(gn, cp[0]) > 0 && tri[0].k != 2) continue;
    g_traceKept++;
    // Lights sit just behind smoked covers in the model: brought 4 cm forward so they show.
    if(tri[0].k == 3 || tri[0].k == 4)
      for(int k = 0; k < 3; k++) {
        float len = sqrtf(vdot(cp[k], cp[k]));
        if(len > 0.1f) cp[k] = vmul(cp[k], 1 - 0.04f / len);
      }
    int mat = car_material(&tri[0]);
    TgV3Vertex sv[3];
    for(int k = 0; k < 3; k++) {
      uint32_t base = 0xFF000000u | (uint32_t) tri[k].r << 16 | (uint32_t) tri[k].g << 8 | tri[k].b;
      if(tri[k].k == 1 && g_paint && !wheel) base = g_paint;
      if(tri[k].k == 3) base = braking ? 0xFFFF4A30 : 0xFFD01A10;
      if(tri[k].k == 4) base = 0xFFFFF6E6;
      if(tri[k].k == 2) base = (base & 0xFFFFFF) | glassA << 24;  // (with an interior behind it)
      // camera space to world: the camera's axes
      V3 nw = vadd(vmul(cam->right, cn[k].x), vadd(vmul(cam->up, cn[k].y), vmul(cam->fwd, cn[k].z)));
      // (to the eye, scaled into -1..1 only: the renderer normalises it, no square root here)
      float ax = fabsf(cp[k].x), ay = fabsf(cp[k].y), az = fabsf(cp[k].z), mx = ax > ay ? ax : ay;
      float il = 0.57f / ((mx > az ? mx : az) + 1e-6f);
      V3 ew = vadd(vmul(cam->right, -cp[k].x * il), vadd(vmul(cam->up, -cp[k].y * il), vmul(cam->fwd, -cp[k].z * il)));
      uint32_t nrm = snorm8(nw.x) | snorm8(nw.y) << 8 | snorm8(nw.z) << 16 | (uint32_t) (mat | (tri[k].ao & 31) << 3) << 24;
      uint32_t eye = snorm8(ew.x) | snorm8(ew.y) << 8 | snorm8(ew.z) << 16 | eyeHi;
      float iz = 1 / cp[k].z;
      sv[k] = (TgV3Vertex) {cam->cx + cp[k].x * cam->f * iz, cam->cy - cp[k].y * cam->f * iz, tri[k].u * (1 / 65535.0f),
                            tri[k].v * (1 / 65535.0f), base, cp[k].z, nrm, eye};
    }
    if(cp[0].z > NEAR && cp[1].z > NEAR && cp[2].z > NEAR) {
      push_tri(g, &sv[0], &sv[1], &sv[2], pic);
    } else {  // cut by the near plane: lit at the corners here (in its plain colour)
      CV q[3];
      for(int k = 0; k < 3; k++) q[k] = (CV) {cp[k], 0, 0, shaded_colour(sv[k].argb, sv[k].nrm, sv[k].eye, g_haze)};
      emit_poly(g, q, 3, q[0].c, -1);
    }
  }
}

static bool mesh_car(Gather* g, TgView3D* v, int id, V3 c, V3 f, V3 r, V3 u, float depth, bool braking, bool nitro, int tick) {
  int type = id == 0 ? v->ptype[0] : id == 0x1F ? v->ptype[1] : (id * 7 + 1) % 5;
  const CarMesh* cm = car_mesh(v, type);
  if(!cm) return false;
  // The level by the car's height on screen (pixels), not its distance: at a wide or tall
  // window the same car is bigger. Levels 3 and 4 are the clustered ones.
  float px = 1.4f * g->cam->f / (depth > 0.5f ? depth : 0.5f);
  // With a margin either way (hysteresis): a car whose size sits on a step doesn't switch
  // back and forth between two levels from frame to frame.
  static uint8_t lastLvls[2][32];  // per view: in 2 players a car is drawn twice, at two sizes
  uint8_t* lastLvl = lastLvls[g_viewIdx & 1];
  int lvl;
  if(g_showcase) {
    lvl = getenv("TG_CAR_LOD") ? atoi(getenv("TG_CAR_LOD")) : 0;
  } else {
    static const float kStep[3] = {260, 110, 45};  // pixels: above the first level 1, ...
    int last = lastLvl[id & 31] ? lastLvl[id & 31] : 4;
    lvl = 4;
    for(int i = 0; i < 3; i++) {
      // the step between levels i+1 and i+2: nearer the last level's side it is 8 % further away
      float t = kStep[i] * (last <= i + 1 ? 1 / 1.08f : 1.08f);
      if(px > t) {
        lvl = i + 1;
        break;
      }
    }
    lastLvl[id & 31] = (uint8_t) lvl;
  }
  g_traceLvl = lvl;
  if(lvl < 3 && lvl >= cm->levels) lvl = cm->levels - 1;
  if(lvl >= 3 && !cm->far[lvl - 3]) lvl = cm->levels - 1;
  if(depth > g_meshCut && !g_showcase) return false;  // far or many: the light made one
  const CarCorner* cc = lvl >= 3 ? cm->far[lvl - 3] : cm->c[lvl];
  int n = lvl >= 3 ? cm->farTris[lvl - 3] : cm->tris[lvl];
  // The wheel likewise: whole when the car is big on screen.
  const CarCorner* wheelC = cm->wheel;
  int wheelN = cm->wheelTris;
  if(!g_showcase && lvl >= 2 && cm->wheelFar[0]) {  // (by the body's level: the same steps)
    int f = lvl == 2 ? 0 : 1;
    if(cm->wheelFar[f]) wheelC = cm->wheelFar[f], wheelN = cm->wheelFarTris[f];
  }
  if(g_showcase && lvl >= 2 && cm->wheelFar[lvl >= 3 ? 1 : 0]) {  // (TG_CAR_LOD: the wheels at that level too)
    int f = lvl >= 3 ? 1 : 0;
    wheelC = cm->wheelFar[f], wheelN = cm->wheelFarTris[f];
  }
  // Far away or small: a plain tyre and disc. (The wheels used to be left out past 40 m: they
  // popped in and out as a car crossed it.)
  if(!g_showcase && (lvl >= 4 || depth >= 40) && cm->wheelFar[2]) wheelC = cm->wheelFar[2], wheelN = cm->wheelFarTris[2];
  begin_item(g, depth - STAND_BIAS);
  // The car's frame in camera space once: each corner is then a few multiply-adds.
  const Cam* cam = g->cam;
  V3 cc0 = to_cam(cam, c);
  V3 rx = v3(vdot(r, cam->right), vdot(r, cam->up), vdot(r, cam->fwd));
  V3 uy = v3(vdot(u, cam->right), vdot(u, cam->up), vdot(u, cam->fwd));
  V3 fz = v3(vdot(f, cam->right), vdot(f, cam->up), vdot(f, cam->fwd));
  V3 L = vnorm(v3(vdot(kLight, cam->right), vdot(kLight, cam->up), vdot(kLight, cam->fwd)));
  V3 E = vnorm(vmul(cc0, -1)), hlf = vnorm(vadd(L, E));
  // The texture (version 3): the model's; the glass see-through on the levels with an interior.
  // (decals in black on light paint, in white on dark: the paint is a rival's vivid one or the
  // model's own)
  car_pic(v, type);
  uint32_t pc = g_paint ? g_paint : cm->ownPaint;
  bool light = (pc >> 16 & 255) * 2126 + (pc >> 8 & 255) * 7152 + (pc & 255) * 722 > 148 * 10000;
  int pic = cm->tex && v->pics[CAR_TEX + type].px ? CAR_TEX + type : -1;
  if(pic >= 0 && light && v->pics[CAR_TEX + 5 + type].px) pic = CAR_TEX + 5 + type;
  uint32_t glassA = lvl < 3 && (cm->flags[lvl] & 1) ? 150 : 255;
  mesh_corners(g, cc, n, cc0, rx, uy, fz, L, E, hlf, depth, braking, false, pic, glassA);
  // The wheels: each turned by how far the car has rolled, the front ones by the steering.
  if(cm->wheelTris) {
    float spin = g_wheelMetres / 0.43f;
    bool whole = wheelC == cm->wheel;
    for(int w = 0; w < 4; w++) {
      const float* hb = cm->hub[w];
      bool right = hb[0] > 0, front = w < 2;
      float st = front ? g_wheelSteer : 0;
      // The wheel's axes in the car: forward and up turned by the spin round the axle, then
      // all of it turned by the steering round up; a right wheel is the left one mirrored (or,
      // whole, its own: version 3's, whose lettering reads the right way).
      bool own = right && whole && cm->wheelR;
      float cs = cosf(spin), sn = sinf(spin), cst = cosf(st), snt = sinf(st);
      V3 wf = vadd(vmul(fz, cst), vmul(rx, snt)), wr = vsub(vmul(rx, cst), vmul(fz, snt)), wu = uy;
      V3 hubC = vadd(cc0, vadd(vmul(rx, hb[0]), vadd(vmul(uy, hb[1]), vmul(fz, hb[2]))));
      if(g_wheel_on) {  // where it sits on the road, turned with the road's frame, not the body's
        V3 wc0 = to_cam(cam, g_wheel_c);
        V3 wrx = v3(vdot(g_wheel_r, cam->right), vdot(g_wheel_r, cam->up), vdot(g_wheel_r, cam->fwd));
        V3 wuy = v3(vdot(g_wheel_u, cam->right), vdot(g_wheel_u, cam->up), vdot(g_wheel_u, cam->fwd));
        V3 wfz = v3(vdot(g_wheel_f, cam->right), vdot(g_wheel_f, cam->up), vdot(g_wheel_f, cam->fwd));
        hubC = vadd(wc0, vadd(vmul(wrx, hb[0]), vadd(vmul(wuy, hb[1]), vmul(wfz, hb[2]))));
        wf = vadd(vmul(wfz, cst), vmul(wrx, snt)), wr = vsub(vmul(wrx, cst), vmul(wfz, snt)), wu = wuy;
      }
      V3 ax2 = right && !own ? vmul(wr, -1) : wr;
      V3 sf = vadd(vmul(wf, cs), vmul(wu, sn)), su = vsub(vmul(wu, cs), vmul(wf, sn));
      mesh_corners(g, own ? cm->wheelR : wheelC, own ? cm->wheelRTris : wheelN, hubC, ax2, su, sf, L, E, hlf, depth, braking, true, pic, 255);
      // the caliper: steers with the wheel, doesn't spin (a right one mirrored)
      if(whole && cm->wheelFixed)
        mesh_corners(g, cm->wheelFixed, cm->wheelFixedTris, hubC, right ? vmul(wr, -1) : wr, wu, wf, L, E, hlf, depth, braking, true, pic, 255);
    }
  }
  // Version 3: the race number's digits on the roundel (or panel) painted on the doors, each
  // car its own, on the side the camera sees; the sponsors are painted.
  if(cm->num && depth < 40) {
    float keepA = g_alpha;
    g_alpha *= depth > 32 ? (40 - depth) / 8 : 1;
    font_ready(v);
    for(int sd = -1; sd <= 1; sd += 2) {
      V3 out = vmul(r, (float) sd);
      if(vdot(out, vsub(g->cam->eye, c)) <= 0) continue;
      V3 rx = vmul(f, (float) sd), dn = vmul(u, -1);  // read left to right from that side (r = f x u: towards the nose on the right)
      V3 ctr = vadd(c, vadd(vmul(out, cm->numAt[2] + 0.012f), vadd(vmul(u, cm->numAt[1]), vmul(f, cm->numAt[0]))));
      char num[4];
      snprintf(num, sizeof(num), "%d", g_number ? g_number : (id & 31) % 20 + 1);
      float nsz = cm->numAt[3] * 1.75f, nw = ftext_w(num, nsz, 2, 0);
      wtext(g, num, vadd(ctr, vadd(vmul(rx, -nw / 2), vmul(u, -nsz * 0.36f))), rx, dn, nsz, cm->numColour, 2, 0);
    }
    g_alpha = keepA;
  }
  // The livery: a race number in a dark roundel on the door, a sponsor under it, on the
  // side the camera sees.
  if(!cm->num && depth < 40 && !g_night) {
    // faded in over 32-40 m (it used to appear at once at 40 m)
    float keepA = g_alpha;
    g_alpha *= depth > 32 ? (40 - depth) / 8 : 1;
    font_ready(v);
    for(int sd = -1; sd <= 1; sd += 2) {
      V3 out = vmul(r, (float) sd);
      if(vdot(out, vsub(g->cam->eye, c)) <= 0) continue;
      V3 rx = vmul(f, (float) sd), dn = vmul(u, -1);  // read left to right from that side (r = f x u: towards the nose on the right)
      V3 ctr = vadd(c, vadd(vmul(out, cm->side + 0.03f), vadd(vmul(u, 0.55f), vmul(f, -0.1f))));
      CV q[12];
      for(int i = 0; i < 12; i++) {
        float a = 2 * PI * i / 12;
        q[i] = (CV) {to_cam(g->cam, vadd(ctr, vadd(vmul(f, cosf(a) * 0.22f), vmul(u, sinf(a) * 0.17f)))), 0, 0, 0};
      }
      emit_poly(g, q, 12, 0xFF141A22, -1);
      char num[4];
      snprintf(num, sizeof(num), "%d", g_number ? g_number : (id & 31) % 20 + 1);
      float nsz = 0.3f, nw = ftext_w(num, nsz, 2, 0);
      V3 lift = vmul(out, 0.006f);
      wtext(g, num, vadd(vadd(ctr, lift), vadd(vmul(rx, -nw / 2), vmul(u, -0.1f))), rx, dn, nsz, 0xFFF4F8FA, 2, 0);  // (the baseline: glyphs rise from it)
      static const char* kSp[4] = {"NITRO", "APEX OIL", "VELOX", "HALCYON"};
      const char* sp = kSp[(id * 3 + 1) & 3];
      float ssz = 0.13f, sw = ftext_w(sp, ssz, 2, 0);
      V3 sat = vadd(ctr, vadd(vmul(u, -0.3f), vmul(rx, -sw / 2)));
      wtext(g, sp, vadd(sat, lift), rx, dn, ssz, 0xFF141A22, 2, 0);
    }
    g_alpha = keepA;
  }
  if(nitro)
    for(int k = -1; k <= 1; k += 2) {
      uint32_t hh = hash32((uint32_t) (tick * 31 + k + id * 7));
      float len = 1.1f + (hh & 255) / 255.0f * 0.9f;
      V3 at = vadd(c, vadd(vmul(r, k * 0.4f), vadd(vmul(u, 0.3f), vmul(f, -2.35f))));
      nitro_flame(g, at, vmul(f, -1), r, u, len);
    }
  return true;
}

// A car's body texture, wrapped round it: u along the car (0 nose, 1 tail), v round it
// (0 the left sill, 0.5 the middle of the top, 1 the right sill).
static void make_livery(TgV3Pic* dst, uint32_t paint, int number, int type) {
  enum { LW = 256, LH = 128 };
  static uint32_t px[LW * LH];
  int lum = ((paint >> 16 & 255) * 3 + (paint >> 8 & 255) * 6 + (paint & 255)) / 10;
  uint32_t stripe = lum > 150 ? 0xFF22252B : 0xFFF2F4F6, seam = shade(paint, 0.55f);
  for(int y = 0; y < LH; y++)
    for(int x = 0; x < LW; x++) {
      float u = (x + 0.5f) / LW, vv = (y + 0.5f) / LH, sv = vv < 0.5f ? vv : 1 - vv;  // sv: 0 sill .. 0.5 top middle
      uint32_t h = hash32((uint32_t) (y * LW + x) * 2654435761u);
      uint32_t c = shade(paint, 0.97f + ((h & 255) / 255.0f) * 0.05f);  // metallic flake
      bool side = sv < 0.21f;
      if(sv < 0.035f) c = shade(paint, 0.4f);                                      // sills
      if(side && fabsf(sv - 0.165f) < 0.006f) c = shade(paint, 0.7f);               // the side crease
      if(side && sv > 0.04f && (fabsf(u - 0.36f) < 0.0035f || fabsf(u - 0.6f) < 0.0035f)) c = seam;  // doors
      if(side && u > 0.55f && u < 0.58f && fabsf(sv - 0.14f) < 0.008f) c = 0xFF2A2D32;  // handle
      // The side intake behind the door: a dark slanted slot with fins.
      if(side && u > 0.63f && u < 0.75f && sv > 0.06f && sv < 0.13f && u - 0.63f > (0.13f - sv) * 0.5f)
        c = ((x / 3) & 1) ? 0xFF15171A : 0xFF24272C;
      // The bonnet's panel lines and vents.
      if(!side && u < 0.3f && (fabsf(sv - 0.36f) < 0.004f)) c = seam;
      if(!side && u > 0.12f && u < 0.2f && sv > 0.38f && sv < 0.46f && ((x / 2) & 1)) c = shade(paint, 0.45f);
      // Each car its own livery.
      switch(type) {
        case 0:  // red: one wide centre stripe with fine edges; strakes along the doors
          if(sv > 0.455f) c = sv > 0.462f ? stripe : shade(paint, 0.5f);
          if(side && u > 0.4f && u < 0.72f && sv > 0.07f && sv < 0.15f && ((int) (sv * 300) % 5 == 0)) c = shade(paint, 0.35f);
          break;
        case 1: {  // white: a black sash slanting across, a red pinstripe at its edge
          float sash = u - (0.5f - sv * 0.9f);
          if(sash > 0 && sash < 0.16f) c = 0xFF1A1C20;
          if(sash > 0.16f && sash < 0.175f) c = 0xFFD81E28;
          break;
        }
        case 2:  // purple: a gold pinstripe along the shoulder, a chrome line under the windows
          if(side && fabsf(sv - 0.185f) < 0.004f) c = 0xFFE0B040;
          if(!side && fabsf(sv - 0.23f) < 0.004f) c = 0xFFD8DEE6;
          break;
        default:  // blue: two-tone, the lower body grey as on the select screen
          if(sv < 0.11f) c = (h & 7) ? 0xFF5E646C : 0xFF666C74;
          if(fabsf(sv - 0.11f) < 0.004f) c = 0xFFC8CED6;
          break;
      }
      px[y * LW + x] = c;
    }
  // The number roundel on each door: a white disc, the number in black.
  static const uint8_t kDig[10][5] = {{7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 7, 1, 7}, {5, 5, 7, 1, 1},
                                      {7, 4, 7, 1, 7}, {7, 4, 7, 5, 7}, {7, 1, 1, 1, 1}, {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7}};
  for(int sideK = 0; sideK < 2; sideK++) {
    float cx = 0.48f * LW, cy = (sideK ? 1 - 0.1f : 0.1f) * LH, r = 0.075f * LH;
    for(int y = (int) (cy - r - 1); y <= (int) (cy + r + 1); y++)
      for(int x = (int) (cx - r * 1.6f); x <= (int) (cx + r * 1.6f); x++) {
        if(x < 0 || x >= LW || y < 0 || y >= LH) continue;
        float dx = (x - cx) / 1.6f, dy = y - cy, d = sqrtf(dx * dx + dy * dy);
        if(d < r) px[y * LW + x] = d > r - 1.2f ? 0xFF202226 : 0xFFF4F4F2;
      }
    char num[4];
    snprintf(num, sizeof(num), "%d", number % 100);
    int len = (int) strlen(num);
    const float sx = 2.6f, sy = 2.6f;  // texels per font pixel
    float totalW = (len * 4 - 1) * sx;
    for(int n = 0; n < len; n++)
      for(int gy = 0; gy < 5; gy++)
        for(int gx = 0; gx < 3; gx++) {
          if(!(kDig[num[n] - '0'][gy] >> (2 - gx) & 1)) continue;
          for(int ty = 0; ty < (int) sy; ty++)
            for(int tx = 0; tx < (int) sx; tx++) {
              // The texture's v runs the other way on the right side: the number reads upright on both.
              // Up the car's side is +v on the left and -v on the right; seen from the right,
              // the tail (+u) is on the left, so that side's number runs the other way.
              int yy = sideK ? (int) (cy - 2.5f * sy + gy * sy + ty) : (int) (cy + 2.5f * sy - gy * sy - ty - 1);
              float fx = (n * 4 + gx) * sx + tx;
              int xx = sideK ? (int) (cx + totalW / 2 - fx) : (int) (cx - totalW / 2 + fx);
              if(xx >= 0 && xx < LW && yy >= 0 && yy < LH) px[yy * LW + xx] = 0xFF15161A;
            }
        }
  }
  set_pic(dst, LW, LH, px, LW);
  dst->smooth = true;
}

static void clean_face_tex(Gather* g, const V3* p, const V3* n, const uint32_t* col, float depth, int pic, const float* uv) {
  V3 fn = vcross(vsub(p[1], p[0]), vsub(p[3], p[0]));
  V3 avg = vadd(vadd(n[0], n[1]), vadd(n[2], n[3]));
  if(vdot(fn, avg) < 0) fn = vmul(fn, -1);
  V3 fc = vmul(vadd(vadd(p[0], p[1]), vadd(p[2], p[3])), 0.25f);
  if(vdot(fn, vsub(fc, g->cam->eye)) >= 0) return;
  begin_item(g, to_cam(g->cam, fc).z - STAND_BIAS);
  CV q[4];
  for(int k = 0; k < 4; k++) q[k] = (CV) {to_cam(g->cam, p[k]), uv[k * 2], uv[k * 2 + 1], fogged(col[k], depth)};
  emit_poly(g, q, 4, q[0].c, pic);
}

static bool clean_car(Gather* g, TgView3D* v, int id, V3 c, V3 f, V3 r, V3 u, float depth, float metres, float yaw,
                      bool braking, bool nitro, int tick, uint32_t paint) {
  int type = id == 0 ? v->ptype[0] : id == 0x1F ? v->ptype[1] : (id * 7 + 1) % 4;
  const TgV3Pic* src = &v->pics[SIDE_SRC + type];
  CarShape* cs = &v->shape[4];
  if(src->w) {
    cs = &v->shape[type];
    if(cs->version != src->version + 1) make_shape(cs, src);
  } else if(!cs->version) {
    default_shape(cs);
  }
  // The paint: the players' cars as the select screen shows them; the others their own.
  uint32_t body = paint ? paint : 0xFFB0B0B8;
  if(src->w && (id == 0 || id == 0x1F)) {
    uint32_t pc = paint_colour(src);
    if(pc) body = pc;
  }
  body = clean_paint(body);
  // Its body texture, made for its paint and number.
  int livery = LIVERY_PIC + (id & 31);
  {
    static uint32_t madeFor[32];
    uint32_t key = body ^ (uint32_t) id * 0x9E3779B1u ^ (uint32_t) type << 29;
    if(!v->pics[livery].w || madeFor[id & 31] != key) {
      make_livery(&v->pics[livery], body, id == 0x1F ? 2 : id + 1, type);
      madeFor[id & 31] = key;
    }
  }
  const float L = CAR_LEN, sill = 0.16f;
  // The outline, smoothed again along the car (no steps between stations).
  float top[STA];
  for(int i = 0; i < STA; i++) {
    float x = (float) i / (STA - 1) * (CAR_STATIONS - 1);
    int a = (int) x;
    float t = x - a;
    float y = a + 1 < CAR_STATIONS ? cs->top[a] * (1 - t) + cs->top[a + 1] * t : cs->top[CAR_STATIONS - 1];
    top[i] = y;
  }
  for(int pass = 0; pass < 2; pass++)
    for(int i = 1; i + 1 < STA; i++) top[i] = (top[i - 1] + 2 * top[i] + top[i + 1]) / 4;
  float roof = 0;
  for(int i = 0; i < STA; i++) roof = top[i] > roof ? top[i] : roof;
  float shoulder = roof * 0.66f;
  V3 P[STA][RING], N[STA][RING];
  float cab[STA];
  for(int i = 0; i < STA; i++) {
    float t = (float) i / (STA - 1), z = L / 2 - t * L, wz = car_width(t);
    float sl = sill + car_lift(t) * 0.12f;
    // Wheel arches: the sides lift round each wheel.
    float sideLift = 0;
    for(int ax = 0; ax < 2; ax++) {
      float dz = (z - cs->wheel_z[ax]) / 0.44f;
      if(fabsf(dz) < 1) sideLift = fmaxf(sideLift, 0.5f * sqrtf(1 - dz * dz));
    }
    float yb = top[i] < shoulder ? top[i] : shoulder;  // the shoulder (bonnet, boot, waist)
    if(yb < sl + 0.14f) yb = sl + 0.14f;
    float h = (top[i] - yb - 0.08f) / 0.22f;  // how much cabin rises here
    h = h < 0 ? 0 : h > 1 ? 1 : h;
    cab[i] = h;
    float yr = yb + 0.025f + (top[i] - yb - 0.025f > 0 ? top[i] - yb - 0.025f : 0);
    float gw = wz * (0.86f - 0.18f * h);
    float side = yb - sl;
    float hx[9] = {-wz * 0.95f, -wz, -wz * 0.995f, -wz * 0.95f, -wz * 0.84f, -gw, -gw * 0.9f, -gw * 0.62f, -gw * 0.22f};
    float sideBot = sl + sideLift < yb - 0.12f ? sl + sideLift : yb - 0.12f;
    float hy[9] = {sideBot, sideBot + (yb - sideBot) * 0.3f, sideBot + (yb - sideBot) * 0.72f, yb - 0.01f, yb + 0.02f,
                   yb + (yr - yb) * 0.55f, yr - 0.025f, yr, yr};
    (void) side;
    for(int k = 0; k < 9; k++) {
      P[i][k] = v3(hx[k], hy[k], z);
      P[i][RING - 1 - k] = v3(-hx[k], hy[k], z);
    }
  }
  // Smooth normals from the neighbours along the ring and along the car.
  for(int i = 0; i < STA; i++)
    for(int k = 0; k < RING; k++) {
      V3 a = P[i][k > 0 ? k - 1 : k], b = P[i][k + 1 < RING ? k + 1 : k];
      V3 cA = P[i > 0 ? i - 1 : i][k], cB = P[i + 1 < STA ? i + 1 : i][k];
      V3 n = vnorm(vcross(vsub(b, a), vsub(cA, cB)));
      V3 out = v3(P[i][k].x, P[i][k].y - 0.45f, 0);
      if(vdot(n, out) < 0) n = vmul(n, -1);
      N[i][k] = n;
    }
  #define W(p) vadd(c, vadd(vmul(r, (p).x), vadd(vmul(u, (p).y), vmul(f, (p).z))))
  #define WN(n) vadd(vmul(r, (n).x), vadd(vmul(u, (n).y), vmul(f, (n).z)))
  uint32_t glass = 0xFF141C26, under = shade(body, 0.35f);
  for(int i = 0; i + 1 < STA; i++) {
    // Windscreen and rear window: where the roof line climbs or falls steeply.
    float slope = fabsf(top[i] - top[i + 1]) / (L / (STA - 1));
    bool cabin = cab[i] > 0.45f && cab[i + 1] > 0.45f;
    for(int k = 0; k + 1 < RING; k++) {
      // Rings: 0-3 left side, 4 shoulder, 5-7 left glass, 8-9 roof, 10-12 right glass, 13-17 right.
      bool sideGlass = cabin && (k == 4 || k == 5 || k == 11 || k == 12);
      bool topGlass = cab[i] > 0.2f && cab[i + 1] > 0.2f && slope > 0.32f && k >= 5 && k <= 11;
      bool low = k == 0 || k == RING - 2;
      bool isGlass = sideGlass || topGlass;
      uint32_t base = isGlass ? glass : 0xFFD0D0D0;  // the paint is in the texture: light it in grey
      float gloss = isGlass ? 1.2f : 0.9f;
      V3 p[4] = {W(P[i][k]), W(P[i + 1][k]), W(P[i + 1][k + 1]), W(P[i][k + 1])};
      V3 n[4] = {WN(N[i][k]), WN(N[i + 1][k]), WN(N[i + 1][k + 1]), WN(N[i][k + 1])};
      uint32_t col[4];
      for(int j = 0; j < 4; j++) col[j] = paint_lit(base, n[j], p[j], g->cam->eye, gloss);
      if(isGlass) {
        clean_face(g, p, n, col, depth);
      } else {
        float u0 = (float) i / (STA - 1), u1 = (float) (i + 1) / (STA - 1), v0 = (float) k / (RING - 1), v1 = (float) (k + 1) / (RING - 1);
        float uv[8] = {u0, v0, u1, v0, u1, v1, u0, v1};
        clean_face_tex(g, p, n, col, depth, livery, uv);
      }
      (void) low;
      (void) under;
    }
  }
  // The nose and the tail: closed by the ring itself.
  for(int e = 0; e < 2; e++) {
    int s = e ? STA - 1 : 0;
    V3 nrm = e ? vmul(f, -1) : f;
    V3 cen = W(v3(0, 0.45f, P[s][0].z));
    if(vdot(nrm, vsub(cen, g->cam->eye)) >= 0) continue;
    begin_item(g, to_cam(g->cam, cen).z - STAND_BIAS);
    CV q[RING];
    for(int k = 0; k < RING; k++) q[k] = (CV) {to_cam(g->cam, W(P[s][k])), 0, 0, 0};
    emit_poly(g, q, RING, fogged(paint_lit(shade(body, e ? 0.75f : 0.9f), nrm, cen, g->cam->eye, 0.4f), depth), -1);
  }
  float zn = L / 2 + 0.01f, zr = -L / 2 - 0.01f;
  float yn = (P[0][2].y + P[0][3].y) / 2, yt = (P[STA - 1][2].y + P[STA - 1][3].y) / 2;
  float wn = car_width(0), wt = car_width(1);
  // Front: a dark intake, headlights with clear covers.
  mbox(g, W(v3(0, 0, zn)), f, r, u, 0.015f, wn * 0.55f, sill + 0.04f, sill + 0.12f, 0xFF101214, 0.2f);
  for(int k = -1; k <= 1; k += 2) {
    mbox(g, W(v3(k * wn * 0.62f, 0, zn - 0.02f)), f, r, u, 0.02f, wn * 0.24f, yn - 0.03f, yn + 0.04f, 0xFFDDE4EC, 0.21f);
    mbox(g, W(v3(k * wn * 0.62f, 0, zn - 0.01f)), f, r, u, 0.02f, wn * 0.12f, yn - 0.015f, yn + 0.025f, 0xFFFFFFF4, 0.22f);
  }
  // Back: a light strip across, the lamps, a diffuser, the exhausts.
  uint32_t lamp = braking ? 0xFFFF3A28 : 0xFFC0180E;
  mbox(g, W(v3(0, 0, zr)), f, r, u, 0.015f, wt * 0.9f, yt - 0.02f, yt + 0.03f, shade(lamp, 0.6f), 0.2f);
  for(int k = -1; k <= 1; k += 2)
    mbox(g, W(v3(k * wt * 0.68f, 0, zr - 0.01f)), f, r, u, 0.015f, wt * 0.22f, yt - 0.04f, yt + 0.05f, lamp, 0.21f);
  mbox(g, W(v3(0, 0, zr)), f, r, u, 0.03f, wt * 0.8f, sill, sill + 0.1f, 0xFF141416, 0.2f);
  for(int k = -1; k <= 1; k += 2)
    frustum(g, W(v3(k * 0.4f, sill + 0.08f, zr + 0.05f)), vmul(f, -1), r, u, 0.05f, 0.05f, 0.08f, 8, 0xFF8A8E94, 0.22f);
  // Mirrors.
  for(int k = -1; k <= 1; k += 2) {
    int i = STA * 36 / 100;
    mbox(g, W(v3(k * (P[i][4].x < 0 ? -P[i][4].x : P[i][4].x) * 1.04f, 0, P[i][0].z)), f, r, u, 0.06f, 0.07f, P[i][4].y + 0.02f,
         P[i][4].y + 0.11f, body, 0.06f);
  }
  // A wing.
  if(cs->wing_top > 0) {
    float zm = (cs->wing_z0 + cs->wing_z1) / 2, hl = fabsf(cs->wing_z0 - cs->wing_z1) / 2 + 0.08f;
    mbox(g, W(v3(0, 0, zm)), f, r, u, hl, wt * 0.98f, cs->wing_top - 0.05f, cs->wing_top, body, 0.15f);
    for(int k = -1; k <= 1; k += 2)
      mbox(g, W(v3(k * 0.55f, 0, zm)), f, r, u, 0.05f, 0.025f, shoulder - 0.05f, cs->wing_top - 0.05f, 0xFF202024, 0.14f);
  }
  // Each car's own parts.
  switch(type) {
    case 0:  // red: strakes along the doors, louvres over the engine
      for(int k = -1; k <= 1; k += 2)
        for(int f5 = 0; f5 < 4; f5++) {
          int i = STA / 2;
          float y = sill + 0.12f + f5 * 0.07f, x = car_width(0.55f) + 0.01f;
          mbox(g, W(v3(k * x, 0, P[i][0].z - 0.35f)), f, r, u, 0.42f, 0.015f, y, y + 0.025f, shade(body, 0.75f), 0.07f);
        }
      for(int lv = 0; lv < 5; lv++) {
        int i = STA * 70 / 100 + lv;
        if(i >= STA) break;
        float y = (P[i][8].y > shoulder ? shoulder : P[i][8].y) + 0.01f;
        mbox(g, W(v3(0, 0, P[i][0].z)), f, r, u, 0.03f, car_width(0.75f) * 0.55f, y, y + 0.02f, 0xFF16181C, 0.08f);
      }
      break;
    case 1: {  // white: a big wing on tall posts, a splitter
      float zw = -L / 2 + 0.28f, yw = roof * 0.98f;
      mbox(g, W(v3(0, 0, zw)), f, r, u, 0.2f, car_width(1) * 1.02f, yw, yw + 0.05f, body, 0.15f);
      for(int k = -1; k <= 1; k += 2) {
        mbox(g, W(v3(k * 0.6f, 0, zw)), f, r, u, 0.06f, 0.03f, shoulder - 0.06f, yw, 0xFF1A1C20, 0.14f);
        mbox(g, W(v3(k * car_width(1) * 1.02f, 0, zw)), f, r, u, 0.22f, 0.012f, yw - 0.1f, yw + 0.08f, 0xFF1A1C20, 0.16f);
      }
      mbox(g, W(v3(0, 0, L / 2 - 0.02f)), f, r, u, 0.1f, car_width(0) * 1.05f, sill - 0.04f, sill, 0xFF15171A, 0.2f);
      break;
    }
    case 2: {  // purple: round headlight pods
      float yh = (P[0][2].y + P[0][3].y) / 2 + 0.04f;
      for(int k = -1; k <= 1; k += 2) {
        frustum(g, W(v3(k * car_width(0.05f) * 0.6f, yh, L / 2 - 0.25f)), f, r, u, 0.12f, 0.12f, 0.24f, 10, shade(body, 0.8f), 0.2f);
        frustum(g, W(v3(k * car_width(0.05f) * 0.6f, yh, L / 2 - 0.0f)), f, r, u, 0.1f, 0.1f, 0.02f, 10, 0xFFFFFAE6, 0.21f);
      }
      break;
    }
    default: {  // blue: a ducktail, diffuser fins
      float yt0 = P[STA - 1][8].y;
      mbox(g, W(v3(0, 0, -L / 2 + 0.12f)), f, r, u, 0.14f, car_width(1) * 0.9f, yt0, yt0 + 0.08f, body, 0.15f);
      for(int fin = -2; fin <= 2; fin++)
        mbox(g, W(v3(fin * 0.25f, 0, -L / 2 + 0.05f)), f, r, u, 0.12f, 0.015f, sill - 0.02f, sill + 0.1f, 0xFF15171A, 0.21f);
      break;
    }
  }
  // Wheels: tyres with alloy rims and spokes turning; the front ones steer.
  float spin = metres / 0.33f;
  for(int ax = 0; ax < 2; ax++)
    for(int j = -1; j <= 1; j += 2) {
      float t = (L / 2 - cs->wheel_z[ax]) / L;
      V3 wc = W(v3(j * (car_width(t) - 0.08f), 0.33f, cs->wheel_z[ax]));
      float st = ax == 0 ? yaw * 1.5f : 0;
      V3 wf = vadd(vmul(f, cosf(st)), vmul(r, sinf(st))), wr = vnorm(vcross(wf, u));
      V3 out = vmul(wr, (float) j);
      frustum(g, vadd(wc, vmul(out, -0.12f)), out, wf, u, 0.33f, 0.33f, 0.24f, 14, 0xFF141416, 0.03f);
      frustum(g, vadd(wc, vmul(out, 0.12f)), out, wf, u, 0.24f, 0.22f, 0.01f, 14, 0xFFB8BEC6, 0.04f);
      frustum(g, vadd(wc, vmul(out, 0.125f)), out, wf, u, 0.06f, 0.05f, 0.01f, 8, 0xFF3A3E44, 0.05f);
      if(depth < 60)
        for(int sp = 0; sp < 5; sp++) {
          float a = spin + sp * 2 * PI / 5;
          V3 d1 = vadd(vmul(wf, cosf(a)), vmul(u, sinf(a)));
          V3 d2 = vnorm(vcross(out, d1));
          mbox(g, vadd(vadd(wc, vmul(out, 0.13f)), vmul(d1, 0.13f)), d1, d2, out, 0.08f, 0.025f, -0.006f, 0.006f, 0xFF5A6068, 0.05f);
        }
    }
  if(nitro)
    for(int k = -1; k <= 1; k += 2) {
      uint32_t hh = hash32((uint32_t) (tick * 31 + k + id * 7));
      float len = 1.1f + (hh & 255) / 255.0f * 0.9f;
      V3 at = W(v3(k * 0.4f, sill + 0.08f, zr - 0.03f));
      V3 back = vmul(f, -1);
      nitro_flame(g, at, back, r, u, len);
    }
  #undef W
  #undef WN
  return true;
}

// The rivals' paint made vivid (owner: "you could add more vivid colors to the computer cars"):
// the game gives a race's rivals a few dull colours, many cars sharing one (Las Vegas: 18
// rivals in a slate and an orange). Each game colour stands for a family of a curated palette
// of modern racing paints (saturated, not pastel, short of flat neon: they are lit, under the
// clear coat and its flake), and the driver's place on the grid among the cars of its colour
// picks one of its family in turn (TgView3D.vivid_rank): deterministic, the same in every
// replay, held for the race as the game's colour is (the colour lock); the game's grid
// alternates its colours, so cars side by side or one behind the other never share one, and a
// colour comes back only every four or five cars of its family.
static uint32_t vivid_paint(uint32_t game, int rank) {
  enum { RED, ORANGE, YELLOW, GOLD, LIME, GREEN, TEAL, BLUE, PURPLE, MAGENTA, WHITE, BLACK };
  static const uint32_t kPaint[12] = {
    0xFFD0121E,  // racing red
    0xFFF5620F,  // sunset orange
    0xFFF5C30C,  // signal yellow
    0xFFC9971F,  // gold
    0xFF7DC81A,  // lime green
    0xFF0D5A33,  // British racing green
    0xFF00A3AE,  // teal
    0xFF1752E6,  // electric blue
    0xFF5E22B0,  // royal purple
    0xFFDC1A8A,  // hot magenta
    0xFFEDEDE6,  // pearl white
    0xFF141418,  // gloss black (its flake shows)
  };
  // the families, the game colour's own first: by hue, or greys by lightness
  static const int8_t kFam[6][6] = {
    {ORANGE, RED, YELLOW, MAGENTA, GOLD, -1},  // warm (reds, oranges, yellows)
    {LIME, GREEN, YELLOW, TEAL, -1},           // greens
    {BLUE, TEAL, PURPLE, GREEN, BLACK, -1},    // blues, cyans
    {PURPLE, MAGENTA, BLUE, RED, -1},          // purples, magentas
    {WHITE, GOLD, TEAL, -1},                   // light greys, silver
    {BLACK, BLUE, GREEN, PURPLE, -1},          // dark greys, slate
  };
  float r = (game >> 16 & 255) / 255.0f, g = (game >> 8 & 255) / 255.0f, b = (game & 255) / 255.0f;
  float mx = fmaxf(r, fmaxf(g, b)), mn = fminf(r, fminf(g, b)), l = (mx + mn) / 2, d = mx - mn;
  float sat = d < 1e-4f ? 0 : d / (1 - fabsf(2 * l - 1));
  int f;
  if(sat < 0.18f) {
    f = l >= 0.55f ? 4 : 5;
  } else {
    float h = mx == r ? fmodf((g - b) / d + 6, 6) : mx == g ? (b - r) / d + 2 : (r - g) / d + 4;
    h *= 60;
    f = h < 50 || h >= 345 ? 0 : h < 165 ? 1 : h < 275 ? 2 : 3;
  }
  int n = 0;
  while(n < 6 && kFam[f][n] >= 0) n++;
  return kPaint[kFam[f][rank % n]];
}

// A modern sports car: a wedge body, a glass cabin with a roof, wheels, lights, a spoiler,
// in the car's own colour (from its picture), with a shadow.
static void modern_car(Gather* g, const TgView3D* v, TgTrackFrame fr, int id, uint16_t kind, bool braking, float metres,
                       float yaw, bool nitro, int tick) {
  TgView3D* vm = (TgView3D*) v;
  // Its paint: its palette's, this race's (pal_paint), made vivid (vivid_paint); a neutral
  // silver until the palette holds colours (the screen is still black then).
  int pal = kind >> 13 & 7;
  uint32_t body = (v->pal_locked >> pal & 1) ? vivid_paint(v->pal_paint[pal], v->vivid_rank[id & 31]) : 0xFFB4B4BC;
  vm->car_paint[id & 31] = body;
  V3 f0 = tv(fr.forward), r0 = tv(fr.right), u = tv(fr.up);
  // Turned the way it is going (the heading clamped near the road's).
  float cy = cosf(yaw), sy = sinf(yaw);
  V3 f = vadd(vmul(f0, cy), vmul(r0, sy)), r = vnorm(vcross(f, u));
  V3 c = vadd(tv(fr.pos), vmul(f0, 2.15f));  // its back where the game draws the car
  // Alive: the body on its springs (step_bodies), eased between the last two game frames,
  // with a fine vibration at speed; the wheels stay on the road.
  const struct CarBody *b0 = &v->body_prev[id & 31], *b1 = &v->body[id & 31];
  float st = v->subframe;
  float roll = b0->roll[0] + (b1->roll[0] - b0->roll[0]) * st;
  float pitch = b0->pitch[0] + (b1->pitch[0] - b0->pitch[0]) * st;
  float heave = b0->heave[0] + (b1->heave[0] - b0->heave[0]) * st;
  float steer = b0->steer[0] + (b1->steer[0] - b0->steer[0]) * st;
  {
    float tsec = ((float) v->frames + v->subframe) / 60.0f, fast = v->car_ms[id & 31] / 60.0f;
    fast = fast < 0 ? 0 : fast > 1 ? 1 : fast;
    float ph = (float) (id & 31) * 1.7f;
    heave += fast * (0.0045f * sinf(tsec * 2 * PI * 11.3f + ph) + 0.0025f * sinf(tsec * 2 * PI * 17.9f + ph * 2.3f));
    roll += fast * 0.0022f * sinf(tsec * 2 * PI * 13.1f + ph * 0.7f);
    pitch += fast * 0.0015f * sinf(tsec * 2 * PI * 9.7f + ph * 1.3f);
  }
  // The body's frame: rolled round its length, pitched round its width, about a point 0.45 m
  // up (near the springs' roll centre), and lifted.
  V3 ub = vadd(vmul(u, cosf(roll)), vmul(r, sinf(roll))), rb = vsub(vmul(r, cosf(roll)), vmul(u, sinf(roll)));
  V3 fb = vsub(vmul(f, cosf(pitch)), vmul(ub, sinf(pitch)));
  ub = vadd(vmul(ub, cosf(pitch)), vmul(f, sinf(pitch)));
  V3 bc = vadd(vadd(c, vmul(u, 0.45f + heave)), vmul(ub, -0.45f));
  float hw = 0.93f;
  float depth = to_cam(g->cam, c).z;
  car_shadow(g, c, f, r, 2.5f, 1.2f, 0.42f);
  if(g_car_lift > 0) c = vadd(c, vmul(u, g_car_lift)), bc = vadd(bc, vmul(u, g_car_lift));  // (on the jacks)
  if(g_night && depth > -3 && depth < 120) {
    // Its headlights on the road ahead: two warm beams fading out, then a glow at each lamp
    // and red at the back.
    V3 lift = vmul(u, 0.03f), nose = vadd(c, vmul(f, 2.1f));
    begin_item(g, depth - 0.6f);
    for(int s2 = -1; s2 <= 1; s2 += 2) {
      V3 a0 = vadd(vadd(nose, vmul(r, s2 * 0.62f)), lift);
      V3 far = vadd(vadd(nose, vmul(f, 24)), vmul(r, s2 * 2.4f));
      far = vadd(far, lift);
      V3 a1 = vadd(a0, vmul(r, 0.5f)), a2 = vadd(a0, vmul(r, -0.5f));
      V3 f1 = vadd(far, vmul(r, 3.2f)), f2 = vadd(far, vmul(r, -3.2f));
      quad_shaded(g, a2, a1, f1, f2, 0x58FFF0C8, 0x58FFF0C8, 0x00FFF0C8, 0x00FFF0C8);
    }
    // the bright pool just in front
    V3 p0 = vadd(vadd(nose, vmul(r, -1.6f)), lift), p1 = vadd(vadd(nose, vmul(r, 1.6f)), lift);
    V3 p2 = vadd(vadd(vadd(nose, vmul(f, 7)), vmul(r, 2.6f)), lift), p3 = vadd(vadd(vadd(nose, vmul(f, 7)), vmul(r, -2.6f)), lift);
    quad_shaded(g, p0, p1, p2, p3, 0x40FFF4D8, 0x40FFF4D8, 0x00FFF4D8, 0x00FFF4D8);
    begin_item(g, depth - STAND_BIAS - 0.3f);
    for(int s2 = -1; s2 <= 1; s2 += 2) {
      billboard_tint(g, vadd(vadd(vadd(c, vmul(f, 2.15f)), vmul(r, s2 * 0.62f)), vmul(u, 0.25f)), 1.1f, 0.7f, TEX_GLOW, 0xFFFFF2D8);
      billboard_tint(g, vadd(vadd(vadd(c, vmul(f, -2.2f)), vmul(r, s2 * 0.6f)), vmul(u, 0.35f)), 0.9f, 0.55f, TEX_GLOW,
                     braking ? 0xFFFF3020 : 0xA0E02018);
    }
  }
  // The front wheels: the road's bend and the car's heading (step_bodies), and for the car
  // the camera follows, the turn it makes against the camera.
  g_wheelMetres = metres, g_wheelSteer = steer + (yaw - v->car_yaw[id & 31]) * 1.5f;
  // Tyre smoke: braking hard or pushed hard through a bend at speed, a few soft puffs from
  // the rear tyres, drifting back.
  {
    float ms = v->car_ms[id & 31], brake = -v->car_accel[id & 31], side = fabsf(roll) / 0.06f;
    float k = ms > 18 ? fmaxf((brake - 9) / 10, (side - 0.85f) * 4) : 0;
    if(k > 0 && depth > 1 && depth < 70) {
      k = k > 1 ? 1 : k;
      begin_item(g, depth + 0.4f);
      for(int p = 0; p < 4; p++) {
        uint32_t hh = hash32((uint32_t) (tick / 3 + p * 101 + id * 7));
        float age = ((tick + p * 5) % 20) / 20.0f;
        for(int sd = -1; sd <= 1; sd += 2) {
          V3 at = vadd(c, vadd(vmul(f, -1.4f - age * 2.5f), vadd(vmul(r, sd * (0.8f + age * 0.5f)), vmul(u, 0.15f + age * 0.5f))));
          at = vadd(at, vmul(r, ((hh >> 8 & 255) / 255.0f - 0.5f) * 0.4f));
          float sz = 0.7f + age * 1.6f;
          int a = (int) (k * (1 - age) * 70);
          if(a > 2) billboard_tint(g, at, sz, sz * 0.8f, TEX_GLOW, with_alpha(0xFFE4E6EA, a));
        }
      }
    }
  }
  // Rivals: the game's colour for them, the same whether the model or the light car draws
  // them. The players' own cars (id 0, player 2's $1F) keep their model's paint, as chosen
  // on the car select screen (Rosso red, Glacier white, Violet purple, Azure blue).
  g_paint = id == 0 || id == 0x1F ? 0 : body;
  g_wheel_on = true, g_wheel_c = c, g_wheel_f = f, g_wheel_r = r, g_wheel_u = u;  // the wheels: on the road
  bool drawn = mesh_car(g, vm, id, bc, fb, rb, ub, depth, braking, nitro, tick);
  if(car_trace()) fprintf(stderr, "CT %u %d paint %08x depth %.1f drawn %d pal %d lvl %d brake %d view %d tris %d kept %d\n", v->frames, id, body, depth, drawn, pal, g_traceLvl, braking, g_viewIdx, g_traceTot, g_traceKept);
  g_traceTot = g_traceKept = 0;
  g_wheel_on = false;
  g_paint = 0;
  if(drawn) return;
  c = bc, f = fb, r = rb, u = ub;  // the made cars move as one
  if(depth < 300 && clean_car(g, vm, id, c, f, r, u, depth, metres, yaw, braking, nitro, tick, body)) return;
  if(depth > 160) {  // far away: the body is enough
    mbox(g, c, f, r, u, 2.15f, hw, 0.2f, 0.95f, body, 0);
    return;
  }
  // Body: cross-sections from the tail to the nose (a high rear deck, a long hood falling
  // to a low nose), each a little narrower at the top; a solid between each two.
  static const float sx[4] = {-1, -1, 1, 1}, sz[4] = {-1, 1, 1, -1};
  static const float zs[6] = {-2.15f, -1.7f, -0.9f, 0.55f, 1.45f, 2.15f};
  static const float tops[6] = {0.64f, 0.71f, 0.72f, 0.63f, 0.56f, 0.43f};
  static const float bots[6] = {0.26f, 0.21f, 0.2f, 0.2f, 0.21f, 0.25f};
  static const float wids[6] = {0.95f, 1.0f, 1.0f, 1.0f, 0.98f, 0.9f};
  for(int sct = 0; sct < 5; sct++) {
    V3 b[8];
    for(int k = 0; k < 4; k++) {
      int at = sz[k] < 0 ? sct : sct + 1;
      V3 base = vadd(c, vadd(vmul(f, zs[at]), vmul(r, sx[k] * hw * wids[at])));
      b[k] = vadd(base, vmul(u, bots[at]));
      b[k + 4] = vadd(vadd(c, vadd(vmul(f, zs[at]), vmul(r, sx[k] * hw * wids[at] * 0.88f))), vmul(u, tops[at]));
    }
    hexa(g, b, body, sct * 0.001f);
  }
  // A dark sill along each side, low down.
  for(int k = -1; k <= 1; k += 2)
    mbox(g, vadd(c, vmul(r, k * hw * 0.99f)), f, r, u, 1.6f, 0.03f, 0.2f, 0.3f, 0xFF1E2024, 0.02f);
  // Cabin: glass, narrowing to the roof, set back of the middle.
  V3 cb[8];
  for(int k = 0; k < 4; k++) {
    float zb = sz[k] < 0 ? -1.35f : 0.55f, zt = sz[k] < 0 ? -1.0f : 0.05f;
    cb[k] = vadd(vadd(c, vadd(vmul(f, zb), vmul(r, sx[k] * hw * 0.84f))), vmul(u, sz[k] < 0 ? 0.65f : 0.57f));
    cb[k + 4] = vadd(vadd(c, vadd(vmul(f, zt), vmul(r, sx[k] * hw * 0.66f))), vmul(u, 1.04f));
  }
  hexa(g, cb, 0xFF26323E, 0.05f);
  mbox(g, vadd(c, vmul(f, -0.47f)), f, r, u, 0.5f, hw * 0.64f, 1.02f, 1.07f, body, 0.1f);  // roof
  mbox(g, vadd(c, vmul(f, -2.1f)), f, r, u, 0.08f, hw * 1.0f, 0.16f, 0.34f, 0xFF1C1E22, 0.15f);  // diffuser
  // Spoiler on two struts.
  mbox(g, vadd(c, vmul(f, -1.95f)), f, r, u, 0.16f, hw * 0.98f, 0.86f, 0.92f, shade(body, 0.8f), 0.12f);
  for(int k = -1; k <= 1; k += 2) mbox(g, vadd(c, vadd(vmul(f, -1.95f), vmul(r, k * hw * 0.6f))), f, r, u, 0.05f, 0.04f, 0.64f, 0.86f, 0xFF202020, 0.11f);
  // Lights: a red light bar across the back, white-yellow in front.
  // Braking: the lights come up bright.
  mbox(g, vadd(c, vmul(f, -2.16f)), f, r, u, 0.03f, hw - 0.12f, 0.53f, 0.57f, braking ? 0xFFFF4030 : 0xFF9A1610, 0.19f);
  for(int k = -1; k <= 1; k += 2) {
    mbox(g, vadd(c, vadd(vmul(f, -2.17f), vmul(r, k * (hw - 0.3f)))), f, r, u, 0.03f, 0.22f, 0.5f, 0.6f, braking ? 0xFFFF7A60 : 0xFFD82418, 0.2f);
    mbox(g, vadd(c, vadd(vmul(f, 2.12f), vmul(r, k * (hw - 0.32f)))), f, r, u, 0.03f, 0.2f, 0.42f, 0.5f, 0xFFFFF4C8, 0.2f);
  }
  // Mirrors.
  for(int k = -1; k <= 1; k += 2)
    mbox(g, vadd(c, vadd(vmul(f, 0.42f), vmul(r, k * hw * 0.92f))), f, r, u, 0.08f, 0.1f, 0.68f, 0.78f, body, 0.06f);
  // Wheels: tyres with silver rims and spokes turning as it rolls; the front ones steer.
  float spin = metres / 0.34f;
  for(int i = -1; i <= 1; i += 2)
    for(int j = -1; j <= 1; j += 2) {
      V3 wc = vadd(c, vadd(vmul(f, 1.35f * i), vadd(vmul(r, j * (hw - 0.02f)), vmul(u, 0.34f))));
      float st = i > 0 ? yaw * 1.5f : 0;  // the front wheels point further into the turn
      V3 wf = vadd(vmul(f, cosf(st)), vmul(r, sinf(st))), wr = vnorm(vcross(wf, u));
      V3 out = vmul(wr, (float) j);
      frustum(g, vadd(wc, vmul(out, -0.13f)), out, wf, u, 0.34f, 0.34f, 0.26f, 8, 0xFF16161A, 0.03f);
      frustum(g, vadd(wc, vmul(out, 0.13f)), out, wf, u, 0.22f, 0.2f, 0.012f, 8, 0xFFB8BEC6, 0.04f);
      if(depth < 60)
        for(int sp = 0; sp < 3; sp++) {  // spokes
          float a = spin + sp * PI / 3;
          V3 d1 = vadd(vmul(wf, cosf(a)), vmul(u, sinf(a)));
          V3 d2 = vnorm(vcross(out, d1));
          mbox(g, vadd(wc, vmul(out, 0.15f)), d1, d2, out, 0.19f, 0.025f, -0.006f, 0.006f, 0xFF6A7078, 0.05f);
        }
    }
  // Nitro: flames from the exhausts, flickering.
  if(nitro) {
    for(int k = -1; k <= 1; k += 2) {
      uint32_t h = hash32((uint32_t) (tick * 31 + k + id * 7));
      float len = 1.1f + (h & 255) / 255.0f * 0.9f;
      V3 at = vadd(c, vadd(vmul(f, -2.2f), vadd(vmul(r, k * 0.45f), vmul(u, 0.3f))));
      V3 back = vmul(f, -1);
      nitro_flame(g, at, back, r, u, len);
    }
  }
}

// The pit lane (view3d_pits.inc): whether a place (segment, side in road half widths) is on it;
// whether a roadside object makes way for it; where the pit sign stands.
static bool pit_blocks(const TgTrack3D* tr, int seg, float side);
static bool pit_hides(const TgTrack3D* tr, int seg, uint16_t ow);
static float pit_sign_side(const TgTrack3D* tr, float side);

static void ground_detail(Gather* g, const TgTrack3D* tr, int seg, float p0, uint32_t ground, float depth) {
  int R = ground >> 16 & 255, G = ground >> 8 & 255, B = ground & 255;
  bool grass = G > R + 10 && G > B, snow = R > 185 && G > 185 && B > 185;
  for(int sideK = -1; sideK <= 1; sideK += 2) {
    uint32_t h = hash32((uint32_t) seg * 2654435761u ^ (sideK > 0 ? 0x5bd1e995u : 0x1b873593u));
    if((h & 255) > 110) continue;
    float along = ((h >> 8) & 255) / 255.0f * TG_TRACK3D_SEG_UNITS;
    float out = 1.45f + ((h >> 16) & 255) / 255.0f * 6.0f;
    if(pit_blocks(tr, seg, sideK * out)) continue;  // (not on the pit lane)
    TgTrackFrame fr = tg_track3d_frame(tr, p0 + along, sideK * out * TG_TRACK3D_SIDE_EDGE);
    V3 at = tv(fr.pos), f = vnorm(v3(fr.forward.x, 0, fr.forward.z)), r = vnorm(v3(fr.right.x, 0, fr.right.z)), up = v3(0, 1, 0);
    float size = 0.35f + ((h >> 24) & 255) / 255.0f * 0.6f;
    int what = grass ? ((h >> 3) & 3 ? 1 : 0) : snow ? ((h >> 3) & 1 ? 2 : 0) : ((h >> 3) & 7 ? 0 : 1);
    if(what == 1) {  // a tuft: a few blades leaning out
      uint32_t c = grass ? shade(mix(ground, 0xFF2E6A24, 0.5f), 0.85f) : 0xFF8A7A48;
      for(int k = 0; k < 3; k++) {
        V3 lean = vnorm(vadd(up, vadd(vmul(r, (k - 1) * 0.35f), vmul(f, ((h >> (k + 5)) & 1) ? 0.2f : -0.2f))));
        frustum(g, vadd(at, vmul(r, (k - 1) * 0.06f)), lean, f, r, 0.05f * size * 2, 0, size * (0.8f + k * 0.15f), 4, fogged(c, depth),
                0.001f * k);
      }
    } else {  // a rock (or a lump of snow): a squat, uneven solid
      uint32_t c = snow && what == 2 ? 0xFFF2F4F8 : mix(ground, 0xFF6E6A66, 0.55f);
      V3 q[8];
      static const float sx[4] = {-1, -1, 1, 1}, sz[4] = {-1, 1, 1, -1};
      for(int k = 0; k < 4; k++) {
        float jx = 0.7f + ((h >> (k * 3)) & 7) / 7.0f * 0.6f, jz = 0.7f + ((h >> (k * 3 + 12)) & 7) / 7.0f * 0.6f;
        q[k] = vadd(at, vadd(vmul(r, sx[k] * size * jx), vmul(f, sz[k] * size * jz * 0.8f)));
        q[k + 4] = vadd(vadd(at, vadd(vmul(r, sx[k] * size * jx * 0.55f), vmul(f, sz[k] * size * jz * 0.45f))), vmul(up, size * (0.5f + 0.1f * k)));
      }
      hexa(g, q, c, 0);
    }
  }
}

// What a roadside picture shows, from its colours and shape.
enum { ART_PICTURE, ART_CACTUS, ART_TREE, ART_BUSH, ART_CHEVRON, ART_WARNING, ART_LAMP, ART_PINE, ART_CYPRESS, ART_PITS };

// How wide a picture is (its opaque pixels) over a band of its rows, on average; and how
// much of that width is filled (a cactus's arms leave gaps, a tree's foliage doesn't).
static float rows_width(const TgV3Pic* p, float from, float to, float* fill) {
  int y0 = (int) (p->h * from), y1 = (int) (p->h * to), n = 0, sum = 0, solid = 0;
  for(int y = y0; y < y1 && y < p->h; y++, n++) {
    int l = -1, r = -1;
    for(int x = 0; x < p->w; x++)
      if(p->px[y * p->w + x] >> 24) {
        if(l < 0) l = x;
        r = x;
        solid++;
      }
    sum += l < 0 ? 0 : r - l + 1;
  }
  if(fill) *fill = sum ? (float) solid / sum : 0;
  return n ? (float) sum / n : 0;
}

// Which way a chevron board points (+1 right, -1 left): the end its arrows' tips are at is
// the end where the red spans the fewest rows.
static bool red_px(uint32_t c) { return (c >> 24) && (c >> 16 & 255) > 150 && (c >> 8 & 255) < 90 && (c & 255) < 110; }

static int red_rows(const TgV3Pic* p, int x0, int x1) {
  int lo = p->h, hi = -1;
  for(int y = 0; y < p->h; y++)
    for(int x = x0; x < x1; x++)
      if(red_px(p->px[y * p->w + x])) {
        lo = y < lo ? y : lo;
        hi = y > hi ? y : hi;
      }
  return hi < 0 ? 0 : hi - lo + 1;
}

static int chevron_dir(const TgV3Pic* p) {
  int a = p->w, b = -1;
  for(int i = 0; i < p->w * p->h; i++)
    if(red_px(p->px[i])) {
      int x = i % p->w;
      a = x < a ? x : a;
      b = x > b ? x : b;
    }
  if(b < 0) return 1;
  int t = (b - a + 1) / 4 > 1 ? (b - a + 1) / 4 : 1;
  return red_rows(p, a, a + t) > red_rows(p, b - t + 1, b + 1) ? 1 : -1;
}

// Which way a lamp's arm reaches (+1 right, -1 left): where its top is against its foot.
static int lamp_dir(const TgV3Pic* p) {
  float top = 0, bottom = 0;
  int nt = 0, nb = 0;
  for(int y = 0; y < p->h; y++)
    for(int x = 0; x < p->w; x++) {
      if(!(p->px[y * p->w + x] >> 24)) continue;
      if(y < p->h / 6) top += x, nt++;
      else if(y >= p->h * 2 / 3) bottom += x, nb++;
    }
  return nt && nb && top / nt > bottom / nb ? 1 : -1;
}

static int art_kind(const TgV3Pic* p) {
  int n = 0, green = 0, red = 0, white = 0, yellow = 0, greyish = 0, bottomGreen = 0, bottomN = 0;
  for(int y = 0; y < p->h; y++)
    for(int x = 0; x < p->w; x++) {
      uint32_t c = p->px[y * p->w + x];
      if(!(c >> 24)) continue;
      int R = c >> 16 & 255, G = c >> 8 & 255, B = c & 255;
      n++;
      bool gr = G > R + 12 && G > B;
      green += gr;
      red += R > 150 && G < 90 && B < 110;
      white += R > 200 && G > 200 && B > 200;
      yellow += R > 180 && G > 150 && B < 100;
      int mx = R > G ? (R > B ? R : B) : (G > B ? G : B), mn = R < G ? (R < B ? R : B) : (G < B ? G : B);
      greyish += mx - mn < 60 && mx > 90;
      if(y > p->h * 3 / 4) {
        bottomN++;
        bottomGreen += gr;
      }
    }
  if(n == 0) return ART_PICTURE;
  float aspect = (float) p->h / p->w;
  {  // The pit lane's sign: yellow letters (PITS) over a chequered flag.
    int ty = 0, tn = 0, bw = 0, bb = 0, bn = 0;
    for(int y = 0; y < p->h; y++)
      for(int x = 0; x < p->w; x++) {
        uint32_t c = p->px[y * p->w + x];
        if(!(c >> 24)) continue;
        int R = c >> 16 & 255, G = c >> 8 & 255, B = c & 255;
        if(y < p->h * 2 / 5) tn++, ty += R > 180 && G > 120 && B < 110;
        else if(y < p->h * 9 / 10) bn++, bw += R > 190 && G > 190 && B > 190, bb += R + G + B < 120;
      }
    if(tn && bn && ty > tn / 4 && bw > bn / 6 && bb > bn / 6 && aspect > 0.6f && aspect < 1.6f) return ART_PITS;
  }
  if(yellow > n / 5) return ART_WARNING;
  if(red > n / 14 && white > n / 8) return aspect > 1.8f ? ART_PICTURE : ART_CHEVRON;  // tall: a banner
  if(green > n * 2 / 5) {
    if(aspect < 1.15f) return ART_BUSH;
    if(bottomN && bottomGreen < bottomN / 3) return ART_TREE;
    // A cactus is widest at its arms, in the middle; a pine at the bottom of its branches.
    float fillTop, fillMid;
    float top = rows_width(p, 0.05f, 0.3f, &fillTop), mid = rows_width(p, 0.35f, 0.6f, &fillMid), low = rows_width(p, 0.65f, 0.9f, NULL);
    if(fillTop < 0.9f || fillMid < 0.85f) return ART_CACTUS;
    return low >= mid * 0.9f && top < low * 0.7f ? ART_PINE : ART_CYPRESS;
  }
  if(aspect > 2.5f && green < n / 10 && greyish > n / 2) return ART_LAMP;
  return ART_PICTURE;
}

// Text on a surface in the world: from at (the baseline's left end) along rx, glyphs hanging
// down dn; size in metres as ftext's in pixels.
static void wtext(Gather* g, const char* str, V3 at, V3 rx, V3 dn, float size, uint32_t col, int face, float track) {
  float k = size / FONT_SIZE, x = 0;
  for(const char* p = str; *p; p++) {
    int gi = glyph_of(face, *p);
    float gw = kFontGlyphs[gi].w, gh = kFontGlyphs[gi].h;
    if(gw > 0 && gh > 0) {
      float x0 = x + kFontGlyphs[gi].left * k, y0 = kFontGlyphs[gi].top * k, x1 = x0 + gw * k, y1 = y0 + gh * k;
      float u0 = (kFontGlyphs[gi].x + 0.5f) / FONT_AW, v0 = (kFontGlyphs[gi].y + 0.5f) / FONT_AH;
      float u1 = (kFontGlyphs[gi].x + gw - 0.5f) / FONT_AW, v1 = (kFontGlyphs[gi].y + gh - 0.5f) / FONT_AH;
      V3 a = vadd(at, vadd(vmul(rx, x0), vmul(dn, y0))), b = vadd(at, vadd(vmul(rx, x1), vmul(dn, y0)));
      V3 c = vadd(at, vadd(vmul(rx, x1), vmul(dn, y1))), d = vadd(at, vadd(vmul(rx, x0), vmul(dn, y1)));
      CV q[4] = {{to_cam(g->cam, a), u0, v0, 0}, {to_cam(g->cam, b), u1, v0, 0}, {to_cam(g->cam, c), u1, v1, 0}, {to_cam(g->cam, d), u0, v1, 0}};
      emit_poly(g, q, 4, col, FONT_PIC);
    }
    x += kFontGlyphs[gi].adv * k + track;
  }
}

// A rounded crown of foliage: an ellipsoid of stacked bands, radius rx, half height ry.
static void crown(Gather* g, V3 c, V3 up, V3 fl, V3 rl, float rx, float ry, uint32_t col) {
  static const float ang[6] = {-1.5707963f, -0.9f, -0.3f, 0.3f, 0.9f, 1.5707963f};
  for(int i = 0; i < 5; i++) {
    float y0 = sinf(ang[i]) * ry, y1 = sinf(ang[i + 1]) * ry, r0 = cosf(ang[i]) * rx, r1 = cosf(ang[i + 1]) * rx;
    frustum(g, vadd(c, vmul(up, y0)), up, fl, rl, r0, r1, y1 - y0, 9, shade(col, 0.88f + i * 0.06f), 0.01f + i * 0.002f);
  }
}

// How opaque a thing standing at base (radius r round its axis, height ht) is drawn: 1
// clear of the camera, down to 0 when the camera is inside or right next to it, or (wide: a
// tree, not a building along the street) when it covers most of the picture's width.
// The player's car in the view being drawn (camera space; its half width and height in
// metres), for near_fade: whatever stands between it and the camera is faded, as third-person
// games fade what hides the player (QA: Florence 4500, a tree hiding the car); the original
// draws its car over everything.
static bool g_occ_on;
static V3 g_occ_eye;          // the eye (world)
static V3 g_occ_rear, g_occ_f, g_occ_r;  // the car's box: its back's middle, ahead, right (level)
static float g_occ_y0;        // the ground under it
static float g_occ_lead;      // how far the car goes in three frames (m)
// The line from the eye to the car's middle (level, u), the distance along it to the middle and
// the car's body across it (b0..b1: its corners, w = u turned right).
static float g_occ_ux, g_occ_uz, g_occ_mid, g_occ_b0, g_occ_b1;

// Sets the player's car for occluder_fade (the eye, the car's back on the ground, its heading,
// its speed in m/s); false when the eye is too near the car to tell.
static bool occluder_setup(V3 eye, V3 rear, V3 fwd, V3 right, float ms) {
  g_occ_eye = eye, g_occ_rear = rear, g_occ_y0 = rear.y;
  g_occ_f = vnorm(v3(fwd.x, 0, fwd.z)), g_occ_r = vnorm(v3(right.x, 0, right.z));
  g_occ_lead = fminf(fmaxf(ms, 0.0f), 90.0f) * 3 / 60.0f;
  V3 mid = vadd(rear, vmul(g_occ_f, 2.15f));
  float ux = mid.x - eye.x, uz = mid.z - eye.z, ul = sqrtf(ux * ux + uz * uz);
  if(ul < 2.5f) return false;
  ux /= ul, uz /= ul;
  g_occ_ux = ux, g_occ_uz = uz, g_occ_mid = ul;
  g_occ_b0 = 1e9f, g_occ_b1 = -1e9f;
  // The body's box (a little over its size: the car turns in its lane): 4.5 m by 2 m.
  for(int k = 0; k < 4; k++) {
    V3 p = vadd(vadd(rear, vmul(g_occ_f, k & 1 ? 4.4f : -0.1f)), vmul(g_occ_r, k & 2 ? 1.0f : -1.0f));
    float b = (p.x - eye.x) * -uz + (p.z - eye.z) * ux;
    g_occ_b0 = fminf(g_occ_b0, b), g_occ_b1 = fmaxf(g_occ_b1, b);
  }
  return true;
}

// How opaque a thing (a cylinder round `base`, radius r, height ht) may be drawn so it does not
// hide the player's car or stand inside it: the original draws its car over everything, and
// third-person games fade what comes between the camera and the player. World-space, per thing,
// two tests:
// - between: nearer the eye than the car's middle and within the car's width of the line from
//   the eye to it (its radius added), at any height - gone, fading over the last 0.5 m across.
//   A tube, not the cone the eye sees the car in: a thing that has come over the car stays gone
//   until it is past the camera, rather than fading back in under the car on its way out of the
//   picture as a ghost (session-20261102-195339 9950-9960).
// - in the car: its foot within the car's box (its width added across: the game's boards face
//   the road), its height over the car's floor - a board the car runs into (QA-029). Faded over
//   0.5 m and three frames' travel ahead of it, so it is gone as they touch, not a ghost fading
//   through the car after (at 83 km/h a board went from clear to inside in two frames).
// Any thing, however thin: the 12 sight lines of the last version missed a marker post between
// them (session-20261102-195339 9936-9942), the screen boxes at the foot of the one before a
// tree whose crown, not its foot, came between (Los Angeles 3596-3620). One dot product for
// everything beyond the car or behind the eye, the most of them.
static float occluder_fade(const Cam* cam, V3 base, float r, float ht) {
  (void) cam;
  if(!g_occ_on) return 1;
  float dx = base.x - g_occ_eye.x, dz = base.z - g_occ_eye.z, rr = fminf(r, 1.5f);
  float a = dx * g_occ_ux + dz * g_occ_uz;  // along the line to the car
  // Beyond the car's nose and its lead (the box below reaches 2.45 m past its middle, rr more
  // across), or behind the eye (near_fade's): untouched.
  if(a > g_occ_mid + 3.0f + g_occ_lead + rr || a + r < 0.5f) return 1;
  float y0 = base.y, y1 = base.y + ht, k = 1;
  if(y0 < g_occ_y0 + 1.4f && y1 > g_occ_y0) {
    V3 d = vsub(base, g_occ_rear);
    float along = vdot(d, g_occ_f), across = fabsf(vdot(d, g_occ_r));
    float gap = fmaxf(fmaxf((along - 4.6f) * 0.5f / (0.5f + g_occ_lead), -0.4f - along), across - 1.0f - rr);
    if(gap <= 0) return 0;
    if(gap < 0.5f) k = gap / 0.5f;
  }
  if(a < g_occ_mid && y1 > g_occ_y0 - 0.5f) {
    float b = dx * -g_occ_uz + dz * g_occ_ux;
    float gap = fmaxf(g_occ_b0 - (b + r), (b - r) - g_occ_b1);
    if(gap <= 0) return 0;
    if(gap < 0.5f) k = fminf(k, gap / 0.5f);
  }
  return k * k * (3 - 2 * k);
}

static float near_fade(const Cam* cam, V3 base, float r, float ht, bool wide) {
  float occ = occluder_fade(cam, base, r, ht);
  if(occ <= 0) return 0;
  float dx = base.x - cam->eye.x, dz = base.z - cam->eye.z, d = sqrtf(dx * dx + dz * dz);
  // Its top below the camera's eye and not ahead of it: no matter.
  if(base.y + ht < cam->eye.y - 0.5f && d > r + 1.0f) return occ;
  // A see-through thing as big as the picture is a ghost (QA: Stockholm 3900, Copenhagen
  // 3300): the fade is short and eased, so a near object spends a few frames see-through,
  // faint, rather than a long stretch half there (a building over a third of the picture for 8
  // frames, Los Angeles 3896-3904; a house at Sheffield 4776-4790: the ramps were 3 m and 12 %).
  float gap = d - r;  // metres between the camera and its side
  float k = (gap - 3.5f) / 1.5f;  // fully gone within 3.5 m, fully there 5 m off
  // A big thing close up (its width on screen over 36 % of the picture's): fades too.
  V3 c = to_cam(cam, vadd(base, v3(0, ht * 0.5f, 0)));
  if(wide && c.z > 0.1f) {
    float frac = 2 * r * cam->f / c.z / cam->w;
    float kw = (0.42f - frac) / 0.06f;  // there below 36 % of the width, gone at 42 %
    if(kw < k) k = kw;
  }
  k = k < 0 ? 0 : k > 1 ? 1 : k;
  return fminf(occ, k * k * (3 - 2 * k));
}

// A car as drawn, for car_cover: the middle of its body on the road, its axes (level ahead,
// right, up).
typedef struct { V3 c, f, r, u; } CarBox;

static CarBox car_box(TgTrackFrame fr, float yaw) {
  V3 f0 = tv(fr.forward), r0 = tv(fr.right), u = tv(fr.up);
  V3 f = vadd(vmul(f0, cosf(yaw)), vmul(r0, sinf(yaw)));
  return (CarBox) {vadd(tv(fr.pos), vmul(f0, 2.15f)), f, vnorm(vcross(f, u)), u};  // (as modern_car places it)
}

// How much of the player's car p another car o hides from the eye, as it is seen: p's body as a
// box (4.2 x 1.76 m, 1.1 m high: to its deck), each face turned to the eye sampled at 24 points
// (at most three faces: from the chase camera its back, mostly, and its top), each point weighed
// by the solid angle its share of the face takes up (area x cos / distance^2, so the share is
// of the car's picture, not of its size); the share of that whose sight line from the eye goes
// through o's body on the way: its lower body (4.3 x 1.86 m, 0.96 m high) or its cabin (2 x 1.5
// m, up to 1.32 m) - one 1.3 m box over its whole length took a car just under the camera,
// whose bonnet the lines pass over, for one hiding a quarter of the car (session-20261102-192824
// 4203). Third-person games fade what comes between the camera and the player (occluder_fade);
// a car right behind the player's (a rival held a car's length back after a rear push, sep_room)
// sits on those lines and hid the lower half of its back. A car beside or behind in the next
// lane crosses an edge at most; one beyond the player's car none.
static bool seg_box(const float e[3], const float d[3], const float half[3]) {  // slab test, s in 0..1
  float s0 = 0, s1 = 1;
  for(int k = 0; k < 3 && s0 <= s1; k++) {
    if(fabsf(d[k]) < 1e-6f) {
      if(fabsf(e[k]) > half[k]) return false;
      continue;
    }
    float ta = (-half[k] - e[k]) / d[k], tb = (half[k] - e[k]) / d[k];
    s0 = fmaxf(s0, fminf(ta, tb)), s1 = fminf(s1, fmaxf(ta, tb));
  }
  return s0 <= s1;
}

static float car_cover(V3 eye, const CarBox* p, const CarBox* o) {
  static const float kBody[3] = {2.15f, 0.93f, 0.48f}, kCab[3] = {1.0f, 0.75f, 0.2f}, kP[3] = {2.1f, 0.88f, 0.55f};
  V3 eo = vsub(eye, vadd(o->c, vmul(o->u, kBody[2])));
  float e[3] = {vdot(eo, o->f), vdot(eo, o->r), vdot(eo, o->u)};  // the eye from o's lower body, in its frame
  float ec[3] = {e[0] + 0.2f, e[1], e[2] - 0.64f};                 // from its cabin (0.2 m back of the middle)
  V3 pc = vadd(p->c, vmul(p->u, kP[2])), ax[3] = {p->f, p->r, p->u};
  float all = 0, hid = 0;
  for(int face = 0; face < 6; face++) {
    int a = face >> 1, a1 = (a + 1) % 3, a2 = (a + 2) % 3;
    V3 n = vmul(ax[a], face & 1 ? 1.0f : -1.0f), fc = vadd(pc, vmul(n, kP[a]));
    if(vdot(n, vsub(eye, fc)) <= 0) continue;  // turned away
    float area = 4 * kP[a1] * kP[a2] / 24;
    for(int i = 0; i < 24; i++) {  // (the middles of 6 x 4 cells)
      V3 q = vadd(fc, vadd(vmul(ax[a1], ((float) (i % 6) - 2.5f) / 3 * kP[a1]), vmul(ax[a2], ((float) (i / 6) - 1.5f) / 2 * kP[a2])));
      V3 d = vsub(q, eye);
      float dd = vdot(d, d), w = area * -vdot(n, d) / (dd * sqrtf(dd));
      if(w <= 0) continue;
      all += w;
      float dl[3] = {vdot(d, o->f), vdot(d, o->r), vdot(d, o->u)};  // the segment eye..q in o's frame
      if(seg_box(e, dl, kBody) || seg_box(ec, dl, kCab)) hid += w;
    }
  }
  return all > 0 ? hid / all : 0;
}

// A roadside object in the modern look: a model of its kind, else its picture.
static void modern_object(Gather* g, TgView3D* v, int key, V3 base, V3 f, V3 r, float w, float h, int curve) {
  const TgV3Pic* pic = &v->pics[ART_PIC + key];
  if(v->art_kinds[key] == 0 || v->art_kind_version[key] != pic->version) {
    int k = key >= 256 ? ART_PICTURE : art_kind(pic);  // groups (the start banner) stay pictures
    int dir = k == ART_CHEVRON ? chevron_dir(pic) : k == ART_LAMP ? lamp_dir(pic) : 1;
    v->art_kinds[key] = (uint8_t) ((k + 1) | (dir > 0 ? 0x80 : 0));
    v->art_kind_version[key] = pic->version;
  }
  int kind = (v->art_kinds[key] & 0x7F) - 1;
  float dir = v->art_kinds[key] & 0x80 ? 1.0f : -1.0f;  // which way it points or reaches
  (void) curve;
  V3 up = v3(0, 1, 0);
  V3 fl = vnorm(v3(f.x, 0, f.z)), rl = vnorm(v3(r.x, 0, r.z));
  float depth = to_cam(g->cam, base).z;
  uint32_t green = v->art_colour[key] ? v->art_colour[key] : 0xFF3C8A3A;
  switch(kind) {
    case ART_CACTUS: {
      shadow(g, base, fl, rl, w * 0.35f, w * 0.35f, 0.3f);
      float t = w * 0.11f;
      frustum(g, base, up, fl, rl, t, t * 0.85f, h * 0.95f, 8, green, 0);
      frustum(g, vadd(base, vmul(up, h * 0.95f)), up, fl, rl, t * 0.85f, 0, t, 8, shade(green, 1.1f), 0);
      for(int s2 = -1; s2 <= 1; s2 += 2) {
        float ay = h * (s2 < 0 ? 0.38f : 0.5f), ax = w * 0.3f;
        frustum(g, vadd(base, vmul(up, ay)), vmul(rl, (float) s2), fl, up, t * 0.7f, t * 0.7f, ax, 6, green, 0.01f);
        frustum(g, vadd(base, vadd(vmul(rl, s2 * ax), vmul(up, ay))), up, fl, rl, t * 0.7f, t * 0.6f, h * 0.28f, 6, green, 0.01f);
      }
      break;
    }
    case ART_TREE: {
      // A broadleaf: a trunk and a rounded, lumpy crown of three blobs.
      shadow(g, base, fl, rl, w * 0.5f, w * 0.5f, 0.32f);
      frustum(g, base, up, fl, rl, w * 0.07f, w * 0.05f, h * 0.5f, 6, 0xFF5A3E28, 0);
      uint32_t hh = hash32((uint32_t) (base.x * 7.0f) * 2654435761u ^ (uint32_t) (base.z * 13.0f));
      float sx = ((hh & 1) ? 1.0f : -1.0f);
      crown(g, vadd(base, vmul(up, h * 0.62f)), up, fl, rl, w * 0.42f, h * 0.34f, shade(green, 0.86f));
      crown(g, vadd(vadd(base, vmul(up, h * 0.52f)), vmul(rl, sx * w * 0.24f)), up, fl, rl, w * 0.28f, h * 0.24f, shade(green, 0.8f));
      crown(g, vadd(vadd(base, vmul(up, h * 0.78f)), vmul(rl, -sx * w * 0.12f)), up, fl, rl, w * 0.3f, h * 0.22f, shade(green, 1.02f));
      break;
    }
    case ART_PINE:
      // Tiers of branches narrowing to the top, on a short trunk.
      shadow(g, base, fl, rl, w * 0.5f, w * 0.5f, 0.32f);
      frustum(g, base, up, fl, rl, w * 0.06f, w * 0.05f, h * 0.2f, 6, 0xFF4E3624, 0);
      for(int t = 0; t < 3; t++) {
        float y0 = h * (0.12f + t * 0.27f), r0 = w * (0.5f - t * 0.12f);
        frustum(g, vadd(base, vmul(up, y0)), up, fl, rl, r0, 0, h * (0.42f - t * 0.04f), 8, shade(green, 0.8f + t * 0.12f), 0.01f + t * 0.01f);
      }
      break;
    case ART_CYPRESS: {
      // A tall evergreen: a spindle of foliage, widest a third of the way up, on a short trunk.
      shadow(g, base, fl, rl, w * 0.45f, w * 0.45f, 0.32f);
      frustum(g, base, up, fl, rl, w * 0.07f, w * 0.06f, h * 0.15f, 6, 0xFF4E3624, 0);
      static const float ys[5] = {0.08f, 0.22f, 0.45f, 0.75f, 1.0f}, rs[5] = {0.3f, 0.48f, 0.46f, 0.3f, 0.0f};
      for(int t = 0; t < 4; t++)
        frustum(g, vadd(base, vmul(up, h * ys[t])), up, fl, rl, w * rs[t], w * rs[t + 1], h * (ys[t + 1] - ys[t]), 8,
                shade(green, 0.82f + t * 0.08f), 0.01f + t * 0.002f);
      break;
    }
    case ART_BUSH:
      shadow(g, base, fl, rl, w * 0.5f, w * 0.5f, 0.28f);
      frustum(g, base, up, fl, rl, w * 0.46f, w * 0.38f, h * 0.55f, 8, shade(green, 0.9f), 0);
      frustum(g, vadd(base, vmul(up, h * 0.55f)), up, fl, rl, w * 0.38f, w * 0.12f, h * 0.4f, 8, green, 0.01f);
      break;
    case ART_CHEVRON:
    case ART_WARNING: {
      // A post and a board facing the oncoming driver; chevrons point the way the picture's do.
      frustum(g, base, up, fl, rl, 0.06f, 0.06f, h * 0.45f, 6, 0xFFB4B8C0, 0);
      float bw = kind == ART_WARNING ? h * 0.55f : w, bh = kind == ART_WARNING ? bw : h * 0.5f, by = h - bh;
      V3 bc = vadd(base, vmul(up, by));
      V3 tl = vadd(bc, vadd(vmul(rl, -bw / 2), vmul(up, bh))), tr = vadd(bc, vadd(vmul(rl, bw / 2), vmul(up, bh)));
      V3 br = vadd(bc, vmul(rl, bw / 2)), bl = vadd(bc, vmul(rl, -bw / 2));
      begin_item(g, depth - STAND_BIAS - 0.05f);
      int tex = kind == ART_WARNING ? TEX_WARNING : dir > 0 ? TEX_CHEVRON_R : TEX_CHEVRON_L;
      if(vdot(fl, vsub(base, g->cam->eye)) > 0) tex_quad(g, tl, tr, br, bl, tex, fogged(0xFFFFFFFF, depth), 2);
      else quad(g, tl, tr, br, bl, fogged(0xFF9098A0, depth));  // its back
      break;
    }
    case ART_PITS: {
      // The pit lane's sign, modelled: two posts, a dark board with PITS in the HUD's italic
      // in gold, a chequered band under it, facing the oncoming driver.
      float bw = w, bh = h * 0.7f, by = h - bh;
      for(int k = -1; k <= 1; k += 2)
        frustum(g, vadd(base, vmul(rl, k * bw * 0.36f)), up, fl, rl, 0.07f, 0.06f, by + 0.05f, 6, 0xFF8E949C, 0);
      V3 bc = vadd(base, vmul(up, by));
      mbox(g, vadd(bc, vmul(fl, 0.07f)), fl, rl, up, 0.06f, bw / 2 + 0.06f, -0.06f, bh + 0.06f, 0xFF20262E, 0.01f);
      begin_item(g, depth - STAND_BIAS - 0.05f);
      if(vdot(fl, vsub(base, g->cam->eye)) <= 0) break;  // its back: the box is enough
      V3 lift = vmul(fl, -0.012f), x0 = vadd(vadd(bc, vmul(rl, -bw / 2)), lift);
      // Board, the chequer (two rows), the word.
      quad(g, vadd(x0, vmul(up, bh)), vadd(vadd(x0, vmul(rl, bw)), vmul(up, bh)), vadd(x0, vmul(rl, bw)), x0, fogged(0xFF141A22, depth));
      int cols = 8;
      float cw = bw / cols, ch = bh * 0.16f;
      V3 l2 = vmul(fl, -0.02f);
      for(int r2 = 0; r2 < 2; r2++)
        for(int c2 = 0; c2 < cols; c2++) {
          V3 a = vadd(vadd(vadd(x0, l2), vmul(rl, c2 * cw)), vmul(up, bh * 0.08f + r2 * ch));
          uint32_t col = ((r2 + c2) & 1) ? 0xFFF4F6F8 : 0xFF0A0C10;
          quad(g, vadd(a, vmul(up, ch)), vadd(vadd(a, vmul(rl, cw)), vmul(up, ch)), vadd(a, vmul(rl, cw)), a, fogged(col, depth));
        }
      font_ready(v);
      float tsz = bh * 0.42f, tw = ftext_w("PITS", tsz, 2, 0);
      V3 at = vadd(vadd(vadd(x0, l2), vmul(rl, (bw - tw) / 2)), vmul(up, bh * 0.48f));
      wtext(g, "PITS", at, rl, vmul(up, -1), tsz, fogged(0xFFFFC93C, depth), 2, 0);
      break;
    }
    case ART_LAMP: {
      // A street lamp: a tapering pole, an arm reaching out the way the picture's does, and
      // a lit head.
      float reach = fmaxf(w * 0.8f, h * 0.18f);
      V3 armDir = vmul(rl, dir);
      frustum(g, base, up, fl, rl, 0.1f, 0.06f, h * 0.98f, 6, 0xFF8E949C, 0);
      frustum(g, vadd(base, vmul(up, h * 0.96f)), armDir, fl, up, 0.05f, 0.04f, reach, 5, 0xFF8E949C, 0.01f);
      V3 head = vadd(base, vadd(vmul(armDir, reach), vmul(up, h * 0.94f)));
      mbox(g, head, fl, armDir, up, 0.18f, 0.32f, 0.0f, 0.12f, 0xFF6A7078, 0.02f);
      mbox(g, head, fl, armDir, up, 0.12f, 0.26f, -0.03f, 0.0f, 0xFFFFF6D8, 0.03f);
      if(g_night) {  // its light: a glow at the head
        begin_item(g, depth - STAND_BIAS - 0.2f);
        billboard(g, vadd(head, v3(0, -1.2f, 0)), 3.2f, 2.6f, TEX_GLOW);
      }
      break;
    }
    default: {
      // A solid, rectangular picture (an advert, a banner) is built: a framed board facing
      // the road on two posts, or a tall banner on a pole. Anything else stays the picture.
      static uint16_t fillVer[512];
      static uint8_t fill[512];
      static uint32_t domCol[512];
      if(key < 512 && pic->w && fillVer[key] != (uint16_t) (pic->version + 1)) {
        int n = 0;
        Hist hh = {0};
        for(int i = 0; i < pic->w * pic->h; i++)
          if(pic->px[i] >> 24) {
            n++;
            int R = pic->px[i] >> 16 & 255, G = pic->px[i] >> 8 & 255, B = pic->px[i] & 255;
            int mx = R > G ? (R > B ? R : B) : (G > B ? G : B), mn = R < G ? (R < B ? R : B) : (G < B ? G : B);
            if(mx - mn > 50) hist_add(&hh, pic->px[i], 1);  // its colourful part: the brand's colour
          }
        fill[key] = (uint8_t) (n * 100 / (pic->w * pic->h));
        domCol[key] = 0xFF000000u | hist_top(&hh, 0, 0);
        if(domCol[key] == 0xFF000000u) domCol[key] = 0xFF3A5A8A;
        fillVer[key] = (uint16_t) (pic->version + 1);
      }
      if(key >= 256 || key >= 512 || fill[key] < 88 || !pic->w) {
        billboard(g, base, w, h, ART_PIC + key);
        break;
      }
      bool banner = h > w * 1.8f;
      float bw = w, bh = banner ? h * 0.82f : h * 0.62f, by = h - bh;
      V3 bc = vadd(base, vmul(up, by));
      if(banner) {
        frustum(g, vadd(base, vmul(rl, -bw / 2 - 0.06f)), up, fl, rl, 0.06f, 0.05f, h * 1.02f, 6, 0xFFB8BEC6, 0);
        mbox(g, vadd(base, vmul(rl, -0.03f)), fl, rl, up, 0.03f, bw / 2 + 0.1f, h * 0.99f, h * 1.02f, 0xFFB8BEC6, 0.01f);
      } else {
        for(int k = -1; k <= 1; k += 2)
          frustum(g, vadd(base, vmul(rl, k * bw * 0.32f)), up, fl, rl, 0.08f, 0.07f, by + 0.05f, 6, 0xFF7C848E, 0);
        mbox(g, vadd(bc, vmul(fl, 0.07f)), fl, rl, up, 0.06f, bw / 2 + 0.08f, -0.08f, bh + 0.08f, 0xFF3A424C, 0.01f);  // the frame
      }
      V3 tl = vadd(bc, vadd(vmul(rl, -bw / 2), vmul(up, bh))), tr = vadd(bc, vadd(vmul(rl, bw / 2), vmul(up, bh)));
      V3 br = vadd(bc, vmul(rl, bw / 2)), bl = vadd(bc, vmul(rl, -bw / 2));
      V3 lift = vmul(fl, -0.01f);
      begin_item(g, depth - STAND_BIAS - 0.05f);
      bool front = vdot(fl, vsub(base, g->cam->eye)) > 0;
      if(banner) {
        tex_quad(g, vadd(tl, lift), vadd(tr, lift), vadd(br, lift), vadd(bl, lift), ART_PIC + key, fogged(0xFFFFFFFF, depth), 2);
      } else if(!front) {
        quad(g, tl, tr, br, bl, fogged(0xFF59616B, depth));  // its back
      } else {
        // A sponsor's board, redrawn: the advert's own colour as a deep panel, a light stripe,
        // a made-up sponsor's name in the HUD's italic.
        static const char* kSponsor[8] = {"NITRO", "APEX OIL", "VELOX", "KESTREL", "TURBO-X", "GRIDLINE", "HALCYON", "ORBITAL"};
        // each board its own (by where it stands, so the same every lap)
        const char* name = kSponsor[hash32((uint32_t) key * 2654435761u ^ (uint32_t) (int) floorf(base.x * 0.5f) * 0x85EBCA6Bu ^
                                           (uint32_t) (int) floorf(base.z * 0.5f) * 0xC2B2AE35u) & 7];
        uint32_t dom = key < 512 ? domCol[key] : 0xFF3A5A8A;
        uint32_t bg = fogged(mix(shade(dom, 0.75f), 0xFF101A2A, 0.35f), depth), stripe = fogged(mix(dom, 0xFFFFFFFF, 0.65f), depth);
        quad(g, vadd(tl, lift), vadd(tr, lift), vadd(br, lift), vadd(bl, lift), bg);
        V3 lift2 = vmul(fl, -0.02f);
        V3 s0 = vadd(bl, vmul(up, bh * 0.12f)), s1 = vadd(br, vmul(up, bh * 0.12f));
        quad(g, vadd(vadd(s0, vmul(up, bh * 0.06f)), lift2), vadd(vadd(s1, vmul(up, bh * 0.06f)), lift2), vadd(s1, lift2), vadd(s0, lift2), stripe);
        font_ready(v);
        float unit = ftext_w(name, 1, 2, 0), size = fminf(bw * 0.78f / fmaxf(unit, 0.01f), bh * 0.55f);
        V3 at = vadd(vadd(bc, vadd(vmul(rl, -unit * size / 2), vmul(up, bh * 0.36f))), lift2);
        wtext(g, name, at, rl, vmul(up, -1), size, fogged(0xFFF4F8FA, depth), 2, 0);
      }
      break;
    }
  }
}

// More of the course's own scenery out on the open ground: its cacti, trees, bushes (the
// kinds its roadside pictures show), scattered off the road, the same every lap.
// How much of an object of picture key shows: one learned from the screen just now fades in
// and grows to its size over about a third of a second, rather than appearing at once all
// along the road (QA-009; an LOD cross-fade, done with alpha); *grow its scale.
static float object_fade(const TgView3D* v, int key, float* grow) {
  float f = v->art_fade[key] / 255.0f;
  *grow = 0.8f + 0.2f * f;
  return f;
}

#define SCENERY_FAR 260.0f  // the extra scenery is drawn this near (m); it fades out over the last 60

static void scenery(Gather* g, TgView3D* v, const TgTrack3D* tr, int seg, float p0, float depth) {
  int keys[64], n = 0;
  for(int key = 0; key < 256 && n < 64; key++) {
    if(v->art[key].n == 0 || !v->pics[ART_PIC + key].w) continue;
    const TgV3Pic* pic = &v->pics[ART_PIC + key];
    if(v->art_kinds[key] == 0 || v->art_kind_version[key] != pic->version) {
      int k = art_kind(pic);
      int dir = k == ART_CHEVRON ? chevron_dir(pic) : k == ART_LAMP ? lamp_dir(pic) : 1;
      v->art_kinds[key] = (uint8_t) ((k + 1) | (dir > 0 ? 0x80 : 0));
      v->art_kind_version[key] = pic->version;
    }
    int kind = (v->art_kinds[key] & 0x7F) - 1;
    if(kind == ART_CACTUS || kind == ART_TREE || kind == ART_BUSH || kind == ART_PINE || kind == ART_CYPRESS) keys[n++] = key;
  }
  if(n == 0) return;
  for(int sideK = -1; sideK <= 1; sideK += 2) {
    uint32_t h = hash32((uint32_t) seg * 0x9E3779B1u ^ (sideK > 0 ? 0xA511E9B3u : 0x63D83595u));
    if((h & 255) > 70) continue;
    // Which picture stands here: the key with the highest hash for this place (rendezvous
    // hashing). With keys[(h >> 8) % n], every place changed its kind at once when a new picture
    // was learned (n grew): a cactus vanished and a tree faded in all along the road (QA-009).
    // Now a new key takes only its share of the places, cross-faded from the one before.
    int key = -1, prev = -1;
    uint32_t bw = 0, pw = 0;
    for(int i = 0; i < n; i++) {
      uint32_t wgt = hash32(h ^ (uint32_t) keys[i] * 0x27D4EB2Fu);
      if(key < 0 || wgt > bw) prev = key, pw = bw, key = keys[i], bw = wgt;
      else if(prev < 0 || wgt > pw) prev = keys[i], pw = wgt;
    }
    float out = 2.6f + ((h >> 13) & 255) / 255.0f * 8.0f;  // road half widths: beyond the roadside row
    if(pit_blocks(tr, seg, sideK * out)) continue;  // (not in the pits)
    float along = ((h >> 21) & 31) / 31.0f * TG_TRACK3D_SEG_UNITS;
    float scale = 0.75f + ((h >> 26) & 31) / 31.0f * 0.6f;
    TgTrackFrame of = tg_track3d_frame(tr, p0 + along, sideK * out * TG_TRACK3D_SIDE_EDGE);
    float far = (SCENERY_FAR - depth) / 60.0f, farK = far < 0 ? 0 : far > 1 ? 1 : far;  // (fading out where it stops: no hard edge)
    // Between the camera and the player's car, in it, or right by the camera: faded as the
    // roadside objects are (these had no fade at all; LA 3596-3620, Sheffield 4800).
    {
      static FadeMem scMem[2][TG_TRACK3D_MAX_SEGMENTS][2];
      const ArtInfo* ai0 = &v->art[key];
      const TgV3Pic* pic0 = &v->pics[ART_PIC + key];
      float oh0 = ai0->sum_h / ai0->n * scale, ow0 = oh0 * pic0->w / pic0->h;
      farK *= fade_mem(&scMem[g_view_k][seg][sideK > 0], near_fade(g->cam, tv(of.pos), ow0 * 0.5f, oh0, true));
    }
    float grow, fa = object_fade(v, key, &grow), rest = 1 - fa;
    for(int pass = 0; pass < 2; pass++) {
      int k = pass ? prev : key;
      if(k < 0) continue;
      float g2, a = pass ? rest * object_fade(v, k, &g2) : fa;
      if(pass) grow = 1;
      const ArtInfo* ai = &v->art[k];
      const TgV3Pic* pic = &v->pics[ART_PIC + k];
      float oh = ai->sum_h / ai->n * scale * grow, ow = oh * pic->w / pic->h;
      g_alpha = a * farK;
      if(g_alpha > 0.01f) modern_object(g, v, k, tv(of.pos), tv(of.forward), tv(of.right), ow, oh, 0);
    }
    g_alpha = 1;
  }
}

// The city's landmark (QA-007), below: its kind, and the part of it that must stay in sight
// (its letters, spire, towers) as offsets from its foot in units of its height H on screen.
static int city_landmark(int course);
static void landmark_box(int kind, float* x0, float* x1, float* y0);

#include "view3d_pits.inc"
#include "view3d_stages.inc"

// The sky of the modern look: deep at the top, pale and hazy at the horizon, a sun with its
// glow; the course's skyline in the haze.
static void screen_rect(TgV3List* l, float x0, float y0, float x1, float y1, float u0, float u1, uint32_t top, uint32_t bottom, int pic);

static const struct CountryLight { const char* name; uint32_t top, glow; } kCountry[8];
static void modern_sky(TgV3List* out, const TgView3D* v, const Cam* cam, float w, float h, float hy, float bandH,
                       uint32_t skyTop, uint32_t skyLow) {
  // The game's own sky colour, deepened a little overhead and paler towards the horizon,
  // with a touch of the country's light (the concept art's). The original's skies are clear
  // saturated cyan: mostly theirs, not washed out to grey (retro audit R-09; Art tunes).
  uint32_t ctop = kCountry[(v->course >> 2) & 7].top, cglow = kCountry[(v->course >> 2) & 7].glow;
  // Art direction (docs/STYLE.md): the game's saturated cyan kept near the horizon (only a
  // little paler, a touch of the country's glow), deepened overhead.
  // (a horizon colour picked up from the original's hills is replaced before it is eased: see
  // tg_view3d_draw_view)
  uint32_t zenith = mix(shade(skyTop, 0.8f), ctop, 0.1f);
  uint32_t horizon = mix(mix(skyLow, 0xFFF4F6FA, 0.16f), cglow, 0.08f);
  if(g_night) {
    // Night: deep navy, the city's purple glow low down, stars, a moon fixed in the world.
    // A dusk the game paints red (Hiroshima's start: a dark red sky) glows red low down.
    uint32_t glowMid = 0xFF221A3E, glowLow = 0xFF3A2650;
    {
      uint32_t raw = hist_top(&v->sky_top, 0, 0);
      int R = raw >> 16 & 255, G = raw >> 8 & 255, B = raw & 255;
      if(raw && R > B + 24 && R > G + 24) {
        glowMid = mix(glowMid, 0xFF000000u | raw, 0.6f);
        glowLow = mix(glowLow, 0xFF000000u | lerp_colour(raw, 0xFFFF7040, 0.35f), 0.8f);
      }
    }
    g_horizon = glowLow;
    screen_rect(out, 0, 0, w, hy - bandH * 0.4f, 0, 1, 0xFF03060F, glowMid, -1);
    screen_rect(out, 0, hy - bandH * 0.4f - 1, w, hy + 1, 0, 1, glowMid, glowLow, -1);
    for(int i = 0; i < 220; i++) {
      uint32_t hh = hash32(0x57A2 + (uint32_t) i * 2654435761u);
      float az = (hh & 4095) / 4096.0f * 2 * PI, el = 0.08f + ((hh >> 12) & 1023) / 1024.0f * 0.9f;
      V3 dir = vnorm(v3(cosf(az) * cosf(el), sinf(el), sinf(az) * cosf(el)));
      V3 c = to_cam(cam, vadd(cam->eye, dir));
      if(c.z < 0.05f) continue;
      float px = cam->cx + c.x / c.z * cam->f, py = cam->cy - c.y / c.z * cam->f;
      if(px < 0 || px > w || py > hy - bandH * 0.6f) continue;
      float r = (0.6f + ((hh >> 22) & 3) * 0.5f) * h / 1080.0f;
      int a = 120 + (int) ((hh >> 24) & 127);
      screen_rect(out, px - r, py - r, px + r, py + r, 0, 1, with_alpha(0xFFDDE6FF, a), with_alpha(0xFFDDE6FF, a), -1);
    }
    V3 md = vnorm(v3(0.5f, 0.35f, 0.75f));
    V3 mc = to_cam(cam, vadd(cam->eye, md));
    if(mc.z > 0.05f) {
      float mx = cam->cx + mc.x / mc.z * cam->f, my = cam->cy - mc.y / mc.z * cam->f, r = h * 0.035f;
      screen_rect(out, mx - r * 6, my - r * 6, mx + r * 6, my + r * 6, 0, 1, 0x40A8B8FF, 0x40A8B8FF, TEX_GLOW);
      screen_rect(out, mx - r * 1.4f, my - r * 1.4f, mx + r * 1.4f, my + r * 1.4f, 0, 1, 0xFFF4F2E8, 0xFFF4F2E8, TEX_GLOW);
    }
    (void) v;
    return;
  }
  g_horizon = horizon;
  screen_rect(out, 0, 0, w, hy - bandH * 0.4f, 0, 1, zenith, horizon, -1);
  screen_rect(out, 0, hy - bandH * 0.4f - 1, w, hy + 1, 0, 1, horizon, horizon, -1);
  // The sun, fixed in the world (so it swings round as the road turns).
  V3 sunDir = vnorm(v3(-0.55f, 0.28f, -0.8f));
  V3 sc = to_cam(cam, vadd(cam->eye, sunDir));
  if(sc.z > 0.05f) {
    float sx = cam->cx + sc.x / sc.z * cam->f, sy = cam->cy - sc.y / sc.z * cam->f;
    float r1 = h * 0.42f, r2 = h * 0.05f;
    screen_rect(out, sx - r1, sy - r1, sx + r1, sy + r1, 0, 1, 0x70FFF2D2, 0x70FFF2D2, TEX_GLOW);
    screen_rect(out, sx - r2 * 2, sy - r2 * 2, sx + r2 * 2, sy + r2 * 2, 0, 1, 0xFFFFFDF2, 0xFFFFFDF2, TEX_GLOW);
  }
  // Clouds, fixed in the world like the sun (they turn with the camera's heading only), drifting;
  // lower ones paler, in the haze.
  for(int i = 0; i < 14; i++) {
    uint32_t hsh = hash32(0xC1009 + (uint32_t) i * 7919);
    if(i % 3 == 1) continue;  // a clear sky: fewer clouds (the original has none)
    // (drifting slowly with the wind, each at its own pace: a turn of the sky in some 20 minutes)
    float drift = ((float) v->frames + v->subframe) / 60.0f * 0.005f * (0.8f + ((hsh >> 4) & 3) * 0.15f);
    float az = (i + (hsh & 255) / 255.0f * 0.7f) / 14 * 2 * PI + drift;
    float el = 0.04f + ((hsh >> 8) & 255) / 255.0f * 0.16f;
    V3 dir = vnorm(v3(cosf(az), el, sinf(az)));
    V3 cc = to_cam(cam, vadd(cam->eye, dir));
    if(cc.z < 0.1f) continue;
    float px = cam->cx + cc.x / cc.z * cam->f, py = cam->cy - cc.y / cc.z * cam->f;
    float size = (0.22f + ((hsh >> 16) & 255) / 255.0f * 0.2f) * cam->f / cc.z * (1 - el * 1.5f);
    if(px + size < 0 || px - size > w) continue;
    float low = 1 - (el - 0.04f) / 0.16f;
    uint32_t tint = with_alpha(mix(0xFFFFFFFF, horizon, low * 0.5f), (int) (235 - low * 60));
    screen_rect(out, px - size, py - size * 0.5f, px + size, py + size * 0.5f, 0, 1, tint, tint, (hsh >> 24) & 1 ? TEX_CLOUD2 : TEX_CLOUD);
  }
  (void) v;
}

static int cmp_item(const void* a, const void* b) {
  float x = ((const Item*) a)->depth, y = ((const Item*) b)->depth;
  return x < y ? 1 : x > y ? -1 : 0;
}

// door: screen-door transparency (TgV3Batch.door), 0 for whole.
static void list_push_door(TgV3List* l, const TgV3Vertex* vs, int n, int pic, int door) {
  l->haze = g_haze;  // (the shaded materials' haze: this picture's)
  if(l->nv + n > l->capv) {
    if(l->nv + n > 4000000) return;  // far more than any picture needs: something ran away
    int cap = (l->nv + n) * 2;
    TgV3Vertex* nv = realloc(l->v, sizeof(TgV3Vertex) * (size_t) cap);
    if(nv == NULL) return;
    l->v = nv;
    l->capv = cap;
  }
  memcpy(&l->v[l->nv], vs, sizeof(TgV3Vertex) * (size_t) n);
  if(l->overlay)  // the HUD: over everything
    for(int k = 0; k < n; k++)
      if(l->v[l->nv + k].w == 0) l->v[l->nv + k].w = -1;
  // Made textures and plain colours: into the atlas (half a pixel in from each picture's
  // edge, so filtering never reaches its neighbour).
  if(g_atlas && (pic < 0 || (pic >= TEX_GRAIN && pic <= TEX_CLOUD2))) {
    float x0, y0, sw, sh;
    if(pic < 0) {
      x0 = y0 = ATLAS_WHITE_X + 4, sw = sh = 8;
    } else {
      size_t i = 0;
      while(kAtlas[i].pic != pic) i++;
      x0 = kAtlas[i].x + 0.5f, y0 = kAtlas[i].y + 0.5f, sw = g_atlas_w[pic] - 1.0f, sh = g_atlas_h[pic] - 1.0f;
    }
    for(int k = 0; k < n; k++) {
      TgV3Vertex* t = &l->v[l->nv + k];
      float u = pic < 0 ? 0.5f : t->u, vv = pic < 0 ? 0.5f : t->v;
      t->u = (x0 + u * sw) / ATLAS_W;
      t->v = (y0 + vv * sh) / ATLAS_H;
    }
    pic = TEX_ATLAS;
  }
  if(l->nb > 0 && l->b[l->nb - 1].pic == pic && l->b[l->nb - 1].door == door) {
    l->b[l->nb - 1].count += n;
  } else {
    if(l->nb == l->capb) {
      int cap = l->capb ? l->capb * 2 : 256;
      TgV3Batch* nb = realloc(l->b, sizeof(TgV3Batch) * (size_t) cap);
      if(nb == NULL) return;
      l->b = nb;
      l->capb = cap;
    }
    l->b[l->nb++] = (TgV3Batch) {pic, l->nv, n, door};
  }
  l->nv += n;
}

static void list_push(TgV3List* l, const TgV3Vertex* vs, int n, int pic) { list_push_door(l, vs, n, pic, 0); }

static void screen_rect(TgV3List* l, float x0, float y0, float x1, float y1, float u0, float u1, uint32_t top, uint32_t bottom, int pic) {
  TgV3Vertex q[6] = {{x0, y0, u0, 0, top}, {x1, y0, u1, 0, top}, {x1, y1, u1, 1, bottom},
                     {x0, y0, u0, 0, top}, {x1, y1, u1, 1, bottom}, {x0, y1, u0, 1, bottom}};
  list_push(l, q, 6, pic);
}

void tg_v3_list_clear(TgV3List* l) {
  l->nv = 0;
  l->nb = 0;
  l->overlay = false;
}

void tg_v3_list_free(TgV3List* l) {
  free(l->v);
  free(l->b);
  memset(l, 0, sizeof(*l));
}

size_t tg_v3_list_upload_bytes(const TgView3D* v, const TgV3List* l, uint32_t* seen) {
  size_t bytes = 0;
  for(int b = 0; b < l->nb; b++) {
    int i = l->b[b].pic;
    if(i < 0 || i >= PIC_COUNT || !v->pics[i].w || !v->pics[i].px) continue;
    uint32_t key = v->pics[i].version + 1;
    if(seen[i] == key) continue;
    // (only the rows of the last change, when that is all that is new: as gl3d sends it)
    bool part = seen[i] && seen[i] == v->pics[i].dirty_from + 1 && v->pics[i].dirty_from + 1 == v->pics[i].version;
    seen[i] = key;
    bytes += (size_t) v->pics[i].w * (part ? v->pics[i].dirty_y1 - v->pics[i].dirty_y0 : v->pics[i].h) * 4;
    for(int k = 1, lw = v->pics[i].w, lh = v->pics[i].h; !part && k <= v->pics[i].mips; k++) {  // (and its mip levels)
      lw = lw > 1 ? lw / 2 : 1, lh = lh > 1 ? lh / 2 : 1;
      bytes += (size_t) lw * lh * 4;
    }
  }
  return bytes;
}

void tg_view3d_draw(TgView3D* v, int w, int h, TgV3Look look, TgV3Camera camMode, TgV3List* out) {
  tg_view3d_draw_view(v, 0, w, h, look, camMode, out);
}

int tg_view3d_views(const TgView3D* v) { return v->two || (v->demo && v->cur.view_car[1] >= 0) ? 2 : 1; }

static void modern_hud(const TgView3D* v, int pl, TgV3List* l, int w, int h);
static void key_hints(TgV3List* out, int w, int h, float s, const char* const (*hints)[2], int n);
static void course_name(int course, char* name, char* country, size_t n);
static void cpanel(TgV3List* l, float x0, float y0, float x1, float y1, float cut, uint32_t top, uint32_t bot, uint32_t edge, float s);

// The skyline redrawn: its outline and colours per column, from the learned panorama.
static void sky_profile(TgView3D* v) {
  const TgV3Pic* p = &v->pics[SKY_PIC];
  if(!p->w || v->sky_prof_ver == p->version + 1) return;
  v->sky_prof_ver = p->version + 1;
  for(int x = 0; x < SKY_W; x++) {
    bool seen = false;
    for(int y = 0; y < SKY_H && !seen; y++) seen = v->sky_px[y * SKY_W + x] != 0;
    v->sky_known[x] = seen;
  }
  for(int x = 0; x < SKY_W && x < p->w; x++) {
    int top = p->h;
    for(int y = 0; y < p->h; y++)
      if(p->px[y * p->w + x] >> 24) {
        top = y;
        break;
      }
    v->sky_prof[x] = (float) (p->h - top);
    // Colours: the commonest of the upper and of the lower part of the column (crisp,
    // like flat painted panels: averaging would mix windows into mud).
    Hist ha = {0}, hb = {0};
    int mid = top + (p->h - top) / 2;
    for(int y = top; y < p->h; y++) {
      uint32_t c = p->px[y * p->w + x];
      if(c >> 24) hist_add(y < mid ? &ha : &hb, c | 0xFF000000u, 1);
    }
    v->sky_ctop[x] = hist_top(&ha, 0, 0xFF808080);
    v->sky_cbot[x] = hist_top(&hb, 0, v->sky_ctop[x]);
  }
  // Buildings and hills as whole panels: neighbouring columns of about the same height are
  // one shape, coloured as one (the commonest colours over all of it), so a skyline reads as
  // clean blocks rather than stripes.
  for(int x0 = 0; x0 < SKY_W;) {
    int x1 = x0 + 1;
    while(x1 < SKY_W && x1 - x0 < 48 && fabsf(v->sky_prof[x1] - v->sky_prof[x1 - 1]) <= 2.5f &&
          (v->sky_prof[x1] > 0.5f) == (v->sky_prof[x0] > 0.5f))
      x1++;
    if(v->sky_prof[x0] > 0.5f) {
      Hist ha = {0}, hb = {0};
      for(int x = x0; x < x1; x++) {
        int top = p->h - (int) v->sky_prof[x], mid = top + (p->h - top) / 2;
        for(int y = top; y < p->h; y++) {
          uint32_t c = p->px[y * p->w + x];
          if(c >> 24) hist_add(y < mid ? &ha : &hb, c | 0xFF000000u, 1);
        }
      }
      uint32_t ct = hist_top(&ha, 0, 0xFF808080), cb = hist_top(&hb, 0, ct);
      for(int x = x0; x < x1; x++) v->sky_ctop[x] = ct, v->sky_cbot[x] = cb;
    }
    x0 = x1;
  }
}

// A country's landmark on its skyline, drawn as a flat silhouette in the haze (the concept
// art's Eiffel Tower): at panorama column col, base on the horizon, hgt pixels tall.
static void sbar(TgV3List* l, float x0, float y0, float x1, float y1, float t, uint32_t c) {
  float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
  if(len < 0.01f) return;
  float nx = -dy / len * t / 2, ny = dx / len * t / 2;
  TgV3Vertex q[6] = {{x0 + nx, y0 + ny, 0, 0, c}, {x1 + nx, y1 + ny, 0, 0, c}, {x1 - nx, y1 - ny, 0, 0, c},
                     {x0 + nx, y0 + ny, 0, 0, c}, {x1 - nx, y1 - ny, 0, 0, c}, {x0 - nx, y0 - ny, 0, 0, c}};
  list_push(l, q, 6, -1);
}

// Each city's own landmark (QA-007: it was one per country, so Los Angeles showed the Las
// Vegas water tower). Found by the course's name as the game spells it (course_name), so
// cities with nothing famous on their horizon get none.
enum {
  LM_NONE, LM_WATER_TOWER, LM_HILL_SIGN, LM_EMPIRE, LM_GOLDEN_GATE, LM_CHRIST, LM_PEAK, LM_PYRAMID, LM_TOKYO_TOWER,
  LM_PAGODA, LM_ONION_TOWERS, LM_SPIRES, LM_HIGH_RISES, LM_EIFFEL, LM_PISA, LM_COLOSSEUM, LM_VOLCANO, LM_DUOMO,
  LM_BIG_BEN, LM_STONEHENGE, LM_STRIP, LM_BAY_BRIDGE, LM_TORII_SEA, LM_CITY_HALL, LM_SPIRAL, LM_DOMES, LM_SKI_JUMP,
  LM_HOTEL_DOME, LM_CHATEAU, LM_ROCK, LM_WORKS, LM_CASTLE, LM_KAPOK, LM_CHURCH
};
// The rest by course number (docs/retro/CITIES.md), so every city has its sign on the horizon.
static const struct { int course, kind; } kCourseLandmark[] = {
  {0, LM_STRIP}, {7, LM_KAPOK}, {9, LM_TORII_SEA}, {10, LM_BAY_BRIDGE}, {14, LM_CHURCH}, {16, LM_CITY_HALL},
  {17, LM_SPIRAL}, {18, LM_DOMES}, {19, LM_SKI_JUMP}, {21, LM_HOTEL_DOME}, {22, LM_CHATEAU}, {23, LM_ROCK},
  {29, LM_WORKS}, {30, LM_CASTLE},
};
static const struct { const char* city; int kind; } kCityLandmark[] = {
  {"LAS VEGAS", LM_WATER_TOWER}, {"LOS ANGELES", LM_HILL_SIGN}, {"NEW YORK", LM_EMPIRE},
  {"SAN FRANCISCO", LM_GOLDEN_GATE}, {"RIO", LM_CHRIST}, {"MACHU PICCHU", LM_PEAK},
  {"CHICHEN ITZA", LM_PYRAMID}, {"TOKYO", LM_TOKYO_TOWER}, {"KYOTO", LM_PAGODA},
  {"MUNICH", LM_ONION_TOWERS}, {"COLOGNE", LM_SPIRES}, {"FRANKFURT", LM_HIGH_RISES},
  {"PARIS", LM_EIFFEL}, {"PISA", LM_PISA}, {"ROME", LM_COLOSSEUM}, {"SICILY", LM_VOLCANO},
  {"FLORENCE", LM_DUOMO}, {"LONDON", LM_BIG_BEN}, {"STONEHENGE", LM_STONEHENGE},
};

static int city_landmark(int course) {
  static int cached = -1, kind = LM_NONE;
  if(course == cached) return kind;
  char name[40], country[40];
  course_name(course, name, country, sizeof(name));
  cached = course, kind = LM_NONE;
  for(size_t i = 0; i < sizeof(kCityLandmark) / sizeof(kCityLandmark[0]); i++)
    if(strcmp(name, kCityLandmark[i].city) == 0) kind = kCityLandmark[i].kind;
  for(size_t i = 0; i < sizeof(kCourseLandmark) / sizeof(kCourseLandmark[0]); i++)
    if((course & 31) == kCourseLandmark[i].course) kind = kCourseLandmark[i].kind;
  if(getenv("TG_LANDMARK")) kind = atoi(getenv("TG_LANDMARK"));  // (look development: this landmark on any course, e.g. at night)
  return kind;
}

// The key part of each landmark, kept in sight (QA-007): its left and right and the height it
// starts at, in units of its height H from its foot (see landmark() for the shapes).
static void landmark_box(int kind, float* x0, float* x1, float* y0) {
  *x0 = -0.5f, *x1 = 0.5f, *y0 = 0.3f;
  switch(kind) {
    case LM_HILL_SIGN: *x0 = -0.62f, *x1 = 0.46f, *y0 = 0.15f; break;  // the letters
    case LM_GOLDEN_GATE: *x0 = -0.62f, *x1 = 0.62f, *y0 = 0.3f; break;  // the towers
    case LM_CHRIST: case LM_PISA: case LM_EIFFEL: case LM_TOKYO_TOWER: case LM_WATER_TOWER: *x0 = -0.25f, *x1 = 0.25f, *y0 = 0.35f; break;
    case LM_EMPIRE: *x0 = -0.2f, *x1 = 0.2f, *y0 = 0.4f; break;
    case LM_BIG_BEN: *x0 = -0.12f, *x1 = 1.25f, *y0 = 0.3f; break;
    case LM_STRIP: *x0 = -0.75f, *x1 = 1.05f, *y0 = 0.35f; break;
    case LM_BAY_BRIDGE: *x0 = -1.1f, *x1 = 1.5f, *y0 = 0.35f; break;
    case LM_TORII_SEA: *x0 = -0.48f, *x1 = 1.25f, *y0 = 0.3f; break;
    case LM_CITY_HALL: case LM_SPIRAL: *x0 = 0.05f, *x1 = 0.65f, *y0 = 0.4f; break;
    case LM_HOTEL_DOME: *x0 = 0.6f, *x1 = 1.0f, *y0 = 0.3f; break;
    case LM_ROCK: *x0 = -0.85f, *x1 = 0.95f, *y0 = 0.35f; break;
    case LM_WORKS: *x0 = -0.85f, *x1 = 0.95f, *y0 = 0.35f; break;
    case LM_CASTLE: *x0 = -0.4f, *x1 = 1.2f, *y0 = 0.12f; break;
    case LM_KAPOK: *x0 = -0.35f, *x1 = 0.35f, *y0 = 0.55f; break;  // the middle giant (the forest is dense)
    case LM_STONEHENGE: *x0 = -0.62f, *x1 = 0.62f, *y0 = 0.1f; break;
    case LM_CHATEAU: *x0 = -0.55f, *x1 = 0.55f, *y0 = 0.25f; break;
    case LM_COLOSSEUM: *x0 = -0.75f, *x1 = 0.75f, *y0 = 0.2f; break;
    case LM_VOLCANO: *x0 = -0.4f, *x1 = 0.7f, *y0 = 0.4f; break;
    case LM_PEAK: *x0 = -0.4f, *x1 = 0.4f, *y0 = 0.5f; break;
    case LM_SKI_JUMP: *x0 = -0.4f, *x1 = 0.75f, *y0 = 0.5f; break;
    case LM_CHURCH: *x0 = -0.15f, *x1 = 0.15f, *y0 = 0.4f; break;
    default: break;
  }
}

static void lm_quad(TgV3List* l, float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, uint32_t c) {
  TgV3Vertex q[6] = {{x0, y0, 0, 0, c}, {x1, y1, 0, 0, c}, {x2, y2, 0, 0, c}, {x0, y0, 0, 0, c}, {x2, y2, 0, 0, c}, {x3, y3, 0, 0, c}};
  list_push(l, q, 6, -1);
}
static void lm_rect(TgV3List* l, float x0, float y0, float x1, float y1, uint32_t c) { lm_quad(l, x0, y0, x1, y0, x1, y1, x0, y1, c); }
static void lm_tri(TgV3List* l, float x0, float y0, float x1, float y1, float x2, float y2, uint32_t c) {
  TgV3Vertex q[3] = {{x0, y0, 0, 0, c}, {x1, y1, 0, 0, c}, {x2, y2, 0, 0, c}};
  list_push(l, q, 3, -1);
}
// A dome: half an ellipse on (x, y), rx wide, ry tall.
static void lm_dome(TgV3List* l, float x, float y, float rx, float ry, uint32_t c) {
  for(int k = 0; k < 8; k++) {
    float a0 = PI * k / 8, a1 = PI * (k + 1) / 8;
    lm_tri(l, x, y, x + cosf(a0) * rx, y - sinf(a0) * ry, x + cosf(a1) * rx, y - sinf(a1) * ry, c);
  }
}

// The landmarks modelled in 3D (tools/make_landmarks.py: Christ the Redeemer on Corcovado,
// Sugarloaf). A far landmark has one fixed direction (lm_place), so like the sun it is only ever
// seen from one side: each is modelled as a real solid and baked for that side - per corner its
// place in units of H, its normal in the view's frame, its colour, material and haze, the
// triangles that show in a painter's order. Here it is lit with the scene's own light (kLight,
// turned into the view's frame for its direction: the props' and cars' sun), hazed as far
// land, and drawn in the skyline's layer as background (the far layer of a "3D skybox").
#include "landmark_meshes.h"
enum { LM_MAT_GROUND, LM_MAT_STONE, LM_MAT_BUILT, LM_MAT_CABLE, LM_MAT_LAMP };

// One corner's colour. By day as the stage props are lit (lit(): ~0.8 in shade to ~1.1 in the
// sun) with a sky/ground hemisphere for the form, the sun wrapped a little round organic
// shapes and a rim where it grazes an edge; by night the statue and its pedestal floodlit from
// the terraces, the mountains dark with the city's glow at their foot. Then the distance haze:
// towards the mist at the foot, the sky's horizon colour higher up (tools/make_landmarks.py's
// lm_light_py is the same lines, for its previews).
static uint32_t lm_light(const LmVert* p, V3 L, bool night, uint32_t haze, uint32_t horizon) {
  float a[3] = {(p->albedo >> 16 & 255) / 255.0f, (p->albedo >> 8 & 255) / 255.0f, (p->albedo & 255) / 255.0f};
  float nx = p->nx / 127.0f, ny = p->ny / 127.0f, nz = p->nz / 127.0f, y = p->y / 8192.0f;
  float ndl = nx * L.x + ny * L.y + nz * L.z, c[3];
  bool flood = p->mat == LM_MAT_STONE || p->mat == LM_MAT_BUILT || p->mat == LM_MAT_LAMP;
  if(p->mat == LM_MAT_CABLE) {
    for(int k = 0; k < 3; k++) c[k] = a[k] * (night ? 0.35f : 1.0f);
  } else if(p->mat == LM_MAT_LAMP && night) {  // a lit window, a clock face
    c[0] = 1.0f, c[1] = 0.82f, c[2] = 0.5f;
  } else if(!night) {
    static const float lo[3] = {0.62f, 0.62f, 0.56f}, hi[3] = {0.70f, 0.76f, 0.86f}, sun[3] = {1.0f, 0.95f, 0.86f};
    float wrap = p->mat == LM_MAT_GROUND ? 0.3f : 0.15f, dif = fmaxf(0, (ndl + wrap) / (1 + wrap));
    float hemi = 0.5f + 0.5f * ny, fr = 0.86f + 0.14f * fmaxf(0, nz), e = 1 - fmaxf(0, nz);
    float rim = e * e * e * fmaxf(0, ndl + 0.25f) * (p->mat == LM_MAT_GROUND ? 0.1f : 0.3f) * (0.3f + 0.7f * fmaxf(0, -L.z));
    float ks = p->mat == LM_MAT_GROUND ? 0.42f : 0.6f;  // (the sun stronger on the pale stone)
    for(int k = 0; k < 3; k++) c[k] = a[k] * ((lo[k] + (hi[k] - lo[k]) * hemi) * fr + sun[k] * dif * ks) + rim * sun[k];
  } else if(flood) {
    static const float amb[3] = {0.10f, 0.12f, 0.2f}, fl[3] = {1.0f, 0.97f, 0.9f};
    float f1 = fmaxf(0, -0.3f * nx - 0.25f * ny + 0.92f * nz), f2 = fmaxf(0, 0.45f * nx - 0.2f * ny + 0.87f * nz);
    for(int k = 0; k < 3; k++) c[k] = a[k] * (amb[k] + fl[k] * (0.62f * f1 + 0.5f * f2));
  } else {
    static const float amb[3] = {0.10f, 0.12f, 0.2f}, gl[3] = {0.14f, 0.08f, 0.2f};
    float moon = fmaxf(0, 0.4f * nx + 0.8f * ny + 0.45f * nz), glow = fmaxf(0, 1 - y / 0.45f);
    for(int k = 0; k < 3; k++) c[k] = a[k] * amb[k] * (0.4f + 0.8f * moon) + gl[k] * glow * glow * 0.5f;
  }
  float kf = p->fog / 255.0f * (night && flood ? 0.45f : 1.0f), t = smooth01(0.05f, 0.4f, y);
  uint32_t out = 0xFF000000u;
  for(int k = 0, sh = 16; k < 3; k++, sh -= 8) {
    float h0 = (haze >> sh & 255) / 255.0f, h1 = (horizon >> sh & 255) / 255.0f, f = h0 + (h1 - h0) * t;
    float v = c[k] * (1 - kf) + f * kf;
    out |= (uint32_t) (sat01(v) * 255 + 0.5f) << sh;
  }
  return out;
}

// A baked landmark with its foot at (x, y) on the screen, H pixels to the unit, seen in the
// direction az (radians, as g_lm_az), alpha its fade.
static void lm_mesh(TgV3List* l, const LmMesh* m, float x, float y, float H, float az, int alpha) {
  static TgV3Vertex* buf;
  static TgV3Vertex* corner;
  static int capb, capc;
  if(m->nt * 3 > capb) {
    TgV3Vertex* nb = realloc(buf, sizeof(TgV3Vertex) * (size_t) m->nt * 3);
    if(!nb) return;
    buf = nb, capb = m->nt * 3;
  }
  if(m->nv > capc) {
    TgV3Vertex* nc = realloc(corner, sizeof(TgV3Vertex) * (size_t) m->nv);
    if(!nc) return;
    corner = nc, capc = m->nv;
  }
  // the scene's light in the view's frame: x to the right of az, y up, z back to the viewer
  V3 Lw = vnorm(kLight);
  float sa = sinf(az), ca = cosf(az), k = H / 8192.0f;
  V3 L = v3(-Lw.x * sa + Lw.z * ca, Lw.y, -(Lw.x * ca + Lw.z * sa));
  for(int i = 0; i < m->nv; i++) {
    const LmVert* p = &m->v[i];
    corner[i] = (TgV3Vertex) {x + p->x * k, y - p->y * k, 0, 0, with_alpha(lm_light(p, L, g_night, g_haze, g_horizon), alpha)};
  }
  for(int i = 0; i < m->nt * 3; i++) buf[i] = corner[m->t[i]];
  list_push(l, buf, m->nt * 3, -1);
}

// The landmarks with a model (landmark_meshes.h); the others are drawn flat (landmark() below).
static const LmMesh* lm_model(int kind) {
  switch(kind) {
    case LM_CHRIST: return &kLmCristo;
    case LM_EIFFEL: return &kLmEiffel;
    case LM_TOKYO_TOWER: return &kLmTokyoTower;
    case LM_PISA: return &kLmPisa;
    default: return NULL;
  }
}

static void landmark(TgV3List* l, int kind, float x, float y, float H, uint32_t c) {
  float t = H * 0.022f + 1;
  uint32_t lite = (c & 0xFF000000u) | (mix(c, 0xFFE8EEF4, 0.35f) & 0xFFFFFF);  // openings, letters: paler
  switch(kind) {
    case LM_HILL_SIGN: {  // a ridge over LA, white letters on its slope (a 3 x 5 block font): ROLLYWOOD, our own sign
      lm_quad(l, x - H * 1.4f, y, x - H * 0.75f, y - H * 0.34f, x + H * 0.1f, y - H * 0.46f, x + H * 1.5f, y, c);
      lm_tri(l, x + H * 0.1f, y - H * 0.46f, x + H * 0.55f, y - H * 0.36f, x + H * 1.5f, y, c);
      lm_tri(l, x - H * 1.4f, y, x - H * 1.0f, y - H * 0.27f, x - H * 0.75f, y - H * 0.34f, c);
      uint32_t white = (c & 0xFF000000u) | (mix(c, 0xFFF6F7F8, 0.82f) & 0xFFFFFF);
      static const char* kWord = "ROLLYWOOD";
      static const uint8_t kGlyph[][5] = {  // rows top to bottom, 3 columns (W: 5), bit 0 = right column
        ['R' - 'A'] = {6, 5, 6, 5, 5}, ['O' - 'A'] = {7, 5, 5, 5, 7}, ['L' - 'A'] = {4, 4, 4, 4, 7},
        ['Y' - 'A'] = {5, 5, 2, 2, 2}, ['W' - 'A'] = {17, 17, 21, 21, 10}, ['D' - 'A'] = {6, 5, 5, 5, 6},
      };
      float px = H * 0.024f, lx = x - H * 0.6f;  // a font pixel; the first letter's left
      for(int k = 0; kWord[k]; k++) {
        const uint8_t* gl = kGlyph[kWord[k] - 'A'];
        int gw = kWord[k] == 'W' ? 5 : 3;
        float ly = y - H * 0.16f - (lx - x + H * 0.6f) * 0.07f;  // up the slope
        for(int r = 0; r < 5; r++)
          for(int c0 = 0; c0 < gw;) {
            if(!(gl[r] >> (gw - 1 - c0) & 1)) {
              c0++;
              continue;
            }
            int c1 = c0;
            while(c1 < gw && (gl[r] >> (gw - 1 - c1) & 1)) c1++;
            lm_rect(l, lx + c0 * px, ly - (5 - r) * px, lx + c1 * px, ly - (4 - r) * px, white);
            c0 = c1;
          }
        lx += (gw + 1.6f) * px;
      }
      break;
    }
    case LM_EMPIRE: {  // a stepped tower and its spire among lower blocks
      lm_rect(l, x - H * 0.5f, y - H * 0.38f, x - H * 0.24f, y, c);
      lm_rect(l, x + H * 0.2f, y - H * 0.48f, x + H * 0.42f, y, c);
      lm_rect(l, x + H * 0.42f, y - H * 0.3f, x + H * 0.6f, y, c);
      lm_rect(l, x - H * 0.14f, y - H * 0.55f, x + H * 0.14f, y, c);
      lm_rect(l, x - H * 0.1f, y - H * 0.7f, x + H * 0.1f, y - H * 0.54f, c);
      lm_rect(l, x - H * 0.06f, y - H * 0.8f, x + H * 0.06f, y - H * 0.69f, c);
      lm_tri(l, x - H * 0.04f, y - H * 0.8f, x + H * 0.04f, y - H * 0.8f, x, y - H * 0.88f, c);
      sbar(l, x, y - H * 0.86f, x, y - H, t * 0.6f, c);
      break;
    }
    case LM_GOLDEN_GATE: {  // two towers, the deck, the cables
      uint32_t red = (c & 0xFF000000u) | (mix(0xFFC4452E, c, 0.5f) & 0xFFFFFF);
      float deck = y - H * 0.22f, top = y - H * 0.9f, tx = H * 0.55f;
      sbar(l, x - H * 1.6f, deck, x + H * 1.6f, deck, t * 1.4f, red);
      for(int s2 = -1; s2 <= 1; s2 += 2) {
        float cx = x + s2 * tx;
        lm_rect(l, cx - H * 0.045f, top, cx - H * 0.015f, y, red);
        lm_rect(l, cx + H * 0.015f, top, cx + H * 0.045f, y, red);
        for(int k = 0; k < 3; k++) lm_rect(l, cx - H * 0.045f, top + H * (0.04f + k * 0.2f), cx + H * 0.045f, top + H * (0.07f + k * 0.2f), red);
        sbar(l, cx, top, cx + s2 * H * 1.0f, deck, t * 0.6f, red);  // the side span's cable
      }
      float px = x - tx, py = top;
      for(int k = 1; k <= 10; k++) {  // the main cable's sag between the towers
        float u = k / 10.0f, nx = x - tx + 2 * tx * u, ny = top + (deck - H * 0.05f - top) * 4 * u * (1 - u);
        sbar(l, px, py, nx, ny, t * 0.6f, red);
        px = nx, py = ny;
      }
      break;
    }
    case LM_CHRIST: {  // the steep hill and the statue, arms open
      lm_quad(l, x - H * 0.75f, y, x - H * 0.12f, y - H * 0.6f, x + H * 0.08f, y - H * 0.62f, x + H * 0.9f, y, c);
      lm_rect(l, x - H * 0.025f, y - H * 0.95f, x + H * 0.025f, y - H * 0.6f, c);
      sbar(l, x - H * 0.16f, y - H * 0.86f, x + H * 0.16f, y - H * 0.86f, t * 1.2f, c);
      lm_dome(l, x, y - H * 0.95f, H * 0.03f, H * 0.04f, c);
      break;
    }
    case LM_PEAK: {  // the sharp peak over the ruins
      lm_quad(l, x - H * 0.9f, y, x - H * 0.25f, y - H * 0.55f, x + H * 0.05f, y - H, x + H * 0.6f, y, c);
      lm_tri(l, x + H * 0.3f, y, x + H * 0.75f, y - H * 0.45f, x + H * 1.2f, y, c);
      break;
    }
    case LM_PYRAMID: {  // the stepped pyramid and the temple on top
      for(int k = 0; k < 6; k++) {
        float hw = H * (0.6f - k * 0.075f);
        lm_rect(l, x - hw, y - H * 0.1f * (k + 1), x + hw, y - H * 0.1f * k, c);
      }
      lm_rect(l, x - H * 0.06f, y - H * 0.55f, x + H * 0.06f, y, lite);  // the stair
      lm_rect(l, x - H * 0.14f, y - H * 0.74f, x + H * 0.14f, y - H * 0.6f, c);
      break;
    }
    case LM_PAGODA: {  // five roofs, each smaller, and the finial
      for(int k = 0; k < 5; k++) {
        float y0 = y - H * 0.15f * k, hw = H * (0.3f - k * 0.04f);
        lm_rect(l, x - hw * 0.6f, y0 - H * 0.1f, x + hw * 0.6f, y0, c);
        lm_quad(l, x - hw * 1.15f, y0 - H * 0.09f, x - hw * 0.5f, y0 - H * 0.15f, x + hw * 0.5f, y0 - H * 0.15f, x + hw * 1.15f, y0 - H * 0.09f, c);
      }
      sbar(l, x, y - H * 0.75f, x, y - H, t * 0.8f, c);
      break;
    }
    case LM_ONION_TOWERS: {  // the cathedral's two towers with their round caps
      lm_rect(l, x - H * 0.32f, y - H * 0.38f, x + H * 0.32f, y, c);
      lm_tri(l, x - H * 0.32f, y - H * 0.38f, x + H * 0.32f, y - H * 0.38f, x, y - H * 0.55f, c);
      for(int s2 = -1; s2 <= 1; s2 += 2) {
        float cx = x + s2 * H * 0.2f;
        lm_rect(l, cx - H * 0.08f, y - H * 0.78f, cx + H * 0.08f, y, c);
        lm_dome(l, cx, y - H * 0.78f, H * 0.08f, H * 0.14f, c);
        sbar(l, cx, y - H * 0.9f, cx, y - H * 0.98f, t * 0.5f, c);
      }
      break;
    }
    case LM_SPIRES: {  // the cathedral's two tall spires
      lm_rect(l, x - H * 0.45f, y - H * 0.35f, x + H * 0.45f, y, c);
      for(int s2 = -1; s2 <= 1; s2 += 2) {
        float cx = x + s2 * H * 0.13f;
        lm_rect(l, cx - H * 0.09f, y - H * 0.62f, cx + H * 0.09f, y, c);
        lm_tri(l, cx - H * 0.08f, y - H * 0.62f, cx + H * 0.08f, y - H * 0.62f, cx, y - H, c);
      }
      break;
    }
    case LM_HIGH_RISES: {  // a cluster of towers, one with a pointed top
      static const float kT[6][3] = {{-0.6f, 0.12f, 0.5f}, {-0.36f, 0.1f, 0.78f}, {-0.12f, 0.12f, 0.62f},
                                     {0.12f, 0.1f, 0.92f}, {0.36f, 0.12f, 0.7f}, {0.58f, 0.1f, 0.48f}};
      for(int k = 0; k < 6; k++) lm_rect(l, x + H * kT[k][0], y - H * kT[k][2], x + H * (kT[k][0] + kT[k][1] * 1.6f), y, c);
      lm_tri(l, x + H * 0.12f, y - H * 0.92f, x + H * 0.28f, y - H * 0.92f, x + H * 0.2f, y - H, c);
      break;
    }
    case LM_EIFFEL: {  // the Eiffel Tower
      float w = H * 0.42f;
      sbar(l, x - w / 2, y, x - w * 0.06f, y - H * 0.62f, t * 1.6f, c);
      sbar(l, x + w / 2, y, x + w * 0.06f, y - H * 0.62f, t * 1.6f, c);
      sbar(l, x - w * 0.06f, y - H * 0.62f, x - 1, y - H * 0.97f, t, c);
      sbar(l, x + w * 0.06f, y - H * 0.62f, x + 1, y - H * 0.97f, t, c);
      sbar(l, x, y - H * 0.97f, x, y - H, t * 0.6f, c);
      sbar(l, x - w * 0.36f, y - H * 0.2f, x + w * 0.36f, y - H * 0.2f, t * 2, c);
      sbar(l, x - w * 0.16f, y - H * 0.46f, x + w * 0.16f, y - H * 0.46f, t * 1.6f, c);
      for(int k = 0; k < 7; k++) {  // the lattice
        float a = H * (0.2f + k * 0.06f), b = a + H * 0.06f, wa = w / 2 * (1 - a / H / 0.66f), wb = w / 2 * (1 - b / H / 0.66f);
        if(wb < 0) break;
        sbar(l, x - wa, y - a, x + wb, y - b, t * 0.5f, c);
        sbar(l, x + wa, y - a, x - wb, y - b, t * 0.5f, c);
      }
      break;
    }
    case LM_PISA: {  // the leaning tower
      float w = H * 0.24f, lean = 0.09f;
      for(int k = 0; k < 7; k++) {
        float y0 = y - H * k / 7, y1 = y - H * (k + 1) / 7 + 1, o0 = lean * H * k / 7, o1 = lean * H * (k + 1) / 7;
        float ww = k == 6 ? w * 0.7f : w;
        TgV3Vertex q[6] = {{x - ww / 2 + o0, y0, 0, 0, c}, {x + ww / 2 + o0, y0, 0, 0, c}, {x + ww / 2 + o1, y1, 0, 0, c},
                           {x - ww / 2 + o0, y0, 0, 0, c}, {x + ww / 2 + o1, y1, 0, 0, c}, {x - ww / 2 + o1, y1, 0, 0, c}};
        list_push(l, q, 6, -1);
        sbar(l, x - ww * 0.62f + o1, y1 + 1, x + ww * 0.62f + o1, y1 + 1, t * 0.8f, c);
      }
      break;
    }
    case LM_COLOSSEUM: {  // the arena's ring, broken down at one end, rows of arches
      lm_quad(l, x - H * 0.75f, y, x - H * 0.75f, y - H * 0.42f, x + H * 0.35f, y - H * 0.42f, x + H * 0.75f, y, c);
      for(int r = 0; r < 3; r++)
        for(int k = 0; k < 10; k++) {
          float ax = x - H * 0.7f + k * H * 0.13f, ay = y - H * (0.06f + r * 0.13f);
          float roof = y - H * 0.42f + (ax > x + H * 0.35f ? (ax - x - H * 0.35f) / (H * 0.4f) * H * 0.42f : 0);
          if(ay - H * 0.08f < roof + H * 0.02f) continue;
          lm_rect(l, ax, ay - H * 0.08f, ax + H * 0.06f, ay, lite);
        }
      break;
    }
    case LM_VOLCANO: {  // Etna: a wide cone and its plume
      lm_quad(l, x - H * 1.4f, y, x - H * 0.16f, y - H * 0.55f, x + H * 0.16f, y - H * 0.55f, x + H * 1.4f, y, c);
      uint32_t smoke = (uint32_t) ((c >> 24) * 0.45f) << 24 | (mix(c, 0xFFE0E4E8, 0.5f) & 0xFFFFFF);
      sbar(l, x, y - H * 0.6f, x + H * 0.25f, y - H * 0.85f, H * 0.12f, smoke);
      sbar(l, x + H * 0.25f, y - H * 0.85f, x + H * 0.7f, y - H * 0.95f, H * 0.16f, smoke);
      break;
    }
    case LM_DUOMO: {  // the dome on its drum and the bell tower beside it
      lm_rect(l, x - H * 0.55f, y - H * 0.3f, x + H * 0.25f, y, c);
      lm_rect(l, x - H * 0.2f, y - H * 0.42f, x + H * 0.2f, y - H * 0.28f, c);
      lm_dome(l, x, y - H * 0.42f, H * 0.2f, H * 0.3f, c);
      lm_rect(l, x - H * 0.03f, y - H * 0.8f, x + H * 0.03f, y - H * 0.7f, c);
      lm_rect(l, x + H * 0.35f, y - H * 0.75f, x + H * 0.47f, y, c);
      break;
    }
    case LM_STONEHENGE: {  // the stone ring: uprights and lintels, low on the plain
      float h = H * 0.3f;
      for(int k = 0; k < 6; k++) {
        float sx = x - H * 0.6f + k * H * 0.24f;
        lm_rect(l, sx, y - h, sx + H * 0.05f, y, c);
        lm_rect(l, sx + H * 0.11f, y - h, sx + H * 0.16f, y, c);
        if(k != 2) lm_rect(l, sx - H * 0.01f, y - h - H * 0.05f, sx + H * 0.17f, y - h, c);
      }
      break;
    }
    case LM_TOKYO_TOWER: {  // a red lattice tower
      uint32_t red = mix(0xFFD8402C, c, 0.45f);
      float w = H * 0.34f;
      sbar(l, x - w / 2, y, x - 1, y - H * 0.92f, t * 1.4f, red);
      sbar(l, x + w / 2, y, x + 1, y - H * 0.92f, t * 1.4f, red);
      sbar(l, x, y - H * 0.92f, x, y - H, t * 0.6f, red);
      for(int k = 0; k < 4; k++) {
        float a = H * (0.18f + k * 0.18f), wa = w / 2 * (1 - a / H / 0.92f);
        sbar(l, x - wa, y - a, x + wa, y - a, t * (k == 1 ? 3 : 1.2f), k == 1 ? c : red);
      }
      break;
    }
    case LM_BIG_BEN: {  // the clock tower and the Houses of Parliament beside it
      float w = H * 0.16f;
      lm_rect(l, x + w / 2, y - H * 0.24f, x + H * 1.3f, y, c);
      for(int k = 0; k < 9; k++) lm_tri(l, x + w / 2 + k * H * 0.09f, y - H * 0.24f, x + w / 2 + k * H * 0.09f + H * 0.05f, y - H * 0.24f,
                                         x + w / 2 + k * H * 0.09f + H * 0.025f, y - H * 0.31f, c);
      lm_rect(l, x + H * 1.05f, y - H * 0.5f, x + H * 1.22f, y, c);  // Victoria Tower
      lm_rect(l, x + H * 0.55f, y - H * 0.34f, x + H * 0.6f, y - H * 0.24f, c);
      TgV3Vertex q[6] = {{x - w / 2, y, 0, 0, c}, {x + w / 2, y, 0, 0, c}, {x + w / 2, y - H * 0.72f, 0, 0, c},
                         {x - w / 2, y, 0, 0, c}, {x + w / 2, y - H * 0.72f, 0, 0, c}, {x - w / 2, y - H * 0.72f, 0, 0, c}};
      list_push(l, q, 6, -1);
      TgV3Vertex r2[3] = {{x - w * 0.6f, y - H * 0.72f, 0, 0, c}, {x + w * 0.6f, y - H * 0.72f, 0, 0, c}, {x, y - H, 0, 0, c}};
      list_push(l, r2, 3, -1);
      sbar(l, x - w * 0.3f, y - H * 0.62f, x + w * 0.3f, y - H * 0.62f, H * 0.08f, mix(c, 0xFFF4E6C0, 0.45f));
      break;
    }
    case LM_WATER_TOWER: {  // a desert water tower
      float w = H * 0.3f;
      sbar(l, x - w / 2, y, x - w / 3, y - H * 0.55f, t, c);
      sbar(l, x + w / 2, y, x + w / 3, y - H * 0.55f, t, c);
      TgV3Vertex q[6] = {{x - w / 2, y - H * 0.55f, 0, 0, c}, {x + w / 2, y - H * 0.55f, 0, 0, c}, {x + w / 2, y - H * 0.85f, 0, 0, c},
                         {x - w / 2, y - H * 0.55f, 0, 0, c}, {x + w / 2, y - H * 0.85f, 0, 0, c}, {x - w / 2, y - H * 0.85f, 0, 0, c}};
      list_push(l, q, 6, -1);
      TgV3Vertex r2[3] = {{x - w * 0.55f, y - H * 0.85f, 0, 0, c}, {x + w * 0.55f, y - H * 0.85f, 0, 0, c}, {x, y - H, 0, 0, c}};
      list_push(l, r2, 3, -1);
      break;
    }
    case LM_STRIP: {  // the Strip: the Stratosphere's needle, Luxor's pyramid, casino slabs
      uint32_t dark = (c & 0xFF000000u) | (mix(c, 0xFF101418, 0.45f) & 0xFFFFFF);
      lm_tri(l, x - H * 1.1f, y, x - H * 0.35f, y, x - H * 0.72f, y - H * 0.42f, dark);   // Luxor
      static const float kSlab[5][3] = {{-0.25f, 0.12f, 0.5f}, {-0.05f, 0.1f, 0.62f}, {0.15f, 0.14f, 0.44f}, {0.42f, 0.1f, 0.56f}, {0.62f, 0.16f, 0.36f}};
      for(int k = 0; k < 5; k++) lm_rect(l, x + H * kSlab[k][0], y - H * kSlab[k][2], x + H * (kSlab[k][0] + kSlab[k][1]), y, c);
      float sx = x + H * 0.95f;  // the Stratosphere
      lm_quad(l, sx - H * 0.05f, y, sx - H * 0.02f, y - H * 0.8f, sx + H * 0.02f, y - H * 0.8f, sx + H * 0.05f, y, c);
      lm_rect(l, sx - H * 0.08f, y - H * 0.9f, sx + H * 0.08f, y - H * 0.8f, c);
      lm_dome(l, sx, y - H * 0.9f, H * 0.06f, H * 0.04f, c);
      sbar(l, sx, y - H * 0.93f, sx, y - H * 1.08f, t * 0.6f, c);
      break;
    }
    case LM_BAY_BRIDGE: {  // Yokohama: the cable-stayed Bay Bridge, the Cosmo Clock wheel, Landmark Tower
      float deck = y - H * 0.25f;
      sbar(l, x - H * 1.6f, deck, x + H * 0.4f, deck, t * 1.4f, c);
      for(int s2 = -1; s2 <= 1; s2 += 2) {
        float cx = x - H * 0.6f + s2 * H * 0.45f;
        lm_rect(l, cx - H * 0.04f, y - H * 0.75f, cx - H * 0.015f, y, c);
        lm_rect(l, cx + H * 0.015f, y - H * 0.75f, cx + H * 0.04f, y, c);
        lm_rect(l, cx - H * 0.04f, y - H * 0.62f, cx + H * 0.04f, y - H * 0.59f, c);
        for(int k = 1; k <= 4; k++) {
          sbar(l, cx, y - H * 0.72f, cx - H * 0.11f * k, deck, t * 0.4f, c);
          sbar(l, cx, y - H * 0.72f, cx + H * 0.11f * k, deck, t * 0.4f, c);
        }
      }
      float wx = x + H * 0.75f, wy = y - H * 0.42f, wr = H * 0.36f;  // the wheel
      for(int k = 0; k < 16; k++) {
        float a0 = 2 * PI * k / 16, a1 = 2 * PI * (k + 1) / 16;
        sbar(l, wx + cosf(a0) * wr, wy + sinf(a0) * wr, wx + cosf(a1) * wr, wy + sinf(a1) * wr, t * 0.9f, c);
        if(k % 2 == 0) sbar(l, wx, wy, wx + cosf(a0) * wr, wy + sinf(a0) * wr, t * 0.3f, c);
      }
      sbar(l, wx - wr * 0.5f, y, wx, wy, t, c);
      sbar(l, wx + wr * 0.5f, y, wx, wy, t, c);
      lm_quad(l, x + H * 1.25f, y, x + H * 1.29f, y - H * 0.95f, x + H * 1.43f, y - H * 0.95f, x + H * 1.47f, y, c);  // Landmark Tower
      break;
    }
    case LM_TORII_SEA: {  // Itsukushima's torii standing in the sea, and the Atomic Bomb Dome
      uint32_t ver = (c & 0xFF000000u) | (mix(0xFFE0401E, c, 0.4f) & 0xFFFFFF);
      for(int s2 = -1; s2 <= 1; s2 += 2) lm_rect(l, x + s2 * H * 0.22f - H * 0.03f, y - H * 0.55f, x + s2 * H * 0.22f + H * 0.03f, y, ver);
      lm_rect(l, x - H * 0.3f, y - H * 0.47f, x + H * 0.3f, y - H * 0.43f, ver);
      lm_quad(l, x - H * 0.42f, y - H * 0.6f, x - H * 0.38f, y - H * 0.55f, x + H * 0.38f, y - H * 0.55f, x + H * 0.42f, y - H * 0.6f, ver);
      lm_quad(l, x - H * 0.46f, y - H * 0.66f, x - H * 0.42f, y - H * 0.6f, x + H * 0.42f, y - H * 0.6f, x + H * 0.46f, y - H * 0.66f, c);
      float dx = x + H * 1.0f;  // the dome: a shell and its bare steel frame
      lm_rect(l, dx - H * 0.25f, y - H * 0.25f, dx + H * 0.25f, y, c);
      lm_rect(l, dx - H * 0.09f, y - H * 0.36f, dx + H * 0.09f, y - H * 0.25f, c);
      for(int k = 0; k <= 4; k++) {
        float a = PI * k / 4;
        sbar(l, dx + cosf(a) * H * 0.09f, y - H * 0.36f, dx + cosf(a) * H * 0.04f, y - H * 0.36f - sinf(a) * H * 0.12f - H * 0.02f, t * 0.4f, c);
      }
      break;
    }
    case LM_CITY_HALL: {  // Stockholm: the brick hall, its square tower with the three crowns
      lm_rect(l, x - H * 0.9f, y - H * 0.28f, x + H * 0.1f, y, c);
      lm_rect(l, x + H * 0.1f, y - H * 0.78f, x + H * 0.26f, y, c);
      lm_rect(l, x + H * 0.13f, y - H * 0.88f, x + H * 0.23f, y - H * 0.78f, c);
      lm_tri(l, x + H * 0.13f, y - H * 0.88f, x + H * 0.23f, y - H * 0.88f, x + H * 0.18f, y - H * 0.95f, c);
      uint32_t gold = (c & 0xFF000000u) | (mix(0xFFE0B850, c, 0.45f) & 0xFFFFFF);
      for(int k = 0; k < 3; k++) lm_rect(l, x + H * (0.155f + k * 0.017f), y - H * 1.0f, x + H * (0.165f + k * 0.017f), y - H * 0.95f, gold);
      break;
    }
    case LM_SPIRAL: {  // Copenhagen: Vor Frelsers Kirke's spiral spire over gabled roofs
      for(int k = 0; k < 6; k++) {
        float gx = x - H * 0.9f + k * H * 0.22f, gh = H * (0.22f + (k & 1) * 0.06f);
        lm_rect(l, gx, y - gh, gx + H * 0.2f, y, c);
        lm_tri(l, gx, y - gh, gx + H * 0.2f, y - gh, gx + H * 0.1f, y - gh - H * 0.1f, c);
      }
      lm_rect(l, x + H * 0.45f, y - H * 0.5f, x + H * 0.6f, y, c);
      lm_tri(l, x + H * 0.45f, y - H * 0.5f, x + H * 0.6f, y - H * 0.5f, x + H * 0.525f, y - H, c);
      uint32_t gold = (c & 0xFF000000u) | (mix(0xFFE0B850, c, 0.5f) & 0xFFFFFF);
      for(int k = 0; k < 4; k++) {
        float yy = y - H * (0.55f + k * 0.1f), hw = H * 0.075f * (1 - (k + 0.5f) / 5);
        sbar(l, x + H * 0.525f - hw, yy, x + H * 0.525f + hw, yy - H * 0.05f, t * 0.6f, gold);
      }
      break;
    }
    case LM_DOMES: {  // Helsinki Cathedral: on its stair, a green central dome and four small ones
      lm_quad(l, x - H * 0.8f, y, x - H * 0.5f, y - H * 0.15f, x + H * 0.5f, y - H * 0.15f, x + H * 0.8f, y, c);
      lm_rect(l, x - H * 0.42f, y - H * 0.45f, x + H * 0.42f, y - H * 0.15f, c);
      lm_tri(l, x - H * 0.2f, y - H * 0.45f, x + H * 0.2f, y - H * 0.45f, x, y - H * 0.53f, c);
      lm_rect(l, x - H * 0.14f, y - H * 0.62f, x + H * 0.14f, y - H * 0.45f, c);
      lm_dome(l, x, y - H * 0.62f, H * 0.15f, H * 0.2f, c);
      sbar(l, x, y - H * 0.82f, x, y - H * 0.92f, t * 0.6f, c);
      for(int s2 = -1; s2 <= 1; s2 += 2) {
        lm_rect(l, x + s2 * H * 0.36f - H * 0.05f, y - H * 0.55f, x + s2 * H * 0.36f + H * 0.05f, y - H * 0.45f, c);
        lm_dome(l, x + s2 * H * 0.36f, y - H * 0.55f, H * 0.05f, H * 0.07f, c);
      }
      break;
    }
    case LM_SKI_JUMP: {  // Oslo: Holmenkollen's ramp on its hill, the fjord's slopes
      lm_quad(l, x - H * 1.5f, y, x - H * 0.6f, y - H * 0.5f, x + H * 0.2f, y - H * 0.55f, x + H * 1.4f, y, c);
      sbar(l, x - H * 0.35f, y - H * 0.98f, x + H * 0.75f, y - H * 0.22f, t * 2.2f, c);
      lm_rect(l, x - H * 0.38f, y - H * 1.0f, x - H * 0.3f, y - H * 0.5f, c);
      break;
    }
    case LM_HOTEL_DOME: {  // Nice: the Negresco's pink dome on its white palace, the hill behind
      uint32_t pink = (c & 0xFF000000u) | (mix(0xFFE8889A, c, 0.45f) & 0xFFFFFF);
      lm_quad(l, x - H * 1.5f, y, x - H * 0.8f, y - H * 0.45f, x - H * 0.3f, y - H * 0.4f, x + H * 0.2f, y, c);
      lm_rect(l, x - H * 0.1f, y - H * 0.35f, x + H * 0.9f, y, c);
      lm_rect(l, x + H * 0.75f, y - H * 0.45f, x + H * 0.95f, y - H * 0.35f, c);
      lm_dome(l, x + H * 0.85f, y - H * 0.45f, H * 0.12f, H * 0.16f, pink);
      break;
    }
    case LM_CHATEAU: {  // a Bordeaux château among the vines: a block, round turrets with cones
      lm_rect(l, x - H * 0.4f, y - H * 0.35f, x + H * 0.4f, y, c);
      lm_tri(l, x - H * 0.42f, y - H * 0.35f, x + H * 0.42f, y - H * 0.35f, x, y - H * 0.5f, c);
      for(int s2 = -1; s2 <= 1; s2 += 2) {
        float cx = x + s2 * H * 0.45f;
        lm_rect(l, cx - H * 0.08f, y - H * 0.5f, cx + H * 0.08f, y, c);
        lm_tri(l, cx - H * 0.1f, y - H * 0.5f, cx + H * 0.1f, y - H * 0.5f, cx, y - H * 0.75f, c);
      }
      for(int k = 0; k < 10; k++) sbar(l, x - H * 1.4f + k * H * 0.28f, y, x - H * 1.3f + k * H * 0.28f, y - H * 0.06f, t, c);
      break;
    }
    case LM_ROCK: {  // Monaco: the Rock covered in tall white blocks, the Casino's two towers
      lm_quad(l, x - H * 1.3f, y, x - H * 0.8f, y - H * 0.3f, x + H * 0.6f, y - H * 0.34f, x + H * 1.1f, y, c);
      static const float kB[7][3] = {{-0.8f, 0.14f, 0.55f}, {-0.6f, 0.12f, 0.72f}, {-0.42f, 0.16f, 0.6f}, {-0.2f, 0.12f, 0.8f},
                                     {0.0f, 0.14f, 0.62f}, {0.2f, 0.12f, 0.7f}, {0.38f, 0.14f, 0.5f}};
      uint32_t white = (c & 0xFF000000u) | (mix(c, 0xFFF4F6F8, 0.35f) & 0xFFFFFF);
      for(int k = 0; k < 7; k++) lm_rect(l, x + H * kB[k][0], y - H * kB[k][2], x + H * (kB[k][0] + kB[k][1]), y - H * 0.25f, white);
      for(int s2 = -1; s2 <= 1; s2 += 2) {
        float cx = x + H * 0.8f + s2 * H * 0.14f;
        lm_rect(l, cx - H * 0.04f, y - H * 0.42f, cx + H * 0.04f, y, c);
        lm_dome(l, cx, y - H * 0.42f, H * 0.045f, H * 0.08f, c);
      }
      lm_rect(l, x + H * 0.66f, y - H * 0.3f, x + H * 0.94f, y, c);
      break;
    }
    case LM_WORKS: {  // Sheffield: cooling towers and chimneys of the steelworks
      for(int k = 0; k < 2; k++) {
        float cx = x - H * 0.6f + k * H * 0.55f, h2 = H * (0.55f - k * 0.08f);
        lm_quad(l, cx - H * 0.24f, y, cx - H * 0.13f, y - h2 * 0.75f, cx + H * 0.13f, y - h2 * 0.75f, cx + H * 0.24f, y, c);
        lm_quad(l, cx - H * 0.13f, y - h2 * 0.75f, cx - H * 0.16f, y - h2, cx + H * 0.16f, y - h2, cx + H * 0.13f, y - h2 * 0.75f, c);
      }
      for(int k = 0; k < 3; k++) lm_rect(l, x + H * (0.3f + k * 0.18f), y - H * (0.85f - k * 0.12f), x + H * (0.34f + k * 0.18f), y, c);
      lm_rect(l, x + H * 0.25f, y - H * 0.22f, x + H * 0.95f, y, c);
      uint32_t smoke = (uint32_t) ((c >> 24) * 0.4f) << 24 | (mix(c, 0xFFE0E4E8, 0.5f) & 0xFFFFFF);
      sbar(l, x + H * 0.32f, y - H * 0.88f, x + H * 0.6f, y - H * 1.02f, H * 0.08f, smoke);
      break;
    }
    case LM_CASTLE: {  // Loch Ness: Urquhart's ruined tower on its headland, and something in the water
      lm_quad(l, x - H * 1.2f, y, x - H * 0.6f, y - H * 0.2f, x + H * 0.1f, y - H * 0.22f, x + H * 0.5f, y, c);
      lm_rect(l, x - H * 0.35f, y - H * 0.65f, x - H * 0.1f, y - H * 0.2f, c);
      for(int k = 0; k < 3; k++) lm_rect(l, x - H * (0.35f - k * 0.1f), y - H * 0.7f, x - H * (0.3f - k * 0.1f), y - H * 0.65f, c);
      lm_rect(l, x - H * 0.75f, y - H * 0.3f, x - H * 0.35f, y - H * 0.2f, c);
      float nx = x + H * 0.9f;  // Nessie: three humps and a neck
      for(int k = 0; k < 3; k++) lm_dome(l, nx + k * H * 0.12f, y, H * 0.045f, H * 0.05f, c);
      sbar(l, nx - H * 0.05f, y, nx - H * 0.1f, y - H * 0.16f, t * 1.2f, c);
      lm_rect(l, nx - H * 0.15f, y - H * 0.19f, nx - H * 0.08f, y - H * 0.15f, c);
      break;
    }
    case LM_KAPOK: {  // the rain forest: giant emergent trees over the canopy
      lm_rect(l, x - H * 1.4f, y - H * 0.2f, x + H * 1.4f, y, c);
      for(int k = 0; k < 3; k++) {
        float cx = x + (k - 1) * H * 0.8f, th = H * (0.6f + (k & 1) * 0.3f);
        lm_rect(l, cx - H * 0.025f, y - th, cx + H * 0.025f, y, c);
        lm_quad(l, cx - H * 0.3f, y - th, cx - H * 0.18f, y - th - H * 0.1f, cx + H * 0.2f, y - th - H * 0.1f, cx + H * 0.32f, y - th, c);
      }
      break;
    }
    case LM_CHURCH: {  // the Black Forest: a village church's onion spire over the fir hills
      lm_quad(l, x - H * 1.4f, y, x - H * 0.5f, y - H * 0.32f, x + H * 0.4f, y - H * 0.36f, x + H * 1.4f, y, c);
      lm_rect(l, x - H * 0.05f, y - H * 0.7f, x + H * 0.05f, y - H * 0.3f, c);
      lm_dome(l, x, y - H * 0.7f, H * 0.07f, H * 0.1f, c);
      sbar(l, x, y - H * 0.8f, x, y - H * 0.92f, t * 0.5f, c);
      for(int k = 0; k < 9; k++) {
        float fx = x - H * 1.2f + k * H * 0.3f;
        lm_tri(l, fx - H * 0.06f, y - H * 0.25f, fx + H * 0.06f, y - H * 0.25f, fx, y - H * 0.45f, c);
      }
      break;
    }
    default: break;
  }
}

// Courses whose skyline is a city (seen in the original), by name: until it is learned their
// far ridge is a row of blocks in the city's blue-grey rather than green hills.
static bool city_skyline(int course) {
  static const char* kCity[] = {"TOKYO", "NEW YORK", "FRANKFURT", "LOS ANGELES", "SAN FRANCISCO", "YOKOHAMA", "LONDON", "PARIS"};
  static int cached = -1;
  static bool city;
  if(course != cached) {
    char name[40], country[40];
    course_name(course, name, country, sizeof(name));
    cached = course, city = false;
    for(size_t i = 0; i < sizeof(kCity) / sizeof(kCity[0]); i++) city |= strcmp(name, kCity[i]) == 0;
  }
  return city;
}

// The redrawn skyline, in layers that move with their depth (parallax): far back the ridge,
// fixed in the world like the sun and the clouds (it turns with the camera's heading only);
// the landmark; a nearer row of hills; the course's own outline in front, at the game's own
// scroll (the reference: the original's skyline turns about 1.5 times as fast as the world,
// and this layer keeps matching the road's bends as the original shows them). The nearer a
// layer, the more it also shifts as the car moves across the road. Each farther layer sits
// deeper in the haze, with a band of mist along its foot. lefts[]: each layer's panorama x at
// the screen's left: ridge, hills, landmark, outline.
enum { SKY_RIDGE, SKY_HILLS, SKY_LANDMARK, SKY_OUTLINE };
static void modern_skyline(TgView3D* v, TgV3List* out, int w, float hy, const float* lefts, float pxs, float bandScale, float lmx,
                           float lmx2) {
  sky_profile(v);
  uint32_t haze = g_haze;
  // The far ridge, from the very first picture of a race (QA-006: the skyline is learned
  // from the screen as the camera turns, which took 10-35 s, and 2-player races never learn
  // one): where the panorama is known its outline, smoothed and raised; where it is not
  // yet, a guess from what is known (panoramas repeat their tiles: the column 64, 128, 256
  // along; else the nearest known one), over a gentle procedural roll. Each column eases to
  // its new height as more is learned, so nothing jumps.
  {
    static float guess[SKY_W];
    bool city = city_skyline(v->course);
    int nknown = 0;
    for(int c = 0; c < SKY_W; c++) nknown += v->sky_known[c];
    for(int c = 0; c < SKY_W; c++) {
      guess[c] = v->sky_prof[c];
      if(v->sky_known[c] || !nknown) continue;
      static const int kRep[] = {256, 128, -128, 64, -64, 192, -192};
      bool found = false;
      for(size_t k = 0; k < sizeof(kRep) / sizeof(kRep[0]) && !found; k++) {
        int q = (c + kRep[k]) & (SKY_W - 1);
        if(v->sky_known[q]) guess[c] = v->sky_prof[q], found = true;
      }
      for(int d = 1; d < 48 && !found; d++)
        for(int sgn = -1; sgn <= 1 && !found; sgn += 2) {
          int q = (c + sgn * d) & (SKY_W - 1);
          if(v->sky_known[q]) guess[c] = v->sky_prof[q], found = true;
        }
    }
    for(int c = 0; c < SKY_W; c++) {
      float sm = 0;
      for(int d = -12; d <= 12; d += 3) sm += guess[(c + d) & (SKY_W - 1)];
      float roll = city ? 10 + (hash32((uint32_t) (c / 7) * 0x9E3779B1u) % 18)  // blocks of buildings
                        : 7 + 4 * sinf(c * (2 * PI * 4 / SKY_W)) + 3 * sinf(c * (2 * PI * 11 / SKY_W));  // hills (periodic: no seam)
      float want = sm / 9 * 1.2f + roll;
      v->ridge[c] = v->ridge_set ? v->ridge[c] + (want - v->ridge[c]) * ease_k(v, 0.04f) : want;
    }
    v->ridge_set = true;
  }
  float colW = pxs;  // screen pixels per panorama column
  for(int layer = 0; layer < 2; layer++) {
    float left = layer ? lefts[SKY_OUTLINE] : lefts[SKY_RIDGE];
    int c0 = (int) floorf(left), n = (int) (w / colW) + 2;
    if(layer == 1) {
      // Mist along the ridge's foot, then the nearer hills (less hazed), then a thinner mist.
      uint32_t mist = g_night ? 0xFF161C2E : haze;
      screen_rect(out, 0, hy - 7 * bandScale, (float) w, hy + 1, 0, 1, with_alpha(mist, 0), with_alpha(mist, 150), -1);
      // The city's landmark, in front of the far ridge and behind the hills: in its own fixed
      // direction in the world (lmx, its foot's place on the screen: lm_place, QA-007).
      {
        int kind = city_landmark(v->course);
        float H = 64 * bandScale;  // (LM_HANG)
        uint32_t lc = g_night ? 0xFF1E1934 : mix(0xFF56677E, haze, 0.55f);  // blue-grey, far off in the haze; at night barely darker than the sky
        v->landmark_fade = fade_up(v, v->landmark_fade, 6);
        const LmMesh* model = lm_model(kind);
        if(model) {
          // Modelled (lm_mesh). Rio: Sugarloaf beside Corcovado in its own direction, on the far
          // side from the road (lmx2); at night the statue is floodlit: a soft glow round it.
          if(lmx > -H * model->x1 && lmx < w - H * model->x0) {
            if(kind == LM_CHRIST && g_night) {
              float gy = hy + 1 - H * 0.8f, r = H * 0.34f;
              screen_rect(out, lmx - r, gy - r, lmx + r, gy + r, 0, 1, with_alpha(0xFFFFF0DC, v->landmark_fade * 70 / 255),
                          with_alpha(0xFFFFF0DC, v->landmark_fade * 70 / 255), TEX_GLOW);
            }
            lm_mesh(out, model, lmx, hy + 1, H, g_lm_az, v->landmark_fade);
          }
          if(kind == LM_CHRIST && lmx2 > -H * kLmSugarloaf.x1 && lmx2 < w - H * kLmSugarloaf.x0)  // (in the bay: in front)
            lm_mesh(out, &kLmSugarloaf, lmx2, hy + 1, H, g_lm_az + g_lm_side * LM_SUGARLOAF_OFF * LM_HANG, v->landmark_fade);
        } else if(lmx > -2 * H && lmx < w + 2 * H)
          landmark(out, kind, lmx, hy + 1, H, with_alpha(lc, v->landmark_fade));
      }
      float hl = lefts[SKY_HILLS];
      int h0 = (int) floorf(hl), hn = (int) (w / colW) + 2;
      bool city = city_skyline(v->course);
      // Hills of the course's own ground (dunes in the desert, green country), darkened as
      // land seen against the light; city blocks blue-grey.
      uint32_t base = city ? 0xFF4E5C70 : mix(shade(g_land, 0.62f), 0xFF46604E, 0.25f);
      uint32_t ht = g_night ? 0xFF15152A : mix(base, haze, 0.4f), hb = g_night ? 0xFF110F22 : mix(base, haze, 0.62f);
      float prev = -1;
      for(int i = 0; i < hn; i++) {
        int c = (h0 + i) & (SKY_W - 1);
        float x = (h0 + i - hl) * colW;
        // Rolling hills (periodic round the panorama: no seam), or low city blocks.
        float hgt = city ? 2.5f + (float) (hash32((uint32_t) (c / 5) * 0x85EBCA6Bu + 77) % 6)
                         : 3.5f + 2.2f * sinf(c * (2 * PI * 6 / SKY_W) + 1.3f) + 1.4f * sinf(c * (2 * PI * 15 / SKY_W) + 0.4f) +
                               0.6f * sinf(c * (2 * PI * 37 / SKY_W));
        float hl0 = prev >= 0 && (!city || fabsf(prev - hgt) < 0.1f) ? prev : hgt;
        TgV3Vertex q[6] = {{x, hy - hl0 * bandScale, 0, 0, ht}, {x + colW, hy - hgt * bandScale, 0, 0, ht}, {x + colW, hy + 1, 0, 0, hb},
                           {x, hy - hl0 * bandScale, 0, 0, ht}, {x + colW, hy + 1, 0, 0, hb}, {x, hy + 1, 0, 0, hb}};
        list_push(out, q, 6, -1);
        prev = hgt;
      }
      screen_rect(out, 0, hy - 3 * bandScale, (float) w, hy + 1, 0, 1, with_alpha(mist, 0), with_alpha(mist, 90), -1);
    }
    float prevH = -1, hk = ease_k(v, 0.07f);
    for(int i = 0; i < n; i++) {
      int c = (c0 + i) & (SKY_W - 1);
      float x = (c0 + i - left) * colW;
      float hgt = v->sky_prof[c];
      if(layer == 1 && hgt >= 0.5f) {
        // A column learned again (another sight of the panorama: the whole city outline at a
        // race's start) grows to its new height over a few frames instead of at once
        // (QA-009: LA 8532, 8920). A new column starts at its height: it fades in (below).
        float* hs = &v->sky_hshow[c];
        *hs = *hs < 0.5f ? hgt : *hs + (hgt - *hs) * hk;
        hgt = *hs;
      }
      uint32_t ct = v->sky_ctop[c], cb = v->sky_cbot[c];
      if(layer == 0) {
        // The ridge behind: a smoothed, raised outline, pale in the haze (see above).
        hgt = v->ridge[c];
        ct = mix(city_skyline(v->course) ? 0xFF5A6A80 : 0xFF4E6A64, haze, 0.62f);  // one even colour: no stripes between the buildings
        cb = mix(ct, haze, 0.4f);
      } else {
        // One clean palette instead of the picture's mottled colours: greenery a deep
        // calm green, everything else city blue-grey; the picture keeps its light and dark.
        int r0 = ct >> 16 & 255, g0 = ct >> 8 & 255, b0 = ct & 255, lum = (r0 * 3 + g0 * 6 + b0) / 10;
        int votes = 0;  // greenery or city by the columns around (no single odd stripes)
        for(int d = -3; d <= 3; d++) {
          uint32_t q = v->sky_ctop[(c + d) & (SKY_W - 1)];
          int qr = q >> 16 & 255, qg = q >> 8 & 255, qb = q & 255;
          votes += qg > qr + 12 && qg > qb + 12;
        }
        bool green = votes >= 4;
        (void) r0, (void) g0, (void) b0;
        uint32_t tone = green ? 0xFF3F6E4C : 0xFF66788E;
        float k = 0.75f + 0.5f * (lum / 255.0f);
        ct = 0xFF000000u | mix(shade(tone, k), haze, 0.18f), cb = 0xFF000000u | mix(shade(tone, k * 0.8f), haze, 0.38f);  // solid
        // Eased: a stretch of the outline read again flips between greenery and city by its
        // columns' votes; drawn at once it was a block of the skyline changing colour in one
        // frame (QA-009: LA 8532, 8920).
        uint32_t* cs = v->sky_cshow[c];
        cs[0] = cs[0] ? ease_colour(cs[0], ct, hk) : ct, cs[1] = cs[1] ? ease_colour(cs[1], cb, hk) : cb;
        ct = cs[0] | 0xFF000000u, cb = cs[1] | 0xFF000000u;
      }
      if(hgt < 0.5f) {
        prevH = -1;
        continue;
      }
      // A column just learned fades in (no skyline popping in piece by piece on a first visit).
      if(layer == 1 && v->sky_fade[c] < 255) v->sky_fade[c] = fade_up(v, v->sky_fade[c], 14);
      int fa = layer == 0 ? 255 : v->sky_fade[c];  // (the ridge eases its height instead: it is there from the start)
      if(g_night && layer == 1) ct = 0xFF141A2A, cb = 0xFF0C1020;
      if(g_night && layer == 0) ct = cb = 0xFF1A1830;
      if(fa < 255) ct = with_alpha(ct, fa), cb = with_alpha(cb, fa);
      // Gentle slopes join smoothly; steps (a building's edge) stay sharp - blended over a unit
      // between the two, so a column easing across the line doesn't jump (QA-009).
      float jk = prevH >= 0 ? 3.0f - fabsf(prevH - hgt) : 0;
      jk = jk < 0 ? 0 : jk > 1 ? 1 : jk;
      float hl = hgt + (prevH - hgt) * jk;
      float tl = hy - hl * bandScale, tr = hy - hgt * bandScale;
      TgV3Vertex q[6] = {{x, tl, 0, 0, ct}, {x + colW, tr, 0, 0, ct}, {x + colW, hy + 1, 0, 0, cb},
                         {x, tl, 0, 0, ct}, {x + colW, hy + 1, 0, 0, cb}, {x, hy + 1, 0, 0, cb}};
      list_push(out, q, 6, -1);
      if(layer == 0) {
        prevH = hgt;
        continue;
      }
      // A building: a flat roof with a sheer wall near by (hills slope, they don't step). Judged
      // on the learned outline (not the eased one: easing, it flipped on and off), and its
      // windows fade in and out with that judgement and with the column (QA-009: 1-3 block
      // skyline pops were windows appearing at once).
      float raw = v->sky_prof[c];
      bool tower = false, roof = raw > 6;
      for(int d = -3; d <= 3 && roof; d++) roof = fabsf(v->sky_prof[(c + d) & (SKY_W - 1)] - raw) < 0.3f;
      if(roof) {  // (a slope's pixel steps leave short flat runs: a roof is wider)
        for(int d = -8; d <= 8 && !tower; d++) {
          float a0 = v->sky_prof[(c + d) & (SKY_W - 1)], a1 = v->sky_prof[(c + d + 1) & (SKY_W - 1)];
          tower = fabsf(a0 - a1) > 3.0f;
        }
      }
      float* tw = &v->sky_tw[c];
      *tw += ((tower ? 1.0f : 0.0f) - *tw) * hk;
      float wk = fa / 255.0f;
      // Lit windows at night on a big city's outline, elsewhere only on what is judged a
      // building (as the day's glints): on every tall column they lit a ridge of firs, hills or
      // a mountain too - a band of window dots across the night sky (QA-031, Black Forest).
      float dayK = *tw * wk, nightK = wk * (hgt - 4 < 0 ? 0 : hgt - 4 > 2 ? 1 : (hgt - 4) / 2) * (city_skyline(v->course) ? 1.0f : *tw);
      if(!g_night && dayK > 0.02f) {  // daytime: the windows' glass catching the sky, a lit roof edge
        float wy0 = hy - hgt * bandScale;
        uint32_t edge = (uint32_t) (0x60 * dayK) << 24 | 0xFFFFFF, glass = (uint32_t) (0x2A * dayK) << 24 | 0xE8F4FF;
        screen_rect(out, x, wy0, x + colW, wy0 + bandScale * 0.8f, 0, 1, edge, edge, -1);
        for(int wy = 1; wy < (int) (hgt / 3); wy++) {
          float yy = wy0 + (wy * 3 + 1.0f) * bandScale;
          screen_rect(out, x + colW * 0.2f, yy, x + colW * 0.8f, yy + bandScale * 1.2f, 0, 1, glass, glass, -1);
        }
      }
      if(g_night && nightK > 0.02f) {  // lit windows
        float wy0 = hy - hgt * bandScale;
        for(int wy = 0; wy < (int) (hgt / 3); wy++) {
          uint32_t hh = hash32((uint32_t) c * 7919u + (uint32_t) wy * 104729u);
          if((hh & 3) == 0) continue;
          float yy = wy0 + (wy * 3 + 1.5f) * bandScale;
          uint32_t wc = (hh >> 4) & 1 ? 0xE0FFD27A : 0xC0BFD8FF;
          wc = (uint32_t) ((wc >> 24) * nightK) << 24 | (wc & 0xFFFFFF);
          screen_rect(out, x + colW * 0.25f, yy, x + colW * 0.75f, yy + bandScale * 1.1f, 0, 1, wc, wc, -1);
        }
      }
      prevH = hgt;
    }
  }
}

static const char* message_text(uint16_t id, char* buf, size_t n);
static bool taunt_up(uint16_t id);
static const char* brand_caps(void);

// The ground the modern look draws, at (x, z): the road, its kerbs and shoulders and the
// ground bands beside it are level across the road and at the road's height along it, one
// strip per segment between the node lines (the centre at node i, across along its right
// vector). The highest strip over the point among segments seg0 +- range (in a tight bend the
// strips on the inside fold over each other: all of them count), out to `reach` metres from
// the middle; the far bands (beyond the shoulder) only where they are drawn, within 9 m of
// the car's ground `ref` (the renderer fades them out further up or down). -1e9 when none.
static float drawn_ground(const TgTrack3D* tr, float x, float z, int seg0, int range, float reach, float ref) {
  int n = tr->tables.segments;
  float best = -1e9f, hw = tr->scale.half_width, shoulder = 3.3f * hw;
  for(int i = seg0 - range; i <= seg0 + range; i++) {
    int s = ((i % n) + n) % n;
    TgVec3 c0 = tr->centre[s], c1 = tr->centre[s + 1];
    float h0 = tr->heading[s], h1 = tr->heading[s + 1];
    // Signed distance ahead of each node's line (its forward, level: cos h, -sin h).
    float d0 = (x - c0.x) * cosf(h0) - (z - c0.z) * sinf(h0);
    float d1 = (x - c1.x) * cosf(h1) - (z - c1.z) * sinf(h1);
    if((d0 >= 0) == (d1 >= 0) || d0 == d1) continue;  // not between the two lines
    float f = d0 / (d0 - d1), hh = h0 + (h1 - h0) * f;
    float cx = c0.x + (c1.x - c0.x) * f, cz = c0.z + (c1.z - c0.z) * f, cy = c0.y + (c1.y - c0.y) * f;
    float side = fabsf((x - cx) * sinf(hh) + (z - cz) * cosf(hh));
    if(side > reach) continue;
    if(side > shoulder && fabsf(cy - ref) > 9) continue;
    if(cy > best) best = cy;
  }
  return best;
}

void tg_view3d_draw_view(TgView3D* v, int view, int w, int h, TgV3Look look, TgV3Camera camMode, TgV3List* out) {
  view = view ? 1 : 0;
  g_viewIdx = view;
  tg_v3_list_clear(out);
  if(!v->have || w <= 0 || h <= 0) return;
  v->ticks++;
  // The game frames since the shown colours last moved (view 2 of a 2-player race: none more).
  v->ease_n = view == 0 ? (float) (v->frames - v->eased_at > 120 ? 120 : v->frames - v->eased_at) : 0;
  if(view == 0) v->eased_at = v->frames;
  if(view == 0)  // the roadside pictures learned: their objects fade in (QA-009)
    for(int i = 0; i < ART_COUNT; i++)
      if(v->art[i].n > 0 && v->pics[ART_PIC + i].w && v->art_fade[i] < 255) v->art_fade[i] = fade_up(v, v->art_fade[i], 12);
  const TgTrack3D* tr = &v->track;
  int nseg = tr->tables.segments;
  float len = (float) nseg * TG_TRACK3D_SEG_UNITS;
  float t = ((float) v->since + v->subframe) / (float) v->period;
  if(t > 1) t = 1;
  v->pit_sub = v->subframe;  // (the drawn pit stops run on by the display's part of a frame)

  // The camera: the game's own, behind the player's car (the original renders from the
  // player's track position, its eye $90 track units behind), or one of the extra ones.
  int pk = v->cur.view_car[view];
  float carPos = 0, pside = 0;
  if(pk >= 0) car_now(v, pk, t, &carPos, &pside);
  // The game's camera ($46): the player's car in the race, easing in towards it as the
  // race starts (the original's fly-in).
  float ppos;
  {
    float d = (float) v->cam_cur[view] - v->cam_prev[view];
    if(d > len / 2) d -= len;
    if(d < -len / 2) d += len;
    ppos = v->cam_prev[view] + d * (fabsf(d) > 0x200 ? 1 : t);
    if(ppos < 0) ppos += len;
    if(ppos >= len) ppos -= len;
    // In the race the camera is the car: follow the car's smooth position exactly.
    float off = ppos - carPos;
    if(off > len / 2) off -= len;
    if(off < -len / 2) off += len;
    if(pk >= 0 && fabsf(off) < 2) ppos = carPos;
    // In its pit stop the car is drawn on the pit lane, away from where the game holds it (and its
    // camera): the camera is the car as drawn.
    if(pk >= 0 && pit_car(v, v->cur.cars[pk].id)) ppos = carPos;
  }
  (void) carPos;
  float back, height, fovX, lookAhead, lookUp;
  switch(camMode) {
    case TG_V3_CAM_HIGH: back = 0xE0; height = 4.5f; fovX = 52; lookAhead = 20; lookUp = 0.4f; break;
    case TG_V3_CAM_BUMPER: back = -12; height = 1.0f; fovX = 72; lookAhead = 30; lookUp = 0.9f; break;
    default: back = 0x90; height = 2.9f; fovX = 46; lookAhead = 30; lookUp = 2.6f; break;  // as the original's: level, car low on screen
  }
  float eyeSide = camMode == TG_V3_CAM_BUMPER ? pside : v->cam_side[view];
  // Standing in its pit box, the chase camera rises a little and swings out over the fast lane
  // (the crew round the car, the track beyond the wall in the picture), back as it drives off.
  if(pk >= 0 && camMode == TG_V3_CAM_GAME) {
    float pc = pit_framing(v, v->cur.cars[pk].id);
    height += 1.3f * pc;
    eyeSide -= g_pit.sgn * 2.6f * pc / tr->scale.half_width * TG_TRACK3D_SIDE_EDGE;
  }
  TgTrackFrame ef = tg_track3d_frame(tr, ppos - back, eyeSide);
  TgTrackFrame lf = tg_track3d_frame(tr, ppos + lookAhead / tr->scale.seg_len * TG_TRACK3D_SEG_UNITS, eyeSide);
  Cam cam;
  cam.eye = vadd(tv(ef.pos), v3(0, height, 0));
  // The original keeps its car in the same place on the screen and its horizon level, over
  // hills, drops and off the road against a bank. The eye's height is the car's (its road
  // under it) plus the camera's height, not the road's 11 m behind it: on a downhill or a
  // drop the car used to sink below the picture while the camera pitched down after the
  // road (QA: Nice, Monaco, Tokyo, Yokohama 3300, Machu Picchu 3900, Black Forest,
  // Stockholm 4500). Never under the road behind it, though.
  if(pk >= 0) {
    V3 carPt = tv(tg_track3d_frame(tr, carPos, pside).pos);
    // Its distance from the car is the camera's, wherever the car is: placed on the track
    // beside the road, far out on the inside of a bend, the eye came within 4 m of the car
    // (Tokyo 3300) and the car filled the picture's corner.
    if(back > 0 && ppos == carPos) {
      V3 c0 = tv(tg_track3d_frame(tr, ppos - back, 0).pos), c1 = tv(tg_track3d_frame(tr, ppos, 0).pos);
      float want = sqrtf((c1.x - c0.x) * (c1.x - c0.x) + (c1.z - c0.z) * (c1.z - c0.z));
      // (within 80-110 % of the arm's length in metres: where the lap's closing error is
      // spread, the road's middle runs 1 m a segment, not 2.5, and the arm shrank to 4.5 m,
      // the car filling the picture - Sheffield 4644-4700)
      float nominal = back / TG_TRACK3D_SEG_UNITS * tr->scale.seg_len;
      want = clampf(want, nominal * 0.8f, nominal * 1.1f);
      float dx = carPt.x - cam.eye.x, dz = carPt.z - cam.eye.z, d = sqrtf(dx * dx + dz * dz);
      if(d < 0.5f) {  // the eye's place on the track is the car's (the middle of a tight bend): behind it
        TgTrackFrame cfr = tg_track3d_frame(tr, carPos, pside);
        dx = cfr.forward.x, dz = cfr.forward.z, d = sqrtf(dx * dx + dz * dz);
      }
      if(d > 1e-4f) cam.eye.x = carPt.x - dx / d * want, cam.eye.z = carPt.z - dz / d * want;
    }
  }
  // The chase camera (QA-022). The eye is a spring arm behind the car as the game places it
  // (the distance along the road, the side eased), at the car's height plus the camera's,
  // kept clear of the ground the modern look draws: on a steep descent the strips 8-11 m
  // behind the car, on the inside of a bend above all, are 3 m higher than the car, and the
  // eye at the car's height + 2.9 m (lowered further to frame the car) was under the road -
  // the picture filled with kerb from above or flat green, the car not drawn (Sheffield
  // 3188-3336, Yokohama 3316-3420). As third-person cameras keep their arm out of the world
  // (a collision probe round the eye and along the line to the target, Unreal's spring arm),
  // the eye stays over the drawn ground round it and the line from it to the car's roof stays
  // over the ground between; the car is framed by pitching the view, not by moving the eye.
  // The height over the car and the framing pitch are critically damped springs (no
  // overshoot; Lowe, "Critically Damped Ease-In/Ease-Out Smoothing", Game Programming Gems 4)
  // stepped by game time, rising quickly and settling slowly; the clearances are hard limits
  // after the springs, so the eye is never inside.
  bool chase = pk >= 0 && camMode != TG_V3_CAM_BUMPER;
  float rigDt = -1;
  V3 carPt = v3(0, 0, 0);
  if(chase) {
    double T = (double) v->frames + v->subframe;
    rigDt = v->rig_on[view] ? (float) ((T - v->rig_t[view]) / 60.0) : -1;
    if(rigDt > 0.5f) rigDt = -1;  // a long gap (a pause, a jump): start from where it should be
    v->rig_t[view] = T;
    carPt = tv(tg_track3d_frame(tr, carPos, pside).pos);
    int cseg = (int) floorf(carPos / TG_TRACK3D_SEG_UNITS);
    float reach = (look == TG_V3_LOOK_MODERN ? VERGE * 0.78f : VERGE) * tr->scale.half_width, ref = carPt.y;
    float gEye = -1e9f;
    static const float probe[5][2] = {{0, 0}, {1.2f, 0}, {-1.2f, 0}, {0, 1.2f}, {0, -1.2f}};
    for(int i = 0; i < 5; i++) gEye = fmaxf(gEye, drawn_ground(tr, cam.eye.x + probe[i][0], cam.eye.z + probe[i][1], cseg, 24, reach, ref));
    float soft = height, hard = -1e9f;
    if(gEye > -1e8f) soft = fmaxf(soft, gEye + 1.6f - carPt.y), hard = gEye + 0.7f - carPt.y;
    // The line from the eye to the car's roof over its middle, over the ground between.
    TgTrackFrame cf0 = tg_track3d_frame(tr, carPos, pside);
    V3 roof = vadd(vadd(carPt, vmul(tv(cf0.forward), 2.0f)), v3(0, 1.1f, 0));
    for(int i = 1; i <= 4; i++) {
      float s = i / 5.0f;
      float gx = cam.eye.x + (roof.x - cam.eye.x) * s, gz = cam.eye.z + (roof.z - cam.eye.z) * s;
      float gl = drawn_ground(tr, gx, gz, cseg, 24, reach, ref);
      if(gl < -1e8f) continue;
      float need = (gl + 0.45f - roof.y * s) / (1 - s) - carPt.y;  // the eye's height over the car for the line to clear it
      hard = fmaxf(hard, need), soft = fmaxf(soft, need + 0.3f);
    }
    float* hs = v->rig_h[view];
    if(rigDt < 0) hs[0] = soft, hs[1] = 0;
    else if(rigDt > 0) spring_step(hs, soft, soft > hs[0] ? 9.0f : 3.5f, rigDt);
    if(hs[0] < hard) hs[0] = hard, hs[1] = fmaxf(hs[1], 0);
    cam.eye.y = carPt.y + hs[0];
  }
  V3 target = vadd(tv(lf.pos), v3(0, lookUp, 0));
  // Nearly level (a chase camera keeps the horizon steady: its pitch follows the road a
  // little only, about 1.5 degrees up, 2.3 down over a crest so the road beyond shows).
  {
    V3 d = vsub(target, cam.eye);
    float flat = sqrtf(d.x * d.x + d.z * d.z), maxRise = flat * 0.022f, minRise = -flat * 0.04f;
    if(d.y > maxRise) target.y = cam.eye.y + maxRise;
    if(d.y < minRise) target.y = cam.eye.y + minRise;
  }
  cam.fwd = vnorm(vsub(target, cam.eye));
  cam.right = vnorm(vcross(cam.fwd, v3(0, 1, 0)));
  cam.up = vcross(cam.right, cam.fwd);
  // The car across the picture as the original keeps it: never further from the middle than
  // the road's own scroll puts it (a tenth of the depth); far out on the verge in a bend the
  // look-ahead along the road turned it to the picture's side (Black Forest 4500). The view
  // turns towards it, round the vertical.
  if(chase) {
    V3 fl = vnorm(v3(cam.fwd.x, 0, cam.fwd.z)), dc = vnorm(v3(carPt.x - cam.eye.x, 0, carPt.z - cam.eye.z));
    float ang = atan2f(vcross(fl, dc).y, vdot(fl, dc)), lim = atanf(0.09f);
    float turn = ang > lim ? ang - lim : ang < -lim ? ang + lim : 0;
    if(turn != 0) {
      float ct = cosf(turn), st = sinf(turn);
      cam.fwd = vnorm(v3(cam.fwd.x * ct + cam.fwd.z * st, cam.fwd.y, -cam.fwd.x * st + cam.fwd.z * ct));
      cam.right = vnorm(vcross(cam.fwd, v3(0, 1, 0)));
      cam.up = vcross(cam.right, cam.fwd);
    }
  }
  // The car where the original keeps it: its back at 0.27 of the depth under the view's axis
  // (the picture's foot is at 0.318), never lower, not much higher. Raised over a descent or
  // a crest the eye looks down at it: the view pitches by what the car needs (eased), and at
  // once by whatever more keeps the whole car in the picture (cut by the bottom edge over a
  // crest: Copenhagen 4500-4580, Sheffield 4476-4512).
  if(chase && ppos == carPos) {
    TgTrackFrame cf1 = tg_track3d_frame(tr, carPos, pside);
    V3 qb = to_cam(&cam, carPt), qt = to_cam(&cam, vadd(vadd(carPt, vmul(tv(cf1.forward), 3.0f)), v3(0, 1.4f, 0)));
    if(qb.z > 1.5f && qt.z > 1.5f) {
      float ab = atanf(qb.y / qb.z), at = atanf(qt.y / qt.z);  // the car's back and roof, as angles off the axis
      float want = 0;  // the pitch (up +) that frames it
      if(ab < atanf(-0.27f)) want = ab - atanf(-0.27f);
      else if(ab > atanf(-0.12f)) want = ab - atanf(-0.12f);
      float* ps = v->rig_p[view];
      if(rigDt < 0) ps[0] = want, ps[1] = 0;
      else if(rigDt > 0) spring_step(ps, want, 7.0f, rigDt);
      float hi = ab - atanf(-0.28f), lo = at - atanf(0.29f);  // the whole car in the picture, wheels and all
      if(ps[0] > hi) ps[0] = hi, ps[1] = fminf(ps[1], 0);
      if(ps[0] < lo && lo <= hi) ps[0] = lo, ps[1] = fmaxf(ps[1], 0);
      float p = ps[0], cp = cosf(p), sp = sinf(p);
      cam.fwd = vnorm(vadd(vmul(cam.fwd, cp), vmul(cam.up, sp)));
      cam.up = vcross(cam.right, cam.fwd);
    }
  }
  if(chase) v->rig_on[view] = true;
  // The player's car, for fading what would hide it (near_fade).
  g_occ_on = false;
  if(chase) {
    TgTrackFrame pf = tg_track3d_frame(tr, carPos, pside);
    g_occ_on = occluder_setup(cam.eye, tv(pf.pos), tv(pf.forward), tv(pf.right), v->car_ms[v->cur.cars[pk].id & 31]) &&
               to_cam(&cam, tv(pf.pos)).z > 1;
  }
  // The field of view is set for a 4:3 picture; wider windows see more at the sides.
  cam.f = (h * 4.0f / 3.0f) / 2 / tanf(fovX * PI / 360);
  cam.cx = w / 2.0f;
  cam.cy = h / 2.0f;
  cam.w = (float) w;
  cam.h = (float) h;

  // The background. The horizon: where a level line ahead meets the screen.
  V3 level = vnorm(v3(cam.fwd.x, 0, cam.fwd.z));
  V3 lc = to_cam(&cam, vadd(cam.eye, level));
  float hy = cam.cy - lc.y / lc.z * cam.f;
  // The original draws its skyline on top of the road band, so it stays above a hill's crest
  // ahead: here too, it sits on the highest ground ahead when that rises over the horizon.
  {
    float top = hy, reach = look == TG_V3_LOOK_MODERN ? 3.0f : VERGE;
    for(int i = 4; i < DRAW_SEGS; i += 3)
      for(int k = -1; k <= 1; k++) {
        V3 q = to_cam(&cam, tv(tg_track3d_frame(tr, ppos - back + i * TG_TRACK3D_SEG_UNITS, k * reach * TG_TRACK3D_SIDE_EDGE).pos));
        float sx = cam.cx + q.x / q.z * cam.f;
        if(q.z < 2 || sx < 0 || sx > w) continue;  // only what is on the screen
        float y = cam.cy - q.y / q.z * cam.f;
        if(y < top) top = y;
      }
    if(top < h * 0.2f) top = h * 0.2f;
    hy = top;
  }
  uint32_t skyTop = hist_top(&v->sky_top, 0, 0xFF3070E8), skyLow = hist_top(&v->sky_low, 0, 0xFF60B0F8);
  {
    // Only a sky-like colour is the sky (blue the strongest): over the start's fly-in or a
    // crest the top of the picture is grass or road, which must not tint the sky or make night.
    // Then the sky stays as it was shown (else the original's cyan), not a deep made-up blue
    // (Chichen Itza over its crests, its dusk sky: QA polish).
    int R = skyTop >> 16 & 255, G = skyTop >> 8 & 255, B = skyTop & 255;
    if(!(B >= R && B + 16 >= G)) skyTop = v->sky_show[0] ? v->sky_show[0] : 0xFF18A8F0;
    // A horizon colour picked up from the original's hills (green) is not the sky's: a paler
    // blue from the top instead (as the original's cyan). Decided on the learned colour,
    // before the ease: decided on the eased one (in modern_sky), the whole sky switched in one
    // frame as it crossed the line (QA-009: LA 8532, 8920).
    if(!sky_like(skyLow)) skyLow = mix(skyTop, 0xFFBFE8FF, 0.55f);
    // Eased: as the sky's colour is learned it changes; shown, it drifts there (no jumps).
    uint32_t want[2] = {skyTop, skyLow};
    for(int i = 0; i < 2; i++) {
      v->sky_show[i] = v->sky_show[i] ? ease_colour(v->sky_show[i], want[i], ease_k(v, 0.03f)) : want[i];
      if(i == 0) skyTop = v->sky_show[i];
      else skyLow = v->sky_show[i];
    }
  }
  uint32_t ground0, ground1;
  {
    // The course's ground pair, kept as picked and eased (no swap between the stripes).
    uint32_t want[2];
    hist_pair(&v->ground, 0xFFD8A878, v->ground_pick);
    want[0] = v->ground_pick[0], want[1] = v->ground_pick[1];
    if(view == 0 || !v->ground_show[0])
      for(int j = 0; j < 2; j++)
        v->ground_show[j] = v->ground_show[j] ? ease_colour(v->ground_show[j], want[j], ease_k(v, 0.06f)) : want[j];
    ground0 = v->ground_show[0], ground1 = v->ground_show[1];
    if(view == 0) ground_ease(v, ground0, ground1);
  }
  uint32_t road0 = hist_top(&v->road, 0, 0xFF485858), road1 = hist_top(&v->road, 1, road0);
  // The open ground: the colour of the stretch the camera is on, eased (no sudden switch).
  uint32_t groundHere;
  {
    int ps = nseg_of(v, ppos);
    uint32_t ga, gb;
    ground_at(v, ps, ground0, ground1, &ga, &gb);
    v->ground_now = v->ground_now ? ease_colour(v->ground_now, ga, ease_k(v, 0.04f)) : ga;
    groundHere = look == TG_V3_LOOK_MODERN ? calm(v->ground_now) : v->ground_now;
  }
  // The skyline: the game's panorama, as many of its pixels across the screen as the
  // original shows across its 256, from the game's own scroll, so it turns as the game's.
  float pxs = (h * 4.0f / 3.0f) / 256.0f;  // screen pixels per panorama pixel
  float bandH = SKY_H * pxs * 0.875f;      // the SNES pixel is 8:7
  bool modern = look == TG_V3_LOOK_MODERN;
  {
    // Night or day by the sky's brightness and the ground's (the original's nights are a
    // navy sky over black ground: Black Forest's night sky alone reads as day), smoothed,
    // with a margin either way: a sky near the line can't make the course blink between the
    // two; a sustained change (the game's own day/night script) gets through in a second or two.
    // The sky by its brightest channel (a saturated royal-blue day sky is lit, though its
    // channels sum low), the ground by its sum.
    int mx = skyTop >> 16 & 255;
    if((int) (skyTop >> 8 & 255) > mx) mx = skyTop >> 8 & 255;
    if((int) (skyTop & 255) > mx) mx = skyTop & 255;
    // (the ground by the brighter of its two stripes: the original's night ground is dark on
    // both, while a day ground going from sand to grass passes one stripe through near black
    // for a few frames - QA-026, Las Vegas 3900: one dark sample eased in made a day night)
    int lt = mx * 8 / 5, lg = (lum3(ground0) > lum3(ground1) ? lum3(ground0) : lum3(ground1)) * 4;
    if(lg < lt) lt = lg;
    if(v->night_lum < 0) v->night_lum = (float) lt, v->night = lt < 150;
    v->night_lum += (lt - v->night_lum) * ease_k(v, 0.05f);
    if(v->night && v->night_lum > 190) v->night = false;
    if(!v->night && v->night_lum < 120) v->night = true;
    g_night = modern && v->night;
  }
  g_land = groundHere;
  if(g_night) groundHere = mix(groundHere, 0xFF0A0E16, 0.84f);
  g_haze = g_night ? 0xFF161C2E : mix(mix(skyLow, 0xFFF4F6FA, 0.45f), groundHere, 0.68f);  // mostly the ground's, paler
  if(modern) {
    modern_sky(out, v, &cam, (float) w, (float) h, hy, bandH, skyTop, skyLow);
  } else {
    screen_rect(out, 0, 0, (float) w, hy - bandH + 1, 0, 1, skyTop, skyLow, -1);
    screen_rect(out, 0, hy - bandH, (float) w, hy + 1, 0, 1, skyLow, skyLow, -1);
  }
  if(modern && !(v->pics[SKY_PIC].w && v->pics[SKY_PIC].replaced)) {  // (also before any is learned: QA-006)
    // The layers' scrolls (see modern_skyline): the outline at the game's own sky position
    // (eased within the frame by its rate), the ridge by the camera's heading (as many
    // panorama pixels a radian as the screen shows: fixed in the world), the hills and the
    // landmark between; the nearer ones shifted by the camera's place across the road.
    double headPx, game = v->sky_acc[view] + v->sky_rate[view] * v->subframe;
    {
      float raw = atan2f(cam.fwd.z, cam.fwd.x);
      if(!v->head_have[view]) {
        v->head_acc[view] = raw, v->head_have[view] = true;
      } else {
        float d = raw - v->head_raw[view];
        while(d > PI) d -= 2 * PI;
        while(d < -PI) d += 2 * PI;
        v->head_acc[view] += d;
      }
      v->head_raw[view] = raw;
      headPx = v->head_acc[view] * (cam.f / pxs);
    }
    double sideM = v->cam_side[view] / TG_TRACK3D_SIDE_EDGE * tr->scale.half_width;
    double base = 128 - (w / 2.0f) / pxs;
    float lefts[4];
    lefts[SKY_RIDGE] = (float) fmod(headPx + base, SKY_W);
    lefts[SKY_LANDMARK] = 0;  // (the landmark has its own direction in the world: lmx below)
    lefts[SKY_HILLS] = (float) fmod(headPx + (game - headPx) * 0.6 + base + sideM * 0.45, SKY_W);
    lefts[SKY_OUTLINE] = (float) fmod(game + base + sideM * 0.9, SKY_W);
    // The landmark: far off in its direction (lm_place), projected as the world is; Rio's
    // Sugarloaf (lmx2) in its own direction beside it.
    float lmx = -1e9f, lmx2 = -1e9f;
    stage_build(v, tr);
    if(g_lm_on && g_pcourse == v->course) {
      for(int k = 0; k < 2; k++) {
        float az = g_lm_az + (k ? g_lm_side * LM_SUGARLOAF_OFF * LM_HANG : 0);
        V3 d = v3(cosf(az), 0, sinf(az));
        float df = vdot(d, cam.fwd), dr = vdot(d, cam.right);
        if(df > 0.2f) *(k ? &lmx2 : &lmx) = cam.cx + dr / df * cam.f;
      }
    }
    modern_skyline(v, out, w, hy, lefts, pxs, pxs * 0.875f, lmx, lmx2);
  } else if(v->pics[SKY_PIC].w) {
    float left = v->sky_scroll + 128 - (w / 2.0f) / pxs;  // panorama x at the screen's left
    float x = 0;
    for(int pieces = 0; x < w && pieces < 16; pieces++) {
      float pu = fmodf(left + x / pxs, SKY_W);
      if(pu < 0) pu += SKY_W;
      if(SKY_W - pu < 0.01f) pu = 0;   // at the wrap itself: start the next turn (a step of
                                       // almost nothing would never move x on)
      float run = (SKY_W - pu) * pxs;  // screen pixels to the panorama's wrap
      float x1 = x + run < w ? x + run : (float) w;
      float u0 = pu / SKY_W, u1 = (pu + (x1 - x) / pxs) / SKY_W;
      uint32_t tint = modern ? 0xFFD8DEE8 : 0xFFFFFFFF;  // modern: the skyline in the haze
      screen_rect(out, x, hy - bandH, x1, hy + 1, u0, u1, tint, tint, modern && v->pics[SKY_SMOOTH].w && !v->pics[SKY_PIC].replaced ? SKY_SMOOTH : SKY_PIC);
      x = x1;
    }
  }
  if(modern) {
    // The open ground, hazed row by row by how far away it lies (as the bands by the road are).
    float prevY = hy, eyeH = 3.0f;
    uint32_t prevC = fogged(groundHere, 1e6f);
    for(int i = 1; i <= 16; i++) {
      float t = i / 16.0f, y = hy + (h - hy) * t * t;
      uint32_t c = fogged(groundHere, eyeH * cam.f / fmaxf(y - hy, 0.01f));
      screen_rect(out, 0, prevY, (float) w, y + (i < 16), 0, 1, prevC, c, -1);
      prevY = y, prevC = c;
    }
  }
  else screen_rect(out, 0, hy, (float) w, (float) h, 0, 1, groundHere, groundHere, -1);

  // The triangles and items gathered, in buffers kept from one picture to the next (per
  // view): grown from nothing each picture by doubling, they were reallocated and copied four
  // times a picture (~4 MB at 50k triangles) and freed again (QA-030).
  static Gather keep[2];
  Gather g = keep[view & 1];
  g.nt = g.ni = 0;
  g.cam = &cam;
  g_track = tr;
  g_view = v;
  g_view_k = view;
  if(modern) stage_build(v, tr);  // the course's own things, placed once (view3d_stages.inc)
  int eseg = (int) floorf((ppos - back) / TG_TRACK3D_SEG_UNITS);
  // The road, segment by segment: ground bands, kerbs, road and its lines, in the
  // original's colours, alternating with its stripe phase as the original's do.
  static const float across[6] = {-VERGE, -1.12f, -1.0f, 1.0f, 1.12f, VERGE};
  // The ground the car is on: the see-through ground bands beside a stretch of road high
  // above or below it fade out (QA: Stockholm 3900, Copenhagen 3300 - where the road climbs
  // far ahead, its 48 m bands hung in the air, stacked into pale ghosts over the picture).
  const float groundRef = tg_track3d_frame(tr, pk >= 0 ? carPos : ppos, 0).pos.y;
  TgTrackFrame prevFr[6];
  for(int i = -3; i < DRAW_SEGS; i++) {
    int s0 = ((eseg + i) % nseg + nseg) % nseg;
    float p0 = (float) (eseg + i) * TG_TRACK3D_SEG_UNITS, p1 = p0 + TG_TRACK3D_SEG_UNITS;
    V3 a[6], b[6];
    for(int j = 0; j < 6; j++) {
      if(i == -3) prevFr[j] = tg_track3d_frame(tr, p0, across[j] * TG_TRACK3D_SIDE_EDGE);
      TgTrackFrame nf = tg_track3d_frame(tr, p1, across[j] * TG_TRACK3D_SIDE_EDGE);
      a[j] = tv(prevFr[j].pos);
      b[j] = tv(nf.pos);
      prevFr[j] = nf;
    }
    V3 mid = vmul(vadd(vadd(a[2], a[3]), vadd(b[2], b[3])), 0.25f);
    float depth = to_cam(&cam, mid).z;
    if(depth < -12) continue;
    // Far away the road and its stripes fade into the plain ground, as haze.
    begin_item(&g, depth);
    float haze = (depth - 250) / 450;
    haze = haze < 0 ? 0 : haze > 1 ? 1 : haze;
    bool alt = ((tr->tables.stripe[s0] >> 1) & 7) < 4;
    uint32_t gA, gB;  // this stretch's ground (the sand, the grass where the game turns green)
    ground_at(v, s0, ground0, ground1, &gA, &gB);
    if(modern) gA = calm(gA), gB = calm(gB);
    if(g_night) gA = mix(gA, 0xFF0A0E16, 0.84f), gB = mix(gB, 0xFF0A0E16, 0.84f);
    // Slopes catch the light: a climb (facing it) a little lighter, a descent darker.
    float grade = tr->grade[s0], light = 1 + grade * 1.6f;
    light = light < 0.85f ? 0.85f : light > 1.12f ? 1.12f : light;
    if(modern) {
      // Grain-textured verges in the course's ground colours, red and white kerbs, asphalt,
      // all fading into the haze.
      uint32_t vg = fogged(shade(mix(alt ? gA : gB, gA, 0.55f), light), depth);
      uint32_t outerC = fogged(ground0, depth);
      int cells = depth < 12 ? 4 : depth < 40 ? 2 : 1;
      // A textured shoulder by the kerbs, then the band fades out into the open ground.
      V3 m0 = vadd(a[1], vmul(vsub(a[0], a[1]), 0.2f)), m1 = vadd(b[1], vmul(vsub(b[0], b[1]), 0.2f));
      V3 n0 = vadd(a[4], vmul(vsub(a[5], a[4]), 0.2f)), n1 = vadd(b[4], vmul(vsub(b[5], b[4]), 0.2f));
      outerC = with_alpha(vg, 0);  // fading into the open ground, whatever its haze
      float bandK = 1 - (fabsf(mid.y - groundRef) - 3.0f) / 6.0f;  // gone 9 m above or below
      bandK = bandK < 0 ? 0 : bandK > 1 ? 1 : bandK;
      g_alpha = bandK;
      if(bandK > 0.01f) {
        quad_shaded(&g, a[0], m0, m1, b[0], outerC, vg, vg, outerC);
        quad_shaded(&g, n0, a[5], b[5], n1, vg, outerC, outerC, vg);
      }
      g_alpha = 1;
      tex_quad_uv(&g, m1, b[1], a[1], m0, TEX_GRAIN, vg, cells, 0, 1, 0, 1);
      tex_quad_uv(&g, b[4], n1, n0, a[4], TEX_GRAIN, vg, cells, 0, 1, 0, 1);
      uint32_t kerbC = fogged(shade(alt ? 0xFFD62A2A : 0xFFF2F2EE, light), depth);
      quad(&g, a[1], a[2], b[2], b[1], kerbC);
      quad(&g, a[3], a[4], b[4], b[3], kerbC);
      uint32_t asphalt = fogged(shade(mix(road0, 0xFF5C6068, 0.6f), light * (alt ? 1.0f : 0.97f)), depth);
      tex_quad_uv(&g, b[2], b[3], a[3], a[2], TEX_ASPHALT, asphalt, cells, 0, 1, 0, 1);
      // Solid edge lines, dashed lane lines.
      V3 lift = v3(0, 0.012f, 0);
      for(int k = -1; k <= 1; k += 2) {
        float ce = k * 0.93f, lw = 0.018f;
        V3 q0 = tv(tg_track3d_frame(tr, p0, (ce - lw) * TG_TRACK3D_SIDE_EDGE).pos), q1 = tv(tg_track3d_frame(tr, p0, (ce + lw) * TG_TRACK3D_SIDE_EDGE).pos);
        V3 q2 = tv(tg_track3d_frame(tr, p1, (ce + lw) * TG_TRACK3D_SIDE_EDGE).pos), q3 = tv(tg_track3d_frame(tr, p1, (ce - lw) * TG_TRACK3D_SIDE_EDGE).pos);
        quad(&g, vadd(q0, lift), vadd(q1, lift), vadd(q2, lift), vadd(q3, lift), fogged(0xFFEEEEEA, depth));
      }
      if(alt) {
        for(int k = -1; k <= 1; k += 2) {
          float ce = k / 3.0f, lw = 0.02f;
          V3 q0 = tv(tg_track3d_frame(tr, p0, (ce - lw) * TG_TRACK3D_SIDE_EDGE).pos), q1 = tv(tg_track3d_frame(tr, p0, (ce + lw) * TG_TRACK3D_SIDE_EDGE).pos);
          V3 q2 = tv(tg_track3d_frame(tr, p1, (ce + lw) * TG_TRACK3D_SIDE_EDGE).pos), q3 = tv(tg_track3d_frame(tr, p1, (ce - lw) * TG_TRACK3D_SIDE_EDGE).pos);
          quad(&g, vadd(q0, lift), vadd(q1, lift), vadd(q2, lift), vadd(q3, lift), fogged(0xFFF4F4F0, depth));
        }
      }
      // The pit lane's ground and paint, where the game has its pits (view3d_pits.inc).
      pit_ground(&g, tr, s0, p0, depth, light);
      // Small things on the ground near the road: rocks, tufts of grass, lumps of snow, as the
      // course's ground suggests; the same ones in the same places every lap.
      // The open ground in fields: wide bands beside the road, each a patch a little lighter
      // or darker, changing slowly along the course (no flat colour out to the horizon).
      if(depth < 320 && !g_night && bandK > 0.01f) {
        g_alpha = bandK;
        for(int k = -1; k <= 1; k += 2)
          for(int j = 0; j < 3; j++) {
            float in = 1.45f + j * 2.6f, out = in + 2.6f;
            float t0 = (float) s0 / 6.0f, t1 = (float) (s0 + 1) / 6.0f;
            uint32_t seed = (uint32_t) (j * 2 + (k > 0)) * 0x9E3779B1u;
            float pa = 0, pb = 0;
            for(int e = 0; e < 2; e++) {
              float t = e ? t1 : t0;
              int i0 = (int) floorf(t);
              float fr = t - i0;
              fr = fr * fr * (3 - 2 * fr);
              float n0 = (hash32(seed ^ (uint32_t) i0 * 0x85EBCA6Bu) & 255) / 255.0f, n1 = (hash32(seed ^ (uint32_t) (i0 + 1) * 0x85EBCA6Bu) & 255) / 255.0f;
              *(e ? &pb : &pa) = n0 + (n1 - n0) * fr;
            }
            bool grassy = (gA >> 8 & 255) > (gA >> 16 & 255) + 10;
            uint32_t fa = grassy && pa > 0.6f ? mix(gA, 0xFF9AA84E, (pa - 0.6f) * 1.4f) : gA;
            uint32_t fb = grassy && pb > 0.6f ? mix(gA, 0xFF9AA84E, (pb - 0.6f) * 1.4f) : gA;
            uint32_t ca = fogged(with_alpha(shade(fa, (0.78f + 0.4f * pa) * light), 230), depth);
            uint32_t cb2 = fogged(with_alpha(shade(fb, (0.78f + 0.4f * pb) * light), 230), depth);
            V3 lift2 = v3(0, 0.004f, 0);
            V3 i0p = vadd(tv(tg_track3d_frame(tr, p0, k * in * TG_TRACK3D_SIDE_EDGE).pos), lift2);
            V3 o0p = vadd(tv(tg_track3d_frame(tr, p0, k * out * TG_TRACK3D_SIDE_EDGE).pos), lift2);
            V3 i1p = vadd(tv(tg_track3d_frame(tr, p1, k * in * TG_TRACK3D_SIDE_EDGE).pos), lift2);
            V3 o1p = vadd(tv(tg_track3d_frame(tr, p1, k * out * TG_TRACK3D_SIDE_EDGE).pos), lift2);
            uint32_t ia = j == 0 ? with_alpha(ca, 0) : ca, ib = j == 0 ? with_alpha(cb2, 0) : cb2;
            uint32_t oa = j == 2 ? with_alpha(ca, 0) : ca, ob = j == 2 ? with_alpha(cb2, 0) : cb2;
            quad_shaded(&g, i0p, o0p, o1p, i1p, ia, oa, ob, ib);
          }
        g_alpha = 1;
      }
      if(depth < 110) ground_detail(&g, tr, s0, p0, gA, depth);
      if(depth < SCENERY_FAR) scenery(&g, v, tr, s0, p0, depth);
      // Marker posts along both edges, every 10 m: white with a red reflector, as in the
      // concept art.
      if((s0 & 3) == 0 && depth > 1 && depth < 220)
        for(int k = -1; k <= 1; k += 2) {
          if(pit_blocks(tr, s0, k * 1.32f)) continue;  // (the pit lane there: its wall instead)
          TgTrackFrame mf = tg_track3d_frame(tr, p0, k * 1.32f * TG_TRACK3D_SIDE_EDGE);
          V3 mb = tv(mf.pos), mfw = vnorm(v3(mf.forward.x, 0, mf.forward.z)), mr = vnorm(v3(mf.right.x, 0, mf.right.z));
          // (a post across the player's car, or the car run into it, is not drawn over it:
          // QA-029, session-20261102-195339 9936-9942)
          g_alpha = depth < 30 ? occluder_fade(&cam, mb, 0.06f, 1.08f) : 1;
          if(g_alpha < 0.02f) {
            g_alpha = 1;
            continue;
          }
          mbox(&g, mb, mfw, mr, v3(0, 1, 0), 0.05f, 0.05f, 0, 0.95f, fogged(0xFFE8ECEE, depth), 0.02f);
          mbox(&g, mb, mfw, mr, v3(0, 1, 0), 0.055f, 0.055f, 0.95f, 1.08f, fogged(0xFFE0302C, depth), 0.03f);
          g_alpha = 1;
        }
      goto roadside;
    }
    // The ground bands fade to the plain ground at their outer edge (no seam where they end).
    uint32_t verge = shade(mix(alt ? gA : gB, gA, haze), light), outer = gA;
    quad_shaded(&g, a[0], a[1], b[1], b[0], outer, verge, verge, outer);
    quad_shaded(&g, a[4], a[5], b[5], b[4], verge, outer, outer, verge);
    uint32_t kerb = shade(mix(alt ? 0xFFD83030 : 0xFFF0F0F0, ground0, haze * 0.7f), light);
    quad(&g, a[1], a[2], b[2], b[1], kerb);
    quad(&g, a[3], a[4], b[4], b[3], kerb);
    quad(&g, a[2], a[3], b[3], b[2], shade(mix(alt ? road0 : road1, ground0, haze * 0.5f), light));
    if(alt) {
      for(int k = -1; k <= 1; k += 2) {
        float c = k / 3.0f, lw = 0.022f;
        V3 lift = v3(0, 0.01f, 0);
        V3 q0 = tv(tg_track3d_frame(tr, p0, (c - lw) * TG_TRACK3D_SIDE_EDGE).pos);
        V3 q1 = tv(tg_track3d_frame(tr, p0, (c + lw) * TG_TRACK3D_SIDE_EDGE).pos);
        V3 q2 = tv(tg_track3d_frame(tr, p1, (c + lw) * TG_TRACK3D_SIDE_EDGE).pos);
        V3 q3 = tv(tg_track3d_frame(tr, p1, (c - lw) * TG_TRACK3D_SIDE_EDGE).pos);
        quad(&g, vadd(q0, lift), vadd(q1, lift), vadd(q2, lift), vadd(q3, lift), 0xFFF0F0F0);
      }
    }
  roadside:;
    // The segment's roadside object, where the game puts it, at the size the game draws it.
    uint16_t ow = tr->tables.object[s0];
    int key = art_key(ow);
    // (on the pit lane the game's own pit-lane boards and roadside pictures make way for it: its
    // wall and garages stand there; the pit sign stands at the lane's entry, by the road)
    if(modern && pit_hides(tr, s0, ow)) key = -1;
    if(key >= 0 && depth > -2) {
      const ArtInfo* ai = &v->art[key];
      const TgV3Pic* pic = &v->pics[ART_PIC + key];
      // Groups: the start gantry spans the road at the start; others (Japan's banners) are
      // their picture where the game draws them.
      bool start = s0 < 4 || s0 >= tr->tables.segments - 4;
      bool gantryLike = key >= 256 && (v->art_side_n[key] == 0 || !pic->w || ai->n == 0 ? start : (ai->sum_h / ai->n) * pic->w / pic->h > 6);
      if(gantryLike) {
        if(depth > 14) gantry(&g, p0 + 16);  // not when the camera stands under it (after the finish)
      } else if(key >= 256) {
        if(pic->w && ai->n > 0 && v->art_side_n[key] > 0) {
          float oh = ai->sum_h / ai->n, ow_ = oh * pic->w / pic->h;
          TgTrackFrame of = tg_track3d_frame(tr, p0 + 16, v->art_side[key] / v->art_side_n[key]);
          billboard(&g, tv(of.pos), ow_, oh, ART_PIC + key);
        }
      } else if(pic->w && ai->n > 0) {
        float oh = ai->sum_h / ai->n, ow_ = oh * pic->w / pic->h;
        // Off the road by the word's distance, on its side.
        float side = object_side(ow);
        if(modern && (ow & 0xFE00) == 0x7400) side = pit_sign_side(tr, side);  // (the pit sign: at the lane's entry)
        TgTrackFrame of = tg_track3d_frame(tr, p0 + 16, side * TG_TRACK3D_SIDE_EDGE);
        V3 base = tv(of.pos);
        if(modern) {
          float grow;
          g_alpha = object_fade(v, key, &grow);
          // Near the camera: faded out, rather than a wall of leaves filling the picture
          // (the game's own trees are big, and its camera passes close by them); eased.
          static FadeMem objMem[2][TG_TRACK3D_MAX_SEGMENTS];
          g_alpha *= fade_mem(&objMem[view][s0], near_fade(&cam, base, ow_ * grow * 0.5f, oh * grow, true));
          if(g_alpha > 0.01f) modern_object(&g, v, key, base, tv(of.forward), tv(of.right), ow_ * grow, oh * grow, tr->tables.curve[s0]);
          g_alpha = 1;
        }
        else if(look == TG_V3_LOOK_MODELS) art_model(&g, base, tv(of.forward), tv(of.right), ow_, oh, ART_PIC + key, v->art_colour[key]);
        else billboard(&g, base, ow_, oh, ART_PIC + key);
      }
    }
  }
  // The course's own things (view3d_stages.inc), by the segment they were placed on but culled
  // and faded by their own place, not their segment's: a prop 85 m off a bend can be in the
  // picture while its segment is behind the camera, or beyond the road drawn ahead where the
  // loop comes back - cut off with the segment, it vanished or appeared at once (QA-009).
  if(modern) {
    int span = DRAW_SEGS + 300 < nseg ? DRAW_SEGS + 300 : nseg, back = span < 360 ? span / 6 : 60;
    for(int i = -back; i < span - back; i++) stage_props(&g, v, tr, ((eseg + i) % nseg + nseg) % nseg);
    pit_props(&g, v, tr);  // the pit lane's wall, garages and crews (view3d_pits.inc)
  }
  // The models for the twelve nearest cars (a grid full of them would be slow).
  {
    float ds[TG_SCENE_MAX_CARS];
    int nd = 0;
    for(int k = 0; k < v->cur.car_count; k++) {
      float pos, side;
      car_now(v, k, t, &pos, &side);
      float z = to_cam(&cam, tv(tg_track3d_frame(tr, pos, side).pos)).z;
      if(z > -5) ds[nd++] = z;
    }
    for(int a = 0; a < nd; a++)
      for(int b = a + 1; b < nd; b++)
        if(ds[b] < ds[a]) {
          float tmp = ds[a];
          ds[a] = ds[b];
          ds[b] = tmp;
        }
    g_meshCut = 1e9f;  // every car its model (far ones at the light level): no swapping between looks
    (void) nd;
  }
  // Every car, at its own place on the circuit.
  g_alpha = 1;  // (whatever was faded before, the cars are whole)
  float sepA[TG_SCENE_MAX_CARS], sepS[TG_SCENE_MAX_CARS];
  if(modern) sep_room(v, t, false, sepA, sepS, NULL);
  // Where each car is drawn: the game's place, and in the modern look apart from the car it
  // overlaps (sep_room, step_separation; never the players').
  float dpos[TG_SCENE_MAX_CARS], dside[TG_SCENE_MAX_CARS];
  int ncars = v->cur.car_count < TG_SCENE_MAX_CARS ? v->cur.car_count : TG_SCENE_MAX_CARS;
  for(int k = 0; k < ncars; k++) {
    int id = v->cur.cars[k].id & 31;
    float pos, side;
    car_now(v, k, t, &pos, &side);
    if(modern) {
      float st = v->subframe;
      float sl0 = v->sep_slew[id][0], sl1 = v->sep_slew[id][1];  // (the rests: on by their rates)
      float r0 = 1.5f / tr->scale.seg_len * TG_TRACK3D_SEG_UNITS * st, r1 = 0.2f / tr->scale.half_width * TG_TRACK3D_SIDE_EDGE * st;
      sl0 = sl0 > r0 ? sl0 - r0 : sl0 < -r0 ? sl0 + r0 : 0, sl1 = sl1 > r1 ? sl1 - r1 : sl1 < -r1 ? sl1 + r1 : 0;
      pos += sepA[k] + v->sep_ease[id][0][0] + v->sep_ease[id][0][1] * st / 60 + sl0;
      side += sepS[k] + v->sep_ease[id][1][0] + v->sep_ease[id][1][1] * st / 60 + sl1;
      if(pos < 0) pos += len;
      if(pos >= len) pos -= len;
    }
    dpos[k] = pos, dside[k] = side;
  }
  // A car that comes between the chase camera and this view's player's car, hiding a good part
  // of it as seen (car_cover: 30 %, let go under 20 % so a car on the edge doesn't flicker; a
  // rival held behind it after a rear push hides half or more, one in the next lane over its
  // corner a sixth and stays solid), is seen through by screen door, 10 pixels of 16 left out,
  // as the props that hide it fade (occluder_fade): see-through while it hides the car, solid
  // otherwise, nothing in between but the easing (fade_mem_rates: out in ~3 game frames, solid
  // again in ~6, ~0.1 s; the share left out rounded down), so a car is never held at a faint
  // sprinkle of holes. Screen door, not alpha blending: a car is a closed
  // model whose triangles cross each other, blended it would show its inside and sort badly;
  // dithered, what is drawn of it is solid, its near side hides its far side as ever and nothing
  // needs sorting. The player's own car and the camera are untouched; each view fades for its
  // own player. Only with the camera on the car (not the race start's fly-in over the grid, nor
  // the finish, where the car drives off 400 m from the eye).
  int door[TG_SCENE_MAX_CARS] = {0};
  if(modern && chase && pk < ncars) {
    static FadeMem coverMem[2][32];
    static bool hides[2][32];
    bool onCar = ppos == carPos;  // (else solid, eased: no pop as the fly-in reaches the car)
    int pid0 = v->cur.cars[pk].id & 31;
    CarBox pb = car_box(tg_track3d_frame(tr, dpos[pk], dside[pk]), v->car_yaw[pid0] + v->sep_yaw[pid0]);
    float pz = to_cam(&cam, pb.c).z;
    for(int k = 0; k < ncars; k++) {
      if(k == pk) continue;
      int id = v->cur.cars[k].id & 31;
      CarBox ob = car_box(tg_track3d_frame(tr, dpos[k], dside[k]), v->car_yaw[id] + v->sep_yaw[id]);
      float oz = to_cam(&cam, ob.c).z, cover = 0;
      if(onCar && oz < pz + 2 && oz > -6) cover = car_cover(cam.eye, &pb, &ob);  // (beyond the player's car: hides none of it)
      bool* on = &hides[view][id];
      if(v->frames - coverMem[view][id].at > 30) *on = false;  // (not seen for a while: afresh)
      *on = cover >= (*on ? 0.2f : 0.3f);
      float a = fade_mem_rates(&coverMem[view][id], *on ? 0.37f : 1, 0.6f, 0.32f);
      door[k] = (int) ((1 - a) * 16);  // (0.37: 10 of 16 left out)
      if(car_trace() && (cover > 0 || door[k]))  // CF frame view id: share of the player's car it hides, drawn opacity, door
        fprintf(stderr, "CF %u %d %d cover %.2f alpha %.2f door %d\n", v->frames, view, id, cover, a, door[k]);
    }
  }
  for(int k = 0; k < ncars; k++) {
    if(k == pk && camMode == TG_V3_CAM_BUMPER) continue;
    int id = v->cur.cars[k].id & 31;
    float pos = dpos[k], side = dside[k];
    float d = pos - (ppos - back);
    while(d < -len / 2) d += len;
    while(d > len / 2) d -= len;
    if(d < -64 || d > DRAW_SEGS * TG_TRACK3D_SEG_UNITS) continue;
    TgTrackFrame cf = tg_track3d_frame(tr, pos, side);
    // A car the game hasn't shown yet borrows another's picture (they are all cars seen
    // from behind), at a car's width, until its own comes.
    int pid = id;
    for(int k2 = 0; k2 < 32 && (v->pics[CAR_PIC + pid].w == 0 || v->car[pid].mpp == 0); k2++) pid = (pid + 1) & 31;
    const TgV3Pic* pic = &v->pics[CAR_PIC + pid];
    if(modern) {
      // (eased towards the heading the game's motion gives: step_bodies, once a game frame)
      bool nitro = (k == v->cur.view_car[0] && v->nitro[0]) || (id == 0x1F && v->nitro[1]);
      // Along the road where it is, turned a little into a lane change. The car the camera
      // follows too: it used to point where the camera looks, which made up for the frame's
      // heading being off the drawn road (track3d.c); now the frame is the road's, and a car
      // pointing at the camera's chord points off the road in a bend.
      float yaw = v->car_yaw[id] + v->sep_yaw[id];
      g_door = door[k];
      float metres = pos / TG_TRACK3D_SEG_UNITS * tr->scale.seg_len;
      g_car_lift = pit_lift(v, id, &metres);  // (in its pit box: on the jacks, the wheel guns turning its wheels)
      modern_car(&g, v, cf, id, v->cur.cars[k].kind, v->car_brake[id] > 0, metres, yaw, nitro, v->ticks);
      g_car_lift = 0;
      g_door = 0;
    } else if(look == TG_V3_LOOK_MODELS && v->pics[REAR_PIC + id].w) {
      car_model(&g, v, cf, id);
    } else if(pic->w && v->car[pid].mpp > 0) {
      float mpp = pid == id ? v->car[id].mpp : CAR_WIDTH / pic->w;
      billboard(&g, tv(cf.pos), pic->w * mpp, pic->h * mpp, CAR_PIC + pid);
    } else if(look == TG_V3_LOOK_MODELS) {
      car_model(&g, v, cf, id);
    }
  }
  if(modern) modern_knock(&g, v, t, sepA, sepS);
  qsort(g.it, (size_t) g.ni, sizeof(Item), cmp_item);
  for(int i = 0; i < g.ni; i++)
    for(int j = 0; j < g.it[i].count; j++) {
      const Tri* tri = &g.t[g.it[i].first + j];
      list_push_door(out, tri->v, 3, tri->pic, tri->door);
    }
  keep[view & 1] = g;  // (kept for the next picture: no free)
  // The game's HUD on top, at the original's pixel size, in zones: its top lines (the map,
  // the speedometer) to the top corners of the screen; below them, the gearbox and nitro to
  // the bottom left corner, the messages kept whole in the middle, position and fuel to
  // the bottom right. Lines 0-3 are the border between the original's two views, left out.
  if(modern) pit_marker(v, view, &cam, carPos, pside);  // where the pit entry is on the screen, for the HUD
  out->overlay = true;  // from here: the HUD
  bool ownHud = modern;
  if(ownHud) modern_hud(v, view, out, w, h);
  if(v->pics[HUD_PIC].w) {
    float sx = (h * 4.0f / 3.0f) / 256.0f, sy = h / 224.0f;
    float cx = w / 2.0f;
    // HUD x0, x1, y0, y1; on screen: hx -1 left edge, 0 centred, 1 right edge; vy 0 top
    // edge, 1 bottom edge.
    static const struct { float x0, x1, y0, y1; int hx, vy; } kZones[] = {
      // The map; the speedometer and its rev bar, then the timer under it; the gearbox and
      // nitro; the middle, where the messages are (lines 30 on, so they stay whole); the
      // position and fuel.
      {0, 64, 4, 44, -1, 0}, {156, 256, 4, 30, 1, 0}, {180, 256, 30, 44, 1, 0},
      {0, 64, 44, HUD_H, -1, 1}, {64, 180, 30, HUD_H, 0, 1}, {180, 256, 44, HUD_H, 1, 1},
    };
    for(size_t z = 0; z < sizeof(kZones) / sizeof(kZones[0]); z++) {
      if(ownHud && z != 4) continue;  // only the game's messages: the rest is drawn above
      if(ownHud) {
        char mb[24];
        g_msg_player = view;
        if(v->msg_id[view] == 0xFFFF) continue;  // no message: nothing of the game's to show
        if(message_text(v->msg_id[view], mb, sizeof(mb)) || taunt_up(v->msg_id[view])) continue;  // drawn above in the HUD's own font
      }
      hud_hi_ready(v);
      float zw = (kZones[z].x1 - kZones[z].x0) * sx, zh = (kZones[z].y1 - kZones[z].y0) * sy;
      float x = kZones[z].hx < 0 ? 0 : kZones[z].hx > 0 ? w - zw : cx - (128 - kZones[z].x0) * sx;
      float y = kZones[z].vy == 0 ? (kZones[z].y0 - 4) * sy : h - (HUD_H - kZones[z].y0) * sy;
      int first = out->nv;
      float off = view ? HUD_VIEW2 : 0;  // the bottom view's HUD lines
      screen_rect(out, x, y, x + zw, y + zh, kZones[z].x0 / 256, kZones[z].x1 / 256, 0xFFFFFFFF, 0xFFFFFFFF,
                  v->pics[HUD_HI].w ? HUD_HI : HUD_PIC);  // the fonts upscaled and smooth
      for(int k = first; k < out->nv; k++)
        out->v[k].v = (out->v[k].v < 0.5f ? kZones[z].y0 + off : kZones[z].y1 + off) / HUD_ROWS;
    }
  }
  // The game's fade (its screen brightness): the race fades in from black and out to it as
  // the original does, not lit at once over the original's black frames (QA-005). Each view
  // its own ($52 the top view's, $65 the bottom's: a 2-player race's views fade apart); not the
  // fades round a view's own pit scene (the modern look shows the stop in the race: QA-024, and
  // the black frame and dark exit of the pit stop).
  int bright = view && (v->two || v->demo) ? v->bright2 : v->bright;
  if(v->pit_hold[view] > 0 && (view == 0 || v->two || v->demo)) bright = 15;
  if(bright < 15) {
    uint32_t a = (uint32_t) ((15 - bright) * 255 / 15);
    screen_rect(out, 0, 0, (float) w, (float) h, 0, 1, a << 24, a << 24, -1);
  }
}

// One car on its own, for looking at the model: car `type` (0-3, the select screen's) in its
// colours, seen from `yaw` degrees round it (0 from behind) and `pitch` degrees above.
void tg_view3d_showcase(TgView3D* v, int type, float yaw, float pitch, int w, int h, TgV3List* out) {
  tg_v3_list_clear(out);
  Cam cam;
  float a = yaw * PI / 180, p = pitch * PI / 180, dist = 7.5f;
  V3 target = v3(0, 0.55f, 0);
  cam.eye = vadd(target, v3(sinf(a) * cosf(p) * dist, sinf(p) * dist, -cosf(a) * cosf(p) * dist));
  cam.fwd = vnorm(vsub(target, cam.eye));
  cam.right = vnorm(vcross(cam.fwd, v3(0, 1, 0)));
  cam.up = vcross(cam.right, cam.fwd);
  cam.f = (h * 4.0f / 3.0f) / 2 / tanf(40 * PI / 360);
  cam.cx = w / 2.0f, cam.cy = h / 2.0f, cam.w = (float) w, cam.h = (float) h;
  g_haze = 0xFFE8ECF0;
  screen_rect(out, 0, 0, (float) w, (float) h * 0.55f, 0, 1, 0xFF5F8FD0, 0xFFDCE6F0, -1);
  screen_rect(out, 0, (float) h * 0.55f - 1, (float) w, (float) h, 0, 1, 0xFF8A8C90, 0xFF5A5C60, -1);
  Gather g = {0};
  g.cam = &cam;
  // A floor under it.
  begin_item(&g, 1e5f);
  quad(&g, v3(-6, 0, -6), v3(6, 0, -6), v3(6, 0, 6), v3(-6, 0, 6), 0xFF6A6C70);
  g_showcase = true;
  uint8_t keep = v->ptype[0];
  v->ptype[0] = (uint8_t) (type >= 0 && type <= 4 ? type : type & 3);  // (4: the rivals' fifth car, amber)
  // (right = forward x up, as the race's frames: the world is right-handed; (1, 0, 0) drew the
  // car mirrored, unseen until the cars had lettering)
  V3 f = v3(0, 0, 1), r = v3(-1, 0, 0), u = v3(0, 1, 0);
  car_shadow(&g, v3(0, 0, 0), f, r, 2.5f, 1.2f, 0.45f);
  float depth = to_cam(&cam, v3(0, 0, 0)).z;
  g_paint = getenv("TG_SHOWCASE_PAINT") ? 0xFF000000u | (uint32_t) strtoul(getenv("TG_SHOWCASE_PAINT"), NULL, 16) : 0;  // a rival's paint
  g_door = getenv("TG_SHOWCASE_DOOR") ? atoi(getenv("TG_SHOWCASE_DOOR")) & 15 : 0;  // seen through (tools/qa/gl_check.py)
  bool drawnS = mesh_car(&g, v, 0, v3(0, 0, 0), f, r, u, depth, false, false, 0);
  g_paint = 0;
  if(!drawnS) clean_car(&g, v, 0, v3(0, 0, 0), f, r, u, depth, 0, 0, false, false, 0, 0);
  g_door = 0;
  v->ptype[0] = keep;
  g_showcase = false;
  qsort(g.it, (size_t) g.ni, sizeof(Item), cmp_item);
  for(int i = 0; i < g.ni; i++)
    for(int j = 0; j < g.it[i].count; j++) list_push_door(out, g.t[g.it[i].first + j].v, 3, g.t[g.it[i].first + j].pic, g.t[g.it[i].first + j].door);
  free(g.t);
  free(g.it);
}

// --- The modern look's HUD: drawn here, from the game's values ----------------------------
// A racing font of 14 segments (slanted, drawn as clean bars), dark glass panels, a speed
// sweep, gear, nitro pills, position, a fuel gauge and the course map from the 3D track.

enum { SA = 1, SB = 2, SC = 4, SD = 8, SE = 16, SF = 32, SG1 = 64, SG2 = 128, SH = 256, SI = 512, SJ = 1024, SK = 2048, SL = 4096,
       SM = 8192 };
static const float kSeg[14][4] = {
  {0, 0, 1, 0}, {1, 0, 1, .5f}, {1, .5f, 1, 1}, {0, 1, 1, 1}, {0, .5f, 0, 1}, {0, 0, 0, .5f}, {0, .5f, .5f, .5f},
  {.5f, .5f, 1, .5f}, {0, 0, .5f, .5f}, {.5f, 0, .5f, .5f}, {1, 0, .5f, .5f}, {0, 1, .5f, .5f}, {.5f, .5f, .5f, 1}, {1, 1, .5f, .5f}};

static int seg_of(char ch) {
  switch(ch) {
    case '0': return SA | SB | SC | SD | SE | SF | SJ | SK;
    case '1': return SI | SL;
    case '2': return SA | SB | SG1 | SG2 | SE | SD;
    case '3': return SA | SB | SC | SD | SG2;
    case '4': return SF | SG1 | SG2 | SB | SC;
    case '5': return SA | SF | SG1 | SG2 | SC | SD;
    case '6': return SA | SF | SG1 | SG2 | SC | SD | SE;
    case '7': return SA | SB | SC;
    case '8': return SA | SB | SC | SD | SE | SF | SG1 | SG2;
    case '9': return SA | SB | SC | SD | SF | SG1 | SG2;
    case 'A': return SA | SB | SC | SE | SF | SG1 | SG2;
    case 'B': return SA | SB | SC | SD | SG2 | SI | SL;
    case 'C': return SA | SD | SE | SF;
    case 'D': return SA | SB | SC | SD | SI | SL;
    case 'E': return SA | SD | SE | SF | SG1 | SG2;
    case 'F': return SA | SE | SF | SG1;
    case 'G': return SA | SC | SD | SE | SF | SG2;
    case 'H': return SB | SC | SE | SF | SG1 | SG2;
    case 'I': return SA | SD | SI | SL;
    case 'K': return SE | SF | SG1 | SJ | SM;
    case 'L': return SD | SE | SF;
    case 'M': return SB | SC | SE | SF | SH | SJ;
    case 'N': return SB | SC | SE | SF | SH | SM;
    case 'O': return SA | SB | SC | SD | SE | SF;
    case 'P': return SA | SB | SE | SF | SG1 | SG2;
    case 'R': return SA | SB | SE | SF | SG1 | SG2 | SM;
    case 'S': return SA | SC | SD | SF | SG1 | SG2;
    case 'T': return SA | SI | SL;
    case 'U': return SB | SC | SD | SE | SF;
    case 'X': return SH | SJ | SK | SM;
    case '/': return SJ | SK;
    case '-': return SG1 | SG2;
    default: return 0;
  }
}

static void sq(TgV3List* l, float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, uint32_t c) {
  TgV3Vertex q[6] = {{x0, y0, 0, 0, c}, {x1, y1, 0, 0, c}, {x2, y2, 0, 0, c}, {x0, y0, 0, 0, c}, {x2, y2, 0, 0, c}, {x3, y3, 0, 0, c}};
  list_push(l, q, 6, -1);
}

// A bar from (x0, y0) to (x1, y1), t thick, its ends cut at 45 degrees.
static void bar(TgV3List* l, float x0, float y0, float x1, float y1, float t, uint32_t c) {
  float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
  if(len < 0.01f) return;
  dx /= len, dy /= len;
  float nx = -dy * t / 2, ny = dx * t / 2, e = t * 0.35f;
  x0 += dx * e, y0 += dy * e, x1 -= dx * e, y1 -= dy * e;
  sq(l, x0 - dx * t / 2, y0 - dy * t / 2, x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 + dx * t / 2, y1 + dy * t / 2, c);
  sq(l, x0 - dx * t / 2, y0 - dy * t / 2, x1 + dx * t / 2, y1 + dy * t / 2, x1 - nx, y1 - ny, x0 - nx, y0 - ny, c);
}

// Text in the racing font: size = cap height in pixels; align -1 left, 0 centre, 1 right.
static float text_width(const char* str, float size) { return (float) strlen(str) * size * 0.78f - size * 0.18f + size * 0.16f; }

static void text(TgV3List* l, const char* str, float x, float y, float size, uint32_t col, int align) {
  float w = text_width(str, size), cw = size * 0.6f, t = size * 0.13f, slant = size * 0.16f;
  if(align == 0) x -= w / 2;
  else if(align > 0) x -= w;
  for(int pass = 0; pass < 2; pass++) {
    float px = x + (pass ? 0 : size * 0.06f), py = y + (pass ? 0 : size * 0.08f);
    uint32_t c = pass ? col : 0x90000000u;
    for(const char* p = str; *p; p++, px += size * 0.78f) {
      if(*p == ':' || *p == '.') {
        float dx = px + cw * 0.15f;
        if(*p == ':') bar(l, dx + slant * 0.6f, py + size * 0.3f, dx + slant * 0.6f + t, py + size * 0.3f, t, c);
        bar(l, dx, py + size * 0.92f, dx + t, py + size * 0.92f, t, c);
        px -= size * 0.4f;
        continue;
      }
      int m = seg_of(*p >= 'a' && *p <= 'z' ? *p - 32 : *p);
      for(int k = 0; k < 14; k++) {
        if(!(m & (1 << k))) continue;
        const float* sgm = kSeg[k];
        float ax = px + sgm[0] * cw + (1 - sgm[1]) * slant, ay = py + sgm[1] * size;
        float bx = px + sgm[2] * cw + (1 - sgm[3]) * slant, by = py + sgm[3] * size;
        bar(l, ax, ay, bx, by, t, c);
      }
    }
  }
}

// A dark glass panel with a coloured edge on one side.
static void panel(TgV3List* l, float x0, float y0, float x1, float y1, uint32_t accent, int edge) {
  float sk = (y1 - y0) * 0.18f;  // slanted ends, like the font
  sq(l, x0 + sk, y0, x1 + sk, y0, x1, y1, x0, y1, 0x9A0C1218);
  sq(l, x0 + sk, y0, x1 + sk, y0, x1 + sk * 0.9f, y0 + 2, x0 + sk * 0.9f, y0 + 2, 0x40FFFFFF);
  if(edge < 0) sq(l, x0 + sk, y0, x0 + sk + 4, y0, x0 + 4, y1, x0, y1, accent);
  else sq(l, x1 + sk - 4, y0, x1 + sk, y0, x1, y1, x1 - 4, y1, accent);
}


// Player 1's race message as text (the game's message ids, $06:DF16), or NULL when it is a
// picture or the player's name (the game's own drawing shows those).
static const char* message_text(uint16_t id, char* buf, size_t n) {
  static const char* kOrd[4] = {"TH", "ST", "ND", "RD"};
  if(id <= 0x13) {
    int p = id + 1, k = p % 10 < 4 && (p < 11 || p > 13) ? p % 10 : 0;
    snprintf(buf, n, "%d%s", p, kOrd[k]);
    return buf;
  }
  if(id == 0x14) return "FINAL LAP";
  if(id >= 0x15 && id <= 0x1C) {
    snprintf(buf, n, "%d LAPS LEFT", id - 0x14 + 1);
    return buf;
  }
  if(id == 0x1D) return "FUEL LOW";
  if(id == 0x1E) return "OUT OF FUEL";
  if(id >= 0x1F && id <= 0x21) {
    snprintf(buf, n, "%d", 0x22 - id);
    return buf;
  }
  if(id == 0x22) return "GO";
  if(id == 0x23) return "RACE OVER";
  // The name over the car at the start: ids $37-$3E by its length (RaceSetupNames), the
  // banner's name ("COMPUTER" for the rival's view, "DEMO" in the attract demo).
  if(id >= 0x37 && id <= 0x3E && g_banner_name[g_msg_player][0]) return g_banner_name[g_msg_player];
  if(id == 0x7E) return "<< PITS";
  if(id == 0x7F) return "PITS >>";
  return NULL;
}

// The rivals' taunts the original shows as speech bubbles in the HUD ($24-$36). Each is a
// small picture in the ROM: 64-byte blocks of 4-bit tiles in bank $02, listed at $06:DC84
// (as RaceUploadMessageTiles streams them to VRAM $0620, tile $62 on), laid out by the
// message's tile map at $06:DF16: a bubble in one colour, its outline and its words in
// another. The modern bubble shows the words as the game drew them, read from the player's
// ROM here: the outline's colour where it is not joined to the outside of the bubble.
// Nothing of the picture is kept in the source.
#define TAUNT_W 48
#define TAUNT_H 32
typedef struct {
  bool done;
  int n;                         // ink pixels (0: nothing to draw, the game's picture shows)
  int x0, y0, x1, y1;            // the ink's box
  uint8_t ink[TAUNT_H][TAUNT_W];
} TauntInk;

static const TauntInk* taunt_ink(uint16_t id) {
  static TauntInk cache[0x37 - 0x24];
  if(id < 0x24 || id > 0x36) return NULL;
  TauntInk* t = &cache[id - 0x24];
  if(t->done) return t->n ? t : NULL;
  t->done = true;
  // The tiles as uploaded: up to 8 blocks of two tiles each.
  static uint8_t tiles[16][32];
  int ntiles = 0;
  uint16_t x = rom16(0x06DC84 + (uint32_t) id * 2);
  if(x < 0x8000) return NULL;
  for(int b = 0; b < 8; b++, x = (uint16_t) (x + 2)) {
    uint16_t a = rom16(0x060000 | x);
    if(a < 0x8000 || a > 0xFFC0) break;
    memcpy(tiles[ntiles], tg_rom(0x020000 | a), 32);
    memcpy(tiles[ntiles + 1], tg_rom(0x020000 | a) + 32, 32);
    ntiles += 2;
  }
  // The picture, placed by the tile map: rows of [VRAM address, count, entries].
  uint8_t px[TAUNT_H][TAUNT_W];
  memset(px, 0, sizeof(px));
  uint16_t y = rom16(0x06DF16 + (uint32_t) id * 2), first = 0;
  bool any = false;
  for(int row = 0; row < 8; row++) {
    uint16_t adr = rom16(0x060000 | y);
    if(adr == 0) break;
    uint16_t cnt = rom16(0x060000 | (uint16_t) (y + 2));
    if(!any) first = adr, any = true;
    int ty = (int) ((adr >> 5) - (first >> 5)), tx0 = (int) (adr & 31) - (int) (first & 31);
    y = (uint16_t) (y + 4);
    for(int c = 0; c < cnt && c < 16; c++, y = (uint16_t) (y + 2)) {
      uint16_t e = rom16(0x060000 | y);
      int tile = (int) (e & 0x3FF) - 0x62, tx = tx0 + c;
      if(tile < 0 || tile >= ntiles || tx < 0 || ty < 0 || tx * 8 + 8 > TAUNT_W || ty * 8 + 8 > TAUNT_H) continue;
      bool hf = e & 0x4000, vf = e & 0x8000;
      const uint8_t* d = tiles[tile];
      for(int r = 0; r < 8; r++)
        for(int q = 0; q < 8; q++) {
          int sh = 7 - q, rr = vf ? 7 - r : r;
          int k = (d[2 * rr] >> sh & 1) | (d[2 * rr + 1] >> sh & 1) << 1 | (d[16 + 2 * rr] >> sh & 1) << 2 | (d[17 + 2 * rr] >> sh & 1) << 3;
          px[ty * 8 + r][tx * 8 + (hf ? 7 - q : q)] = (uint8_t) k;
        }
    }
  }
  // The outline's colour: the commonest next to the outside (a transparent pixel or the edge).
  int votes[16] = {0};
  for(int r = 0; r < TAUNT_H; r++)
    for(int q = 0; q < TAUNT_W; q++) {
      if(!px[r][q]) continue;
      bool out = r == 0 || q == 0 || r == TAUNT_H - 1 || q == TAUNT_W - 1 || !px[r - 1][q] || !px[r + 1][q] || !px[r][q - 1] || !px[r][q + 1];
      if(out) votes[px[r][q]]++;
    }
  int line = 0;
  for(int k = 1; k < 16; k++)
    if(votes[k] > votes[line]) line = k;
  if(!line) return NULL;
  // The outline: that colour joined (side by side) to the outside; the rest of it is words.
  static uint8_t seen[TAUNT_H][TAUNT_W];
  static int stack[TAUNT_H * TAUNT_W];
  memset(seen, 0, sizeof(seen));
  int sp = 0;
  for(int r = 0; r < TAUNT_H; r++)
    for(int q = 0; q < TAUNT_W; q++) {
      if(px[r][q] != line) continue;
      bool out = r == 0 || q == 0 || r == TAUNT_H - 1 || q == TAUNT_W - 1 || !px[r - 1][q] || !px[r + 1][q] || !px[r][q - 1] || !px[r][q + 1];
      if(out) seen[r][q] = 1, stack[sp++] = r * TAUNT_W + q;
    }
  while(sp > 0) {
    int p = stack[--sp], r = p / TAUNT_W, q = p % TAUNT_W;
    static const int kd[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    for(int i = 0; i < 4; i++) {
      int rr = r + kd[i][0], qq = q + kd[i][1];
      if(rr < 0 || qq < 0 || rr >= TAUNT_H || qq >= TAUNT_W || seen[rr][qq] || px[rr][qq] != line) continue;
      seen[rr][qq] = 1, stack[sp++] = rr * TAUNT_W + qq;
    }
  }
  t->x0 = TAUNT_W, t->y0 = TAUNT_H, t->x1 = -1, t->y1 = -1;
  for(int r = 0; r < TAUNT_H; r++)
    for(int q = 0; q < TAUNT_W; q++) {
      t->ink[r][q] = px[r][q] == line && !seen[r][q];
      if(!t->ink[r][q]) continue;
      t->n++;
      t->x0 = q < t->x0 ? q : t->x0, t->x1 = q > t->x1 ? q : t->x1;
      t->y0 = r < t->y0 ? r : t->y0, t->y1 = r > t->y1 ? r : t->y1;
    }
  return t->n ? t : NULL;
}

static bool taunt_up(uint16_t id) { return taunt_ink(id) != NULL; }

// Whether the HUD's message (if it is one the modern HUD draws) shows now: up, past its
// delay, in the lit half of its blink.
static bool message_showing(const TgView3D* v, int pl) {
  // (the dark half of the blink only hides a blinking message: messages.c draws a steady one
  // once and leaves it up through its count)
  return v->msg_id[pl] < 0xFFFE && v->msg_delay[pl] == 0 && v->msg_blink[pl] != 0 &&
         !((v->msg_blink[pl] & 0x8000) && (v->msg_flags[pl] & 0x8000));
}

// The HUD's font: Lato from the baked atlas. face 0 bold, 1 regular, 2 black italic.
static void font_ready(TgView3D* v) {
  if(v->pics[FONT_PIC].w) return;
  uint32_t* px = malloc((size_t) FONT_AW * FONT_AH * 4);
  if(!px) return;
  size_t o = 0;
  for(unsigned i = 0; i < kFontPackedLen && o < (size_t) FONT_AW * FONT_AH; i++) {
    if(kFontPacked[i] == 0 && i + 1 < kFontPackedLen) {
      for(int k = 0; k < kFontPacked[i + 1] && o < (size_t) FONT_AW * FONT_AH; k++) px[o++] = 0x00FFFFFF;
      i++;
    } else {
      px[o++] = (uint32_t) kFontPacked[i] << 24 | 0xFFFFFF;
    }
  }
  while(o < (size_t) FONT_AW * FONT_AH) px[o++] = 0x00FFFFFF;
  set_pic(&v->pics[FONT_PIC], FONT_AW, FONT_AH, px, FONT_AW);
  v->pics[FONT_PIC].smooth = true;
  free(px);
}

static int glyph_of(int face, char ch) {
  int c = (unsigned char) ch;
  if(c < 32 || c > 126) c = '?';
  return face * 95 + (c - 32);
}

// Width of text at cap size `size` (pixels of the font's 64 px em scaled), with letter spacing.
static float ftext_w(const char* str, float size, int face, float track) {
  float k = size / FONT_SIZE, w = 0;
  for(const char* p = str; *p; p++) w += kFontGlyphs[glyph_of(face, *p)].adv * k + track;
  return *str ? w - track : 0;
}

// Text: y is the baseline; align -1 left, 0 centre, 1 right. track: extra space between letters.
static void ftext(TgV3List* l, const char* str, float x, float y, float size, uint32_t col, int align, int face, float track) {
  float k = size / FONT_SIZE, w = ftext_w(str, size, face, track);
  if(align == 0) x -= w / 2;
  else if(align > 0) x -= w;
  for(const char* p = str; *p; p++) {
    int gi = glyph_of(face, *p);
    float gw = kFontGlyphs[gi].w, gh = kFontGlyphs[gi].h;
    if(gw > 0 && gh > 0) {
      float x0 = x + kFontGlyphs[gi].left * k, y0 = y + kFontGlyphs[gi].top * k, x1 = x0 + gw * k, y1 = y0 + gh * k;
      float u0 = (kFontGlyphs[gi].x + 0.5f) / FONT_AW, v0 = (kFontGlyphs[gi].y + 0.5f) / FONT_AH;
      float u1 = (kFontGlyphs[gi].x + gw - 0.5f) / FONT_AW, v1 = (kFontGlyphs[gi].y + gh - 0.5f) / FONT_AH;
      TgV3Vertex q[6] = {{x0, y0, u0, v0, col}, {x1, y0, u1, v0, col}, {x1, y1, u1, v1, col},
                         {x0, y0, u0, v0, col}, {x1, y1, u1, v1, col}, {x0, y1, u0, v1, col}};
      list_push(l, q, 6, FONT_PIC);
    }
    x += kFontGlyphs[gi].adv * k + track;
  }
}

// A rounded panel: dark navy glass with a faint edge.
static void rpanel(TgV3List* l, float x0, float y0, float x1, float y1, float r, uint32_t fill) {
  enum { SEG = 6 };
  float cx[4] = {x1 - r, x0 + r, x0 + r, x1 - r}, cy[4] = {y0 + r, y0 + r, y1 - r, y1 - r};
  float pts[4 * (SEG + 1)][2];
  int n = 0;
  for(int c = 0; c < 4; c++)
    for(int i = 0; i <= SEG; i++) {
      float a = (c * 90 + i * 90.0f / SEG) * PI / 180;
      pts[n][0] = cx[c] + cosf(a) * r, pts[n][1] = cy[c] - sinf(a) * r;
      n++;
    }
  float mx = (x0 + x1) / 2, my = (y0 + y1) / 2;
  for(int i = 0; i < n; i++) {
    int j = (i + 1) % n;
    TgV3Vertex q[3] = {{mx, my, 0, 0, fill}, {pts[i][0], pts[i][1], 0, 0, fill}, {pts[j][0], pts[j][1], 0, 0, fill}};
    list_push(l, q, 3, -1);
  }
  uint32_t edge = 0x30A9D2E0;
  for(int i = 0; i < n; i++) {
    int j = (i + 1) % n;
    bar(l, pts[i][0], pts[i][1], pts[j][0], pts[j][1], 1.2f, edge);
  }
}

static void rect(TgV3List* l, float x0, float y0, float x1, float y1, uint32_t c) { sq(l, x0, y0, x1, y0, x1, y1, x0, y1, c); }

// The countries in the game's order ($1F06 bits 2-4), and each one's light as in the
// concept art: sky at the top, glow at the horizon.
static const struct CountryLight kCountry[8] = {
  {"UNITED STATES", 0xFF31315D, 0xFFFFAA72}, {"SOUTH AMERICA", 0xFF265A78, 0xFFD9CCB2}, {"JAPAN", 0xFF0C1536, 0xFF7D446D},
  {"GERMANY", 0xFF2D506B, 0xFFC3D8CC},       {"SCANDINAVIA", 0xFF20445F, 0xFFC2D7E0},   {"FRANCE", 0xFF235375, 0xFFEEC69B},
  {"ITALY", 0xFF3C5D7D, 0xFFFCCB8D},         {"UNITED KINGDOM", 0xFF253547, 0xFFB0AEA6},
};

static void list_fade(TgV3List* l, int from, float alpha, float dy);

// The pit stop on the HUD (broadcast pit graphics and racing games' pit HUDs: the stage in steps,
// one big fuel gauge with its number, the stationary time, the lollipop's word): PIT ENTRY, BOX,
// REFUELLING, GO, following the stop as drawn (view3d_pits.inc), so GO comes with the lollipop.
// On the way to the pits (on their side of the road, or short of fuel): where the entry is - a
// marker over it on the screen (an arrow at the edge when it is off it) and the distance.
static void pit_hud(const TgView3D* v, int pl, TgV3List* l, int w, int h, float s) {
  const uint32_t white = 0xFFF0F5F7, muted = 0xFF9CABB9, cyan = 0xFF62F2DC, gold = 0xFFE6B866, red = 0xFFFF5A48, green = 0xFF38D878;
  char buf[48];
  const TgV3Hud* hd = &v->hudv[pl];
  int vk = v->cur.view_car[pl];
  const PitCar* c = vk >= 0 ? pit_car(v, v->cur.cars[vk].id) : NULL;
  float seg = v->track.scale.seg_len, u = v->pit_sub;
  if(c) {
    double sBox = pit_box_s(&v->track, c->box);
    int stage = c->phase == PIT_IN ? (c->s < sBox - 15 ? 0 : 1) : c->phase == PIT_BOX ? 2 : 3;
    float alpha = fminf(1, (c->t + u) / 10.0f);
    if(c->phase == PIT_JOIN) alpha *= 1 - sat01(((float) c->s - PR_WALL1 * seg) / 30);
    if(alpha <= 0.01f) return;
    int first = l->nv;
    float pw = 760 * s, ph = 240 * s, px0 = w / 2.0f - pw / 2, py0 = 232 * s, px1 = px0 + pw, py1 = py0 + ph;
    cpanel(l, px0, py0, px1, py1, 24 * s, 0xEC102230, 0xEC0A141E, cyan, s);
    rect(l, px0, py0 + 18 * s, px0 + 6 * s, py1 - 18 * s, gold);
    ftext(l, "PIT STOP", px0 + 34 * s, py0 + 62 * s, 48 * s, white, -1, 2, 0);
    // The stop's times: standing in the box, and in the pits.
    float stand = c->t_box > 0 ? (c->phase == PIT_BOX ? c->t_box + u : (float) c->t_box) / 60.0f : 0;
    snprintf(buf, sizeof(buf), "%.1f", stand);
    float tx = px1 - 196 * s;
    ftext(l, "STATIONARY", tx, py0 + 30 * s, 12 * s, muted, -1, 0, 2 * s);
    ftext(l, buf, tx, py0 + 66 * s, 34 * s, white, -1, 0, 0);
    ftext(l, "S", tx + ftext_w(buf, 34 * s, 0, 0) + 6 * s, py0 + 66 * s, 16 * s, muted, -1, 1, 0);
    // The lollipop's word: BRAKE while it stands, GO with the board.
    {
      bool go = c->go;
      float bx = px1 - 44 * s, by = py0 + 48 * s, rr = 30 * s;
      float pulse = go ? 1 + 0.08f * sinf((c->t_go + u) * 0.35f) * (c->t_go < 90) : 1;
      enum { N = 20 };
      for(int i = 0; i < N; i++) {
        float a0 = 2 * PI * i / N, a1 = 2 * PI * (i + 1) / N;
        TgV3Vertex q[3] = {{bx, by, 0, 0, go ? green : 0xFFE0303A},
                           {bx + cosf(a0) * rr * pulse, by + sinf(a0) * rr * pulse, 0, 0, go ? green : 0xFFE0303A},
                           {bx + cosf(a1) * rr * pulse, by + sinf(a1) * rr * pulse, 0, 0, go ? green : 0xFFE0303A}};
        list_push(l, q, 3, -1);
      }
      ftext(l, go ? "GO" : "BRAKE", bx, by + (go ? 9 : 5) * s, (go ? 26 : 13) * s, 0xFFFFFFFF, 0, 2, 0);
    }
    // The steps: a marker each on a line, the words under them.
    static const char* kStep[4] = {"PIT ENTRY", "BOX", "REFUELLING", "GO"};
    float sx0 = px0 + 52 * s, sx1 = px1 - 70 * s, sy = py0 + 96 * s, step = (sx1 - sx0) / 3;
    bool through = stage == 3 && c->t_box == 0;  // (let go before the box: no stop)
    for(int i = 0; i < 4; i++) {
      float cx = sx0 + step * i;
      bool skipped = through && (i == 1 || i == 2);
      uint32_t ccol = skipped ? 0xFF3A4652 : i < stage ? cyan : i == stage ? (i == 3 ? green : gold) : 0xFF3A4652;
      if(i < 3) rect(l, cx + 14 * s, sy - 1.5f * s, cx + step - 14 * s, sy + 1.5f * s, i < stage ? cyan : 0xFF3A4652);
      float r0 = (i == stage ? 11 : 8) * s;
      sq(l, cx, sy - r0, cx + r0, sy, cx, sy + r0, cx - r0, sy, ccol);
      if(i == stage && i != 3 && ((int) ((c->t + u) / 15) & 1)) sq(l, cx, sy - 4 * s, cx + 4 * s, sy, cx, sy + 4 * s, cx - 4 * s, sy, 0xFF102030);
      ftext(l, kStep[i], cx, sy + 30 * s, 16 * s, i == stage ? (i == 3 ? green : gold) : i < stage && !skipped ? white : muted, 0, 0, 1.5f * s);
    }
    // The fuel: one big gauge, its number, what the stop has put in.
    float gx0 = px0 + 40 * s, gx1 = px1 - 196 * s, gy0 = py0 + 154 * s, gy1 = gy0 + 26 * s;
    float pct = fminf(100, v->fuel_word[pl] * 100.0f / 0xA7FF), ff = pct / 100;  // (the game's fuel word, finer than the HUD's 5 % steps)
    bool full = v->fuel_word[pl] >= 0xA7FF;
    rect(l, gx0, gy0, gx1, gy1, 0xFF22303C);
    uint32_t fc = ff < 0.25f ? red : full ? cyan : gold;
    rect(l, gx0 + 3 * s, gy0 + 3 * s, gx0 + 3 * s + (gx1 - gx0 - 6 * s) * ff, gy1 - 3 * s, fc);
    if(c->phase == PIT_BOX && !c->full) {  // the fuel going in now: a moving light at the edge
      float ex = gx0 + 3 * s + (gx1 - gx0 - 6 * s) * ff, ph2 = fmodf((c->t + u) / 20.0f, 1);
      rect(l, ex - 26 * s * ph2, gy0 + 3 * s, ex, gy1 - 3 * s, with_alpha(0xFFFFFFFF, (int) (90 * (1 - ph2))));
    }
    for(int k = 1; k < 4; k++) rect(l, gx0 + (gx1 - gx0) * k / 4, gy0, gx0 + (gx1 - gx0) * k / 4 + 2 * s, gy1, 0xC0102030);
    ftext(l, "FUEL", gx0, gy0 - 6 * s, 12 * s, muted, -1, 0, 2 * s);
    snprintf(buf, sizeof(buf), "%d", (int) pct);
    ftext(l, buf, px1 - 104 * s, gy1 + 4 * s, 44 * s, full ? cyan : white, 1, 0, 0);
    ftext(l, "%", px1 - 100 * s, gy1 + 4 * s, 20 * s, muted, -1, 1, 0);
    int added = (int) pct - (int) c->fuel_start;
    if(added > 0) {
      snprintf(buf, sizeof(buf), "+%d %%", added);
      ftext(l, buf, px1 - 44 * s, gy1 + 4 * s, 14 * s, cyan, 0, 1, 0);
    }
    // What is happening, in words.
    const char* what;
    if(stage == 0) what = hd->kph ? "PIT LIMITER  60 KM/H" : "PIT LIMITER  37 MPH";
    else if(stage == 1) what = "STOP ON YOUR MARKS";
    else if(stage == 2) what = !c->full ? "REFUELLING  -  JACKS UP  -  TYRES" : "FUEL FULL  -  CREW FINISHING";
    else if(c->s >= PR_LIM1 * seg) what = "PIT EXIT  -  LIMITER OFF";
    else what = through ? "NO STOP  -  DRIVE THROUGH" : "GO GO GO";
    ftext(l, what, px0 + 40 * s, py1 - 20 * s, 15 * s, stage == 3 ? green : gold, -1, 0, 3 * s);
    list_fade(l, first, alpha, 0);
    return;
  }
  // On the way: where the entry is.
  if(pl < 2 && g_pmark[pl].on) {
    float m = g_pmark[pl].metres;
    bool in = g_pmark[pl].committed;
    uint32_t col = in ? cyan : gold;
    float x = g_pmark[pl].sx, y = g_pmark[pl].sy;
    int first = l->nv;
    if(g_pmark[pl].seen) {
      // Over the entry: a chevron pointing down at it, PIT and the distance.
      y = fmaxf(y, 150 * s);
      float cw = 26 * s;
      sq(l, x - cw, y - 30 * s, x, y - 8 * s, x, y + 4 * s, x - cw, y - 18 * s, col);
      sq(l, x, y - 8 * s, x + cw, y - 30 * s, x + cw, y - 18 * s, x, y + 4 * s, col);
      rpanel(l, x - 62 * s, y - 92 * s, x + 62 * s, y - 40 * s, 10 * s, 0xD809121E);
      ftext(l, in ? "PIT IN" : "PIT", x, y - 63 * s, 22 * s, col, 0, 2, 1 * s);
      snprintf(buf, sizeof(buf), "%d M", (int) (m + 0.5f));
      ftext(l, buf, x, y - 46 * s, 13 * s, white, 0, 0, 1.5f * s);
    } else {
      // Off the picture: an arrow at its side.
      int sg = g_pmark[pl].sgn;
      float ex = sg > 0 ? w - 70 * s : 70 * s, ey = h * 0.45f, aw = 22 * s * sg;
      sq(l, ex - aw, ey - 26 * s, ex + aw, ey, ex + aw, ey, ex - aw, ey + 26 * s, col);
      snprintf(buf, sizeof(buf), "PIT %d M", (int) (m + 0.5f));
      ftext(l, buf, ex - aw * 1.6f, ey + 50 * s, 15 * s, white, sg > 0 ? 1 : -1, 0, 1.5f * s);
    }
    // And the rule (keep past the white line at the sign), where the pit panel goes: under the top
    // corners' panels, also in a narrow 2-player view.
    snprintf(buf, sizeof(buf), g_pmark[pl].sgn > 0 ? "PIT ENTRY  %d M  -  KEEP RIGHT OF THE LINE" : "PIT ENTRY  %d M  -  KEEP LEFT OF THE LINE",
             (int) (m + 0.5f));
    float tw = ftext_w(buf, 16 * s, 0, 2 * s), by = 236 * s;
    rpanel(l, w / 2.0f - tw / 2 - 22 * s, by, w / 2.0f + tw / 2 + 22 * s, by + 34 * s, 10 * s, 0xD809121E);
    rect(l, w / 2.0f - tw / 2 - 22 * s, by, w / 2.0f - tw / 2 - 17 * s, by + 34 * s, col);
    ftext(l, buf, w / 2.0f, by + 23 * s, 16 * s, col, 0, 0, 2 * s);
    list_fade(l, first, sat01((320 - m) / 40), 0);
  }
}

static void modern_hud(const TgView3D* v, int pl, TgV3List* l, int w, int h) {
  font_ready((TgView3D*) v);
  const TgV3Hud* hd = &v->hudv[pl];
  float s = h / 1080.0f;
  char buf[32];
  const uint32_t white = 0xFFF0F5F7, muted = 0xFF9CABB9, cyan = 0xFF62F2DC, panelC = 0xDA09121E, gold = 0xFFE6B866;
  float m = 52 * s;
  // Top middle: where the race is.
  if(!v->two && !v->demo) {
    char t2[48];
    char cn[24], cc[24], t1[56];
    course_name(v->course, cn, cc, sizeof(cn));
    snprintf(t1, sizeof(t1), "%s  /  %s", cn, cc);
    snprintf(t2, sizeof(t2), "WORLD TOUR  /  %s  /  RACE %d OF 4", kCountry[(v->course >> 2) & 7].name, (v->course & 3) + 1);
    ftext(l, t1, w / 2.0f, 66 * s, 26 * s, white, 0, 2, 2 * s);
    ftext(l, t2, w / 2.0f, 94 * s, 13 * s, muted, 0, 1, 1.5f * s);
  }
  // Top left: the course and the lap.
  {
    float x0 = m, y0 = 44 * s, x1 = x0 + 296 * s, y1 = y0 + 180 * s;
    rpanel(l, x0, y0, x1, y1, 12 * s, panelC);
    ftext(l, "CIRCUIT / LIVE", x0 + 24 * s, y0 + 32 * s, 13 * s, cyan, -1, 0, 3 * s);
    const TgTrack3D* tr = &v->track;
    int n = tr->tables.segments;
    // Turned so the start straight runs left to right, as on the game's own course map.
    float rc = 1, rs = 0, mnx = 1e9f, mxx = -1e9f, mnz = 1e9f, mxz = -1e9f;
    if(n > 8) {
      float dx = tr->centre[4].x - tr->centre[0].x, dz = tr->centre[4].z - tr->centre[0].z, l = sqrtf(dx * dx + dz * dz);
      if(l > 0.01f) rc = dx / l, rs = dz / l;
      for(int i = 0; i < n; i++) {
        float rx = tr->centre[i].x * rc + tr->centre[i].z * rs, rz = -tr->centre[i].x * rs + tr->centre[i].z * rc;
        mnx = fminf(mnx, rx), mxx = fmaxf(mxx, rx), mnz = fminf(mnz, rz), mxz = fmaxf(mxz, rz);
      }
    }
    float ex = mxx - mnx, ez = mxz - mnz;
    if(n > 8 && ex > 1 && ez > 1) {
      float bw = 110 * s, bh = 100 * s, k = fminf(bw / ex, bh / ez);
      float cx = x0 + 24 * s + bw / 2, cy = y0 + 46 * s + bh / 2;
#define MX(p) (cx + ((p).x * rc + (p).z * rs - (mnx + mxx) / 2) * k)
#define MY(p) (cy + (-(p).x * rs + (p).z * rc - (mnz + mxz) / 2) * k)
      int step = n / 140 > 0 ? n / 140 : 1;
      for(int i = 0; i < n; i += step) {
        TgVec3 a = tr->centre[i], b = tr->centre[(i + step) % n];
        bar(l, MX(a), MY(a), MX(b), MY(b), 3 * s, white);
      }
      for(int c = v->cur.car_count - 1; c >= 0; c--) {
        TgVec3 p = v->cur.cars[c].world.pos;
        bool me = c == v->cur.view_car[pl];
        float r0 = (me ? 6.5f : 3.5f) * s, px = MX(p), py = MY(p);
        sq(l, px, py - r0, px + r0, py, px, py + r0, px - r0, py, me ? cyan : 0xFF6E8296);
      }
#undef MX
#undef MY
    }
    ftext(l, "LAP", x0 + 180 * s, y0 + 82 * s, 17 * s, muted, -1, 1, 0);
    snprintf(buf, sizeof(buf), "%02d", hd->lap);
    ftext(l, buf, x0 + 178 * s, y0 + 126 * s, 40 * s, white, -1, 0, 0);
    snprintf(buf, sizeof(buf), "/ %02d", hd->laps);
    ftext(l, buf, x0 + 182 * s + ftext_w("00", 40 * s, 0, 0), y0 + 124 * s, 18 * s, muted, -1, 1, 0);
  }
  // Top right: position and race time.
  {
    float x1 = w - m, x0 = x1 - 386 * s, y0 = 44 * s, y1 = y0 + 126 * s;
    rpanel(l, x0, y0, x1, y1, 12 * s, panelC);
    ftext(l, "POSITION", x0 + 28 * s, y0 + 32 * s, 15 * s, white, -1, 0, 0);
    snprintf(buf, sizeof(buf), "%d", hd->position);
    ftext(l, buf, x0 + 26 * s, y0 + 92 * s, 60 * s, white, -1, 0, 0);
    ftext(l, "/ 20", x0 + 36 * s + ftext_w(buf, 60 * s, 0, 0), y0 + 92 * s, 24 * s, muted, -1, 1, 0);
    rect(l, x0 + 208 * s, y0 + 24 * s, x0 + 209.5f * s, y1 - 24 * s, 0x50A9D2E0);
    ftext(l, "RACE TIME", x0 + 230 * s, y0 + 36 * s, 14 * s, white, -1, 0, 0);
    snprintf(buf, sizeof(buf), "%02d:%02d.%02d", hd->minutes, hd->seconds, hd->hundredths);
    ftext(l, buf, x0 + 230 * s, y0 + 74 * s, 27 * s, white, -1, 0, 0);
  }
  // Bottom left: nitro and fuel.
  {
    float x0 = m, y1 = h - 52 * s, y0 = y1 - 160 * s, x1 = x0 + 318 * s;
    rpanel(l, x0, y0, x1, y1, 12 * s, panelC);
    ftext(l, "NITRO", x0 + 26 * s, y0 + 34 * s, 17 * s, white, -1, 0, 0);
    snprintf(buf, sizeof(buf), "%02d", hd->nitros);
    ftext(l, buf, x1 - 30 * s, y0 + 32 * s, 16 * s, cyan, 1, 0, 0);
    for(int i = 0; i < 3; i++) {
      float bx = x0 + 14 * s + i * 90 * s, by = y0 + 56 * s, bw = 80 * s, bh2 = 20 * s, sk = 10 * s;
      sq(l, bx + sk, by, bx + bw + sk, by, bx + bw, by + bh2, bx, by + bh2, i < hd->nitros ? cyan : 0xFF3A4652);
    }
    ftext(l, "FUEL", x0 + 26 * s, y0 + 120 * s, 13 * s, white, -1, 0, 0);
    float fx0 = x0 + 74 * s, fx1 = x1 - 32 * s, fy = y0 + 111 * s, fh = 7 * s, ff = hd->fuel / 100.0f;
    rect(l, fx0, fy, fx1, fy + fh, 0xFF3A4652);
    rect(l, fx0, fy, fx0 + (fx1 - fx0) * ff, fy + fh, ff < 0.25f ? 0xFFFF5A48 : gold);
  }
  // Bottom right: gear, speed, revs.
  {
    float x1 = w - m, y1 = h - 52 * s, y0 = y1 - 204 * s, x0 = x1 - 434 * s;
    rpanel(l, x0, y0, x1, y1, 12 * s, panelC);
    ftext(l, "AUTO", x0 + 28 * s, y0 + 34 * s, 13 * s, white, -1, 0, 0);
    snprintf(buf, sizeof(buf), "%d", hd->gear);
    ftext(l, buf, x0 + 28 * s, y0 + 92 * s, 52 * s, cyan, -1, 0, 0);
    // (in its pit stop the game's speed word is its pit scene's: the car's as drawn, on the limiter)
    int shown = hd->speed;
    {
      int vk = v->cur.view_car[pl];
      const PitCar* pc = vk >= 0 ? pit_car(v, v->cur.cars[vk].id) : NULL;
      // (as the game's own figure for that motion: its speed word moves a car speed / 512 track units
      // a frame, and shows as speed x 24 / 1024 km/h, x 15 / 1024 mph)
      if(pc) shown = (int) (pc->vel * (hd->kph ? 2.56f : 1.6f) + 0.5f);
      if(pc && (pc->phase == PIT_IN || (pc->phase == PIT_OUT && pc->s < PR_LIM1 * v->track.scale.seg_len))) {
        float bx = x0 + 120 * s, by = y0 + 52 * s;
        rpanel(l, bx, by, bx + 132 * s, by + 34 * s, 8 * s, 0xFF8A6A12);
        ftext(l, "PIT LIMITER", bx + 66 * s, by + 23 * s, 13 * s, 0xFFFFE9A8, 0, 0, 1.5f * s);
      }
    }
    snprintf(buf, sizeof(buf), "%d", shown);
    ftext(l, buf, x1 - 54 * s, y0 + 122 * s, 104 * s, white, 1, 2, 0);
    ftext(l, hd->kph ? "KM/H" : "MPH", x1 - 30 * s, y0 + 154 * s, 16 * s, white, 1, 0, 0);
    float rev = v->rpm[pl] / (float) 0x0A00;
    rev = rev < 0 ? 0 : rev > 1 ? 1 : rev;
    int segs = 26;
    for(int i = 0; i < segs; i++) {
      float t = (i + 0.5f) / segs, bx = x0 + 30 * s + i * 13.5f * s, by = y0 + 166 * s;
      uint32_t c = t <= rev ? (t > 0.86f ? 0xFFFF4A5A : cyan) : 0xFF3A4652;
      sq(l, bx + 3 * s, by, bx + 10 * s, by, bx + 7 * s, by + 17 * s, bx, by + 17 * s, c);
    }
  }
  // The pit stop's panel, and the cue for the pit entry on the way to it.
  pit_hud(v, pl, l, w, h, s);
  // The race's message: big, clean, in the middle of the upper screen.
  {
    char mb[24];
    g_msg_player = pl;
    const char* msg = message_text(v->msg_id[pl], mb, sizeof(mb));
    if(msg && message_showing(v, pl)) {
      bool warn = v->msg_id[pl] == 0x1D || v->msg_id[pl] == 0x1E;
      float size = (strlen(msg) <= 4 ? 96 : 64) * s, tw = ftext_w(msg, size, 2, 0), cy = h * 0.32f;
      {  // (under the pit panel while it is up: rivals passing during the stop change the position)
        int vk = v->cur.view_car[pl];
        if(vk >= 0 && pit_car(v, v->cur.cars[vk].id)) size *= 0.7f, tw *= 0.7f, cy = 560 * s;
      }
      rpanel(l, w / 2 - tw / 2 - 40 * s, cy - size - 22 * s, w / 2 + tw / 2 + 40 * s, cy + 28 * s, 14 * s, 0xB009121E);
      rect(l, w / 2 - tw / 2 - 18 * s, cy + 12 * s, w / 2 + tw / 2 + 18 * s, cy + 15 * s, warn ? 0xFFFF5A48 : cyan);
      ftext(l, msg, w / 2.0f, cy, size, warn ? 0xFFFF7A6A : v->msg_id[pl] == 0x22 ? cyan : white, 0, 2, 0);
    }
    // A rival's taunt: a speech bubble where the original has it (left or right of the
    // middle, over the cars), white with dark words; the shouts ($33-$36) in a gold burst.
    // The words are the game's own lettering, read from the ROM (taunt_ink), each of its
    // pixels a crisp square.
    const TauntInk* ti = taunt_ink(v->msg_id[pl]);
    if(ti && message_showing(v, pl)) {
      uint16_t y = rom16(0x06DF16 + (uint32_t) v->msg_id[pl] * 2);
      int col = rom16(0x060000 | y) & 31;
      float sx = (h * 4.0f / 3.0f) / 256.0f;
      float bx = w / 2.0f + ((col * 8 + 18) - 128) * sx, by = h * 0.56f;
      bool shout = v->msg_id[pl] >= 0x33;
      // It grows in over 8 frames from 60 % (eased out), fading in.
      float age = fminf(1.0f, v->msg_age[pl] / 8.0f), grow = 1 - (1 - age) * (1 - age);
      s *= 0.6f + 0.4f * grow;
      float dot = 6 * s;  // one pixel of the game's lettering
      int iw = ti->x1 - ti->x0 + 1, ih = ti->y1 - ti->y0 + 1;
      float tw = iw * dot, th = ih * dot;
      float bh = th + 34 * s, bw = tw + 44 * s;
      float x0 = bx - bw / 2, x1 = bx + bw / 2, y1 = by, y0 = by - bh;
      if(x0 < 8 * s) x1 += 8 * s - x0, x0 = 8 * s;
      if(x1 > w - 8 * s) x0 -= x1 - (w - 8 * s), x1 = w - 8 * s;
      int al = (int) (255 * (0.25f + 0.75f * age));
      uint32_t fillc = with_alpha(shout ? 0xF8FFC93C : 0xF6F4F7FA, al * 248 / 255), ink = with_alpha(0xFF101820, al),
               edge = with_alpha(shout ? 0xFFD0202A : 0xFF101820, al);
      rpanel(l, x0 - 3 * s, y0 - 3 * s, x1 + 3 * s, y1 + 3 * s, 16 * s, edge);
      rpanel(l, x0, y0, x1, y1, 14 * s, fillc);
      // The tail, down towards the car.
      float tx = (x0 + x1) / 2 - 10 * s;
      sq(l, tx - 4 * s, y1 - 2 * s, tx + 22 * s, y1 - 2 * s, tx + 2 * s, y1 + 26 * s, tx - 2 * s, y1 + 26 * s, edge);
      sq(l, tx, y1 - 3 * s, tx + 16 * s, y1 - 3 * s, tx + 2 * s, y1 + 19 * s, tx + 1 * s, y1 + 19 * s, fillc);
      // The lettering, a row's runs of pixels at a time.
      float lx = (x0 + x1) / 2 - tw / 2, ly = y0 + 17 * s;
      for(int r = ti->y0; r <= ti->y1; r++)
        for(int q = ti->x0; q <= ti->x1;) {
          if(!ti->ink[r][q]) {
            q++;
            continue;
          }
          int q1 = q;
          while(q1 <= ti->x1 && ti->ink[r][q1]) q1++;
          rect(l, lx + (q - ti->x0) * dot, ly + (r - ti->y0) * dot, lx + (q1 - ti->x0) * dot, ly + (r - ti->y0 + 1) * dot, ink);
          q = q1;
        }
    }
  }
}

// --- The car select screen, redrawn (the modern look) -------------------------------------
static const char* kCarName[4] = {"ROSSO", "GLACIER", "VIOLET", "AZURE"};
static const char* kCarClass[4] = {"V12", "R", "GT", "XR"};
static const char* kCarLine[4] = {"THE ORIGINAL. REENGINEERED.", "BUILT FOR THE TRACK.", "LOW, LONG AND FAST.",
                                  "OPEN TOP. NO COMPROMISE."};

bool tg_view3d_select_up(const TgView3D* v) { return v->select_up > 0; }
void tg_view3d_set_country_select(TgView3D* v, bool free, bool busy) { v->country_free = free, v->country_busy = busy; }

// One car model on the screen at (cx, cy) in a box of height bh, turned yaw degrees.
static void select_car(TgView3D* v, Gather* g, Cam* cam, int type, float cx, float cy, float focal, float yaw, float pitch,
                       float dist, float w, float h) {
  float a = yaw * PI / 180, p = pitch * PI / 180;
  V3 target = v3(0, 0.6f, 0);
  cam->eye = vadd(target, v3(sinf(a) * cosf(p) * dist, sinf(p) * dist, -cosf(a) * cosf(p) * dist));
  cam->fwd = vnorm(vsub(target, cam->eye));
  cam->right = vnorm(vcross(cam->fwd, v3(0, 1, 0)));
  cam->up = vcross(cam->right, cam->fwd);
  cam->f = focal;
  cam->cx = cx, cam->cy = cy, cam->w = w, cam->h = h;
  g->cam = cam;
  V3 f = v3(0, 0, 1), r = v3(-1, 0, 0), u = v3(0, 1, 0);  // (right = forward x up: not mirrored)
  car_shadow(g, v3(0, 0, 0), f, r, 2.6f, 1.3f, 0.5f);
  uint8_t keep = v->ptype[0];
  v->ptype[0] = (uint8_t) type;
  g_showcase = true;
  g_wheelMetres = 0, g_wheelSteer = 0;
  float depth = to_cam(cam, v3(0, 0, 0)).z;
  g_number = type + 1;  // each machine its own number on the select screen
  bool drawn = mesh_car(g, v, 0, v3(0, 0, 0), f, r, u, depth, false, false, 0);
  g_number = 0;
  if(!drawn) clean_car(g, v, 0, v3(0, 0, 0), f, r, u, depth, 0, 0, false, false, 0, 0);
  g_showcase = false;
  v->ptype[0] = keep;
}

void tg_view3d_draw_select(TgView3D* v, int w, int h, float seconds, TgV3List* out) {
  tg_v3_list_clear(out);
  font_ready(v);
  float s = h / 1080.0f;
  const uint32_t white = 0xFFF0F5F7, muted = 0xFF9CABB9, cyan = 0xFF62F2DC;
  int sel = v->select_cursor & 3;
  // The background: deep navy, a lighter band behind the car, a floor grid.
  screen_rect(out, 0, 0, (float) w, (float) h, 0, 1, 0xFF0B1622, 0xFF0F1E2C, -1);
  for(int i = 0; i < 18; i++) {
    float y = h * 0.62f + i * i * 1.6f * s;
    if(y > h) break;
    TgV3Vertex q[6] = {{0, y, 0, 0, 0x2862F2DC}, {(float) w, y, 0, 0, 0x2862F2DC}, {(float) w, y + 1.2f * s, 0, 0, 0x2862F2DC},
                       {0, y, 0, 0, 0x2862F2DC}, {(float) w, y + 1.2f * s, 0, 0, 0x2862F2DC}, {0, y + 1.2f * s, 0, 0, 0x2862F2DC}};
    list_push(out, q, 6, -1);
  }
  // The cars: the chosen one big, turning slowly; the four small in their cards.
  Gather g = {0};
  Cam cam;
  g_haze = 0xFF0F1E2C;
  select_car(v, &g, &cam, sel, w * 0.68f, h * 0.47f, h * 1.15f, 215 + sinf(seconds * 0.5f) * 25, 14, 7.2f, (float) w, (float) h);
  float cw = (w - 2 * 78 * s - 3 * 20 * s) / 4, cy0 = h - 310 * s, ch = 196 * s;
  for(int c = 0; c < 4; c++) {
    float cx0 = 78 * s + c * (cw + 20 * s);
    select_car(v, &g, &cam, c, cx0 + cw / 2, cy0 + ch * 0.45f, cw * 1.25f, 90, 6, 7.5f, (float) w, (float) h);
  }
  qsort(g.it, (size_t) g.ni, sizeof(Item), cmp_item);
  // The cards first (behind the small cars), then the cars, then the text.
  for(int c = 0; c < 4; c++) {
    float cx0 = 78 * s + c * (cw + 20 * s);
    rpanel(out, cx0, cy0, cx0 + cw, cy0 + ch, 10 * s, c == sel ? 0xE0132636 : 0xC00E1A26);
    if(c == sel) {
      rect(out, cx0 + 10 * s, cy0 + ch - 4 * s, cx0 + cw - 10 * s, cy0 + ch - 1.5f * s, cyan);
    }
  }
  for(int i = 0; i < g.ni; i++)
    for(int j = 0; j < g.it[i].count; j++) list_push(out, g.t[g.it[i].first + j].v, 3, g.t[g.it[i].first + j].pic);
  free(g.t);
  free(g.it);
  out->overlay = true;
  char buf[48];
  for(int c = 0; c < 4; c++) {
    float cx0 = 78 * s + c * (cw + 20 * s);
    snprintf(buf, sizeof(buf), "%s / %s", kCarClass[c], kCarName[c]);
    ftext(out, buf, cx0 + 22 * s, cy0 + ch - 24 * s, 19 * s, c == sel ? cyan : white, -1, 0, 1 * s);
  }
  ftext(out, brand_caps(), 76 * s, 74 * s, 24 * s, cyan, -1, 0, 9 * s);
  ftext(out, "WORLD TOUR", w - 76 * s, 72 * s, 15 * s, muted, 1, 0, 2 * s);
  rect(out, 74 * s, 100 * s, w - 74 * s, 101.5f * s, 0x40A9D2E0);
  ftext(out, "02  /  GARAGE", 78 * s, 160 * s, 15 * s, cyan, -1, 0, 6 * s);
  ftext(out, "CHOOSE YOUR MACHINE", 72 * s, 236 * s, 72 * s, white, -1, 2, 0);
  ftext(out, "Four icons. One world championship.", 78 * s, 284 * s, 20 * s, muted, -1, 1, 0);
  snprintf(buf, sizeof(buf), "0%d  /  %s CLASS", sel + 1, kCarClass[sel]);
  ftext(out, buf, 80 * s, 378 * s, 19 * s, cyan, -1, 0, 6 * s);
  ftext(out, kCarName[sel], 74 * s, 486 * s, 96 * s, white, -1, 2, 0);
  ftext(out, kCarLine[sel], 82 * s, 538 * s, 15 * s, white, -1, 0, 1 * s);
  // The game's own figures for this car, with bars as in the concept art.
  static const char* kStat[4] = {"TOP SPEED", "0-100 KM/H", "TIRE GRIP", "FUEL CONSUMPTION"};
  for(int row = 0; row < 4; row++) {
    const char* val = v->car_stat[sel][row];
    float y = 590 * s + row * 46 * s, x0 = 82 * s, x1 = 488 * s;
    ftext(out, kStat[row], x0, y, 13 * s, white, -1, 0, 1 * s);
    ftext(out, val[0] ? val : "-", x1, y, 18 * s, white, 1, 1, 0);
    float fr = 0.5f;
    if(row == 0) fr = (float) atoi(val) / 260;
    else if(row == 1) fr = 1 - (atof(val) - 2.5f) / 6;
    else fr = strstr(val, "HIGH") ? (row == 2 ? 0.9f : 0.85f) : strstr(val, "LOW") ? (row == 2 ? 0.45f : 0.35f) : 0.65f;
    fr = fr < 0.05f ? 0.05f : fr > 1 ? 1 : fr;
    rect(out, x0, y + 14 * s, x1, y + 18 * s, 0xFF2A3644);
    rect(out, x0, y + 14 * s, x0 + (x1 - x0) * fr, y + 18 * s, row == 3 ? 0xFFE6B866 : cyan);
  }
  {  // the cars in a line: Left / Right step through them (the frontend turns that into the
     // game's 2 x 2 grid moves, src/select_input.h); any other button takes the car
    static const char* const hints[2][2] = {{"LEFT / RIGHT", "CHOOSE"}, {"A / START", "SELECT"}};
    key_hints(out, w, h, s, hints, 2);
  }
}

// --- The title screen, redrawn ---------------------------------------------------------
bool tg_view3d_title_up(const TgView3D* v) { return v->title_up > 0 || v->splash_up > 0; }

// The project's name in capitals (TG_APP_NAME), for the title screens and menus.
static const char* brand_caps(void) {
  static char b[64];
  if(!b[0])
    for(size_t i = 0; TG_APP_NAME[i] && i + 1 < sizeof(b); i++) b[i] = (char) toupper((unsigned char) TG_APP_NAME[i]);
  return b;
}

// The emblem (tools/gen_launcher_art.py draws the same for the launcher): a globe with a lap
// round it, a tilted orbit in orange whose near half passes in front, a light running on it.
static void emblem(TgV3List* l, float cx, float cy, float r, float seconds, int alpha) {
  const float tilt = -18 * PI / 180, orx = r * 1.42f, ory = r * 0.46f;
  const int n = 48;
  float ct = cosf(tilt), st = sinf(tilt), w = r * 0.075f;
  for(int half = 0; half < 2; half++) {  // the far half of the orbit, then (after the globe) the near
    if(half == 1) {
      for(int i = 0; i < n; i++) {  // the globe: a disc lit from the top left, its rim and lines
        float a0 = i * 2 * PI / n, a1 = (i + 1) * 2 * PI / n;
        uint32_t c = with_alpha(0xFF2A3660, alpha), cl = with_alpha(0xFF5C70A8, alpha);
        TgV3Vertex q[3] = {{cx - r * 0.35f, cy - r * 0.4f, 0, 0, cl}, {cx + cosf(a0) * r, cy + sinf(a0) * r, 0, 0, c}, {cx + cosf(a1) * r, cy + sinf(a1) * r, 0, 0, c}};
        list_push(l, q, 3, -1);
        sbar(l, cx + cosf(a0) * r, cy + sinf(a0) * r, cx + cosf(a1) * r, cy + sinf(a1) * r, r * 0.05f, with_alpha(0xFFC4CEE2, alpha));
      }
      for(int k = 0; k < 2; k++) {
        float kx = k ? 0.78f : 0.36f;
        for(int i = 0; i < n; i++) {
          float a0 = i * 2 * PI / n, a1 = (i + 1) * 2 * PI / n;
          sbar(l, cx + cosf(a0) * r * kx, cy + sinf(a0) * r, cx + cosf(a1) * r * kx, cy + sinf(a1) * r, r * 0.03f, with_alpha(0x80C4CEE2, alpha));
        }
      }
      sbar(l, cx - r, cy, cx + r, cy, r * 0.03f, with_alpha(0x80C4CEE2, alpha));
    }
    for(int i = 0; i < n; i++) {
      float t0 = (half ? 0 : PI) + i * PI / n, t1 = t0 + PI / n;
      float x0 = orx * cosf(t0), y0 = ory * sinf(t0), x1 = orx * cosf(t1), y1 = ory * sinf(t1);
      float k = (float) i / n;
      uint32_t c = half ? lerp_colour(0xFFFFC440, 0xFFFF5C38, k) : 0xFF963C28;
      sbar(l, cx + x0 * ct - y0 * st, cy + x0 * st + y0 * ct, cx + x1 * ct - y1 * st, cy + x1 * st + y1 * ct, half ? w * 1.15f : w,
           with_alpha(c, alpha * (half ? 255 : 220) / 255));
    }
  }
  // The light running round the lap (its near half).
  float t = fmodf(seconds * 0.35f, 1.0f) * PI, x = orx * cosf(t), y = ory * sinf(t);
  float px = cx + x * ct - y * st, py = cy + x * st + y * ct, d = r * 0.16f;
  screen_rect(l, px - d * 3, py - d * 3, px + d * 3, py + d * 3, 0, 1, with_alpha(0xFFFFE6C8, alpha * 3 / 4), with_alpha(0xFFFFE6C8, alpha * 3 / 4), TEX_GLOW);
  for(int i = 0; i < 16; i++) {
    float a0 = i * 2 * PI / 16, a1 = (i + 1) * 2 * PI / 16;
    uint32_t c = with_alpha(0xFFFFFFFF, alpha);
    TgV3Vertex q[3] = {{px, py, 0, 0, c}, {px + cosf(a0) * d, py + sinf(a0) * d, 0, 0, c}, {px + cosf(a1) * d, py + sinf(a1) * d, 0, 0, c}};
    list_push(l, q, 3, -1);
  }
}

// The name as a wordmark: upright capitals with a chrome face (white over a horizon line, a
// dark steel reflection under it warming to orange at the foot) on a soft shadow, centred on
// x with its baseline at y.
static void wordmark(TgV3List* out, const char* word, float x, float y, float size, float track, float seconds, int alpha) {
  float tw = ftext_w(word, size, 0, track), s = size / 100;
  for(int k = 0; k < 4; k++) ftext(out, word, x + (2 + k) * s, y + (3 + k) * s, size, with_alpha(0x000008, alpha * 0x30 / 255), 0, 0, track);
  float k = size / FONT_SIZE, gx = x - tw / 2;
  float sweep = fmodf(seconds * 0.45f, 2.2f) * (tw + 400 * s) - 200 * s;
  for(const char* p = word; *p; p++) {
    int gi = glyph_of(0, *p);
    float gw = kFontGlyphs[gi].w, gh = kFontGlyphs[gi].h;
    if(gw > 0) {
      float x0 = gx + kFontGlyphs[gi].left * k, y0 = y + kFontGlyphs[gi].top * k, x1 = x0 + gw * k, y1 = y0 + gh * k;
      float u0 = (kFontGlyphs[gi].x + 0.5f) / FONT_AW, v0 = (kFontGlyphs[gi].y + 0.5f) / FONT_AH;
      float u1 = (kFontGlyphs[gi].x + gw - 0.5f) / FONT_AW, v1 = (kFontGlyphs[gi].y + gh - 0.5f) / FONT_AH;
      float d = fabsf((x0 + x1) / 2 - (x - tw / 2 + sweep)) / (90 * s * 4), lit = d < 1 ? (1 - d) * (1 - d) : 0;
      float ym = y0 + (y1 - y0) * 0.52f, vm = v0 + (v1 - v0) * 0.52f;
      uint32_t top = with_alpha(0xFFFFFFFF, alpha), hz = with_alpha(mix(0xFFCED8E8, 0xFFFFFFFF, lit), alpha);
      uint32_t dark = with_alpha(mix(0xFF404E70, 0xFF8090B8, lit), alpha), bot = with_alpha(mix(0xFFFFA878, 0xFFFFD0B0, lit), alpha);
      TgV3Vertex q[12] = {{x0, y0, u0, v0, top}, {x1, y0, u1, v0, top}, {x1, ym, u1, vm, hz},
                          {x0, y0, u0, v0, top}, {x1, ym, u1, vm, hz}, {x0, ym, u0, vm, hz},
                          {x0, ym, u0, vm, dark}, {x1, ym, u1, vm, dark}, {x1, y1, u1, v1, bot},
                          {x0, ym, u0, vm, dark}, {x1, y1, u1, v1, bot}, {x0, y1, u0, v1, bot}};
      list_push(out, q, 12, FONT_PIC);
    }
    gx += kFontGlyphs[gi].adv * k + track;
  }
}

// The modern look's splash (while the game shows its publisher's): the project's emblem and
// name drawn in light, a line sweeping across.
static void draw_splash(TgView3D* v, int w, int h, float seconds, TgV3List* out) {
  float s = h / 1080.0f, t = v->splash_frames / 60.0f;
  screen_rect(out, 0, 0, (float) w, (float) h, 0, 1, 0xFF04070C, 0xFF000000, -1);
  float a = t < 1 ? t : t > 7 ? fmaxf(0, 8 - t) : 1;
  int al = (int) (255 * a);
  screen_rect(out, w * 0.2f, h * 0.25f, w * 0.8f, h * 0.75f, 0, 1, with_alpha(0xFF3A5CFF, al / 5), with_alpha(0xFF3A5CFF, al / 5), TEX_GLOW);
  out->overlay = true;
  emblem(out, w / 2.0f, h * 0.36f, 62 * s, seconds, al);
  wordmark(out, brand_caps(), w / 2.0f, h * 0.56f, 120 * s, 10 * s, seconds, al);
  float sw = fmodf(t * 0.5f, 1.5f) * (w + 400 * s) - 200 * s;
  sbar(out, sw, h * 0.62f, sw + 120 * s, h * 0.42f, 6 * s, with_alpha(0xFFFFFFFF, al / 4));
  rect(out, w / 2.0f - 240 * s * a, h * 0.585f, w / 2.0f + 240 * s * a, h * 0.585f + 2 * s, with_alpha(0xFF62F2DC, al));
  ftext(out, "A FAN-MADE 3D MODIFICATION", w / 2.0f, h * 0.64f, 24 * s, with_alpha(0xFF9CABB9, al), 0, 0, 12 * s);
}

// The original's credit lines (tl_credit, read off its title picture), each of its pixels a
// square dot wide, centred on cx with their bottom at y; a dark edge a third of a dot round them.
static void draw_credit(const TgView3D* v, TgV3List* out, float cx, float y, float dot, uint32_t col, uint32_t edge) {
  const int* b = v->tl_credit_box;
  if(b[2] <= b[0] || b[3] < b[1]) return;
  float x0 = cx - (b[2] - b[0] + 1) * dot / 2, y0 = y - (b[3] - b[1] + 1) * dot;
  for(int pass = 0; pass < 2; pass++) {
    float g = pass ? 0 : dot / 3;
    uint32_t c = pass ? col : edge;
    for(int r = b[1]; r <= b[3]; r++)
      for(int q = b[0]; q <= b[2];) {
        if(!v->tl_credit[r][q]) {
          q++;
          continue;
        }
        int q1 = q;
        while(q1 <= b[2] && v->tl_credit[r][q1]) q1++;
        rect(out, x0 + (q - b[0]) * dot - g, y0 + (r - b[1]) * dot - g, x0 + (q1 - b[0]) * dot + g, y0 + (r - b[1] + 1) * dot + g, c);
        q = q1;
      }
  }
}

static void draw_title_classic(TgView3D* v, int w, int h, float seconds, TgV3List* out);
static void draw_splash_classic(TgView3D* v, int w, int h, float seconds, TgV3List* out);
static void draw_title_modern(TgView3D* v, int w, int h, float seconds, TgV3List* out);

void tg_view3d_set_title_style(TgView3D* v, int style) { v->title_style = style; }

void tg_view3d_draw_title(TgView3D* v, int w, int h, float seconds, TgV3List* out) {
  tg_v3_list_clear(out);
  font_ready(v);
  if(v->splash_up && !v->title_up) {
    if(v->title_style == TG_V3_TITLE_CLASSIC) draw_splash_classic(v, w, h, seconds, out);
    else draw_splash(v, w, h, seconds, out);
    return;
  }
  if(v->title_style == TG_V3_TITLE_CLASSIC) draw_title_classic(v, w, h, seconds, out);
  else draw_title_modern(v, w, h, seconds, out);
}

// The keys this project adds, at the top left of both title screens (the Tab menu's Hotkeys
// page has them all).
static void title_keys(TgV3List* out, float s, int alpha) {
  static const char* const keys[3][2] = {{"F7", "2D / 3D"}, {"TAB", "OPTIONS & HOTKEYS"}, {"F11", "FULLSCREEN"}};
  float bx = 60 * s, y = 46 * s;
  for(int k = 0; k < 3; k++) {
    float kw = ftext_w(keys[k][0], 14 * s, 0, 2 * s) + 22 * s;
    rpanel(out, bx, y, bx + kw, y + 28 * s, 6 * s, with_alpha(0x0A1420, alpha * 3 / 4));
    ftext(out, keys[k][0], bx + kw / 2, y + 20 * s, 14 * s, with_alpha(0xFFFFFF, alpha), 0, 0, 2 * s);
    ftext(out, keys[k][1], bx + kw + 10 * s, y + 20 * s, 14 * s, with_alpha(0xD8E2EC, alpha), -1, 0, 3 * s);
    bx += kw + 30 * s + ftext_w(keys[k][1], 14 * s, 0, 3 * s);
  }
}

// The modern title: the project's emblem and name in chrome over a car on a grid, the
// original's credit lines (read off its own title) at the foot.
static void draw_title_modern(TgView3D* v, int w, int h, float seconds, TgV3List* out) {
  float s = h / 1080.0f, t = v->title_frames / 60.0f;
  const uint32_t white = 0xFFF0F5F7, cyan = 0xFF62F2DC;
  // Night sky to a glowing horizon, a floor grid running away.
  float hz = h * 0.62f;
  screen_rect(out, 0, 0, (float) w, hz, 0, 1, 0xFF070D18, 0xFF183A5A, -1);
  screen_rect(out, 0, hz - 1, (float) w, (float) h, 0, 1, 0xFF0F2436, 0xFF060A12, -1);
  screen_rect(out, 0, hz - 60 * s, (float) w, hz + 2, 0, 1, 0x0062F2DC, 0x6062F2DC, -1);
  for(int i = 1; i < 22; i++) {  // the floor's lines, rushing towards us
    float k = fmodf(i + seconds * 1.8f, 22) / 22, y = hz + (h - hz) * k * k;
    uint32_t c = with_alpha(cyan, (int) (90 * k));
    TgV3Vertex q[6] = {{0, y, 0, 0, c}, {(float) w, y, 0, 0, c}, {(float) w, y + 1.5f * s, 0, 0, c},
                       {0, y, 0, 0, c}, {(float) w, y + 1.5f * s, 0, 0, c}, {0, y + 1.5f * s, 0, 0, c}};
    list_push(out, q, 6, -1);
  }
  for(int i = -12; i <= 12; i++) {
    float x0 = w / 2.0f + i * w * 0.09f, x1 = w / 2.0f + i * w * 0.9f;
    sbar(out, x0, hz, x1, (float) h, 1.5f * s, 0x3062F2DC);
  }
  // The car, turning slowly on the floor.
  Gather g = {0};
  Cam cam;
  g_haze = 0xFF0F2436;
  select_car(v, &g, &cam, 0, w * 0.5f, h * 0.74f, h * 0.9f, 200 + seconds * 18, 9, 8.5f, (float) w, (float) h);
  qsort(g.it, (size_t) g.ni, sizeof(Item), cmp_item);
  for(int i = 0; i < g.ni; i++)
    for(int j = 0; j < g.it[i].count; j++) list_push(out, g.t[g.it[i].first + j].v, 3, g.t[g.it[i].first + j].pic);
  free(g.t);
  free(g.it);
  out->overlay = true;
  // The emblem and the name: upright, in chrome; they rise and fade in, then stay.
  float in = t < 1.0f ? 1 - (1 - t) * (1 - t) * (1 - t) : 1;
  int al = (int) (255 * fminf(1, t * 1.5f));
  const char* word = brand_caps();
  float size = 150 * s, track = 12 * s, ly = h * 0.33f + (1 - in) * 40 * s;
  float tw = ftext_w(word, size, 0, track), er = 52 * s, gap = 40 * s;
  float x0 = w / 2.0f - (tw + gap + er * 2.9f) / 2;  // the emblem's left, the lot centred
  screen_rect(out, w / 2.0f - tw * 0.75f, ly - size * 1.2f, w / 2.0f + tw * 0.75f, ly + size * 0.6f, 0, 1, 0x40FF7A30, 0x40FF7A30, TEX_GLOW);
  emblem(out, x0 + er * 1.45f, ly - size * 0.36f, er, seconds, al);
  float wx = x0 + er * 2.9f + gap + tw / 2;
  wordmark(out, word, wx, ly, size, track, seconds, al);
  // A rule from gold to orange under the name, the line in it.
  {
    float ry = ly + 30 * s, rx0 = wx - tw / 2, rx1 = wx + tw / 2;
    const char* sub = "3D RACING MOD";
    float sw = ftext_w(sub, 26 * s, 0, 14 * s);
    ftext(out, sub, rx0, ry + 32 * s, 26 * s, with_alpha(0xFFFFC440, al), -1, 0, 14 * s);
    float r0 = rx0 + sw + 24 * s;
    if(rx1 - r0 > 20 * s) {
      uint32_t c0 = with_alpha(0xFFFFC440, al), c1 = with_alpha(0xFFFF5C38, al);
      float y0 = ry + 18 * s, y1 = ry + 21 * s;
      TgV3Vertex q[6] = {{r0, y0, 0, 0, c0}, {rx1, y0, 0, 0, c1}, {rx1, y1, 0, 0, c1}, {r0, y0, 0, 0, c0}, {rx1, y1, 0, 0, c1}, {r0, y1, 0, 0, c0}};
      list_push(out, q, 6, -1);
    }
  }
  // PRESS START, breathing; the credits.
  if(t > 1.0f) {
    int a = (int) (160 + 95 * sinf(seconds * 3.2f));
    ftext(out, "PRESS START", w / 2.0f, h * 0.865f, 30 * s, with_alpha(white, a), 0, 0, 10 * s);
  }
  title_keys(out, s, al * 9 / 10);
  // The fan disclaimer, then the original's own credit lines, read off its title.
  ftext(out, "A FAN-MADE MODIFICATION  -  NOT FOR SALE", w / 2.0f, h * 0.895f, 17 * s, white, 0, 0, 4 * s);
  ftext(out, "A love letter to the game that made my childhood so happy.", w / 2.0f, h * 0.92f, 19 * s, cyan, 0, 1, 1 * s);
  draw_credit(v, out, w / 2.0f, h - 6 * s, 1.5f * s, 0xFFB8C4D0, 0xA0000008);
  ftext(out, "POWERED BY ARTIFICIAL INTELLIGENCE", w - 60 * s, 60 * s, 14 * s, cyan, 1, 0, 5 * s);
  ftext(out, "WITH HUMAN STUBBORNNESS", w - 60 * s, 84 * s, 14 * s, white, 1, 0, 5 * s);
}

// --- The classic title ------------------------------------------------------------------
// The original's own logo rebuilt as a solid, like a voxel model: its pixels (read off the
// game's picture, capture_title_logo) merged into rectangles of one part each (greedy
// meshing), the face raised a little over its purple rim, and every outer edge extruded back
// into real depth: dark purple sides that end in the original's pale purple line, where the
// original painted a drop under the letters. It turns gently under one light, a glint
// sweeping over it.

typedef struct {
  Gather* g;
  float rot[9];         // the object's rotation (rows)
  float k;              // world units per pixel
  float px, py, sc;     // a grid's pixel (px, py) scaled by sc about it ...
  float ox, oy;         // ... lands this far (pixels) from the object's centre
  float depth, raise;   // the extrusion and the face over the rim (pixels)
  float sweep;          // the glint's band (pixels along x - 0.6 y from the centre)
  V3 light;             // towards the light (world)
  V3 lamp;              // a lamp near it, for the highlight (world)
} TlObj;

static V3 tl_rot(const TlObj* o, V3 a) {
  return v3(o->rot[0] * a.x + o->rot[1] * a.y + o->rot[2] * a.z, o->rot[3] * a.x + o->rot[4] * a.y + o->rot[5] * a.z,
            o->rot[6] * a.x + o->rot[7] * a.y + o->rot[8] * a.z);
}

// Pixels from the object's centre (x right, y down) of grid point (x, y).
static float tl_cx(const TlObj* o, float x) { return (x - o->px) * o->sc + o->ox; }
static float tl_cy(const TlObj* o, float y) { return (y - o->py) * o->sc + o->oy; }

static V3 tl_at(const TlObj* o, float x, float y, float z) {
  return tl_rot(o, v3(tl_cx(o, x) * o->k, -tl_cy(o, y) * o->k, z * o->sc * o->k));
}

// A front face's colour at p: lit, a soft highlight that moves as it turns, the glint's band.
static uint32_t tl_front(const TlObj* o, uint32_t c, V3 n, V3 p, float x, float y, float gloss, uint32_t glint) {
  float l = vdot(n, o->light);
  uint32_t col = shade(c, 0.9f + 0.16f * (l > 0 ? l : 0));
  V3 hv = vnorm(vadd(vnorm(vsub(o->lamp, p)), vnorm(vsub(o->g->cam->eye, p))));
  float sp = vdot(n, hv);
  sp = sp > 0 ? powf(sp, 90) * gloss : 0;
  float d = (tl_cx(o, x) - 0.6f * tl_cy(o, y) - o->sweep) / 13;
  float band = expf(-d * d) * 0.75f;
  col = mix(col, glint, band > 0.9f ? 0.9f : band);
  return mix(col, 0xFFFFFFFF, sp > 0.8f ? 0.8f : sp);
}

static void tl_quad(const TlObj* o, V3 a, V3 b, V3 c, V3 d, uint32_t ca, uint32_t cb, uint32_t cc, uint32_t cd) {
  begin_item(o->g, to_cam(o->g->cam, vmul(vadd(a, c), 0.5f)).z);
  quad_shaded(o->g, a, b, c, d, ca, cb, cc, cd);
}

// The face's colour at a grid corner: the face pixels round it, averaged.
static uint32_t tl_corner(const uint8_t* cls, const uint32_t* col, int W, int H, int x, int y) {
  uint32_t r = 0, g = 0, b = 0, n = 0;
  for(int dy = -1; dy <= 0; dy++)
    for(int dx = -1; dx <= 0; dx++) {
      int xx = x + dx, yy = y + dy;
      if(xx < 0 || yy < 0 || xx >= W || yy >= H) continue;
      uint8_t k = cls[yy * W + xx];
      if(k != TL_FACE && k != TL_HI) continue;
      uint32_t c = col[yy * W + xx];
      r += c >> 16 & 255, g += c >> 8 & 255, b += c & 255, n++;
    }
  return n ? 0xFF000000u | (r / n) << 16 | (g / n) << 8 | (b / n) : 0xFFF08030;
}

static void tl_mesh(const TlObj* o, const uint8_t* cls, const uint32_t* col, int W, int H, uint32_t rim) {
  static uint8_t used[256 * 224];
  if(W * H > (int) sizeof(used)) return;
#define TLK(x, y) ((x) < 0 || (y) < 0 || (x) >= W || (y) >= H ? TL_NONE : cls[(y) * W + (x)])
#define TLFACE(k) ((k) == TL_FACE || (k) == TL_HI)
  const V3 eye = o->g->cam->eye;
  // The front: the raised face (and its bright edge), the rim; rectangles of one part, the
  // face's no bigger than 4 x 4 so its gradient keeps its shape.
  V3 nf = tl_rot(o, v3(0, 0, -1));
  memset(used, 0, (size_t) (W * H));
  static const uint8_t kParts[3] = {TL_FACE, TL_HI, TL_RIM};
  for(int pi = 0; pi < 3; pi++) {
    uint8_t part = kParts[pi];
    int maxw = part == TL_RIM ? 6 : 4, maxh = maxw;
    float z = part == TL_RIM ? 0 : -o->raise;
    for(int y = 0; y < H; y++)
      for(int x = 0; x < W; x++) {
        if(cls[y * W + x] != part || used[y * W + x]) continue;
        int ww = 1, hh = 1;
        while(x + ww < W && ww < maxw && cls[y * W + x + ww] == part && !used[y * W + x + ww]) ww++;
        for(bool ok = true; ok && y + hh < H && hh < maxh;) {
          for(int i = 0; i < ww && ok; i++) ok = cls[(y + hh) * W + x + i] == part && !used[(y + hh) * W + x + i];
          if(ok) hh++;
        }
        for(int j = 0; j < hh; j++) memset(&used[(y + j) * W + x], 1, (size_t) ww);
        float xs[4] = {(float) x, (float) (x + ww), (float) (x + ww), (float) x}, ys[4] = {(float) y, (float) y, (float) (y + hh), (float) (y + hh)};
        V3 p[4];
        uint32_t c[4];
        for(int k = 0; k < 4; k++) {
          p[k] = tl_at(o, xs[k], ys[k], z);
          uint32_t base = part == TL_RIM ? rim : tl_corner(cls, col, W, H, (int) xs[k], (int) ys[k]);
          if(part == TL_HI) base = mix(base, 0xFFFFFFA0, 0.6f);
          c[k] = part == TL_RIM ? tl_front(o, base, nf, p[k], xs[k], ys[k], 0.25f, 0xFFC4B4FF)
                                : tl_front(o, base, nf, p[k], xs[k], ys[k], 0.55f, 0xFFFFFFF0);
        }
        if(vdot(nf, vsub(p[0], eye)) < 0) tl_quad(o, p[0], p[1], p[2], p[3], c[0], c[1], c[2], c[3]);
      }
  }
  // The sides: along each grid line, runs of the same kind of edge: outer (the solid against
  // the sky, back to the full depth) or inner (the raised face against its rim).
  static const struct { int dx, dy; float nx, ny; } kDir[4] = {{-1, 0, -1, 0}, {1, 0, 1, 0}, {0, -1, 0, 1}, {0, 1, 0, -1}};
  for(int d = 0; d < 4; d++) {
    int dx = kDir[d].dx, dy = kDir[d].dy;
    bool vert = dx != 0;  // a wall along y (left or right side)
    V3 n = tl_rot(o, v3(kDir[d].nx, kDir[d].ny, 0));
    float ln = vdot(n, o->light);
    ln = ln > 0 ? ln : 0;
    int lines = vert ? W : H, len = vert ? H : W;
    for(int li = 0; li < lines; li++) {
      for(int j = 0; j < len;) {
        int x = vert ? li : j, y = vert ? j : li;
        uint8_t k = TLK(x, y), nb = TLK(x + dx, y + dy);
        int kind = !k ? 0 : !nb ? (TLFACE(k) ? 2 : 1) : (TLFACE(k) && nb == TL_RIM ? 3 : 0);
        if(!kind) {
          j++;
          continue;
        }
        int j1 = j + 1;
        for(; j1 < len && (kind != 3 || j1 - j < 4); j1++) {
          int x1 = vert ? li : j1, y1 = vert ? j1 : li;
          uint8_t k1 = TLK(x1, y1), nb1 = TLK(x1 + dx, y1 + dy);
          int kind1 = !k1 ? 0 : !nb1 ? (TLFACE(k1) ? 2 : 1) : (TLFACE(k1) && nb1 == TL_RIM ? 3 : 0);
          if(kind1 != kind) break;
        }
        // The line: from (ax, ay) to (bx, by) on the grid.
        float lx = (float) (vert ? li + (dx > 0) : j), ly = (float) (vert ? j : li + (dy > 0));
        float ax = lx, ay = ly, bx = vert ? lx : (float) j1, by = vert ? (float) j1 : ly;
        float z0 = kind == 1 ? 0 : -o->raise, z1 = kind == 3 ? 0 : o->depth;
        V3 a0 = tl_at(o, ax, ay, z0), b0 = tl_at(o, bx, by, z0), a1 = tl_at(o, ax, ay, z1), b1 = tl_at(o, bx, by, z1);
        V3 mid = vmul(vadd(a0, b1), 0.5f);
        if(vdot(n, vsub(mid, eye)) < 0) {
          if(kind == 3) {  // the bevel round the raised face: the face's colour, lit
            float lf = 0.5f + 0.75f * ln;
            uint32_t ca = shade(tl_corner(cls, col, W, H, (int) ax, (int) ay), lf);
            uint32_t cb = shade(tl_corner(cls, col, W, H, (int) bx, (int) by), lf);
            tl_quad(o, a0, b0, b1, a1, ca, cb, cb, ca);
          } else {  // the side: dark purple to near black, the pale line at its back edge
            float lf = 0.32f + 0.95f * ln, zs = o->depth * 0.84f;
            uint32_t front = shade(0xFF2C1A78, lf), back = shade(0xFF050312, lf), line = shade(0xFF8C8CFF, 0.55f + 0.6f * ln);
            V3 as = tl_at(o, ax, ay, zs), bs = tl_at(o, bx, by, zs);
            tl_quad(o, a0, b0, bs, as, front, front, back, back);
            tl_quad(o, as, bs, b1, a1, line, line, line, line);
          }
        }
        j = j1;
      }
    }
  }
#undef TLK
#undef TLFACE
}

// The logo as vector shapes (logo_vector.c: traced, fitted with lines, filled, extruded),
// lit as the pixels were: the fronts with the light, a highlight and the glint, the sides
// darker. bright: the whole of it towards black (a fade).
static const LvStyle kLogoStyle = {1.6f, 26, 0.55f, 0.9f, 0.84f, 5, 5, 5, 0.9f, true, 0xFF6B39DE, 0xFF2C1A78, 0xFF050312, 0xFF8C8CFF};

static void tl_vdraw(const TlObj* o, const LvMesh* m, float bright);

static void tl_vmesh(const TlObj* o, LvMesh* m, const uint8_t* cls, const uint32_t* col, int W, int H, const LvStyle* st, float bright) {
  lv_build(m, cls, col, W, H, st);
  tl_vdraw(o, m, bright);
}

static void tl_vdraw(const TlObj* o, const LvMesh* m, float bright) {
  if(getenv("TG_LV_STATS")) fprintf(stderr, "logo: %d shapes, %d corners, %d polygons, built %d times, last %.2f ms\n", m->shapes, m->corners, m->np, m->builds, m->ms);
  const V3 eye = o->g->cam->eye;
  for(int i = 0; i < m->np; i++) {
    const LvPoly* q = &m->p[i];
    V3 n = tl_rot(o, v3(q->nx, -q->ny, q->nz)), p[LV_MAXV];
    for(int k = 0; k < q->n; k++) p[k] = tl_at(o, q->v[k].x, q->v[k].y, q->v[k].z);
    if(vdot(n, vsub(p[0], eye)) >= 0) continue;
    float ln = fmaxf(0, vdot(n, o->light));
    CV cv[LV_MAXV];
    for(int k = 0; k < q->n; k++) {
      uint32_t c = q->v[k].c;
      switch(q->mat) {
        case LV_M_FACE: c = tl_front(o, c, n, p[k], q->v[k].x, q->v[k].y, 0.55f, 0xFFFFFFF0); break;
        case LV_M_RIM: c = tl_front(o, c, n, p[k], q->v[k].x, q->v[k].y, 0.25f, 0xFFC4B4FF); break;
        case LV_M_FBEVEL: c = tl_front(o, shade(c, 0.62f + 0.62f * ln), n, p[k], q->v[k].x, q->v[k].y, 0.8f, 0xFFFFFFF0); break;
        case LV_M_RBEVEL: c = shade(c, 0.45f + 0.8f * ln); break;
        case LV_M_SIDE: c = shade(c, 0.32f + 0.95f * ln); break;
        default: c = shade(c, 0.55f + 0.6f * ln); break;
      }
      if(bright < 1) c = shade(c, bright);
      cv[k] = (CV) {to_cam(o->g->cam, p[k]), 0, 0, c};
    }
    begin_item(o->g, cv[0].p.z);
    emit_poly(o->g, cv, q->n, cv[0].c, -1);
  }
}

// The classic look's publisher splash: the original's own logo (read off the screen, traced
// into vector shapes), a solid in its colours turning gently under the light, faded in and
// out with the original's picture.
static void draw_splash_classic(TgView3D* v, int w, int h, float seconds, TgV3List* out) {
  const LvSplash* sp = &v->lv_splash;
  float s = h / 1080.0f, P = 4.6f * s, f = sp->bright;
  screen_rect(out, 0, 0, (float) w, (float) h, 0, 1, 0xFF000000, 0xFF000000, -1);
  float lcx = w * 0.5f + (sp->cx - 128) * P, lcy = h * 0.5f + (sp->cy - 112) * P;
  int ga = (int) (70 * f);
  screen_rect(out, lcx - 90 * P, lcy - 40 * P, lcx + 90 * P, lcy + 40 * P, 0, 1, with_alpha(0xFF3A3AFF, ga), with_alpha(0xFF3A3AFF, ga), TEX_GLOW);
  Gather g = {0};
  Cam cam;
  const float dist = 24, k = 0.04f;
  cam.eye = v3(0, 0, -dist), cam.fwd = v3(0, 0, 1), cam.right = v3(1, 0, 0), cam.up = v3(0, 1, 0);
  cam.f = P * dist / k, cam.cx = lcx, cam.cy = lcy, cam.w = (float) w, cam.h = (float) h;
  g.cam = &cam;
  g_haze = 0xFF000000;
  float yaw = 10 * sinf(seconds * 0.5f) * PI / 180, pitch = (11 + 2 * sinf(seconds * 0.37f)) * PI / 180;
  float cyw = cosf(yaw), syw = sinf(yaw), cp = cosf(pitch), spi = sinf(pitch);
  TlObj o = {&g, {cyw, syw * spi, syw * cp, 0, cp, -spi, -syw, cyw * spi, cyw * cp}, k, sp->cx, sp->cy, 1, 0, 0, 14, 1.4f, 0,
             vnorm(v3(-0.45f, 0.6f, -0.65f)), v3(-2.5f, 2.0f, -9)};
  float ph = fmodf(seconds, 3.5f) / 1.5f;
  o.sweep = ph < 1 ? -90 + ph * 200 : 1e9f;
  static const LvStyle kKemco = {1.4f, 14, 0.5f, 0.8f, 1.0f, 200, 0, 3, 0.4f, true, 0, 0xFF161670, 0xFF02020E, 0xFF02020E};
  LvStyle ks = kKemco;
  ks.rim = sp->rim;
  if(sp->valid && f > 0.01f) tl_vmesh(&o, &v->lv_kemco, sp->cls, sp->col, 256, 224, &ks, f);
  // The word under it: from a font's outlines (logo_glyphs.h), white shaded to blue, fading on its own.
  static const LvStyle kWord = {0.8f, 7, 0.35f, 0.4f, 1.0f, 0, 0, 3, 0.9f, false, 0xFF2A2A90, 0xFF16166A, 0xFF03031A, 0xFF03031A};
  if(sp->nword == 8 && sp->word_bright > 0.01f) {
    lv_build_word(&v->lv_word, sp, &kWord, 0xFFFFFFFF, 0xFFB4BCFF);
    tl_vdraw(&o, &v->lv_word, sp->word_bright);
  }
  qsort(g.it, (size_t) g.ni, sizeof(Item), cmp_item);
  for(int i = 0; i < g.ni; i++)
    for(int j = 0; j < g.it[i].count; j++) list_push(out, g.t[g.it[i].first + j].v, 3, g.t[g.it[i].first + j].pic);
  free(g.t);
  free(g.it);
  out->overlay = true;
}

// Text as on the original's title: italic, white, a dark outline and a drop to the right.
static void otext(TgV3List* l, const char* str, float x, float y, float size, uint32_t col, uint32_t edge, int align, int face,
                  float track, float o) {
  // (the edge's passes pile up: fading, they fade faster than the letters)
  float f = (col >> 24) / 255.0f;
  int a = (int) ((edge >> 24) * f * f * f);
  ftext(l, str, x + o * 1.1f, y + o * 1.6f, size, with_alpha(edge, a * 3 / 4), align, face, track);
  for(int k = 0; k < 8; k++) ftext(l, str, x + cosf(k * PI / 4) * o, y + sinf(k * PI / 4) * o, size, with_alpha(edge, a), align, face, track);
  ftext(l, str, x, y, size, col, align, face, track);
}

// A strip on the screen from (xa, ya) to (xb, yb): ta / tb thick and ca / cb coloured at
// each end (a grid line that thins and fades into the distance).
static void taper(TgV3List* l, float xa, float ya, float xb, float yb, float ta, float tb, uint32_t ca, uint32_t cb) {
  float dx = xb - xa, dy = yb - ya, n = sqrtf(dx * dx + dy * dy);
  if(n < 1e-3f) return;
  float nx = -dy / n, ny = dx / n;
  TgV3Vertex q[6] = {{xa - nx * ta / 2, ya - ny * ta / 2, 0, 0, ca}, {xa + nx * ta / 2, ya + ny * ta / 2, 0, 0, ca},
                     {xb + nx * tb / 2, yb + ny * tb / 2, 0, 0, cb}, {xa - nx * ta / 2, ya - ny * ta / 2, 0, 0, ca},
                     {xb + nx * tb / 2, yb + ny * tb / 2, 0, 0, cb}, {xb - nx * tb / 2, yb - ny * tb / 2, 0, 0, cb}};
  list_push(l, q, 6, -1);
}

static void draw_title_classic(TgView3D* v, int w, int h, float seconds, TgV3List* out) {
  float s = h / 1080.0f, t = v->title_frames / 60.0f;
  // The composition (player: "the logo screen should also have the car, the gradient, the
  // grid and the new logo"): the original's sky as a rich gradient over a neon floor grid
  // running to a glowing horizon, the modern title's car turning on the floor, the classic
  // logo the hero above it, the words in bands round them. Proportions from the height, so
  // 16:9 and 21:9 keep the same layout (the wider picture gets more floor and sky).
  const float hz = h * 0.60f;  // the horizon
  const uint32_t cyan = 0xFF00F4FF;
  // The sky: the original's colours (navy, near black, deep blue, cyan) without its dither,
  // a violet dusk between them, the cyan as a glow along the horizon.
  screen_rect(out, 0, 0, (float) w, h * 0.16f, 0, 1, 0xFF00002E, 0xFF02010C, -1);
  screen_rect(out, 0, h * 0.16f - 1, (float) w, h * 0.42f, 0, 1, 0xFF02010C, 0xFF1A0C9A, -1);
  screen_rect(out, 0, h * 0.42f - 1, (float) w, hz - h * 0.055f, 0, 1, 0xFF1A0C9A, 0xFF1446F0, -1);
  screen_rect(out, 0, hz - h * 0.055f - 1, (float) w, hz + 1, 0, 1, 0xFF1446F0, 0xFF22D2FF, -1);
  // The sun's glow at the vanishing point (warm, like the logo's colours), the horizon's.
  screen_rect(out, w * 0.5f - h * 0.75f, hz - h * 0.30f, w * 0.5f + h * 0.75f, hz + h * 0.10f, 0, 1, 0x50FF5AB4, 0x50FF5AB4, TEX_GLOW);
  screen_rect(out, w * 0.5f - h * 0.40f, hz - h * 0.12f, w * 0.5f + h * 0.40f, hz + h * 0.05f, 0, 1, 0x60FFB050, 0x60FFB050, TEX_GLOW);
  // Faint motes drifting up through the sky at three depths, twinkling in the dark above.
  for(int i = 0; i < 64; i++) {
    uint32_t hs = hash32((uint32_t) i * 2654435761u + 17);
    float depth = 0.35f + (hs & 3) * 0.32f, speed = 0.012f * depth;
    float x = (hs >> 4 & 1023) / 1023.0f * w, y = fmodf((hs >> 14 & 1023) / 1023.0f + 2 - seconds * speed, 1.0f) * hz * 1.05f - h * 0.03f;
    float r = (2.2f + 3.5f * depth) * s, tw = 0.6f + 0.4f * sinf(seconds * (1.3f + (hs >> 24 & 7) * 0.3f) + i);
    int a = (int) ((y < h * 0.40f ? 150 : 60) * tw * depth);
    if(y > hz - 6 * s) continue;
    screen_rect(out, x - r * 3, y - r * 3, x + r * 3, y + r * 3, 0, 1, with_alpha(0xFFBFE8FF, a / 3), with_alpha(0xFFBFE8FF, a / 3), TEX_GLOW);
    screen_rect(out, x - r * 0.7f, y - r * 0.7f, x + r * 0.7f, y + r * 0.7f, 0, 1, with_alpha(0xFFFFFFFF, a), with_alpha(0xFFFFFFFF, a), TEX_GLOW);
  }
  // The floor: deep blue to near black, a grid in perspective (a floor 1 unit under the eye:
  // a line z ahead is (h - hz) / z under the horizon) rushing towards us, its lines thinning
  // and fading into the distance; the horizon a bright cyan line with a glow.
  screen_rect(out, 0, hz, (float) w, (float) h, 0, 1, 0xFF0A1250, 0xFF02030E, -1);
  {
    const float F = h - hz, step = 0.55f;
    float scroll = fmodf(seconds * 1.6f, step);
    for(int i = 0; i < 40; i++) {
      float z = 1 + i * step - scroll;
      if(z < 0.98f) continue;
      float y = hz + F / z, a = fminf(1, 2.4f / z) * fmaxf(0, 1 - z / 20);
      if(y > h + 4 * s) continue;
      uint32_t c = with_alpha(cyan, (int) (170 * a));
      taper(out, 0, y, (float) w, y, fmaxf(0.8f, 3.2f * s / z), fmaxf(0.8f, 3.2f * s / z), c, c);
    }
    float zfar = 16;
    for(int i = -48; i <= 48; i++) {
      float X = i * 0.55f;  // the lateral line: from the picture's foot (or its side) to the horizon
      float zb = 0.98f, xb = w * 0.5f + F * X / zb;
      if(xb < -40 * s || xb > w + 40 * s) {  // it leaves by the side: start where it does
        zb = F * fabsf(X) / (w * 0.5f + 40 * s);
        xb = w * 0.5f + F * X / zb;
      }
      if(zb > zfar * 0.7f) continue;
      float yb = hz + F / zb, a = fminf(1, 2.4f / zb) * (1 - zb / zfar);
      taper(out, xb, yb, w * 0.5f + F * X / zfar, hz + F / zfar, fmaxf(0.8f, 3.2f * s / zb), 0.6f, with_alpha(cyan, (int) (170 * a)),
            with_alpha(cyan, 0));
    }
    screen_rect(out, 0, hz - 26 * s, (float) w, hz, 0, 1, with_alpha(cyan, 0), with_alpha(cyan, 0x70), -1);
    screen_rect(out, 0, hz, (float) w, hz + h * 0.075f, 0, 1, 0xD00A1250, 0x000A1250, -1);  // haze over the far grid
    screen_rect(out, 0, hz, (float) w, hz + 40 * s, 0, 1, with_alpha(cyan, 0x50), with_alpha(cyan, 0), -1);
    rect(out, 0, hz - 1.2f * s, (float) w, hz + 1.2f * s, 0xFFB8FFFF);
  }
  // The logo's place: its centre and its size (screen pixels per pixel of the game's).
  float lcx = w * 0.5f, lcy = h * 0.285f, P = 3.55f * s;
  // The edition's words once the logo is whole.
  float done = v->tl_lic_frames / 60.0f;
  bool whole = v->tl_lic || t > 6;
  if(!v->tl_lic && t > 6) done = t - 6;
  int fa = whole ? (int) (255 * fminf(1, done / 0.8f)) : 0;
  // A warm glow behind the logo, a cool one under it.
  if(v->tl_n > 0) {
    screen_rect(out, lcx - 170 * P, lcy - 80 * P, lcx + 170 * P, lcy + 80 * P, 0, 1, 0x40FF7A30, 0x40FF7A30, TEX_GLOW);
    screen_rect(out, lcx - 120 * P, lcy - 20 * P, lcx + 120 * P, lcy + 70 * P, 0, 1, 0x305A3AFF, 0x305A3AFF, TEX_GLOW);
  }
  // The car on the floor, turning slowly (the modern title's), a glow under it. It drives in
  // from the distance once the logo is whole (the original's fly-in comes first).
  Gather g = {0};
  Cam cam;
  float carIn = whole ? fminf(1, done / 1.2f) : 0;
  if(carIn > 0) {
    float e = 1 - (1 - carIn) * (1 - carIn) * (1 - carIn), cy = h * 0.705f - (1 - e) * h * 0.11f;
    float focal = h * 0.84f * (0.35f + 0.65f * e);
    screen_rect(out, w * 0.5f - h * 0.36f, cy - h * 0.02f, w * 0.5f + h * 0.36f, cy + h * 0.12f, 0, 1, with_alpha(0xFF00C8FF, (int) (110 * e)),
                with_alpha(0xFF00C8FF, (int) (110 * e)), TEX_GLOW);
    g_haze = 0xFF0A1250;
    g_alpha = fminf(1, e * 3);  // solid early: it comes from the distance, not a ghost
    select_car(v, &g, &cam, 0, w * 0.5f, cy, focal, 200 + seconds * 18, 9, 8.5f, (float) w, (float) h);
    g_alpha = 1;
    qsort(g.it, (size_t) g.ni, sizeof(Item), cmp_item);
    for(int i = 0; i < g.ni; i++)
      for(int j = 0; j < g.it[i].count; j++) list_push(out, g.t[g.it[i].first + j].v, 3, g.t[g.it[i].first + j].pic);
    g.nt = g.ni = 0;
  }
  // The logo: an object turning slowly, tipped so its underside shows as the original drew it.
  // Built at a tenth of the car's scale and as near: the same picture, and always in front of
  // the car in the depth buffer (both are world objects).
  const float dist = 2.4f, k = 0.004f;
  cam.eye = v3(0, 0, -dist), cam.fwd = v3(0, 0, 1), cam.right = v3(1, 0, 0), cam.up = v3(0, 1, 0);
  cam.f = P * dist / k, cam.cx = lcx, cam.cy = lcy, cam.w = (float) w, cam.h = (float) h;
  g.cam = &cam;
  g_haze = 0xFF000020;
  float yaw = 15 * sinf(seconds * 0.42f) * PI / 180, pitch = (13 + 3 * sinf(seconds * 0.31f)) * PI / 180;
  float cyw = cosf(yaw), syw = sinf(yaw), cp = cosf(pitch), sp = sinf(pitch);
  TlObj o = {&g, {cyw, syw * sp, syw * cp, 0, cp, -sp, -syw, cyw * sp, cyw * cp}, k, 161, 100, 1, 0, 0, 26, 1.6f, 0, vnorm(v3(-0.45f, 0.6f, -0.65f)), v3(-0.25f, 0.2f, -0.9f)};
  float ph = fmodf(seconds, 5.5f) / 1.5f;
  o.sweep = ph < 1 ? -200 + ph * 420 : 1e9f;
  const bool pixels = getenv("TG_LOGO_PIXELS") != NULL;  // (the logo as it was: a voxel model)
  LvStyle ls = kLogoStyle;
  ls.rim = v->tl_rim;
  if(v->tl_n > 0 && pixels) tl_mesh(&o, &v->tl_cls[0][0], &v->tl_col[0][0], 256, 224, v->tl_rim);
  else if(v->tl_n > 0) tl_vmesh(&o, &v->lv_logo, &v->tl_cls[0][0], &v->tl_col[0][0], 256, 224, &ls, 1);
  qsort(g.it, (size_t) g.ni, sizeof(Item), cmp_item);
  for(int i = 0; i < g.ni; i++)
    for(int j = 0; j < g.it[i].count; j++) list_push(out, g.t[g.it[i].first + j].v, 3, g.t[g.it[i].first + j].pic);
  free(g.t);
  free(g.it);
  out->overlay = true;
  // A sparkle on each star's point as the glint crosses it.
  if(v->tl_n > 0)
    for(int i = 0; i < 2; i++) {
      float gx = i ? 219.0f : 185.0f, gy = 66.0f;
      float d = ((gx - 161) - 0.6f * (gy - 100) - o.sweep) / 16, a = expf(-d * d);
      a = fmaxf(a, 0.25f * fmaxf(0, sinf(seconds * 1.7f + i * 2.1f)));
      if(a < 0.02f || v->tl_cls[(int) gy][(int) gx] == TL_NONE) continue;
      V3 p = to_cam(&cam, tl_at(&o, gx, gy, -o.raise));
      float sx = cam.cx + p.x * cam.f / p.z, sy = cam.cy - p.y * cam.f / p.z, r = 26 * s * a;
      screen_rect(out, sx - r * 2, sy - r * 2, sx + r * 2, sy + r * 2, 0, 1, with_alpha(0xFFFFF0C0, (int) (150 * a)), with_alpha(0xFFFFF0C0, (int) (150 * a)), TEX_GLOW);
      sbar(out, sx - r * 2.2f, sy, sx + r * 2.2f, sy, 2.2f * s, with_alpha(0xFFFFFFFF, (int) (230 * a)));
      sbar(out, sx, sy - r * 2.2f, sx, sy + r * 2.2f, 2.2f * s, with_alpha(0xFFFFFFFF, (int) (230 * a)));
    }
  // The words: the edition and the tagline under the logo once it is whole; PRESS START
  // blinking with the original's; its licence lines; the fan notes.
  const uint32_t white = 0xFFFFFFFF, edge = 0xFF000010;
  float o1 = 2.6f * s;
  if(fa > 0) {
    otext(out, brand_caps(), w / 2.0f, h * 0.528f, 34 * s, with_alpha(0xFFFFD040, fa), with_alpha(0xFF2A1468, fa), 0, 0, 12 * s, o1);
    otext(out, "A love letter to the game that made my childhood so happy.", w / 2.0f, h * 0.566f, 22 * s, with_alpha(white, fa * 9 / 10),
          with_alpha(0xFF000830, fa * 2 / 3), 0, 1, 1 * s, 1.6f * s);
  }
  if(v->tl_press || (!v->tl_lic && t > 6 && fmodf(seconds, 1.0f) < 0.6f)) otext(out, "PRESS START", w / 2.0f, h * 0.872f, 42 * s, white, edge, 0, 2, 7 * s, o1);
  if(fa > 0) {
    // The original's credit lines, as its own title shows them (read off its picture).
    draw_credit(v, out, w / 2.0f, h - 40 * s, 2.3f * s, with_alpha(white, fa), with_alpha(edge, fa * 3 / 4));
    otext(out, "A FAN-MADE MODIFICATION  -  NOT FOR SALE", w / 2.0f, h - 18 * s, 15 * s, with_alpha(0xFFB8C8E8, fa), with_alpha(0xFF000010, fa / 2), 0, 0, 5 * s, 0.8f * s);
  }
  title_keys(out, s, 230);
  ftext(out, "POWERED BY ARTIFICIAL INTELLIGENCE", w - 60 * s, 60 * s, 14 * s, 0xFF62F2DC, 1, 0, 5 * s);
  ftext(out, "WITH HUMAN STUBBORNNESS", w - 60 * s, 84 * s, 14 * s, 0xFFF0F5F7, 1, 0, 5 * s);
}

// --- Any other menu, redrawn -------------------------------------------------------------
void tg_view3d_menu_dump(const TgView3D* v) {
  fprintf(stderr, "scroll %d tiles %04x\n", v->menu_scroll, v->menu_tiles);
  for(int i = 0; i < v->menu_n; i++)
    fprintf(stderr, "item %d r%d c%d big%d [%s]%s\n", i, v->menu_item[i].row, v->menu_item[i].col, v->menu_item[i].big, v->menu_item[i].text,
            i == v->menu_sel ? " <" : "");
}

bool tg_view3d_menu_up(const TgView3D* v) { return (v->menu_up > 0 || v->loading_up) && !v->select_up && !v->title_up; }

static void draw_menu_modern(TgView3D* v, int w, int h, float seconds, TgV3List* out, int header, int label);
static void draw_country(TgView3D* v, int w, int h, float seconds, TgV3List* out);
static void draw_results(TgView3D* v, int w, int h, float seconds, TgV3List* out, int header);
static void menu_photo(const TgView3D* v, TgV3List* out, int w, int h, float seconds);
static void draw_message(TgView3D* v, int w, int h, float seconds, TgV3List* out);
static void menu_frame(const TgView3D* v, TgV3List* out, int w, int h, float seconds, const char* title);

// Between a menu and its race: where the race is, a bar running while it loads.
static void draw_loading(TgView3D* v, int w, int h, float seconds, TgV3List* out) {
  float s = h / 1080.0f, t = v->loading_frames / 60.0f;
  const uint32_t white = 0xFFF0F5F7, cyan = 0xFF62F2DC, muted = 0xFF8A9AAA;
  menu_frame(v, out, w, h, seconds, "GET READY");
  char name[40], country[40], line[96];
  course_name(v->menu_course, name, country, sizeof(name));
  float y = h * 0.52f;
  ftext(out, "NEXT", 78 * s, y - 120 * s, 18 * s, cyan, -1, 0, 5 * s);
  ftext(out, name, 72 * s, y, 120 * s, white, -1, 2, 0);
  snprintf(line, sizeof(line), "%s   /   RACE %d OF 4", country, (v->menu_course & 3) + 1);
  ftext(out, line, 78 * s, y + 60 * s, 26 * s, muted, -1, 0, 4 * s);
  // An indeterminate bar: a light running along a track.
  float bx0 = 78 * s, bx1 = fminf(w - 78 * s, 78 * s + 900 * s), by = y + 120 * s;
  rect(out, bx0, by, bx1, by + 4 * s, 0x30A9D2E0);
  float k = fmodf(t * 0.8f, 1.0f), seg = (bx1 - bx0) * 0.25f, sx = bx0 + (bx1 - bx0 + seg) * k - seg;
  rect(out, fmaxf(bx0, sx), by, fminf(bx1, sx + seg), by + 4 * s, cyan);
  ftext(out, "LOADING", bx0, by + 40 * s, 14 * s, muted, -1, 0, 5 * s);
}

void tg_view3d_draw_menu(TgView3D* v, int w, int h, float seconds, TgV3List* out) {
  tg_v3_list_clear(out);
  font_ready(v);
  if(v->menu_up == 0 && v->loading_up) {
    draw_loading(v, w, h, seconds, out);
    return;
  }
  {
    // Menus of big lines (options, name entry, gear, controls): the modern template.
    int header = -1, label = -1, bigs = 0;
    for(int i = 0; i < v->menu_n; i++)
      if(v->menu_item[i].big) {
        if(v->menu_item[i].row <= 5) {
          if(header >= 0) label = header;
          header = i;
        } else {
          bigs++;
        }
      }
    bool info = false;
    for(int i = 0; i < v->menu_n; i++) info |= strstr(v->menu_item[i].text, "STATISTICS") != NULL;
    int pts = 0;
    for(int i = 0; i < v->menu_n; i++) pts += strstr(v->menu_item[i].text, " PT") != NULL;
    if(pts >= 4) {
      draw_results(v, w, h, seconds, out, header);
      return;
    }
    if(header < 0 && v->menu_box[2] < 0 && !info) {
      draw_message(v, w, h, seconds, out);
      return;
    }
    if(v->menu_screen == 7 && header >= 0 && strstr(v->menu_item[header].text, "COUNTRY")) {
      draw_country(v, w, h, seconds, out);
      return;
    }
    if((header >= 0 && bigs >= 1) || info) {
      draw_menu_modern(v, w, h, seconds, out, header, label);
      return;
    }
  }
  float s = h / 1080.0f;
  const uint32_t white = 0xFFF0F5F7, cyan = 0xFF62F2DC, muted = 0xFF9CABB9;
  (void) muted;
  screen_rect(out, 0, 0, (float) w, (float) h, 0, 1, 0xFF0B1622, 0xFF0F1E2C, -1);
  for(int i = 0; i < 18; i++) {
    float k = fmodf(i + seconds * 0.6f, 18) / 18, y = h * 0.66f + (h * 0.34f) * k * k;
    uint32_t c = with_alpha(cyan, (int) (50 * k));
    TgV3Vertex q[6] = {{0, y, 0, 0, c}, {(float) w, y, 0, 0, c}, {(float) w, y + 1.2f * s, 0, 0, c},
                       {0, y, 0, 0, c}, {(float) w, y + 1.2f * s, 0, 0, c}, {0, y + 1.2f * s, 0, 0, c}};
    list_push(out, q, 6, -1);
  }
  // The game's 256x224 screen mapped into the middle of ours.
  float k = fminf(w / 256.0f, h / 224.0f) * 0.94f, ox = (w - 256 * k) / 2, oy = (h - 224 * k) / 2;
  if(v->menu_box[2] > v->menu_box[0] && v->menu_box[3] > v->menu_box[1])
    rpanel(out, ox + v->menu_box[0] * 8 * k, oy + v->menu_box[1] * 8 * k, ox + (v->menu_box[2] + 1) * 8 * k,
           oy + (v->menu_box[3] + 1) * 8 * k, 14 * s, 0xC00E1A26);
  out->overlay = true;
  if(v->menu_pics && v->pics[MENU_HI].w)
    screen_rect(out, ox, oy, ox + 256 * k, oy + 224 * k, 0, 1, 0xFFFFFFFF, 0xFFFFFFFF, MENU_HI);
  if(v->menu_cur[0] >= 0) {  // the cursor on a picture: a glowing teal frame round it
    float x0 = ox + v->menu_cur[0] * k, y0 = oy + v->menu_cur[1] * k, x1 = ox + v->menu_cur[2] * k, y1 = oy + v->menu_cur[3] * k;
    float pulse = 0.7f + 0.3f * sinf(seconds * 5);
    uint32_t c = with_alpha(cyan, (int) (255 * pulse));
    float th = 3 * s;
    rect(out, x0, y0, x1, y0 + th, c), rect(out, x0, y1 - th, x1, y1, c);
    rect(out, x0, y0, x0 + th, y1, c), rect(out, x1 - th, y0, x1, y1, c);
  }
  // The header: the first big line near the top.
  // (Two big lines up there: the first is the player's name, a label over the title.)
  int header = -1, label = -1;
  for(int i = 0; i < v->menu_n; i++)
    if(v->menu_item[i].big && v->menu_item[i].row <= 5) {
      if(header >= 0) label = header;
      header = i;
    }
  for(int i = 0; i < v->menu_n; i++) {
    const char* t = v->menu_item[i].text;
    bool big = v->menu_item[i].big, sel = i == v->menu_sel;
    float x = ox + v->menu_item[i].col * 8 * k, y = oy + (v->menu_item[i].row * 8 + (big ? 14 : 7)) * k;
    float size = (big ? 13.5f : 7.2f) * k;
    if(i == label) {
      ftext(out, t, ox + 16 * 8 * k, y - 6 * k, size * 0.62f, cyan, 0, 0, size * 0.3f);
      continue;
    }
    if(i == header) {
      ftext(out, t, x - 2 * k, y + 1 * k, size * 1.25f, white, -1, 2, 0);
      rect(out, x, y + 5 * k, x + ftext_w(t, size * 1.25f, 2, 0), y + 6.2f * k, cyan);
      continue;
    }
    if(sel) {
      float tw = ftext_w(t, size, 0, size * 0.06f);
      float pulse = 0.75f + 0.25f * sinf(seconds * 5);
      rpanel(out, x - 8 * k, y - size - 4 * k, x + tw + 8 * k, y + 5 * k, 6 * k, with_alpha(0xFF14303E, (int) (235 * pulse)));
      rect(out, x - 8 * k, y + 3.5f * k, x + tw + 8 * k, y + 5 * k, cyan);
    }
    ftext(out, t, x, y, size, sel ? cyan : white, -1, big ? 0 : 1, size * 0.06f);
  }
}

// A card: dark glass, a light edge on top; selected: lifted, a teal glow and edge.
static void card(TgV3List* out, float x0, float y0, float x1, float y1, float s, bool sel, float seconds) {
  bool ov = out->overlay;
  out->overlay = false;  // under any 3D model drawn on it
  float cut = fminf(36 * s, (y1 - y0) * 0.3f);
  if(sel) {
    float lift = 10 * s;
    y0 -= lift, y1 -= lift;
    screen_rect(out, x0 - 80 * s, y0 - 80 * s, x1 + 80 * s, y1 + 80 * s, 0, 1, 0x5862F2DC, 0x5862F2DC, TEX_GLOW);
    cpanel(out, x0 + 8 * s, y0 + 14 * s, x1 + 8 * s, y1 + 14 * s, cut, 0x60000000, 0x60000000, 0, s);  // shadow
    cpanel(out, x0, y0, x1, y1, cut, 0xF51E4652, 0xF50E2430, with_alpha(0xFF62F2DC, (int) (210 + 45 * sinf(seconds * 4))), s);
    rect(out, x0, y0 + cut * 0.4f, x0 + 5 * s, y1 - cut * 0.4f, 0xFF62F2DC);  // the accent bar
    // A sheen across it.
    float sw = fmodf(seconds * 0.5f, 1.6f) * (x1 - x0 + 200 * s) - 100 * s;
    sbar(out, x0 + sw, y1, x0 + sw + (y1 - y0) * 0.5f, y0, 40 * s, 0x10FFFFFF);
  } else {
    cpanel(out, x0, y0, x1, y1, cut, 0xD0182430, 0xD00A121A, 0x38A9D2E0, s);
  }
  out->overlay = ov;
}

// A course's name and country as the game has them ($0D:C77C, the strings in bank $0F).
static void course_name(int course, char* name, char* country, size_t n) {
  name[0] = country[0] = 0;
  uint16_t x = rom16(0x0DC77C + (uint32_t) ((course & 31) << 1));
  for(int part = 0; part < 2; part++) {
    if(rom16(0x0F0000 | x) == 0) break;
    x += 4;
    char* o = part ? country : name;
    size_t k = 0;
    for(uint8_t c; (c = tg_rom(0x0F0000 | x)[0]) != 0; x++)
      if(k + 1 < n) o[k++] = c == '=' ? '.' : (char) c;
    o[k] = 0;
    x++;
  }
}

// A chamfered panel (a racing game's card): its top right corner cut, a gradient from top to
// bottom, a light edge.
static void cpanel(TgV3List* l, float x0, float y0, float x1, float y1, float cut, uint32_t top, uint32_t bot, uint32_t edge, float s) {
  TgV3Vertex q[12] = {{x0, y0, 0, 0, top}, {x1 - cut, y0, 0, 0, top}, {x1, y1, 0, 0, bot},
                      {x0, y0, 0, 0, top}, {x1, y1, 0, 0, bot}, {x0, y1, 0, 0, bot},
                      {x1 - cut, y0, 0, 0, top}, {x1, y0 + cut, 0, 0, mix(top, bot, cut / (y1 - y0))}, {x1, y1, 0, 0, bot},
                      {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}};
  list_push(l, q, 9, -1);
  if(edge >> 24) {
    float t = 1.4f * s;
    sbar(l, x0, y0, x1 - cut, y0, t, edge);
    sbar(l, x1 - cut, y0, x1, y0 + cut, t, edge);
    sbar(l, x1, y0 + cut, x1, y1, t, with_alpha(edge, (int) ((edge >> 24) * 0.4f)));
    sbar(l, x0, y0, x0, y1, t, with_alpha(edge, (int) ((edge >> 24) * 0.4f)));
    sbar(l, x0, y1, x1, y1, t, with_alpha(edge, (int) ((edge >> 24) * 0.25f)));
  }
}

// --- Menu props in 3D: gear sticks and a controller ------------------------------------------
static void prop_cam(Cam* cam, float cx, float cy, float focal, float yaw, float pitch, float dist, V3 target, float w, float h) {
  float a = yaw * PI / 180, p = pitch * PI / 180;
  cam->eye = vadd(target, v3(sinf(a) * cosf(p) * dist, sinf(p) * dist, -cosf(a) * cosf(p) * dist));
  cam->fwd = vnorm(vsub(target, cam->eye));
  cam->right = vnorm(vcross(cam->fwd, v3(0, 1, 0)));
  cam->up = vcross(cam->right, cam->fwd);
  cam->f = focal, cam->cx = cx, cam->cy = cy, cam->w = w, cam->h = h;
}

static void prop_flush(Gather* g, TgV3List* out) {
  qsort(g->it, (size_t) g->ni, sizeof(Item), cmp_item);
  for(int i = 0; i < g->ni; i++)
    for(int j = 0; j < g->it[i].count; j++) list_push(out, g->t[g->it[i].first + j].v, 3, g->t[g->it[i].first + j].pic);
  free(g->t);
  free(g->it);
}

// A gear lever on its console: manual (an H gate, a round knob) or automatic (a straight
// gate, a grip with a button). lit: the selected one glows.
static void gear_prop(TgV3List* out, bool automatic, float cx, float cy, float focal, float yaw, bool lit, float w, float h) {
  Gather g = {0};
  Cam cam;
  g_haze = 0xFF0A1420;
  prop_cam(&cam, cx, cy, focal, yaw, 28, 3.4f, v3(0, 0.45f, 0), w, h);
  g.cam = &cam;
  V3 f = v3(0, 0, 1), r = v3(1, 0, 0), u = v3(0, 1, 0);
  uint32_t metal = 0xFF3A424C, dark = 0xFF14181E, chrome = 0xFFC8D2DC, accent = lit ? 0xFF62F2DC : 0xFF56606C;
  // The console: a bevelled block, a carbon top, a glowing trim line.
  mbox(&g, v3(0, 0, 0), f, r, u, 0.62f, 0.5f, 0, 0.16f, metal, 0);
  mbox(&g, v3(0, 0, 0), f, r, u, 0.58f, 0.46f, 0.16f, 0.2f, 0xFF1C2128, 0.01f);
  mbox(&g, v3(0, 0, -0.6f), f, r, u, 0.01f, 0.46f, 0.15f, 0.19f, accent, 0.02f);
  float lean = 0;
  if(!automatic) {
    // The H gate: three slots across, one along; the lever in third.
    for(int k = -1; k <= 1; k++) mbox(&g, v3(k * 0.16f, 0, 0), f, r, u, 0.22f, 0.025f, 0.2f, 0.205f, dark, 0.02f);
    mbox(&g, v3(0, 0, 0), f, r, u, 0.025f, 0.2f, 0.2f, 0.205f, dark, 0.02f);
    lean = 0.18f;
  } else {
    mbox(&g, v3(0, 0, 0), f, r, u, 0.34f, 0.03f, 0.2f, 0.205f, dark, 0.02f);  // the straight gate
    for(int k = 0; k < 4; k++)  // P R N D marks
      mbox(&g, v3(0.12f, 0, -0.24f + k * 0.16f), f, r, u, 0.02f, 0.05f, 0.2f, 0.21f, k == 3 ? accent : 0xFF8A949E, 0.03f);
  }
  // A leather boot, the lever, the knob.
  V3 base = v3(automatic ? 0 : -0.16f, 0.2f, automatic ? 0.12f : 0.22f);
  frustum(&g, base, u, f, r, 0.16f, 0.05f, 0.18f, 10, 0xFF22262C, 0.04f);
  V3 dir = vnorm(v3(0, 1, automatic ? 0 : lean * 0.3f));
  frustum(&g, vadd(base, vmul(u, 0.15f)), dir, f, r, 0.03f, 0.025f, 0.55f, 8, chrome, 0.05f);
  V3 top = vadd(base, vadd(vmul(u, 0.15f), vmul(dir, 0.55f)));
  if(!automatic) {
    frustum(&g, top, u, f, r, 0.05f, 0.11f, 0.08f, 12, 0xFF1A1D22, 0.06f);
    frustum(&g, vadd(top, vmul(u, 0.08f)), u, f, r, 0.11f, 0.1f, 0.06f, 12, 0xFF1A1D22, 0.07f);
    frustum(&g, vadd(top, vmul(u, 0.14f)), u, f, r, 0.1f, 0.0f, 0.05f, 12, accent, 0.08f);
  } else {
    mbox(&g, vadd(top, v3(0, 0, 0.03f)), f, r, u, 0.1f, 0.06f, 0, 0.22f, 0xFF1A1D22, 0.06f);
    mbox(&g, vadd(top, v3(0, 0, 0.12f)), f, r, u, 0.02f, 0.035f, 0.12f, 0.18f, 0xFFE21E2C, 0.08f);
    mbox(&g, vadd(top, v3(0, 0, 0.03f)), f, r, u, 0.1f, 0.062f, 0.22f, 0.24f, accent, 0.07f);
  }
  prop_flush(&g, out);
}

// The controller, a modern one, seen from the front and above. Its button centres on the
// screen into pts (d-pad, Y, X, B, A, L, R, select, start) for the labels.
static void pad_prop(TgV3List* out, float cx, float cy, float focal, float yaw, float pitch, float w, float h, float pts[9][2]) {
  Gather g = {0};
  Cam cam;
  g_haze = 0xFF0A1420;
  prop_cam(&cam, cx, cy, focal, yaw, pitch, 4.2f, v3(0, 0.1f, 0), w, h);
  g.cam = &cam;
  V3 f = v3(0, 0, 1), r = v3(1, 0, 0), u = v3(0, 1, 0);
  uint32_t shell = 0xFF2A3038, shellHi = 0xFF3A424C, dark = 0xFF101318, teal = 0xFF62F2DC;
  // The body: a middle and two round grips.
  mbox(&g, v3(0, 0, 0), f, r, u, 0.42f, 0.62f, 0, 0.18f, shell, 0);
  for(int k = -1; k <= 1; k += 2) {
    frustum(&g, v3(k * 0.62f, 0, 0.02f), u, f, r, 0.44f, 0.42f, 0.18f, 16, shell, 0.001f);
    frustum(&g, v3(k * 0.62f, 0.18f, 0.02f), u, f, r, 0.42f, 0.36f, 0.03f, 16, shellHi, 0.002f);
  }
  mbox(&g, v3(0, 0, 0), f, r, u, 0.4f, 0.6f, 0.18f, 0.2f, shellHi, 0.002f);
  // A light strip along the top edge.
  mbox(&g, v3(0, 0, 0.43f), f, r, u, 0.012f, 0.5f, 0.1f, 0.14f, teal, 0.01f);
  // The d-pad.
  V3 dp = v3(-0.62f, 0.21f, 0.02f);
  mbox(&g, dp, f, r, u, 0.2f, 0.065f, 0, 0.04f, dark, 0.02f);
  mbox(&g, dp, f, r, u, 0.065f, 0.2f, 0, 0.04f, dark, 0.02f);
  // The face buttons: Y left, X top, B bottom, A right.
  V3 fb = v3(0.62f, 0.21f, 0.02f);
  static const float bx[4] = {-0.17f, 0, 0, 0.17f}, bz[4] = {0, 0.17f, -0.17f, 0};
  uint32_t bc[4] = {0xFF3A4652, 0xFF3A4652, 0xFF3A4652, 0xFF3A4652};
  for(int k = 0; k < 4; k++) {
    V3 b = vadd(fb, v3(bx[k], 0, bz[k]));
    frustum(&g, b, u, f, r, 0.075f, 0.07f, 0.045f, 12, bc[k], 0.03f);
    frustum(&g, vadd(b, v3(0, 0.045f, 0)), u, f, r, 0.05f, 0.0f, 0.004f, 12, teal, 0.04f);
  }
  // Shoulders, select and start.
  for(int k = -1; k <= 1; k += 2) {
    mbox(&g, v3(k * 0.62f, 0.06f, 0.44f), f, r, u, 0.06f, 0.24f, 0.0f, 0.12f, shellHi, 0.03f);
    mbox(&g, v3(k * 0.12f, 0.2f, -0.08f), f, r, u, 0.03f, 0.07f, 0, 0.02f, dark, 0.03f);
  }
  prop_flush(&g, out);
  V3 at[9] = {dp, vadd(fb, v3(-0.17f, 0, 0)), vadd(fb, v3(0, 0, 0.17f)), vadd(fb, v3(0, 0, -0.17f)), vadd(fb, v3(0.17f, 0, 0)),
              v3(-0.62f, 0.12f, 0.46f), v3(0.62f, 0.12f, 0.46f), v3(-0.12f, 0.2f, -0.08f), v3(0.12f, 0.2f, -0.08f)};
  for(int k = 0; k < 9; k++) {
    V3 c = to_cam(&cam, at[k]);
    pts[k][0] = cam.cx + c.x / c.z * cam.f, pts[k][1] = cam.cy - c.y / c.z * cam.f;
  }
}

// Words of a line: the n-th (0 based) into buf.
static int words(const char* t, char out[][33], int maxw) {
  int n = 0, k = 0;
  for(const char* p = t;; p++) {
    if(*p == ' ' || !*p) {
      if(k && n < maxw) out[n][k] = 0, n++;
      k = 0;
      if(!*p) break;
    } else if(k < 32 && n < maxw) {
      out[n][k++] = *p;
    }
  }
  return n;
}

// --- The country screen: eight cards with their flags, drawn as shapes ----------------------
//
// The flags to their official construction sheets (researched 2026-11-03):
//  USA: Executive Order 10834 (1959): hoist 1, fly 1.9, union 7/13 x 0.76, 50 stars in 9
//    rows of 6 and 5, spaced 0.054 down and 0.063 across, 0.0616 across a star.
//  Brazil: Law 5.700 (1971, as amended in 1992): 14 x 20 modules M; the rhombus 1.7 M in
//    from the edges, the globe 3.5 M across its radius, the white band between arcs of 8 M and
//    8.5 M about a point 2 M left of the globe's vertical diameter on the lower edge.
//  Japan: 2:3, the disc 3/5 of the hoist across, in the centre (Act on National Flag, 1999).
//  Germany: 3:5, black, red, gold in equal bands (Basic Law art. 22).
//  Sweden (the Nordic cross for Scandinavia): 10 x 16, the cross 2 wide, 5:2:9 across, 4:2:4
//    down.
//  France, Italy: 2:3, three equal vertical bands.
//  Union Flag: 30 x 60 (1:2, at sea and customary); St George's cross 6 wide on white 10;
//    St Andrew's saltire 6 wide, St Patrick's red 2 inside it, broad white 3 on the hoist's
//    upper side, narrow 1, counterchanged (the red on the anticlockwise side of each arm).

// Flag width over height.
static const float kFlagRatio[8] = {1.9f, 10.0f / 7.0f, 1.5f, 5.0f / 3.0f, 1.6f, 1.5f, 1.5f, 2.0f};

// A convex polygon, clipped to a box (Sutherland-Hodgman), filled.
static void fpoly(TgV3List* l, const float (*p)[2], int n, float cx0, float cy0, float cx1, float cy1, uint32_t c) {
  float a[24][2], b[24][2];
  int na = n < 12 ? n : 12;
  memcpy(a, p, sizeof(float) * 2 * (size_t) na);
  for(int e = 0; e < 4 && na > 0; e++) {
    int nb = 0;
    for(int i = 0; i < na && nb < 22; i++) {
      const float* P = a[i];
      const float* Q = a[(i + 1) % na];
      float dp = e == 0 ? P[0] - cx0 : e == 1 ? cx1 - P[0] : e == 2 ? P[1] - cy0 : cy1 - P[1];
      float dq = e == 0 ? Q[0] - cx0 : e == 1 ? cx1 - Q[0] : e == 2 ? Q[1] - cy0 : cy1 - Q[1];
      if(dp >= 0) b[nb][0] = P[0], b[nb][1] = P[1], nb++;
      if((dp >= 0) != (dq >= 0)) {
        float t = dp / (dp - dq);
        b[nb][0] = P[0] + (Q[0] - P[0]) * t, b[nb][1] = P[1] + (Q[1] - P[1]) * t, nb++;
      }
    }
    memcpy(a, b, sizeof(float) * 2 * (size_t) nb);
    na = nb;
  }
  for(int i = 1; i + 1 < na; i++) {
    TgV3Vertex t[3] = {{a[0][0], a[0][1], 0, 0, c}, {a[i][0], a[i][1], 0, 0, c}, {a[i + 1][0], a[i + 1][1], 0, 0, c}};
    list_push(l, t, 3, -1);
  }
}

static void fdisc(TgV3List* l, float cx, float cy, float r, uint32_t c) {
  int n = r > 40 ? 64 : r > 8 ? 40 : 16;
  for(int i = 0; i < n; i++) {
    float a0 = i * 2 * PI / n, a1 = (i + 1) * 2 * PI / n;
    TgV3Vertex t[3] = {{cx, cy, 0, 0, c}, {cx + cosf(a0) * r, cy + sinf(a0) * r, 0, 0, c}, {cx + cosf(a1) * r, cy + sinf(a1) * r, 0, 0, c}};
    list_push(l, t, 3, -1);
  }
}

// A five-pointed star, point up, r the outer radius.
static void fstar(TgV3List* l, float cx, float cy, float r, uint32_t c) {
  float ri = r * 0.381966f, p[10][2];
  for(int i = 0; i < 10; i++) {
    float a = -PI / 2 + i * PI / 5, rr = i & 1 ? ri : r;
    p[i][0] = cx + cosf(a) * rr, p[i][1] = cy + sinf(a) * rr;
  }
  for(int i = 0; i < 10; i++) {
    TgV3Vertex t[3] = {{cx, cy, 0, 0, c}, {p[i][0], p[i][1], 0, 0, c}, {p[(i + 1) % 10][0], p[(i + 1) % 10][1], 0, 0, c}};
    list_push(l, t, 3, -1);
  }
}

// The white band of Brazil's globe: between two arcs about (ax, ay), inside the globe.
static void brazil_band(TgV3List* l, float gx, float gy, float gr, float ax, float ay, float r0, float r1, uint32_t c) {
  enum { N = 48 };
  for(int i = 0; i < N; i++) {
    // Sweep x across the globe; on each slice the band is between the arcs, kept in the disc.
    float x0 = gx - gr + 2 * gr * i / N, x1 = gx - gr + 2 * gr * (i + 1) / N;
    float q[4][2];
    float xs[2] = {x0, x1};
    bool ok = true;
    for(int k = 0; k < 2; k++) {
      float dx = xs[k] - ax, half = sqrtf(fmaxf(gr * gr - (xs[k] - gx) * (xs[k] - gx), 0));
      if(r0 * r0 < dx * dx || r1 * r1 < dx * dx) ok = false;
      float yin = ay - sqrtf(fmaxf(r0 * r0 - dx * dx, 0)), yout = ay - sqrtf(fmaxf(r1 * r1 - dx * dx, 0));
      float top = gy - half, bot = gy + half;
      yout = fmaxf(yout, top), yin = fminf(yin, bot);
      if(yin < yout) yin = yout;
      q[k ? 1 : 0][0] = xs[k], q[k ? 1 : 0][1] = yout;
      q[k ? 2 : 3][0] = xs[k], q[k ? 2 : 3][1] = yin;
    }
    if(!ok) continue;
    TgV3Vertex t[6] = {{q[0][0], q[0][1], 0, 0, c}, {q[1][0], q[1][1], 0, 0, c}, {q[2][0], q[2][1], 0, 0, c},
                       {q[0][0], q[0][1], 0, 0, c}, {q[2][0], q[2][1], 0, 0, c}, {q[3][0], q[3][1], 0, 0, c}};
    list_push(l, t, 6, -1);
  }
}

// A band along the line from (x0, y0) to (x1, y1), from offset a to b on its normal (n), made
// long enough to leave the flag and clipped to it.
static void fband(TgV3List* l, float x0, float y0, float x1, float y1, float nx, float ny, float a, float b, const float* box, uint32_t c) {
  float p[4][2] = {{x0 + nx * a, y0 + ny * a}, {x1 + nx * a, y1 + ny * a}, {x1 + nx * b, y1 + ny * b}, {x0 + nx * b, y0 + ny * b}};
  fpoly(l, (const float (*)[2]) p, 4, box[0], box[1], box[2], box[3], c);
}

// Country c's flag, h high (its width from its proportions), top left at (x, y).
static void draw_flag(TgV3List* l, int c, float x, float y, float h) {
  float w = h * kFlagRatio[c & 7], box[4] = {x, y, x + w, y + h};
  const uint32_t white = 0xFFFFFFFF;
  switch(c & 7) {
    case 0: {  // United States
      const uint32_t red = 0xFFB22234, blue = 0xFF3C3B6E;
      float st = h / 13;
      rect(l, x, y, x + w, y + h, white);
      for(int i = 0; i < 13; i += 2) rect(l, x, y + i * st, x + w, y + (i + 1) * st, red);
      float uw = h * 0.76f, uh = st * 7;
      rect(l, x, y, x + uw, y + uh, blue);
      float E = uh / 10, H = uw / 12, r = h * 0.0616f / 2;
      for(int row = 0; row < 9; row++)
        for(int k = 0; k < (row & 1 ? 5 : 6); k++) fstar(l, x + (row & 1 ? 2 * k + 2 : 2 * k + 1) * H, y + (row + 1) * E, r, white);
      break;
    }
    case 1: {  // Brazil
      const uint32_t green = 0xFF009C3B, yellow = 0xFFFFDF00, blue = 0xFF002776;
      float M = h / 14;
      rect(l, x, y, x + w, y + h, green);
      float rh[4][2] = {{x + 1.7f * M, y + 7 * M}, {x + 10 * M, y + 1.7f * M}, {x + 18.3f * M, y + 7 * M}, {x + 10 * M, y + 12.3f * M}};
      fpoly(l, (const float (*)[2]) rh, 4, box[0], box[1], box[2], box[3], yellow);
      float gx = x + 10 * M, gy = y + 7 * M, gr = 3.5f * M;
      fdisc(l, gx, gy, gr, blue);
      brazil_band(l, gx, gy, gr, x + 8 * M, y + 14 * M, 8 * M, 8.5f * M, white);
      // The stars (a few of the 27, in their places: the Southern Cross, Spica above the band).
      static const float st[][3] = {{10.0f, 9.9f, .30f}, {10.9f, 8.75f, .26f}, {9.05f, 8.95f, .24f}, {10.15f, 8.05f, .20f}, {9.65f, 8.75f, .14f},
                                    {11.7f, 9.35f, .20f}, {8.2f, 9.6f, .20f},  {12.5f, 8.2f, .16f}, {7.6f, 8.3f, .16f},  {11.0f, 10.0f, .16f},
                                    {12.3f, 6.2f, .24f},  {9.2f, 10.2f, .14f}};
      for(size_t i = 0; i < sizeof(st) / sizeof(st[0]); i++) fstar(l, x + st[i][0] * M, y + st[i][1] * M, st[i][2] * M, white);
      break;
    }
    case 2:  // Japan
      rect(l, x, y, x + w, y + h, white);
      fdisc(l, x + w / 2, y + h / 2, h * 0.3f, 0xFFBC002D);
      break;
    case 3: {  // Germany
      const uint32_t c3[3] = {0xFF000000, 0xFFDD0000, 0xFFFFCE00};
      for(int i = 0; i < 3; i++) rect(l, x, y + h * i / 3, x + w, y + h * (i + 1) / 3, c3[i]);
      break;
    }
    case 4: {  // Sweden: the Nordic cross
      float u = h / 10;
      rect(l, x, y, x + w, y + h, 0xFF006AA7);
      rect(l, x + 5 * u, y, x + 7 * u, y + h, 0xFFFECC02);
      rect(l, x, y + 4 * u, x + w, y + 6 * u, 0xFFFECC02);
      break;
    }
    case 5:
    case 6: {  // France, Italy
      const uint32_t fr[3] = {0xFF0055A4, white, 0xFFEF4135}, it[3] = {0xFF009246, 0xFFF4F5F0, 0xFFCE2B37};
      for(int i = 0; i < 3; i++) rect(l, x + w * i / 3, y, x + w * (i + 1) / 3, y + h, (c & 7) == 5 ? fr[i] : it[i]);
      break;
    }
    default: {  // the Union Flag, in units of h / 30
      const uint32_t blue = 0xFF012169, red = 0xFFC8102E;
      float u = h / 30, cx = x + w / 2, cy = y + h / 2;
      rect(l, x, y, x + w, y + h, blue);
      float dl = sqrtf(w * w + h * h), dx = w / dl, dy = h / dl, ext = h;
      // St Andrew's: the two diagonals, 6 wide.
      fband(l, x - dx * ext, y - dy * ext, x + w + dx * ext, y + h + dy * ext, dy, -dx, -3 * u, 3 * u, box, white);
      fband(l, x - dx * ext, y + h + dy * ext, x + w + dx * ext, y - dy * ext, dy, dx, -3 * u, 3 * u, box, white);
      // St Patrick's: each arm from the centre, the red 2 wide on its anticlockwise side.
      const float corner[4][2] = {{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}};
      for(int k = 0; k < 4; k++) {
        float ax = corner[k][0] - cx, ay = corner[k][1] - cy, al = sqrtf(ax * ax + ay * ay);
        ax /= al, ay /= al;
        fband(l, cx, cy, corner[k][0] + ax * ext, corner[k][1] + ay * ext, ay, -ax, 0, 2 * u, box, red);
      }
      // St George's: white 10, red 6.
      rect(l, cx - 5 * u, y, cx + 5 * u, y + h, white);
      rect(l, x, cy - 5 * u, x + w, cy + 5 * u, white);
      rect(l, cx - 3 * u, y, cx + 3 * u, y + h, red);
      rect(l, x, cy - 3 * u, x + w, cy + 3 * u, red);
      break;
    }
  }
}

// A flag with its finish: a soft shadow, a light from the top left, a fine edge.
static void flag_card(TgV3List* l, int c, float x, float y, float h, float s, bool lit, float seconds) {
  float w = h * kFlagRatio[c & 7];
  bool ov = l->overlay;
  l->overlay = true;
  screen_rect(l, x - 18 * s, y - 10 * s, x + w + 26 * s, y + h + 30 * s, 0, 1, 0x50000000, 0x50000000, TEX_GLOW);
  draw_flag(l, c, x, y, h);
  screen_rect(l, x, y, x + w, y + h * 0.5f, 0, 1, 0x26FFFFFF, 0x00FFFFFF, -1);
  screen_rect(l, x, y + h * 0.5f, x + w, y + h, 0, 1, 0x00000000, 0x1E000000, -1);
  if(lit) {  // a glint running across the selected one
    float k = fmodf(seconds * 0.45f, 1.6f) - 0.3f, gx = x + w * k;
    if(gx > x - h * 0.3f && gx < x + w) {
      float g0 = fmaxf(x, gx), g1 = fminf(x + w, gx + h * 0.35f);
      if(g1 > g0) rect(l, g0, y, g1, y + h, 0x1CFFFFFF);
    }
  }
  uint32_t edge = lit ? 0xC062F2DC : 0x40FFFFFF;
  float t = (lit ? 2.2f : 1.2f) * s;
  rect(l, x - t, y - t, x + w + t, y, edge), rect(l, x - t, y + h, x + w + t, y + h + t, edge);
  rect(l, x - t, y, x, y + h, edge), rect(l, x + w, y, x + w + t, y + h, edge);
  l->overlay = ov;
}

// A small padlock (closed) at (x, y) its body's top left, size its width.
static void padlock(TgV3List* l, float x, float y, float size, uint32_t c) {
  float r = size * 0.32f, cx = x + size / 2, t = size * 0.14f;
  for(int a = 0; a < 12; a++) {
    float a0 = PI + a * PI / 12, a1 = PI + (a + 1) * PI / 12;
    sbar(l, cx + cosf(a0) * r, y + sinf(a0) * r, cx + cosf(a1) * r, y + sinf(a1) * r, t, c);
  }
  rect(l, cx - r - t / 2, y - 0.5f, cx - r + t / 2, y + 1, c), rect(l, cx + r - t / 2, y - 0.5f, cx + r + t / 2, y + 1, c);
  rpanel(l, x, y, x + size, y + size * 0.8f, size * 0.12f, c);
}

static const char* const kSkillName[3] = {"AMATEUR", "PROFESSIONAL", "CHAMPIONSHIP"};

// The country screen ($0F:929E, screen 7): a card for each of the eight countries, its flag,
// name and four cities (the courses' names from the ROM), open or locked; the game's own
// cursor ($5A) lit, an eased frame following it.
static void draw_country(TgView3D* v, int w, int h, float seconds, TgV3List* out) {
  float s = h / 1080.0f;
  const uint32_t white = 0xFFF0F5F7, cyan = 0xFF62F2DC, muted = 0xFF8A9AAA, amber = 0xFFFFC24A;
  menu_frame(v, out, w, h, seconds, "COUNTRY");
  int sel = v->menu_cursor, open = v->menu_open;
  // Top right: the skill level and how far the tour is open.
  {
    char b[48];
    snprintf(b, sizeof(b), "%s", kSkillName[v->menu_skill]);
    float bw = ftext_w(b, 18 * s, 0, 4 * s) + 36 * s;
    rpanel(out, w - 78 * s - bw, 168 * s, w - 78 * s, 210 * s, 8 * s, 0x2862F2DC);
    ftext(out, b, w - 78 * s - bw / 2, 196 * s, 18 * s, cyan, 0, 0, 4 * s);
    ftext(out, "SKILL LEVEL", w - 78 * s - bw - 20 * s, 196 * s, 15 * s, muted, 1, 0, 4 * s);
    snprintf(b, sizeof(b), "%d OF 8 COUNTRIES OPEN", open + 1);
    ftext(out, b, w - 78 * s, 240 * s, 15 * s, muted, 1, 0, 4 * s);
  }
  // The cards: two columns of four, in the game's order down the first column and on down
  // the second (the game's cursor moves up and down through the eight, wrapping).
  float gap = 18 * s, colgap = 30 * s, top = 282 * s, bottom = h - 150 * s;
  float cw = fminf(1000 * s, (w - 156 * s - colgap) / 2), ch = (bottom - top - 3 * gap) / 4;
  float gx0 = (w - (2 * cw + colgap)) / 2;
  float tx = gx0 + (sel >> 2) * (cw + colgap), ty = top + (sel & 3) * (ch + gap);
  float dt = seconds - v->ctry_t;
  if(dt < 0 || dt > 0.25f || v->ctry_hx == 0) {
    v->ctry_hx = tx, v->ctry_hy = ty;
  } else {
    float k = 1 - expf(-dt * 14);
    v->ctry_hx += (tx - v->ctry_hx) * k, v->ctry_hy += (ty - v->ctry_hy) * k;
  }
  v->ctry_t = seconds;
  float fh = fminf(ch - 44 * s, 96 * s), slot = fh * 2.0f, pad = 26 * s;
  for(int c = 0; c < 8; c++) {
    float x = gx0 + (c >> 2) * (cw + colgap), y = top + (c & 3) * (ch + gap);
    bool on = c == sel, locked = c > open;
    float lift = on ? 10 * s : 0;
    card(out, x, y, x + cw, y + ch, s, on, seconds);
    // The flag, in a slot as wide as the widest (the Union Flag, 1:2), centred on it.
    float grow = on ? 1.06f : 1.0f, ffh = fh * grow, fw = ffh * kFlagRatio[c];
    float fx = x + pad + 6 * s + (slot - fw) / 2, fy = y + (ch - ffh) / 2 - lift;
    flag_card(out, c, fx, fy, ffh, s, on, seconds);
    if(locked && !v->country_free) {  // locked, its password to type: the flag dimmed, a padlock
      bool ov = out->overlay;
      out->overlay = true;
      rect(out, fx, fy, fx + fw, fy + ffh, 0x980A1420);
      padlock(out, fx + fw / 2 - 16 * s, fy + ffh / 2 - 6 * s, 32 * s, 0xFFE0E8EE);
      out->overlay = ov;
    }
    float tx0 = x + pad + slot + 40 * s, tw = x + cw - pad - tx0;
    // The number and the state, top right.
    char idx[4];
    snprintf(idx, sizeof(idx), "%02d", c + 1);
    ftext(out, idx, x + cw - pad - 10 * s, y + 44 * s - lift, 22 * s, on ? cyan : 0x50FFFFFF, 1, 0, 2 * s);
    const char* st = !locked ? "OPEN" : v->country_free ? "NO PASSWORD NEEDED" : "PASSWORD";
    uint32_t sc = !locked ? cyan : v->country_free ? amber : 0xFFB8C4D0;
    float stw = ftext_w(st, 13 * s, 0, 3 * s);
    float sx = x + cw - pad - 10 * s - 50 * s - stw, sy = y + 43 * s - lift;
    fdisc(out, sx - 12 * s, sy - 5.5f * s, 4.5f * s, sc);
    ftext(out, st, sx, sy, 13 * s, sc, -1, 0, 3 * s);
    // The name.
    float nsz = fminf(46 * s, ch * 0.34f);
    while(nsz > 24 * s && ftext_w(kCountry[c].name, nsz, 2, 0.5f * s) > sx - 30 * s - tx0) nsz -= 2 * s;
    float ny = y + ch * 0.47f - lift;
    ftext(out, kCountry[c].name, tx0, ny, nsz, on ? white : 0xFFC8D2DC, -1, 2, 0.5f * s);
    // The four cities, in the order raced.
    float ly = ny + 18 * s;
    rect(out, tx0, ly, x + cw - pad - 10 * s, ly + 1.2f * s, on ? 0x6062F2DC : 0x20FFFFFF);
    float csz = fminf(17 * s, ch * 0.13f), colw = tw / 4, cy = ly + 14 * s + csz;
    for(int k = 0; k < 4; k++) {
      char name[40], country[40], num[4];
      course_name(c * 4 + k, name, country, sizeof(name));
      float cx = tx0 + k * colw;
      snprintf(num, sizeof(num), "%d", k + 1);
      ftext(out, num, cx, cy, csz * 0.82f, on ? cyan : muted, -1, 0, 0);
      float sz = csz;
      while(sz > 9 * s && ftext_w(name, sz, 0, 1.2f * s) > colw - 30 * s) sz -= 0.5f * s;
      ftext(out, name, cx + 17 * s, cy, sz, on ? 0xFFDCE6EE : 0xFF9FB0BE, -1, 0, 1.2f * s);
    }
  }
  // The frame that follows the cursor.
  {
    float x0 = v->ctry_hx - 7 * s, y0 = v->ctry_hy - 17 * s, x1 = v->ctry_hx + cw + 7 * s, y1 = v->ctry_hy + ch - 3 * s;
    float pulse = 0.65f + 0.35f * sinf(seconds * 5);
    uint32_t c = with_alpha(cyan, (int) (255 * pulse));
    float th = 3 * s, L = 30 * s;
    bool ov = out->overlay;
    out->overlay = true;
    rect(out, x0, y0, x0 + L, y0 + th, c), rect(out, x0, y0, x0 + th, y0 + L, c);
    rect(out, x1 - L, y0, x1, y0 + th, c), rect(out, x1 - th, y0, x1, y0 + L, c);
    rect(out, x1 - L, y1 - th, x1, y1, c), rect(out, x1 - th, y1 - L, x1, y1, c);
    rect(out, x0, y1 - th, x0 + L, y1, c), rect(out, x0, y1 - L, x0 + th, y1, c);
    out->overlay = ov;
  }
  // What choosing it does.
  {
    char b[96], pw[9];
    bool locked = sel > open;
    if(!locked) snprintf(b, sizeof(b), "THE WORLD TOUR STARTS IN %s,  RACE 1  /  4", kCountry[sel].name);
    else if(v->country_free && tg_country_password(sel, v->menu_skill, pw)) snprintf(b, sizeof(b), "LOCKED  -  ITS PASSWORD (%s) IS ENTERED FOR YOU", pw);
    else snprintf(b, sizeof(b), "LOCKED  -  ENTER ITS PASSWORD TO START HERE");
    ftext(out, b, w - 78 * s, h - 105 * s, 15 * s, locked && !v->country_free ? 0xFFB8C4D0 : white, 1, 0, 3 * s);
  }
  static const char* const hints[3][2] = {{"D-PAD", "CHOOSE"}, {"A / START", "SELECT"}, {"TAB", "OPTIONS: COUNTRY SELECT"}};
  key_hints(out, w, h, s, hints, 3);
}

static void draw_menu_modern(TgView3D* v, int w, int h, float seconds, TgV3List* out, int header, int label) {
  float s = h / 1080.0f;
  const uint32_t white = 0xFFF0F5F7, cyan = 0xFF62F2DC, muted = 0xFF8A9AAA, ink = 0xFF071019;
  const char* title = header >= 0 ? v->menu_item[header].text : "NEXT RACE";
  // The backdrop: deep navy, the game's photograph in it, light streaks crossing it, a
  // vignette; the title huge and faint.
  screen_rect(out, 0, 0, (float) w, (float) h, 0, 1, 0xFF0A1420, 0xFF050A10, -1);
  menu_photo(v, out, w, h, seconds);
  for(int i = 0; i < 5; i++) {
    float x = fmodf(i * 0.27f + seconds * 0.015f * (i + 1), 1.4f) * w - 0.2f * w;
    uint32_t c = i == 2 ? 0x18FF4A3A : 0x1462F2DC;
    sbar(out, x, (float) h * 1.1f, x + h * 0.9f, -(float) h * 0.1f, (60 + i * 50) * s, c);
  }
  screen_rect(out, w * 0.1f, -h * 0.2f, w * 0.9f, h * 0.7f, 0, 1, 0x2862F2DC, 0x2862F2DC, TEX_GLOW);
  out->overlay = true;
  ftext(out, title, w * 0.5f, h * 0.62f, 300 * s, 0x0CFFFFFF, 0, 2, 10 * s);
  // The top: the game's name, where we are, the title, the driver.
  ftext(out, brand_caps(), 76 * s, 74 * s, 22 * s, cyan, -1, 0, 9 * s);
  ftext(out, "WORLD TOUR", w - 76 * s, 72 * s, 15 * s, muted, 1, 0, 3 * s);
  rect(out, 74 * s, 100 * s, w - 74 * s, 101.5f * s, 0x30A9D2E0);
  ftext(out, title, 72 * s, 220 * s, 88 * s, white, -1, 2, 0);
  rect(out, 78 * s, 240 * s, 78 * s + 90 * s, 245 * s, cyan);
  if(label >= 0) {
    char b[48];
    snprintf(b, sizeof(b), "DRIVER  %s", v->menu_item[label].text);
    ftext(out, b, w - 76 * s, 220 * s, 22 * s, white, 1, 0, 3 * s);
  }
  // The choices.
  int items[48], ni = 0, letters = 0;
  for(int i = 0; i < v->menu_n; i++)
    if(i != header && i != label && (header < 0 || v->menu_item[i].row > 5 || !v->menu_item[i].big)) {
      items[ni++] = i;
      if(strlen(v->menu_item[i].text) == 1) letters++;
    }
  bool pics = v->menu_pics && v->pics[MENU_HI].w && v->menu_pbox[2] > v->menu_pbox[0];
  float top = 320 * s, bottom = h - 130 * s;
  bool info = false;
  for(int q = 0; q < ni; q++) info |= strstr(v->menu_item[items[q]].text, "STATISTICS") != NULL;
  if(info) {
    // The course before the race: its name huge, a table of its figures, its map traced.
    char cn[24], cc[24], buf[48];
    course_name(v->menu_course, cn, cc, sizeof(cn));
    int laps = tg_rom(0x0DC6D6 + v->menu_course)[0];
    int len = tg_rom(0x0DC6F6 + v->menu_course)[0] * (v->menu_kph ? 0x0F : 0x0A);
    int rec = v->menu_record, sec = rec / 60;
    ftext(out, cc, 80 * s, 330 * s, 26 * s, cyan, -1, 0, 8 * s);
    ftext(out, cn, 72 * s, 440 * s, 120 * s, white, -1, 2, 0);
    snprintf(buf, sizeof(buf), "RACE %d OF 4", (v->menu_course & 3) + 1);
    ftext(out, buf, 80 * s, 490 * s, 22 * s, muted, -1, 0, 4 * s);
    struct { const char* k; char v[24]; } rows[3];
    rows[0].k = "DISTANCE", snprintf(rows[0].v, 24, "%d.%02d %s", len >> 8, (len & 0xFF) * 100 >> 8, v->menu_kph ? "KM" : "MI");
    rows[1].k = "LAP RECORD", snprintf(rows[1].v, 24, "%d:%02d.%02d", sec / 60, sec % 60, rec % 60 * 100 / 60);
    rows[2].k = "LAPS", snprintf(rows[2].v, 24, "%d", laps);
    float tx0 = 78 * s, tx1 = w * 0.48f, ty = 560 * s;
    card(out, tx0, ty, tx1, ty + 3 * 96 * s + 20 * s, s, false, seconds);
    for(int r = 0; r < 3; r++) {
      float y = ty + 20 * s + r * 96 * s;
      if(r) rect(out, tx0 + 30 * s, y, tx1 - 30 * s, y + 1.2f * s, 0x28FFFFFF);
      ftext(out, rows[r].k, tx0 + 36 * s, y + 58 * s, 22 * s, muted, -1, 0, 5 * s);
      ftext(out, rows[r].v, tx1 - 40 * s, y + 66 * s, 56 * s, r == 1 ? cyan : white, 1, 2, 1 * s);
    }
    // The map: the game's picture of it, every lit pixel a glowing dot.
    if(v->menu_pics && v->pics[MENU_PIC].w && v->menu_pbox[2] > v->menu_pbox[0]) {
      float mx0 = w * 0.54f, mx1 = w - 90 * s, my0 = top - 40 * s, my1 = bottom;
      card(out, mx0, my0, mx1, my1, s, false, seconds);
      ftext(out, "TRACK", mx0 + 36 * s, my0 + 52 * s, 20 * s, cyan, -1, 0, 6 * s);
      const TgV3Pic* mp = &v->pics[MENU_PIC];
      // Only the map: under TRACK, left of STATISTICS (the rest are the game's figures).
      int bx0 = 256, by0 = 224, bx1 = 0, by1 = 0, lim = 256, top2 = 0;
      for(int q = 0; q < ni; q++) {
        if(strstr(v->menu_item[items[q]].text, "STATISTICS")) lim = v->menu_item[items[q]].col * 8 - 4;
        if(strstr(v->menu_item[items[q]].text, "TRACK")) top2 = v->menu_item[items[q]].row * 8 + 10;
      }
      for(int y = top2; y < 224; y++)
        for(int x2 = 0; x2 < lim; x2++)
          if(mp->px[y * 256 + x2] >> 24) bx0 = x2 < bx0 ? x2 : bx0, bx1 = x2 + 1 > bx1 ? x2 + 1 : bx1, by0 = y < by0 ? y : by0, by1 = y + 1 > by1 ? y + 1 : by1;
      if(bx1 <= bx0) bx0 = 0, bx1 = 1, by0 = 0, by1 = 1;
      int pb[4] = {bx0, by0, bx1, by1};
      float pw = pb[2] - pb[0], ph = pb[3] - pb[1];
      float kk = fminf((mx1 - mx0 - 160 * s) / pw, (my1 - my0 - 160 * s) / ph);
      float ox = (mx0 + mx1) / 2 - pw * kk / 2, oy = (my0 + my1) / 2 + 20 * s - ph * kk / 2;
      for(int pass = 0; pass < 2; pass++)
        for(int y = pb[1]; y < pb[3]; y++)
          for(int x2 = pb[0]; x2 < pb[2]; x2++) {
            uint32_t c = mp->px[y * 256 + x2];
            if(!(c >> 24)) continue;
            bool red = (c >> 16 & 255) > 180 && (c >> 8 & 255) < 90;
            float px = ox + (x2 - pb[0] + 0.5f) * kk, py = oy + (y - pb[1] + 0.5f) * kk;
            int lum = (c >> 16 & 255) + (c >> 8 & 255) + (c & 255);
            if(!red && lum < 500) continue;  // the map's light line only (not its dark rim)
            float r0 = pass ? kk * 0.3f : kk * 1.4f;
            uint32_t col = red ? 0xFFFF4A3A : cyan;
            if(!pass) screen_rect(out, px - r0, py - r0, px + r0, py + r0, 0, 1, with_alpha(col, 70), with_alpha(col, 70), TEX_GLOW);
            else rpanel(out, px - r0, py - r0, px + r0, py + r0, r0, red ? col : 0xFFE8FFFB);
          }
    }
  } else if(letters >= 10) {
    // The name, big, a blinking caret; the keyboard under it, full width.
    // The keyboard is the rows of single keys (three or more on a row); the name is whatever
    // is above them, put back together by its columns: a name of one letter, or of letters
    // and spaces ("A A"), is captured as single characters too and must not become keys.
    int kbTop = 99;
    {
      int perRow[32] = {0};
      for(int q = 0; q < ni; q++)
        if(strlen(v->menu_item[items[q]].text) == 1 && v->menu_item[items[q]].row < 32) perRow[v->menu_item[items[q]].row]++;
      // (the keyboard's five rows are the lowest such rows: a spaced name above them can make one too)
      int found = 0;
      for(int r = 31; r >= 0 && found < 5; r--)
        if(perRow[r] >= 3) kbTop = r, found++;
    }
    char name[12];
    {
      char buf[40];
      memset(buf, ' ', sizeof(buf));
      int col0 = 99, last = -1;
      for(int q = 0; q < ni; q++)
        if(v->menu_item[items[q]].row < kbTop && v->menu_item[items[q]].col < col0) col0 = v->menu_item[items[q]].col;
      for(int q = 0; q < ni; q++) {
        const char* t = v->menu_item[items[q]].text;
        if(v->menu_item[items[q]].row >= kbTop) continue;
        int step = v->menu_item[items[q]].big ? 2 : 1;
        for(int k = 0; t[k]; k++) {
          int at = (v->menu_item[items[q]].col - col0) / step + k;
          if(at >= 0 && at < 39 && t[k] != ' ') buf[at] = t[k], last = at > last ? at : last;
        }
      }
      int n = last + 1 > 8 ? 8 : last + 1;  // a name is 8 characters at most
      memcpy(name, buf, (size_t) (n > 0 ? n : 0));
      name[n > 0 ? n : 0] = 0;
    }
    float nw = ftext_w(name, 96 * s, 2, 14 * s);
    ftext(out, name, w / 2.0f - nw / 2, top + 90 * s, 96 * s, white, -1, 2, 14 * s);
    float ul = fmaxf(260 * s, nw / 2 + 40 * s);
    rect(out, w / 2.0f - ul, top + 118 * s, w / 2.0f + ul, top + 121 * s, 0x50A9D2E0);
    rect(out, w / 2.0f + nw / 2 + 10 * s, top + 10 * s, w / 2.0f + nw / 2 + 16 * s, top + 96 * s,
         with_alpha(cyan, (int) (255 * (sinf(seconds * 7) > 0))));
    if(v->menu_screen == 8 && v->country_free && v->country_busy) {
      // Country select FREE: the frontend is typing the country's password (country_input.h).
      char b[80];
      int c = v->menu_pw_country;
      snprintf(b, sizeof(b), "ENTERING THE PASSWORD FOR %s", kCountry[c].name);
      float bw = ftext_w(b, 18 * s, 0, 4 * s) + 36 * s * kFlagRatio[c] + 110 * s, bx = w - 78 * s - bw, by = 150 * s;
      rpanel(out, bx, by, bx + bw, by + 64 * s, 10 * s, 0xC0122A36);
      flag_card(out, c, bx + 18 * s, by + 14 * s, 36 * s, s, false, seconds);
      ftext(out, b, bx + 36 * s + 36 * s * kFlagRatio[c], by + 40 * s, 18 * s, 0xFFFFC24A, -1, 0, 4 * s);
      for(int d = 0; d < 3; d++)
        fdisc(out, bx + bw - 44 * s + d * 12 * s, by + 33 * s, 3.5f * s, with_alpha(0xFFFFC24A, (int) (90 + 165 * (0.5f + 0.5f * sinf(seconds * 8 - d)))));
    }
    int minc = 99, maxc = 0, minr = 99;
    for(int q = 0; q < ni; q++) {
      const char* t = v->menu_item[items[q]].text;
      if((strlen(t) > 1 && !strstr(t, "END")) || v->menu_item[items[q]].row < kbTop) continue;
      minc = v->menu_item[items[q]].col < minc ? v->menu_item[items[q]].col : minc;
      maxc = v->menu_item[items[q]].col > maxc ? v->menu_item[items[q]].col : maxc;
      minr = v->menu_item[items[q]].row < minr ? v->menu_item[items[q]].row : minr;
    }
    float key = 84 * s, gap = 16 * s, cols = (maxc - minc) / 4.0f + 1, kx0 = w / 2.0f - (cols * (key + gap) - gap) / 2, ky0 = top + 180 * s;
    for(int q = 0; q < ni; q++) {
      int i = items[q];
      const char* t = v->menu_item[i].text;
      if((strlen(t) > 1 && !strstr(t, "END")) || v->menu_item[i].row < kbTop) continue;
      bool sel = i == v->menu_sel;
      float kx = kx0 + (v->menu_item[i].col - minc) / 4.0f * (key + gap), ky = ky0 + (v->menu_item[i].row - minr) / 2.0f * (key + gap);
      float kw = strlen(t) > 1 ? ftext_w(t, 32 * s, 0, 2 * s) + 50 * s : key;
      card(out, kx, ky, kx + kw, ky + key, s, sel, seconds);
      ftext(out, t, kx + kw / 2, ky + key * 0.66f - (sel ? 10 * s : 0), 34 * s, sel ? cyan : white, 0, 0, 2 * s);
    }
  } else if(pics) {
    // Choices with pictures. One line of as many words as the pictures split into: a card
    // each (the gear select); else the picture big, its small labels on it, the big lines
    // as chips (the controls).
    char wd[8][33];
    int nw = 0, line = -1;
    for(int q = 0; q < ni; q++)
      if(v->menu_item[items[q]].big) line = items[q];
    if(line >= 0) nw = words(v->menu_item[line].text, wd, 8);
    float pw = v->menu_pbox[2] - v->menu_pbox[0], ph = v->menu_pbox[3] - v->menu_pbox[1];
    bool split = nw >= 2 && nw <= 4 && pw > ph * 1.3f && v->menu_cx >= 0;
    if(split) {
      float cw = fminf(420 * s, (w - 200 * s - (nw - 1) * 40 * s) / nw), ch = bottom - top;
      float x = w / 2.0f - (nw * cw + (nw - 1) * 40 * s) / 2;
      float curx = v->menu_cx;
      for(int k = 0; k < nw; k++, x += cw + 40 * s) {
        float c0 = v->menu_pbox[0] + pw * k / nw, c1 = v->menu_pbox[0] + pw * (k + 1) / nw;
        bool sel = curx >= c0 && curx < c1;
        card(out, x, top, x + cw, top + ch, s, sel, seconds);
        float lift = sel ? 10 * s : 0;
        float sw = c1 - c0, kk = fminf((cw - 80 * s) / sw, (ch - 180 * s) / ph), px = x + cw / 2 - sw * kk / 2, py = top + 50 * s - lift;
        if(strstr(wd[k], "MANUAL") || strstr(wd[k], "AUTO")) {
          gear_prop(out, strstr(wd[k], "AUTO") != NULL, x + cw / 2, top + (ch - 120 * s) / 2 - lift, ch * 1.15f,
                    200 + (sel ? seconds * 30 : 20), sel, (float) w, (float) h);
          char idx2[8];
          snprintf(idx2, sizeof(idx2), "0%d", k + 1);
          ftext(out, idx2, x + 30 * s, top + ch - 80 * s - lift, 20 * s, sel ? cyan : muted, -1, 0, 3 * s);
          ftext(out, wd[k], x + 30 * s, top + ch - 34 * s - lift, 44 * s, sel ? white : 0xFFB8C4D0, -1, 2, 2 * s);
          continue;
        }
        TgV3Vertex qv[6] = {{px, py, c0 / 256, v->menu_pbox[1] / 224.0f, 0xFFFFFFFF},
                            {px + sw * kk, py, c1 / 256, v->menu_pbox[1] / 224.0f, 0xFFFFFFFF},
                            {px + sw * kk, py + ph * kk, c1 / 256, v->menu_pbox[3] / 224.0f, 0xFFFFFFFF},
                            {px, py, c0 / 256, v->menu_pbox[1] / 224.0f, 0xFFFFFFFF},
                            {px + sw * kk, py + ph * kk, c1 / 256, v->menu_pbox[3] / 224.0f, 0xFFFFFFFF},
                            {px, py + ph * kk, c0 / 256, v->menu_pbox[3] / 224.0f, 0xFFFFFFFF}};
        list_push(out, qv, 6, MENU_HI);
        char idx[8];
        snprintf(idx, sizeof(idx), "0%d", k + 1);
        ftext(out, idx, x + 30 * s, top + ch - 80 * s - lift, 20 * s, sel ? cyan : muted, -1, 0, 3 * s);
        ftext(out, wd[k], x + 30 * s, top + ch - 34 * s - lift, 44 * s, sel ? white : 0xFFB8C4D0, -1, 2, 2 * s);
      }
    } else {
      float px0 = w * 0.36f, px1 = w - 90 * s, py0 = top, py1 = bottom;
      if(strstr(title, "CONTROL")) {
        card(out, px0, py0, px1, py1, s, false, seconds);
        float pts[9][2];
        pad_prop(out, (px0 + px1) / 2, (py0 + py1) / 2 + 30 * s, (py1 - py0) * 1.6f, 180 + sinf(seconds * 0.6f) * 10, 52, (float) w, (float) h, pts);
        // The game's own labels, each to its button: the labels sit at the picture's places;
        // a line runs to the nearest button.
        int used[9] = {0};
        for(int q = 0; q < ni; q++) {
          int i = items[q];
          if(v->menu_item[i].big) continue;
          const char* t = v->menu_item[i].text;
          int b = strstr(t, "LEFT") ? 0 : strstr(t, "GEAR") ? 5 : strstr(t, "BRAKE") ? 1 : strstr(t, "ACCEL") ? 3 : strstr(t, "NITRO") ? 4 : -1;
          if(b < 0 || used[b]) continue;
          used[b] = 1;
          float lx = b == 0 ? px0 + 40 * s : b == 5 ? px0 + 60 * s : px1 - 60 * s;
          float ly = b == 0 ? py1 - 50 * s : b == 5 ? py0 + 60 * s : py0 + 80 * s + (b == 1 ? 0 : b == 3 ? 90 : 180) * s;
          sbar(out, pts[b][0], pts[b][1], lx + (b == 0 || b == 5 ? 0 : -10 * s), ly - 8 * s, 2 * s, 0xC062F2DC);
          rpanel(out, pts[b][0] - 5 * s, pts[b][1] - 5 * s, pts[b][0] + 5 * s, pts[b][1] + 5 * s, 5 * s, 0xFF62F2DC);
          ftext(out, t, lx, ly, 24 * s, white, b == 0 || b == 5 ? -1 : 1, 0, 3 * s);
        }
        goto chips;
      }
      float kk = fminf((px1 - px0) / pw, (py1 - py0) / ph), cx = (px0 + px1) / 2, cy = (py0 + py1) / 2;
      float x0 = cx - pw * kk / 2, y0 = cy - ph * kk / 2;
      card(out, x0 - 40 * s, y0 - 40 * s, x0 + pw * kk + 40 * s, y0 + ph * kk + 40 * s, s, false, seconds);
      TgV3Vertex qv[6] = {{x0, y0, v->menu_pbox[0] / 256.0f, v->menu_pbox[1] / 224.0f, 0xFFFFFFFF},
                          {x0 + pw * kk, y0, v->menu_pbox[2] / 256.0f, v->menu_pbox[1] / 224.0f, 0xFFFFFFFF},
                          {x0 + pw * kk, y0 + ph * kk, v->menu_pbox[2] / 256.0f, v->menu_pbox[3] / 224.0f, 0xFFFFFFFF},
                          {x0, y0, v->menu_pbox[0] / 256.0f, v->menu_pbox[1] / 224.0f, 0xFFFFFFFF},
                          {x0 + pw * kk, y0 + ph * kk, v->menu_pbox[2] / 256.0f, v->menu_pbox[3] / 224.0f, 0xFFFFFFFF},
                          {x0, y0 + ph * kk, v->menu_pbox[0] / 256.0f, v->menu_pbox[3] / 224.0f, 0xFFFFFFFF}};
      list_push(out, qv, 6, MENU_HI);
      // Small labels on the picture, where they are on the game's screen.
      for(int q = 0; q < ni; q++) {
        int i = items[q];
        if(v->menu_item[i].big) continue;
        float gx = v->menu_item[i].col * 8, gy = v->menu_item[i].row * 8 + 6;
        ftext(out, v->menu_item[i].text, x0 + (gx - v->menu_pbox[0]) * kk, y0 + (gy - v->menu_pbox[1]) * kk, 20 * s, white, -1, 0, 2 * s);
      }
    chips:;
      // Big lines as chips: the first word a label, the rest chips; the cursor's chip lit.
      float y = top + 20 * s;
      for(int q = 0; q < ni; q++) {
        int i = items[q];
        if(!v->menu_item[i].big) continue;
        int n2 = words(v->menu_item[i].text, wd, 8);
        if(n2 < 1) continue;
        ftext(out, wd[0], 80 * s, y + 20 * s, 18 * s, muted, -1, 0, 6 * s);
        float cx2 = 80 * s, best = 1e9f;
        int sel = -1;
        for(int k = 1; k < n2; k++) {  // which chip: by the cursor's x on the game's screen
          float gx = v->menu_item[i].col * 8 + (strlen(wd[0]) + 1 + (k - 1) * 2) * 16 + 8;
          float d = fabsf(gx - v->menu_cx);
          if(v->menu_cx >= 0 && d < best) best = d, sel = k;
        }
        for(int k = 1; k < n2; k++) {
          float cw = 96 * s;
          card(out, cx2, y + 40 * s, cx2 + cw, y + 136 * s, s, k == sel, seconds);
          ftext(out, wd[k], cx2 + cw / 2, y + 104 * s - (k == sel ? 10 * s : 0), 46 * s, k == sel ? cyan : white, 0, 2, 0);
          cx2 += cw + 18 * s;
        }
        y += 180 * s;
      }
    }
  } else {
    // A list: tall cards across the screen, each with an icon, its name and a line about
    // it; the last one (PLAY GAME and the like) a wide red call to action below.
    int n = ni;
    bool cta = n >= 3 && (strstr(v->menu_item[items[n - 1]].text, "PLAY") || strstr(v->menu_item[items[n - 1]].text, "START"));
    int row = cta ? n - 1 : n;
    float gap = 26 * s, cw = fminf(400 * s, (w - 156 * s - (row - 1) * gap) / row), ch = cta ? 360 * s : 440 * s;
    float x = 78 * s;
    for(int k = 0; k < row; k++, x += cw + gap) {
      int i = items[k];
      const char* t = v->menu_item[i].text;
      bool sel = i == v->menu_sel;
      float lift = sel ? 10 * s : 0;
      card(out, x, top, x + cw, top + ch, s, sel, seconds);
      // The icon, drawn: players, a gauge, a globe, a speedometer, else a chevron.
      float ix = x + 52 * s, iy = top + 76 * s - lift, ir = 30 * s;
      uint32_t ic = sel ? cyan : 0xFF8AA0B2;
      if(strstr(t, "PLAYER")) {
        rpanel(out, ix - 12 * s, iy - 30 * s, ix + 12 * s, iy - 6 * s, 12 * s, ic);
        rpanel(out, ix - 26 * s, iy, ix + 26 * s, iy + 26 * s, 12 * s, ic);
      } else if(strstr(t, "SKILL")) {
        for(int b = 0; b < 3; b++) rect(out, ix - 28 * s + b * 22 * s, iy + 22 * s - (b + 1) * 16 * s, ix - 12 * s + b * 22 * s, iy + 24 * s, b < 2 || sel ? ic : 0xFF3A4A58);
      } else if(strstr(t, "COUNTRY")) {
        for(int a = 0; a < 24; a++) sbar(out, ix + cosf(a * PI / 12) * ir, iy + sinf(a * PI / 12) * ir, ix + cosf((a + 1) * PI / 12) * ir,
                                         iy + sinf((a + 1) * PI / 12) * ir, 3 * s, ic);
        sbar(out, ix - ir, iy, ix + ir, iy, 2.5f * s, ic);
        for(int a = 0; a < 12; a++) sbar(out, ix + sinf(a * PI / 12 - PI / 2) * ir * 0.45f, iy - cosf(a * PI / 12 - PI / 2) * ir,
                                         ix + sinf((a + 1) * PI / 12 - PI / 2) * ir * 0.45f, iy - cosf((a + 1) * PI / 12 - PI / 2) * ir, 2.5f * s, ic);
      } else if(strstr(t, "SPEED") || strstr(t, "KPH") || strstr(t, "MPH")) {
        for(int a = 0; a < 16; a++) sbar(out, ix + cosf(PI + a * PI / 16) * ir, iy + 10 * s + sinf(PI + a * PI / 16) * ir,
                                         ix + cosf(PI + (a + 1) * PI / 16) * ir, iy + 10 * s + sinf(PI + (a + 1) * PI / 16) * ir, 3.5f * s, ic);
        sbar(out, ix, iy + 10 * s, ix + ir * 0.6f, iy - ir * 0.45f, 3.5f * s, sel ? 0xFFFF4A3A : ic);
      } else {
        sbar(out, ix - 18 * s, iy - 18 * s, ix + 6 * s, iy, 6 * s, ic), sbar(out, ix + 6 * s, iy, ix - 18 * s, iy + 18 * s, 6 * s, ic);
      }
      char idx[8];
      snprintf(idx, sizeof(idx), "%02d", k + 1);
      ftext(out, idx, x + cw - 24 * s, top + 64 * s - lift, 26 * s, sel ? cyan : 0x50FFFFFF, 1, 0, 2 * s);
      const char* desc = strstr(t, "PLAYER") ? "Race alone, or split screen with a friend." : strstr(t, "SKILL") ? "Amateur, professional or championship."
                         : strstr(t, "COUNTRY") ? "Choose where your world tour begins." : strstr(t, "SPEED") ? "Show speed in km/h or mph." : "";
      char wd2[6][33];
      int n2 = words(t, wd2, 6);
      float ty = top + ch - 110 * s - lift - (n2 - 1) * 50 * s;
      for(int q = 0; q < n2; q++, ty += 50 * s) ftext(out, wd2[q], x + 30 * s, ty, 54 * s, sel ? white : 0xFFC8D2DC, -1, 2, 1 * s);
      rect(out, x + 30 * s, top + ch - 86 * s - lift, x + cw - 30 * s, top + ch - 85 * s - lift, sel ? 0x6062F2DC : 0x20FFFFFF);
      // The line about it, wrapped in two.
      if(desc[0]) {
        char l1[64] = "", l2[64] = "";
        const char* br = strchr(desc + 18, ' ');
        if(br && strlen(desc) > 26) {
          snprintf(l1, sizeof(l1), "%.*s", (int) (br - desc), desc);
          snprintf(l2, sizeof(l2), "%s", br + 1);
        } else {
          snprintf(l1, sizeof(l1), "%s", desc);
        }
        ftext(out, l1, x + 30 * s, top + ch - 50 * s - lift, 22 * s, sel ? 0xFFB8CCD8 : muted, -1, 1, 0.5f * s);
        if(l2[0]) ftext(out, l2, x + 30 * s, top + ch - 24 * s - lift, 22 * s, sel ? 0xFFB8CCD8 : muted, -1, 1, 0.5f * s);
      }
    }
    if(cta) {
      int i = items[n - 1];
      bool sel = i == v->menu_sel;
      float y0 = top + ch + 40 * s, y1 = y0 + 120 * s, x1 = 78 * s + row * (cw + gap) - gap;
      float lift = sel ? 8 * s : 0;
      bool ov = out->overlay;
      out->overlay = false;
      if(sel) {
        screen_rect(out, 78 * s, y0 - 90 * s, x1, y1 + 90 * s, 0, 1, 0x70FF3A2C, 0x70FF3A2C, TEX_GLOW);
        cpanel(out, 78 * s, y0 - lift, x1, y1 - lift, 40 * s, 0xFFFF4A3A, 0xFFC0141E, 0x80FFFFFF, s);
        float sw = fmodf(seconds * 0.6f, 1.4f) * (x1 - 78 * s + 300 * s) - 150 * s;
        sbar(out, 78 * s + sw, y1 - lift, 78 * s + sw + 60 * s, y0 - lift, 50 * s, 0x28FFFFFF);
      } else {
        cpanel(out, 78 * s, y0, x1, y1, 40 * s, 0xD03A1218, 0xD01E0A0E, 0x50FF4A3A, s);
      }
      out->overlay = ov;
      ftext(out, v->menu_item[i].text, 120 * s, y0 + 82 * s - lift, 64 * s, white, -1, 2, 4 * s);
      for(int a = 0; a < 3; a++) {
        float ax = x1 - 150 * s + a * 34 * s, ay = y0 + 60 * s - lift;
        uint32_t c = with_alpha(0xFFFFFFFF, sel ? (int) (120 + 135 * (0.5f + 0.5f * sinf(seconds * 6 - a))) : 90);
        sbar(out, ax, ay - 22 * s, ax + 20 * s, ay, 9 * s, c), sbar(out, ax + 20 * s, ay, ax, ay + 22 * s, 9 * s, c);
      }
    }
  }
  // The bottom: the controls, as the game takes them. These menus move with the D-pad and
  // take any other button (A, B, X, Y, L, R, START) as the choice: there is no back button.
  static const char* const menuHints[2][2] = {{"D-PAD", "CHOOSE"}, {"A / START", "SELECT"}};
  // The name entry takes its letters with A, and ends on END (START does nothing here).
  static const char* const nameHints[3][2] = {{"D-PAD", "MOVE"}, {"A", "TYPE THE LETTER  (X KEY)"}, {"END", "SELECT IT WITH A TO FINISH"}};
  if(letters >= 10) key_hints(out, w, h, s, nameHints, 3);
  else key_hints(out, w, h, s, menuHints, 2);
  (void) ink;
}

// The backdrop and header the modern menus share.
static void menu_photo(const TgView3D* v, TgV3List* out, int w, int h, float seconds) {
  if(!v->menu_has_bg || !v->pics[MENU_BG].w) return;
  // The game's photograph, wide and slowly drifting, tinted into the navy, a vignette.
  float k = fmaxf(w / 256.0f, h / 224.0f) * 1.08f, ox = (w - 256 * k) / 2 + sinf(seconds * 0.2f) * 20, oy = (h - 224 * k) / 2;
  screen_rect(out, ox, oy, ox + 256 * k, oy + 224 * k, 0, 1, 0x7096B8D0, 0x7096B8D0, MENU_BG);
  screen_rect(out, 0, 0, (float) w, (float) h * 0.5f, 0, 1, 0xC00A1420, 0x400A1420, -1);
  screen_rect(out, 0, h * 0.5f, (float) w, (float) h, 0, 1, 0x400A1420, 0xE0050A10, -1);
  screen_rect(out, 0, 0, w * 0.45f, (float) h, 0, 1, 0x900A1420, 0x900A1420, -1);
}

static void menu_frame(const TgView3D* v, TgV3List* out, int w, int h, float seconds, const char* title) {
  float s = h / 1080.0f;
  const uint32_t white = 0xFFF0F5F7, cyan = 0xFF62F2DC, muted = 0xFF8A9AAA;
  screen_rect(out, 0, 0, (float) w, (float) h, 0, 1, 0xFF0A1420, 0xFF050A10, -1);
  menu_photo(v, out, w, h, seconds);
  for(int i = 0; i < 5; i++) {
    float x = fmodf(i * 0.27f + seconds * 0.015f * (i + 1), 1.4f) * w - 0.2f * w;
    sbar(out, x, (float) h * 1.1f, x + h * 0.9f, -(float) h * 0.1f, (60 + i * 50) * s, i == 2 ? 0x18FF4A3A : 0x1462F2DC);
  }
  screen_rect(out, w * 0.1f, -h * 0.2f, w * 0.9f, h * 0.7f, 0, 1, 0x2862F2DC, 0x2862F2DC, TEX_GLOW);
  out->overlay = true;
  ftext(out, title, w * 0.5f, h * 0.62f, 300 * s, 0x0CFFFFFF, 0, 2, 10 * s);
  ftext(out, brand_caps(), 76 * s, 74 * s, 22 * s, cyan, -1, 0, 9 * s);
  ftext(out, "WORLD TOUR", w - 76 * s, 72 * s, 15 * s, muted, 1, 0, 3 * s);
  rect(out, 74 * s, 100 * s, w - 74 * s, 101.5f * s, 0x30A9D2E0);
  ftext(out, title, 72 * s, 220 * s, 88 * s, white, -1, 2, 0);
  rect(out, 78 * s, 240 * s, 78 * s + 90 * s, 245 * s, cyan);
}

// The controls along the bottom: key chips and what they do (what the game accepts there).
static void key_hints(TgV3List* out, int w, int h, float s, const char* const (*hints)[2], int n) {
  const uint32_t white = 0xFFF0F5F7, muted = 0xFF8A9AAA;
  rect(out, 74 * s, h - 92 * s, w - 74 * s, h - 90.5f * s, 0x30A9D2E0);
  float bx = 78 * s;
  for(int k = 0; k < n; k++) {
    float kw = ftext_w(hints[k][0], 15 * s, 0, 2 * s) + 24 * s;
    rpanel(out, bx, h - 66 * s, bx + kw, h - 34 * s, 6 * s, 0x30FFFFFF);
    ftext(out, hints[k][0], bx + kw / 2, h - 44 * s, 15 * s, white, 0, 0, 2 * s);
    ftext(out, hints[k][1], bx + kw + 12 * s, h - 44 * s, 15 * s, muted, -1, 0, 3 * s);
    bx += kw + 30 * s + ftext_w(hints[k][1], 15 * s, 0, 3 * s);
  }
}

// Fades (and moves) everything pushed since vertex `from`: the page transitions.
static void list_fade(TgV3List* l, int from, float alpha, float dy) {
  alpha = alpha < 0 ? 0 : alpha > 1 ? 1 : alpha;
  for(int i = from; i < l->nv; i++) {
    TgV3Vertex* t = &l->v[i];
    t->y += dy;
    t->argb = (t->argb & 0xFFFFFF) | (uint32_t) ((float) (t->argb >> 24) * alpha + 0.5f) << 24;
  }
}

// A time in frames as the game writes it (seconds = frames / 60, hundredths = the rest * 100
// / 60, truncated: MenuFormatTime), as M:SS.HH.
static void race_time(char* o, size_t n, unsigned frames, bool plus) {
  unsigned sec = frames / 60, hund = (frames % 60) * 100 / 60;
  if(plus && sec < 60) snprintf(o, n, "+%u.%02u", sec, hund);
  else snprintf(o, n, "%s%u:%02u.%02u", plus ? "+" : "", sec / 60, sec % 60, hund);
}

static const char* ordinal(int p) {
  static char b[8];
  const char* suf = (p % 100) / 10 == 1 ? "TH" : p % 10 == 1 ? "ST" : p % 10 == 2 ? "ND" : p % 10 == 3 ? "RD" : "TH";
  snprintf(b, sizeof(b), "%d%s", p, suf);
  return b;
}

// Is driver d one of the players? (0 player 1; 1 player 2 in a 2-player game, else the
// computer rival.) 1 or 2 = which player, 0 = a computer driver.
static int res_player(const TgView3D* v, int d) { return d == 0 ? 1 : d == 1 && v->res.two ? 2 : 0; }

// Race points by place ($0E:8024).
static int race_points(int place) { return place >= 0 && place < 10 ? tg_rom(0x0E8024 + (uint32_t) place)[0] : 0; }

// The results page or the standings page, from the game's own tables (v->res). Layout after
// broadcast timing screens: the winner's TIME and everybody's GAP to him in their own
// columns (F1's LEADER/GAP labelling), the player's row marked, the cut that decides the
// next race drawn as a line; a card beside it tells the player his own outcome.
static void results_page(TgView3D* v, int w, int h, float seconds, TgV3List* out, bool ranking) {
  float s = h / 1080.0f;
  const uint32_t white = 0xFFF0F5F7, cyan = 0xFF62F2DC, muted = 0xFF8A9AAA, gold = 0xFFE6B866, red = 0xFFFF5A48;
  const uint32_t silver = 0xFFD8DEE6, bronze = 0xFFD08A50;
  // The table and, where there is room, the player's card beside it.
  float x0 = 78 * s, cardW = 560 * s, gapW = 56 * s;
  float x1 = fminf(x0 + 1220 * s, w - 78 * s - cardW - gapW);
  bool withCard = x1 - x0 > 900 * s;
  if(!withCard) x1 = w - 78 * s;
  float W = x1 - x0, top = 292 * s, bottom = h - 120 * s;
  // Each player's place in this race (-1: not in the first ten) and in the championship.
  int place[2] = {-1, -1}, standing[2] = {-1, -1};
  for(int i = 0; i < 10; i++)
    if(res_player(v, v->res.order[i]) && place[v->res.order[i]] < 0) place[v->res.order[i]] = i;
  for(int i = 0; i < 20; i++)
    if(res_player(v, v->res.rank[i]) && standing[v->res.rank[i]] < 0) standing[v->res.rank[i]] = i;
  int players = v->res.two ? 2 : 1;
  bool through = false;  // a player in the first five goes on (MenuCheckQualified)
  for(int p = 0; p < players; p++) through |= place[p] >= 0 && place[p] < 5;
  char buf[64];
  if(!ranking) {
    // RESULTS: POS | DRIVER | TIME | GAP | PTS.
    float cTime = x0 + W * 0.50f, cGap = x0 + W * 0.68f, cPts = x1 - 34 * s;
    ftext(out, "POS", x0 + 40 * s, top, 15 * s, muted, 0, 0, 4 * s);
    ftext(out, "DRIVER", x0 + 110 * s, top, 15 * s, muted, -1, 0, 4 * s);
    ftext(out, "TIME", cTime, top, 15 * s, muted, -1, 0, 4 * s);
    ftext(out, "GAP TO WINNER", cGap, top, 15 * s, muted, -1, 0, 4 * s);
    ftext(out, "PTS", cPts, top, 15 * s, muted, 1, 0, 4 * s);
    float rh = fminf(60 * s, (bottom - top - 40 * s) / 10), y = top + 16 * s;
    unsigned win = v->res.time[v->res.order[0]];
    for(int r = 0; r < 10; r++, y += rh) {
      int d = v->res.order[r], pl = res_player(v, d);
      float ry0 = y + 3 * s, ry1 = y + rh - 3 * s, base = ry1 - (rh - 6 * s) * 0.27f;
      bool ov = out->overlay;
      out->overlay = false;
      cpanel(out, x0, ry0, x1, ry1, 16 * s, pl ? 0xF01E4652 : r < 3 ? 0xD0202A36 : 0xC0141C26, pl ? 0xF00E2430 : 0xC00A121A,
             pl ? cyan : 0x28A9D2E0, s);
      out->overlay = ov;
      if(pl) rect(out, x0, ry0 + 6 * s, x0 + 5 * s, ry1 - 6 * s, cyan);
      uint32_t pc = r == 0 ? gold : r == 1 ? silver : r == 2 ? bronze : muted;
      snprintf(buf, sizeof(buf), "%d", r + 1);
      ftext(out, buf, x0 + 40 * s, ry1 - (rh - 6 * s) * 0.22f, rh * 0.6f, pc, 0, 2, 0);
      ftext(out, v->res.name[d], x0 + 110 * s, base, rh * 0.46f, pl ? cyan : white, -1, 0, 2 * s);
      if(pl) {  // a tag: YOU (P1 / P2 in a 2-player game)
        const char* tag = players == 2 ? (pl == 1 ? "P1" : "P2") : "YOU";
        float tx = x0 + 110 * s + ftext_w(v->res.name[d], rh * 0.46f, 0, 2 * s) + 16 * s, tw = ftext_w(tag, 13 * s, 0, 2 * s) + 16 * s;
        rpanel(out, tx, base - 22 * s, tx + tw, base + 4 * s, 5 * s, cyan);
        ftext(out, tag, tx + tw / 2, base - 3 * s, 13 * s, 0xFF0A1420, 0, 0, 2 * s);
      }
      unsigned t = v->res.time[d];
      race_time(buf, sizeof(buf), t, false);
      ftext(out, buf, cTime, base, rh * 0.44f, white, -1, 1, 1 * s);
      if(r == 0) {
        ftext(out, "WINNER", cGap, base, rh * 0.34f, gold, -1, 0, 3 * s);
      } else {
        race_time(buf, sizeof(buf), t >= win ? t - win : 0, true);
        ftext(out, buf, cGap, base, rh * 0.40f, 0xFF9FB0C0, -1, 1, 1 * s);
      }
      snprintf(buf, sizeof(buf), "+%d", race_points(r));
      ftext(out, buf, cPts, ry1 - (rh - 6 * s) * 0.22f, rh * 0.52f, white, 1, 2, 0);
      if(r == 4) {  // the cut: the first five go on to the next race
        float ly = y + rh;
        for(float lx = x0; lx < x1; lx += 18 * s) rect(out, lx, ly - 1 * s, fminf(lx + 10 * s, x1), ly + 1 * s, 0xB062F2DC);
        const char* cut = "TOP 5 GO THROUGH";
        float cw = ftext_w(cut, 13 * s, 0, 3 * s);
        rpanel(out, x1 - cw - 44 * s, ly - 11 * s, x1 - 14 * s, ly + 11 * s, 5 * s, 0xFF0B1A24);
        ftext(out, cut, x1 - 29 * s, ly + 6 * s, 13 * s, cyan, 1, 0, 3 * s);
      }
    }
  } else {
    // WORLD RANKING: the 20 drivers in two columns of ten: POS | DRIVER | PTS (+ this race's).
    int racePts[20] = {0};
    for(int i = 0; i < 10; i++) racePts[v->res.order[i]] = race_points(i);
    float colW = (W - 30 * s) / 2, rh = fminf(60 * s, (bottom - top - 30 * s) / 10);
    for(int c = 0; c < 2; c++) {
      float cx0 = x0 + c * (colW + 30 * s), cx1 = cx0 + colW, y = top + 16 * s;
      ftext(out, "POS", cx0 + 36 * s, top, 15 * s, muted, 0, 0, 4 * s);
      ftext(out, "DRIVER", cx0 + 90 * s, top, 15 * s, muted, -1, 0, 4 * s);
      ftext(out, "PTS", cx1 - 26 * s, top, 15 * s, muted, 1, 0, 4 * s);
      for(int r = c * 10; r < c * 10 + 10; r++, y += rh) {
        int d = v->res.rank[r], pl = res_player(v, d);
        float ry0 = y + 3 * s, ry1 = y + rh - 3 * s, base = ry1 - (rh - 6 * s) * 0.27f;
        bool ov = out->overlay;
        out->overlay = false;
        cpanel(out, cx0, ry0, cx1, ry1, 14 * s, pl ? 0xF01E4652 : r < 3 ? 0xD0202A36 : 0xC0141C26, pl ? 0xF00E2430 : 0xC00A121A,
               pl ? cyan : 0x28A9D2E0, s);
        out->overlay = ov;
        if(pl) rect(out, cx0, ry0 + 6 * s, cx0 + 5 * s, ry1 - 6 * s, cyan);
        uint32_t pc = r == 0 ? gold : r == 1 ? silver : r == 2 ? bronze : muted;
        snprintf(buf, sizeof(buf), "%d", r + 1);
        ftext(out, buf, cx0 + 36 * s, ry1 - (rh - 6 * s) * 0.22f, rh * 0.56f, pc, 0, 2, 0);
        ftext(out, v->res.name[d], cx0 + 90 * s, base, rh * 0.44f, pl ? cyan : white, -1, 0, 2 * s);
        if(pl) {
          const char* tag = players == 2 ? (pl == 1 ? "P1" : "P2") : "YOU";
          float tx = cx0 + 90 * s + ftext_w(v->res.name[d], rh * 0.44f, 0, 2 * s) + 14 * s, tw = ftext_w(tag, 13 * s, 0, 2 * s) + 16 * s;
          rpanel(out, tx, base - 22 * s, tx + tw, base + 4 * s, 5 * s, cyan);
          ftext(out, tag, tx + tw / 2, base - 3 * s, 13 * s, 0xFF0A1420, 0, 0, 2 * s);
        }
        snprintf(buf, sizeof(buf), "%d", v->res.pts[d]);
        ftext(out, buf, cx1 - 26 * s, ry1 - (rh - 6 * s) * 0.22f, rh * 0.52f, white, 1, 2, 0);
        if(racePts[d]) {
          snprintf(buf, sizeof(buf), "+%d", racePts[d]);
          ftext(out, buf, cx1 - 26 * s - ftext_w("000", rh * 0.52f, 2, 0) - 14 * s, base, rh * 0.32f, 0xFF7FD8C8, 1, 1, 1 * s);
        }
      }
    }
  }
  // The player's card: his place, and what it means.
  if(withCard) {
    float cx0 = x1 + gapW, cx1 = fminf(w - 78 * s, cx0 + cardW), cy0 = top - 26 * s, cy1 = cy0 + (players == 2 ? 580 : 400) * s;
    card(out, cx0, cy0, cx1, cy1, s, false, seconds);
    float y = cy0 + 54 * s;
    ftext(out, ranking ? "CHAMPIONSHIP" : "YOUR RACE", cx0 + 36 * s, y, 15 * s, muted, -1, 0, 4 * s);
    for(int p = 0; p < players; p++) {
      int d = p, pos = ranking ? standing[p] : place[p];
      y += 30 * s;
      if(players == 2) {
        ftext(out, v->res.name[d], cx0 + 36 * s, y + 10 * s, 22 * s, cyan, -1, 0, 2 * s);
        y += 22 * s;
      }
      uint32_t pc = pos == 0 ? gold : pos == 1 ? silver : pos == 2 ? bronze : white;
      float big = players == 2 ? 96 * s : 140 * s;
      ftext(out, pos >= 0 ? ordinal(pos + 1) : "--", cx0 + 32 * s, y + big * 0.95f, big, pc, -1, 2, 0);
      y += big * 0.95f + 44 * s;
      if(!ranking) {
        if(pos >= 0) {
          race_time(buf, sizeof(buf), v->res.time[d], false);
          char line[80];
          snprintf(line, sizeof(line), "%s   +%d PTS", buf, race_points(pos));
          ftext(out, line, cx0 + 36 * s, y, 30 * s, white, -1, 1, 1 * s);
        } else {
          ftext(out, "OUTSIDE THE TOP 10   NO POINTS", cx0 + 36 * s, y, 22 * s, white, -1, 0, 2 * s);
        }
      } else {
        int lead = v->res.pts[v->res.rank[0]], mine = v->res.pts[d];
        if(pos == 0) snprintf(buf, sizeof(buf), "%d PTS   LEADER", mine);
        else snprintf(buf, sizeof(buf), "%d PTS   %d BEHIND THE LEADER", mine, lead - mine);
        ftext(out, buf, cx0 + 36 * s, y, 26 * s, white, -1, 1, 1 * s);
      }
      y += 20 * s;
    }
    // What happens next: race n of 4 in this country; the top-5 rule.
    y = cy1 - 40 * s;
    rect(out, cx0 + 36 * s, y - 62 * s, cx1 - 36 * s, y - 61 * s, 0x30A9D2E0);
    const char* msg = !through ? "NOT QUALIFIED  -  TOP 5 NEEDED"
                      : (v->menu_course & 3) == 3 ? "QUALIFIED FOR THE NEXT COUNTRY" : "QUALIFIED FOR THE NEXT RACE";
    ftext(out, msg, cx0 + 36 * s, y - 22 * s, 20 * s, through ? cyan : red, -1, 0, 3 * s);
    char name[40], country[40];
    course_name(v->menu_course, name, country, sizeof(name));
    snprintf(buf, sizeof(buf), "RACE %d OF 4", (v->menu_course & 3) + 1);
    ftext(out, buf, cx0 + 36 * s, y + 8 * s, 14 * s, muted, -1, 0, 4 * s);
  }
}

// The race's results, then (the game scrolls BG1 down to them) the championship standings.
// The tables come from WRAM; BG1 only says which page is up and how far the scroll is.
static void draw_results(TgView3D* v, int w, int h, float seconds, TgV3List* out, int header) {
  (void) header;
  float s = h / 1080.0f;
  float t = v->menu_scroll / 256.0f;  // 0 results .. 1 standings (3 lines a frame)
  t = t < 0 ? 0 : t > 1 ? 1 : t;
  bool ranking = t >= 0.5f;
  menu_frame(v, out, w, h, seconds, ranking ? "WORLD RANKING" : "RESULTS");
  {
    char name[40], country[40], line[96];
    course_name(v->menu_course, name, country, sizeof(name));
    snprintf(line, sizeof(line), "%s%s%s", name, country[0] ? ",  " : "", country);
    ftext(out, line, 78 * s + ftext_w(ranking ? "WORLD RANKING" : "RESULTS", 88 * s, 2, 0) + 40 * s, 214 * s, 26 * s, 0xFFB8C4D0, -1, 0, 3 * s);
  }
  // The page slides as the game's scroll goes: the results up and out, the standings in.
  int from = out->nv;
  results_page(v, w, h, seconds, out, ranking);
  float k = ranking ? (t - 0.5f) * 2 : t * 2;  // 0 settled .. 1 at the swap
  if(ranking) k = 1 - k;
  list_fade(out, from, 1 - k, (ranking ? 1 : -1) * k * 120 * s);
  static const char* const hints[1][2] = {{"ANY BUTTON", "CONTINUE"}};
  key_hints(out, w, h, s, hints, 1);
}

// A screen of a few lines (the race over, a qualification): its picture in a card, the
// lines big beside it.
#include "view3d_champion.inc"

static void draw_message(TgView3D* v, int w, int h, float seconds, TgV3List* out) {
  if(v->champ_frames > 0) {
    draw_champion(v, w, h, out);
    return;
  }
  float s = h / 1080.0f;
  const uint32_t white = 0xFFF0F5F7, cyan = 0xFF62F2DC, red = 0xFFFF5A48;
  bool bad = false;
  for(int i = 0; i < v->menu_n; i++) bad |= strstr(v->menu_item[i].text, "SORRY") || strstr(v->menu_item[i].text, "FAIL");
  menu_frame(v, out, w, h, seconds, bad ? "RACE OVER" : "WORLD TOUR");
  float tx = 78 * s;
  {
    // The player's car, turning slowly under a spot (red if the race went badly).
    float px0 = 78 * s, py0 = 300 * s, px1 = w * 0.5f, py1 = h - 140 * s;
    card(out, px0, py0, px1, py1, s, false, seconds);
    bool ov = out->overlay;
    out->overlay = false;
    screen_rect(out, px0, py0, px1, py1, 0, 1, bad ? 0x50FF3A2C : 0x5062F2DC, bad ? 0x50FF3A2C : 0x5062F2DC, TEX_GLOW);
    out->overlay = ov;
    Gather g = {0};
    Cam cam;
    g_haze = 0xFF0A1420;
    select_car(v, &g, &cam, v->ptype[0] & 3, (px0 + px1) / 2, (py0 + py1) / 2 + 30 * s, (py1 - py0) * 1.05f, 200 + seconds * 16, 12, 7.8f,
               (float) w, (float) h);
    prop_flush(&g, out);
    tx = px1 + 70 * s;
  }
  bool pics = false;
  if(pics) {
    float px0 = 78 * s, py0 = 300 * s, py1 = h - 140 * s, ph = v->menu_pbox[3] - v->menu_pbox[1], pw = v->menu_pbox[2] - v->menu_pbox[0];
    float kk = (py1 - py0) / ph, px1 = px0 + pw * kk;
    card(out, px0, py0, px1, py1, s, false, seconds);
    TgV3Vertex q[6] = {{px0, py0, v->menu_pbox[0] / 256.0f, v->menu_pbox[1] / 224.0f, 0xFFFFFFFF},
                       {px1, py0, v->menu_pbox[2] / 256.0f, v->menu_pbox[1] / 224.0f, 0xFFFFFFFF},
                       {px1, py1, v->menu_pbox[2] / 256.0f, v->menu_pbox[3] / 224.0f, 0xFFFFFFFF},
                       {px0, py0, v->menu_pbox[0] / 256.0f, v->menu_pbox[1] / 224.0f, 0xFFFFFFFF},
                       {px1, py1, v->menu_pbox[2] / 256.0f, v->menu_pbox[3] / 224.0f, 0xFFFFFFFF},
                       {px0, py1, v->menu_pbox[0] / 256.0f, v->menu_pbox[3] / 224.0f, 0xFFFFFFFF}};
    list_push(out, q, 6, MENU_HI);
    tx = px1 + 70 * s;
  }
  // Each player's name and place in the race (the original's "PLAYER 1  8TH" label, drawn
  // there in sprites; here from the finishing order $1F12).
  float y = 420 * s;
  bool endScreen = false;  // the end of a race's run: failed, qualified, the game completed
  for(int i = 0; i < v->menu_n; i++)
    endScreen |= strstr(v->menu_item[i].text, "QUALIF") || strstr(v->menu_item[i].text, "SORRY") || strstr(v->menu_item[i].text, "COMPLET");
  if(endScreen) {
    int players = v->res.two ? 2 : 1;
    float ly = 330 * s;
    for(int p = 0; p < players; p++) {
      int place = -1;
      for(int i = 0; i < 10; i++)
        if(v->res.order[i] == p) place = i;
      char line[48];
      if(place >= 0) snprintf(line, sizeof(line), "FINISHED %s", ordinal(place + 1));
      else snprintf(line, sizeof(line), "OUTSIDE THE TOP 10");
      const char* nm = v->res.name[p][0] ? v->res.name[p] : (p ? "PLAYER 2" : "PLAYER 1");
      float nw = ftext_w(nm, 26 * s, 0, 2 * s), lw = ftext_w(line, 26 * s, 2, 1 * s);
      rpanel(out, tx - 14 * s, ly - 34 * s, tx + nw + lw + 50 * s, ly + 12 * s, 8 * s, 0xC0101C28);
      rect(out, tx - 14 * s, ly - 34 * s, tx - 9 * s, ly + 12 * s, bad ? red : cyan);
      ftext(out, nm, tx + 6 * s, ly, 26 * s, cyan, -1, 0, 2 * s);
      ftext(out, line, tx + nw + 30 * s, ly, 26 * s, place >= 0 && place < 5 ? white : (bad ? red : white), -1, 2, 1 * s);
      ly += 56 * s;
    }
    y = ly + 120 * s;  // the message under the label(s)
  }
  for(int i = 0; i < v->menu_n; i++) {
    bool first = i == 0;
    ftext(out, v->menu_item[i].text, tx, y, first ? 110 * s : 44 * s, first ? (bad ? red : cyan) : white, -1, 2, 1 * s);
    y += first ? 90 * s : 60 * s;
  }
  static const char* const hints[1][2] = {{"ANY BUTTON", "CONTINUE"}};  // MenuWaitButton: any button
  key_hints(out, w, h, s, hints, 1);
}

bool tg_view3d_has_hud(const TgView3D* v) { return v->pics[HUD_PIC].w != 0; }

void tg_view3d_set_subframe(TgView3D* v, float t) { v->subframe = t < 0 ? 0 : t > 1 ? 1 : t; }

// --- A small rasteriser, for tests and tg_headless ---------------------------------------

// A mipmapped picture (TgV3Pic.mips) sampled as GL does with LINEAR_MIPMAP_LINEAR: the level
// from lod (log2 of texels per pixel), bilinear in the two levels around it, clamped at the
// edges. out: a, r, g, b (0-255).
static void sample_mip(const TgV3Pic* p, float u, float v, float lod, float out[4]) {
  if(lod < 0) lod = 0;
  if(lod > p->mips) lod = (float) p->mips;
  int l0 = (int) lod;
  float fl = lod - l0;
  for(int i = 0; i < 4; i++) out[i] = 0;
  for(int s = 0; s < 2; s++) {
    int lv = l0 + s;
    float wgt = s ? fl : 1 - fl;
    if(wgt <= 0 || lv > p->mips) continue;
    const uint32_t* px = p->px;
    int lw = p->w, lh = p->h;
    for(int k = 0; k < lv; k++) px += (size_t) lw * lh, lw = lw > 1 ? lw / 2 : 1, lh = lh > 1 ? lh / 2 : 1;
    float x = u * lw - 0.5f, y = v * lh - 0.5f;
    int xi = (int) floorf(x), yi = (int) floorf(y);
    float fx = x - xi, fy = y - yi;
    for(int dy = 0; dy < 2; dy++)
      for(int dx = 0; dx < 2; dx++) {
        int tx = xi + dx, ty = yi + dy;
        tx = tx < 0 ? 0 : tx >= lw ? lw - 1 : tx;
        ty = ty < 0 ? 0 : ty >= lh ? lh - 1 : ty;
        uint32_t c = px[(size_t) ty * lw + tx];
        float k = wgt * (dx ? fx : 1 - fx) * (dy ? fy : 1 - fy);
        out[0] += k * (c >> 24), out[1] += k * (c >> 16 & 255), out[2] += k * (c >> 8 & 255), out[3] += k * (c & 255);
      }
  }
}

void tg_v3_raster(const TgView3D* v, const TgV3List* l, uint32_t* out, int w, int h) {
  // A depth buffer for the world (1/w: larger is nearer); the background is under it, the
  // overlay over it. Pictures are interpolated in perspective; a shaded material's picture
  // gives its colour before it is lit (as gl3d.c's shader).
  float* zb = calloc((size_t) w * h, sizeof(float));
  static const uint8_t kBayer[16] = TG_V3_BAYER4;
  for(int b = 0; b < l->nb; b++) {
    const TgV3Pic* pic = l->b[b].pic >= 0 ? tg_view3d_pic(v, l->b[b].pic) : NULL;
    if(pic && pic->w == 0) continue;
    int door = l->b[b].door;  // screen door: the pixels whose dither cell is below it left out
    for(int i = 0; i < l->b[b].count; i += 3) {
      const TgV3Vertex* p = &l->v[l->b[b].first + i];
      bool world = p[0].w > 0;
      float iw[3];
      for(int k = 0; k < 3; k++) iw[k] = world ? 1 / p[k].w : 1;
      float minx = fminf(p[0].x, fminf(p[1].x, p[2].x)), maxx = fmaxf(p[0].x, fmaxf(p[1].x, p[2].x));
      float miny = fminf(p[0].y, fminf(p[1].y, p[2].y)), maxy = fmaxf(p[0].y, fmaxf(p[1].y, p[2].y));
      int x0 = (int) fmaxf(0, floorf(minx)), x1 = (int) fminf((float) w - 1, ceilf(maxx));
      int y0 = (int) fmaxf(0, floorf(miny)), y1 = (int) fminf((float) h - 1, ceilf(maxy));
      float area = (p[1].x - p[0].x) * (p[2].y - p[0].y) - (p[2].x - p[0].x) * (p[1].y - p[0].y);
      if(fabsf(area) < 1e-6f) continue;
      // A mipmapped picture: the uv's change per pixel, for its level (u = U / Z, both linear
      // on the screen: du/dx = (Ux - u Zx) / Z).
      bool mip = pic && pic->mips > 0;
      float Zx = 0, Zy = 0, Ux = 0, Uy = 0, Vx = 0, Vy = 0;
      if(mip) {
        float a0x = (p[1].y - p[2].y) / area, a0y = (p[2].x - p[1].x) / area;
        float a1x = (p[2].y - p[0].y) / area, a1y = (p[0].x - p[2].x) / area;
        Zx = a0x * (iw[0] - iw[2]) + a1x * (iw[1] - iw[2]), Zy = a0y * (iw[0] - iw[2]) + a1y * (iw[1] - iw[2]);
        Ux = a0x * (iw[0] * p[0].u - iw[2] * p[2].u) + a1x * (iw[1] * p[1].u - iw[2] * p[2].u);
        Uy = a0y * (iw[0] * p[0].u - iw[2] * p[2].u) + a1y * (iw[1] * p[1].u - iw[2] * p[2].u);
        Vx = a0x * (iw[0] * p[0].v - iw[2] * p[2].v) + a1x * (iw[1] * p[1].v - iw[2] * p[2].v);
        Vy = a0y * (iw[0] * p[0].v - iw[2] * p[2].v) + a1y * (iw[1] * p[1].v - iw[2] * p[2].v);
      }
      bool opaqueVerts = (p[0].argb >> 24) == 255 && (p[1].argb >> 24) == 255 && (p[2].argb >> 24) == 255;
      // A shaded material: lit per pixel, as gl3d.c's shader does.
      int mat = world ? (int) (p[0].nrm >> 24 & 7) : 0;
      V3 mN[3] = {{0}}, mE[3] = {{0}};
      float mAo[3] = {0}, mFog[3] = {0};
      bool mNight = (p[0].eye >> 31) != 0;
      if(mat)
        for(int k = 0; k < 3; k++) {
          mN[k] = unsnorm8(p[k].nrm), mE[k] = unsnorm8(p[k].eye);
          mAo[k] = (float) (p[k].nrm >> 27) / 31, mFog[k] = (float) (p[k].eye >> 24 & 127) / 127;
        }
      for(int y = y0; y <= y1; y++)
        for(int x = x0; x <= x1; x++) {
          float px = x + 0.5f, py = y + 0.5f;
          float w0 = ((p[1].x - px) * (p[2].y - py) - (p[2].x - px) * (p[1].y - py)) / area;
          float w1 = ((p[2].x - px) * (p[0].y - py) - (p[0].x - px) * (p[2].y - py)) / area;
          float w2 = 1 - w0 - w1;
          if(w0 < 0 || w1 < 0 || w2 < 0) continue;
          if(door && kBayer[((h - 1 - y) & 3) * 4 + (x & 3)] < door) continue;  // (rows from the bottom, as GL's)
          float z = w0 * iw[0] + w1 * iw[1] + w2 * iw[2];
          if(world && z < zb[y * w + x]) continue;  // behind what is there
          // Perspective-correct weights.
          float q0 = w0 * iw[0] / z, q1 = w1 * iw[1] / z, q2 = w2 * iw[2] / z;
          float cr = q0 * (p[0].argb >> 16 & 255) + q1 * (p[1].argb >> 16 & 255) + q2 * (p[2].argb >> 16 & 255);
          float cg = q0 * (p[0].argb >> 8 & 255) + q1 * (p[1].argb >> 8 & 255) + q2 * (p[2].argb >> 8 & 255);
          float cb = q0 * (p[0].argb & 255) + q1 * (p[1].argb & 255) + q2 * (p[2].argb & 255);
          float ca = q0 * (p[0].argb >> 24) + q1 * (p[1].argb >> 24) + q2 * (p[2].argb >> 24);
          bool cut = false;
          float spark = 0;
          if(pic) {
            float u = q0 * p[0].u + q1 * p[1].u + q2 * p[2].u, vv = q0 * p[0].v + q1 * p[1].v + q2 * p[2].v;
            float t[4];
            if(mip) {
              float dux = (Ux - u * Zx) / z * pic->w, duy = (Uy - u * Zy) / z * pic->w;
              float dvx = (Vx - vv * Zx) / z * pic->h, dvy = (Vy - vv * Zy) / z * pic->h;
              float rho2 = fmaxf(dux * dux + dvx * dvx, duy * duy + dvy * dvy);
              sample_mip(pic, u, vv, rho2 > 1e-12f ? 0.5f * log2f(rho2) : 0, t);
              if(pic->mask && mat == TG_V3_MAT_PAINT) spark = paint_spark(u, vv, sqrtf(rho2) * 1024 / pic->w) * t[0] / 255;
            } else {
              int tx = (int) (u * pic->w), ty = (int) (vv * pic->h);
              tx = tx < 0 ? 0 : tx >= pic->w ? pic->w - 1 : tx;
              ty = ty < 0 ? 0 : ty >= pic->h ? pic->h - 1 : ty;
              uint32_t c = pic->px[ty * pic->w + tx];
              t[0] = (float) (c >> 24), t[1] = (float) (c >> 16 & 255), t[2] = (float) (c >> 8 & 255), t[3] = (float) (c & 255);
            }
            if(pic->mask) {  // the colour is the picture's, tinted by the vertex's where its alpha says
              float a = t[0] / 255;
              cr = t[1] * (1 - a + a * cr / 255);
              cg = t[2] * (1 - a + a * cg / 255);
              cb = t[3] * (1 - a + a * cb / 255);
            } else {
              if(t[0] < 0.5f) continue;
              cut = t[0] < 254.5f;
              cr = cr * t[1] / 255;
              cg = cg * t[2] / 255;
              cb = cb * t[3] / 255;
              ca = ca * t[0] / 255;
            }
          }
          if(mat) {
            V3 N = vnorm(vadd(vadd(vmul(mN[0], q0), vmul(mN[1], q1)), vmul(mN[2], q2)));
            V3 E = vnorm(vadd(vadd(vmul(mE[0], q0), vmul(mE[1], q1)), vmul(mE[2], q2)));
            float c3[3] = {cr / 255, cg / 255, cb / 255}, fog = q0 * mFog[0] + q1 * mFog[1] + q2 * mFog[2];
            if(mat == TG_V3_MAT_GLASS && ca < 254.5f) {  // see-through glass: more opaque as it turns away (Fresnel)
              float ne = fabsf(vdot(N, E)), fr = 1 - ne, fr2 = fr * fr;
              ca += (255 - ca) * (0.1f + 0.9f * fr2 * fr2 * fr);
            }
            shade_mat(c3, mat, q0 * mAo[0] + q1 * mAo[1] + q2 * mAo[2], mNight, N, E, spark);
            cr = (c3[0] + ((l->haze >> 16 & 255) / 255.0f - c3[0]) * fog) * 255;
            cg = (c3[1] + ((l->haze >> 8 & 255) / 255.0f - c3[1]) * fog) * 255;
            cb = (c3[2] + ((l->haze & 255) / 255.0f - c3[2]) * fog) * 255;
          }
          if(ca < 254.5f) {  // see-through: over what is already there
            uint32_t d = out[y * w + x];
            float a = ca / 255;
            cr = cr * a + (d >> 16 & 255) * (1 - a);
            cg = cg * a + (d >> 8 & 255) * (1 - a);
            cb = cb * a + (d & 255) * (1 - a);
          }
          if(world && opaqueVerts && !cut) zb[y * w + x] = z;  // see-through things don't hide others
          out[y * w + x] = 0xFF000000u | (uint32_t) cr << 16 | (uint32_t) cg << 8 | (uint32_t) cb;
        }
    }
  }
  free(zb);
}

// --- The HUD's values -------------------------------------------------------------------

// A player's HUD values (0: player 1, 1: player 2; player 2's words follow player 1's).
static void hud_of(const uint8_t* m, int pl, TgV3Hud* hud) {
  int o = pl ? 2 : 0;
  bool kph = m[0x1FB0 + pl] != 0 && !(m[0x1FF6] & 0x80);
  uint16_t speed = r16(m, 0x1E5A + o);
  int factor = kph ? 0x18 : 0x0F;
  hud->speed = (speed & 0x8000) ? 0 : (int) ((uint16_t) (((int32_t) (int16_t) speed * factor) >> 8) >> 2);
  hud->kph = kph;
  uint16_t pos = r16(m, 0x1F00 + o);
  if(pos & 0x8000) pos = r16(m, 0x1E7A + o);
  hud->position = pos + 1;
  int laps = r16(m, 0x1E56);
  int lap = (int16_t) r16(m, 0x1E76 + o);
  hud->laps = laps;
  hud->lap = lap < 0 ? 1 : lap + 1 > laps ? laps : lap + 1;
  uint16_t t = r16(m, 0x1F26 + o);
  if(t & 0x8000) t = r16(m, 0x1EAE);
  hud->minutes = t / 3600;
  hud->seconds = t / 60 % 60;
  hud->hundredths = t % 60 * 100 / 60;
  hud->gear = r16(m, 0x1E72 + o) + 1;
  uint16_t n = r16(m, 0x1E6C + o);
  int d = n & 3;
  if((n & 0x8000) && d != 0) d--;
  hud->nitros = d;
  int full = (r16(m, 0x1E62 + o) >> 8) >> 3;
  hud->fuel = (full > 20 ? 20 : full) * 5;
}

void tg_view3d_hud(const uint8_t* m, TgV3Hud* hud) { hud_of(m, 0, hud); }
