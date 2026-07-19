#include <gtest/gtest.h>

#include "common/range.h"

TEST(CommonRange, InRangeExact)
{
	EXPECT_TRUE(in_range(5, 5, 0));
}

TEST(CommonRange, InRangeWithinTolerance)
{
	EXPECT_TRUE(in_range(5.0, 5.5, 1.0));
	EXPECT_TRUE(in_range(5.0, 4.5, 1.0));
}

TEST(CommonRange, OutOfRange)
{
	EXPECT_FALSE(in_range(5.0, 7.0, 1.0));
	EXPECT_FALSE(in_range(5.0, 3.0, 1.0));
}

TEST(CommonRange, BoundaryValues)
{
	EXPECT_TRUE(in_range(5.0, 6.0, 1.0));
	EXPECT_TRUE(in_range(5.0, 4.0, 1.0));
}
