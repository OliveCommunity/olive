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

#include <OpenColorIO/OpenColorIO.h>
namespace ocio = OCIO_NAMESPACE;

#include "common/ocioutils.h"

/**
 * @note The whole oakcommon ocioutils family links against OpenColorIO;
 * if the test binary is built without OCIO these tests cannot run at
 * all. The mapping itself is pure logic and needs no OCIO config or GPU,
 * so no GTEST_SKIP() is required at runtime.
 */

TEST(OCIOUtilsCApi, InitReturnsHandle)
{
	OakCommonOCIOUtils *utils = oakcommon_ocioutils_init();
	ASSERT_NE(utils, nullptr);
	oakcommon_ocioutils_free(utils);
}

TEST(OCIOUtilsCApi, FreeNullIsNoOp)
{
	oakcommon_ocioutils_free(NULL);
}

TEST(OCIOUtilsCApi, BitDepthMappingMatchesOCIO)
{
	OakCommonOCIOUtils *utils = oakcommon_ocioutils_init();
	ASSERT_NE(utils, nullptr);

	const struct {
		int pixel_format;
		ocio::BitDepth expected;
	} cases[] = {
		{ OAKCOMMON_PIXEL_FORMAT_U8, ocio::BIT_DEPTH_UINT8 },
		{ OAKCOMMON_PIXEL_FORMAT_U10, ocio::BIT_DEPTH_UINT10 },
		{ OAKCOMMON_PIXEL_FORMAT_U16, ocio::BIT_DEPTH_UINT16 },
		{ OAKCOMMON_PIXEL_FORMAT_F16, ocio::BIT_DEPTH_F16 },
		{ OAKCOMMON_PIXEL_FORMAT_F32, ocio::BIT_DEPTH_F32 },
	};

	for (const auto &c : cases) {
		int depth = -1;
		EXPECT_EQ(oakcommon_ocioutils_get_ocio_bit_depth_from_pixel_format(
					  utils, c.pixel_format, &depth),
				  OAKCOMMON_OK);
		EXPECT_EQ(depth, static_cast<int>(c.expected));
	}

	oakcommon_ocioutils_free(utils);
}

TEST(OCIOUtilsCApi, InvalidFormatYieldsUnknownDepth)
{
	OakCommonOCIOUtils *utils = oakcommon_ocioutils_init();
	ASSERT_NE(utils, nullptr);

	int depth = -1;
	EXPECT_EQ(oakcommon_ocioutils_get_ocio_bit_depth_from_pixel_format(
				  utils, OAKCOMMON_PIXEL_FORMAT_INVALID, &depth),
			  OAKCOMMON_OK);
	EXPECT_EQ(depth, static_cast<int>(ocio::BIT_DEPTH_UNKNOWN));

	oakcommon_ocioutils_free(utils);
}

TEST(OCIOUtilsCApi, NullHandleReturnsInvalid)
{
	int depth = 0;
	EXPECT_EQ(oakcommon_ocioutils_get_ocio_bit_depth_from_pixel_format(
				  NULL, OAKCOMMON_PIXEL_FORMAT_U8, &depth),
			  OAKCOMMON_E_INVALID);
}

TEST(OCIOUtilsCApi, NullOutParamReturnsInvalid)
{
	OakCommonOCIOUtils *utils = oakcommon_ocioutils_init();
	ASSERT_NE(utils, nullptr);

	EXPECT_EQ(oakcommon_ocioutils_get_ocio_bit_depth_from_pixel_format(
				  utils, OAKCOMMON_PIXEL_FORMAT_U8, NULL),
			  OAKCOMMON_E_INVALID);

	oakcommon_ocioutils_free(utils);
}

TEST(OCIOUtilsCApi, OutOfRangeFormatReturnsInvalid)
{
	OakCommonOCIOUtils *utils = oakcommon_ocioutils_init();
	ASSERT_NE(utils, nullptr);

	int depth = 0;
	EXPECT_EQ(oakcommon_ocioutils_get_ocio_bit_depth_from_pixel_format(
				  utils, OAKCOMMON_PIXEL_FORMAT_COUNT, &depth),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_ocioutils_get_ocio_bit_depth_from_pixel_format(
				  utils, -2, &depth),
			  OAKCOMMON_E_INVALID);

	oakcommon_ocioutils_free(utils);
}
