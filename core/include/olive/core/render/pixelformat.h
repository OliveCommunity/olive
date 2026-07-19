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

#ifndef OAK_LIBOLIVECORE_PIXELFORMAT_H
#define OAK_LIBOLIVECORE_PIXELFORMAT_H
#include "ofxCore.h"
#include <string>
namespace olive::core
{

class PixelFormat {
public:
	enum Format { invalid = -1, u8, u10, u16, f16, f32, count };

	PixelFormat(Format f = invalid)
	{
		f_ = f;
	}

	operator Format() const
	{
		return f_;
	}

	static PixelFormat from_ofx(std::string ofx_format){
		if(ofx_format == kOfxBitDepthByte){
			return PixelFormat::u8;
		}
		else if (ofx_format == kOfxBitDepthShort){
			return PixelFormat::u16;
		}
		else if(ofx_format == kOfxBitDepthHalf){
			return PixelFormat::f16;
		}
		else if(ofx_format == kOfxBitDepthFloat){
			return PixelFormat::f32;
		}
		return PixelFormat::invalid;
	}
	static int byte_count(Format f)
	{
		switch (f) {
		case invalid:
		case count:
			break;
		case u8:
			return 1;
		case u10:
			return 4; // packed RGBA10A2, treated as 4 bytes per pixel
		case u16:
		case f16:
			return 2;
		case f32:
			return 4;
		}

		return 0;
	}

	const char *to_string() const
	{
		switch (f_) {
		case u8:
			return "u8";
		case u10:
			return "u10";
		case u16:
			return "u16";
		case f16:
			return "f16";
		case f32:
			return "f32";
		case invalid:
		case count:
			break;
		}

		return "";
	}

	int byte_count() const
	{
		return byte_count(f_);
	}

	static bool is_float(Format f)
	{
		switch (f) {
		case invalid:
		case count:
		case u8:
		case u10:
		case u16:
			break;
		case f16:
		case f32:
			return true;
		}

		return false;
	}

	bool is_float() const
	{
		return is_float(f_);
	}

private:
	Format f_;
};

}

#endif // OAK_LIBOLIVECORE_PIXELFORMAT_H
