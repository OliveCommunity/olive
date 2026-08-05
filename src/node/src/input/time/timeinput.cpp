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

#include "timeinput.h"

namespace olive
{

#define super Node

TimeInput::TimeInput()
{
}

std::string TimeInput::name() const
{
	return "Time";
}

std::string TimeInput::id() const
{
	return "org.olivevideoeditor.Olive.time";
}

std::vector<Node::CategoryID> TimeInput::category() const
{
	return { k_category_time };
}

std::string TimeInput::description() const
{
	return "Generates the time (in seconds) at this frame.";
}

void TimeInput::value(const NodeValueRow &value, const NodeGlobals &globals,
					  NodeValueTable *table) const
{
	table->push(NodeValue::k_float, globals.time().in().to_double(), this, false,
				"time");
}

}
