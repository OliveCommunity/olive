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

#include "colorbutton.h"

#include "dialog/color/colordialog.h"

namespace olive
{

ColorButton::ColorButton(ColorManager *color_manager, bool show_dialog_on_click,
						 QWidget *parent)
	: QPushButton(parent)
	, color_manager_(color_manager)
	, color_processor_(nullptr)
	, dialog_open_(false)
{
	setAutoFillBackground(true);

	if (show_dialog_on_click) {
		connect(this, &ColorButton::clicked, this,
				&ColorButton::show_color_dialog);
	}

	set_color(Color(1.0f, 1.0f, 1.0f));
}

const ManagedColor &ColorButton::get_color() const
{
	return color_;
}

void ColorButton::set_color(const ManagedColor &c)
{
	color_ = c;

	color_.set_color_input(
		color_manager_->get_compliant_color_space(color_.color_input()));
	color_.set_color_output(
		color_manager_->get_compliant_color_space(color_.color_output()));

	update_color();
}

void ColorButton::show_color_dialog()
{
	if (!dialog_open_) {
		dialog_open_ = true;
		ColorDialog *cd = new ColorDialog(color_manager_, color_, this);

		connect(cd, &ColorDialog::finished, this,
				&ColorButton::color_dialog_finished);

		cd->show();
	}
}

void ColorButton::color_dialog_finished(int e)
{
	ColorDialog *cd = static_cast<ColorDialog *>(sender());

	if (e == QDialog::Accepted) {
		color_ = cd->get_selected_color();

		update_color();

		emit color_changed(color_);
	}

	cd->deleteLater();

	dialog_open_ = false;
}

void ColorButton::update_color()
{
	color_processor_ = ColorProcessor::create(
		color_manager_, color_.color_input(), color_.color_output());

	QColor managed = QtUtils::to_q_color(color_processor_->convert_color(color_));

	setStyleSheet(QStringLiteral("%1--ColorButton {background: %2;}")
					  .arg(MACRO_VAL_AS_STR(olive), managed.name()));
}

}
