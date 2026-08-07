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

#ifndef OAK_PROXYTASK_H
#define OAK_PROXYTASK_H

#include <string>
#include <vector>

#include "codec/proxy.h"
#include "codec/task.h"
#include "task.h"

namespace olive
{

class ProxyTask : public Task {
public:
	ProxyTask(const OakCodecTaskRequest &request,
			  const oakcodec_proxy_params &params);

	/**
	 * @brief Builds the ffmpeg command line for a proxy generation run
	 *
	 * Extracted for testability. The video stream is always mapped first so
	 * that it is stream 0 in the proxy file; audio streams (when enabled)
	 * follow in source order.
	 */
	static std::vector<std::string> build_arguments(
		const std::string &source_filename, int stream_index,
		const oakcodec_proxy_params &params,
		const std::string &output_filename);

	/**
	 * @brief Parses one line of ffmpeg "-progress" output
	 *
	 * Extracted for testability. If the line carries an output timestamp
	 * ("out_time_us=" or "out_time_ms="), returns the progress fraction in
	 * the range [0, 1] against duration_seconds. Returns a negative value
	 * when the line carries no timestamp or duration_seconds is unknown.
	 */
	static double parse_progress(const std::string &line,
								 double duration_seconds);

protected:
	virtual bool run() override;

private:
	std::string source_filename_;
	int stream_index_;
	oakcodec_proxy_params params_;
	std::string output_filename_;
};

}

#endif // OAK_PROXYTASK_H
