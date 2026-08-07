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

#include "proxy.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace olive
{

namespace
{

std::string shell_quote(const std::string &s)
{
	std::string out = "'";
	for (char c : s) {
		if (c == '\'') {
			out += "'\\''";
		} else {
			out += c;
		}
	}
	out += "'";
	return out;
}

/**
 * @brief Probes the source duration with the ffprobe next to ffmpeg
 *
 * Returns 0 when ffprobe is unavailable or the duration cannot be
 * determined, in which case the task simply reports no intermediate
 * progress.
 */
double probe_source_duration_seconds(const std::string &ffmpeg_path,
									 const std::string &source_filename)
{
	std::filesystem::path ffprobe =
		std::filesystem::path(ffmpeg_path).parent_path() / "ffprobe";
#if defined(_WIN32)
	ffprobe += ".exe";
#endif
	std::error_code ec;
	if (!std::filesystem::exists(ffprobe, ec)) {
		return 0.0;
	}

	std::string cmd = shell_quote(ffprobe.string()) +
					  " -v error -show_entries format=duration"
					  " -of default=noprint_wrappers=1:nokey=1 " +
					  shell_quote(source_filename);
	FILE *pipe = popen(cmd.c_str(), "r");
	if (!pipe) {
		return 0.0;
	}
	char buf[128] = { 0 };
	std::string output;
	while (fgets(buf, sizeof(buf), pipe)) {
		output += buf;
	}
	if (pclose(pipe) != 0) {
		return 0.0;
	}
	return strtod(output.c_str(), nullptr);
}

} // namespace

ProxyTask::ProxyTask(const OakCodecTaskRequest &request,
					 const oakcodec_proxy_params &params)
	: source_filename_(request.input_filename ? request.input_filename : "")
	, stream_index_(request.stream_index)
	, params_(params)
	, output_filename_(request.output_filename ? request.output_filename
											   : "")
{
	// Divider-based requests (width/height == 0) take the source fraction
	if (request.proxy_width > 0 && request.proxy_height > 0) {
		params_.width = request.proxy_width;
		params_.height = request.proxy_height;
		params_.divider = 1;
	}

	set_title("Generating Proxy " + source_filename_ + ":" +
			  std::to_string(stream_index_));
}

std::vector<std::string>
ProxyTask::build_arguments(const std::string &source_filename,
						   int stream_index,
						   const oakcodec_proxy_params &params,
						   const std::string &output_filename)
{
	std::string scale_filter;
	if (params.divider > 1) {
		// Fraction of the source resolution, rounded down to even dimensions
		// as required by yuv420p
		scale_filter = "scale=w=trunc(iw/" + std::to_string(params.divider) +
					   "/2)*2:h=trunc(ih/" + std::to_string(params.divider) +
					   "/2)*2";
	} else {
		scale_filter = "scale=w=" + std::to_string(params.width) +
					   ":h=" + std::to_string(params.height) +
					   ":force_original_aspect_ratio=decrease";
	}

	const std::string container_format =
		params.extension[0] ? params.extension : "mp4";

	std::vector<std::string> args;
	args.emplace_back("-y");
	// Report machine-readable progress on stdout for the task dialog
	args.emplace_back("-nostats");
	args.emplace_back("-progress");
	args.emplace_back("pipe:1");
	args.emplace_back("-i");
	args.emplace_back(source_filename);
	// Map the requested video stream first so it is stream 0 in the proxy
	args.emplace_back("-map");
	args.emplace_back("0:" + std::to_string(stream_index));

	if (params.include_audio) {
		// Keep the source audio (if any) so the proxy can also be used for
		// audio preview. Audio streams follow the video stream in source order.
		args.emplace_back("-map");
		args.emplace_back("0:a?");
		args.emplace_back("-c:a");
		args.emplace_back("aac");
		args.emplace_back("-b:a");
		args.emplace_back("128k");
	} else {
		args.emplace_back("-an");
	}

	args.emplace_back("-vf");
	args.emplace_back(scale_filter);
	args.emplace_back("-c:v");
	args.emplace_back("libx264");
	args.emplace_back("-preset");
	args.emplace_back(params.preset);
	args.emplace_back("-crf");
	args.emplace_back(std::to_string(params.crf));
	args.emplace_back("-pix_fmt");
	args.emplace_back("yuv420p");
	args.emplace_back("-movflags");
	args.emplace_back("+faststart");
	args.emplace_back("-f");
	args.emplace_back(container_format);
	args.emplace_back(output_filename);

	return args;
}

double ProxyTask::parse_progress(const std::string &line,
								 double duration_seconds)
{
	if (duration_seconds <= 0.0) {
		return -1.0;
	}

	int64_t out_time_us = -1;
	if (line.rfind("out_time_us=", 0) == 0) {
		out_time_us = strtoll(line.c_str() + 12, nullptr, 10);
	} else if (line.rfind("out_time_ms=", 0) == 0) {
		// Despite the name, ffmpeg reports this value in microseconds
		out_time_us = strtoll(line.c_str() + 12, nullptr, 10);
	}

	if (out_time_us < 0) {
		return -1.0;
	}

	double progress = out_time_us / 1000000.0 / duration_seconds;
	return progress < 0.0 ? 0.0 : (progress > 1.0 ? 1.0 : progress);
}

bool ProxyTask::run()
{
	char ffmpeg_buf[1024];
	if (oakcodec_proxy_find_ffmpeg(nullptr, ffmpeg_buf,
								   sizeof(ffmpeg_buf)) <= 0) {
		set_error("Failed to generate proxy: ffmpeg executable was not "
				  "found. Set the ffmpeg path in Preferences > Disk > Proxy "
				  "Settings.");
		fprintf(stderr, "ProxyTask: ffmpeg executable not found\n");
		return false;
	}

	std::filesystem::path output_dir =
		std::filesystem::path(output_filename_).parent_path();
	std::error_code ec;
	if (!output_dir.empty() && !std::filesystem::exists(output_dir, ec) &&
		!std::filesystem::create_directories(output_dir, ec)) {
		set_error("Failed to create proxy output directory");
		return false;
	}

	fprintf(stderr, "ProxyTask: starting ffmpeg proxy generation: %s -> %s\n",
			source_filename_.c_str(), output_filename_.c_str());

	std::filesystem::remove(output_filename_, ec);

	const std::string working_filename = output_filename_ + ".working.mp4";
	std::filesystem::remove(working_filename, ec);

	const std::vector<std::string> args = build_arguments(
		source_filename_, stream_index_, params_, working_filename);

	std::string cmd = shell_quote(ffmpeg_buf);
	for (const std::string &arg : args) {
		cmd += " ";
		cmd += shell_quote(arg);
	}
	cmd += " 2>&1";

	const double duration_seconds =
		probe_source_duration_seconds(ffmpeg_buf, source_filename_);

	FILE *process = popen(cmd.c_str(), "r");
	if (!process) {
		set_error("Failed to start ffmpeg for proxy generation");
		return false;
	}

	std::string progress_buffer;
	double last_progress = 0.0;
	char buf[256];

	while (true) {
		size_t n = fread(buf, 1, sizeof(buf), process);
		if (n > 0) {
			progress_buffer.append(buf, n);
			size_t newline;
			while ((newline = progress_buffer.find('\n')) !=
				   std::string::npos) {
				std::string line = progress_buffer.substr(0, newline);
				progress_buffer.erase(0, newline + 1);
				double progress = parse_progress(line, duration_seconds);
				if (progress >= 0.0 && progress - last_progress > 0.001) {
					last_progress = progress;
					emit_progress(progress);
				}
			}
		}

		if (n < sizeof(buf)) {
			if (feof(process)) {
				break;
			}
			if (ferror(process)) {
				break;
			}
		}

		if (is_cancelled()) {
			pclose(process);
			std::filesystem::remove(working_filename, ec);
			set_error("Proxy generation was cancelled");
			return false;
		}
	}

	int exit_status = pclose(process);
	if (exit_status != 0) {
		std::filesystem::remove(working_filename, ec);
		set_error("ffmpeg failed to generate proxy");
		fprintf(stderr, "ProxyTask: ffmpeg failed with exit status %d\n",
				exit_status);
		return false;
	}

	if (!std::filesystem::exists(working_filename, ec)) {
		set_error("ffmpeg finished but proxy file was not created");
		return false;
	}

	std::filesystem::rename(working_filename, output_filename_, ec);
	if (ec) {
		set_error("Failed to move proxy into place");
		return false;
	}

	fprintf(stderr, "ProxyTask: proxy generation succeeded: %s\n",
			output_filename_.c_str());
	emit_progress(1.0);
	return true;
}

}
