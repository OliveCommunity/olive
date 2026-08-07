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
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

# Standalone build driver for the oakaudio module (M6). Mirrors
# src/codec/standalone: oakaudio links oakcodec (encoder for recording,
# decoder for waveform extraction), oakcommon (config + ffmpegutils C
# ABI), olivecore and ffmpeg_bridge, plus PortAudio for the output
# device. oakcodec's own dependency stack (oakrender/oaknode/...) is
# assembled the same way src/codec/standalone does it.
#
# Usage (macOS/Homebrew):
#   cmake -S src/audio/standalone -B build-audio
#   cmake --build build-audio -j
#   ctest --test-dir build-audio

cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

project(oakaudio-standalone LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

get_filename_component(OAK_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)

list(APPEND CMAKE_MODULE_PATH "${OAK_REPO_ROOT}/cmake")
if(EXISTS "/opt/homebrew")
  list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew")
endif()

find_package(EXPAT REQUIRED)
find_package(OpenColorIO CONFIG REQUIRED)
find_package(OpenImageIO CONFIG REQUIRED)

set(OCIO_LIBRARIES OpenColorIO::OpenColorIO)
set(OCIO_INCLUDE_DIRS "")
set(OIIO_LIBRARIES OpenImageIO::OpenImageIO)
set(OIIO_INCLUDE_DIRS "")

# In-repo libraries, built from source (same set src/codec/standalone
# assembles, because oakaudio links oakcodec):
#  - olivecore (core/): oakcore_* C ABI and olive::core C++ wrappers
#  - ffmpeg_bridge: fb_* C ABI (resampler infra + codec's FFmpeg access)
#  - oakundo / oakcommon / oaknode / oakrender: oakcodec's own dependencies
#  - oakcodec: encoder (recording) + decoder (waveform extract) C ABI
set(OLIVECORE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CMAKE_DISABLE_FIND_PACKAGE_OpenTimelineIO ON)

add_subdirectory(${OAK_REPO_ROOT}/core ${CMAKE_BINARY_DIR}/core)
target_include_directories(olivecore PUBLIC ${OAK_REPO_ROOT}/third_party/openfx/include)
add_subdirectory(${OAK_REPO_ROOT}/ffmpeg_bridge ${CMAKE_BINARY_DIR}/ffmpeg_bridge)

set(BUILD_TESTS OFF)
add_subdirectory(${OAK_REPO_ROOT}/src/undo ${CMAKE_BINARY_DIR}/undo)
add_subdirectory(${OAK_REPO_ROOT}/src/common ${CMAKE_BINARY_DIR}/common)
add_subdirectory(${OAK_REPO_ROOT}/src/node ${CMAKE_BINARY_DIR}/node)
set(BUILD_TESTS ON)

# oaknode needs its transition stubs when built in this tree (see
# src/render/standalone/CMakeLists.txt).
target_include_directories(oaknode BEFORE PUBLIC
        ${OAK_REPO_ROOT}/src/render/transition
        ${OAK_REPO_ROOT}/src/node/transition
        ${OAK_REPO_ROOT}/src/render/src
)
target_include_directories(oaknode PUBLIC
        ${OAK_REPO_ROOT}/third_party/openfx/HostSupport/include
        /opt/homebrew/include
        /opt/homebrew/include/Imath
)
target_link_options(oaknode PRIVATE
        "-undefined" "dynamic_lookup"
)

add_subdirectory(${OAK_REPO_ROOT}/src/render/src ${CMAKE_BINARY_DIR}/render)
add_subdirectory(${OAK_REPO_ROOT}/src/render/c_api ${CMAKE_BINARY_DIR}/render_c_api)

# Transition stub dirs must precede everything else: src/render/transition
# first, then src/node/transition (shared stubs).
target_include_directories(oakrender BEFORE PUBLIC
        ${OAK_REPO_ROOT}/src/render/transition
        ${OAK_REPO_ROOT}/src/node/transition
)

target_include_directories(oakrender PUBLIC
        ${OAK_REPO_ROOT}/engine/include
        ${OAK_REPO_ROOT}/third_party/openfx/HostSupport/include
        /opt/homebrew/include
        /opt/homebrew/include/Imath
        /opt/homebrew/include/OpenEXR
)

# Vulkan headers (Homebrew keg-only vulkan-headers).
if(NOT EXISTS "/opt/homebrew/include/vulkan/vulkan.h")
  execute_process(COMMAND brew --prefix vulkan-headers
          OUTPUT_VARIABLE VULKAN_HEADERS_PREFIX
          OUTPUT_STRIP_TRAILING_WHITESPACE
          ERROR_QUIET)
  if(VULKAN_HEADERS_PREFIX AND EXISTS "${VULKAN_HEADERS_PREFIX}/include/vulkan/vulkan.h")
    target_include_directories(oakrender PUBLIC "${VULKAN_HEADERS_PREFIX}/include")
  endif()
endif()

# Symbols of the not-yet-split engine modules dangle by design. The
# backend libraries resolve most symbols from liboakrender at load time
# and dangle the same way.
foreach(t oakrender oakgl oakgl2 oakvulkan)
  if(TARGET ${t})
    target_link_options(${t} PRIVATE
            "-undefined" "dynamic_lookup"
    )
  endif()
endforeach()

target_link_libraries(oakrender PRIVATE
        oaknode
        oakcommon
        oakundo
        olivecore
        ffmpeg_bridge
        ${OCIO_LIBRARIES}
        ${OIIO_LIBRARIES}
        "-framework OpenGL"
        "-framework CoreVideo"
        "-framework Metal"
        "-framework QuartzCore"
)

# oakcodec (oakaudio's only codec access goes through its C ABI).
add_subdirectory(${OAK_REPO_ROOT}/src/codec/src ${CMAKE_BINARY_DIR}/codec)
add_subdirectory(${OAK_REPO_ROOT}/src/codec/c_api ${CMAKE_BINARY_DIR}/codec_c_api)

target_link_options(oakcodec PRIVATE
        "-undefined" "dynamic_lookup"
)

# oakaudio itself.
add_subdirectory(${OAK_REPO_ROOT}/src/audio/src ${CMAKE_BINARY_DIR}/audio)
add_subdirectory(${OAK_REPO_ROOT}/src/audio/c_api ${CMAKE_BINARY_DIR}/audio_c_api)

# Tests (oakaudio-gtest).
if(BUILD_TESTS)
  enable_testing()
  add_subdirectory(${OAK_REPO_ROOT}/src/audio/tests ${CMAKE_BINARY_DIR}/audio_tests)
endif()
add_subdirectory(${OAK_REPO_ROOT}/src/plugin ${CMAKE_BINARY_DIR}/plugin)
