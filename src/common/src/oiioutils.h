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

#ifndef OAK_OIIOUTILS_H
#define OAK_OIIOUTILS_H

#include <cstdint>

#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/typedesc.h>

#include <olive/core/render/pixelformat.h>
#include <olive/core/util/rational.h>

/**
 * @brief A collection of static OpenImageIO helper functions
 *
 * Qt-free reimplementation of the former olive::OIIOUtils. The reverse
 * dependencies on codec/frame.h and render/videoparams.h (which pull in
 * Qt) were replaced with the Qt-free olive/core/render/pixelformat.h and
 * olive/core/util/rational.h. The Frame-based helpers were flattened to
 * raw data pointer + linesize, which is all they ever used from Frame.
 */
class OIIOUtils {
public:
	/**
	 * @brief Returns the OIIO base type matching a native pixel format
	 *
	 * Returns OIIO::TypeDesc::UNKNOWN for invalid, out-of-range or
	 * unmappable formats (e.g. u10, which OIIO has no base type for).
	 */
	static OIIO::TypeDesc::BASETYPE
	get_oiio_base_type_from_format(olive::core::PixelFormat format);

	/**
	 * @brief Returns the native pixel format matching an OIIO base type
	 *
	 * Returns olive::core::PixelFormat::invalid for unknown or unmappable
	 * base types.
	 */
	static olive::core::PixelFormat
	get_format_from_oiio_basetype(OIIO::TypeDesc::BASETYPE type);

	/**
	 * @brief Reads the PixelAspectRatio attribute of an image spec
	 *
	 * Defaults to 1:1 when the attribute is not present.
	 */
	static olive::core::Rational
	get_pixel_aspect_ratio_from_oiio(const OIIO::ImageSpec &spec);

	/**
	 * @brief Copies raw pixel data into an OIIO image buffer
	 *
	 * Flattened form of the former Frame-based frame_to_buffer(); pass
	 * Frame::const_data() and Frame::linesize_bytes() at the call site.
	 */
	static void frame_to_buffer(const void *data, int64_t linesize_bytes,
								OIIO::ImageBuf *buf);

	/**
	 * @brief Copies an OIIO image buffer's pixels into raw memory
	 *
	 * Flattened form of the former Frame-based buffer_to_frame(); pass
	 * Frame::data() and Frame::linesize_bytes() at the call site.
	 */
	static void buffer_to_frame(OIIO::ImageBuf *buf, void *data,
								int64_t linesize_bytes);
};

#endif // OAK_OIIOUTILS_H
