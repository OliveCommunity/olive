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
        include/oakengine/events.h
        include/oakengine/app.h
        include/oakengine/project.h
        include/oakengine/timeline.h
        include/oakengine/renderer.h
        include/oakengine/display.h
        include/oakengine/footage.h
        include/oakengine/exporter.h
        include/oakengine/encoding.h
        include/oakengine/videoparams.h
        include/oakengine/color.h
        include/oakengine/node.h
        include/oakengine/playback.h
        include/oakengine/preview.h
        include/oakengine/sync.h
        include/oakengine/worker.h
        include/oakengine/viewer.h
        include/oakengine/traverse.h
        include/oakengine/task.h
        include/oakengine/undo.h
        include/oakengine/config.h
        include/oakengine/audio.h
        include/oakengine/disk.h
        include/oakengine/proxy.h
        include/oakengine/lut.h
        include/oakengine/serializer.h
        include/oakengine/plugin.h
        include/oakengine/gizmo.h
        src/capi/init.cpp
        src/capi/events.cpp
        src/capi/app.cpp
        src/capi/project.cpp
        src/capi/timeline.cpp
        src/capi/renderer.cpp
        src/capi/display.cpp
        src/capi/footage.cpp
        src/capi/export.cpp
        src/capi/encoding.cpp
        src/capi/color.cpp
        src/capi/node.cpp
        src/capi/playback.cpp
        src/capi/preview.cpp
        src/capi/sync.cpp
        src/capi/worker.cpp
        src/capi/viewer.cpp
        src/capi/traverse.cpp
        src/capi/task.cpp
        src/capi/undo.cpp
        src/capi/config.cpp
        src/capi/audio.cpp
        src/capi/disk.cpp
        src/capi/proxy.cpp
        src/capi/lut.cpp
        src/capi/serializer.cpp
        src/capi/plugin.cpp
        src/capi/gizmo.cpp
        PARENT_SCOPE
)
