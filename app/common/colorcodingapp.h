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

#ifndef OAK_COLORCODINGAPP_H
#define OAK_COLORCODINGAPP_H

#include <olive/core/core.h>
#include <QString>
#include <QVector>

namespace olive
{

using namespace core;

/**
 * @brief App-side color label mapping (moved from engine/ui/colorcoding.h)
 *
 * Provides the same static color-label mapping as the engine version but
 * without QObject inheritance (no moc symbols). Only the static methods
 * used by app code are included.
 */
class AppColorCoding {
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

	static const QVector<Color> &standard_colors();

private:
	static QVector<Color> colors;
};

} // namespace olive

#endif // OAK_COLORCODINGAPP_H
