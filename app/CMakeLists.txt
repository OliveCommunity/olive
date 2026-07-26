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

# Set Olive sources and resources
set(OLIVE_SOURCES
        core.h
        core.cpp
        engineeventbridge.h
        engineeventbridge.cpp
        common/colorcodingapp.h
        common/nodevaluehandle.h
)

#set(OLIVE_RESOURCES)

# Add subdirectories, which will populate the above variables
add_subdirectory(dialog)
add_subdirectory(packaging)
add_subdirectory(panel)
add_subdirectory(timeline)
add_subdirectory(ts)
add_subdirectory(ui)
add_subdirectory(widget)
add_subdirectory(window)

# Add translations
qt_add_translation(OLIVE_QM_FILES ${OLIVE_TS_FILES})

set(QRC_BODY "")
foreach (QM_FILE ${OLIVE_QM_FILES})
    get_filename_component(QM_FILENAME_COMPONENT ${QM_FILE} NAME_WE)
    string(APPEND QRC_BODY "<file alias=\"${QM_FILENAME_COMPONENT}\">${QM_FILE}</file>\n")
endforeach ()
configure_file(ts/translations.qrc.in ts/translations.qrc @ONLY)

set(OLIVE_RESOURCES
        ${OLIVE_RESOURCES}
        ${CMAKE_CURRENT_BINARY_DIR}/ts/translations.qrc
        widget/nodeparamview/nodeparambutton.cpp
        widget/nodeparamview/nodeparambutton.h
)

# Add main library
add_library(libolive-editor
        OBJECT
        ${OLIVE_SOURCES}
        ${OLIVE_RESOURCES}
)

target_compile_features(libolive-editor PUBLIC cxx_std_23)
include_directories(../third_party/openfx/include
        ../third_party/openfx/HostSupport/include)
# Remove prefix - prevents CMake calling it "liblibolive-editor"
set_target_properties(libolive-editor PROPERTIES PREFIX "")

option(OAK_ENABLE_DYNAMIC_RENDER_BACKEND "Build and use the dynamic render backend adapter" ON)
if (OAK_ENABLE_DYNAMIC_RENDER_BACKEND)
    set_target_properties(libolive-editor PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_compile_definitions(libolive-editor PRIVATE OAK_ENABLE_DYNAMIC_RENDER_BACKEND)

    foreach (target olivecore kddockwidgets)
        if (TARGET ${target})
            set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
        endif ()
    endforeach ()
endif ()

# Add application
add_executable(olive-editor
        main.cpp
        $<TARGET_OBJECTS:libolive-editor>
        $<TARGET_OBJECTS:olive-version-obj>
)

target_link_libraries(olive-editor PUBLIC OfxHost)
add_dependencies(olive-editor oakgl-cabi-check)
if (OAK_ENABLE_DYNAMIC_RENDER_BACKEND)
    add_dependencies(olive-editor oakgl)
    if (TARGET oakvulkan)
        add_dependencies(olive-editor oakvulkan)
    endif ()
endif ()
set_target_properties(olive-editor PROPERTIES OUTPUT_NAME "oak-editor")

# Create docs if doxygen was found
if (DOXYGEN_FOUND)
    set(DOXYGEN_PROJECT_NAME "Oak Video Editor")
    set(DOXYGEN_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/docs")
    set(DOXYGEN_EXTRACT_ALL "YES")
    set(DOXYGEN_EXTRACT_PRIVATE "YES")
    doxygen_add_docs(docs ALL ${OLIVE_SOURCES})
endif ()

# Platform-specific deployment preferences
if (WIN32)
    # Set Windows application icon
    target_sources(olive-editor PRIVATE packaging/windows/resources.rc)

    # Preserve folder structure in visual studio
    source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${OLIVE_SOURCES})

elseif (APPLE)
    # Set Mac application icon
    set(OLIVE_ICON packaging/macos/oak.icns)
    target_sources(olive-editor PRIVATE ${OLIVE_ICON})

    # Set Mac bundle properties
    set_target_properties(olive-editor PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/MacOSXBundleInfo.plist.in
            MACOSX_BUNDLE_GUI_IDENTIFIER org.oakvideoeditor.Oak
            MACOSX_BUNDLE_ICON_FILE oak.icns
            MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
            MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
            MACOSX_BUNDLE_BUNDLE_NAME "Oak Video Editor"
            MACOSX_BUNDLE_INFO_STRING "Oak Video Editor ${PROJECT_LONG_VERSION}"
            MACOSX_BUNDLE_COPYRIGHT "©2018-2021 Olive Studios LLC and others. Fork maintained by Oak Video Editor Team."
            RESOURCE "${OLIVE_ICON}"
            OUTPUT_NAME "Oak"
    )

    # Copy the render worker, dynamic render backends, and the FFmpeg bridge
    # library into the app bundle. They are looked up in
    # QCoreApplication::applicationDirPath(), which on macOS points to
    # Oak.app/Contents/MacOS.
    add_custom_command(TARGET olive-editor POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_BUNDLE_DIR:olive-editor>/Contents/MacOS
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:olive-render-worker> $<TARGET_BUNDLE_DIR:olive-editor>/Contents/MacOS/
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:oakgl> $<TARGET_BUNDLE_DIR:olive-editor>/Contents/MacOS/
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:ffmpeg_bridge> $<TARGET_BUNDLE_DIR:olive-editor>/Contents/MacOS/
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:olivecore> $<TARGET_BUNDLE_DIR:olive-editor>/Contents/MacOS/
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:oakengine> $<TARGET_BUNDLE_DIR:olive-editor>/Contents/MacOS/
            COMMENT "Copying oak-render-worker, render backends, ffmpeg_bridge, liboakcore and liboakengine into Oak.app"
    )
    if (TARGET oakvulkan)
        add_custom_command(TARGET olive-editor POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:oakvulkan> $<TARGET_BUNDLE_DIR:olive-editor>/Contents/MacOS/
        )
    endif ()
elseif (UNIX)
    # Set Linux-specific properties for application
    install(TARGETS olive-editor RUNTIME DESTINATION bin)
endif ()

if (WIN32)
    # Windows has no RPATH: shared libraries must sit next to the executable
    add_custom_command(TARGET olive-editor POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:ffmpeg_bridge> $<TARGET_FILE_DIR:olive-editor>
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:olivecore> $<TARGET_FILE_DIR:olive-editor>
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:oakengine> $<TARGET_FILE_DIR:olive-editor>
    )
endif ()

# Set link libraries
target_link_libraries(olive-editor PRIVATE ${OLIVE_LIBRARIES})
target_link_libraries(libolive-editor PRIVATE ${OLIVE_LIBRARIES})
# The app is an internal consumer that still uses engine C++ classes directly.
# Link the object library to bypass the version-script restrictions on
# liboakengine.so (which only exposes the C ABI for external consumers).
target_link_libraries(olive-editor PRIVATE oakengine-obj)
target_link_libraries(libolive-editor PRIVATE oakengine-obj)

# The ffmpeg_bridge shared library ships next to the binaries: inside the
# macOS app bundle (Contents/MacOS, resolved via @loader_path), and in the
# standard lib directory on other platforms.
if (APPLE)
    # macOS bundles are distributed via POST_BUILD copies (not install()), so
    # @loader_path must already be in the build-tree binaries' RPATH.
    set(OLIVE_FB_RPATH "@loader_path")
    set_target_properties(olive-editor PROPERTIES
            BUILD_RPATH "@loader_path")
elseif (UNIX)
    set(OLIVE_FB_RPATH "$ORIGIN/../lib")
endif ()
if (OLIVE_FB_RPATH)
    set_target_properties(olive-editor PROPERTIES INSTALL_RPATH "${OLIVE_FB_RPATH}")
    if (TARGET oakgl)
        set_target_properties(oakgl PROPERTIES INSTALL_RPATH "${OLIVE_FB_RPATH}")
    endif ()
    if (TARGET oakvulkan)
        set_target_properties(oakvulkan PROPERTIES INSTALL_RPATH "${OLIVE_FB_RPATH}")
    endif ()
endif ()

# Set compile options
target_compile_options(olive-editor PRIVATE ${OLIVE_COMPILE_OPTIONS})
target_compile_options(libolive-editor PRIVATE ${OLIVE_COMPILE_OPTIONS})

# Set global definitions
target_compile_definitions(olive-editor PRIVATE ${OLIVE_DEFINITIONS})
target_compile_definitions(libolive-editor PRIVATE ${OLIVE_DEFINITIONS})

# Set include dirs
target_include_directories(olive-editor PRIVATE ${OLIVE_INCLUDE_DIRS})
target_include_directories(libolive-editor PRIVATE ${OLIVE_INCLUDE_DIRS})

# Add crash handler
if (GoogleCrashpad_FOUND AND Qt${QT_VERSION_MAJOR}Network_FOUND)
    add_subdirectory(crashhandler)
endif ()
