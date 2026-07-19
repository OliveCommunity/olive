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

#ifndef OAK_OIIOUTILS_H
#define OAK_OIIOUTILS_H

#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/typedesc.h>

#include "codec/frame.h"
#include "render/videoparams.h"

namespace olive
{

class OIIOUtils {
public:
	static OIIO::TypeDesc::BASETYPE
	get_oiio_base_type_from_format(PixelFormat format)
	{
		switch (format) {
		case PixelFormat::u8:
			return OIIO::TypeDesc::UINT8;
		case PixelFormat::u10:
			return OIIO::TypeDesc::UNKNOWN;
		case PixelFormat::u16:
			return OIIO::TypeDesc::UINT16;
		case PixelFormat::f16:
			return OIIO::TypeDesc::HALF;
		case PixelFormat::f32:
			return OIIO::TypeDesc::FLOAT;
		case PixelFormat::invalid:
		case PixelFormat::count:
			break;
		}

		return OIIO::TypeDesc::UNKNOWN;
	}

	static void frame_to_buffer(const Frame *frame, OIIO::ImageBuf *buf);

	static void buffer_to_frame(OIIO::ImageBuf *buf, Frame *frame);

	static PixelFormat get_format_from_oiio_basetype(OIIO::TypeDesc::BASETYPE type);

	static Rational get_pixel_aspect_ratio_from_oiio(const OIIO::ImageSpec &spec);
};

}

#endif // OAK_OIIOUTILS_H
