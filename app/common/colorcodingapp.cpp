/***

  Oak - Non-Linear Video Editor
  Copyright (C) 2026 Oak Team

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

#include "common/colorcodingapp.h"

#include <QObject>

namespace olive
{

QVector<Color> AppColorCoding::colors = {
	Color(0.545f, 0.255f, 0.255f), Color(0.412f, 0.188f, 0.259f),
	Color(0.561f, 0.427f, 0.239f), Color(0.486f, 0.306f, 0.235f),
	Color(0.631f, 0.612f, 0.212f), Color(0.404f, 0.478f, 0.243f),
	Color(0.349f, 0.576f, 0.275f), Color(0.224f, 0.459f, 0.251f),
	Color(0.259f, 0.471f, 0.541f), Color(0.184f, 0.376f, 0.329f),
	Color(0.259f, 0.365f, 0.541f), Color(0.196f, 0.216f, 0.412f),
	Color(0.612f, 0.294f, 0.502f), Color(0.404f, 0.220f, 0.459f),
	Color(0.800f, 0.800f, 0.800f), Color(0.502f, 0.502f, 0.502f)
};

const QVector<Color> &AppColorCoding::standard_colors()
{
	return colors;
}

QString AppColorCoding::get_color_name(int c)
{
	switch (c) {
	case k_red: return QObject::tr("Red");
	case k_maroon: return QObject::tr("Maroon");
	case k_orange: return QObject::tr("Orange");
	case k_brown: return QObject::tr("Brown");
	case k_yellow: return QObject::tr("Yellow");
	case k_olive: return QObject::tr("Oak");
	case k_lime: return QObject::tr("Lime");
	case k_green: return QObject::tr("Green");
	case k_cyan: return QObject::tr("Cyan");
	case k_teal: return QObject::tr("Teal");
	case k_blue: return QObject::tr("Blue");
	case k_navy: return QObject::tr("Navy");
	case k_pink: return QObject::tr("Pink");
	case k_purple: return QObject::tr("Purple");
	case k_silver: return QObject::tr("Silver");
	case k_gray: return QObject::tr("Gray");
	}
	return QString();
}

Color AppColorCoding::get_color(int c)
{
	return colors.at(c);
}

Qt::GlobalColor AppColorCoding::get_ui_selector_color(const Color &c)
{
	if (c.get_rough_luminance() > 0.40f) {
		return Qt::black;
	} else {
		return Qt::white;
	}
}

}
