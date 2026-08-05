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

#ifndef OAK_EDITOR_OCIOUTILS_H
#define OAK_EDITOR_OCIOUTILS_H

#include "common/error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Native pixel format codes, mirroring olive::core::PixelFormat
 *
 * The numeric values must stay in sync with
 * olive/core/render/pixelformat.h (Format enum).
 */
enum OakCommonPixelFormat {
	OAKCOMMON_PIXEL_FORMAT_INVALID = -1, /**< Invalid/unknown format. */
	OAKCOMMON_PIXEL_FORMAT_U8 = 0, /**< 8-bit unsigned integer. */
	OAKCOMMON_PIXEL_FORMAT_U10 = 1, /**< 10-bit unsigned integer. */
	OAKCOMMON_PIXEL_FORMAT_U16 = 2, /**< 16-bit unsigned integer. */
	OAKCOMMON_PIXEL_FORMAT_F16 = 3, /**< 16-bit float (half). */
	OAKCOMMON_PIXEL_FORMAT_F32 = 4, /**< 32-bit float. */
	OAKCOMMON_PIXEL_FORMAT_COUNT = 5 /**< Sentinel, not a valid format. */
};

/**
 * @brief OpenColorIO bit depth codes, matching OCIO::BitDepth
 *
 * Returned through the out parameter of
 * oakcommon_ocioutils_get_ocio_bit_depth_from_pixel_format() as a plain
 * int so that callers never see OCIO types. Values match the OCIO
 * BitDepth enum: 0 = unknown, 1 = uint8, 2 = uint10, 3 = uint12,
 * 4 = uint14, 5 = uint16, 6 = uint32, 7 = f16, 8 = f32 (OCIO v2).
 */
typedef struct OakCommonOCIOUtils OakCommonOCIOUtils;

/**
 * @brief Creates an OCIOUtils handle
 *
 * The object is stateless; the handle exists only to satisfy the C API
 * lifetime contract. Returns NULL on failure.
 */
OakCommonOCIOUtils *oakcommon_ocioutils_init(void);

/**
 * @brief Destroys an OCIOUtils handle; no-op on NULL
 */
void oakcommon_ocioutils_free(OakCommonOCIOUtils *self);

/**
 * @brief Maps a native pixel format to an OCIO bit depth
 *
 * @param self handle from oakcommon_ocioutils_init()
 * @param pixel_format one of the OakCommonPixelFormat values
 * @param out_bit_depth receives the OCIO bit depth as an int (see the
 *        OakCommonOCIOUtils typedef documentation); set to 0
 *        (BIT_DEPTH_UNKNOWN) for invalid formats
 * @return OAKCOMMON_OK, or OAKCOMMON_E_INVALID if self or
 *         out_bit_depth is NULL or pixel_format is not a known code
 */
int oakcommon_ocioutils_get_ocio_bit_depth_from_pixel_format(
	OakCommonOCIOUtils *self, int pixel_format, int *out_bit_depth);

#ifdef __cplusplus
}
#endif

#endif // OAK_EDITOR_OCIOUTILS_H
