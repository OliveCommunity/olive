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

#ifndef OAK_STYLEMANAGER_H
#define OAK_STYLEMANAGER_H

#include <QSettings>
#include <QWidget>

#include "common/define.h"

namespace olive
{

class StyleManager : public QObject {
public:
	static void init();

	static const QString &get_style();

	static void set_style(const QString &style_path);

	inline static const char *k_default_style = "olive-dark";

	static const QMap<QString, QString> &available_themes()
	{
		return available_themes_;
	}

private:
	static QPalette parse_palette(const QString &ini_path);

	static void parse_palette_group(QSettings *ini, QPalette *palette,
								  QPalette::ColorGroup group);

	static void parse_palette_color(QSettings *ini, QPalette *palette,
								  QPalette::ColorGroup group,
								  const QString &role_name);

	static QString current_style;

	static QMap<QString, QString> available_themes_;
};

}

#endif // OAK_STYLEMANAGER_H
