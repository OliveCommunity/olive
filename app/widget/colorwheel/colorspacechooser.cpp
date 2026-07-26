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

#include "colorspacechooser.h"

#include <QGridLayout>
#include <QLabel>

#include "widget/manageddisplay/colorprocessorhandle.h"

namespace olive
{

ColorSpaceChooser::ColorSpaceChooser(OakEngineColorManager *color_manager,
									 bool enable_input_field,
									 bool enable_display_fields,
									 QWidget *parent)
	: QGroupBox(parent)
	, color_manager_(color_manager)
{
	QGridLayout *layout = new QGridLayout(this);

	setTitle(tr("Color Management"));

	int row = 0;

	if (enable_input_field) {
		QString field_text;

		if (enable_display_fields) {
			// If the display fields are visible, identify this as the input
			field_text = tr("Input:");
		} else {
			// Otherwise, this widget will essentially just serve as a list of standard color spaces
			field_text = tr("Color Space:");
		}

		layout->addWidget(new QLabel(field_text), row, 0);

		input_combobox_ = new QComboBox();
		layout->addWidget(input_combobox_, row, 1);

		QStringList input_spaces = oak_query_string_list(
			[this]() {
				return oakengine_color_manager_colorspace_count(color_manager_);
			},
			[this](int i, char *buf, int size) {
				return oakengine_color_manager_colorspace_at(color_manager_, i,
															 buf, size);
			});

		foreach (const QString &s, input_spaces) {
			input_combobox_->addItem(s);
		}

		QString def_input = oak_query_string([this](char *buf, int size) {
			return oakengine_color_manager_default_input_color_space(
				color_manager_, buf, size);
		});
		if (!def_input.isEmpty()) {
			input_combobox_->setCurrentText(def_input);
		}

		connect(input_combobox_, &QComboBox::currentTextChanged, this,
				&ColorSpaceChooser::combo_box_changed);

		row++;
	} else {
		input_combobox_ = nullptr;
	}

	if (enable_display_fields) {
		{
			layout->addWidget(new QLabel(tr("Display:")), row, 0);

			display_combobox_ = new QComboBox();
			layout->addWidget(display_combobox_, row, 1);

			QStringList display_spaces = oak_query_string_list(
				[this]() {
					return oakengine_color_manager_display_count(color_manager_);
				},
				[this](int i, char *buf, int size) {
					return oakengine_color_manager_display_at(color_manager_, i,
															  buf, size);
				});

			foreach (const QString &s, display_spaces) {
				display_combobox_->addItem(s);
			}

			display_combobox_->setCurrentText(
				oak_query_string([this](char *buf, int size) {
					return oakengine_color_manager_default_display(
						color_manager_, buf, size);
				}));

			connect(display_combobox_, &QComboBox::currentTextChanged, this,
					&ColorSpaceChooser::combo_box_changed);
		}

		row++;

		{
			layout->addWidget(new QLabel(tr("View:")), row, 0);

			view_combobox_ = new QComboBox();
			layout->addWidget(view_combobox_, row, 1);

			update_views(display_combobox_->currentText());

			connect(view_combobox_, &QComboBox::currentTextChanged, this,
					&ColorSpaceChooser::combo_box_changed);
		}

		row++;

		{
			layout->addWidget(new QLabel(tr("Look:")), row, 0);

			look_combobox_ = new QComboBox();
			layout->addWidget(look_combobox_, row, 1);

			QStringList looks = oak_query_string_list(
				[this]() {
					return oakengine_color_manager_look_count(color_manager_);
				},
				[this](int i, char *buf, int size) {
					return oakengine_color_manager_look_at(color_manager_, i,
														   buf, size);
				});

			look_combobox_->addItem(tr("(None)"), QString());

			foreach (const QString &s, looks) {
				look_combobox_->addItem(s, s);
			}

			connect(look_combobox_, &QComboBox::currentTextChanged, this,
					&ColorSpaceChooser::combo_box_changed);
		}
	} else {
		display_combobox_ = nullptr;
		view_combobox_ = nullptr;
		look_combobox_ = nullptr;
	}
}

QString ColorSpaceChooser::input() const
{
	if (input_combobox_) {
		return input_combobox_->currentText();
	} else {
		return QString();
	}
}

ColorTransform ColorSpaceChooser::output() const
{
	return ColorTransform(display_combobox_->currentText(),
						  view_combobox_->currentText(),
						  look_combobox_->currentIndex() == 0 ?
							  QString() :
							  look_combobox_->currentText());
}

void ColorSpaceChooser::set_input(const QString &s)
{
	QByteArray name = s.toUtf8();
	QString compliant = oak_query_string([this, &name](char *buf, int size) {
		return oakengine_color_manager_compliant_color_space(
			color_manager_, name.constData(), buf, size);
	});
	input_combobox_->setCurrentText(compliant);
}

void ColorSpaceChooser::set_output(const ColorTransform &out)
{
	QByteArray o, v, l;
	oak_color_transform pod = oak_to_transform(out, &o, &v, &l);
	int out_is_display = 0;
	char out_buf[256], view_buf[256], look_buf[256];
	oakengine_color_manager_compliant_transform(
		color_manager_, &pod, 0, &out_is_display, out_buf, sizeof(out_buf),
		view_buf, sizeof(view_buf), look_buf, sizeof(look_buf));

	display_combobox_->setCurrentText(QString::fromUtf8(out_buf));
	view_combobox_->setCurrentText(QString::fromUtf8(view_buf));

	QString look_str = QString::fromUtf8(look_buf);
	if (look_str.isEmpty()) {
		look_combobox_->setCurrentIndex(0);
	} else {
		look_combobox_->setCurrentText(look_str);
	}
}

void ColorSpaceChooser::update_views(const QString &display)
{
	QString v = view_combobox_->currentText();

	view_combobox_->clear();

	QByteArray disp = display.toUtf8();
	QStringList views = oak_query_string_list(
		[this, &disp]() {
			return oakengine_color_manager_view_count(color_manager_,
													  disp.constData());
		},
		[this, &disp](int i, char *buf, int size) {
			return oakengine_color_manager_view_at(color_manager_,
												   disp.constData(), i, buf,
												   size);
		});

	foreach (const QString &s, views) {
		view_combobox_->addItem(s);
	}

	if (views.contains(v)) {
		// If we have the view we had before, set it again
		view_combobox_->setCurrentText(v);
	} else {
		// Otherwise reset to default view for this display
		view_combobox_->setCurrentText(
			oak_query_string([this, &disp](char *buf, int size) {
				return oakengine_color_manager_default_view(
					color_manager_, disp.constData(), buf, size);
			}));
	}
}

void ColorSpaceChooser::combo_box_changed()
{
	if (sender() == display_combobox_) {
		update_views(display_combobox_->currentText());
	}

	if (input_combobox_) {
		emit input_color_space_changed(input());
	}

	if (display_combobox_) {
		emit output_color_space_changed(output());
	}

	if (input_combobox_ && display_combobox_) {
		emit color_space_changed(input(), output());
	}
}

}
