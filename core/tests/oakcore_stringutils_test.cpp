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
 * @file oakcore_stringutils_test.cpp
 * @brief Pure C API test for oakcore/stringutils.h
 *
 * Exercises every C function directly (no C++ wrapper, no test framework).
 */

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "olive/core/oakcore/stringutils.h"

static void test_split()
{
	int count = 0;

	// Basic split
	char **arr = oakcore_stringutils_split("a,b,c", ',', &count);
	assert(arr != NULL);
	assert(count == 3);
	assert(strcmp(arr[0], "a") == 0);
	assert(strcmp(arr[1], "b") == 0);
	assert(strcmp(arr[2], "c") == 0);
	oakcore_stringutils_free_string_array(arr, count);

	// No separator present: single element equal to the whole string
	arr = oakcore_stringutils_split("abc", ',', &count);
	assert(arr != NULL);
	assert(count == 1);
	assert(strcmp(arr[0], "abc") == 0);
	oakcore_stringutils_free_string_array(arr, count);

	// Trailing separator yields a trailing empty string
	arr = oakcore_stringutils_split("a,", ',', &count);
	assert(arr != NULL);
	assert(count == 2);
	assert(strcmp(arr[0], "a") == 0);
	assert(strcmp(arr[1], "") == 0);
	oakcore_stringutils_free_string_array(arr, count);

	// Empty string yields one empty string
	arr = oakcore_stringutils_split("", ',', &count);
	assert(arr != NULL);
	assert(count == 1);
	assert(strcmp(arr[0], "") == 0);
	oakcore_stringutils_free_string_array(arr, count);

	// NULL string behaves like an empty string
	arr = oakcore_stringutils_split(NULL, ',', &count);
	assert(arr != NULL);
	assert(count == 1);
	assert(strcmp(arr[0], "") == 0);
	oakcore_stringutils_free_string_array(arr, count);
}

static void test_split_regex()
{
	int count = 0;

	// Split on runs of digits
	char **arr = oakcore_stringutils_split_regex("a1b22c", "[0-9]+", &count);
	assert(arr != NULL);
	assert(count == 3);
	assert(strcmp(arr[0], "a") == 0);
	assert(strcmp(arr[1], "b") == 0);
	assert(strcmp(arr[2], "c") == 0);
	oakcore_stringutils_free_string_array(arr, count);

	// Pattern that never matches yields the whole string
	arr = oakcore_stringutils_split_regex("abc", "[0-9]+", &count);
	assert(arr != NULL);
	assert(count == 1);
	assert(strcmp(arr[0], "abc") == 0);
	oakcore_stringutils_free_string_array(arr, count);

	// Freeing NULL is safe
	oakcore_stringutils_free_string_array(NULL, 0);
}

static void test_to_int()
{
	int ok = 0;

	// Base 10
	assert(oakcore_stringutils_to_int("42", 10, &ok) == 42);
	assert(ok == 1);

	// Negative
	assert(oakcore_stringutils_to_int("-17", 10, &ok) == -17);
	assert(ok == 1);

	// Base 16
	assert(oakcore_stringutils_to_int("ff", 16, &ok) == 255);
	assert(ok == 1);

	// Parser error: returns 0 and reports failure
	assert(oakcore_stringutils_to_int("xyz", 10, &ok) == 0);
	assert(ok == 0);

	// ok output parameter is optional
	assert(oakcore_stringutils_to_int("7", 10, NULL) == 7);
}

/* Variadic forwarder to exercise oakcore_stringutils_format_v() */
static int format_forward(char *buf, int buf_size, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	const int r = oakcore_stringutils_format_v(buf, buf_size, fmt, args);
	va_end(args);
	return r;
}

static void test_format()
{
	char buf[64];

	// Format with mixed argument types
	const int needed = oakcore_stringutils_format(buf, sizeof(buf), "%d-%s-%.2f",
												  42, "mid", 1.5);
	assert(needed == 11);
	assert(strcmp(buf, "42-mid-1.50") == 0);

	// NULL buffer queries the required size
	assert(oakcore_stringutils_format(NULL, 0, "%d-%s-%.2f", 42, "mid",
									  1.5) == needed);

	// Too-small buffer truncates but still reports the full required size
	char small[5];
	const int needed2 =
		oakcore_stringutils_format(small, sizeof(small), "%s", "abcdefgh");
	assert(needed2 == 8);
	assert(strcmp(small, "abcd") == 0);

	// va_list form produces identical results
	char buf2[64];
	const int needed3 = format_forward(buf2, sizeof(buf2), "%d-%s-%.2f", 42,
									   "mid", 1.5);
	assert(needed3 == needed);
	assert(strcmp(buf2, buf) == 0);
}

int main()
{
	test_split();
	test_split_regex();
	test_to_int();
	test_format();

	printf("oakcore_stringutils_test: all tests passed\n");
	return 0;
}
