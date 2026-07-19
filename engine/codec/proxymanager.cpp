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

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMutexLocker>
#include <QStandardPaths>

#include "common/filefunctions.h"
#include "config/config.h"
#include "task/proxy/proxy.h"
#include "task/taskmanager.h"

namespace olive
{

ProxyManager *ProxyManager::instance_ = nullptr;

bool proxy_params_equal(const ProxyManager::ProxyParams &a,
					  const ProxyManager::ProxyParams &b)
{
	return a.width == b.width && a.height == b.height &&
		   a.divider == b.divider && a.version == b.version &&
		   a.extension == b.extension && a.crf == b.crf && a.preset == b.preset &&
		   a.include_audio == b.include_audio;
}

QString ProxyManager::get_proxy_directory(const QString &cache_path)
{
	return QDir(cache_path).filePath(QStringLiteral("proxy"));
}

QString ProxyManager::get_proxy_filename(const QString &cache_path,
									   const QString &source_filename,
									   int stream_index,
									   const ProxyParams &params)
{
	const QString proxy_dir = get_proxy_directory(cache_path);
	const QString extension =
		params.extension.isEmpty() ? QStringLiteral("mp4") : params.extension;

	// Divider mode scales relative to the source, so the tag names the
	// divider rather than an absolute target size
	QString size_tag;
	if (params.divider > 1) {
		size_tag = QStringLiteral("div%1").arg(QString::number(params.divider));
	} else {
		size_tag = QStringLiteral("%1x%2")
					   .arg(QString::number(params.width),
							QString::number(params.height));
	}

	const QString filename =
		QStringLiteral("%1-%2.%3.v%4.a%5.%6")
			.arg(FileFunctions::get_unique_file_identifier(source_filename),
				 QString::number(stream_index), size_tag,
				 QString::number(params.version),
				 params.include_audio ? QStringLiteral("1") : QStringLiteral("0"),
				 extension);

	return QDir(proxy_dir).filePath(filename);
}

QString ProxyManager::get_working_proxy_filename(const QString &proxy_filename)
{
	// Append a recognizable suffix while keeping a standard container extension
	// so ffmpeg can infer the output format.
	return QStringLiteral("%1.working.mp4").arg(proxy_filename);
}

ProxyManager::ProxyState
ProxyManager::get_proxy_state(const QString &proxy_filename)
{
	if (QFileInfo::exists(proxy_filename)) {
		return k_proxy_ready;
	}

	if (QFileInfo::exists(get_working_proxy_filename(proxy_filename))) {
		return k_proxy_generating;
	}

	return k_proxy_missing;
}

QString ProxyManager::proxy_state_to_string(ProxyState state)
{
	switch (state) {
	case k_proxy_missing:
		return QStringLiteral("missing");
	case k_proxy_generating:
		return QStringLiteral("generating");
	case k_proxy_ready:
		return QStringLiteral("ready");
	case k_proxy_failed:
		return QStringLiteral("failed");
	}

	return QStringLiteral("missing");
}

ProxyManager::ProxyState
ProxyManager::proxy_state_from_string(const QString &state)
{
	if (state == QStringLiteral("generating")) {
		return k_proxy_generating;
	}

	if (state == QStringLiteral("ready")) {
		return k_proxy_ready;
	}

	if (state == QStringLiteral("failed")) {
		return k_proxy_failed;
	}

	return k_proxy_missing;
}

bool ProxyManager::proxy_filename_has_audio(const QString &proxy_filename)
{
	return QFileInfo(proxy_filename).fileName().contains(
		QStringLiteral(".a1."));
}

ProxyManager::ProxyParams ProxyManager::proxy_params_from_config()
{
	ProxyParams params;
	params.width = OAK_CONFIG("ProxyWidth").value<int>();
	params.height = OAK_CONFIG("ProxyHeight").value<int>();
	params.divider = OAK_CONFIG("ProxyDivider").value<int>();
	params.crf = OAK_CONFIG("ProxyCRF").value<int>();
	params.preset = OAK_CONFIG("ProxyPreset").toString();
	params.include_audio = OAK_CONFIG("ProxyIncludeAudio").toBool();
	return params;
}

QString ProxyManager::find_f_fmpeg_executable(const QString &configured_path)
{
	// An explicitly configured path takes precedence if it is usable
	if (!configured_path.isEmpty()) {
		const QFileInfo configured_info(configured_path);
		if (configured_info.exists() && configured_info.isFile() &&
			configured_info.isExecutable()) {
			return configured_info.absoluteFilePath();
		}

		qWarning() << "Configured ffmpeg path is not a valid executable:"
				   << configured_path;
	}

	// Fall back to searching the system PATH
	const QString from_path =
		QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
	if (!from_path.isEmpty()) {
		return from_path;
	}

	// Finally, try common install locations (PATH on GUI-launched apps,
	// particularly on macOS, often lacks these)
	QStringList candidates;
	candidates.append(QCoreApplication::applicationDirPath() +
					  QStringLiteral("/ffmpeg"));
#ifdef Q_OS_MAC
	candidates.append(QStringLiteral("/opt/homebrew/bin/ffmpeg"));
	candidates.append(QStringLiteral("/usr/local/bin/ffmpeg"));
#endif
#ifdef Q_OS_WINDOWS
	candidates.append(QCoreApplication::applicationDirPath() +
					  QStringLiteral("/ffmpeg.exe"));
#endif
	candidates.append(QStringLiteral("/usr/bin/ffmpeg"));
	candidates.append(QStringLiteral("/usr/local/bin/ffmpeg"));

	for (const QString &candidate : candidates) {
		const QFileInfo info(candidate);
		if (info.exists() && info.isFile() && info.isExecutable()) {
			return info.absoluteFilePath();
		}
	}

	return QString();
}

ProxyManager::Proxy
ProxyManager::get_or_start_proxy(const QString &cache_path,
							  const QString &source_filename, int stream_index,
							  const ProxyParams &params)
{
	QMutexLocker locker(&mutex_);

	const QString filename =
		get_proxy_filename(cache_path, source_filename, stream_index, params);
	const ProxyState file_state = get_proxy_state(filename);
	if (file_state == k_proxy_ready) {
		return { k_proxy_ready, filename, nullptr };
	}

	for (const ProxyData &data : proxying_) {
		if (data.source_filename == source_filename &&
			data.stream_index == stream_index &&
			proxy_params_equal(data.params, params)) {
			return { k_proxy_generating, filename, data.task };
		}
	}

	if (file_state == k_proxy_generating) {
		QFile::remove(get_working_proxy_filename(filename));
	}

	const QString working_filename = get_working_proxy_filename(filename);
	ProxyTask *task =
		new ProxyTask(source_filename, stream_index, params, working_filename);
	connect(task, &Task::finished, this, &ProxyManager::proxy_task_finished);
	task->moveToThread(TaskManager::instance()->thread());
	QMetaObject::invokeMethod(TaskManager::instance(), "add_task",
							  Qt::QueuedConnection, Q_ARG(Task *, task));

	proxying_.append({ source_filename, stream_index, params, task,
					   working_filename, filename });

	return { k_proxy_generating, filename, task };
}

void ProxyManager::proxy_task_finished(Task *task, bool succeeded)
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
			emit proxy_ready(data.source_filename, data.stream_index,
							data.finished_filename);
			emit proxy_finished(data.source_filename, data.stream_index,
							   data.finished_filename, k_proxy_ready);
			return;
		}
	}

	QFile::remove(data.working_filename);
	locker.unlock();
	emit proxy_finished(data.source_filename, data.stream_index,
					   data.finished_filename, k_proxy_failed);
}

}
