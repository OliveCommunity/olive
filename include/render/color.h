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

#ifndef OAK_EDITOR_RENDER_COLOR_H
#define OAK_EDITOR_RENDER_COLOR_H

#include "error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file color.h
 * @brief C ABI for oakrender color processing (olive::ColorProcessor) and
 *        the process-wide default OCIO config (olive::ColorManager
 *        statics), M7 §2.3.
 *
 * An OakColorProcessor IS a wrapper allocation holding a
 * ColorProcessorPtr (ColorProcessor is shared_ptr-managed); release with
 * oakrender_color_processor_free(). NULL is accepted by every function
 * and yields a no-op / OAKRENDER_E_INVALID.
 *
 * Processors are built against the process-wide default OCIO config
 * (olive::ColorManager::get_default_config()): the $OCIO config when the
 * environment variable is set, otherwise the config extracted to the
 * user configuration location. oakrender_color_manager_set_up_default_config()
 * (re)builds it.
 */

/** Direction values for oakrender_color_processor_create(). */
enum {
	OAKRENDER_COLOR_DIRECTION_NORMAL = 0,
	OAKRENDER_COLOR_DIRECTION_INVERSE = 1
};

typedef struct OakColorProcessor OakColorProcessor;

/**
 * @brief Create a colorspace-to-colorspace processor on the default
 * OCIO config.
 *
 * @param src_space Source colorspace name (role names are resolved).
 * @param dst_transform Destination colorspace / output transform name.
 * @param direction OAKRENDER_COLOR_DIRECTION_NORMAL (src -> dst) or
 *        OAKRENDER_COLOR_DIRECTION_INVERSE (dst -> src).
 *
 * OCIO failures are non-fatal (matching the C++ behavior): the handle is
 * still returned but oakrender_color_processor_is_valid() reports 0 and
 * conversions are pass-through.
 *
 * @return Processor handle, or NULL for NULL/empty strings, an unknown
 *         direction, no default config, or allocation failure.
 */
OakColorProcessor *oakrender_color_processor_create(const char *src_space,
													const char *dst_transform,
													int direction);

/** @brief Release a processor handle. NULL-safe no-op. */
void oakrender_color_processor_free(OakColorProcessor *processor);

/**
 * @brief 1 when the processor holds a valid OCIO processor
 * (ColorProcessor::get_processor() != null), 0 otherwise / on NULL.
 */
int oakrender_color_processor_is_valid(const OakColorProcessor *processor);

/**
 * @brief Convert a single RGBA color (ColorProcessor::convert_color()).
 * On an invalid processor the input is copied through.
 *
 * @return OAKRENDER_OK, or OAKRENDER_E_INVALID for NULL arguments.
 */
int oakrender_color_processor_convert(OakColorProcessor *processor,
									  double ir, double ig, double ib,
									  double ia, double *out_r, double *out_g,
									  double *out_b, double *out_a);

/* ---- ColorManager statics ------------------------------------------------- */

/**
 * @brief (Re)build the process-wide default OCIO config
 * (ColorManager::set_up_default_config()).
 *
 * @return OAKRENDER_OK, or OAKRENDER_E_FAILED when no config could be
 *         created.
 */
int oakrender_color_manager_set_up_default_config(void);

/**
 * @brief Describe the active default config: the $OCIO path when set,
 * otherwise the extracted default config's path. Two-stage string
 * getter: returns the required buffer size including NUL; pass
 * buf == NULL or too small a buffer to query the size.
 *
 * @return Required size (non-negative), or OAKRENDER_E_STATE when no
 *         default config exists.
 */
int oakrender_color_manager_get_config(char *buf, int n);

/**
 * @brief OCIO cache id of the display/view transform of the active
 * default config, computed from the config's reference colorspace
 * (a stable identifier usable as a conversion cache key).
 *
 * Two-stage string getter (same convention as
 * oakrender_color_manager_get_config()).
 *
 * @return Required size (non-negative), OAKRENDER_E_INVALID (NULL/empty
 *         display or view), OAKRENDER_E_STATE (no default config), or
 *         OAKRENDER_E_NOT_FOUND (unknown display/view).
 */
int oakrender_color_manager_display_transform(const char *display,
											  const char *view, char *buf,
											  int n);

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_RENDER_COLOR_H
