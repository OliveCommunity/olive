#include <gtest/gtest.h>

#include "common/digit.h"

TEST(CommonDigit, SingleDigit)
{
	EXPECT_EQ(olive::get_digit_count(0), 1);
	EXPECT_EQ(olive::get_digit_count(5), 1);
	EXPECT_EQ(olive::get_digit_count(-5), 1);
}

TEST(CommonDigit, MultipleDigits)
{
	EXPECT_EQ(olive::get_digit_count(10), 2);
	EXPECT_EQ(olive::get_digit_count(999), 3);
	EXPECT_EQ(olive::get_digit_count(1000), 4);
	EXPECT_EQ(olive::get_digit_count(-12345), 5);
}

TEST(CommonDigit, LargeValue)
{
	EXPECT_EQ(olive::get_digit_count(123456789012345LL), 15);
}
