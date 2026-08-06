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

add_library(oakcommon SHARED
        commandlineparser.cpp
        commandlineparser.h
        colortransform.h
        configstore.cpp
        configstore.h
        current.cpp
        current.h
        debug.cpp
        debug.h
        decibel.h
        define.h
        dropworkflowbehavior.h
        loopmode.h
        subtitleparams.cpp
        subtitleparams.h
        videoparams.cpp
        videoparams.h
        ffmpegutils.cpp
        ffmpegutils.h
        filefunctions.cpp
        filefunctions.h
        lerp.h
        memorypool.h
        ocioutils.cpp
        ocioutils.h
        oiioutils.cpp
        oiioutils.h
        power.h
        qtutils.cpp
        qtutils.h
        threadsafemap.h
        xmlutils.cpp
        xmlutils.h
)

# In a full-tree build the repo root is CMAKE_SOURCE_DIR; a standalone
# build (see src/common/standalone) sets OAK_REPO_ROOT explicitly.
if(NOT DEFINED OAK_REPO_ROOT)
  set(OAK_REPO_ROOT ${CMAKE_SOURCE_DIR})
endif()

target_include_directories(oakcommon PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${OAK_REPO_ROOT}/include
        ${OAK_REPO_ROOT}/third_party/openfx/include
        ${OCIO_INCLUDE_DIRS}
        ${OIIO_INCLUDE_DIRS}
)

# Public headers of oakcommon include the olive/core wrapper headers
# (pixelformat.h, sampleformat.h, rational.h) and the ffmpeg_bridge C ABI
# header. Their include dirs are PRIVATE to those targets, so re-export them.
target_include_directories(oakcommon PUBLIC
        ${OAK_REPO_ROOT}/core/include
        ${OAK_REPO_ROOT}/ffmpeg_bridge/include
)

target_link_libraries(oakcommon PUBLIC
        olivecore
        ffmpeg_bridge
        EXPAT::EXPAT
        ${OCIO_LIBRARIES}
        ${OIIO_LIBRARIES}
)
