#include <gtest/gtest.h>
#include <math.h>
#include <string.h>

#include "olive/core/oakcore/rational.h"

TEST(OakcoreRational, CreateNdReducesAndNormalizesSigns)
{
	OakRational *r = oakcore_rational_create_nd(2, 4);
	ASSERT_NE(r, nullptr);
	EXPECT_EQ(oakcore_rational_numerator(r), 1);
	EXPECT_EQ(oakcore_rational_denominator(r), 2);

	OakRational *neg = oakcore_rational_create_nd(1, -2);
	EXPECT_EQ(oakcore_rational_numerator(neg), -1);
	EXPECT_EQ(oakcore_rational_denominator(neg), 2);
	oakcore_rational_free(neg);
	oakcore_rational_free(r);
}

TEST(OakcoreRational, CreateDefaultsToNOver1)
{
	OakRational *five = oakcore_rational_create(5);
	EXPECT_EQ(oakcore_rational_numerator(five), 5);
	EXPECT_EQ(oakcore_rational_denominator(five), 1);
	oakcore_rational_free(five);
}

TEST(OakcoreRational, NaN)
{
	OakRational *nan = oakcore_rational_create_nan();
	EXPECT_EQ(oakcore_rational_is_nan(nan), 1);
	EXPECT_EQ(oakcore_rational_is_null(nan), 1);
	EXPECT_TRUE(isnan(oakcore_rational_to_double(nan)));
	oakcore_rational_free(nan);
}

TEST(OakcoreRational, CopyIsDeep)
{
	OakRational *r = oakcore_rational_create_nd(1, 2);
	OakRational *copy = oakcore_rational_copy(r);
	EXPECT_NE(copy, r);
	EXPECT_EQ(oakcore_rational_compare(copy, r), 0);

	oakcore_rational_add_assign(copy, r);
	EXPECT_EQ(oakcore_rational_numerator(copy), 1);
	EXPECT_EQ(oakcore_rational_denominator(copy), 1);
	// Original unchanged
	EXPECT_EQ(oakcore_rational_numerator(r), 1);
	EXPECT_EQ(oakcore_rational_denominator(r), 2);

	oakcore_rational_free(copy);
	oakcore_rational_free(r);
}

TEST(OakcoreRational, ToDoubleAndToString)
{
	OakRational *r = oakcore_rational_create_nd(1, 2);
	EXPECT_NEAR(oakcore_rational_to_double(r), 0.5, 1e-12);

	EXPECT_EQ(oakcore_rational_to_string(r, NULL, 0), 3);

	char buf[32];
	EXPECT_EQ(oakcore_rational_to_string(r, buf, sizeof(buf)), 3);
	EXPECT_STREQ(buf, "1/2");

	// Truncated write
	EXPECT_EQ(oakcore_rational_to_string(r, buf, 2), 3);
	EXPECT_STREQ(buf, "1");

	oakcore_rational_free(r);
}

TEST(OakcoreRational, FromDoubleAndFromString)
{
	int ok = 0;
	OakRational *r = oakcore_rational_create_nd(1, 2);

	OakRational *half = oakcore_rational_from_double(0.5, &ok);
	EXPECT_EQ(ok, 1);
	EXPECT_EQ(oakcore_rational_compare(half, r), 0);
	oakcore_rational_free(half);

	OakRational *bad = oakcore_rational_from_double(NAN, &ok);
	EXPECT_EQ(ok, 0);
	EXPECT_TRUE(oakcore_rational_is_nan(bad));
	oakcore_rational_free(bad);

	OakRational *parsed = oakcore_rational_from_string("3/4", &ok);
	EXPECT_EQ(ok, 1);
	EXPECT_NEAR(oakcore_rational_to_double(parsed), 0.75, 1e-12);
	oakcore_rational_free(parsed);

	OakRational *unparsed = oakcore_rational_from_string("1/2/3", &ok);
	EXPECT_EQ(ok, 0);
	oakcore_rational_free(unparsed);

	oakcore_rational_free(r);
}

TEST(OakcoreRational, Flip)
{
	OakRational *r = oakcore_rational_create_nd(1, 2);
	OakRational *flip = oakcore_rational_flipped(r);
	EXPECT_EQ(oakcore_rational_numerator(flip), 2);
	EXPECT_EQ(oakcore_rational_denominator(flip), 1);

	oakcore_rational_flip(flip);
	EXPECT_EQ(oakcore_rational_compare(flip, r), 0);

	oakcore_rational_free(flip);
	oakcore_rational_free(r);
}

TEST(OakcoreRational, ArithmeticAssignments)
{
	OakRational *a = oakcore_rational_create_nd(1, 3);
	OakRational *b = oakcore_rational_create_nd(1, 6);

	oakcore_rational_add_assign(a, b);
	EXPECT_EQ(oakcore_rational_numerator(a), 1);
	EXPECT_EQ(oakcore_rational_denominator(a), 2);

	oakcore_rational_sub_assign(a, b);
	EXPECT_EQ(oakcore_rational_numerator(a), 1);
	EXPECT_EQ(oakcore_rational_denominator(a), 3);

	oakcore_rational_mul_assign(a, b);
	EXPECT_EQ(oakcore_rational_numerator(a), 1);
	EXPECT_EQ(oakcore_rational_denominator(a), 18);

	oakcore_rational_div_assign(a, b);
	EXPECT_EQ(oakcore_rational_numerator(a), 1);
	EXPECT_EQ(oakcore_rational_denominator(a), 3);

	// Compare ordering
	EXPECT_LT(oakcore_rational_compare(b, a), 0);
	EXPECT_GT(oakcore_rational_compare(a, b), 0);

	oakcore_rational_free(a);
	oakcore_rational_free(b);
}
