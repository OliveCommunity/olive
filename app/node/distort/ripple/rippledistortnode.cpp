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

#include "rippledistortnode.h"

namespace olive
{

const QString RippleDistortNode::k_texture_input = QStringLiteral("tex_in");
const QString RippleDistortNode::k_evolution_input =
	QStringLiteral("evolution_in");
const QString RippleDistortNode::k_intensity_input =
	QStringLiteral("intensity_in");
const QString RippleDistortNode::k_frequency_input =
	QStringLiteral("frequency_in");
const QString RippleDistortNode::k_position_input = QStringLiteral("position_in");
const QString RippleDistortNode::k_stretch_input = QStringLiteral("stretch_in");

#define super Node

RippleDistortNode::RippleDistortNode()
{
	add_input(k_texture_input, NodeValue::k_texture,
			 InputFlags(k_input_flag_not_keyframable));

	add_input(k_evolution_input, NodeValue::k_float, 0);
	add_input(k_intensity_input, NodeValue::k_float, 100);

	add_input(k_frequency_input, NodeValue::k_float, 1);
	set_input_property(k_frequency_input, QStringLiteral("base"), 0.01);

	add_input(k_position_input, NodeValue::k_vec2, QVector2D(0, 0));
	add_input(k_stretch_input, NodeValue::k_boolean, false);

	set_flag(k_video_effect);
	set_effect_input(k_texture_input);

	gizmo_ = add_draggable_gizmo<PointGizmo>({
		NodeKeyframeTrackReference(NodeInput(this, k_position_input), 0),
		NodeKeyframeTrackReference(NodeInput(this, k_position_input), 1),
	});
	gizmo_->set_shape(PointGizmo::k_anchor_point);
}

QString RippleDistortNode::name() const
{
	return tr("Ripple");
}

QString RippleDistortNode::id() const
{
	return QStringLiteral("org.olivevideoeditor.Olive.ripple");
}

QVector<Node::CategoryID> RippleDistortNode::category() const
{
	return { k_category_distort };
}

QString RippleDistortNode::description() const
{
	return tr("Distorts an image with a ripple effect.");
}

void RippleDistortNode::retranslate()
{
	super::retranslate();

	set_input_name(k_texture_input, tr("Input"));
	set_input_name(k_frequency_input, tr("Frequency"));
	set_input_name(k_intensity_input, tr("Intensity"));
	set_input_name(k_evolution_input, tr("Evolution"));
	set_input_name(k_position_input, tr("Position"));
	set_input_name(k_stretch_input, tr("Stretch"));
}

ShaderCode RippleDistortNode::get_shader_code(const ShaderRequest &request) const
{
	Q_UNUSED(request)
	return ShaderCode(FileFunctions::read_file_as_string(":/shaders/ripple.frag"));
}

void RippleDistortNode::value(const NodeValueRow &value,
							  const NodeGlobals &globals,
							  NodeValueTable *table) const
{
	// If there's no texture, no need to run an operation
	if (TexturePtr tex = value[k_texture_input].to_texture()) {
		// Only run shader if at least one of flip or flop are selected
		if (!qIsNull(value[k_intensity_input].to_double())) {
			ShaderJob job(value);
			job.insert(QStringLiteral("resolution_in"),
					   NodeValue(NodeValue::k_vec2, tex->virtual_resolution(),
								 this));
			table->push(NodeValue::k_texture, tex->to_job(job), this);
		} else {
			// If we're not flipping or flopping just push the texture
			table->push(value[k_texture_input]);
		}
	}
}

void RippleDistortNode::update_gizmo_positions(const NodeValueRow &row,
											 const NodeGlobals &globals)
{
	if (TexturePtr tex = row[k_texture_input].to_texture()) {
		QPointF half_res(tex->virtual_resolution().x() / 2,
						 tex->virtual_resolution().y() / 2);
		gizmo_->set_point(half_res + row[k_position_input].to_vec2().toPointF());
	}
}

void RippleDistortNode::gizmo_drag_move(double x, double y,
									  const Qt::KeyboardModifiers &modifiers)
{
	NodeInputDragger &x_drag = gizmo_->get_draggers()[0];
	NodeInputDragger &y_drag = gizmo_->get_draggers()[1];

	x_drag.drag(x_drag.get_start_value().toDouble() + x);
	y_drag.drag(y_drag.get_start_value().toDouble() + y);
}

}
