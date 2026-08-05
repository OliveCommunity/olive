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

#include "common/ocioutils.h"

#include <new>

#include "../src/ocioutils.h"

struct OakCommonOCIOUtils {
	int unused; /**< Stateless; only the address matters. */
};

OakCommonOCIOUtils *oakcommon_ocioutils_init(void)
{
	try {
		return new (std::nothrow) OakCommonOCIOUtils{};
	} catch (...) {
		return NULL;
	}
}

void oakcommon_ocioutils_free(OakCommonOCIOUtils *self)
{
	delete self;
}

int oakcommon_ocioutils_get_ocio_bit_depth_from_pixel_format(
	OakCommonOCIOUtils *self, int pixel_format, int *out_bit_depth)
{
	try {
		if (self == NULL || out_bit_depth == NULL)
			return OAKCOMMON_E_INVALID;
		if (pixel_format < OAKCOMMON_PIXEL_FORMAT_INVALID ||
			pixel_format >= OAKCOMMON_PIXEL_FORMAT_COUNT)
			return OAKCOMMON_E_INVALID;

		ocio::BitDepth depth = OCIOUtils::get_ocio_bit_depth_from_pixel_format(
			static_cast<olive::core::PixelFormat::Format>(pixel_format));
		*out_bit_depth = static_cast<int>(depth);
		return OAKCOMMON_OK;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}
