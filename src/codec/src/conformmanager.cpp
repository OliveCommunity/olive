/*
 * Oak Video Editor - Non-Linear Video Editor
 * Copyright (C) 2025 Olive CE Team
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

#include "conformmanager.h"

#include <filesystem>

#include "common/filefunctions.h"

#include "taskcallbacks.h"

namespace olive
{

ConformManager *ConformManager::instance_ = nullptr;

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

} // namespace

ConformManager::Conform ConformManager::get_conform_state(
	const std::string &cache_path, const Decoder::CodecStream &stream,
	const core::AudioParams &params, bool wait)
{
	// Return existing conform if exists
	std::vector<std::string> filenames =
		get_conformed_filename(cache_path, stream, params);
	if (all_conforms_exist(filenames)) {
		return { k_conform_exists, filenames };
	}

	if (!oakcodec_task_submit_is_registered()) {
		// Interim state (pre-M8): no task system, conform cannot be generated
		return { k_conform_unavailable, std::vector<std::string>() };
	}

	// The task owns the ".working" temporary names and the rename to the
	// final per-channel filenames on success (previously done in
	// conform_task_finished); output_filename carries the first channel's
	// final path and the task derives the siblings.
	OakCodecTaskRequest req = {};
	req.kind = OAKCODEC_TASK_CONFORM;
	req.input_filename = stream.filename().c_str();
	req.output_filename =
		filenames.empty() ? nullptr : filenames.front().c_str();
	req.stream_index = stream.stream();
	req.sample_rate = params.sample_rate();
	req.channel_layout = params.channel_layout();
	req.sample_format = int(params.format());

	// Interim simplification: submission is synchronous - we always wait
	// for SubmitTask to return, regardless of `wait`.
	int result = SubmitTask(req);
	if (result < 0) {
		return { k_conform_unavailable, std::vector<std::string>() };
	}

	if (all_conforms_exist(filenames)) {
		return { k_conform_exists, filenames };
	}

	if (wait) {
		// Synchronous wait already happened and the conform still does not
		// exist: report the wait as failed.
		return { k_conform_unavailable, std::vector<std::string>() };
	}

	return { k_conform_generating, std::vector<std::string>() };
}

std::vector<std::string>
ConformManager::get_conformed_filename(const std::string &cache_path,
									   const Decoder::CodecStream &stream,
									   const core::AudioParams &params)
{
	std::vector<std::string> filenames(size_t(params.channel_count()));

	const std::string base = unique_file_identifier(stream.filename()) + "-" +
							 std::to_string(stream.stream()) + "." +
							 std::to_string(params.sample_rate()) + "." +
							 std::to_string(int(params.format())) + "." +
							 std::to_string(params.channel_layout());

	for (size_t i = 0; i < filenames.size(); i++) {
		filenames[i] = (std::filesystem::path(cache_path) /
						(base + "." + std::to_string(i) + ".pcm"))
						   .string();
	}

	return filenames;
}

bool ConformManager::all_conforms_exist(const std::vector<std::string> &filenames)
{
	std::error_code ec;
	for (const std::string &fn : filenames) {
		if (!std::filesystem::exists(fn, ec)) {
			return false;
		}
	}

	return true;
}

} // namespace olive
