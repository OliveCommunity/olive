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

# oak-cli: headless command-line consumer of the liboakengine C ABI facade.
# It is the first real consumer of the facade and therefore links ONLY
# against liboakengine (which publicly re-exports liboakcore); the source
# includes only the public oakengine/*.h C headers -- no engine C++ headers,
# no Qt headers.

add_executable(oak-cli
        main.cpp
)

if (COMMAND oak_copy_otio_runtime)
  oak_copy_otio_runtime(oak-cli)
endif()

# oakengine leaves olive::k_app_version to the final executable; the version
# object library provides it (same as the engine C ABI tests and worker).
target_sources(oak-cli PRIVATE $<TARGET_OBJECTS:olive-version-obj>)

target_link_libraries(oak-cli PRIVATE oakengine)
target_include_directories(oak-cli PRIVATE
        ${CMAKE_SOURCE_DIR}/engine/include
)
set_target_properties(oak-cli PROPERTIES
        OUTPUT_NAME oak-cli
)

if (APPLE)
    # Distribute inside Oak.app/Contents/MacOS next to the other helper
    # binaries (mirrors the oak-render-worker copies in app/CMakeLists.txt).
    add_custom_command(TARGET oak-cli POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_BUNDLE_DIR:olive-editor>/Contents/MacOS
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:oak-cli> $<TARGET_BUNDLE_DIR:olive-editor>/Contents/MacOS/
            COMMENT "Copying oak-cli into Oak.app"
    )
elseif (UNIX)
    install(TARGETS oak-cli RUNTIME DESTINATION bin)
    # liboakengine installs into the platform lib dir (see
    # engine/CMakeLists.txt), resolve it relative to the binary.
    set_target_properties(oak-cli PROPERTIES INSTALL_RPATH "$ORIGIN/../lib")
endif ()

if (WIN32)
    # Windows has no RPATH: shared libraries must sit next to the executable
    # (mirrors tests/gtest/CMakeLists.txt).
    add_custom_command(TARGET oak-cli POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:ffmpeg_bridge> $<TARGET_FILE_DIR:oak-cli>
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:olivecore> $<TARGET_FILE_DIR:oak-cli>
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:oakengine> $<TARGET_FILE_DIR:oak-cli>
    )
endif ()

if (BUILD_TESTS)
    enable_testing()

    # info always runs (headless); the output must mention the fixture's
    # sequence name and footage filename.
    add_test(NAME oak_cli_info
            COMMAND oak-cli info ${CMAKE_SOURCE_DIR}/tests/project_with_footage.ove
    )
    set_tests_properties(oak_cli_info PROPERTIES
            PASS_REGULAR_EXPRESSION "Fixture Sequence.*demo\\.mp4"
    )

    # Render smoke test: without a GL render backend oak-cli reports the
    # failure and exits 2, which ctest treats as a skip.
    add_test(NAME oak_cli_render
            COMMAND oak-cli render ${CMAKE_SOURCE_DIR}/tests/project_with_footage.ove 0 1 ${CMAKE_CURRENT_BINARY_DIR}/oak_cli_render_out
    )
    set_tests_properties(oak_cli_render PROPERTIES
            SKIP_RETURN_CODE 2
    )

    # probe is headless and always runs; the output must mention the demo
    # file's 1920x1080 video and 48000 Hz audio.
    add_test(NAME oak_cli_probe
            COMMAND oak-cli probe ${CMAKE_SOURCE_DIR}/tests/demo.mp4
    )
    set_tests_properties(oak_cli_probe PROPERTIES
            PASS_REGULAR_EXPRESSION "1920.*48000"
    )

    # transcode smoke test: full "media in, renders out" round trip to a
    # single H.264 MP4 (the default format); like render it exits 2 without
    # a GL backend (skip). The verify step then checks the file with ffprobe.
    add_test(NAME oak_cli_transcode
            COMMAND oak-cli transcode ${CMAKE_SOURCE_DIR}/tests/demo.mp4 ${CMAKE_CURRENT_BINARY_DIR}/oak_cli_transcode.mp4 960
    )
    set_tests_properties(oak_cli_transcode PROPERTIES
            SKIP_RETURN_CODE 2
    )
    add_test(NAME oak_cli_transcode_verify
            COMMAND ${CMAKE_COMMAND} -DOUT=${CMAKE_CURRENT_BINARY_DIR}/oak_cli_transcode.mp4 -P ${CMAKE_CURRENT_SOURCE_DIR}/verify_transcode_mp4.cmake
    )
    set_tests_properties(oak_cli_transcode_verify PROPERTIES
            DEPENDS oak_cli_transcode
    )

    # Same round trip through the PPM+WAV path.
    add_test(NAME oak_cli_transcode_ppm
            COMMAND oak-cli transcode ${CMAKE_SOURCE_DIR}/tests/demo.mp4 ${CMAKE_CURRENT_BINARY_DIR}/oak_cli_transcode_ppm_out 960 --format ppm
    )
    set_tests_properties(oak_cli_transcode_ppm PROPERTIES
            SKIP_RETURN_CODE 2
    )
    # Rendering needs the render worker and the dynamic backend plugins.
    if (TARGET olive-render-worker)
        add_dependencies(oak-cli olive-render-worker)
    endif ()
    if (TARGET oakgl)
        add_dependencies(oak-cli oakgl)
    endif ()
    if (TARGET oakvulkan)
        add_dependencies(oak-cli oakvulkan)
    endif ()
endif ()
