/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2023 Olive Studios LLC
  Modifications Copyright (C) 2026 Oak Team

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

#ifndef OAK_LIBOLIVECORE_FRACTIONUTILS_H
#define OAK_LIBOLIVECORE_FRACTIONUTILS_H

#include <stdint.h>

#include "olive/core/oakcore/fractionutils.h"

namespace olive::core
{

/**
 * @brief Rounding modes for rescale_rnd()
 *
 * Mirrors the FFmpeg AVRounding modes that this codebase used before the
 * FFmpeg dependency was removed from core.
 *
 * Consumer-side wrapper over the liboakcore C ABI: every function forwards
 * across the C boundary. The public API is unchanged from the original
 * implementation.
 */
enum class FractionRounding {
	/**
	 * Round to the nearest value; halfway cases are rounded away from zero.
	 * Equivalent to FFmpeg's AV_ROUND_NEAR_INF.
	 */
	k_near_inf,

	/**
	 * Round toward positive infinity. Equivalent to FFmpeg's AV_ROUND_UP.
	 */
	k_up
};

/**
 * @brief Reduce a fraction so that numerator and denominator fit within `max`
 *
 * Native re-implementation of FFmpeg's av_reduce(): divides out the greatest
 * common divisor and, if the values still do not fit within `max`, finds the
 * closest approximation using continued fractions.
 *
 * A zero denominator is preserved (with the numerator set to zero).
 */
inline void reduce_fraction(int64_t &num, int64_t &den, int64_t max)
{
	oakcore_fractionutils_reduce_fraction(&num, &den, max);
}

/**
 * @brief Compare two fractions
 *
 * Native re-implementation of FFmpeg's av_cmp_q(): returns -1 if a < b,
 * 0 if a == b, 1 if a > b, and INT_MIN when the comparison is meaningless
 * (degenerate zero-denominator fractions).
 */
inline int compare_fractions(int an, int ad, int bn, int bd)
{
	return oakcore_fractionutils_compare_fractions(an, ad, bn, bd);
}

/**
 * @brief Rescale `a` by the fraction b/c: returns a * b / c
 *
 * Native re-implementation of FFmpeg's av_rescale_rnd(). The intermediate
 * product is computed with 128-bit arithmetic where available so that no
 * precision is lost for large timestamps.
 */
inline int64_t rescale_rnd(int64_t a, int64_t b, int64_t c, FractionRounding rnd)
{
	return oakcore_fractionutils_rescale_rnd(a, b, c,
											 static_cast<OakFractionRounding>(rnd));
}

}

#endif // OAK_LIBOLIVECORE_FRACTIONUTILS_H
