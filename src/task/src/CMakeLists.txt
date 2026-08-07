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

file(GLOB_RECURSE OAKTASK_SOURCES CONFIGURE_DEPENDS *.cpp)

add_library(oaktask SHARED ${OAKTASK_SOURCES})

target_include_directories(oaktask PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${OAK_REPO_ROOT}/include
        ${OAK_REPO_ROOT}/src/common/src
        ${OAK_REPO_ROOT}/src/undo/src
        ${OAK_REPO_ROOT}/core/include
        ${OAK_REPO_ROOT}/otio-install/include
)

target_compile_definitions(oaktask PUBLIC USE_OTIO)

target_link_directories(oaktask PUBLIC ${OAK_REPO_ROOT}/otio-install/lib)

target_link_libraries(oaktask PUBLIC
        opentimelineio
        opentime
        oaknode
        oakrender
        oakcodec
        oakundo
        oakcommon
        olivecore
)

# OTIO dylibs use @loader_path install names; they must sit next to
# liboaktask.dylib (the loading object).
add_custom_command(TARGET oaktask POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${OAK_REPO_ROOT}/otio-install/lib/libopentimelineio.dylib
                ${OAK_REPO_ROOT}/otio-install/lib/libopentime.dylib
                $<TARGET_FILE_DIR:oaktask>)
