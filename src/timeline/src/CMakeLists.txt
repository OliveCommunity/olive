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

file(GLOB_RECURSE OAKTIMELINE_SOURCES CONFIGURE_DEPENDS *.cpp)

add_library(oaktimeline SHARED ${OAKTIMELINE_SOURCES})

target_include_directories(oaktimeline PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${OAK_REPO_ROOT}/include
        ${OAK_REPO_ROOT}/src/common/src
        ${OAK_REPO_ROOT}/src/undo/src
        ${OAK_REPO_ROOT}/core/include
)

target_link_libraries(oaktimeline PUBLIC
        oaknode
        oakundo
        oakcommon
        olivecore
)
