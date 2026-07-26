#include <gtest/gtest.h>
#include <stdarg.h>
#include <string.h>

#include "olive/core/oakcore/stringutils.h"

TEST(OakcoreStringUtils, SplitBasic)
{
	int count = 0;
	char **arr = oakcore_stringutils_split("a,b,c", ',', &count);
	ASSERT_NE(arr, nullptr);
	ASSERT_EQ(count, 3);
	EXPECT_STREQ(arr[0], "a");
	EXPECT_STREQ(arr[1], "b");
	EXPECT_STREQ(arr[2], "c");
	oakcore_stringutils_free_string_array(arr, count);
}

TEST(OakcoreStringUtils, SplitNoSeparator)
{
	int count = 0;
	char **arr = oakcore_stringutils_split("abc", ',', &count);
	ASSERT_NE(arr, nullptr);
	ASSERT_EQ(count, 1);
	EXPECT_STREQ(arr[0], "abc");
	oakcore_stringutils_free_string_array(arr, count);
}

TEST(OakcoreStringUtils, SplitTrailingSeparator)
{
	int count = 0;
	char **arr = oakcore_stringutils_split("a,", ',', &count);
	ASSERT_NE(arr, nullptr);
	ASSERT_EQ(count, 2);
	EXPECT_STREQ(arr[0], "a");
	EXPECT_STREQ(arr[1], "");
	oakcore_stringutils_free_string_array(arr, count);
}

TEST(OakcoreStringUtils, SplitEmptyAndNull)
{
	int count = 0;
	char **arr = oakcore_stringutils_split("", ',', &count);
	ASSERT_NE(arr, nullptr);
	ASSERT_EQ(count, 1);
	EXPECT_STREQ(arr[0], "");
	oakcore_stringutils_free_string_array(arr, count);

	arr = oakcore_stringutils_split(NULL, ',', &count);
	ASSERT_NE(arr, nullptr);
	ASSERT_EQ(count, 1);
	EXPECT_STREQ(arr[0], "");
	oakcore_stringutils_free_string_array(arr, count);
}

TEST(OakcoreStringUtils, SplitRegex)
{
	int count = 0;
	char **arr = oakcore_stringutils_split_regex("a1b22c", "[0-9]+", &count);
	ASSERT_NE(arr, nullptr);
	ASSERT_EQ(count, 3);
	EXPECT_STREQ(arr[0], "a");
	EXPECT_STREQ(arr[1], "b");
	EXPECT_STREQ(arr[2], "c");
	oakcore_stringutils_free_string_array(arr, count);

	arr = oakcore_stringutils_split_regex("abc", "[0-9]+", &count);
	ASSERT_NE(arr, nullptr);
	ASSERT_EQ(count, 1);
	EXPECT_STREQ(arr[0], "abc");
	oakcore_stringutils_free_string_array(arr, count);

	// Freeing NULL is safe
	oakcore_stringutils_free_string_array(NULL, 0);
}

TEST(OakcoreStringUtils, ToInt)
{
	int ok = 0;
	EXPECT_EQ(oakcore_stringutils_to_int("42", 10, &ok), 42);
	EXPECT_EQ(ok, 1);
	EXPECT_EQ(oakcore_stringutils_to_int("-17", 10, &ok), -17);
	EXPECT_EQ(ok, 1);
	EXPECT_EQ(oakcore_stringutils_to_int("ff", 16, &ok), 255);
	EXPECT_EQ(ok, 1);
	EXPECT_EQ(oakcore_stringutils_to_int("xyz", 10, &ok), 0);
	EXPECT_EQ(ok, 0);
	EXPECT_EQ(oakcore_stringutils_to_int("7", 10, NULL), 7);
}

static int format_forward(char *buf, int buf_size, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	const int r = oakcore_stringutils_format_v(buf, buf_size, fmt, args);
	va_end(args);
	return r;
}

TEST(OakcoreStringUtils, Format)
{
	char buf[64];
	const int needed = oakcore_stringutils_format(buf, sizeof(buf), "%d-%s-%.2f",
												  42, "mid", 1.5);
	EXPECT_EQ(needed, 11);
	EXPECT_STREQ(buf, "42-mid-1.50");

	// NULL buffer queries size
	EXPECT_EQ(oakcore_stringutils_format(NULL, 0, "%d-%s-%.2f", 42, "mid", 1.5),
			  needed);

	// Truncation
	char small[5];
	const int needed2 =
		oakcore_stringutils_format(small, sizeof(small), "%s", "abcdefgh");
	EXPECT_EQ(needed2, 8);
	EXPECT_STREQ(small, "abcd");

	// va_list form
	char buf2[64];
	const int needed3 = format_forward(buf2, sizeof(buf2), "%d-%s-%.2f", 42,
									   "mid", 1.5);
	EXPECT_EQ(needed3, needed);
	EXPECT_STREQ(buf2, buf);
}
