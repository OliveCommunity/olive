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

#include <cstdlib>

#include <gtest/gtest.h>

#include "common/current.h"

TEST(OakCommonCurrent, InstanceIsSingleton)
{
	OakCommonCurrent *a = oakcommon_current_instance();
	OakCommonCurrent *b = oakcommon_current_instance();

	ASSERT_NE(a, nullptr);
	EXPECT_EQ(a, b);
}

TEST(OakCommonCurrent, FreeNullIsNoOp)
{
	oakcommon_current_free(nullptr);
	oakcommon_current_free(oakcommon_current_instance());
	SUCCEED();
}

TEST(OakCommonCurrent, VideoParamsSetGetRoundTrip)
{
	OakCommonCurrent *c = oakcommon_current_instance();
	int *params = static_cast<int *>(malloc(sizeof(int)));
	ASSERT_NE(params, nullptr);
	*params = 42;

	ASSERT_EQ(oakcommon_current_set_video_params(c, params, free),
		  OAKCOMMON_OK);

	void *out = nullptr;
	ASSERT_EQ(oakcommon_current_get_video_params(c, &out), OAKCOMMON_OK);
	EXPECT_EQ(out, params);
	EXPECT_EQ(*static_cast<int *>(out), 42);

	// Clearing the slot must invoke the destroy callback.
	ASSERT_EQ(oakcommon_current_set_video_params(c, nullptr, nullptr),
		  OAKCOMMON_OK);
	ASSERT_EQ(oakcommon_current_get_video_params(c, &out), OAKCOMMON_OK);
	EXPECT_EQ(out, nullptr);
}

TEST(OakCommonCurrent, AudioParamsSetGetRoundTrip)
{
	OakCommonCurrent *c = oakcommon_current_instance();
	int value = 7; // non-owning storage, no destroy callback

	ASSERT_EQ(oakcommon_current_set_audio_params(c, &value, nullptr),
		  OAKCOMMON_OK);

	void *out = nullptr;
	ASSERT_EQ(oakcommon_current_get_audio_params(c, &out), OAKCOMMON_OK);
	EXPECT_EQ(out, &value);

	ASSERT_EQ(oakcommon_current_set_audio_params(c, nullptr, nullptr),
		  OAKCOMMON_OK);
}

TEST(OakCommonCurrent, PluginHostAndCacheRoundTrip)
{
	OakCommonCurrent *c = oakcommon_current_instance();
	int host = 1, cache = 2;

	ASSERT_EQ(oakcommon_current_set_plugin_host(c, &host, nullptr),
		  OAKCOMMON_OK);
	ASSERT_EQ(oakcommon_current_set_plugin_cache(c, &cache, nullptr),
		  OAKCOMMON_OK);

	void *out = nullptr;
	ASSERT_EQ(oakcommon_current_get_plugin_host(c, &out), OAKCOMMON_OK);
	EXPECT_EQ(out, &host);
	ASSERT_EQ(oakcommon_current_get_plugin_cache(c, &out), OAKCOMMON_OK);
	EXPECT_EQ(out, &cache);

	ASSERT_EQ(oakcommon_current_set_plugin_host(c, nullptr, nullptr),
		  OAKCOMMON_OK);
	ASSERT_EQ(oakcommon_current_set_plugin_cache(c, nullptr, nullptr),
		  OAKCOMMON_OK);
}

TEST(OakCommonCurrent, NullHandleAndOutArgs)
{
	void *out = nullptr;
	int flag = 0;

	EXPECT_EQ(oakcommon_current_set_video_params(nullptr, &flag, nullptr),
		  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_current_get_video_params(nullptr, &out),
		  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_current_get_video_params(
			  oakcommon_current_instance(), nullptr),
		  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_current_is_interactive(nullptr, &flag),
		  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_current_is_interactive(
			  oakcommon_current_instance(), nullptr),
		  OAKCOMMON_E_INVALID);
}

TEST(OakCommonCurrent, IsInteractive)
{
	int flag = 0;

	ASSERT_EQ(oakcommon_current_is_interactive(
			  oakcommon_current_instance(), &flag),
		  OAKCOMMON_OK);
	EXPECT_EQ(flag, 1);
}
