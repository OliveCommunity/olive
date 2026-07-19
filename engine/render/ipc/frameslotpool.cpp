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

#include "oliveimpl/render/ipc/frameslotpool.h"

#include <cstring>

namespace olive
{
namespace engine
{
namespace internal
{
namespace ipc
{

namespace
{

// Round `value` up to the next multiple of `align` (align must be a power of two).
size_t align_up(size_t value, size_t align)
{
	return (value + (align - 1)) & ~(align - 1);
}

constexpr size_t k_align = 64; // Cache-line alignment for each sub-region.

} // namespace

size_t FrameSlotPool::bytes_needed(uint32_t slot_count, size_t slot_data_bytes)
{
	const uint32_t ring_cap = ring_capacity(slot_count);
	size_t total = align_up(sizeof(Header), k_align);
	total +=
		align_up(olive::ipc::SpscRingBuffer::bytes_needed(ring_cap), k_align); // free ring
	total +=
		align_up(olive::ipc::SpscRingBuffer::bytes_needed(ring_cap), k_align); // ready ring
	total +=
		align_up(sizeof(FrameSlotMeta) * slot_count, k_align); // metadata array
	total += align_up(slot_data_bytes, k_align) * slot_count; // pixel data blocks
	return total;
}

FrameSlotPool FrameSlotPool::create(void *mem, uint32_t slot_count,
									size_t slot_data_bytes)
{
	FrameSlotPool pool;
	pool.base_ = reinterpret_cast<uint8_t *>(mem);

	const uint32_t ring_cap = ring_capacity(slot_count);

	size_t offset = 0;
	const size_t header_off = offset;
	offset += align_up(sizeof(Header), k_align);

	const size_t free_off = offset;
	offset += align_up(olive::ipc::SpscRingBuffer::bytes_needed(ring_cap), k_align);

	const size_t ready_off = offset;
	offset += align_up(olive::ipc::SpscRingBuffer::bytes_needed(ring_cap), k_align);

	const size_t meta_off = offset;
	offset += align_up(sizeof(FrameSlotMeta) * slot_count, k_align);

	const size_t data_off = offset;

	pool.header_ = reinterpret_cast<Header *>(pool.base_ + header_off);
	pool.header_->magic = k_magic;
	pool.header_->slot_count = slot_count;
	pool.header_->slot_data_bytes = slot_data_bytes;
	pool.header_->free_ring_offset = free_off;
	pool.header_->ready_ring_offset = ready_off;
	pool.header_->meta_offset = meta_off;
	pool.header_->data_offset = data_off;

	pool.free_ring_ = olive::ipc::SpscRingBuffer::create(pool.base_ + free_off, ring_cap);
	pool.ready_ring_ = olive::ipc::SpscRingBuffer::create(pool.base_ + ready_off, ring_cap);
	pool.meta_ = reinterpret_cast<FrameSlotMeta *>(pool.base_ + meta_off);
	pool.data_ = pool.base_ + data_off;

	memset(pool.meta_, 0, sizeof(FrameSlotMeta) * slot_count);

	// Seed the free ring with every slot index so the filler can Acquire() immediately.
	for (uint32_t i = 0; i < slot_count; i++) {
		pool.free_ring_->push(i);
	}

	return pool;
}

FrameSlotPool FrameSlotPool::attach(void *mem)
{
	FrameSlotPool pool;
	pool.base_ = reinterpret_cast<uint8_t *>(mem);
	pool.header_ = reinterpret_cast<Header *>(pool.base_);

	if (pool.header_->magic != k_magic) {
		// Caller will see IsValid() == false via a null header reset.
		pool.header_ = nullptr;
		pool.base_ = nullptr;
		return pool;
	}

	pool.free_ring_ =
		olive::ipc::SpscRingBuffer::attach(pool.base_ + pool.header_->free_ring_offset);
	pool.ready_ring_ =
		olive::ipc::SpscRingBuffer::attach(pool.base_ + pool.header_->ready_ring_offset);
	pool.meta_ = reinterpret_cast<FrameSlotMeta *>(pool.base_ +
												   pool.header_->meta_offset);
	pool.data_ = pool.base_ + pool.header_->data_offset;

	return pool;
}

uint32_t FrameSlotPool::slot_count() const
{
	return header_ ? header_->slot_count : 0;
}

size_t FrameSlotPool::slot_data_bytes() const
{
	return header_ ? size_t(header_->slot_data_bytes) : 0;
}

bool FrameSlotPool::acquire(uint32_t *index)
{
	return free_ring_->pop(index);
}

void *FrameSlotPool::slot_data(uint32_t index)
{
	return data_ + size_t(index) * align_up(slot_data_bytes(), k_align);
}

const void *FrameSlotPool::slot_data(uint32_t index) const
{
	return data_ + size_t(index) * align_up(slot_data_bytes(), k_align);
}

FrameSlotMeta *FrameSlotPool::meta(uint32_t index)
{
	return &meta_[index];
}

const FrameSlotMeta *FrameSlotPool::meta(uint32_t index) const
{
	return &meta_[index];
}

bool FrameSlotPool::publish(uint32_t index)
{
	return ready_ring_->push(index);
}

bool FrameSlotPool::consume(uint32_t *index)
{
	return ready_ring_->pop(index);
}

bool FrameSlotPool::release(uint32_t index)
{
	return free_ring_->push(index);
}

} // namespace ipc
} // namespace internal
} // namespace engine
} // namespace olive