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

#ifndef OAK_COLORSWATCHCHOOSER_H
#define OAK_COLORSWATCHCHOOSER_H

#include "node/color/colormanager/colormanager.h"
#include "widget/colorbutton/colorbutton.h"

namespace olive
{

class ColorSwatchChooser : public QWidget {
	Q_OBJECT
public:
	ColorSwatchChooser(ColorManager *manager, QWidget *parent = nullptr);

public slots:
	void set_current_color(const ManagedColor &c)
	{
		current_ = c;
	}

signals:
	void color_clicked(const ManagedColor &c);

private:
	void set_default_color(int index);

	static QString get_swatch_filename();

	void load_swatches();
	void save_swatches();

	static const int k_row_count = 4;
	static const int k_col_count = 8;
	static const int k_btn_count = k_row_count * k_col_count;
	ColorButton *buttons_[k_btn_count];

	ManagedColor current_;
	ColorButton *menu_btn_;

private slots:
	void handle_button_click();

	void handle_context_menu();

	void save_current_color();

	void reset_menu_button();
};

}

#endif // OAK_COLORSWATCHCHOOSER_H
