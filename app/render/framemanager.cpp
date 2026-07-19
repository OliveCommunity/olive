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

#include <QDateTime>
#include <QDebug>

namespace olive
{

FrameManager *FrameManager::instance_ = nullptr;
const int FrameManager::k_frame_lifetime = 5000;

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
{
	clear_timer_.setInterval(k_frame_lifetime);
	connect(&clear_timer_, &QTimer::timeout, this,
			&FrameManager::garbage_collection);
	clear_timer_.start();
}

char *FrameManager::allocate_from_pool(int size)
{
	QMutexLocker locker(&mutex_);

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
	QMutexLocker locker(&mutex_);

	std::list<Buffer> &buffer_list = pool_[size];

	buffer_list.push_back({ QDateTime::currentMSecsSinceEpoch(), buffer });
}

void FrameManager::garbage_collection()
{
	QMutexLocker locker(&mutex_);

	qint64 min_life = QDateTime::currentMSecsSinceEpoch() - k_frame_lifetime;

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
	QMutexLocker locker(&mutex_);

	for (auto it = pool_.begin(); it != pool_.end(); it++) {
		std::list<Buffer> &list = it->second;
		for (auto jt = list.begin(); jt != list.end(); jt++) {
			delete[] (*jt).data;
		}
	}

	pool_.clear();
}

}
