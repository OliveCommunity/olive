# libolivecore
# Copyright (C) 2023 Olive Studios LLC
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

cmake_minimum_required(VERSION 3.13 FATAL_ERROR)

project(libolivecore VERSION 1.0.0 LANGUAGES CXX)

option(OLIVECORE_BUILD_TESTS "Build liboakcore tests" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Link Imath
find_package(Imath REQUIRED CONFIG)

# Link OpenGL
if(UNIX AND NOT APPLE AND NOT DEFINED OpenGL_GL_PREFERENCE)
  set(OpenGL_GL_PREFERENCE LEGACY)
endif()
find_package(OpenGL REQUIRED)

# liboakcore is a shared library. It exports a pure C ABI (see
# include/olive/core/oakcore/): every public function is declared with
# OAKCORE_API and all C++ symbols are hidden, so no C++ ABI crosses the
# library boundary. Consumers use the wrapper headers in include/olive/core.
add_library(olivecore SHARED
  src/capi/audioparams.cpp
  src/capi/bezier.cpp
  src/capi/color.cpp
  src/capi/fractionutils.cpp
  src/capi/rational.cpp
  src/capi/samplebuffer.cpp
  src/capi/stringutils.cpp
  src/capi/timecodefunctions.cpp
  src/capi/timerange.cpp
  src/render/audioparams.cpp
  src/render/samplebuffer.cpp
  src/util/bezier.cpp
  src/util/color.cpp
  src/util/fractionutils.cpp
  src/util/rational.cpp
  src/util/stringutils.cpp
  src/util/tests.cpp
  src/util/timecodefunctions.cpp
  src/util/timerange.cpp
  src/util/value.cpp
)

set_target_properties(olivecore PROPERTIES
  OUTPUT_NAME oakcore
  POSITION_INDEPENDENT_CODE ON
  C_VISIBILITY_PRESET hidden
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON
)

# OAKCORE_BUILD marks the library side of the export macros (dllexport)
target_compile_definitions(olivecore PRIVATE OAKCORE_BUILD)

# Export only the C ABI (oakcore_*), nothing else — not even symbols from
# statically linked runtime libraries
if (UNIX AND NOT APPLE)
  target_link_options(olivecore PRIVATE
    "LINKER:--version-script,${CMAKE_CURRENT_SOURCE_DIR}/oakcore.map")
endif ()

# The library builds against its internal implementation headers
# (src/oliveimpl); consumers only ever see the public C API and wrapper
# headers (include/olive/core). oliveimpl must come first so that internal
# sources never pick up a wrapper header by accident.
target_include_directories(olivecore PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src/oliveimpl"
  "${CMAKE_CURRENT_SOURCE_DIR}/include/olive/core"
  "${CMAKE_SOURCE_DIR}/third_party/openfx/include/"
)

target_link_libraries(olivecore PRIVATE
  OpenGL::GL
  Imath::Imath
)

# Link OpenTimelineIO (optional)
find_package(OpenTimelineIO)
if (OpenTimelineIO_FOUND)
  target_compile_definitions(olivecore PRIVATE USE_OTIO)
  target_include_directories(olivecore PRIVATE ${OTIO_INCLUDE_DIRS})
  target_link_libraries(olivecore PRIVATE ${OTIO_LIBRARIES})
else()
  message("   OpenTimelineIO interchange will be disabled.")
endif()

# liboakcore installs into the platform's standard library directory
# (/usr/lib, /usr/lib64 or the Debian multiarch path), not a custom one.
# Windows DLLs go next to the executables in bin.
include(GNUInstallDirs)
if (WIN32)
  install(TARGETS olivecore
          RUNTIME DESTINATION bin
          ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
else ()
  install(TARGETS olivecore
          LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
          ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif ()

if (OLIVECORE_BUILD_TESTS)
  enable_testing()

  function(make_test name)
    add_executable(${name}
      tests/${name}.cpp
    )
    target_link_libraries(${name} PRIVATE olivecore)
    target_include_directories(${name} PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}/include"
      "${CMAKE_CURRENT_SOURCE_DIR}/include/olive/core"
    )
    add_test(${name} ${name})
  endfunction()

  # Pure C ABI tests for the liboakcore public interface
  make_test(oakcore_audioparams_test)
  make_test(oakcore_bezier_test)
  make_test(oakcore_color_test)
  make_test(oakcore_fractionutils_test)
  make_test(oakcore_rational_test)
  make_test(oakcore_samplebuffer_test)
  make_test(oakcore_stringutils_test)
  make_test(oakcore_timecodefunctions_test)
  make_test(oakcore_timerange_test)
endif()
