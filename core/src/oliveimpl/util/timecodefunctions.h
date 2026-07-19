/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2023 Olive Studios LLC
  Modifications Copyright (C) 2025 mikesolar

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

#ifndef OAK_LIBOLIVECORE_TIMECODEFUNCTIONS_H
#define OAK_LIBOLIVECORE_TIMECODEFUNCTIONS_H

#include "rational.h"

#include <cstdint>

namespace olive::core::internal
{

/**
 * @brief Functions for converting times/timecodes/timestamps
 *
 * Olive uses the following terminology through its code:
 *
 * `time` - time in seconds presented in a Rational form
 * `timebase` - the base time unit of an audio/video stream in seconds
 * `timestamp` - an integer representation of a time in timebase units (in many cases is used like a frame number)
 * `timecode` a user-friendly string representation of a time according to Timecode::Display
 */
class Timecode {
public:
	enum Display {
		k_timecode_drop_frame,
		k_timecode_non_drop_frame,
		k_timecode_seconds,
		k_frames,
		k_milliseconds
	};

	enum Rounding { k_ceil, k_floor, k_round };

	/**
   * @brief Convert a timestamp (according to a Rational timebase) to a user-friendly string representation
   */
	static std::string time_to_timecode(const Rational &time,
										const Rational &timebase,
										const Display &display,
										bool show_plus_if_positive = false);
	static Rational timecode_to_time(std::string timecode,
									 const Rational &timebase,
									 const Display &display,
									 bool *ok = nullptr);

	static std::string time_to_string(int64_t ms);

	static Rational snap_time_to_timebase(const Rational &time,
										  const Rational &timebase,
										  Rounding floor = k_round);

	static int64_t time_to_timestamp(const Rational &time,
									 const Rational &timebase,
									 Rounding floor = k_round);
	static int64_t time_to_timestamp(const double &time,
									 const Rational &timebase,
									 Rounding floor = k_round);

	static int64_t rescale_timestamp(const int64_t &ts, const Rational &source,
									 const Rational &dest);
	static int64_t rescale_timestamp_ceil(const int64_t &ts,
										  const Rational &source,
										  const Rational &dest);

	static Rational timestamp_to_time(const int64_t &timestamp,
									  const Rational &timebase);

	static bool timebase_is_drop_frame(const Rational &timebase);
};

}

#endif // OAK_LIBOLIVECORE_TIMECODEFUNCTIONS_H
