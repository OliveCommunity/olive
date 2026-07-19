/*
 * OFX Plugin Format Conversion Tests
 */

#include <gtest/gtest.h>
#include <QtGlobal>
#include <QDir>
#include <QDebug>
#include <QFileInfo>

#include "codec/decoder.h"
#include "common/ffmpegutils.h"
#include "render/videoparams.h"
#include "render/texture.h"

using namespace olive;
using namespace olive::core;

// Test helper to create AVFrame with specific format
static AVFramePtr create_test_frame(int width, int height, int fmt,
								  uint32_t fill_color = 0xFF804020)
{
	AVFramePtr frame = create_av_frame_ptr();
	frame->set_width(width);
	frame->set_height(height);
	frame->set_format(fmt);

	if (frame->get_buffer(0) < 0) {
		return nullptr;
	}

	if (frame->make_writable() < 0) {
		return nullptr;
	}

	// Fill with test pattern
	uint8_t r = (fill_color >> 24) & 0xFF;
	uint8_t g = (fill_color >> 16) & 0xFF;
	uint8_t b = (fill_color >> 8) & 0xFF;
	uint8_t a = fill_color & 0xFF;

	if (fmt == fb_pix_fmt_rgba) {
		for (int y = 0; y < height; ++y) {
			uint8_t *row = frame->data(0) + y * frame->linesize(0);
			for (int x = 0; x < width; ++x) {
				row[x * 4 + 0] = r;
				row[x * 4 + 1] = g;
				row[x * 4 + 2] = b;
				row[x * 4 + 3] = a;
			}
		}
	} else if (fmt == fb_pix_fmt_rgb_a64_le) {
		uint16_t r16 = (r << 8) | r;
		uint16_t g16 = (g << 8) | g;
		uint16_t b16 = (b << 8) | b;
		uint16_t a16 = (a << 8) | a;
		for (int y = 0; y < height; ++y) {
			uint16_t *row = reinterpret_cast<uint16_t *>(frame->data(0) +
														 y * frame->linesize(0));
			for (int x = 0; x < width; ++x) {
				row[x * 4 + 0] = r16;
				row[x * 4 + 1] = g16;
				row[x * 4 + 2] = b16;
				row[x * 4 + 3] = a16;
			}
		}
	}

	return frame;
}

// Test U8 to U16 conversion through the bridge scaler
TEST(FormatConversion, U8ToU16)
{
	const int width = 10;
	const int height = 10;
	const uint32_t test_color = 0xFF804020; // R=255, G=128, B=64, A=32

	// Create U8 source frame
	AVFramePtr u8_frame =
		create_test_frame(width, height, fb_pix_fmt_rgba, test_color);
	ASSERT_NE(u8_frame, nullptr);

	// Create U16 destination frame
	AVFramePtr u16_frame =
		create_test_frame(width, height, fb_pix_fmt_rgb_a64_le, 0);
	ASSERT_NE(u16_frame, nullptr);

	// Use the bridge scaler to convert
	FBScaler *sws_ctx = fb_scaler_create(width, height, fb_pix_fmt_rgba, width,
										 height, fb_pix_fmt_rgb_a64_le,
										 FB_SCALER_POINT);
	ASSERT_NE(sws_ctx, nullptr);

	uint8_t *src_data[4];
	int src_linesize[4];
	uint8_t *dst_data[4];
	int dst_linesize[4];
	for (int i = 0; i < 4; ++i) {
		src_data[i] = u8_frame->data(i);
		src_linesize[i] = u8_frame->linesize(i);
		dst_data[i] = u16_frame->data(i);
		dst_linesize[i] = u16_frame->linesize(i);
	}

	fb_scaler_scale_slices(sws_ctx, src_data, src_linesize, height, dst_data,
						   dst_linesize);
	fb_scaler_free(&sws_ctx);

	// Verify conversion: each 16-bit channel should hold the 8-bit value
	// scaled up (v * 257, i.e. (v << 8) | v); allow two 8-bit LSBs of
	// rounding like the U16 -> U8 test below.
	const uint16_t *first_pixel =
		reinterpret_cast<const uint16_t *>(u16_frame->data(0));
	EXPECT_NEAR(first_pixel[0], 0xFFFF, 512); // R
	EXPECT_NEAR(first_pixel[1], 0x8080, 512); // G
	EXPECT_NEAR(first_pixel[2], 0x4040, 512); // B
	EXPECT_NEAR(first_pixel[3], 0x2020, 512); // A
}

// Test FFmpeg sws_scale for U16 to U8 conversion
TEST(FormatConversion, FFmpegU16ToU8)
{
	const int width = 10;
	const int height = 10;
	const uint32_t test_color = 0xFF804020;

	// Create U16 frame
	AVFramePtr u16_frame =
		create_test_frame(width, height, fb_pix_fmt_rgb_a64_le, test_color);
	ASSERT_NE(u16_frame, nullptr);

	// Create destination U8 frame
	AVFramePtr u8_frame = create_test_frame(width, height, fb_pix_fmt_rgba, 0);
	ASSERT_NE(u8_frame, nullptr);

	// Use the bridge scaler to convert
	FBScaler *sws_ctx = fb_scaler_create(width, height, fb_pix_fmt_rgb_a64_le,
										 width, height, fb_pix_fmt_rgba,
										 FB_SCALER_POINT);
	ASSERT_NE(sws_ctx, nullptr);

	uint8_t *src_data[4];
	int src_linesize[4];
	uint8_t *dst_data[4];
	int dst_linesize[4];
	for (int i = 0; i < 4; ++i) {
		src_data[i] = u16_frame->data(i);
		src_linesize[i] = u16_frame->linesize(i);
		dst_data[i] = u8_frame->data(i);
		dst_linesize[i] = u8_frame->linesize(i);
	}

	fb_scaler_scale_slices(sws_ctx, src_data, src_linesize, height, dst_data,
						   dst_linesize);
	fb_scaler_free(&sws_ctx);

	// Verify conversion (U16 0xFFFF -> U8 0xFF, 0x8080 -> ~0x80, etc.)
	// Note: FFmpeg sws_scale has rounding offset, so values may be off by 1
	uint8_t *first_pixel = u8_frame->data(0);
	EXPECT_NEAR(first_pixel[0], 0xFF, 1); // R (255 vs 255)
	EXPECT_NEAR(first_pixel[1], 0x80, 1); // G (128 vs 129)
	EXPECT_NEAR(first_pixel[2], 0x40, 1); // B (64 vs 64)
	EXPECT_NEAR(first_pixel[3], 0x20, 1); // A (32 vs 32)
}

// Test VideoParams to bridge pixel format mapping
TEST(FormatConversion, VideoParamsToAVFormat)
{
	// U8 RGBA
	VideoParams u8_rgba(320, 240, PixelFormat::u8, 4);
	int fmt_u8_rgba = FFmpegUtils::get_f_fmpeg_pixel_format(
		u8_rgba.format(), u8_rgba.channel_count());
	EXPECT_EQ(fmt_u8_rgba, fb_pix_fmt_rgba);

	// U16 RGBA
	VideoParams u16_rgba(320, 240, PixelFormat::u16, 4);
	int fmt_u16_rgba = FFmpegUtils::get_f_fmpeg_pixel_format(
		u16_rgba.format(), u16_rgba.channel_count());
	EXPECT_EQ(fmt_u16_rgba, fb_pix_fmt_rgb_a64_le);

	// U8 RGB
	VideoParams u8_rgb(320, 240, PixelFormat::u8, 3);
	int fmt_u8_rgb = FFmpegUtils::get_f_fmpeg_pixel_format(
		u8_rgb.format(), u8_rgb.channel_count());
	EXPECT_EQ(fmt_u8_rgb, fb_pix_fmt_rg_b24);

	// U16 RGB
	VideoParams u16_rgb(320, 240, PixelFormat::u16, 3);
	int fmt_u16_rgb = FFmpegUtils::get_f_fmpeg_pixel_format(
		u16_rgb.format(), u16_rgb.channel_count());
	EXPECT_EQ(fmt_u16_rgb, fb_pix_fmt_rg_b48_le);
}

// Test row bytes calculation via VideoParams::GetBytesPerPixel
TEST(FormatConversion, RowBytes)
{
	const int width = 320;

	// U8 RGBA: 4 bytes per pixel
	EXPECT_EQ(width * VideoParams::get_bytes_per_pixel(PixelFormat::u8, 4), 1280);

	// U16 RGBA: 8 bytes per pixel
	EXPECT_EQ(width * VideoParams::get_bytes_per_pixel(PixelFormat::u16, 4), 2560);

	// U8 RGB: 3 bytes per pixel
	EXPECT_EQ(width * VideoParams::get_bytes_per_pixel(PixelFormat::u8, 3), 960);

	// U16 RGB: 6 bytes per pixel
	EXPECT_EQ(width * VideoParams::get_bytes_per_pixel(PixelFormat::u16, 3), 1920);
}

// Test that linesize may differ from width * bpp due to alignment
TEST(FormatConversion, LinesizeAlignment)
{
	const int width = 10;
	const int height = 10;

	AVFramePtr frame = create_av_frame_ptr();
	frame->set_width(width);
	frame->set_height(height);
	frame->set_format(fb_pix_fmt_rgba);

	ASSERT_EQ(frame->get_buffer(0), 0);

	// linesize[0] should be at least width * 4
	EXPECT_GE(frame->linesize(0), width * 4);

	// linesize may be larger due to alignment (typically 32-byte aligned)
	qDebug() << "Width:" << width << "Expected bytes:" << width * 4
			 << "Actual linesize:" << frame->linesize(0);
}

// Test loading actual image file
TEST(FormatConversion, LoadImageFile)
{
	const QString img_path = QDir(QStringLiteral(OAK_TEST_SOURCE_DIR))
								 .filePath(QStringLiteral("tests/img.png"));
	ASSERT_TRUE(QFileInfo::exists(img_path));

	DecoderPtr decoder = Decoder::create_from_id(QStringLiteral("oiio"));
	ASSERT_TRUE(decoder);
	ASSERT_TRUE(decoder->open(Decoder::CodecStream(img_path, 0, nullptr)));

	Decoder::RetrieveVideoParams params;
	params.time = Rational(0);
	params.divider = 1;

	FramePtr frame = decoder->retrieve_video_frame(params);
	decoder->close();

	ASSERT_TRUE(frame);
	ASSERT_TRUE(frame->is_allocated());
	EXPECT_EQ(frame->width(), 1920);
	EXPECT_EQ(frame->height(), 1080);

	// Still images are decoded to F32 RGBA (channel values scaled by 1/255)
	EXPECT_EQ(frame->format(), PixelFormat::f32);
	EXPECT_EQ(frame->channel_count(), 4);

	// Spot-check decoded pixels against the known PNG content
	const float eps = 1.0f / 255.0f + 0.001f;

	const Color top_left = frame->get_pixel(0, 0);
	EXPECT_NEAR(top_left.red(), 75.0f / 255.0f, eps);
	EXPECT_NEAR(top_left.green(), 124.0f / 255.0f, eps);
	EXPECT_NEAR(top_left.blue(), 127.0f / 255.0f, eps);
	EXPECT_NEAR(top_left.alpha(), 1.0f, eps);

	const Color center = frame->get_pixel(960, 540);
	EXPECT_NEAR(center.red(), 131.0f / 255.0f, eps);
	EXPECT_NEAR(center.green(), 108.0f / 255.0f, eps);
	EXPECT_NEAR(center.blue(), 111.0f / 255.0f, eps);
	EXPECT_NEAR(center.alpha(), 1.0f, eps);
}

// Tests are registered with gtest, no main needed
