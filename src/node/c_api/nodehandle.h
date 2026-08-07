/***

  Oak Video Editor - Non-Linear Video Editor
  Copyright (C) 2026 Oak Team

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  the GNU General Public License.  See the GNU General Public License
  for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

***/

#ifndef OAK_EDITOR_NODE_NODEHANDLE_H
#define OAK_EDITOR_NODE_NODEHANDLE_H

#include <atomic>
#include <new>

#include "node/error.h"

#include "alivecount.h"

/**
 * @brief Internal control block behind every OakNode* value handle,
 * shared between the c_api translation units.
 *
 * All oaknode public handle structs have the identical layout
 * (ctx/addref/release/abi_version), so a single generic box and
 * addref/release pair serves every family; `deleter` knows the
 * concrete C++ type to destroy.
 *
 * `owns` is true for objects created through init/create/factory
 * functions (releasing the last reference destroys the object through
 * `deleter`) and false for references into library-owned graphs
 * (children, tracks in a track list, nodes in a project): releasing
 * those only destroys the box. Functions that insert an owned object
 * into a graph (oaknode_project_add_node(), oaknode_tracklist_add_track(),
 * the track block operations, ...) flip `owns` to false through
 * mark_container_owned() once the graph assumes the lifetime.
 *
 * Live-object accounting: make_handle() counts every owned handle it
 * creates (alive_inc) and handle_release() un-counts an owned object
 * right after destroying it (alive_dec), keeping
 * oaknode_debug_alive_count() meaningful for leak checking.
 */
struct OakNodeBox {
	void *object;
	bool owns;
	std::atomic<uint32_t> refs;
	void (*deleter)(void *object);

	OakNodeBox(void *o, bool own, void (*del)(void *))
		: object(o)
		, owns(own)
		, refs(1)
		, deleter(del)
	{
	}
};

namespace oaknode_c_api
{

inline void handle_addref(void *ctx)
{
	if (ctx) {
		static_cast<OakNodeBox *>(ctx)->refs.fetch_add(1);
	}
}

inline void handle_release(void *ctx)
{
	if (!ctx) {
		return;
	}
	OakNodeBox *box = static_cast<OakNodeBox *>(ctx);
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
 * `deleter`. Returns an empty handle (ctx == nullptr) for a null
 * object or on allocation failure (an owned object is destroyed via
 * `deleter` in the latter case).
 */
template <typename Handle>
inline Handle make_handle(void *object, bool owns, void (*deleter)(void *))
{
	Handle handle = {};
	if (!object) {
		return handle;
	}

	OakNodeBox *box = new (std::nothrow) OakNodeBox(object, owns, deleter);
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
	handle.abi_version = OAKNODE_ABI_VERSION;
	return handle;
}

/**
 * @brief Unwrap a value handle to the native C++ pointer (nullptr for
 * an empty handle).
 */
template <typename T, typename Handle>
inline T *to_native(Handle h)
{
	if (!h.ctx) {
		return nullptr;
	}
	return static_cast<T *>(static_cast<OakNodeBox *>(h.ctx)->object);
}

/**
 * @brief Mark a handle's object as owned by a container (graph), so
 * releasing the handle no longer destroys the object.
 */
template <typename Handle>
inline void mark_container_owned(Handle h)
{
	if (h.ctx) {
		static_cast<OakNodeBox *>(h.ctx)->owns = false;
	}
}

/**
 * @brief Deleter for non-owning (borrowed) boxes: never deletes.
 */
inline void delete_noop(void *object)
{
	(void)object;
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
 * @brief Generic free() implementation for every oaknode family:
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

} // namespace oaknode_c_api

#endif // OAK_EDITOR_NODE_NODEHANDLE_H
