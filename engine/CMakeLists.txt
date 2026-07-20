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

# Add version object
add_library(olive-version-obj
        OBJECT
        version.cpp
        version.h
)
target_link_libraries(olive-version-obj PRIVATE Qt${QT_VERSION_MAJOR}::Core)
target_compile_options(olive-version-obj PRIVATE -DAPPVERSION="${PROJECT_VERSION}" -DAPPVERSIONLONG="${PROJECT_LONG_VERSION}")

# Engine library (liboakengine): the node graph, render pipeline, codecs,
# timeline, task system and engine services, independent of the editor UI.
# The editor (app/) and the render worker (worker/) both link against it.

set(OLIVE_SOURCES
        coreengine.h
        coreengine.cpp
)

add_subdirectory(audio)
add_subdirectory(cli)
add_subdirectory(codec)
add_subdirectory(config)
add_subdirectory(node)
add_subdirectory(render)
add_subdirectory(shaders)
add_subdirectory(task)
add_subdirectory(timeline)
add_subdirectory(tool)
add_subdirectory(ui)
add_subdirectory(undo)
add_subdirectory(src/capi)

add_library(oakengine SHARED
        ${OLIVE_SOURCES}
        ${OLIVE_RESOURCES}
)

add_subdirectory(common)
add_subdirectory(pluginSupport)

target_compile_features(oakengine PUBLIC cxx_std_23)
set_target_properties(oakengine PROPERTIES
        OUTPUT_NAME oakengine
        POSITION_INDEPENDENT_CODE ON
)

# Consumers resolve engine headers ("node/...", "render/...", "coreengine.h",
# "ui/icons/icons.h", "tool/tool.h") from the engine root, and the public C
# API ("oakengine/ipc.h") from include/. The library itself builds against its
# internal implementation headers under src/oliveimpl, included with an
# "oliveimpl/"-prefixed path resolved from src/ (mirrors the src/oliveimpl
# comment in core/CMakeLists.txt). A plain include-order trick like core's
# cannot work here: CMAKE_INCLUDE_CURRENT_DIR puts the engine root ahead of
# every target-level directory, so the prefixed paths are what keep internal
# sources from ever picking up a consumer wrapper header by accident.
target_include_directories(oakengine
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
        PRIVATE ${CMAKE_SOURCE_DIR}/third_party/openfx/include
        ${CMAKE_SOURCE_DIR}/third_party/openfx/HostSupport/include
        ${OLIVE_INCLUDE_DIRS}
)

target_link_libraries(oakengine PUBLIC ${OLIVE_LIBRARIES} OfxHost)
# OAKENGINE_BUILD marks the library side of the C ABI export macros (dllexport)
target_compile_definitions(oakengine PRIVATE ${OLIVE_DEFINITIONS} OAKENGINE_BUILD)
target_compile_options(oakengine PRIVATE ${OLIVE_COMPILE_OPTIONS})

# Install into the platform's standard library directory (/usr/lib,
# /usr/lib64 or the Debian multiarch path); Windows DLLs go next to the
# executables in bin.
include(GNUInstallDirs)
if (WIN32)
    install(TARGETS oakengine
            RUNTIME DESTINATION bin
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
else ()
    install(TARGETS oakengine
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif ()

if (UNIX AND NOT APPLE)
    set_target_properties(oakengine PROPERTIES INSTALL_RPATH "$ORIGIN")
endif ()

# Dynamic render backends (OpenGL/Vulkan) are engine plugins loaded via
# dlopen; they share this engine library instead of embedding a static
# render-core subset.
if (OAK_ENABLE_DYNAMIC_RENDER_BACKEND)
    target_compile_definitions(oakengine PRIVATE OAK_ENABLE_DYNAMIC_RENDER_BACKEND)

    foreach (target olivecore kddockwidgets)
        if (TARGET ${target})
            set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
        endif ()
    endforeach ()

    add_library(oakgl SHARED
            render/opengl/openglbackend_c.cpp
            render/opengl/openglrenderer.cpp
            render/opengl/openglrenderer.h
    )
    target_link_libraries(oakgl PRIVATE oakengine)
    target_include_directories(oakgl PRIVATE ${OLIVE_INCLUDE_DIRS})
    target_compile_definitions(oakgl PRIVATE OAK_RENDER_BACKEND_PLUGIN)
    set_target_properties(oakgl PROPERTIES
            OUTPUT_NAME oakgl
            LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
    if (WIN32)
        set_target_properties(oakgl PROPERTIES PREFIX "")
    endif ()
    install(TARGETS oakgl
            RUNTIME DESTINATION bin
            LIBRARY DESTINATION lib
            ARCHIVE DESTINATION lib
    )

    if (Vulkan_FOUND)
        add_library(oakvulkan SHARED
                render/vulkan/vulkanbackend_c.cpp
                render/vulkan/vulkanrenderer.cpp
                render/vulkan/vulkanrenderer.h
        )
        target_link_libraries(oakvulkan PRIVATE oakengine)
        target_include_directories(oakvulkan PRIVATE ${OLIVE_INCLUDE_DIRS})
        target_link_libraries(oakvulkan PRIVATE Vulkan::Vulkan)
        target_compile_definitions(oakvulkan PRIVATE OAK_HAS_VULKAN)
        if (SHADERC_FOUND)
            target_link_libraries(oakvulkan PRIVATE ${SHADERC_LIBRARIES})
            target_include_directories(oakvulkan PRIVATE ${SHADERC_INCLUDE_DIRS})
            target_compile_definitions(oakvulkan PRIVATE OAK_HAS_SHADERC)
        endif ()
        set_target_properties(oakvulkan PROPERTIES
                OUTPUT_NAME oakvulkan
                LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
                RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
                ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        )
        if (WIN32)
            set_target_properties(oakvulkan PROPERTIES PREFIX "")
        endif ()
        install(TARGETS oakvulkan
                RUNTIME DESTINATION bin
                LIBRARY DESTINATION lib
                ARCHIVE DESTINATION lib
        )
    endif ()

    add_library(oakgl-cabi-check OBJECT
            render/opengl/openglbackend_c.cpp
    )
    target_include_directories(oakgl-cabi-check PRIVATE
            ${CMAKE_SOURCE_DIR}/app
            ${CMAKE_SOURCE_DIR}/third_party/openfx/include
            ${CMAKE_SOURCE_DIR}/third_party/openfx/HostSupport/include
            ${OLIVE_INCLUDE_DIRS}
    )
    target_link_libraries(oakgl-cabi-check PRIVATE ${OLIVE_LIBRARIES} OfxHost)
    target_compile_definitions(oakgl-cabi-check PRIVATE ${OLIVE_DEFINITIONS})
    target_compile_options(oakgl-cabi-check PRIVATE ${OLIVE_COMPILE_OPTIONS})

    if (Vulkan_FOUND)
        add_library(oakvulkan-cabi-check OBJECT
                render/vulkan/vulkanbackend_c.cpp
                render/vulkan/vulkanrenderer.cpp
                render/vulkan/vulkanrenderer.h
        )
        target_include_directories(oakvulkan-cabi-check PRIVATE
                ${CMAKE_SOURCE_DIR}/app
                ${CMAKE_SOURCE_DIR}/third_party/openfx/include
                ${CMAKE_SOURCE_DIR}/third_party/openfx/HostSupport/include
                ${OLIVE_INCLUDE_DIRS}
        )
        target_link_libraries(oakvulkan-cabi-check PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Gui Qt${QT_VERSION_MAJOR}::Widgets)
        target_link_libraries(oakvulkan-cabi-check PRIVATE Vulkan::Vulkan)
        target_compile_definitions(oakvulkan-cabi-check PRIVATE OAK_HAS_VULKAN)
        if (SHADERC_FOUND)
            target_link_libraries(oakvulkan-cabi-check PRIVATE ${SHADERC_LIBRARIES})
            target_include_directories(oakvulkan-cabi-check PRIVATE ${SHADERC_INCLUDE_DIRS})
            target_compile_definitions(oakvulkan-cabi-check PRIVATE OAK_HAS_SHADERC)
        endif ()
        target_compile_definitions(oakvulkan-cabi-check PRIVATE ${OLIVE_DEFINITIONS})
        target_compile_options(oakvulkan-cabi-check PRIVATE ${OLIVE_COMPILE_OPTIONS})
    endif ()
endif ()

# Pure C ABI tests for the liboakengine public interface (mirrors the
# make_test pattern in core/CMakeLists.txt). They link only against oakengine
# and must not depend on OpenGL/Vulkan.
if (BUILD_TESTS)
    enable_testing()

    function(make_oakengine_test name)
        # oakengine leaves olive::k_app_version to the final executable; the
        # version object library provides it (same as worker/CMakeLists.txt).
        add_executable(${name}
                tests/${name}.cpp
                $<TARGET_OBJECTS:olive-version-obj>
        )
        target_link_libraries(${name} PRIVATE oakengine)
        target_include_directories(${name} PRIVATE
                "${CMAKE_CURRENT_SOURCE_DIR}/include"
        )
        add_test(${name} ${name})
    endfunction()

    make_oakengine_test(oakengine_ipc_test)

    make_oakengine_test(oakengine_init_test)
    # Resolves the real test assets (tests/demo.mp4, the footage fixture
    # project) relative to the repository root, like tests/gtest does.
    target_compile_definitions(oakengine_init_test PRIVATE
            OAK_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )

    make_oakengine_test(oakengine_renderer_test)
    # The renderer test builds sequence content through the engine C++ API
    # (allowed for engine-internal tests) and probes the dynamic render
    # backend the same way the gtest harness does, so it needs the internal
    # include paths and backend definitions (mirrors tests/gtest).
    target_include_directories(oakengine_renderer_test PRIVATE
            ${CMAKE_SOURCE_DIR}/third_party/openfx/include
            ${CMAKE_SOURCE_DIR}/third_party/openfx/HostSupport/include
            ${OLIVE_INCLUDE_DIRS}
    )
    target_compile_definitions(oakengine_renderer_test PRIVATE
            ${OLIVE_DEFINITIONS}
            OAK_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )
    target_compile_options(oakengine_renderer_test PRIVATE
            ${OLIVE_COMPILE_OPTIONS}
    )
    if (OAK_ENABLE_DYNAMIC_RENDER_BACKEND)
        target_compile_definitions(oakengine_renderer_test PRIVATE
                OAK_ENABLE_DYNAMIC_RENDER_BACKEND)
        add_dependencies(oakengine_renderer_test oakgl)
        if (TARGET oakvulkan)
            add_dependencies(oakengine_renderer_test oakvulkan)
        endif ()
    endif ()
    # render_frame goes through the render worker pool; make sure the worker
    # binary in the build tree is up to date.
    if (TARGET olive-render-worker)
        add_dependencies(oakengine_renderer_test olive-render-worker)
    endif ()

    make_oakengine_test(oakengine_footage_test)
    target_compile_definitions(oakengine_footage_test PRIVATE
            OAK_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )

    make_oakengine_test(oakengine_timeline_edit_test)
    target_compile_definitions(oakengine_timeline_edit_test PRIVATE
            OAK_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )

    make_oakengine_test(oakengine_export_test)
    # The export test builds sequence content through the engine C++ API and
    # probes the dynamic render backend like oakengine_renderer_test does.
    target_include_directories(oakengine_export_test PRIVATE
            ${CMAKE_SOURCE_DIR}/third_party/openfx/include
            ${CMAKE_SOURCE_DIR}/third_party/openfx/HostSupport/include
            ${OLIVE_INCLUDE_DIRS}
    )
    target_compile_definitions(oakengine_export_test PRIVATE
            ${OLIVE_DEFINITIONS}
            OAK_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )
    target_compile_options(oakengine_export_test PRIVATE
            ${OLIVE_COMPILE_OPTIONS}
    )
    if (OAK_ENABLE_DYNAMIC_RENDER_BACKEND)
        target_compile_definitions(oakengine_export_test PRIVATE
                OAK_ENABLE_DYNAMIC_RENDER_BACKEND)
        add_dependencies(oakengine_export_test oakgl)
        if (TARGET oakvulkan)
            add_dependencies(oakengine_export_test oakvulkan)
        endif ()
    endif ()
    if (TARGET olive-render-worker)
        add_dependencies(oakengine_export_test olive-render-worker)
    endif ()

    make_oakengine_test(oakengine_node_test)
    target_compile_definitions(oakengine_node_test PRIVATE
            OAK_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )

    make_oakengine_test(oakengine_keyframe_test)
    target_compile_definitions(oakengine_keyframe_test PRIVATE
            OAK_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )

    make_oakengine_test(oakengine_preview_test)
    # The preview test needs audio rendering (RenderManager + workers).
    target_include_directories(oakengine_preview_test PRIVATE
            ${CMAKE_SOURCE_DIR}/third_party/openfx/include
            ${CMAKE_SOURCE_DIR}/third_party/openfx/HostSupport/include
            ${OLIVE_INCLUDE_DIRS}
    )
    target_compile_definitions(oakengine_preview_test PRIVATE
            ${OLIVE_DEFINITIONS}
            OAK_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )
    target_compile_options(oakengine_preview_test PRIVATE
            ${OLIVE_COMPILE_OPTIONS}
    )
    if (OAK_ENABLE_DYNAMIC_RENDER_BACKEND)
        target_compile_definitions(oakengine_preview_test PRIVATE
                OAK_ENABLE_DYNAMIC_RENDER_BACKEND)
        add_dependencies(oakengine_preview_test oakgl)
    endif ()
    if (TARGET olive-render-worker)
        add_dependencies(oakengine_preview_test olive-render-worker)
    endif ()
endif ()
