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

#include <cstdio>
#include <cstring>

namespace olive
{

const std::string PolygonGenerator::k_points_input = "points_in";
const std::string PolygonGenerator::k_color_input = "color_in";

#define super GeneratorWithMerge

PolygonGenerator::PolygonGenerator()
{
	add_input(k_points_input, NodeValue::k_bezier, Vector2D(0, 0),
			 InputFlags(k_input_flag_array));

	add_input(k_color_input, NodeValue::k_color,
			 Variant::from_value(Color(1.0, 1.0, 1.0)));

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
	add_gizmo(poly_gizmo_);
}

std::string PolygonGenerator::name() const
{
	return "Polygon";
}

std::string PolygonGenerator::id() const
{
	return "org.olivevideoeditor.Olive.polygon";
}

std::vector<Node::CategoryID> PolygonGenerator::category() const
{
	return { k_category_generator };
}

std::string PolygonGenerator::description() const
{
	return "Generate a 2D polygon of any amount of points.";
}

void PolygonGenerator::retranslate()
{
	super::retranslate();

	set_input_name(k_points_input, "Points");
	set_input_name(k_color_input, "Color");
}

ShaderJob PolygonGenerator::get_generate_job(const NodeValueRow &value,
										   const VideoParams &params) const
{
	VideoParams p = params;
	p.set_format(PixelFormat::u8);
	auto job = Texture::job(p, GenerateJob(value));

	// Conversion to RGB
	ShaderJob rgb;
	rgb.set_shader_id("rgb");
	rgb.insert("texture_in", NodeValue(NodeValue::k_texture, job, this));
	rgb.insert("color_in", value.at(k_color_input));

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
	// Formerly drawn with QPainter into a QImage wrapping the frame buffer
	// (QImage::Format_RGBA8888_Premultiplied, filled Qt::transparent). The
	// rasterizer now lives behind the facade-installed PathFillBackend (see
	// node/geometry.h); the path data model below is unchanged.
	unsigned char *img = reinterpret_cast<unsigned char *>(frame->data());
	const int width = frame->width();
	const int height = frame->height();
	const int linesize = frame->linesize_bytes();

	// Clear to transparent (formerly QImage::fill(Qt::transparent))
	for (int y = 0; y < height; y++) {
		std::memset(img + size_t(linesize) * y, 0, size_t(width) * 4);
	}

	if (PathFillBackend fill = path_fill_backend()) {
		auto points = job.get(k_points_input).to_array();

		PainterPath path = generate_path(points, input_array_size(k_points_input));

		double par = frame->video_params().pixel_aspect_ratio().to_double();
		double scale_x = 1.0 / frame->video_params().divider() / par;
		double scale_y = 1.0 / frame->video_params().divider();
		double translate_x = frame->video_params().width() / 2 * par;
		double translate_y = frame->video_params().height() / 2;

		fill(path, scale_x, scale_y, translate_x, translate_y, img, width,
			 height, linesize);
	} else {
		static bool warned = false;
		if (!warned) {
			fprintf(stderr,
					"PolygonGenerator: no PathFillBackend installed, frame left empty\n");
			warned = true;
		}
	}
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
void PolygonGenerator::validate_gizmo_vector_size(std::vector<T *> &vec, int new_sz)
{
	int old_sz = int(vec.size());

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
	Vector2D res;
	if (TexturePtr tex = row.at(k_base_input).to_texture()) {
		res = tex->virtual_resolution();
	} else {
		res = globals.square_resolution();
	}

	Imath::V2d half_res(res.x() / 2, res.y() / 2);

	auto points = row.at(k_points_input).to_array();

	int current_pos_sz = int(gizmo_position_handles_.size());

	validate_gizmo_vector_size(gizmo_position_handles_, int(points.size()));
	validate_gizmo_vector_size(gizmo_bezier_handles_, int(points.size()) * 2);
	validate_gizmo_vector_size(gizmo_bezier_lines_, int(points.size()) * 2);

	for (int i = current_pos_sz; i < int(gizmo_position_handles_.size()); i++) {
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

			gizmo_position_handles_[i]->set_point(PointF(main.x, main.y));

			gizmo_bezier_handles_[i * 2]->set_point(PointF(cp1.x, cp1.y));
			gizmo_bezier_lines_[i * 2]->set_line(
				LineF(PointF(main.x, main.y), PointF(cp1.x, cp1.y)));
			gizmo_bezier_handles_[i * 2 + 1]->set_point(PointF(cp2.x, cp2.y));
			gizmo_bezier_lines_[i * 2 + 1]->set_line(
				LineF(PointF(main.x, main.y), PointF(cp2.x, cp2.y)));
		}
	}

	// The PathGizmo no longer stores a path (drawing data belongs to the app
	// layer since the gizmo wave), so the former set_path() call with the
	// half-resolution-translated generate_path() result is gone; the path
	// itself is still built for generate_frame().
}

ShaderCode PolygonGenerator::get_shader_code(const ShaderRequest &request) const
{
	if (request.id == "rgb") {
		return ShaderCode(
			FileFunctions::read_file_as_string(":/shaders/rgb.frag"));
	} else {
		return super::get_shader_code(request);
	}
}

void PolygonGenerator::gizmo_drag_move(double x, double y, int modifiers)
{
	(void) modifiers;

	DraggableGizmo *gizmo = static_cast<DraggableGizmo *>(current_gizmo());

	if (gizmo == poly_gizmo_) {
		// FIXME: Drag all points
	} else {
		NodeInputDragger &x_drag = gizmo->get_draggers()[0];
		NodeInputDragger &y_drag = gizmo->get_draggers()[1];
		x_drag.drag(x_drag.get_start_value().to_double() + x);
		y_drag.drag(y_drag.get_start_value().to_double() + y);
	}
}

void PolygonGenerator::add_point_to_path(PainterPath *path, const Bezier &before,
									  const Bezier &after)
{
	Imath::V2d a = before.to_vec() + before.control_point_2_to_vec();
	Imath::V2d b = after.to_vec() + after.control_point_1_to_vec();
	Imath::V2d c = after.to_vec();

	path->cubic_to(PointF(a.x, a.y), PointF(b.x, b.y), PointF(c.x, c.y));
}

PainterPath PolygonGenerator::generate_path(const NodeValueArray &points,
											int size)
{
	PainterPath path;

	if (!points.empty()) {
		const Bezier &first_pt = points.at(0).to_bezier();
		Imath::V2d v = first_pt.to_vec();
		path.move_to(PointF(v.x, v.y));

		for (int i = 1; i < size; i++) {
			add_point_to_path(&path, points.at(i - 1).to_bezier(),
						   points.at(i).to_bezier());
		}

		add_point_to_path(&path, points.at(size - 1).to_bezier(), first_pt);
	}

	return path;
}

}
