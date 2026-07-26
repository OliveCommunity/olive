#include <gtest/gtest.h>
#include <math.h>
#include <string.h>

#include "olive/core/oakcore/timecodefunctions.h"

class OakcoreTimecodeTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		tb_30 = oakcore_rational_create_nd(1, 30);
		tb_df = oakcore_rational_create_nd(1001, 30000);
		tb_60 = oakcore_rational_create_nd(1, 60);
	}
	void TearDown() override
	{
		oakcore_rational_free(tb_60);
		oakcore_rational_free(tb_df);
		oakcore_rational_free(tb_30);
	}
	OakRational *tb_30, *tb_df, *tb_60;
	char buf[64];
};

TEST_F(OakcoreTimecodeTest, TimeToTimecodeDropFrame)
{
	OakRational *time = oakcore_rational_create(1);
	const int size = oakcore_timecode_time_to_timecode(
		time, tb_df, OAK_TIMECODE_DISPLAY_DROP_FRAME, 0, NULL, 0);
	EXPECT_EQ(size, 11);
	oakcore_timecode_time_to_timecode(
		time, tb_df, OAK_TIMECODE_DISPLAY_DROP_FRAME, 0, buf, sizeof(buf));
	EXPECT_STREQ(buf, "00:00:01;00");

	// Truncating buffer
	oakcore_timecode_time_to_timecode(
		time, tb_df, OAK_TIMECODE_DISPLAY_DROP_FRAME, 0, buf, 6);
	EXPECT_STREQ(buf, "00:00");
	oakcore_rational_free(time);
}

TEST_F(OakcoreTimecodeTest, TimeToTimecodeInvalidTimebase)
{
	OakRational *time = oakcore_rational_create(0);
	OakRational *bizarre = oakcore_rational_create(156632219);
	oakcore_timecode_time_to_timecode(
		time, bizarre, OAK_TIMECODE_DISPLAY_DROP_FRAME, 0, buf, sizeof(buf));
	EXPECT_STREQ(buf, "INVALID TIMEBASE");
	oakcore_rational_free(bizarre);
	oakcore_rational_free(time);
}

TEST_F(OakcoreTimecodeTest, TimeToTimecodeNonDrop)
{
	OakRational *time = oakcore_rational_create_nd(3, 2);
	oakcore_timecode_time_to_timecode(
		time, tb_30, OAK_TIMECODE_DISPLAY_NON_DROP_FRAME, 0, buf, sizeof(buf));
	EXPECT_STREQ(buf, "00:00:01:15");

	oakcore_timecode_time_to_timecode(
		time, tb_30, OAK_TIMECODE_DISPLAY_NON_DROP_FRAME, 1, buf, sizeof(buf));
	EXPECT_STREQ(buf, "+00:00:01:15");
	oakcore_rational_free(time);

	time = oakcore_rational_create_nd(-3, 2);
	oakcore_timecode_time_to_timecode(
		time, tb_30, OAK_TIMECODE_DISPLAY_NON_DROP_FRAME, 0, buf, sizeof(buf));
	EXPECT_STREQ(buf, "-00:00:01:15");
	oakcore_rational_free(time);
}

TEST_F(OakcoreTimecodeTest, TimeToTimecodeDisplayModes)
{
	OakRational *time = oakcore_rational_create_nd(123, 2);
	oakcore_timecode_time_to_timecode(
		time, tb_30, OAK_TIMECODE_DISPLAY_SECONDS, 0, buf, sizeof(buf));
	EXPECT_STREQ(buf, "00:01:01.500");
	oakcore_rational_free(time);

	time = oakcore_rational_create_nd(3, 2);
	oakcore_timecode_time_to_timecode(
		time, tb_30, OAK_TIMECODE_DISPLAY_FRAMES, 0, buf, sizeof(buf));
	EXPECT_STREQ(buf, "45");
	oakcore_timecode_time_to_timecode(
		time, tb_30, OAK_TIMECODE_DISPLAY_MILLISECONDS, 0, buf, sizeof(buf));
	EXPECT_STREQ(buf, "1500");
	oakcore_rational_free(time);
}

TEST_F(OakcoreTimecodeTest, TimecodeToTime)
{
	int ok = 0;
	OakRational *r;

	r = oakcore_timecode_timecode_to_time("00:00:01:15", tb_30,
										  OAK_TIMECODE_DISPLAY_NON_DROP_FRAME, &ok);
	EXPECT_EQ(ok, 1);
	EXPECT_EQ(oakcore_rational_numerator(r), 3);
	EXPECT_EQ(oakcore_rational_denominator(r), 2);
	oakcore_rational_free(r);

	r = oakcore_timecode_timecode_to_time("00:00:01;00", tb_df,
										  OAK_TIMECODE_DISPLAY_DROP_FRAME, &ok);
	EXPECT_EQ(ok, 1);
	EXPECT_EQ(oakcore_rational_numerator(r), 1001);
	EXPECT_EQ(oakcore_rational_denominator(r), 1000);
	oakcore_rational_free(r);

	r = oakcore_timecode_timecode_to_time("45", tb_30,
										  OAK_TIMECODE_DISPLAY_FRAMES, &ok);
	EXPECT_EQ(ok, 1);
	EXPECT_EQ(oakcore_rational_numerator(r), 3);
	oakcore_rational_free(r);

	r = oakcore_timecode_timecode_to_time("abc", tb_30,
										  OAK_TIMECODE_DISPLAY_NON_DROP_FRAME, &ok);
	EXPECT_EQ(ok, 0);
	oakcore_rational_free(r);

	r = oakcore_timecode_timecode_to_time(NULL, tb_30,
										  OAK_TIMECODE_DISPLAY_NON_DROP_FRAME, &ok);
	EXPECT_EQ(ok, 0);
	oakcore_rational_free(r);
}

TEST_F(OakcoreTimecodeTest, TimeToString)
{
	EXPECT_EQ(oakcore_timecode_time_to_string(3661000, NULL, 0), 8);
	EXPECT_EQ(oakcore_timecode_time_to_string(3661000, buf, sizeof(buf)), 8);
	EXPECT_STREQ(buf, "01:01:01");
	oakcore_timecode_time_to_string(0, buf, sizeof(buf));
	EXPECT_STREQ(buf, "00:00:00");
}

TEST_F(OakcoreTimecodeTest, SnapTimeToTimebase)
{
	OakRational *time = oakcore_rational_create_nd(102, 100);
	OakRational *r = oakcore_timecode_snap_time_to_timebase(
		time, tb_30, OAK_TIMECODE_ROUNDING_ROUND);
	EXPECT_EQ(oakcore_rational_numerator(r), 31);
	EXPECT_EQ(oakcore_rational_denominator(r), 30);
	oakcore_rational_free(r);
	oakcore_rational_free(time);

	time = oakcore_rational_create_nd(151, 100);
	r = oakcore_timecode_snap_time_to_timebase(time, tb_30, OAK_TIMECODE_ROUNDING_FLOOR);
	EXPECT_EQ(oakcore_rational_numerator(r), 3);
	EXPECT_EQ(oakcore_rational_denominator(r), 2);
	oakcore_rational_free(r);
	r = oakcore_timecode_snap_time_to_timebase(time, tb_30, OAK_TIMECODE_ROUNDING_CEIL);
	EXPECT_EQ(oakcore_rational_numerator(r), 23);
	EXPECT_EQ(oakcore_rational_denominator(r), 15);
	oakcore_rational_free(r);
	oakcore_rational_free(time);
}

TEST_F(OakcoreTimecodeTest, TimeToTimestamp)
{
	OakRational *time = oakcore_rational_create_nd(3, 2);
	EXPECT_EQ(oakcore_timecode_time_to_timestamp(time, tb_30, OAK_TIMECODE_ROUNDING_ROUND), 45);
	oakcore_rational_free(time);

	time = oakcore_rational_create_nd(151, 100);
	EXPECT_EQ(oakcore_timecode_time_to_timestamp(time, tb_30, OAK_TIMECODE_ROUNDING_ROUND), 45);
	EXPECT_EQ(oakcore_timecode_time_to_timestamp(time, tb_30, OAK_TIMECODE_ROUNDING_FLOOR), 45);
	EXPECT_EQ(oakcore_timecode_time_to_timestamp(time, tb_30, OAK_TIMECODE_ROUNDING_CEIL), 46);
	oakcore_rational_free(time);

	EXPECT_EQ(oakcore_timecode_time_to_timestamp_d(1.5, tb_30, OAK_TIMECODE_ROUNDING_ROUND), 45);
	EXPECT_EQ(oakcore_timecode_time_to_timestamp_d(NAN, tb_30, OAK_TIMECODE_ROUNDING_ROUND), 0);
}

TEST_F(OakcoreTimecodeTest, RescaleTimestamp)
{
	EXPECT_EQ(oakcore_timecode_rescale_timestamp(30, tb_30, tb_60), 60);
	EXPECT_EQ(oakcore_timecode_rescale_timestamp(30, tb_30, tb_30), 30);

	OakRational *src = oakcore_rational_create_nd(1, 5);
	OakRational *dst = oakcore_rational_create_nd(3, 4);
	EXPECT_EQ(oakcore_timecode_rescale_timestamp(1, src, dst), 0);
	EXPECT_EQ(oakcore_timecode_rescale_timestamp_ceil(1, src, dst), 1);
	oakcore_rational_free(dst);
	oakcore_rational_free(src);
}

TEST_F(OakcoreTimecodeTest, TimestampToTime)
{
	OakRational *r = oakcore_timecode_timestamp_to_time(45, tb_30);
	EXPECT_EQ(oakcore_rational_numerator(r), 3);
	EXPECT_EQ(oakcore_rational_denominator(r), 2);
	oakcore_rational_free(r);
}

TEST_F(OakcoreTimecodeTest, TimebaseIsDropFrame)
{
	EXPECT_EQ(oakcore_timecode_timebase_is_drop_frame(tb_df), 1);
	EXPECT_EQ(oakcore_timecode_timebase_is_drop_frame(tb_30), 0);
}
