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

#include "common/oiioutils.h"

#include <new>

#include "../src/oiioutils.h"

struct OakCommonOIIOUtils {
	int unused; /**< Stateless; only the address matters. */
};

OakCommonOIIOUtils *oakcommon_oiioutils_init(void)
{
	try {
		return new (std::nothrow) OakCommonOIIOUtils{};
	} catch (...) {
		return NULL;
	}
}

void oakcommon_oiioutils_free(OakCommonOIIOUtils *self)
{
	delete self;
}

int oakcommon_oiioutils_get_oiio_base_type_from_format(
	OakCommonOIIOUtils *self, int pixel_format, int *out_base_type)
{
	try {
		if (self == NULL || out_base_type == NULL)
			return OAKCOMMON_E_INVALID;
		if (pixel_format < OAKCOMMON_PIXEL_FORMAT_INVALID ||
			pixel_format >= OAKCOMMON_PIXEL_FORMAT_COUNT)
			return OAKCOMMON_E_INVALID;

		OIIO::TypeDesc::BASETYPE base_type =
			OIIOUtils::get_oiio_base_type_from_format(
				static_cast<olive::core::PixelFormat::Format>(pixel_format));
		*out_base_type = static_cast<int>(base_type);
		return OAKCOMMON_OK;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_oiioutils_get_format_from_oiio_basetype(
	OakCommonOIIOUtils *self, int base_type, int *out_pixel_format)
{
	try {
		if (self == NULL || out_pixel_format == NULL)
			return OAKCOMMON_E_INVALID;
		if (base_type < 0 || base_type >= OIIO::TypeDesc::LASTBASE)
			return OAKCOMMON_E_INVALID;

		olive::core::PixelFormat format =
			OIIOUtils::get_format_from_oiio_basetype(
				static_cast<OIIO::TypeDesc::BASETYPE>(base_type));
		*out_pixel_format = static_cast<int>(format);
		return OAKCOMMON_OK;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_oiioutils_get_pixel_aspect_ratio(
	OakCommonOIIOUtils *self, double pixel_aspect_ratio, int *out_numerator,
	int *out_denominator)
{
	try {
		if (self == NULL || out_numerator == NULL || out_denominator == NULL)
			return OAKCOMMON_E_INVALID;

		olive::core::Rational par =
			olive::core::Rational::from_double(pixel_aspect_ratio);
		*out_numerator = par.numerator();
		*out_denominator = par.denominator();
		return OAKCOMMON_OK;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}
