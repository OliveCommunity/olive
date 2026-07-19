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

#ifndef OAK_LIBOLIVECORE_SAMPLEFORMAT_H
#define OAK_LIBOLIVECORE_SAMPLEFORMAT_H

#include <stdexcept>
#include <string>

namespace olive::core
{

class SampleFormat {
public:
	enum Format {
		invalid = -1,

		u8_p,
		s16_p,
		s32_p,
		s64_p,
		f32_p,
		f64_p,

		u8,
		s16,
		s32,
		s64,
		f32,
		f64,

		count,

		planar_start = u8_p,
		packed_start = u8,
		planar_end = packed_start,
		packed_end = count,
	};

	SampleFormat(Format f = invalid)
	{
		f_ = f;
	}

	operator Format() const
	{
		return f_;
	}

	static int byte_count(Format f)
	{
		switch (f) {
		case u8:
		case u8_p:
			return 1;
		case s16:
		case s16_p:
			return 2;
		case s32:
		case f32:
		case s32_p:
		case f32_p:
			return 4;
		case s64:
		case f64:
		case s64_p:
		case f64_p:
			return 8;
		case invalid:
		case count:
			break;
		}

		return 0;
	}

	int byte_count() const
	{
		return byte_count(f_);
	}

	static std::string to_string(Format f)
	{
		switch (f) {
		case invalid:
		case count:
			break;
		case u8:
			return "u8";
		case s16:
			return "s16";
		case s32:
			return "s32";
		case s64:
			return "s64";
		case f32:
			return "f32";
		case f64:
			return "f64";
		case u8_p:
			return "u8p";
		case s16_p:
			return "s16p";
		case s32_p:
			return "s32p";
		case s64_p:
			return "s64p";
		case f32_p:
			return "f32p";
		case f64_p:
			return "f64p";
		}

		return "";
	}

	std::string to_string() const
	{
		return to_string(f_);
	}

	static SampleFormat from_string(const std::string &s)
	{
		if (s.empty()) {
			return invalid;
		} else if (s == "u8") {
			return u8;
		} else if (s == "s16") {
			return s16;
		} else if (s == "s32") {
			return s32;
		} else if (s == "s64") {
			return s64;
		} else if (s == "f32") {
			return f32;
		} else if (s == "f64") {
			return f64;
		} else if (s == "u8p") {
			return u8_p;
		} else if (s == "s16p") {
			return s16_p;
		} else if (s == "s32p") {
			return s32_p;
		} else if (s == "s64p") {
			return s64_p;
		} else if (s == "f32p") {
			return f32_p;
		} else if (s == "f64p") {
			return f64_p;
		} else {
			// Deprecated: sample formats used to be serialized as an integer. Handle that here, but we'll
			//             probably remove that eventually.
			try {
				int i = std::stoi(s);
				if (i > invalid && i < count) {
					return static_cast<Format>(i);
				}
			} catch (const std::invalid_argument &e) {
			}

			// Failed to deserialize from string
			return invalid;
		}
	}

	static bool is_packed(Format f)
	{
		return f >= packed_start && f < packed_end;
	}

	bool is_packed() const
	{
		return is_packed(f_);
	}

	static bool is_planar(Format f)
	{
		return f >= planar_start && f < planar_end;
	}

	bool is_planar() const
	{
		return is_planar(f_);
	}

	static SampleFormat to_packed_equivalent(SampleFormat fmt)
	{
		switch (fmt) {
		// For packed input, just return input
		case u8:
		case s16:
		case s32:
		case s64:
		case f32:
		case f64:
			return fmt;

		// Convert to packed
		case u8_p:
			return u8;
		case s16_p:
			return s16;
		case s32_p:
			return s32;
		case s64_p:
			return s64;
		case f32_p:
			return f32;
		case f64_p:
			return f64;

		case invalid:
		case count:
			break;
		}

		return invalid;
	}

	SampleFormat to_packed_equivalent() const
	{
		return to_packed_equivalent(f_);
	}

	static SampleFormat to_planar_equivalent(SampleFormat fmt)
	{
		switch (fmt) {
		// Convert to planar
		case u8:
			return u8_p;
		case s16:
			return s16_p;
		case s32:
			return s32_p;
		case s64:
			return s64_p;
		case f32:
			return f32_p;
		case f64:
			return f64_p;

		// For planar input, just return input
		case u8_p:
		case s16_p:
		case s32_p:
		case s64_p:
		case f32_p:
		case f64_p:
			return fmt;

		case invalid:
		case count:
			break;
		}

		return invalid;
	}

	SampleFormat to_planar_equivalent() const
	{
		return to_planar_equivalent(f_);
	}

private:
	Format f_;
};

}

#endif // OAK_LIBOLIVECORE_SAMPLEFORMAT_H
