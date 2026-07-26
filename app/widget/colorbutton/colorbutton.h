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

#ifndef OAK_COLORBUTTON_H
#define OAK_COLORBUTTON_H

#include <QPushButton>

#include "oakengine/color.h"
#include "widget/manageddisplay/colorprocessorhandle.h"

namespace olive
{

class ColorButton : public QPushButton {
	Q_OBJECT
public:
	ColorButton(OakEngineColorManager *color_manager, bool show_dialog_on_click,
				QWidget *parent = nullptr);
	ColorButton(OakEngineColorManager *color_manager, QWidget *parent = nullptr)
		: ColorButton(color_manager, true, parent)
	{
	}

	const ManagedColor &get_color() const;

public slots:
	void set_color(const ManagedColor &c);

signals:
	void color_changed(const ManagedColor &c);

private slots:
	void show_color_dialog();

	void color_dialog_finished(int e);

private:
	void update_color();

	OakEngineColorManager *color_manager_;

	ManagedColor color_;

	ColorProcessorHandlePtr color_processor_;

	bool dialog_open_;
};

}

#endif // OAK_COLORBUTTON_H
