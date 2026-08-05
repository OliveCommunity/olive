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
# build (see src/node/standalone) sets OAK_REPO_ROOT explicitly.
if(NOT DEFINED OAK_REPO_ROOT)
  set(OAK_REPO_ROOT ${CMAKE_SOURCE_DIR})
endif()

add_executable(oaknode-gtest
        block_test.cpp
        colormanager_test.cpp
        factory_test.cpp
        folder_test.cpp
        footage_test.cpp
        group_test.cpp
        keyframe_test.cpp
        node_test.cpp
        project_test.cpp
        sequence_test.cpp
        serializer_test.cpp
        track_test.cpp
        traverser_test.cpp
)

target_link_libraries(oaknode-gtest PRIVATE
        oaknode
        oakundo
        oakcommon
        olivecore
        GTest::gtest
        GTest::gtest_main
)

# liboaknode dangles OFX host symbols (-undefined dynamic_lookup); force-load
# the host support archive into the test process so dyld finds them (and the
# OFX typeinfos) in the flat namespace at startup. Without this the binary
# crashes on launch, which also breaks gtest_discover_tests PRE_TEST
# discovery. The archive is a prebuilt artifact of the full-tree build
# (third_party/openfx); the standalone driver sets OAKNODE_OFX_HOST_ARCHIVE.
if(NOT DEFINED OAKNODE_OFX_HOST_ARCHIVE)
  find_library(OAKNODE_OFX_HOST_ARCHIVE NAMES OfxHost
          PATHS ${OAK_REPO_ROOT}/build/third_party/openfx/HostSupport)
endif()
if(NOT OAKNODE_OFX_HOST_ARCHIVE)
  message(FATAL_ERROR
          "libOfxHost.a not found; run the full-tree build once or set "
          "OAKNODE_OFX_HOST_ARCHIVE")
endif()
target_link_options(oaknode-gtest PRIVATE
        "-Wl,-force_load,${OAKNODE_OFX_HOST_ARCHIVE}")

# ColorManager builds its default OCIO config at Project construction time;
# point at the engine's bundled config (the de-Qt'd ":/ocioconf" extraction
# is a Qt-resource leftover that always fails). colormanager_test.cpp reads
# OAK_OCIO_TEST_CONFIG; the OCIO environment variable is needed both at
# ctest time and during PRE_TEST discovery (binary startup).
target_compile_definitions(oaknode-gtest PRIVATE
        OAK_OCIO_TEST_CONFIG="${OAK_REPO_ROOT}/engine/render/ocioconf/config.ocio")

gtest_discover_tests(oaknode-gtest
        DISCOVERY_MODE PRE_TEST
        PROPERTIES ENVIRONMENT
        "OCIO=${OAK_REPO_ROOT}/engine/render/ocioconf/config.ocio"
)
