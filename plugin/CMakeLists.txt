# VST3 instrument of the EPS-16 Plus emulator.
#
#   cmake -S plugin -B build-vst -DCMAKE_BUILD_TYPE=Release [-DVST3_SDK_ROOT=...] [-DSDL2_SOURCE_DIR=...]
#   cmake --build build-vst --config Release
#
# The VST3 SDK (MIT licence since 3.8) and SDL2 are downloaded automatically when no local
# copy is given. SDL2 is linked statically: it only draws the front panel off screen.
cmake_minimum_required(VERSION 3.25)

set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "" FORCE)
set(VSTGUI_STANDALONE OFF CACHE BOOL "" FORCE)
set(VSTGUI_STANDALONE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(VSTGUI_TOOLS OFF CACHE BOOL "" FORCE)
set(VSTGUI_DISABLE_UNITTESTS ON CACHE BOOL "" FORCE)
option(EPS16_VST_GUI "Build the plug-in editor (needs VSTGUI)" ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
  set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "" FORCE)
endif()
if(APPLE AND NOT DEFINED ENV{MACOSX_DEPLOYMENT_TARGET} AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
endif()

project(eps16vst VERSION 0.1.0 LANGUAGES C CXX)
if(APPLE)
  enable_language(OBJC)
  enable_language(OBJCXX)   # VST3 SDK + SDL use .m/.mm sources
  # new Apple SDKs deprecate std::wstring_convert, which VSTGUI uses (built with -Werror)
  add_compile_options(-Wno-error=deprecated-declarations -Wno-deprecated-declarations)
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
include(FetchContent)

# ---------------------------------------------------------------- VST3 SDK
set(VST3_SDK_ROOT "" CACHE PATH "Local copy of the VST3 SDK (empty = download)")
if(NOT EPS16_VST_GUI)
  set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "" FORCE)
endif()
if(VST3_SDK_ROOT STREQUAL "")
  FetchContent_Declare(vst3sdk
    GIT_REPOSITORY https://github.com/steinbergmedia/vst3sdk.git
    GIT_TAG v3.8.0_build_66
    GIT_SHALLOW TRUE
    GIT_SUBMODULES base cmake pluginterfaces public.sdk vstgui4)
  FetchContent_MakeAvailable(vst3sdk)
else()
  set(vst3sdk_SOURCE_DIR ${VST3_SDK_ROOT})
  add_subdirectory(${vst3sdk_SOURCE_DIR} ${PROJECT_BINARY_DIR}/vst3sdk)
endif()
set(SMTG_VSTGUI_ROOT "${vst3sdk_SOURCE_DIR}")
smtg_enable_vst3_sdk()

# ---------------------------------------------------------------- SDL2 (static, off-screen drawing)
if(EPS16_VST_GUI)
  set(SDL2_SOURCE_DIR "" CACHE PATH "Local SDL2 source tree (empty = download)")
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_STATIC_PIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST OFF CACHE BOOL "" FORCE)
  set(SDL2_DISABLE_SDL2MAIN ON CACHE BOOL "" FORCE)
  set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
  foreach(o SDL_AUDIO SDL_JOYSTICK SDL_HAPTIC SDL_HIDAPI SDL_SENSOR SDL_POWER SDL_OPENGL SDL_OPENGLES SDL_VULKAN)
    set(${o} OFF CACHE BOOL "" FORCE)
  endforeach()
  if(NOT APPLE)   # on macOS the Metal render driver needs the Metal view code: keep SDL's default
    set(SDL_METAL OFF CACHE BOOL "" FORCE)
  endif()
  if(SDL2_SOURCE_DIR STREQUAL "")
    FetchContent_Declare(sdl2 URL https://github.com/libsdl-org/SDL/releases/download/release-2.30.12/SDL2-2.30.12.tar.gz)
    FetchContent_MakeAvailable(sdl2)
  else()
    add_subdirectory(${SDL2_SOURCE_DIR} ${PROJECT_BINARY_DIR}/sdl2 EXCLUDE_FROM_ALL)
  endif()
endif()

# ---------------------------------------------------------------- emulator core
set(EMU ${CMAKE_CURRENT_SOURCE_DIR}/..)
add_library(eps16core_pic STATIC
  ${EMU}/musashi/m68kcpu.c ${EMU}/musashi/m68kops.c ${EMU}/musashi/m68kdasm.c ${EMU}/musashi/softfloat/softfloat.c
  ${EMU}/src/eps16.cpp ${EMU}/src/duart.cpp ${EMU}/src/wd1772.cpp ${EMU}/src/hd63450.cpp
  ${EMU}/src/es5505.cpp ${EMU}/src/es5510.cpp ${EMU}/src/panel.cpp ${EMU}/src/scsi.cpp ${EMU}/src/efe_disk.cpp ${EMU}/src/settings.cpp)
target_include_directories(eps16core_pic PUBLIC ${EMU}/src ${EMU}/musashi ${EMU}/musashi/softfloat)
set_target_properties(eps16core_pic PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------- the plug-in
set(target EPS16)
set(sources
  source/eps16_plugin.cpp source/eps16_plugin.h
  source/eps16_engine.cpp source/eps16_engine.h
  source/version.h
  ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.cpp)
if(EPS16_VST_GUI)
  list(APPEND sources source/eps16_editor.cpp source/eps16_editor.h
       ${EMU}/src/rack_offscreen.cpp ${EMU}/src/rack_ui.cpp ${EMU}/src/vfd.cpp)
endif()

smtg_add_vst3plugin(${target} ${sources})
target_include_directories(${target} PRIVATE source ${EMU}/src)
target_link_libraries(${target} PRIVATE sdk eps16core_pic)
if(EPS16_VST_GUI)
  target_compile_definitions(${target} PRIVATE EPS16_WITH_GUI=1)
  target_link_libraries(${target} PRIVATE vstgui_support SDL2::SDL2-static)
endif()
if(UNIX AND NOT APPLE)
  target_link_libraries(${target} PRIVATE ${CMAKE_DL_LIBS})
endif()
if(MINGW)
  # MSVC links these through #pragma comment(lib); MinGW needs them spelled out
  target_link_libraries(${target} PRIVATE d2d1 dwrite dwmapi shlwapi opengl32 windowscodecs
    ole32 oleaut32 uuid comdlg32 shell32 imm32 version winmm setupapi gdi32 user32)
  target_link_options(${target} PRIVATE -static)
endif()

# ROMs and the OS disk inside the bundle (Contents/Resources/roms)
file(GLOB rom_files ${EMU}/roms/*.bin ${EMU}/roms/*.img)
if(rom_files)
  smtg_target_add_plugin_resources(${target} RESOURCES ${rom_files} OUTPUT_SUBDIRECTORY roms)
else()
  message(STATUS "roms/ is empty: the plug-in will look for the ROMs in the user folder (.../Ensoniq/EPS16emu/roms)")
endif()

if(SMTG_MAC)
  smtg_target_set_bundle(${target}
    BUNDLE_IDENTIFIER "org.eps16emu.vst3"
    COMPANY_NAME "EPS16emu")
endif()

# ---------------------------------------------------------------- offline test host (optional)
option(EPS16_VST_TESTHOST "Build the minimal offline test host" OFF)
if(EPS16_VST_TESTHOST)
  add_executable(minihost test/minihost.cpp ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/module_linux.cpp)
  target_link_libraries(minihost PRIVATE sdk_hosting ${CMAKE_DL_LIBS})
endif()
