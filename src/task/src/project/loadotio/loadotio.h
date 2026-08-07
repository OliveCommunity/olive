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

#ifndef OAK_LOADOTIOTASK_H
#define OAK_LOADOTIOTASK_H

#include <functional>
#include <string>
#include <vector>

#include "../load/load.h"

namespace olive
{

class LoadOTIOTask : public ProjectLoadBaseTask {
public:
	LoadOTIOTask(const std::string &filename);

	/**
	 * @brief Ask the user which sequences to import (facade/UI concern).
	 *
	 * Receives the sequence labels, returns true to accept the import.
	 * When no callback is installed, everything is imported (headless
	 * default).
	 */
	using ImportConfirmFn = std::function<bool(
		const std::vector<std::string> &sequence_names)>;
	static void set_import_confirm_callback(ImportConfirmFn callback)
	{
		confirm_callback_ = std::move(callback);
	}

protected:
	virtual bool run() override;

private:
	static ImportConfirmFn confirm_callback_;
};

}

#endif // OAK_LOADOTIOTASK_H
