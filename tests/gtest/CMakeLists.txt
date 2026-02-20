# This file is part of Oak Video Editor - A fork of original project Olive 
#
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2025 mikesolar

add_executable(olive-gtest
  main.cpp
  common_current_test.cpp
  common_xmlutils_test.cpp
  config_test.cpp
  node_value_test.cpp
  node_keyframe_test.cpp
  node_serialization_test.cpp
  render_videoparams_test.cpp
  render_videoparams_branch_test.cpp
  render_audioparams_test.cpp
  render_audioparams_branch_test.cpp
  render_sampleformat_test.cpp
  render_pixelformat_test.cpp
        render_thread_pool_test.cpp
  project_serializer_test.cpp
  timeline_marker_test.cpp
  undo_stack_test.cpp
  plugin_support_test.cpp
  plugin_support_image_test.cpp
  plugin_support_clip_test.cpp
  plugin_support_param_test.cpp
  opengl_readback_guard_test.cpp
  plugin_render_pipeline_test.cpp
  plugin_renderer_readback_test.cpp
  plugin_ofx_integration_test.cpp
  codec_frame_test.cpp
  codec_exportcodec_test.cpp
  codec_exportformat_test.cpp
  codec_encoder_test.cpp
  task_taskmanager_test.cpp
  module_smoke_test.cpp
  shader_resources_test.cpp
  timebased_widget_test.cpp
  timeline_coordinate_test.cpp
  timeline_workarea_test.cpp
)

target_sources(olive-gtest PRIVATE $<TARGET_OBJECTS:libolive-editor>)

target_include_directories(
  olive-gtest
  PRIVATE
  ${CMAKE_SOURCE_DIR}/app
  ${CMAKE_SOURCE_DIR}/tests
  ${CMAKE_SOURCE_DIR}/third_party/openfx/include
  ${CMAKE_SOURCE_DIR}/third_party/openfx/HostSupport/include
  ${OLIVE_INCLUDE_DIRS}
)

target_link_libraries(
  olive-gtest
  PRIVATE
  ${OLIVE_LIBRARIES}
  GTest::gtest
)

target_compile_definitions(
  olive-gtest
  PRIVATE
  ${OLIVE_DEFINITIONS}
)

target_compile_options(
  olive-gtest
  PRIVATE
  ${OLIVE_COMPILE_OPTIONS}
)

if (MSVC)
  add_test("Olive.gtest" olive-gtest)
else()
  add_test(olive-gtest olive-gtest)
endif()

# Copy required DLLs to the executable directory on Windows
if (WIN32)
  # Find Qt DLLs
  get_target_property(_qt_core_location Qt${QT_VERSION_MAJOR}::Core LOCATION)
  get_filename_component(_qt_bin_dir "${_qt_core_location}" DIRECTORY)

  # Determine Qt DLL suffix based on configuration
  set(_qt_suffix "")
  if (CMAKE_BUILD_TYPE STREQUAL "Debug" OR (CMAKE_CONFIGURATION_TYPES AND "Debug" IN_LIST CMAKE_CONFIGURATION_TYPES))
    set(_qt_suffix "d")
  endif ()

  set(_qt_dlls
          Qt${QT_VERSION_MAJOR}Core${_qt_suffix}.dll
          Qt${QT_VERSION_MAJOR}Gui${_qt_suffix}.dll
          Qt${QT_VERSION_MAJOR}Widgets${_qt_suffix}.dll
          Qt${QT_VERSION_MAJOR}OpenGL${_qt_suffix}.dll
          Qt${QT_VERSION_MAJOR}Concurrent${_qt_suffix}.dll
  )
  if (${QT_VERSION_MAJOR} EQUAL "6")
    list(APPEND _qt_dlls Qt${QT_VERSION_MAJOR}OpenGLWidgets${_qt_suffix}.dll)
  endif ()

  foreach (_dll ${_qt_dlls})
    add_custom_command(TARGET olive-gtest POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_qt_bin_dir}/${_dll}"
            $<TARGET_FILE_DIR:olive-gtest>
            COMMENT "Copying ${_dll} to output directory"
    )
  endforeach ()

  # Copy Qt plugins (platforms directory) - handle Debug/Release
  if (CMAKE_BUILD_TYPE STREQUAL "Debug" OR (CMAKE_CONFIGURATION_TYPES AND "Debug" IN_LIST CMAKE_CONFIGURATION_TYPES))
    set(_qt_plugins_dir "${_qt_bin_dir}/../plugins/platforms")
  else ()
    set(_qt_plugins_dir "${_qt_bin_dir}/../plugins/platforms")
  endif ()
  add_custom_command(TARGET olive-gtest POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${_qt_plugins_dir}"
          $<TARGET_FILE_DIR:olive-gtest>/platforms
          COMMENT "Copying Qt platforms plugins"
  )

  # Copy FFmpeg DLLs if found
  if (TARGET FFMPEG::avutil)
    foreach (_ffmpeg_lib avutil avcodec avformat avfilter swscale swresample)
      if (TARGET FFMPEG::${_ffmpeg_lib})
        get_target_property(_ffmpeg_location FFMPEG::${_ffmpeg_lib} LOCATION)
        if (_ffmpeg_location)
          add_custom_command(TARGET olive-gtest POST_BUILD
                  COMMAND ${CMAKE_COMMAND} -E copy_if_different
                  "${_ffmpeg_location}"
                  $<TARGET_FILE_DIR:olive-gtest>
                  COMMENT "Copying FFmpeg ${_ffmpeg_lib} DLL"
          )
        endif ()
      endif ()
    endforeach ()
  endif ()

  # Copy OpenImageIO DLLs if found
  # Try IMPORTED_LOCATION first (for imported targets), then LOCATION
  if (TARGET OpenImageIO::OpenImageIO)
    # Debug: print all properties
    get_target_property(_oiio_type OpenImageIO::OpenImageIO TYPE)
    message(STATUS "OpenImageIO::OpenImageIO target type: ${_oiio_type}")

    # Try to find DLL location
    unset(_oiio_dll)
    get_target_property(_oiio_imported_loc OpenImageIO::OpenImageIO IMPORTED_LOCATION)
    message(STATUS "OpenImageIO IMPORTED_LOCATION: ${_oiio_imported_loc}")

    if (_oiio_imported_loc AND EXISTS "${_oiio_imported_loc}")
      set(_oiio_dll "${_oiio_imported_loc}")
    else ()
      # Try config-specific locations
      get_target_property(_oiio_implib OpenImageIO::OpenImageIO IMPORTED_IMPLIB)
      message(STATUS "OpenImageIO IMPORTED_IMPLIB: ${_oiio_implib}")

      if (_oiio_implib)
        # From import lib, derive DLL location
        get_filename_component(_oiio_lib_dir "${_oiio_implib}" DIRECTORY)
        get_filename_component(_oiio_base_name "${_oiio_implib}" NAME_WE)
        # Try bin directory (typical vcpkg layout)
        set(_oiio_dll "${_oiio_lib_dir}/../bin/${_oiio_base_name}.dll")
        message(STATUS "Derived OIIO DLL path: ${_oiio_dll}")
      endif ()
    endif ()

    if (_oiio_dll AND EXISTS "${_oiio_dll}")
      add_custom_command(TARGET olive-gtest POST_BUILD
              COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${_oiio_dll}"
              $<TARGET_FILE_DIR:olive-gtest>
              COMMENT "Copying OpenImageIO DLL from ${_oiio_dll}"
      )
    else ()
      message(WARNING "OpenImageIO::OpenImageIO target found but DLL could not be found at ${_oiio_dll}")
    endif ()
  endif ()

  if (TARGET OpenImageIO::OpenImageIO_Util)
    unset(_oiio_util_dll)
    get_target_property(_oiio_util_imported_loc OpenImageIO::OpenImageIO_Util IMPORTED_LOCATION)
    message(STATUS "OpenImageIO_Util IMPORTED_LOCATION: ${_oiio_util_imported_loc}")

    if (_oiio_util_imported_loc AND EXISTS "${_oiio_util_imported_loc}")
      set(_oiio_util_dll "${_oiio_util_imported_loc}")
    else ()
      get_target_property(_oiio_util_implib OpenImageIO::OpenImageIO_Util IMPORTED_IMPLIB)
      message(STATUS "OpenImageIO_Util IMPORTED_IMPLIB: ${_oiio_util_implib}")

      if (_oiio_util_implib)
        get_filename_component(_oiio_util_lib_dir "${_oiio_util_implib}" DIRECTORY)
        get_filename_component(_oiio_util_base_name "${_oiio_util_implib}" NAME_WE)
        set(_oiio_util_dll "${_oiio_util_lib_dir}/../bin/${_oiio_util_base_name}.dll")
        message(STATUS "Derived OIIO_Util DLL path: ${_oiio_util_dll}")
      endif ()
    endif ()

    if (_oiio_util_dll AND EXISTS "${_oiio_util_dll}")
      add_custom_command(TARGET olive-gtest POST_BUILD
              COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${_oiio_util_dll}"
              $<TARGET_FILE_DIR:olive-gtest>
              COMMENT "Copying OpenImageIO_Util DLL from ${_oiio_util_dll}"
      )
    else ()
      message(WARNING "OpenImageIO::OpenImageIO_Util target found but DLL could not be found at ${_oiio_util_dll}")
    endif ()
  endif ()

  # Copy OpenColorIO DLL if found
  if (TARGET OpenColorIO::OpenColorIO)
    get_target_property(_ocio_location OpenColorIO::OpenColorIO LOCATION)
    if (_ocio_location)
      add_custom_command(TARGET olive-gtest POST_BUILD
              COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${_ocio_location}"
              $<TARGET_FILE_DIR:olive-gtest>
              COMMENT "Copying OpenColorIO DLL"
      )
    endif ()
  endif ()

  # Copy PortAudio DLL if found
  if (TARGET portaudio)
    get_target_property(_pa_location portaudio LOCATION)
    if (_pa_location)
      add_custom_command(TARGET olive-gtest POST_BUILD
              COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${_pa_location}"
              $<TARGET_FILE_DIR:olive-gtest>
              COMMENT "Copying PortAudio DLL"
      )
    endif ()
  endif ()

  # Copy expat DLL if found
  if (TARGET expat::expat)
    get_target_property(_expat_location expat::expat LOCATION)
    if (_expat_location)
      add_custom_command(TARGET olive-gtest POST_BUILD
              COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${_expat_location}"
              $<TARGET_FILE_DIR:olive-gtest>
              COMMENT "Copying expat DLL"
      )
    endif ()
  endif ()
endif ()
