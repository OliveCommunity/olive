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

#include "diskmanager.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "configaccessor.h"
#include "coreengine.h"
#include "filefunctions.h"

namespace olive
{

DiskManager *DiskManager::instance_ = nullptr;

DiskManager::ShowDiskCacheSettingsHandler
	DiskManager::show_disk_cache_settings_handler_;

namespace
{

int64_t current_msecs_since_epoch()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(
			   std::chrono::system_clock::now().time_since_epoch())
		.count();
}

} // namespace

DiskManager::DiskManager()
{
	// Add default cache location
	std::ifstream default_disk_cache_file(get_default_disk_cache_config_file(),
										  std::ios::binary);
	if (default_disk_cache_file.is_open()) {
		std::stringstream ss;
		ss << default_disk_cache_file.rdbuf();
		std::string default_dir = ss.str();

		if (!default_dir.empty()) {
			if (FileFunctions::directory_is_valid(default_dir)) {
				get_open_folder(default_dir);
			} else {
				// The UI warning (QMessageBox) moved to the app layer; the
				// engine falls back to the default cache location.
				fprintf(stderr,
						"Disk Cache Error: Unable to set custom application disk "
						"cache. Using default instead.\n");
			}
		}
	}

	// If no custom default was loaded, load default
	if (open_folders_.empty()) {
		get_open_folder(get_default_disk_cache_path());
	}

	std::string disk_cache_index_path =
		(std::filesystem::path(FileFunctions::get_configuration_location()) /
		 "diskcache2")
			.string();

	std::ifstream disk_cache_index(disk_cache_index_path);
	if (disk_cache_index.is_open()) {
		std::string line;
		while (std::getline(disk_cache_index, line)) {
			get_open_folder(line);
		}
	}
}

DiskManager::~DiskManager()
{
	std::ofstream default_disk_cache_file(get_default_disk_cache_config_file(),
										  std::ios::binary | std::ios::trunc);
	if (default_disk_cache_file.is_open()) {
		if (get_default_disk_cache_path() != get_default_cache_path()) {
			default_disk_cache_file << get_default_cache_path();
		}
	}

	// DiskCacheFolder children used to be deleted via QObject parentship
	for (DiskCacheFolder *f : open_folders_) {
		delete f;
	}
	open_folders_.clear();
}

void DiskManager::create_instance()
{
	instance_ = new DiskManager();
}

void DiskManager::destroy_instance()
{
	delete instance_;
	instance_ = nullptr;
}

DiskManager *DiskManager::instance()
{
	// Lazy self-create: the Qt app called create_instance() at startup, but
	// library consumers (oaknode standalone tests) may reach instance()
	// without any facade having run. Matches FrameManager callers' tolerance
	// for a missing instance by guaranteeing one exists instead.
	if (!instance_) {
		create_instance();
	}
	return instance_;
}

void DiskManager::accessed(const std::string &cache_folder,
						   const std::string &filename)
{
	DiskCacheFolder *f = get_open_folder(cache_folder);

	f->accessed(filename);
}

void DiskManager::created_file(const std::string &cache_folder,
							   const std::string &filename)
{
	DiskCacheFolder *f = get_open_folder(cache_folder);

	f->created_file(filename);
}

void DiskManager::delete_specific_file(const std::string &filename)
{
	for (DiskCacheFolder *f : open_folders_) {
		f->delete_specific_file(filename);
	}
}

bool DiskManager::clear_disk_cache(const std::string &cache_folder)
{
	DiskCacheFolder *f = get_open_folder(cache_folder);

	return f->clear_cache();
}

DiskCacheFolder *DiskManager::get_open_folder(const std::string &path)
{
	// If path is empty, this must mean default
	if (path.empty()) {
		return get_default_cache_folder();
	}

	// See if we have an existing path with this name
	for (DiskCacheFolder *f : open_folders_) {
		if (f->get_path() == path) {
			return f;
		}
	}

	// We must have to open this folder
	DiskCacheFolder *f = new DiskCacheFolder(path);
	f->add_deleted_frame_handler(
		[this](const std::string &p, const std::string &fn) {
			emit_deleted_frame(p, fn);
		});
	open_folders_.push_back(f);

	return f;
}

std::string DiskManager::get_default_disk_cache_config_file()
{
	return (std::filesystem::path(FileFunctions::get_configuration_location()) /
			"defaultdiskcache")
		.string();
}

std::string DiskManager::get_default_disk_cache_path()
{
	// QStandardPaths::AppLocalDataLocation equivalent: the configuration
	// location is the app data root on all platforms.
	return (std::filesystem::path(FileFunctions::get_configuration_location()) /
			"mediacache")
		.string();
}

void DiskManager::set_show_disk_cache_settings_handler(
	ShowDiskCacheSettingsHandler handler)
{
	show_disk_cache_settings_handler_ = std::move(handler);
}

void DiskManager::show_disk_cache_settings_dialog(DiskCacheFolder *folder)
{
	if (show_disk_cache_settings_handler_) {
		show_disk_cache_settings_handler_(folder);
		return;
	}

	fprintf(stderr,
			"No disk cache settings dialog handler registered, skipping\n");
}

void DiskManager::show_disk_cache_settings_dialog(const std::string &path)
{
	if (!FileFunctions::directory_is_valid(path)) {
		// The UI error dialog (QMessageBox) moved to the app layer
		fprintf(stderr,
				"Disk Cache Error: Failed to open disk cache at \"%s\". Try a "
				"different folder.\n",
				path.c_str());
		return;
	}

	DiskCacheFolder *folder = get_open_folder(path);

	show_disk_cache_settings_dialog(folder);
}

size_t DiskManager::add_deleted_frame_handler(DeletedFrameHandler handler)
{
	size_t id = next_handler_id_++;
	deleted_frame_handlers_[id] = std::move(handler);
	return id;
}

void DiskManager::remove_deleted_frame_handler(size_t id)
{
	deleted_frame_handlers_.erase(id);
}

size_t DiskManager::add_invalidate_project_handler(
	InvalidateProjectHandler handler)
{
	size_t id = next_handler_id_++;
	invalidate_project_handlers_[id] = std::move(handler);
	return id;
}

void DiskManager::remove_invalidate_project_handler(size_t id)
{
	invalidate_project_handlers_.erase(id);
}

void DiskManager::emit_deleted_frame(const std::string &path,
									 const std::string &filename)
{
	for (const auto &e : deleted_frame_handlers_) {
		e.second(path, filename);
	}
}

void DiskManager::emit_invalidate_project(Project *p)
{
	for (const auto &e : invalidate_project_handlers_) {
		e.second(p);
	}
}

DiskCacheFolder::DiskCacheFolder(const std::string &path)
{
	set_path(path);

	// QTimer replacement: periodic index save on a background thread. The
	// timer used to fire in DiskManager's (GUI) thread; cross-thread callers
	// reached the folder through queued QMetaObject invocations.
	int interval = OAK_CONFIG("DiskCacheSaveInterval").toInt();
	if (interval <= 0) {
		// Defensive fallback; the compiled-in config default is 10000 ms
		interval = 10000;
	}
	save_thread_stop_ = false;
	save_thread_ = std::thread([this, interval]() {
		int64_t elapsed = 0;
		while (!save_thread_stop_) {
			int64_t step = std::min<int64_t>(50, interval - elapsed);
			std::this_thread::sleep_for(std::chrono::milliseconds(step));
			if (save_thread_stop_) {
				break;
			}
			elapsed += step;
			if (elapsed >= interval) {
				elapsed = 0;
				save_disk_cache_index();
			}
		}
	});
}

DiskCacheFolder::~DiskCacheFolder()
{
	save_thread_stop_ = true;
	if (save_thread_.joinable()) {
		save_thread_.join();
	}

	close_cache_folder();
}

bool DiskCacheFolder::clear_cache()
{
	std::lock_guard<std::recursive_mutex> lock(data_mutex_);

	bool deleted_files = true;

	auto i = disk_data_.begin();

	while (i != disk_data_.end()) {
		// We return a false result if any of the files fail to delete, but still try to delete as many as we can
		std::string filename = i->first;

		std::error_code ec;
		bool removed = std::filesystem::remove(filename, ec);
		std::error_code ec2;
		if (removed || !std::filesystem::exists(filename, ec2)) {
			emit_deleted_frame(path_, filename);
			i = disk_data_.erase(i);
		} else {
			fprintf(stderr, "Failed to delete %s\n", filename.c_str());
			deleted_files = false;
			i++;
		}
	}

	return deleted_files;
}

void DiskCacheFolder::accessed(const std::string &filename)
{
	std::lock_guard<std::recursive_mutex> lock(data_mutex_);

	if (!disk_data_.count(filename)) {
		return;
	}

	disk_data_[filename].access_time = current_msecs_since_epoch();
}

void DiskCacheFolder::created_file(const std::string &filename)
{
	std::lock_guard<std::recursive_mutex> lock(data_mutex_);

	std::error_code ec;
	int64_t file_size = int64_t(std::filesystem::file_size(filename, ec));
	if (ec) {
		file_size = 0;
	}

	disk_data_.insert({ filename, { file_size, current_msecs_since_epoch() } });

	consumption_ += file_size;

	while (consumption_ > limit_) {
		delete_least_recent();
	}
}

void DiskCacheFolder::set_path(const std::string &path)
{
	std::lock_guard<std::recursive_mutex> lock(data_mutex_);

	// If this is currently set to a folder, close it out now
	close_cache_folder();

	// Signal that disk cache is gone
	if (!disk_data_.empty()) {
		for (auto it = disk_data_.cbegin(); it != disk_data_.cend(); it++) {
			emit_deleted_frame(path_, it->first);
		}
		disk_data_.clear();
	}

	// Set defaults
	clear_on_close_ = false;
	consumption_ = 0;
	limit_ = 21474836480; // Default to 20 GB

	// Set path
	path_ = path;

	// Attempt to load existing index file from path
	FileFunctions::directory_is_valid(path_);

	index_path_ = (std::filesystem::path(path_) / "index").string();

	// Try to load any current cache index from file
	std::FILE *cache_index_file = std::fopen(index_path_.c_str(), "rb");

	if (cache_index_file) {
		BinaryStreamReader ds(cache_index_file);

		ds >> limit_;
		ds >> clear_on_close_;

		while (!ds.at_end()) {
			std::string filename;
			HashTime h;

			ds >> filename;
			ds >> h.file_size;
			ds >> h.access_time;

			std::error_code ec;
			if (std::filesystem::exists(filename, ec)) {
				consumption_ += h.file_size;
				disk_data_.insert({ filename, h });
			}
		}

		std::fclose(cache_index_file);
	}
}

bool DiskCacheFolder::delete_file_internal(
	std::map<std::string, HashTime>::iterator hash_to_delete)
{
	// Cache HashTime object
	std::string filename = hash_to_delete->first;
	HashTime ht = hash_to_delete->second;

	// Remove from disk
	std::error_code ec;
	bool removed = std::filesystem::remove(filename, ec);
	std::error_code ec2;
	bool exists = std::filesystem::exists(filename, ec2);

	if (!exists || removed) {
		// Remove from internal map
		disk_data_.erase(hash_to_delete);

		// Reduce consumption
		consumption_ -= ht.file_size;

		emit_deleted_frame(path_, filename);
		return true;
	}

	return false;
}

bool DiskCacheFolder::delete_specific_file(const std::string &f)
{
	std::lock_guard<std::recursive_mutex> lock(data_mutex_);

	for (auto it = disk_data_.begin(); it != disk_data_.end(); it++) {
		if (it->first == f) {
			// Break out of this loop, assuming we'll only have one instance_ of each filename
			return delete_file_internal(it);
		}
	}

	return false;
}

bool DiskCacheFolder::delete_least_recent()
{
	auto hash_to_delete = disk_data_.begin();

	if (disk_data_.begin() != disk_data_.end()) {
		for (auto it = std::next(disk_data_.begin()); it != disk_data_.end(); it++) {
			if (it->second.access_time < hash_to_delete->second.access_time) {
				hash_to_delete = it;
			}
		}

		bool e = delete_file_internal(hash_to_delete);

		if (e) {
			EngineCore::instance()->warn_cache_full();
		}

		return e;
	} else {
		return false;
	}
}

void DiskCacheFolder::close_cache_folder()
{
	if (path_.empty()) {
		return;
	}

	if (clear_on_close_) {
		// If we're not moving to new and we're set to clear on close, clear now or else it'll never
		// get cleared later
		clear_cache();
	}

	// Save current cache index
	save_disk_cache_index();
}

void DiskCacheFolder::save_disk_cache_index()
{
	std::lock_guard<std::recursive_mutex> lock(data_mutex_);

	std::FILE *cache_index_file = std::fopen(index_path_.c_str(), "wb");

	if (cache_index_file) {
		BinaryStreamWriter ds(cache_index_file);

		ds << limit_;
		ds << clear_on_close_;

		for (auto it = disk_data_.cbegin(); it != disk_data_.cend(); it++) {
			const HashTime &ht = it->second;

			ds << it->first;
			ds << ht.file_size;
			ds << ht.access_time;
		}

		std::fclose(cache_index_file);
	} else {
		fprintf(stderr, "Failed to write cache index: %s\n", index_path_.c_str());
	}
}

size_t DiskCacheFolder::add_deleted_frame_handler(DeletedFrameHandler handler)
{
	size_t id = next_handler_id_++;
	deleted_frame_handlers_[id] = std::move(handler);
	return id;
}

void DiskCacheFolder::remove_deleted_frame_handler(size_t id)
{
	deleted_frame_handlers_.erase(id);
}

void DiskCacheFolder::emit_deleted_frame(const std::string &path,
										 const std::string &filename)
{
	for (const auto &e : deleted_frame_handlers_) {
		e.second(path, filename);
	}
}

}
