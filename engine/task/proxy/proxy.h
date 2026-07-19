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

#ifndef OAK_PROXYTASK_H
#define OAK_PROXYTASK_H

#include "codec/proxymanager.h"
#include "task/task.h"

namespace olive
{

class ProxyTask : public Task {
	Q_OBJECT
public:
	ProxyTask(const QString &source_filename, int stream_index,
			  const ProxyManager::ProxyParams &params,
			  const QString &output_filename);

	/**
	 * @brief Builds the ffmpeg command line for a proxy generation run
	 *
	 * Extracted for testability. The video stream is always mapped first so
	 * that it is stream 0 in the proxy file; audio streams (when enabled)
	 * follow in source order.
	 */
	static QStringList build_arguments(const QString &source_filename,
									  int stream_index,
									  const ProxyManager::ProxyParams &params,
									  const QString &output_filename);

	/**
	 * @brief Parses one line of ffmpeg "-progress" output
	 *
	 * Extracted for testability. If the line carries an output timestamp
	 * ("out_time_us=" or "out_time_ms="), returns the progress fraction in
	 * the range [0, 1] against duration_seconds. Returns a negative value
	 * when the line carries no timestamp or duration_seconds is unknown.
	 */
	static double parse_progress(const QString &line, double duration_seconds);

protected:
	virtual bool run() override;

private:
	QString source_filename_;
	int stream_index_;
	ProxyManager::ProxyParams params_;
	QString output_filename_;
};

}

#endif // OAK_PROXYTASK_H
