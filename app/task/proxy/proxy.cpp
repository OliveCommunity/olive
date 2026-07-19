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

#include "proxy.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

#include "config/config.h"

namespace olive
{

ProxyTask::ProxyTask(const QString &source_filename, int stream_index,
					 const ProxyManager::ProxyParams &params,
					 const QString &output_filename)
	: source_filename_(source_filename)
	, stream_index_(stream_index)
	, params_(params)
	, output_filename_(output_filename)
{
	set_title(tr("Generating Proxy %1:%2")
				 .arg(source_filename_, QString::number(stream_index_)));
}

QStringList ProxyTask::build_arguments(const QString &source_filename,
									  int stream_index,
									  const ProxyManager::ProxyParams &params,
									  const QString &output_filename)
{
	QString scale_filter;
	if (params.divider > 1) {
		// Fraction of the source resolution, rounded down to even dimensions
		// as required by yuv420p
		scale_filter =
			QStringLiteral("scale=w=trunc(iw/%1/2)*2:h=trunc(ih/%1/2)*2")
				.arg(QString::number(params.divider));
	} else {
		scale_filter =
			QStringLiteral("scale=w=%1:h=%2:force_original_aspect_ratio=decrease")
				.arg(QString::number(params.width),
					 QString::number(params.height));
	}

	const QString container_format =
		params.extension.isEmpty() ? QStringLiteral("mp4") : params.extension;

	QStringList args;
	args << QStringLiteral("-y")
		 // Report machine-readable progress on stdout for the task dialog
		 << QStringLiteral("-nostats") << QStringLiteral("-progress")
		 << QStringLiteral("pipe:1") << QStringLiteral("-i") << source_filename
		 // Map the requested video stream first so it is stream 0 in the proxy
		 << QStringLiteral("-map") << QStringLiteral("0:%1").arg(stream_index);

	if (params.include_audio) {
		// Keep the source audio (if any) so the proxy can also be used for
		// audio preview. Audio streams follow the video stream in source order.
		args << QStringLiteral("-map") << QStringLiteral("0:a?")
			 << QStringLiteral("-c:a") << QStringLiteral("aac")
			 << QStringLiteral("-b:a") << QStringLiteral("128k");
	} else {
		args << QStringLiteral("-an");
	}

	args << QStringLiteral("-vf") << scale_filter << QStringLiteral("-c:v")
		 << QStringLiteral("libx264") << QStringLiteral("-preset")
		 << params.preset << QStringLiteral("-crf")
		 << QString::number(params.crf) << QStringLiteral("-pix_fmt")
		 << QStringLiteral("yuv420p") << QStringLiteral("-movflags")
		 << QStringLiteral("+faststart") << QStringLiteral("-f")
		 << container_format << output_filename;

	return args;
}

double ProxyTask::parse_progress(const QString &line, double duration_seconds)
{
	if (duration_seconds <= 0.0) {
		return -1.0;
	}

	qint64 out_time_us = -1;
	if (line.startsWith(QStringLiteral("out_time_us="))) {
		out_time_us = line.mid(12).toLongLong();
	} else if (line.startsWith(QStringLiteral("out_time_ms="))) {
		// Despite the name, ffmpeg reports this value in microseconds
		out_time_us = line.mid(12).toLongLong();
	}

	if (out_time_us < 0) {
		return -1.0;
	}

	return qBound(0.0, out_time_us / 1000000.0 / duration_seconds, 1.0);
}

namespace
{

/**
 * @brief Probes the source duration with the ffprobe next to ffmpeg
 *
 * Returns 0 when ffprobe is unavailable or the duration cannot be
 * determined, in which case the task simply reports no intermediate
 * progress.
 */
double probe_source_duration_seconds(const QString &ffmpeg_path,
									 const QString &source_filename)
{
	QString ffprobe =
		QFileInfo(ffmpeg_path).dir().filePath(QStringLiteral("ffprobe"));
#if defined(Q_OS_WIN)
	ffprobe += QStringLiteral(".exe");
#endif
	if (!QFileInfo::exists(ffprobe)) {
		return 0.0;
	}

	QProcess probe;
	probe.start(ffprobe,
				{ QStringLiteral("-v"), QStringLiteral("error"),
				  QStringLiteral("-show_entries"), QStringLiteral("format=duration"),
				  QStringLiteral("-of"),
				  QStringLiteral("default=noprint_wrappers=1:nokey=1"),
				  source_filename });
	if (!probe.waitForFinished(10000) || probe.exitCode() != 0) {
		return 0.0;
	}

	bool ok = false;
	const double duration =
		QString::fromUtf8(probe.readAllStandardOutput()).trimmed().toDouble(&ok);
	return ok ? duration : 0.0;
}

} // namespace

bool ProxyTask::run()
{
	const QString ffmpeg = ProxyManager::find_f_fmpeg_executable(
		OAK_CONFIG("FFmpegPath").toString());
	if (ffmpeg.isEmpty()) {
		set_error(
			tr("Failed to generate proxy: ffmpeg executable was not found. Set "
			   "the ffmpeg path in Preferences > Disk > Proxy Settings."));
		qWarning() << "ProxyTask: ffmpeg executable not found";
		return false;
	}

	QDir output_dir = QFileInfo(output_filename_).dir();
	if (!output_dir.exists() && !output_dir.mkpath(QStringLiteral("."))) {
		set_error(tr("Failed to create proxy output directory"));
		qWarning() << "ProxyTask: failed to create output directory"
				   << output_dir.absolutePath();
		return false;
	}

	qDebug()
		<< "ProxyTask: starting ffmpeg proxy generation:" << source_filename_
		<< "->" << output_filename_;

	QFile::remove(output_filename_);

	const QStringList args = build_arguments(source_filename_, stream_index_,
											params_, output_filename_);

	QProcess process;
	process.setProgram(ffmpeg);
	process.setArguments(args);
	process.setProcessChannelMode(QProcess::MergedChannels);

	const double duration_seconds =
		probe_source_duration_seconds(ffmpeg, source_filename_);

	process.start();

	if (!process.waitForStarted()) {
		set_error(tr("Failed to start ffmpeg for proxy generation"));
		qWarning()
			<< "ProxyTask: failed to start ffmpeg" << process.errorString();
		return false;
	}

	QString progress_buffer;
	double last_progress = 0.0;

	const auto drain_progress = [&]() {
		progress_buffer += QString::fromUtf8(process.readAll());
		int newline = -1;
		while ((newline = progress_buffer.indexOf(QLatin1Char('\n'))) >= 0) {
			const QString line = progress_buffer.left(newline).trimmed();
			progress_buffer.remove(0, newline + 1);
			const double progress = parse_progress(line, duration_seconds);
			if (progress >= 0.0 && progress - last_progress > 0.001) {
				last_progress = progress;
				emit progress_changed(progress);
			}
		}
	};

	while (!process.waitForFinished(100)) {
		drain_progress();
		if (is_cancelled()) {
			process.kill();
			process.waitForFinished();
			QFile::remove(output_filename_);
			set_error(tr("Proxy generation was cancelled"));
			return false;
		}
	}
	drain_progress();

	if (process.exitStatus() != QProcess::NormalExit ||
		process.exitCode() != 0) {
		const QString output = QString::fromUtf8(process.readAll()).trimmed();
		QFile::remove(output_filename_);
		set_error(tr("ffmpeg failed to generate proxy: %1").arg(output));
		qWarning() << "ProxyTask: ffmpeg failed with exit code"
				   << process.exitCode() << "output:" << output;
		return false;
	}

	if (!QFileInfo::exists(output_filename_)) {
		set_error(tr("ffmpeg finished but proxy file was not created"));
		qWarning() << "ProxyTask: ffmpeg finished but output file missing"
				   << output_filename_;
		return false;
	}

	qDebug() << "ProxyTask: proxy generation succeeded:" << output_filename_;
	emit progress_changed(1.0);
	return true;
}

}
