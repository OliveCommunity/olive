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

#include "blur.h"

namespace olive
{

const QString BlurFilterNode::k_texture_input = QStringLiteral("tex_in");
const QString BlurFilterNode::k_method_input = QStringLiteral("method_in");
const QString BlurFilterNode::k_radius_input = QStringLiteral("radius_in");
const QString BlurFilterNode::k_horiz_input = QStringLiteral("horiz_in");
const QString BlurFilterNode::k_vert_input = QStringLiteral("vert_in");
const QString BlurFilterNode::k_repeat_edge_pixels_input =
	QStringLiteral("repeat_edge_pixels_in");

const QString BlurFilterNode::k_directional_degrees_input =
	QStringLiteral("directional_degrees_in");

const QString BlurFilterNode::k_radial_center_input =
	QStringLiteral("radial_center_in");

#define super Node

BlurFilterNode::BlurFilterNode()
{
	add_input(k_texture_input, NodeValue::k_texture,
			 InputFlags(k_input_flag_not_keyframable));

	Method default_method = k_gaussian;

	add_input(k_method_input, NodeValue::k_combo, default_method,
			 InputFlags(k_input_flag_not_keyframable | k_input_flag_not_connectable));

	add_input(k_radius_input, NodeValue::k_float, 10.0);
	set_input_property(k_radius_input, QStringLiteral("min"), 0.0);

	{
		// Box and gaussian only
		add_input(k_horiz_input, NodeValue::k_boolean, true);
		add_input(k_vert_input, NodeValue::k_boolean, true);
	}

	{
		// Directional only
		add_input(k_directional_degrees_input, NodeValue::k_float, 0.0);
	}

	{
		// Radial only
		add_input(k_radial_center_input, NodeValue::k_vec2, QVector2D(0, 0));
	}

	update_inputs(default_method);

	add_input(k_repeat_edge_pixels_input, NodeValue::k_boolean, true);

	set_flag(k_video_effect);
	set_effect_input(k_texture_input);

	radial_center_gizmo_ = add_draggable_gizmo<PointGizmo>();
	radial_center_gizmo_->set_shape(PointGizmo::k_anchor_point);
	radial_center_gizmo_->add_input(
		NodeKeyframeTrackReference(NodeInput(this, k_radial_center_input), 0));
	radial_center_gizmo_->add_input(
		NodeKeyframeTrackReference(NodeInput(this, k_radial_center_input), 1));
}

QString BlurFilterNode::name() const
{
	return tr("Blur");
}

QString BlurFilterNode::id() const
{
	return QStringLiteral("org.olivevideoeditor.Olive.blur");
}

QVector<Node::CategoryID> BlurFilterNode::category() const
{
	return { k_category_filter };
}

QString BlurFilterNode::description() const
{
	return tr("Blurs an image.");
}

void BlurFilterNode::retranslate()
{
	super::retranslate();

	set_input_name(k_texture_input, tr("Input"));
	set_input_name(k_method_input, tr("Method"));
	set_combo_box_strings(k_method_input, { tr("Box"), tr("Gaussian"),
									   tr("Directional"), tr("Radial") });
	set_input_name(k_radius_input, tr("Radius"));
	set_input_name(k_horiz_input, tr("Horizontal"));
	set_input_name(k_vert_input, tr("Vertical"));
	set_input_name(k_repeat_edge_pixels_input, tr("Repeat Edge Pixels"));

	set_input_name(k_directional_degrees_input, tr("Direction"));
	set_input_name(k_radial_center_input, tr("Center"));
}

ShaderCode BlurFilterNode::get_shader_code(const ShaderRequest &request) const
{
	Q_UNUSED(request)
	return ShaderCode(FileFunctions::read_file_as_string(":/shaders/blur.frag"));
}

void BlurFilterNode::value(const NodeValueRow &value,
						   const NodeGlobals &globals,
						   NodeValueTable *table) const
{
	// If there's no texture, no need to run an operation
	if (TexturePtr tex = value[k_texture_input].to_texture()) {
		Method method = static_cast<Method>(value[k_method_input].to_int());

		bool can_push_job = true;
		int iterations = 1;

		// Check if radius is > 0
		if (value[k_radius_input].to_double() > 0.0) {
			// Method-specific considerations
			switch (method) {
			case k_box:
			case k_gaussian: {
				bool horiz = value[k_horiz_input].to_bool();
				bool vert = value[k_vert_input].to_bool();

				if (!horiz && !vert) {
					// Disable job if horiz and vert are unchecked
					can_push_job = false;
				} else if (horiz && vert) {
					// Set iteration count to 2 if we're blurring both horizontally and vertically
					iterations = 2;
				}
				break;
			}
			case k_directional:
			case k_radial:
				break;
			}
		} else {
			can_push_job = false;
		}

		if (can_push_job) {
			ShaderJob job(value);
			job.insert(QStringLiteral("resolution_in"),
					   NodeValue(NodeValue::k_vec2, tex->virtual_resolution(),
								 this));
			job.set_iterations(iterations, k_texture_input);
			table->push(NodeValue::k_texture, tex->to_job(job), this);
		} else {
			// If we're not performing the blur job, just push the texture
			table->push(value[k_texture_input]);
		}
	}
}

void BlurFilterNode::update_gizmo_positions(const NodeValueRow &row,
										  const NodeGlobals &globals)
{
	if (TexturePtr tex = row[k_texture_input].to_texture()) {
		if (row[k_method_input].to_int() == k_radial) {
			const QVector2D &sequence_res = tex->virtual_resolution();
			QVector2D sequence_half_res = sequence_res * 0.5;

			radial_center_gizmo_->set_visible(true);
			radial_center_gizmo_->set_point(
				sequence_half_res.toPointF() +
				row[k_radial_center_input].to_vec2().toPointF());

			set_input_property(k_radial_center_input, QStringLiteral("offset"),
							 sequence_half_res);
		} else {
			radial_center_gizmo_->set_visible(false);
		}
	}
}

void BlurFilterNode::gizmo_drag_move(double x, double y,
								   const Qt::KeyboardModifiers &modifiers)
{
	DraggableGizmo *gizmo = static_cast<DraggableGizmo *>(sender());

	if (gizmo == radial_center_gizmo_) {
		NodeInputDragger &x_drag = gizmo->get_draggers()[0];
		NodeInputDragger &y_drag = gizmo->get_draggers()[1];

		x_drag.drag(x_drag.get_start_value().toDouble() + x);
		y_drag.drag(y_drag.get_start_value().toDouble() + y);
	}
}

void BlurFilterNode::InputValueChangedEvent(const QString &input, int element)
{
	if (input == k_method_input) {
		update_inputs(get_method());
	}

	super::InputValueChangedEvent(input, element);
}

void BlurFilterNode::update_inputs(Method method)
{
	set_input_flag(k_horiz_input, k_input_flag_hidden,
				 !(method == k_box || method == k_gaussian));
	set_input_flag(k_vert_input, k_input_flag_hidden,
				 !(method == k_box || method == k_gaussian));
	set_input_flag(k_directional_degrees_input, k_input_flag_hidden,
				 !(method == k_directional));
	set_input_flag(k_radial_center_input, k_input_flag_hidden, !(method == k_radial));
}

}
