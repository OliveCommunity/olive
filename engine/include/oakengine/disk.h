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

#ifndef OAKENGINE_DISK_H
#define OAKENGINE_DISK_H

#include "export.h"
#include "init.h"
#include "project.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file disk.h
 * @brief C ABI for the engine's disk cache singleton (olive::DiskManager)
 *
 * A thin facade over DiskManager's instance lifecycle, default/custom cache
 * path management, cache clearing, settings dialog dispatch and project
 * invalidation. The opaque folder handle returned by
 * oakengine_disk_get_open_folder() is a borrowed pointer to the engine's
 * internal DiskCacheFolder for that path; it must not be freed and becomes
 * invalid when the DiskManager instance is destroyed.
 *
 * Conventions match the other facade families:
 *   - 0 (OAKENGINE_OK) / negative OAKENGINE_E_* codes.
 *   - String output uses the buf/size convention.
 *   - Booleans are int (1/0).
 */

/**
 * @brief Callback invoked when the engine requests the disk cache settings
 * dialog for a folder.
 *
 * `folder_path` is the UTF-8 path of the cache folder. `parent_window` is a
 * borrowed pointer to the QWidget that should act as the dialog's parent (may
 * be NULL). `userdata` is the value passed to
 * oakengine_disk_set_settings_handler().
 */
typedef void (*oakengine_disk_settings_fn)(const char *folder_path,
										   void *parent_window,
										   void *userdata);

/**
 * @brief Create the DiskManager singleton.
 *
 * Safe to call when the instance already exists (no-op). Returns
 * OAKENGINE_OK or OAKENGINE_E_FAILED.
 */
OAKENGINE_API int oakengine_disk_create_instance(void);

/**
 * @brief Destroy the DiskManager singleton.
 *
 * Safe to call when no instance exists (no-op). Returns OAKENGINE_OK.
 */
OAKENGINE_API int oakengine_disk_destroy_instance(void);

/**
 * @brief Register the handler used to show the disk cache settings dialog.
 *
 * The engine calls this handler when the user requests the settings dialog.
 * Passing NULL clears the handler. Returns OAKENGINE_OK.
 */
OAKENGINE_API int oakengine_disk_set_settings_handler(
		oakengine_disk_settings_fn fn, void *userdata);

/**
 * @brief Show the disk cache settings dialog for `path`.
 *
 * If `path` is NULL or empty, the default cache folder is used. The actual
 * dialog is shown by the handler registered with
 * oakengine_disk_set_settings_handler(); if no handler is registered the
 * request is logged and skipped. Returns OAKENGINE_OK or an error code.
 */
OAKENGINE_API int oakengine_disk_show_settings_dialog(const char *path,
													  void *parent_window);

/**
 * @brief Show a confirmation dialog before changing the disk cache location.
 *
 * Returns 1 if the user confirms, 0 otherwise. `parent_window` may be NULL.
 */
OAKENGINE_API int oakengine_disk_show_change_confirmation_dialog(
		void *parent_window);

/**
 * @brief Clear the disk cache in `path`.
 *
 * Returns 1 on success, 0 on failure. The folder is opened if necessary.
 */
OAKENGINE_API int oakengine_disk_clear_cache(const char *path);

/**
 * @brief Get the default cache folder path (buf/size convention).
 *
 * Returns the string length on success, or a negative OAKENGINE_E_* code when
 * no DiskManager instance exists.
 */
OAKENGINE_API int oakengine_disk_get_default_cache_path(char *buf,
														int buf_size);

/**
 * @brief Set the default cache folder path.
 *
 * The default folder's path is updated and will be persisted when the
 * DiskManager instance is destroyed. Returns OAKENGINE_OK or an error code.
 */
OAKENGINE_API int oakengine_disk_set_default_cache_path(const char *path);

/**
 * @brief Get or create a borrowed opaque handle to the cache folder for
 * `path`.
 *
 * Returns NULL if no DiskManager instance exists or if `path` is invalid. If
 * `path` is NULL or empty, the default cache folder is returned. The returned
 * handle is a borrowed pointer whose lifetime follows the DiskManager
 * instance; it must not be freed.
 */
OAKENGINE_API void *oakengine_disk_get_open_folder(const char *path);

/**
 * @brief Emit the invalidate_project signal on the DiskManager instance.
 *
 * This tells consumers of the disk cache that `project` has changed and any
 * cached data for it should be discarded. Returns OAKENGINE_OK or an error
 * code.
 */
OAKENGINE_API int oakengine_disk_invalidate_project(
		OakEngineProject *project);

/* ---- DiskCacheFolder accessors -------------------------------------------------
 *
 * Accessors for a borrowed folder handle from
 * oakengine_disk_get_open_folder() (olive::DiskCacheFolder, passed as a
 * plain `void *` like it is returned). The limit is a byte count carried
 * as a double (the engine stores a qint64; a double is exact up to
 * 2**53 bytes, far beyond any cache size).
 */

/**
 * @brief Cache size limit of the folder in bytes
 * (DiskCacheFolder::get_limit()). 0 on a NULL handle.
 */
OAKENGINE_API double oakengine_disk_folder_get_limit(const void *folder);

/**
 * @brief Set the cache size limit in bytes
 * (DiskCacheFolder::set_limit()). `limit` < 0 yields
 * OAKENGINE_E_INVALID.
 */
OAKENGINE_API int oakengine_disk_folder_set_limit(void *folder, double limit);

/**
 * @brief 1 if the folder is cleared when the application closes
 * (DiskCacheFolder::get_clear_on_close()). 0 on a NULL handle.
 */
OAKENGINE_API int oakengine_disk_folder_get_clear_on_close(
		const void *folder);

/**
 * @brief Set the clear-on-close flag (DiskCacheFolder::set_clear_on_close()).
 * Returns OAKENGINE_OK or OAKENGINE_E_INVALID.
 */
OAKENGINE_API int oakengine_disk_folder_set_clear_on_close(void *folder,
														   int clear);

/**
 * @brief The folder's path (DiskCacheFolder::get_path(); buf/size
 * convention). Returns OAKENGINE_E_INVALID on a NULL handle.
 */
OAKENGINE_API int oakengine_disk_folder_get_path(const void *folder,
												 char *buf, int buf_size);

#ifdef __cplusplus
}
#endif

#endif /* OAKENGINE_DISK_H */
