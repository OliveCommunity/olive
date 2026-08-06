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

#include "timecodemetadata.h"

#include <cstdlib>
#include <limits>
#include <numeric>

#include "olive/core/util/timecodefunctions.h"

namespace olive
{

namespace
{

std::string trimmed(const std::string &s)
{
	const char *ws = " \t\n\r\f\v";
	size_t begin = s.find_first_not_of(ws);
	if (begin == std::string::npos) {
		return std::string();
	}
	size_t end = s.find_last_not_of(ws);
	return s.substr(begin, end - begin + 1);
}

} // namespace

TimecodeMetadata::SourceTime
TimecodeMetadata::from_timecode_string(const std::string &timecode,
									 const core::Rational &timebase)
{
	SourceTime result;
	const std::string trimmed_tc = trimmed(timecode);
	if (trimmed_tc.empty()) {
		return result;
	}

	bool ok = false;
	const core::Timecode::Display display =
		trimmed_tc.find(';') != std::string::npos ?
			core::Timecode::k_timecode_drop_frame :
			core::Timecode::k_timecode_non_drop_frame;
	result.time =
		core::Timecode::timecode_to_time(trimmed_tc, timebase, display, &ok);
	result.valid = ok;
	if (ok) {
		result.source = "timecode";
	}
	return result;
}

TimecodeMetadata::SourceTime
TimecodeMetadata::from_bwf_time_reference(const std::string &time_reference,
									   int sample_rate)
{
	SourceTime result;
	if (sample_rate <= 0) {
		return result;
	}

	bool ok = false;
	const std::string trimmed_ref = trimmed(time_reference);
	char *end = nullptr;
	const unsigned long long samples =
		std::strtoull(trimmed_ref.c_str(), &end, 10);
	ok = end != trimmed_ref.c_str() && *end == '\0';
	if (!ok) {
		return result;
	}

	unsigned long long numerator = samples;
	unsigned long long denominator = static_cast<unsigned long long>(sample_rate);
	const unsigned long long divisor = std::gcd(numerator, denominator);
	numerator /= divisor;
	denominator /= divisor;

	const unsigned long long rational_limit =
		static_cast<unsigned long long>(std::numeric_limits<int>::max());
	if (numerator <= rational_limit && denominator <= rational_limit) {
		result.time = core::Rational(static_cast<int>(numerator),
									 static_cast<int>(denominator));
	} else {
		result.time = core::Rational::from_double(
			static_cast<double>(samples) / static_cast<double>(sample_rate));
	}
	result.source = "bwf_time_reference";
	result.valid = true;
	return result;
}

}
