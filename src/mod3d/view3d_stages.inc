// Stages with soul: each course's own things along the road (included by view3d.c).
//
// The original's roadside is one picture per segment; in 3D that leaves the land empty.
// Here every course gets a kit (docs/STYLE.md, docs/retro/CITIES.md): its vegetation, its
// houses and towers, its street furniture, in three depths beside the road (near: street
// furniture and the country's trees; middle: houses, trees, parked vehicles; far: blocks,
// towers, forest), denser in its towns and sparse in open country, a stadium at the
// start/finish, distance boards before the big corners, an overpass on city courses.
//
// Everything is placed once per course (a hash of the segment, the same every lap), on a
// grid of where the road runs so nothing stands on the road (also where the loop passes
// near itself), nor on the game's own roadside objects or on another prop. Props are built
// from boxes, prisms and frustums in plain colours (one batch with the rest of the world),
// culled at the screen's sides, and fade out over the last 60 m of their draw distance.

enum {
  K_NONE,
  // vegetation
  K_PALM, K_TALL_PALM, K_CYPRESS, K_POPLAR, K_SPRUCE, K_BROADLEAF, K_CHERRY, K_MAPLE, K_UMBRELLA, K_BIRCH,
  K_OLIVE, K_PLANE, K_KAPOK, K_FERN, K_JOSHUA, K_SAGUARO, K_SCRUB, K_HEDGE, K_VINES, K_HEATHER, K_MESA,
  K_BARROW,
  // buildings
  K_HOUSE, K_FLAT, K_BLOCK, K_TOWER, K_TIMBER, K_REDHOUSE, K_FARM, K_FAVELA, K_VICTORIAN, K_MACHIYA,
  K_TERRACE, K_HAUSSMANN, K_CASINO, K_FACTORY, K_COOLING, K_VILLA, K_HUT, K_WAREHOUSE, K_PAGODA, K_STEPPYR,
  // street furniture and small things
  K_LAMP, K_NEON, K_BILLBOARD, K_PHONEBOX, K_BUS, K_CAB, K_CABLECAR, K_TORII, K_LANTERN, K_NOBORI, K_MOOSE,
  K_AUTOBAHN, K_LOGS, K_MAYPOLE, K_MORRIS, K_LLAMA, K_STONES, K_WALL, K_COLUMN, K_VENDING, K_FLAG,
  // set pieces
  K_GRANDSTAND, K_PITS, K_DISTBOARD, K_OVERPASS, K_WELCOME,
  K_COUNT
};

// Footprint (metres): half length along the road, half depth across it. Draw distance class.
static const struct { float ha, hn; uint8_t cls; } kFoot[K_COUNT] = {
  [K_PALM] = {1.6f, 1.6f, 1}, [K_TALL_PALM] = {1.2f, 1.2f, 1}, [K_CYPRESS] = {1.0f, 1.0f, 1}, [K_POPLAR] = {1.2f, 1.2f, 1},
  [K_SPRUCE] = {2.0f, 2.0f, 1}, [K_BROADLEAF] = {2.6f, 2.6f, 1}, [K_CHERRY] = {2.2f, 2.2f, 1}, [K_MAPLE] = {2.4f, 2.4f, 1},
  [K_UMBRELLA] = {3.2f, 3.2f, 1}, [K_BIRCH] = {1.4f, 1.4f, 1}, [K_OLIVE] = {2.0f, 2.0f, 1}, [K_PLANE] = {3.0f, 3.0f, 1},
  [K_KAPOK] = {6.0f, 6.0f, 2}, [K_FERN] = {1.2f, 1.2f, 0}, [K_JOSHUA] = {1.6f, 1.6f, 1}, [K_SAGUARO] = {1.2f, 1.2f, 1},
  [K_SCRUB] = {0.9f, 0.9f, 0}, [K_HEDGE] = {4.5f, 0.7f, 0}, [K_VINES] = {5.0f, 5.5f, 1}, [K_HEATHER] = {1.4f, 1.4f, 0},
  [K_MESA] = {20, 20, 2}, [K_BARROW] = {8, 8, 1},
  [K_HOUSE] = {5, 4, 1}, [K_FLAT] = {6, 5, 1}, [K_BLOCK] = {8, 7, 2}, [K_TOWER] = {9, 9, 2}, [K_TIMBER] = {5, 4.5f, 1},
  [K_REDHOUSE] = {4.5f, 3.5f, 1}, [K_FARM] = {8, 6.5f, 1}, [K_FAVELA] = {6, 6, 1}, [K_VICTORIAN] = {3.2f, 5, 1},
  [K_MACHIYA] = {4.5f, 4, 1}, [K_TERRACE] = {8, 4.5f, 1}, [K_HAUSSMANN] = {9, 7, 2}, [K_CASINO] = {14, 10, 2},
  [K_FACTORY] = {12, 8, 2}, [K_COOLING] = {13, 13, 2}, [K_VILLA] = {5.5f, 5, 1}, [K_HUT] = {3, 2.5f, 1},
  [K_WAREHOUSE] = {12, 6, 2}, [K_PAGODA] = {4, 4, 2}, [K_STEPPYR] = {9, 9, 2},
  [K_LAMP] = {0.4f, 0.4f, 0}, [K_NEON] = {2.0f, 0.6f, 0}, [K_BILLBOARD] = {3.4f, 0.8f, 1}, [K_PHONEBOX] = {0.6f, 0.6f, 0},
  [K_BUS] = {5.4f, 1.4f, 0}, [K_CAB] = {2.4f, 1.0f, 0}, [K_CABLECAR] = {4.4f, 1.4f, 0}, [K_TORII] = {3.0f, 0.8f, 1},
  [K_LANTERN] = {0.6f, 0.6f, 0}, [K_NOBORI] = {0.5f, 0.5f, 0}, [K_MOOSE] = {0.8f, 0.4f, 0}, [K_AUTOBAHN] = {2.2f, 0.4f, 0},
  [K_LOGS] = {2.4f, 1.2f, 0}, [K_MAYPOLE] = {1.0f, 1.0f, 1}, [K_MORRIS] = {0.8f, 0.8f, 0}, [K_LLAMA] = {1.0f, 0.5f, 0},
  [K_STONES] = {5, 3, 1}, [K_WALL] = {4.5f, 0.5f, 0}, [K_COLUMN] = {0.8f, 0.8f, 1}, [K_VENDING] = {0.6f, 0.5f, 0},
  [K_FLAG] = {0.6f, 0.6f, 0}, [K_GRANDSTAND] = {10, 5, 2}, [K_PITS] = {10, 4, 2}, [K_DISTBOARD] = {0.9f, 0.3f, 0},
  [K_OVERPASS] = {5, 5, 2}, [K_WELCOME] = {3.2f, 0.8f, 1},
};
static const float kClassFar[3] = {150, 250, 400};  // draw distance by class (m)

#define KL 10  // kinds per list (zero-terminated)
typedef struct {
  uint8_t town[3][KL], open[3][KL];  // near, middle, far
  float urban;                       // share of the lap that is town
  float dt[3], dopen[3];             // chance per slot: town, open country
  uint8_t row;                       // a row of this along the road in open country (OutRun's rhythm), or 0
  float rowShare;
  uint32_t walls[4], roofs[4];       // the place's colours
  uint32_t veg;                      // its broadleaf green
  bool snow, overpass, glass;  // glass: its towers are blue glass (else its walls' colours)
  uint32_t flag[3];                  // its flag colours (start/finish)
} Kit;

#define LIST(...) {__VA_ARGS__}
static Kit g_kit;
static int g_pcourse;  // the course the props were placed for (defined below)

static void kit_for(int course, Kit* k) {
  memset(k, 0, sizeof(*k));
  k->urban = 0.4f;
  float dt[3] = {0.35f, 0.55f, 0.45f}, dop[3] = {0.12f, 0.22f, 0.2f};
  memcpy(k->dt, dt, sizeof(dt));
  memcpy(k->dopen, dop, sizeof(dop));
  k->veg = 0xFF4C8A3E;
  k->rowShare = 0.35f;
#define SET(arr, ...) do { static const uint8_t t_[KL] = {__VA_ARGS__}; memcpy(arr, t_, KL); } while(0)
#define PAL(w0, w1, w2, w3, r0, r1, r2, r3) do { uint32_t w_[4] = {w0, w1, w2, w3}, r_[4] = {r0, r1, r2, r3}; memcpy(k->walls, w_, 16); memcpy(k->roofs, r_, 16); } while(0)
#define FLAG(a, b, c) do { k->flag[0] = a, k->flag[1] = b, k->flag[2] = c; } while(0)
  int country = (course >> 2) & 7;
  switch(country) {
    case 0: FLAG(0xFFB22234, 0xFFFFFFFF, 0xFF3C3B6E); break;
    case 1: FLAG(0xFF009C3B, 0xFFFFDF00, 0xFF002776); break;
    case 2: FLAG(0xFFFFFFFF, 0xFFBC002D, 0xFFFFFFFF); break;
    case 3: FLAG(0xFF101010, 0xFFDD0000, 0xFFFFCE00); break;
    case 4: FLAG(0xFF006AA7, 0xFFFECC00, 0xFF006AA7); break;
    case 5: FLAG(0xFF0055A4, 0xFFFFFFFF, 0xFFEF4135); break;
    case 6: FLAG(0xFF009246, 0xFFFFFFFF, 0xFFCE2B37); break;
    default: FLAG(0xFF012169, 0xFFFFFFFF, 0xFFC8102E); break;
  }
  switch(course & 31) {
    case 0:  // Las Vegas: the Strip in town, the Mojave outside
      SET(k->town[0], K_NEON, K_TALL_PALM, K_LAMP, K_NEON, K_BILLBOARD);
      SET(k->town[1], K_CASINO, K_TALL_PALM, K_NEON, K_FLAT, K_TALL_PALM);
      SET(k->town[2], K_CASINO, K_TOWER, K_CASINO);
      SET(k->open[0], K_SCRUB, K_SCRUB, K_JOSHUA);
      SET(k->open[1], K_JOSHUA, K_SAGUARO, K_SCRUB, K_BILLBOARD, K_SCRUB);
      SET(k->open[2], K_MESA, K_SCRUB, K_JOSHUA);
      k->urban = 0.35f, k->row = K_TALL_PALM, k->rowShare = 0.0f, k->glass = true;
      PAL(0xFFF2EEE4, 0xFFE8C878, 0xFFF0B8C8, 0xFFDCD6CC, 0xFFC89A40, 0xFFE8E0D0, 0xFF8A6A3A, 0xFFB0A898);
      k->veg = 0xFF6E8A48;
      break;
    case 1:  // Los Angeles: palms, stucco, billboards, downtown towers, the hills
      SET(k->town[0], K_TALL_PALM, K_LAMP, K_TALL_PALM, K_BILLBOARD);
      SET(k->town[1], K_FLAT, K_TALL_PALM, K_FLAT, K_BILLBOARD, K_HOUSE);
      SET(k->town[2], K_TOWER, K_BLOCK, K_TOWER);
      SET(k->open[0], K_SCRUB, K_TALL_PALM);
      SET(k->open[1], K_BROADLEAF, K_SCRUB, K_FLAT, K_TALL_PALM);
      SET(k->open[2], K_BROADLEAF, K_FLAT);
      k->urban = 0.5f, k->row = K_TALL_PALM, k->rowShare = 0.5f, k->overpass = true, k->glass = true;
      PAL(0xFFF0E2C8, 0xFFE8C8A8, 0xFFF4F0E8, 0xFFD8E2E8, 0xFFB85A38, 0xFFC8743C, 0xFF9A9A98, 0xFFB86A48);
      k->veg = 0xFF5E7A3A;
      break;
    case 2:  // New York (night): walk-ups, towers, cabs, street lamps
      SET(k->town[0], K_LAMP, K_CAB, K_LAMP, K_PLANE);
      SET(k->town[1], K_BLOCK, K_BLOCK, K_CAB, K_TERRACE);
      SET(k->town[2], K_TOWER, K_TOWER, K_BLOCK);
      SET(k->open[0], K_LAMP, K_PLANE);
      SET(k->open[1], K_PLANE, K_BROADLEAF, K_BLOCK);
      SET(k->open[2], K_BLOCK, K_TOWER);
      k->urban = 0.8f, k->overpass = true, k->glass = true;
      float d2[3] = {0.5f, 0.75f, 0.7f};
      memcpy(k->dt, d2, sizeof(d2));
      PAL(0xFF8A4A3A, 0xFF9A6A4E, 0xFF6E6A68, 0xFFB0A898, 0xFF3A3A40, 0xFF4A4A50, 0xFF2E2E34, 0xFF505058);
      break;
    case 3:  // San Francisco: painted ladies, cable cars, cypress
      SET(k->town[0], K_LAMP, K_CABLECAR, K_CYPRESS, K_LAMP);
      SET(k->town[1], K_VICTORIAN, K_VICTORIAN, K_CYPRESS, K_VICTORIAN);
      SET(k->town[2], K_BLOCK, K_TOWER, K_BLOCK);
      SET(k->open[0], K_CYPRESS, K_SCRUB);
      SET(k->open[1], K_CYPRESS, K_BROADLEAF, K_VICTORIAN);
      SET(k->open[2], K_BROADLEAF, K_CYPRESS);
      k->urban = 0.6f, k->row = K_CYPRESS;
      PAL(0xFFF2C8D2, 0xFFB8D8E8, 0xFFF4E2A8, 0xFFC8E0C0, 0xFF6A6E78, 0xFF8A5A60, 0xFF5A6A7A, 0xFF7A6458);
      break;
    case 4:  // Rio: palms, favela hillsides, white towers by the sea
      SET(k->town[0], K_PALM, K_LAMP, K_PALM);
      SET(k->town[1], K_FAVELA, K_PALM, K_FAVELA, K_FLAT);
      SET(k->town[2], K_FAVELA, K_BLOCK, K_FAVELA);
      SET(k->open[0], K_PALM, K_FERN);
      SET(k->open[1], K_PALM, K_BROADLEAF, K_FERN);
      SET(k->open[2], K_BROADLEAF, K_KAPOK);
      k->urban = 0.5f, k->row = K_PALM;
      PAL(0xFFE8A040, 0xFFE87898, 0xFF5AA0D8, 0xFFF4F0E4, 0xFFB85A38, 0xFF8A8A88, 0xFF6A7A88, 0xFFC0704A);
      k->veg = 0xFF2E8A3A;
      break;
    case 5:  // Machu Picchu: stone huts and walls, llamas, cloud forest
      SET(k->town[0], K_WALL, K_LLAMA, K_WALL);
      SET(k->town[1], K_HUT, K_HUT, K_WALL, K_LLAMA);
      SET(k->town[2], K_HUT, K_BROADLEAF);
      SET(k->open[0], K_FERN, K_LLAMA, K_SCRUB);
      SET(k->open[1], K_BROADLEAF, K_FERN, K_HUT);
      SET(k->open[2], K_BROADLEAF, K_SPRUCE);
      k->urban = 0.3f;
      PAL(0xFF8E8A80, 0xFF9A948A, 0xFF7E7A72, 0xFFA8A296, 0xFFB49A5A, 0xFFA08848, 0xFFB8A060, 0xFF9A8448);
      k->veg = 0xFF3E7A3A;
      break;
    case 6:  // Chichen Itza: scrub jungle, small stepped pyramids, columns
      SET(k->town[0], K_COLUMN, K_SCRUB, K_COLUMN);
      SET(k->town[1], K_STEPPYR, K_COLUMN, K_BROADLEAF);
      SET(k->town[2], K_STEPPYR, K_BROADLEAF);
      SET(k->open[0], K_SCRUB, K_FERN, K_SCRUB);
      SET(k->open[1], K_BROADLEAF, K_PALM, K_SCRUB);
      SET(k->open[2], K_BROADLEAF, K_BROADLEAF, K_PALM);
      k->urban = 0.25f;
      float d6[3] = {0.2f, 0.35f, 0.35f};
      memcpy(k->dopen, d6, sizeof(d6));
      PAL(0xFFD8D0B8, 0xFFC8BEA4, 0xFFE0D8C4, 0xFFBEB49C, 0xFFB0A68E, 0xFFA09A88, 0xFFC0B498, 0xFF9A9080);
      k->veg = 0xFF4E7A34;
      break;
    case 7:  // Rain forest: a wall of forest
      SET(k->town[0], K_FERN, K_PALM, K_FERN);
      SET(k->town[1], K_KAPOK, K_PALM, K_BROADLEAF);
      SET(k->town[2], K_KAPOK, K_BROADLEAF);
      SET(k->open[0], K_FERN, K_PALM, K_FERN);
      SET(k->open[1], K_KAPOK, K_BROADLEAF, K_PALM, K_BROADLEAF);
      SET(k->open[2], K_KAPOK, K_BROADLEAF, K_KAPOK);
      k->urban = 0.0f, k->row = K_PALM;
      float d7[3] = {0.45f, 0.7f, 0.8f};
      memcpy(k->dopen, d7, sizeof(d7));
      PAL(0xFFB07A4A, 0xFFA06A3A, 0xFFB07A4A, 0xFFA06A3A, 0xFF8A6A3A, 0xFF8A6A3A, 0xFF8A6A3A, 0xFF8A6A3A);
      k->veg = 0xFF1E7A34;
      break;
    case 8:  // Tokyo: neon, vending machines, banners, grey towers, cherry trees
      SET(k->town[0], K_NEON, K_VENDING, K_NOBORI, K_LAMP, K_NEON);
      SET(k->town[1], K_BLOCK, K_NEON, K_BLOCK, K_CHERRY, K_MACHIYA);
      SET(k->town[2], K_TOWER, K_BLOCK, K_TOWER);
      SET(k->open[0], K_NOBORI, K_LAMP);
      SET(k->open[1], K_CHERRY, K_BROADLEAF, K_MACHIYA);
      SET(k->open[2], K_BLOCK, K_BROADLEAF);
      k->urban = 0.8f, k->overpass = true, k->glass = true;
      float d8[3] = {0.45f, 0.7f, 0.7f};
      memcpy(k->dt, d8, sizeof(d8));
      PAL(0xFFD8D8D4, 0xFFB8BCC4, 0xFFE8E2D8, 0xFF9AA0A8, 0xFF4A4E58, 0xFF5A5E68, 0xFF3A3E48, 0xFF686C74);
      break;
    case 9:  // Hiroshima: machiya, pines, torii, lanterns, the river town
      SET(k->town[0], K_LANTERN, K_LAMP, K_TORII);
      SET(k->town[1], K_MACHIYA, K_BLOCK, K_UMBRELLA, K_MACHIYA);
      SET(k->town[2], K_BLOCK, K_TOWER);
      SET(k->open[0], K_LANTERN, K_SCRUB);
      SET(k->open[1], K_UMBRELLA, K_CHERRY, K_MACHIYA);
      SET(k->open[2], K_SPRUCE, K_BROADLEAF);
      k->urban = 0.5f;
      PAL(0xFFE8E2D4, 0xFF4A3A2E, 0xFFD0C8B8, 0xFF5A4636, 0xFF3E424C, 0xFF4A4E58, 0xFF34383F, 0xFF56545A);
      break;
    case 10:  // Yokohama: harbour warehouses, towers, neon, cherry
      SET(k->town[0], K_LAMP, K_NEON, K_VENDING);
      SET(k->town[1], K_WAREHOUSE, K_BLOCK, K_CHERRY);
      SET(k->town[2], K_TOWER, K_TOWER, K_BLOCK);
      SET(k->open[0], K_LAMP, K_SCRUB);
      SET(k->open[1], K_CHERRY, K_BROADLEAF);
      SET(k->open[2], K_WAREHOUSE, K_BLOCK);
      k->urban = 0.65f, k->overpass = true, k->glass = true;
      PAL(0xFFA8503A, 0xFFE8E4DC, 0xFFB8C4D0, 0xFF9A4A36, 0xFF505058, 0xFF5E626A, 0xFF46464E, 0xFF6A6A70);
      break;
    case 11:  // Kyoto: torii, stone lanterns, machiya, pagodas, maples, wooded hills
      SET(k->town[0], K_LANTERN, K_TORII, K_LANTERN, K_MAPLE);
      SET(k->town[1], K_MACHIYA, K_MAPLE, K_PAGODA, K_CHERRY, K_MACHIYA);
      SET(k->town[2], K_PAGODA, K_SPRUCE, K_BROADLEAF);
      SET(k->open[0], K_LANTERN, K_TORII);
      SET(k->open[1], K_MAPLE, K_CHERRY, K_SPRUCE);
      SET(k->open[2], K_SPRUCE, K_BROADLEAF, K_SPRUCE);
      k->urban = 0.45f, k->row = K_TORII, k->rowShare = 0.2f;
      float d11[3] = {0.18f, 0.4f, 0.55f};
      memcpy(k->dopen, d11, sizeof(d11));
      PAL(0xFF4A3A2E, 0xFFE8E0CC, 0xFF5A4636, 0xFFD8CEB8, 0xFF3A3E46, 0xFF464A52, 0xFF34383E, 0xFF4E5058);
      k->veg = 0xFF3A6A34;
      break;
    case 12:  // Munich: Bavarian houses, maypoles, chestnuts, autobahn signs
      SET(k->town[0], K_LAMP, K_AUTOBAHN, K_MAYPOLE, K_LAMP);
      SET(k->town[1], K_HOUSE, K_BROADLEAF, K_HOUSE, K_TIMBER);
      SET(k->town[2], K_BLOCK, K_HOUSE, K_SPRUCE);
      SET(k->open[0], K_AUTOBAHN, K_SCRUB);
      SET(k->open[1], K_SPRUCE, K_BROADLEAF, K_FARM);
      SET(k->open[2], K_SPRUCE, K_SPRUCE, K_BROADLEAF);
      k->urban = 0.5f, k->row = K_SPRUCE;
      PAL(0xFFF4EEDC, 0xFFEADCB8, 0xFFF8F4EA, 0xFFE4D2A8, 0xFFB04A32, 0xFF9A4232, 0xFF8A5A3E, 0xFFB86244);
      k->veg = 0xFF3E7A3A;
      break;
    case 13:  // Cologne: timber-framed houses, gabled town, autobahn
      SET(k->town[0], K_LAMP, K_AUTOBAHN, K_LAMP);
      SET(k->town[1], K_TIMBER, K_HOUSE, K_TIMBER, K_BROADLEAF);
      SET(k->town[2], K_BLOCK, K_TIMBER, K_BLOCK);
      SET(k->open[0], K_AUTOBAHN, K_SCRUB);
      SET(k->open[1], K_BROADLEAF, K_SPRUCE, K_TIMBER);
      SET(k->open[2], K_SPRUCE, K_BROADLEAF);
      k->urban = 0.6f;
      PAL(0xFFF0E8D8, 0xFFD8C0A0, 0xFFE8DCC8, 0xFFC8A880, 0xFF7A3A2E, 0xFF5A3A30, 0xFF4A4A50, 0xFF8A4632);
      break;
    case 14:  // Black Forest: dark spruce, big farmhouses, log piles
      SET(k->town[0], K_LOGS, K_SPRUCE, K_LOGS);
      SET(k->town[1], K_FARM, K_SPRUCE, K_FARM);
      SET(k->town[2], K_SPRUCE, K_SPRUCE, K_FARM);
      SET(k->open[0], K_SPRUCE, K_LOGS, K_SPRUCE);
      SET(k->open[1], K_SPRUCE, K_SPRUCE, K_FARM);
      SET(k->open[2], K_SPRUCE, K_SPRUCE, K_SPRUCE);
      k->urban = 0.2f, k->row = K_SPRUCE, k->rowShare = 0.6f;
      float d14[3] = {0.3f, 0.65f, 0.85f};
      memcpy(k->dopen, d14, sizeof(d14));
      PAL(0xFF8A6A4A, 0xFFE8E0D0, 0xFF7A5A3E, 0xFFDCD2BE, 0xFF5A4636, 0xFF4E3E30, 0xFF6A5040, 0xFF46382C);
      k->veg = 0xFF2E5A34;
      break;
    case 15:  // Frankfurt: glass towers, blocks, the Römer gables
      SET(k->town[0], K_LAMP, K_AUTOBAHN, K_LAMP, K_PLANE);
      SET(k->town[1], K_BLOCK, K_TIMBER, K_BLOCK, K_PLANE);
      SET(k->town[2], K_TOWER, K_TOWER, K_BLOCK);
      SET(k->open[0], K_AUTOBAHN, K_SCRUB);
      SET(k->open[1], K_BROADLEAF, K_HOUSE);
      SET(k->open[2], K_TOWER, K_BROADLEAF);
      k->urban = 0.7f, k->overpass = true, k->glass = true;
      PAL(0xFFE4DCCC, 0xFFC8CCD0, 0xFFF0E8D8, 0xFFB8A890, 0xFF8A4632, 0xFF5A5A60, 0xFF7A3A2E, 0xFF4A4A52);
      break;
    case 16:  // Stockholm: red wooden houses, ochre town, birch and spruce, moose signs
      SET(k->town[0], K_LAMP, K_MOOSE, K_LAMP, K_BIRCH);
      SET(k->town[1], K_REDHOUSE, K_TERRACE, K_BIRCH, K_REDHOUSE);
      SET(k->town[2], K_BLOCK, K_SPRUCE, K_BLOCK);
      SET(k->open[0], K_MOOSE, K_SCRUB, K_BIRCH);
      SET(k->open[1], K_SPRUCE, K_BIRCH, K_REDHOUSE, K_SPRUCE);
      SET(k->open[2], K_SPRUCE, K_SPRUCE, K_BIRCH);
      k->urban = 0.5f, k->row = K_SPRUCE, k->snow = true;
      PAL(0xFFE0A040, 0xFFD87038, 0xFFB83A2A, 0xFFF0D898, 0xFF3A3C42, 0xFF5A3E34, 0xFF2E5A50, 0xFF46484E);
      k->veg = 0xFF4A8048;
      break;
    case 17:  // Copenhagen: Nyhavn's coloured gables, bikes, birch
      SET(k->town[0], K_LAMP, K_LAMP, K_BIRCH);
      SET(k->town[1], K_TERRACE, K_TERRACE, K_BROADLEAF, K_REDHOUSE);
      SET(k->town[2], K_BLOCK, K_TERRACE);
      SET(k->open[0], K_MOOSE, K_SCRUB);
      SET(k->open[1], K_BIRCH, K_BROADLEAF, K_REDHOUSE);
      SET(k->open[2], K_BROADLEAF, K_SPRUCE);
      k->urban = 0.6f, k->snow = true;
      PAL(0xFFF2C840, 0xFFD84A32, 0xFF4A7AB8, 0xFFE8883A, 0xFF8A3A2E, 0xFF3A3C42, 0xFF8A3A2E, 0xFF46484E);
      k->veg = 0xFF4A8048;
      break;
    case 18:  // Helsinki: red houses, birch and pine, granite, moose
      SET(k->town[0], K_LAMP, K_MOOSE, K_BIRCH);
      SET(k->town[1], K_REDHOUSE, K_BLOCK, K_BIRCH);
      SET(k->town[2], K_BLOCK, K_SPRUCE);
      SET(k->open[0], K_MOOSE, K_BIRCH, K_SCRUB);
      SET(k->open[1], K_SPRUCE, K_BIRCH, K_REDHOUSE);
      SET(k->open[2], K_SPRUCE, K_SPRUCE, K_BIRCH);
      k->urban = 0.4f, k->row = K_BIRCH, k->snow = true;
      PAL(0xFFB83A2A, 0xFFF0E8D0, 0xFFE8C878, 0xFFB0A898, 0xFF3A3C42, 0xFF2E5A50, 0xFF46484E, 0xFF5A3E34);
      k->veg = 0xFF4A8048;
      break;
    case 19:  // Oslo: fjord slopes of spruce with snow, red houses
      SET(k->town[0], K_MOOSE, K_LAMP, K_SPRUCE);
      SET(k->town[1], K_REDHOUSE, K_SPRUCE, K_REDHOUSE);
      SET(k->town[2], K_SPRUCE, K_BLOCK);
      SET(k->open[0], K_SPRUCE, K_MOOSE, K_SCRUB);
      SET(k->open[1], K_SPRUCE, K_SPRUCE, K_REDHOUSE);
      SET(k->open[2], K_SPRUCE, K_SPRUCE);
      k->urban = 0.35f, k->row = K_SPRUCE, k->rowShare = 0.5f, k->snow = true;
      float d19[3] = {0.25f, 0.5f, 0.7f};
      memcpy(k->dopen, d19, sizeof(d19));
      PAL(0xFFB83A2A, 0xFFE8C878, 0xFFF0E8D0, 0xFF6A8AA8, 0xFF3A3C42, 0xFF46484E, 0xFF5A3E34, 0xFF3A3C42);
      k->veg = 0xFF3E7044;
      break;
    case 20:  // Paris: Haussmann blocks, plane trees, Morris columns, poplars outside
      SET(k->town[0], K_LAMP, K_PLANE, K_MORRIS, K_LAMP);
      SET(k->town[1], K_HAUSSMANN, K_PLANE, K_HAUSSMANN);
      SET(k->town[2], K_HAUSSMANN, K_HAUSSMANN, K_BLOCK);
      SET(k->open[0], K_SCRUB, K_HEDGE);
      SET(k->open[1], K_POPLAR, K_BROADLEAF, K_HOUSE);
      SET(k->open[2], K_POPLAR, K_BROADLEAF);
      k->urban = 0.65f, k->row = K_POPLAR;
      PAL(0xFFEDE2CA, 0xFFE4D8BC, 0xFFF2EADA, 0xFFDCCFB2, 0xFF6A7078, 0xFF5E646C, 0xFF747A82, 0xFF666C74);
      k->veg = 0xFF4E8040;
      break;
    case 21:  // Nice: palms, Belle Epoque villas, umbrella pines
      SET(k->town[0], K_PALM, K_LAMP, K_PALM);
      SET(k->town[1], K_VILLA, K_PALM, K_VILLA, K_FLAT);
      SET(k->town[2], K_VILLA, K_BLOCK, K_UMBRELLA);
      SET(k->open[0], K_PALM, K_SCRUB);
      SET(k->open[1], K_UMBRELLA, K_VILLA, K_CYPRESS);
      SET(k->open[2], K_UMBRELLA, K_VILLA);
      k->urban = 0.6f, k->row = K_PALM;
      PAL(0xFFF4E4D0, 0xFFF0C8B4, 0xFFF8F2E4, 0xFFF0D8A0, 0xFFC0603A, 0xFFB45A38, 0xFFC86E44, 0xFFA85234);
      k->veg = 0xFF4E7A3A;
      break;
    case 22:  // Bordeaux: vineyards, stone farmhouses, poplars, cypress
      SET(k->town[0], K_LAMP, K_PLANE, K_LAMP);
      SET(k->town[1], K_FARM, K_HOUSE, K_PLANE);
      SET(k->town[2], K_HAUSSMANN, K_HOUSE);
      SET(k->open[0], K_SCRUB, K_CYPRESS);
      SET(k->open[1], K_VINES, K_VINES, K_FARM, K_VINES);
      SET(k->open[2], K_VINES, K_POPLAR, K_VINES);
      k->urban = 0.3f, k->row = K_POPLAR;
      float d22[3] = {0.12f, 0.5f, 0.45f};
      memcpy(k->dopen, d22, sizeof(d22));
      PAL(0xFFE8DCC0, 0xFFDCCCA8, 0xFFF0E6D0, 0xFFD4C4A0, 0xFFB86244, 0xFF6A7078, 0xFFA85A3E, 0xFFC0704C);
      k->veg = 0xFF5A8A3A;
      break;
    case 23:  // Monaco: apartment blocks on the rock, villas, palms
      SET(k->town[0], K_PALM, K_LAMP, K_PALM);
      SET(k->town[1], K_VILLA, K_BLOCK, K_PALM);
      SET(k->town[2], K_BLOCK, K_BLOCK, K_VILLA);
      SET(k->open[0], K_PALM, K_SCRUB);
      SET(k->open[1], K_UMBRELLA, K_VILLA);
      SET(k->open[2], K_BLOCK, K_UMBRELLA);
      k->urban = 0.75f, k->row = K_PALM;
      PAL(0xFFF6EEE0, 0xFFF0D8C0, 0xFFF4E8B8, 0xFFE8D0C8, 0xFFC0603A, 0xFFB45A38, 0xFFC86E44, 0xFF9AA0A8);
      k->veg = 0xFF4E7A3A;
      break;
    case 24:  // Pisa: Tuscan houses, cypress rows, umbrella pines, olives
      SET(k->town[0], K_LAMP, K_CYPRESS);
      SET(k->town[1], K_VILLA, K_VILLA, K_CYPRESS);
      SET(k->town[2], K_VILLA, K_BLOCK);
      SET(k->open[0], K_CYPRESS, K_SCRUB);
      SET(k->open[1], K_OLIVE, K_UMBRELLA, K_VILLA, K_CYPRESS);
      SET(k->open[2], K_CYPRESS, K_UMBRELLA);
      k->urban = 0.4f, k->row = K_CYPRESS, k->rowShare = 0.5f;
      PAL(0xFFF0E2C4, 0xFFE8D0A8, 0xFFF4EAD8, 0xFFE0C49C, 0xFFC0603A, 0xFFB45A38, 0xFFC86E44, 0xFFA85234);
      k->veg = 0xFF5A7E3A;
      break;
    case 25:  // Rome: ochre blocks, umbrella pines, ruined columns
      SET(k->town[0], K_UMBRELLA, K_LAMP, K_COLUMN);
      SET(k->town[1], K_BLOCK, K_UMBRELLA, K_VILLA, K_COLUMN);
      SET(k->town[2], K_BLOCK, K_BLOCK);
      SET(k->open[0], K_COLUMN, K_SCRUB);
      SET(k->open[1], K_UMBRELLA, K_CYPRESS, K_VILLA);
      SET(k->open[2], K_UMBRELLA, K_CYPRESS);
      k->urban = 0.6f, k->row = K_UMBRELLA;
      PAL(0xFFE0A860, 0xFFD88C52, 0xFFE8C890, 0xFFC87850, 0xFFB45A38, 0xFFC0603A, 0xFFA85234, 0xFFC86E44);
      k->veg = 0xFF4E7A3A;
      break;
    case 26:  // Sicily: white villages, olives, stone walls, prickly pears
      SET(k->town[0], K_WALL, K_LAMP, K_OLIVE);
      SET(k->town[1], K_FLAT, K_VILLA, K_OLIVE);
      SET(k->town[2], K_FLAT, K_VILLA);
      SET(k->open[0], K_WALL, K_SCRUB, K_SAGUARO);
      SET(k->open[1], K_OLIVE, K_OLIVE, K_SCRUB, K_VILLA);
      SET(k->open[2], K_OLIVE, K_UMBRELLA);
      k->urban = 0.3f;
      PAL(0xFFF6F2E8, 0xFFF0E4C8, 0xFFE8D8B0, 0xFFF8F6EE, 0xFFC0603A, 0xFFB45A38, 0xFFC86E44, 0xFFE8E2D4);
      k->veg = 0xFF6E8A48;
      break;
    case 27:  // Florence (night start): terracotta town, cypress hills, lamps
      SET(k->town[0], K_LAMP, K_CYPRESS, K_LAMP);
      SET(k->town[1], K_VILLA, K_BLOCK, K_VILLA);
      SET(k->town[2], K_BLOCK, K_VILLA);
      SET(k->open[0], K_CYPRESS, K_LAMP);
      SET(k->open[1], K_CYPRESS, K_OLIVE, K_VILLA);
      SET(k->open[2], K_CYPRESS, K_UMBRELLA);
      k->urban = 0.5f, k->row = K_CYPRESS;
      PAL(0xFFE8D0A0, 0xFFF0E0C0, 0xFFD8B080, 0xFFF4EAD4, 0xFFC0603A, 0xFFB45A38, 0xFFC86E44, 0xFFA85234);
      k->veg = 0xFF4E7A3A;
      break;
    case 28:  // London: brick terraces, phone boxes, buses, plane trees, hedges
      SET(k->town[0], K_PHONEBOX, K_LAMP, K_BUS, K_CAB, K_LAMP);
      SET(k->town[1], K_TERRACE, K_PLANE, K_TERRACE, K_BLOCK);
      SET(k->town[2], K_BLOCK, K_BLOCK, K_TOWER);
      SET(k->open[0], K_HEDGE, K_HEDGE, K_PHONEBOX);
      SET(k->open[1], K_BROADLEAF, K_HEDGE, K_HOUSE);
      SET(k->open[2], K_BROADLEAF, K_BLOCK);
      k->urban = 0.7f;
      PAL(0xFF9A5440, 0xFFB06A4E, 0xFFE8E2D4, 0xFF8A4A3A, 0xFF4A4C54, 0xFF3E4048, 0xFF56585E, 0xFF4A4C54);
      k->veg = 0xFF467A3A;
      break;
    case 29:  // Sheffield: brick terraces, works with chimneys and cooling towers, moors
      SET(k->town[0], K_LAMP, K_PHONEBOX, K_WALL);
      SET(k->town[1], K_TERRACE, K_FACTORY, K_TERRACE);
      SET(k->town[2], K_FACTORY, K_COOLING, K_BLOCK);
      SET(k->open[0], K_WALL, K_HEATHER);
      SET(k->open[1], K_HEATHER, K_BROADLEAF, K_WALL);
      SET(k->open[2], K_COOLING, K_HEATHER);
      k->urban = 0.55f;
      PAL(0xFF8A4A38, 0xFF9A5440, 0xFF7A4234, 0xFFA8604A, 0xFF3E4048, 0xFF4A4C54, 0xFF46484E, 0xFF3A3C42);
      k->veg = 0xFF5A7A3A;
      break;
    case 30:  // Loch Ness: stone cottages, heather, Scots pine, dry stone walls
      SET(k->town[0], K_WALL, K_HEATHER);
      SET(k->town[1], K_HOUSE, K_SPRUCE, K_WALL);
      SET(k->town[2], K_SPRUCE, K_HOUSE);
      SET(k->open[0], K_HEATHER, K_WALL, K_HEATHER);
      SET(k->open[1], K_HEATHER, K_SPRUCE, K_WALL);
      SET(k->open[2], K_SPRUCE, K_SPRUCE, K_BROADLEAF);
      k->urban = 0.15f;
      float d30[3] = {0.35f, 0.55f, 0.6f};
      memcpy(k->dopen, d30, sizeof(d30));
      PAL(0xFFB8B4A8, 0xFFA8A498, 0xFFC8C2B4, 0xFF9A968A, 0xFF4A4C54, 0xFF3E4048, 0xFF56585E, 0xFF4A4C54);
      k->veg = 0xFF4A7238;
      break;
    default:  // 31 Stonehenge: open chalk grassland, hedgerows, barrows, standing stones
      SET(k->town[0], K_HEDGE, K_WALL);
      SET(k->town[1], K_STONES, K_BARROW, K_HOUSE);
      SET(k->town[2], K_BARROW, K_BROADLEAF);
      SET(k->open[0], K_HEDGE, K_HEDGE);
      SET(k->open[1], K_BARROW, K_BROADLEAF, K_STONES);
      SET(k->open[2], K_BROADLEAF, K_BARROW);
      k->urban = 0.1f;
      float d31[3] = {0.3f, 0.4f, 0.4f};
      memcpy(k->dopen, d31, sizeof(d31));
      PAL(0xFFE8E2D4, 0xFFD8D0C0, 0xFFE8E2D4, 0xFFD8D0C0, 0xFF6A5A3A, 0xFF7A6A44, 0xFF6A5A3A, 0xFF7A6A44);
      k->veg = 0xFF4A7A3A;
      break;
  }
#undef SET
#undef PAL
#undef FLAG
}

// --- Primitives: faces of one prop (one item), lit by the sun, hazed, darkened at night ---

static float g_pd;   // the prop's depth (its haze)
static V3 g_pc;      // the centre of the part being built (which side of a face is out)
static bool g_emit;  // boxes built now give their own light (lamp heads, lit fireboxes)
static const V3 kUp = {0, 1, 0};

static uint32_t pshade(uint32_t col, V3 n) {
  static V3 L;  // the sun's direction, worked out once
  if(L.y == 0) L = vnorm(kLight);
  float l = vdot(n, L);
  uint32_t c = shade(col, 0.78f + 0.3f * (l > 0 ? l : 0));  // as lit()
  if(g_night) c = mix(c, 0xFF0A0E16, 0.74f);
  return fogged(c, g_pd);
}

// A flat convex face; culled when it faces away. emissive: its own colour (lit windows).
static void pface(Gather* g, const V3* p, int n, uint32_t col, bool emissive) {
  V3 nn = v3(0, 0, 0), fc = v3(0, 0, 0);
  for(int i = 0; i < n; i++) {
    V3 a = p[i], b = p[(i + 1) % n];
    nn.x += (a.y - b.y) * (a.z + b.z);
    nn.y += (a.z - b.z) * (a.x + b.x);
    nn.z += (a.x - b.x) * (a.y + b.y);
    fc = vadd(fc, vmul(a, 1.0f / n));
  }
  nn = vnorm(nn);
  if(vdot(nn, vsub(fc, g_pc)) < 0) nn = vmul(nn, -1);
  if(vdot(nn, vsub(fc, g->cam->eye)) >= 0) return;
  CV q[8];
  for(int i = 0; i < n && i < 8; i++) q[i] = (CV) {to_cam(g->cam, p[i]), 0, 0, 0};
  uint32_t c = emissive ? (col & 0xFF000000u) | (fogged(col, g_pd * 0.5f) & 0xFFFFFF) : pshade(col, nn);
  emit_poly(g, q, n < 8 ? n : 8, c, -1);
}

// A face whose outward normal is known (boxes): no normal to work out.
static void pface_n(Gather* g, const V3* p, int n, uint32_t col, V3 nn) {
  if(vdot(nn, vsub(p[0], g->cam->eye)) >= 0) return;
  CV q[4];
  for(int i = 0; i < n; i++) q[i] = (CV) {to_cam(g->cam, p[i]), 0, 0, 0};
  emit_poly(g, q, n, g_emit ? (col & 0xFF000000u) | (fogged(col, g_pd * 0.5f) & 0xFFFFFF) : pshade(col, nn), -1);
}

// A two-sided triangle (fronds, flags): lit by whichever side is seen.
static void ptri2(Gather* g, V3 a, V3 b, V3 c, uint32_t col) {
  V3 n = vnorm(vcross(vsub(b, a), vsub(c, a)));
  V3 fc = vmul(vadd(vadd(a, b), c), 1.0f / 3);
  if(vdot(n, vsub(fc, g->cam->eye)) > 0) n = vmul(n, -1);
  CV q[3] = {{to_cam(g->cam, a), 0, 0, 0}, {to_cam(g->cam, b), 0, 0, 0}, {to_cam(g->cam, c), 0, 0, 0}};
  emit_poly(g, q, 3, pshade(col, n), -1);
}

static V3 at3(V3 b, V3 n, V3 a, float dn, float da, float y) { return vadd(b, vadd(vadd(vmul(n, dn), vmul(a, da)), vmul(kUp, y))); }

// A box on base b: hn half depth along n, ha half length along a, from height y0 to y1.
static void pbox(Gather* g, V3 b, V3 n, V3 a, float hn, float ha, float y0, float y1, uint32_t col) {
  V3 c[8];
  static const float sn[4] = {-1, -1, 1, 1}, sa[4] = {-1, 1, 1, -1};
  for(int k = 0; k < 4; k++) {
    c[k] = at3(b, n, a, sn[k] * hn, sa[k] * ha, y0);
    c[k + 4] = at3(b, n, a, sn[k] * hn, sa[k] * ha, y1);
  }
  V3 f[4];
  f[0] = c[4], f[1] = c[5], f[2] = c[6], f[3] = c[7];
  pface_n(g, f, 4, col, kUp);
  if(y0 > 0.05f) {
    f[0] = c[0], f[1] = c[1], f[2] = c[2], f[3] = c[3];
    pface_n(g, f, 4, col, vmul(kUp, -1));
  }
  V3 sideN[4] = {vmul(n, -1), a, n, vmul(a, -1)};  // side k: from corner k to k+1
  for(int k = 0; k < 4; k++) {
    int j = (k + 1) & 3;
    f[0] = c[k], f[1] = c[j], f[2] = c[j + 4], f[3] = c[k + 4];
    pface_n(g, f, 4, col, sideN[k]);
  }
}

// A gable roof over a box's top: ridge along a (along n if ridgeN) at height y1.
static void pgable(Gather* g, V3 b, V3 n, V3 a, float hn, float ha, float y0, float y1, bool ridgeN, uint32_t col, uint32_t gableCol) {
  if(ridgeN) {
    V3 t = n;
    n = a, a = t;
    float th = hn;
    hn = ha, ha = th;
  }
  V3 e0 = at3(b, n, a, -hn, -ha, y0), e1 = at3(b, n, a, -hn, ha, y0), e2 = at3(b, n, a, hn, ha, y0), e3 = at3(b, n, a, hn, -ha, y0);
  V3 r0 = at3(b, n, a, 0, -ha, y1), r1 = at3(b, n, a, 0, ha, y1);
  g_pc = at3(b, n, a, 0, 0, (y0 + y1) / 2);
  V3 s1[4] = {e0, e1, r1, r0}, s2[4] = {e3, e2, r1, r0};
  pface(g, s1, 4, col, false);
  pface(g, s2, 4, col, false);
  V3 t1[3] = {e0, e3, r0}, t2[3] = {e1, e2, r1};
  pface(g, t1, 3, gableCol, false);
  pface(g, t2, 3, gableCol, false);
}

// A hip roof (or a pyramid when k is 0): ridge along a, half length ha*k.
static void phip(Gather* g, V3 b, V3 n, V3 a, float hn, float ha, float y0, float y1, float k, uint32_t col) {
  V3 e0 = at3(b, n, a, -hn, -ha, y0), e1 = at3(b, n, a, -hn, ha, y0), e2 = at3(b, n, a, hn, ha, y0), e3 = at3(b, n, a, hn, -ha, y0);
  V3 r0 = at3(b, n, a, 0, -ha * k, y1), r1 = at3(b, n, a, 0, ha * k, y1);
  g_pc = at3(b, n, a, 0, 0, (y0 + y1) / 2);
  V3 s1[4] = {e0, e1, r1, r0}, s2[4] = {e3, e2, r1, r0};
  pface(g, s1, 4, col, false);
  pface(g, s2, 4, col, false);
  V3 t1[3] = {e0, e3, r0}, t2[3] = {e1, e2, r1};
  pface(g, t1, 3, col, false);
  pface(g, t2, 3, col, false);
}

// A frustum of s sides (a cone when r1 is 0) from b along axis, its top cap if any.
static void pcyl(Gather* g, V3 b, V3 axis, float r0, float r1, float len, int s, uint32_t col) {
  if(s > 8) s = 8;
  V3 a1 = fabsf(axis.y) > 0.9f ? v3(1, 0, 0) : kUp;
  a1 = vnorm(vcross(axis, a1));
  V3 a2 = vcross(axis, a1);
  V3 r0p[8], r1p[8], top = vadd(b, vmul(axis, len));
  for(int i = 0; i < s; i++) {
    float ang = 2 * PI * (i + 0.5f) / s;
    V3 d = vadd(vmul(a1, cosf(ang)), vmul(a2, sinf(ang)));
    r0p[i] = vadd(b, vmul(d, r0));
    r1p[i] = vadd(top, vmul(d, r1));
  }
  g_pc = vadd(b, vmul(axis, len / 2));
  for(int i = 0; i < s; i++) {
    int j = (i + 1) % s;
    if(r1 > 0.001f) {
      V3 f[4] = {r0p[i], r0p[j], r1p[j], r1p[i]};
      pface(g, f, 4, col, false);
    } else {
      V3 f[3] = {r0p[i], r0p[j], top};
      pface(g, f, 3, col, false);
    }
  }
  if(r1 > 0.001f) pface(g, r1p, s, shade(col, 1.06f), false);
}

// A quad on a face of a box (windows, doors, signs), lifted off it: on the face at +n*hn
// (side 0), or at +a*ha (side 1), at u (across, from the face's middle) and y.
static void pdecal(Gather* g, V3 b, V3 n, V3 a, float hn, float u0, float u1, float y0, float y1, uint32_t col, bool emissive) {
  V3 c = vadd(b, vmul(n, hn + 0.04f + g_pd * 0.0025f));  // lifted more far off: no fighting with the wall
  V3 f[4] = {at3(c, n, a, 0, u0, y0), at3(c, n, a, 0, u1, y0), at3(c, n, a, 0, u1, y1), at3(c, n, a, 0, u0, y1)};
  bool e = g_emit;
  g_emit = emissive;
  pface_n(g, f, 4, col, n);
  g_emit = e;
}

// Windows on the face toward the road: floors from y0, rows of n per floor; lit at night.
static void windows(Gather* g, V3 b, V3 n, V3 a, float hn, float ha, float y0, int floors, float fh, int per, uint32_t seed,
                    uint32_t dayCol) {
  if(vdot(n, vsub(vadd(b, vmul(n, hn)), g->cam->eye)) >= 0) return;  // the face looks away
  float pitch = ha * 2 / per;
  for(int f = 0; f < floors; f++) {
    float wy0 = y0 + f * fh + fh * 0.3f, wy1 = wy0 + fh * 0.45f;
    if(!g_night) {  // by day: a band of glass a floor (the same at every distance: nothing pops)
      pdecal(g, b, n, a, hn, -ha + pitch * 0.25f, ha - pitch * 0.25f, wy0, wy1, with_alpha(dayCol, 96), false);
      continue;
    }
    for(int i = 0; i < per; i++) {
      float u0 = -ha + pitch * (i + 0.22f), u1 = -ha + pitch * (i + 0.78f);
      if(g_night) {
        uint32_t hh = hash32(seed + (uint32_t) (f * 31 + i) * 2654435761u);
        if((hh & 7) < 3) continue;  // dark windows stay the wall's (dark) colour
        pdecal(g, b, n, a, hn, u0, u1, wy0, wy1, (hh >> 4) & 3 ? 0xFFFFD27A : 0xFFCFE0FF, true);
      } else {
        pdecal(g, b, n, a, hn, u0, u1, wy0, wy1, dayCol, false);
      }
    }
  }
}

static void glow(Gather* g, V3 p, float size, uint32_t col) {
  if(!g_night) return;
  V3 r = vnorm(v3(g->cam->right.x, 0, g->cam->right.z));
  V3 a = vadd(p, vadd(vmul(r, -size / 2), vmul(kUp, -size / 2))), b = vadd(p, vadd(vmul(r, size / 2), vmul(kUp, -size / 2)));
  tex_quad(g, vadd(a, vmul(kUp, size)), vadd(b, vmul(kUp, size)), b, a, TEX_GLOW, fogged(col, g_pd * 0.5f), 1);
}

static uint32_t pick(const uint32_t* pal, uint32_t h) { return pal[h & 3]; }

// --- The props -----------------------------------------------------------------------------

typedef struct {
  V3 p, n, a;       // where, toward the road, along the road
  float s;          // scale
  uint32_t h;       // its own hash (variation)
  uint16_t seg;
  uint8_t kind, cls;
} Prop;

static void tree_crown(Gather* g, V3 c, float r, float hgt, uint32_t col) {
  // A rounded faceted crown: three bands, 7 sides, lighter at the top.
  pcyl(g, vadd(c, vmul(kUp, -hgt * 0.5f)), kUp, r * 0.5f, r * 0.95f, hgt * 0.3f, 7, shade(col, 0.86f));
  pcyl(g, vadd(c, vmul(kUp, -hgt * 0.2f)), kUp, r * 0.95f, r * 0.85f, hgt * 0.35f, 7, col);
  pcyl(g, vadd(c, vmul(kUp, hgt * 0.15f)), kUp, r * 0.85f, r * 0.3f, hgt * 0.35f, 7, shade(col, 1.08f));
}

static void frond(Gather* g, V3 top, V3 dir, float len, float droop, float wid, uint32_t col) {
  V3 side = vnorm(vcross(dir, kUp));
  V3 mid = vadd(top, vadd(vmul(dir, len * 0.55f), vmul(kUp, len * 0.12f)));
  V3 tip = vadd(top, vadd(vmul(dir, len), vmul(kUp, -droop)));
  ptri2(g, top, vadd(mid, vmul(side, wid)), mid, col);
  ptri2(g, top, mid, vadd(mid, vmul(side, -wid)), shade(col, 0.9f));
  ptri2(g, vadd(mid, vmul(side, wid)), tip, vadd(mid, vmul(side, -wid)), shade(col, 0.95f));
}

static void build_prop(Gather* g, const Prop* p) {
  V3 b = p->p, n = p->n, a = p->a;
  float s = p->s;
  uint32_t h = p->h;
  const Kit* k = &g_kit;
  uint32_t wall = pick(k->walls, h), roof = pick(k->roofs, h >> 2);
  if(p->kind == K_NEON || p->kind == K_BILLBOARD || p->kind == K_TORII) {  // signs turn toward the oncoming driver
    V3 n2 = vnorm(vsub(vmul(n, 0.8f), vmul(a, 0.6f)));
    a = vnorm(vadd(vmul(a, 0.8f), vmul(n, 0.6f)));
    n = n2;
  }
  switch(p->kind) {
    case K_PALM: {  // coconut palm: a curving trunk and a star of drooping fronds
      float H = 7.5f * s;
      V3 lean = vnorm(vadd(kUp, vmul(n, 0.12f + (h & 7) * 0.02f)));
      V3 pnt = b;
      for(int i = 0; i < 3; i++) {
        V3 ax = vnorm(vadd(lean, vmul(n, i * 0.1f)));
        pcyl(g, pnt, ax, 0.22f - i * 0.03f, 0.19f - i * 0.03f, H / 3, 5, 0xFF8A7458);
        pnt = vadd(pnt, vmul(ax, H / 3));
      }
      for(int i = 0; i < 7; i++) {
        float ang = i * 2 * PI / 7 + (h & 15) * 0.2f;
        V3 d = vnorm(vadd(vmul(n, cosf(ang)), vmul(a, sinf(ang))));
        frond(g, pnt, d, 3.2f * s, 1.6f * s, 0.45f * s, i & 1 ? 0xFF3E8A3A : 0xFF4E9A40);
      }
      break;
    }
    case K_TALL_PALM: {  // Washingtonia: a very tall thin trunk, a small tufted head
      float H = (13 + (h & 7)) * s;
      pcyl(g, b, kUp, 0.24f, 0.16f, H, 5, 0xFF8E7A62);
      V3 top = vadd(b, vmul(kUp, H));
      pcyl(g, vadd(top, vmul(kUp, -1.6f)), kUp, 0.2f, 0.55f, 1.7f, 5, 0xFF8A7A50);  // the dry skirt
      for(int i = 0; i < 7; i++) {
        float ang = i * 2 * PI / 7 + (h & 15) * 0.2f;
        V3 d = vnorm(vadd(vmul(n, cosf(ang)), vmul(a, sinf(ang))));
        frond(g, top, d, 2.0f * s, 0.9f * s, 0.4f * s, i & 1 ? 0xFF4A8A3A : 0xFF5A9A44);
      }
      break;
    }
    case K_CYPRESS: {
      float H = (8 + (h & 3)) * s;
      pcyl(g, b, kUp, 0.15f, 0.12f, 0.8f, 5, 0xFF4E3624);
      pcyl(g, vadd(b, vmul(kUp, 0.6f)), kUp, 0.55f * s, 0.85f * s, H * 0.3f, 6, 0xFF2A5230);
      pcyl(g, vadd(b, vmul(kUp, 0.6f + H * 0.3f)), kUp, 0.85f * s, 0, H * 0.7f, 6, 0xFF305C34);
      break;
    }
    case K_POPLAR: {
      float H = (13 + (h & 3)) * s;
      pcyl(g, b, kUp, 0.2f, 0.15f, H * 0.2f, 5, 0xFF6A5A48);
      pcyl(g, vadd(b, vmul(kUp, H * 0.15f)), kUp, 0.8f * s, 1.2f * s, H * 0.35f, 6, 0xFF4E7E3A);
      pcyl(g, vadd(b, vmul(kUp, H * 0.5f)), kUp, 1.2f * s, 0, H * 0.5f, 6, 0xFF5A8A42);
      break;
    }
    case K_SPRUCE: {
      float H = (9 + (h & 7)) * s;
      uint32_t c = k->veg == 0xFF3E7044 || k->snow ? 0xFF24483A : 0xFF24482E;
      pcyl(g, b, kUp, 0.22f, 0.18f, H * 0.15f, 5, 0xFF4A3424);
      for(int t = 0; t < 3; t++) {
        float y0 = H * (0.1f + t * 0.26f), r0 = (2.1f - t * 0.5f) * s;
        pcyl(g, vadd(b, vmul(kUp, y0)), kUp, r0, 0, H * (0.42f - t * 0.04f), 6, shade(c, 0.9f + t * 0.08f));
        if(k->snow && t == 2) pcyl(g, vadd(b, vmul(kUp, y0 + H * 0.22f)), kUp, r0 * 0.48f, 0, H * 0.17f, 6, 0xFFF0F4F8);
      }
      break;
    }
    case K_BROADLEAF: case K_PLANE: case K_CHERRY: case K_MAPLE: case K_OLIVE: {
      float H = (p->kind == K_CHERRY ? 5.5f : p->kind == K_OLIVE ? 4.5f : p->kind == K_PLANE ? 10 : 8) * s * (0.85f + (h & 7) * 0.04f);
      float R = (p->kind == K_OLIVE ? 2.2f : p->kind == K_PLANE ? 3.0f : p->kind == K_MAPLE || p->kind == K_CHERRY ? 2.1f : 2.5f) * s;
      uint32_t c = p->kind == K_CHERRY ? ((h >> 3) & 1 ? 0xFFF2A8C8 : 0xFFF6C0D6) : p->kind == K_MAPLE ? ((h >> 3) & 1 ? 0xFFD8502A : 0xFFE87A2E)
                   : p->kind == K_OLIVE ? 0xFF7E8E5A : shade(k->veg, 0.9f + ((h >> 3) & 3) * 0.06f);
      pcyl(g, b, kUp, 0.25f * s, 0.18f * s, H * 0.55f, 5, p->kind == K_PLANE ? 0xFF9A9078 : 0xFF5A4232);
      tree_crown(g, vadd(b, vmul(kUp, H * 0.68f)), R, H * 0.6f, c);
      break;
    }
    case K_UMBRELLA: {  // stone pine: a tall bare leaning trunk, a flat dark crown
      float H = (9 + (h & 3)) * s;
      V3 ax = vnorm(vadd(kUp, vmul(a, ((h & 1) ? 0.12f : -0.12f))));
      pcyl(g, b, ax, 0.28f, 0.2f, H, 5, 0xFF6A5040);
      V3 top = vadd(b, vmul(ax, H));
      pcyl(g, vadd(top, vmul(kUp, -0.6f)), kUp, 1.2f * s, 3.4f * s, 0.8f, 7, 0xFF33552E);
      pcyl(g, vadd(top, vmul(kUp, 0.2f)), kUp, 3.4f * s, 2.0f * s, 0.9f, 7, 0xFF3C6234);
      break;
    }
    case K_BIRCH: {
      float H = (8 + (h & 3)) * s;
      pcyl(g, b, kUp, 0.14f, 0.1f, H * 0.7f, 5, 0xFFECEAE0);
      pbox(g, vadd(b, vmul(kUp, H * 0.25f)), n, a, 0.15f, 0.15f, 0, 0.18f, 0xFF2A2A2A);
      tree_crown(g, vadd(b, vmul(kUp, H * 0.7f)), 1.5f * s, H * 0.55f, k->snow ? 0xFF9AB458 : 0xFF7EA84A);
      break;
    }
    case K_KAPOK: {  // a rainforest giant: buttress roots, a tall trunk, a wide layered crown
      float H = (20 + (h & 7)) * s;
      for(int i = 0; i < 3; i++) {
        float ang = i * 2 * PI / 3;
        V3 d = vnorm(vadd(vmul(n, cosf(ang)), vmul(a, sinf(ang))));
        pbox(g, vadd(b, vmul(d, 0.8f)), d, vcross(kUp, d), 0.9f, 0.12f, 0, 2.2f, 0xFF7A6A52);
      }
      pcyl(g, b, kUp, 0.8f, 0.5f, H, 6, 0xFF8A7A62);
      V3 top = vadd(b, vmul(kUp, H));
      pcyl(g, vadd(top, vmul(kUp, -2)), kUp, 3, 8 * s, 2.2f, 7, 0xFF2E6A2E);
      pcyl(g, vadd(top, vmul(kUp, 0.2f)), kUp, 8 * s, 4 * s, 2.4f, 7, 0xFF3A7A34);
      break;
    }
    case K_FERN: {
      for(int i = 0; i < 5; i++) {
        float ang = i * 2 * PI / 5 + (h & 7) * 0.4f;
        V3 d = vnorm(vadd(vmul(n, cosf(ang)), vmul(a, sinf(ang))));
        frond(g, vadd(b, vmul(kUp, 0.3f)), vnorm(vadd(d, vmul(kUp, 0.6f))), 1.6f * s, 0.6f, 0.35f, i & 1 ? 0xFF2E7A2E : 0xFF3E8A36);
      }
      break;
    }
    case K_JOSHUA: {
      float H = 3.2f * s;
      pcyl(g, b, kUp, 0.25f, 0.2f, H * 0.6f, 5, 0xFF6E5E4A);
      V3 fork = vadd(b, vmul(kUp, H * 0.6f));
      for(int i = 0; i < 3; i++) {
        float ang = i * 2 * PI / 3 + (h & 7);
        V3 d = vnorm(vadd(vadd(vmul(n, cosf(ang) * 0.7f), vmul(a, sinf(ang) * 0.7f)), kUp));
        pcyl(g, fork, d, 0.16f, 0.13f, H * 0.4f, 5, 0xFF6E5E4A);
        V3 tip = vadd(fork, vmul(d, H * 0.4f));
        pcyl(g, tip, kUp, 0.45f, 0, 0.7f, 5, 0xFF6E8A3E);
        pcyl(g, tip, vmul(kUp, -1), 0.4f, 0, 0.35f, 5, 0xFF5E7A36);
      }
      break;
    }
    case K_SAGUARO: {
      float H = (4 + (h & 3)) * s, t = 0.32f * s;
      pcyl(g, b, kUp, t, t * 0.85f, H, 6, 0xFF4E7A3A);
      for(int sg = -1; sg <= 1; sg += 2) {
        if((h >> (4 + sg)) & 1) continue;
        float ay = H * (sg < 0 ? 0.4f : 0.52f);
        V3 c = vadd(b, vmul(kUp, ay));
        pcyl(g, c, vmul(a, (float) sg), t * 0.7f, t * 0.7f, 0.9f * s, 5, 0xFF4E7A3A);
        pcyl(g, vadd(c, vmul(a, sg * 0.9f * s)), kUp, t * 0.7f, t * 0.6f, H * 0.3f, 5, 0xFF4E7A3A);
      }
      break;
    }
    case K_SCRUB: {
      float R = (0.6f + (h & 7) * 0.08f) * s;
      uint32_t c = k->snow ? 0xFF6E7A5A : mix(k->veg, 0xFF9A9468, 0.45f);
      pcyl(g, b, kUp, R, R * 0.4f, R * 0.9f, 6, c);
      break;
    }
    case K_HEATHER: {
      float R = (1.0f + (h & 3) * 0.15f) * s;
      pcyl(g, b, kUp, R, R * 0.6f, 0.6f * s, 6, (h & 1) ? 0xFF8A5A88 : 0xFF7A6A4A);
      break;
    }
    case K_HEDGE:
      pbox(g, b, n, a, 0.6f, 4.3f, 0, 1.4f * s, shade(k->veg, 0.8f));
      pbox(g, b, n, a, 0.5f, 4.2f, 1.4f * s, 1.6f * s, shade(k->veg, 0.9f));
      break;
    case K_VINES: {  // rows at right angles to the road (a rhythm as they pass), on posts
      for(int i = -2; i <= 2; i++) {
        V3 rb = vadd(b, vmul(a, i * 2.2f));
        pbox(g, rb, n, a, 5.0f, 0.3f, 0, 1.1f, i & 1 ? 0xFF4E7A30 : 0xFF5A8636);
      }
      break;
    }
    case K_MESA: {  // a red butte on the desert floor
      float H = (14 + (h & 7) * 2) * s, R = 18 * s;
      pcyl(g, b, kUp, R, R * 0.8f, H * 0.35f, 7, 0xFFB47252);
      pcyl(g, vadd(b, vmul(kUp, H * 0.35f)), kUp, R * 0.72f, R * 0.66f, H * 0.65f, 7, 0xFFC0805A);
      break;
    }
    case K_BARROW:
      pcyl(g, b, kUp, 7.5f * s, 3.0f * s, 2.4f * s, 8, shade(k->veg, 1.05f));
      break;
    // --- buildings ---
    case K_HOUSE: case K_TIMBER: case K_REDHOUSE: case K_VILLA: {
      float hn = 3.6f * s, ha = (4.0f + (h & 3) * 0.4f) * s;
      int floors = p->kind == K_VILLA ? 2 : 1 + ((h >> 5) & 1) + (p->kind == K_TIMBER);
      float fh = 3.0f, top = floors * fh;
      if(p->kind == K_REDHOUSE) wall = (h >> 6) % 3 ? 0xFFA8382A : wall, roof = 0xFF3A3C42;
      if(p->kind == K_TIMBER) wall = (h >> 6) & 1 ? 0xFFF2ECDC : 0xFFE8DCC0;
      pbox(g, b, n, a, hn, ha, 0, top, wall);
      if(p->kind == K_VILLA) {
        phip(g, b, n, a, hn + 0.5f, ha + 0.5f, top, top + 2.0f, 0.5f, roof);
      } else {
        float rh = p->kind == K_TIMBER ? 4.5f : p->kind == K_REDHOUSE ? 2.6f : 3.2f;
        pgable(g, b, n, a, hn + 0.4f, ha + 0.3f, top, top + rh, false, roof, wall);
      }
      if(vdot(n, vsub(vadd(b, vmul(n, hn)), g->cam->eye)) < 0) {
        if(p->kind == K_TIMBER) {  // the dark frame: posts, rails, braces
          for(int i = -2; i <= 2; i++) pdecal(g, b, n, a, hn, i * ha * 0.48f - 0.12f, i * ha * 0.48f + 0.12f, 0, top, 0xFF4A3426, false);
          for(int f = 1; f <= floors; f++) pdecal(g, b, n, a, hn, -ha, ha, f * fh - 0.2f, f * fh, 0xFF4A3426, false);
        }
        if(p->kind == K_REDHOUSE)  // white corners
          for(int sg = -1; sg <= 1; sg += 2) pdecal(g, b, n, a, hn, sg * ha - 0.25f, sg * ha + 0.25f, 0, top, 0xFFF4F4F0, false);
        pdecal(g, b, n, a, hn, -0.5f, 0.5f, 0, 2.1f, p->kind == K_VILLA ? 0xFF3A6A4A : 0xFF4A3A30, false);  // door
        windows(g, b, n, a, hn, ha, 0, floors, fh, 3, h, p->kind == K_VILLA ? 0xFF3A6A4A : p->kind == K_REDHOUSE ? 0xFFF4F4F0 : 0xFF4A5A6E);
      }
      break;
    }
    case K_FLAT: {  // stucco, flat roof, a parapet
      float hn = 4.5f * s, ha = 5.5f * s, top = (3.2f + ((h >> 4) & 1) * 3.2f);
      pbox(g, b, n, a, hn, ha, 0, top, wall);
      pbox(g, b, n, a, hn + 0.1f, ha + 0.1f, top, top + 0.5f, shade(wall, 0.92f));
      windows(g, b, n, a, hn, ha, 0, top > 4 ? 2 : 1, 3.2f, 4, h, 0xFF4A6278);
      break;
    }
    case K_BLOCK: case K_HAUSSMANN: case K_TERRACE: {
      int floors = p->kind == K_HAUSSMANN ? 6 : p->kind == K_TERRACE ? 3 : 4 + (h >> 4) % 5;
      float fh = 3.1f, top = floors * fh, hn = (p->kind == K_TERRACE ? 4 : 6) * s, ha = (p->kind == K_TERRACE ? 7.5f : 7.5f) * s;
      if(p->kind == K_TERRACE) {  // a row of narrow houses, each its colour (London brick, Nyhavn's)
        for(int i = 0; i < 3; i++) {
          V3 hb = vadd(b, vmul(a, (i - 1) * ha * 2 / 3));
          uint32_t wc = k->walls[(h + (uint32_t) i) & 3];
          float th = top + ((h >> (i * 2)) & 1) * 1.2f;
          pbox(g, hb, n, a, hn, ha / 3 - 0.05f, 0, th, wc);
          pgable(g, hb, n, a, hn, ha / 3, th, th + 2.6f, k->snow, roof, wc);
          windows(g, hb, n, a, hn, ha / 3, 0, floors, fh, 2, h + (uint32_t) i * 77, 0xFF3E4A5A);
          if(!k->snow) pbox(g, vadd(hb, vmul(a, ha / 3 - 0.6f)), n, a, 0.4f, 0.3f, th + 1.4f, th + 3.4f, 0xFF7A4A3A);  // chimney
        }
        break;
      }
      pbox(g, b, n, a, hn, ha, 0, top, wall);
      if(p->kind == K_HAUSSMANN) {
        phip(g, b, n, a, hn, ha, top, top + 3.2f, 0.75f, 0xFF6A7078);  // the zinc mansard
        for(int f = 1; f < 6; f += 3) pbox(g, vadd(b, vmul(n, hn + 0.3f)), n, a, 0.3f, ha, f * fh, f * fh + 0.25f, 0xFF20242A);  // balconies
      } else {
        pbox(g, vadd(b, vmul(a, ha * 0.3f)), n, a, hn * 0.35f, ha * 0.25f, top, top + 2.2f, shade(wall, 0.8f));  // the roof's plant room
      }
      if(p->kind == K_HAUSSMANN && !g_night) {  // tall French windows in columns
        if(vdot(n, vsub(vadd(b, vmul(n, hn)), g->cam->eye)) < 0)
          for(int f = 0; f < floors; f++)
            for(int i = 0; i < 5; i++) {
              float u = -ha + ha * 2 * (i + 0.5f) / 5;
            pdecal(g, b, n, a, hn, u - 0.45f, u + 0.45f, f * fh + 0.5f, f * fh + 2.5f, 0xFF4E5868, false);
          }
      } else {
        windows(g, b, n, a, hn, ha, 0.5f, floors, fh, 5, h, 0xFF4A5A6E);
      }
      break;
    }
    case K_TOWER: {
      float H = (35 + (h >> 4) % 45) * s, hn = 7 * s, ha = 7 * s;
      uint32_t c = k->glass && ((h >> 9) & 3) ? ((h >> 11) & 1 ? 0xFF6A8AA8 : 0xFF7E98AE) : wall;
      if(g_night) c = 0xFF202838;
      pbox(g, b, n, a, hn, ha, 0, H, c);
      pbox(g, b, n, a, hn * 0.7f, ha * 0.7f, H, H + 6, shade(c, 0.9f));
      if((h >> 11) & 1) pcyl(g, vadd(b, vmul(kUp, H + 6)), kUp, 0.3f, 0.05f, 10, 4, 0xFFB0B8C0);  // a mast
      windows(g, b, n, a, hn, ha, 1, (int) (H / 3.6f), 3.6f, 6, h, shade(c, 1.18f));
      break;
    }
    case K_FARM: {  // Black Forest / country farmhouse: a huge hip roof reaching down low
      float hn = 6 * s, ha = 7.5f * s;
      uint32_t wc = k->veg == 0xFF2E5A34 ? 0xFF8A6A4A : wall;
      pbox(g, b, n, a, hn, ha, 0, 4.5f, wc);
      phip(g, b, n, a, hn + 1.2f, ha + 0.8f, 3.2f, 10.5f, 0.55f, k->veg == 0xFF2E5A34 ? 0xFF4E3E30 : roof);
      windows(g, b, n, a, hn, ha, 0, 1, 3.0f, 4, h, 0xFF3A3A40);
      break;
    }
    case K_FAVELA: {  // stacked bright boxes climbing away from the road
      for(int i = 0; i < 5; i++) {
        uint32_t hh = hash32(h + (uint32_t) i * 7919u);
        V3 hb = vadd(b, vadd(vmul(n, -((float) (i / 2) * 3.5f) + 2), vmul(a, ((i & 1) ? 2.6f : -2.4f))));
        float y0 = (float) (i / 2) * 2.5f, y1 = y0 + 2.8f + (hh & 3) * 0.6f;
        pbox(g, hb, n, a, 1.6f, 1.9f, y0, y1, pick(k->walls, hh));
        pdecal(g, hb, n, a, 1.6f, -0.5f, 0.5f, y0 + 1, y0 + 1.9f, g_night ? 0xFFFFD27A : 0xFF3A3A40, g_night && (hh & 4));
      }
      break;
    }
    case K_VICTORIAN: {  // a narrow pastel house, its gable to the street, a bay window
      float hn = 4.5f * s, ha = 3.0f * s, top = 7.5f;
      pbox(g, b, n, a, hn, ha, 0, top, wall);
      pgable(g, b, n, a, hn, ha + 0.2f, top, top + 3.4f, true, shade(wall, 0.75f), wall);
      pbox(g, vadd(b, vmul(n, hn + 0.6f)), n, a, 0.6f, ha * 0.55f, 1.0f, 6.4f, shade(wall, 1.05f));
      windows(g, vadd(b, vmul(n, 1.2f)), n, a, hn, ha * 0.45f, 1, 2, 3.0f, 2, h, 0xFF3E4A5A);
      break;
    }
    case K_MACHIYA: {  // dark timber, a low tiled roof with a deep overhang, two tiers
      float hn = 3.6f * s, ha = 4.2f * s;
      pbox(g, b, n, a, hn, ha, 0, 3.2f, wall);
      phip(g, b, n, a, hn + 0.9f, ha + 0.6f, 3.0f, 4.2f, 0.85f, roof);
      pbox(g, b, n, a, hn * 0.8f, ha * 0.9f, 4.2f, 6.2f, wall);
      phip(g, b, n, a, hn * 0.8f + 0.8f, ha * 0.9f + 0.5f, 6.0f, 7.8f, 0.6f, roof);
      windows(g, b, n, a, hn, ha, 0, 1, 3.0f, 3, h, 0xFFD8CCB0);
      break;
    }
    case K_CASINO: {  // a wide white-and-gold base, a slab tower, a gold crown band
      float hn = 9 * s, ha = 13 * s, H = (28 + (h >> 4) % 20) * s;
      uint32_t gold = 0xFFE0B850;
      pbox(g, b, n, a, hn, ha, 0, 8, wall);
      pbox(g, vadd(b, vmul(n, -3)), n, a, 4 * s, ha * 0.7f, 8, H, (h >> 6) & 1 ? 0xFFF4F0E8 : 0xFFE8C878);
      pbox(g, vadd(b, vmul(n, -3)), n, a, 4.1f * s, ha * 0.71f, H - 2, H, gold);
      windows(g, vadd(b, vmul(n, -3)), n, a, 4 * s, ha * 0.7f, 8, (int) ((H - 10) / 3.5f), 3.5f, 6, h, 0xFF6A7E98);
      // the marquee: a lit band along the base
      pdecal(g, b, n, a, hn, -ha, ha, 6.2f, 7.6f, g_night ? 0xFFFF5AB4 : 0xFFE84A8A, g_night);
      if(g_night) glow(g, at3(b, n, a, hn + 1, 0, 7), 14, 0x80FF5AB4);
      break;
    }
    case K_FACTORY: {  // a brick works: sawtooth roof and a tall chimney
      float hn = 7 * s, ha = 11 * s;
      pbox(g, b, n, a, hn, ha, 0, 7, wall);
      for(int i = -1; i <= 1; i++) pgable(g, vadd(b, vmul(a, i * ha * 0.66f)), n, a, hn, ha / 3, 7, 10, true, 0xFF4A4C54, wall);
      pcyl(g, vadd(b, vadd(vmul(n, -3), vmul(a, ha * 0.6f))), kUp, 1.3f, 0.9f, 28 * s, 6, 0xFF7A4234);
      windows(g, b, n, a, hn, ha, 0.5f, 2, 3.0f, 6, h, 0xFF4A5060);
      break;
    }
    case K_COOLING:
      pcyl(g, b, kUp, 12 * s, 7.5f * s, 22 * s, 8, 0xFFB8B4AC);
      pcyl(g, vadd(b, vmul(kUp, 22 * s)), kUp, 7.5f * s, 9 * s, 12 * s, 8, 0xFFC4C0B8);
      break;
    case K_HUT: {  // Inca: dry stone walls, a steep thatched gable
      float hn = 2.4f * s, ha = 2.8f * s;
      pbox(g, b, n, a, hn, ha, 0, 2.6f, wall);
      pgable(g, b, n, a, hn + 0.3f, ha + 0.2f, 2.6f, 5.4f, true, 0xFFB49A5A, wall);
      pdecal(g, b, n, a, hn, -0.45f, 0.45f, 0, 1.8f, 0xFF2A2622, false);
      break;
    }
    case K_WAREHOUSE: {  // red brick harbour warehouse
      float hn = 5.5f * s, ha = 11.5f * s;
      pbox(g, b, n, a, hn, ha, 0, 10, 0xFF9A4A36);
      pgable(g, b, n, a, hn + 0.2f, ha + 0.2f, 10, 12.5f, false, 0xFF5A5E66, 0xFF9A4A36);
      for(int i = -2; i <= 2; i++) pdecal(g, b, n, a, hn, i * 4.2f - 1.2f, i * 4.2f + 1.2f, 0, 3.2f, 0xFF2E2A28, false);
      windows(g, b, n, a, hn, ha, 3.5f, 2, 3.0f, 6, h, 0xFF3A3A40);
      break;
    }
    case K_PAGODA: {  // a small temple pagoda: stacked tiers, wide roofs, a finial
      float y = 0;
      for(int t = 0; t < 3; t++) {
        float hw = (3.2f - t * 0.7f) * s;
        pbox(g, b, n, a, hw * 0.7f, hw * 0.7f, y, y + 2.6f, 0xFF5A3A2A);
        phip(g, b, n, a, hw + 1.0f, hw + 1.0f, y + 2.4f, y + 3.6f, 0.2f, 0xFF3A3E46);
        y += 3.6f;
      }
      pcyl(g, vadd(b, vmul(kUp, y)), kUp, 0.15f, 0.05f, 3.5f, 4, 0xFFC8A040);
      break;
    }
    case K_STEPPYR: {  // a small Maya pyramid: stepped terraces, a temple on top, the stair
      float y = 0;
      for(int t = 0; t < 4; t++) {
        float hw = (8.5f - t * 1.7f) * s;
        pbox(g, b, n, a, hw, hw, y, y + 2.4f, t & 1 ? wall : shade(wall, 0.94f));
        y += 2.4f;
      }
      pbox(g, b, n, a, 1.8f * s, 2.2f * s, y, y + 3.2f, shade(wall, 0.9f));
      pdecal(g, b, n, a, 8.5f * s, -1.4f, 1.4f, 0, 2.3f, shade(wall, 1.08f), false);
      break;
    }
    // --- street furniture ---
    case K_LAMP: {  // a street lamp: pole, arm over the road, a lit head (and its glow at night)
      float H = 8.0f;
      pcyl(g, b, kUp, 0.11f, 0.07f, H, 5, 0xFF7E848C);
      V3 top = vadd(b, vmul(kUp, H));
      pbox(g, vadd(top, vmul(n, 0.8f)), n, a, 0.85f, 0.05f, -0.06f, 0.06f, 0xFF7E848C);
      V3 head = vadd(top, vmul(n, 1.6f));
      pbox(g, head, n, a, 0.35f, 0.18f, -0.15f, 0.08f, 0xFF5A6068);
      g_emit = g_night;
      pbox(g, head, n, a, 0.28f, 0.14f, -0.2f, -0.14f, g_night ? 0xFFFFE8B0 : 0xFFE8ECEE);
      g_emit = false;
      glow(g, vadd(head, vmul(kUp, -0.4f)), 4.0f, 0xC0FFD890);
      break;
    }
    case K_NEON: {  // a sign on a pole: a bright panel, its border lit at night
      static const uint32_t kNeon[4] = {0xFFFF3C9A, 0xFF2EE6F0, 0xFFFFC23C, 0xFF7A4CFF};
      uint32_t c = kNeon[h & 3], c2 = kNeon[(h >> 2) & 3];
      float H = 5.5f + (h >> 4 & 3);
      pcyl(g, b, kUp, 0.14f, 0.12f, H, 5, 0xFF5A6068);
      V3 pb = vadd(b, vmul(kUp, H));
      bool vert = (h >> 7) & 1;  // a tall vertical sign (Tokyo's) or a wide one
      float hw = vert ? 0.7f : 1.9f, hh = vert ? 3.2f : 1.4f;
      pbox(g, pb, n, a, 0.25f, hw, -hh, hh, g_night ? 0xFF14141C : 0xFF8A9098);
      for(int sd = -1; sd <= 1; sd += 2)  // lit on both faces
        pdecal(g, pb, vmul(n, (float) sd), vmul(a, (float) sd), 0.25f, -hw + 0.18f, hw - 0.18f, -hh + 0.18f, hh - 0.18f,
               g_night ? c : mix(c, 0xFFFFFFFF, 0.15f), g_night);
      pdecal(g, vadd(pb, vmul(n, 0.02f)), n, a, 0.25f, -hw + 0.4f, hw - 0.4f, vert ? -hh * 0.3f : -0.25f, vert ? hh * 0.3f : 0.25f,
             g_night ? c2 : 0xFFFFFFFF, g_night);
      if(g_night) glow(g, vadd(pb, vmul(n, 0.6f)), hw * 4, with_alpha(c, 0x90));
      break;
    }
    case K_BILLBOARD: {  // a framed board on two legs, a bold two-colour design
      static const uint32_t kAd[6] = {0xFFE8402A, 0xFF2A6AD8, 0xFFF0C030, 0xFF2AA860, 0xFFF4F4F0, 0xFF202830};
      uint32_t c1 = kAd[h % 6], c2 = kAd[(h >> 3) % 6];
      if(c1 == c2) c2 = 0xFFF4F4F0;
      for(int sg = -1; sg <= 1; sg += 2) pbox(g, vadd(b, vmul(a, sg * 2.0f)), n, a, 0.12f, 0.12f, 0, 3.2f, 0xFF5A6068);
      V3 pb = vadd(b, vmul(kUp, 3.2f));
      pbox(g, pb, n, a, 0.15f, 3.2f, 0, 3.4f, 0xFF7A8088);
      pdecal(g, pb, n, a, 0.15f, -3.0f, 3.0f, 0.2f, 3.2f, c1, g_night);
      pdecal(g, vadd(pb, vmul(n, 0.02f)), n, a, 0.15f, -3.0f, 0.2f, 0.2f, 1.6f, c2, g_night);
      pdecal(g, vadd(pb, vmul(n, 0.02f)), n, a, 0.15f, 0.6f, 2.6f, 2.0f, 2.8f, 0xFFFFFFFF, g_night);
      if(g_night) glow(g, vadd(pb, vadd(vmul(n, 1), vmul(kUp, 1.7f))), 8, 0x60FFF0D0);
      break;
    }
    case K_PHONEBOX: {  // the red K6 kiosk: a red slab, glazed, a lit crown
      pbox(g, b, n, a, 0.5f, 0.5f, 0, 2.4f, 0xFFC8102E);
      phip(g, b, n, a, 0.55f, 0.55f, 2.4f, 2.7f, 0, 0xFFC8102E);
      for(int f = 0; f < 3; f++) pdecal(g, b, n, a, 0.5f, -0.35f, 0.35f, 0.9f + f * 0.42f, 1.22f + f * 0.42f, g_night ? 0xFFFFF0C0 : 0xFFB8C8D0, g_night);
      pdecal(g, b, n, a, 0.5f, -0.4f, 0.4f, 2.12f, 2.32f, 0xFFF4F4F0, g_night);
      break;
    }
    case K_BUS: {  // a red double-decker, parked along the kerb
      pbox(g, b, a, n, 5.2f, 1.25f, 0.4f, 4.3f, 0xFFC8102E);
      for(int f = 0; f < 2; f++) {
        float y0 = 1.6f + f * 1.6f;
        V3 fc = at3(b, n, a, 0, 0, 0);
        pdecal(g, fc, n, a, 1.25f, -4.8f, 4.6f, y0, y0 + 0.9f, g_night ? 0xFFFFE6A8 : 0xFF2A3440, g_night);
        pdecal(g, fc, vmul(n, -1), vmul(a, -1), 1.25f, -4.6f, 4.8f, y0, y0 + 0.9f, g_night ? 0xFFFFE6A8 : 0xFF2A3440, g_night);
      }
      for(int i = -1; i <= 1; i += 2)
        for(int j = -1; j <= 1; j += 2) pbox(g, at3(b, n, a, j * 1.1f, i * 3.4f, 0), n, a, 0.18f, 0.45f, 0, 0.9f, 0xFF18181C);
      break;
    }
    case K_CAB: {  // a parked cab: NY yellow, London black
      uint32_t c = (g_kit.flag[0] == 0xFF012169) ? 0xFF1A1A20 : 0xFFF2C21E;
      pbox(g, b, a, n, 2.3f, 0.9f, 0.3f, 1.0f, c);
      pbox(g, vadd(b, vmul(a, -0.2f)), a, n, 1.2f, 0.82f, 1.0f, 1.55f, shade(c, 0.95f));
      pdecal(g, vadd(b, vmul(a, -0.2f)), n, a, 0.82f, -1.1f, 0.8f, 1.05f, 1.45f, 0xFF283038, false);
      pdecal(g, vadd(b, vmul(a, -0.2f)), vmul(n, -1), vmul(a, -1), 0.82f, -0.8f, 1.1f, 1.05f, 1.45f, 0xFF283038, false);
      if(c != 0xFF1A1A20) pbox(g, vadd(b, vmul(a, -0.2f)), a, n, 0.3f, 0.15f, 1.55f, 1.75f, g_night ? 0xFFFFF0B0 : 0xFFF4F4F0);
      for(int i = -1; i <= 1; i += 2)
        for(int j = -1; j <= 1; j += 2) pbox(g, at3(b, n, a, j * 0.75f, i * 1.45f, 0), n, a, 0.16f, 0.32f, 0, 0.62f, 0xFF18181C);
      break;
    }
    case K_CABLECAR: {  // San Francisco's: cream and maroon, an open end, a roof
      pbox(g, b, a, n, 4.2f, 1.25f, 0.5f, 1.6f, 0xFF8A1E24);
      pbox(g, b, a, n, 4.2f, 1.2f, 1.6f, 3.0f, 0xFFF0E6C8);
      pbox(g, b, a, n, 4.4f, 1.35f, 3.0f, 3.3f, 0xFF6A1A1E);
      pdecal(g, b, n, a, 1.2f, -3.6f, 3.6f, 1.8f, 2.7f, g_night ? 0xFFFFE6A8 : 0xFF3A3A40, g_night);
      pdecal(g, b, vmul(n, -1), vmul(a, -1), 1.2f, -3.6f, 3.6f, 1.8f, 2.7f, g_night ? 0xFFFFE6A8 : 0xFF3A3A40, g_night);
      break;
    }
    case K_TORII: {  // vermilion: two posts, the tie beam, the curved top beam
      uint32_t ver = 0xFFE0401E, blk = 0xFF1C1C20;
      float H = 5.2f * s, hw = 2.0f * s;
      for(int sg = -1; sg <= 1; sg += 2) {
        pcyl(g, vadd(b, vmul(a, sg * hw)), kUp, 0.24f * s, 0.2f * s, H, 6, ver);
        pbox(g, vadd(b, vmul(a, sg * hw)), n, a, 0.32f * s, 0.32f * s, 0, 0.5f, blk);
      }
      pbox(g, b, n, a, 0.18f * s, hw + 0.4f, H * 0.72f, H * 0.72f + 0.35f * s, ver);
      pbox(g, b, n, a, 0.28f * s, hw + 0.9f, H, H + 0.35f * s, ver);
      pbox(g, b, n, a, 0.3f * s, hw + 1.1f, H + 0.35f * s, H + 0.6f * s, blk);
      for(int sg = -1; sg <= 1; sg += 2) {  // the upturned ends
        V3 e = vadd(b, vmul(a, sg * (hw + 1.1f)));
        V3 c[3] = {at3(e, n, a, 0, 0, H + 0.35f * s), at3(e, n, a, 0, sg * 0.6f, H + 0.9f * s), at3(e, n, a, 0, 0, H + 0.6f * s)};
        ptri2(g, c[0], c[1], c[2], blk);
      }
      break;
    }
    case K_LANTERN: {  // a stone lantern: foot, pillar, the firebox (lit), a roof, a knob
      uint32_t st = 0xFF9A968C;
      pbox(g, b, n, a, 0.45f, 0.45f, 0, 0.3f, st);
      pcyl(g, vadd(b, vmul(kUp, 0.3f)), kUp, 0.16f, 0.16f, 1.0f, 6, st);
      pbox(g, b, n, a, 0.36f, 0.36f, 1.3f, 1.4f, st);
      g_emit = g_night;
      pbox(g, b, n, a, 0.26f, 0.26f, 1.4f, 1.85f, g_night ? 0xFFFFC870 : 0xFF6A665E);
      g_emit = false;
      phip(g, b, n, a, 0.55f, 0.55f, 1.85f, 2.25f, 0, st);
      glow(g, vadd(b, vmul(kUp, 1.6f)), 2.0f, 0xA0FFC870);
      break;
    }
    case K_NOBORI: {  // a tall vertical banner on its pole
      static const uint32_t kNob[4] = {0xFF2AA84A, 0xFFE0301E, 0xFF2A5AD8, 0xFFF0C030};
      pcyl(g, b, kUp, 0.05f, 0.05f, 4.6f, 4, 0xFF8A8A8A);
      V3 c = vadd(b, vmul(a, 0.4f));
      pbox(g, c, n, a, 0.03f, 0.34f, 1.2f, 4.4f, kNob[h & 3]);
      pdecal(g, c, n, a, 0.03f, -0.12f, 0.12f, 1.6f, 4.0f, 0xFFF4F4F0, false);
      break;
    }
    case K_MOOSE: case K_DISTBOARD: case K_AUTOBAHN: {  // signs facing the oncoming driver
      V3 face = vmul(a, -1), across = n;
      if(p->kind == K_AUTOBAHN) {
        for(int sg = -1; sg <= 1; sg += 2) pcyl(g, vadd(b, vmul(across, sg * 1.4f)), kUp, 0.07f, 0.07f, 2.4f, 4, 0xFF8E949C);
        V3 pb = vadd(b, vmul(kUp, 2.2f));
        pbox(g, pb, face, across, 0.05f, 1.9f, 0, 1.8f, 0xFF1E4EA8);
        pdecal(g, pb, face, across, 0.05f, -1.75f, 1.75f, 0.12f, 0.2f, 0xFFF4F4F4, false);
        pdecal(g, pb, face, across, 0.05f, -1.4f, 0.6f, 1.05f, 1.35f, 0xFFF4F4F4, false);
        pdecal(g, pb, face, across, 0.05f, -1.4f, 0.2f, 0.5f, 0.8f, 0xFFF4F4F4, false);
        pdecal(g, pb, face, across, 0.05f, 0.9f, 1.4f, 0.4f, 1.45f, 0xFFF4F4F4, false);
      } else if(p->kind == K_DISTBOARD) {  // 3, 2, 1 bars before a corner
        pcyl(g, b, kUp, 0.06f, 0.06f, 1.2f, 4, 0xFF8E949C);
        V3 pb = vadd(b, vmul(kUp, 1.1f));
        pbox(g, pb, face, across, 0.04f, 0.6f, 0, 1.6f, 0xFFF4F4F0);
        int bars = 1 + (int) (h % 3);
        for(int i = 0; i < bars; i++) pdecal(g, pb, face, across, 0.04f, -0.45f, 0.45f, 0.2f + i * 0.45f, 0.45f + i * 0.45f, 0xFF18181C, false);
      } else {  // the moose (elk) warning triangle
        pcyl(g, b, kUp, 0.05f, 0.05f, 2.1f, 4, 0xFF8E949C);
        V3 c = vadd(vadd(b, vmul(kUp, 2.0f)), vmul(face, 0.06f));
        V3 o[3] = {at3(c, face, across, 0, -0.7f, 0), at3(c, face, across, 0, 0.7f, 0), at3(c, face, across, 0, 0, 1.25f)};
        g_pc = vadd(c, vmul(face, -1));
        pface(g, o, 3, 0xFFD0202A, false);
        V3 c2 = vadd(c, vmul(face, 0.02f));
        V3 in[3] = {at3(c2, face, across, 0, -0.52f, 0.12f), at3(c2, face, across, 0, 0.52f, 0.12f), at3(c2, face, across, 0, 0, 1.02f)};
        pface(g, in, 3, 0xFFF5C518, false);
        V3 c3 = vadd(c2, vmul(face, 0.02f));
        pdecal(g, c3, face, across, 0, -0.22f, 0.16f, 0.36f, 0.52f, 0xFF141414, false);   // body
        pdecal(g, c3, face, across, 0, 0.12f, 0.3f, 0.5f, 0.62f, 0xFF141414, false);      // head
        pdecal(g, c3, face, across, 0, -0.2f, -0.14f, 0.2f, 0.37f, 0xFF141414, false);    // legs
        pdecal(g, c3, face, across, 0, 0.08f, 0.14f, 0.2f, 0.37f, 0xFF141414, false);
        pdecal(g, c3, face, across, 0, 0.1f, 0.3f, 0.64f, 0.7f, 0xFF141414, false);       // antlers
      }
      break;
    }
    case K_LOGS:
      for(int i = 0; i < 4; i++) {
        V3 c = at3(b, n, a, (i - 1.5f) * 0.5f + (i == 3 ? 0.75f : 0), -2.2f, 0.25f + (i == 3 ? 0.45f : 0));
        if(i == 3) c = at3(b, n, a, 0, -2.2f, 0.7f);
        pcyl(g, c, a, 0.25f, 0.25f, 4.4f, 6, 0xFF8A6A48);
      }
      break;
    case K_MAYPOLE: {  // Bavarian: a tall blue-and-white spiral pole with guild signs
      float H = 22;
      for(int i = 0; i < 8; i++) pcyl(g, vadd(b, vmul(kUp, i * H / 8)), kUp, 0.22f - i * 0.015f, 0.2f - i * 0.015f, H / 8, 5, i & 1 ? 0xFF2E6AC8 : 0xFFF4F4F4);
      for(int i = 0; i < 3; i++) pbox(g, vadd(b, vmul(kUp, 9 + i * 3.5f)), n, a, 0.04f, 1.4f, 0, 0.8f, 0xFFE8E0C8);
      pcyl(g, vadd(b, vmul(kUp, H)), kUp, 1.0f, 0, 1.6f, 6, 0xFF2E6A2E);
      break;
    }
    case K_MORRIS:  // the Paris advertising column: green, a poster band, an onion top
      pcyl(g, b, kUp, 0.6f, 0.6f, 0.4f, 7, 0xFF2E5A3E);
      pcyl(g, vadd(b, vmul(kUp, 0.4f)), kUp, 0.55f, 0.55f, 2.2f, 7, 0xFFE8DCC0);
      pcyl(g, vadd(b, vmul(kUp, 2.6f)), kUp, 0.62f, 0.4f, 0.5f, 7, 0xFF2E5A3E);
      pcyl(g, vadd(b, vmul(kUp, 3.1f)), kUp, 0.4f, 0, 0.6f, 7, 0xFF2E5A3E);
      break;
    case K_LLAMA: {
      uint32_t c = (h & 1) ? 0xFFF0E8DC : 0xFF8A6A4A;
      pbox(g, vadd(b, vmul(kUp, 0.9f)), a, n, 0.65f, 0.3f, 0, 0.6f, c);
      pbox(g, vadd(b, vmul(a, 0.55f)), a, n, 0.12f, 0.12f, 1.3f, 2.2f, c);
      pbox(g, vadd(b, vmul(a, 0.7f)), a, n, 0.22f, 0.12f, 2.05f, 2.3f, c);
      for(int i = -1; i <= 1; i += 2)
        for(int j = -1; j <= 1; j += 2) pbox(g, at3(b, n, a, j * 0.18f, i * 0.5f, 0), n, a, 0.06f, 0.06f, 0, 0.95f, shade(c, 0.85f));
      break;
    }
    case K_STONES:  // sarsens: uprights and a lintel or two
      for(int i = -1; i <= 1; i++) {
        V3 c = vadd(b, vmul(a, i * 3.0f));
        pbox(g, vadd(c, vmul(a, -0.8f)), n, a, 0.5f, 0.55f, 0, 4.2f, 0xFF8E8C88);
        pbox(g, vadd(c, vmul(a, 0.8f)), n, a, 0.5f, 0.55f, 0, 4.0f, 0xFF96948E);
        if(i != (int) (h % 3) - 1) pbox(g, c, n, a, 0.5f, 1.5f, 4.0f, 4.9f, 0xFF8A8884);
      }
      break;
    case K_WALL:  // dry stone wall
      pbox(g, b, n, a, 0.4f, 4.3f, 0, 1.0f, (g_kit.flag[0] == 0xFF009246) ? 0xFFC8B89A : 0xFF8E8A80);
      pbox(g, b, n, a, 0.32f, 4.3f, 1.0f, 1.15f, 0xFF7A766E);
      break;
    case K_COLUMN: {  // a ruined column: drum, capital (some broken off)
      float H = (h & 1) ? 6.0f : 2.5f + (h >> 2 & 3);
      pbox(g, b, n, a, 0.7f, 0.7f, 0, 0.4f, 0xFFD8CCB0);
      pcyl(g, vadd(b, vmul(kUp, 0.4f)), kUp, 0.48f, 0.42f, H, 7, 0xFFE4D8BC);
      if(h & 1) pbox(g, b, n, a, 0.65f, 0.65f, H + 0.4f, H + 0.85f, 0xFFD8CCB0);
      break;
    }
    case K_VENDING:  // a Japanese vending machine: white, a lit front of bottles
      pbox(g, b, n, a, 0.4f, 0.5f, 0, 1.85f, (h & 1) ? 0xFFE8E8EC : 0xFFD02A2A);
      pdecal(g, b, n, a, 0.4f, -0.42f, 0.42f, 0.95f, 1.75f, g_night ? 0xFFE8F4FF : 0xFFB8D0E0, g_night);
      glow(g, vadd(b, vadd(vmul(n, 0.8f), vmul(kUp, 1.3f))), 2.4f, 0x90D0E8FF);
      break;
    case K_FLAG: {  // a pole with the country's flag (or a chequered one), 6 x 3 cells
      pcyl(g, b, kUp, 0.06f, 0.05f, 7.5f, 4, 0xFFC8CCD0);
      V3 t = vadd(b, vmul(kUp, 7.3f));
      bool cheq = (h & 3) == 0;
      int course = g_pcourse & 31, country = course >> 2;
      for(int j = 0; j < 3; j++)
        for(int i = 0; i < 6; i++) {
          uint32_t col;
          if(cheq) col = (i + j) & 1 ? 0xFF141414 : 0xFFF4F4F4;
          else switch(country) {
            case 0: col = i < 2 && j < 2 ? 0xFF3C3B6E : j == 1 ? 0xFFFFFFFF : 0xFFB22234; break;  // stars and stripes
            case 1: col = (i == 2 || i == 3) && j == 1 ? 0xFFFFDF00 : 0xFF009C3B; break;
            case 2: col = (i == 2 || i == 3) && j == 1 ? 0xFFBC002D : 0xFFFFFFFF; break;
            case 3: col = j == 0 ? 0xFF101010 : j == 1 ? 0xFFDD0000 : 0xFFFFCE00; break;
            case 4: {
              static const uint32_t kNord[4][2] = {{0xFF006AA7, 0xFFFECC00}, {0xFFC8102E, 0xFFFFFFFF}, {0xFFFFFFFF, 0xFF003580}, {0xFFBA0C2F, 0xFF00205B}};
              col = (i == 1 || j == 1) ? kNord[course & 3][1] : kNord[course & 3][0];
              break;
            }
            case 5: col = i < 2 ? 0xFF0055A4 : i < 4 ? 0xFFFFFFFF : 0xFFEF4135; break;
            case 6: col = i < 2 ? 0xFF009246 : i < 4 ? 0xFFFFFFFF : 0xFFCE2B37; break;
            default:
              if(course == 30) col = (i == 2 * j || i == 2 * j + 1 || i == 5 - 2 * j || i == 4 - 2 * j) ? 0xFFFFFFFF : 0xFF005EB8;  // the saltire
              else col = (i == 2 || i == 3 || j == 1) ? 0xFFC8102E : 0xFF012169;
              break;
          }
          V3 c0 = vadd(t, vadd(vmul(a, 0.3f * i), vmul(kUp, -0.4f * j - 0.03f * i)));
          V3 c1 = vadd(c0, vadd(vmul(a, 0.3f), vmul(kUp, -0.03f))), d = vmul(kUp, -0.4f);
          ptri2(g, c0, c1, vadd(c1, d), col);
          ptri2(g, c0, vadd(c1, d), vadd(c0, d), col);
        }
      break;
    }
    case K_GRANDSTAND: {  // stepped seats full of people, a roof on posts, a sponsor band
      for(int t = 0; t < 5; t++)
        pbox(g, vadd(b, vmul(n, -t * 1.4f)), n, a, 0.7f, 9.5f, 0, 0.8f + t * 0.8f, t & 1 ? 0xFFB8BCC4 : 0xFFA8ACB4);
      for(int t = 0; t < 5; t++)  // the crowd: rows of people in colourful clothes
          for(int i = 0; i < 8; i += 3) {
            uint32_t hh = hash32(h + (uint32_t) (t * 8 + i) * 2654435761u);
            static const uint32_t kCrowd[6] = {0xFFE8402A, 0xFF2A6AD8, 0xFFF0C030, 0xFFF4F4F0, 0xFF2AA860, 0xFF303440};
            pbox(g, vadd(b, vadd(vmul(n, -t * 1.4f), vmul(a, -6.4f + i * 2.15f))), n, a, 0.3f, 2.9f, 0.8f + t * 0.8f,
                 1.25f + t * 0.8f, mix(kCrowd[hh % 6], 0xFF8A8E96, 0.35f));
          }
      for(int i = -1; i <= 1; i += 2) pbox(g, at3(b, n, a, -6.5f, i * 9.0f, 0), n, a, 0.15f, 0.15f, 0, 7.5f, 0xFF8E949C);
      pbox(g, vadd(b, vmul(n, -3.5f)), n, a, 4.5f, 10, 7.5f, 7.9f, 0xFFE8ECEE);
      pdecal(g, vadd(b, vmul(n, -3.5f)), n, a, 4.5f, -9.8f, 9.8f, 7.5f, 7.9f, g_kit.flag[2] == 0xFFFFFFFF ? g_kit.flag[1] : g_kit.flag[2], false);
      break;
    }
    case K_PITS: {  // the pit building: white, garage doors, a roof band in the country's colour
      pbox(g, b, n, a, 3.5f, 9.8f, 0, 4.2f, 0xFFF0F2F4);
      pbox(g, b, n, a, 3.7f, 10.0f, 4.2f, 4.8f, g_kit.flag[0] == 0xFFFFFFFF ? g_kit.flag[1] : g_kit.flag[0]);
      for(int i = -3; i <= 3; i++) pdecal(g, b, n, a, 3.5f, i * 2.7f - 1.1f, i * 2.7f + 1.1f, 0, 3.0f, g_night ? 0xFFFFF0D0 : 0xFF3A3E46, g_night);
      break;
    }
    case K_OVERPASS: {  // a concrete bridge over the road: deck, parapets, piers outside the road
      float span = 13;
      uint32_t con = 0xFFB4B0A8;
      pbox(g, b, a, n, 4.0f, span, 6.2f, 7.4f, con);
      pbox(g, vadd(b, vmul(a, 3.8f)), a, n, 0.2f, span, 7.4f, 8.3f, shade(con, 0.95f));
      pbox(g, vadd(b, vmul(a, -3.8f)), a, n, 0.2f, span, 7.4f, 8.3f, shade(con, 0.95f));
      for(int sg = -1; sg <= 1; sg += 2) pbox(g, vadd(b, vmul(n, sg * 10.5f)), a, n, 2.6f, 0.6f, 0, 6.2f, shade(con, 0.9f));
      pdecal(g, b, vmul(a, -1), n, 4.0f, -span, span, 6.7f, 7.0f, g_kit.flag[0] == 0xFFFFFFFF ? g_kit.flag[1] : g_kit.flag[0], false);
      break;
    }
    case K_WELCOME: {  // "Welcome to Fabulous Las Vegas": a white diamond on two posts, a gold star
      V3 face = vnorm(vsub(vmul(n, 0.5f), vmul(a, 0.87f))), across = vnorm(vadd(vmul(a, 0.5f), vmul(n, 0.87f)));
      for(int sg = -1; sg <= 1; sg += 2) pbox(g, vadd(b, vmul(across, sg * 1.2f)), face, across, 0.1f, 0.1f, 0, 3.0f, 0xFFE8E8EC);
      V3 c = vadd(b, vmul(kUp, 4.6f));
      V3 d[4] = {at3(c, face, across, 0, -3.0f, 0), at3(c, face, across, 0, 0, -1.9f), at3(c, face, across, 0, 3.0f, 0), at3(c, face, across, 0, 0, 1.9f)};
      g_pc = vsub(c, face);
      pface(g, d, 4, 0xFFF6F6F2, false);
      V3 c2 = vadd(c, vmul(face, 0.03f));
      pdecal(g, c2, face, across, 0, -2.0f, 2.0f, -0.55f, 0.5f, 0xFFD82030, g_night);  // the red lettering band
      pdecal(g, c2, face, across, 0, -1.2f, 1.2f, -1.1f, -0.75f, 0xFF2A5AB8, false);  // "LAS VEGAS" in blue
      V3 st = vadd(c2, vmul(kUp, 1.25f));
      for(int k = 0; k < 5; k++) {
        float a0 = PI / 2 + k * 2 * PI / 5;
        V3 tip = at3(st, face, across, 0, cosf(a0) * 0.5f, sinf(a0) * 0.5f);
        V3 l0 = at3(st, face, across, 0, cosf(a0 + 0.6f) * 0.2f, sinf(a0 + 0.6f) * 0.2f);
        V3 l1 = at3(st, face, across, 0, cosf(a0 - 0.6f) * 0.2f, sinf(a0 - 0.6f) * 0.2f);
        ptri2(g, l0, tip, l1, 0xFFE8B830);
      }
      ptri2(g, at3(st, face, across, 0, -0.2f, -0.1f), at3(st, face, across, 0, 0.2f, -0.1f), at3(st, face, across, 0, 0, 0.2f), 0xFFE8B830);
      if(g_night) glow(g, c, 9, 0x70FFF0D0);
      break;
    }
    default: break;
  }
}

// --- The landmark's place in the world (QA-007) ------------------------------------------------
// Each city's landmark stands in one fixed direction, at the horizon, like the sun: chosen once
// per course as the way the road looks most often (the main straight; the grid, where the
// player waits, counts more), turned a little to the pits' side so the cars ahead are not
// standing on it. The props are then kept out of its line of sight: one that would cover the
// landmark's key part (the letters, the spire) from a place on the road where the landmark is
// on the screen is not placed - the vista is left open, as level designers keep a sightline to
// a far landmark. (Before, its place followed the game's sky scroll and had no direction in the
// world, so nothing could be kept out of its way: LA's towers hid the hillside sign.)
#define LM_HANG 0.1857f  // the landmark's height H as an angle (64 x 0.875 panorama px over f = 301.5 px)
#define LM_SUGARLOAF_OFF 1.75f  // Rio: Sugarloaf this many H beside Corcovado, away from the road
static float g_lm_az, g_lm_t0, g_lm_t1, g_lm_e0;  // its direction; the key part's sides (tangents) and foot
static int g_lm_side = 1;  // the side of the road it stands (+1 the right, looking that way)
static bool g_lm_on;
static V3* g_lm_eye;
static int g_lm_neye, g_lm_cap;

static float wrap_pi(float a) {
  while(a > PI) a -= 2 * PI;
  while(a < -PI) a += 2 * PI;
  return a;
}

static void lm_place(const TgView3D* v, const TgTrack3D* tr, int stand) {
  int nseg = tr->tables.segments, kind = city_landmark(v->course);
  g_lm_on = false, g_lm_neye = 0;
  if(kind == 0 || nseg <= 0) return;
  float* hd = malloc(sizeof(float) * (size_t) nseg);
  if(!hd) return;
  for(int s = 0; s < nseg; s++) {
    TgTrackFrame f = tg_track3d_frame(tr, s * (float) TG_TRACK3D_SEG_UNITS + 16, 0);
    hd[s] = atan2f(f.forward.z, f.forward.x);
  }
  // The direction the road looks most: each segment votes for its heading (+-17 deg), the
  // start straight (the grid, where the player waits) twelve times.
  float best = 0, bestScore = -1;
  for(int k = 0; k < 720; k++) {
    float A = k * (2 * PI / 720) - PI, score = 0;
    for(int s = 0; s < nseg; s++) {
      int ds = s < nseg / 2 ? s : nseg - s;
      float d = fabsf(wrap_pi(A - hd[s])), wv = 1 - d / 0.3f;
      if(wv > 0) score += wv * (ds < 60 ? 12 : 1);
    }
    if(score > bestScore) bestScore = score, best = A;
  }
  float x0, x1, y0;
  landmark_box(kind, &x0, &x1, &y0);
  // On the pits' side of the road (the grandstands, on the other, would stand in front of it
  // on the grid), its key part starting ~6 deg off the road's vanishing point: clear of the
  // cars ahead and of most of the START gantry over the road.
  TgTrackFrame f0 = tg_track3d_frame(tr, 16, 0);
  float rx = -sinf(best), rz = cosf(best);  // the screen's right, looking that way
  int side = (rx * f0.right.x + rz * f0.right.z) * -stand > 0 ? 1 : -1;
  g_lm_az = side > 0 ? best + 0.1f - x0 * LM_HANG : best - 0.1f - x1 * LM_HANG;
  g_lm_side = side;
  g_lm_t0 = tanf(x0 * LM_HANG), g_lm_t1 = tanf(x1 * LM_HANG), g_lm_e0 = tanf(y0 * LM_HANG);
  if(getenv("TG_LM_INFO")) {  // (look development: the light a modelled landmark gets, for tools/make_landmarks.py)
    V3 L = vnorm(kLight);
    fprintf(stderr, "landmark course %d kind %d az %.4f side %d light %.3f %.3f %.3f\n", v->course, kind, g_lm_az, side,
            -L.x * sinf(g_lm_az) + L.z * cosf(g_lm_az), L.y, -(L.x * cosf(g_lm_az) + L.z * sinf(g_lm_az)));
  }
  // The places it is seen from: the road wherever the key part is within a wide picture, at
  // its middle and towards both edges (the camera follows the car across the road; on the
  // grid it stands behind a car's slot).
  float mid = g_lm_az + (x0 + x1) * 0.5f * LM_HANG;
  for(int s = 0; s < nseg; s += 2) {
    if(fabsf(wrap_pi(mid - hd[s])) > 0.95f) continue;
    for(int k = -1; k <= 1; k++) {
      if(g_lm_neye == g_lm_cap) {
        int cap = g_lm_cap ? g_lm_cap * 2 : 1024;
        V3* ne = realloc(g_lm_eye, sizeof(V3) * (size_t) cap);
        if(!ne) break;
        g_lm_eye = ne, g_lm_cap = cap;
      }
      TgTrackFrame f = tg_track3d_frame(tr, s * (float) TG_TRACK3D_SEG_UNITS + 16, k * 0.6f * TG_TRACK3D_SIDE_EDGE);
      g_lm_eye[g_lm_neye++] = vadd(tv(f.pos), v3(0, 2.9f, 0));
    }
  }
  free(hd);
  g_lm_on = true;
}

// Would a prop at p (its foot), top metres tall and rad wide, cover the landmark's key part?
static bool lm_blocks(V3 p, float top, float rad) {
  if(!g_lm_on) return false;
  float c = cosf(g_lm_az), s = sinf(g_lm_az);
  for(int i = 0; i < g_lm_neye; i++) {
    V3 e = g_lm_eye[i];
    float dx = p.x - e.x, dz = p.z - e.z, along = dx * c + dz * s;
    if(along < 4 || along > 440) continue;  // (beyond the props' draw distance)
    if((p.y + top - e.y) / along <= g_lm_e0) continue;  // under it
    float lat = -dx * s + dz * c;
    if((lat + rad) / along < g_lm_t0 || (lat - rad) / along > g_lm_t1) continue;  // beside it
    return true;
  }
  return false;
}

static bool tall_kind(int k);

// A prop's height (m), as build_prop makes it (a little over: a mast, a roof).
static float prop_top(int kind, float s, uint32_t h) {
  switch(kind) {
    case K_TOWER: return (35 + (h >> 4) % 45) * s + 16;
    case K_CASINO: return (28 + (h >> 4) % 20) * s + 6;
    case K_BLOCK: return (4 + (h >> 4) % 5) * 3.1f + 2.5f;
    case K_HAUSSMANN: return 22;
    case K_TERRACE: return 15;
    case K_KAPOK: return 32 * s;
    case K_MESA: return 30 * s;
    case K_FACTORY: case K_COOLING: return 32 * s;
    case K_TALL_PALM: return 22 * s;
    case K_POPLAR: return 17 * s;
    case K_PAGODA: case K_STEPPYR: return 20 * s;
    default: break;
  }
  if(tall_kind(kind)) return 13 * s;
  return kFoot[kind].cls == 2 ? 25 * s : kFoot[kind].cls == 1 ? 10 * s : 4 * s;
}

// --- Placement, once per course ------------------------------------------------------------

#define OCC_CELL 2.0f
static Prop* g_props;
static int g_nprops, g_capprops;
static int g_pfirst[TG_TRACK3D_MAX_SEGMENTS + 1];
static uint32_t g_pkey;
static int g_pcourse = -1, g_pnseg;
static int g_pver;  // the pit lane's layout the props were placed round (view3d_pits.inc)
static FadeMem* g_pmem[2];
static uint8_t* g_occ;
static int g_ow, g_oh;
static float g_ox, g_oz;

static bool occ_rect(float x, float z, V3 n, V3 a, float hn, float ha, bool mark) {
  float rx = fabsf(n.x) * hn + fabsf(a.x) * ha, rz = fabsf(n.z) * hn + fabsf(a.z) * ha;
  int x0 = (int) floorf((x - rx - g_ox) / OCC_CELL), x1 = (int) floorf((x + rx - g_ox) / OCC_CELL);
  int z0 = (int) floorf((z - rz - g_oz) / OCC_CELL), z1 = (int) floorf((z + rz - g_oz) / OCC_CELL);
  if(x0 < 0 || z0 < 0 || x1 >= g_ow || z1 >= g_oh) return false;
  for(int cz = z0; cz <= z1; cz++)
    for(int cx = x0; cx <= x1; cx++) {
      float px = g_ox + (cx + 0.5f) * OCC_CELL - x, pz = g_oz + (cz + 0.5f) * OCC_CELL - z;
      float dn = px * n.x + pz * n.z, da = px * a.x + pz * a.z;
      if(fabsf(dn) > hn + OCC_CELL * 0.5f || fabsf(da) > ha + OCC_CELL * 0.5f) continue;
      if(mark) g_occ[cz * g_ow + cx] = 1;
      else if(g_occ[cz * g_ow + cx]) return false;
    }
  return true;
}

static void occ_disc(float x, float z, float r) { occ_rect(x, z, v3(1, 0, 0), v3(0, 0, 1), r, r, true); }

static float stage_noise(int seg, int nseg, float period, uint32_t seed) {
  int cells = (int) (nseg / period + 0.5f);
  if(cells < 1) cells = 1;
  float t = (float) seg * cells / nseg;
  int i0 = (int) floorf(t);
  float f = t - i0;
  f = f * f * (3 - 2 * f);
  float n0 = (hash32(seed ^ (uint32_t) (i0 % cells) * 0x85EBCA6Bu) & 1023) / 1023.0f;
  float n1 = (hash32(seed ^ (uint32_t) ((i0 + 1) % cells) * 0x85EBCA6Bu) & 1023) / 1023.0f;
  return n0 + (n1 - n0) * f;
}

static void add_prop(const TgTrack3D* tr, int seg, float along, float sideM, int kind, float s, uint32_t h, bool check) {
  if(kind <= K_NONE || kind >= K_COUNT) return;
  float hw = tr->scale.half_width;
  TgTrackFrame fr = tg_track3d_frame(tr, seg * (float) TG_TRACK3D_SEG_UNITS + along, sideM / hw * TG_TRACK3D_SIDE_EDGE);
  V3 a = vnorm(v3(fr.forward.x, 0, fr.forward.z)), r = vnorm(v3(fr.right.x, 0, fr.right.z));
  V3 n = sideM > 0 ? vmul(r, -1) : r;  // toward the road
  if(kind == K_OVERPASS) n = r;
  float sk = kind >= K_HOUSE && kind <= K_STEPPYR ? fmaxf(s, 1.0f) : s;
  float fa = kFoot[kind].ha * sk, fn = kFoot[kind].hn * sk;
  if(check && !occ_rect(fr.pos.x, fr.pos.z, n, a, fn, fa, false)) return;
  if(check && kind < K_GRANDSTAND && lm_blocks(tv(fr.pos), prop_top(kind, s, h), fmaxf(fa, fn))) return;  // (QA-007)
  occ_rect(fr.pos.x, fr.pos.z, n, a, fn, fa, true);
  if(g_nprops == g_capprops) {
    int cap = g_capprops ? g_capprops * 2 : 4096;
    Prop* np = realloc(g_props, sizeof(Prop) * (size_t) cap);
    if(!np) return;
    g_props = np, g_capprops = cap;
  }
  Prop* p = &g_props[g_nprops++];
  p->p = tv(fr.pos), p->n = n, p->a = a, p->s = s, p->h = h, p->seg = (uint16_t) seg, p->kind = (uint8_t) kind, p->cls = kFoot[kind].cls;
}

static bool tall_kind(int k) {
  return (k >= K_PALM && k <= K_KAPOK) || k == K_JOSHUA || k == K_SAGUARO || k == K_NEON || k == K_MAYPOLE || k == K_TORII ||
         k == K_BILLBOARD;
}

static int list_len(const uint8_t* l) {
  int c = 0;
  while(c < KL && l[c]) c++;
  return c;
}

static int cmp_prop(const void* x, const void* y) {
  const Prop *a = x, *b = y;
  if(a->seg != b->seg) return a->seg < b->seg ? -1 : 1;
  return a->h < b->h ? -1 : a->h > b->h;  // (a total order: the same every run)
}

static void stage_build(const TgView3D* v, const TgTrack3D* tr) {
  int nseg = tr->tables.segments;
  if(nseg <= 0) return;
  if(g_pkey == tr->key && g_pcourse == v->course && g_pnseg == nseg && g_pver == g_pit_ver) return;
  g_pkey = tr->key, g_pcourse = v->course, g_pnseg = nseg;
  kit_for(v->course, &g_kit);
  g_nprops = 0;
  // The grid: the road (with its kerbs and marker posts), the game's own roadside objects.
  float margin = 140;
  g_ox = tr->min.x - margin, g_oz = tr->min.z - margin;
  g_ow = (int) ((tr->max.x - tr->min.x + 2 * margin) / OCC_CELL) + 1;
  g_oh = (int) ((tr->max.z - tr->min.z + 2 * margin) / OCC_CELL) + 1;
  if(g_ow > 2048) g_ow = 2048;
  if(g_oh > 2048) g_oh = 2048;
  free(g_occ);
  g_occ = calloc((size_t) g_ow * g_oh, 1);
  if(!g_occ) return;
  float hw = tr->scale.half_width;
  for(int s = 0; s < nseg; s++)
    for(int k = 0; k < 2; k++) {
      TgTrackFrame c = tg_track3d_frame(tr, s * (float) TG_TRACK3D_SEG_UNITS + k * 16, 0);
      occ_disc(c.pos.x, c.pos.z, hw * 1.45f + 0.8f);
    }
  // The pit lane, its garages and the ground behind them (view3d_pits.inc): kept clear.
  {
    const PitLane* pl = pit_layout(tr);
    g_pver = g_pit_ver;
    if(pl->on)
      for(int r = PR_ENT0 - 4; r <= PR_EXIT1 + 4; r++)
        for(int k = 0; k < 2; k++) {
          float outer = fmaxf(pit_outer(r + k * 0.5f), pl->kerb) + (r >= PR_GAR0 - 2 && r <= PR_GAR1 + 2 ? 14.0f : 3.0f);
          float mid = (pl->kerb + outer) / 2, rad = (outer - pl->kerb) / 2 + 1;
          for(float x = mid - rad * 0.5f; x <= mid + rad * 0.5f + 0.01f; x += rad * 0.5f) {
            TgTrackFrame c = tg_track3d_frame(tr, ((pl->S + r) % nseg + nseg) % nseg * (float) TG_TRACK3D_SEG_UNITS + k * 16,
                                              pl->sgn * x / hw * TG_TRACK3D_SIDE_EDGE);
            occ_disc(c.pos.x, c.pos.z, rad * 0.6f + 1.2f);
          }
        }
  }
  for(int s = 0; s < nseg; s++) {
    uint16_t ow = tr->tables.object[s];
    int key = art_key(ow);
    if(key < 0 || key >= 256) continue;
    float side = object_side(ow);
    TgTrackFrame of = tg_track3d_frame(tr, s * (float) TG_TRACK3D_SEG_UNITS + 16, side * TG_TRACK3D_SIDE_EDGE);
    occ_disc(of.pos.x, of.pos.z, 2.2f);
  }
  // The learned scenery's places (scenery(): its positions don't depend on what it shows).
  for(int s = 0; s < nseg; s++)
    for(int sideK = -1; sideK <= 1; sideK += 2) {
      uint32_t h = hash32((uint32_t) s * 0x9E3779B1u ^ (sideK > 0 ? 0xA511E9B3u : 0x63D83595u));
      if((h & 255) > 70) continue;
      float out = 2.6f + ((h >> 13) & 255) / 255.0f * 8.0f, along = ((h >> 21) & 31) / 31.0f * TG_TRACK3D_SEG_UNITS;
      TgTrackFrame of = tg_track3d_frame(tr, s * (float) TG_TRACK3D_SEG_UNITS + along, sideK * out * TG_TRACK3D_SIDE_EDGE);
      occ_disc(of.pos.x, of.pos.z, 1.5f);
    }
  uint32_t cseed = hash32(0x57A6E + (uint32_t) v->course * 7919u);
  // The stadium at the start/finish: grandstands on one side, the pits on the other, flags.
  int stand = (cseed & 1) ? -1 : 1;
  lm_place(v, tr, stand);  // the landmark's direction, and the places it is seen from
  for(int d = -44; d <= 16; d++) {
    int s = ((d % nseg) + nseg) % nseg;
    if(d % 8 == 0 && d <= 8) add_prop(tr, s, 0, stand * 17.0f, K_GRANDSTAND, 1, hash32(cseed + (uint32_t) d), true);
    if(d % 8 == 4 && d >= -28 && d <= 4) add_prop(tr, s, 0, -stand * 13.5f, K_PITS, 1, hash32(cseed + (uint32_t) d * 3), true);
    if(d % 3 == 0) add_prop(tr, s, 8, (d & 1 ? stand : -stand) * 7.6f, K_FLAG, 1, hash32(cseed ^ (uint32_t) d), true);
  }
  if((v->course & 31) == 0) add_prop(tr, 40 % nseg, 0, -stand * 9.5f, K_WELCOME, 1, cseed, true);  // Las Vegas' sign
  // Distance boards before the big corners (3, 2, 1 bars at 120, 80 and 40 m).
  {
    int mx = 1;
    for(int s = 0; s < nseg; s++) mx = abs(tr->tables.curve[s]) > mx ? abs(tr->tables.curve[s]) : mx;
    int thr = mx * 45 / 100, last = -1000;
    for(int s = 0; s < nseg; s++) {
      if(abs(tr->tables.curve[s]) < thr) continue;
      bool straight = true;
      for(int d = 1; d <= 40 && straight; d++) straight = abs(tr->tables.curve[(s - d + nseg) % nseg]) < thr / 2;
      if(!straight || s - last < 80) continue;
      last = s;
      for(int i = 1; i <= 3; i++) {
        int bs = ((s - i * 16) % nseg + nseg) % nseg;
        for(int sg = -1; sg <= 1; sg += 2) add_prop(tr, bs, 0, sg * 6.6f, K_DISTBOARD, 1, (uint32_t) (i - 1), true);
      }
    }
  }
  // An overpass on the city courses: on the straightest stretch half way round.
  if(g_kit.overpass) {
    int best = -1;
    long long bestSum = 1LL << 40;
    for(int s = nseg / 3; s < nseg * 2 / 3; s++) {
      long long sum = 0;
      for(int d = -12; d <= 12; d++) sum += abs(tr->tables.curve[(s + d) % nseg]);
      if(sum < bestSum) bestSum = sum, best = s;
    }
    if(best >= 0) {
      add_prop(tr, best, 0, 0, K_OVERPASS, 1, cseed, false);
      for(int sg = -1; sg <= 1; sg += 2) {
        TgTrackFrame c = tg_track3d_frame(tr, best * (float) TG_TRACK3D_SEG_UNITS, sg * 10.5f / hw * TG_TRACK3D_SIDE_EDGE);
        occ_disc(c.pos.x, c.pos.z, 3.5f);
      }
    }
  }
  // The course's own things, layer by layer.
  static const struct { float in, out; int step; } kLay[3] = {{7.4f, 10.5f, 2}, {16.0f, 34.0f, 2}, {38.0f, 85.0f, 3}};
  for(int s = 0; s < nseg; s++) {
    int ds = s < nseg / 2 ? s : nseg - s;  // from the start line
    bool town = ds < 60 || stage_noise(s, nseg, 110, cseed) < g_kit.urban;
    bool row = g_kit.row && !town && stage_noise(s, nseg, 70, cseed ^ 0xBEEF) < g_kit.rowShare;
    for(int lay = 0; lay < 3; lay++) {
      if(s % kLay[lay].step) continue;
      for(int sideK = -1; sideK <= 1; sideK += 2) {
        uint32_t h = hash32(cseed ^ (uint32_t) s * 0x9E3779B1u ^ (uint32_t) (lay * 2 + (sideK > 0)) * 0x85EBCA6Bu);
        float u = (h & 1023) / 1023.0f;
        if(lay == 0 && row && s % 4 == 0) {
          add_prop(tr, s, 0, sideK * 16.5f, g_kit.row, 0.95f + ((h >> 10) & 7) * 0.02f, h, true);
          continue;
        }
        const uint8_t* list = town ? g_kit.town[lay] : g_kit.open[lay];
        float dens = town ? g_kit.dt[lay] : g_kit.dopen[lay];
        int cnt = list_len(list);
        if(!cnt || u >= dens) continue;
        int kind = list[(h >> 10) % cnt];
        float out = kLay[lay].in + ((h >> 16) & 255) / 255.0f * (kLay[lay].out - kLay[lay].in);
        out += kFoot[kind].hn * (lay > 0 ? 1.0f : 0.0f);
        // Tall things stand back from the road: close by they sweep across the screen's edge
        // in a frame or two at speed (QA: popping); the near row is for low street furniture.
        if(lay == 0 && kind != K_LAMP && tall_kind(kind)) out = 16.0f + ((h >> 16) & 255) / 255.0f * 4.0f;
        float sc = 0.85f + ((h >> 24) & 15) / 15.0f * 0.35f;
        add_prop(tr, s, ((h >> 28) & 3) * 8.0f, sideK * out, kind, sc, hash32(h), true);
      }
    }
  }
  // Index by segment (they were added in no strict order: sort by segment, then by order added).
  qsort(g_props, (size_t) g_nprops, sizeof(Prop), cmp_prop);
  for(int k = 0; k < 2; k++) {  // each prop's fade, eased (fade_mem), per view
    free(g_pmem[k]);
    g_pmem[k] = calloc((size_t) (g_nprops ? g_nprops : 1), sizeof(FadeMem));
  }
  int j = 0;
  for(int s = 0; s <= nseg; s++) {
    while(j < g_nprops && g_props[j].seg < s) j++;
    g_pfirst[s] = j;
  }
  g_pfirst[nseg] = g_nprops;
}

// The props of segment seg: culled at the screen's sides, faded out at their draw distance.
static void stage_props(Gather* g, const TgView3D* v, const TgTrack3D* tr, int seg) {
  if(!g_props || g_pcourse != v->course || g_pnseg != tr->tables.segments || seg < 0 || seg >= g_pnseg) return;
  float tanx = g->cam->w * 0.5f / g->cam->f;
  for(int i = g_pfirst[seg]; i < g_pfirst[seg + 1]; i++) {
    const Prop* p = &g_props[i];
    V3 c = to_cam(g->cam, p->p);
    float far = kClassFar[p->cls], rad = (kFoot[p->kind].ha + kFoot[p->kind].hn) * p->s + 4;
    if(c.z < -rad || c.z > far) continue;
    if(fabsf(c.x) > c.z * tanx + rad) continue;
    float fa = (far - c.z) / 60.0f;
    // Near the camera (the car run wide onto the verge, a big tree by the road): faded out
    // rather than a wall filling the picture (near_fade).
    float foot = fmaxf(kFoot[p->kind].ha, kFoot[p->kind].hn) * p->s;
    bool tall = tall_kind(p->kind);
    float near = near_fade(g->cam, p->p, foot, tall || p->cls >= 1 ? 40.0f : 1.5f, true);  // (a house as much as a tree: QA ghosts)
    if(near < fa) fa = near;
    fa = fa > 1 ? 1 : fa < 0 ? 0 : fa;
    float alpha = g_pmem[g_view_k] ? fade_mem(&g_pmem[g_view_k][i], fa) : fa;  // eased: no pop (QA-009)
    // (skipped without touching g_alpha: left near 0 by a faded prop, it made every later prop
    // and every car invisible - the player's car vanished for a frame, Copenhagen 4492)
    if(alpha < 0.02f) continue;
    g_alpha = alpha;
    g_pd = c.z;
    begin_item(g, c.z - STAND_BIAS);
    build_prop(g, p);
    g_alpha = 1;
  }
}
