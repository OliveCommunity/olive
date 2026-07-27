/***

  Oak - Non-Linear Video Editor
  Copyright (C) 2026 Oak Team

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

***/

// Shared entry point for the liboakengine facade GTest binaries.
//
// Unlike the Qt-based olive-gtest harness, these facade tests manage their own
// oakengine_init()/oakengine_shutdown() lifecycle inside each TEST (their init
// flags differ -- headless vs. render vs. lifecycle probing -- so a single
// shared fixture is not possible). This main therefore only bootstraps Google
// Test; no global engine state is created here.

#include <gtest/gtest.h>

int main(int argc, char **argv)
{
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
