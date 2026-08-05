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

#ifndef OAK_TEXTUREHANDLE_H
#define OAK_TEXTUREHANDLE_H

// Merged content of engine/src/capi/displayinternal.h (M7 §3 layering fix):
// renderprocessor needs to hand TexturePtr/FramePtr across the C ABI app
// layer as opaque refcounted handles; the helper lived in the facade layer,
// which render must not include. The control-block layout below matches the
// facade's OakEngineDisplayTexture/OakEngineCodecFrame blocks field-for-field
// ({ptr, refcount=1}), so handles created here stay compatible with the
// facade's retain/release/unwrap once the facade migrates to this header.

#include <atomic>

#include "texture.h"
#include "codec/frame.h"

namespace olive
{

struct TextureHandleBlock {
	TexturePtr ptr;
	std::atomic<int> refcount{ 1 };
};

struct FrameHandleBlock {
	FramePtr ptr;
	std::atomic<int> refcount{ 1 };
};

/**
 * @brief Wrap an existing TexturePtr into a refcounted ABI handle (refcount=1).
 * Returns nullptr if tp is null.
 */
inline void *oakrender_internal_wrap_texture(const TexturePtr &tp)
{
	if (!tp) {
		return nullptr;
	}
	auto *blk = new TextureHandleBlock;
	blk->ptr = tp;
	blk->refcount.store(1);
	return blk;
}

/**
 * @brief Wrap an existing FramePtr into a refcounted ABI handle (refcount=1).
 * Returns nullptr if fp is null.
 */
inline void *oakrender_internal_wrap_frame(const FramePtr &fp)
{
	if (!fp) {
		return nullptr;
	}
	auto *blk = new FrameHandleBlock;
	blk->ptr = fp;
	blk->refcount.store(1);
	return blk;
}

/**
 * @brief Unwrap an ABI texture handle to the underlying TexturePtr.
 * Returns empty TexturePtr if handle is null.
 */
inline TexturePtr oakrender_internal_unwrap_texture(void *handle)
{
	if (!handle) {
		return nullptr;
	}
	return static_cast<TextureHandleBlock *>(handle)->ptr;
}

/**
 * @brief Unwrap an ABI frame handle to the underlying FramePtr.
 * Returns empty FramePtr if handle is null.
 */
inline FramePtr oakrender_internal_unwrap_frame(void *handle)
{
	if (!handle) {
		return nullptr;
	}
	return static_cast<FrameHandleBlock *>(handle)->ptr;
}

}

#endif // OAK_TEXTUREHANDLE_H
