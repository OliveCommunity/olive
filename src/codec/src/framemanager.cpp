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

#include "framemanager.h"

#include <chrono>

namespace olive
{

FrameManager *FrameManager::instance_ = nullptr;
const int FrameManager::k_frame_lifetime = 5000;

static int64_t current_msecs_since_epoch()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(
			   std::chrono::system_clock::now().time_since_epoch())
		.count();
}

void FrameManager::create_instance()
{
	instance_ = new FrameManager();
}

void FrameManager::destroy_instance()
{
	delete instance_;
	instance_ = nullptr;
}

FrameManager *FrameManager::instance()
{
	return instance_;
}

char *FrameManager::allocate(int size)
{
	if (instance()) {
		return instance()->allocate_from_pool(size);
	} else {
		return new char[size];
	}
}

void FrameManager::deallocate(int size, char *buffer)
{
	if (instance()) {
		instance()->deallocate_to_pool(size, buffer);
	} else {
		delete[] buffer;
	}
}

FrameManager::FrameManager()
	: gc_thread_stop_(false)
{
	// Replaces the QTimer that fired garbage_collection() every
	// k_frame_lifetime ms
	gc_thread_ = std::thread([this]() {
		while (!gc_thread_stop_.load()) {
			std::this_thread::sleep_for(
				std::chrono::milliseconds(k_frame_lifetime));
			if (gc_thread_stop_.load()) {
				break;
			}
			garbage_collection();
		}
	});
}

char *FrameManager::allocate_from_pool(int size)
{
	std::lock_guard<std::mutex> locker(mutex_);

	std::list<Buffer> &buffer_list = pool_[size];
	char *buf = nullptr;

	if (buffer_list.empty()) {
		buf = new char[size];
	} else {
		// Take this buffer from the list
		buf = buffer_list.front().data;
		buffer_list.pop_front();
	}

	return buf;
}

void FrameManager::deallocate_to_pool(int size, char *buffer)
{
	std::lock_guard<std::mutex> locker(mutex_);

	std::list<Buffer> &buffer_list = pool_[size];

	buffer_list.push_back({ current_msecs_since_epoch(), buffer });
}

void FrameManager::garbage_collection()
{
	std::lock_guard<std::mutex> locker(mutex_);

	int64_t min_life = current_msecs_since_epoch() - k_frame_lifetime;

	for (auto it = pool_.begin(); it != pool_.end(); it++) {
		std::list<Buffer> &list = it->second;

		while (list.size() > 0 && list.front().time < min_life) {
			delete[] list.front().data;
			list.pop_front();
		}
	}
}

FrameManager::~FrameManager()
{
	gc_thread_stop_.store(true);
	if (gc_thread_.joinable()) {
		gc_thread_.join();
	}

	std::lock_guard<std::mutex> locker(mutex_);

	for (auto it = pool_.begin(); it != pool_.end(); it++) {
		std::list<Buffer> &list = it->second;
		for (auto jt = list.begin(); jt != list.end(); jt++) {
			delete[](*jt).data;
		}
	}

	pool_.clear();
}

}
