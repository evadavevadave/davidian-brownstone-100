# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Davidian
#
# VST3 (Windows, macOS, Linux) and Audio Unit (macOS) builds, made with the DISTRHO Plugin
# Framework (DPF, ISC licence), which is fetched at a pinned commit when configuring.
# The LV2 version is built by the Makefile / build.bat instead.
#
#   cmake -S . -B build-cmake -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-cmake --config Release
#
# Results: build-cmake/bin/brownstone100.vst3 and (macOS) build-cmake/bin/brownstone100.component
# To use a local DPF checkout instead of fetching: -DDPF_ROOT=/path/to/DPF

cmake_minimum_required(VERSION 3.17)

if(APPLE)
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary")
  set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Oldest macOS supported")
endif()
# Static C/C++ runtime with MSVC, so hosts don't need a redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(brownstone100 VERSION 1.1.0 LANGUAGES C CXX)

set(DPF_EXAMPLES OFF CACHE BOOL "" FORCE)
set(DPF_LIBRARIES OFF CACHE BOOL "" FORCE)

if(DPF_ROOT)
  add_subdirectory("${DPF_ROOT}" dpf)
else()
  include(FetchContent)
  FetchContent_Declare(dpf
    GIT_REPOSITORY https://github.com/DISTRHO/DPF.git
    GIT_TAG        4238e1c7f0351bbe488d79f0899c540543ac7583
    GIT_SUBMODULES_RECURSE TRUE)
  FetchContent_MakeAvailable(dpf)
endif()

set(BS_TARGETS vst3)
if(APPLE)
  list(APPEND BS_TARGETS au)
endif()

dpf_add_plugin(brownstone100
  TARGETS ${BS_TARGETS}
  FILES_DSP
    plugin/BrownstonePlugin.cpp
    src/brownstone100.c
  FILES_UI
    plugin/BrownstoneUI.cpp)

target_include_directories(brownstone100 PUBLIC plugin src)

# The DSP file also builds the LV2 plugin; here it must not export the LV2 entry point.
set_source_files_properties(src/brownstone100.c PROPERTIES
  COMPILE_DEFINITIONS BROWNSTONE_NO_LV2_EXPORT)
