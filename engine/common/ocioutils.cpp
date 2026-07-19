/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2022 Olive Team
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

#include "ocioutils.h"

namespace olive
{

ocio::BitDepth OCIOUtils::get_ocio_bit_depth_from_pixel_format(PixelFormat format)
{
	switch (format) {
	case PixelFormat::u8:
		return ocio::BIT_DEPTH_UINT8;
	case PixelFormat::u10:
		return ocio::BIT_DEPTH_UINT10;
	case PixelFormat::u16:
		return ocio::BIT_DEPTH_UINT16;
		break;
	case PixelFormat::f16:
		return ocio::BIT_DEPTH_F16;
		break;
	case PixelFormat::f32:
		return ocio::BIT_DEPTH_F32;
		break;
	case PixelFormat::invalid:
	case PixelFormat::count:
		break;
	}

	return ocio::BIT_DEPTH_UNKNOWN;
}

}
