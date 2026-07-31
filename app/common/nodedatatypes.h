/***

  Oak - Non-Linear Video Editor
  Copyright (C) 2026 Oak Team

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

#ifndef OAK_NODEDATATYPES_H
#define OAK_NODEDATATYPES_H

namespace olive
{

/**
 * @brief App-side mirror of engine's olive::Node::DataType
 * (engine/node/node.h).
 *
 * Enumerator ordinals must stay in sync with the engine enum: the C ABI
 * oakengine_node_get_data() (and the oak::Node::data() wrapper) takes the
 * `role` argument as a plain int carrying these ordinals.
 */
enum NodeDataType {
	k_node_data_icon,
	k_node_data_duration,
	k_node_data_created_time,
	k_node_data_modified_time,
	k_node_data_frequency_rate,
	k_node_data_tooltip
};

}

#endif // OAK_NODEDATATYPES_H
