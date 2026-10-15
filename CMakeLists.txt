cmake_minimum_required(VERSION 3.20)
project(MickeyDonaldPort CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")   # static CRT: no redistributable needed

if(MSVC)
    add_compile_options(/W3 /utf-8 /permissive- /MP)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
    string(APPEND CMAKE_CXX_FLAGS_RELEASE " /O2 /Oi /GL")
    string(APPEND CMAKE_EXE_LINKER_FLAGS_RELEASE " /LTCG")
    string(APPEND CMAKE_SHARED_LINKER_FLAGS_RELEASE " /LTCG")
endif()

# ---- emulation core -----------------------------------------------------------
add_library(mdcore STATIC
    src/core/m68k.cpp
    src/core/z80.cpp
    src/core/vdp.cpp
    src/core/ym2612.cpp
    src/core/psg.cpp
    src/core/md.cpp
    src/game/common.cpp
    src/game/coi.cpp
    src/game/qs.cpp
    src/game/woi.cpp
)
target_include_directories(mdcore PUBLIC src)

# ---- the game -----------------------------------------------------------------
add_executable(DisneyClassics WIN32
    src/app/main.cpp
    src/app/app.cpp
    src/app/app_menus.cpp
    src/app/launcher.cpp
    src/app/jukebox.cpp
    src/app/session.cpp
    src/app/settings.cpp
    src/app/palette.cpp
    src/app/audio_mixer.cpp
    src/app/romfind.cpp
    src/app/unzip.cpp
    src/app/monitors.cpp
    src/app/png.cpp
    src/app/rewind.cpp
    src/app/ui.cpp
    src/platform/video.cpp
    src/platform/audio.cpp
    src/platform/input.cpp
    res/app.rc
)
target_link_libraries(DisneyClassics PRIVATE mdcore
    user32 gdi32 ole32 d3d11 dxgi d3dcompiler xaudio2 xinput dinput8 dxguid dwrite winmm dwmapi shell32)
set_target_properties(DisneyClassics PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin-debug)

# ---- analysis DLL for tools/mdt.py ---------------------------------------------
add_library(mdtool SHARED src/tools/mdtool.cpp)
target_link_libraries(mdtool PRIVATE mdcore)
set_target_properties(mdtool PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/tools
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/tools)
