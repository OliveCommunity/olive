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

#include "common/oiioutils.h"

/**
 * @note The whole oakcommon oiioutils family links against OpenImageIO,
 * but every function exposed through the C API is pure logic (enum
 * mapping and double-to-rational conversion) that needs no image I/O,
 * codec or GPU at runtime, so no GTEST_SKIP() is required. The
 * OIIO::ImageBuf-based helpers (frame_to_buffer / buffer_to_frame) are
 * intentionally not part of the C API and are not tested here.
 *
 * OIIO TypeDesc::BASETYPE values used below: UNKNOWN = 0, UINT8 = 2,
 * UINT16 = 4, HALF = 10, FLOAT = 11, DOUBLE = 12 (LASTBASE is
 * version-dependent; 100 is safely out of range).
 */

TEST(OIIOUtilsCApi, InitReturnsHandle)
{
	OakCommonOIIOUtils *utils = oakcommon_oiioutils_init();
	ASSERT_NE(utils, nullptr);
	oakcommon_oiioutils_free(utils);
}

TEST(OIIOUtilsCApi, FreeNullIsNoOp)
{
	oakcommon_oiioutils_free(NULL);
}

TEST(OIIOUtilsCApi, BaseTypeFromPixelFormat)
{
	OakCommonOIIOUtils *utils = oakcommon_oiioutils_init();
	ASSERT_NE(utils, nullptr);

	const struct {
		int pixel_format;
		int expected_base_type;
	} cases[] = {
		{ OAKCOMMON_PIXEL_FORMAT_U8, 2 }, /* UINT8 */
		{ OAKCOMMON_PIXEL_FORMAT_U16, 4 }, /* UINT16 */
		{ OAKCOMMON_PIXEL_FORMAT_F16, 10 }, /* HALF */
		{ OAKCOMMON_PIXEL_FORMAT_F32, 11 }, /* FLOAT */
		{ OAKCOMMON_PIXEL_FORMAT_U10, 0 }, /* UNKNOWN: unmappable */
		{ OAKCOMMON_PIXEL_FORMAT_INVALID, 0 }, /* UNKNOWN */
	};

	for (const auto &c : cases) {
		int base_type = -1;
		EXPECT_EQ(oakcommon_oiioutils_get_oiio_base_type_from_format(
					  utils, c.pixel_format, &base_type),
				  OAKCOMMON_OK);
		EXPECT_EQ(base_type, c.expected_base_type);
	}

	oakcommon_oiioutils_free(utils);
}

TEST(OIIOUtilsCApi, BaseTypeFromPixelFormatRejectsBadArgs)
{
	OakCommonOIIOUtils *utils = oakcommon_oiioutils_init();
	ASSERT_NE(utils, nullptr);

	int base_type = 0;
	EXPECT_EQ(oakcommon_oiioutils_get_oiio_base_type_from_format(
				  NULL, OAKCOMMON_PIXEL_FORMAT_U8, &base_type),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_oiioutils_get_oiio_base_type_from_format(
				  utils, OAKCOMMON_PIXEL_FORMAT_U8, NULL),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_oiioutils_get_oiio_base_type_from_format(
				  utils, OAKCOMMON_PIXEL_FORMAT_COUNT, &base_type),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_oiioutils_get_oiio_base_type_from_format(
				  utils, -2, &base_type),
			  OAKCOMMON_E_INVALID);

	oakcommon_oiioutils_free(utils);
}

TEST(OIIOUtilsCApi, PixelFormatFromBaseType)
{
	OakCommonOIIOUtils *utils = oakcommon_oiioutils_init();
	ASSERT_NE(utils, nullptr);

	const struct {
		int base_type;
		int expected_pixel_format;
	} cases[] = {
		{ 2, OAKCOMMON_PIXEL_FORMAT_U8 }, /* UINT8 */
		{ 4, OAKCOMMON_PIXEL_FORMAT_U16 }, /* UINT16 */
		{ 10, OAKCOMMON_PIXEL_FORMAT_F16 }, /* HALF */
		{ 11, OAKCOMMON_PIXEL_FORMAT_F32 }, /* FLOAT */
		{ 0, OAKCOMMON_PIXEL_FORMAT_INVALID }, /* UNKNOWN */
		{ 12, OAKCOMMON_PIXEL_FORMAT_INVALID }, /* DOUBLE: unmappable */
		{ 13, OAKCOMMON_PIXEL_FORMAT_INVALID }, /* STRING: unmappable */
	};

	for (const auto &c : cases) {
		int pixel_format = -2;
		EXPECT_EQ(oakcommon_oiioutils_get_format_from_oiio_basetype(
					  utils, c.base_type, &pixel_format),
				  OAKCOMMON_OK);
		EXPECT_EQ(pixel_format, c.expected_pixel_format);
	}

	oakcommon_oiioutils_free(utils);
}

TEST(OIIOUtilsCApi, PixelFormatFromBaseTypeRejectsBadArgs)
{
	OakCommonOIIOUtils *utils = oakcommon_oiioutils_init();
	ASSERT_NE(utils, nullptr);

	int pixel_format = 0;
	EXPECT_EQ(oakcommon_oiioutils_get_format_from_oiio_basetype(
				  NULL, 2, &pixel_format),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_oiioutils_get_format_from_oiio_basetype(
				  utils, 2, NULL),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_oiioutils_get_format_from_oiio_basetype(
				  utils, -1, &pixel_format),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_oiioutils_get_format_from_oiio_basetype(
				  utils, 100, &pixel_format),
			  OAKCOMMON_E_INVALID); /* >= LASTBASE */

	oakcommon_oiioutils_free(utils);
}

TEST(OIIOUtilsCApi, PixelAspectRatioConvertsToRational)
{
	OakCommonOIIOUtils *utils = oakcommon_oiioutils_init();
	ASSERT_NE(utils, nullptr);

	int num = 0;
	int den = 0;
	ASSERT_EQ(oakcommon_oiioutils_get_pixel_aspect_ratio(utils, 1.0, &num,
														 &den),
			  OAKCOMMON_OK);
	EXPECT_EQ(num, 1);
	EXPECT_EQ(den, 1);

	ASSERT_EQ(oakcommon_oiioutils_get_pixel_aspect_ratio(utils, 1.5, &num,
														 &den),
			  OAKCOMMON_OK);
	ASSERT_NE(den, 0);
	EXPECT_NEAR(static_cast<double>(num) / den, 1.5, 1e-9);

	oakcommon_oiioutils_free(utils);
}

TEST(OIIOUtilsCApi, PixelAspectRatioRejectsBadArgs)
{
	OakCommonOIIOUtils *utils = oakcommon_oiioutils_init();
	ASSERT_NE(utils, nullptr);

	int num = 0;
	int den = 0;
	EXPECT_EQ(oakcommon_oiioutils_get_pixel_aspect_ratio(NULL, 1.0, &num,
														 &den),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_oiioutils_get_pixel_aspect_ratio(utils, 1.0, NULL,
														 &den),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_oiioutils_get_pixel_aspect_ratio(utils, 1.0, &num,
														 NULL),
			  OAKCOMMON_E_INVALID);

	oakcommon_oiioutils_free(utils);
}
