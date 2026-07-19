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

const QString TrigonometryNode::k_method_in = QStringLiteral("method_in");
const QString TrigonometryNode::k_x_in = QStringLiteral("x_in");

#define super Node

TrigonometryNode::TrigonometryNode()
{
	add_input(k_method_in, NodeValue::k_combo,
			 InputFlags(k_input_flag_not_connectable | k_input_flag_not_keyframable));

	add_input(k_x_in, NodeValue::k_float, 0.0);
}

QString TrigonometryNode::name() const
{
	return tr("Trigonometry");
}

QString TrigonometryNode::id() const
{
	return QStringLiteral("org.olivevideoeditor.Olive.trigonometry");
}

QVector<Node::CategoryID> TrigonometryNode::category() const
{
	return { k_category_math };
}

QString TrigonometryNode::description() const
{
	return tr("Perform a trigonometry operation on a value.");
}

void TrigonometryNode::retranslate()
{
	super::retranslate();

	QStringList strings = { tr("Sine"),
							tr("Cosine"),
							tr("Tangent"),
							tr("Inverse Sine"),
							tr("Inverse Cosine"),
							tr("Inverse Tangent"),
							tr("Hyperbolic Sine"),
							tr("Hyperbolic Cosine"),
							tr("Hyperbolic Tangent") };

	set_combo_box_strings(k_method_in, strings);

	set_input_name(k_method_in, tr("Method"));

	set_input_name(k_x_in, tr("Value"));
}

void TrigonometryNode::value(const NodeValueRow &value,
							 const NodeGlobals &globals,
							 NodeValueTable *table) const
{
	double x = value[k_x_in].to_double();

	switch (static_cast<Operation>(get_standard_value(k_method_in).toInt())) {
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
