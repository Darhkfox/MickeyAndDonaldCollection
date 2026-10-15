# Native part of the Android build: SDL2 from source (libSDL2.so) and the port itself (libmain.so, loaded by
# SDLActivity, which calls its SDL_main). The emulation core, game modules and app logic are the Windows
# build's; only the platform layer differs (src/platform/android, src/app/*_android.cpp).
cmake_minimum_required(VERSION 3.18)
project(MickeyDonaldAndroid C CXX)

get_filename_component(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../../.. ABSOLUTE)
set(SDL_DIR ${ROOT}/third_party/SDL2-2.32.10-src)
if(NOT EXISTS ${SDL_DIR}/CMakeLists.txt)
  message(FATAL_ERROR "SDL2 source not found in ${SDL_DIR}: run android/fetch_sdl.ps1 first")
endif()

set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
add_subdirectory(${SDL_DIR} ${CMAKE_BINARY_DIR}/SDL)

set(SRC ${ROOT}/src)
add_library(main SHARED
  ${SRC}/core/m68k.cpp
  ${SRC}/core/z80.cpp
  ${SRC}/core/vdp.cpp
  ${SRC}/core/ym2612.cpp
  ${SRC}/core/psg.cpp
  ${SRC}/core/md.cpp
  ${SRC}/game/common.cpp
  ${SRC}/game/coi.cpp
  ${SRC}/game/qs.cpp
  ${SRC}/game/woi.cpp
  ${SRC}/app/app.cpp
  ${SRC}/app/app_android.cpp
  ${SRC}/app/app_menus.cpp
  ${SRC}/app/launcher.cpp
  ${SRC}/app/jukebox.cpp
  ${SRC}/app/session.cpp
  ${SRC}/app/settings.cpp
  ${SRC}/app/palette.cpp
  ${SRC}/app/audio_mixer.cpp
  ${SRC}/app/romfind.cpp
  ${SRC}/app/unzip.cpp
  ${SRC}/app/sys_android.cpp
  ${SRC}/app/png.cpp
  ${SRC}/app/rewind.cpp
  ${SRC}/app/ui.cpp
  ${SRC}/app/touch.cpp
  ${SRC}/platform/android/video_gles.cpp
  ${SRC}/platform/android/audio_sdl.cpp
  ${SRC}/platform/android/input_sdl.cpp
)
set_target_properties(main PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_include_directories(main PRIVATE ${SRC})
# (the emulation core needs optimising even in debug builds to reach full speed)
target_compile_options(main PRIVATE -O2 -Wno-unused-parameter -Wno-sign-compare)
target_link_libraries(main PRIVATE SDL2 GLESv3 android log)
