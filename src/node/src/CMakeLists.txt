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

# oaknode library. The tree is flat: src/node/src is the include root and all
# in-module includes use the unprefixed form ("value.h", "block/block.h").
# The per-subdirectory CMakeLists.txt files are inert legacy (they only append
# to OLIVE_SOURCES) and are intentionally not added.
file(GLOB_RECURSE OAKNODE_SOURCES CONFIGURE_DEPENDS *.cpp)

add_library(oaknode SHARED ${OAKNODE_SOURCES})

target_include_directories(oaknode PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${OAK_REPO_ROOT}/include
        ${OAK_REPO_ROOT}/src/common/src
        ${OAK_REPO_ROOT}/src/undo/src
        ${OAK_REPO_ROOT}/core/include
        ${OAK_REPO_ROOT}/ffmpeg_bridge/include
        ${OAK_REPO_ROOT}/third_party/openfx/include
        ${OCIO_INCLUDE_DIRS}
        ${OIIO_INCLUDE_DIRS}
)
