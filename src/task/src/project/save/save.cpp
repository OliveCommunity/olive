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

#include "node/serializer.h"

namespace olive
{

namespace
{

std::string project_filename(OakNodeProject *project)
{
	int needed = oaknode_project_filename(project, nullptr, 0);
	if (needed <= 0) {
		return std::string();
	}
	std::string filename(size_t(needed), 0);
	oaknode_project_filename(project, filename.data(), needed);
	filename.resize(size_t(needed) - 1);
	return filename;
}

} // namespace

ProjectSaveTask::ProjectSaveTask(OakNodeProject *project,
								 bool use_compression)
	: project_(project)
	, use_compression_(use_compression)
{
	set_title("Saving '" + project_filename(project_) + "'");
}

bool ProjectSaveTask::run()
{
	std::string using_filename = override_filename_.empty()
									 ? project_filename(project_)
									 : override_filename_;

	if (using_filename.empty()) {
		set_error("Project has no filename to save to.");
		return false;
	}

	int code = OAKNODE_SERIALIZER_RESULT_FILE_ERROR;
	char details[512];
	details[0] = 0;
	int result = oaknode_serializer_save_to_file(
		project_, using_filename.c_str(), use_compression_ ? 1 : 0, &code,
		details, sizeof(details));

	bool success = false;
	switch (code) {
	case OAKNODE_SERIALIZER_RESULT_SUCCESS:
		success = true;
		break;
	case OAKNODE_SERIALIZER_RESULT_XML_ERROR:
		set_error("Failed to write XML data.");
		break;
	case OAKNODE_SERIALIZER_RESULT_FILE_ERROR:
		set_error(std::string("Failed to open file for writing: ") + details);
		break;
	case OAKNODE_SERIALIZER_RESULT_OVERWRITE_ERROR:
		set_error("Failed to overwrite \"" + using_filename +
				  "\". Project has been saved as \"" + details +
				  "\" instead.");
		success = true;
		break;
	default:
		set_error("Unknown error.");
		break;
	}

	(void)result;
	return success;
}

}
