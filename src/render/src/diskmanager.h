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

#ifndef OAK_DISKMANAGER_H
#define OAK_DISKMANAGER_H

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "binarystream.h"
#include "define.h"
#include "project.h"

namespace olive
{

class DiskCacheFolder {
public:
	DiskCacheFolder(const std::string &path);

	~DiskCacheFolder();

	bool clear_cache();

	void accessed(const std::string &filename);

	void created_file(const std::string &filename);

	const std::string &get_path() const
	{
		return path_;
	}

	void set_path(const std::string &path);

	int64_t get_limit() const
	{
		return limit_;
	}

	/**
	 * @brief Bytes currently consumed by tracked files in this folder.
	 *
	 * Non-const because the counter is guarded by the (non-mutable)
	 * data mutex. Exposed for the oakrender C ABI
	 * (oakrender_disk_cache_size(), M7 §2.4).
	 */
	int64_t get_consumption()
	{
		std::lock_guard<std::recursive_mutex> locker(data_mutex_);
		return consumption_;
	}

	bool get_clear_on_close() const
	{
		return clear_on_close_;
	}

	void set_limit(int64_t l)
	{
		limit_ = l;
	}

	void set_clear_on_close(bool e)
	{
		clear_on_close_ = e;
	}

	bool delete_specific_file(const std::string &f);

	// Explicit handler list replacing the `deleted_frame` signal
	using DeletedFrameHandler =
		std::function<void(const std::string &path, const std::string &filename)>;
	size_t add_deleted_frame_handler(DeletedFrameHandler handler);
	void remove_deleted_frame_handler(size_t id);

private:
	struct HashTime {
		int64_t file_size;
		int64_t access_time;
	};

	bool delete_file_internal(std::map<std::string, HashTime>::iterator hash_to_delete);

	bool delete_least_recent();

	void close_cache_folder();

	void emit_deleted_frame(const std::string &path, const std::string &filename);

	std::string path_;

	std::string index_path_;

	std::map<std::string, HashTime> disk_data_;

	int64_t consumption_;

	int64_t limit_;

	bool clear_on_close_;

	// Guards disk_data_/consumption_. The QObject version was serialized by
	// thread affinity (queued QMetaObject calls + GUI-thread timer); with
	// direct cross-thread calls and a background save thread, a mutex takes
	// that role.
	std::recursive_mutex data_mutex_;

	// QTimer replacement: periodic save of the disk cache index on a
	// background thread (the timer used to fire in the GUI thread)
	std::thread save_thread_;
	std::atomic<bool> save_thread_stop_;

	std::map<size_t, DeletedFrameHandler> deleted_frame_handlers_;
	size_t next_handler_id_ = 1;

	// Formerly a QTimer timeout slot
	void save_disk_cache_index();
};

class DiskManager {
public:
	static void create_instance();

	static void destroy_instance();

	static DiskManager *instance();

	bool clear_disk_cache(const std::string &cache_folder);

	DiskCacheFolder *get_default_cache_folder() const
	{
		// The first folder will always be the default
		return open_folders_.front();
	}

	const std::string &get_default_cache_path() const
	{
		return get_default_cache_folder()->get_path();
	}

	DiskCacheFolder *get_open_folder(const std::string &path);

	const std::vector<DiskCacheFolder *> &get_open_folders() const
	{
		return open_folders_;
	}

	static std::string get_default_disk_cache_config_file();

	static std::string get_default_disk_cache_path();

	/**
	 * @brief Handler showing the disk cache settings dialog for a folder
	 *
	 * Registered by the UI layer (e.g. a DiskCacheDialog-based
	 * implementation), since the engine cannot show dialogs itself. Without
	 * a handler, the request is logged and skipped.
	 */
	using ShowDiskCacheSettingsHandler =
		std::function<void(DiskCacheFolder *folder)>;

	static void set_show_disk_cache_settings_handler(
		ShowDiskCacheSettingsHandler handler);

	void show_disk_cache_settings_dialog(DiskCacheFolder *folder);
	void show_disk_cache_settings_dialog(const std::string &path);

	// Formerly slots invoked cross-thread via QMetaObject; now direct calls.
	void accessed(const std::string &cache_folder, const std::string &filename);

	void created_file(const std::string &cache_folder, const std::string &filename);

	void delete_specific_file(const std::string &filename);

	// Explicit handler lists replacing the `deleted_frame` /
	// `invalidate_project` signals (subscribers: FrameHashCache et al.)
	using DeletedFrameHandler = DiskCacheFolder::DeletedFrameHandler;
	size_t add_deleted_frame_handler(DeletedFrameHandler handler);
	void remove_deleted_frame_handler(size_t id);

	using InvalidateProjectHandler = std::function<void(Project *p)>;
	size_t add_invalidate_project_handler(InvalidateProjectHandler handler);
	void remove_invalidate_project_handler(size_t id);

	void emit_deleted_frame(const std::string &path, const std::string &filename);
	void emit_invalidate_project(Project *p);

private:
	DiskManager();

	~DiskManager();

	static DiskManager *instance_;

	static ShowDiskCacheSettingsHandler show_disk_cache_settings_handler_;

	std::vector<DiskCacheFolder *> open_folders_;

	std::map<size_t, DeletedFrameHandler> deleted_frame_handlers_;
	std::map<size_t, InvalidateProjectHandler> invalidate_project_handlers_;
	size_t next_handler_id_ = 1;
};

}

#endif // OAK_DISKMANAGER_H
