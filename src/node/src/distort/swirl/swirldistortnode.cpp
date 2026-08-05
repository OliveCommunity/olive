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

#include "swirldistortnode.h"

namespace olive
{

const std::string SwirlDistortNode::k_texture_input = "tex_in";
const std::string SwirlDistortNode::k_radius_input = "radius_in";
const std::string SwirlDistortNode::k_angle_input = "angle_in";
const std::string SwirlDistortNode::k_position_input = "pos_in";

#define super Node

SwirlDistortNode::SwirlDistortNode()
{
	add_input(k_texture_input, NodeValue::k_texture,
			 InputFlags(k_input_flag_not_keyframable));

	add_input(k_radius_input, NodeValue::k_float, 200);
	set_input_property(k_radius_input, "min", 0);

	add_input(k_angle_input, NodeValue::k_float, 10);
	set_input_property(k_angle_input, "base", 0.1);

	add_input(k_position_input, NodeValue::k_vec2, Vector2D(0, 0));

	set_flag(k_video_effect);
	set_effect_input(k_texture_input);

	gizmo_ = add_draggable_gizmo<PointGizmo>({
		NodeKeyframeTrackReference(NodeInput(this, k_position_input), 0),
		NodeKeyframeTrackReference(NodeInput(this, k_position_input), 1),
	});
	gizmo_->set_shape(PointGizmo::k_anchor_point);
}

std::string SwirlDistortNode::name() const
{
	return "Swirl";
}

std::string SwirlDistortNode::id() const
{
	return "org.olivevideoeditor.Olive.swirl";
}

std::vector<Node::CategoryID> SwirlDistortNode::category() const
{
	return { k_category_distort };
}

std::string SwirlDistortNode::description() const
{
	return "Distorts an image by swirling it around a center point.";
}

void SwirlDistortNode::retranslate()
{
	super::retranslate();

	set_input_name(k_texture_input, "Input");
	set_input_name(k_radius_input, "Radius");
	set_input_name(k_angle_input, "Angle");
	set_input_name(k_position_input, "Position");
}

ShaderCode SwirlDistortNode::get_shader_code(const ShaderRequest &request) const
{
	(void) request;
	return ShaderCode(FileFunctions::read_file_as_string(":/shaders/swirl.frag"));
}

void SwirlDistortNode::value(const NodeValueRow &value,
							 const NodeGlobals &globals,
							 NodeValueTable *table) const
{
	// If there's no texture, no need to run an operation
	if (TexturePtr tex = value.at(k_texture_input).to_texture()) {
		// Only run shader if at least one of flip or flop are selected
		if (value.at(k_angle_input).to_double() != 0.0 &&
			value.at(k_radius_input).to_double() != 0.0) {
			ShaderJob job(value);
			job.insert("resolution_in",
					   NodeValue(NodeValue::k_vec2, tex->virtual_resolution(),
								 this));
			table->push(NodeValue::k_texture, tex->to_job(job), this);
		} else {
			// If we're not flipping or flopping just push the texture
			table->push(value.at(k_texture_input));
		}
	}
}

void SwirlDistortNode::update_gizmo_positions(const NodeValueRow &row,
											const NodeGlobals &globals)
{
	PointF half_res(globals.square_resolution().x() / 2,
					 globals.square_resolution().y() / 2);

	gizmo_->set_point(half_res + row.at(k_position_input).to_vec2().to_point_f());
}

void SwirlDistortNode::gizmo_drag_move(double x, double y, int modifiers)
{
	NodeInputDragger &x_drag = gizmo_->get_draggers()[0];
	NodeInputDragger &y_drag = gizmo_->get_draggers()[1];

	x_drag.drag(x_drag.get_start_value().to_double() + x);
	y_drag.drag(y_drag.get_start_value().to_double() + y);
}

}
