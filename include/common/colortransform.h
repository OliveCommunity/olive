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

#ifndef OAK_EDITOR_COLORTRANSFORM_H
#define OAK_EDITOR_COLORTRANSFORM_H

#include "common/error.h"
#include "common/handle.h"

#ifdef __cplusplus
namespace olive
{
class ColorTransform;
}
extern "C" {
#endif

/**
 * @brief Neutral by-value handle to a color transform description
 *        (olive::ColorTransform).
 *
 * Ownership/count semantics follow the convention in common/handle.h:
 * init functions return a handle whose object has reference count 1,
 * addref(ctx)/release(ctx) adjust it atomically, and release destroys
 * the object at zero. abi_version is always OAKCOMMON_ABI_VERSION.
 */
typedef struct OakColorTransform {
	void *ctx; /**< Opaque pointer to the reference-counted object. */
	void (*addref)(void *ctx); /**< Atomically increments the count. */
	void (*release)(void *ctx); /**< Decrements the count, destroys at 0. */
	uint32_t abi_version; /**< OAKCOMMON_ABI_VERSION. */
} OakColorTransform;

/**
 * @brief Create a plain output-colorspace transform.
 *
 * @param output Output colorspace name. Must not be NULL.
 * @return Handle with reference count 1; ctx is NULL on failure.
 */
OakColorTransform oakcommon_colortransform_init_output(
	const char *output);

/**
 * @brief Create a display/view/look transform.
 *
 * All three strings must not be NULL.
 *
 * @return Handle with reference count 1; ctx is NULL on failure.
 */
OakColorTransform oakcommon_colortransform_init_display(
	const char *display, const char *view, const char *look);

#ifdef __cplusplus
/**
 * @brief Copy a native olive::ColorTransform into a new handle.
 *
 * The source object is deep-copied; the handle does not keep any
 * reference to @p src, which may be destroyed immediately afterwards.
 * Only visible to C++ consumers.
 *
 * @return Handle with reference count 1; ctx is NULL if src is NULL or
 *         on allocation failure.
 */
OakColorTransform oakcommon_colortransform_init_from_native(
	const olive::ColorTransform *src);

/**
 * @brief Borrow the native object behind a handle.
 *
 * The returned pointer is borrowed: it stays valid while the caller
 * holds a reference to the handle (i.e. until the matching release).
 * Only visible to C++ consumers.
 *
 * @return Borrowed pointer, or NULL if transform is NULL or
 *         transform->ctx is NULL.
 */
const olive::ColorTransform *oakcommon_colortransform_get_native(
	OakColorTransform transform);
#endif

/**
 * @brief Release one reference to a transform.
 *
 * Convenience wrapper around handle.release(handle.ctx): decrements the
 * atomic reference count and destroys the object when it reaches zero.
 * No-op when transform is NULL or transform->ctx is NULL.
 */
void oakcommon_colortransform_free(OakColorTransform *transform);

/**
 * @brief Query whether this is a display/view/look transform.
 *
 * @param is_display Receives the result. Must not be NULL.
 * @return OAKCOMMON_OK or a negative OAKCOMMON_E_* error code.
 */
int oakcommon_colortransform_is_display(OakColorTransform transform,
										int *is_display);

/**
 * @brief Get the display name (two-stage string getter).
 *
 * @return Required buffer size in bytes including the terminating NUL
 *         (non-negative), or a negative OAKCOMMON_E_* error code.
 */
int oakcommon_colortransform_get_display(OakColorTransform transform,
										 char *buf, int buf_size);

/**
 * @brief Get the output colorspace name (two-stage string getter).
 *
 * @return Required buffer size in bytes including the terminating NUL
 *         (non-negative), or a negative OAKCOMMON_E_* error code.
 */
int oakcommon_colortransform_get_output(OakColorTransform transform,
										char *buf, int buf_size);

/**
 * @brief Get the view name (two-stage string getter).
 *
 * @return Required buffer size in bytes including the terminating NUL
 *         (non-negative), or a negative OAKCOMMON_E_* error code.
 */
int oakcommon_colortransform_get_view(OakColorTransform transform,
									  char *buf, int buf_size);

/**
 * @brief Get the look name (two-stage string getter).
 *
 * @return Required buffer size in bytes including the terminating NUL
 *         (non-negative), or a negative OAKCOMMON_E_* error code.
 */
int oakcommon_colortransform_get_look(OakColorTransform transform,
									  char *buf, int buf_size);

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_COLORTRANSFORM_H
