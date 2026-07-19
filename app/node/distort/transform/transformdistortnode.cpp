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

#include "transformdistortnode.h"

#include <QGuiApplication>

namespace olive
{

const QString TransformDistortNode::k_parent_input = QStringLiteral("parent_in");
const QString TransformDistortNode::k_texture_input = QStringLiteral("tex_in");
const QString TransformDistortNode::k_autoscale_input =
	QStringLiteral("autoscale_in");
const QString TransformDistortNode::k_interpolation_input =
	QStringLiteral("interpolation_in");

#define super MatrixGenerator

TransformDistortNode::TransformDistortNode()
{
	add_input(k_parent_input, NodeValue::k_matrix);

	add_input(k_autoscale_input, NodeValue::k_combo, 0);

	add_input(k_interpolation_input, NodeValue::k_combo, 2);

	prepend_input(k_texture_input, NodeValue::k_texture,
				 InputFlags(k_input_flag_not_keyframable));

	// Initiate gizmos
	rotation_gizmo_ = add_draggable_gizmo<ScreenGizmo>();
	rotation_gizmo_->add_input(NodeInput(this, k_rotation_input));
	rotation_gizmo_->set_drag_value_behavior(ScreenGizmo::k_absolute);

	poly_gizmo_ = add_draggable_gizmo<PolygonGizmo>();
	poly_gizmo_->add_input(
		NodeKeyframeTrackReference(NodeInput(this, k_position_input), 0));
	poly_gizmo_->add_input(
		NodeKeyframeTrackReference(NodeInput(this, k_position_input), 1));

	anchor_gizmo_ = add_draggable_gizmo<PointGizmo>();
	anchor_gizmo_->set_shape(PointGizmo::k_anchor_point);
	anchor_gizmo_->add_input(
		NodeKeyframeTrackReference(NodeInput(this, k_anchor_input), 0));
	anchor_gizmo_->add_input(
		NodeKeyframeTrackReference(NodeInput(this, k_anchor_input), 1));
	anchor_gizmo_->add_input(
		NodeKeyframeTrackReference(NodeInput(this, k_position_input), 0));
	anchor_gizmo_->add_input(
		NodeKeyframeTrackReference(NodeInput(this, k_position_input), 1));

	for (int i = 0; i < k_gizmo_scale_count; i++) {
		point_gizmo_[i] = add_draggable_gizmo<PointGizmo>();
		point_gizmo_[i]->add_input(
			NodeKeyframeTrackReference(NodeInput(this, k_scale_input), 0));
		point_gizmo_[i]->add_input(
			NodeKeyframeTrackReference(NodeInput(this, k_scale_input), 1));
		point_gizmo_[i]->set_drag_value_behavior(PointGizmo::k_absolute);
	}

	set_flag(k_video_effect);
	set_effect_input(k_texture_input);
}

void TransformDistortNode::retranslate()
{
	super::retranslate();

	set_input_name(k_parent_input, tr("Parent"));
	set_input_name(k_autoscale_input, tr("Auto-Scale"));
	set_input_name(k_texture_input, tr("Texture"));
	set_input_name(k_interpolation_input, tr("Interpolation"));

	set_combo_box_strings(k_autoscale_input,
					   { tr("None"), tr("Fit"), tr("Fill"), tr("Stretch") });
	set_combo_box_strings(k_interpolation_input,
					   { tr("Nearest Neighbor"), tr("Bilinear"),
						 tr("Mipmapped Bilinear") });
}

void TransformDistortNode::value(const NodeValueRow &value,
								 const NodeGlobals &globals,
								 NodeValueTable *table) const
{
	// Generate matrix
	QMatrix4x4 generated_matrix = generate_matrix(
		value, false, false, false, value[k_parent_input].to_matrix());

	// Pop texture
	NodeValue texture_meta = value[k_texture_input];

	TexturePtr job_to_push = nullptr;

	// If we have a texture, generate a matrix and make it happen
	if (TexturePtr texture = texture_meta.to_texture()) {
		// Adjust our matrix by the resolutions involved
		QMatrix4x4 real_matrix = generate_auto_scaled_matrix(
			generated_matrix, value, globals, texture->params());

		if (!real_matrix.isIdentity()) {
			// The matrix will transform things
			ShaderJob job;
			job.insert(QStringLiteral("ove_maintex"), texture_meta);
			job.insert(QStringLiteral("ove_mvpmat"),
					   NodeValue(NodeValue::k_matrix, real_matrix, this));
			job.set_interpolation(QStringLiteral("ove_maintex"),
								 static_cast<Texture::Interpolation>(
									 value[k_interpolation_input].to_int()));

			// Use global resolution rather than texture resolution because this may result in a size change
			job_to_push = Texture::job(globals.vparams(), job);
		}
	}

	table->push(NodeValue::k_matrix, QVariant::fromValue(generated_matrix),
				this);

	if (!job_to_push) {
		// Re-push whatever value we received
		table->push(texture_meta);
	} else {
		table->push(NodeValue::k_texture, job_to_push, this);
	}
}

ShaderCode
TransformDistortNode::get_shader_code(const ShaderRequest &request) const
{
	Q_UNUSED(request);

	// Returns default frag and vert shader
	return ShaderCode();
}

void TransformDistortNode::gizmo_drag_start(const NodeValueRow &row, double x,
										  double y, const Rational &time)
{
	DraggableGizmo *gizmo = static_cast<DraggableGizmo *>(sender());

	if (gizmo == anchor_gizmo_) {
		gizmo_inverted_transform_ =
			generate_matrix(row, true, true, false, row[k_parent_input].to_matrix())
				.toTransform()
				.inverted();

	} else if (is_a_scale_gizmo(gizmo)) {
		// Dragging scale handle
		TexturePtr tex = row[k_texture_input].to_texture();
		if (!tex) {
			return;
		}

		gizmo_scale_uniform_ = row[k_uniform_scale_input].to_bool();
		gizmo_anchor_pt_ = (row[k_anchor_input].to_vec2() +
							gizmo->get_globals().nonsquare_resolution() / 2)
							   .toPointF();

		if (gizmo == point_gizmo_[k_gizmo_scale_top_left] ||
			gizmo == point_gizmo_[k_gizmo_scale_top_right] ||
			gizmo == point_gizmo_[k_gizmo_scale_bottom_left] ||
			gizmo == point_gizmo_[k_gizmo_scale_bottom_right]) {
			gizmo_scale_axes_ = k_gizmo_scale_both;
		} else if (gizmo == point_gizmo_[k_gizmo_scale_center_left] ||
				   gizmo == point_gizmo_[k_gizmo_scale_center_right]) {
			gizmo_scale_axes_ = k_gizmo_scale_x_only;
		} else {
			gizmo_scale_axes_ = k_gizmo_scale_y_only;
		}

		// Store texture size
		VideoParams texture_params = tex->params();
		QVector2D texture_sz(texture_params.square_pixel_width(),
							 texture_params.height());
		gizmo_scale_anchor_ = row[k_anchor_input].to_vec2() + texture_sz / 2;

		if (gizmo == point_gizmo_[k_gizmo_scale_top_right] ||
			gizmo == point_gizmo_[k_gizmo_scale_bottom_right] ||
			gizmo == point_gizmo_[k_gizmo_scale_center_right]) {
			// Right handles, flip X axis
			gizmo_scale_anchor_.setX(texture_sz.x() - gizmo_scale_anchor_.x());
		}

		if (gizmo == point_gizmo_[k_gizmo_scale_bottom_left] ||
			gizmo == point_gizmo_[k_gizmo_scale_bottom_right] ||
			gizmo == point_gizmo_[k_gizmo_scale_bottom_center]) {
			// Bottom handles, flip Y axis
			gizmo_scale_anchor_.setY(texture_sz.y() - gizmo_scale_anchor_.y());
		}

		// Store current matrix
		gizmo_inverted_transform_ =
			generate_matrix(row, true, true, true, row[k_parent_input].to_matrix())
				.toTransform()
				.inverted();

	} else if (gizmo == rotation_gizmo_) {
		gizmo_anchor_pt_ = (row[k_anchor_input].to_vec2() +
							gizmo->get_globals().nonsquare_resolution() / 2)
							   .toPointF();
		gizmo_start_angle_ =
			std::atan2(y - gizmo_anchor_pt_.y(), x - gizmo_anchor_pt_.x());
		gizmo_last_angle_ = gizmo_start_angle_;
		gizmo_last_alt_angle_ =
			std::atan2(x - gizmo_anchor_pt_.x(), y - gizmo_anchor_pt_.y());
		gizmo_rotate_wrap_ = 0;
		gizmo_rotate_last_dir_ = k_direction_none;
	}
}

void TransformDistortNode::gizmo_drag_move(double x, double y,
										 const Qt::KeyboardModifiers &modifiers)
{
	DraggableGizmo *gizmo = static_cast<DraggableGizmo *>(sender());

	if (gizmo == poly_gizmo_) {
		NodeInputDragger &x_drag = gizmo->get_draggers()[0];
		NodeInputDragger &y_drag = gizmo->get_draggers()[1];

		x_drag.drag(x_drag.get_start_value().toDouble() + x);
		y_drag.drag(y_drag.get_start_value().toDouble() + y);

	} else if (gizmo == anchor_gizmo_) {
		NodeInputDragger &x_anchor_drag = gizmo->get_draggers()[0];
		NodeInputDragger &y_anchor_drag = gizmo->get_draggers()[1];
		NodeInputDragger &x_pos_drag = gizmo->get_draggers()[2];
		NodeInputDragger &y_pos_drag = gizmo->get_draggers()[3];

		QPointF inverted_movement(gizmo_inverted_transform_.map(QPointF(x, y)));

		x_anchor_drag.drag(x_anchor_drag.get_start_value().toDouble() +
						   inverted_movement.x());
		y_anchor_drag.drag(y_anchor_drag.get_start_value().toDouble() +
						   inverted_movement.y());
		x_pos_drag.drag(x_pos_drag.get_start_value().toDouble() + x);
		y_pos_drag.drag(y_pos_drag.get_start_value().toDouble() + y);

	} else if (gizmo == rotation_gizmo_) {
		double raw_angle =
			std::atan2(y - gizmo_anchor_pt_.y(), x - gizmo_anchor_pt_.x());
		double alt_angle =
			std::atan2(x - gizmo_anchor_pt_.x(), y - gizmo_anchor_pt_.y());

		double current_angle = raw_angle;

		// Detect rotation wrap around
		RotationDirection this_dir =
			get_direction_from_angles(gizmo_last_angle_, raw_angle);
		RotationDirection alt_dir =
			get_direction_from_angles(gizmo_last_alt_angle_, alt_angle);

		if (gizmo_rotate_last_dir_ != k_direction_none &&
			this_dir != gizmo_rotate_last_dir_) {
			if (alt_dir == gizmo_rotate_last_alt_dir_) {
				if ((raw_angle - gizmo_last_angle_) < 0) {
					gizmo_rotate_wrap_++;
				} else {
					gizmo_rotate_wrap_--;
				}

				this_dir = gizmo_rotate_last_dir_;
				alt_dir = gizmo_rotate_last_alt_dir_;
			}
		}

		gizmo_rotate_last_dir_ = this_dir;
		gizmo_rotate_last_alt_dir_ = alt_dir;
		gizmo_last_angle_ = raw_angle;
		gizmo_last_alt_angle_ = alt_angle;

		current_angle += M_PI * 2 * gizmo_rotate_wrap_;

		// Convert radians to degrees
		double rotation_difference =
			(current_angle - gizmo_start_angle_) * 57.2958;

		NodeInputDragger &d = gizmo->get_draggers()[0];
		d.drag(d.get_start_value().toDouble() + rotation_difference);

	} else if (is_a_scale_gizmo(gizmo)) {
		QPointF mouse_relative =
			gizmo_inverted_transform_.map(QPointF(x, y) - gizmo_anchor_pt_);

		double x_scaled_movement =
			qAbs(mouse_relative.x() / gizmo_scale_anchor_.x());
		double y_scaled_movement =
			qAbs(mouse_relative.y() / gizmo_scale_anchor_.y());

		NodeInputDragger &x_drag = gizmo->get_draggers()[0];
		NodeInputDragger &y_drag = gizmo->get_draggers()[1];

		switch (gizmo_scale_axes_) {
		case k_gizmo_scale_x_only:
			x_drag.drag(x_scaled_movement);
			break;
		case k_gizmo_scale_y_only:
			if (gizmo_scale_uniform_) {
				x_drag.drag(y_scaled_movement);
			} else {
				y_drag.drag(y_scaled_movement);
			}
			break;
		case k_gizmo_scale_both:
			if (gizmo_scale_uniform_) {
				double distance =
					std::hypot(mouse_relative.x(), mouse_relative.y());
				double texture_diag = std::hypot(gizmo_scale_anchor_.x(),
												 gizmo_scale_anchor_.y());

				x_drag.drag(qAbs(distance / texture_diag));
			} else {
				x_drag.drag(x_scaled_movement);
				y_drag.drag(y_scaled_movement);
			}
			break;
		}
	}
}

QMatrix4x4 TransformDistortNode::adjust_matrix_by_resolutions(
	const QMatrix4x4 &mat, const QVector2D &sequence_res,
	const QVector2D &texture_res, const QVector2D &offset,
	AutoScaleType autoscale_type)
{
	// First, create an identity matrix
	QMatrix4x4 adjusted_matrix;

	// Scale it to a square based on the sequence's resolution
	adjusted_matrix.scale(2.0 / sequence_res.x(), 2.0 / sequence_res.y(), 1.0);

	// Apply offset if applicable
	adjusted_matrix.translate(offset.x(), offset.y());

	// Adjust by the matrix we generated earlier
	adjusted_matrix *= mat;

	// Scale back out to texture size (adjusted by pixel aspect)
	adjusted_matrix.scale(texture_res.x() * 0.5, texture_res.y() * 0.5, 1.0);

	// If auto-scale is enabled, fit the texture to the sequence (without cropping)
	if (autoscale_type != k_auto_scale_none) {
		if (autoscale_type == k_auto_scale_stretch) {
			adjusted_matrix.scale(sequence_res.x() / texture_res.x(),
								  sequence_res.y() / texture_res.y(), 1.0);
		} else {
			double footage_real_ar = texture_res.x() / texture_res.y();
			double sequence_real_ar = sequence_res.x() / sequence_res.y();

			double scale_by_x = sequence_res.x() / texture_res.x();
			double scale_by_y = sequence_res.y() / texture_res.y();
			double autoscale_val;

			if ((autoscale_type == k_auto_scale_fit) ==
				(sequence_real_ar > footage_real_ar)) {
				// Scale by height. Either the sequence is wider than the footage or we're using fill and
				// cutting off the sides
				autoscale_val = scale_by_y;
			} else {
				// Scale by width. Either the footage is wider than the sequence or we're using fill and
				// cutting off the top and bottom
				autoscale_val = scale_by_x;
			}

			adjusted_matrix.scale(autoscale_val, autoscale_val, 1.0);
		}
	}

	return adjusted_matrix;
}

void TransformDistortNode::update_gizmo_positions(const NodeValueRow &row,
												const NodeGlobals &globals)
{
	TexturePtr tex = row[k_texture_input].to_texture();
	if (!tex) {
		return;
	}

	// Get the sequence resolution
	const QVector2D &sequence_res = globals.nonsquare_resolution();
	QVector2D sequence_half_res = sequence_res * 0.5;
	QPointF sequence_half_res_pt = sequence_half_res.toPointF();

	// GizmoTraverser just returns the sizes of the textures and no other data
	VideoParams tex_params = tex->params();
	QVector2D tex_sz(tex_params.square_pixel_width(), tex_params.height());
	QVector2D tex_offset = tex_params.offset();

	// Retrieve autoscale value
	AutoScaleType autoscale =
		static_cast<AutoScaleType>(row[k_autoscale_input].to_int());

	// Fold values into a matrix for the rectangle
	QMatrix4x4 rectangle_matrix;
	rectangle_matrix.scale(sequence_half_res.x(), sequence_half_res.y());
	rectangle_matrix *= adjust_matrix_by_resolutions(
		generate_matrix(row, false, false, false, row[k_parent_input].to_matrix()),
		sequence_res, tex_sz, tex_offset, autoscale);

	// Create rect and transform it
	const QVector<QPointF> points = { QPointF(-1, -1), QPointF(1, -1),
									  QPointF(1, 1), QPointF(-1, 1),
									  QPointF(-1, -1) };
	QTransform rectangle_transform = rectangle_matrix.toTransform();
	QPolygonF r = rectangle_transform.map(points);
	r.translate(sequence_half_res_pt);
	poly_gizmo_->set_polygon(r);

	// Draw anchor point
	QMatrix4x4 anchor_matrix;
	anchor_matrix.scale(sequence_half_res.x(), sequence_half_res.y());
	anchor_matrix *= adjust_matrix_by_resolutions(
		generate_matrix(row, true, false, false, row[k_parent_input].to_matrix()),
		sequence_res, tex_sz, tex_offset, autoscale);
	anchor_gizmo_->set_point(anchor_matrix.toTransform().map(QPointF(0, 0)) +
							sequence_half_res_pt);

	// Draw scale handles
	point_gizmo_[k_gizmo_scale_top_left]->set_point(
		create_scale_point(-1, -1, sequence_half_res_pt, rectangle_matrix));
	point_gizmo_[k_gizmo_scale_top_center]->set_point(
		create_scale_point(0, -1, sequence_half_res_pt, rectangle_matrix));
	point_gizmo_[k_gizmo_scale_top_right]->set_point(
		create_scale_point(1, -1, sequence_half_res_pt, rectangle_matrix));
	point_gizmo_[k_gizmo_scale_bottom_left]->set_point(
		create_scale_point(-1, 1, sequence_half_res_pt, rectangle_matrix));
	point_gizmo_[k_gizmo_scale_bottom_center]->set_point(
		create_scale_point(0, 1, sequence_half_res_pt, rectangle_matrix));
	point_gizmo_[k_gizmo_scale_bottom_right]->set_point(
		create_scale_point(1, 1, sequence_half_res_pt, rectangle_matrix));
	point_gizmo_[k_gizmo_scale_center_left]->set_point(
		create_scale_point(-1, 0, sequence_half_res_pt, rectangle_matrix));
	point_gizmo_[k_gizmo_scale_center_right]->set_point(
		create_scale_point(1, 0, sequence_half_res_pt, rectangle_matrix));

	// Use offsets to make the appearance of values that start in the top left, even though we
	// really anchor around the center
	set_input_property(k_position_input, QStringLiteral("offset"),
					 sequence_half_res + tex_offset);
	set_input_property(k_anchor_input, QStringLiteral("offset"), tex_sz * 0.5);
}

QTransform
TransformDistortNode::gizmo_transformation(const NodeValueRow &row,
										  const NodeGlobals &globals) const
{
	if (TexturePtr texture = row[k_texture_input].to_texture()) {
		//auto m = GenerateMatrix(row, false, false, false, row[kParentInput].toMatrix());
		auto m = generate_matrix(row, false, false, false, QMatrix4x4());
		return generate_auto_scaled_matrix(m, row, globals, texture->params())
			.toTransform();
	}
	return super::gizmo_transformation(row, globals);
}

QPointF TransformDistortNode::create_scale_point(double x, double y,
											   const QPointF &half_res,
											   const QMatrix4x4 &mat)
{
	return mat.map(QPointF(x, y)) + half_res;
}

QMatrix4x4 TransformDistortNode::generate_auto_scaled_matrix(
	const QMatrix4x4 &generated_matrix, const NodeValueRow &value,
	const NodeGlobals &globals, const VideoParams &texture_params) const
{
	const QVector2D &sequence_res = globals.nonsquare_resolution();
	QVector2D texture_res(texture_params.square_pixel_width(),
						  texture_params.height());
	AutoScaleType autoscale =
		static_cast<AutoScaleType>(value[k_autoscale_input].to_int());

	return adjust_matrix_by_resolutions(generated_matrix, sequence_res,
									 texture_res, texture_params.offset(),
									 autoscale);
}

bool TransformDistortNode::is_a_scale_gizmo(NodeGizmo *g) const
{
	for (int i = 0; i < k_gizmo_scale_count; i++) {
		if (point_gizmo_[i] == g) {
			return true;
		}
	}

	return false;
}

TransformDistortNode::RotationDirection
TransformDistortNode::get_direction_from_angles(double last, double current)
{
	return (current > last) ? k_direction_positive : k_direction_negative;
}

}
