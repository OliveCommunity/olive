#include <gtest/gtest.h>

#include "render/plugin/pluginrenderer.h"

TEST(PluginRendererReadback, BytesToPixels)
{
	olive::VideoParams params(16, 16, olive::core::PixelFormat::u8, 4,
							  olive::core::Rational(1, 1),
							  olive::VideoParams::k_interlace_none, 1);

	const int bytes_per_pixel = olive::VideoParams::get_bytes_per_pixel(
		params.format(), params.channel_count());
	ASSERT_EQ(bytes_per_pixel, 4);

	EXPECT_EQ(olive::plugin::detail::bytes_to_pixels(64, params), 16);
	EXPECT_EQ(olive::plugin::detail::bytes_to_pixels(0, params), 0);
	EXPECT_EQ(olive::plugin::detail::bytes_to_pixels(-1, params), 0);
}
