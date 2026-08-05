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

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque handle to a color transform description
 *        (olive::ColorTransform).
 */
typedef struct OakCommonColorTransform OakCommonColorTransform;

/**
 * @brief Create a plain output-colorspace transform.
 *
 * @param output Output colorspace name. Must not be NULL.
 * @return Transform handle, or NULL on failure.
 */
OakCommonColorTransform *oakcommon_colortransform_init_output(
	const char *output);

/**
 * @brief Create a display/view/look transform.
 *
 * All three strings must not be NULL.
 *
 * @return Transform handle, or NULL on failure.
 */
OakCommonColorTransform *oakcommon_colortransform_init_display(
	const char *display, const char *view, const char *look);

/**
 * @brief Destroy a transform. No-op on NULL.
 */
void oakcommon_colortransform_free(OakCommonColorTransform *transform);

/**
 * @brief Query whether this is a display/view/look transform.
 *
 * @param is_display Receives the result. Must not be NULL.
 * @return OAKCOMMON_OK or a negative OAKCOMMON_E_* error code.
 */
int oakcommon_colortransform_is_display(OakCommonColorTransform *transform,
										int *is_display);

/**
 * @brief Get the display name (two-stage string getter).
 *
 * @return Required buffer size in bytes including the terminating NUL
 *         (non-negative), or a negative OAKCOMMON_E_* error code.
 */
int oakcommon_colortransform_get_display(OakCommonColorTransform *transform,
										 char *buf, int buf_size);

/**
 * @brief Get the output colorspace name (two-stage string getter).
 *
 * @return Required buffer size in bytes including the terminating NUL
 *         (non-negative), or a negative OAKCOMMON_E_* error code.
 */
int oakcommon_colortransform_get_output(OakCommonColorTransform *transform,
										char *buf, int buf_size);

/**
 * @brief Get the view name (two-stage string getter).
 *
 * @return Required buffer size in bytes including the terminating NUL
 *         (non-negative), or a negative OAKCOMMON_E_* error code.
 */
int oakcommon_colortransform_get_view(OakCommonColorTransform *transform,
									  char *buf, int buf_size);

/**
 * @brief Get the look name (two-stage string getter).
 *
 * @return Required buffer size in bytes including the terminating NUL
 *         (non-negative), or a negative OAKCOMMON_E_* error code.
 */
int oakcommon_colortransform_get_look(OakCommonColorTransform *transform,
									  char *buf, int buf_size);

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_COLORTRANSFORM_H
