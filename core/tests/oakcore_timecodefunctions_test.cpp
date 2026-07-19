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

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "olive/core/oakcore/timecodefunctions.h"

int main()
{
	char buf[64];
	int ok = 0;
	OakRational *r = NULL;

	/* Common timebases: 30 fps non-drop, 29.97 drop-frame, 60 fps */
	OakRational *tb_30 = oakcore_rational_create_nd(1, 30);
	OakRational *tb_df = oakcore_rational_create_nd(1001, 30000);
	OakRational *tb_60 = oakcore_rational_create_nd(1, 60);

	/* oakcore_timecode_time_to_timecode: drop-frame ( Olive's own test case ) */
	{
		OakRational *time = oakcore_rational_create(1);
		const int size = oakcore_timecode_time_to_timecode(
			time, tb_df, OAK_TIMECODE_DISPLAY_DROP_FRAME, 0, NULL, 0);
		assert(size == 11);
		assert(oakcore_timecode_time_to_timecode(
				   time, tb_df, OAK_TIMECODE_DISPLAY_DROP_FRAME, 0, buf,
				   sizeof(buf)) == 11);
		assert(strcmp(buf, "00:00:01;00") == 0);
		/* Truncating buffer still reports the full required size */
		assert(oakcore_timecode_time_to_timecode(
				   time, tb_df, OAK_TIMECODE_DISPLAY_DROP_FRAME, 0, buf,
				   6) == 11);
		assert(strcmp(buf, "00:00") == 0);
		oakcore_rational_free(time);
	}

	/* oakcore_timecode_time_to_timecode: invalid timebase */
	{
		OakRational *time = oakcore_rational_create(0);
		OakRational *bizarre = oakcore_rational_create(156632219);
		assert(oakcore_timecode_time_to_timecode(
				   time, bizarre, OAK_TIMECODE_DISPLAY_DROP_FRAME, 0, buf,
				   sizeof(buf)) == 16);
		assert(strcmp(buf, "INVALID TIMEBASE") == 0);
		oakcore_rational_free(bizarre);
		oakcore_rational_free(time);
	}

	/* oakcore_timecode_time_to_timecode: non-drop, plus sign, negative */
	{
		OakRational *time = oakcore_rational_create_nd(3, 2);
		assert(oakcore_timecode_time_to_timecode(
				   time, tb_30, OAK_TIMECODE_DISPLAY_NON_DROP_FRAME, 0, buf,
				   sizeof(buf)) == 11);
		assert(strcmp(buf, "00:00:01:15") == 0);
		assert(oakcore_timecode_time_to_timecode(
				   time, tb_30, OAK_TIMECODE_DISPLAY_NON_DROP_FRAME, 1, buf,
				   sizeof(buf)) == 12);
		assert(strcmp(buf, "+00:00:01:15") == 0);
		oakcore_rational_free(time);
		time = oakcore_rational_create_nd(-3, 2);
		assert(oakcore_timecode_time_to_timecode(
				   time, tb_30, OAK_TIMECODE_DISPLAY_NON_DROP_FRAME, 0, buf,
				   sizeof(buf)) == 12);
		assert(strcmp(buf, "-00:00:01:15") == 0);
		oakcore_rational_free(time);
	}

	/* oakcore_timecode_time_to_timecode: seconds / frames / milliseconds */
	{
		OakRational *time = oakcore_rational_create_nd(123, 2); /* 61.5 s */
		assert(oakcore_timecode_time_to_timecode(
				   time, tb_30, OAK_TIMECODE_DISPLAY_SECONDS, 0, buf,
				   sizeof(buf)) == 12);
		assert(strcmp(buf, "00:01:01.500") == 0);
		oakcore_rational_free(time);
		time = oakcore_rational_create_nd(3, 2);
		assert(oakcore_timecode_time_to_timecode(
				   time, tb_30, OAK_TIMECODE_DISPLAY_FRAMES, 0, buf,
				   sizeof(buf)) == 2);
		assert(strcmp(buf, "45") == 0);
		assert(oakcore_timecode_time_to_timecode(
				   time, tb_30, OAK_TIMECODE_DISPLAY_MILLISECONDS, 0, buf,
				   sizeof(buf)) == 4);
		assert(strcmp(buf, "1500") == 0);
		oakcore_rational_free(time);
	}

	/* oakcore_timecode_timecode_to_time: non-drop */
	r = oakcore_timecode_timecode_to_time("00:00:01:15", tb_30,
										  OAK_TIMECODE_DISPLAY_NON_DROP_FRAME,
										  &ok);
	assert(ok == 1);
	assert(oakcore_rational_numerator(r) == 3 &&
		   oakcore_rational_denominator(r) == 2);
	oakcore_rational_free(r);

	/* oakcore_timecode_timecode_to_time: drop-frame */
	r = oakcore_timecode_timecode_to_time("00:00:01;00", tb_df,
										  OAK_TIMECODE_DISPLAY_DROP_FRAME,
										  &ok);
	assert(ok == 1);
	assert(oakcore_rational_numerator(r) == 1001 &&
		   oakcore_rational_denominator(r) == 1000);
	oakcore_rational_free(r);

	/* oakcore_timecode_timecode_to_time: seconds / frames / milliseconds */
	r = oakcore_timecode_timecode_to_time("00:00:01.5", tb_30,
										  OAK_TIMECODE_DISPLAY_SECONDS, &ok);
	assert(ok == 1);
	assert(oakcore_rational_numerator(r) == 3 &&
		   oakcore_rational_denominator(r) == 2);
	oakcore_rational_free(r);
	r = oakcore_timecode_timecode_to_time("45", tb_30,
										  OAK_TIMECODE_DISPLAY_FRAMES, &ok);
	assert(ok == 1);
	assert(oakcore_rational_numerator(r) == 3 &&
		   oakcore_rational_denominator(r) == 2);
	oakcore_rational_free(r);
	r = oakcore_timecode_timecode_to_time("1500", tb_30,
										  OAK_TIMECODE_DISPLAY_MILLISECONDS,
										  &ok);
	assert(ok == 1);
	assert(oakcore_rational_numerator(r) == 3 &&
		   oakcore_rational_denominator(r) == 2);
	oakcore_rational_free(r);

	/* oakcore_timecode_timecode_to_time: invalid and NULL input */
	r = oakcore_timecode_timecode_to_time("abc", tb_30,
										  OAK_TIMECODE_DISPLAY_NON_DROP_FRAME,
										  &ok);
	assert(ok == 0);
	assert(oakcore_rational_numerator(r) == 0);
	oakcore_rational_free(r);
	r = oakcore_timecode_timecode_to_time(NULL, tb_30,
										  OAK_TIMECODE_DISPLAY_NON_DROP_FRAME,
										  &ok);
	assert(ok == 0);
	oakcore_rational_free(r);
	/* ok pointer itself may be NULL */
	r = oakcore_timecode_timecode_to_time("45", tb_30,
										  OAK_TIMECODE_DISPLAY_FRAMES, NULL);
	assert(oakcore_rational_to_double(r) == 1.5);
	oakcore_rational_free(r);

	/* oakcore_timecode_time_to_string */
	assert(oakcore_timecode_time_to_string(3661000, NULL, 0) == 8);
	assert(oakcore_timecode_time_to_string(3661000, buf, sizeof(buf)) == 8);
	assert(strcmp(buf, "01:01:01") == 0);
	assert(oakcore_timecode_time_to_string(0, buf, sizeof(buf)) == 8);
	assert(strcmp(buf, "00:00:00") == 0);

	/* oakcore_timecode_snap_time_to_timebase */
	{
		OakRational *time = oakcore_rational_create_nd(102, 100); /* 1.02 s */
		r = oakcore_timecode_snap_time_to_timebase(
			time, tb_30, OAK_TIMECODE_ROUNDING_ROUND);
		assert(oakcore_rational_numerator(r) == 31 &&
			   oakcore_rational_denominator(r) == 30);
		oakcore_rational_free(r);
		oakcore_rational_free(time);
		time = oakcore_rational_create_nd(151, 100); /* 1.51 s -> 45.3 frames */
		r = oakcore_timecode_snap_time_to_timebase(
			time, tb_30, OAK_TIMECODE_ROUNDING_FLOOR);
		assert(oakcore_rational_numerator(r) == 3 &&
			   oakcore_rational_denominator(r) == 2);
		oakcore_rational_free(r);
		r = oakcore_timecode_snap_time_to_timebase(
			time, tb_30, OAK_TIMECODE_ROUNDING_CEIL);
		assert(oakcore_rational_numerator(r) == 23 &&
			   oakcore_rational_denominator(r) == 15);
		oakcore_rational_free(r);
		oakcore_rational_free(time);
	}

	/* oakcore_timecode_time_to_timestamp (Rational) */
	{
		OakRational *time = oakcore_rational_create_nd(3, 2);
		assert(oakcore_timecode_time_to_timestamp(
				   time, tb_30, OAK_TIMECODE_ROUNDING_ROUND) == 45);
		oakcore_rational_free(time);
		time = oakcore_rational_create_nd(151, 100); /* 45.3 frames */
		assert(oakcore_timecode_time_to_timestamp(
				   time, tb_30, OAK_TIMECODE_ROUNDING_ROUND) == 45);
		assert(oakcore_timecode_time_to_timestamp(
				   time, tb_30, OAK_TIMECODE_ROUNDING_FLOOR) == 45);
		assert(oakcore_timecode_time_to_timestamp(
				   time, tb_30, OAK_TIMECODE_ROUNDING_CEIL) == 46);
		oakcore_rational_free(time);
	}

	/* oakcore_timecode_time_to_timestamp_d (double) */
	assert(oakcore_timecode_time_to_timestamp_d(1.5, tb_30,
												OAK_TIMECODE_ROUNDING_ROUND) ==
		   45);
	assert(oakcore_timecode_time_to_timestamp_d(NAN, tb_30,
												OAK_TIMECODE_ROUNDING_ROUND) ==
		   0);

	/* oakcore_timecode_rescale_timestamp */
	assert(oakcore_timecode_rescale_timestamp(30, tb_30, tb_60) == 60);
	assert(oakcore_timecode_rescale_timestamp(30, tb_30, tb_30) == 30);
	{
		/* 1 * (1*4) / (5*3) = 0.2666... -> nearest is 0 */
		OakRational *src = oakcore_rational_create_nd(1, 5);
		OakRational *dst = oakcore_rational_create_nd(3, 4);
		assert(oakcore_timecode_rescale_timestamp(1, src, dst) == 0);
		/* ... but ceil rounds up to 1 */
		assert(oakcore_timecode_rescale_timestamp_ceil(1, src, dst) == 1);
		oakcore_rational_free(dst);
		oakcore_rational_free(src);
	}
	assert(oakcore_timecode_rescale_timestamp_ceil(30, tb_30, tb_60) == 60);

	/* oakcore_timecode_timestamp_to_time */
	r = oakcore_timecode_timestamp_to_time(45, tb_30);
	assert(oakcore_rational_numerator(r) == 3 &&
		   oakcore_rational_denominator(r) == 2);
	oakcore_rational_free(r);
	r = oakcore_timecode_timestamp_to_time(0, tb_30);
	assert(oakcore_rational_to_double(r) == 0.0);
	oakcore_rational_free(r);

	/* oakcore_timecode_timebase_is_drop_frame */
	assert(oakcore_timecode_timebase_is_drop_frame(tb_df) == 1);
	assert(oakcore_timecode_timebase_is_drop_frame(tb_30) == 0);

	oakcore_rational_free(tb_60);
	oakcore_rational_free(tb_df);
	oakcore_rational_free(tb_30);

	printf("oakcore_timecodefunctions_test: all tests passed\n");
	return 0;
}
