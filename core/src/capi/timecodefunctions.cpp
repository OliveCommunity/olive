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

#include "oakcore/timecodefunctions.h"

#include <stdio.h>

#include <string>

#include "util/rational.h"
#include "util/timecodefunctions.h"

namespace
{

const olive::core::internal::Rational *impl(const OakRational *h)
{
	return reinterpret_cast<const olive::core::internal::Rational *>(h);
}

OakRational *wrap(olive::core::internal::Rational *r)
{
	return reinterpret_cast<OakRational *>(r);
}

olive::core::internal::Timecode::Display to_display(OakTimecodeDisplay d)
{
	return static_cast<olive::core::internal::Timecode::Display>(d);
}

olive::core::internal::Timecode::Rounding to_rounding(OakTimecodeRounding r)
{
	return static_cast<olive::core::internal::Timecode::Rounding>(r);
}

int write_string(const std::string &s, char *buf, int buf_size)
{
	if (buf && buf_size > 0) {
		snprintf(buf, size_t(buf_size), "%s", s.c_str());
	}
	return int(s.size());
}

} // namespace

extern "C"
{

int oakcore_timecode_time_to_timecode(const OakRational *time,
									  const OakRational *timebase,
									  OakTimecodeDisplay display,
									  int show_plus_if_positive, char *buf,
									  int buf_size)
{
	const std::string s = olive::core::internal::Timecode::time_to_timecode(
		*impl(time), *impl(timebase), to_display(display),
		show_plus_if_positive != 0);
	return write_string(s, buf, buf_size);
}

OakRational *oakcore_timecode_timecode_to_time(const char *timecode,
											   const OakRational *timebase,
											   OakTimecodeDisplay display,
											   int *ok)
{
	bool b = false;
	olive::core::internal::Rational *r = new olive::core::internal::Rational(
		olive::core::internal::Timecode::timecode_to_time(
			timecode ? timecode : "", *impl(timebase), to_display(display),
			&b));
	if (ok) {
		*ok = b ? 1 : 0;
	}
	return wrap(r);
}

int oakcore_timecode_time_to_string(int64_t ms, char *buf, int buf_size)
{
	return write_string(olive::core::internal::Timecode::time_to_string(ms),
						buf, buf_size);
}

OakRational *oakcore_timecode_snap_time_to_timebase(const OakRational *time,
													const OakRational *timebase,
													OakTimecodeRounding rounding)
{
	return wrap(new olive::core::internal::Rational(
		olive::core::internal::Timecode::snap_time_to_timebase(
			*impl(time), *impl(timebase), to_rounding(rounding))));
}

int64_t oakcore_timecode_time_to_timestamp(const OakRational *time,
										   const OakRational *timebase,
										   OakTimecodeRounding rounding)
{
	return olive::core::internal::Timecode::time_to_timestamp(
		*impl(time), *impl(timebase), to_rounding(rounding));
}

int64_t oakcore_timecode_time_to_timestamp_d(double time,
											 const OakRational *timebase,
											 OakTimecodeRounding rounding)
{
	return olive::core::internal::Timecode::time_to_timestamp(
		time, *impl(timebase), to_rounding(rounding));
}

int64_t oakcore_timecode_rescale_timestamp(int64_t ts,
										   const OakRational *source,
										   const OakRational *dest)
{
	return olive::core::internal::Timecode::rescale_timestamp(
		ts, *impl(source), *impl(dest));
}

int64_t oakcore_timecode_rescale_timestamp_ceil(int64_t ts,
												const OakRational *source,
												const OakRational *dest)
{
	return olive::core::internal::Timecode::rescale_timestamp_ceil(
		ts, *impl(source), *impl(dest));
}

OakRational *oakcore_timecode_timestamp_to_time(int64_t timestamp,
												const OakRational *timebase)
{
	return wrap(new olive::core::internal::Rational(
		olive::core::internal::Timecode::timestamp_to_time(timestamp,
														   *impl(timebase))));
}

int oakcore_timecode_timebase_is_drop_frame(const OakRational *timebase)
{
	return olive::core::internal::Timecode::timebase_is_drop_frame(
			   *impl(timebase))
			   ? 1
			   : 0;
}

} // extern "C"
