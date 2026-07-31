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

if(UNIX)
    find_path(OTIO_BASE_DIR
            include/opentimelineio/timeline.h
        HINTS
            "${OTIO_LOCATION}"
            "$ENV{OTIO_LOCATION}"
            "/opt/otio"
    )
    find_path(OTIO_LIBRARY_DIR
            libopentimelineio.so
        HINTS
            "${OTIO_LOCATION}"
            "$ENV{OTIO_LOCATION}"
            "${OTIO_BASE_DIR}"
        PATH_SUFFIXES
            lib/
        DOC
            "OpenTimelineIO library path"
    )
elseif(WIN32)
    find_path(OTIO_BASE_DIR
            include/opentimelineio/timeline.h
        HINTS
            "${OTIO_LOCATION}"
            "$ENV{OTIO_LOCATION}"
    )
    find_path(OTIO_LIBRARY_DIR
            opentimelineio.lib
        HINTS
            "${OTIO_LOCATION}"
            "$ENV{OTIO_LOCATION}"
            "${OTIO_BASE_DIR}"
        PATH_SUFFIXES
            lib/
        DOC
            "OpenTimelineIO library path"
    )
endif()

find_path(OTIO_INCLUDE_DIR
        opentimelineio/timeline.h
    HINTS
        "${OTIO_LOCATION}"
        "$ENV{OTIO_LOCATION}"
        "${OTIO_BASE_DIR}"
    PATH_SUFFIXES
        include/
    DOC
        "OpenTimelineIO headers path"
)

list(APPEND OTIO_INCLUDE_DIRS ${OTIO_INCLUDE_DIR})

find_path(OTIO_DEPS_INCLUDE_DIR
        any/any.hpp
    HINTS
        "${OTIO_LOCATION}"
        "$ENV{OTIO_LOCATION}"
        "${OTIO_BASE_DIR}"
    PATH_SUFFIXES
        include/opentimelineio/deps/
    DOC
        "OpenTimelineIO headers path"
)

if(OTIO_DEPS_INCLUDE_DIR)
    list(APPEND OTIO_INCLUDE_DIRS ${OTIO_DEPS_INCLUDE_DIR})
endif()

find_path(OT_INCLUDE_DIR
        opentime/rationalTime.h
    HINTS
        "${OTIO_LOCATION}"
        "$ENV{OTIO_LOCATION}"
        "${OTIO_BASE_DIR}"
    PATH_SUFFIXES
        include/
    DOC
        "OpenTime headers path"
)

list(APPEND OTIO_INCLUDE_DIRS ${OT_INCLUDE_DIR})

find_library(OTIO_LIBRARY
        opentimelineio
    HINTS
        "${OTIO_LOCATION}"
        "$ENV{OTIO_LOCATION}"
        "${OTIO_BASE_DIR}"
    PATH_SUFFIXES
        lib/
    DOC
        "OTIO's ${OTIO_LIB} library path"
)

list(APPEND OTIO_LIBRARIES ${OTIO_LIBRARY})

find_library(OT_LIBRARY
        opentime
    HINTS
        "${OTIO_LOCATION}"
        "$ENV{OTIO_LOCATION}"
        "${OTIO_BASE_DIR}"
    PATH_SUFFIXES
        lib/
    DOC
        "OpenTime's ${OTIO_LIB} library path"
)

list(APPEND OTIO_LIBRARIES ${OT_LIBRARY})

# OTIO_LIBRARY_DIR is only probed for .so above (never matches on macOS);
# derive it from the located library as a fallback.
if(NOT OTIO_LIBRARY_DIR AND OTIO_LIBRARY)
    get_filename_component(OTIO_LIBRARY_DIR "${OTIO_LIBRARY}" DIRECTORY)
endif()

include(FindPackageHandleStandardArgs)

# OTIO_DEPS_INCLUDE_DIR (bundled any/optional headers) only exists in OTIO
# <= 0.15; v0.16 uses C++17 std::any and ships no deps headers, so it is
# intentionally NOT part of REQUIRED_VARS.
find_package_handle_standard_args(OpenTimelineIO
    REQUIRED_VARS
        OTIO_LIBRARIES
        OTIO_INCLUDE_DIRS
)
