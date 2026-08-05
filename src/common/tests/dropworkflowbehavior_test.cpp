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

#include "common/dropworkflowbehavior.h"

TEST(OakCommonDropWorkflowBehavior, IsValidAcceptsAllEnumerators)
{
	EXPECT_EQ(oakcommon_drop_workflow_behavior_is_valid(OAKCOMMON_DWS_ASK),
		  1);
	EXPECT_EQ(oakcommon_drop_workflow_behavior_is_valid(OAKCOMMON_DWS_AUTO),
		  1);
	EXPECT_EQ(
		oakcommon_drop_workflow_behavior_is_valid(OAKCOMMON_DWS_MANUAL),
		1);
	EXPECT_EQ(
		oakcommon_drop_workflow_behavior_is_valid(OAKCOMMON_DWS_DISABLE),
		1);
}

TEST(OakCommonDropWorkflowBehavior, IsValidRejectsOutOfRange)
{
	EXPECT_EQ(oakcommon_drop_workflow_behavior_is_valid(-1), 0);
	EXPECT_EQ(oakcommon_drop_workflow_behavior_is_valid(4), 0);
}

TEST(OakCommonDropWorkflowBehavior, NameRoundTrip)
{
	char buf[16];

	int needed = oakcommon_drop_workflow_behavior_name(OAKCOMMON_DWS_AUTO,
							   buf, sizeof(buf));
	ASSERT_GT(needed, 0);
	ASSERT_LE(needed, (int)sizeof(buf));
	EXPECT_STREQ(buf, "AUTO");
}

TEST(OakCommonDropWorkflowBehavior, NameQuerySizeAndTooSmallBuffer)
{
	int needed = oakcommon_drop_workflow_behavior_name(OAKCOMMON_DWS_DISABLE,
							   nullptr, 0);
	EXPECT_EQ(needed, 8); // "DISABLE" + NUL

	char buf[4];
	EXPECT_EQ(oakcommon_drop_workflow_behavior_name(OAKCOMMON_DWS_DISABLE,
							buf, sizeof(buf)),
		  needed);
}

TEST(OakCommonDropWorkflowBehavior, NameInvalidValue)
{
	char buf[16];

	int needed = oakcommon_drop_workflow_behavior_name(99, buf, sizeof(buf));
	ASSERT_GT(needed, 0);
	ASSERT_LE(needed, (int)sizeof(buf));
	EXPECT_STREQ(buf, "UNKNOWN");
}
