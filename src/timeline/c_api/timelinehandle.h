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

#ifndef OAK_TIMELINE_CAPI_TIMELINEHANDLE_H
#define OAK_TIMELINE_CAPI_TIMELINEHANDLE_H

#include <atomic>
#include <new>

#include "timeline/error.h"

/**
 * @brief Internal control block behind the oaktimeline value handles,
 *        shared between the c_api translation units.
 *
 * Both public handle types (OakTimelineMarkerList, OakTimelineWorkArea)
 * are borrowed references into viewer nodes: the box never owns the
 * underlying object, so release() only destroys the box. A single
 * generic box and addref/release pair serves both families (identical
 * ctx/addref/release/abi_version layout).
 */
struct OakTimelineBox {
	void *object;
	std::atomic<uint32_t> refs;

	explicit OakTimelineBox(void *o)
		: object(o)
		, refs(1)
	{
	}
};

namespace oaktimeline_capi
{

inline void handle_addref(void *ctx)
{
	if (ctx) {
		static_cast<OakTimelineBox *>(ctx)->refs.fetch_add(1);
	}
}

inline void handle_release(void *ctx)
{
	if (!ctx) {
		return;
	}
	OakTimelineBox *box = static_cast<OakTimelineBox *>(ctx);
	if (box->refs.fetch_sub(1) == 1) {
		delete box;
	}
}

/**
 * @brief Wrap a borrowed native object in a value handle with reference
 *        count 1. Returns an empty handle (ctx == nullptr) for a null
 *        object or on allocation failure.
 */
template <typename Handle>
inline Handle make_handle(void *object)
{
	Handle handle = {};
	if (!object) {
		return handle;
	}

	OakTimelineBox *box = new (std::nothrow) OakTimelineBox(object);
	if (!box) {
		return handle;
	}

	handle.ctx = box;
	handle.addref = handle_addref;
	handle.release = handle_release;
	handle.abi_version = OAKTIMELINE_ABI_VERSION;
	return handle;
}

/**
 * @brief Unwrap a value handle to the native C++ pointer (nullptr for
 *        an empty handle).
 */
template <typename T, typename Handle>
inline T *to_native(Handle h)
{
	if (!h.ctx) {
		return nullptr;
	}
	return static_cast<T *>(static_cast<OakTimelineBox *>(h.ctx)->object);
}

/**
 * @brief Shared free() body: release the caller's reference (box only)
 *        and null out the handle. NULL and ctx == NULL are no-ops.
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

} // namespace oaktimeline_capi

#endif // OAK_TIMELINE_CAPI_TIMELINEHANDLE_H
