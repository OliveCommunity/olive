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

#include "cornerpindistortnode.h"

#include <cassert>

#include "common/lerp.h"

namespace olive
{

const std::string CornerPinDistortNode::k_texture_input = "tex_in";
const std::string CornerPinDistortNode::k_top_left_input = "top_left_in";
const std::string CornerPinDistortNode::k_top_right_input = "top_right_in";
const std::string CornerPinDistortNode::k_bottom_right_input =
	"bottom_right_in";
const std::string CornerPinDistortNode::k_bottom_left_input = "bottom_left_in";
const std::string CornerPinDistortNode::k_perspective_input = "perspective_in";

#define super Node

CornerPinDistortNode::CornerPinDistortNode()
{
	add_input(k_texture_input, NodeValue::k_texture,
			 InputFlags(k_input_flag_not_keyframable));
	add_input(k_perspective_input, NodeValue::k_boolean, true);
	add_input(k_top_left_input, NodeValue::k_vec2, Vector2D(0.0, 0.0));
	add_input(k_top_right_input, NodeValue::k_vec2, Vector2D(0.0, 0.0));
	add_input(k_bottom_right_input, NodeValue::k_vec2, Vector2D(0.0, 0.0));
	add_input(k_bottom_left_input, NodeValue::k_vec2, Vector2D(0.0, 0.0));

	// Initiate gizmos
	gizmo_whole_rect_ = add_draggable_gizmo<PolygonGizmo>();
	gizmo_resize_handle_[0] = add_draggable_gizmo<PointGizmo>(
		{ NodeKeyframeTrackReference(NodeInput(this, k_top_left_input), 0),
		  NodeKeyframeTrackReference(NodeInput(this, k_top_left_input), 1) });
	gizmo_resize_handle_[1] = add_draggable_gizmo<PointGizmo>(
		{ NodeKeyframeTrackReference(NodeInput(this, k_top_right_input), 0),
		  NodeKeyframeTrackReference(NodeInput(this, k_top_right_input), 1) });
	gizmo_resize_handle_[2] = add_draggable_gizmo<PointGizmo>(
		{ NodeKeyframeTrackReference(NodeInput(this, k_bottom_right_input), 0),
		  NodeKeyframeTrackReference(NodeInput(this, k_bottom_right_input), 1) });
	gizmo_resize_handle_[3] = add_draggable_gizmo<PointGizmo>(
		{ NodeKeyframeTrackReference(NodeInput(this, k_bottom_left_input), 0),
		  NodeKeyframeTrackReference(NodeInput(this, k_bottom_left_input), 1) });

	set_flag(k_video_effect);
	set_effect_input(k_texture_input);
}

void CornerPinDistortNode::retranslate()
{
	super::retranslate();

	set_input_name(k_texture_input, "Texture");
	set_input_name(k_perspective_input, "Perspective");
	set_input_name(k_top_left_input, "Top Left");
	set_input_name(k_top_right_input, "Top Right");
	set_input_name(k_bottom_right_input, "Bottom Right");
	set_input_name(k_bottom_left_input, "Bottom Left");
}

void CornerPinDistortNode::value(const NodeValueRow &value,
								 const NodeGlobals &globals,
								 NodeValueTable *table) const
{
	// If no texture do nothing
	if (TexturePtr tex = value.at(k_texture_input).to_texture()) {
		// In the special case that all sliders are in their default position just
		// push the texture.
		if (!(value.at(k_top_left_input).to_vec2().is_null() &&
			  value.at(k_top_right_input).to_vec2().is_null() &&
			  value.at(k_bottom_right_input).to_vec2().is_null() &&
			  value.at(k_bottom_left_input).to_vec2().is_null())) {
			ShaderJob job(value);
			job.insert("resolution_in",
					   NodeValue(NodeValue::k_vec2, tex->virtual_resolution(),
								 this));

			// Convert slider values to their pixel values and then convert to clip space (-1.0 ... 1.0) for overriding the
			// vertex coordinates.
			const Vector2D &resolution = tex->virtual_resolution();
			Vector2D half_resolution = resolution * 0.5;
			PointF top_left_pt = value_to_pixel(0, value, resolution);
			Vector2D top_left = Vector2D(top_left_pt.x(), top_left_pt.y()) /
									half_resolution -
								Vector2D(1.0, 1.0);
			PointF top_right_pt = value_to_pixel(1, value, resolution);
			Vector2D top_right =
				Vector2D(top_right_pt.x(), top_right_pt.y()) /
					half_resolution -
				Vector2D(1.0, 1.0);
			PointF bottom_right_pt = value_to_pixel(2, value, resolution);
			Vector2D bottom_right =
				Vector2D(bottom_right_pt.x(), bottom_right_pt.y()) /
					half_resolution -
				Vector2D(1.0, 1.0);
			PointF bottom_left_pt = value_to_pixel(3, value, resolution);
			Vector2D bottom_left =
				Vector2D(bottom_left_pt.x(), bottom_left_pt.y()) /
					half_resolution -
				Vector2D(1.0, 1.0);

			// Override default vertex coordinates.
			std::vector<float> adjusted_vertices = {
				top_left.x(),	  top_left.y(),		0.0f,
				top_right.x(),	  top_right.y(),	0.0f,
				bottom_right.x(), bottom_right.y(), 0.0f,

				top_left.x(),	  top_left.y(),		0.0f,
				bottom_left.x(),  bottom_left.y(),	0.0f,
				bottom_right.x(), bottom_right.y(), 0.0f
			};
			job.set_vertex_coordinates(adjusted_vertices);

			table->push(NodeValue::k_texture, tex->to_job(job), this);
		} else {
			table->push(value.at(k_texture_input));
		}
	}
}

ShaderCode
CornerPinDistortNode::get_shader_code(const ShaderRequest &request) const
{
	(void) request;

	return ShaderCode(FileFunctions::read_file_as_string(
						  ":/shaders/cornerpin.frag"),
					  FileFunctions::read_file_as_string(
						  ":/shaders/cornerpin.vert"));
}

PointF CornerPinDistortNode::value_to_pixel(int value, const NodeValueRow &row,
										   const Vector2D &resolution) const
{
	assert(value >= 0 && value <= 3);

	Vector2D v;

	switch (value) {
	case 0: // Top left
		v = row.at(k_top_left_input).to_vec2();
		return PointF(v.x(), v.y());
	case 1: // Top right
		v = row.at(k_top_right_input).to_vec2();
		return PointF(resolution.x() + v.x(), v.y());
	case 2: // Bottom right
		v = row.at(k_bottom_right_input).to_vec2();
		return PointF(resolution.x() + v.x(), resolution.y() + v.y());
	case 3: //Bottom left
		v = row.at(k_bottom_left_input).to_vec2();
		return PointF(v.x(), v.y() + resolution.y());
	default: // We should never get here
		return PointF();
	}
}

void CornerPinDistortNode::gizmo_drag_move(double x, double y, int modifiers)
{
	DraggableGizmo *gizmo = static_cast<DraggableGizmo *>(current_gizmo());

	if (gizmo != gizmo_whole_rect_) {
		gizmo->get_draggers()[0].drag(
			gizmo->get_draggers()[0].get_start_value().to_double() + x);
		gizmo->get_draggers()[1].drag(
			gizmo->get_draggers()[1].get_start_value().to_double() + y);
	}
}

void CornerPinDistortNode::update_gizmo_positions(const NodeValueRow &row,
												const NodeGlobals &globals)
{
	if (TexturePtr tex = row.at(k_texture_input).to_texture()) {
		const Vector2D &resolution = tex->virtual_resolution();

		PointF top_left = value_to_pixel(0, row, resolution);
		PointF top_right = value_to_pixel(1, row, resolution);
		PointF bottom_right = value_to_pixel(2, row, resolution);
		PointF bottom_left = value_to_pixel(3, row, resolution);

		// Add the correct offset to each slider
		set_input_property(k_top_left_input, "offset",
						 Vector2D(0.0, 0.0));
		set_input_property(k_top_right_input, "offset",
						 Vector2D(resolution.x(), 0.0));
		set_input_property(k_bottom_right_input, "offset",
						 resolution);
		set_input_property(k_bottom_left_input, "offset",
						 Vector2D(0.0, resolution.y()));

		// Draw bounding box
		gizmo_whole_rect_->set_polygon(
			{ top_left, top_right, bottom_right, bottom_left, top_left });

		// Create handles
		gizmo_resize_handle_[0]->set_point(top_left);
		gizmo_resize_handle_[1]->set_point(top_right);
		gizmo_resize_handle_[2]->set_point(bottom_right);
		gizmo_resize_handle_[3]->set_point(bottom_left);
	}
}

}
