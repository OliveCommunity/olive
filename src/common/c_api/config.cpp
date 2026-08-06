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

#include "common/config.h"

#include <cstring>
#include <mutex>

#include "../src/configstore.h"

namespace
{

std::mutex handler_mutex;
OakCommonConfigErrorHandler error_handler = nullptr;
void *error_handler_userdata = nullptr;

bool is_valid_key(const char *key)
{
	return key != nullptr && key[0] != '\0';
}

bool is_valid_string_out(char *buf, int buf_size)
{
	return buf_size >= 0 && (buf_size == 0 || buf != nullptr);
}

int write_string_result(const std::string &value, char *buf, int buf_size)
{
	int required = static_cast<int>(value.size()) + 1;
	if (buf != nullptr && buf_size >= required) {
		memcpy(buf, value.c_str(), required);
	}
	return required;
}

int to_c_type(ConfigStore::Type type)
{
	switch (type) {
	case ConfigStore::Type::k_string:
		return OAKCOMMON_CONFIG_ENTRY_STRING;
	case ConfigStore::Type::k_int:
		return OAKCOMMON_CONFIG_ENTRY_INT;
	case ConfigStore::Type::k_double:
		return OAKCOMMON_CONFIG_ENTRY_DOUBLE;
	case ConfigStore::Type::k_bool:
		return OAKCOMMON_CONFIG_ENTRY_BOOL;
	default:
		return OAKCOMMON_CONFIG_ENTRY_NONE;
	}
}

} // namespace

int oakcommon_config_load(void)
{
	try {
		return ConfigStore::current().load() ? OAKCOMMON_OK
										  : OAKCOMMON_E_FAILED;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_config_save(void)
{
	try {
		return ConfigStore::current().save() ? OAKCOMMON_OK
										  : OAKCOMMON_E_FAILED;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_config_reset_defaults(void)
{
	try {
		ConfigStore::current().set_defaults();
		return OAKCOMMON_OK;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

void oakcommon_config_set(const char *group, const char *key,
						  const char *value_utf8)
{
	if (!is_valid_key(key) || value_utf8 == nullptr) {
		return;
	}

	try {
		ConfigStore &store = ConfigStore::current();
		const std::string joined = ConfigStore::join_key(group, key);
		const ConfigStore::Entry *existing = store.get(joined);
		if (existing == nullptr ||
		    existing->type == ConfigStore::Type::k_string) {
			ConfigStore::Entry e;
			e.type = ConfigStore::Type::k_string;
			e.string_value = value_utf8;
			store.set(joined, e);
			return;
		}

		// Existing typed entry: parse the string into its declared type;
		// an unparseable value leaves the entry unchanged.
		ConfigStore::Entry parsed;
		if (ConfigStore::string_to_value(value_utf8, existing->type,
										&parsed)) {
			store.set(joined, parsed);
		}
	} catch (...) {
		// §2.1 setters return void; allocation failures are swallowed.
	}
}

int oakcommon_config_get(const char *group, const char *key, char *buf,
						 int buf_size)
{
	if (!is_valid_key(key) || !is_valid_string_out(buf, buf_size)) {
		return OAKCOMMON_E_INVALID;
	}

	try {
		const ConfigStore::Entry *entry =
			ConfigStore::current().get(ConfigStore::join_key(group, key));
		if (entry == nullptr) {
			return OAKCOMMON_E_NOT_FOUND;
		}
		return write_string_result(ConfigStore::value_to_string(*entry), buf,
								   buf_size);
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_config_get_int(const char *group, const char *key,
							 int fallback)
{
	return static_cast<int>(
		oakcommon_config_get_int64(group, key, fallback));
}

int64_t oakcommon_config_get_int64(const char *group, const char *key,
								   int64_t fallback)
{
	if (!is_valid_key(key)) {
		return fallback;
	}

	try {
		const ConfigStore::Entry *entry =
			ConfigStore::current().get(ConfigStore::join_key(group, key));
		if (entry == nullptr || entry->type != ConfigStore::Type::k_int) {
			return fallback;
		}
		return entry->int_value;
	} catch (...) {
		return fallback;
	}
}

void oakcommon_config_set_int(const char *group, const char *key, int v)
{
	oakcommon_config_set_int64(group, key, v);
}

void oakcommon_config_set_int64(const char *group, const char *key,
								int64_t v)
{
	if (!is_valid_key(key)) {
		return;
	}

	try {
		ConfigStore::Entry e;
		e.type = ConfigStore::Type::k_int;
		e.int_value = v;
		ConfigStore::current().set(ConfigStore::join_key(group, key), e);
	} catch (...) {
	}
}

double oakcommon_config_get_double(const char *group, const char *key,
								   double fallback)
{
	if (!is_valid_key(key)) {
		return fallback;
	}

	try {
		const ConfigStore::Entry *entry =
			ConfigStore::current().get(ConfigStore::join_key(group, key));
		if (entry == nullptr || entry->type != ConfigStore::Type::k_double) {
			return fallback;
		}
		return entry->double_value;
	} catch (...) {
		return fallback;
	}
}

void oakcommon_config_set_double(const char *group, const char *key,
								 double v)
{
	if (!is_valid_key(key)) {
		return;
	}

	try {
		ConfigStore::Entry e;
		e.type = ConfigStore::Type::k_double;
		e.double_value = v;
		ConfigStore::current().set(ConfigStore::join_key(group, key), e);
	} catch (...) {
	}
}

int oakcommon_config_get_bool(const char *group, const char *key,
							  int fallback)
{
	if (!is_valid_key(key)) {
		return fallback;
	}

	try {
		const ConfigStore::Entry *entry =
			ConfigStore::current().get(ConfigStore::join_key(group, key));
		if (entry == nullptr || entry->type != ConfigStore::Type::k_bool) {
			return fallback;
		}
		return entry->bool_value ? 1 : 0;
	} catch (...) {
		return fallback;
	}
}

void oakcommon_config_set_bool(const char *group, const char *key, int v)
{
	if (!is_valid_key(key)) {
		return;
	}

	try {
		ConfigStore::Entry e;
		e.type = ConfigStore::Type::k_bool;
		e.bool_value = v != 0;
		ConfigStore::current().set(ConfigStore::join_key(group, key), e);
	} catch (...) {
	}
}

int oakcommon_config_entry_type(const char *group, const char *key)
{
	if (!is_valid_key(key)) {
		return OAKCOMMON_E_INVALID;
	}

	try {
		const ConfigStore::Entry *entry =
			ConfigStore::current().get(ConfigStore::join_key(group, key));
		if (entry == nullptr) {
			return OAKCOMMON_E_NOT_FOUND;
		}
		return to_c_type(entry->type);
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_config_set_error_handler(OakCommonConfigErrorHandler handler,
									   void *userdata)
{
	try {
		{
			std::lock_guard<std::mutex> lock(handler_mutex);
			error_handler = handler;
			error_handler_userdata = userdata;
		}

		if (handler != nullptr) {
			ConfigStore::set_error_handler(
				[](const std::string &title, const std::string &message) {
					std::lock_guard<std::mutex> lock(handler_mutex);
					if (error_handler != nullptr) {
						error_handler(title.c_str(), message.c_str(),
									  error_handler_userdata);
					}
				});
		} else {
			ConfigStore::set_error_handler(nullptr);
		}
		return OAKCOMMON_OK;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}
