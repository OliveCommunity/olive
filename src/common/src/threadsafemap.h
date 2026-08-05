/*
 * Oak Video Editor - Non-Linear Video Editor
 * Copyright (C) 2025 Olive CE Team
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef OAK_THREADSAFEMAP_H
#define OAK_THREADSAFEMAP_H

#include <map>
#include <mutex>

/**
 * @brief A minimal mutex-guarded associative container
 *
 * Drop-in stdlib replacement for the former QMap/QMutex based template.
 */
template <typename K, typename V> class ThreadSafeMap {
public:
	ThreadSafeMap() = default;

	/**
	 * @brief Insert or overwrite the value stored under `key`
	 */
	void insert(const K &key, const V &value)
	{
		std::lock_guard<std::mutex> locker(mutex_);
		map_.insert_or_assign(key, value);
	}

	/**
	 * @brief Returns whether `key` exists in the map
	 */
	bool contains(const K &key) const
	{
		std::lock_guard<std::mutex> locker(mutex_);
		return map_.find(key) != map_.end();
	}

	/**
	 * @brief Copy the value stored under `key` into `out`
	 * @return true if the key existed, false otherwise (`out` left untouched)
	 */
	bool get(const K &key, V *out) const
	{
		std::lock_guard<std::mutex> locker(mutex_);
		auto it = map_.find(key);
		if (it == map_.end()) {
			return false;
		}
		*out = it->second;
		return true;
	}

	/**
	 * @brief Returns the number of entries in the map
	 */
	size_t size() const
	{
		std::lock_guard<std::mutex> locker(mutex_);
		return map_.size();
	}

private:
	mutable std::mutex mutex_;

	std::map<K, V> map_;
};

#endif // OAK_THREADSAFEMAP_H
