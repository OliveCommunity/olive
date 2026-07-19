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

#ifndef OAKCORE_FRACTIONUTILS_H
#define OAKCORE_FRACTIONUTILS_H

#include <stdint.h>

#include "export.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file fractionutils.h
 * @brief C ABI for the fraction utility functions
 *
 * Pure integer helpers: there is no object and therefore no opaque handle.
 * In/out values are passed through pointer out-parameters.
 */

/**
 * @brief Rounding modes for oakcore_fractionutils_rescale_rnd()
 *
 * Mirrors the FFmpeg AVRounding modes: NEAR_INF rounds to the nearest value
 * with halfway cases rounded away from zero, UP rounds toward positive
 * infinity.
 */
typedef enum OakFractionRounding {
	OAK_FRACTION_ROUNDING_NEAR_INF = 0,
	OAK_FRACTION_ROUNDING_UP = 1
} OakFractionRounding;

/**
 * @brief Reduces the fraction *num / *den in place so that both fit within max
 *
 * Divides out the greatest common divisor and, if the values still do not
 * fit within max, finds the closest approximation using continued fractions.
 *
 * A zero denominator is preserved (with the numerator set to zero).
 * num and den must not be NULL.
 */
OAKCORE_API void oakcore_fractionutils_reduce_fraction(int64_t *num,
													   int64_t *den,
													   int64_t max);

/**
 * @brief Three-way comparison of the fractions an/ad and bn/bd
 *
 * Returns -1 if a < b, 0 if a == b, 1 if a > b, and INT_MIN when the
 * comparison is meaningless (degenerate zero-denominator fractions).
 */
OAKCORE_API int oakcore_fractionutils_compare_fractions(int an, int ad,
														int bn, int bd);

/**
 * @brief Rescales a by the fraction b/c: returns a * b / c
 *
 * The intermediate product is computed with 128-bit arithmetic where
 * available so that no precision is lost for large timestamps.
 */
OAKCORE_API int64_t oakcore_fractionutils_rescale_rnd(int64_t a, int64_t b,
													  int64_t c,
													  OakFractionRounding rnd);

#ifdef __cplusplus
}
#endif

#endif /* OAKCORE_FRACTIONUTILS_H */
