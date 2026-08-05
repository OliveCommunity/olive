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

#ifndef OAK_OCIOUTILS_H
#define OAK_OCIOUTILS_H

#include <OpenColorIO/OpenColorIO.h>
namespace ocio = OCIO_NAMESPACE;

#include <olive/core/render/pixelformat.h>

/**
 * @brief A collection of static OpenColorIO helper functions
 *
 * Qt-free reimplementation of the former olive::OCIOUtils. The dependency
 * on render/videoparams.h (which pulls in Qt) was replaced with the
 * Qt-free olive/core/render/pixelformat.h, where PixelFormat is defined.
 */
class OCIOUtils {
public:
	/**
	 * @brief Returns the OCIO bit depth matching a native pixel format
	 *
	 * Returns ocio::BIT_DEPTH_UNKNOWN for invalid or out-of-range formats.
	 */
	static ocio::BitDepth get_ocio_bit_depth_from_pixel_format(
		olive::core::PixelFormat format);
};

#endif // OAK_OCIOUTILS_H
