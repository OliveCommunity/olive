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

#include "proxy.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

namespace olive
{

ProxyTask::ProxyTask(const QString &source_filename,
					 int stream_index,
					 const ProxyManager::ProxyParams &params,
					 const QString &output_filename)
	: source_filename_(source_filename)
	, stream_index_(stream_index)
	, params_(params)
	, output_filename_(output_filename)
{
	SetTitle(tr("Generating Proxy %1:%2")
				 .arg(source_filename_, QString::number(stream_index_)));
}

bool ProxyTask::Run()
{
	const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
	if (ffmpeg.isEmpty()) {
		SetError(tr("Failed to generate proxy: ffmpeg executable was not found"));
		return false;
	}

	QDir output_dir = QFileInfo(output_filename_).dir();
	if (!output_dir.exists() && !output_dir.mkpath(QStringLiteral("."))) {
		SetError(tr("Failed to create proxy output directory"));
		return false;
	}

	QFile::remove(output_filename_);

	const QString scale_filter = QStringLiteral(
		"scale=w=%1:h=%2:force_original_aspect_ratio=decrease")
							.arg(QString::number(params_.width),
								 QString::number(params_.height));

	QStringList args;
	args << QStringLiteral("-y")
		 << QStringLiteral("-i") << source_filename_
		 << QStringLiteral("-map") << QStringLiteral("0:%1").arg(stream_index_)
		 << QStringLiteral("-an")
		 << QStringLiteral("-vf") << scale_filter
		 << QStringLiteral("-c:v") << QStringLiteral("libx264")
		 << QStringLiteral("-preset") << QStringLiteral("veryfast")
		 << QStringLiteral("-crf") << QStringLiteral("23")
		 << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p")
		 << QStringLiteral("-movflags") << QStringLiteral("+faststart")
		 << output_filename_;

	QProcess process;
	process.setProgram(ffmpeg);
	process.setArguments(args);
	process.setProcessChannelMode(QProcess::MergedChannels);
	process.start();

	if (!process.waitForStarted()) {
		SetError(tr("Failed to start ffmpeg for proxy generation"));
		return false;
	}

	while (!process.waitForFinished(100)) {
		if (IsCancelled()) {
			process.kill();
			process.waitForFinished();
			QFile::remove(output_filename_);
			SetError(tr("Proxy generation was cancelled"));
			return false;
		}
	}

	if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
		const QString output = QString::fromUtf8(process.readAll()).trimmed();
		QFile::remove(output_filename_);
		SetError(tr("ffmpeg failed to generate proxy: %1").arg(output));
		return false;
	}

	if (!QFileInfo::exists(output_filename_)) {
		SetError(tr("ffmpeg finished but proxy file was not created"));
		return false;
	}

	emit ProgressChanged(1.0);
	return true;
}

}
