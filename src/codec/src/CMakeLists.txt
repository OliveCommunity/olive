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

add_library(oakcodec SHARED
        conformmanager.cpp
        conformmanager.h
        decoder.cpp
        decoder.h
        encoder.cpp
        encoder.h
        exportcodec.cpp
        exportcodec.h
        exportformat.cpp
        exportformat.h
        footagedescription.h
        frame.cpp
        frame.h
        framemanager.cpp
        framemanager.h
        oiioframebridge.cpp
        oiioframebridge.h
        planarfiledevice.cpp
        planarfiledevice.h
        proxymanager.cpp
        proxymanager.h
        taskcallbacks.cpp
        taskcallbacks.h
        timecodemetadata.cpp
        timecodemetadata.h
)
add_subdirectory(ffmpeg)
add_subdirectory(oiio)

# In a full-tree build the repo root is CMAKE_SOURCE_DIR; a standalone
# build (see src/codec/standalone) sets OAK_REPO_ROOT explicitly.
if(NOT DEFINED OAK_REPO_ROOT)
  set(OAK_REPO_ROOT ${CMAKE_SOURCE_DIR})
endif()

target_include_directories(oakcodec PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${OAK_REPO_ROOT}/include
        ${OAK_REPO_ROOT}/core/include
        ${OAK_REPO_ROOT}/ffmpeg_bridge/include
        ${OAK_REPO_ROOT}/third_party/openfx/include
        ${OIIO_INCLUDE_DIRS}
        ${OCIO_INCLUDE_DIRS}
)

# 01 §1 rule 5: only the OAKCODEC_API-marked C functions are exported;
# codec-internal C++ classes (olive::Frame, FootageDescription, ...) must not
# leak into the global symbol namespace where they would interpose on
# same-named weak symbols inside oakcommon/oakrender.
target_compile_options(oakcodec PRIVATE
        -fvisibility=hidden
        -fvisibility-inlines-hidden
)

# oakcommon's C API implementation links these PUBLICly; oakcodec consumes
# the oakcommon C ABI (and olivecore's C++ wrappers) only.
target_link_libraries(oakcodec PUBLIC
        oakcommon
        oakrender
        olivecore
        ffmpeg_bridge
        ${OCIO_LIBRARIES}
        ${OIIO_LIBRARIES}
)
