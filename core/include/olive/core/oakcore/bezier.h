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

#ifndef OAKCORE_BEZIER_H
#define OAKCORE_BEZIER_H

#include "export.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file bezier.h
 * @brief C ABI for the keyframe easing bezier curve value type
 *
 * Opaque handle + free functions. All returned OakBezier* are owned by
 * the caller and must be released with oakcore_bezier_free().
 */
typedef struct OakBezier OakBezier;

OAKCORE_API OakBezier *oakcore_bezier_create(void);
OAKCORE_API OakBezier *oakcore_bezier_create_xy(double x, double y);
OAKCORE_API OakBezier *oakcore_bezier_create_full(double x, double y,
												  double cp1_x, double cp1_y,
												  double cp2_x, double cp2_y);
OAKCORE_API OakBezier *oakcore_bezier_copy(const OakBezier *self);
OAKCORE_API void oakcore_bezier_free(OakBezier *self);

OAKCORE_API double oakcore_bezier_x(const OakBezier *self);
OAKCORE_API double oakcore_bezier_y(const OakBezier *self);
OAKCORE_API double oakcore_bezier_cp1_x(const OakBezier *self);
OAKCORE_API double oakcore_bezier_cp1_y(const OakBezier *self);
OAKCORE_API double oakcore_bezier_cp2_x(const OakBezier *self);
OAKCORE_API double oakcore_bezier_cp2_y(const OakBezier *self);

OAKCORE_API void oakcore_bezier_set_x(OakBezier *self, double x);
OAKCORE_API void oakcore_bezier_set_y(OakBezier *self, double y);
OAKCORE_API void oakcore_bezier_set_cp1_x(OakBezier *self, double cp1_x);
OAKCORE_API void oakcore_bezier_set_cp1_y(OakBezier *self, double cp1_y);
OAKCORE_API void oakcore_bezier_set_cp2_x(OakBezier *self, double cp2_x);
OAKCORE_API void oakcore_bezier_set_cp2_y(OakBezier *self, double cp2_y);

OAKCORE_API double oakcore_bezier_quadratic_xto_t(double x, double a, double b,
												  double c);
OAKCORE_API double oakcore_bezier_quadratic_tto_y(double a, double b, double c,
												  double t);
OAKCORE_API double oakcore_bezier_cubic_xto_t(double x, double a, double b,
											  double c, double d);
OAKCORE_API double oakcore_bezier_cubic_tto_y(double a, double b, double c,
											  double d, double t);

#ifdef __cplusplus
}
#endif

#endif /* OAKCORE_BEZIER_H */
