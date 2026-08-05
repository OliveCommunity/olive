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

#ifndef OAK_SERIALIZER210907_H
#define OAK_SERIALIZER210907_H

#include "serializer.h"

namespace olive
{

class ProjectSerializer210907 : public ProjectSerializer {
public:
	ProjectSerializer210907() = default;

protected:
	virtual LoadData load(Project *project, XmlStreamReader *reader,
						  LoadType load_type, void *reserved) const override;

	virtual uint version() const override
	{
		return 210907;
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
			uintptr_t input_node;
			std::string input_id;
			int input_element;
		};

		std::map<uintptr_t, Node *> node_ptrs;
		std::vector<SerializedConnection> desired_connections;
		std::vector<BlockLink> block_links;
		std::vector<GroupLink> group_input_links;
		std::map<NodeGroup *, uintptr_t> group_output_links;
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

	bool load_position(XmlStreamReader *reader, uintptr_t *node_ptr,
					  Node::Position *pos) const;

	void post_connect(const XMLNodeData &xml_node_data) const;

	void load_node_custom(XmlStreamReader *reader, Node *node,
						XMLNodeData &xml_node_data) const;

	void load_timeline_points(XmlStreamReader *reader,
							ViewerOutput *points) const;

	void load_work_area(XmlStreamReader *reader,
					  TimelineWorkArea *workarea) const;

	void load_marker_list(XmlStreamReader *reader,
						TimelineMarkerList *markers) const;

	void load_value_hint(Node::ValueHint *hint, XmlStreamReader *reader) const;
};

}

#endif // SERIALIZER211228_H
