# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kyr4l
# Builds the example plugin on its own, the way a plugin outside this repository is built:
#   cmake -S examples/plugins/hello -B build-hello && cmake --build build-hello
cmake_minimum_required(VERSION 3.20)
project(hello-plugin LANGUAGES CXX)
if(NOT DEFINED COMPANION_SOURCE_DIR)
  get_filename_component(COMPANION_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
endif()
include(${COMPANION_SOURCE_DIR}/cmake/CompanionPlugin.cmake)
companion_add_plugin(hello hello.cpp)
