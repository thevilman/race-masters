cmake_minimum_required(VERSION 3.20)
project(topgear_pc C)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# The project's name, in one place: window titles, message boxes and the title screen
# (TG_APP_NAME), and the programs' and settings folder's file names (TG_EXE_NAME,
# TG_EXE_NAME-launcher). src/brand.h.in passes them to the code; tools/gen_launcher_art.py
# draws the launcher's logo from TG_APP_NAME's line here (run it again after a change; until
# then the launcher shows the name as plain text). Override with -DTG_APP_NAME=... if needed.
set(TG_APP_NAME "Race Masters" CACHE STRING "The project's display name")
set(TG_EXE_NAME "race-masters" CACHE STRING "The programs' file names (and the settings folder's)")
configure_file(src/brand.h.in ${CMAKE_BINARY_DIR}/gen/brand.h @ONLY)

option(TG_BUILD_FRONTEND "Build the SDL3 game executable" ON)
option(TG_FETCH_SDL "Download and statically link SDL3 if it is not installed" ON)
set(TG_TEST_ROM "" CACHE FILEPATH "Your Top Gear (USA) ROM, for ROM-dependent tests")

if(MSVC)
  add_compile_options(/W3 /utf-8)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
  add_compile_options(-Wall)
endif()

# Reference emulator (LakeSnes, MIT). Runs the original ROM: today as the whole game,
# later as the fallback for untranslated routines and as the lockstep referee.
file(GLOB LAKESNES_SOURCES third_party/lakesnes/*.c)
add_library(lakesnes STATIC ${LAKESNES_SOURCES})
target_include_directories(lakesnes PUBLIC third_party/lakesnes)
if(NOT MSVC)
  target_compile_options(lakesnes PRIVATE -w)
endif()

add_library(tgcore STATIC
  src/coverage.c
  src/fiber.c
  src/game.c
  src/input_script.c
  src/lockstep.c
  src/port/port.c
  src/port/cars.c
  src/port/decompress.c
  src/port/drive.c
  src/port/render.c
  src/port/ghost.c
  src/port/setup.c
  src/port/tilecache.c
  src/port/hud.c
  src/port/menu_leaves.c
  src/port/menu_text.c
  src/port/menu_results.c
  src/port/menu_flow.c
  src/port/messages.c
  src/port/irq.c
  src/port/program.c
  src/port/race.c
  src/port/registry.c
  src/port/sprites.c
  src/port/uploads.c
  src/port/visible.c
  src/port/waits.c
  src/select_input.c
  src/country_input.c
  src/rom.c
  src/sha256.c
  src/now_ms.c
)
target_include_directories(tgcore PUBLIC src ${CMAKE_BINARY_DIR}/gen)
target_link_libraries(tgcore PUBLIC lakesnes)
if(NOT WIN32)
  target_link_libraries(tgcore PUBLIC m)
endif()

# 3D mod (docs/3D-MOD.md): the course as a 3D circuit, the race as a scene, and the race
# drawn in 3D from the game's own state, camera and sprites. Reads the game only.
add_library(tgmod3d STATIC
  src/mod3d/track3d.c
  src/mod3d/scene.c
  src/mod3d/view3d.c
  src/mod3d/logo_vector.c
  src/mod3d/transition.c
)
target_include_directories(tgmod3d PUBLIC src/mod3d)
target_link_libraries(tgmod3d PUBLIC tgcore)

# Modern music (docs/AUDIO.md): the player's remade songs streamed in place of the sound
# chip's music. stb_vorbis (MIT or public domain) decodes the Ogg Vorbis files.
add_library(stb_vorbis STATIC third_party/stb_vorbis/stb_vorbis.c)
target_include_directories(stb_vorbis PUBLIC third_party/stb_vorbis)
if(NOT MSVC)
  target_compile_options(stb_vorbis PRIVATE -w)
endif()
add_library(tgaudio STATIC src/audio/music.c)
target_include_directories(tgaudio PUBLIC src/audio)
target_link_libraries(tgaudio PUBLIC tgcore stb_vorbis)

add_executable(tg_headless tools/tg_headless.c)
target_link_libraries(tg_headless PRIVATE tgcore tgmod3d tgaudio)

# tools/audio's note extractor, which the modern-music generator (tools/music) runs on the
# player's ROM: built here so the release downloads carry it ready-made (music-tools/), and
# players need no compiler. Our code and LakeSnes only; it holds nothing of the game.
add_executable(tg_audio tools/audio/tg_audio.c src/input_script.c)
target_include_directories(tg_audio PRIVATE src)
target_link_libraries(tg_audio PRIVATE lakesnes)
if(NOT WIN32)
  target_link_libraries(tg_audio PRIVATE m)
endif()
if(MINGW)
  target_link_options(tg_audio PRIVATE -static -static-libgcc)
endif()

# The launcher's "Build music" state, settings and progress lines (no SDL, so tested here).
add_library(tgmusicbuild STATIC src/music_build.c src/unzip.c)
target_include_directories(tgmusicbuild PUBLIC src)

if(TG_BUILD_FRONTEND)
  find_package(SDL3 CONFIG QUIET)
  if(NOT SDL3_FOUND)
    if(NOT TG_FETCH_SDL)
      message(FATAL_ERROR "SDL3 not found; install it or enable TG_FETCH_SDL")
    endif()
    include(FetchContent)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL3
      URL https://github.com/libsdl-org/SDL/releases/download/release-3.2.24/SDL3-3.2.24.tar.gz
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(SDL3)
  endif()
  # The game and the launcher: targets topgear and topgear-launcher, files named by TG_EXE_NAME.
  add_executable(topgear WIN32 src/main.c src/gl3d.c)
  target_link_libraries(topgear PRIVATE tgcore tgmod3d tgaudio SDL3::SDL3)
  set_target_properties(topgear PROPERTIES OUTPUT_NAME ${TG_EXE_NAME})
  # The launcher: choose the ROM and the options, then start the game.
  add_executable(topgear-launcher WIN32 src/launcher.c src/rom.c src/sha256.c)
  target_include_directories(topgear-launcher PRIVATE src ${CMAKE_BINARY_DIR}/gen)
  target_link_libraries(topgear-launcher PRIVATE tgmusicbuild SDL3::SDL3)
  set_target_properties(topgear-launcher PROPERTIES OUTPUT_NAME ${TG_EXE_NAME}-launcher)
  # The launcher's icon (assets/launcher-icon.ico), logo and Start word (src/launcher_art.h):
  # tools/gen_launcher_art.py.
  set(TG_LAUNCHER_ICON "${CMAKE_SOURCE_DIR}/assets/launcher-icon.ico" CACHE FILEPATH "Icon (.ico) for the launcher")
  if(WIN32 AND EXISTS "${TG_LAUNCHER_ICON}")
    file(WRITE ${CMAKE_BINARY_DIR}/launcher_icon.rc "1 ICON \"${TG_LAUNCHER_ICON}\"\n")
    target_sources(topgear-launcher PRIVATE ${CMAKE_BINARY_DIR}/launcher_icon.rc)
    set_source_files_properties(${CMAKE_BINARY_DIR}/launcher_icon.rc PROPERTIES OBJECT_DEPENDS "${TG_LAUNCHER_ICON}")
  endif()
  if(MINGW)
    target_link_options(topgear PRIVATE -static -static-libgcc)
    target_link_options(topgear-launcher PRIVATE -static -static-libgcc)
  endif()
endif()

enable_testing()
add_executable(test_core tests/test_core.c)
target_link_libraries(test_core PRIVATE tgcore tgmod3d)
add_test(NAME core COMMAND test_core)
# The launcher's "Build music": progress lines, the build's states, launcher.txt, consent.txt.
add_executable(test_music_build tests/test_music_build.c)
target_include_directories(test_music_build PRIVATE tests)
target_link_libraries(test_music_build PRIVATE tgmusicbuild)
if(NOT WIN32)
  target_link_libraries(test_music_build PRIVATE m)
endif()
add_test(NAME music_build COMMAND test_music_build ${CMAKE_SOURCE_DIR}/tools/music/tgmusic/data/consent.txt)
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_FOUND)
  # The repository's coverage file keeps no bytes of the game's code (tools/tgcov.py).
  add_test(NAME coverage_public COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/tgcov.py
    --check ${CMAKE_SOURCE_DIR}/disasm/coverage.tgcov)
  # The music generator's own parts that need no packages: progress lines, cancelling,
  # resumable downloads, the pins (requirements.txt, the launcher's Python), consent.txt.
  # (-X utf8: as the generator always runs, PYFLAGS; on Windows the files are otherwise the locale's)
  add_test(NAME music_tools COMMAND ${Python3_EXECUTABLE} -B -X utf8 -m unittest discover -s ${CMAKE_SOURCE_DIR}/tools/music/tests -p "test_*.py")
endif()
if(TG_TEST_ROM)
  # Recording tests run with --port verify: every call to a translated routine is checked
  # against the original (src/lockstep.h), and any difference fails the test.
  add_test(NAME rom_boot COMMAND test_core --rom ${TG_TEST_ROM})
  # Two minutes of the untouched attract sequence, ending in the split-screen demo race.
  # Pinned hashes catch any change in game behaviour or rendering.
  add_test(NAME attract_demo COMMAND tg_headless --rom ${TG_TEST_ROM} --port verify --frames 7200
    --expect-wram eb074942bd3f6b5efa7402979c2d6e56e4373739112cc711cfe448f4a8291cba
    --expect-frame fc8fc8246cc971806acc2607897b4d99093d1b48884d9aa682271d1036f81eca)
  # Menus, a full scripted 1-player race and the results screen.
  add_test(NAME race_1p COMMAND tg_headless --rom ${TG_TEST_ROM} --port verify --frames 11000
    --input ${CMAKE_SOURCE_DIR}/tests/inputs/race_1p.txt
    --expect-wram 08f94c7b56cb0b8ac33b9c557c5c0fdd25c7ab80ee479d374a45a6253b5b2a8a
    --expect-frame 94766e6c901d25c1fdb0f14cf24d3b8c78be1e25318146f0f3905f6d904537c9)
  # Chris's 15-minute session recorded on Windows (the original project's recordings). Slow, so labelled "long":
  # ctest -L long to run it, ctest -LE long to skip it.
  add_test(NAME chris_session_01 COMMAND tg_headless --rom ${TG_TEST_ROM} --port verify --frames 54853
    --input ${CMAKE_SOURCE_DIR}/tests/inputs/chris_session_01.txt
    --expect-wram 4ff4ffc25d58655992eaa28e1c404ff7f3a73e815903af5e62361bd135f76cd8)
  set_tests_properties(chris_session_01 PROPERTIES LABELS long TIMEOUT 1800)
  # Chris's later sessions, recorded on Windows: a 5-minute one with a race, and an
  # 85-minute one with about twenty races.
  add_test(NAME chris_session_02 COMMAND tg_headless --rom ${TG_TEST_ROM} --port verify --frames 17047
    --input ${CMAKE_SOURCE_DIR}/tests/inputs/chris_session_02.txt
    --expect-wram a7367821082e64bf84eee43439ddcc1dc23bea23a99d868e25e1acecd714b908)
  add_test(NAME chris_session_03 COMMAND tg_headless --rom ${TG_TEST_ROM} --port verify --frames 304552
    --input ${CMAKE_SOURCE_DIR}/tests/inputs/chris_session_03.txt
    --expect-wram bc050a7e61dd3e96502383ceb71374eba269ceea30be1c99ccbc89c726722056)
  set_tests_properties(chris_session_02 PROPERTIES LABELS long TIMEOUT 1800)
  set_tests_properties(chris_session_03 PROPERTIES LABELS long TIMEOUT 7200)
  # Scripted recordings that reach the other game modes: a 2-player race, and the first
  # race of each country unlocked with its password, and a race on each harder skill level,
  # and the race's car contacts. Each takes a while, so labelled "long".
  foreach(rec IN ITEMS
      "race_2p|14000|af867184fd380ccfc5327252cdb15d44ee7b6ce23c06b7aaa668bac6bf5f8592|ca02793076b8845454d3cc4d949bda5bc49fe16d063e310331f86318ed644ba7"
      "country_s_america|9762|299d275f2492573fc26ca109689ecd4422d5e7e7bdeb4997fca83cef3332213f|af587dd1ef071b5d55271b8b80aa345a5a30715d6f0bcff295af4bfa6c138d78"
      "country_japan|10242|89cf229c909c271462147467800dfa3ddebb4fa75f871746fa0c91ba6e8d8a20|c9225e4c522e860785001a9310eb0197082544ea3ccff226332aea03f721beb6"
      "country_germany|10402|85168033966361e7cb9a92e44f65e31dbaa0c9165935e93317ba93970151a4a2|b87c5f9d370169fe3f4bf8d42be01d63e7d20f9ce7727036c401ca4600f03af3"
      "country_scandinavia|10602|00697c0c070b2350a196a6a60be443d7f20743104dfc7de3265e79e9c6499754|c81af3c92b1ebaefd42d36be63e180765e2f96e4acb483ed51aa3e8e9facf4bd"
      "country_france|10562|8cd9a88843f7c213dfb411ee86b251c072163cbe24dcb3bca46c9e0ae6e085e6|2cbbce0c1b6fc7de74bf4e59a1e598ceb17949066756fc32f645a36c389eeee6"
      "country_italy|10522|4402fc56aed633d28a8d45a181ed0a634d4ee3b5680e9f7805ca377877d2e86a|0cd8c2b0ed333398a9122c100d3de1cfa42fc5fab9974732045cc66e39373d29"
      "country_uk|10242|b9dc85e182f4d1d6c0c33712e8405030e380c15526a343071e7b02214cf091d5|d783491c8f629c6fdea76648c4d59b31692a5fa7dcfe9a427c3b067e459824ba"
      "skill_professional|11432|71dd6a46a8b858aeb363354fee2df784005d192a38f58c6448af9377560897bb|94766e6c901d25c1fdb0f14cf24d3b8c78be1e25318146f0f3905f6d904537c9"
      "skill_championship|11540|f9a5a1c7b220f84e72bbf16cb828384de6c29c0fd4ffa5dcf330db7412cee631|94766e6c901d25c1fdb0f14cf24d3b8c78be1e25318146f0f3905f6d904537c9"
      # Cars from behind ramming a car stopped on the grid (five rear pushes), and the
      # curves pushing an unsteered car off the road.
      "rear_push|9000|a7d236e61a2a77219df636aa69c219eb0e6bf28ba6d9b959e92c814f88c8b7df|882981678f6028facbe41d0ab19d99e3c5138575f0263d459d5a8eba7ff7cae2"
      "curve_push|9000|6a3146d7e4b2f47ae4f1ff35434e157c49c10371cec634a877f79a83a84d1a85|54f7771f41f8206b67760d26c397330cdd60435eed3cf34ca88f2c830d1b6814"
    )
    string(REPLACE "|" ";" rec "${rec}")
    list(GET rec 0 name)
    list(GET rec 1 frames)
    list(GET rec 2 wram)
    list(GET rec 3 frame)
    add_test(NAME ${name} COMMAND tg_headless --rom ${TG_TEST_ROM} --port verify --frames ${frames}
      --input ${CMAKE_SOURCE_DIR}/tests/inputs/${name}.txt --expect-wram ${wram} --expect-frame ${frame})
    set_tests_properties(${name} PROPERTIES LABELS long TIMEOUT 1800)
  endforeach()
  # Disassemble, reassemble with ca65/ld65 and require a byte-identical ROM.
  find_program(CA65 ca65)
  find_program(LD65 ld65)
  # The race loop in C against the emulated one, allowing for its faster loading.
  if(Python3_FOUND)
    add_test(NAME race_loop_c COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/shift_compare.py
      --bin $<TARGET_FILE:tg_headless> --rom ${TG_TEST_ROM} --input ${CMAKE_SOURCE_DIR}/tests/inputs/race_2p.txt --frames 14000)
    set_tests_properties(race_loop_c PROPERTIES LABELS long TIMEOUT 3600)
  endif()
  if(Python3_FOUND AND CA65 AND LD65)
    add_test(NAME disasm_roundtrip COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_disasm.py
      --rom ${TG_TEST_ROM} --out ${CMAKE_BINARY_DIR}/disasm)
  endif()
endif()
