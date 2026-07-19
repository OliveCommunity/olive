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

#ifndef OAK_COLORCODING_H
#define OAK_COLORCODING_H

#include <olive/core/core.h>
#include <QObject>

namespace olive
{

using namespace core;

class ColorCoding : public QObject {
	Q_OBJECT
public:
	enum Code {
		k_red,
		k_maroon,
		k_orange,
		k_brown,
		k_yellow,
		k_olive,
		k_lime,
		k_green,
		k_cyan,
		k_teal,
		k_blue,
		k_navy,
		k_pink,
		k_purple,
		k_silver,
		k_gray
	};

	static QString get_color_name(int c);

	static Color get_color(int c);

	static Qt::GlobalColor get_ui_selector_color(const Color &c);

	static const QVector<Color> &standard_colors()
	{
		return colors;
	}

private:
	static QVector<Color> colors;
};

}

#endif // OAK_COLORCODING_H
