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

#include <QDir>
#include <QFileInfo>

#include "config/config.h"
#include "core.h"
#include "node/nodeundo.h"
#include "node/project/footage/footage.h"

namespace olive
{

ProjectImportTask::ProjectImportTask(Folder *folder,
									 const QStringList &filenames)
	: command_(nullptr)
	, folder_(folder)
{
	foreach (const QString &f, filenames) {
		filenames_.append(QFileInfo(f));
	}

	file_count_ = Core::count_files_in_file_list(filenames_);

	set_title(tr("Importing %n file(s)", nullptr, file_count_));
}

const int &ProjectImportTask::get_file_count() const
{
	return file_count_;
}

bool ProjectImportTask::run()
{
	command_ = new MultiUndoCommand();

	int imported = 0;

	import(folder_, filenames_, imported, command_);

	if (is_cancelled()) {
		delete command_;
		command_ = nullptr;
		return false;
	} else {
		return true;
	}
}

void ProjectImportTask::import(Folder *folder, QFileInfoList entries,
							   int &counter, MultiUndoCommand *parent_command)
{
	for (int i = 0; i < entries.size(); i++) {
		if (is_cancelled()) {
			break;
		}

		const QFileInfo &file_info = entries.at(i);

		// Check if this file is a directory
		if (file_info.isDir()) {
			// QDir::entryList only returns filenames, we can use entryInfoList() to get full paths
			QFileInfoList entry_list =
				QDir(file_info.absoluteFilePath()).entryInfoList();

			// Strip out "." and ".." (for some reason QDir::NoDotAndDotDot	doesn't work with entryInfoList, so we have to
			// check manually)
			for (int j = 0; j < entry_list.size(); j++) {
				if (entry_list.at(j).fileName() == QStringLiteral(".") ||
					entry_list.at(j).fileName() == QStringLiteral("..")) {
					entry_list.removeAt(j);
					j--;
				}
			}

			// Only proceed if the empty actually has files in it
			if (!entry_list.isEmpty()) {
				// Create a folder corresponding to the directory
				Folder *f = new Folder();

				f->set_label(file_info.fileName());

				// Create undoable command that adds the items to the model
				add_item_to_folder(folder, f, parent_command);

				// Recursively follow this path
				import(f, entry_list, counter, parent_command);
			}

		} else {
			Footage *footage = new Footage();

			footage->set_cancel_pointer(this->get_cancel_atom());

			footage->set_filename(file_info.absoluteFilePath());
			footage->set_label(file_info.fileName());

			footage->set_cancel_pointer(nullptr);

			if (footage->is_valid()) {
				// See if this footage is an image sequence
				validate_image_sequence(footage, entries, i);

				// Create undoable command that adds the items to the model
				add_item_to_folder(folder, footage, parent_command);

				// Add to vector
				imported_footage_.push_back(footage);
			} else {
				// Add to list so we can tell the user about it later
				invalid_files_.append(file_info.absoluteFilePath());

				delete footage;
			}

			counter++;

			emit progress_changed(static_cast<double>(counter) /
								 static_cast<double>(file_count_));
		}
	}
}

void ProjectImportTask::validate_image_sequence(Footage *footage,
											  QFileInfoList &info_list,
											  int index)
{
	// Heuristically determine whether this file is part of an image sequence or not
	//
	// By this point we've established that video contains a single still image stream. Now we'll
	// see if it ends with numbers.
	if (Decoder::get_image_sequence_digit_count(footage->filename()) > 0 &&
		!image_sequence_ignore_files_.contains(footage->filename()) &&
		footage->input_array_size(Footage::k_video_params_input)) {
		VideoParams video_stream = footage->get_video_params(0);
		QSize dim(video_stream.width(), video_stream.height());

		int64_t ind = Decoder::get_image_sequence_index(footage->filename());

		// Check if files around exist around it with that follow a sequence
		QString previous_img_fn = Decoder::transform_image_sequence_file_name(
			footage->filename(), ind - 1);
		QString next_img_fn = Decoder::transform_image_sequence_file_name(
			footage->filename(), ind + 1);

		Footage *previous_file = new Footage(previous_img_fn);
		Footage *next_file = new Footage(next_img_fn);

		// Finally see if these files have the same dimensions
		if ((previous_file->is_valid() &&
			 compare_still_image_size(previous_file, dim)) ||
			(next_file->is_valid() && compare_still_image_size(next_file, dim))) {
			// By this point, we've established this file is a still image with a number at the end of
			// the filename surrounded by adjacent numbers. It could be a still image! But let's ask the
			// user just in case...
			bool is_sequence;

			QMetaObject::invokeMethod(Core::instance(), "confirm_image_sequence",
									  Qt::BlockingQueuedConnection,
									  Q_RETURN_ARG(bool, is_sequence),
									  Q_ARG(QString, footage->filename()));

			int64_t seq_index =
				Decoder::get_image_sequence_index(footage->filename());

			// Heuristic to find the first and last images (users can always override this later in
			// FootagePropertiesDialog)
			int64_t start_index =
				get_image_sequence_limit(footage->filename(), seq_index, false);
			int64_t end_index =
				get_image_sequence_limit(footage->filename(), seq_index, true);

			// Depending on the user's choice, either remove them from the list or don't ask for the
			// remainders
			for (int64_t j = start_index; j <= end_index; j++) {
				QString entry_fn = Decoder::transform_image_sequence_file_name(
					footage->filename(), j);

				if (is_sequence) {
					// If this is part of the sequence we're importing here, remove it
					for (int i = index + 1; i < info_list.size(); i++) {
						if (info_list.at(i).absoluteFilePath() == entry_fn) {
							if (is_sequence) {
								info_list.removeAt(i);
							}
							break;
						}
					}
				} else {
					image_sequence_ignore_files_.append(entry_fn);
				}
			}

			if (is_sequence) {
				// User has confirmed it is a still image, let's set it accordingly.
				video_stream.set_video_type(
					VideoParams::k_video_type_image_sequence);

				Rational default_timebase =
					OAK_CONFIG("DefaultSequenceFrameRate").value<Rational>();
				video_stream.set_time_base(default_timebase);
				video_stream.set_frame_rate(default_timebase.flipped());

				video_stream.set_start_time(start_index);
				video_stream.set_duration(end_index - start_index + 1);

				footage->set_video_params(video_stream, 0);
			}
		}

		delete previous_file;
		delete next_file;
	}
}

void ProjectImportTask::add_item_to_folder(Folder *folder, Node *item,
										MultiUndoCommand *command)
{
	// Create undoable command that adds the items to the model
	Project *project = folder_->project();

	NodeAddCommand *nac = new NodeAddCommand(project, item);
	nac->push_to_thread(project->thread());
	command->add_child(nac);

	command->add_child(new FolderAddChild(folder, item));
}

bool ProjectImportTask::item_is_still_image_footage_only(Footage *footage)
{
	if (footage->get_total_stream_count() != 1) {
		// Footage with more than one stream (usually video+audio) most likely isn't an image sequence
		return false;
	}

	VideoParams vp = footage->get_video_params(0);

	// Footage must be valid and video stream must be a still image to be an image sequence
	return vp.is_valid() && vp.video_type() == VideoParams::k_video_type_still;
}

bool ProjectImportTask::compare_still_image_size(Footage *footage, const QSize &sz)
{
	if (!item_is_still_image_footage_only(footage)) {
		return false;
	}

	VideoParams stream = footage->get_video_params(0);

	return stream.width() == sz.width() && stream.height() == sz.height();
}

int64_t ProjectImportTask::get_image_sequence_limit(const QString &start_fn,
												 int64_t start, bool up)
{
	QString test_filename;
	int test_index;

	forever
	{
		if (up) {
			test_index = start + 1;
		} else {
			test_index = start - 1;
		}

		test_filename =
			Decoder::transform_image_sequence_file_name(start_fn, test_index);

		if (!QFileInfo::exists(test_filename)) {
			// Reached end of index
			break;
		}

		start = test_index;
	}

	return start;
}

}
