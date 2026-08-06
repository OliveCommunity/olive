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

#ifndef OAK_FRAMEMANAGER_H
#define OAK_FRAMEMANAGER_H

#include <atomic>
#include <cstdint>
#include <list>
#include <map>
#include <mutex>
#include <thread>

namespace olive
{

class FrameManager {
public:
	static void create_instance();

	static void destroy_instance();

	static FrameManager *instance();

	static char *allocate(int size);

	static void deallocate(int size, char *buffer);

private:
	FrameManager();

	~FrameManager();

	FrameManager(const FrameManager &) = delete;
	FrameManager &operator=(const FrameManager &) = delete;

	/**
   * @brief Allocate buffer
   *
   * Caller takes ownership of buffer and can delete it if they want. It can also be returned to
   * the manager with Deallocate and potentially be re-used later.
   *
   * Thread-safe.
   */
	char *allocate_from_pool(int size);

	/**
   * @brief Deallocate buffer
   *
   * Manager will take ownership and buffer will stay allocated for some time in case it can be
   * re-used.
   *
   * Thread-safe.
   */
	void deallocate_to_pool(int size, char *buffer);

	static FrameManager *instance_;

	static const int k_frame_lifetime;

	struct Buffer {
		int64_t time;
		char *data;
	};

	std::map<int, std::list<Buffer>> pool_;

	std::mutex mutex_;

	// QTimer replacement: periodic garbage collection on a background
	// thread (the timer used to fire in the GUI thread)
	std::thread gc_thread_;
	std::atomic<bool> gc_thread_stop_;

	// Formerly a QTimer timeout slot
	void garbage_collection();
};

}

#endif // OAK_FRAMEMANAGER_H
