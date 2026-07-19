#include <gtest/gtest.h>

#include "olive/core/util/timecodefunctions.h"

using namespace olive::core;

TEST(CoreTimecode, TimeToTimecodeSeconds)
{
	Rational time(5, 1);
	Rational tb(1, 25);
	std::string tc =
		Timecode::time_to_timecode(time, tb, Timecode::k_timecode_seconds);
	EXPECT_EQ(tc, "00:00:05.000");
}

TEST(CoreTimecode, TimeToTimecodeNonDropFrame)
{
	Rational time(2, 1);
	Rational tb(1, 25);
	std::string tc =
		Timecode::time_to_timecode(time, tb, Timecode::k_timecode_non_drop_frame);
	EXPECT_EQ(tc, "00:00:02:00");
}

TEST(CoreTimecode, TimeToTimecodePlusSign)
{
	Rational time(1, 1);
	Rational tb(1, 25);
	std::string tc =
		Timecode::time_to_timecode(time, tb, Timecode::k_timecode_seconds, true);
	EXPECT_EQ(tc.substr(0, 1), "+");
}

TEST(CoreTimecode, TimeToTimecodeInvalidTimebase)
{
	Rational time(1, 1);
	EXPECT_EQ(Timecode::time_to_timecode(time, Rational(), Timecode::k_frames),
			  "INVALID TIMEBASE");
}

TEST(CoreTimecode, TimecodeToTimeSeconds)
{
	Rational tb(1, 25);
	bool ok = false;
	Rational t = Timecode::timecode_to_time("00:00:05.500", tb,
											Timecode::k_timecode_seconds, &ok);
	EXPECT_TRUE(ok);
	EXPECT_EQ(t, Rational(11, 2));
}

TEST(CoreTimecode, TimecodeToTimeNonDropFrame)
{
	Rational tb(1, 25);
	bool ok = false;
	Rational t = Timecode::timecode_to_time(
		"00:00:02:03", tb, Timecode::k_timecode_non_drop_frame, &ok);
	EXPECT_TRUE(ok);
	EXPECT_EQ(t, Rational(53, 25));
}

TEST(CoreTimecode, TimecodeToTimeInvalid)
{
	Rational tb(1, 25);
	bool ok = true;
	Timecode::timecode_to_time("not a timecode", tb, Timecode::k_timecode_seconds,
							   &ok);
	EXPECT_FALSE(ok);
}

TEST(CoreTimecode, TimeToString)
{
	EXPECT_EQ(Timecode::time_to_string(3661000), "01:01:01");
}

TEST(CoreTimecode, SnapTimeToTimebase)
{
	Rational tb(1, 25);
	Rational snapped = Timecode::snap_time_to_timebase(Rational(1, 10), tb);
	// 0.1s @ 25fps rounds to frame 3 (0.12s)
	EXPECT_EQ(snapped, Rational(3, 25));
}

TEST(CoreTimecode, TimeToTimestamp)
{
	Rational tb(1, 25);
	EXPECT_EQ(Timecode::time_to_timestamp(Rational(2, 1), tb), 50);

	// 0.08s @ 25fps lands exactly on frame 2, so the rounding mode
	// must not matter
	EXPECT_EQ(Timecode::time_to_timestamp(0.08, tb, Timecode::k_floor), 2);
	EXPECT_EQ(Timecode::time_to_timestamp(0.08, tb, Timecode::k_ceil), 2);

	// 0.1s @ 25fps is 2.5 frames: floor and ceil must differ
	EXPECT_EQ(Timecode::time_to_timestamp(0.1, tb, Timecode::k_floor), 2);
	EXPECT_EQ(Timecode::time_to_timestamp(0.1, tb, Timecode::k_ceil), 3);
}

TEST(CoreTimecode, TimestampToTime)
{
	Rational tb(1, 25);
	EXPECT_EQ(Timecode::timestamp_to_time(50, tb), Rational(2, 1));
}

TEST(CoreTimecode, RescaleTimestamp)
{
	Rational src(1, 25);
	Rational dst(1, 30);
	EXPECT_EQ(Timecode::rescale_timestamp(50, src, dst), 60);
	EXPECT_EQ(Timecode::rescale_timestamp(50, src, src), 50);
}

TEST(CoreTimecode, RescaleTimestampCeil)
{
	Rational src(1, 25);
	Rational dst(1, 30);
	EXPECT_EQ(Timecode::rescale_timestamp_ceil(1, src, dst), 2);
}

TEST(CoreTimecode, TimebaseIsDropFrame)
{
	EXPECT_FALSE(Timecode::timebase_is_drop_frame(Rational(1, 25)));
	EXPECT_TRUE(Timecode::timebase_is_drop_frame(Rational(1001, 30000)));
}
