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

#ifndef OAK_EDITOR_FFMPEGUTILS_H
#define OAK_EDITOR_FFMPEGUTILS_H

#include "common/error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Stateless mappings between native pixel/sample formats and the
 * opaque FBPixelFormat / FBSampleFormat constants of ffmpeg_bridge
 *
 * All functions are pure format conversions; there is no handle to create
 * or free. Native pixel/sample formats are passed as plain ints matching
 * the olive::core::PixelFormat::Format / SampleFormat::Format enum values
 * (invalid = -1). Bridge formats are the fb_pix_fmt_* / fb_sample_fmt_*
 * constants from ffmpeg_bridge/ffmpeg_bridge.h.
 */

/**
 * @brief RGB / RGBA channel counts (flattened from VideoParams)
 */
#define OAKCOMMON_RGB_CHANNEL_COUNT 3
#define OAKCOMMON_RGBA_CHANNEL_COUNT 4

/**
 * @brief Returns a bridge pixel format that a frame can be converted to
 * with minimal data loss, clamped to a maximum native precision
 *
 * @param pix_fmt bridge pixel format to find a compatible conversion for
 * @param maximum_pix_fmt maximum native pixel format, or -1 for no limit
 * @param out receives the chosen bridge pixel format
 * @return OAKCOMMON_OK on success, OAKCOMMON_E_INVALID if out is NULL
 */
int oakcommon_ffmpegutils_get_compatible_bridge_pixel_format(
	int pix_fmt, int maximum_pix_fmt, int *out);

/**
 * @brief Returns a native pixel format usable to convert from a native
 * frame to a bridge frame with minimal data loss
 *
 * @param pix_fmt native pixel format
 * @param out receives the compatible native pixel format (-1 if none)
 * @return OAKCOMMON_OK on success, OAKCOMMON_E_INVALID if out is NULL
 */
int oakcommon_ffmpegutils_get_compatible_pixel_format(int pix_fmt,
						      int *out);

/**
 * @brief Returns a bridge pixel format for a given native pixel format
 *
 * @param pix_fmt native pixel format
 * @param channel_count OAKCOMMON_RGB_CHANNEL_COUNT or
 * OAKCOMMON_RGBA_CHANNEL_COUNT
 * @param out receives the bridge pixel format (fb_pix_fmt_none if none)
 * @return OAKCOMMON_OK on success, OAKCOMMON_E_INVALID if out is NULL
 */
int oakcommon_ffmpegutils_get_ffmpeg_pixel_format(int pix_fmt,
						  int channel_count,
						  int *out);

/**
 * @brief Returns a native sample format for a given bridge sample format
 *
 * @param smp_fmt bridge sample format
 * @param out receives the native sample format (-1 if unknown)
 * @return OAKCOMMON_OK on success, OAKCOMMON_E_INVALID if out is NULL
 */
int oakcommon_ffmpegutils_get_native_sample_format(int smp_fmt, int *out);

/**
 * @brief Returns a bridge sample format for a given native sample format
 *
 * @param smp_fmt native sample format
 * @param out receives the bridge sample format (fb_sample_fmt_none if none)
 * @return OAKCOMMON_OK on success, OAKCOMMON_E_INVALID if out is NULL
 */
int oakcommon_ffmpegutils_get_ffmpeg_sample_format(int smp_fmt, int *out);

/**
 * @brief Converts a "JPEG" full-range bridge pixel format to its regular
 * counterpart
 *
 * @param pix_fmt bridge pixel format
 * @param out receives the regular-range format (unchanged if not JPEG)
 * @return OAKCOMMON_OK on success, OAKCOMMON_E_INVALID if out is NULL
 */
int oakcommon_ffmpegutils_convert_jpeg_space_to_regular_space(int pix_fmt,
							      int *out);

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_FFMPEGUTILS_H
