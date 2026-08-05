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

#ifndef OAK_IPC_FRAMESLOTPOOL_H
#define OAK_IPC_FRAMESLOTPOOL_H

#include <cstddef>
#include <cstdint>

#include "oakengine/ipc.h"

namespace olive
{
namespace ipc
{

/**
 * @brief Per-slot metadata describing the frame currently occupying a slot.
 *
 * Trivially-copyable POD that lives in shared memory alongside the pixel data, part of the
 * version-1 wire protocol with the render worker. This is the C ABI oak_frame_slot_meta struct,
 * aliased so the shared-memory layout is defined exactly once, in oakengine/ipc.h.
 */
typedef oak_frame_slot_meta FrameSlotMeta;

/**
 * @brief A fixed-size pool of equal-sized frame slots in shared memory, with lock-free hand-off.
 *
 * Consumer-side wrapper over the liboakengine C ABI: the object only holds an opaque
 * OakFrameSlotPool handle and forwards every call across the C boundary. The public API is
 * unchanged from the original implementation; see oakengine/ipc.h for the protocol description.
 *
 * One pool models a single direction of frame flow (e.g. worker -> main for rendered output, or
 * main -> worker for decoded input). The pool does NOT own the memory; it is constructed over a
 * SharedMemoryRegion mapping. Use bytes_needed() to size that region.
 */
class FrameSlotPool {
public:
	FrameSlotPool() = default;

	FrameSlotPool(const FrameSlotPool &rhs)
		: handle_(oakengine_ipc_framepool_copy(rhs.handle_))
	{
	}

	FrameSlotPool(FrameSlotPool &&rhs) noexcept
		: handle_(rhs.handle_)
	{
		rhs.handle_ = nullptr;
	}

	~FrameSlotPool()
	{
		oakengine_ipc_framepool_free(handle_);
	}

	FrameSlotPool &operator=(const FrameSlotPool &rhs)
	{
		if (this != &rhs) {
			oakengine_ipc_framepool_free(handle_);
			handle_ = oakengine_ipc_framepool_copy(rhs.handle_);
		}
		return *this;
	}

	FrameSlotPool &operator=(FrameSlotPool &&rhs) noexcept
	{
		if (this != &rhs) {
			oakengine_ipc_framepool_free(handle_);
			handle_ = rhs.handle_;
			rhs.handle_ = nullptr;
		}
		return *this;
	}

	/**
   * @brief Total bytes a region must provide to back a pool of `slot_count` x `slot_data_bytes`.
   */
	static size_t bytes_needed(uint32_t slot_count, size_t slot_data_bytes)
	{
		return oakengine_ipc_framepool_bytes_needed(slot_count, slot_data_bytes);
	}

	/**
   * @brief Lay out and initialize a brand-new pool over `mem` (owner side, once).
   *
   * Initializes both rings, seeds the free ring with every slot index, and zeroes metadata.
   * `mem` must provide at least bytes_needed(slot_count, slot_data_bytes) bytes.
   */
	static FrameSlotPool create(void *mem, uint32_t slot_count,
								size_t slot_data_bytes)
	{
		return from_handle(oakengine_ipc_framepool_create(mem, slot_count,
														  slot_data_bytes));
	}

	/**
   * @brief Map an existing, already-initialized pool (peer side).
   *
   * Reads slot_count/slot_data_bytes from the in-memory header written by create().
   */
	static FrameSlotPool attach(void *mem)
	{
		return from_handle(oakengine_ipc_framepool_attach(mem));
	}

	bool is_valid() const
	{
		return oakengine_ipc_framepool_is_valid(handle_) != 0;
	}

	uint32_t slot_count() const
	{
		return oakengine_ipc_framepool_slot_count(handle_);
	}

	size_t slot_data_bytes() const
	{
		return oakengine_ipc_framepool_slot_data_bytes(handle_);
	}

	// ---- Filler side ----

	/**
   * @brief Take ownership of a free slot. Returns false (and leaves *index untouched) if none free.
   */
	bool acquire(uint32_t *index)
	{
		return oakengine_ipc_framepool_acquire(handle_, index) != 0;
	}

	/**
   * @brief Pointer to a slot's pixel data block (slot_data_bytes available).
   */
	void *slot_data(uint32_t index)
	{
		return oakengine_ipc_framepool_slot_data(handle_, index);
	}

	/**
   * @brief Mutable metadata for a slot. Filler writes this before publish().
   */
	FrameSlotMeta *meta(uint32_t index)
	{
		return oakengine_ipc_framepool_meta(handle_, index);
	}

	/**
   * @brief Publish a filled slot to the drainer. Must follow a successful acquire() of `index`.
   */
	bool publish(uint32_t index)
	{
		return oakengine_ipc_framepool_publish(handle_, index) != 0;
	}

	// ---- Drainer side ----

	/**
   * @brief Take the next published slot. Returns false if nothing is ready.
   */
	bool consume(uint32_t *index)
	{
		return oakengine_ipc_framepool_consume(handle_, index) != 0;
	}

	/**
   * @brief Return a consumed slot to the free pool for reuse. Must follow consume() of `index`.
   */
	bool release(uint32_t index)
	{
		return oakengine_ipc_framepool_release(handle_, index) != 0;
	}

	const FrameSlotMeta *meta(uint32_t index) const
	{
		return oakengine_ipc_framepool_meta_const(handle_, index);
	}

	const void *slot_data(uint32_t index) const
	{
		return oakengine_ipc_framepool_slot_data_const(handle_, index);
	}

	/**
   * @brief The wrapped C handle, for cross-type wrappers and direct C API use
   */
	OakFrameSlotPool *handle() const
	{
		return handle_;
	}

	/**
   * @brief Wraps an owned C handle (takes ownership)
   */
	static FrameSlotPool from_handle(OakFrameSlotPool *handle)
	{
		return FrameSlotPool(handle);
	}

private:
	explicit FrameSlotPool(OakFrameSlotPool *handle)
		: handle_(handle)
	{
	}

	OakFrameSlotPool *handle_ = nullptr;
};

} // namespace ipc
} // namespace olive

#endif // OAK_IPC_FRAMESLOTPOOL_H
