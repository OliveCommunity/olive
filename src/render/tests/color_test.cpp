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

#include "render/color.h"

#include "render/cache.h" /* oakrender_debug_alive_count */

#include <cstdlib>
#include <vector>

#include <gtest/gtest.h>

// ctest injects OCIO=<repo>/engine/render/ocioconf/config.ocio (see
// tests/CMakeLists.txt), so the default config resolves to the bundled
// one with "Linear", "sRGB OETF" colorspaces and an "sRGB" display.

TEST(OakRenderColorTest, SetUpDefaultConfig)
{
	ASSERT_NE(std::getenv("OCIO"), nullptr)
		<< "OCIO env must be injected by ctest";
	EXPECT_EQ(oakrender_color_manager_set_up_default_config(), OAKRENDER_OK);
}

TEST(OakRenderColorTest, GetConfigTwoStage)
{
	const int required = oakrender_color_manager_get_config(nullptr, 0);
	ASSERT_GT(required, 1);

	std::vector<char> buf(static_cast<size_t>(required));
	EXPECT_EQ(oakrender_color_manager_get_config(buf.data(), required),
			  required);

	// With $OCIO set the getter reports it verbatim.
	const char *ocio_env = std::getenv("OCIO");
	ASSERT_NE(ocio_env, nullptr);
	EXPECT_STREQ(buf.data(), ocio_env);
}

TEST(OakRenderColorTest, ProcessorCreateConvertKnownValue)
{
	const int alive_before = oakrender_debug_alive_count();

	OakColorProcessor *p = oakrender_color_processor_create(
		"sRGB OETF", "Linear", OAKRENDER_COLOR_DIRECTION_NORMAL);
	ASSERT_NE(p, nullptr);
	ASSERT_EQ(oakrender_color_processor_is_valid(p), 1);

	// sRGB signal 0.5 -> linear 0.214041 (bundled sRGB_OETF_to_Linear
	// LUT), tolerance 1e-3 per M7 §4.
	double r = 0, g = 0, b = 0, a = 0;
	ASSERT_EQ(oakrender_color_processor_convert(p, 0.5, 0.25, 1.0, 0.75, &r,
												&g, &b, &a),
			  OAKRENDER_OK);
	EXPECT_NEAR(r, 0.214041, 1e-3);
	EXPECT_NEAR(g, 0.050875, 1e-3); // ((0.25+0.055)/1.055)^2.4
	EXPECT_NEAR(b, 1.0, 1e-3);
	EXPECT_NEAR(a, 0.75, 1e-9); // alpha passes through

	oakrender_color_processor_free(p);
	EXPECT_EQ(oakrender_debug_alive_count(), alive_before);
}

TEST(OakRenderColorTest, ProcessorCreateInvalidArgs)
{
	EXPECT_EQ(oakrender_color_processor_create(nullptr, "Linear",
											   OAKRENDER_COLOR_DIRECTION_NORMAL),
			  nullptr);
	EXPECT_EQ(oakrender_color_processor_create("sRGB OETF", nullptr,
											   OAKRENDER_COLOR_DIRECTION_NORMAL),
			  nullptr);
	EXPECT_EQ(oakrender_color_processor_create("", "Linear",
											   OAKRENDER_COLOR_DIRECTION_NORMAL),
			  nullptr);
	EXPECT_EQ(oakrender_color_processor_create("sRGB OETF", "Linear", 7),
			  nullptr);
}

TEST(OakRenderColorTest, ProcessorUnknownColorspaceIsPassThrough)
{
	// OCIO failures are non-fatal: the handle exists, reports invalid,
	// and conversions pass through (matching the C++ behavior).
	OakColorProcessor *p = oakrender_color_processor_create(
		"No Such Colorspace", "Linear", OAKRENDER_COLOR_DIRECTION_NORMAL);
	ASSERT_NE(p, nullptr);
	EXPECT_EQ(oakrender_color_processor_is_valid(p), 0);

	double r = 0, g = 0, b = 0, a = 0;
	EXPECT_EQ(oakrender_color_processor_convert(p, 0.1, 0.2, 0.3, 0.4, &r, &g,
												&b, &a),
			  OAKRENDER_OK);
	// (double -> float -> double round-trip through olive::Color)
	EXPECT_NEAR(r, 0.1, 1e-6);
	EXPECT_NEAR(g, 0.2, 1e-6);
	EXPECT_NEAR(b, 0.3, 1e-6);
	EXPECT_NEAR(a, 0.4, 1e-6);

	oakrender_color_processor_free(p);
}

TEST(OakRenderColorTest, ProcessorConvertInvalidArgs)
{
	double r, g, b, a;
	EXPECT_EQ(oakrender_color_processor_convert(nullptr, 0, 0, 0, 0, &r, &g,
												&b, &a),
			  OAKRENDER_E_INVALID);

	OakColorProcessor *p = oakrender_color_processor_create(
		"Linear", "Linear", OAKRENDER_COLOR_DIRECTION_NORMAL);
	ASSERT_NE(p, nullptr);
	EXPECT_EQ(oakrender_color_processor_convert(p, 0, 0, 0, 0, nullptr, &g,
												&b, &a),
			  OAKRENDER_E_INVALID);
	oakrender_color_processor_free(p);

	// NULL free is a no-op
	oakrender_color_processor_free(nullptr);
}

TEST(OakRenderColorTest, ProcessorInverseDirection)
{
	OakColorProcessor *p = oakrender_color_processor_create(
		"Linear", "sRGB OETF", OAKRENDER_COLOR_DIRECTION_NORMAL);
	ASSERT_NE(p, nullptr);
	ASSERT_EQ(oakrender_color_processor_is_valid(p), 1);

	// Linear 0.214041 -> sRGB ~0.5 (inverse of the forward sample)
	double r = 0, g = 0, b = 0, a = 0;
	ASSERT_EQ(oakrender_color_processor_convert(p, 0.214041, 0, 0, 1, &r, &g,
												&b, &a),
			  OAKRENDER_OK);
	EXPECT_NEAR(r, 0.5, 1e-3);
	oakrender_color_processor_free(p);
}

TEST(OakRenderColorTest, DisplayTransformKnownDisplay)
{
	const int required = oakrender_color_manager_display_transform(
		"sRGB", "sRGB OETF", nullptr, 0);
	ASSERT_GT(required, 1);

	std::vector<char> buf(static_cast<size_t>(required));
	EXPECT_EQ(oakrender_color_manager_display_transform("sRGB", "sRGB OETF",
														buf.data(), required),
			  required);
	EXPECT_GT(buf[0], '\0');

	// Deterministic for the same input
	std::vector<char> buf2(static_cast<size_t>(required));
	ASSERT_EQ(oakrender_color_manager_display_transform("sRGB", "sRGB OETF",
														buf2.data(), required),
			  required);
	EXPECT_STREQ(buf.data(), buf2.data());
}

TEST(OakRenderColorTest, DisplayTransformInvalidArgs)
{
	char buf[64];
	EXPECT_EQ(oakrender_color_manager_display_transform(nullptr, "sRGB OETF",
														buf, sizeof(buf)),
			  OAKRENDER_E_INVALID);
	EXPECT_EQ(oakrender_color_manager_display_transform("sRGB", nullptr, buf,
														sizeof(buf)),
			  OAKRENDER_E_INVALID);
	EXPECT_EQ(oakrender_color_manager_display_transform("No Such Display",
														"sRGB OETF", buf,
														sizeof(buf)),
			  OAKRENDER_E_NOT_FOUND);
	EXPECT_EQ(oakrender_color_manager_display_transform("sRGB",
														"No Such View", buf,
														sizeof(buf)),
			  OAKRENDER_E_NOT_FOUND);
}
