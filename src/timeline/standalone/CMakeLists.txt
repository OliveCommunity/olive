# Olive - Non-Linear Video Editor
# Copyright (C) 2022 Olive Team
# Modifications Copyright (C) 2025 mikesolar
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

cmake_minimum_required(VERSION 3.16)
project(oaktimeline-standalone CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# oaktimeline manipulates node graphs through the oaknode C ABI; the
# not-yet-split engine modules' symbols are allowed to dangle, resolved
# by transitional stubs in src/{node,render}/transition.
set(OAK_REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
list(APPEND CMAKE_MODULE_PATH "${OAK_REPO_ROOT}/cmake")

set(BUILD_TESTS ON CACHE BOOL "" FORCE)
set(OLIVECORE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
# The /opt/otio OpenTimelineIO dylibs use @loader_path install names that
# break when linked from a build tree; oaktimeline does not need OTIO.
set(CMAKE_DISABLE_FIND_PACKAGE_OpenTimelineIO ON)

find_package(EXPAT REQUIRED)
find_package(OpenColorIO CONFIG REQUIRED)
find_package(OpenImageIO CONFIG REQUIRED)
set(OCIO_LIBRARIES OpenColorIO::OpenColorIO)
set(OCIO_INCLUDE_DIRS ${OpenColorIO_INCLUDE_DIRS})
set(OIIO_LIBRARIES OpenImageIO::OpenImageIO)
set(OIIO_INCLUDE_DIRS ${OpenImageIO_INCLUDE_DIRS})

add_subdirectory(${OAK_REPO_ROOT}/core ${CMAKE_BINARY_DIR}/core)
target_include_directories(olivecore PUBLIC
        ${OAK_REPO_ROOT}/third_party/openfx/include
)
add_subdirectory(${OAK_REPO_ROOT}/ffmpeg_bridge ${CMAKE_BINARY_DIR}/ffmpeg_bridge)

set(BUILD_TESTS OFF)
add_subdirectory(${OAK_REPO_ROOT}/src/undo ${CMAKE_BINARY_DIR}/undo)
add_subdirectory(${OAK_REPO_ROOT}/src/common ${CMAKE_BINARY_DIR}/common)
add_subdirectory(${OAK_REPO_ROOT}/src/render/src ${CMAKE_BINARY_DIR}/render)
set(BUILD_TESTS ON)

enable_testing()
add_subdirectory(${OAK_REPO_ROOT}/src/node ${CMAKE_BINARY_DIR}/node)
add_subdirectory(${OAK_REPO_ROOT}/src/timeline ${CMAKE_BINARY_DIR}/timeline)

# oaknode needs its transition stubs and the oakrender real headers
# (src/node/transition/render/* bridge there); src/render/transition
# must precede src/node/transition.
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
foreach(t oakrender oakgl oakgl2 oakvulkan)
  if(TARGET ${t})
    target_link_options(${t} PRIVATE
            "-undefined" "dynamic_lookup"
    )
  endif()
endforeach()

target_include_directories(oaktimeline PUBLIC
        ${OAK_REPO_ROOT}/src/render/transition
        ${OAK_REPO_ROOT}/src/node/transition
        ${OAK_REPO_ROOT}/src/render/src
        ${OAK_REPO_ROOT}/src/node/src
)
target_link_options(oaktimeline PRIVATE
        "-undefined" "dynamic_lookup"
)

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

target_link_libraries(oaknode-gtest PRIVATE oakrender)

# liboakrender references the oakengine_ipc_* C ABI (worker IPC) via
# dynamic_lookup; the real implementation (engine/src/capi/ipc.cpp) is still
# Qt-based, so test binaries link an inert shim instead.
target_sources(oaknode-gtest PRIVATE
        ${OAK_REPO_ROOT}/src/node/standalone/oakengine_ipc_shim.cpp
)
target_include_directories(oaknode-gtest PRIVATE
        ${OAK_REPO_ROOT}/engine/include
)

target_link_libraries(oaktimeline-gtest PRIVATE oakrender)
target_sources(oaktimeline-gtest PRIVATE
        ${OAK_REPO_ROOT}/src/node/standalone/oakengine_ipc_shim.cpp
)
target_include_directories(oaktimeline-gtest PRIVATE
        ${OAK_REPO_ROOT}/engine/include
)

# OFX typeinfo is referenced via dynamic_lookup; force-load the host
# support archive so RTTI resolves (same as oaknode's test wiring).
find_library(OAKNODE_OFX_HOST_ARCHIVE NAMES OfxHost
        PATHS ${OAK_REPO_ROOT}/build/third_party/openfx/HostSupport
        NO_DEFAULT_PATH)
if(NOT OAKNODE_OFX_HOST_ARCHIVE)
  message(FATAL_ERROR
          "libOfxHost.a not found; run the full-tree build once or set "
          "OAKNODE_OFX_HOST_ARCHIVE")
endif()
target_link_options(oaktimeline-gtest PRIVATE
        "-Wl,-force_load,${OAKNODE_OFX_HOST_ARCHIVE}")
target_link_options(oaknode-gtest PRIVATE
        "-Wl,-force_load,${OAKNODE_OFX_HOST_ARCHIVE}")

# oaknode's Sequence::add_default_nodes() constructs
# TimelineAddTrackCommand, which lives in liboaktimeline; the two
# libraries resolve each other at runtime (dynamic_lookup).
target_link_libraries(oaknode-gtest PRIVATE oaktimeline)
