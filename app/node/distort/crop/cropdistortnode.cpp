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

#include "cropdistortnode.h"

#include "common/util.h"
#include "node/sliderdisplaytype.h"

namespace olive
{

const QString CropDistortNode::k_texture_input = QStringLiteral("tex_in");
const QString CropDistortNode::k_left_input = QStringLiteral("left_in");
const QString CropDistortNode::k_top_input = QStringLiteral("top_in");
const QString CropDistortNode::k_right_input = QStringLiteral("right_in");
const QString CropDistortNode::k_bottom_input = QStringLiteral("bottom_in");
const QString CropDistortNode::k_feather_input = QStringLiteral("feather_in");

#define super Node

CropDistortNode::CropDistortNode()
{
	add_input(k_texture_input, NodeValue::k_texture,
			 InputFlags(k_input_flag_not_keyframable));

	create_crop_side_input(k_left_input);
	create_crop_side_input(k_top_input);
	create_crop_side_input(k_right_input);
	create_crop_side_input(k_bottom_input);

	add_input(k_feather_input, NodeValue::k_float, 0.0);
	set_input_property(k_feather_input, QStringLiteral("min"), 0.0);

	// Initiate gizmos
	poly_gizmo_ = add_draggable_gizmo<PolygonGizmo>(
		{ k_left_input, k_top_input, k_right_input, k_bottom_input });

	point_gizmo_[k_gizmo_scale_top_left] =
		add_draggable_gizmo<PointGizmo>({ k_left_input, k_top_input });
	point_gizmo_[k_gizmo_scale_top_center] =
		add_draggable_gizmo<PointGizmo>({ k_top_input });
	point_gizmo_[k_gizmo_scale_top_right] =
		add_draggable_gizmo<PointGizmo>({ k_right_input, k_top_input });
	point_gizmo_[k_gizmo_scale_bottom_left] =
		add_draggable_gizmo<PointGizmo>({ k_left_input, k_bottom_input });
	point_gizmo_[k_gizmo_scale_bottom_center] =
		add_draggable_gizmo<PointGizmo>({ k_bottom_input });
	point_gizmo_[k_gizmo_scale_bottom_right] =
		add_draggable_gizmo<PointGizmo>({ k_right_input, k_bottom_input });
	point_gizmo_[k_gizmo_scale_center_left] =
		add_draggable_gizmo<PointGizmo>({ k_left_input });
	point_gizmo_[k_gizmo_scale_center_right] =
		add_draggable_gizmo<PointGizmo>({ k_right_input });

	set_flag(k_video_effect);
	set_effect_input(k_texture_input);
}

void CropDistortNode::retranslate()
{
	super::retranslate();

	set_input_name(k_texture_input, tr("Texture"));
	set_input_name(k_left_input, tr("Left"));
	set_input_name(k_top_input, tr("Top"));
	set_input_name(k_right_input, tr("Right"));
	set_input_name(k_bottom_input, tr("Bottom"));
	set_input_name(k_feather_input, tr("Feather"));
}

void CropDistortNode::value(const NodeValueRow &value,
							const NodeGlobals &globals,
							NodeValueTable *table) const
{
	ShaderJob job;
	job.insert(value);

	if (TexturePtr texture = job.get(k_texture_input).to_texture()) {
		job.insert(QStringLiteral("resolution_in"),
				   NodeValue(NodeValue::k_vec2,
							 QVector2D(texture->params().width(),
									   texture->params().height()),
							 this));

		if (!qIsNull(job.get(k_left_input).to_double()) ||
			!qIsNull(job.get(k_right_input).to_double()) ||
			!qIsNull(job.get(k_top_input).to_double()) ||
			!qIsNull(job.get(k_bottom_input).to_double())) {
			table->push(NodeValue::k_texture, texture->to_job(job), this);
		} else {
			table->push(job.get(k_texture_input));
		}
	}
}

ShaderCode CropDistortNode::get_shader_code(const ShaderRequest &request) const
{
	Q_UNUSED(request)
	return ShaderCode(
		FileFunctions::read_file_as_string(QStringLiteral(":/shaders/crop.frag")));
}

void CropDistortNode::update_gizmo_positions(const NodeValueRow &row,
										   const NodeGlobals &globals)
{
	if (TexturePtr tex = row[k_texture_input].to_texture()) {
		const QVector2D &resolution = tex->virtual_resolution();
		temp_resolution_ = resolution;

		double left_pt = resolution.x() * row[k_left_input].to_double();
		double top_pt = resolution.y() * row[k_top_input].to_double();
		double right_pt = resolution.x() * (1.0 - row[k_right_input].to_double());
		double bottom_pt =
			resolution.y() * (1.0 - row[k_bottom_input].to_double());
		double center_x_pt = mid(left_pt, right_pt);
		double center_y_pt = mid(top_pt, bottom_pt);

		point_gizmo_[k_gizmo_scale_top_left]->set_point(QPointF(left_pt, top_pt));
		point_gizmo_[k_gizmo_scale_top_center]->set_point(
			QPointF(center_x_pt, top_pt));
		point_gizmo_[k_gizmo_scale_top_right]->set_point(QPointF(right_pt, top_pt));
		point_gizmo_[k_gizmo_scale_bottom_left]->set_point(
			QPointF(left_pt, bottom_pt));
		point_gizmo_[k_gizmo_scale_bottom_center]->set_point(
			QPointF(center_x_pt, bottom_pt));
		point_gizmo_[k_gizmo_scale_bottom_right]->set_point(
			QPointF(right_pt, bottom_pt));
		point_gizmo_[k_gizmo_scale_center_left]->set_point(
			QPointF(left_pt, center_y_pt));
		point_gizmo_[k_gizmo_scale_center_right]->set_point(
			QPointF(right_pt, center_y_pt));

		poly_gizmo_->set_polygon(
			QRectF(left_pt, top_pt, right_pt - left_pt, bottom_pt - top_pt));
	}
}

void CropDistortNode::gizmo_drag_move(double x_diff, double y_diff,
									const Qt::KeyboardModifiers &modifiers)
{
	DraggableGizmo *gizmo = static_cast<DraggableGizmo *>(sender());

	QVector2D res = temp_resolution_;
	x_diff /= res.x();
	y_diff /= res.y();

	for (int j = 0; j < gizmo->get_draggers().size(); j++) {
		NodeInputDragger &i = gizmo->get_draggers()[j];
		double s = i.get_start_value().toDouble();
		if (i.get_input().input().input() == k_left_input) {
			i.drag(s + x_diff);
		} else if (i.get_input().input().input() == k_top_input) {
			i.drag(s + y_diff);
		} else if (i.get_input().input().input() == k_right_input) {
			i.drag(s - x_diff);
		} else if (i.get_input().input().input() == k_bottom_input) {
			i.drag(s - y_diff);
		}
	}
}

void CropDistortNode::create_crop_side_input(const QString &id)
{
	add_input(id, NodeValue::k_float, 0.0);
	set_input_property(id, QStringLiteral("min"), 0.0);
	set_input_property(id, QStringLiteral("max"), 1.0);
	set_input_property(id, QStringLiteral("view"), slider::k_percentage);
}

}
