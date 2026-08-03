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

# Object library: compiled once, consumed by both the shared library (with
# version-script restrictions) and internal test executables (unrestricted).
add_library(oakengine-obj OBJECT
        ${OLIVE_SOURCES}
        ${OLIVE_RESOURCES}
)

add_library(oakengine SHARED $<TARGET_OBJECTS:oakengine-obj>)

if (WIN32)
  # Windows DLLs cannot have undefined symbols; embed the version objects
  # here too (each module keeps its own copy, unlike ELF where the final
  # executable provides the definition).
  target_sources(oakengine PRIVATE $<TARGET_OBJECTS:olive-version-obj>)
endif ()

if (COMMAND oak_copy_otio_runtime)
  oak_copy_otio_runtime(oakengine)
endif()

# macOS: hides the render worker's dock icon; called from the worker main in
# src/capi/worker.cpp (declared there as a plain C++ symbol).
if (APPLE)
    target_sources(oakengine-obj PRIVATE src/worker_dockicon_mac.mm)
    target_link_libraries(oakengine PRIVATE "-framework Cocoa")
endif ()

add_subdirectory(common)
add_subdirectory(pluginSupport)

target_compile_features(oakengine-obj PUBLIC cxx_std_23)
set_target_properties(oakengine-obj PROPERTIES
        POSITION_INDEPENDENT_CODE ON
)
set_target_properties(oakengine PROPERTIES
        OUTPUT_NAME oakengine
)

# Consumers resolve engine headers ("node/...", "render/...", "coreengine.h",
# "tool/tool.h") from the engine root, and the public C
# API ("oakengine/ipc.h") from include/. The library itself builds against its
# internal implementation headers under src/oliveimpl, included with an
# "oliveimpl/"-prefixed path resolved from src/ (mirrors the src/oliveimpl
# comment in core/CMakeLists.txt). A plain include-order trick like core's
# cannot work here: CMAKE_INCLUDE_CURRENT_DIR puts the engine root ahead of
# every target-level directory, so the prefixed paths are what keep internal
# sources from ever picking up a consumer wrapper header by accident.
target_include_directories(oakengine-obj
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
        PRIVATE ${CMAKE_SOURCE_DIR}/third_party/openfx/include
        ${CMAKE_SOURCE_DIR}/third_party/openfx/HostSupport/include
        ${OLIVE_INCLUDE_DIRS}
)

target_link_libraries(oakengine-obj PUBLIC ${OLIVE_LIBRARIES} OfxHost)
target_link_libraries(oakengine PRIVATE oakengine-obj)
# Propagate engine header paths to consumers of the shared library
# (the OBJECT lib's PUBLIC includes don't transit through a PRIVATE link).
target_include_directories(oakengine
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)
# Consumers also need the third-party link deps (Qt, FFmpeg, etc.)
target_link_libraries(oakengine PUBLIC ${OLIVE_LIBRARIES} OfxHost)
# OAKENGINE_BUILD marks the library side of the C ABI export macros (dllexport)
target_compile_definitions(oakengine-obj PRIVATE ${OLIVE_DEFINITIONS} OAKENGINE_BUILD)
target_compile_options(oakengine-obj PRIVATE ${OLIVE_COMPILE_OPTIONS})

# Version script: only oakengine_* + render-backend plugin ABI are exported;
# all other C++ internals are hidden (local: *).
if (UNIX AND NOT APPLE)
    target_link_options(oakengine PRIVATE
            "LINKER:--version-script,${CMAKE_CURRENT_SOURCE_DIR}/oakengine.ver")
    set_target_properties(oakengine PROPERTIES LINK_DEPENDS
            ${CMAKE_CURRENT_SOURCE_DIR}/oakengine.ver)
endif ()

# olive::k_app_version is intentionally left undefined in the shared library
# and provided by the final executable via olive-version-obj (see
# worker/CMakeLists.txt). ELF allows undefined symbols in shared libraries by
# default; the macOS linker does not, so whitelist that one symbol.
if (APPLE)
    target_link_options(oakengine PRIVATE "LINKER:-U,__ZN5olive13k_app_versionE")
endif ()

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
option(OAK_ENABLE_DYNAMIC_RENDER_BACKEND
       "Build OpenGL/Vulkan render backends as dlopen plugins (oakgl/oakvulkan)" ON)
if (OAK_ENABLE_DYNAMIC_RENDER_BACKEND)
    target_compile_definitions(oakengine-obj PRIVATE OAK_ENABLE_DYNAMIC_RENDER_BACKEND)

    foreach (target olivecore kddockwidgets)
        if (TARGET ${target})
            set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
        endif ()
    endforeach ()

    if (WIN32)
        # Engine-internal C++ symbols are not exported from liboakengine.dll,
        # so embed the object library like the engine tests do. oakengine-obj
        # already contains openglrenderer.cpp and its moc output, so on
        # Windows the plugin only needs the C entry point (duplicating the
        # sources would cause duplicate definitions at link time). MinGW
        # compares type_info by name, so cross-DLL RTTI still works.
        add_library(oakgl SHARED render/opengl/openglbackend_c.cpp)
        set_target_properties(oakgl PROPERTIES AUTOMOC OFF)
        target_link_libraries(oakgl PRIVATE oakengine-obj
                $<TARGET_OBJECTS:olive-version-obj>)
    else ()
        add_library(oakgl SHARED
                render/opengl/openglbackend_c.cpp
                render/opengl/openglrenderer.cpp
                render/opengl/openglrenderer.h
        )
        target_link_libraries(oakgl PRIVATE oakengine)
    endif ()
    if (COMMAND oak_copy_otio_runtime)
        oak_copy_otio_runtime(oakgl)
    endif()
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
        if (WIN32)
            # Engine-internal C++ symbols are not exported from
            # liboakengine.dll; link the object library like the engine tests
            # (plus version obj for k_app_version). Unlike openglrenderer.cpp,
            # vulkanrenderer.cpp is NOT part of oakengine-obj, so the plugin
            # must compile it itself.
            add_library(oakvulkan SHARED
                    render/vulkan/vulkanbackend_c.cpp
                    render/vulkan/vulkanrenderer.cpp
                    render/vulkan/vulkanrenderer.h
            )
            target_link_libraries(oakvulkan PRIVATE oakengine-obj
                    $<TARGET_OBJECTS:olive-version-obj>)
        else ()
            add_library(oakvulkan SHARED
                    render/vulkan/vulkanbackend_c.cpp
                    render/vulkan/vulkanrenderer.cpp
                    render/vulkan/vulkanrenderer.h
            )
            target_link_libraries(oakvulkan PRIVATE oakengine)
        endif ()
        if (COMMAND oak_copy_otio_runtime)
            oak_copy_otio_runtime(oakvulkan)
        endif()
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
    include(GoogleTest)

    function(make_oakengine_gtest name)
        # oakengine leaves olive::k_app_version to the final executable; the
        # version object library provides it (same as worker/CMakeLists.txt).
        # Each facade test keeps its own binary because the tests have
        # heterogeneous oakengine_init() flag requirements; the shared
        # gtest_main.cpp only bootstraps Google Test (RUN_ALL_TESTS).
        add_executable(${name}
                tests/${name}.cpp
                tests/gtest_main.cpp
                $<TARGET_OBJECTS:olive-version-obj>
        )
        if (COMMAND oak_copy_otio_runtime)
            oak_copy_otio_runtime(${name})
        endif()
        # Link the object library directly to bypass the version-script
        # restrictions on liboakengine.so (tests are internal consumers).
        target_link_libraries(${name} PRIVATE oakengine-obj GTest::gtest)
        target_include_directories(${name} PRIVATE
                "${CMAKE_CURRENT_SOURCE_DIR}/include"
        )
        # -rdynamic exports the test executable's symbols so that dlopen'd
        # render backend plugins (oakgl/oakvulkan) can resolve the embedded
        # engine objects (Renderer base class, etc.).
        if (UNIX AND NOT APPLE)
            target_link_options(${name} PRIVATE "LINKER:--export-dynamic")
        endif ()
        if (WIN32)
            # The test embeds oakengine-obj; its OAKENGINE_API references must
            # match the library side (dllexport), otherwise MinGW looks for
            # __imp_* import stubs that only a real DLL import lib provides.
            target_compile_definitions(${name} PRIVATE OAKENGINE_BUILD)
        endif ()
        # Freshly linked binaries can exceed the 5s default discovery
        # timeout on first run (dyld cold cache + Qt/OCIO init)
        if (WIN32)
            # Defer discovery to ctest time; at build time the DLLs are not
            # next to the exe yet (copied below).
            gtest_discover_tests(${name} DISCOVERY_TIMEOUT 60
                                 DISCOVERY_MODE PRE_TEST)
            add_custom_command(TARGET ${name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        $<TARGET_FILE:oakengine> $<TARGET_FILE_DIR:${name}>
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        $<TARGET_FILE:olivecore> $<TARGET_FILE_DIR:${name}>
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        $<TARGET_FILE:ffmpeg_bridge> $<TARGET_FILE_DIR:${name}>)
        else ()
            gtest_discover_tests(${name} DISCOVERY_TIMEOUT 60)
        endif ()
    endfunction()

    make_oakengine_gtest(oakengine_ipc_test)

    make_oakengine_gtest(oakengine_worker_test)

    make_oakengine_gtest(oakengine_init_test)

    make_oakengine_gtest(oakengine_app_test)
    # Resolves the real test assets (tests/demo.mp4, the footage fixture
    # project) relative to the repository root, like tests/gtest does.
    target_compile_definitions(oakengine_init_test PRIVATE
            OAK_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )

    make_oakengine_gtest(oakengine_task_test)
    # The task test creates temporary projects and imports non-existent files,
    # so it only needs the headless engine services.

    make_oakengine_gtest(oakengine_config_test)
    # The config test exercises the QSettings-backed key/value store through
    # the C ABI; it is headless and does not touch the disk cache.

    make_oakengine_gtest(oakengine_audio_test)
    # The audio test exercises the AudioManager instance lifecycle, device
    # get/set round-trips and the output_params_changed event. It is headless
    # and only needs PortAudio initialization.

    make_oakengine_gtest(oakengine_disk_test)
    # The disk test exercises the DiskManager instance lifecycle, default cache
    # path queries, cache clearing, settings handler dispatch and default path
    # mutation. It is headless and only touches temporary directories.

    make_oakengine_gtest(oakengine_proxy_test)
    # The proxy test exercises proxy parameter defaults, state string
    # round-trips and the ProxyManager singleton lifecycle. It is headless.

    make_oakengine_gtest(oakengine_lut_test)
    # The LUT test exercises directory/file list queries and the
    # set_directories round-trip. It is headless.

    make_oakengine_gtest(oakengine_serializer_test)
    # The serializer test exercises compressed-project detection, clipboard
    # create/free/copy and empty-node copy. It is headless.

    make_oakengine_gtest(oakengine_renderer_test)
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

    make_oakengine_gtest(oakengine_footage_test)
    target_compile_definitions(oakengine_footage_test PRIVATE
            OAK_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )

    make_oakengine_gtest(oakengine_timeline_edit_test)
    target_compile_definitions(oakengine_timeline_edit_test PRIVATE
            OAK_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )

    make_oakengine_gtest(oakengine_events_test)
    target_compile_definitions(oakengine_events_test PRIVATE
            OAK_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )

    make_oakengine_gtest(oakengine_encoding_test)

    make_oakengine_gtest(oakengine_color_test)

    make_oakengine_gtest(oakengine_export_test)
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

    make_oakengine_gtest(oakengine_node_test)

    make_oakengine_gtest(oakengine_nodevalue_test)
    target_compile_definitions(oakengine_node_test PRIVATE
            OAK_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )

    make_oakengine_gtest(oakengine_keyframe_test)
    target_compile_definitions(oakengine_keyframe_test PRIVATE
            OAK_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )

    make_oakengine_gtest(oakengine_viewer_test)
    target_compile_definitions(oakengine_viewer_test PRIVATE
            OAK_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )

    make_oakengine_gtest(oakengine_traverse_test)
    target_compile_definitions(oakengine_traverse_test PRIVATE
            OAK_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )

    make_oakengine_gtest(oakengine_preview_test)
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

    make_oakengine_gtest(oakengine_playback_test)
    # The playback test builds sequence content through the engine C++ API
    # and pulls frames through the render worker pool (same needs as
    # oakengine_renderer_test).
    target_include_directories(oakengine_playback_test PRIVATE
            ${CMAKE_SOURCE_DIR}/third_party/openfx/include
            ${CMAKE_SOURCE_DIR}/third_party/openfx/HostSupport/include
            ${OLIVE_INCLUDE_DIRS}
    )
    target_compile_definitions(oakengine_playback_test PRIVATE
            ${OLIVE_DEFINITIONS}
            OAK_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )
    target_compile_options(oakengine_playback_test PRIVATE
            ${OLIVE_COMPILE_OPTIONS}
    )
    if (OAK_ENABLE_DYNAMIC_RENDER_BACKEND)
        target_compile_definitions(oakengine_playback_test PRIVATE
                OAK_ENABLE_DYNAMIC_RENDER_BACKEND)
        add_dependencies(oakengine_playback_test oakgl)
        if (TARGET oakvulkan)
            add_dependencies(oakengine_playback_test oakvulkan)
        endif ()
    endif ()
    if (TARGET olive-render-worker)
        add_dependencies(oakengine_playback_test olive-render-worker)
    endif ()

    make_oakengine_gtest(oakengine_sync_test)
    # The sync test renders the clips' audio through the worker pool (same
    # needs as oakengine_playback_test).
    target_include_directories(oakengine_sync_test PRIVATE
            ${CMAKE_SOURCE_DIR}/third_party/openfx/include
            ${CMAKE_SOURCE_DIR}/third_party/openfx/HostSupport/include
            ${OLIVE_INCLUDE_DIRS}
    )
    target_compile_definitions(oakengine_sync_test PRIVATE
            ${OLIVE_DEFINITIONS}
            OAK_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )
    target_compile_options(oakengine_sync_test PRIVATE
            ${OLIVE_COMPILE_OPTIONS}
    )
    if (OAK_ENABLE_DYNAMIC_RENDER_BACKEND)
        target_compile_definitions(oakengine_sync_test PRIVATE
                OAK_ENABLE_DYNAMIC_RENDER_BACKEND)
        add_dependencies(oakengine_sync_test oakgl)
        if (TARGET oakvulkan)
            add_dependencies(oakengine_sync_test oakvulkan)
        endif ()
    endif ()
    if (TARGET olive-render-worker)
        add_dependencies(oakengine_sync_test olive-render-worker)
    endif ()
endif ()
