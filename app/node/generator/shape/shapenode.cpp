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

#include "shapenode.h"

namespace olive
{

#define super ShapeNodeBase

QString ShapeNode::k_type_input = QStringLiteral("type_in");
QString ShapeNode::k_radius_input = QStringLiteral("radius_in");

ShapeNode::ShapeNode()
{
	prepend_input(k_type_input, NodeValue::k_combo);

	add_input(k_radius_input, NodeValue::k_float, 20.0);
	set_input_property(k_radius_input, QStringLiteral("min"), 0.0);
}

QString ShapeNode::name() const
{
	return tr("Shape");
}

QString ShapeNode::id() const
{
	return QStringLiteral("org.olivevideoeditor.Olive.shape");
}

QVector<Node::CategoryID> ShapeNode::category() const
{
	return { k_category_generator };
}

QString ShapeNode::description() const
{
	return tr("Generate a 2D primitive shape.");
}

void ShapeNode::retranslate()
{
	super::retranslate();

	set_input_name(k_type_input, tr("Type"));
	set_input_name(k_radius_input, tr("Radius"));

	// Coordinate with Type enum
	set_combo_box_strings(k_type_input, { tr("Rectangle"), tr("Ellipse"),
									 tr("Rounded Rectangle") });
}

ShaderCode ShapeNode::get_shader_code(const ShaderRequest &request) const
{
	if (request.id == QStringLiteral("shape")) {
		return ShaderCode(FileFunctions::read_file_as_string(
			QStringLiteral(":/shaders/shape.frag")));
	} else {
		return super::get_shader_code(request);
	}
}

void ShapeNode::value(const NodeValueRow &value, const NodeGlobals &globals,
					  NodeValueTable *table) const
{
	TexturePtr base = value[k_base_input].to_texture();

	ShaderJob job(value);

	job.insert(QStringLiteral("resolution_in"),
			   NodeValue(NodeValue::k_vec2,
						 base ? base->virtual_resolution() :
								globals.square_resolution(),
						 this));
	job.set_shader_id(QStringLiteral("shape"));

	push_mergable_job(
		value, Texture::job(base ? base->params() : globals.vparams(), job),
		table);
}

void ShapeNode::InputValueChangedEvent(const QString &input, int element)
{
	if (input == k_type_input) {
		set_input_flag(k_radius_input, k_input_flag_hidden,
					 (get_standard_value(k_type_input).toInt() !=
					  k_rounded_rectangle));
	}
	super::InputValueChangedEvent(input, element);
}

}
