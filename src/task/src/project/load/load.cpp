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

#include "node/serializer.h"

namespace olive
{

ProjectLoadBaseTask::ProjectLoadBaseTask(const std::string &filename)
	: project_({})
	, filename_(filename)
{
	set_title("Loading '" + filename + "'");
}

ProjectLoadTask::ProjectLoadTask(const std::string &filename)
	: ProjectLoadBaseTask(filename)
{
}

bool ProjectLoadTask::run()
{
	project_ = oaknode_project_init();
	if (!project_.ctx) {
		set_error("Failed to create project");
		return false;
	}

	oaknode_project_set_filename(project_, get_filename().c_str());

	int code = OAKNODE_SERIALIZER_RESULT_FILE_ERROR;
	char details[512];
	details[0] = 0;
	int result = oaknode_serializer_load_from_file(
		project_, get_filename().c_str(), &code, details, sizeof(details));

	bool success = false;
	switch (code) {
	case OAKNODE_SERIALIZER_RESULT_SUCCESS:
		success = true;
		break;
	case OAKNODE_SERIALIZER_RESULT_PROJECT_TOO_OLD:
		set_error("This project is from a version of Oak Video Editor that "
				  "is no longer supported in this version.");
		break;
	case OAKNODE_SERIALIZER_RESULT_PROJECT_TOO_NEW:
		set_error("This project is from a newer version of Oak Video Editor "
				  "and cannot be opened in this version.");
		break;
	case OAKNODE_SERIALIZER_RESULT_UNKNOWN_VERSION:
		set_error("Failed to determine project version.");
		break;
	case OAKNODE_SERIALIZER_RESULT_FILE_ERROR:
		set_error("Failed to read file \"" + get_filename() +
				  "\" for reading.");
		break;
	case OAKNODE_SERIALIZER_RESULT_XML_ERROR:
		set_error(std::string(
					  "Failed to read XML document. File may be corrupt. "
					  "Error was: ") +
				  details);
		break;
	case OAKNODE_SERIALIZER_RESULT_NO_DATA:
		set_error("Failed to find any data to parse.");
		break;
	default:
		set_error("Unknown error.");
		break;
	}

	if (result == OAKNODE_OK && success) {
		return true;
	}

	oaknode_project_free(&project_);
	return false;
}

}
