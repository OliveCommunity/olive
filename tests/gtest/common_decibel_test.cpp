#include <gtest/gtest.h>

#include "common/decibel.h"

TEST(CommonDecibel, FromLinearZeroReturnsMinimum)
{
	EXPECT_DOUBLE_EQ(olive::Decibel::from_linear(0.0), olive::Decibel::minimum);
}

TEST(CommonDecibel, FromLinearOneReturnsZero)
{
	EXPECT_DOUBLE_EQ(olive::Decibel::from_linear(1.0), 0.0);
}

TEST(CommonDecibel, FromLinearTenReturnsTwenty)
{
	EXPECT_DOUBLE_EQ(olive::Decibel::from_linear(10.0), 20.0);
}

TEST(CommonDecibel, ToLinearZeroReturnsOne)
{
	EXPECT_DOUBLE_EQ(olive::Decibel::to_linear(0.0), 1.0);
}

TEST(CommonDecibel, ToLinearMinimumReturnsZero)
{
	EXPECT_DOUBLE_EQ(olive::Decibel::to_linear(olive::Decibel::minimum), 0.0);
}

TEST(CommonDecibel, ToLinearTwentyReturnsTen)
{
	EXPECT_DOUBLE_EQ(olive::Decibel::to_linear(20.0), 10.0);
}

TEST(CommonDecibel, FromLogarithmicAtEdges)
{
	EXPECT_DOUBLE_EQ(olive::Decibel::from_logarithmic(0.0),
					 olive::Decibel::minimum);
	EXPECT_DOUBLE_EQ(olive::Decibel::from_logarithmic(1.0), 0.0);
}

TEST(CommonDecibel, ToLogarithmicAtEdges)
{
	EXPECT_DOUBLE_EQ(olive::Decibel::to_logarithmic(0.0), 1.0);
}

TEST(CommonDecibel, LinearLogarithmicRoundTrip)
{
	EXPECT_NEAR(olive::Decibel::logarithmic_to_linear(
					olive::Decibel::linear_to_logarithmic(0.5)),
				0.5, 1e-6);
}
