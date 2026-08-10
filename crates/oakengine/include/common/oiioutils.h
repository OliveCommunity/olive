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

#ifndef OAK_EDITOR_OIIOUTILS_H
#define OAK_EDITOR_OIIOUTILS_H

#include "common/error.h"

/* Reuses the OakPixelFormat enum (mirroring
 * olive::core::PixelFormat) rather than redefining it here. */
#include "common/ocioutils.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief OIIO base type codes, matching OIIO::TypeDesc::BASETYPE
 *
 * Passed through the C API as plain ints so callers never see OIIO
 * types. Values match the OIIO TypeDesc::BASETYPE enum: 0 = UNKNOWN,
 * 1 = NONE, 2 = UINT8, 3 = INT8, 4 = UINT16, 5 = INT16, 6 = UINT32,
 * 7 = INT32, 8 = UINT64, 9 = INT64, 10 = HALF, 11 = FLOAT, 12 = DOUBLE,
 * 13 = STRING, 14 = PTR. OIIO >= 2.5 adds 15 = USTRINGHASH and shifts
 * LASTBASE, so the exact LASTBASE value is version-dependent.
 */
/**
 * @brief Neutral by-value handle for the OIIO utils family
 *
 * The object is stateless; the handle exists only to satisfy the C API
 * lifetime contract. Ownership/count semantics follow common/handle.h:
 * init returns a handle whose (empty) object has reference count 1 and
 * release destroys it at zero. abi_version is always
 * OAKCOMMON_ABI_VERSION.
 */
typedef struct OakOIIOUtils {
	void *ctx; /**< Opaque pointer to the reference-counted object. */
	void (*addref)(void *ctx); /**< Atomically increments the count. */
	void (*release)(void *ctx); /**< Decrements the count, destroys at 0. */
	uint32_t abi_version; /**< OAKCOMMON_ABI_VERSION. */
} OakOIIOUtils;

/**
 * @brief Creates an OIIOUtils handle
 *
 * @return Handle with reference count 1; ctx is NULL on failure.
 */
OakOIIOUtils oakcommon_oiioutils_init(void);

/**
 * @brief Releases one reference to an OIIOUtils handle
 *
 * Convenience wrapper around handle.release(handle.ctx); no-op when
 * self is NULL or self->ctx is NULL.
 */
void oakcommon_oiioutils_free(OakOIIOUtils *self);

/**
 * @brief Maps a native pixel format to an OIIO base type
 *
 * @param self handle from oakcommon_oiioutils_init()
 * @param pixel_format one of the OakPixelFormat values
 * @param out_base_type receives the OIIO base type as an int (see the
 *        OakOIIOUtils typedef documentation); set to 0
 *        (TypeDesc::UNKNOWN) for invalid or unmappable formats
 * @return OAKCOMMON_OK, or OAKCOMMON_E_INVALID if self.ctx or
 *         out_base_type is NULL or pixel_format is not a known code
 */
int oakcommon_oiioutils_get_oiio_base_type_from_format(
	OakOIIOUtils self, int pixel_format, int *out_base_type);

/**
 * @brief Maps an OIIO base type to a native pixel format
 *
 * @param self handle from oakcommon_oiioutils_init()
 * @param base_type an OIIO TypeDesc::BASETYPE value as an int
 * @param out_pixel_format receives one of the OakPixelFormat
 *        values; set to OAKCOMMON_PIXEL_FORMAT_INVALID for unknown or
 *        unmappable base types
 * @return OAKCOMMON_OK, or OAKCOMMON_E_INVALID if self.ctx or
 *         out_pixel_format is NULL or base_type is negative
 */
int oakcommon_oiioutils_get_format_from_oiio_basetype(
	OakOIIOUtils self, int base_type, int *out_pixel_format);

/**
 * @brief Converts a PixelAspectRatio attribute value to a rational
 *
 * Flattened form of the former ImageSpec-based helper: the caller reads
 * the "PixelAspectRatio" float attribute from the OIIO::ImageSpec
 * (defaulting to 1.0 when absent) and passes it here.
 *
 * @param self handle from oakcommon_oiioutils_init()
 * @param pixel_aspect_ratio the PixelAspectRatio attribute value
 * @param out_numerator receives the rational numerator
 * @param out_denominator receives the rational denominator
 * @return OAKCOMMON_OK, or OAKCOMMON_E_INVALID if self.ctx,
 *         out_numerator or out_denominator is NULL
 */
int oakcommon_oiioutils_get_pixel_aspect_ratio(
	OakOIIOUtils self, double pixel_aspect_ratio, int *out_numerator,
	int *out_denominator);

#ifdef __cplusplus
}
#endif

#endif // OAK_EDITOR_OIIOUTILS_H
