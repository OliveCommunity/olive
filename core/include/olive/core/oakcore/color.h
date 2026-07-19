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

#ifndef OAKCORE_COLOR_H
#define OAKCORE_COLOR_H

#include "export.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file color.h
 * @brief C ABI for the high precision RGBA color value type
 *
 * Opaque handle + free functions. All returned OakColor* are owned by the
 * caller and must be released with oakcore_color_free().
 *
 * A color is always 4 float channels in red/green/blue/alpha order. Channel
 * values are unbounded: values outside [0, 1] are valid (HDR).
 *
 * Functions taking a `format` argument expect the same values as
 * PixelFormat::Format (render/pixelformat.h): invalid = -1, u8 = 0, u10 = 1,
 * u16 = 2, f16 = 3, f32 = 4.
 */
typedef struct OakColor OakColor;

OAKCORE_API OakColor *oakcore_color_create(void);
OAKCORE_API OakColor *oakcore_color_create_rgba(float r, float g, float b,
												float a);
OAKCORE_API OakColor *oakcore_color_copy(const OakColor *self);
OAKCORE_API void oakcore_color_free(OakColor *self);

/**
 * Creates a color from hue/saturation/value.
 *
 * Hue expects a value between 0.0 and 360.0. Saturation and value expect a
 * value between 0.0 and 1.0.
 */
OAKCORE_API OakColor *oakcore_color_from_hsv(float h, float s, float v);

/**
 * Creates a color from raw pixel data in the given pixel format. At most 4
 * channels are read; missing channels default to 0.
 */
OAKCORE_API OakColor *oakcore_color_from_data(const char *data, int format,
											  int nb_channels);

OAKCORE_API float oakcore_color_red(const OakColor *self);
OAKCORE_API float oakcore_color_green(const OakColor *self);
OAKCORE_API float oakcore_color_blue(const OakColor *self);
OAKCORE_API float oakcore_color_alpha(const OakColor *self);

OAKCORE_API void oakcore_color_set_red(OakColor *self, float red);
OAKCORE_API void oakcore_color_set_green(OakColor *self, float green);
OAKCORE_API void oakcore_color_set_blue(OakColor *self, float blue);
OAKCORE_API void oakcore_color_set_alpha(OakColor *self, float alpha);

OAKCORE_API void oakcore_color_to_hsv(const OakColor *self, float *hue,
									  float *sat, float *val);
OAKCORE_API float oakcore_color_hsv_hue(const OakColor *self);
OAKCORE_API float oakcore_color_hsv_saturation(const OakColor *self);
OAKCORE_API float oakcore_color_value(const OakColor *self);

OAKCORE_API void oakcore_color_to_hsl(const OakColor *self, float *hue,
									  float *sat, float *lightness);
OAKCORE_API float oakcore_color_hsl_hue(const OakColor *self);
OAKCORE_API float oakcore_color_hsl_saturation(const OakColor *self);
OAKCORE_API float oakcore_color_lightness(const OakColor *self);

/**
 * Borrowed pointer to the 4 float channels (rgba order), valid until
 * oakcore_color_free().
 */
OAKCORE_API float *oakcore_color_data(OakColor *self);
OAKCORE_API const float *oakcore_color_const_data(const OakColor *self);

/**
 * Writes the color as raw pixel data in the given pixel format. At most 4
 * channels are written; out must have room for nb_channels * bytes-per-channel
 * (u10 always packs to 4 bytes).
 */
OAKCORE_API void oakcore_color_to_data(const OakColor *self, char *out,
									   int format, int nb_channels);

/**
 * Super rough luminance value mostly used for UI (determining whether to
 * overlay with black or white text).
 */
OAKCORE_API float oakcore_color_get_rough_luminance(const OakColor *self);

OAKCORE_API void oakcore_color_add_assign(OakColor *self,
										  const OakColor *other);
OAKCORE_API void oakcore_color_sub_assign(OakColor *self,
										  const OakColor *other);
OAKCORE_API void oakcore_color_add_scalar_assign(OakColor *self, float value);
OAKCORE_API void oakcore_color_sub_scalar_assign(OakColor *self, float value);
OAKCORE_API void oakcore_color_mul_scalar_assign(OakColor *self, float value);
OAKCORE_API void oakcore_color_div_scalar_assign(OakColor *self, float value);

#ifdef __cplusplus
}
#endif

#endif /* OAKCORE_COLOR_H */
