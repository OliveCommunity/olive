# Oak Video Editor - Non-Linear Video Editor
# Copyright (C) 2026 Oak Team
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.

find_package(GTest REQUIRED)
include(GoogleTest)

# In a full-tree build the repo root is CMAKE_SOURCE_DIR; a standalone
# build (see src/render/standalone) sets OAK_REPO_ROOT explicitly.
if(NOT DEFINED OAK_REPO_ROOT)
  set(OAK_REPO_ROOT ${CMAKE_SOURCE_DIR})
endif()

add_executable(oakrender-gtest
        cache_test.cpp
        cancelatom_test.cpp
        color_test.cpp
        manager_test.cpp
        renderer_test.cpp
)

target_link_libraries(oakrender-gtest PRIVATE
        oakrender
        oaknode
        oakcommon
        oakundo
        olivecore
        GTest::gtest
        GTest::gtest_main
)

# liboakrender/liboaknode dangle OFX host symbols (-undefined
# dynamic_lookup); force-load the host support archive into the test
# process so dyld finds them (and the OFX typeinfos) in the flat
# namespace at startup. Mirrors src/node/tests/CMakeLists.txt.
if(NOT DEFINED OAKRENDER_OFX_HOST_ARCHIVE)
  find_library(OAKRENDER_OFX_HOST_ARCHIVE NAMES OfxHost
          PATHS ${OAK_REPO_ROOT}/build/third_party/openfx/HostSupport)
endif()
if(NOT OAKRENDER_OFX_HOST_ARCHIVE)
  message(FATAL_ERROR
          "libOfxHost.a not found; run the full-tree build once or set "
          "OAKRENDER_OFX_HOST_ARCHIVE")
endif()
target_link_options(oakrender-gtest PRIVATE
        "-Wl,-force_load,${OAKRENDER_OFX_HOST_ARCHIVE}")

# liboakrender references the oakengine_ipc_* C ABI (worker IPC) via
# dynamic_lookup; the real implementation is still Qt-based, so the test
# binary links the inert shim from src/node/standalone instead.
target_sources(oakrender-gtest PRIVATE
        ${OAK_REPO_ROOT}/src/node/standalone/oakengine_ipc_shim.cpp)
target_include_directories(oakrender-gtest PRIVATE
        ${OAK_REPO_ROOT}/engine/include
)

# The color tests build OCIO processors against the engine's bundled
# config; the OCIO environment variable is needed both at ctest time and
# during PRE_TEST discovery (binary startup).
gtest_discover_tests(oakrender-gtest
        DISCOVERY_MODE PRE_TEST
        PROPERTIES ENVIRONMENT
        "OCIO=${OAK_REPO_ROOT}/engine/render/ocioconf/config.ocio"
)
