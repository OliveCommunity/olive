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
#include "renderer.h"
#include "common/colortransform.h" /* OakColorTransform */
#include "node/colormanager.h" /* OakNodeColorManager */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file color.h
 * @brief C ABI for oakrender color processing (olive::ColorProcessor) and
 *        the process-wide default OCIO config (olive::ColorManager
 *        statics), M7 §2.3.
 *
 * An OakColorProcessor is a by-value reference-counted handle (shared_ptr
 * semantics, see oakcommon's common/handle.h) boxing a ColorProcessorPtr
 * (ColorProcessor is shared_ptr-managed); release with
 * oakrender_color_processor_free(). Empty handles (ctx == NULL) are
 * accepted by every function and yield a no-op / OAKRENDER_E_INVALID.
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

typedef struct OakColorProcessor {
	void *ctx; /**< Opaque pointer to the reference-counted object. */
	void (*addref)(void *ctx); /**< Atomically increments the count. */
	void (*release)(void *ctx); /**< Decrements the count, destroys at 0. */
	uint32_t abi_version; /**< OAKRENDER_ABI_VERSION. */
} OakColorProcessor;

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
 * @return Processor handle with reference count 1; ctx is NULL for
 *         NULL/empty strings, an unknown direction, no default config,
 *         or allocation failure.
 */
OakColorProcessor oakrender_color_processor_create(const char *src_space,
												   const char *dst_transform,
												   int direction);

/**
 * @brief Release one reference to a processor handle. Convenience
 * wrapper around processor->release(processor->ctx). NULL /
 * empty-handle no-op; clears processor->ctx after releasing.
 */
void oakrender_color_processor_free(OakColorProcessor *processor);

/**
 * @brief 1 when the processor holds a valid OCIO processor
 * (ColorProcessor::get_processor() != null), 0 otherwise / empty.
 */
int oakrender_color_processor_is_valid(OakColorProcessor processor);

/**
 * @brief Create a processor from an input colorspace and a destination
 *        transform on a node's color manager
 *        (ColorProcessor::create(ColorManager*, input, dest, dir)).
 *
 * @param manager Borrowed manager handle (e.g.
 *        oaknode_colormanager_wrap_borrowed()).
 * @param direction OAKRENDER_COLOR_DIRECTION_NORMAL / _INVERSE.
 * @return Processor handle with reference count 1; ctx is NULL for
 *         empty/invalid arguments or allocation failure.
 */
OakColorProcessor oakrender_color_processor_create_transform(
	OakNodeColorManager manager, const char *input,
	OakColorTransform dest, int direction);

/**
 * @brief Create a processor from a LUT file on a node's color manager
 *        (OCIO FileTransform with linear interpolation; direction
 *        selects forward/inverse).
 *
 * @return Processor handle with reference count 1; ctx is NULL for
 *         empty/invalid arguments, an unreadable LUT, or allocation
 *         failure.
 */
OakColorProcessor oakrender_color_processor_create_lut(
	OakNodeColorManager manager, const char *path, int direction);

/**
 * @brief Grading-primary transform styles for
 *        oakrender_color_processor_create_grading_primary().
 */
enum OakRenderGradingPrimaryStyle {
	OAKRENDER_GRADING_PRIMARY_LIN = 0, /**< OCIO GRADING_LIN */
	OAKRENDER_GRADING_PRIMARY_LOG = 1 /**< OCIO GRADING_LOG */
};

/**
 * @brief Create a dynamic grading-primary processor on a node's color
 *        manager (OCIO GradingPrimaryTransform, forward direction).
 *
 * @return Processor handle with reference count 1; ctx is NULL for
 *         invalid arguments or allocation failure.
 */
OakColorProcessor oakrender_color_processor_create_grading_primary(
	OakNodeColorManager manager, int style);

/* ---- LUT library ---------------------------------------------------- */

/**
 * @brief 1 when `extension` (without dot, case-insensitive) is a
 *        supported LUT extension (LUTLibrary::is_supported_extension()).
 */
int oakrender_lut_is_supported_extension(const char *extension);

/**
 * @brief Number of supported LUT extensions
 *        (LUTLibrary::supported_extensions()).
 */
int oakrender_lut_supported_extensions_count(void);

/**
 * @brief Supported LUT extension at `index`, two-stage string.
 *
 * @return Required buffer size in bytes (including NUL), or a negative
 *         OAKRENDER_E_* code for an out-of-range index.
 */
int oakrender_lut_supported_extension_at(int index, char *buf,
										 int buf_size);

/**
 * @brief Convert a single RGBA color (ColorProcessor::convert_color()).
 * On an invalid processor the input is copied through.
 *
 * @return OAKRENDER_OK, or OAKRENDER_E_INVALID for empty/NULL arguments.
 */
int oakrender_color_processor_convert(OakColorProcessor processor,
									  double ir, double ig, double ib,
									  double ia, double *out_r, double *out_g,
									  double *out_b, double *out_a);

/**
 * @brief Convert a CPU frame's pixels through the processor, in place
 * (olive::ColorProcessor::convert_frame()).
 *
 * The frame's data buffer is rewritten through an OCIO PackedImageDesc
 * view; nothing is allocated and the frame handle stays owned by the
 * caller. A processor whose underlying OCIO processor is null
 * (oakrender_color_processor_create() treats lookup failure as
 * non-fatal) is a pass-through and returns OAKRENDER_OK, mirroring the
 * C++ API.
 *
 * @return OAKRENDER_OK, OAKRENDER_E_INVALID for empty/uninitialized
 *         arguments, or OAKRENDER_E_FAILED on an internal exception.
 */
int oakrender_color_processor_convert_frame(OakColorProcessor processor,
											OakCodecFrame frame);

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
} /* extern "C" */

#include <memory>
namespace olive { class ColorProcessor; }

extern "C" {
#endif

/**
 * @brief Borrowed access to the underlying C++ processor (C++ only, for
 *        adapter layers; a shared_ptr copy keeps the object alive).
 *        Empty shared_ptr for an empty handle.
 */
std::shared_ptr<olive::ColorProcessor> oakrender_color_processor_get_native(
	OakColorProcessor processor);

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_RENDER_COLOR_H
