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

#include "oiioutils.h"

#include <cstdio>

OIIO::TypeDesc::BASETYPE
OIIOUtils::get_oiio_base_type_from_format(olive::core::PixelFormat format)
{
	switch (format) {
	case olive::core::PixelFormat::u8:
		return OIIO::TypeDesc::UINT8;
	case olive::core::PixelFormat::u10:
		return OIIO::TypeDesc::UNKNOWN;
	case olive::core::PixelFormat::u16:
		return OIIO::TypeDesc::UINT16;
	case olive::core::PixelFormat::f16:
		return OIIO::TypeDesc::HALF;
	case olive::core::PixelFormat::f32:
		return OIIO::TypeDesc::FLOAT;
	case olive::core::PixelFormat::invalid:
	case olive::core::PixelFormat::count:
		break;
	}

	return OIIO::TypeDesc::UNKNOWN;
}

olive::core::PixelFormat
OIIOUtils::get_format_from_oiio_basetype(OIIO::TypeDesc::BASETYPE type)
{
	switch (type) {
	case OIIO::TypeDesc::UNKNOWN:
	case OIIO::TypeDesc::NONE:
#if OIIO_VERSION >= 20500
	case OIIO::TypeDesc::USTRINGHASH:
#endif
	default:
		break;

	case OIIO::TypeDesc::INT8:
	case OIIO::TypeDesc::INT16:
	case OIIO::TypeDesc::INT32:
	case OIIO::TypeDesc::UINT32:
	case OIIO::TypeDesc::INT64:
	case OIIO::TypeDesc::UINT64:
	case OIIO::TypeDesc::STRING:
	case OIIO::TypeDesc::PTR:
	case OIIO::TypeDesc::LASTBASE:
	case OIIO::TypeDesc::DOUBLE:
		fprintf(stderr, "Tried to use unknown OIIO base type\n");
		break;

	case OIIO::TypeDesc::UINT8:
		return olive::core::PixelFormat::u8;
	case OIIO::TypeDesc::UINT16:
		return olive::core::PixelFormat::u16;
	case OIIO::TypeDesc::HALF:
		return olive::core::PixelFormat::f16;
	case OIIO::TypeDesc::FLOAT:
		return olive::core::PixelFormat::f32;
	}

	return olive::core::PixelFormat::invalid;
}

olive::core::Rational
OIIOUtils::get_pixel_aspect_ratio_from_oiio(const OIIO::ImageSpec &spec)
{
	return olive::core::Rational::from_double(
		spec.get_float_attribute("PixelAspectRatio", 1));
}
