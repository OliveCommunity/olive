# Oak Video Editor - Non-Linear Video Editor
# Copyright (C) 2026 Oak Team
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

# Standalone build driver for the oakundo module. oakundo depends only on
# oakcommon's header-only define.h macros (pulled in by include path, no
# linking), so this builds just oakundo and its tests.
#
# Usage (macOS/Homebrew):
#   cmake -S src/undo/standalone -B build-oakundo
#   cmake --build build-oakundo -j
#   ctest --test-dir build-oakundo

cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

project(oakundo-standalone LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

get_filename_component(OAK_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)

list(APPEND CMAKE_MODULE_PATH "${OAK_REPO_ROOT}/cmake")
if(EXISTS "/opt/homebrew")
  list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew")
endif()

set(BUILD_TESTS ON)
enable_testing()
add_subdirectory(${OAK_REPO_ROOT}/src/undo ${CMAKE_BINARY_DIR}/undo)
