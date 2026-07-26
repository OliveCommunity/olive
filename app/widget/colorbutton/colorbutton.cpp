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

#include "common/qtutils.h"
#include "dialog/color/colordialog.h"

namespace olive
{

ColorButton::ColorButton(OakEngineColorManager *color_manager, bool show_dialog_on_click,
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

	QByteArray in_name = color_.color_input().toUtf8();
	QString compliant_in = oak_query_string([this, &in_name](char *buf, int size) {
		return oakengine_color_manager_compliant_color_space(
			color_manager_, in_name.constData(), buf, size);
	});
	color_.set_color_input(compliant_in);

	QByteArray out_name = color_.color_output().output().toUtf8();
	ColorTransform cs_out = color_.color_output();
	QByteArray o, v, l;
	oak_color_transform pod = oak_to_transform(cs_out, &o, &v, &l);
	int out_is_display = 0;
	char out_buf[256], view_buf[256], look_buf[256];
	oakengine_color_manager_compliant_transform(
		color_manager_, &pod, 0, &out_is_display, out_buf, sizeof(out_buf),
		view_buf, sizeof(view_buf), look_buf, sizeof(look_buf));
	if (out_is_display) {
		color_.set_color_output(ColorTransform(QString::fromUtf8(out_buf),
											   QString::fromUtf8(view_buf),
											   QString::fromUtf8(look_buf)));
	} else {
		color_.set_color_output(ColorTransform(QString::fromUtf8(out_buf)));
	}

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
	QByteArray in_cs = color_.color_input().toUtf8();
	ColorTransform out = color_.color_output();
	QByteArray o, v, l;
	oak_color_transform out_pod = oak_to_transform(out, &o, &v, &l);
	color_processor_ = ColorProcessorHandlePtr(
		oakengine_color_processor_create(color_manager_, in_cs.constData(),
										&out_pod,
										OAKENGINE_COLOR_PROCESSOR_NORMAL),
		ColorProcessorHandleDeleter());

	QColor managed = QtUtils::to_q_color(oak_convert_color(color_processor_, color_));

	setStyleSheet(QStringLiteral("%1--ColorButton {background: %2;}")
					  .arg(MACRO_VAL_AS_STR(olive), managed.name()));
}

}
