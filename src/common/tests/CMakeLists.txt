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

find_package(GTest REQUIRED)
include(GoogleTest)

add_executable(oakcommon-gtest
        colortransform_test.cpp
        commandlineparser_test.cpp
        config_test.cpp
        current_test.cpp
        debug_test.cpp
        dropworkflowbehavior_test.cpp
        ffmpegutils_test.cpp
        filefunctions_test.cpp
        handle_test.cpp
        log_test.cpp
        memorypool_test.cpp
        miscutils_test.cpp
        ocioutils_test.cpp
        oiioutils_test.cpp
        power_test.cpp
        qtutils_test.cpp
        subtitleparams_test.cpp
        threadsafemap_test.cpp
        videoparams_test.cpp
        xmlutils_test.cpp
)

target_link_libraries(oakcommon-gtest PRIVATE
        oakcommon
        GTest::gtest
        GTest::gtest_main
)

gtest_discover_tests(oakcommon-gtest)
