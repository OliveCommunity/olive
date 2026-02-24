#	This file is part of Oak Video Editor - A fork of original project Olive 
#
# Olive - Non-Linear Video Editor
# Copyright (C) 2022 Olive Team
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
)

#set(OLIVE_RESOURCES)

# Add subdirectories, which will populate the above variables
add_subdirectory(audio)
add_subdirectory(cli)
add_subdirectory(codec)
add_subdirectory(config)
add_subdirectory(dialog)
add_subdirectory(node)
add_subdirectory(packaging)
add_subdirectory(panel)
add_subdirectory(shaders)
add_subdirectory(task)
add_subdirectory(timeline)
add_subdirectory(ts)
add_subdirectory(tool)
add_subdirectory(ui)
add_subdirectory(undo)
add_subdirectory(widget)
add_subdirectory(window)

# Add translations
qt_add_translation(OLIVE_QM_FILES ${OLIVE_TS_FILES})

set(QRC_BODY "")
foreach(QM_FILE ${OLIVE_QM_FILES})
  get_filename_component(QM_FILENAME_COMPONENT ${QM_FILE} NAME_WE)
  string(APPEND QRC_BODY "<file alias=\"${QM_FILENAME_COMPONENT}\">${QM_FILE}</file>\n")
endforeach()
configure_file(ts/translations.qrc.in ts/translations.qrc @ONLY)

set(OLIVE_RESOURCES
  ${OLIVE_RESOURCES}
  ${CMAKE_CURRENT_BINARY_DIR}/ts/translations.qrc
        render/job/pluginjob.cpp
        render/job/pluginjob.h
        widget/nodeparamview/nodeparambutton.cpp
        widget/nodeparamview/nodeparambutton.h
)

# Add version object
add_library(olive-version-obj
  OBJECT
  version.cpp
  version.h
)
target_link_libraries(olive-version-obj PRIVATE Qt${QT_VERSION_MAJOR}::Core)
target_compile_options(olive-version-obj PRIVATE -DAPPVERSION="${PROJECT_VERSION}" -DAPPVERSIONLONG="${PROJECT_LONG_VERSION}" )

# Add main library
add_library(libolive-editor
  OBJECT
  ${OLIVE_SOURCES}
  ${OLIVE_RESOURCES}
)
add_subdirectory(common)
add_subdirectory(render)
add_subdirectory(pluginSupport)

# Add resources that were populated by subdirectories (e.g., ocioconf.qrc)
target_sources(libolive-editor PRIVATE ${OLIVE_RESOURCES})

target_compile_features(libolive-editor PUBLIC cxx_std_23)
include_directories(../third_party/openfx/include
        ../third_party/openfx/HostSupport/include)
# Remove prefix - prevents CMake calling it "liblibolive-editor"
set_target_properties(libolive-editor PROPERTIES PREFIX "")

# Add application
add_executable(olive-editor
  main.cpp
  $<TARGET_OBJECTS:libolive-editor>
  $<TARGET_OBJECTS:olive-version-obj>
)
target_include_directories(olive-editor PUBLIC pluginSupport)
target_link_libraries(olive-editor PUBLIC OfxHost)
# Create docs if doxygen was found
if(DOXYGEN_FOUND)
  set(DOXYGEN_PROJECT_NAME "Oak Video Editor")
  set(DOXYGEN_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/docs")
  set(DOXYGEN_EXTRACT_ALL "YES")
  set(DOXYGEN_EXTRACT_PRIVATE "YES")
  doxygen_add_docs(docs ALL ${OLIVE_SOURCES})
endif()

# Platform-specific deployment preferences
if (WIN32)
  # Set Windows application icon
  target_sources(olive-editor PRIVATE packaging/windows/resources.rc)

  # Preserve folder structure in visual studio
  source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${OLIVE_SOURCES})

elseif(APPLE)
  # Set Mac application icon
  set(OLIVE_ICON packaging/macos/olive.icns)
  target_sources(olive-editor PRIVATE ${OLIVE_ICON})

  # Set Mac bundle properties
  set_target_properties(olive-editor PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/MacOSXBundleInfo.plist.in
    MACOSX_BUNDLE_GUI_IDENTIFIER org.oakvideoeditor.Oak
    MACOSX_BUNDLE_ICON_FILE olive.icns
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_BUNDLE_NAME "Oak Video Editor"
    MACOSX_BUNDLE_INFO_STRING "Oak Video Editor ${PROJECT_LONG_VERSION}"
    MACOSX_BUNDLE_COPYRIGHT "©2018-2021 Olive Studios LLC and others. Fork maintained by Oak Video Editor Team."
    RESOURCE "${OLIVE_ICON}"
    OUTPUT_NAME "Olive"
  )
elseif(UNIX)
  # Set Linux-specific properties for application
  install(TARGETS olive-editor RUNTIME DESTINATION bin)
endif()

# Set link libraries
target_link_libraries(olive-editor PRIVATE ${OLIVE_LIBRARIES})
target_link_libraries(libolive-editor PRIVATE ${OLIVE_LIBRARIES})

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
endif()

# Windows: Auto-copy DLLs to executable directory (MSYS2)
if (WIN32)
  # ── Use windeployqt for Qt dependencies ─────────────────────────────────────
  find_program(WINDEPLOYQT_EXECUTABLE windeployqt
    PATHS
      /ucrt64/bin
      /mingw64/bin
      /mingw32/bin
      "C:/msys64/ucrt64/bin"
      "C:/msys64/mingw64/bin"
      "C:/msys64/mingw32/bin"
      "${Qt${QT_VERSION_MAJOR}Core_DIR}/../../../bin"
  )

  if (WINDEPLOYQT_EXECUTABLE)
    add_custom_command(TARGET olive-editor POST_BUILD
      COMMAND "${WINDEPLOYQT_EXECUTABLE}"
        --verbose 0
        --no-compiler-runtime
        --no-translations
        --no-system-d3d-compiler
        \"$<TARGET_FILE:olive-editor>\"
      COMMENT "Deploying Qt dependencies with windeployqt..."
    )
  else ()
    message(WARNING "windeployqt not found, Qt DLLs may not be copied correctly")
  endif ()

  # ── Copy MSYS2 runtime DLLs ─────────────────────────────────────────────────
  # Search paths: Unix style first (MSYS2 terminal), then Windows style
  set(_msys2_search_paths
    # Unix style paths (MSYS2 terminal) - try ucrt64 first, then mingw64
    /ucrt64/bin
    /mingw64/bin
    /mingw32/bin
    # Windows style paths (native Windows)
    "C:/msys64/ucrt64/bin"
    "C:/msys64/mingw64/bin"
    "C:/msys64/mingw32/bin"
  )

  # DLLs to copy from MSYS2 (common dependencies)
  set(_msys2_dlls
    # FFmpeg
    avcodec-61.dll avformat-61.dll avutil-59.dll avfilter-10.dll
    swscale-8.dll swresample-5.dll
    # OpenImageIO
    OpenImageIO.dll OpenImageIO_Util.dll
    # OpenColorIO
    OpenColorIO.dll
    # OpenEXR
    OpenEXR-3-3.dll Imath-3-1.dll
    # PortAudio
    portaudio.dll
    # Expat
    libexpat-1.dll
    # Other common dependencies
    libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll
    zlib1.dll libpng16-16.dll libjpeg-8.dll libtiff-6.dll
    libfreetype-6.dll libharfbuzz-0.dll libglib-2.0-0.dll
    libintl-8.dll libiconv-2.dll libbz2-1.dll libbrotlidec.dll
    libbrotlienc.dll libbrotlicommon.dll
  )

  foreach (_dll IN LISTS _msys2_dlls)
    unset(_dll_path)
    foreach (_search_path IN LISTS _msys2_search_paths)
      if (EXISTS "${_search_path}/${_dll}")
        set(_dll_path "${_search_path}/${_dll}")
        break()
      endif ()
    endforeach ()

    if (_dll_path)
      add_custom_command(TARGET olive-editor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${_dll_path}"
          $<TARGET_FILE_DIR:olive-editor>
        COMMENT "Copying MSYS2 DLL: ${_dll}"
      )
    endif ()
  endforeach ()

  # Note: We intentionally do NOT copy all DLLs from MSYS2 bin directories.
  # The windeployqt tool handles Qt dependencies, and the _msys2_dlls list
  # above contains only the specific runtime dependencies we need.
  # Copying all DLLs would include unnecessary libraries (Python, OpenCV, etc.)
  # that bloat the distribution and may cause conflicts.
endif ()
