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

#include <cstdlib>
#include <filesystem>
#include <vector>

#include <unistd.h>

#include "common/filefunctions.h"

#include "taskcallbacks.h"

namespace olive
{

ProxyManager *ProxyManager::instance_ = nullptr;

namespace
{

/**
 * @brief oakcommon C API wrapper for FileFunctions::get_unique_file_identifier
 */
std::string unique_file_identifier(const std::string &filename)
{
	OakFileFunctions ff = oakcommon_filefunctions_init();
	if (!ff.ctx) {
		return std::string();
	}

	std::string result;
	int size = oakcommon_filefunctions_get_unique_file_identifier(
		ff, filename.c_str(), nullptr, 0);
	if (size > 0) {
		result.resize(size_t(size) - 1); // size includes the NUL
		oakcommon_filefunctions_get_unique_file_identifier(
			ff, filename.c_str(), result.data(), size);
	}

	oakcommon_filefunctions_free(&ff);
	return result;
}

/**
 * @brief oakcommon C API wrapper for FileFunctions::get_application_path
 */
std::string application_path()
{
	OakFileFunctions ff = oakcommon_filefunctions_init();
	if (!ff.ctx) {
		return std::string();
	}

	std::string result;
	int size = oakcommon_filefunctions_get_application_path(ff, nullptr, 0);
	if (size > 0) {
		result.resize(size_t(size) - 1);
		oakcommon_filefunctions_get_application_path(ff, result.data(), size);
	}

	oakcommon_filefunctions_free(&ff);
	return result;
}

bool is_executable_file(const std::filesystem::path &p)
{
	std::error_code ec;
	return std::filesystem::is_regular_file(p, ec) &&
		   ::access(p.c_str(), X_OK) == 0;
}

} // namespace

std::string ProxyManager::get_proxy_directory(const std::string &cache_path)
{
	return (std::filesystem::path(cache_path) / "proxy").string();
}

std::string ProxyManager::get_proxy_filename(const std::string &cache_path,
											 const std::string &source_filename,
											 int stream_index,
											 const ProxyParams &params)
{
	const std::string proxy_dir = get_proxy_directory(cache_path);
	const std::string extension =
		params.extension.empty() ? "mp4" : params.extension;

	// Divider mode scales relative to the source, so the tag names the
	// divider rather than an absolute target size
	std::string size_tag;
	if (params.divider > 1) {
		size_tag = "div" + std::to_string(params.divider);
	} else {
		size_tag = std::to_string(params.width) + "x" +
				   std::to_string(params.height);
	}

	const std::string filename = unique_file_identifier(source_filename) + "-" +
								 std::to_string(stream_index) + "." + size_tag +
								 ".v" + std::to_string(params.version) + ".a" +
								 (params.include_audio ? "1" : "0") + "." +
								 extension;

	return (std::filesystem::path(proxy_dir) / filename).string();
}

std::string ProxyManager::get_working_proxy_filename(const std::string &proxy_filename)
{
	// Append a recognizable suffix while keeping a standard container extension
	// so ffmpeg can infer the output format.
	return proxy_filename + ".working.mp4";
}

ProxyManager::ProxyState
ProxyManager::get_proxy_state(const std::string &proxy_filename)
{
	std::error_code ec;
	if (std::filesystem::exists(proxy_filename, ec)) {
		return k_proxy_ready;
	}

	if (std::filesystem::exists(get_working_proxy_filename(proxy_filename), ec)) {
		return k_proxy_generating;
	}

	return k_proxy_missing;
}

std::string ProxyManager::proxy_state_to_string(ProxyState state)
{
	switch (state) {
	case k_proxy_missing:
		return "missing";
	case k_proxy_generating:
		return "generating";
	case k_proxy_ready:
		return "ready";
	case k_proxy_failed:
		return "failed";
	}

	return "missing";
}

ProxyManager::ProxyState
ProxyManager::proxy_state_from_string(const std::string &state)
{
	if (state == "generating") {
		return k_proxy_generating;
	}

	if (state == "ready") {
		return k_proxy_ready;
	}

	if (state == "failed") {
		return k_proxy_failed;
	}

	return k_proxy_missing;
}

bool ProxyManager::proxy_filename_has_audio(const std::string &proxy_filename)
{
	return std::filesystem::path(proxy_filename)
			   .filename()
			   .string()
			   .find(".a1.") != std::string::npos;
}

ProxyManager::ProxyParams ProxyManager::proxy_params_from_config()
{
	// Interim state: the Qt config store (OAK_CONFIG ProxyWidth/ProxyHeight/
	// ProxyDivider/ProxyCRF/ProxyPreset/ProxyIncludeAudio) is not split yet,
	// so the compiled-in defaults apply.
	return ProxyParams();
}

std::string ProxyManager::find_f_fmpeg_executable(const std::string &configured_path)
{
	// An explicitly configured path takes precedence if it is usable
	if (!configured_path.empty()) {
		if (is_executable_file(configured_path)) {
			return std::filesystem::absolute(configured_path).string();
		}

		fprintf(stderr, "Configured ffmpeg path is not a valid executable: %s\n",
				configured_path.c_str());
	}

	// Fall back to searching the system PATH
	if (const char *path_env = std::getenv("PATH")) {
		std::string paths = path_env;
		size_t pos = 0;
		while (pos <= paths.size()) {
			size_t colon = paths.find(':', pos);
			std::string dir = paths.substr(
				pos, colon == std::string::npos ? colon : colon - pos);
			if (!dir.empty()) {
				std::filesystem::path candidate = std::filesystem::path(dir) / "ffmpeg";
				if (is_executable_file(candidate)) {
					return candidate.string();
				}
			}
			if (colon == std::string::npos) {
				break;
			}
			pos = colon + 1;
		}
	}

	// Finally, try common install locations (PATH on GUI-launched apps,
	// particularly on macOS, often lacks these)
	std::vector<std::string> candidates;
	const std::string app_path = application_path();
	if (!app_path.empty()) {
		candidates.push_back(app_path + "/ffmpeg");
	}
#ifdef __APPLE__
	candidates.push_back("/opt/homebrew/bin/ffmpeg");
	candidates.push_back("/usr/local/bin/ffmpeg");
#endif
	candidates.push_back("/usr/bin/ffmpeg");
	candidates.push_back("/usr/local/bin/ffmpeg");

	for (const std::string &candidate : candidates) {
		if (is_executable_file(candidate)) {
			return std::filesystem::absolute(candidate).string();
		}
	}

	return std::string();
}

ProxyManager::Proxy
ProxyManager::get_or_start_proxy(const std::string &cache_path,
								 const std::string &source_filename, int stream_index,
								 const ProxyParams &params)
{
	const std::string filename =
		get_proxy_filename(cache_path, source_filename, stream_index, params);
	const ProxyState file_state = get_proxy_state(filename);
	if (file_state == k_proxy_ready) {
		return { k_proxy_ready, filename };
	}

	if (!oakcodec_task_submit_is_registered()) {
		// Interim state (pre-M8): no task system, proxy cannot be generated
		return { k_proxy_missing, filename };
	}

	if (file_state == k_proxy_generating) {
		// Stale working file from an interrupted run
		std::error_code ec;
		std::filesystem::remove(get_working_proxy_filename(filename), ec);
	}

	// The task owns the ".working.mp4" temporary name and the rename to the
	// final filename on success (previously done in proxy_task_finished).
	OakCodecTaskRequest req = {};
	req.kind = OAKCODEC_TASK_PROXY;
	req.input_filename = source_filename.c_str();
	req.output_filename = filename.c_str();
	req.stream_index = stream_index;
	if (params.divider <= 1) {
		req.proxy_width = params.width;
		req.proxy_height = params.height;
	}

	// Interim simplification: submission is synchronous.
	int result = SubmitTask(req);
	if (result < 0) {
		return { k_proxy_failed, filename };
	}

	if (get_proxy_state(filename) == k_proxy_ready) {
		return { k_proxy_ready, filename };
	}

	return { k_proxy_generating, filename };
}

} // namespace olive
