/***

  Oak - Non-Linear Video Editor
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

#ifndef OAK_DISPLAYBUFFER_H
#define OAK_DISPLAYBUFFER_H

#include <memory>

#include <QMetaType>
#include <QVariant>

#include "oakengine/display.h"

namespace olive
{

/**
 * @brief Refcounted wrapper around an opaque display handle (texture or frame).
 *
 * Stored inside QVariant for playback queue / set_image() path. The deleter
 * calls the matching facade free function when the last shared_ptr copy dies.
 */
struct OakSharedBuffer {
	void *handle = nullptr;
	enum Type { k_texture, k_frame } type = k_frame;
};

using OakSharedBufferPtr = std::shared_ptr<OakSharedBuffer>;

inline OakSharedBufferPtr oak_make_shared_texture(void *handle)
{
	return std::shared_ptr<OakSharedBuffer>(
		new OakSharedBuffer{handle, OakSharedBuffer::k_texture},
		[](OakSharedBuffer *b) {
			oakengine_display_texture_free(b->handle);
			delete b;
		});
}

inline OakSharedBufferPtr oak_make_shared_frame(void *handle)
{
	return std::shared_ptr<OakSharedBuffer>(
		new OakSharedBuffer{handle, OakSharedBuffer::k_frame},
		[](OakSharedBuffer *b) {
			oakengine_codec_frame_free(b->handle);
			delete b;
		});
}

} // namespace olive

Q_DECLARE_METATYPE(olive::OakSharedBufferPtr)

#endif // OAK_DISPLAYBUFFER_H
