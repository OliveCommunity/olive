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

#ifndef OAK_PROJECTSAVEMANAGER_H
#define OAK_PROJECTSAVEMANAGER_H

#include "node/project.h"
#include "node/project/serializer/serializedlayoutinfo.h"
#include "task/task.h"

namespace olive
{

class ProjectSaveTask : public Task {
	Q_OBJECT
public:
	ProjectSaveTask(Project *project, bool use_compression);

	Project *get_project() const
	{
		return project_;
	}

	void set_override_filename(const QString &filename)
	{
		override_filename_ = filename;
	}

	void set_layout(const SerializedLayoutInfo &layout)
	{
		layout_ = layout;
	}

protected:
	virtual bool run() override;

private:
	Project *project_;

	QString override_filename_;

	bool use_compression_;

	SerializedLayoutInfo layout_;
};

}

#endif // OAK_PROJECTSAVEMANAGER_H
