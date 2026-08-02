# oakcore GTest target — migrated from pure C assert tests
# Copyright (C) 2026 Oak Team
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.

include(GoogleTest)

add_executable(oakcore_gtest
  oakcore_rational_test.cpp
  oakcore_audioparams_test.cpp
  oakcore_bezier_test.cpp
  oakcore_color_test.cpp
  oakcore_fractionutils_test.cpp
  oakcore_samplebuffer_test.cpp
  oakcore_stringutils_test.cpp
  oakcore_timecodefunctions_test.cpp
  oakcore_timerange_test.cpp
)

target_link_libraries(oakcore_gtest PRIVATE
  olivecore
  GTest::gtest
  GTest::gtest_main
)

target_include_directories(oakcore_gtest PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../include"
  "${CMAKE_CURRENT_SOURCE_DIR}/../include/olive/core"
)

gtest_discover_tests(oakcore_gtest)

if (WIN32)
  # gtest discovery executes the binary at build time; it needs the DLL next to it
  add_custom_command(TARGET oakcore_gtest POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:olivecore> $<TARGET_FILE_DIR:oakcore_gtest>)
endif ()
