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

#include "multicamdisplay.h"

#include "oakengine/display.h"
#include "oakengine/node.h"

namespace olive
{

#define super ViewerDisplayWidget

MulticamDisplay::MulticamDisplay(QWidget *parent)
	: super(parent)
	, node_(nullptr)
	, shader_(nullptr)
	, rows_(0)
	, cols_(0)
{
}

void MulticamDisplay::on_paint()
{
	super::on_paint();

	if (node_) {
		QPainter p(paint_device());

		p.setPen(QPen(Qt::yellow, fontMetrics().height() / 4));
		p.setBrush(Qt::NoBrush);

		int rows, cols;
		oakengine_multicam_get_rows_and_columns(
			oakengine_multicam_get_source_count(
				reinterpret_cast<OakEngineNode *>(node_)),
			&rows, &cols);

		int multi = std::max(rows, cols);
		int cell_width = width() / multi;
		int cell_height = height() / multi;

		int col, row;
		int current_source = oakengine_multicam_get_current_source(
			reinterpret_cast<OakEngineNode *>(node_));
		oakengine_multicam_index_to_row_cols(
			current_source, rows, cols, &row, &col);

		QRect r(cell_width * col, cell_height * row, cell_width, cell_height);
		p.drawRect(generate_world_transform().mapRect(r));
	}
}

void MulticamDisplay::on_destroy()
{
	if (shader_) {
		oakengine_display_renderer_destroy_shader(renderer(), shader_);
		shader_ = nullptr;
	}
}

void *MulticamDisplay::load_custom_texture_from_frame(const QVariant &v)
{
	if (v.canConvert<QVector<void *>>()) {
		QVector<void *> tex = v.value<QVector<void *>>();

		oak_video_params main_params = this->get_viewport_params();
		void *main_tex = oakengine_display_texture_create(
			renderer(), &main_params, nullptr, 0);

		int rows, cols;
		oakengine_multicam_get_rows_and_columns(tex.size(), &rows, &cols);

		if (!shader_ || rows_ != rows || cols_ != cols) {
			if (shader_) {
				oakengine_display_renderer_destroy_shader(renderer(), shader_);
			}

			QString code = generate_shader_code(rows, cols);
			shader_ = oakengine_display_renderer_create_shader(
				renderer(), code.toUtf8().constData(), nullptr);

			rows_ = rows;
			cols_ = cols;
		}

		// Build name and texture arrays for multi-texture blit
		const int count = tex.size();
		QVector<QByteArray> name_storage(count);
		QVector<const char *> names(count);
		QVector<void *> textures(count);

		for (int i = 0; i < count; i++) {
			int c, r;
			oakengine_multicam_index_to_row_cols(i, rows, cols, &r, &c);
			name_storage[i] = QStringLiteral("tex_%1_%2")
								  .arg(QString::number(r), QString::number(c))
								  .toUtf8();
			names[i] = name_storage[i].constData();
			textures[i] = tex.at(i);
		}

		oakengine_display_renderer_blit_shader_multi(
			renderer(), shader_, names.data(), textures.data(), count,
			main_tex);

		// Release input texture handles (they were retained by the engine)
		for (int i = 0; i < count; i++) {
			oakengine_display_texture_free(tex.at(i));
		}

		return main_tex;
	} else {
		return super::load_custom_texture_from_frame(v);
	}
}

QString dbl_to_glsl(double d)
{
	return QString::number(d, 'f');
}

QString MulticamDisplay::generate_shader_code(int rows, int cols)
{
	int multiplier = std::max(cols, rows);

	QStringList shader;

	shader.append(QStringLiteral("in vec2 ove_texcoord;"));
	shader.append(QStringLiteral("out vec4 frag_color;"));

	for (int x = 0; x < cols; x++) {
		for (int y = 0; y < rows; y++) {
			shader.append(QStringLiteral("uniform sampler2D tex_%1_%2;")
							  .arg(QString::number(y), QString::number(x)));
			shader.append(QStringLiteral("uniform bool tex_%1_%2_enabled;")
							  .arg(QString::number(y), QString::number(x)));
		}
	}

	shader.append(QStringLiteral("void main() {"));

	for (int x = 0; x < cols; x++) {
		if (x > 0) {
			shader.append(QStringLiteral("  else"));
		}
		if (x == cols - 1) {
			shader.append(QStringLiteral("  {"));
		} else {
			shader.append(
				QStringLiteral("  if (ove_texcoord.x < %1) {")
					.arg(dbl_to_glsl(double(x + 1) / double(multiplier))));
		}

		for (int y = 0; y < rows; y++) {
			if (y > 0) {
				shader.append(QStringLiteral("    else"));
			}
			if (y == rows - 1) {
				shader.append(QStringLiteral("    {"));
			} else {
				shader.append(
					QStringLiteral("    if (ove_texcoord.y < %1) {")
						.arg(dbl_to_glsl(double(y + 1) / double(multiplier))));
			}
			QString input = QStringLiteral("tex_%1_%2")
								.arg(QString::number(y), QString::number(x));
			shader.append(
				QStringLiteral(
					"      vec2 coord = vec2((ove_texcoord.x+%1)*%2, (ove_texcoord.y+%3)*%4);")
					.arg(dbl_to_glsl(-double(x) / double(multiplier)),
						 dbl_to_glsl(multiplier),
						 dbl_to_glsl(-double(y) / double(multiplier)),
						 dbl_to_glsl(multiplier)));
			shader.append(
				QStringLiteral(
					"      if (%1_enabled && coord.x >= 0.0 && coord.x < 1.0 && coord.y >= 0.0 && coord.y < 1.0) {")
					.arg(input));
			shader.append(
				QStringLiteral("        frag_color = texture(%1, coord);")
					.arg(input));
			shader.append(QStringLiteral("      } else {"));
			shader.append(QStringLiteral("        discard;"));
			shader.append(QStringLiteral("      }"));
			shader.append(QStringLiteral("    }"));
		}

		shader.append(QStringLiteral("  }"));
	}

	shader.append(QStringLiteral("}"));

	return shader.join('\n');
}

void MulticamDisplay::set_multicam_node(MultiCamNode *n)
{
	node_ = n;
}

}
