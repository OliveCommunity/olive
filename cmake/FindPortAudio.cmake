# - Find PortAudio includes and libraries
#
#   PORTAUDIO_FOUND        - True if PORTAUDIO_INCLUDE_DIR & PORTAUDIO_LIBRARY
#                            are found
#   PORTAUDIO_LIBRARIES    - Set when PORTAUDIO_LIBRARY is found
#   PORTAUDIO_INCLUDE_DIRS - Set when PORTAUDIO_INCLUDE_DIR is found
#
#   PORTAUDIO_INCLUDE_DIR - where to find portaudio.h, etc.
#   PORTAUDIO_LIBRARY     - the portaudio library
#

find_package(portaudio CONFIG QUIET)
if (TARGET portaudio)
    set(PORTAUDIO_LIBRARY portaudio)
    get_target_property(PORTAUDIO_INCLUDE_DIR portaudio INTERFACE_INCLUDE_DIRECTORIES)
endif()

if (NOT PORTAUDIO_INCLUDE_DIR)
    find_path(PORTAUDIO_INCLUDE_DIR
              NAMES portaudio.h
              PATHS
                /mingw64/include
                /ucrt64/include
                "C:/msys64/mingw64/include"
                "C:/msys64/ucrt64/include"
              DOC "The PortAudio include directory"
    )
endif()

if (NOT PORTAUDIO_LIBRARY)
    find_library(PORTAUDIO_LIBRARY
                 NAMES portaudio
                       libportaudio.dll.a
                       libportaudio.a
                 PATHS
                   /mingw64/lib
                   /ucrt64/lib
                   "C:/msys64/mingw64/lib"
                   "C:/msys64/ucrt64/lib"
                 DOC "The PortAudio library"
    )
endif()

# 使用 pkg-config 查找 PortAudio（支持 Linux 和 MSYS2）
if (NOT PORTAUDIO_LIBRARY OR NOT PORTAUDIO_INCLUDE_DIR)
    find_package(PkgConfig QUIET)
    if (PKG_CONFIG_FOUND)
        pkg_check_modules(PC_PORTAUDIO QUIET IMPORTED_TARGET portaudio-2.0)
        if (PC_PORTAUDIO_FOUND)
            set(PORTAUDIO_LIBRARY PkgConfig::PC_PORTAUDIO)
            set(PORTAUDIO_INCLUDE_DIR ${PC_PORTAUDIO_INCLUDE_DIRS})
        endif()
    endif()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(PortAudio
    REQUIRED_VARS PORTAUDIO_LIBRARY PORTAUDIO_INCLUDE_DIR
)

if(PORTAUDIO_FOUND)
    set(PORTAUDIO_LIBRARIES ${PORTAUDIO_LIBRARY})
    set(PORTAUDIO_INCLUDE_DIRS ${PORTAUDIO_INCLUDE_DIR})
endif()

mark_as_advanced(PORTAUDIO_INCLUDE_DIR PORTAUDIO_LIBRARY)
