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

/**
 * @file oakcore_timerange_test.cpp
 * @brief Smoke test for the OakTimeRange C ABI
 *
 * Pure C API test: no gtest, no C++ wrapper classes. Every allocated handle
 * is released with the matching free function.
 */

#include <assert.h>
#include <limits.h>
#include <stdio.h>

#include "olive/core/oakcore/timerange.h"

static OakRational *mk_time(int n)
{
	return oakcore_rational_create(n);
}

static OakTimeRange *mk_range(int in, int out)
{
	OakRational *ri = mk_time(in);
	OakRational *ro = mk_time(out);
	OakTimeRange *r = oakcore_timerange_create_io(ri, ro);
	oakcore_rational_free(ri);
	oakcore_rational_free(ro);
	return r;
}

static void expect_int_time(const OakRational *r, int expected)
{
	assert(oakcore_rational_numerator(r) == expected);
	assert(oakcore_rational_denominator(r) == 1);
}

static void expect_range(const OakTimeRange *r, int in, int out)
{
	OakRational *ri = oakcore_timerange_in(r);
	OakRational *ro = oakcore_timerange_out(r);
	expect_int_time(ri, in);
	expect_int_time(ro, out);
	oakcore_rational_free(ri);
	oakcore_rational_free(ro);
}

static void test_create_default(void)
{
	OakTimeRange *r = oakcore_timerange_create();
	expect_range(r, 0, 0);

	OakRational *length = oakcore_timerange_length(r);
	expect_int_time(length, 0);
	oakcore_rational_free(length);

	oakcore_timerange_free(r);
}

static void test_create_and_getters(void)
{
	OakTimeRange *r = mk_range(10, 20);
	expect_range(r, 10, 20);

	OakRational *length = oakcore_timerange_length(r);
	expect_int_time(length, 10);
	oakcore_rational_free(length);

	oakcore_timerange_free(r);
}

static void test_normalize_swaps_in_out(void)
{
	// out earlier than in must be swapped
	OakTimeRange *r = mk_range(20, 10);
	expect_range(r, 10, 20);

	OakRational *length = oakcore_timerange_length(r);
	expect_int_time(length, 10);
	oakcore_rational_free(length);

	oakcore_timerange_free(r);
}

static void test_length_nan_on_extremes(void)
{
	// RATIONAL_MIN/RATIONAL_MAX endpoints make the length NaN
	OakTimeRange *r = mk_range(INT_MIN, 0);
	OakRational *length = oakcore_timerange_length(r);
	assert(oakcore_rational_is_nan(length) == 1);
	oakcore_rational_free(length);
	oakcore_timerange_free(r);

	r = mk_range(0, INT_MAX);
	length = oakcore_timerange_length(r);
	assert(oakcore_rational_is_nan(length) == 1);
	oakcore_rational_free(length);
	oakcore_timerange_free(r);
}

static void test_copy_and_equal(void)
{
	OakTimeRange *r = mk_range(3, 7);
	OakTimeRange *copy = oakcore_timerange_copy(r);

	assert(oakcore_timerange_equal(r, copy) == 1);
	expect_range(copy, 3, 7);

	// Mutating the copy must not affect the original (deep ownership)
	OakRational *nine = mk_time(9);
	oakcore_timerange_set_in(copy, nine);
	oakcore_rational_free(nine);

	// set_in(9) with out==7 normalizes by swapping to (7, 9)
	expect_range(copy, 7, 9);
	assert(oakcore_timerange_equal(r, copy) == 0);
	expect_range(r, 3, 7);

	oakcore_timerange_free(copy);
	oakcore_timerange_free(r);
}

static void test_setters(void)
{
	OakTimeRange *r = mk_range(0, 10);
	OakRational *t = mk_time(5);

	oakcore_timerange_set_in(r, t);
	expect_range(r, 5, 10);

	oakcore_timerange_set_out(r, t);
	// set_out(5) with in==5 collapses to a zero-length range
	expect_range(r, 5, 5);

	{
		OakRational *in = mk_time(30);
		OakRational *out = mk_time(20);
		oakcore_timerange_set_range(r, in, out);
		oakcore_rational_free(in);
		oakcore_rational_free(out);
	}
	// set_range also normalizes
	expect_range(r, 20, 30);

	oakcore_rational_free(t);
	oakcore_timerange_free(r);
}

static void test_overlaps_with(void)
{
	OakTimeRange *a = mk_range(0, 10);
	OakTimeRange *b = mk_range(10, 20);
	OakTimeRange *c = mk_range(5, 15);

	// Touching ranges (0,10) vs (10,20): in_inclusive governs the
	// other.out-vs-self.in comparison, out_inclusive the other.in-vs-self.out
	// one, so the results are asymmetric for mixed flags
	assert(oakcore_timerange_overlaps_with(a, b, 1, 1) == 1);
	assert(oakcore_timerange_overlaps_with(a, b, 0, 0) == 0);
	assert(oakcore_timerange_overlaps_with(a, b, 1, 0) == 0);
	assert(oakcore_timerange_overlaps_with(a, b, 0, 1) == 1);

	// Genuinely overlapping ranges overlap under any inclusivity
	assert(oakcore_timerange_overlaps_with(a, c, 1, 1) == 1);
	assert(oakcore_timerange_overlaps_with(a, c, 0, 0) == 1);

	oakcore_timerange_free(c);
	oakcore_timerange_free(b);
	oakcore_timerange_free(a);
}

static void test_contains_range(void)
{
	OakTimeRange *outer = mk_range(0, 30);
	OakTimeRange *inner = mk_range(5, 10);
	OakTimeRange *same = mk_range(0, 30);
	OakTimeRange *partial = mk_range(5, 40);

	assert(oakcore_timerange_contains_range(outer, inner, 1, 1) == 1);
	assert(oakcore_timerange_contains_range(outer, inner, 0, 0) == 1);

	// Identical ranges: contained inclusively, not exclusively
	assert(oakcore_timerange_contains_range(outer, same, 1, 1) == 1);
	assert(oakcore_timerange_contains_range(outer, same, 0, 0) == 0);

	assert(oakcore_timerange_contains_range(outer, partial, 1, 1) == 0);
	assert(oakcore_timerange_contains_range(inner, outer, 1, 1) == 0);

	oakcore_timerange_free(partial);
	oakcore_timerange_free(same);
	oakcore_timerange_free(inner);
	oakcore_timerange_free(outer);
}

static void test_contains_time(void)
{
	OakTimeRange *r = mk_range(0, 10);
	OakRational *inside = mk_time(5);
	OakRational *edge_in = mk_time(0);
	OakRational *edge_out = mk_time(10);
	OakRational *outside = mk_time(-1);

	// contains(time) is in-inclusive but out-exclusive
	assert(oakcore_timerange_contains_time(r, inside) == 1);
	assert(oakcore_timerange_contains_time(r, edge_in) == 1);
	assert(oakcore_timerange_contains_time(r, edge_out) == 0);
	assert(oakcore_timerange_contains_time(r, outside) == 0);

	oakcore_rational_free(outside);
	oakcore_rational_free(edge_out);
	oakcore_rational_free(edge_in);
	oakcore_rational_free(inside);
	oakcore_timerange_free(r);
}

static void test_combine_and_intersect(void)
{
	OakTimeRange *a = mk_range(0, 10);
	OakTimeRange *b = mk_range(20, 30);

	OakTimeRange *combined = oakcore_timerange_combined(a, b);
	expect_range(combined, 0, 30);
	oakcore_timerange_free(combined);

	combined = oakcore_timerange_combine(b, a);
	expect_range(combined, 0, 30);
	oakcore_timerange_free(combined);

	// Disjoint ranges intersect to (20, 10), which normalize() swaps to (10, 20)
	OakTimeRange *intersected = oakcore_timerange_intersected(a, b);
	expect_range(intersected, 10, 20);
	oakcore_timerange_free(intersected);

	oakcore_timerange_free(b);
	oakcore_timerange_free(a);

	a = mk_range(0, 20);
	b = mk_range(10, 30);

	intersected = oakcore_timerange_intersected(a, b);
	expect_range(intersected, 10, 20);
	oakcore_timerange_free(intersected);

	intersected = oakcore_timerange_intersect(b, a);
	expect_range(intersected, 10, 20);
	oakcore_timerange_free(intersected);

	oakcore_timerange_free(b);
	oakcore_timerange_free(a);
}

static void test_arithmetic(void)
{
	OakTimeRange *r = mk_range(0, 10);
	OakRational *five = mk_time(5);

	OakTimeRange *shifted = oakcore_timerange_add(r, five);
	expect_range(shifted, 5, 15);

	OakTimeRange *back = oakcore_timerange_sub(shifted, five);
	expect_range(back, 0, 10);
	oakcore_timerange_free(back);
	oakcore_timerange_free(shifted);

	oakcore_timerange_add_assign(r, five);
	expect_range(r, 5, 15);

	oakcore_timerange_sub_assign(r, five);
	expect_range(r, 0, 10);

	OakRational *length = oakcore_timerange_length(r);
	expect_int_time(length, 10);
	oakcore_rational_free(length);

	oakcore_rational_free(five);
	oakcore_timerange_free(r);
}

static void test_split(void)
{
	OakTimeRange *r = mk_range(0, 10);

	// Size query, both directly and through the NULL-buffer form
	assert(oakcore_timerange_split_count(r, 4) == 3);
	assert(oakcore_timerange_split(r, 4, NULL, 0) == 3);

	// Full split: (0,4), (4,8), (8,10)
	OakTimeRange *chunks[3];
	assert(oakcore_timerange_split(r, 4, chunks, 3) == 3);
	expect_range(chunks[0], 0, 4);
	expect_range(chunks[1], 4, 8);
	expect_range(chunks[2], 8, 10);
	for (int i = 0; i < 3; i++) {
		oakcore_timerange_free(chunks[i]);
	}

	// A too-small buffer receives a prefix, the return value is the total
	OakTimeRange *prefix[2];
	assert(oakcore_timerange_split(r, 4, prefix, 2) == 3);
	expect_range(prefix[0], 0, 4);
	expect_range(prefix[1], 4, 8);
	oakcore_timerange_free(prefix[0]);
	oakcore_timerange_free(prefix[1]);

	// Zero-length range splits into exactly one chunk
	OakTimeRange *zero = mk_range(5, 5);
	assert(oakcore_timerange_split_count(zero, 4) == 1);
	OakTimeRange *single = NULL;
	assert(oakcore_timerange_split(zero, 4, &single, 1) == 1);
	expect_range(single, 5, 5);
	oakcore_timerange_free(single);
	oakcore_timerange_free(zero);

	oakcore_timerange_free(r);
}

int main(void)
{
	test_create_default();
	test_create_and_getters();
	test_normalize_swaps_in_out();
	test_length_nan_on_extremes();
	test_copy_and_equal();
	test_setters();
	test_overlaps_with();
	test_contains_range();
	test_contains_time();
	test_combine_and_intersect();
	test_arithmetic();
	test_split();

	printf("oakcore_timerange_test: all tests passed\n");
	return 0;
}
