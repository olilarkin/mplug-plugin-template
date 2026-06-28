cmake_minimum_required(VERSION 3.21)

project(MyPlugin
  VERSION 1.0.0
  DESCRIPTION "MPlug plugin template"
  LANGUAGES CXX C
)

# Enable Objective-C++ on Apple platforms (needed for the macOS WebView editor)
if(APPLE)
  enable_language(OBJCXX)
endif()

# Default macOS build architecture (desktop only — iOS/visionOS set their own):
#   * CI (the CI env var is set, e.g. on GitHub Actions) → universal arm64;x86_64
#     so released artifacts run on both Apple Silicon and Intel.
#   * Local dev → just the host arch, for faster builds and smaller binaries.
# Pass -DCMAKE_OSX_ARCHITECTURES=... explicitly to override either default.
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES
   AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS"
   AND NOT CMAKE_SYSTEM_NAME STREQUAL "visionOS")
  if(DEFINED ENV{CI})
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
  else()
    set(CMAKE_OSX_ARCHITECTURES "${CMAKE_HOST_SYSTEM_PROCESSOR}")
  endif()
  message(STATUS "macOS architectures: ${CMAKE_OSX_ARCHITECTURES} "
                 "(override with -DCMAKE_OSX_ARCHITECTURES=...)")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ----------------------------------------------------------------------------
# Fetch the MPlug framework
#
# By default this clones MPlug from GitHub. For local development against a
# checkout of MPlug, configure with:
#   cmake -B build -DFETCHCONTENT_SOURCE_DIR_MPLUG=/path/to/mplug
# ----------------------------------------------------------------------------
include(FetchContent)

FetchContent_Declare(mplug
  GIT_REPOSITORY https://github.com/olilarkin/mplug.git
  GIT_TAG        main
  GIT_SHALLOW    TRUE
)

# Don't build MPlug's own examples/tests when consumed as a dependency
set(MPLUG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(MPLUG_BUILD_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(mplug)

# ----------------------------------------------------------------------------
# Native plugin formats (skipped when building WebAssembly/WCLAP via Emscripten).
# Produces: MyPluginClap, MyPluginVST3, MyPluginAUv2 (macOS), MyPluginApp,
#           MyPluginCli, MyPluginPython
#
# The plugin GUI is a CHOC WebView. Embedding it into a plugin host's window is
# implemented for macOS (WKWebView) and Windows (WebView2); the Linux stub
# returns nullptr (hosts show their generic parameter UI). The standalone App
# provides the full webview UI on all three platforms.
#
# The GUI lives in resources/web/editor.html and is embedded into the binary at
# configure time (see the editor-source section below) — a single cross-platform
# source of truth, with no resource files to bundle into each plugin format.
# ----------------------------------------------------------------------------
# iOS / visionOS (and their simulators) only support the AUv3 app-extension
# format; the desktop formats (App/VST3/CLI/...) can't configure there.
if(CMAKE_SYSTEM_NAME STREQUAL "iOS" OR CMAKE_SYSTEM_NAME STREQUAL "visionOS")
  set(MY_PLUGIN_APPLE_EMBEDDED TRUE)
endif()

if(MY_PLUGIN_APPLE_EMBEDDED)
  # ---- iOS / visionOS: AUv3 app extension only (requires -G Xcode) ----
  include(MPlugAUv3)

  mplug_add_auv3(MyPluginAUv3
    PLUGIN_CLASS MyPlugin
    PLUGIN_HEADER src/my_plugin.h
    IDENTIFIER "com.example.myplugin"
    SUBTYPE "mypl"
    MANUFACTURER "MPlg"
    TYPE "aufx"
    VERSION "1.0.0"
    DESCRIPTION "A simple gain plugin"
    SOURCES src/my_plugin.cpp
  )

elseif(NOT EMSCRIPTEN)
  # ---- Native desktop formats (macOS / Windows / Linux) ----
  # MPlug plugin macros (provides mplug_add_plugin). Included here so the native
  # VST3 SDK target isn't created for Emscripten/WCLAP builds.
  include(MPlugPlugin)

  # Embed resources/web/editor.html into a generated header at configure time.
  # The macOS and Windows editors #include "my_plugin_editor_html.h" and call
  # myplugin::editorHTML(). Re-running embed on every edit is wired through
  # CMAKE_CONFIGURE_DEPENDS, so saving the HTML triggers a re-configure (and thus
  # a regenerated header) on the next build — no manual cmake step needed.
  set(MY_PLUGIN_EDITOR_HTML     ${CMAKE_CURRENT_SOURCE_DIR}/resources/web/editor.html)
  set(MY_PLUGIN_GENERATED_DIR   ${CMAKE_CURRENT_BINARY_DIR}/generated)
  set(MY_PLUGIN_EDITOR_HTML_HDR ${MY_PLUGIN_GENERATED_DIR}/my_plugin_editor_html.h)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${MY_PLUGIN_EDITOR_HTML}
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedWebResource.cmake)
  file(MAKE_DIRECTORY ${MY_PLUGIN_GENERATED_DIR})
  execute_process(
    COMMAND ${CMAKE_COMMAND}
            -DINPUT=${MY_PLUGIN_EDITOR_HTML}
            -DOUTPUT=${MY_PLUGIN_EDITOR_HTML_HDR}
            -DSYMBOL=editorHTML
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedWebResource.cmake
    RESULT_VARIABLE MY_PLUGIN_EMBED_RESULT)
  if(NOT MY_PLUGIN_EMBED_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to embed ${MY_PLUGIN_EDITOR_HTML}")
  endif()
  # The generated header is reachable as #include "my_plugin_editor_html.h" from
  # the editor sources compiled into every plugin target created below.
  include_directories(${MY_PLUGIN_GENERATED_DIR})

  if(APPLE)
    set(MY_PLUGIN_EDITOR_SOURCES src/my_plugin_editor_mac.mm)
    find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
    find_library(WEBKIT_FRAMEWORK WebKit REQUIRED)
    set(MY_PLUGIN_EDITOR_LIBS ${COCOA_FRAMEWORK} ${WEBKIT_FRAMEWORK})
  elseif(WIN32)
    set(MY_PLUGIN_EDITOR_SOURCES src/my_plugin_editor_win.cpp)
    # No extra link libraries: choc's WebView2 backend calls CoInitialize (ole32,
    # part of the MSVC default library set) and loads WebView2Loader.dll /
    # shlwapi.dll dynamically, so nothing needs naming explicitly here.
    set(MY_PLUGIN_EDITOR_LIBS)
  else()
    set(MY_PLUGIN_EDITOR_SOURCES src/my_plugin_editor_generic.cpp)
    set(MY_PLUGIN_EDITOR_LIBS)
  endif()

  mplug_add_plugin(MyPlugin
    PLUGIN_CLASS MyPlugin
    PLUGIN_HEADER src/my_plugin.h
    PLUGIN_ID "com.example.myplugin"
    AU_SUBTYPE "mypl"
    AU_TYPE "aufx"
    CLAP_DESCRIPTION "A simple gain plugin"
    CLAP_FEATURES "audio-effect" "utility" "stereo"
    VST3_CATEGORY "Fx"
    APP_NAME "MyPlugin"
    SOURCES
      src/my_plugin.h
      src/my_plugin.cpp
    EDITOR_SOURCES
      ${MY_PLUGIN_EDITOR_SOURCES}
    LINK_LIBRARIES
      ${MY_PLUGIN_EDITOR_LIBS}
  )
endif()

# ----------------------------------------------------------------------------
# WebAssembly CLAP (WCLAP) — the plugin compiled to run in the browser.
# Enable with -DMPLUG_BUILD_WCLAP=ON via the Emscripten toolchain (emcmake).
# Headless: no editor sources.
# ----------------------------------------------------------------------------
if(MPLUG_BUILD_WCLAP)
  include(MPlugWClap)

  mplug_add_wclap(MyPluginWClap
    PLUGIN_CLASS MyPlugin
    PLUGIN_HEADER src/my_plugin.h
    PLUGIN_ID "com.example.myplugin"
    VENDOR "MPlug"
    VERSION "1.0.0"
    DESCRIPTION "A simple gain plugin"
    FEATURES "audio-effect" "utility" "stereo"
    SOURCES
      src/my_plugin.h
      src/my_plugin.cpp
  )
endif()
