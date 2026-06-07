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

#include "proxymanager.h"

#include <QDir>
#include <QFileInfo>
#include <QMutexLocker>

#include "common/filefunctions.h"
#include "task/proxy/proxy.h"
#include "task/taskmanager.h"

namespace olive
{

ProxyManager *ProxyManager::instance_ = nullptr;

bool ProxyParamsEqual(const ProxyManager::ProxyParams &a,
					  const ProxyManager::ProxyParams &b)
{
	return a.width == b.width && a.height == b.height &&
		   a.version == b.version && a.extension == b.extension;
}

QString ProxyManager::GetProxyDirectory(const QString &cache_path)
{
	return QDir(cache_path).filePath(QStringLiteral("proxy"));
}

QString ProxyManager::GetProxyFilename(const QString &cache_path,
									   const QString &source_filename,
									   int stream_index,
									   const ProxyParams &params)
{
	const QString proxy_dir = GetProxyDirectory(cache_path);
	const QString extension =
		params.extension.isEmpty() ? QStringLiteral("mp4") : params.extension;
	const QString filename = QStringLiteral("%1-%2.%3x%4.v%5.%6")
								 .arg(FileFunctions::GetUniqueFileIdentifier(
										  source_filename),
									  QString::number(stream_index),
									  QString::number(params.width),
									  QString::number(params.height),
									  QString::number(params.version),
									  extension);

	return QDir(proxy_dir).filePath(filename);
}

QString ProxyManager::GetWorkingProxyFilename(const QString &proxy_filename)
{
	return QStringLiteral("%1.working").arg(proxy_filename);
}

ProxyManager::ProxyState
ProxyManager::GetProxyState(const QString &proxy_filename)
{
	if (QFileInfo::exists(proxy_filename)) {
		return kProxyReady;
	}

	if (QFileInfo::exists(GetWorkingProxyFilename(proxy_filename))) {
		return kProxyGenerating;
	}

	return kProxyMissing;
}

QString ProxyManager::ProxyStateToString(ProxyState state)
{
	switch (state) {
	case kProxyMissing:
		return QStringLiteral("missing");
	case kProxyGenerating:
		return QStringLiteral("generating");
	case kProxyReady:
		return QStringLiteral("ready");
	case kProxyFailed:
		return QStringLiteral("failed");
	}

	return QStringLiteral("missing");
}

ProxyManager::ProxyState
ProxyManager::ProxyStateFromString(const QString &state)
{
	if (state == QStringLiteral("generating")) {
		return kProxyGenerating;
	}

	if (state == QStringLiteral("ready")) {
		return kProxyReady;
	}

	if (state == QStringLiteral("failed")) {
		return kProxyFailed;
	}

	return kProxyMissing;
}

ProxyManager::Proxy ProxyManager::GetOrStartProxy(
	const QString &cache_path, const QString &source_filename,
	int stream_index, const ProxyParams &params)
{
	QMutexLocker locker(&mutex_);

	const QString filename =
		GetProxyFilename(cache_path, source_filename, stream_index, params);
	const ProxyState file_state = GetProxyState(filename);
	if (file_state == kProxyReady) {
		return { kProxyReady, filename, nullptr };
	}

	for (const ProxyData &data : proxying_) {
		if (data.source_filename == source_filename &&
			data.stream_index == stream_index &&
			ProxyParamsEqual(data.params, params)) {
			return { kProxyGenerating, filename, data.task };
		}
	}

	if (file_state == kProxyGenerating) {
		QFile::remove(GetWorkingProxyFilename(filename));
	}

	const QString working_filename = GetWorkingProxyFilename(filename);
	ProxyTask *task = new ProxyTask(source_filename, stream_index, params,
									working_filename);
	connect(task, &Task::Finished, this,
			&ProxyManager::ProxyTaskFinished);
	task->moveToThread(TaskManager::instance()->thread());
	QMetaObject::invokeMethod(TaskManager::instance(), "AddTask",
							  Qt::QueuedConnection, Q_ARG(Task *, task));

	proxying_.append({ source_filename, stream_index, params, task,
					   working_filename, filename });

	return { kProxyGenerating, filename, task };
}

void ProxyManager::ProxyTaskFinished(Task *task, bool succeeded)
{
	QMutexLocker locker(&mutex_);

	ProxyData data;
	bool found = false;
	for (int i = 0; i < proxying_.size(); i++) {
		const ProxyData &candidate = proxying_.at(i);
		if (candidate.task == task) {
			data = candidate;
			proxying_.removeAt(i);
			found = true;
			break;
		}
	}

	if (!found) {
		return;
	}

	if (succeeded) {
		QFile::remove(data.finished_filename);
		if (QFile::rename(data.working_filename, data.finished_filename)) {
			locker.unlock();
			emit ProxyReady(data.source_filename, data.stream_index,
							data.finished_filename);
			emit ProxyFinished(data.source_filename, data.stream_index,
							   data.finished_filename, kProxyReady);
			return;
		}
	}

	QFile::remove(data.working_filename);
	locker.unlock();
	emit ProxyFinished(data.source_filename, data.stream_index,
					   data.finished_filename, kProxyFailed);
}

}
