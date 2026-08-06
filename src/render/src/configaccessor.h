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

#ifndef OAK_RENDER_CONFIGACCESSOR_H
#define OAK_RENDER_CONFIGACCESSOR_H

/**
 * @brief Consumer-side shim over the oakcommon_config_* C ABI
 *
 * config 波次: the old engine/config/config.h (Qt) and the in-memory
 * transition/config/config.h stub are gone. This header keeps the
 * OAK_CONFIG(...)/Config::current()[...] call shape so call sites did
 * not change; every accessor is a C call into liboakcommon. Per the
 * cross-module rule only the oakcommon C API is used here, never the
 * ConfigStore C++ class.
 */

#include <cstdint>
#include <string>
#include <type_traits>

#include "common/config.h"

#ifndef QStringLiteral
#define QStringLiteral(x) x
#endif

namespace olive
{

/**
 * @brief Key-bound value proxy with the QVariant-subset accessors the
 * call sites use
 */
class ConfigValue {
public:
	ConfigValue() = default;
	explicit ConfigValue(const std::string &key)
		: key_(key)
	{
	}

	bool to_bool() const
	{
		return oakcommon_config_get_bool(nullptr, key_.c_str(), 0) != 0;
	}
	bool toBool() const { return to_bool(); }

	int to_int() const
	{
		return oakcommon_config_get_int(nullptr, key_.c_str(), 0);
	}
	int toInt() const { return to_int(); }

	int64_t to_long_long() const
	{
		return oakcommon_config_get_int64(nullptr, key_.c_str(), 0);
	}
	uint64_t to_u_long_long() const
	{
		return static_cast<uint64_t>(to_long_long());
	}
	uint64_t toULongLong() const { return to_u_long_long(); }

	double to_double() const
	{
		return oakcommon_config_get_double(nullptr, key_.c_str(), 0.0);
	}

	std::string to_string() const
	{
		const int size =
			oakcommon_config_get(nullptr, key_.c_str(), nullptr, 0);
		if (size <= 0) {
			return std::string();
		}
		std::string s(size - 1, '\0');
		oakcommon_config_get(nullptr, key_.c_str(), s.data(), size);
		return s;
	}
	std::string toString() const { return to_string(); }

	template <typename T> T value() const { return value_impl<T>(); }

	ConfigValue &operator=(const std::string &s)
	{
		oakcommon_config_set(nullptr, key_.c_str(), s.c_str());
		return *this;
	}
	ConfigValue &operator=(const char *s)
	{
		return *this = std::string(s);
	}

private:
	template <typename T>
	typename std::enable_if<std::is_integral<T>::value ||
								std::is_enum<T>::value,
							T>::type
	value_impl() const
	{
		return static_cast<T>(to_long_long());
	}

	template <typename T>
	typename std::enable_if<std::is_floating_point<T>::value, T>::type
	value_impl() const
	{
		return static_cast<T>(to_double());
	}

	/**
	 * @brief Rational and friends: parsed from the stored "num/den"
	 * string via the type's own static from_string.
	 */
	template <typename T>
	typename std::enable_if<!std::is_arithmetic<T>::value &&
								!std::is_enum<T>::value,
							T>::type
	value_impl() const
	{
		return T::from_string(to_string());
	}

	std::string key_;
};

class Config {
public:
	static Config &current()
	{
		static Config c;
		return c;
	}

	ConfigValue operator[](const std::string &key) const
	{
		return ConfigValue(key);
	}
};

} // namespace olive

#ifndef OAK_CONFIG
#define OAK_CONFIG(x) Config::current()[x]
#endif
#ifndef OAK_CONFIG_STR
#define OAK_CONFIG_STR(x) Config::current()[x]
#endif

#endif // OAK_RENDER_CONFIGACCESSOR_H
