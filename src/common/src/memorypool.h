/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2022 Olive Team
  Modifications Copyright (C) 2025 mikesolar

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

#ifndef OAK_MEMORYPOOL_H
#define OAK_MEMORYPOOL_H

#include <chrono>
#include <cstdio>
#include <list>
#include <memory>
#include <mutex>
#include <stdint.h>
#include <vector>

#include "define.h"

namespace olive
{

/**
 * @brief MemoryPool base class
 *
 * A custom memory system that allocates several objects in a large chunk (as opposed to several small
 * allocations). Improves performance and memory consumption.
 *
 * As a class, this base is usable by subclassing and overriding get_element_size() if elements are larger than
 * one byte. The pool will then allocate `(element_count * get_element_size())` per arena. Arenas are allocated
 * and destroyed on the fly - when an arena fills up, another is allocated.
 *
 * get() will return an ElementPtr. The original desired data can be accessed through ElementPtr::data(). This data
 * will belong to the caller until ElementPtr goes out of scope and the memory is freed back into the pool.
 *
 * Empty arenas are NOT reaped automatically anymore (the Qt version used a QTimer driven by the Qt event loop).
 * Call clear_empty_arenas() periodically from whatever scheduling mechanism the host application provides.
 */
class MemoryPool {
public:
	/**
	 * @brief Constructor
	 * @param element_count
	 *
	 * Number of elements per arena
	 */
	MemoryPool(int element_count)
	{
		element_count_ = element_count;
	}

	/**
	 * @brief Destructor
	 *
	 * Deletes all arenas.
	 */
	virtual ~MemoryPool()
	{
		clear();
	}

	DISABLE_COPY_MOVE(MemoryPool)

	/**
	 * @brief Clears all arenas, freeing all of their memory
	 *
	 * Note that this function is not safe, any elements that are still out there will be invalid
	 * and accessing them will cause a crash. You'll need to make sure all elements are already
	 * relinquished before then.
	 */
	void clear()
	{
		std::lock_guard<std::mutex> locker(lock_);

		for (Arena *a : arenas_) {
			delete a;
		}
		arenas_.clear();
	}

	/**
	 * @brief Returns whether any arenas are successfully allocated
	 */
	inline bool is_allocated() const
	{
		return !arenas_.empty();
	}

	/**
	 * @brief Returns current number of allocated arenas
	 */
	inline int get_arena_count() const
	{
		return arenas_.size();
	}

	/**
	 * @brief Deletes arenas that have been empty for at least kMaxEmptyArenaLife milliseconds
	 *
	 * In the Qt version this was a private slot invoked periodically by a QTimer. Without Qt it is now a
	 * public function that the caller is responsible for invoking (e.g. from a std::thread timer).
	 */
	void clear_empty_arenas()
	{
		std::lock_guard<std::mutex> locker(lock_);

		const int64_t min_time = now_ms() - kMaxEmptyArenaLife;

		for (auto it = arenas_.begin(); it != arenas_.end();) {
			Arena *arena = (*it);

			if (arena->get_usage_count() == 0 &&
				arena->time_arena_was_made_empty() <= min_time) {
				delete arena;
				it = arenas_.erase(it);
			} else {
				it++;
			}
		}
	}

	class Arena;

	/**
	 * @brief A handle for a chunk of memory in an arena
	 *
	 * Calling get() on the pool or arena will return a shared pointer to an element which will contain a pointer to
	 * the desired object/data in data(). When Element is destroyed (i.e. when ElementPtr goes out of scope), the
	 * memory is released back into the pool so it can be used by another class.
	 */
	class Element {
	public:
		/**
		 * @brief Element Constructor
		 *
		 * There is no need to use this outside of the memory pool's internal functions.
		 */
		Element(Arena *parent, uint8_t *data)
		{
			parent_ = parent;
			data_ = data;
			accessed_ = now_ms();
		}

		/**
		 * @brief Element Destructor
		 *
		 * Automatically releases this element's memory back to the arena it was retrieved from.
		 */
		~Element()
		{
			release();
		}

		DISABLE_COPY_MOVE(Element)

		/**
		 * @brief Access data represented in the pool
		 */
		inline uint8_t *data() const
		{
			return data_;
		}

		inline const int64_t &timestamp() const
		{
			return timestamp_;
		}

		inline void set_timestamp(const int64_t &timestamp)
		{
			timestamp_ = timestamp;
		}

		/**
		 * @brief Register that this element has been accessed
		 *
		 * \see last_accessed()
		 */
		inline void access()
		{
			accessed_ = now_ms();
		}

		/**
		 * @brief Returns the last time `access()` was called on this function
		 *
		 * Useful for determining the relative age of an element (i.e. if it hasn't been accessed for a certain amount
		 * of time, it can probably be freed back into the pool). This requires all usages to call `access()`.
		 */
		inline const int64_t &last_accessed() const
		{
			return accessed_;
		}

		void release()
		{
			if (data_) {
				parent_->release(this);
				data_ = nullptr;
			}
		}

	private:
		Arena *parent_;

		uint8_t *data_;

		int64_t timestamp_;

		int64_t accessed_;
	};

	using ElementPtr = std::shared_ptr<Element>;

	/**
	 * @brief A memory pool arena - a subsection of memory
	 *
	 * The pool itself does not store memory, it stores "arenas". This is so that the pool can handle the situation of
	 * an arena becoming full with no more memory to lend. A pool can automatically allocate another arena and
	 * continue providing memory (and freeing arenas when they're no longer in use).
	 */
	class Arena {
	public:
		Arena(MemoryPool *parent)
		{
			parent_ = parent;
			data_ = nullptr;
			allocated_sz_ = 0;
			empty_time_ = now_ms();
		}

		~Arena()
		{
			std::list<Element *> copy = lent_elements_;
			for (Element *e : copy) {
				e->release();
			}

			delete[] data_;
		}

		DISABLE_COPY_MOVE(Arena)

		/**
		 * @brief Returns an element if there is free memory to do so
		 */
		ElementPtr get()
		{
			std::lock_guard<std::mutex> locker(lock_);

			for (size_t i = 0; i < available_.size(); i++) {
				if (available_.at(i)) {
					// This buffer is available
					available_[i] = false;

					ElementPtr e = std::make_shared<Element>(
						this,
						reinterpret_cast<uint8_t *>(data_ + i * element_sz_));
					lent_elements_.push_back(e.get());

					return e;
				}
			}

			return nullptr;
		}

		/**
		 * @brief Releases an element back into the pool for use elsewhere
		 */
		void release(Element *e)
		{
			std::lock_guard<std::mutex> locker(lock_);
			uintptr_t diff = reinterpret_cast<uintptr_t>(e->data()) -
							 reinterpret_cast<uintptr_t>(data_);

			size_t index = diff / element_sz_;

			available_[index] = true;

			lent_elements_.remove(e);

			if (lent_elements_.empty()) {
				empty_time_ = now_ms();
			}
		}

		int get_usage_count()
		{
			std::lock_guard<std::mutex> locker(lock_);
			return lent_elements_.size();
		}

		bool allocate(size_t ele_sz, size_t nb_elements)
		{
			if (is_allocated()) {
				return true;
			}

			element_sz_ = ele_sz;

			allocated_sz_ = element_sz_ * nb_elements;

			if ((data_ = new (std::nothrow) uint8_t[allocated_sz_])) {
				available_.assign(nb_elements, true);

				return true;
			} else {
				available_.clear();

				return false;
			}
		}

		inline int get_element_count() const
		{
			return available_.size();
		}

		inline bool is_allocated() const
		{
			return data_;
		}

		inline int64_t time_arena_was_made_empty()
		{
			std::lock_guard<std::mutex> locker(lock_);
			return empty_time_;
		}

	private:
		MemoryPool *parent_;

		uint8_t *data_;

		size_t allocated_sz_;

		std::vector<bool> available_;

		std::mutex lock_;

		size_t element_sz_;

		std::list<Element *> lent_elements_;

		int64_t empty_time_;
	};

	/**
	 * @brief Retrieves an element from an available arena
	 */
	ElementPtr get()
	{
		std::lock_guard<std::mutex> locker(lock_);

		// Attempt to get an element from an arena
		for (Arena *a : arenas_) {
			ElementPtr e = a->get();

			if (e) {
				return e;
			}
		}

		// All arenas were empty, we'll need to create a new one
		size_t ele_sz = get_element_size();

		if (!ele_sz) {
			fprintf(stderr, "Failed to create arena, element size was 0\n");
			return nullptr;
		}

		if (element_count_ <= 0) {
			fprintf(stderr, "Failed to create arena, element count was invalid: %d\n",
					element_count_);
			return nullptr;
		}

		Arena *a = new Arena(this);
		if (!a->allocate(ele_sz, element_count_)) {
			fprintf(stderr,
					"Failed to create arena, allocation failed. Out of memory?\n");
			delete a;
			return nullptr;
		}

		arenas_.push_back(a);
		return a->get();
	}

protected:
	/**
	 * @brief The size of each element
	 *
	 * Override this to use a custom size (e.g. a char array where the element size is > 1)
	 */
	virtual size_t get_element_size()
	{
		return sizeof(uint8_t);
	}

private:
	/**
	 * @brief Milliseconds since the epoch (replaces QDateTime::currentMSecsSinceEpoch())
	 */
	static int64_t now_ms()
	{
		return std::chrono::duration_cast<std::chrono::milliseconds>(
				   std::chrono::system_clock::now().time_since_epoch())
			.count();
	}

	int element_count_;

	std::list<Arena *> arenas_;

	std::mutex lock_;

	static const int64_t kMaxEmptyArenaLife = 5000;
};

}

#endif // OAK_MEMORYPOOL_H
