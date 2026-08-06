/***

  Oak Video Editor - Non-Linear Video Editor
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

#ifndef OAK_CONFIGSTORE_H
#define OAK_CONFIGSTORE_H

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>

/**
 * @brief De-Qt application configuration store (process singleton)
 *
 * Replacement for the Qt-based olive::Config (engine/config/config.h):
 * QMap/QVariant became std::map + a small typed value, QSettings became a
 * self-written INI file, and NodeValue::Type became ConfigStore::Type so
 * config no longer depends on the node module.
 *
 * Keys keep the QSettings INI shape: "group/key" maps to an INI [group]
 * section; flat keys stay at the top level. The file lives at
 * <FileFunctions::get_configuration_location()>/config.ini (the
 * OAK_CONFIG_DIR override applies, which is what tests use).
 *
 * All public methods are thread-safe (single mutex).
 */
class ConfigStore {
public:
	enum class Type { k_none, k_string, k_int, k_double, k_bool };

	struct Entry {
		Type type = Type::k_none;
		std::string string_value;
		int64_t int_value = 0;
		double double_value = 0.0;
		bool bool_value = false;
	};

	using ErrorHandler =
		std::function<void(const std::string &title, const std::string &message)>;

	static ConfigStore &current();

	/**
	 * @brief Resets the store to compiled-in defaults (drops custom keys)
	 */
	void set_defaults();

	/**
	 * @brief Resets to defaults, then applies config.ini if it exists
	 *
	 * @return false when the file exists but could not be read (the error
	 * is also reported through the registered error handler).
	 */
	bool load();

	/**
	 * @brief Writes the store to config.ini via temp file + rename
	 *
	 * @return false on failure (also reported through the error handler).
	 */
	bool save();

	/**
	 * @brief Returns the entry for key, or nullptr when absent
	 *
	 * Keys are the joined "group/key" form (or the bare key when group is
	 * null/empty).
	 */
	const Entry *get(const std::string &key) const;

	/**
	 * @brief Creates or replaces an entry
	 */
	void set(const std::string &key, const Entry &entry);

	static void set_error_handler(ErrorHandler handler);
	static void report_error(const std::string &title,
							 const std::string &message);

	/**
	 * @brief <get_configuration_location()>/config.ini
	 */
	static std::string get_config_file_path();

	/**
	 * @brief Joins group and key into the stored "group/key" form
	 */
	static std::string join_key(const char *group, const char *key);

	/**
	 * @brief Serializes an entry for the INI file / string getter
	 */
	static std::string value_to_string(const Entry &entry);

	/**
	 * @brief Parses text into an entry of the given type
	 *
	 * @return false when the text cannot be parsed as the requested type
	 * (strings always parse).
	 */
	static bool string_to_value(const std::string &text, Type type,
								Entry *out);

private:
	ConfigStore();

	std::map<std::string, Entry> config_map_;
	mutable std::mutex mutex_;

	static ErrorHandler error_handler_;
};

#endif // OAK_CONFIGSTORE_H
