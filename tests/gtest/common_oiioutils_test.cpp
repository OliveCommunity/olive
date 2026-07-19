#include <gtest/gtest.h>

#include <OpenImageIO/imagebuf.h>

#include <cstring>

#include "codec/frame.h"
#include "common/oiioutils.h"

TEST(CommonOIIOUtils, BaseTypeFromPixelFormat)
{
	using olive::core::PixelFormat;

	EXPECT_EQ(olive::OIIOUtils::get_oiio_base_type_from_format(PixelFormat::u8),
			  OIIO::TypeDesc::UINT8);
	EXPECT_EQ(olive::OIIOUtils::get_oiio_base_type_from_format(PixelFormat::u16),
			  OIIO::TypeDesc::UINT16);
	EXPECT_EQ(olive::OIIOUtils::get_oiio_base_type_from_format(PixelFormat::f16),
			  OIIO::TypeDesc::HALF);
	EXPECT_EQ(olive::OIIOUtils::get_oiio_base_type_from_format(PixelFormat::f32),
			  OIIO::TypeDesc::FLOAT);
	EXPECT_EQ(olive::OIIOUtils::get_oiio_base_type_from_format(PixelFormat::u10),
			  OIIO::TypeDesc::UNKNOWN);
	EXPECT_EQ(olive::OIIOUtils::get_oiio_base_type_from_format(PixelFormat::invalid),
			  OIIO::TypeDesc::UNKNOWN);
}

TEST(CommonOIIOUtils, PixelFormatFromBaseType)
{
	using olive::core::PixelFormat;

	EXPECT_EQ(olive::OIIOUtils::get_format_from_oiio_basetype(OIIO::TypeDesc::UINT8),
			  PixelFormat::u8);
	EXPECT_EQ(
			olive::OIIOUtils::get_format_from_oiio_basetype(OIIO::TypeDesc::UINT16),
			PixelFormat::u16);
	EXPECT_EQ(olive::OIIOUtils::get_format_from_oiio_basetype(OIIO::TypeDesc::HALF),
			  PixelFormat::f16);
	EXPECT_EQ(olive::OIIOUtils::get_format_from_oiio_basetype(OIIO::TypeDesc::FLOAT),
			  PixelFormat::f32);
	EXPECT_EQ(
			olive::OIIOUtils::get_format_from_oiio_basetype(OIIO::TypeDesc::UNKNOWN),
			PixelFormat::invalid);
	EXPECT_EQ(olive::OIIOUtils::get_format_from_oiio_basetype(OIIO::TypeDesc::DOUBLE),
			  PixelFormat::invalid);
	EXPECT_EQ(olive::OIIOUtils::get_format_from_oiio_basetype(OIIO::TypeDesc::STRING),
			  PixelFormat::invalid);
}

TEST(CommonOIIOUtils, FormatRoundTripIsSymmetric)
{
	using olive::core::PixelFormat;

	for (PixelFormat fmt :
		 { PixelFormat::u8, PixelFormat::u16, PixelFormat::f16,
		   PixelFormat::f32 }) {
		EXPECT_EQ(olive::OIIOUtils::get_format_from_oiio_basetype(
					  olive::OIIOUtils::get_oiio_base_type_from_format(fmt)),
				  fmt);
	}
}

TEST(CommonOIIOUtils, PixelAspectRatioDefaultsToOne)
{
	OIIO::ImageSpec spec(16, 16, 4, OIIO::TypeDesc::FLOAT);

	const olive::core::Rational par =
		olive::OIIOUtils::get_pixel_aspect_ratio_from_oiio(spec);

	EXPECT_EQ(par, olive::core::Rational(1, 1));
}

TEST(CommonOIIOUtils, PixelAspectRatioIsReadFromAttribute)
{
	OIIO::ImageSpec spec(16, 16, 4, OIIO::TypeDesc::FLOAT);
	spec.attribute("PixelAspectRatio", 2.0f);

	const olive::core::Rational par =
		olive::OIIOUtils::get_pixel_aspect_ratio_from_oiio(spec);

	EXPECT_EQ(par, olive::core::Rational(2, 1));
}

TEST(CommonOIIOUtils, FrameBufferRoundTripPreservesPixels)
{
	using olive::core::PixelFormat;

	const olive::VideoParams params(8, 8, PixelFormat::f32,
									olive::VideoParams::k_rgba_channel_count);

	auto frame = olive::Frame::create();
	frame->set_video_params(params);
	frame->allocate();

	frame->set_pixel(0, 0, olive::Color(0.1f, 0.2f, 0.3f, 1.0f));
	frame->set_pixel(7, 7, olive::Color(0.9f, 0.8f, 0.7f, 0.6f));

	OIIO::ImageSpec spec(params.effective_width(), params.effective_height(),
						 params.channel_count(),
						 olive::OIIOUtils::get_oiio_base_type_from_format(
							 params.format()));
	OIIO::ImageBuf buf(spec);
	ASSERT_TRUE(buf.initialized());

	olive::OIIOUtils::frame_to_buffer(frame.get(), &buf);

	auto out = olive::Frame::create();
	out->set_video_params(params);
	out->allocate();
	// Poison the output so a failed transfer is visible.
	std::memset(out->data(), 0xFF, out->allocated_size());

	olive::OIIOUtils::buffer_to_frame(&buf, out.get());

	const olive::Color a = out->get_pixel(0, 0);
	EXPECT_NEAR(a.red(), 0.1f, 1e-5f);
	EXPECT_NEAR(a.green(), 0.2f, 1e-5f);
	EXPECT_NEAR(a.blue(), 0.3f, 1e-5f);
	EXPECT_NEAR(a.alpha(), 1.0f, 1e-5f);

	const olive::Color b = out->get_pixel(7, 7);
	EXPECT_NEAR(b.red(), 0.9f, 1e-5f);
	EXPECT_NEAR(b.green(), 0.8f, 1e-5f);
	EXPECT_NEAR(b.blue(), 0.7f, 1e-5f);
	EXPECT_NEAR(b.alpha(), 0.6f, 1e-5f);
}
