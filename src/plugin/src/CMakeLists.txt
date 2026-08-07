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

file(GLOB OAKPLUGIN_SOURCES CONFIGURE_DEPENDS *.cpp)

add_library(oakplugin SHARED ${OAKPLUGIN_SOURCES})

target_include_directories(oakplugin PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${OAK_REPO_ROOT}/include
        ${OAK_REPO_ROOT}/src/common/src
        ${OAK_REPO_ROOT}/src/undo/src
        ${OAK_REPO_ROOT}/core/include
        ${OAK_REPO_ROOT}/third_party/openfx/include
        ${OAK_REPO_ROOT}/third_party/openfx/HostSupport/include
        ${OAK_REPO_ROOT}/ffmpeg_bridge/include
)

find_library(OAKPLUGIN_OFX_HOST_ARCHIVE NAMES OfxHost
        PATHS ${OAK_REPO_ROOT}/build/third_party/openfx/HostSupport
        NO_DEFAULT_PATH)
if(NOT OAKPLUGIN_OFX_HOST_ARCHIVE)
  message(FATAL_ERROR
          "libOfxHost.a not found; run the full-tree build once or set "
          "OAKPLUGIN_OFX_HOST_ARCHIVE")
endif()
target_link_options(oakplugin PUBLIC
        "-Wl,-force_load,${OAKPLUGIN_OFX_HOST_ARCHIVE}")

target_link_libraries(oakplugin PUBLIC
        oaknode
        oakrender
        oakcommon
        oakundo
        olivecore
        ffmpeg_bridge
)
