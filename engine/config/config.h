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

#ifndef OAK_CONFIG_H
#define OAK_CONFIG_H

#include <QMap>
#include <QString>
#include <QVariant>

#include "node/value.h"

namespace olive
{

#ifndef OAK_CONFIG
#define OAK_CONFIG(x) Config::current()[QStringLiteral(x)]
#endif
#ifndef OAK_CONFIG_STR
#define OAK_CONFIG_STR(x) Config::current()[x]
#endif

class Config {
public:
	static Config &current();

	void set_defaults();

	static void load();

	static void save();

	/**
	 * @brief Handler for configuration errors that should be shown to the user
	 *
	 * The engine layer cannot show dialogs itself. The UI registers a
	 * handler (e.g. QMessageBox-based) at startup; without one, errors go
	 * to the log instead.
	 */
	using ErrorHandler = void (*)(const QString &title,
								  const QString &message);

	static void set_error_handler(ErrorHandler handler);

	/**
	 * @brief Report an error through the registered error handler
	 *
	 * Public so engine-layer code (e.g. EngineCore) can surface errors to
	 * the user without depending on the UI itself.
	 */
	static void report_error(const QString &title, const QString &message);

	QVariant operator[](const QString &) const;

	QVariant &operator[](const QString &);

	NodeValue::Type get_config_entry_type(const QString &key) const;

private:
	Config();

	struct ConfigEntry {
		NodeValue::Type type;
		QVariant data;
	};

	void set_entry_internal(const QString &key, NodeValue::Type type,
						  const QVariant &data);

	QMap<QString, ConfigEntry> config_map_;

	static Config current_config;

	static ErrorHandler error_handler_;

	static QString get_config_file_path();
};

}

#endif // OAK_CONFIG_H
