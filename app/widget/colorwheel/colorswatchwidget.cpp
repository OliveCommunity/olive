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

#include "colorswatchwidget.h"

#include <QMouseEvent>

#include "common/colorcodingapp.h"

namespace olive
{

ColorSwatchWidget::ColorSwatchWidget(QWidget *parent)
	: QWidget(parent)
	, to_linear_processor_(nullptr)
	, to_display_processor_(nullptr)
{
}

const Color &ColorSwatchWidget::get_selected_color() const
{
	return selected_color_;
}

void ColorSwatchWidget::set_color_processor(ColorProcessorHandlePtr to_linear,
										  ColorProcessorHandlePtr to_display)
{
	to_linear_processor_ = to_linear;
	to_display_processor_ = to_display;

	// Force full update
	SelectedColorChangedEvent(get_selected_color(), true);
	update();
}

void ColorSwatchWidget::set_selected_color(const Color &c)
{
	set_selected_color_internal(c, true);
}

void ColorSwatchWidget::mousePressEvent(QMouseEvent *e)
{
	QWidget::mousePressEvent(e);

	set_selected_color_internal(get_color_from_screen_pos(e->pos()), false);
	emit selected_color_changed(get_selected_color());
}

void ColorSwatchWidget::mouseMoveEvent(QMouseEvent *e)
{
	QWidget::mouseMoveEvent(e);

	if (e->buttons() & Qt::LeftButton) {
		set_selected_color_internal(get_color_from_screen_pos(e->pos()), false);
		emit selected_color_changed(get_selected_color());
	}
}

void ColorSwatchWidget::SelectedColorChangedEvent(const Color &, bool)
{
}

Qt::GlobalColor ColorSwatchWidget::get_ui_selector_color() const
{
	return AppColorCoding::get_ui_selector_color(get_selected_color());
}

Color ColorSwatchWidget::get_managed_color(const Color &input) const
{
	if (to_linear_processor_ && to_display_processor_) {
		return oak_convert_color(to_display_processor_,
			oak_convert_color(to_linear_processor_, input));
	}

	return input;
}

void ColorSwatchWidget::set_selected_color_internal(const Color &c, bool external)
{
	selected_color_ = c;
	SelectedColorChangedEvent(c, external);
	update();
}

}
