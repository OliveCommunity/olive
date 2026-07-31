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

#include "oakengine/disk.h"

#include <cstring>

#include <QByteArray>
#include <QString>
#include <QWidget>

#include "node/project.h"
#include "render/diskmanager.h"

namespace
{

int write_string(const QString &s, char *buf, int buf_size)
{
	const QByteArray utf8 = s.toUtf8();
	const int len = int(utf8.size());
	if (buf && buf_size > 0) {
		const int n = qMin(len, buf_size - 1);
		std::memcpy(buf, utf8.constData(), size_t(n));
		buf[n] = '\0';
	}
	return len;
}

olive::DiskManager *manager()
{
	return olive::DiskManager::instance();
}

olive::DiskCacheFolder *folder_from_path(olive::DiskManager *m,
										 const char *path)
{
	if (!m) {
		return nullptr;
	}
	if (!path || std::strlen(path) == 0) {
		return m->get_default_cache_folder();
	}
	return m->get_open_folder(QString::fromUtf8(path));
}

struct SettingsHandlerState {
	oakengine_disk_settings_fn fn = nullptr;
	void *userdata = nullptr;
};

SettingsHandlerState g_settings_handler;

void cpp_settings_handler(olive::DiskCacheFolder *folder, QWidget *parent)
{
	if (!g_settings_handler.fn || !folder) {
		return;
	}
	const QByteArray path = folder->get_path().toUtf8();
	g_settings_handler.fn(path.constData(), parent, g_settings_handler.userdata);
}

} // namespace

extern "C" int oakengine_disk_create_instance(void)
{
	olive::DiskManager::create_instance();
	return manager() ? OAKENGINE_OK : OAKENGINE_E_FAILED;
}

extern "C" int oakengine_disk_destroy_instance(void)
{
	olive::DiskManager::destroy_instance();
	return OAKENGINE_OK;
}

extern "C" int oakengine_disk_set_settings_handler(
		oakengine_disk_settings_fn fn, void *userdata)
{
	g_settings_handler.fn = fn;
	g_settings_handler.userdata = userdata;

	olive::DiskManager::set_show_disk_cache_settings_handler(
			fn ? cpp_settings_handler : olive::DiskManager::ShowDiskCacheSettingsHandler{});

	return OAKENGINE_OK;
}

extern "C" int oakengine_disk_show_settings_dialog(const char *path,
													 void *parent_window)
{
	olive::DiskManager *m = manager();
	if (!m) {
		return OAKENGINE_E_STATE;
	}

	olive::DiskCacheFolder *folder = folder_from_path(m, path);
	if (!folder) {
		return OAKENGINE_E_FAILED;
	}

	m->show_disk_cache_settings_dialog(folder,
									   static_cast<QWidget *>(parent_window));
	return OAKENGINE_OK;
}

extern "C" int oakengine_disk_show_change_confirmation_dialog(
		void *parent_window)
{
	return olive::DiskManager::show_disk_cache_change_confirmation_dialog(
				static_cast<QWidget *>(parent_window))
			   ? 1
			   : 0;
}

extern "C" int oakengine_disk_clear_cache(const char *path)
{
	olive::DiskManager *m = manager();
	if (!m) {
		return 0;
	}

	olive::DiskCacheFolder *folder = folder_from_path(m, path);
	if (!folder) {
		return 0;
	}

	return m->clear_disk_cache(folder->get_path()) ? 1 : 0;
}

extern "C" int oakengine_disk_get_default_cache_path(char *buf, int buf_size)
{
	olive::DiskManager *m = manager();
	if (!m) {
		return OAKENGINE_E_STATE;
	}

	return write_string(m->get_default_cache_path(), buf, buf_size);
}

extern "C" int oakengine_disk_set_default_cache_path(const char *path)
{
	if (!path) {
		return OAKENGINE_E_INVALID;
	}

	olive::DiskManager *m = manager();
	if (!m) {
		return OAKENGINE_E_STATE;
	}

	m->get_default_cache_folder()->set_path(QString::fromUtf8(path));
	return OAKENGINE_OK;
}

extern "C" void *oakengine_disk_get_open_folder(const char *path)
{
	return folder_from_path(manager(), path);
}

extern "C" int oakengine_disk_invalidate_project(OakEngineProject *project)
{
	olive::DiskManager *m = manager();
	if (!m) {
		return OAKENGINE_E_STATE;
	}

	emit m->invalidate_project(reinterpret_cast<olive::Project *>(project));
	return OAKENGINE_OK;
}

/* ---- DiskCacheFolder accessors ------------------------------------------------- */

extern "C" double oakengine_disk_folder_get_limit(const void *folder)
{
	if (!folder) {
		return 0.0;
	}
	return static_cast<double>(
		reinterpret_cast<const olive::DiskCacheFolder *>(folder)
			->get_limit());
}

extern "C" int oakengine_disk_folder_set_limit(void *folder, double limit)
{
	if (!folder || limit < 0.0) {
		return OAKENGINE_E_INVALID;
	}
	reinterpret_cast<olive::DiskCacheFolder *>(folder)->set_limit(
		qRound64(limit));
	return OAKENGINE_OK;
}

extern "C" int oakengine_disk_folder_get_clear_on_close(const void *folder)
{
	if (!folder) {
		return 0;
	}
	return reinterpret_cast<const olive::DiskCacheFolder *>(folder)
				   ->get_clear_on_close()
			   ? 1
			   : 0;
}

extern "C" int oakengine_disk_folder_set_clear_on_close(void *folder,
														int clear)
{
	if (!folder) {
		return OAKENGINE_E_INVALID;
	}
	reinterpret_cast<olive::DiskCacheFolder *>(folder)->set_clear_on_close(
		clear != 0);
	return OAKENGINE_OK;
}

extern "C" int oakengine_disk_folder_get_path(const void *folder, char *buf,
											  int buf_size)
{
	if (!folder) {
		return OAKENGINE_E_INVALID;
	}
	return write_string(
		reinterpret_cast<const olive::DiskCacheFolder *>(folder)->get_path(),
		buf, buf_size);
}
