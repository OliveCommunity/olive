/*
 * Oak Video Editor - Non-Linear Video Editor
 * Copyright (C) 2026 Oak Team
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

#ifndef PROXYMANAGER_H
#define PROXYMANAGER_H

#include <QMutex>
#include <QObject>
#include <QString>
#include <QVector>

#include "task/task.h"

namespace olive
{

class ProxyTask;

class ProxyManager : public QObject {
	Q_OBJECT
public:
	static void CreateInstance()
	{
		if (!instance_) {
			instance_ = new ProxyManager();
		}
	}

	static void DestroyInstance()
	{
		delete instance_;
		instance_ = nullptr;
	}

	static ProxyManager *instance()
	{
		return instance_;
	}

	enum ProxyState {
		kProxyMissing,
		kProxyGenerating,
		kProxyReady,
		kProxyFailed
	};

	struct ProxyParams {
		int width = 1280;
		int height = 720;
		int version = 1;
		QString extension = QStringLiteral("mp4");
	};

	struct Proxy {
		ProxyState state = kProxyMissing;
		QString filename;
		ProxyTask *task = nullptr;
	};

	static QString GetProxyDirectory(const QString &cache_path);

	static QString GetProxyFilename(const QString &cache_path,
									const QString &source_filename,
									int stream_index,
									const ProxyParams &params);

	static QString GetWorkingProxyFilename(const QString &proxy_filename);

	static ProxyState GetProxyState(const QString &proxy_filename);

	static QString ProxyStateToString(ProxyState state);

	static ProxyState ProxyStateFromString(const QString &state);

	Proxy GetOrStartProxy(const QString &cache_path,
						  const QString &source_filename,
						  int stream_index,
						  const ProxyParams &params);

signals:
	void ProxyReady(const QString &source_filename, int stream_index,
					const QString &proxy_filename);
	void ProxyFinished(const QString &source_filename, int stream_index,
					   const QString &proxy_filename, ProxyState state);

private:
	ProxyManager() = default;

	static ProxyManager *instance_;

	struct ProxyData {
		QString source_filename;
		int stream_index = -1;
		ProxyParams params;
		ProxyTask *task = nullptr;
		QString working_filename;
		QString finished_filename;
	};

	QMutex mutex_;
	QVector<ProxyData> proxying_;

private slots:
	void ProxyTaskFinished(Task *task, bool succeeded);
};

}

#endif // PROXYMANAGER_H
