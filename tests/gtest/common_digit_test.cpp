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

// Migrated from the legacy OAK_ADD_TEST framework (tests/general/common-tests.cpp)
TEST(CommonDigit, LegacyExhaustive)
{
	EXPECT_EQ(olive::get_digit_count(1), 1);
	EXPECT_EQ(olive::get_digit_count(69), 2);
	EXPECT_EQ(olive::get_digit_count(420), 3);
	EXPECT_EQ(olive::get_digit_count(1337), 4);
	EXPECT_EQ(olive::get_digit_count(80085), 5);
	EXPECT_EQ(olive::get_digit_count(555555), 6);
	EXPECT_EQ(olive::get_digit_count(8675309), 7);
	EXPECT_EQ(olive::get_digit_count(78956423), 8);
	EXPECT_EQ(olive::get_digit_count(148497523), 9);
	EXPECT_EQ(olive::get_digit_count(4845821233LL), 10);
	EXPECT_EQ(olive::get_digit_count(18002738255LL), 11);
	EXPECT_EQ(olive::get_digit_count(180027382556LL), 12);
	EXPECT_EQ(olive::get_digit_count(1800273825568LL), 13);
	EXPECT_EQ(olive::get_digit_count(18002738255685LL), 14);
	EXPECT_EQ(olive::get_digit_count(180027382556857LL), 15);
	EXPECT_EQ(olive::get_digit_count(1800273825564857LL), 16);
}
