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

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "common/videoparams.h"

TEST(CommonVideoParamsCApi, InitDefaults)
{
	OakCommonVideoParams *p = oakcommon_videoparams_init();
	ASSERT_NE(p, nullptr);

	int v = -1;
	EXPECT_EQ(oakcommon_videoparams_get_width(p, &v), OAKCOMMON_OK);
	EXPECT_EQ(v, 0);
	EXPECT_EQ(oakcommon_videoparams_get_is_valid(p, &v), OAKCOMMON_OK);
	EXPECT_EQ(v, 0);

	oakcommon_videoparams_free(p);
}

TEST(CommonVideoParamsCApi, FreeNull)
{
	oakcommon_videoparams_free(nullptr);
}

TEST(CommonVideoParamsCApi, InitBasic)
{
	OakCommonVideoParams *p = oakcommon_videoparams_init_basic(
		1920, 1080, OAKCOMMON_PIXEL_FORMAT_U8,
		4, 1, 1, OAKCOMMON_VIDEO_INTERLACE_NONE, 2);
	ASSERT_NE(p, nullptr);

	int v;
	EXPECT_EQ(oakcommon_videoparams_get_width(p, &v), OAKCOMMON_OK);
	EXPECT_EQ(v, 1920);
	EXPECT_EQ(oakcommon_videoparams_get_height(p, &v), OAKCOMMON_OK);
	EXPECT_EQ(v, 1080);
	EXPECT_EQ(oakcommon_videoparams_get_depth(p, &v), OAKCOMMON_OK);
	EXPECT_EQ(v, 1);
	EXPECT_EQ(oakcommon_videoparams_get_is_3d(p, &v), OAKCOMMON_OK);
	EXPECT_EQ(v, 0);
	EXPECT_EQ(oakcommon_videoparams_get_format(p, &v), OAKCOMMON_OK);
	EXPECT_EQ(v, OAKCOMMON_PIXEL_FORMAT_U8);
	EXPECT_EQ(oakcommon_videoparams_get_channel_count(p, &v), OAKCOMMON_OK);
	EXPECT_EQ(v, 4);
	EXPECT_EQ(oakcommon_videoparams_get_divider(p, &v), OAKCOMMON_OK);
	EXPECT_EQ(v, 2);
	EXPECT_EQ(oakcommon_videoparams_get_interlacing(p, &v), OAKCOMMON_OK);
	EXPECT_EQ(v, OAKCOMMON_VIDEO_INTERLACE_NONE);
	EXPECT_EQ(oakcommon_videoparams_get_is_valid(p, &v), OAKCOMMON_OK);
	EXPECT_EQ(v, 1);

	// Effective size is divided by the divider
	EXPECT_EQ(oakcommon_videoparams_get_effective_width(p, &v), OAKCOMMON_OK);
	EXPECT_EQ(v, 960);
	EXPECT_EQ(oakcommon_videoparams_get_effective_height(p, &v), OAKCOMMON_OK);
	EXPECT_EQ(v, 540);
	EXPECT_EQ(oakcommon_videoparams_get_square_pixel_width(p, &v),
			  OAKCOMMON_OK);
	EXPECT_EQ(v, 1920);

	// u8 RGBA: 4 bytes per pixel
	EXPECT_EQ(oakcommon_videoparams_get_bytes_per_channel(p, &v),
			  OAKCOMMON_OK);
	EXPECT_EQ(v, 1);
	EXPECT_EQ(oakcommon_videoparams_get_bytes_per_pixel(p, &v), OAKCOMMON_OK);
	EXPECT_EQ(v, 4);
	EXPECT_EQ(oakcommon_videoparams_get_buffer_size(p, &v), OAKCOMMON_OK);
	EXPECT_EQ(v, 1920 * 1080 * 4);

	oakcommon_videoparams_free(p);
}

TEST(CommonVideoParamsCApi, InitWithTimeBase)
{
	OakCommonVideoParams *p = oakcommon_videoparams_init_with_time_base(
		1280, 720, 1001, 30000, OAKCOMMON_PIXEL_FORMAT_F32, 4, 1, 1,
		OAKCOMMON_VIDEO_INTERLACE_NONE, 1);
	ASSERT_NE(p, nullptr);

	int num, den;
	EXPECT_EQ(oakcommon_videoparams_get_time_base(p, &num, &den),
			  OAKCOMMON_OK);
	EXPECT_EQ(num, 1001);
	EXPECT_EQ(den, 30000);

	// Frame rate is the flipped time base
	EXPECT_EQ(oakcommon_videoparams_get_frame_rate(p, &num, &den),
			  OAKCOMMON_OK);
	EXPECT_EQ(num, 30000);
	EXPECT_EQ(den, 1001);
	EXPECT_EQ(oakcommon_videoparams_frame_rate_as_time_base(p, &num, &den),
			  OAKCOMMON_OK);
	EXPECT_EQ(num, 1001);
	EXPECT_EQ(den, 30000);

	oakcommon_videoparams_free(p);
}

TEST(CommonVideoParamsCApi, ScalarSetters)
{
	OakCommonVideoParams *p = oakcommon_videoparams_init();
	ASSERT_NE(p, nullptr);

	int v;
	EXPECT_EQ(oakcommon_videoparams_set_width(p, 640), OAKCOMMON_OK);
	EXPECT_EQ(oakcommon_videoparams_set_height(p, 480), OAKCOMMON_OK);
	EXPECT_EQ(oakcommon_videoparams_set_depth(p, 4), OAKCOMMON_OK);
	EXPECT_EQ(oakcommon_videoparams_get_is_3d(p, &v), OAKCOMMON_OK);
	EXPECT_EQ(v, 1);
	EXPECT_EQ(oakcommon_videoparams_set_format(p, OAKCOMMON_PIXEL_FORMAT_U16),
			  OAKCOMMON_OK);
	EXPECT_EQ(oakcommon_videoparams_set_channel_count(p, 3), OAKCOMMON_OK);
	EXPECT_EQ(oakcommon_videoparams_set_interlacing(
				  p, OAKCOMMON_VIDEO_INTERLACED_TOP_FIRST),
			  OAKCOMMON_OK);
	EXPECT_EQ(oakcommon_videoparams_set_divider(p, 4), OAKCOMMON_OK);
	EXPECT_EQ(oakcommon_videoparams_set_enabled(p, 0), OAKCOMMON_OK);
	EXPECT_EQ(oakcommon_videoparams_set_stream_index(p, 5), OAKCOMMON_OK);
	EXPECT_EQ(oakcommon_videoparams_set_video_type(
				  p, OAKCOMMON_VIDEO_TYPE_IMAGE_SEQUENCE),
			  OAKCOMMON_OK);
	EXPECT_EQ(oakcommon_videoparams_set_premultiplied_alpha(p, 1),
			  OAKCOMMON_OK);
	EXPECT_EQ(oakcommon_videoparams_set_color_range(
				  p, OAKCOMMON_COLOR_RANGE_FULL),
			  OAKCOMMON_OK);
	EXPECT_EQ(oakcommon_videoparams_set_color_primaries(p, 9), OAKCOMMON_OK);
	EXPECT_EQ(oakcommon_videoparams_set_color_transfer(p, 16), OAKCOMMON_OK);
	EXPECT_EQ(oakcommon_videoparams_set_time_base(p, 1, 25), OAKCOMMON_OK);
	EXPECT_EQ(oakcommon_videoparams_set_frame_rate(p, 25, 1), OAKCOMMON_OK);
	EXPECT_EQ(oakcommon_videoparams_set_pixel_aspect_ratio(p, 4, 3),
			  OAKCOMMON_OK);

	float f;
	EXPECT_EQ(oakcommon_videoparams_set_x(p, 1.5f), OAKCOMMON_OK);
	EXPECT_EQ(oakcommon_videoparams_set_y(p, -2.5f), OAKCOMMON_OK);
	EXPECT_EQ(oakcommon_videoparams_get_x(p, &f), OAKCOMMON_OK);
	EXPECT_FLOAT_EQ(f, 1.5f);
	EXPECT_EQ(oakcommon_videoparams_get_y(p, &f), OAKCOMMON_OK);
	EXPECT_FLOAT_EQ(f, -2.5f);

	int64_t i64;
	EXPECT_EQ(oakcommon_videoparams_set_start_time(p, 100), OAKCOMMON_OK);
	EXPECT_EQ(oakcommon_videoparams_set_duration(p, 250), OAKCOMMON_OK);
	EXPECT_EQ(oakcommon_videoparams_get_start_time(p, &i64), OAKCOMMON_OK);
	EXPECT_EQ(i64, 100);
	EXPECT_EQ(oakcommon_videoparams_get_duration(p, &i64), OAKCOMMON_OK);
	EXPECT_EQ(i64, 250);

	EXPECT_EQ(oakcommon_videoparams_get_width(p, &v), OAKCOMMON_OK);
	EXPECT_EQ(v, 640);
	EXPECT_EQ(oakcommon_videoparams_get_effective_width(p, &v), OAKCOMMON_OK);
	EXPECT_EQ(v, 160);
	EXPECT_EQ(oakcommon_videoparams_get_interlacing(p, &v), OAKCOMMON_OK);
	EXPECT_EQ(v, OAKCOMMON_VIDEO_INTERLACED_TOP_FIRST);
	EXPECT_EQ(oakcommon_videoparams_get_enabled(p, &v), OAKCOMMON_OK);
	EXPECT_EQ(v, 0);
	EXPECT_EQ(oakcommon_videoparams_get_stream_index(p, &v), OAKCOMMON_OK);
	EXPECT_EQ(v, 5);
	EXPECT_EQ(oakcommon_videoparams_get_video_type(p, &v), OAKCOMMON_OK);
	EXPECT_EQ(v, OAKCOMMON_VIDEO_TYPE_IMAGE_SEQUENCE);
	EXPECT_EQ(oakcommon_videoparams_get_premultiplied_alpha(p, &v),
			  OAKCOMMON_OK);
	EXPECT_EQ(v, 1);
	EXPECT_EQ(oakcommon_videoparams_get_color_range(p, &v), OAKCOMMON_OK);
	EXPECT_EQ(v, OAKCOMMON_COLOR_RANGE_FULL);
	EXPECT_EQ(oakcommon_videoparams_get_color_primaries(p, &v), OAKCOMMON_OK);
	EXPECT_EQ(v, 9);
	EXPECT_EQ(oakcommon_videoparams_get_color_transfer(p, &v), OAKCOMMON_OK);
	EXPECT_EQ(v, 16);

	int num, den;
	EXPECT_EQ(oakcommon_videoparams_get_pixel_aspect_ratio(p, &num, &den),
			  OAKCOMMON_OK);
	EXPECT_EQ(num, 4);
	EXPECT_EQ(den, 3);
	// Square pixel width = 640 * 4/3 = 853.33 -> 853
	EXPECT_EQ(oakcommon_videoparams_get_square_pixel_width(p, &v),
			  OAKCOMMON_OK);
	EXPECT_EQ(v, 853);
	EXPECT_EQ(oakcommon_videoparams_get_effective_depth(p, &v), OAKCOMMON_OK);
	EXPECT_EQ(v, 1);

	oakcommon_videoparams_free(p);
}

TEST(CommonVideoParamsCApi, NullHandleErrors)
{
	int i;
	float f;
	int64_t i64;
	char buf[16];
	EXPECT_EQ(oakcommon_videoparams_get_width(nullptr, &i),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_videoparams_set_width(nullptr, 1),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_videoparams_get_height(nullptr, &i),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_videoparams_set_height(nullptr, 1),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_videoparams_get_depth(nullptr, &i),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_videoparams_set_depth(nullptr, 1),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_videoparams_get_is_3d(nullptr, &i),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_videoparams_get_time_base(nullptr, &i, &i),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_videoparams_set_time_base(nullptr, 1, 1),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_videoparams_get_frame_rate(nullptr, &i, &i),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_videoparams_set_frame_rate(nullptr, 1, 1),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_videoparams_frame_rate_as_time_base(nullptr, &i, &i),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_videoparams_get_pixel_aspect_ratio(nullptr, &i, &i),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_videoparams_set_pixel_aspect_ratio(nullptr, 1, 1),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_videoparams_get_format(nullptr, &i),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_videoparams_set_format(nullptr, 0),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_videoparams_get_channel_count(nullptr, &i),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_videoparams_set_channel_count(nullptr, 1),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_videoparams_get_interlacing(nullptr, &i),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_videoparams_set_interlacing(nullptr, 0),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_videoparams_get_divider(nullptr, &i),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_videoparams_set_divider(nullptr, 1),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_videoparams_get_enabled(nullptr, &i),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_videoparams_set_enabled(nullptr, 1),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_videoparams_get_x(nullptr, &f), OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_videoparams_set_x(nullptr, 0.0f), OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_videoparams_get_y(nullptr, &f), OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_videoparams_set_y(nullptr, 0.0f), OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_videoparams_get_stream_index(nullptr, &i),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_videoparams_set_stream_index(nullptr, 0),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_videoparams_get_video_type(nullptr, &i),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_videoparams_set_video_type(nullptr, 0),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_videoparams_get_start_time(nullptr, &i64),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_videoparams_set_start_time(nullptr, 0),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_videoparams_get_duration(nullptr, &i64),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_videoparams_set_duration(nullptr, 0),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_videoparams_get_premultiplied_alpha(nullptr, &i),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_videoparams_set_premultiplied_alpha(nullptr, 0),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_videoparams_get_color_range(nullptr, &i),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_videoparams_set_color_range(nullptr, 0),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_videoparams_get_color_primaries(nullptr, &i),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_videoparams_set_color_primaries(nullptr, 0),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_videoparams_get_color_transfer(nullptr, &i),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_videoparams_set_color_transfer(nullptr, 0),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_videoparams_get_colorspace(nullptr, buf, sizeof(buf)),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_videoparams_set_colorspace(nullptr, "x"),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_videoparams_get_square_pixel_width(nullptr, &i),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_videoparams_get_effective_width(nullptr, &i),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_videoparams_get_effective_height(nullptr, &i),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_videoparams_get_effective_depth(nullptr, &i),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_videoparams_get_is_valid(nullptr, &i),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_videoparams_get_bytes_per_channel(nullptr, &i),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_videoparams_get_bytes_per_pixel(nullptr, &i),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_videoparams_get_buffer_size(nullptr, &i),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_videoparams_get_time_in_timebase_units(nullptr, 0, 1,
															   &i64),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_videoparams_load_xml(nullptr, "<a/>"),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_videoparams_save_xml(nullptr, buf, sizeof(buf)),
			  OAKCOMMON_E_INVALID);
}

TEST(CommonVideoParamsCApi, NullOutParamErrors)
{
	OakCommonVideoParams *p = oakcommon_videoparams_init();
	ASSERT_NE(p, nullptr);
	EXPECT_EQ(oakcommon_videoparams_get_width(p, nullptr),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_videoparams_get_time_base(p, nullptr, nullptr),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_videoparams_set_colorspace(p, nullptr),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_videoparams_load_xml(p, nullptr), OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_videoparams_load_xml(p, "not xml"),
			  OAKCOMMON_E_FAILED);
	oakcommon_videoparams_free(p);
}

TEST(CommonVideoParamsCApi, Colorspace)
{
	OakCommonVideoParams *p = oakcommon_videoparams_init();
	ASSERT_NE(p, nullptr);
	EXPECT_EQ(oakcommon_videoparams_set_colorspace(p, "rec709"),
			  OAKCOMMON_OK);
	int needed = oakcommon_videoparams_get_colorspace(p, nullptr, 0);
	ASSERT_GT(needed, 0);
	std::vector<char> buf(needed);
	EXPECT_EQ(oakcommon_videoparams_get_colorspace(p, buf.data(), needed),
			  needed);
	EXPECT_STREQ(buf.data(), "rec709");
	oakcommon_videoparams_free(p);
}

TEST(CommonVideoParamsCApi, TimeInTimebaseUnits)
{
	OakCommonVideoParams *p = oakcommon_videoparams_init_with_time_base(
		1920, 1080, 1, 25, OAKCOMMON_PIXEL_FORMAT_U8, 4, 1, 1,
		OAKCOMMON_VIDEO_INTERLACE_NONE, 1);
	ASSERT_NE(p, nullptr);

	int64_t ts = -1;
	EXPECT_EQ(oakcommon_videoparams_get_time_in_timebase_units(p, 2, 1, &ts),
			  OAKCOMMON_OK);
	EXPECT_EQ(ts, 50); // 2 seconds at 25 fps

	// Without a time base the result is AV_NOPTS_VALUE
	OakCommonVideoParams *q = oakcommon_videoparams_init();
	ASSERT_NE(q, nullptr);
	EXPECT_EQ(oakcommon_videoparams_get_time_in_timebase_units(q, 2, 1, &ts),
			  OAKCOMMON_OK);
	EXPECT_EQ(ts, INT64_MIN);

	oakcommon_videoparams_free(p);
	oakcommon_videoparams_free(q);
}

TEST(CommonVideoParamsCApi, Equals)
{
	OakCommonVideoParams *a = oakcommon_videoparams_init_basic(
		1920, 1080, OAKCOMMON_PIXEL_FORMAT_U8, 4, 1, 1,
		OAKCOMMON_VIDEO_INTERLACE_NONE, 1);
	OakCommonVideoParams *b = oakcommon_videoparams_init_basic(
		1920, 1080, OAKCOMMON_PIXEL_FORMAT_U8, 4, 1, 1,
		OAKCOMMON_VIDEO_INTERLACE_NONE, 1);
	ASSERT_NE(a, nullptr);
	ASSERT_NE(b, nullptr);

	int equal = 0;
	EXPECT_EQ(oakcommon_videoparams_equals(a, b, &equal), OAKCOMMON_OK);
	EXPECT_EQ(equal, 1);

	EXPECT_EQ(oakcommon_videoparams_set_divider(b, 2), OAKCOMMON_OK);
	EXPECT_EQ(oakcommon_videoparams_equals(a, b, &equal), OAKCOMMON_OK);
	EXPECT_EQ(equal, 0);

	EXPECT_EQ(oakcommon_videoparams_equals(nullptr, b, &equal),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_videoparams_equals(a, nullptr, &equal),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_videoparams_equals(a, b, nullptr),
			  OAKCOMMON_E_INVALID);

	oakcommon_videoparams_free(a);
	oakcommon_videoparams_free(b);
}

TEST(CommonVideoParamsCApi, XmlRoundTrip)
{
	OakCommonVideoParams *p = oakcommon_videoparams_init_with_time_base(
		1920, 1080, 1, 25, OAKCOMMON_PIXEL_FORMAT_F16, 4, 4, 3,
		OAKCOMMON_VIDEO_INTERLACED_BOTTOM_FIRST, 2);
	ASSERT_NE(p, nullptr);
	EXPECT_EQ(oakcommon_videoparams_set_colorspace(p, "rec709"),
			  OAKCOMMON_OK);
	EXPECT_EQ(oakcommon_videoparams_set_color_primaries(p, 9), OAKCOMMON_OK);

	int needed = oakcommon_videoparams_save_xml(p, nullptr, 0);
	ASSERT_GT(needed, 0);
	std::vector<char> buf(needed);
	ASSERT_EQ(oakcommon_videoparams_save_xml(p, buf.data(), needed), needed);

	OakCommonVideoParams *q = oakcommon_videoparams_init();
	ASSERT_NE(q, nullptr);
	ASSERT_EQ(oakcommon_videoparams_load_xml(q, buf.data()), OAKCOMMON_OK);

	int equal = 0;
	EXPECT_EQ(oakcommon_videoparams_equals(p, q, &equal), OAKCOMMON_OK);
	EXPECT_EQ(equal, 1);

	int num, den;
	EXPECT_EQ(oakcommon_videoparams_get_frame_rate(q, &num, &den),
			  OAKCOMMON_OK);
	EXPECT_EQ(num, 25);
	EXPECT_EQ(den, 1);

	int cneeded = oakcommon_videoparams_get_colorspace(q, nullptr, 0);
	std::vector<char> cbuf(cneeded);
	EXPECT_EQ(oakcommon_videoparams_get_colorspace(q, cbuf.data(), cneeded),
			  cneeded);
	EXPECT_STREQ(cbuf.data(), "rec709");

	int v;
	EXPECT_EQ(oakcommon_videoparams_get_color_primaries(q, &v), OAKCOMMON_OK);
	EXPECT_EQ(v, 9);

	oakcommon_videoparams_free(p);
	oakcommon_videoparams_free(q);
}

TEST(CommonVideoParamsCApi, StaticHelpers)
{
	EXPECT_EQ(oakcommon_videoparams_get_bytes_per_channel_for_format(
				  OAKCOMMON_PIXEL_FORMAT_U8),
			  1);
	EXPECT_EQ(oakcommon_videoparams_get_bytes_per_channel_for_format(
				  OAKCOMMON_PIXEL_FORMAT_F32),
			  4);
	EXPECT_EQ(oakcommon_videoparams_get_bytes_per_channel_for_format(
				  OAKCOMMON_PIXEL_FORMAT_INVALID),
			  0);

	EXPECT_EQ(oakcommon_videoparams_get_bytes_per_pixel_for_format(
				  OAKCOMMON_PIXEL_FORMAT_U10, 4),
			  4);
	EXPECT_EQ(oakcommon_videoparams_get_bytes_per_pixel_for_format(
				  OAKCOMMON_PIXEL_FORMAT_U10, 3),
			  0);
	EXPECT_EQ(oakcommon_videoparams_get_bytes_per_pixel_for_format(
				  OAKCOMMON_PIXEL_FORMAT_U16, 3),
			  6);

	EXPECT_EQ(oakcommon_videoparams_calculate_buffer_size(
				  100, 50, OAKCOMMON_PIXEL_FORMAT_U8, 4),
			  20000);

	EXPECT_EQ(oakcommon_videoparams_format_is_float(
				  OAKCOMMON_PIXEL_FORMAT_F16),
			  1);
	EXPECT_EQ(oakcommon_videoparams_format_is_float(OAKCOMMON_PIXEL_FORMAT_U8),
			  0);

	EXPECT_EQ(oakcommon_videoparams_generate_auto_divider(1920, 1080), 2);
	EXPECT_EQ(oakcommon_videoparams_generate_auto_divider(640, 480), 1);
	EXPECT_EQ(oakcommon_videoparams_generate_auto_divider(16000, 16000), 16);

	EXPECT_EQ(oakcommon_videoparams_get_scaled_dimension(1920, 2), 960);
	EXPECT_EQ(oakcommon_videoparams_get_divider_for_target_resolution(
				  3840, 2160, 1920, 1080),
			  2);
}

TEST(CommonVideoParamsCApi, StaticStrings)
{
	char buf[64];

	int needed = oakcommon_videoparams_get_name_for_divider(1, nullptr, 0);
	ASSERT_GT(needed, 0);
	EXPECT_EQ(oakcommon_videoparams_get_name_for_divider(1, buf, sizeof(buf)),
			  needed);
	EXPECT_STREQ(buf, "Full");
	EXPECT_GT(oakcommon_videoparams_get_name_for_divider(4, buf, sizeof(buf)),
			  0);
	EXPECT_STREQ(buf, "1/4");

	EXPECT_GT(oakcommon_videoparams_get_format_name(OAKCOMMON_PIXEL_FORMAT_U8,
													buf, sizeof(buf)),
			  0);
	EXPECT_STREQ(buf, "8-bit");
	EXPECT_GT(oakcommon_videoparams_get_format_name(
				  OAKCOMMON_PIXEL_FORMAT_INVALID, buf, sizeof(buf)),
			  0);
	EXPECT_NE(std::string(buf).find("Unknown"), std::string::npos);

	EXPECT_GT(oakcommon_videoparams_frame_rate_to_string(25, 1, buf,
														 sizeof(buf)),
			  0);
	EXPECT_STREQ(buf, "25 FPS");
}
