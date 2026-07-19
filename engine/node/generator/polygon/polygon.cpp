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

#include "polygon.h"

#include <QGuiApplication>
#include <QVector2D>

namespace olive
{

const QString PolygonGenerator::k_points_input = QStringLiteral("points_in");
const QString PolygonGenerator::k_color_input = QStringLiteral("color_in");

#define super GeneratorWithMerge

PolygonGenerator::PolygonGenerator()
{
	add_input(k_points_input, NodeValue::k_bezier, QVector2D(0, 0),
			 InputFlags(k_input_flag_array));

	add_input(k_color_input, NodeValue::k_color,
			 QVariant::fromValue(Color(1.0, 1.0, 1.0)));

	const int k_middle_x = 135;
	const int k_middle_y = 45;
	const int k_bottom_x = 90;
	const int k_bottom_y = 120;
	const int k_top_y = 135;

	// The Default Pentagon(tm)
	input_array_resize(k_points_input, 5);
	set_split_standard_value_on_track(k_points_input, 0, 0, 0);
	set_split_standard_value_on_track(k_points_input, 1, -k_top_y, 0);
	set_split_standard_value_on_track(k_points_input, 0, k_middle_x, 1);
	set_split_standard_value_on_track(k_points_input, 1, -k_middle_y, 1);
	set_split_standard_value_on_track(k_points_input, 0, k_bottom_x, 2);
	set_split_standard_value_on_track(k_points_input, 1, k_bottom_y, 2);
	set_split_standard_value_on_track(k_points_input, 0, -k_bottom_x, 3);
	set_split_standard_value_on_track(k_points_input, 1, k_bottom_y, 3);
	set_split_standard_value_on_track(k_points_input, 0, -k_middle_x, 4);
	set_split_standard_value_on_track(k_points_input, 1, -k_middle_y, 4);

	// Initiate gizmos
	poly_gizmo_ = new PathGizmo(this);
}

QString PolygonGenerator::name() const
{
	return tr("Polygon");
}

QString PolygonGenerator::id() const
{
	return QStringLiteral("org.olivevideoeditor.Olive.polygon");
}

QVector<Node::CategoryID> PolygonGenerator::category() const
{
	return { k_category_generator };
}

QString PolygonGenerator::description() const
{
	return tr("Generate a 2D polygon of any amount of points.");
}

void PolygonGenerator::retranslate()
{
	super::retranslate();

	set_input_name(k_points_input, tr("Points"));
	set_input_name(k_color_input, tr("Color"));
}

ShaderJob PolygonGenerator::get_generate_job(const NodeValueRow &value,
										   const VideoParams &params) const
{
	VideoParams p = params;
	p.set_format(PixelFormat::u8);
	auto job = Texture::job(p, GenerateJob(value));

	// Conversion to RGB
	ShaderJob rgb;
	rgb.set_shader_id(QStringLiteral("rgb"));
	rgb.insert(QStringLiteral("texture_in"),
			   NodeValue(NodeValue::k_texture, job, this));
	rgb.insert(QStringLiteral("color_in"), value[k_color_input]);

	return rgb;
}

void PolygonGenerator::value(const NodeValueRow &value,
							 const NodeGlobals &globals,
							 NodeValueTable *table) const
{
	push_mergable_job(value,
					Texture::job(globals.vparams(),
								 get_generate_job(value, globals.vparams())),
					table);
}

void PolygonGenerator::generate_frame(FramePtr frame,
									 const GenerateJob &job) const
{
	// This could probably be more optimized, but for now we use Qt to draw to a QImage.
	// QImages only support integer pixels and we use float pixels, so what we do here is draw onto
	// a single-channel QImage (alpha only) and then transplant that alpha channel to our float buffer
	// with correct float RGB.
	QImage img((uchar *)frame->data(), frame->width(), frame->height(),
			   frame->linesize_bytes(), QImage::Format_RGBA8888_Premultiplied);
	img.fill(Qt::transparent);

	auto points = job.get(k_points_input).to_array();

	QPainterPath path = generate_path(points, input_array_size(k_points_input));

	QPainter p(&img);
	double par = frame->video_params().pixel_aspect_ratio().to_double();
	p.scale(1.0 / frame->video_params().divider() / par,
			1.0 / frame->video_params().divider());
	p.translate(frame->video_params().width() / 2 * par,
				frame->video_params().height() / 2);
	p.setBrush(Qt::white);
	p.setPen(Qt::NoPen);

	p.drawPath(path);
}

template <typename T> NodeGizmo *PolygonGenerator::create_appropriate_gizmo()
{
	return new T(this);
}

template <> NodeGizmo *PolygonGenerator::create_appropriate_gizmo<PointGizmo>()
{
	return add_draggable_gizmo<PointGizmo>();
}

template <typename T>
void PolygonGenerator::validate_gizmo_vector_size(QVector<T *> &vec, int new_sz)
{
	int old_sz = vec.size();

	if (old_sz != new_sz) {
		if (old_sz > new_sz) {
			for (int i = new_sz; i < old_sz; i++) {
				delete vec.at(i);
			}
		}

		vec.resize(new_sz);

		if (old_sz < new_sz) {
			for (int i = old_sz; i < new_sz; i++) {
				vec[i] = static_cast<T *>(create_appropriate_gizmo<T>());
			}
		}
	}
}

void PolygonGenerator::update_gizmo_positions(const NodeValueRow &row,
											const NodeGlobals &globals)
{
	QVector2D res;
	if (TexturePtr tex = row[k_base_input].to_texture()) {
		res = tex->virtual_resolution();
	} else {
		res = globals.square_resolution();
	}

	Imath::V2d half_res(res.x() / 2, res.y() / 2);

	auto points = row[k_points_input].to_array();

	int current_pos_sz = gizmo_position_handles_.size();

	validate_gizmo_vector_size(gizmo_position_handles_, points.size());
	validate_gizmo_vector_size(gizmo_bezier_handles_, points.size() * 2);
	validate_gizmo_vector_size(gizmo_bezier_lines_, points.size() * 2);

	for (int i = current_pos_sz; i < gizmo_position_handles_.size(); i++) {
		gizmo_position_handles_.at(i)->add_input(
			NodeKeyframeTrackReference(NodeInput(this, k_points_input, i), 0));
		gizmo_position_handles_.at(i)->add_input(
			NodeKeyframeTrackReference(NodeInput(this, k_points_input, i), 1));

		PointGizmo *bez_gizmo1 = gizmo_bezier_handles_.at(i * 2 + 0);
		bez_gizmo1->add_input(
			NodeKeyframeTrackReference(NodeInput(this, k_points_input, i), 2));
		bez_gizmo1->add_input(
			NodeKeyframeTrackReference(NodeInput(this, k_points_input, i), 3));
		bez_gizmo1->set_shape(PointGizmo::k_circle);
		bez_gizmo1->set_smaller(true);

		PointGizmo *bez_gizmo2 = gizmo_bezier_handles_.at(i * 2 + 1);
		bez_gizmo2->add_input(
			NodeKeyframeTrackReference(NodeInput(this, k_points_input, i), 4));
		bez_gizmo2->add_input(
			NodeKeyframeTrackReference(NodeInput(this, k_points_input, i), 5));
		bez_gizmo2->set_shape(PointGizmo::k_circle);
		bez_gizmo2->set_smaller(true);
	}

	int pts_sz = input_array_size(k_points_input);
	if (!points.empty()) {
		for (int i = 0; i < pts_sz; i++) {
			const Bezier &pt = points.at(i).to_bezier();

			Imath::V2d main = pt.to_vec() + half_res;
			Imath::V2d cp1 = main + pt.control_point_1_to_vec();
			Imath::V2d cp2 = main + pt.control_point_2_to_vec();

			gizmo_position_handles_[i]->set_point(QPointF(main.x, main.y));

			gizmo_bezier_handles_[i * 2]->set_point(QPointF(cp1.x, cp1.y));
			gizmo_bezier_lines_[i * 2]->set_line(
				QLineF(QPointF(main.x, main.y), QPointF(cp1.x, cp1.y)));
			gizmo_bezier_handles_[i * 2 + 1]->set_point(QPointF(cp2.x, cp2.y));
			gizmo_bezier_lines_[i * 2 + 1]->set_line(
				QLineF(QPointF(main.x, main.y), QPointF(cp2.x, cp2.y)));
		}
	}

	poly_gizmo_->set_path(generate_path(points, pts_sz)
							 .translated(QPointF(half_res.x, half_res.y)));
}

ShaderCode PolygonGenerator::get_shader_code(const ShaderRequest &request) const
{
	if (request.id == QStringLiteral("rgb")) {
		return ShaderCode(
			FileFunctions::read_file_as_string(":/shaders/rgb.frag"));
	} else {
		return super::get_shader_code(request);
	}
}

void PolygonGenerator::gizmo_drag_move(double x, double y,
									 const Qt::KeyboardModifiers &modifiers)
{
	DraggableGizmo *gizmo = static_cast<DraggableGizmo *>(sender());

	if (gizmo == poly_gizmo_) {
		// FIXME: Drag all points
	} else {
		NodeInputDragger &x_drag = gizmo->get_draggers()[0];
		NodeInputDragger &y_drag = gizmo->get_draggers()[1];
		x_drag.drag(x_drag.get_start_value().toDouble() + x);
		y_drag.drag(y_drag.get_start_value().toDouble() + y);
	}
}

void PolygonGenerator::add_point_to_path(QPainterPath *path, const Bezier &before,
									  const Bezier &after)
{
	Imath::V2d a = before.to_vec() + before.control_point_2_to_vec();
	Imath::V2d b = after.to_vec() + after.control_point_1_to_vec();
	Imath::V2d c = after.to_vec();

	path->cubicTo(QPointF(a.x, a.y), QPointF(b.x, b.y), QPointF(c.x, c.y));
}

QPainterPath PolygonGenerator::generate_path(const NodeValueArray &points,
											int size)
{
	QPainterPath path;

	if (!points.empty()) {
		const Bezier &first_pt = points.at(0).to_bezier();
		Imath::V2d v = first_pt.to_vec();
		path.moveTo(QPointF(v.x, v.y));

		for (int i = 1; i < size; i++) {
			add_point_to_path(&path, points.at(i - 1).to_bezier(),
						   points.at(i).to_bezier());
		}

		add_point_to_path(&path, points.at(size - 1).to_bezier(), first_pt);
	}

	return path;
}

}
