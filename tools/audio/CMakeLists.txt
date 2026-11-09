# The audio ripper, built on its own (it does not touch the game's build):
#   cmake -S tools/audio -B build-audio/audio -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-audio/audio -j8
cmake_minimum_required(VERSION 3.20)
project(tg_audio C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(TG_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
file(GLOB LAKESNES_SOURCES ${TG_ROOT}/third_party/lakesnes/*.c)
add_library(tga_lakesnes STATIC ${LAKESNES_SOURCES})
target_include_directories(tga_lakesnes PUBLIC ${TG_ROOT}/third_party/lakesnes)
target_compile_options(tga_lakesnes PRIVATE -w)

add_executable(tg_audio tg_audio.c ${TG_ROOT}/src/input_script.c)
target_include_directories(tg_audio PRIVATE ${TG_ROOT}/src)
target_compile_options(tg_audio PRIVATE -Wall)
target_link_libraries(tg_audio PRIVATE tga_lakesnes m)
