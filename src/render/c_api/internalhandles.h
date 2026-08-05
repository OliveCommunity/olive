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

#ifndef OAK_EDITOR_RENDER_INTERNALHANDLES_H
#define OAK_EDITOR_RENDER_INTERNALHANDLES_H

/**
 * @brief Control-block definitions behind the public opaque handles
 *        (internal, not installed).
 *
 * Textures, frames and color processors wrap shared_ptr-managed engine
 * objects, so their handles are heap control blocks (the R7-A §A.2
 * ownership protocol). The refcount on textures/frames implements the
 * retain/free pairing rule; every alive control block participates in
 * oakrender_debug_alive_count().
 */

#include <atomic>

#include "codec/frame.h"
#include "colorprocessor.h"
#include "texture.h"

struct OakRenderTexture {
	olive::TexturePtr ptr;
	std::atomic<int> refcount{ 1 };
};

struct OakCodecFrame {
	olive::FramePtr ptr;
	std::atomic<int> refcount{ 1 };
};

struct OakColorProcessor {
	olive::ColorProcessorPtr ptr;
};

#endif //OAK_EDITOR_RENDER_INTERNALHANDLES_H
