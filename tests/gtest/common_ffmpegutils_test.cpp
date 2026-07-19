#include <gtest/gtest.h>

#include "common/ffmpegutils.h"

using namespace olive;

TEST(CommonFFmpegUtils, GetNativeSampleFormatMapsCorrectly)
{
	EXPECT_EQ(FFmpegUtils::get_native_sample_format(fb_sample_fmt_u8),
			  SampleFormat::u8);
	EXPECT_EQ(FFmpegUtils::get_native_sample_format(fb_sample_fmt_s16),
			  SampleFormat::s16);
	EXPECT_EQ(FFmpegUtils::get_native_sample_format(fb_sample_fmt_s32),
			  SampleFormat::s32);
	EXPECT_EQ(FFmpegUtils::get_native_sample_format(fb_sample_fmt_s64),
			  SampleFormat::s64);
	EXPECT_EQ(FFmpegUtils::get_native_sample_format(fb_sample_fmt_flt),
			  SampleFormat::f32);
	EXPECT_EQ(FFmpegUtils::get_native_sample_format(fb_sample_fmt_dbl),
			  SampleFormat::f64);
	EXPECT_EQ(FFmpegUtils::get_native_sample_format(fb_sample_fmt_u8_p),
			  SampleFormat::u8_p);
	EXPECT_EQ(FFmpegUtils::get_native_sample_format(fb_sample_fmt_s16_p),
			  SampleFormat::s16_p);
	EXPECT_EQ(FFmpegUtils::get_native_sample_format(fb_sample_fmt_s32_p),
			  SampleFormat::s32_p);
	EXPECT_EQ(FFmpegUtils::get_native_sample_format(fb_sample_fmt_s64_p),
			  SampleFormat::s64_p);
	EXPECT_EQ(FFmpegUtils::get_native_sample_format(fb_sample_fmt_fltp),
			  SampleFormat::f32_p);
	EXPECT_EQ(FFmpegUtils::get_native_sample_format(fb_sample_fmt_dblp),
			  SampleFormat::f64_p);
	EXPECT_EQ(FFmpegUtils::get_native_sample_format(fb_sample_fmt_none),
			  SampleFormat::invalid);
}

TEST(CommonFFmpegUtils, GetFFmpegSampleFormatMapsCorrectly)
{
	EXPECT_EQ(FFmpegUtils::get_f_fmpeg_sample_format(SampleFormat::u8),
			  fb_sample_fmt_u8);
	EXPECT_EQ(FFmpegUtils::get_f_fmpeg_sample_format(SampleFormat::s16),
			  fb_sample_fmt_s16);
	EXPECT_EQ(FFmpegUtils::get_f_fmpeg_sample_format(SampleFormat::s32),
			  fb_sample_fmt_s32);
	EXPECT_EQ(FFmpegUtils::get_f_fmpeg_sample_format(SampleFormat::s64),
			  fb_sample_fmt_s64);
	EXPECT_EQ(FFmpegUtils::get_f_fmpeg_sample_format(SampleFormat::f32),
			  fb_sample_fmt_flt);
	EXPECT_EQ(FFmpegUtils::get_f_fmpeg_sample_format(SampleFormat::f64),
			  fb_sample_fmt_dbl);
	EXPECT_EQ(FFmpegUtils::get_f_fmpeg_sample_format(SampleFormat::u8_p),
			  fb_sample_fmt_u8_p);
	EXPECT_EQ(FFmpegUtils::get_f_fmpeg_sample_format(SampleFormat::s16_p),
			  fb_sample_fmt_s16_p);
	EXPECT_EQ(FFmpegUtils::get_f_fmpeg_sample_format(SampleFormat::s32_p),
			  fb_sample_fmt_s32_p);
	EXPECT_EQ(FFmpegUtils::get_f_fmpeg_sample_format(SampleFormat::s64_p),
			  fb_sample_fmt_s64_p);
	EXPECT_EQ(FFmpegUtils::get_f_fmpeg_sample_format(SampleFormat::f32_p),
			  fb_sample_fmt_fltp);
	EXPECT_EQ(FFmpegUtils::get_f_fmpeg_sample_format(SampleFormat::f64_p),
			  fb_sample_fmt_dblp);
	EXPECT_EQ(FFmpegUtils::get_f_fmpeg_sample_format(SampleFormat::invalid),
			  fb_sample_fmt_none);
}

TEST(CommonFFmpegUtils, ConvertJPEGSpaceToRegularSpace)
{
	EXPECT_EQ(FFmpegUtils::convert_jpeg_space_to_regular_space(fb_pix_fmt_yuv_j420_p),
			  fb_pix_fmt_yu_v420_p);
	EXPECT_EQ(FFmpegUtils::convert_jpeg_space_to_regular_space(fb_pix_fmt_yuv_j422_p),
			  fb_pix_fmt_yu_v422_p);
	EXPECT_EQ(FFmpegUtils::convert_jpeg_space_to_regular_space(fb_pix_fmt_yuv_j444_p),
			  fb_pix_fmt_yu_v444_p);
	EXPECT_EQ(FFmpegUtils::convert_jpeg_space_to_regular_space(fb_pix_fmt_yuv_j440_p),
			  fb_pix_fmt_yu_v440_p);
	EXPECT_EQ(FFmpegUtils::convert_jpeg_space_to_regular_space(fb_pix_fmt_yuv_j411_p),
			  fb_pix_fmt_yu_v411_p);
	EXPECT_EQ(FFmpegUtils::convert_jpeg_space_to_regular_space(fb_pix_fmt_yu_v420_p),
			  fb_pix_fmt_yu_v420_p);
}

TEST(CommonFFmpegUtils, GetCompatiblePixelFormatNative)
{
	EXPECT_EQ(FFmpegUtils::get_compatible_pixel_format(PixelFormat::u8),
			  PixelFormat::u8);
	EXPECT_EQ(FFmpegUtils::get_compatible_pixel_format(PixelFormat::u10),
			  PixelFormat::u8);
	EXPECT_EQ(FFmpegUtils::get_compatible_pixel_format(PixelFormat::u16),
			  PixelFormat::u16);
	EXPECT_EQ(FFmpegUtils::get_compatible_pixel_format(PixelFormat::f16),
			  PixelFormat::u16);
	EXPECT_EQ(FFmpegUtils::get_compatible_pixel_format(PixelFormat::f32),
			  PixelFormat::u16);
	EXPECT_EQ(FFmpegUtils::get_compatible_pixel_format(PixelFormat::invalid),
			  PixelFormat::invalid);
}

TEST(CommonFFmpegUtils, GetFFmpegPixelFormat)
{
	EXPECT_EQ(FFmpegUtils::get_f_fmpeg_pixel_format(PixelFormat::u8,
												VideoParams::k_rgb_channel_count),
			  fb_pix_fmt_rg_b24);
	EXPECT_EQ(FFmpegUtils::get_f_fmpeg_pixel_format(PixelFormat::u16,
												VideoParams::k_rgb_channel_count),
			  fb_pix_fmt_rg_b48_le);
	EXPECT_EQ(FFmpegUtils::get_f_fmpeg_pixel_format(PixelFormat::f32,
												VideoParams::k_rgb_channel_count),
			  fb_pix_fmt_rgb_f32_le);
	EXPECT_EQ(FFmpegUtils::get_f_fmpeg_pixel_format(PixelFormat::u8,
												VideoParams::k_rgba_channel_count),
			  fb_pix_fmt_rgba);
	EXPECT_EQ(FFmpegUtils::get_f_fmpeg_pixel_format(PixelFormat::u16,
												VideoParams::k_rgba_channel_count),
			  fb_pix_fmt_rgb_a64_le);
	EXPECT_EQ(FFmpegUtils::get_f_fmpeg_pixel_format(PixelFormat::f32,
												VideoParams::k_rgba_channel_count),
			  fb_pix_fmt_rgba_f32_le);
	EXPECT_EQ(FFmpegUtils::get_f_fmpeg_pixel_format(PixelFormat::invalid, 0),
			  fb_pix_fmt_none);
}

TEST(CommonFFmpegUtils, GetCompatiblePixelFormatAV)
{
	int fmt = FFmpegUtils::get_compatible_bridge_pixel_format(fb_pix_fmt_yu_v420_p);
	EXPECT_NE(fmt, fb_pix_fmt_none);

	fmt = FFmpegUtils::get_compatible_bridge_pixel_format(fb_pix_fmt_yu_v420_p,
												PixelFormat::u8);
	EXPECT_EQ(fmt, fb_pix_fmt_rgba);
}
