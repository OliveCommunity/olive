/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2022 Olive Team
  Modifications Copyright (C) 2026 mikesolar

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

#include "vectorscope.h"

#include <QPainter>
#include <QtMath>
#include <QVector2D>
#include <QVector3D>

#include "common/qtutils.h"
#include "node/node.h"

namespace olive
{

#define super ScopeBase

VectorscopeScope::VectorscopeScope(QWidget *parent)
	: super(parent)
{
}

ShaderCode VectorscopeScope::generate_shader_code()
{
	return ShaderCode(
		FileFunctions::read_file_as_string(":/shaders/rgbvectorscope.frag"),
		FileFunctions::read_file_as_string(":/shaders/rgbvectorscope.vert"));
}

void VectorscopeScope::draw_scope(TexturePtr managed_tex, QVariant pipeline)
{
	float vectorscope_scale = 0.80f;
	float vectorscope_gain = 1.45f;
	float vectorscope_point_radius = 1.75f;
	float vectorscope_intensity = 0.035f;
	float vectorscope_sample_grid = 28.0f;

	ShaderJob job;

	job.insert(QStringLiteral("viewport"),
			   NodeValue(NodeValue::k_vec2, QVector2D(width(), height())));

	double luma_coeffs[3] = { 0.0f, 0.0f, 0.0f };
	oakengine_color_manager_default_luma_coefs(color_manager(), luma_coeffs);
	job.insert(
		QStringLiteral("luma_coeffs"),
		NodeValue(NodeValue::k_vec3,
				  QVector3D(luma_coeffs[0], luma_coeffs[1], luma_coeffs[2])));

	job.insert(QStringLiteral("vectorscope_scale"),
			   NodeValue(NodeValue::k_float, vectorscope_scale));
	job.insert(QStringLiteral("vectorscope_gain"),
			   NodeValue(NodeValue::k_float, vectorscope_gain));
	job.insert(QStringLiteral("vectorscope_point_radius"),
			   NodeValue(NodeValue::k_float, vectorscope_point_radius));
	job.insert(QStringLiteral("vectorscope_intensity"),
			   NodeValue(NodeValue::k_float, vectorscope_intensity));
	job.insert(QStringLiteral("vectorscope_sample_grid"),
			   NodeValue(NodeValue::k_float, vectorscope_sample_grid));

	job.insert(QStringLiteral("ove_maintex"),
			   NodeValue(NodeValue::k_texture,
						 QVariant::fromValue(managed_tex)));

	renderer()->blit(pipeline, job, get_viewport_params());

	QPainter p(paint_device());
	QFont font = p.font();
	font.setPixelSize(10);
	QFontMetrics font_metrics = QFontMetrics(font);

	p.setCompositionMode(QPainter::CompositionMode_Plus);
	p.setPen(QColor(0, 153, 0));
	p.setFont(font);

	float scope_size = qMin(width(), height()) * vectorscope_scale;
	QPointF center(width() * 0.5, height() * 0.5);
	float radius = scope_size * 0.5;

	p.drawEllipse(center, radius, radius);
	p.drawLine(QPointF(center.x() - radius, center.y()),
			   QPointF(center.x() + radius, center.y()));
	p.drawLine(QPointF(center.x(), center.y() - radius),
			   QPointF(center.x(), center.y() + radius));

	struct Target {
		const char *label;
		float angle;
	};
	const Target targets[] = {
		{ "R", 0.0f },	  { "Mg", 60.0f }, { "B", 120.0f },
		{ "Cy", 180.0f }, { "G", 240.0f }, { "Yl", 300.0f },
	};

	const float label_radius = radius + 12.0f;
	const float marker_radius = radius * 0.72f;
	constexpr float k_pi = 3.14159265358979323846f;

	for (const Target &target : targets) {
		float radians = target.angle * k_pi / 180.0f;
		QPointF direction(qCos(radians), -qSin(radians));
		QPointF marker = center + direction * marker_radius;
		QPointF label_pos = center + direction * label_radius;
		QString label = QString::fromUtf8(target.label);

		p.drawEllipse(marker, 3.0, 3.0);
		p.drawText(label_pos.x() -
					   QtUtils::q_font_metrics_width(font_metrics, label) * 0.5,
				   label_pos.y() + font_metrics.capHeight() * 0.5, label);
	}
}

void VectorscopeScope::draw_scope_software(QPainter &p, const QImage &image)
{
	const float vectorscope_scale = 0.80f;
	const float vectorscope_gain = 1.45f;
	const float vectorscope_intensity = 0.035f;

	QImage buf(width(), height(), QImage::Format_ARGB32_Premultiplied);
	buf.fill(Qt::transparent);

	double luma_coeffs[3] = { 0.0, 0.0, 0.0 };
	oakengine_color_manager_default_luma_coefs(color_manager(), luma_coeffs);

	const int src_w = image.width();
	const int src_h = image.height();

	// Limit analysis resolution to keep CPU usage reasonable.
	const int step_x = qMax(1, src_w / 256);
	const int step_y = qMax(1, src_h / 256);

	float scope_size = qMin(width(), height()) * vectorscope_scale;
	QPointF center(width() * 0.5, height() * 0.5);
	float radius = scope_size * 0.5;

	for (int sy = 0; sy < src_h; sy += step_y) {
		const uchar *src_line = image.constScanLine(sy);
		for (int sx = 0; sx < src_w; sx += step_x) {
			const uchar *src = src_line + sx * 4;
			float r = src[0] / 255.0f;
			float g = src[1] / 255.0f;
			float b = src[2] / 255.0f;

			float y =
				r * luma_coeffs[0] + g * luma_coeffs[1] + b * luma_coeffs[2];
			float cb = (b - y) / qMax(2.0f * (1.0f - luma_coeffs[2]), 0.0001f);
			float cr = (r - y) / qMax(2.0f * (1.0f - luma_coeffs[0]), 0.0001f);

			QPointF point = center + QPointF(cr * vectorscope_gain * radius,
											 -cb * vectorscope_gain * radius);

			int px = qRound(point.x());
			int py = qRound(point.y());
			if (px < 0 || px >= width() || py < 0 || py >= height()) {
				continue;
			}

			QRgb *dst_line = reinterpret_cast<QRgb *>(buf.scanLine(py));
			QRgb cur = dst_line[px];
			int add = qRound(255.0f * vectorscope_intensity);
			int nr = qMin(255, qRed(cur) + int(r * add));
			int ng = qMin(255, qGreen(cur) + int(g * add));
			int nb = qMin(255, qBlue(cur) + int(b * add));
			int na = qMax(qMax(nr, ng), nb);
			dst_line[px] = qRgba(nr, ng, nb, na);
		}
	}

	p.setCompositionMode(QPainter::CompositionMode_Plus);
	p.drawImage(0, 0, buf);

	// Draw overlay (circle, cross-hairs, targets)
	QFont font = p.font();
	font.setPixelSize(10);
	QFontMetrics font_metrics = QFontMetrics(font);

	p.setCompositionMode(QPainter::CompositionMode_SourceOver);
	p.setPen(QColor(0, 153, 0));
	p.setFont(font);

	p.drawEllipse(center, radius, radius);
	p.drawLine(QPointF(center.x() - radius, center.y()),
			   QPointF(center.x() + radius, center.y()));
	p.drawLine(QPointF(center.x(), center.y() - radius),
			   QPointF(center.x(), center.y() + radius));

	struct Target {
		const char *label;
		float angle;
	};
	const Target targets[] = {
		{ "R", 0.0f },	  { "Mg", 60.0f }, { "B", 120.0f },
		{ "Cy", 180.0f }, { "G", 240.0f }, { "Yl", 300.0f },
	};

	const float label_radius = radius + 12.0f;
	const float marker_radius = radius * 0.72f;
	constexpr float k_pi = 3.14159265358979323846f;

	for (const Target &target : targets) {
		float radians = target.angle * k_pi / 180.0f;
		QPointF direction(qCos(radians), -qSin(radians));
		QPointF marker = center + direction * marker_radius;
		QPointF label_pos = center + direction * label_radius;
		QString label = QString::fromUtf8(target.label);

		p.drawEllipse(marker, 3.0, 3.0);
		p.drawText(label_pos.x() -
					   QtUtils::q_font_metrics_width(font_metrics, label) * 0.5,
				   label_pos.y() + font_metrics.capHeight() * 0.5, label);
	}
}

}
