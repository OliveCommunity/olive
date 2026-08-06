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

TEST(OakCurrent, InstanceIsSingleton)
{
	OakCurrent a = oakcommon_current_instance();
	OakCurrent b = oakcommon_current_instance();

	ASSERT_NE(a.ctx, nullptr);
	EXPECT_EQ(a.ctx, b.ctx);
	EXPECT_EQ(a.abi_version, OAKCOMMON_ABI_VERSION);
}

TEST(OakCurrent, FreeNullIsNoOp)
{
	oakcommon_current_free(nullptr);
	OakCurrent c = oakcommon_current_instance();
	oakcommon_current_free(&c);
	// Releasing the singleton handle never destroys the object.
	EXPECT_NE(c.ctx, nullptr);
	SUCCEED();
}

TEST(OakCurrent, VideoParamsSetGetRoundTrip)
{
	OakCurrent c = oakcommon_current_instance();
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

TEST(OakCurrent, AudioParamsSetGetRoundTrip)
{
	OakCurrent c = oakcommon_current_instance();
	int value = 7; // non-owning storage, no destroy callback

	ASSERT_EQ(oakcommon_current_set_audio_params(c, &value, nullptr),
		  OAKCOMMON_OK);

	void *out = nullptr;
	ASSERT_EQ(oakcommon_current_get_audio_params(c, &out), OAKCOMMON_OK);
	EXPECT_EQ(out, &value);

	ASSERT_EQ(oakcommon_current_set_audio_params(c, nullptr, nullptr),
		  OAKCOMMON_OK);
}

TEST(OakCurrent, PluginHostAndCacheRoundTrip)
{
	OakCurrent c = oakcommon_current_instance();
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

TEST(OakCurrent, NullHandleAndOutArgs)
{
	void *out = nullptr;
	int flag = 0;

	EXPECT_EQ(oakcommon_current_set_video_params(OakCurrent{}, &flag, nullptr),
		  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_current_get_video_params(OakCurrent{}, &out),
		  OAKCOMMON_E_INVALID);
	OakCurrent c = oakcommon_current_instance();
	EXPECT_EQ(oakcommon_current_get_video_params(c, nullptr),
		  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_current_is_interactive(OakCurrent{}, &flag),
		  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_current_is_interactive(c, nullptr),
		  OAKCOMMON_E_INVALID);
}

TEST(OakCurrent, IsInteractive)
{
	int flag = 0;

	OakCurrent c = oakcommon_current_instance();
	ASSERT_EQ(oakcommon_current_is_interactive(c, &flag), OAKCOMMON_OK);
	EXPECT_EQ(flag, 1);
}
