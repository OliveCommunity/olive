/***

  Oak - Non-Linear Video Editor
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

#ifndef OAKENGINE_VIDEOPARAMS_H
#define OAKENGINE_VIDEOPARAMS_H

#include <stdint.h>

#include "export.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file videoparams.h
 * @brief C ABI POD and static-data accessors for the engine's VideoParams
 *
 * Covers the parameter surface the export and sequence dialogs need:
 * the POD carried by the encoding family (oakengine/encoding.h) and the
 * static metadata behind the standard combo boxes (supported frame rates,
 * pixel aspect ratios, dividers, pixel format names). Display/render-path
 * helpers (bytes per pixel, scaled texture sizes, ...) are out of scope for
 * now.
 *
 * Conventions match the other facade families: buf/size strings (return
 * value is the would-be length excluding the NUL), -1 on invalid indexes,
 * 0 (OAKENGINE_OK) / negative OAKENGINE_E_* codes where applicable.
 */

/**
 * @brief POD mirror of olive::VideoParams' user-facing fields.
 *
 * `time_base_*` is the frame duration (frame rate flipped), matching
 * VideoParams::time_base(); the frame rate is den/num. `format` is an
 * olive::PixelFormat::Format value, `interlacing` an
 * olive::VideoParams::Interlacing value (0 = none/progressive, 1 = top
 * field first, 2 = bottom field first), `color_range` an
 * olive::VideoParams::ColorRange value. The video channel count is an
 * engine-internal constant and not exposed.
 */
typedef struct oak_video_params {
	int width;
	int height;
	int time_base_num; /**< Frame duration numerator (e.g. 1001/30000 s). */
	int time_base_den;
	int format; /**< olive::PixelFormat::Format. */
	int pixel_aspect_num;
	int pixel_aspect_den;
	int interlacing; /**< olive::VideoParams::Interlacing. */
	int color_range; /**< olive::VideoParams::ColorRange. */
	int divider; /**< Preview resolution divider (1 = full). */
	/* The two fields below are only populated by the viewer family
	 * (oakengine_viewer_get_video_params(), B8c); other producers leave
	 * them 0 (k_video_type_video / not premultiplied). */
	int video_type; /**< olive::VideoParams::Type. */
	int premultiplied_alpha; /**< 0/1. */
} oak_video_params;

/** @brief Number of standard frame rates (VideoParams::k_supported_frame_rates). */
OAKENGINE_API int oakengine_video_params_supported_frame_rate_count(void);

/**
 * @brief The `index`-th standard frame rate as num/den (e.g. 24000/1001);
 * OAKENGINE_E_INVALID when out of range.
 */
OAKENGINE_API int oakengine_video_params_supported_frame_rate_at(int index,
																 int *num,
																 int *den);

/**
 * @brief User-friendly label of a frame rate num/den pair
 * (VideoParams::frame_rate_to_string(); buf/size).
 */
OAKENGINE_API int oakengine_video_params_frame_rate_to_string(int num, int den,
															  char *buf,
															  int buf_size);

/** @brief Number of standard pixel aspect ratios. */
OAKENGINE_API int oakengine_video_params_standard_pixel_aspect_count(void);

/** @brief The `index`-th standard pixel aspect ratio as num/den. */
OAKENGINE_API int oakengine_video_params_standard_pixel_aspect_at(int index,
																  int *num,
																  int *den);

/** @brief Display name of the `index`-th standard pixel aspect (buf/size). */
OAKENGINE_API int
oakengine_video_params_standard_pixel_aspect_name(int index, char *buf,
												  int buf_size);

/**
 * @brief VideoParams::format_pixel_aspect_ratio_string(): formats `format`
 * (a printf-style "%1" template) with the pixel aspect ratio num/den
 * (buf/size).
 */
OAKENGINE_API int oakengine_video_params_format_pixel_aspect_ratio_string(
	const char *format, int num, int den, char *buf, int buf_size);

/** @brief Number of supported preview dividers. */
OAKENGINE_API int oakengine_video_params_supported_divider_count(void);

/** @brief The `index`-th supported divider; -1 when out of range. */
OAKENGINE_API int oakengine_video_params_supported_divider_at(int index);

/** @brief Display name of a divider (VideoParams::get_name_for_divider()). */
OAKENGINE_API int oakengine_video_params_divider_name(int divider, char *buf,
													  int buf_size);

/**
 * @brief 1 when `format` (a PixelFormat::Format value) is a float format
 * (VideoParams::format_is_float()).
 */
OAKENGINE_API int oakengine_video_params_format_is_float(int format);

/** @brief Display name of a PixelFormat::Format value (buf/size). */
OAKENGINE_API int oakengine_video_params_pixel_format_name(int format,
														   char *buf,
														   int buf_size);

/**
 * @brief Effective (divider-scaled) dimensions of width/height at `divider`
 * (VideoParams::effective_width()/effective_height()). Any output pointer
 * may be NULL.
 *
 * @return OAKENGINE_OK, or OAKENGINE_E_INVALID for non-positive
 * width/height/divider.
 */
OAKENGINE_API int oakengine_video_params_effective_size(int width, int height,
														int divider,
														int *out_width,
														int *out_height);

/**
 * @brief Fill an oak_video_params POD (the display-path VideoParams
 * constructor equivalent). No validation is performed beyond rejecting a
 * NULL `p`; use oakengine_video_params_is_valid() to validate.
 *
 * @return OAKENGINE_OK, or OAKENGINE_E_INVALID for NULL `p`.
 */
OAKENGINE_API int oakengine_video_params_make(oak_video_params *p, int width,
											  int height, int time_base_num,
											  int time_base_den, int format,
											  int pixel_aspect_num,
											  int pixel_aspect_den,
											  int interlacing, int color_range,
											  int divider);

/**
 * @brief Create an engine-side olive::VideoParams object from a POD.
 *
 * The returned pointer must be freed with oakengine_video_params_free().
 * This is the only legal way for app code to construct a VideoParams object
 * during the R6 C ABI migration.
 *
 * @return Engine-owned VideoParams pointer, or NULL if pod is NULL.
 */
OAKENGINE_API void *oakengine_video_params_create(const oak_video_params *pod);

/** @brief Free a VideoParams object created by oakengine_video_params_create(). */
OAKENGINE_API void oakengine_video_params_free(void *params);

/**
 * @brief 1 when all user-facing fields of `a` and `b` match
 * (VideoParams::operator==), 0 otherwise or when either is NULL.
 */
OAKENGINE_API int oakengine_video_params_equal(const oak_video_params *a,
											   const oak_video_params *b);

/**
 * @brief 1 when the POD describes a usable video stream
 * (VideoParams::is_valid(): positive dimensions, non-null pixel aspect,
 * in-range pixel format), 0 otherwise or when `p` is NULL.
 */
OAKENGINE_API int oakengine_video_params_is_valid(const oak_video_params *p);

/**
 * @brief Bytes per pixel of `format` (a PixelFormat::Format value) with
 * `channels` channels (VideoParams::get_bytes_per_pixel()).
 */
OAKENGINE_API int oakengine_video_params_bytes_per_pixel(int format,
														 int channels);

/**
 * @brief The engine-internal video channel count
 * (VideoParams::k_internal_channel_count, i.e. RGBA).
 */
OAKENGINE_API int oakengine_video_params_internal_channel_count(void);

#ifdef __cplusplus
}
#endif

#endif /* OAKENGINE_VIDEOPARAMS_H */
