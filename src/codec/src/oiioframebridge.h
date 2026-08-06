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

#ifndef OAK_OIIOFRAMEBRIDGE_H
#define OAK_OIIOFRAMEBRIDGE_H

#include <cstdint>

#include <OpenImageIO/imagebuf.h>

namespace olive
{

/**
 * @brief Copies raw pixel data into an OIIO image buffer
 *
 * Moved from oakcommon's OIIOUtils (M5): these two helpers are only used
 * by codec (Frame::convert() and the OIIO decoder/encoder), so they live
 * here as internal C++ functions and no longer cross the oakcommon
 * boundary. `format`/`nb_channels` describe the raw buffer and are only
 * needed by callers to have set up `buf`'s spec correctly beforehand;
 * the copy itself goes by the buffer's spec.
 */
void oiio_frame_to_buffer(const void *data, int64_t linesize_bytes,
						  OIIO::ImageBuf *buf);

/**
 * @brief Copies an OIIO image buffer's pixels into raw memory
 */
void oiio_buffer_to_frame(OIIO::ImageBuf *buf, void *data,
						  int64_t linesize_bytes);

}

#endif // OAK_OIIOFRAMEBRIDGE_H
