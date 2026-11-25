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
# The app logic shared by the Windows and Linux builds (Android builds the same files: android/app/src/main/cpp)
set(APP_SHARED
    src/app/app.cpp
    src/app/app_achievements.cpp
    src/app/achievements.cpp
    src/app/app_menus.cpp
    src/app/launcher.cpp
    src/app/jukebox.cpp
    src/app/session.cpp
    src/app/soundtrack.cpp
    src/app/settings.cpp
    src/app/palette.cpp
    src/app/audio_mixer.cpp
    src/app/romfind.cpp
    src/app/unzip.cpp
    src/app/png.cpp
    src/app/rewind.cpp
    src/app/ui.cpp
    src/app/imageload.cpp
    src/app/updatecheck.cpp
    src/app/achexport.cpp
)

if(WIN32)
add_executable(DisneyClassics WIN32
    ${APP_SHARED}
    src/app/main.cpp
    src/app/app_win.cpp
    src/app/sys_win.cpp
    src/app/monitors.cpp
    src/platform/video.cpp
    src/platform/slang.cpp
    src/platform/audio.cpp
    src/platform/input.cpp
    res/app.rc
)
target_include_directories(DisneyClassics PRIVATE third_party/stb)   # (stb_image: the player's cover pictures)
target_link_libraries(DisneyClassics PRIVATE mdcore
    user32 gdi32 ole32 d3d11 dxgi d3dcompiler xaudio2 xinput dinput8 dxguid dwrite winmm dwmapi shell32)
set_target_properties(DisneyClassics PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin-debug)
# Slang shader presets: librashader.dll (loaded at run time when present, third_party/librashader) and the
# bundled presets (a few of the libretro slang-shaders, with their licences) next to the exe
if(EXISTS ${CMAKE_SOURCE_DIR}/third_party/librashader/librashader.dll)
    add_custom_command(TARGET DisneyClassics POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/third_party/librashader/librashader.dll $<TARGET_FILE_DIR:DisneyClassics>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/third_party/librashader/LICENSE.md $<TARGET_FILE_DIR:DisneyClassics>/librashader-LICENSE.md)
endif()
if(EXISTS ${CMAKE_SOURCE_DIR}/third_party/slang-shaders)
    add_custom_command(TARGET DisneyClassics POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/third_party/slang-shaders $<TARGET_FILE_DIR:DisneyClassics>/shaders)
endif()

elseif(UNIX AND NOT APPLE)
# Linux (desktop and Steam Deck): SDL2 for the window, sound and controllers, OpenGL ES 3 for drawing (the Android
# build's layer, src/platform/sdl), FreeType for the menus' text. linux/build_appimage.sh packs an AppImage.
find_package(SDL2 REQUIRED)
find_package(Freetype REQUIRED)
find_library(GLESV2_LIBRARY NAMES GLESv2 REQUIRED)
find_package(Threads REQUIRED)
add_executable(DisneyClassics
    ${APP_SHARED}
    src/app/app_linux.cpp
    src/app/sys_linux.cpp
    src/app/sys_posix.cpp
    src/platform/sdl/video_gles.cpp
    src/platform/sdl/audio_sdl.cpp
    src/platform/sdl/input_sdl.cpp
    src/platform/sdl/glyphs_ft.cpp
)
target_include_directories(DisneyClassics PRIVATE third_party/stb)
if(TARGET SDL2::SDL2)
    target_link_libraries(DisneyClassics PRIVATE SDL2::SDL2)
else()
    target_include_directories(DisneyClassics PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(DisneyClassics PRIVATE ${SDL2_LIBRARIES})
endif()
target_link_libraries(DisneyClassics PRIVATE mdcore Freetype::Freetype ${GLESV2_LIBRARY} Threads::Threads ${CMAKE_DL_LIBS})
target_compile_options(DisneyClassics PRIVATE -Wno-unused-parameter -Wno-sign-compare)
set_target_properties(DisneyClassics PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endif()

# The Theatre menu style's title font (Luckiest Guy, Apache 2.0) next to the program
add_custom_command(TARGET DisneyClassics POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/third_party/fonts/LuckiestGuy-Regular.ttf $<TARGET_FILE_DIR:DisneyClassics>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/third_party/fonts/LICENSE.txt $<TARGET_FILE_DIR:DisneyClassics>/LuckiestGuy-LICENSE.txt)

# ---- analysis DLL for tools/mdt.py (Windows) -------------------------------------
if(WIN32)
add_library(mdtool SHARED src/tools/mdtool.cpp)
target_link_libraries(mdtool PRIVATE mdcore)
set_target_properties(mdtool PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/tools
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/tools)
endif()
