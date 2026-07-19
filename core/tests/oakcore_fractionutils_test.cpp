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

#include "oakcore/fractionutils.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

/**
 * @file oakcore_fractionutils_test.cpp
 * @brief Pure C API test for the fraction utility functions
 *
 * Exercises every oakcore_fractionutils_* function directly, without the
 * C++ wrapper and without a test framework.
 */
int main()
{
	// reduce_fraction: greatest common divisor is divided out
	{
		int64_t num = 6, den = 9;
		oakcore_fractionutils_reduce_fraction(&num, &den, INT64_MAX);
		assert(num == 2 && den == 3);
	}

	// reduce_fraction: the sign is normalized into the numerator
	{
		int64_t num = -6, den = 9;
		oakcore_fractionutils_reduce_fraction(&num, &den, INT64_MAX);
		assert(num == -2 && den == 3);
	}
	{
		int64_t num = 6, den = -9;
		oakcore_fractionutils_reduce_fraction(&num, &den, INT64_MAX);
		assert(num == -2 && den == 3);
	}

	// reduce_fraction: a zero denominator is preserved, numerator zeroed
	{
		int64_t num = 42, den = 0;
		oakcore_fractionutils_reduce_fraction(&num, &den, INT64_MAX);
		assert(num == 0 && den == 0);
	}

	// reduce_fraction: a zero numerator reduces to 0/1
	{
		int64_t num = 0, den = 7;
		oakcore_fractionutils_reduce_fraction(&num, &den, INT64_MAX);
		assert(num == 0 && den == 1);
	}

	// reduce_fraction: exact reduction well within max
	{
		int64_t num = 1000000, den = 1000000;
		oakcore_fractionutils_reduce_fraction(&num, &den, 100);
		assert(num == 1 && den == 1);
	}

	// reduce_fraction: continued-fraction approximation respects max
	{
		int64_t num = 1048576, den = 1000000;
		oakcore_fractionutils_reduce_fraction(&num, &den, 10000);
		assert(num > 0 && num <= 10000 && den > 0 && den <= 10000);
		const double approx = double(num) / double(den);
		assert(fabs(approx - 1.048576) < 0.001);
	}

	// compare_fractions: three-way result -1 / 0 / 1
	assert(oakcore_fractionutils_compare_fractions(1, 3, 1, 2) == -1);
	assert(oakcore_fractionutils_compare_fractions(1, 2, 2, 4) == 0);
	assert(oakcore_fractionutils_compare_fractions(2, 3, 1, 2) == 1);

	// compare_fractions: meaningless degenerate comparison returns INT_MIN
	assert(oakcore_fractionutils_compare_fractions(0, 0, 0, 5) == INT_MIN);

	// rescale_rnd: exact division needs no rounding
	assert(oakcore_fractionutils_rescale_rnd(100, 3, 4,
											 OAK_FRACTION_ROUNDING_NEAR_INF) == 75);

	// rescale_rnd: round to nearest, halfway cases away from zero
	assert(oakcore_fractionutils_rescale_rnd(5, 1, 2,
											 OAK_FRACTION_ROUNDING_NEAR_INF) == 3);
	assert(oakcore_fractionutils_rescale_rnd(-5, 1, 2,
											 OAK_FRACTION_ROUNDING_NEAR_INF) == -3);
	assert(oakcore_fractionutils_rescale_rnd(7, 1, 2,
											 OAK_FRACTION_ROUNDING_NEAR_INF) == 4);

	// rescale_rnd: round toward positive infinity
	assert(oakcore_fractionutils_rescale_rnd(5, 1, 2,
											 OAK_FRACTION_ROUNDING_UP) == 3);
	assert(oakcore_fractionutils_rescale_rnd(-5, 1, 2,
											 OAK_FRACTION_ROUNDING_UP) == -2);
	assert(oakcore_fractionutils_rescale_rnd(4, 1, 2,
											 OAK_FRACTION_ROUNDING_UP) == 2);

	// rescale_rnd: a negative divisor is normalized into the multiplier
	assert(oakcore_fractionutils_rescale_rnd(10, 1, -2,
											 OAK_FRACTION_ROUNDING_NEAR_INF) == -5);

	// rescale_rnd: large intermediates stay exact (128-bit path)
	assert(oakcore_fractionutils_rescale_rnd(INT64_MAX / 2, 4, 2,
											 OAK_FRACTION_ROUNDING_NEAR_INF)
		   == INT64_MAX - 1);

	printf("oakcore_fractionutils_test: all assertions passed\n");
	return 0;
}
