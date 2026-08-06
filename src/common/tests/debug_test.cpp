/***

  Oak Video Editor - Non-Linear Video Editor
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

#include <gtest/gtest.h>

#include "common/debug.h"

TEST(OakDebug, LogValidMessage)
{
	EXPECT_EQ(oakcommon_debug_log(OAKCOMMON_DEBUG_WARNING, "hello"),
		  OAKCOMMON_OK);
}

TEST(OakDebug, LogNullMessage)
{
	EXPECT_EQ(oakcommon_debug_log(OAKCOMMON_DEBUG_WARNING, nullptr),
		  OAKCOMMON_E_INVALID);
}

TEST(OakDebug, LogOutOfRangeLevel)
{
	// Out-of-range levels are tolerated and print as UNKNOWN.
	EXPECT_EQ(oakcommon_debug_log(999, "odd level"), OAKCOMMON_OK);
}

TEST(OakDebug, LevelNameRoundTrip)
{
	char buf[16];

	int needed = oakcommon_debug_level_name(OAKCOMMON_DEBUG_WARNING,
						buf, sizeof(buf));
	ASSERT_GT(needed, 0);
	ASSERT_LE(needed, (int)sizeof(buf));
	EXPECT_STREQ(buf, "WARNING");
}

TEST(OakDebug, LevelNameQuerySize)
{
	int needed = oakcommon_debug_level_name(OAKCOMMON_DEBUG_DEBUG,
						nullptr, 0);
	EXPECT_EQ(needed, 6); // "DEBUG" + NUL
}

TEST(OakDebug, LevelNameUnknownLevel)
{
	char buf[16];

	int needed = oakcommon_debug_level_name(-1, buf, sizeof(buf));
	ASSERT_GT(needed, 0);
	ASSERT_LE(needed, (int)sizeof(buf));
	EXPECT_STREQ(buf, "UNKNOWN");
}
