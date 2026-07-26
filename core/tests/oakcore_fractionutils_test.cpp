#include <gtest/gtest.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>

#include "oakcore/fractionutils.h"

TEST(OakcoreFractionUtils, ReduceFraction)
{
	int64_t num = 6, den = 9;
	oakcore_fractionutils_reduce_fraction(&num, &den, INT64_MAX);
	EXPECT_EQ(num, 2);
	EXPECT_EQ(den, 3);
}

TEST(OakcoreFractionUtils, ReduceFractionSignNormalization)
{
	{
		int64_t num = -6, den = 9;
		oakcore_fractionutils_reduce_fraction(&num, &den, INT64_MAX);
		EXPECT_EQ(num, -2);
		EXPECT_EQ(den, 3);
	}
	{
		int64_t num = 6, den = -9;
		oakcore_fractionutils_reduce_fraction(&num, &den, INT64_MAX);
		EXPECT_EQ(num, -2);
		EXPECT_EQ(den, 3);
	}
}

TEST(OakcoreFractionUtils, ReduceFractionZeroDenominator)
{
	int64_t num = 42, den = 0;
	oakcore_fractionutils_reduce_fraction(&num, &den, INT64_MAX);
	EXPECT_EQ(num, 0);
	EXPECT_EQ(den, 0);
}

TEST(OakcoreFractionUtils, ReduceFractionZeroNumerator)
{
	int64_t num = 0, den = 7;
	oakcore_fractionutils_reduce_fraction(&num, &den, INT64_MAX);
	EXPECT_EQ(num, 0);
	EXPECT_EQ(den, 1);
}

TEST(OakcoreFractionUtils, ReduceFractionExactWithinMax)
{
	int64_t num = 1000000, den = 1000000;
	oakcore_fractionutils_reduce_fraction(&num, &den, 100);
	EXPECT_EQ(num, 1);
	EXPECT_EQ(den, 1);
}

TEST(OakcoreFractionUtils, ReduceFractionApproximation)
{
	int64_t num = 1048576, den = 1000000;
	oakcore_fractionutils_reduce_fraction(&num, &den, 10000);
	EXPECT_GT(num, 0);
	EXPECT_LE(num, 10000);
	EXPECT_GT(den, 0);
	EXPECT_LE(den, 10000);
	const double approx = double(num) / double(den);
	EXPECT_NEAR(approx, 1.048576, 0.001);
}

TEST(OakcoreFractionUtils, CompareFractions)
{
	EXPECT_EQ(oakcore_fractionutils_compare_fractions(1, 3, 1, 2), -1);
	EXPECT_EQ(oakcore_fractionutils_compare_fractions(1, 2, 2, 4), 0);
	EXPECT_EQ(oakcore_fractionutils_compare_fractions(2, 3, 1, 2), 1);
	EXPECT_EQ(oakcore_fractionutils_compare_fractions(0, 0, 0, 5), INT_MIN);
}

TEST(OakcoreFractionUtils, RescaleRndExact)
{
	EXPECT_EQ(oakcore_fractionutils_rescale_rnd(100, 3, 4,
												OAK_FRACTION_ROUNDING_NEAR_INF), 75);
}

TEST(OakcoreFractionUtils, RescaleRndNearInf)
{
	EXPECT_EQ(oakcore_fractionutils_rescale_rnd(5, 1, 2,
												OAK_FRACTION_ROUNDING_NEAR_INF), 3);
	EXPECT_EQ(oakcore_fractionutils_rescale_rnd(-5, 1, 2,
												OAK_FRACTION_ROUNDING_NEAR_INF), -3);
	EXPECT_EQ(oakcore_fractionutils_rescale_rnd(7, 1, 2,
												OAK_FRACTION_ROUNDING_NEAR_INF), 4);
}

TEST(OakcoreFractionUtils, RescaleRndUp)
{
	EXPECT_EQ(oakcore_fractionutils_rescale_rnd(5, 1, 2,
												OAK_FRACTION_ROUNDING_UP), 3);
	EXPECT_EQ(oakcore_fractionutils_rescale_rnd(-5, 1, 2,
												OAK_FRACTION_ROUNDING_UP), -2);
	EXPECT_EQ(oakcore_fractionutils_rescale_rnd(4, 1, 2,
												OAK_FRACTION_ROUNDING_UP), 2);
}

TEST(OakcoreFractionUtils, RescaleRndNegativeDivisor)
{
	EXPECT_EQ(oakcore_fractionutils_rescale_rnd(10, 1, -2,
												OAK_FRACTION_ROUNDING_NEAR_INF), -5);
}

TEST(OakcoreFractionUtils, RescaleRndLargeIntermediates)
{
	EXPECT_EQ(oakcore_fractionutils_rescale_rnd(INT64_MAX / 2, 4, 2,
												OAK_FRACTION_ROUNDING_NEAR_INF),
			  INT64_MAX - 1);
}
