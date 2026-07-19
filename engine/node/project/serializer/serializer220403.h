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
	virtual LoadData load(Project *project, QXmlStreamReader *reader,
						  LoadType load_type, void *reserved) const override;

	virtual uint version() const override
	{
		return 220403;
	}

private:
	struct XMLNodeData {
		struct SerializedConnection {
			NodeInput input;
			quintptr output_node;
		};

		struct BlockLink {
			Node *block;
			quintptr link;
		};

		struct GroupLink {
			NodeGroup *group;
			QString passthrough_id;
			quintptr input_node;
			QString input_id;
			int input_element;
			QString custom_name;
			InputFlags custom_flags;
			NodeValue::Type data_type;
			QVariant default_val;
			QHash<QString, QVariant> custom_properties;
		};

		QHash<quintptr, Node *> node_ptrs;
		QList<SerializedConnection> desired_connections;
		QList<BlockLink> block_links;
		QVector<GroupLink> group_input_links;
		QHash<NodeGroup *, quintptr> group_output_links;
		QHash<Node *, QUuid> node_uuids;
	};

	void load_node(Node *node, XMLNodeData &xml_node_data,
				  QXmlStreamReader *reader) const;

	void load_color_manager(QXmlStreamReader *reader, Project *project) const;

	void load_project_settings(QXmlStreamReader *reader, Project *project) const;

	void load_input(Node *node, QXmlStreamReader *reader,
				   XMLNodeData &xml_node_data) const;

	void load_immediate(QXmlStreamReader *reader, Node *node,
					   const QString &input, int element,
					   XMLNodeData &xml_node_data) const;

	void load_keyframe(QXmlStreamReader *reader, NodeKeyframe *key,
					  NodeValue::Type data_type) const;

	bool load_position(QXmlStreamReader *reader, quintptr *node_ptr,
					  Node::Position *pos) const;

	void post_connect(const XMLNodeData &xml_node_data) const;

	void load_node_custom(QXmlStreamReader *reader, Node *node,
						XMLNodeData &xml_node_data) const;

	void load_timeline_points(QXmlStreamReader *reader,
							ViewerOutput *viewer) const;

	void load_marker(QXmlStreamReader *reader, TimelineMarker *marker) const;

	void load_work_area(QXmlStreamReader *reader,
					  TimelineWorkArea *workarea) const;

	void load_marker_list(QXmlStreamReader *reader,
						TimelineMarkerList *markers) const;

	void load_value_hint(Node::ValueHint *hint, QXmlStreamReader *reader) const;
};

}

#endif // OAK_SERIALIZER220403_H
