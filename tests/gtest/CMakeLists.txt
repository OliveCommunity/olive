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
  opengl_thread_test.cpp
  render_videoparams_test.cpp
  render_videoparams_branch_test.cpp
  render_audioparams_test.cpp
  render_audioparams_branch_test.cpp
  render_sampleformat_test.cpp
  render_pixelformat_test.cpp
  render_architecture_test.cpp
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

# Copy required DLLs to the executable directory on Windows (MSYS2).
# Qt libraries are handled by windeployqt.
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
    add_custom_command(TARGET olive-gtest POST_BUILD
      COMMAND "${WINDEPLOYQT_EXECUTABLE}"
        --verbose 0
        --no-compiler-runtime
        --no-translations
        --no-system-d3d-compiler
        \"$<TARGET_FILE:olive-gtest>\"
      COMMENT "Deploying Qt dependencies with windeployqt..."
    )
  else ()
    message(WARNING "windeployqt not found, Qt DLLs may not be copied correctly")
  endif ()

  # ── Copy MSYS2 runtime DLLs ─────────────────────────────────────────────────
  # Function to find and copy DLLs from MSYS2 paths
  function(copy_msys2_dlls)
    # Search paths: Unix style first (MSYS2 terminal), then Windows style
    set(_msys2_search_paths
      # Unix style paths (MSYS2 terminal)
      /ucrt64/bin
      /mingw64/bin
      /mingw32/bin
      # Windows style paths (native Windows)
      "C:/msys64/ucrt64/bin"
      "C:/msys64/mingw64/bin"
      "C:/msys64/mingw32/bin"
    )

    # DLLs to copy from MSYS2
    set(_msys2_dlls
      # FFmpeg
      avcodec-63.dll avformat-63.dll avutil-59.dll avfilter-12.dll
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
        add_custom_command(TARGET olive-gtest POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_dll_path}"
            $<TARGET_FILE_DIR:olive-gtest>
          COMMENT "Copying MSYS2 DLL: ${_dll}"
        )
      endif ()
    endforeach ()
  endfunction()

  copy_msys2_dlls()

  # ── Copy GTest DLL if needed ────────────────────────────────────────────────
  if (TARGET GTest::gtest)
    get_target_property(_gtest_location GTest::gtest IMPORTED_LOCATION)
    if (_gtest_location AND EXISTS "${_gtest_location}")
      get_filename_component(_gtest_dir "${_gtest_location}" DIRECTORY)
      # Also check for DLL in the same directory (MSYS2 style)
      get_filename_component(_gtest_name "${_gtest_location}" NAME_WE)
      if (EXISTS "${_gtest_dir}/${_gtest_name}.dll")
        add_custom_command(TARGET olive-gtest POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_gtest_dir}/${_gtest_name}.dll"
            $<TARGET_FILE_DIR:olive-gtest>
          COMMENT "Copying GTest DLL"
        )
      endif ()
    endif ()
  endif ()

  # Note: We intentionally do NOT copy all DLLs from MSYS2 bin directories.
  # The windeployqt tool handles Qt dependencies, and the _msys2_dlls list
  # above contains only the specific runtime dependencies we need.
  # Copying all DLLs would include unnecessary libraries (Python, OpenCV, etc.)
  # that bloat the distribution and may cause conflicts.
endif ()
