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

#include <QMap>
#include <QMutex>
#include <QObject>
#include <QTimer>

#include <functional>

#include "common/define.h"
#include "node/project.h"

namespace olive
{

class DiskCacheFolder : public QObject {
	Q_OBJECT
public:
	DiskCacheFolder(const QString &path, QObject *parent = nullptr);

	virtual ~DiskCacheFolder() override;

	bool clear_cache();

	void accessed(const QString &filename);

	void created_file(const QString &filename);

	const QString &get_path() const
	{
		return path_;
	}

	void set_path(const QString &path);

	qint64 get_limit() const
	{
		return limit_;
	}

	bool get_clear_on_close() const
	{
		return clear_on_close_;
	}

	void set_limit(qint64 l)
	{
		limit_ = l;
	}

	void set_clear_on_close(bool e)
	{
		clear_on_close_ = e;
	}

	bool delete_specific_file(const QString &f);

signals:
	void deleted_frame(const QString &path, const QString &filename);

private:
	struct HashTime {
		qint64 file_size;
		qint64 access_time;
	};

	bool delete_file_internal(QMap<QString, HashTime>::iterator hash_to_delete);

	bool delete_least_recent();

	void close_cache_folder();

	QString path_;

	QString index_path_;

	QMap<QString, HashTime> disk_data_;

	qint64 consumption_;

	qint64 limit_;

	bool clear_on_close_;

	QTimer save_timer_;

private slots:
	void save_disk_cache_index();
};

class DiskManager : public QObject {
	Q_OBJECT
public:
	static void create_instance();

	static void destroy_instance();

	static DiskManager *instance();

	bool clear_disk_cache(const QString &cache_folder);

	DiskCacheFolder *get_default_cache_folder() const
	{
		// The first folder will always be the default
		return open_folders_.first();
	}

	const QString &get_default_cache_path() const
	{
		return get_default_cache_folder()->get_path();
	}

	DiskCacheFolder *get_open_folder(const QString &path);

	const QVector<DiskCacheFolder *> &get_open_folders() const
	{
		return open_folders_;
	}

	static bool show_disk_cache_change_confirmation_dialog(QWidget *parent);

	static QString get_default_disk_cache_config_file();

	static QString get_default_disk_cache_path();

	/**
	 * @brief Handler showing the disk cache settings dialog for a folder
	 *
	 * Registered by the UI layer (e.g. a DiskCacheDialog-based
	 * implementation), since the engine cannot show dialogs itself. Without
	 * a handler, the request is logged and skipped.
	 */
	using ShowDiskCacheSettingsHandler =
		std::function<void(DiskCacheFolder *folder, QWidget *parent)>;

	static void set_show_disk_cache_settings_handler(
		ShowDiskCacheSettingsHandler handler);

	void show_disk_cache_settings_dialog(DiskCacheFolder *folder, QWidget *parent);
	void show_disk_cache_settings_dialog(const QString &path, QWidget *parent);

public slots:
	void accessed(const QString &cache_folder, const QString &filename);

	void created_file(const QString &cache_folder, const QString &filename);

	void delete_specific_file(const QString &filename);

signals:
	void deleted_frame(const QString &path, const QString &filename);

	void invalidate_project(Project *p);

private:
	DiskManager();

	virtual ~DiskManager() override;

	static DiskManager *instance_;

	static ShowDiskCacheSettingsHandler show_disk_cache_settings_handler_;

	QVector<DiskCacheFolder *> open_folders_;
};

}

#endif // OAK_DISKMANAGER_H
