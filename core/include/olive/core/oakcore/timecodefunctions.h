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

#ifndef OAKCORE_TIMECODEFUNCTIONS_H
#define OAKCORE_TIMECODEFUNCTIONS_H

#include <stdint.h>

#include "export.h"
#include "rational.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file timecodefunctions.h
 * @brief C ABI for the time/timecode/timestamp conversion functions
 *
 * Free functions only (the wrapped class is all-static, so there is no
 * object handle of its own). Times and timebases are passed as borrowed
 * OakRational handles; every returned OakRational* is owned by the caller
 * and must be released with oakcore_rational_free().
 *
 * Terminology used throughout:
 *
 * `time` - time in seconds presented in a Rational form
 * `timebase` - the base time unit of an audio/video stream in seconds
 * `timestamp` - an integer representation of a time in timebase units
 * (in many cases is used like a frame number)
 * `timecode` - a user-friendly string representation of a time according to OakTimecodeDisplay
 */

/**
 * @brief User-friendly timecode display modes
 *
 * Values must stay in sync with the wrapped class's Display enum.
 */
typedef enum OakTimecodeDisplay {
	OAK_TIMECODE_DISPLAY_DROP_FRAME = 0,
	OAK_TIMECODE_DISPLAY_NON_DROP_FRAME = 1,
	OAK_TIMECODE_DISPLAY_SECONDS = 2,
	OAK_TIMECODE_DISPLAY_FRAMES = 3,
	OAK_TIMECODE_DISPLAY_MILLISECONDS = 4
} OakTimecodeDisplay;

/**
 * @brief Rounding modes for time/timestamp conversions
 *
 * Values must stay in sync with the wrapped class's Rounding enum.
 */
typedef enum OakTimecodeRounding {
	OAK_TIMECODE_ROUNDING_CEIL = 0,
	OAK_TIMECODE_ROUNDING_FLOOR = 1,
	OAK_TIMECODE_ROUNDING_ROUND = 2
} OakTimecodeRounding;

/**
 * @brief Convert a time (according to a Rational timebase) to a user-friendly string representation
 *
 * Writes the timecode into buf (NUL-terminated when buf_size > 0).
 * Returns the number of characters that would have been written excluding
 * the NUL, so buf == NULL or a too-small buffer can be used to query the
 * required size.
 */
OAKCORE_API int oakcore_timecode_time_to_timecode(const OakRational *time,
												  const OakRational *timebase,
												  OakTimecodeDisplay display,
												  int show_plus_if_positive,
												  char *buf, int buf_size);

/**
 * @brief Convert a user-friendly timecode string to a time in seconds
 *
 * Returns a new owned OakRational (free with oakcore_rational_free()).
 * ok is set to 1 on success and 0 on failure (may be NULL).
 */
OAKCORE_API OakRational *oakcore_timecode_timecode_to_time(const char *timecode,
														   const OakRational *timebase,
														   OakTimecodeDisplay display,
														   int *ok);

/**
 * @brief Convert a millisecond count to an "HH:MM:SS" string
 *
 * Same buf/buf_size convention as oakcore_timecode_time_to_timecode().
 */
OAKCORE_API int oakcore_timecode_time_to_string(int64_t ms, char *buf,
												int buf_size);

/**
 * @brief Snap a time to the nearest timestamp boundary of a timebase
 *
 * Returns a new owned OakRational (free with oakcore_rational_free()).
 */
OAKCORE_API OakRational *oakcore_timecode_snap_time_to_timebase(const OakRational *time,
																const OakRational *timebase,
																OakTimecodeRounding rounding);

OAKCORE_API int64_t oakcore_timecode_time_to_timestamp(const OakRational *time,
													   const OakRational *timebase,
													   OakTimecodeRounding rounding);
OAKCORE_API int64_t oakcore_timecode_time_to_timestamp_d(double time,
														 const OakRational *timebase,
														 OakTimecodeRounding rounding);

OAKCORE_API int64_t oakcore_timecode_rescale_timestamp(int64_t ts,
													   const OakRational *source,
													   const OakRational *dest);
OAKCORE_API int64_t oakcore_timecode_rescale_timestamp_ceil(int64_t ts,
															const OakRational *source,
															const OakRational *dest);

/**
 * @brief Convert a timestamp in timebase units back to a time in seconds
 *
 * Returns a new owned OakRational (free with oakcore_rational_free()).
 */
OAKCORE_API OakRational *oakcore_timecode_timestamp_to_time(int64_t timestamp,
															const OakRational *timebase);

OAKCORE_API int oakcore_timecode_timebase_is_drop_frame(const OakRational *timebase);

#ifdef __cplusplus
}
#endif

#endif /* OAKCORE_TIMECODEFUNCTIONS_H */
