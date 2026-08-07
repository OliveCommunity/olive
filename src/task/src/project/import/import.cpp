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

#include "import.h"

#include <filesystem>

#include "codec/decoder.h"
#include "common/config.h"
#include "common/videoparams.h"
#include "undo/undostack.h"

namespace olive
{

namespace
{

int count_files_recursive(const std::vector<std::string> &paths)
{
	int count = 0;
	std::error_code ec;
	for (const std::string &path : paths) {
		if (std::filesystem::is_directory(path, ec)) {
			for (const auto &entry :
				 std::filesystem::recursive_directory_iterator(path, ec)) {
				if (entry.is_regular_file(ec)) {
					count++;
				}
			}
		} else {
			count++;
		}
	}
	return count;
}

std::string basename_of(const std::string &path)
{
	return std::filesystem::path(path).filename().string();
}

} // namespace

ProjectImportTask::ImageSequenceConfirmFn ProjectImportTask::confirm_callback_;

ProjectImportTask::ProjectImportTask(
	OakNodeFolder folder, OakNodeProject project,
	const std::vector<std::string> &filenames)
	: command_({})
	, folder_(folder)
	, project_(project)
	, filenames_(filenames)
{
	file_count_ = count_files_recursive(filenames_);
	if (file_count_ <= 0) {
		file_count_ = 1;
	}

	set_title("Importing " + std::to_string(file_count_) + " file(s)");
}

ProjectImportTask::~ProjectImportTask()
{
	if (command_.ctx) {
		oakundo_command_free(&command_);
	}
	// Borrowed handles: releasing them only frees the handle boxes (the
	// footage nodes are owned by the project).
	for (OakNodeFootage &footage : imported_footage_) {
		if (footage.ctx) {
			footage.release(footage.ctx);
		}
	}
}

const int &ProjectImportTask::get_file_count() const
{
	return file_count_;
}

bool ProjectImportTask::run()
{
	command_ = oakundo_command_init_multi();
	if (!command_.ctx) {
		set_error("Failed to create import command");
		return false;
	}

	int imported = 0;

	import(folder_, filenames_, imported, command_);

	if (is_cancelled()) {
		oakundo_command_free(&command_);
		command_ = OakUndoCommand{};
		return false;
	}
	return true;
}

void ProjectImportTask::import(OakNodeFolder folder,
							   const std::vector<std::string> &entries,
							   int &counter, OakUndoCommand parent_command)
{
	std::vector<std::string> mutable_entries = entries;

	for (size_t i = 0; i < mutable_entries.size(); i++) {
		if (is_cancelled()) {
			break;
		}

		const std::string &file_path = mutable_entries[i];

		std::error_code ec;
		if (std::filesystem::is_directory(file_path, ec)) {
			// Create a folder corresponding to the directory and recurse
			std::vector<std::string> entry_list;
			for (const auto &entry :
				 std::filesystem::directory_iterator(file_path, ec)) {
				entry_list.push_back(entry.path().string());
			}

			if (!entry_list.empty()) {
				OakNodeFolder f = oaknode_folder_create(project_);
				if (!f.ctx) {
					continue;
				}

				oaknode_node_set_label(oaknode_folder_as_node(f),
									   basename_of(file_path).c_str());

				add_item_to_folder(folder, oaknode_folder_as_node(f),
								   parent_command);

				// Recursively follow this path
				import(f, entry_list, counter, parent_command);
			}

		} else {
			OakNodeFootage footage =
				oaknode_footage_create(project_, nullptr);
			if (!footage.ctx) {
				continue;
			}

			oaknode_footage_set_cancel_atom(footage, get_cancel_atom());

			bool ok = oaknode_footage_set_filename(footage,
												   file_path.c_str()) ==
					  OAKNODE_OK;

			OakCancelAtom empty_atom = {};
			oaknode_footage_set_cancel_atom(footage, empty_atom);

			if (ok && oaknode_footage_is_valid(footage)) {
				oaknode_node_set_label(oaknode_footage_as_node(footage),
									   basename_of(file_path).c_str());

				// See if this footage is an image sequence
				validate_image_sequence(footage, mutable_entries, i);

				// Create undoable command that adds the items to the model
				add_item_to_folder(folder, oaknode_footage_as_node(footage),
								   parent_command);

				// Add to vector
				imported_footage_.push_back(footage);
			} else {
				// Add to list so we can tell the user about it later
				invalid_files_.push_back(file_path);

				// Remove the invalid footage from the graph; the remove
				// command takes ownership on redo and deletes the node
				// when the command is destroyed.
				OakUndoCommand remove = oaknode_command_create_remove_node(
					oaknode_footage_as_node(footage));
				if (remove.ctx) {
					oakundo_command_redo_now(remove);
					oakundo_command_free(&remove);
				}
			}

			counter++;

			emit_progress(static_cast<double>(counter) /
						  static_cast<double>(file_count_));
		}
	}
}

void ProjectImportTask::validate_image_sequence(
	OakNodeFootage footage, std::vector<std::string> &info_list,
	size_t index)
{
	char filename[1024];
	if (oaknode_footage_filename(footage, filename, sizeof(filename)) <=
		0) {
		return;
	}

	// Heuristically determine whether this file is part of an image sequence or not.
	// By this point we've established that video contains a single still image stream.
	int digit_count = oakcodec_decoder_get_image_sequence_digit_count(filename);
	if (digit_count <= 0) {
		return;
	}

	bool ignored = false;
	for (const std::string &ignore : image_sequence_ignore_files_) {
		if (ignore == filename) {
			ignored = true;
			break;
		}
	}
	if (ignored) {
		return;
	}

	if (!item_is_still_image_footage_only(footage)) {
		return;
	}

	OakVideoParams video_stream = {};
	if (oaknode_footage_get_video_params(footage, 0, &video_stream) !=
		OAKNODE_OK) {
		return;
	}

	int width = 0, height = 0;
	oakcommon_videoparams_get_width(video_stream, &width);
	oakcommon_videoparams_get_height(video_stream, &height);

	int64_t ind = oakcodec_decoder_get_image_sequence_index(filename);

	// Check if files around exist that follow a sequence
	char prev_fn[1024], next_fn[1024];
	oakcodec_decoder_transform_image_sequence_file_name(filename, ind - 1,
													  prev_fn, sizeof(prev_fn));
	oakcodec_decoder_transform_image_sequence_file_name(filename, ind + 1,
													  next_fn, sizeof(next_fn));

	OakNodeFootage previous_file =
		oaknode_footage_create(project_, prev_fn);
	OakNodeFootage next_file = oaknode_footage_create(project_, next_fn);

	bool prev_matches =
		previous_file.ctx && oaknode_footage_is_valid(previous_file) &&
		compare_still_image_size(previous_file, width, height);
	bool next_matches =
		next_file.ctx && oaknode_footage_is_valid(next_file) &&
		compare_still_image_size(next_file, width, height);

	if (prev_matches || next_matches) {
		// By this point, we've established this file is a still image with a number at the end of
		// the filename surrounded by adjacent numbers. It could be a still image! But let's ask the
		// user just in case...
		bool is_sequence =
			confirm_callback_ ? confirm_callback_(filename) : false;

		int64_t seq_index = oakcodec_decoder_get_image_sequence_index(filename);

		// Heuristic to find the first and last images (users can always override this later in
		// FootagePropertiesDialog)
		int64_t start_index = get_image_sequence_limit(filename, seq_index,
													   false);
		int64_t end_index = get_image_sequence_limit(filename, seq_index,
													 true);

		// Depending on the user's choice, either remove them from the list or don't ask for
		// the remainders
		for (int64_t j = start_index; j <= end_index; j++) {
			char entry_fn[1024];
			oakcodec_decoder_transform_image_sequence_file_name(
				filename, j, entry_fn, sizeof(entry_fn));

			if (is_sequence) {
				// If this is part of the sequence we're importing here, remove it
				for (size_t k = index + 1; k < info_list.size(); k++) {
					if (info_list[k] == entry_fn) {
						info_list.erase(info_list.begin() + k);
						break;
					}
				}
			} else {
				image_sequence_ignore_files_.push_back(entry_fn);
			}
		}

		if (is_sequence) {
			// User has confirmed it is an image sequence, let's set it accordingly.
			oakcommon_videoparams_set_video_type(
				video_stream, OAKCOMMON_VIDEO_TYPE_IMAGE_SEQUENCE);

			char rate_str[64];
			int needed = oakcommon_config_get(
				nullptr, "DefaultSequenceFrameRate", rate_str,
				sizeof(rate_str));
			if (needed > 0) {
				int num = 0, den = 1;
				if (sscanf(rate_str, "%d/%d", &num, &den) == 2 && den != 0) {
					oakcommon_videoparams_set_time_base(video_stream, num,
														den);
					oakcommon_videoparams_set_frame_rate(video_stream, den,
														 num);
				}
			}

			oakcommon_videoparams_set_start_time(video_stream,
												 start_index);
			oakcommon_videoparams_set_duration(video_stream,
											   end_index - start_index + 1);

			oaknode_footage_set_video_params(footage, 0, &video_stream);
		}
	}

	oakcommon_videoparams_free(&video_stream);

	// The probe footage above was only created for comparison; remove it
	// from the graph again (the remove command deletes the node, see
	// import()).
	for (OakNodeFootage probe : { previous_file, next_file }) {
		if (probe.ctx) {
			OakUndoCommand remove = oaknode_command_create_remove_node(
				oaknode_footage_as_node(probe));
			if (remove.ctx) {
				oakundo_command_redo_now(remove);
				oakundo_command_free(&remove);
			}
		}
	}
}

void ProjectImportTask::add_item_to_folder(OakNodeFolder folder,
										   OakNodeNode item,
										   OakUndoCommand command)
{
	OakUndoCommand child =
		oaknode_command_create_folder_add_child(folder, item);
	if (child.ctx) {
		oakundo_command_multi_add_child(command, child);
	}
}

bool ProjectImportTask::item_is_still_image_footage_only(
	OakNodeFootage footage)
{
	if (oaknode_footage_total_stream_count(footage) != 1) {
		// Footage with more than one stream (usually video+audio) most likely isn't an image sequence
		return false;
	}

	OakVideoParams vp = {};
	if (oaknode_footage_get_video_params(footage, 0, &vp) != OAKNODE_OK) {
		return false;
	}

	int video_type = 0;
	oakcommon_videoparams_get_video_type(vp, &video_type);
	int valid = 0;
	oakcommon_videoparams_get_is_valid(vp, &valid);
	oakcommon_videoparams_free(&vp);

	// Footage must be valid and video stream must be a still image to be an image sequence
	return valid && video_type == OAKCOMMON_VIDEO_TYPE_STILL;
}

bool ProjectImportTask::compare_still_image_size(OakNodeFootage footage,
												 int width, int height)
{
	if (!item_is_still_image_footage_only(footage)) {
		return false;
	}

	OakVideoParams stream = {};
	if (oaknode_footage_get_video_params(footage, 0, &stream) !=
		OAKNODE_OK) {
		return false;
	}

	int w = 0, h = 0;
	oakcommon_videoparams_get_width(stream, &w);
	oakcommon_videoparams_get_height(stream, &h);
	oakcommon_videoparams_free(&stream);

	return w == width && h == height;
}

int64_t ProjectImportTask::get_image_sequence_limit(
	const std::string &start_fn, int64_t start, bool up)
{
	std::error_code ec;

	while (true) {
		int64_t test_index = up ? start + 1 : start - 1;

		char test_filename[1024];
		oakcodec_decoder_transform_image_sequence_file_name(
			start_fn.c_str(), test_index, test_filename,
			sizeof(test_filename));

		if (!std::filesystem::exists(test_filename, ec)) {
			// Reached end of index
			break;
		}

		start = test_index;
	}

	return start;
}

}
