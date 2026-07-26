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

#include "markerpainting.h"

#include <QApplication>
#include <QPalette>

#include "common/qtutils.h"
#include "common/colorcodingapp.h"

namespace olive
{

namespace MarkerPainting
{

int height(const QFontMetrics &fm)
{
	return fm.height();
}

QRect draw(QPainter *p, const QPoint &pt, int max_right, double scale,
		   bool selected, const QString &name, int color,
		   const core::Rational &in, const core::Rational &out)
{
	QFontMetrics fm = p->fontMetrics();

	int marker_height = height(fm);
	int marker_width = QtUtils::q_font_metrics_width(fm, QStringLiteral("H"));

	int half_width = marker_width / 2;

	QColor c = QtUtils::to_q_color(ColorCoding::get_color(color));
	if (selected) {
		p->setPen(Qt::white);
		p->setBrush(c.lighter());
	} else {
		p->setPen(Qt::black);
		p->setBrush(c);
	}

	int top = pt.y() - marker_height;

	QTextOption op(Qt::AlignLeft | Qt::AlignVCenter);
	op.setWrapMode(QTextOption::NoWrap);

	if (out != in) {
		QRect marker_rect(pt.x(), top, (out - in).to_double() * scale,
						  marker_height);

		p->drawRect(marker_rect);

		if (!name.isEmpty()) {
			p->setPen(ColorCoding::get_ui_selector_color(
				ColorCoding::get_color(color)));
			p->drawText(marker_rect.adjusted(marker_width / 4, 0, 0, 0), name,
						op);
		}

		return marker_rect;
	} else {
		int half_marker_height = marker_height / 3;
		int left = pt.x() - half_width;
		int right = pt.x() + half_width;
		int center_y = pt.y() - half_marker_height;

		QPoint points[] = {
			pt,
			QPoint(left, center_y),
			QPoint(left, top),
			QPoint(right, top),
			QPoint(right, center_y),
			pt,
		};

		p->setRenderHint(QPainter::Antialiasing);
		p->drawPolygon(points, 6);

		if (!name.isEmpty() && max_right != -1) {
			QRect text_rect(right, top, max_right - right, marker_height);

			int padding = QtUtils::q_font_metrics_width(p->fontMetrics(),
														QStringLiteral(" "));
			text_rect.adjust(padding, 0, -padding - half_width, 0);

			p->setPen(qApp->palette().text().color());
			p->drawText(text_rect, name, op);
		}

		return QRect(left, top, marker_width, marker_height);
	}
}

}

}
