# Oak Video Editor - Non-Linear Video Editor
# Copyright (C) 2026 Oak Team
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# Standalone build driver for the oakrender module (M7). Mirrors
# src/node/standalone: references into the not-yet-split codec/audio/task/
# config/pluginSupport modules are allowed to dangle — headers come from the
# transitional stubs in src/render/transition (with src/node/transition as
# fallback for shared stubs), and symbols are left undefined via
# -undefined dynamic_lookup (macOS).
#
# Usage (macOS/Homebrew):
#   cmake -S src/render/standalone -B build-oakrender
#   cmake --build build-oakrender -j

cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

project(oakrender-standalone LANGUAGES CXX)

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

# In-repo libraries, built from source (real targets, not prebuilt dylibs):
#  - olivecore (core/): oakcore_* C ABI and olive::core C++ utils
#  - ffmpeg_bridge: fb_* C ABI used by oakcommon
#  - oakundo: UndoCommand/UndoStack
#  - oakcommon: XmlStreamReader/Writer, Current, misc utils
#  - oaknode: de-Qt node graph (render pulls node headers/classes directly
#    until the C ABI wave lands)
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

# oaknode needs its transition stubs too when built in this tree (see
# src/node/standalone/CMakeLists.txt). src/render/transition must precede
# src/node/transition so both sides resolve the same extended stubs; the
# bridged render/ headers pull real oakrender headers from src/render/src.
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

# oakrender C ABI (M7): target_sources into the oakrender target.
add_subdirectory(${OAK_REPO_ROOT}/src/render/c_api ${CMAKE_BINARY_DIR}/render_c_api)

# Transition stub dirs must precede everything else: src/render/transition
# first (render's own stubs and render/ bridge headers), then
# src/node/transition (shared stubs such as render/samplebuffer.h that the
# bridge headers reference).
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

# Vulkan headers (Homebrew keg-only vulkan-headers, or already in
# /opt/homebrew/include).
if(NOT EXISTS "/opt/homebrew/include/vulkan/vulkan.h")
  execute_process(COMMAND brew --prefix vulkan-headers
          OUTPUT_VARIABLE VULKAN_HEADERS_PREFIX
          OUTPUT_STRIP_TRAILING_WHITESPACE
          ERROR_QUIET)
  if(VULKAN_HEADERS_PREFIX AND EXISTS "${VULKAN_HEADERS_PREFIX}/include/vulkan/vulkan.h")
    target_include_directories(oakrender PUBLIC "${VULKAN_HEADERS_PREFIX}/include")
  endif()
endif()

# Symbols of the not-yet-split engine modules (codec/audio/task/config/
# pluginSupport/...) dangle by design. The backend libraries resolve most
# symbols from liboakrender at load time and dangle the same way.
foreach(t oakrender oakgl oakvulkan)
  target_link_options(${t} PRIVATE
          "-undefined" "dynamic_lookup"
  )
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

# Tests (oakrender-gtest). BUILD_TESTS mirrors the src/render/CMakeLists.txt
# gate; it is ON here by default and forced OFF around the in-repo library
# subdirectories above that have their own test gates.
if(BUILD_TESTS)
  enable_testing()
  add_subdirectory(${OAK_REPO_ROOT}/src/render/tests ${CMAKE_BINARY_DIR}/render_tests)
endif()
