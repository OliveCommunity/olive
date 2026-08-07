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

#ifndef OAK_PROJECTIMPORTMANAGER_H
#define OAK_PROJECTIMPORTMANAGER_H

#include <functional>
#include <string>
#include <vector>

#include "node/folder.h"
#include "node/footage.h"
#include "node/node.h"
#include "task.h"
#include "undo/undocommand.h"

namespace olive
{

class ProjectImportTask : public Task {
public:
	ProjectImportTask(OakNodeFolder folder, OakNodeProject project,
					  const std::vector<std::string> &filenames);
	~ProjectImportTask() override;

	const int &get_file_count() const;

	/** Take ownership of the import command. After this call the task no
	 *  longer owns (and will not free) the returned command. */
	OakUndoCommand take_command()
	{
		OakUndoCommand c = command_;
		command_ = OakUndoCommand{};
		return c;
	}

	const std::vector<std::string> &get_invalid_files() const
	{
		return invalid_files_;
	}

	bool has_invalid_files() const
	{
		return !invalid_files_.empty();
	}

	const std::vector<OakNodeFootage> &get_imported_footage() const
	{
		return imported_footage_;
	}

	/**
	 * @brief Callback asking the user whether numbered stills form an
	 *        image sequence (facade/UI concern). When no callback is
	 *        installed, files are NOT treated as sequences (safest
	 *        default - imports every file individually).
	 */
	using ImageSequenceConfirmFn = std::function<bool(
		const std::string &filename)>;
	static void set_image_sequence_confirm_callback(
		ImageSequenceConfirmFn callback)
	{
		confirm_callback_ = std::move(callback);
	}

protected:
	virtual bool run() override;

private:
	void import(OakNodeFolder folder,
				const std::vector<std::string> &entries, int &counter,
				OakUndoCommand parent_command);

	void validate_image_sequence(OakNodeFootage footage,
								 std::vector<std::string> &info_list,
								 size_t index);

	void add_item_to_folder(OakNodeFolder folder, OakNodeNode item,
							OakUndoCommand command);

	static bool item_is_still_image_footage_only(OakNodeFootage footage);

	static bool compare_still_image_size(OakNodeFootage footage, int width,
										 int height);

	static int64_t get_image_sequence_limit(const std::string &start_fn,
											int64_t start, bool up);

	OakUndoCommand command_;

	/** Borrowed handles; the caller keeps ownership of both. */
	OakNodeFolder folder_;

	OakNodeProject project_;

	std::vector<std::string> filenames_;

	int file_count_;

	std::vector<std::string> invalid_files_;

	std::vector<std::string> image_sequence_ignore_files_;

	/** Borrowed footage handles; the project owns the footage nodes. */
	std::vector<OakNodeFootage> imported_footage_;

	static ImageSequenceConfirmFn confirm_callback_;
};

}

#endif // OAK_PROJECTIMPORTMANAGER_H
