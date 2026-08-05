/***

  Oak Video Editor - Non-Linear Video Editor
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

#include "ocioutils.h"

ocio::BitDepth OCIOUtils::get_ocio_bit_depth_from_pixel_format(
	olive::core::PixelFormat format)
{
	switch (format) {
	case olive::core::PixelFormat::u8:
		return ocio::BIT_DEPTH_UINT8;
	case olive::core::PixelFormat::u10:
		return ocio::BIT_DEPTH_UINT10;
	case olive::core::PixelFormat::u16:
		return ocio::BIT_DEPTH_UINT16;
	case olive::core::PixelFormat::f16:
		return ocio::BIT_DEPTH_F16;
	case olive::core::PixelFormat::f32:
		return ocio::BIT_DEPTH_F32;
	case olive::core::PixelFormat::invalid:
	case olive::core::PixelFormat::count:
		break;
	}

	return ocio::BIT_DEPTH_UNKNOWN;
}
