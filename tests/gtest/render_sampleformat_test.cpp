#include <gtest/gtest.h>

#include "olive/core/render/sampleformat.h"

TEST(RenderSampleFormat, ByteCountAndStringRoundTrip)
{
	using olive::core::SampleFormat;

	EXPECT_EQ(SampleFormat::byte_count(SampleFormat::invalid), 0);
	EXPECT_EQ(SampleFormat::byte_count(SampleFormat::u8), 1);
	EXPECT_EQ(SampleFormat::byte_count(SampleFormat::s16), 2);
	EXPECT_EQ(SampleFormat::byte_count(SampleFormat::f32), 4);
	EXPECT_EQ(SampleFormat::byte_count(SampleFormat::f64), 8);

	EXPECT_EQ(SampleFormat::to_string(SampleFormat::s16), "s16");
	EXPECT_EQ(SampleFormat::from_string("s16"), SampleFormat::s16);
	EXPECT_EQ(SampleFormat::from_string(""), SampleFormat::invalid);
	EXPECT_EQ(SampleFormat::from_string("unknown"), SampleFormat::invalid);
}

TEST(RenderSampleFormat, PackedAndPlanarChecks)
{
	using olive::core::SampleFormat;

	EXPECT_TRUE(SampleFormat::is_packed(SampleFormat::s16));
	EXPECT_FALSE(SampleFormat::is_packed(SampleFormat::s16_p));
	EXPECT_TRUE(SampleFormat::is_planar(SampleFormat::s16_p));
	EXPECT_FALSE(SampleFormat::is_planar(SampleFormat::s16));
}
