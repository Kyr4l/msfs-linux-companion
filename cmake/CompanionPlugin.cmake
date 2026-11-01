# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kyr4l
# Builds a plugin for msfs-linux-companion (see docs/PLUGINS.md). In a plugin's own CMakeLists.txt:
#
#   cmake_minimum_required(VERSION 3.20)
#   project(my-plugin LANGUAGES CXX)
#   set(COMPANION_SOURCE_DIR /path/to/msfs-linux-companion)   # the source of the program the plugin is for
#   include(${COMPANION_SOURCE_DIR}/cmake/CompanionPlugin.cmake)
#   companion_add_plugin(my_plugin my_plugin.cpp)
#
# The result is my_plugin.so. It is not linked against Dear ImGui or anything else of the program: the program provides those symbols when
# it loads the plugin, so the plugin draws in the program's own window and writes to its log.
if(NOT DEFINED COMPANION_SOURCE_DIR)
  get_filename_component(COMPANION_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()
if(NOT EXISTS "${COMPANION_SOURCE_DIR}/src/plugin/Plugin.hpp")
  message(FATAL_ERROR "COMPANION_SOURCE_DIR (${COMPANION_SOURCE_DIR}) is not the source of msfs-linux-companion")
endif()
if(NOT EXISTS "${COMPANION_SOURCE_DIR}/third_party/imgui/imgui.h")
  message(FATAL_ERROR "Dear ImGui submodule missing in ${COMPANION_SOURCE_DIR}. Run: git submodule update --init --recursive")
endif()
# the version of the program the plugin is built for, from its CMakeLists.txt
file(READ "${COMPANION_SOURCE_DIR}/CMakeLists.txt" _companion_top)
string(REGEX MATCH "project\\(msfs-linux-companion VERSION ([0-9.]+)" _companion_match "${_companion_top}")
set(COMPANION_PLUGIN_FOR_VERSION "${CMAKE_MATCH_1}")

function(companion_add_plugin name)
  add_library(${name} MODULE ${ARGN})
  set_target_properties(${name} PROPERTIES
    PREFIX ""
    CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF
    # default visibility: the plugin's references to the program's shared objects (the log, ImGui's context) must resolve to the program's
    CXX_VISIBILITY_PRESET default
    POSITION_INDEPENDENT_CODE ON)
  target_include_directories(${name} PRIVATE
    "${COMPANION_SOURCE_DIR}/src" "${COMPANION_SOURCE_DIR}/third_party"
    "${COMPANION_SOURCE_DIR}/third_party/imgui" "${COMPANION_SOURCE_DIR}/third_party/imgui/misc/cpp")
  target_compile_definitions(${name} PRIVATE COMPANION_VERSION="${COMPANION_PLUGIN_FOR_VERSION}")
  target_compile_options(${name} PRIVATE -Wall -Wextra -Wno-unused-parameter)
endfunction()
