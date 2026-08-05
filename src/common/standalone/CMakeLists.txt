# Oak - Non-Linear Video Editor
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
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

# Standalone build driver for the oakcommon module. Builds only oakcommon,
# its tests, and the in-repo libraries it links against (olivecore,
# ffmpeg_bridge), without configuring the heavy top-level project.
#
# Usage (macOS/Homebrew):
#   cmake -S src/common/standalone -B build-oakcommon
#   cmake --build build-oakcommon -j
#   ctest --test-dir build-oakcommon

cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

project(oakcommon-standalone LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

get_filename_component(OAK_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)

list(APPEND CMAKE_MODULE_PATH "${OAK_REPO_ROOT}/cmake")
if(EXISTS "/opt/homebrew")
  list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew")
endif()

# Third-party dependencies. The top-level project resolves OCIO/OIIO through
# cmake/FindOpenColorIO.cmake / FindOpenImageIO.cmake (which are .so-centric);
# here we use the upstream CMake config packages shipped by Homebrew and map
# them onto the variables oakcommon's CMakeLists consumes.
find_package(EXPAT REQUIRED)
find_package(OpenColorIO CONFIG REQUIRED)
find_package(OpenImageIO CONFIG REQUIRED)

set(OCIO_LIBRARIES OpenColorIO::OpenColorIO)
set(OCIO_INCLUDE_DIRS "")
set(OIIO_LIBRARIES OpenImageIO::OpenImageIO)
set(OIIO_INCLUDE_DIRS "")

# In-repo libraries oakcommon links against:
#  - olivecore: olive::core::Rational wraps the oakcore_* C ABI (real symbols)
#  - ffmpeg_bridge: fb_find_best_pix_fmt_of_list (real symbol)
set(OLIVECORE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
# The /opt/otio OpenTimelineIO dylibs use @loader_path install names that
# break when linked from a build tree; oakcommon does not need OTIO.
set(CMAKE_DISABLE_FIND_PACKAGE_OpenTimelineIO ON)

add_subdirectory(${OAK_REPO_ROOT}/core ${CMAKE_BINARY_DIR}/core)
# The top-level project puts third_party/openfx/include on the global
# include path; replicate it here for olivecore's pixelformat.h (ofxCore.h).
target_include_directories(olivecore PUBLIC ${OAK_REPO_ROOT}/third_party/openfx/include)
add_subdirectory(${OAK_REPO_ROOT}/ffmpeg_bridge ${CMAKE_BINARY_DIR}/ffmpeg_bridge)

set(BUILD_TESTING ON)
enable_testing()
add_subdirectory(${OAK_REPO_ROOT}/src/common ${CMAKE_BINARY_DIR}/common)
