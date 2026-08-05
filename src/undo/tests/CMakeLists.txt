# Oak Video Editor - Non-Linear Video Editor
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

find_package(GTest REQUIRED)
include(GoogleTest)

add_executable(oakundo-gtest
        undocommand_test.cpp
        undostack_test.cpp
)

target_link_libraries(oakundo-gtest PRIVATE
        oakundo
        GTest::gtest
        GTest::gtest_main
)

gtest_discover_tests(oakundo-gtest)
