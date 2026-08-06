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

#include "oiioframebridge.h"

namespace olive
{

void oiio_frame_to_buffer(const void *data, int64_t linesize_bytes,
						  OIIO::ImageBuf *buf)
{
	buf->set_pixels(OIIO::ROI(), buf->spec().format, data, OIIO::AutoStride,
					static_cast<OIIO::stride_t>(linesize_bytes));
}

void oiio_buffer_to_frame(OIIO::ImageBuf *buf, void *data,
						  int64_t linesize_bytes)
{
	buf->get_pixels(OIIO::ROI(), buf->spec().format, data, OIIO::AutoStride,
					static_cast<OIIO::stride_t>(linesize_bytes));
}

}
