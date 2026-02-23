# This file is part of Oak Video Editor - A fork of original project Olive 
#
# Olive - Non-Linear Video Editor
# Copyright (C) 2023 Olive Studios LLC
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

cmake_minimum_required(VERSION 3.13 FATAL_ERROR)

project(olive-editor VERSION 0.2.0 LANGUAGES CXX)
option(BUILD_QT6 "Build with Qt 6 over 5 (experimental)" ON)
option(BUILD_DOXYGEN "Build Doxygen documentation" OFF)
option(BUILD_TESTS "Build unit tests" OFF)
option(USE_WERROR "Error on compile warning" OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Generates a compile_commands.json in the build dir, link that to the repo
# root to enrich your IDE with clangd language server protocol functionalities
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Sanitizers
add_library(olive-sanitizers INTERFACE)
include(cmake/Sanitizers.cmake)
enable_sanitizers(olive-sanitizers)
list(APPEND OLIVE_LIBRARIES olive-sanitizers)

# Set compiler options
if(MSVC)
  set(OLIVE_COMPILE_OPTIONS
    /wd4267
    /wd4244
    /experimental:external
    /external:anglebrackets
    /external:W0
    "$<$<CONFIG:RELEASE>:/O2>"
    "$<$<COMPILE_LANGUAGE:CXX>:/MP>"
    /DOFX_SUPPORTS_OPENGLRENDER
  )
  if (USE_WERROR)
    list(APPEND OLIVE_COMPILE_OPTIONS "/WX")
  endif()
else()
  set(OLIVE_COMPILE_OPTIONS
    "$<$<CONFIG:RELEASE>:-O2>"
    -Wuninitialized
    -pedantic-errors
    -Wall
    -Wextra
    -Wno-unused-parameter
    -Wshadow
    -DOFX_SUPPORTS_OPENGLRENDER

  )
  if (USE_WERROR)
    list(APPEND OLIVE_COMPILE_OPTIONS "-Werror")
  endif()
endif()

set(OLIVE_DEFINITIONS -DQT_DEPRECATED_WARNINGS)
if (WIN32)
  list(APPEND OLIVE_DEFINITIONS -DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# Improve package discovery in manifest/local vcpkg layouts on Windows.
if (WIN32)
  set(_VCPKG_PREFIX_CANDIDATES "")
  set(_VCPKG_ROOT_HINTS
    "${CMAKE_SOURCE_DIR}/vcpkg_installed"
    "${CMAKE_BINARY_DIR}/vcpkg_installed"
    "$ENV{VCPKG_ROOT}/installed"
  )
  foreach(_vcpkg_root IN LISTS _VCPKG_ROOT_HINTS)
    if (EXISTS "${_vcpkg_root}")
      file(GLOB _vcpkg_triplets LIST_DIRECTORIES TRUE "${_vcpkg_root}/*")
      foreach(_triplet_dir IN LISTS _vcpkg_triplets)
        if (EXISTS "${_triplet_dir}/share")
          list(APPEND _VCPKG_PREFIX_CANDIDATES "${_triplet_dir}")
        endif()
      endforeach()
    endif()
  endforeach()
  if (_VCPKG_PREFIX_CANDIDATES)
    list(PREPEND CMAKE_PREFIX_PATH ${_VCPKG_PREFIX_CANDIDATES})
  endif()
endif()

# OFX HostSupport requires expat::expat.
# 1) Try CMake package discovery first.
# 2) On Windows, fallback to vcpkg's generated expat.cmake import file.
# 3) On Linux, fallback to pkg-config module "expat".
find_package(EXPAT QUIET)

if (NOT EXPAT_FOUND AND WIN32)
  set(_EXPAT_VCPKG_ROOT_HINTS
    "${CMAKE_BINARY_DIR}/vcpkg_installed"
    "${CMAKE_SOURCE_DIR}/vcpkg_installed"
    "$ENV{VCPKG_ROOT}/installed"
  )
  set(_EXPAT_VCPKG_CMAKE_FILES "")
  foreach(_vcpkg_root IN LISTS _EXPAT_VCPKG_ROOT_HINTS)
    if (EXISTS "${_vcpkg_root}")
      file(GLOB _expat_matches "${_vcpkg_root}/*/share/expat/expat.cmake")
      list(APPEND _EXPAT_VCPKG_CMAKE_FILES ${_expat_matches})
    endif()
  endforeach()
  list(LENGTH _EXPAT_VCPKG_CMAKE_FILES _EXPAT_VCPKG_CMAKE_COUNT)
  if (_EXPAT_VCPKG_CMAKE_COUNT GREATER 0)
    list(GET _EXPAT_VCPKG_CMAKE_FILES 0 EXPAT_VCPKG_CMAKE)
    include("${EXPAT_VCPKG_CMAKE}")
    if (TARGET expat::expat)
      set(EXPAT_FOUND TRUE)
      message(STATUS "Using EXPAT from vcpkg import file: ${EXPAT_VCPKG_CMAKE}")
    endif()
  endif()
endif()

if (NOT EXPAT_FOUND AND (UNIX AND NOT APPLE))
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(EXPAT REQUIRED IMPORTED_TARGET expat)
  if (NOT TARGET expat::expat)
    add_library(expat::expat INTERFACE IMPORTED)
    set_property(TARGET expat::expat PROPERTY
      INTERFACE_LINK_LIBRARIES PkgConfig::EXPAT)
  endif()
  if (NOT TARGET EXPAT::EXPAT)
    add_library(EXPAT::EXPAT INTERFACE IMPORTED)
    set_property(TARGET EXPAT::EXPAT PROPERTY
      INTERFACE_LINK_LIBRARIES expat::expat)
  endif()
  set(EXPAT_FOUND TRUE)
endif()

if (NOT EXPAT_FOUND)
  message(FATAL_ERROR "EXPAT was not found.")
endif()

# Normalize target names so both conventions are always available.
if (TARGET expat::expat AND NOT TARGET EXPAT::EXPAT)
  add_library(EXPAT::EXPAT ALIAS expat::expat)
endif()
if (TARGET EXPAT::EXPAT AND NOT TARGET expat::expat)
  add_library(expat::expat ALIAS EXPAT::EXPAT)
endif()

# Link OpenGL
if(UNIX AND NOT APPLE AND NOT DEFINED OpenGL_GL_PREFERENCE)
  set(OpenGL_GL_PREFERENCE LEGACY)
endif()
find_package(OpenGL REQUIRED)
list(APPEND OLIVE_LIBRARIES OpenGL::GL)

# Link GLEW (for cross-platform OpenGL function loading)
find_package(GLEW QUIET)
if (GLEW_FOUND)
  list(APPEND OLIVE_LIBRARIES GLEW::GLEW)
  message(STATUS "GLEW found: ${GLEW_VERSION}")
else()
  # Fallback: try to find GLEW manually for MSYS2/MinGW
  find_library(GLEW_LIBRARY NAMES glew32 glew PATHS /ucrt64/lib /mingw64/lib)
  find_path(GLEW_INCLUDE_DIR GL/glew.h PATHS /ucrt64/include /mingw64/include)
  if (GLEW_LIBRARY AND GLEW_INCLUDE_DIR)
    list(APPEND OLIVE_LIBRARIES ${GLEW_LIBRARY})
    list(APPEND OLIVE_INCLUDE_DIRS ${GLEW_INCLUDE_DIR})
    message(STATUS "GLEW found manually: ${GLEW_LIBRARY}")
  else()
    message(FATAL_ERROR "GLEW not found. Please install GLEW.")
  endif()
endif()

# Force include GLEW before any other headers to avoid conflicts
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-include GL/glew.h)
elseif (MSVC)
  add_compile_options(/FI"GL/glew.h")
endif()

# Link OpenColorIO
find_package(OpenColorIO 2.1.1 CONFIG QUIET)
if (TARGET OpenColorIO::OpenColorIO)
  list(APPEND OLIVE_LIBRARIES OpenColorIO::OpenColorIO)
  get_target_property(_OCIO_INCLUDES OpenColorIO::OpenColorIO INTERFACE_INCLUDE_DIRECTORIES)
  if (_OCIO_INCLUDES)
    list(APPEND OLIVE_INCLUDE_DIRS ${_OCIO_INCLUDES})
  endif()
else()
  find_package(OpenColorIO 2.1.1 REQUIRED)
  list(APPEND OLIVE_LIBRARIES ${OCIO_LIBRARIES})
  list(APPEND OLIVE_INCLUDE_DIRS ${OCIO_INCLUDE_DIRS})
endif()

# Link OpenImageIO
find_package(OpenImageIO 2.1.12 CONFIG QUIET)
if (TARGET OpenImageIO::OpenImageIO)
  list(APPEND OLIVE_LIBRARIES OpenImageIO::OpenImageIO)
  if (TARGET OpenImageIO::OpenImageIO_Util)
    list(APPEND OLIVE_LIBRARIES OpenImageIO::OpenImageIO_Util)
  endif()
  get_target_property(_OIIO_INCLUDES OpenImageIO::OpenImageIO INTERFACE_INCLUDE_DIRECTORIES)
  if (_OIIO_INCLUDES)
    list(APPEND OLIVE_INCLUDE_DIRS ${_OIIO_INCLUDES})
  endif()
else()
  find_package(OpenImageIO 2.1.12 REQUIRED)
  list(APPEND OLIVE_LIBRARIES ${OIIO_LIBRARIES})
  list(APPEND OLIVE_INCLUDE_DIRS ${OIIO_INCLUDE_DIRS})
endif()

# Link OpenEXR
find_package(OpenEXR REQUIRED)
list(APPEND OLIVE_LIBRARIES ${OPENEXR_LIBRARIES})
list(APPEND OLIVE_INCLUDE_DIRS ${OPENEXR_INCLUDES})

# Link Olive
list(APPEND OLIVE_LIBRARIES olivecore)
list(APPEND OLIVE_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/ext/core/include)


# Link Qt
set(QT_LIBRARIES
  Core
  Gui
  Widgets
  OpenGL
  LinguistTools
  Concurrent
)
if (UNIX AND NOT APPLE)
  list(APPEND QT_LIBRARIES DBus)
endif()
if (BUILD_QT6)
  set(QT_NAME Qt6)
else()
  set(QT_NAME Qt5)
endif()
find_package(QT
  NAMES
    ${QT_NAME}
  REQUIRED
  COMPONENTS
    ${QT_LIBRARIES}
  OPTIONAL_COMPONENTS
    Network
)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED
  COMPONENTS
    ${QT_LIBRARIES}
  OPTIONAL_COMPONENTS
    Network
)
if (NOT Qt${QT_VERSION_MAJOR}Network_FOUND)
  message("   Qt${QT_VERSION_MAJOR}::Network module not found, crash reporting will be disabled.")
endif()
list(APPEND OLIVE_LIBRARIES
  Qt${QT_VERSION_MAJOR}::Core
  Qt${QT_VERSION_MAJOR}::Gui
  Qt${QT_VERSION_MAJOR}::Widgets
  Qt${QT_VERSION_MAJOR}::OpenGL
  Qt${QT_VERSION_MAJOR}::Concurrent
)

if (${QT_VERSION_MAJOR} EQUAL "6")
  find_package(Qt${QT_VERSION_MAJOR}
    REQUIRED
      OpenGLWidgets
  )

  list(APPEND OLIVE_LIBRARIES
    Qt${QT_VERSION_MAJOR}::OpenGLWidgets
  )

  # Link KDDockWidgets
  #find_package(KDDockWidgets-qt6 CONFIG REQUIRED)
else()
  # Link KDDockWidgets
  #find_package(KDDockWidgets CONFIG REQUIRED)
endif()

list(APPEND OLIVE_LIBRARIES
  KDAB::kddockwidgets
)

# Link OFX HostSupport wherever libolive-editor objects are used.
list(APPEND OLIVE_LIBRARIES OfxHost)

# Link FFmpeg
find_package(FFMPEG REQUIRED
  COMPONENTS
  avutil
  avcodec
  avformat
  avfilter
  swscale
  swresample
)
list(APPEND OLIVE_INCLUDE_DIRS ${FFMPEG_INCLUDE_DIRS})
list(APPEND OLIVE_LIBRARIES
  FFMPEG::avutil
  FFMPEG::avcodec
  FFMPEG::avformat
  FFMPEG::avfilter
  FFMPEG::swscale
  FFMPEG::swresample
)

# Link PortAudio
find_package(portaudio CONFIG QUIET)
if (TARGET portaudio)
  set(PORTAUDIO_LIBRARIES portaudio)
  get_target_property(_PORTAUDIO_INCLUDES portaudio INTERFACE_INCLUDE_DIRECTORIES)
  if (_PORTAUDIO_INCLUDES)
    set(PORTAUDIO_INCLUDE_DIRS ${_PORTAUDIO_INCLUDES})
  endif()
else()
  find_package(PortAudio REQUIRED)
endif()

set(CMAKE_REQUIRED_INCLUDES ${PORTAUDIO_INCLUDE_DIRS})
include(CheckIncludeFileCXX)
check_include_file_cxx("pa_jack.h" PA_HAS_JACK)
if (PA_HAS_JACK)
  list(APPEND OLIVE_DEFINITIONS PA_HAS_JACK)
endif()

list(APPEND OLIVE_INCLUDE_DIRS ${PORTAUDIO_INCLUDE_DIRS})
list(APPEND OLIVE_LIBRARIES ${PORTAUDIO_LIBRARIES})

# Optional: Link OpenTimelineIO
find_package(OpenTimelineIO)
if (OpenTimelineIO_FOUND)
  list(APPEND OLIVE_DEFINITIONS USE_OTIO)
  list(APPEND OLIVE_INCLUDE_DIRS ${OTIO_INCLUDE_DIRS})
  list(APPEND OLIVE_LIBRARIES ${OTIO_LIBRARIES})
else()
  message("   OpenTimelineIO interchange will be disabled.")
endif()

# Optional: Link Google Crashpad
find_package(GoogleCrashpad)
if (GoogleCrashpad_FOUND)
  list(APPEND OLIVE_DEFINITIONS USE_CRASHPAD)
  list(APPEND OLIVE_INCLUDE_DIRS ${CRASHPAD_INCLUDE_DIRS})
  list(APPEND OLIVE_LIBRARIES ${CRASHPAD_LIBRARIES})
else()
  message("   Automatic crash reporting will be disabled.")

  if (APPLE)
    # Enables use of special functions for slider dragging, only linked if Crashpad isn't found
    # because Crashpad links it itself and will cause duplicate references if we also link it
    list(APPEND OLIVE_LIBRARIES "-framework ApplicationServices")
  endif()
endif()

if (WIN32)
  # UNICODE 已经在上面定义过了，这里不需要重复定义
elseif (APPLE)
  list(APPEND OLIVE_LIBRARIES "-framework IOKit")
elseif(UNIX)
  list(APPEND OLIVE_LIBRARIES Qt${QT_VERSION_MAJOR}::DBus)
endif()

# Generate Git hash
set(PROJECT_LONG_VERSION ${PROJECT_VERSION})
if(EXISTS "${CMAKE_SOURCE_DIR}/.git")
  find_package(Git)
  if(GIT_FOUND)
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short=8 HEAD
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      OUTPUT_VARIABLE GIT_HASH
      OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    set(PROJECT_LONG_VERSION ${PROJECT_VERSION}-${GIT_HASH})
  endif()
endif()

# Optional: Find Doxygen if requested
if(BUILD_DOXYGEN)
  find_package(Doxygen)
endif()

set(CMAKE_INCLUDE_CURRENT_DIR ON)
list(APPEND OLIVE_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/ext)

add_subdirectory(ext)
add_subdirectory(third_party/openfx/HostSupport)
add_subdirectory(app)

if (BUILD_TESTS)
  add_compile_options(
          $<$<CXX_COMPILER_ID:Clang>:-Wno-character-conversion>
          $<$<CXX_COMPILER_ID:AppleClang>:-Wno-character-conversion>
  )
  enable_testing()
  add_subdirectory(tests)
endif()
