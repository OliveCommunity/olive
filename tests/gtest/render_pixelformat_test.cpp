#include <gtest/gtest.h>

#include "olive/core/render/pixelformat.h"

TEST(RenderPixelFormat, ByteCountAndString)
{
	using olive::core::PixelFormat;

	EXPECT_EQ(PixelFormat::byte_count(PixelFormat::invalid), 0);
	EXPECT_EQ(PixelFormat::byte_count(PixelFormat::u8), 1);
	EXPECT_EQ(PixelFormat::byte_count(PixelFormat::u16), 2);
	EXPECT_EQ(PixelFormat::byte_count(PixelFormat::f16), 2);
	EXPECT_EQ(PixelFormat::byte_count(PixelFormat::f32), 4);

	EXPECT_EQ(PixelFormat(PixelFormat::u8).to_string(), std::string("u8"));
	EXPECT_EQ(PixelFormat(PixelFormat::invalid).to_string(), std::string(""));
}

TEST(RenderPixelFormat, FloatChecks)
{
	using olive::core::PixelFormat;

	EXPECT_FALSE(PixelFormat::is_float(PixelFormat::u8));
	EXPECT_FALSE(PixelFormat::is_float(PixelFormat::u16));
	EXPECT_TRUE(PixelFormat::is_float(PixelFormat::f16));
	EXPECT_TRUE(PixelFormat::is_float(PixelFormat::f32));
	EXPECT_FALSE(PixelFormat::is_float(PixelFormat::invalid));
}
