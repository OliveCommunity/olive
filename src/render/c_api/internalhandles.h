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
 * @brief Internal control block behind every oakrender value handle,
 *        shared between the c_api translation units (internal, not
 *        installed).
 *
 * All oakrender public handle structs have the identical layout
 * (ctx/addref/release/abi_version), so a single generic box and
 * addref/release pair serves every family; `deleter` knows the concrete
 * C++ type to destroy. Textures, frames and color processors box the
 * *Impl structs below (shared_ptr-managed engine objects); renderers,
 * tickets, caches and copiers box their native pointers with custom
 * deleters.
 *
 * `owns` is true for objects created through create/factory functions
 * (releasing the last reference destroys the object through `deleter`)
 * and false for borrowed wrappers (oakrender_cache_wrap_borrowed()):
 * releasing those only destroys the box.
 *
 * Live-object accounting: make_handle() counts every owned handle it
 * creates (alive_inc) and handle_release() un-counts an owned object
 * right after destroying it (alive_dec), keeping
 * oakrender_debug_alive_count() meaningful for leak checking.
 */

#include <atomic>
#include <new>

#include "render/error.h"

#include "alivecount.h"

#include "codec/frame.h"
#include "colorprocessor.h"
#include "texture.h"

/**
 * @brief Boxed object behind OakRenderTexture: a shared_ptr-managed
 *        GPU texture.
 */
struct OakRenderTextureImpl {
	olive::TexturePtr ptr;
};

/**
 * @brief Boxed object behind OakCodecFrame: a shared_ptr-managed CPU
 *        frame.
 */
struct OakCodecFrameImpl {
	olive::FramePtr ptr;
};

/**
 * @brief Boxed object behind OakColorProcessor: a shared_ptr-managed
 *        color processor.
 */
struct OakColorProcessorImpl {
	olive::ColorProcessorPtr ptr;
};

struct OakRenderBox {
	void *object;
	bool owns;
	std::atomic<uint32_t> refs;
	void (*deleter)(void *object);

	OakRenderBox(void *o, bool own, void (*del)(void *))
		: object(o)
		, owns(own)
		, refs(1)
		, deleter(del)
	{
	}
};

namespace oakrender_c_api
{

inline void handle_addref(void *ctx)
{
	if (ctx) {
		static_cast<OakRenderBox *>(ctx)->refs.fetch_add(1);
	}
}

inline void handle_release(void *ctx)
{
	if (!ctx) {
		return;
	}
	OakRenderBox *box = static_cast<OakRenderBox *>(ctx);
	if (box->refs.fetch_sub(1) == 1) {
		if (box->owns) {
			box->deleter(box->object);
			alive_dec();
		}
		delete box;
	}
}

/**
 * @brief Wrap `object` in a value handle with reference count 1.
 *
 * `owns` selects whether the final release destroys the object through
 * `deleter`. Returns an empty handle (ctx == nullptr) for a null object
 * or on allocation failure (an owned object is destroyed via `deleter`
 * in the latter case).
 */
template <typename Handle>
inline Handle make_handle(void *object, bool owns, void (*deleter)(void *))
{
	Handle handle = {};
	if (!object) {
		return handle;
	}

	OakRenderBox *box = new (std::nothrow) OakRenderBox(object, owns,
														deleter);
	if (!box) {
		if (owns && deleter) {
			deleter(object);
		}
		return handle;
	}

	if (owns) {
		alive_inc();
	}

	handle.ctx = box;
	handle.addref = handle_addref;
	handle.release = handle_release;
	handle.abi_version = OAKRENDER_ABI_VERSION;
	return handle;
}

/**
 * @brief Unwrap a value handle to the boxed object (nullptr for an
 *        empty handle).
 */
template <typename T, typename Handle>
inline T *to_native(Handle h)
{
	if (!h.ctx) {
		return nullptr;
	}
	return static_cast<T *>(static_cast<OakRenderBox *>(h.ctx)->object);
}

/**
 * @brief Raw boxed object behind a handle ctx (NULL-safe). Used by the
 * blit path, whose job struct carries borrowed ctx pointers.
 */
inline void *box_object(const void *ctx)
{
	if (!ctx) {
		return nullptr;
	}
	return static_cast<const OakRenderBox *>(ctx)->object;
}

/**
 * @brief Deleter callback stamping the concrete C++ type.
 */
template <typename T>
inline void delete_as(void *object)
{
	delete static_cast<T *>(object);
}

/**
 * @brief Generic free() implementation for every oakrender family:
 * release the caller's reference and null out the handle. NULL and
 * ctx == NULL are no-ops.
 */
template <typename Handle>
inline void free_handle(Handle *h)
{
	if (!h || !h->ctx) {
		return;
	}
	h->release(h->ctx);
	h->ctx = nullptr;
}

} // namespace oakrender_c_api

#endif //OAK_EDITOR_RENDER_INTERNALHANDLES_H
