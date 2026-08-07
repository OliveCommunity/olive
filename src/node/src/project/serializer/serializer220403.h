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

#ifndef OAK_SERIALIZER220403_H
#define OAK_SERIALIZER220403_H

#include "serializer.h"

namespace olive
{

class ProjectSerializer220403 : public ProjectSerializer {
public:
	ProjectSerializer220403() = default;

protected:
	virtual LoadData load(Project *project, XmlStreamReader *reader,
						  LoadType load_type, void *reserved) const override;

	virtual uint version() const override
	{
		return 220403;
	}

private:
	struct XMLNodeData {
		struct SerializedConnection {
			NodeInput input;
			uintptr_t output_node;
		};

		struct BlockLink {
			Node *block;
			uintptr_t link;
		};

		struct GroupLink {
			NodeGroup *group;
			std::string passthrough_id;
			uintptr_t input_node;
			std::string input_id;
			int input_element;
			std::string custom_name;
			InputFlags custom_flags;
			NodeValue::Type data_type;
			Variant default_val;
			std::map<std::string, Variant> custom_properties;
		};

		std::map<uintptr_t, Node *> node_ptrs;
		std::vector<SerializedConnection> desired_connections;
		std::vector<BlockLink> block_links;
		std::vector<GroupLink> group_input_links;
		std::map<NodeGroup *, uintptr_t> group_output_links;
		std::map<Node *, std::string> node_uuids;
	};

	void load_node(Node *node, XMLNodeData &xml_node_data,
				  XmlStreamReader *reader) const;

	void load_color_manager(XmlStreamReader *reader, Project *project) const;

	void load_project_settings(XmlStreamReader *reader, Project *project) const;

	void load_input(Node *node, XmlStreamReader *reader,
				   XMLNodeData &xml_node_data) const;

	void load_immediate(XmlStreamReader *reader, Node *node,
					   const std::string &input, int element,
					   XMLNodeData &xml_node_data) const;

	void load_keyframe(XmlStreamReader *reader, NodeKeyframe *key,
					  NodeValue::Type data_type) const;

	bool load_position(XmlStreamReader *reader, uintptr_t *node_ptr,
					  Node::Position *pos) const;

	void post_connect(const XMLNodeData &xml_node_data) const;

	void load_node_custom(XmlStreamReader *reader, Node *node,
						XMLNodeData &xml_node_data) const;

	void load_timeline_points(XmlStreamReader *reader,
							ViewerOutput *viewer) const;

	void load_marker(XmlStreamReader *reader, SerializedMarker *marker) const;

	void load_work_area(XmlStreamReader *reader,
					  const OakTimelineWorkArea &workarea) const;

	void load_marker_list(XmlStreamReader *reader,
						const OakTimelineMarkerList &markers) const;

	void load_value_hint(Node::ValueHint *hint, XmlStreamReader *reader) const;
};

}

#endif // OAK_SERIALIZER220403_H
