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

#include "tiledistortnode.h"

#include <cmath>

#include "sliderdisplaytype.h"

namespace olive
{

const std::string TileDistortNode::k_texture_input = "tex_in";
const std::string TileDistortNode::k_scale_input = "scale_in";
const std::string TileDistortNode::k_position_input = "position_in";
const std::string TileDistortNode::k_anchor_input = "anchor_in";
const std::string TileDistortNode::k_mirror_x_input = "mirrorx_in";
const std::string TileDistortNode::k_mirror_y_input = "mirrory_in";

#define super Node

TileDistortNode::TileDistortNode()
{
	add_input(k_texture_input, NodeValue::k_texture,
			 InputFlags(k_input_flag_not_keyframable));

	add_input(k_scale_input, NodeValue::k_float, 0.5);
	set_input_property(k_scale_input, "min", 0);
	set_input_property(k_scale_input, "view",
					 slider::k_percentage);

	add_input(k_position_input, NodeValue::k_vec2, Vector2D(0, 0));

	add_input(k_anchor_input, NodeValue::k_combo, k_middle_center);

	add_input(k_mirror_x_input, NodeValue::k_boolean, false);
	add_input(k_mirror_y_input, NodeValue::k_boolean, false);

	set_flag(k_video_effect);
	set_effect_input(k_texture_input);

	gizmo_ = add_draggable_gizmo<PointGizmo>({
		NodeKeyframeTrackReference(NodeInput(this, k_position_input), 0),
		NodeKeyframeTrackReference(NodeInput(this, k_position_input), 1),
	});
	gizmo_->set_shape(PointGizmo::k_anchor_point);
}

std::string TileDistortNode::name() const
{
	return "Tile";
}

std::string TileDistortNode::id() const
{
	return "org.olivevideoeditor.Olive.tile";
}

std::vector<Node::CategoryID> TileDistortNode::category() const
{
	return { k_category_distort };
}

std::string TileDistortNode::description() const
{
	return "Infinitely tile an image horizontally and vertically.";
}

void TileDistortNode::retranslate()
{
	super::retranslate();

	set_input_name(k_texture_input, "Input");
	set_input_name(k_scale_input, "Scale");
	set_input_name(k_position_input, "Position");
	set_input_name(k_mirror_x_input, "Mirror Horizontally");
	set_input_name(k_mirror_y_input, "Mirror Vertically");

	set_input_name(k_anchor_input, "Anchor");
	set_combo_box_strings(k_anchor_input, {
										 "Top-Left",
										 "Top-Center",
										 "Top-Right",
										 "Middle-Left",
										 "Middle-Center",
										 "Middle-Right",
										 "Bottom-Left",
										 "Bottom-Center",
										 "Bottom-Right",
									 });
}

ShaderCode TileDistortNode::get_shader_code(const ShaderRequest &request) const
{
	(void) request;
	return ShaderCode(FileFunctions::read_file_as_string(":/shaders/tile.frag"));
}

void TileDistortNode::value(const NodeValueRow &value,
							const NodeGlobals &globals,
							NodeValueTable *table) const
{
	// If there's no texture, no need to run an operation
	if (TexturePtr tex = value.at(k_texture_input).to_texture()) {
		// Only run shader if at least one of flip or flop are selected
		double scale_value = value.at(k_scale_input).to_double();
		if (!(std::abs(scale_value - 1.0) * 1000000000000.0 <=
			  std::min(std::abs(scale_value), std::abs(1.0)))) {
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

void TileDistortNode::update_gizmo_positions(const NodeValueRow &row,
										   const NodeGlobals &globals)
{
	if (TexturePtr tex = row.at(k_texture_input).to_texture()) {
		PointF res = tex->virtual_resolution().to_point_f();
		PointF pos = row.at(k_position_input).to_vec2().to_point_f();
		double x = pos.x();
		double y = pos.y();

		Anchor a = static_cast<Anchor>(row.at(k_anchor_input).to_int());
		if (a == k_top_left || a == k_top_center || a == k_top_right) {
			// Do nothing
		} else if (a == k_middle_left || a == k_middle_center ||
				   a == k_middle_right) {
			y += res.y() / 2;
		} else if (a == k_bottom_left || a == k_bottom_center ||
				   a == k_bottom_right) {
			y += res.y();
		}
		if (a == k_top_left || a == k_middle_left || a == k_bottom_left) {
			// Do nothing
		} else if (a == k_top_center || a == k_middle_center ||
				   a == k_bottom_center) {
			x += res.x() / 2;
		} else if (a == k_top_right || a == k_middle_right || a == k_bottom_right) {
			x += res.x();
		}

		gizmo_->set_point(PointF(x, y));
	}
}

void TileDistortNode::gizmo_drag_move(double x, double y, int modifiers)
{
	NodeInputDragger &x_drag = gizmo_->get_draggers()[0];
	NodeInputDragger &y_drag = gizmo_->get_draggers()[1];

	x_drag.drag(x_drag.get_start_value().to_double() + x);
	y_drag.drag(y_drag.get_start_value().to_double() + y);
}

}
