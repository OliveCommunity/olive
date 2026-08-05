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

#include <cstring>
#include <string>
#include <vector>

#include "common/colortransform.h"

namespace
{

std::string read_string(int (*fn)(OakCommonColorTransform *, char *, int),
						OakCommonColorTransform *t)
{
	int needed = fn(t, nullptr, 0);
	EXPECT_GT(needed, 0);
	std::vector<char> buf(needed);
	EXPECT_EQ(fn(t, buf.data(), needed), needed);
	return std::string(buf.data());
}

} // namespace

TEST(CommonColorTransformCApi, InitOutput)
{
	OakCommonColorTransform *t =
		oakcommon_colortransform_init_output("sRGB");
	ASSERT_NE(t, nullptr);

	int is_display = -1;
	EXPECT_EQ(oakcommon_colortransform_is_display(t, &is_display),
			  OAKCOMMON_OK);
	EXPECT_EQ(is_display, 0);
	EXPECT_EQ(read_string(oakcommon_colortransform_get_output, t), "sRGB");
	EXPECT_EQ(read_string(oakcommon_colortransform_get_display, t), "sRGB");

	oakcommon_colortransform_free(t);
}

TEST(CommonColorTransformCApi, InitOutputNullString)
{
	EXPECT_EQ(oakcommon_colortransform_init_output(nullptr), nullptr);
}

TEST(CommonColorTransformCApi, InitDisplay)
{
	OakCommonColorTransform *t =
		oakcommon_colortransform_init_display("sRGB", "Studio", "None");
	ASSERT_NE(t, nullptr);

	int is_display = 0;
	EXPECT_EQ(oakcommon_colortransform_is_display(t, &is_display),
			  OAKCOMMON_OK);
	EXPECT_EQ(is_display, 1);
	EXPECT_EQ(read_string(oakcommon_colortransform_get_display, t), "sRGB");
	EXPECT_EQ(read_string(oakcommon_colortransform_get_view, t), "Studio");
	EXPECT_EQ(read_string(oakcommon_colortransform_get_look, t), "None");

	oakcommon_colortransform_free(t);
}

TEST(CommonColorTransformCApi, InitDisplayNullString)
{
	EXPECT_EQ(oakcommon_colortransform_init_display(nullptr, "v", "l"),
			  nullptr);
	EXPECT_EQ(oakcommon_colortransform_init_display("d", nullptr, "l"),
			  nullptr);
	EXPECT_EQ(oakcommon_colortransform_init_display("d", "v", nullptr),
			  nullptr);
}

TEST(CommonColorTransformCApi, FreeNull)
{
	oakcommon_colortransform_free(nullptr);
}

TEST(CommonColorTransformCApi, NullHandleErrors)
{
	int i = 0;
	char buf[16];
	EXPECT_EQ(oakcommon_colortransform_is_display(nullptr, &i),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_colortransform_get_display(nullptr, buf, sizeof(buf)),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_colortransform_get_output(nullptr, buf, sizeof(buf)),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_colortransform_get_view(nullptr, buf, sizeof(buf)),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_colortransform_get_look(nullptr, buf, sizeof(buf)),
			  OAKCOMMON_E_INVALID);
}

TEST(CommonColorTransformCApi, NullOutParam)
{
	OakCommonColorTransform *t =
		oakcommon_colortransform_init_output("sRGB");
	ASSERT_NE(t, nullptr);
	EXPECT_EQ(oakcommon_colortransform_is_display(t, nullptr),
			  OAKCOMMON_E_INVALID);
	oakcommon_colortransform_free(t);
}
