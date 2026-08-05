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

#include "../../include/common/power.h"

TEST(Power, CeilToPowerOf2)
{
	uint32_t out = 0;

	ASSERT_EQ(oakcommon_power_ceil_to_power_of_2(1, &out), OAKCOMMON_OK);
	EXPECT_EQ(out, 1u);

	ASSERT_EQ(oakcommon_power_ceil_to_power_of_2(2, &out), OAKCOMMON_OK);
	EXPECT_EQ(out, 2u);

	ASSERT_EQ(oakcommon_power_ceil_to_power_of_2(3, &out), OAKCOMMON_OK);
	EXPECT_EQ(out, 4u);

	ASSERT_EQ(oakcommon_power_ceil_to_power_of_2(1000, &out), OAKCOMMON_OK);
	EXPECT_EQ(out, 1024u);
}

TEST(Power, CeilToPowerOf2NullOut)
{
	EXPECT_EQ(oakcommon_power_ceil_to_power_of_2(3, nullptr),
			  OAKCOMMON_E_INVALID);
}

TEST(Power, FloorToPowerOf2)
{
	uint32_t out = 0;

	ASSERT_EQ(oakcommon_power_floor_to_power_of_2(1, &out), OAKCOMMON_OK);
	EXPECT_EQ(out, 1u);

	ASSERT_EQ(oakcommon_power_floor_to_power_of_2(3, &out), OAKCOMMON_OK);
	EXPECT_EQ(out, 2u);

	ASSERT_EQ(oakcommon_power_floor_to_power_of_2(1024, &out), OAKCOMMON_OK);
	EXPECT_EQ(out, 1024u);

	ASSERT_EQ(oakcommon_power_floor_to_power_of_2(1025, &out), OAKCOMMON_OK);
	EXPECT_EQ(out, 1024u);
}

TEST(Power, FloorToPowerOf2NullOut)
{
	EXPECT_EQ(oakcommon_power_floor_to_power_of_2(3, nullptr),
			  OAKCOMMON_E_INVALID);
}
