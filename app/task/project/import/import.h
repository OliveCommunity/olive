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

#include <QFileInfoList>
#include <QUndoCommand>

#include "codec/decoder.h"
#include "node/project/footage/footage.h"
#include "node/project/folder/folder.h"
#include "task/task.h"

namespace olive
{

class ProjectImportTask : public Task {
	Q_OBJECT
public:
	ProjectImportTask(Folder *folder, const QStringList &filenames);

	const int &get_file_count() const;

	MultiUndoCommand *get_command() const
	{
		return command_;
	}

	const QStringList &get_invalid_files() const
	{
		return invalid_files_;
	}

	bool has_invalid_files() const
	{
		return !invalid_files_.isEmpty();
	}

	const QVector<Footage *> &get_imported_footage() const
	{
		return imported_footage_;
	}

protected:
	virtual bool run() override;

private:
	void import(Folder *folder, QFileInfoList entries, int &counter,
				MultiUndoCommand *parent_command);

	void validate_image_sequence(Footage *footage, QFileInfoList &info_list,
							   int index);

	void add_item_to_folder(Folder *folder, Node *item, MultiUndoCommand *command);

	static bool item_is_still_image_footage_only(Footage *footage);

	static bool compare_still_image_size(Footage *footage, const QSize &sz);

	static int64_t get_image_sequence_limit(const QString &start_fn, int64_t start,
										 bool up);

	MultiUndoCommand *command_;

	Folder *folder_;

	QFileInfoList filenames_;

	int file_count_;

	QStringList invalid_files_;

	QList<QString> image_sequence_ignore_files_;

	QVector<Footage *> imported_footage_;
};

}

#endif // OAK_PROJECTIMPORTMANAGER_H
