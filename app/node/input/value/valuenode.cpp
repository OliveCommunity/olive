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

#include "valuenode.h"

namespace olive
{

const QString ValueNode::k_type_input = QStringLiteral("type_in");
const QString ValueNode::k_value_input = QStringLiteral("value_in");
const QVector<NodeValue::Type> ValueNode::k_supported_types = {
	NodeValue::k_float, NodeValue::k_int,		NodeValue::k_rational,
	NodeValue::k_vec2,  NodeValue::k_vec3,	NodeValue::k_vec4,
	NodeValue::k_color, NodeValue::k_text,	NodeValue::k_matrix,
	NodeValue::k_font,  NodeValue::k_boolean,
};

#define super Node

ValueNode::ValueNode()
{
	add_input(k_type_input, NodeValue::k_combo, 0,
			 InputFlags(k_input_flag_not_connectable | k_input_flag_not_keyframable));

	add_input(k_value_input, k_supported_types.first(), QVariant(),
			 InputFlags(k_input_flag_not_connectable));
}

void ValueNode::retranslate()
{
	super::retranslate();

	set_input_name(k_type_input, QStringLiteral("Type"));
	set_input_name(k_value_input, QStringLiteral("Value"));

	QStringList type_names;
	type_names.reserve(k_supported_types.size());
	foreach (NodeValue::Type type, k_supported_types) {
		type_names.append(NodeValue::get_pretty_data_type_name(type));
	}
	set_combo_box_strings(k_type_input, type_names);
}

void ValueNode::value(const NodeValueRow &value, const NodeGlobals &globals,
					  NodeValueTable *table) const
{
	Q_UNUSED(globals)

	// Ensure value is pushed onto the table
	table->push(value[k_value_input]);
}

void ValueNode::InputValueChangedEvent(const QString &input, int element)
{
	if (input == k_type_input) {
		set_input_data_type(
			k_value_input,
			k_supported_types.at(get_standard_value(k_type_input).toInt()));
	}

	super::InputValueChangedEvent(input, element);
}

}
