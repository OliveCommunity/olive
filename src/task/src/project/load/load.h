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

#ifndef OAK_PROJECTLOADMANAGER_H
#define OAK_PROJECTLOADMANAGER_H

#include <string>

#include "node/project.h"
#include "task.h"

namespace olive
{

/**
 * @brief Base task for loading a project from a file
 */
class ProjectLoadBaseTask : public Task {
public:
	ProjectLoadBaseTask(const std::string &filename);

	/**
	 * @brief Take the loaded project (ownership transfer). Empty handle
	 *        (ctx == NULL) if the task has not succeeded.
	 */
	OakNodeProject take_project()
	{
		OakNodeProject p = project_;
		project_ = OakNodeProject{};
		return p;
	}

	const std::string &get_filename() const
	{
		return filename_;
	}

protected:
	OakNodeProject project_;

private:
	std::string filename_;
};

class ProjectLoadTask : public ProjectLoadBaseTask {
public:
	ProjectLoadTask(const std::string &filename);

protected:
	virtual bool run() override;
};

}

#endif // OAK_PROJECTLOADMANAGER_H
