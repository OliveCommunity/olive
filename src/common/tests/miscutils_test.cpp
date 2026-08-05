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

#include <cmath>

#include <gtest/gtest.h>

#include "common/miscutils.h"

TEST(MiscUtilsDecibel, FromLinearZero)
{
	double db;
	ASSERT_EQ(oakcommon_decibel_from_linear(0.0, &db), OAKCOMMON_OK);
	EXPECT_DOUBLE_EQ(db, OAKCOMMON_DECIBEL_MINIMUM);
}

TEST(MiscUtilsDecibel, FromLinearNegative)
{
	double db;
	ASSERT_EQ(oakcommon_decibel_from_linear(-1.0, &db), OAKCOMMON_OK);
	// log10 of a negative value is NaN, not clamped to minimum
	EXPECT_TRUE(std::isnan(db));
}

TEST(MiscUtilsDecibel, FromLinearOne)
{
	double db;
	ASSERT_EQ(oakcommon_decibel_from_linear(1.0, &db), OAKCOMMON_OK);
	EXPECT_DOUBLE_EQ(db, 0.0);
}

TEST(MiscUtilsDecibel, FromLinearNullOut)
{
	EXPECT_EQ(oakcommon_decibel_from_linear(1.0, nullptr),
			  OAKCOMMON_E_INVALID);
}

TEST(MiscUtilsDecibel, ToLinearZeroDb)
{
	double linear;
	ASSERT_EQ(oakcommon_decibel_to_linear(0.0, &linear), OAKCOMMON_OK);
	EXPECT_DOUBLE_EQ(linear, 1.0);
}

TEST(MiscUtilsDecibel, ToLinearMinimum)
{
	double linear;
	ASSERT_EQ(oakcommon_decibel_to_linear(OAKCOMMON_DECIBEL_MINIMUM,
										  &linear),
			  OAKCOMMON_OK);
	EXPECT_DOUBLE_EQ(linear, 0.0);
}

TEST(MiscUtilsDecibel, ToLinearNullOut)
{
	EXPECT_EQ(oakcommon_decibel_to_linear(0.0, nullptr),
			  OAKCOMMON_E_INVALID);
}

TEST(MiscUtilsDecibel, LinearRoundTrip)
{
	double db;
	double linear;
	ASSERT_EQ(oakcommon_decibel_from_linear(0.5, &db), OAKCOMMON_OK);
	ASSERT_EQ(oakcommon_decibel_to_linear(db, &linear), OAKCOMMON_OK);
	EXPECT_NEAR(linear, 0.5, 1e-12);
}

TEST(MiscUtilsDecibel, FromLogarithmicBounds)
{
	double db;
	ASSERT_EQ(oakcommon_decibel_from_logarithmic(0.0, &db), OAKCOMMON_OK);
	EXPECT_DOUBLE_EQ(db, OAKCOMMON_DECIBEL_MINIMUM);

	ASSERT_EQ(oakcommon_decibel_from_logarithmic(1.0, &db), OAKCOMMON_OK);
	EXPECT_DOUBLE_EQ(db, 0.0);
}

TEST(MiscUtilsDecibel, FromLogarithmicNullOut)
{
	EXPECT_EQ(oakcommon_decibel_from_logarithmic(0.5, nullptr),
			  OAKCOMMON_E_INVALID);
}

TEST(MiscUtilsDecibel, ToLogarithmicZeroDb)
{
	double logarithmic;
	ASSERT_EQ(oakcommon_decibel_to_logarithmic(0.0, &logarithmic),
			  OAKCOMMON_OK);
	EXPECT_DOUBLE_EQ(logarithmic, 1.0);
}

TEST(MiscUtilsDecibel, ToLogarithmicNullOut)
{
	EXPECT_EQ(oakcommon_decibel_to_logarithmic(0.0, nullptr),
			  OAKCOMMON_E_INVALID);
}

TEST(MiscUtilsDecibel, LogarithmicRoundTrip)
{
	double logarithmic;
	double linear;
	ASSERT_EQ(oakcommon_decibel_linear_to_logarithmic(0.5, &logarithmic),
			  OAKCOMMON_OK);
	ASSERT_EQ(oakcommon_decibel_logarithmic_to_linear(logarithmic, &linear),
			  OAKCOMMON_OK);
	EXPECT_NEAR(linear, 0.5, 1e-12);
}

TEST(MiscUtilsDecibel, LogarithmicDirectNullOut)
{
	double out;
	EXPECT_EQ(oakcommon_decibel_linear_to_logarithmic(0.5, nullptr),
			  OAKCOMMON_E_INVALID);
	EXPECT_EQ(oakcommon_decibel_logarithmic_to_linear(0.5, nullptr),
			  OAKCOMMON_E_INVALID);
	(void)out;
}

TEST(MiscUtilsLerp, Endpoints)
{
	double value;
	ASSERT_EQ(oakcommon_lerp(2.0, 8.0, 0.0, &value), OAKCOMMON_OK);
	EXPECT_DOUBLE_EQ(value, 2.0);

	ASSERT_EQ(oakcommon_lerp(2.0, 8.0, 1.0, &value), OAKCOMMON_OK);
	EXPECT_DOUBLE_EQ(value, 8.0);
}

TEST(MiscUtilsLerp, Midpoint)
{
	double value;
	ASSERT_EQ(oakcommon_lerp(2.0, 8.0, 0.5, &value), OAKCOMMON_OK);
	EXPECT_DOUBLE_EQ(value, 5.0);
}

TEST(MiscUtilsLerp, NegativeValues)
{
	double value;
	ASSERT_EQ(oakcommon_lerp(-10.0, 10.0, 0.25, &value), OAKCOMMON_OK);
	EXPECT_DOUBLE_EQ(value, -5.0);
}

TEST(MiscUtilsLerp, NullOut)
{
	EXPECT_EQ(oakcommon_lerp(0.0, 1.0, 0.5, nullptr), OAKCOMMON_E_INVALID);
}
