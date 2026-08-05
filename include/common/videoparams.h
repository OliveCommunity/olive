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
#include "common/ocioutils.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque handle to a video parameter set (olive::VideoParams).
 */
typedef struct OakCommonVideoParams OakCommonVideoParams;

/**
 * @brief Interlacing modes, mirroring olive::VideoParams::Interlacing.
 */
enum OakCommonVideoInterlacing {
	OAKCOMMON_VIDEO_INTERLACE_NONE = 0,
	OAKCOMMON_VIDEO_INTERLACED_TOP_FIRST = 1,
	OAKCOMMON_VIDEO_INTERLACED_BOTTOM_FIRST = 2
};

/**
 * @brief Video stream types, mirroring olive::VideoParams::Type.
 */
enum OakCommonVideoType {
	OAKCOMMON_VIDEO_TYPE_VIDEO = 0,
	OAKCOMMON_VIDEO_TYPE_STILL = 1,
	OAKCOMMON_VIDEO_TYPE_IMAGE_SEQUENCE = 2
};

/**
 * @brief Color range codes, mirroring olive::VideoParams::ColorRange.
 */
enum OakCommonVideoColorRange {
	OAKCOMMON_COLOR_RANGE_LIMITED = 0, /**< 16-235 */
	OAKCOMMON_COLOR_RANGE_FULL = 1 /**< 0-255 */
};

/**
 * @brief Create a default (invalid) video parameter set.
 *
 * @return Params handle, or NULL on allocation failure.
 */
OakCommonVideoParams *oakcommon_videoparams_init(void);

/**
 * @brief Create a video parameter set without a time base.
 *
 * @param pixel_format One of the OakCommonPixelFormat values.
 * @return Params handle, or NULL on allocation failure.
 */
OakCommonVideoParams *oakcommon_videoparams_init_basic(
	int width, int height, int pixel_format, int nb_channels,
	int pixel_aspect_num, int pixel_aspect_den, int interlacing, int divider);

/**
 * @brief Create a video parameter set with a time base.
 *
 * The frame rate is derived as the flipped time base.
 *
 * @param pixel_format One of the OakCommonPixelFormat values.
 * @return Params handle, or NULL on allocation failure.
 */
OakCommonVideoParams *oakcommon_videoparams_init_with_time_base(
	int width, int height, int time_base_num, int time_base_den,
	int pixel_format, int nb_channels, int pixel_aspect_num,
	int pixel_aspect_den, int interlacing, int divider);

/**
 * @brief Destroy a video parameter set. No-op on NULL.
 */
void oakcommon_videoparams_free(OakCommonVideoParams *params);

int oakcommon_videoparams_get_width(OakCommonVideoParams *params, int *width);
int oakcommon_videoparams_set_width(OakCommonVideoParams *params, int width);
int oakcommon_videoparams_get_height(OakCommonVideoParams *params, int *height);
int oakcommon_videoparams_set_height(OakCommonVideoParams *params, int height);
int oakcommon_videoparams_get_depth(OakCommonVideoParams *params, int *depth);
int oakcommon_videoparams_set_depth(OakCommonVideoParams *params, int depth);
int oakcommon_videoparams_get_is_3d(OakCommonVideoParams *params, int *is_3d);

/**
 * @brief Rational getters return the value as a numerator/denominator pair.
 */
int oakcommon_videoparams_get_time_base(OakCommonVideoParams *params,
										int *numerator, int *denominator);
int oakcommon_videoparams_set_time_base(OakCommonVideoParams *params,
										int numerator, int denominator);
int oakcommon_videoparams_get_frame_rate(OakCommonVideoParams *params,
										 int *numerator, int *denominator);
int oakcommon_videoparams_set_frame_rate(OakCommonVideoParams *params,
										 int numerator, int denominator);
int oakcommon_videoparams_frame_rate_as_time_base(OakCommonVideoParams *params,
												  int *numerator,
												  int *denominator);
int oakcommon_videoparams_get_pixel_aspect_ratio(OakCommonVideoParams *params,
												 int *numerator,
												 int *denominator);
int oakcommon_videoparams_set_pixel_aspect_ratio(OakCommonVideoParams *params,
												 int numerator, int denominator);

/**
 * @brief Format getters/setters use the OakCommonPixelFormat codes.
 */
int oakcommon_videoparams_get_format(OakCommonVideoParams *params, int *format);
int oakcommon_videoparams_set_format(OakCommonVideoParams *params, int format);
int oakcommon_videoparams_get_channel_count(OakCommonVideoParams *params,
											int *count);
int oakcommon_videoparams_set_channel_count(OakCommonVideoParams *params,
											int count);
int oakcommon_videoparams_get_interlacing(OakCommonVideoParams *params,
										  int *interlacing);
int oakcommon_videoparams_set_interlacing(OakCommonVideoParams *params,
										  int interlacing);
int oakcommon_videoparams_get_divider(OakCommonVideoParams *params,
									  int *divider);
int oakcommon_videoparams_set_divider(OakCommonVideoParams *params,
									  int divider);
int oakcommon_videoparams_get_enabled(OakCommonVideoParams *params,
									  int *enabled);
int oakcommon_videoparams_set_enabled(OakCommonVideoParams *params,
									  int enabled);
int oakcommon_videoparams_get_x(OakCommonVideoParams *params, float *x);
int oakcommon_videoparams_set_x(OakCommonVideoParams *params, float x);
int oakcommon_videoparams_get_y(OakCommonVideoParams *params, float *y);
int oakcommon_videoparams_set_y(OakCommonVideoParams *params, float y);
int oakcommon_videoparams_get_stream_index(OakCommonVideoParams *params,
										   int *index);
int oakcommon_videoparams_set_stream_index(OakCommonVideoParams *params,
										   int index);
int oakcommon_videoparams_get_video_type(OakCommonVideoParams *params,
										 int *type);
int oakcommon_videoparams_set_video_type(OakCommonVideoParams *params,
										 int type);
int oakcommon_videoparams_get_start_time(OakCommonVideoParams *params,
										 int64_t *start_time);
int oakcommon_videoparams_set_start_time(OakCommonVideoParams *params,
										 int64_t start_time);
int oakcommon_videoparams_get_duration(OakCommonVideoParams *params,
									   int64_t *duration);
int oakcommon_videoparams_set_duration(OakCommonVideoParams *params,
									   int64_t duration);
int oakcommon_videoparams_get_premultiplied_alpha(OakCommonVideoParams *params,
												  int *premultiplied);
int oakcommon_videoparams_set_premultiplied_alpha(OakCommonVideoParams *params,
												  int premultiplied);
int oakcommon_videoparams_get_color_range(OakCommonVideoParams *params,
										  int *color_range);
int oakcommon_videoparams_set_color_range(OakCommonVideoParams *params,
										  int color_range);
int oakcommon_videoparams_get_color_primaries(OakCommonVideoParams *params,
											  int *primaries);
int oakcommon_videoparams_set_color_primaries(OakCommonVideoParams *params,
											  int primaries);
int oakcommon_videoparams_get_color_transfer(OakCommonVideoParams *params,
											 int *transfer);
int oakcommon_videoparams_set_color_transfer(OakCommonVideoParams *params,
											 int transfer);

/**
 * @brief Get the colorspace name (two-stage string getter).
 *
 * @return Required buffer size in bytes including the terminating NUL
 *         (non-negative), or a negative OAKCOMMON_E_* error code.
 */
int oakcommon_videoparams_get_colorspace(OakCommonVideoParams *params,
										 char *buf, int buf_size);
int oakcommon_videoparams_set_colorspace(OakCommonVideoParams *params,
										 const char *colorspace);

/**
 * @brief Width multiplied by the pixel aspect ratio.
 */
int oakcommon_videoparams_get_square_pixel_width(OakCommonVideoParams *params,
												 int *width);
int oakcommon_videoparams_get_effective_width(OakCommonVideoParams *params,
											  int *width);
int oakcommon_videoparams_get_effective_height(OakCommonVideoParams *params,
											   int *height);
int oakcommon_videoparams_get_effective_depth(OakCommonVideoParams *params,
											  int *depth);
int oakcommon_videoparams_get_is_valid(OakCommonVideoParams *params,
									   int *is_valid);
int oakcommon_videoparams_get_bytes_per_channel(OakCommonVideoParams *params,
												int *bytes);
int oakcommon_videoparams_get_bytes_per_pixel(OakCommonVideoParams *params,
											  int *bytes);
int oakcommon_videoparams_get_buffer_size(OakCommonVideoParams *params,
										  int *size);

/**
 * @brief Convert a time (in seconds, as a rational) to time base units.
 *
 * Returns INT64_MIN (AV_NOPTS_VALUE) in @p timestamp when no time base is
 * set.
 */
int oakcommon_videoparams_get_time_in_timebase_units(
	OakCommonVideoParams *params, int time_num, int time_den,
	int64_t *timestamp);

/**
 * @brief Compare two parameter sets for equality.
 */
int oakcommon_videoparams_equals(OakCommonVideoParams *params,
								 OakCommonVideoParams *other, int *equal);

/**
 * @brief Load parameters from an XML fragment.
 *
 * @param xml NUL-terminated XML text. Must not be NULL.
 * @return OAKCOMMON_OK or a negative OAKCOMMON_E_* error code.
 */
int oakcommon_videoparams_load_xml(OakCommonVideoParams *params,
								   const char *xml);

/**
 * @brief Save parameters to an XML fragment (two-stage string getter).
 *
 * @return Required buffer size in bytes including the terminating NUL
 *         (non-negative), or a negative OAKCOMMON_E_* error code.
 */
int oakcommon_videoparams_save_xml(OakCommonVideoParams *params, char *buf,
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

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_VIDEOPARAMS_H
