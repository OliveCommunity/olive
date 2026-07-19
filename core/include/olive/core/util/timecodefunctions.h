/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2023 Olive Studios LLC
  Modifications Copyright (C) 2026 Oak Team

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
#include <string>

#include "olive/core/oakcore/timecodefunctions.h"

namespace olive::core
{

/**
 * @brief Functions for converting times/timecodes/timestamps
 *
 * Consumer-side wrapper over the liboakcore C ABI: the class is all-static
 * and every call is forwarded across the C boundary (times and timebases
 * cross it as borrowed Rational handles). The public API is unchanged from
 * the original implementation.
 *
 * Olive uses the following terminology through its code:
 *
 * `time` - time in seconds presented in a Rational form
 * `timebase` - the base time unit of an audio/video stream in seconds
 * `timestamp` - an integer representation of a time in timebase units
 * (in many cases is used like a frame number)
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
   * @brief Convert a timestamp (according to a Rational timebase) to a
   * user-friendly string representation
   */
	static std::string time_to_timecode(const Rational &time,
										const Rational &timebase,
										const Display &display,
										bool show_plus_if_positive = false)
	{
		const OakTimecodeDisplay c_display =
			static_cast<OakTimecodeDisplay>(display);
		const int c_show_plus = show_plus_if_positive ? 1 : 0;
		const int size = oakcore_timecode_time_to_timecode(
			time.handle(), timebase.handle(), c_display, c_show_plus, nullptr,
			0);
		std::string s(size_t(size) + 1, '\0');
		oakcore_timecode_time_to_timecode(time.handle(), timebase.handle(),
										  c_display, c_show_plus, s.data(),
										  size + 1);
		s.resize(size_t(size));
		return s;
	}

	static Rational timecode_to_time(std::string timecode,
									 const Rational &timebase,
									 const Display &display,
									 bool *ok = nullptr)
	{
		int c_ok = 0;
		Rational r = Rational::from_handle(oakcore_timecode_timecode_to_time(
			timecode.c_str(), timebase.handle(),
			static_cast<OakTimecodeDisplay>(display), &c_ok));
		if (ok) {
			*ok = (c_ok != 0);
		}
		return r;
	}

	static std::string time_to_string(int64_t ms)
	{
		const int size = oakcore_timecode_time_to_string(ms, nullptr, 0);
		std::string s(size_t(size) + 1, '\0');
		oakcore_timecode_time_to_string(ms, s.data(), size + 1);
		s.resize(size_t(size));
		return s;
	}

	static Rational snap_time_to_timebase(const Rational &time,
										  const Rational &timebase,
										  Rounding floor = k_round)
	{
		return Rational::from_handle(oakcore_timecode_snap_time_to_timebase(
			time.handle(), timebase.handle(),
			static_cast<OakTimecodeRounding>(floor)));
	}

	static int64_t time_to_timestamp(const Rational &time,
									 const Rational &timebase,
									 Rounding floor = k_round)
	{
		return oakcore_timecode_time_to_timestamp(
			time.handle(), timebase.handle(),
			static_cast<OakTimecodeRounding>(floor));
	}

	static int64_t time_to_timestamp(const double &time,
									 const Rational &timebase,
									 Rounding floor = k_round)
	{
		return oakcore_timecode_time_to_timestamp_d(
			time, timebase.handle(), static_cast<OakTimecodeRounding>(floor));
	}

	static int64_t rescale_timestamp(const int64_t &ts, const Rational &source,
									 const Rational &dest)
	{
		return oakcore_timecode_rescale_timestamp(ts, source.handle(),
												  dest.handle());
	}

	static int64_t rescale_timestamp_ceil(const int64_t &ts,
										  const Rational &source,
										  const Rational &dest)
	{
		return oakcore_timecode_rescale_timestamp_ceil(ts, source.handle(),
													   dest.handle());
	}

	static Rational timestamp_to_time(const int64_t &timestamp,
									  const Rational &timebase)
	{
		return Rational::from_handle(oakcore_timecode_timestamp_to_time(
			timestamp, timebase.handle()));
	}

	static bool timebase_is_drop_frame(const Rational &timebase)
	{
		return oakcore_timecode_timebase_is_drop_frame(timebase.handle()) != 0;
	}
};

}

#endif // OAK_LIBOLIVECORE_TIMECODEFUNCTIONS_H
