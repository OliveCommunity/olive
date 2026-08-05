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

#include <ffmpeg_bridge/ffmpeg_bridge.h>

#include "common/ffmpegutils.h"

#include "../src/ffmpegutils.h"

TEST(OakCommonFFmpegUtils, GetCompatiblePixelFormatMapsCorrectly)
{
	int out = -2;

	EXPECT_EQ(oakcommon_ffmpegutils_get_compatible_pixel_format(
			  olive::core::PixelFormat::u8, &out),
		  OAKCOMMON_OK);
	EXPECT_EQ(out, olive::core::PixelFormat::u8);

	EXPECT_EQ(oakcommon_ffmpegutils_get_compatible_pixel_format(
			  olive::core::PixelFormat::u10, &out),
		  OAKCOMMON_OK);
	EXPECT_EQ(out, olive::core::PixelFormat::u8);

	EXPECT_EQ(oakcommon_ffmpegutils_get_compatible_pixel_format(
			  olive::core::PixelFormat::f32, &out),
		  OAKCOMMON_OK);
	EXPECT_EQ(out, olive::core::PixelFormat::u16);

	EXPECT_EQ(oakcommon_ffmpegutils_get_compatible_pixel_format(
			  olive::core::PixelFormat::invalid, &out),
		  OAKCOMMON_OK);
	EXPECT_EQ(out, olive::core::PixelFormat::invalid);
}

TEST(OakCommonFFmpegUtils, GetCompatiblePixelFormatNullOut)
{
	EXPECT_EQ(oakcommon_ffmpegutils_get_compatible_pixel_format(
			  olive::core::PixelFormat::u8, nullptr),
		  OAKCOMMON_E_INVALID);
}

TEST(OakCommonFFmpegUtils, GetFFmpegPixelFormatMapsCorrectly)
{
	int out = -2;

	EXPECT_EQ(oakcommon_ffmpegutils_get_ffmpeg_pixel_format(
			  olive::core::PixelFormat::u8,
			  OAKCOMMON_RGBA_CHANNEL_COUNT, &out),
		  OAKCOMMON_OK);
	EXPECT_EQ(out, fb_pix_fmt_rgba);

	EXPECT_EQ(oakcommon_ffmpegutils_get_ffmpeg_pixel_format(
			  olive::core::PixelFormat::u8,
			  OAKCOMMON_RGB_CHANNEL_COUNT, &out),
		  OAKCOMMON_OK);
	EXPECT_EQ(out, fb_pix_fmt_rg_b24);

	EXPECT_EQ(oakcommon_ffmpegutils_get_ffmpeg_pixel_format(
			  olive::core::PixelFormat::u16,
			  OAKCOMMON_RGBA_CHANNEL_COUNT, &out),
		  OAKCOMMON_OK);
	EXPECT_EQ(out, fb_pix_fmt_rgb_a64_le);

	EXPECT_EQ(oakcommon_ffmpegutils_get_ffmpeg_pixel_format(
			  olive::core::PixelFormat::invalid,
			  OAKCOMMON_RGBA_CHANNEL_COUNT, &out),
		  OAKCOMMON_OK);
	EXPECT_EQ(out, fb_pix_fmt_none);

	EXPECT_EQ(oakcommon_ffmpegutils_get_ffmpeg_pixel_format(
			  olive::core::PixelFormat::u8, 2, &out),
		  OAKCOMMON_OK);
	EXPECT_EQ(out, fb_pix_fmt_none);
}

TEST(OakCommonFFmpegUtils, GetFFmpegPixelFormatNullOut)
{
	EXPECT_EQ(oakcommon_ffmpegutils_get_ffmpeg_pixel_format(
			  olive::core::PixelFormat::u8,
			  OAKCOMMON_RGBA_CHANNEL_COUNT, nullptr),
		  OAKCOMMON_E_INVALID);
}

TEST(OakCommonFFmpegUtils, GetNativeSampleFormatMapsCorrectly)
{
	int out = -2;

	EXPECT_EQ(oakcommon_ffmpegutils_get_native_sample_format(
			  fb_sample_fmt_u8, &out),
		  OAKCOMMON_OK);
	EXPECT_EQ(out, olive::core::SampleFormat::u8);

	EXPECT_EQ(oakcommon_ffmpegutils_get_native_sample_format(
			  fb_sample_fmt_fltp, &out),
		  OAKCOMMON_OK);
	EXPECT_EQ(out, olive::core::SampleFormat::f32_p);

	EXPECT_EQ(oakcommon_ffmpegutils_get_native_sample_format(
			  fb_sample_fmt_dblp, &out),
		  OAKCOMMON_OK);
	EXPECT_EQ(out, olive::core::SampleFormat::f64_p);

	EXPECT_EQ(oakcommon_ffmpegutils_get_native_sample_format(
			  fb_sample_fmt_none, &out),
		  OAKCOMMON_OK);
	EXPECT_EQ(out, olive::core::SampleFormat::invalid);
}

TEST(OakCommonFFmpegUtils, GetNativeSampleFormatNullOut)
{
	EXPECT_EQ(oakcommon_ffmpegutils_get_native_sample_format(
			  fb_sample_fmt_u8, nullptr),
		  OAKCOMMON_E_INVALID);
}

TEST(OakCommonFFmpegUtils, GetFFmpegSampleFormatMapsCorrectly)
{
	int out = -2;

	EXPECT_EQ(oakcommon_ffmpegutils_get_ffmpeg_sample_format(
			  olive::core::SampleFormat::u8, &out),
		  OAKCOMMON_OK);
	EXPECT_EQ(out, fb_sample_fmt_u8);

	EXPECT_EQ(oakcommon_ffmpegutils_get_ffmpeg_sample_format(
			  olive::core::SampleFormat::f32_p, &out),
		  OAKCOMMON_OK);
	EXPECT_EQ(out, fb_sample_fmt_fltp);

	EXPECT_EQ(oakcommon_ffmpegutils_get_ffmpeg_sample_format(
			  olive::core::SampleFormat::f64_p, &out),
		  OAKCOMMON_OK);
	EXPECT_EQ(out, fb_sample_fmt_dblp);

	EXPECT_EQ(oakcommon_ffmpegutils_get_ffmpeg_sample_format(
			  olive::core::SampleFormat::invalid, &out),
		  OAKCOMMON_OK);
	EXPECT_EQ(out, fb_sample_fmt_none);
}

TEST(OakCommonFFmpegUtils, GetFFmpegSampleFormatNullOut)
{
	EXPECT_EQ(oakcommon_ffmpegutils_get_ffmpeg_sample_format(
			  olive::core::SampleFormat::u8, nullptr),
		  OAKCOMMON_E_INVALID);
}

TEST(OakCommonFFmpegUtils, ConvertJpegSpaceToRegularSpaceMapsCorrectly)
{
	int out = -2;

	EXPECT_EQ(oakcommon_ffmpegutils_convert_jpeg_space_to_regular_space(
			  fb_pix_fmt_yuv_j420_p, &out),
		  OAKCOMMON_OK);
	EXPECT_EQ(out, fb_pix_fmt_yu_v420_p);

	EXPECT_EQ(oakcommon_ffmpegutils_convert_jpeg_space_to_regular_space(
			  fb_pix_fmt_yuv_j444_p, &out),
		  OAKCOMMON_OK);
	EXPECT_EQ(out, fb_pix_fmt_yu_v444_p);

	EXPECT_EQ(oakcommon_ffmpegutils_convert_jpeg_space_to_regular_space(
			  fb_pix_fmt_rgba, &out),
		  OAKCOMMON_OK);
	EXPECT_EQ(out, fb_pix_fmt_rgba);
}

TEST(OakCommonFFmpegUtils, ConvertJpegSpaceToRegularSpaceNullOut)
{
	EXPECT_EQ(oakcommon_ffmpegutils_convert_jpeg_space_to_regular_space(
			  fb_pix_fmt_rgba, nullptr),
		  OAKCOMMON_E_INVALID);
}

TEST(OakCommonFFmpegUtils, GetCompatibleBridgePixelFormatNullOut)
{
	EXPECT_EQ(oakcommon_ffmpegutils_get_compatible_bridge_pixel_format(
			  fb_pix_fmt_rgba, -1, nullptr),
		  OAKCOMMON_E_INVALID);
}

TEST(OakCommonFFmpegUtils, GetCompatibleBridgePixelFormatMapsCorrectly)
{
	/* Calls fb_find_best_pix_fmt_of_list() in ffmpeg_bridge, which
	 * requires a working FFmpeg runtime environment. */
	GTEST_SKIP() << "requires FFmpeg runtime environment";
}
