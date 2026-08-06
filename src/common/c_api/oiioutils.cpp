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
#include "refcounted.h"

namespace
{

/**
 * @brief Stateless family; the boxed object is empty and only exists so
 *        the handle has something to reference-count.
 */
struct OIIOUtilsState {
	int unused;
};

} // namespace

OakOIIOUtils oakcommon_oiioutils_init(void)
{
	try {
		return oakcommon::make_handle<OakOIIOUtils>(
			OIIOUtilsState{0});
	} catch (...) {
		OakOIIOUtils h = {};
		return h;
	}
}

void oakcommon_oiioutils_free(OakOIIOUtils *self)
{
	oakcommon::free_handle(self);
}

int oakcommon_oiioutils_get_oiio_base_type_from_format(
	OakOIIOUtils self, int pixel_format, int *out_base_type)
{
	try {
		if (self.ctx == NULL || out_base_type == NULL)
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
	OakOIIOUtils self, int base_type, int *out_pixel_format)
{
	try {
		if (self.ctx == NULL || out_pixel_format == NULL)
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
	OakOIIOUtils self, double pixel_aspect_ratio, int *out_numerator,
	int *out_denominator)
{
	try {
		if (self.ctx == NULL || out_numerator == NULL || out_denominator == NULL)
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
