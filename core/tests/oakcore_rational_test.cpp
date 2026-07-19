#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "olive/core/oakcore/rational.h"

int main()
{
	// create_nd reduces and normalizes signs
	OakRational *r = oakcore_rational_create_nd(2, 4);
	assert(r != NULL);
	assert(oakcore_rational_numerator(r) == 1);
	assert(oakcore_rational_denominator(r) == 2);

	OakRational *neg = oakcore_rational_create_nd(1, -2);
	assert(oakcore_rational_numerator(neg) == -1);
	assert(oakcore_rational_denominator(neg) == 2);
	oakcore_rational_free(neg);

	// create defaults to n/1
	OakRational *five = oakcore_rational_create(5);
	assert(oakcore_rational_numerator(five) == 5);
	assert(oakcore_rational_denominator(five) == 1);
	oakcore_rational_free(five);

	// NaN
	OakRational *nan = oakcore_rational_create_nan();
	assert(oakcore_rational_is_nan(nan) == 1);
	assert(oakcore_rational_is_null(nan) == 1);
	assert(isnan(oakcore_rational_to_double(nan)));
	oakcore_rational_free(nan);

	// copy is deep
	OakRational *copy = oakcore_rational_copy(r);
	assert(copy != r);
	assert(oakcore_rational_compare(copy, r) == 0);
	oakcore_rational_add_assign(copy, r);
	assert(oakcore_rational_numerator(copy) == 1);
	assert(oakcore_rational_denominator(copy) == 1);
	assert(oakcore_rational_numerator(r) == 1);
	assert(oakcore_rational_denominator(r) == 2);
	oakcore_rational_free(copy);

	// to_double / to_string
	assert(fabs(oakcore_rational_to_double(r) - 0.5) < 1e-12);
	char buf[32];
	int needed = oakcore_rational_to_string(r, NULL, 0);
	assert(needed == 3);
	assert(oakcore_rational_to_string(r, buf, sizeof(buf)) == 3);
	assert(strcmp(buf, "1/2") == 0);
	assert(oakcore_rational_to_string(r, buf, 2) == 3); // truncated write
	assert(strcmp(buf, "1") == 0);

	// from_double / from_string
	int ok = 0;
	OakRational *half = oakcore_rational_from_double(0.5, &ok);
	assert(ok == 1);
	assert(oakcore_rational_compare(half, r) == 0);
	oakcore_rational_free(half);

	OakRational *bad = oakcore_rational_from_double(NAN, &ok);
	assert(ok == 0);
	assert(oakcore_rational_is_nan(bad));
	oakcore_rational_free(bad);

	OakRational *parsed = oakcore_rational_from_string("3/4", &ok);
	assert(ok == 1);
	assert(fabs(oakcore_rational_to_double(parsed) - 0.75) < 1e-12);
	oakcore_rational_free(parsed);

	OakRational *unparsed = oakcore_rational_from_string("1/2/3", &ok);
	assert(ok == 0);
	oakcore_rational_free(unparsed);

	// flip
	OakRational *flip = oakcore_rational_flipped(r);
	assert(oakcore_rational_numerator(flip) == 2);
	assert(oakcore_rational_denominator(flip) == 1);
	oakcore_rational_flip(flip);
	assert(oakcore_rational_compare(flip, r) == 0);
	oakcore_rational_free(flip);

	// arithmetic assignments
	OakRational *a = oakcore_rational_create_nd(1, 3);
	OakRational *b = oakcore_rational_create_nd(1, 6);
	oakcore_rational_add_assign(a, b);
	assert(oakcore_rational_numerator(a) == 1);
	assert(oakcore_rational_denominator(a) == 2);
	oakcore_rational_sub_assign(a, b);
	assert(oakcore_rational_numerator(a) == 1);
	assert(oakcore_rational_denominator(a) == 3);
	oakcore_rational_mul_assign(a, b);
	assert(oakcore_rational_numerator(a) == 1);
	assert(oakcore_rational_denominator(a) == 18);
	oakcore_rational_div_assign(a, b);
	assert(oakcore_rational_numerator(a) == 1);
	assert(oakcore_rational_denominator(a) == 3);

	// compare ordering
	assert(oakcore_rational_compare(b, a) < 0);
	assert(oakcore_rational_compare(a, b) > 0);

	oakcore_rational_free(a);
	oakcore_rational_free(b);
	oakcore_rational_free(r);

	printf("oakcore_rational_test: all assertions passed\n");
	return 0;
}
