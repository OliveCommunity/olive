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

#ifndef OAKENGINE_CONFIG_H
#define OAKENGINE_CONFIG_H

#include <stdint.h>

#include "init.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file config.h
 * @brief C ABI for the engine configuration store (olive::Config).
 *
 * A thin facade over the QSettings-backed key/value store used by the editor
 * for persistent preferences. Only the types actually used by the UI are
 * exposed (string/int); the engine keeps ownership of the singleton.
 */

typedef void (*oakengine_config_error_fn)(const char *title,
										  const char *message,
										  void *userdata);

/**
 * @brief Load configuration from disk (Config::load).
 */
OAKENGINE_API int oakengine_config_load(void);

/**
 * @brief Save configuration to disk (Config::save).
 */
OAKENGINE_API int oakengine_config_save(void);

/**
 * @brief Read a string value (buf/size convention).
 *
 * @return the string length on success, 0 when the key is missing or empty,
 *         or a negative OAKENGINE_E_* code on error.
 */
OAKENGINE_API int oakengine_config_get_string(const char *key, char *buf,
											  int buf_size);

/**
 * @brief Write a string value.
 */
OAKENGINE_API int oakengine_config_set_string(const char *key,
											  const char *value);

/**
 * @brief Read an integer value. Returns `default_value` when the key is
 * missing or not convertible to int.
 */
OAKENGINE_API int64_t oakengine_config_get_int(const char *key,
											   int64_t default_value);

/**
 * @brief Write an integer value.
 */
OAKENGINE_API int oakengine_config_set_int(const char *key, int64_t value);

/**
 * @brief Register a callback for configuration errors (e.g. disk write
 * failures). Passing NULL clears the handler.
 */
OAKENGINE_API int oakengine_config_set_error_handler(
		oakengine_config_error_fn fn, void *userdata);

/**
 * @brief Report an error through the registered handler. If no handler is
 * set the error is logged and discarded.
 */
OAKENGINE_API int oakengine_config_report_error(const char *title,
												const char *message);

#ifdef __cplusplus
}
#endif

#endif /* OAKENGINE_CONFIG_H */
