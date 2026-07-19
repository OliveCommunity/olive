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

#include "save.h"

#include <QDir>
#include <QFile>
#include <QXmlStreamWriter>

#include "common/filefunctions.h"
#include "core.h"
#include "node/project/serializer/serializer.h"

namespace olive
{

ProjectSaveTask::ProjectSaveTask(Project *project, bool use_compression)
	: project_(project)
	, use_compression_(use_compression)
{
	set_title(tr("Saving '%1'").arg(project->filename()));
}

bool ProjectSaveTask::run()
{
	QString using_filename = override_filename_.isEmpty() ?
								 project_->filename() :
								 override_filename_;

	ProjectSerializer::SaveData data(ProjectSerializer::k_project);

	data.set_filename(using_filename);
	data.set_project(project_);
	data.set_layout(layout_);

	ProjectSerializer::Result result =
		ProjectSerializer::save(data, use_compression_);

	bool success = false;

	switch (result.code()) {
	case ProjectSerializer::k_success:
		success = true;
		break;
	case ProjectSerializer::k_xml_error:
		set_error(tr("Failed to write XML data."));
		break;
	case ProjectSerializer::k_file_error:
		set_error(tr("Failed to open file \"%1\" for writing.")
					 .arg(result.get_details()));
		break;
	case ProjectSerializer::k_overwrite_error:
		set_error(
			tr("Failed to overwrite \"%1\". Project has been saved as \"%2\" instead.")
				.arg(using_filename, result.get_details()));
		success = true;
		break;

		// Errors that should never be thrown by a save
	case ProjectSerializer::k_project_too_new:
	case ProjectSerializer::k_project_too_old:
	case ProjectSerializer::k_unknown_version:
	case ProjectSerializer::k_no_data:
		set_error(tr("Unknown error."));
		break;
	}

	return success;
}

}
