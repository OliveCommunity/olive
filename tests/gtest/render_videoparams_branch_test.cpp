#include <gtest/gtest.h>

#include <cstdint>

#include <QBuffer>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "render/videoparams.h"

TEST(RenderVideoParams, BytesPerChannelAndPixel)
{
	EXPECT_EQ(olive::VideoParams::get_bytes_per_channel(
				  olive::core::PixelFormat::invalid),
			  0);
	EXPECT_EQ(
		olive::VideoParams::get_bytes_per_channel(olive::core::PixelFormat::u8),
		1);
	EXPECT_EQ(
		olive::VideoParams::get_bytes_per_channel(olive::core::PixelFormat::u16),
		2);
	EXPECT_EQ(
		olive::VideoParams::get_bytes_per_channel(olive::core::PixelFormat::f16),
		2);
	EXPECT_EQ(
		olive::VideoParams::get_bytes_per_channel(olive::core::PixelFormat::f32),
		4);
	EXPECT_EQ(olive::VideoParams::get_bytes_per_pixel(olive::core::PixelFormat::u8,
												   4),
			  4);
}

TEST(RenderVideoParams, DividerAndFormatNames)
{
	EXPECT_EQ(olive::VideoParams::get_name_for_divider(1), QStringLiteral("Full"));
	EXPECT_EQ(olive::VideoParams::get_name_for_divider(3), QStringLiteral("1/3"));

	const QString unknown =
		olive::VideoParams::get_format_name(olive::core::PixelFormat::invalid);
	EXPECT_TRUE(unknown.contains(QStringLiteral("Unknown")));
}

TEST(RenderVideoParams, ScalingAndDividerForTarget)
{
	EXPECT_EQ(olive::VideoParams::get_scaled_dimension(100, 3), 33);
	EXPECT_EQ(olive::VideoParams::get_divider_for_target_resolution(1920, 1080, 960,
																540),
			  2);
	EXPECT_EQ(olive::VideoParams::get_divider_for_target_resolution(1920, 1080, 480,
																270),
			  4);
}

TEST(RenderVideoParams, FrameRateStringsAndPixelAspect)
{
	const QString fps =
		olive::VideoParams::frame_rate_to_string(olive::core::Rational(24, 1));
	EXPECT_TRUE(fps.contains(QStringLiteral("24")));
	EXPECT_TRUE(fps.contains(QStringLiteral("FPS")));

	const QStringList names =
		olive::VideoParams::get_standard_pixel_aspect_ratio_names();
	ASSERT_EQ(names.size(), 6);
	EXPECT_TRUE(names.at(0).contains(QStringLiteral("1.0000")));
}

TEST(RenderVideoParams, AutoDividerAndPixelAspect)
{
	EXPECT_EQ(olive::VideoParams::generate_auto_divider(640, 480), 1);
	EXPECT_EQ(olive::VideoParams::generate_auto_divider(7680, 4320), 6);
	EXPECT_EQ(olive::VideoParams::generate_auto_divider(50000, 50000), 16);

	olive::VideoParams params(100, 50, olive::core::PixelFormat::u8, 4);
	params.set_pixel_aspect_ratio(olive::core::Rational(0, 1));
	EXPECT_EQ(params.pixel_aspect_ratio(), olive::core::Rational(1, 1));
	EXPECT_EQ(params.square_pixel_width(), 100);

	params.set_pixel_aspect_ratio(olive::core::Rational(2, 1));
	EXPECT_EQ(params.square_pixel_width(), 200);
}

TEST(RenderVideoParams, ValidityAndTimebase)
{
	olive::VideoParams params;
	EXPECT_FALSE(params.is_valid());
	EXPECT_EQ(params.get_time_in_timebase_units(olive::core::Rational(1, 1)),
			  INT64_MIN /* AV_NOPTS_VALUE */);

	params.set_width(1920);
	params.set_height(1080);
	params.set_format(olive::core::PixelFormat::u8);
	params.set_channel_count(4);
	params.set_pixel_aspect_ratio(olive::core::Rational(1, 1));
	params.set_time_base(olive::core::Rational(1, 1));
	params.set_start_time(10);
	EXPECT_TRUE(params.is_valid());
	EXPECT_EQ(params.get_time_in_timebase_units(olive::core::Rational(2, 1)),
			  12);
}

TEST(RenderVideoParams, CopyConstructorPreservesValues)
{
	olive::VideoParams params(1920, 1080, olive::core::Rational(24, 1),
							  olive::core::PixelFormat::f32, 4);
	params.set_colorspace(QStringLiteral("ACEScg"));

	olive::VideoParams copy(params);
	EXPECT_EQ(copy.width(), 1920);
	EXPECT_EQ(copy.height(), 1080);
	EXPECT_EQ(copy.format(), olive::core::PixelFormat::f32);
	EXPECT_EQ(copy.channel_count(), 4);
	EXPECT_EQ(copy.colorspace(), QStringLiteral("ACEScg"));
}

TEST(RenderVideoParams, AssignmentPreservesValues)
{
	olive::VideoParams params(1280, 720, olive::core::Rational(30, 1),
							  olive::core::PixelFormat::u16, 4);
	olive::VideoParams copy;
	copy = params;
	EXPECT_EQ(copy.width(), 1280);
	EXPECT_EQ(copy.height(), 720);
	EXPECT_EQ(copy.format(), olive::core::PixelFormat::u16);
}

TEST(RenderVideoParams, EqualityComparesDimensionsAndFormat)
{
	olive::VideoParams a(1920, 1080, olive::core::PixelFormat::u8, 4);
	olive::VideoParams b(1920, 1080, olive::core::PixelFormat::u8, 4);
	olive::VideoParams c(1280, 720, olive::core::PixelFormat::u8, 4);
	olive::VideoParams d(1920, 1080, olive::core::PixelFormat::f32, 4);

	EXPECT_EQ(a, b);
	EXPECT_NE(a, c);
	EXPECT_NE(a, d);
}

TEST(RenderVideoParams, SaveLoadRoundTripExtended)
{
	olive::VideoParams params(1920, 1080, olive::core::Rational(1, 24),
							  olive::core::PixelFormat::u16, 4);
	params.set_depth(2);
	params.set_pixel_aspect_ratio(olive::core::Rational(4, 3));
	params.set_interlacing(olive::VideoParams::k_interlaced_top_first);
	params.set_divider(2);
	params.set_enabled(false);
	params.set_x(1.5f);
	params.set_y(-2.25f);
	params.set_stream_index(7);
	params.set_video_type(olive::VideoParams::k_video_type_image_sequence);
	params.set_frame_rate(olive::core::Rational(30000, 1001));
	params.set_start_time(123);
	params.set_duration(456);
	params.set_premultiplied_alpha(true);
	params.set_colorspace(QStringLiteral("Rec.709"));
	params.set_color_range(olive::VideoParams::k_color_range_full);

	QByteArray xml;
	QBuffer buffer(&xml);
	buffer.open(QIODevice::WriteOnly);
	QXmlStreamWriter writer(&buffer);
	writer.writeStartDocument();
	writer.writeStartElement(QStringLiteral("videoparams"));
	params.save(&writer);
	writer.writeEndElement();
	writer.writeEndDocument();
	buffer.close();

	olive::VideoParams loaded;
	QBuffer read_buffer(&xml);
	read_buffer.open(QIODevice::ReadOnly);
	QXmlStreamReader reader(&read_buffer);
	ASSERT_TRUE(reader.readNextStartElement());
	EXPECT_EQ(reader.name().toString(), QStringLiteral("videoparams"));
	loaded.load(&reader);

	EXPECT_EQ(loaded.width(), 1920);
	EXPECT_EQ(loaded.height(), 1080);
	EXPECT_EQ(loaded.depth(), 2);
	EXPECT_EQ(loaded.time_base(), olive::core::Rational(1, 24));
	EXPECT_EQ(loaded.format(), olive::core::PixelFormat::u16);
	EXPECT_EQ(loaded.channel_count(), 4);
	EXPECT_EQ(loaded.pixel_aspect_ratio(), olive::core::Rational(4, 3));
	EXPECT_EQ(loaded.interlacing(), olive::VideoParams::k_interlaced_top_first);
	EXPECT_EQ(loaded.divider(), 2);
	EXPECT_EQ(loaded.enabled(), false);
	EXPECT_FLOAT_EQ(loaded.x(), 1.5f);
	EXPECT_FLOAT_EQ(loaded.y(), -2.25f);
	EXPECT_EQ(loaded.stream_index(), 7);
	EXPECT_EQ(loaded.video_type(), olive::VideoParams::k_video_type_image_sequence);
	EXPECT_EQ(loaded.frame_rate(), olive::core::Rational(30000, 1001));
	EXPECT_EQ(loaded.start_time(), 123);
	EXPECT_EQ(loaded.duration(), 456);
	EXPECT_TRUE(loaded.premultiplied_alpha());
	EXPECT_EQ(loaded.colorspace(), QStringLiteral("Rec.709"));
	EXPECT_EQ(loaded.color_range(), olive::VideoParams::k_color_range_full);
}
