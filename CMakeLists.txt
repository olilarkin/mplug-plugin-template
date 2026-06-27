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

# Include the MPlug plugin macros (provides mplug_add_plugin)
include(MPlugPlugin)

# ----------------------------------------------------------------------------
# Platform-specific editor sources
#
# The plugin GUI is a CHOC WebView (HTML/JS/CSS). Embedding the webview into a
# plugin host's parent window is currently implemented for macOS only; the
# Windows/Linux stubs return nullptr so hosts fall back to their generic
# parameter UI. The standalone App provides a full cross-platform webview UI on
# all three platforms regardless.
# ----------------------------------------------------------------------------
# ----------------------------------------------------------------------------
# Native plugin formats (skipped when building WebAssembly/WCLAP via Emscripten).
# Produces: MyPluginClap, MyPluginVST3, MyPluginAUv2 (macOS), MyPluginApp,
#           MyPluginCli, MyPluginPython
# ----------------------------------------------------------------------------
if(NOT EMSCRIPTEN)
  if(APPLE)
    set(MY_PLUGIN_EDITOR_SOURCES src/my_plugin_editor_mac.mm)
    find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
    find_library(WEBKIT_FRAMEWORK WebKit REQUIRED)
    set(MY_PLUGIN_EDITOR_LIBS ${COCOA_FRAMEWORK} ${WEBKIT_FRAMEWORK})
  elseif(WIN32)
    set(MY_PLUGIN_EDITOR_SOURCES src/my_plugin_editor_win.cpp)
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
