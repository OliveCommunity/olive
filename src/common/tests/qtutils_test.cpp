/***

  Oak Video Editor - Non-Linear Video Editor
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

#include <cstdio>
#include <fstream>

#include <gtest/gtest.h>

#include "common/qtutils.h"

TEST(QtUtils, PtrToValueRoundTrip)
{
	int value = 42;
	uint64_t encoded = 0;

	ASSERT_EQ(oakcommon_qtutils_ptr_to_value(&value, &encoded),
			  OAKCOMMON_OK);
	EXPECT_EQ(encoded, static_cast<uint64_t>(
						  reinterpret_cast<uintptr_t>(&value)));

	void *decoded = nullptr;
	ASSERT_EQ(oakcommon_qtutils_value_to_ptr(encoded, &decoded),
			  OAKCOMMON_OK);
	EXPECT_EQ(decoded, static_cast<void *>(&value));
}

TEST(QtUtils, PtrToValueNullOutParam)
{
	int value = 42;

	EXPECT_EQ(oakcommon_qtutils_ptr_to_value(&value, nullptr),
			  OAKCOMMON_E_INVALID);
}

TEST(QtUtils, ValueToPtrNullPointer)
{
	void *decoded = reinterpret_cast<void *>(0xdeadbeef);

	ASSERT_EQ(oakcommon_qtutils_value_to_ptr(0, &decoded), OAKCOMMON_OK);
	EXPECT_EQ(decoded, nullptr);
}

TEST(QtUtils, ValueToPtrNullOutParam)
{
	EXPECT_EQ(oakcommon_qtutils_value_to_ptr(0, nullptr),
			  OAKCOMMON_E_INVALID);
}

TEST(QtUtils, GetCreationDateExistingFile)
{
	char path[] = "/tmp/oakcommon_qtutils_test_XXXXXX";
	int fd = mkstemp(path);
	ASSERT_NE(fd, -1);
	close(fd);

	int64_t secs = 0;
	ASSERT_EQ(oakcommon_qtutils_get_creation_date(path, &secs),
			  OAKCOMMON_OK);
	EXPECT_GT(secs, 0);

	std::remove(path);
}

TEST(QtUtils, GetCreationDateMissingFile)
{
	int64_t secs = 0;

	EXPECT_EQ(oakcommon_qtutils_get_creation_date(
				  "/tmp/oakcommon_qtutils_no_such_file_12345", &secs),
			  OAKCOMMON_E_NOT_FOUND);
}

TEST(QtUtils, GetCreationDateNullArgs)
{
	int64_t secs = 0;

	EXPECT_EQ(oakcommon_qtutils_get_creation_date(nullptr, &secs),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_qtutils_get_creation_date("/tmp", nullptr),
			  OAKCOMMON_E_INVALID);
}
