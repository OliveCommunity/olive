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

#ifndef OAKCORE_RATIONAL_H
#define OAKCORE_RATIONAL_H

#include "export.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file rational.h
 * @brief C ABI for the rational number value type
 *
 * Opaque handle + free functions. All returned OakRational* are owned by
 * the caller and must be released with oakcore_rational_free().
 */
typedef struct OakRational OakRational;

OAKCORE_API OakRational *oakcore_rational_create(int numerator);
OAKCORE_API OakRational *oakcore_rational_create_nd(int numerator,
													int denominator);
OAKCORE_API OakRational *oakcore_rational_create_nan(void);
OAKCORE_API OakRational *oakcore_rational_copy(const OakRational *self);
OAKCORE_API void oakcore_rational_free(OakRational *self);

OAKCORE_API int oakcore_rational_numerator(const OakRational *self);
OAKCORE_API int oakcore_rational_denominator(const OakRational *self);
OAKCORE_API double oakcore_rational_to_double(const OakRational *self);

/**
 * Writes "num/den" into buf (NUL-terminated when buf_size > 0).
 * Returns the number of characters that would have been written excluding
 * the NUL, so buf == NULL or a too-small buffer can be used to query the
 * required size.
 */
OAKCORE_API int oakcore_rational_to_string(const OakRational *self, char *buf,
										   int buf_size);

OAKCORE_API OakRational *oakcore_rational_from_double(double value, int *ok);
OAKCORE_API OakRational *oakcore_rational_from_string(const char *str, int *ok);

OAKCORE_API int oakcore_rational_is_null(const OakRational *self);
OAKCORE_API int oakcore_rational_is_nan(const OakRational *self);

OAKCORE_API OakRational *oakcore_rational_flipped(const OakRational *self);
OAKCORE_API void oakcore_rational_flip(OakRational *self);

OAKCORE_API void oakcore_rational_add_assign(OakRational *self,
											 const OakRational *other);
OAKCORE_API void oakcore_rational_sub_assign(OakRational *self,
											 const OakRational *other);
OAKCORE_API void oakcore_rational_mul_assign(OakRational *self,
											 const OakRational *other);
OAKCORE_API void oakcore_rational_div_assign(OakRational *self,
											 const OakRational *other);

/**
 * Three-way comparison like compare_fractions: -1, 0 or 1.
 */
OAKCORE_API int oakcore_rational_compare(const OakRational *self,
										 const OakRational *other);

#ifdef __cplusplus
}
#endif

#endif /* OAKCORE_RATIONAL_H */
