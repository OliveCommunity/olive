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

#ifndef OAKAUDIO_C_API_REFCOUNTED_H
#define OAKAUDIO_C_API_REFCOUNTED_H

#include <atomic>
#include <cstdint>
#include <type_traits>
#include <utility>

#include "audio/error.h"

namespace oakaudio
{

/**
 * @brief Heap box behind every handle's ctx pointer.
 *
 * Same pattern as oakcodec's c_api/refcounted.h: holds the wrapped
 * object plus its atomic reference count. addref and release are emitted
 * per boxed type so that the function pointers stored in a handle always
 * run code from the DLL that created the object. Every box also
 * participates in the oakaudio_debug_alive_count() ledger.
 */
template <typename T> struct RefCounted {
	T impl;
	std::atomic<uint32_t> refs;

	template <typename... Args>
	explicit RefCounted(Args &&...args)
		: impl(std::forward<Args>(args)...)
		, refs(1)
	{
	}
};

template <typename T> void ref_counted_addref(void *ctx)
{
	auto *box = static_cast<RefCounted<T> *>(ctx);
	if (box)
		box->refs.fetch_add(1, std::memory_order_relaxed);
}

void alive_inc();
void alive_dec();

template <typename T> void ref_counted_release(void *ctx)
{
	auto *box = static_cast<RefCounted<T> *>(ctx);
	if (box && box->refs.fetch_sub(1, std::memory_order_acq_rel) == 1) {
		delete box;
		alive_dec();
	}
}

/**
 * @brief Build a by-value handle owning a freshly boxed object (count 1).
 *
 * On allocation failure the returned handle has ctx == NULL (all C API
 * functions treat that as OAKAUDIO_E_INVALID and free() as a no-op).
 */
template <typename Handle, typename T, typename... Args>
Handle make_handle_in_place(Args &&...args)
{
	Handle h = {};
	try {
		h.ctx = new RefCounted<T>(std::forward<Args>(args)...);
		alive_inc();
	} catch (...) {
		h.ctx = nullptr;
	}
	h.addref = &ref_counted_addref<T>;
	h.release = &ref_counted_release<T>;
	h.abi_version = OAKAUDIO_ABI_VERSION;
	return h;
}

template <typename Handle, typename T> Handle make_handle(T &&value)
{
	return make_handle_in_place<Handle, typename std::decay<T>::type>(
		std::forward<T>(value));
}

/**
 * @brief Recover the boxed object from a handle ctx (NULL-safe).
 */
template <typename T> T *handle_impl(void *ctx)
{
	auto *box = static_cast<RefCounted<T> *>(ctx);
	return box ? &box->impl : nullptr;
}

/**
 * @brief Shared free() body: release the ctx, no-op on NULL/empty handle.
 */
template <typename Handle> void free_handle(Handle *h)
{
	if (!h || !h->ctx || !h->release)
		return;
	h->release(h->ctx);
	h->ctx = nullptr;
}

} // namespace oakaudio

#endif // OAKAUDIO_C_API_REFCOUNTED_H
