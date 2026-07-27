
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

#include "waveform.h"

#include <QContextMenuEvent>
#include <QMenu>
#include <QPainter>
#include <QtMath>

#include "oakutil/qtutils.h"
#include "common/configwrapper.h"
#include "oakutil/filefunctions.h"
#include "oakengine/color.h"
#include "oakengine/display.h"

namespace olive
{

#define super ScopeBase

WaveformScope::WaveformScope(QWidget *parent)
	: super(parent)
	, parade_mode_(OAK_CONFIG("WaveformRgbParade").toBool())
{
}

void WaveformScope::set_parade_mode(bool enabled)
{
	parade_mode_ = enabled;
	OAK_CONFIG("WaveformRgbParade") = enabled;
	update();
}

void WaveformScope::contextMenuEvent(QContextMenuEvent *event)
{
	QMenu menu(this);
	QAction *parade = menu.addAction(tr("RGB Parade"));
	parade->setCheckable(true);
	parade->setChecked(parade_mode_);
	connect(parade, &QAction::triggered, this, &WaveformScope::set_parade_mode);
	menu.exec(event->globalPos());
}

ScopeShaderCode WaveformScope::generate_shader_code()
{
	return ScopeShaderCode{
		FileFunctions::read_file_as_string(":/shaders/rgbwaveform.frag"),
		FileFunctions::read_file_as_string(":/shaders/rgbwaveform.vert")};
}

void WaveformScope::draw_scope(void *managed_tex, void *pipeline)
{
	float waveform_scale = 0.80f;

	double luma_coeffs[3] = { 0.0f, 0.0f, 0.0f };
	oakengine_color_manager_default_luma_coefs(color_manager(), luma_coeffs);

	// Set up uniforms
	oak_shader_uniform uniforms[4];
	uniforms[0] = {"viewport", 1,
				   {static_cast<float>(width()), static_cast<float>(height()),
					0.0f, 0.0f}};
	uniforms[1] = {"luma_coeffs", 3,
				   {static_cast<float>(luma_coeffs[0]),
					static_cast<float>(luma_coeffs[1]),
					static_cast<float>(luma_coeffs[2]), 0.0f}};
	uniforms[2] = {"waveform_scale", 0, {waveform_scale, 0.0f, 0.0f, 0.0f}};
	uniforms[3] = {"parade_mode", 0,
				   {parade_mode_ ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f}};

	oak_video_params vp = get_viewport_params();
	oakengine_display_renderer_blit_shader_uniforms(
		renderer(), pipeline, managed_tex, uniforms, 4, nullptr, &vp);

	float waveform_dim_x = ceil((width() - 1.0) * waveform_scale);
	float waveform_dim_y = ceil((height() - 1.0) * waveform_scale);
	float waveform_start_dim_x = ((width() - 1.0) - waveform_dim_x) / 2.0f;
	float waveform_start_dim_y = ((height() - 1.0) - waveform_dim_y) / 2.0f;
	float waveform_end_dim_x = (width() - 1.0) - waveform_start_dim_x;

	// Draw line overlays
	QPainter p(paint_device());
	QFont font;
	font.setPixelSize(10);
	QFontMetrics font_metrics = QFontMetrics(font);
	QString label;
	float ire_increment = 0.1f;
	int ire_steps = qRound(1.0 / ire_increment);
	QVector<QLine> ire_lines(ire_steps + 1);
	int font_x_offset = 0;
	int font_y_offset = font_metrics.capHeight() / 2.0f;

	p.setCompositionMode(QPainter::CompositionMode_Plus);

	p.setPen(QColor(0.0, 0.6 * 255.0, 0.0));
	p.setFont(font);

	for (int i = 0; i <= ire_steps; i++) {
		ire_lines[i].setLine(
			waveform_start_dim_x,
			(waveform_dim_y * (i * ire_increment)) + waveform_start_dim_y,
			waveform_end_dim_x,
			(waveform_dim_y * (i * ire_increment)) + waveform_start_dim_y);
		label = QString::number(1.0 - (i * ire_increment), 'f', 1);
		font_x_offset = QtUtils::q_font_metrics_width(font_metrics, label) + 4;

		p.drawText(waveform_start_dim_x - font_x_offset,
				   (waveform_dim_y * (i * ire_increment)) +
					   waveform_start_dim_y + font_y_offset,
				   label);
	}

	p.drawLines(ire_lines);
}

void WaveformScope::draw_scope_software(QPainter &p, const QImage &image)
{
	const float waveform_scale = 0.80f;
	const int waveform_dim_x = qCeil((width() - 1.0) * waveform_scale);
	const int waveform_dim_y = qCeil((height() - 1.0) * waveform_scale);
	const int waveform_start_dim_x =
		qFloor(((width() - 1.0) - waveform_dim_x) / 2.0f);
	const int waveform_start_dim_y =
		qFloor(((height() - 1.0) - waveform_dim_y) / 2.0f);
	const int waveform_end_dim_x = width() - 1 - waveform_start_dim_x;

	QImage buf(width(), height(), QImage::Format_ARGB32_Premultiplied);
	buf.fill(Qt::transparent);

	const int src_w = image.width();
	const int src_h = image.height();

	// Limit analysis resolution to keep CPU usage reasonable on large frames.
	const int step_x = qMax(1, src_w / 512);
	const int step_y = qMax(1, src_h / 512);

	for (int sy = 0; sy < src_h; sy += step_y) {
		const uchar *src_line = image.constScanLine(sy);
		for (int sx = 0; sx < src_w; sx += step_x) {
			const uchar *src = src_line + sx * 4;
			float r = src[0] / 255.0f;
			float g = src[1] / 255.0f;
			float b = src[2] / 255.0f;

			int scope_x = waveform_start_dim_x +
						  int((float(sx) / float(src_w)) * waveform_dim_x);
			if (scope_x < waveform_start_dim_x ||
				scope_x >= waveform_end_dim_x) {
				continue;
			}

			auto mark = [&](int x, float value, int add_r, int add_g, int add_b) {
				int scope_y =
					waveform_start_dim_y + int((1.0f - value) * waveform_dim_y);
				if (x < waveform_start_dim_x || x >= waveform_end_dim_x ||
					scope_y < waveform_start_dim_y ||
					scope_y >= waveform_start_dim_y + waveform_dim_y) {
					return;
				}
				QRgb *dst_line = reinterpret_cast<QRgb *>(buf.scanLine(scope_y));
				QRgb cur = dst_line[x];
				int nr = qMin(255, qRed(cur) + add_r);
				int ng = qMin(255, qGreen(cur) + add_g);
				int nb = qMin(255, qBlue(cur) + add_b);
				int na = qMax(qMax(nr, ng), nb);
				dst_line[x] = qRgba(nr, ng, nb, na);
			};

			if (parade_mode_) {
				// RGB parade: each channel gets one third of the scope width
				const float zone_x = float(sx) / float(src_w) / 3.0f;
				mark(waveform_start_dim_x +
						 int((0.0f + zone_x) * waveform_dim_x),
					 r, 30, 0, 0);
				mark(waveform_start_dim_x +
						 int((1.0f / 3.0f + zone_x) * waveform_dim_x),
					 g, 0, 30, 0);
				mark(waveform_start_dim_x +
						 int((2.0f / 3.0f + zone_x) * waveform_dim_x),
					 b, 0, 0, 30);
			} else {
				mark(scope_x, r, 30, 0, 0);
				mark(scope_x, g, 0, 30, 0);
				mark(scope_x, b, 0, 0, 30);
			}
		}
	}

	p.setCompositionMode(QPainter::CompositionMode_Plus);
	p.drawImage(0, 0, buf);

	// Draw IRE line overlays
	QFont font;
	font.setPixelSize(10);
	QFontMetrics font_metrics = QFontMetrics(font);
	QString label;
	float ire_increment = 0.1f;
	int ire_steps = qRound(1.0 / ire_increment);
	QVector<QLine> ire_lines(ire_steps + 1);
	int font_x_offset = 0;
	int font_y_offset = font_metrics.capHeight() / 2.0f;

	p.setPen(QColor(0.0, 0.6 * 255.0, 0.0));
	p.setFont(font);

	for (int i = 0; i <= ire_steps; i++) {
		ire_lines[i].setLine(
			waveform_start_dim_x,
			(waveform_dim_y * (i * ire_increment)) + waveform_start_dim_y,
			waveform_end_dim_x,
			(waveform_dim_y * (i * ire_increment)) + waveform_start_dim_y);
		label = QString::number(1.0 - (i * ire_increment), 'f', 1);
		font_x_offset = QtUtils::q_font_metrics_width(font_metrics, label) + 4;

		p.drawText(waveform_start_dim_x - font_x_offset,
				   (waveform_dim_y * (i * ire_increment)) +
					   waveform_start_dim_y + font_y_offset,
				   label);
	}

	p.drawLines(ire_lines);
}

}
