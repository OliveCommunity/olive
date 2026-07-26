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

#include "colorswatchchooser.h"

#include <QGridLayout>

#include "common/filefunctions.h"
#include "widget/menu/menu.h"

namespace olive
{

const int k_default_color_count = 16;
const Color k_default_colors[k_default_color_count] = {
	Color(1.0, 1.0, 1.0),	 Color(1.0, 1.0, 0.0),	Color(1.0, 0.5, 0.0),
	Color(1.0, 0.0, 0.0),	 Color(1.0, 0.0, 1.0),	Color(0.5, 0.0, 1.0),
	Color(0.0, 0.0, 1.0),	 Color(0.0, 0.5, 1.0),	Color(0.0, 1.0, 0.0),
	Color(0.0, 0.5, 0.0),	 Color(0.5, 0.25, 0.0), Color(0.75, 0.5, 0.25),
	Color(0.75, 0.75, 0.75), Color(0.5, 0.5, 0.5),	Color(0.25, 0.25, 0.25),
	Color(0.0, 0.0, 0.0)
};

ColorSwatchChooser::ColorSwatchChooser(OakEngineColorManager *manager, QWidget *parent)
	: QWidget(parent)
{
	auto layout = new QGridLayout(this);

	for (int x = 0; x < k_col_count; x++) {
		for (int y = 0; y < k_row_count; y++) {
			// Create button
			auto b = new ColorButton(manager, false);
			b->setFixedWidth(b->sizeHint().height() / 2 * 3);
			b->setContextMenuPolicy(Qt::CustomContextMenu);
			layout->addWidget(b, y, x);

			// Save button in buttons array
			int btn_index = x + k_col_count * y;
			buttons_[btn_index] = b;

			// Set default color
			set_default_color(btn_index);

			// Connect clicks
			connect(b, &ColorButton::clicked, this,
					&ColorSwatchChooser::handle_button_click);
			connect(b, &ColorButton::customContextMenuRequested, this,
					&ColorSwatchChooser::handle_context_menu);
		}
	}

	load_swatches();
}

void ColorSwatchChooser::set_default_color(int index)
{
	if (index < k_default_color_count) {
		buttons_[index]->set_color(k_default_colors[index]);
	} else {
		buttons_[index]->set_color(Color(1.0, 1.0, 1.0));
	}
}

void ColorSwatchChooser::handle_button_click()
{
	auto b = static_cast<ColorButton *>(sender());

	emit color_clicked(b->get_color());
	set_current_color(b->get_color());
}

void ColorSwatchChooser::handle_context_menu()
{
	Menu m(this);

	auto save_action = m.addAction(tr("Save Color Here"));
	connect(save_action, &QAction::triggered, this,
			&ColorSwatchChooser::save_current_color);

	m.addSeparator();

	auto reset_action = m.addAction(tr("Reset To Default"));
	connect(reset_action, &QAction::triggered, this,
			&ColorSwatchChooser::reset_menu_button);

	menu_btn_ = static_cast<ColorButton *>(sender());

	m.exec(QCursor::pos());
}

void ColorSwatchChooser::save_current_color()
{
	menu_btn_->set_color(current_);

	save_swatches();
}

void ColorSwatchChooser::reset_menu_button()
{
	for (int i = 0; i < k_btn_count; i++) {
		if (buttons_[i] == menu_btn_) {
			set_default_color(i);
			break;
		}
	}
}

QString ColorSwatchChooser::get_swatch_filename()
{
	return QDir(FileFunctions::get_configuration_location())
		.filePath(QStringLiteral("swatch"));
}

void ColorSwatchChooser::load_swatches()
{
	QFile f(get_swatch_filename());
	if (f.open(QFile::ReadOnly)) {
		QDataStream d(&f);

		uint version;
		d >> version;

		if (version == 1) {
			int index = 0;
			while (index < k_btn_count && !d.atEnd()) {
				Color::DataType r;
				QString s;
				ManagedColor c;
				ColorTransform t;
				bool is_display;

				c.set_alpha(1.0);

				d >> r;
				c.set_red(r);

				d >> r;
				c.set_green(r);

				d >> r;
				c.set_blue(r);

				d >> s;
				c.set_color_input(s);

				d >> is_display;
				if (is_display) {
					QString display, view, look;
					d >> display;
					d >> view;
					d >> look;
					c.set_color_output(ColorTransform(display, view, look));
				} else {
					d >> s;
					c.set_color_output(ColorTransform(s));
				}

				buttons_[index]->set_color(c);

				index++;
			}
		}

		f.close();
	}
}

void ColorSwatchChooser::save_swatches()
{
	QString fn = get_swatch_filename();
	QFile f(fn);

	if (f.open(QFile::WriteOnly)) {
		QDataStream d(&f);

		const uint version = 1;
		d << version;

		for (int i = 0; i < k_btn_count; i++) {
			const ManagedColor &c = buttons_[i]->get_color();
			d << c.red();
			d << c.green();
			d << c.blue();
			d << c.color_input();
			d << c.color_output().is_display();

			if (c.color_output().is_display()) {
				d << c.color_output().display();
				d << c.color_output().view();
				d << c.color_output().look();
			} else {
				d << c.color_output().output();
			}
		}

		f.close();
	} else {
		qCritical() << "Failed to open swatch file" << fn << "for writing";
	}
}

}
