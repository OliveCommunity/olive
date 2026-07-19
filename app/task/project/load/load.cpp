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

#include "load.h"

#include <QApplication>

#include "node/project/serializer/serializer.h"

namespace olive
{

ProjectLoadTask::ProjectLoadTask(const QString &filename)
	: ProjectLoadBaseTask(filename)
{
}

bool ProjectLoadTask::run()
{
	project_ = new Project();

	project_->set_filename(get_filename());

	ProjectSerializer::Result result = ProjectSerializer::load(
		project_, get_filename(), ProjectSerializer::k_project);

	layout_ = result.get_load_data().layout;

	switch (result.code()) {
	case ProjectSerializer::k_success:
		break;
	case ProjectSerializer::k_project_too_old:
		set_error(tr(
			"This project is from a version of Oak Video Editor that is no longer supported in this version."));
		break;
	case ProjectSerializer::k_project_too_new:
		set_error(tr(
			"This project is from a newer version of Oak Video Editor and cannot be opened in this version."));
		break;
	case ProjectSerializer::k_unknown_version:
		set_error(tr("Failed to determine project version."));
		break;
	case ProjectSerializer::k_file_error:
		set_error(
			tr("Failed to read file \"%1\" for reading.").arg(get_filename()));
		break;
	case ProjectSerializer::k_xml_error:
		set_error(
			tr("Failed to read XML document. File may be corrupt. Error was: %1")
				.arg(result.get_details()));
		break;
	case ProjectSerializer::k_no_data:
		set_error(tr("Failed to find any data to parse."));
		break;

		// Errors that should never be thrown by a load
	case ProjectSerializer::k_overwrite_error:
		set_error(tr("Unknown error."));
		break;
	}

	if (result == ProjectSerializer::k_success) {
		project_->moveToThread(qApp->thread());
		return true;
	} else {
		delete project_;
		project_ = nullptr;
		return false;
	}
}

}
