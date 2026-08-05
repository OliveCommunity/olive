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

#include "shapenodebase.h"

#include <cmath>

#include "nodeundo.h"

namespace olive
{

#define super GeneratorWithMerge

// Qt::AltModifier / Qt::ShiftModifier flag values (modifiers arrive as an int)
static const int k_alt_modifier = 0x08000000;
static const int k_shift_modifier = 0x02000000;

const std::string ShapeNodeBase::k_position_input = "pos_in";
const std::string ShapeNodeBase::k_size_input = "size_in";
const std::string ShapeNodeBase::k_color_input = "color_in";

ShapeNodeBase::ShapeNodeBase(bool create_color_input)
{
	add_input(k_position_input, NodeValue::k_vec2, Vector2D(0, 0));
	add_input(k_size_input, NodeValue::k_vec2, Vector2D(100, 100));
	set_input_property(k_size_input, "min", Vector2D(0, 0));

	if (create_color_input) {
		add_input(k_color_input, NodeValue::k_color,
				 Variant::from_value(Color(1.0, 0.0, 0.0, 1.0)));
	}

	// Initiate gizmos
	std::vector<NodeKeyframeTrackReference> pos_n_sz = {
		NodeKeyframeTrackReference(NodeInput(this, k_position_input), 0),
		NodeKeyframeTrackReference(NodeInput(this, k_position_input), 1),
		NodeKeyframeTrackReference(NodeInput(this, k_size_input), 0),
		NodeKeyframeTrackReference(NodeInput(this, k_size_input), 1)
	};
	poly_gizmo_ = add_draggable_gizmo<PolygonGizmo>({
		NodeKeyframeTrackReference(NodeInput(this, k_position_input), 0),
		NodeKeyframeTrackReference(NodeInput(this, k_position_input), 1),
	});
	for (int i = 0; i < k_gizmo_scale_count; i++) {
		point_gizmo_[i] =
			add_draggable_gizmo<PointGizmo>(pos_n_sz, PointGizmo::k_absolute);
	}
}

void ShapeNodeBase::retranslate()
{
	super::retranslate();

	set_input_name(k_position_input, "Position");
	set_input_name(k_size_input, "Size");

	if (has_input_with_id(k_color_input)) {
		set_input_name(k_color_input, "Color");
	}
}

void ShapeNodeBase::update_gizmo_positions(const NodeValueRow &row,
										 const NodeGlobals &globals)
{
	// Use offsets to make the appearance of values that start in the top left, even though we
	// really anchor around the center
	Vector2D center_pt = globals.square_resolution() * 0.5f;
	set_input_property(k_position_input, "offset", center_pt);

	Vector2D pos = row.at(k_position_input).to_vec2();
	Vector2D sz = row.at(k_size_input).to_vec2();
	Vector2D half_sz = sz * 0.5f;

	double left_pt = pos.x() + center_pt.x() - half_sz.x();
	double top_pt = pos.y() + center_pt.y() - half_sz.y();
	double right_pt = left_pt + sz.x();
	double bottom_pt = top_pt + sz.y();
	double center_x_pt = (left_pt + right_pt) * 0.5;
	double center_y_pt = (top_pt + bottom_pt) * 0.5;

	point_gizmo_[k_gizmo_scale_top_left]->set_point(PointF(left_pt, top_pt));
	point_gizmo_[k_gizmo_scale_top_center]->set_point(PointF(center_x_pt, top_pt));
	point_gizmo_[k_gizmo_scale_top_right]->set_point(PointF(right_pt, top_pt));
	point_gizmo_[k_gizmo_scale_bottom_left]->set_point(PointF(left_pt, bottom_pt));
	point_gizmo_[k_gizmo_scale_bottom_center]->set_point(
		PointF(center_x_pt, bottom_pt));
	point_gizmo_[k_gizmo_scale_bottom_right]->set_point(
		PointF(right_pt, bottom_pt));
	point_gizmo_[k_gizmo_scale_center_left]->set_point(
		PointF(left_pt, center_y_pt));
	point_gizmo_[k_gizmo_scale_center_right]->set_point(
		PointF(right_pt, center_y_pt));

	// QPolygonF is now std::vector<PointF>; the corners below mirror the
	// QPolygonF(QRectF) constructor order (top left, top right, bottom right,
	// bottom left)
	poly_gizmo_->set_polygon({ PointF(left_pt, top_pt), PointF(right_pt, top_pt),
						   PointF(right_pt, bottom_pt),
						   PointF(left_pt, bottom_pt) });
}

void ShapeNodeBase::set_rect(RectF rect, const VideoParams &sequence_res,
							MultiUndoCommand *command)
{
	// Normalize around center of sequence (RectF is a plain data carrier now,
	// so the two translate() calls are spelled out)
	rect = RectF(rect.x() - sequence_res.width() * 0.5,
				 rect.y() - sequence_res.height() * 0.5, rect.width(),
				 rect.height());
	rect = RectF(rect.x() + rect.width() * 0.5,
				 rect.y() + rect.height() * 0.5, rect.width(), rect.height());

	NodeInput pos(this, ShapeNodeBase::k_position_input);
	NodeInput sz(this, ShapeNodeBase::k_size_input);

	command->add_child(new NodeParamSetStandardValueCommand(
		NodeKeyframeTrackReference(sz, 0), rect.width()));
	command->add_child(new NodeParamSetStandardValueCommand(
		NodeKeyframeTrackReference(sz, 1), rect.height()));
	command->add_child(new NodeParamSetStandardValueCommand(
		NodeKeyframeTrackReference(pos, 0), rect.x()));
	command->add_child(new NodeParamSetStandardValueCommand(
		NodeKeyframeTrackReference(pos, 1), rect.y()));
}

void ShapeNodeBase::gizmo_drag_move(double x, double y, int modifiers)
{
	DraggableGizmo *gizmo = static_cast<DraggableGizmo *>(current_gizmo());

	NodeInputDragger &x_drag = gizmo->get_draggers()[0];
	NodeInputDragger &y_drag = gizmo->get_draggers()[1];

	if (gizmo == poly_gizmo_) {
		x_drag.drag(x_drag.get_start_value().to_double() + x);
		y_drag.drag(y_drag.get_start_value().to_double() + y);
	} else {
		bool from_center = modifiers & k_alt_modifier;
		bool keep_ratio = modifiers & k_shift_modifier;

		NodeInputDragger &w_drag = gizmo->get_draggers()[2];
		NodeInputDragger &h_drag = gizmo->get_draggers()[3];

		Vector2D gizmo_sz_start(w_drag.get_start_value().to_double(),
							 h_drag.get_start_value().to_double());
		Vector2D gizmo_pos_start(x_drag.get_start_value().to_double(),
							  y_drag.get_start_value().to_double());
		Vector2D gizmo_half_res = gizmo->get_globals().square_resolution() / 2;
		Vector2D adjusted_pt(x, y);
		Vector2D new_size;
		Vector2D new_pos;
		Vector2D anchor;
		static const int k_xy_count = 2;
		bool negative[k_xy_count] = { false };

		double original_ratio;
		if (keep_ratio) {
			original_ratio = w_drag.get_start_value().to_double() /
							 h_drag.get_start_value().to_double();
		}

		// Calculate new size
		if (from_center) {
			// Calculate new size by using distance from center and doubling it
			new_size = (adjusted_pt - gizmo_half_res - gizmo_pos_start) * 2;

			if (is_gizmo_top(gizmo)) {
				new_size.set_y(-new_size.y());
			}

			if (is_gizmo_left(gizmo)) {
				new_size.set_x(-new_size.x());
			}
		} else {
			// Calculate new size by using distance from "anchor" - i.e. the opposite point of the shape
			// from the gizmo being dragged
			adjusted_pt -= gizmo_half_res;

			anchor = generate_gizmo_anchor(gizmo_pos_start, gizmo_sz_start, gizmo,
										 &adjusted_pt) +
					 gizmo_half_res;

			adjusted_pt += gizmo_half_res;

			// Calculate size and position
			new_size = adjusted_pt - anchor;

			// Abs size so neither coord is negative (component-wise, formerly
			// a loop over QVector2D::operator[])
			if (new_size.x() < 0) {
				negative[0] = true;
				new_size.set_x(-new_size.x());
			}
			if (new_size.y() < 0) {
				negative[1] = true;
				new_size.set_y(-new_size.y());
			}
		}

		// Restrict sizes by constraints
		if (is_gizmo_vertical_center(gizmo)) {
			if (keep_ratio) {
				// Calculate width from new height
				new_size.set_x(new_size.y() * original_ratio);
			} else {
				// Constrain to original width
				new_size.set_x(gizmo_sz_start.x());
			}
		}

		if (is_gizmo_horizontal_center(gizmo)) {
			if (keep_ratio) {
				// Calculate height from new width
				new_size.set_y(new_size.x() * original_ratio);
			} else {
				// Constrain to original height
				new_size.set_y(gizmo_sz_start.y());
			}
		}

		if (is_gizmo_corner(gizmo)) {
			if (keep_ratio) {
				float hypot = std::hypot(new_size.x(), new_size.y());

				float original_angle =
					std::atan2(gizmo_sz_start.x(), gizmo_sz_start.y());

				// Calculate new size based on original angle and hypotenuse
				new_size.set_x(std::sin(original_angle) * hypot);
				new_size.set_y(std::cos(original_angle) * hypot);
			}
		}

		// Calculate position
		if (from_center) {
			new_pos = gizmo_pos_start;
		} else {
			Vector2D using_size = new_size;

			// Un-abs size (component-wise, see above)
			if (negative[0]) {
				using_size.set_x(-using_size.x());
			}
			if (negative[1]) {
				using_size.set_y(-using_size.y());
			}

			// I'm pretty sure there's an algorithmic way of doing this, but I'm tired and this works
			if (is_gizmo_horizontal_center(gizmo)) {
				using_size.set_y(0);
			}

			if (is_gizmo_vertical_center(gizmo)) {
				using_size.set_x(0);
			}

			new_pos =
				generate_gizmo_anchor(gizmo_pos_start, gizmo_sz_start, gizmo) +
				using_size / 2;
		}

		x_drag.drag(new_pos.x());
		y_drag.drag(new_pos.y());
		w_drag.drag(new_size.x());
		h_drag.drag(new_size.y());
	}
}

Vector2D ShapeNodeBase::generate_gizmo_anchor(const Vector2D &pos,
											 const Vector2D &size,
											 NodeGizmo *gizmo,
											 Vector2D *pt) const
{
	Vector2D anchor = pos;
	Vector2D half_sz = size / 2;

	if (is_gizmo_left(gizmo)) {
		anchor.set_x(anchor.x() + half_sz.x());
		if (pt && pt->x() > anchor.x()) {
			pt->set_x(anchor.x());
		}
	}

	if (is_gizmo_right(gizmo)) {
		anchor.set_x(anchor.x() - half_sz.x());
		if (pt && pt->x() < anchor.x()) {
			pt->set_x(anchor.x());
		}
	}

	if (is_gizmo_top(gizmo)) {
		anchor.set_y(anchor.y() + half_sz.y());
		if (pt && pt->y() > anchor.y()) {
			pt->set_y(anchor.y());
		}
	}

	if (is_gizmo_bottom(gizmo)) {
		anchor.set_y(anchor.y() - half_sz.y());
		if (pt && pt->y() < anchor.y()) {
			pt->set_y(anchor.y());
		}
	}

	return anchor;
}

bool ShapeNodeBase::is_gizmo_top(NodeGizmo *g) const
{
	return g == point_gizmo_[k_gizmo_scale_top_center] ||
		   g == point_gizmo_[k_gizmo_scale_top_left] ||
		   g == point_gizmo_[k_gizmo_scale_top_right];
}

bool ShapeNodeBase::is_gizmo_bottom(NodeGizmo *g) const
{
	return g == point_gizmo_[k_gizmo_scale_bottom_center] ||
		   g == point_gizmo_[k_gizmo_scale_bottom_left] ||
		   g == point_gizmo_[k_gizmo_scale_bottom_right];
}

bool ShapeNodeBase::is_gizmo_left(NodeGizmo *g) const
{
	return g == point_gizmo_[k_gizmo_scale_top_left] ||
		   g == point_gizmo_[k_gizmo_scale_center_left] ||
		   g == point_gizmo_[k_gizmo_scale_bottom_left];
}

bool ShapeNodeBase::is_gizmo_right(NodeGizmo *g) const
{
	return g == point_gizmo_[k_gizmo_scale_top_right] ||
		   g == point_gizmo_[k_gizmo_scale_center_right] ||
		   g == point_gizmo_[k_gizmo_scale_bottom_right];
}

bool ShapeNodeBase::is_gizmo_horizontal_center(NodeGizmo *g) const
{
	return g == point_gizmo_[k_gizmo_scale_center_left] ||
		   g == point_gizmo_[k_gizmo_scale_center_right];
}

bool ShapeNodeBase::is_gizmo_vertical_center(NodeGizmo *g) const
{
	return g == point_gizmo_[k_gizmo_scale_top_center] ||
		   g == point_gizmo_[k_gizmo_scale_bottom_center];
}

bool ShapeNodeBase::is_gizmo_corner(NodeGizmo *g) const
{
	return g == point_gizmo_[k_gizmo_scale_top_left] ||
		   g == point_gizmo_[k_gizmo_scale_top_right] ||
		   g == point_gizmo_[k_gizmo_scale_bottom_right] ||
		   g == point_gizmo_[k_gizmo_scale_bottom_left];
}

}
