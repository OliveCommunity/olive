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

#include "trigonometry.h"

namespace olive
{

const std::string TrigonometryNode::k_method_in = "method_in";
const std::string TrigonometryNode::k_x_in = "x_in";

#define super Node

TrigonometryNode::TrigonometryNode()
{
	add_input(k_method_in, NodeValue::k_combo,
			 InputFlags(k_input_flag_not_connectable | k_input_flag_not_keyframable));

	add_input(k_x_in, NodeValue::k_float, 0.0);
}

std::string TrigonometryNode::name() const
{
	return "Trigonometry";
}

std::string TrigonometryNode::id() const
{
	return "org.olivevideoeditor.Olive.trigonometry";
}

std::vector<Node::CategoryID> TrigonometryNode::category() const
{
	return { k_category_math };
}

std::string TrigonometryNode::description() const
{
	return "Perform a trigonometry operation on a value.";
}

void TrigonometryNode::retranslate()
{
	super::retranslate();

	StringList strings = { "Sine",
						   "Cosine",
						   "Tangent",
						   "Inverse Sine",
						   "Inverse Cosine",
						   "Inverse Tangent",
						   "Hyperbolic Sine",
						   "Hyperbolic Cosine",
						   "Hyperbolic Tangent" };

	set_combo_box_strings(k_method_in, strings);

	set_input_name(k_method_in, "Method");

	set_input_name(k_x_in, "Value");
}

void TrigonometryNode::value(const NodeValueRow &value,
							 const NodeGlobals &globals,
							 NodeValueTable *table) const
{
	double x = value.at(k_x_in).to_double();

	switch (static_cast<Operation>(get_standard_value(k_method_in).to_int())) {
	case k_op_sine:
		x = std::sin(x);
		break;
	case k_op_cosine:
		x = std::cos(x);
		break;
	case k_op_tangent:
		x = std::tan(x);
		break;
	case k_op_arc_sine:
		x = std::asin(x);
		break;
	case k_op_arc_cosine:
		x = std::acos(x);
		break;
	case k_op_arc_tangent:
		x = std::atan(x);
		break;
	case k_op_hyp_sine:
		x = std::sinh(x);
		break;
	case k_op_hyp_cosine:
		x = std::cosh(x);
		break;
	case k_op_hyp_tangent:
		x = std::tanh(x);
		break;
	}

	table->push(NodeValue::k_float, x, this);
}

}
