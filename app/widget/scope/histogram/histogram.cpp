
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

#include "histogram.h"

#include <array>
#include <QPainter>
#include <QtMath>

#include "oakutil/qtutils.h"
#include "oakutil/filefunctions.h"
#include "oakengine/display.h"

namespace olive
{

#define super ScopeBase

HistogramScope::HistogramScope(QWidget *parent)
	: super(parent)
	, pipeline_secondary_(nullptr)
	, texture_row_sums_(nullptr)
{
}

void HistogramScope::on_init()
{
	super::on_init();

	QString frag = FileFunctions::read_file_as_string(
		":/shaders/rgbhistogram_secondary.frag");
	QString vert = FileFunctions::read_file_as_string(
		":/shaders/rgbhistogram.vert");
	pipeline_secondary_ = oakengine_display_renderer_create_shader(
		renderer(), frag.toUtf8().constData(), vert.toUtf8().constData());
}

void HistogramScope::on_destroy()
{
	if (pipeline_secondary_) {
		oakengine_display_renderer_destroy_shader(renderer(),
												  pipeline_secondary_);
		pipeline_secondary_ = nullptr;
	}
	if (texture_row_sums_) {
		oakengine_display_texture_free(texture_row_sums_);
		texture_row_sums_ = nullptr;
	}

	super::on_destroy();
}

ScopeShaderCode HistogramScope::generate_shader_code()
{
	return ScopeShaderCode{
		FileFunctions::read_file_as_string(":/shaders/rgbhistogram.frag"),
		FileFunctions::read_file_as_string(":/shaders/default.vert")};
}

void HistogramScope::draw_scope(void *managed_tex, void *pipeline)
{
	float histogram_scale = 0.80f;
	// This value is eyeballed for usefulness. Until we have a geometry
	// shader approach, it is impossible to normalize against a peak
	// sum of image values.
	float histogram_base = 2.5f;
	float histogram_power = 1.0f / histogram_base;

	// Set up uniforms shared by both passes
	oak_shader_uniform uniforms[3];
	uniforms[0] = {"viewport", 1,
				   {static_cast<float>(width()), static_cast<float>(height())}};
	uniforms[1] = {"histogram_scale", 0, {histogram_scale, 0.0f}};
	uniforms[2] = {"histogram_power", 0, {histogram_power, 0.0f}};

	// Recreate row-sums texture if size changed
	bool need_recreate = false;
	if (!texture_row_sums_) {
		need_recreate = true;
	} else if (oakengine_display_texture_width(texture_row_sums_) != width() ||
			   oakengine_display_texture_height(texture_row_sums_) !=
				   height()) {
		need_recreate = true;
	}

	if (need_recreate) {
		if (texture_row_sums_) {
			oakengine_display_texture_free(texture_row_sums_);
		}
		oak_video_params pod = {};
		pod.width = width();
		pod.height = height();
		pod.format = oakengine_display_texture_format(managed_tex);
		texture_row_sums_ =
			oakengine_display_texture_create(renderer(), &pod, nullptr, 0);
	}

	// Pass 1: Draw managed texture to row-sums texture
	oakengine_display_renderer_blit_shader_uniforms(
		renderer(), pipeline, managed_tex, uniforms, 3, texture_row_sums_,
		nullptr);

	// Pass 2: Draw row-sums into a histogram on screen
	oak_video_params vp = get_viewport_params();
	oakengine_display_renderer_blit_shader_uniforms(
		renderer(), pipeline_secondary_, texture_row_sums_, uniforms, 3,
		nullptr, &vp);

	// Draw line overlays
	QPainter p(paint_device());
	QFont font = p.font();
	font.setPixelSize(10);
	QFontMetrics font_metrics = QFontMetrics(font);
	QString label;
	std::vector<float> histogram_increments = { 0.00, 0.25, 0.50, 1.0 };

	int histogram_steps = histogram_increments.size();
	QVector<QLine> histogram_lines(histogram_steps + 1);
	int font_x_offset = 0;
	int font_y_offset = font_metrics.capHeight() / 2.0f;

	p.setCompositionMode(QPainter::CompositionMode_Plus);

	p.setPen(QColor(0.0, 0.6 * 255.0, 0.0));
	p.setFont(font);

	float histogram_dim_x = ceil((width() - 1.0) * histogram_scale);
	float histogram_dim_y = ceil((height() - 1.0) * histogram_scale);
	float histogram_start_dim_x = ((width() - 1.0) - histogram_dim_x) / 2.0f;
	float histogram_start_dim_y = ((height() - 1.0) - histogram_dim_y) / 2.0f;
	float histogram_end_dim_x = (width() - 1.0) - histogram_start_dim_x;

	for (std::vector<float>::iterator it = histogram_increments.begin();
		 it != histogram_increments.end(); it++) {
		histogram_lines[it - histogram_increments.begin()].setLine(
			histogram_start_dim_x,
			(histogram_dim_y * pow(1.0 - *it, histogram_base)) +
				histogram_start_dim_y,
			histogram_end_dim_x,
			(histogram_dim_y * pow(1.0 - *it, histogram_base)) +
				histogram_start_dim_y);
		label = QString::number(*it * 100, 'f', 1) + "%";
		font_x_offset = QtUtils::q_font_metrics_width(font_metrics, label) + 4;

		p.drawText(histogram_start_dim_x - font_x_offset,
				   (histogram_dim_y * pow(1.0 - *it, histogram_base)) +
					   histogram_start_dim_y + font_y_offset,
				   label);
	}
	p.drawLines(histogram_lines);
}

void HistogramScope::draw_scope_software(QPainter &p, const QImage &image)
{
	const float histogram_scale = 0.80f;
	const float histogram_base = 2.5f;

	const int histogram_dim_x = qCeil((width() - 1.0) * histogram_scale);
	const int histogram_dim_y = qCeil((height() - 1.0) * histogram_scale);
	const int histogram_start_dim_x =
		qFloor(((width() - 1.0) - histogram_dim_x) / 2.0f);
	const int histogram_start_dim_y =
		qFloor(((height() - 1.0) - histogram_dim_y) / 2.0f);
	const int histogram_end_dim_x = width() - 1 - histogram_start_dim_x;

	std::array<int, 256> r_counts{};
	std::array<int, 256> g_counts{};
	std::array<int, 256> b_counts{};

	const int src_w = image.width();
	const int src_h = image.height();

	// Limit analysis resolution to keep CPU usage reasonable.
	const int step_x = qMax(1, src_w / 512);
	const int step_y = qMax(1, src_h / 512);

	for (int sy = 0; sy < src_h; sy += step_y) {
		const uchar *src_line = image.constScanLine(sy);
		for (int sx = 0; sx < src_w; sx += step_x) {
			const uchar *src = src_line + sx * 4;
			r_counts[src[0]]++;
			g_counts[src[1]]++;
			b_counts[src[2]]++;
		}
	}

	int max_count = 1;
	for (int i = 0; i < 256; ++i) {
		max_count = qMax(max_count, r_counts[i]);
		max_count = qMax(max_count, g_counts[i]);
		max_count = qMax(max_count, b_counts[i]);
	}

	auto draw_channel = [&](const std::array<int, 256> &counts,
							const QColor &color) {
		QPen pen(color);
		pen.setWidth(2);
		p.setPen(pen);

		QVector<QPointF> points;
		points.reserve(256);
		for (int i = 0; i < 256; ++i) {
			float x =
				histogram_start_dim_x + (float(i) / 255.0f) * histogram_dim_x;
			float normalized = float(counts[i]) / float(max_count);
			float y = histogram_start_dim_y +
					  histogram_dim_y *
						  (1.0f - pow(normalized, 1.0f / histogram_base));
			points.append(QPointF(x, y));
		}
		p.drawPolyline(points.constData(), points.size());
	};

	p.setCompositionMode(QPainter::CompositionMode_Plus);
	draw_channel(r_counts, QColor(255, 0, 0));
	draw_channel(g_counts, QColor(0, 255, 0));
	draw_channel(b_counts, QColor(0, 0, 255));

	// Draw percentage line overlays
	QFont font = p.font();
	font.setPixelSize(10);
	QFontMetrics font_metrics = QFontMetrics(font);
	QString label;
	std::vector<float> histogram_increments = { 0.00, 0.25, 0.50, 1.0 };

	int histogram_steps = histogram_increments.size();
	QVector<QLine> histogram_lines(histogram_steps + 1);
	int font_x_offset = 0;
	int font_y_offset = font_metrics.capHeight() / 2.0f;

	p.setCompositionMode(QPainter::CompositionMode_SourceOver);
	p.setPen(QColor(0.0, 0.6 * 255.0, 0.0));
	p.setFont(font);

	for (std::vector<float>::iterator it = histogram_increments.begin();
		 it != histogram_increments.end(); it++) {
		histogram_lines[it - histogram_increments.begin()].setLine(
			histogram_start_dim_x,
			(histogram_dim_y * pow(1.0 - *it, histogram_base)) +
				histogram_start_dim_y,
			histogram_end_dim_x,
			(histogram_dim_y * pow(1.0 - *it, histogram_base)) +
				histogram_start_dim_y);
		label = QString::number(*it * 100, 'f', 1) + "%";
		font_x_offset = QtUtils::q_font_metrics_width(font_metrics, label) + 4;

		p.drawText(histogram_start_dim_x - font_x_offset,
				   (histogram_dim_y * pow(1.0 - *it, histogram_base)) +
					   histogram_start_dim_y + font_y_offset,
				   label);
	}
	p.drawLines(histogram_lines);
}

}
