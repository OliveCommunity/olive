# Oak - Non-Linear Video Editor
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

# C ABI facade of liboakengine (mirrors the pattern documented in
# render/ipc/CMakeLists.txt):
#   - include/oakengine/*.h   public C API
#   - src/capi/*.cpp          C ABI implementations
# The init/project/timeline families wrap the engine core, the project
# serializer and the sequence/viewer timeline objects.
set(OLIVE_SOURCES
        ${OLIVE_SOURCES}
        include/oakengine/init.h
        include/oakengine/project.h
        include/oakengine/timeline.h
        include/oakengine/renderer.h
        include/oakengine/footage.h
        src/capi/init.cpp
        src/capi/project.cpp
        src/capi/timeline.cpp
        src/capi/renderer.cpp
        src/capi/footage.cpp
        PARENT_SCOPE
)
