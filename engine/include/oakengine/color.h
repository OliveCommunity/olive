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

#ifndef OAKENGINE_COLOR_H
#define OAKENGINE_COLOR_H

#include "export.h"
#include "init.h"
#include "project.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file color.h
 * @brief C ABI for color management (the olive::ColorManager /
 * ColorTransform / ColorProcessor surface)
 *
 * Covers everything the application's display and color-picker paths need
 * without importing an engine C++ symbol:
 *
 *   - OakEngineColorManager: borrowed handle to a project's color manager
 *     (olive::ColorManager). Obtain it with
 *     oakengine_color_manager_from_project(); like the other borrowed
 *     handles it is just the engine pointer reinterpreted and its lifetime
 *     follows the project. All list queries use the index + buf/size
 *     string pattern (the return value of a string getter is the would-be
 *     length excluding the NUL, so buf == NULL queries the size).
 *
 *   - oak_color_transform: POD mirror of olive::ColorTransform. `output`
 *     is the colorspace name when `is_display` is 0, otherwise the display
 *     device name with `view`/`look` selecting the display transform. NULL
 *     strings mean "unset" (the empty QString).
 *
 *   - OakEngineColorProcessor: owned handle wrapping an OCIO-backed
 *     olive::ColorProcessorPtr. Free with
 *     oakengine_color_processor_free(). Color conversion is per-color
 *     (double RGBA in/out); the frame-level GPU path goes through
 *     ColorTransformJob on the engine side.
 *
 *   - OakEngineColorConfig: owned handle to a standalone OCIO config (the
 *     project properties dialog lists the colorspaces of a config file
 *     before applying it).
 *
 * Change notifications (config reloads, reference space changes) are
 * delivered through the event family: subscribe with
 * OAKENGINE_EVENT_COLOR_MANAGER_CONFIG_CHANGED /
 * OAKENGINE_EVENT_COLOR_MANAGER_REFERENCE_SPACE_CHANGED (oakengine/events.h).
 *
 * Error model: functions that can hit an OCIO failure report the reason
 * through oakengine_color_last_error() (thread-local, buf/size
 * convention). List/query functions never fail on a valid handle.
 */

/** @brief Borrowed color manager handle (olive::ColorManager). */
typedef struct OakEngineColorManager OakEngineColorManager;

/** @brief Owned color processor handle; free with oakengine_color_processor_free(). */
typedef struct OakEngineColorProcessor OakEngineColorProcessor;

/** @brief Owned standalone OCIO config handle; free with oakengine_color_config_free(). */
typedef struct OakEngineColorConfig OakEngineColorConfig;

/** @brief Processor direction: input -> output (olive k_normal). */
#define OAKENGINE_COLOR_PROCESSOR_NORMAL 0
/** @brief Processor direction: output -> input (olive k_inverse). */
#define OAKENGINE_COLOR_PROCESSOR_INVERSE 1

/**
 * @brief POD mirror of olive::ColorTransform. Strings are UTF-8; NULL is
 * the unset/empty value.
 */
typedef struct oak_color_transform {
	int is_display; /**< 0: `output` is a colorspace; 1: display/view/look. */
	const char *output; /**< Colorspace name, or display device when is_display. */
	const char *view; /**< Display view (is_display only). */
	const char *look; /**< Display look (is_display only). */
} oak_color_transform;

/**
 * @brief Human-readable reason of the last failed color call on this
 * thread (buf/size convention). Empty when the last call succeeded.
 */
OAKENGINE_API int oakengine_color_last_error(char *buf, int buf_size);

/**
 * @brief The project's color manager (borrowed; NULL for a NULL project or
 * a project without one).
 */
OAKENGINE_API OakEngineColorManager *
oakengine_color_manager_from_project(OakEngineProject *project);

/** @brief Current OCIO config filename of the manager (buf/size). */
OAKENGINE_API int oakengine_color_manager_get_config_filename(
	const OakEngineColorManager *mgr, char *buf, int buf_size);

/**
 * @brief Point the manager at a different OCIO config file
 * (ColorManager::set_config_filename()). OAKENGINE_E_INVALID for NULL
 * args. OCIO load failures surface lazily through the list queries.
 */
OAKENGINE_API int oakengine_color_manager_set_config_filename(
	OakEngineColorManager *mgr, const char *filename);

/** @brief Number of colorspaces in the manager's active config. */
OAKENGINE_API int oakengine_color_manager_colorspace_count(
	const OakEngineColorManager *mgr);

/** @brief Name of the `index`-th colorspace (buf/size); OAKENGINE_E_INVALID out of range. */
OAKENGINE_API int oakengine_color_manager_colorspace_at(
	const OakEngineColorManager *mgr, int index, char *buf, int buf_size);

/** @brief Number of display devices in the active config. */
OAKENGINE_API int oakengine_color_manager_display_count(
	const OakEngineColorManager *mgr);

/** @brief Name of the `index`-th display device (buf/size). */
OAKENGINE_API int oakengine_color_manager_display_at(
	const OakEngineColorManager *mgr, int index, char *buf, int buf_size);

/** @brief Number of views available on `display` (NULL/empty = active display). */
OAKENGINE_API int oakengine_color_manager_view_count(
	const OakEngineColorManager *mgr, const char *display);

/** @brief Name of the `index`-th view on `display` (buf/size). */
OAKENGINE_API int oakengine_color_manager_view_at(
	const OakEngineColorManager *mgr, const char *display, int index,
	char *buf, int buf_size);

/** @brief Number of looks in the active config. */
OAKENGINE_API int oakengine_color_manager_look_count(
	const OakEngineColorManager *mgr);

/** @brief Name of the `index`-th look (buf/size). */
OAKENGINE_API int oakengine_color_manager_look_at(
	const OakEngineColorManager *mgr, int index, char *buf, int buf_size);

/** @brief The config's default display device (buf/size). */
OAKENGINE_API int oakengine_color_manager_default_display(
	const OakEngineColorManager *mgr, char *buf, int buf_size);

/** @brief The config's default view for `display` (buf/size). */
OAKENGINE_API int oakengine_color_manager_default_view(
	const OakEngineColorManager *mgr, const char *display, char *buf,
	int buf_size);

/** @brief The project's default input colorspace (buf/size). */
OAKENGINE_API int oakengine_color_manager_default_input_color_space(
	const OakEngineColorManager *mgr, char *buf, int buf_size);

/** @brief Set the project's default input colorspace. */
OAKENGINE_API int oakengine_color_manager_set_default_input_color_space(
	OakEngineColorManager *mgr, const char *colorspace);

/** @brief The config's reference (scene-linear) colorspace (buf/size). */
OAKENGINE_API int oakengine_color_manager_reference_color_space(
	const OakEngineColorManager *mgr, char *buf, int buf_size);

/**
 * @brief The config's default luma coefficients written to `rgb` (exactly
 * 3 doubles; ColorManager::get_default_luma_coefs()). OAKENGINE_E_INVALID
 * for NULL args.
 */
OAKENGINE_API int oakengine_color_manager_default_luma_coefs(
	const OakEngineColorManager *mgr, double *rgb);

/**
 * @brief Resolve `name` to a colorspace of the active config
 * (ColorManager::get_compliant_color_space(QString); buf/size). Unknown
 * names resolve to the default input colorspace.
 */
OAKENGINE_API int oakengine_color_manager_compliant_color_space(
	const OakEngineColorManager *mgr, const char *name, char *buf,
	int buf_size);

/**
 * @brief Resolve a transform to one the active config supports
 * (ColorManager::get_compliant_color_space(ColorTransform, force_display)).
 *
 * The resolved transform is written into the output buffers; any output
 * pointer may be NULL. Buffers that are too small truncate (NUL-terminated
 * when size > 0). `out_is_display` receives the resolved kind.
 *
 * @return OAKENGINE_OK, or OAKENGINE_E_INVALID for NULL mgr/in.
 */
OAKENGINE_API int oakengine_color_manager_compliant_transform(
	const OakEngineColorManager *mgr, const oak_color_transform *in,
	int force_display, int *out_is_display, char *out_output,
	int output_size, char *out_view, int view_size, char *out_look,
	int look_size);

/**
 * @brief Load the engine's built-in default OCIO config (owned handle).
 * NULL on failure (see oakengine_color_last_error()).
 */
OAKENGINE_API OakEngineColorConfig *oakengine_color_config_load_default(void);

/**
 * @brief Load an OCIO config from `filename` (owned handle). NULL on
 * failure (see oakengine_color_last_error()).
 */
OAKENGINE_API OakEngineColorConfig *
oakengine_color_config_load_file(const char *filename);

/** @brief Release a config handle (NULL-safe no-op). */
OAKENGINE_API void oakengine_color_config_free(OakEngineColorConfig *config);

/** @brief Number of colorspaces in the config. */
OAKENGINE_API int
oakengine_color_config_colorspace_count(const OakEngineColorConfig *config);

/** @brief Name of the `index`-th colorspace in the config (buf/size). */
OAKENGINE_API int oakengine_color_config_colorspace_at(
	const OakEngineColorConfig *config, int index, char *buf, int buf_size);

/**
 * @brief Create a color processor converting from colorspace `input` to
 * the `dest` transform (ColorProcessor::create(); owned handle).
 *
 * `direction` is OAKENGINE_COLOR_PROCESSOR_NORMAL or
 * OAKENGINE_COLOR_PROCESSOR_INVERSE. OCIO failures are non-fatal (matching
 * the engine's C++ behavior): the handle is still returned but
 * oakengine_color_processor_is_valid() reports 0 and conversions are
 * pass-through.
 *
 * @return The handle, or NULL for NULL mgr/input/dest or an unknown
 * direction.
 */
OAKENGINE_API OakEngineColorProcessor *oakengine_color_processor_create(
	const OakEngineColorManager *mgr, const char *input,
	const oak_color_transform *dest, int direction);

/** @brief Release a processor handle (NULL-safe no-op). */
OAKENGINE_API void oakengine_color_processor_free(OakEngineColorProcessor *proc);

/**
 * @brief 1 when the processor holds a valid OCIO processor
 * (ColorProcessor::get_processor() != null), 0 otherwise.
 */
OAKENGINE_API int
oakengine_color_processor_is_valid(const OakEngineColorProcessor *proc);

/**
 * @brief Convert a single RGBA color (ColorProcessor::convert_color()).
 * `in_rgba`/`out_rgba` are 4-double arrays; on an invalid processor the
 * input is copied through.
 *
 * @return OAKENGINE_OK, or OAKENGINE_E_INVALID for NULL args.
 */
OAKENGINE_API int oakengine_color_processor_convert_color(
	const OakEngineColorProcessor *proc, const double *in_rgba,
	double *out_rgba);

/**
 * @brief The OCIO cache id of the processor (ColorProcessor::id();
 * buf/size). Used by display paths to invalidate cached conversions.
 */
OAKENGINE_API int oakengine_color_processor_id(
	const OakEngineColorProcessor *proc, char *buf, int buf_size);

/**
 * @brief Attach a processor to an engine ColorTransformJob
 * (ColorTransformJob::set_color_processor()).
 *
 * Transitional bridge for the display/scopes GPU path until the blit
 * family covers ColorTransformJob: `job` is an
 * olive::ColorTransformJob* the caller owns, passed as void* to keep the
 * C++ type out of the ABI.
 *
 * @return OAKENGINE_OK, or OAKENGINE_E_INVALID for a NULL job.
 */
OAKENGINE_API int oakengine_color_transform_job_set_processor(
	void *job, const OakEngineColorProcessor *proc);

#ifdef __cplusplus
}
#include <QtCore/qmetatype.h>
Q_DECLARE_OPAQUE_POINTER(OakEngineColorManager *)
Q_DECLARE_OPAQUE_POINTER(OakEngineColorProcessor *)
Q_DECLARE_OPAQUE_POINTER(OakEngineColorConfig *)
#endif

#endif /* OAKENGINE_COLOR_H */
