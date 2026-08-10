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

#ifndef OAK_EDITOR_VIDEOPARAMS_H
#define OAK_EDITOR_VIDEOPARAMS_H

#ifndef __cplusplus
#include <stdbool.h>
#endif

#include <stdint.h>

#include "common/error.h"
#include "common/handle.h"
#include "common/ocioutils.h"

#ifdef __cplusplus
namespace olive
{
class VideoParams;
}
extern "C" {
#endif

/**
 * @brief Neutral by-value handle to a video parameter set
 *        (olive::VideoParams).
 *
 * Ownership/count semantics follow the convention in common/handle.h:
 * init functions return a handle whose object has reference count 1,
 * addref(ctx)/release(ctx) adjust it atomically, and release destroys
 * the object at zero. abi_version is always OAKCOMMON_ABI_VERSION.
 */
typedef struct OakVideoParams {
	void *ctx; /**< Opaque pointer to the reference-counted object. */
	void (*addref)(void *ctx); /**< Atomically increments the count. */
	void (*release)(void *ctx); /**< Decrements the count, destroys at 0. */
	uint32_t abi_version; /**< OAKCOMMON_ABI_VERSION. */
} OakVideoParams;

/**
 * @brief Interlacing modes, mirroring olive::VideoParams::Interlacing.
 */
enum OakVideoInterlacing {
	OAKCOMMON_VIDEO_INTERLACE_NONE = 0,
	OAKCOMMON_VIDEO_INTERLACED_TOP_FIRST = 1,
	OAKCOMMON_VIDEO_INTERLACED_BOTTOM_FIRST = 2
};

/**
 * @brief Video stream types, mirroring olive::VideoParams::Type.
 */
enum OakVideoType {
	OAKCOMMON_VIDEO_TYPE_VIDEO = 0,
	OAKCOMMON_VIDEO_TYPE_STILL = 1,
	OAKCOMMON_VIDEO_TYPE_IMAGE_SEQUENCE = 2
};

/**
 * @brief Color range codes, mirroring olive::VideoParams::ColorRange.
 */
enum OakVideoColorRange {
	OAKCOMMON_COLOR_RANGE_LIMITED = 0, /**< 16-235 */
	OAKCOMMON_COLOR_RANGE_FULL = 1 /**< 0-255 */
};

/**
 * @brief Create a default (invalid) video parameter set.
 *
 * @return Handle with reference count 1; ctx is NULL on allocation
 *         failure.
 */
OakVideoParams oakcommon_videoparams_init(void);

/**
 * @brief Create a video parameter set without a time base.
 *
 * @param pixel_format One of the OakPixelFormat values.
 * @return Handle with reference count 1; ctx is NULL on allocation
 *         failure.
 */
OakVideoParams oakcommon_videoparams_init_basic(
	int width, int height, int pixel_format, int nb_channels,
	int pixel_aspect_num, int pixel_aspect_den, int interlacing, int divider);

/**
 * @brief Create a video parameter set with a time base.
 *
 * The frame rate is derived as the flipped time base.
 *
 * @param pixel_format One of the OakPixelFormat values.
 * @return Handle with reference count 1; ctx is NULL on allocation
 *         failure.
 */
OakVideoParams oakcommon_videoparams_init_with_time_base(
	int width, int height, int time_base_num, int time_base_den,
	int pixel_format, int nb_channels, int pixel_aspect_num,
	int pixel_aspect_den, int interlacing, int divider);

#ifdef __cplusplus
/**
 * @brief Copy a native olive::VideoParams into a new handle.
 *
 * The source object is deep-copied; the handle does not keep any
 * reference to @p src, which may be destroyed immediately afterwards.
 * Only visible to C++ consumers.
 *
 * @return Handle with reference count 1; ctx is NULL if src is NULL or
 *         on allocation failure.
 */
OakVideoParams oakcommon_videoparams_init_from_native(
	const olive::VideoParams *src);

/**
 * @brief Borrow the native object behind a handle.
 *
 * The returned pointer is borrowed: it stays valid while the caller
 * holds a reference to the handle (i.e. until the matching release).
 * Only visible to C++ consumers.
 *
 * @return Borrowed pointer, or NULL if params is NULL or params->ctx is
 *         NULL.
 */
const olive::VideoParams *oakcommon_videoparams_get_native(
	OakVideoParams params);
#endif

/**
 * @brief Release one reference to a video parameter set.
 *
 * Convenience wrapper around handle.release(handle.ctx): decrements the
 * atomic reference count and destroys the object when it reaches zero.
 * No-op when params is NULL or params->ctx is NULL.
 */
void oakcommon_videoparams_free(OakVideoParams *params);

int oakcommon_videoparams_get_width(OakVideoParams params, int *width);
int oakcommon_videoparams_set_width(OakVideoParams params, int width);
int oakcommon_videoparams_get_height(OakVideoParams params, int *height);
int oakcommon_videoparams_set_height(OakVideoParams params, int height);
int oakcommon_videoparams_get_depth(OakVideoParams params, int *depth);
int oakcommon_videoparams_set_depth(OakVideoParams params, int depth);
int oakcommon_videoparams_get_is_3d(OakVideoParams params, int *is_3d);

/**
 * @brief Rational getters return the value as a numerator/denominator pair.
 */
int oakcommon_videoparams_get_time_base(OakVideoParams params,
										int *numerator, int *denominator);
int oakcommon_videoparams_set_time_base(OakVideoParams params,
										int numerator, int denominator);
int oakcommon_videoparams_get_frame_rate(OakVideoParams params,
										 int *numerator, int *denominator);
int oakcommon_videoparams_set_frame_rate(OakVideoParams params,
										 int numerator, int denominator);
int oakcommon_videoparams_frame_rate_as_time_base(OakVideoParams params,
												  int *numerator,
												  int *denominator);
int oakcommon_videoparams_get_pixel_aspect_ratio(OakVideoParams params,
												 int *numerator,
												 int *denominator);
int oakcommon_videoparams_set_pixel_aspect_ratio(OakVideoParams params,
												 int numerator, int denominator);

/**
 * @brief Format getters/setters use the OakPixelFormat codes.
 */
int oakcommon_videoparams_get_format(OakVideoParams params, int *format);
int oakcommon_videoparams_set_format(OakVideoParams params, int format);
int oakcommon_videoparams_get_channel_count(OakVideoParams params,
											int *count);
int oakcommon_videoparams_set_channel_count(OakVideoParams params,
											int count);
int oakcommon_videoparams_get_interlacing(OakVideoParams params,
										  int *interlacing);
int oakcommon_videoparams_set_interlacing(OakVideoParams params,
										  int interlacing);
int oakcommon_videoparams_get_divider(OakVideoParams params,
									  int *divider);
int oakcommon_videoparams_set_divider(OakVideoParams params,
									  int divider);
int oakcommon_videoparams_get_enabled(OakVideoParams params,
									  int *enabled);
int oakcommon_videoparams_set_enabled(OakVideoParams params,
									  int enabled);
int oakcommon_videoparams_get_x(OakVideoParams params, float *x);
int oakcommon_videoparams_set_x(OakVideoParams params, float x);
int oakcommon_videoparams_get_y(OakVideoParams params, float *y);
int oakcommon_videoparams_set_y(OakVideoParams params, float y);
int oakcommon_videoparams_get_stream_index(OakVideoParams params,
										   int *index);
int oakcommon_videoparams_set_stream_index(OakVideoParams params,
										   int index);
int oakcommon_videoparams_get_video_type(OakVideoParams params,
										 int *type);
int oakcommon_videoparams_set_video_type(OakVideoParams params,
										 int type);
int oakcommon_videoparams_get_start_time(OakVideoParams params,
										 int64_t *start_time);
int oakcommon_videoparams_set_start_time(OakVideoParams params,
										 int64_t start_time);
int oakcommon_videoparams_get_duration(OakVideoParams params,
									   int64_t *duration);
int oakcommon_videoparams_set_duration(OakVideoParams params,
									   int64_t duration);
int oakcommon_videoparams_get_premultiplied_alpha(OakVideoParams params,
												  int *premultiplied);
int oakcommon_videoparams_set_premultiplied_alpha(OakVideoParams params,
												  int premultiplied);
int oakcommon_videoparams_get_color_range(OakVideoParams params,
										  int *color_range);
int oakcommon_videoparams_set_color_range(OakVideoParams params,
										  int color_range);
int oakcommon_videoparams_get_color_primaries(OakVideoParams params,
											  int *primaries);
int oakcommon_videoparams_set_color_primaries(OakVideoParams params,
											  int primaries);
int oakcommon_videoparams_get_color_transfer(OakVideoParams params,
											 int *transfer);
int oakcommon_videoparams_set_color_transfer(OakVideoParams params,
											 int transfer);

/**
 * @brief Get the colorspace name (two-stage string getter).
 *
 * @return Required buffer size in bytes including the terminating NUL
 *         (non-negative), or a negative OAKCOMMON_E_* error code.
 */
int oakcommon_videoparams_get_colorspace(OakVideoParams params,
										 char *buf, int buf_size);
int oakcommon_videoparams_set_colorspace(OakVideoParams params,
										 const char *colorspace);

/**
 * @brief Width multiplied by the pixel aspect ratio.
 */
int oakcommon_videoparams_get_square_pixel_width(OakVideoParams params,
												 int *width);
int oakcommon_videoparams_get_effective_width(OakVideoParams params,
											  int *width);
int oakcommon_videoparams_get_effective_height(OakVideoParams params,
											   int *height);
int oakcommon_videoparams_get_effective_depth(OakVideoParams params,
											  int *depth);
int oakcommon_videoparams_get_is_valid(OakVideoParams params,
									   int *is_valid);
int oakcommon_videoparams_get_bytes_per_channel(OakVideoParams params,
												int *bytes);
int oakcommon_videoparams_get_bytes_per_pixel(OakVideoParams params,
											  int *bytes);
int oakcommon_videoparams_get_buffer_size(OakVideoParams params,
										  int *size);

/**
 * @brief Convert a time (in seconds, as a rational) to time base units.
 *
 * Returns INT64_MIN (AV_NOPTS_VALUE) in @p timestamp when no time base is
 * set.
 */
int oakcommon_videoparams_get_time_in_timebase_units(
	OakVideoParams params, int time_num, int time_den,
	int64_t *timestamp);

/**
 * @brief Compare two parameter sets for equality.
 */
int oakcommon_videoparams_equals(OakVideoParams params,
								 OakVideoParams other, int *equal);

/**
 * @brief Load parameters from an XML fragment.
 *
 * @param xml NUL-terminated XML text. Must not be NULL.
 * @return OAKCOMMON_OK or a negative OAKCOMMON_E_* error code.
 */
int oakcommon_videoparams_load_xml(OakVideoParams params,
								   const char *xml);

/**
 * @brief Save parameters to an XML fragment (two-stage string getter).
 *
 * @return Required buffer size in bytes including the terminating NUL
 *         (non-negative), or a negative OAKCOMMON_E_* error code.
 */
int oakcommon_videoparams_save_xml(OakVideoParams params, char *buf,
								   int buf_size);

/* Static helpers (no handle required). */

int oakcommon_videoparams_get_bytes_per_channel_for_format(int pixel_format);
int oakcommon_videoparams_get_bytes_per_pixel_for_format(int pixel_format,
														 int channels);
int oakcommon_videoparams_calculate_buffer_size(int width, int height,
												int pixel_format,
												int channels);
int oakcommon_videoparams_format_is_float(int pixel_format);
int oakcommon_videoparams_generate_auto_divider(int64_t width, int64_t height);
int oakcommon_videoparams_get_scaled_dimension(int dimension, int divider);
int oakcommon_videoparams_get_divider_for_target_resolution(int src_width,
															int src_height,
															int dst_width,
															int dst_height);

/**
 * @brief Human-readable name for a divider ("Full", "1/2", ...).
 *
 * @return Required buffer size in bytes including the terminating NUL
 *         (non-negative), or a negative OAKCOMMON_E_* error code.
 */
int oakcommon_videoparams_get_name_for_divider(int divider, char *buf,
											   int buf_size);

/**
 * @brief Human-readable name for a pixel format.
 *
 * @return Required buffer size in bytes including the terminating NUL
 *         (non-negative), or a negative OAKCOMMON_E_* error code.
 */
int oakcommon_videoparams_get_format_name(int pixel_format, char *buf,
										  int buf_size);

/**
 * @brief Human-readable frame rate string ("23.976 FPS").
 *
 * @return Required buffer size in bytes including the terminating NUL
 *         (non-negative), or a negative OAKCOMMON_E_* error code.
 */
int oakcommon_videoparams_frame_rate_to_string(int numerator, int denominator,
											   char *buf, int buf_size);

/**
 * @brief Get bytes per channel.
 *
 * @return Bytes per channel.
 */
int oakcommon_videoparams_static_get_bytes_per_channel(OakPixelFormat format);

/**
 * @brief Get bytes per pixel.
 *
 * @return Bytes per pixel.
 */
int oakcommon_videoparams_static_get_bytes_per_pixel(OakPixelFormat format,
												 int channels);

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_VIDEOPARAMS_H
