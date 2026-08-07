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

find_package(GTest REQUIRED)
include(GoogleTest)

# In a full-tree build the repo root is CMAKE_SOURCE_DIR; a standalone
# build (see src/codec/standalone) sets OAK_REPO_ROOT explicitly.
if(NOT DEFINED OAK_REPO_ROOT)
  set(OAK_REPO_ROOT ${CMAKE_SOURCE_DIR})
endif()

add_executable(oakcodec-gtest
        frame_test.cpp
        decoder_test.cpp
        encoder_test.cpp
        task_test.cpp
)

target_link_libraries(oakcodec-gtest PRIVATE
        oakcodec
        oakrender
        oaknode
        oakplugin
        oakcommon
        oakundo
        olivecore
        GTest::gtest
        GTest::gtest_main
)

# liboakrender/liboaknode dangle OFX host symbols (-undefined
# dynamic_lookup); force-load the host support archive into the test
# process so dyld finds them in the flat namespace at startup. Mirrors
# src/render/tests/CMakeLists.txt.
if(NOT DEFINED OAKRENDER_OFX_HOST_ARCHIVE)
  find_library(OAKRENDER_OFX_HOST_ARCHIVE NAMES OfxHost
          PATHS ${OAK_REPO_ROOT}/build/third_party/openfx/HostSupport)
endif()
if(NOT OAKRENDER_OFX_HOST_ARCHIVE)
  message(FATAL_ERROR
          "libOfxHost.a not found; run the full-tree build once or set "
          "OAKRENDER_OFX_HOST_ARCHIVE")
endif()
target_link_options(oakcodec-gtest PRIVATE
        "-Wl,-force_load,${OAKRENDER_OFX_HOST_ARCHIVE}")

# liboakrender references the oakengine_ipc_* C ABI (worker IPC) via
# dynamic_lookup; the test binary links the inert shim from
# src/node/standalone instead.
target_sources(oakcodec-gtest PRIVATE
        ${OAK_REPO_ROOT}/src/node/standalone/oakengine_ipc_shim.cpp)
target_include_directories(oakcodec-gtest PRIVATE
        ${OAK_REPO_ROOT}/engine/include
)

# include/ must win over the render/node transition dirs that leak in
# through oaknode's PUBLIC includes: they carry codec/*.h stubs that would
# otherwise shadow the real oakcodec public headers. -iquote is searched
# before every -I for quoted includes.
target_compile_options(oakcodec-gtest PRIVATE
        "-iquote" "${OAK_REPO_ROOT}/include"
)

# tests/demo.mp4 lives at the repo's shared tests directory.
target_compile_definitions(oakcodec-gtest PRIVATE
        OAKCODEC_TEST_DATA_DIR="${OAK_REPO_ROOT}/tests")

gtest_discover_tests(oakcodec-gtest
        DISCOVERY_MODE PRE_TEST
        PROPERTIES ENVIRONMENT
        "OCIO=${OAK_REPO_ROOT}/engine/render/ocioconf/config.ocio")
