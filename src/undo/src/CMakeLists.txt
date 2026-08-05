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

add_library(oakundo SHARED
        undocommand.cpp
        undocommand.h
        undostack.cpp
        undostack.h
)

# In a full-tree build the repo root is CMAKE_SOURCE_DIR; a standalone
# build (see src/undo/standalone) sets OAK_REPO_ROOT explicitly.
if(NOT DEFINED OAK_REPO_ROOT)
  set(OAK_REPO_ROOT ${CMAKE_SOURCE_DIR})
endif()

# oakundo depends only on oakcommon's header-only define.h macros
# (DISABLE_COPY_MOVE), so an include path is enough; no library is linked.
target_include_directories(oakundo PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${OAK_REPO_ROOT}/src/common/src
        ${OAK_REPO_ROOT}/include
)
