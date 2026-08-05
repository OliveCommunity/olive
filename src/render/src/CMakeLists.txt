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

file(GLOB_RECURSE OAKRENDER_SOURCES CONFIGURE_DEPENDS *.cpp)

# The backend_c translation units are the C ABI entry points of the
# dynamically loaded backend libraries (liboakgl/liboakvulkan, see
# backend/dynamicrenderer.cpp). They export the same oak_renderer_* symbol
# table, so they must NOT be linked into liboakrender itself.
list(REMOVE_ITEM OAKRENDER_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/opengl/openglbackend_c.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/vulkan/vulkanbackend_c.cpp
)

add_library(oakrender SHARED ${OAKRENDER_SOURCES})

# Dynamically loaded render backends (oak_renderer_* C ABI). Loaded via
# dlopen by DynamicRenderer from the app render_backends/ dir.
add_library(oakgl SHARED opengl/openglbackend_c.cpp)
add_library(oakvulkan SHARED vulkan/vulkanbackend_c.cpp)
foreach(backend oakgl oakvulkan)
        target_link_libraries(${backend} PRIVATE oakrender)
endforeach()

target_include_directories(oakrender PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${OAK_REPO_ROOT}/include
        ${OAK_REPO_ROOT}/src/common/src
        ${OAK_REPO_ROOT}/src/undo/src
        ${OAK_REPO_ROOT}/src/node/src
        ${OAK_REPO_ROOT}/core/include
        ${OAK_REPO_ROOT}/ffmpeg_bridge/include
        ${OAK_REPO_ROOT}/third_party/openfx/include
        ${OCIO_INCLUDE_DIRS}
        ${OIIO_INCLUDE_DIRS}
)
