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

#include "colorpreviewbox.h"

#include <QPainter>

#include "oakutil/qtutils.h"

namespace olive
{

ColorPreviewBox::ColorPreviewBox(QWidget *parent)
	: QWidget(parent)
	, to_ref_processor_(nullptr)
	, to_display_processor_(nullptr)
{
}

void ColorPreviewBox::set_color_processor(ColorProcessorHandlePtr to_ref,
										ColorProcessorHandlePtr to_display)
{
	to_ref_processor_ = to_ref;
	to_display_processor_ = to_display;

	update();
}

void ColorPreviewBox::set_color(const Color &c)
{
	color_ = c;
	update();
}

void ColorPreviewBox::paintEvent(QPaintEvent *e)
{
	QWidget::paintEvent(e);

	QColor c;

	// Color management
	if (to_ref_processor_ && to_display_processor_) {
		c = QtUtils::to_q_color(oak_convert_color(to_display_processor_,
			oak_convert_color(to_ref_processor_, color_)));
	} else {
		c = QtUtils::to_q_color(color_);
	}

	QPainter p(this);

	QRect draw_rect = rect().adjusted(0, 0, -1, -1);

	p.setPen(Qt::black);

	if (color_.alpha() < 1.0) {
		// Draw black background so the background isn't the window color
		p.setBrush(Qt::black);
		p.drawRect(draw_rect);
	}

	// Draw with color over the top
	p.setBrush(c);
	p.drawRect(draw_rect);
}

}
