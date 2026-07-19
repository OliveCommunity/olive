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

# Render worker process (oak-render-worker).
#
# The worker is a headless render process spawned by the editor through
# RenderWorkerPool. It reuses the libolive-editor object library so it
# shares the exact same render/node/codec code as the editor. The link set
# is currently the full OLIVE_LIBRARIES for simplicity; trimming UI-only
# dependencies is a later-phase cleanup (render-process-isolation plan).
add_executable(olive-render-worker
        workermain.cpp
        $<TARGET_OBJECTS:libolive-editor>
        $<TARGET_OBJECTS:olive-version-obj>
)
set_target_properties(olive-render-worker PROPERTIES OUTPUT_NAME "oak-render-worker")

if (APPLE)
    target_sources(olive-render-worker PRIVATE workermain_mac.mm)
    target_link_libraries(olive-render-worker PRIVATE "-framework Cocoa")
endif ()

# CMAKE_INCLUDE_CURRENT_DIR only covers the app/ scope, so add the app
# include roots explicitly now that the worker lives outside it
target_include_directories(olive-render-worker PRIVATE
        ${CMAKE_SOURCE_DIR}/app
        ${CMAKE_BINARY_DIR}/app
        ${CMAKE_SOURCE_DIR}/app/pluginSupport
        ${OLIVE_INCLUDE_DIRS})

target_link_libraries(olive-render-worker PUBLIC OfxHost)
target_link_libraries(olive-render-worker PRIVATE ${OLIVE_LIBRARIES})
target_compile_options(olive-render-worker PRIVATE ${OLIVE_COMPILE_OPTIONS})
target_compile_definitions(olive-render-worker PRIVATE ${OLIVE_DEFINITIONS})

if (OAK_ENABLE_DYNAMIC_RENDER_BACKEND)
    target_compile_definitions(olive-render-worker PRIVATE OAK_ENABLE_DYNAMIC_RENDER_BACKEND)
    add_dependencies(olive-render-worker oakgl)
    if (TARGET oakvulkan)
        add_dependencies(olive-render-worker oakvulkan)
    endif ()
endif ()

# The ffmpeg_bridge shared library ships next to the binaries: inside the
# macOS app bundle (Contents/MacOS, resolved via @loader_path), and in the
# standard lib directory on other platforms.
if (APPLE)
    set_target_properties(olive-render-worker PROPERTIES
            BUILD_RPATH "@loader_path"
            INSTALL_RPATH "@loader_path")
elseif (UNIX)
    set_target_properties(olive-render-worker PROPERTIES
            INSTALL_RPATH "$ORIGIN/../lib")
endif ()

# The editor always needs the worker next to it (macOS bundle POST_BUILD
# copies, Linux install() into the same bin directory)
add_dependencies(olive-editor olive-render-worker)

if (WIN32)
    # Windows has no RPATH: shared libraries must sit next to the executable
    add_custom_command(TARGET olive-render-worker POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:ffmpeg_bridge> $<TARGET_FILE_DIR:olive-render-worker>
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:olivecore> $<TARGET_FILE_DIR:olive-render-worker>
    )
endif ()

# Install the render worker alongside the editor on Linux.
if (UNIX AND NOT APPLE)
    install(TARGETS olive-render-worker RUNTIME DESTINATION bin)
endif ()
