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

#ifndef OAK_NODEVIEWCOMMON_H
#define OAK_NODEVIEWCOMMON_H

#include <QtGlobal>

#include "oakutil/define.h"

namespace olive
{

class NodeViewCommon {
public:
	enum FlowDirection {
		k_invalid_direction = -1,
		k_top_to_bottom,
		k_bottom_to_top,
		k_left_to_right,
		k_right_to_left
	};

	static Qt::Orientation get_flow_orientation(FlowDirection dir)
	{
		if (dir == k_top_to_bottom || dir == k_bottom_to_top) {
			return Qt::Vertical;
		} else {
			return Qt::Horizontal;
		}
	}

	static bool is_flow_vertical(FlowDirection dir)
	{
		return dir == k_top_to_bottom || dir == k_bottom_to_top;
	}

	static bool is_flow_horizontal(FlowDirection dir)
	{
		return dir == k_left_to_right || dir == k_right_to_left;
	}

	static bool directions_are_opposing(FlowDirection a, FlowDirection b)
	{
		return ((a == NodeViewCommon::k_left_to_right &&
				 b == NodeViewCommon::k_right_to_left) ||
				(a == NodeViewCommon::k_right_to_left &&
				 b == NodeViewCommon::k_left_to_right) ||
				(a == NodeViewCommon::k_top_to_bottom &&
				 b == NodeViewCommon::k_bottom_to_top) ||
				(a == NodeViewCommon::k_bottom_to_top &&
				 b == NodeViewCommon::k_top_to_bottom));
	}
};

}

#endif // OAK_NODEVIEWCOMMON_H
