# Oak Video Editor - Non-Linear Video Editor
# Copyright (C) 2026 Oak Team
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# Standalone build driver for the oaknode module. Per the M3 ruling (裁决 A),
# references into the not-yet-split render/codec/timeline/audio/pluginSupport
# modules are allowed to dangle: those headers are provided by the transitional
# stubs in src/node/transition (the engine headers are still Qt-based), and
# symbols are left undefined via -undefined dynamic_lookup (macOS).
#
# Builds oaknode, its C API, and its tests against the real in-repo targets
# (olivecore, ffmpeg_bridge, oakcommon, oakundo) instead of prebuilt dylibs.
#
# Usage (macOS/Homebrew):
#   cmake -S src/node/standalone -B build-oaknode
#   cmake --build build-oaknode -j
#   ctest --test-dir build-oaknode

cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

project(oaknode-standalone LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

get_filename_component(OAK_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)

list(APPEND CMAKE_MODULE_PATH "${OAK_REPO_ROOT}/cmake")
if(EXISTS "/opt/homebrew")
  list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew")
endif()

# Third-party dependencies. As in src/common/standalone, use the upstream
# CMake config packages shipped by Homebrew and map them onto the variables
# oakcommon's CMakeLists consumes.
find_package(EXPAT REQUIRED)
find_package(OpenColorIO CONFIG REQUIRED)
find_package(OpenImageIO CONFIG REQUIRED)

set(OCIO_LIBRARIES OpenColorIO::OpenColorIO)
set(OCIO_INCLUDE_DIRS "")
set(OIIO_LIBRARIES OpenImageIO::OpenImageIO)
set(OIIO_INCLUDE_DIRS "")

# In-repo libraries, built from source (real targets, not prebuilt dylibs):
#  - olivecore (core/): oakcore_* C ABI, output name liboakcore
#  - ffmpeg_bridge: fb_* C ABI used by oakcommon
#  - oakundo: UndoCommand/UndoStack used by the oaknode C API
#  - oakcommon: XmlStreamReader/Writer, Current, misc utils
set(OLIVECORE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
# The /opt/otio OpenTimelineIO dylibs use @loader_path install names that
# break when linked from a build tree; oaknode does not need OTIO.
set(CMAKE_DISABLE_FIND_PACKAGE_OpenTimelineIO ON)

add_subdirectory(${OAK_REPO_ROOT}/core ${CMAKE_BINARY_DIR}/core)
# The top-level project puts third_party/openfx/include on the global
# include path; replicate it here for olivecore's pixelformat.h (ofxCore.h).
target_include_directories(olivecore PUBLIC ${OAK_REPO_ROOT}/third_party/openfx/include)
add_subdirectory(${OAK_REPO_ROOT}/ffmpeg_bridge ${CMAKE_BINARY_DIR}/ffmpeg_bridge)

set(BUILD_TESTS OFF)
add_subdirectory(${OAK_REPO_ROOT}/src/undo ${CMAKE_BINARY_DIR}/undo)
add_subdirectory(${OAK_REPO_ROOT}/src/common ${CMAKE_BINARY_DIR}/common)
set(BUILD_TESTS ON)

enable_testing()
add_subdirectory(${OAK_REPO_ROOT}/src/node ${CMAKE_BINARY_DIR}/node)

# M4 收尾：oaknode 的 Sequence::add_default_nodes() 依赖 oaktimeline 的
# TimelineAddTrackCommand（src/node/transition/timeline/* 已桥接真身），
# 两个库在运行期互相解析（dynamic_lookup）
set(BUILD_TESTS OFF)
add_subdirectory(${OAK_REPO_ROOT}/src/timeline ${CMAKE_BINARY_DIR}/timeline)
set(BUILD_TESTS ON)
target_link_options(oaktimeline PRIVATE
        "-undefined" "dynamic_lookup"
)

# The transition stub dir must precede ${OAK_REPO_ROOT}/include so that e.g.
# "common/current.h" resolves to the transitional forwarder, not the stale
# public mirror in include/common.
# M7 收尾：src/node/transition/render/* 已改为桥接头，转发到
# src/render/src 真身；因此 oaknode 需要 src/render/src 在 include 路径上，
# 且 src/render/transition 必须置于 src/node/transition 之前（两侧共用
# render 的扩展 stub，契约一致）。
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

# Symbols of the not-yet-split engine modules dangle; the test binary
# resolves them by linking the real targets and force-loading libOfxHost.
target_link_options(oaknode PRIVATE
        "-undefined" "dynamic_lookup"
)

# M7 收尾：oaknode 的 render/ 引用现在解析到 src/render/src 的真身类，
# 其成员函数（如 PlaybackCache::InvalidateEvent）定义在 liboakrender 里。
# 把 oakrender 构建进来并链进测试二进制；未拆分模块符号同样悬置。
set(BUILD_TESTS OFF)
add_subdirectory(${OAK_REPO_ROOT}/src/render/src ${CMAKE_BINARY_DIR}/render)
set(BUILD_TESTS ON)

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

target_link_libraries(oaknode-gtest PRIVATE oakrender oaktimeline)

# liboakrender references the oakengine_ipc_* C ABI (worker IPC) via
# dynamic_lookup; the real implementation (engine/src/capi/ipc.cpp) is still
# Qt-based, so the test binary links an inert shim instead.
target_sources(oaknode-gtest PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/oakengine_ipc_shim.cpp
)
target_include_directories(oaknode-gtest PRIVATE
        ${OAK_REPO_ROOT}/engine/include
)
