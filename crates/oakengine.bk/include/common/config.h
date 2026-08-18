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

#ifndef OAK_EDITOR_COMMON_CONFIG_H
#define OAK_EDITOR_COMMON_CONFIG_H

#include <stdint.h>

#include "common/error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief De-Qt application configuration store, C ABI
 * (M1-oakcommon.md §2.1, extended for the real consumer surface)
 *
 * oakcommon_config is a process-wide singleton key/value store (the de-Qt
 * replacement for engine/config/config.h's QSettings/QVariant wrapper).
 * Per the config-wave ruling it is NOT wrapped in the refcounted-handle
 * convention of common/handle.h: there is exactly one store per process,
 * so the family is a plain set of functions over that singleton (same
 * singleton precedent as OakCurrent).
 *
 * Keys follow the frozen (group, key) convention of §2.1 and keep the
 * QSettings INI shape: pass group == NULL (or "") for a top-level key,
 * otherwise the entry is stored under an INI [group] section and
 * addressed as "group/key" internally.
 *
 * Values are typed (string / int64 / double / bool). Rational settings
 * are stored as strings in the "num/den" form used by
 * oakcore_rational_to_string(). Typed getters take a fallback which is
 * returned when the key is absent or has a different type (§2.1 special
 * convention: they return values, not error codes).
 *
 * Persistence is an INI file at
 * <FileFunctions::get_configuration_location()>/config.ini. The store
 * starts up with compiled-in defaults; oakcommon_config_load() re-reads
 * the file (a missing file is not an error) and oakcommon_config_save()
 * writes it. The OAK_CONFIG_DIR environment override honored by
 * get_configuration_location() also redirects this file (tests/tooling).
 *
 * NOTE (behavior change): the old Qt implementation persisted to
 * config.xml (engine XML) — and on macOS QSettings used a plist — so
 * previously saved settings do NOT carry over; the first run starts from
 * the compiled-in defaults.
 */

typedef enum OakCommonConfigEntryType {
	OAKCOMMON_CONFIG_ENTRY_NONE = 0, /**< No entry / null type. */
	OAKCOMMON_CONFIG_ENTRY_STRING = 1,
	OAKCOMMON_CONFIG_ENTRY_INT = 2,
	OAKCOMMON_CONFIG_ENTRY_DOUBLE = 3,
	OAKCOMMON_CONFIG_ENTRY_BOOL = 4
} OakCommonConfigEntryType;

/**
 * @brief Handler for configuration errors that should be shown to the user
 *
 * The engine layer cannot show dialogs itself. The UI registers a handler
 * (e.g. QMessageBox-based) at startup; without one, errors go to stderr.
 * Same injection pattern as the codec task-submit callback.
 */
typedef void (*OakCommonConfigErrorHandler)(const char *title,
											const char *message,
											void *userdata);

/**
 * @brief Resets the store to compiled-in defaults and loads config.ini
 *
 * A missing file leaves the defaults in place and returns OAKCOMMON_OK.
 * Malformed lines are skipped. An unreadable existing file is reported
 * through the error handler and returns OAKCOMMON_E_FAILED.
 */
int oakcommon_config_load(void);

/**
 * @brief Writes the current store to config.ini (via a temp file + rename)
 *
 * On failure the error handler is invoked and OAKCOMMON_E_FAILED is
 * returned.
 */
int oakcommon_config_save(void);

/**
 * @brief Resets the store to compiled-in defaults (drops custom keys)
 */
int oakcommon_config_reset_defaults(void);

/**
 * @brief Sets a string entry (§2.1)
 *
 * A new key is created as OAKCOMMON_CONFIG_ENTRY_STRING. Setting an
 * existing typed (INT/DOUBLE/BOOL) entry parses the string into its
 * declared type; an unparseable value returns OAKCOMMON_E_STATE and
 * leaves the entry unchanged.
 */
void oakcommon_config_set(const char *group, const char *key,
						  const char *value_utf8);

/**
 * @brief Reads an entry as a string, two-stage buffer (§2.1)
 *
 * Numeric/bool entries are formatted (bools as "true"/"false", doubles
 * with %g).
 *
 * @return Required buffer size in bytes (including the terminating NUL),
 * or a negative OAKCOMMON_E_* error code (OAKCOMMON_E_NOT_FOUND when the
 * key is absent).
 */
int oakcommon_config_get(const char *group, const char *key, char *buf,
						 int buf_size);

/**
 * @brief Reads an INT entry as int (§2.1)
 *
 * @return The stored value, or `fallback` when the key is absent or has
 * a different type.
 */
int oakcommon_config_get_int(const char *group, const char *key,
							 int fallback);

/**
 * @brief Reads a DOUBLE entry (§2.1), fallback semantics as get_int
 */
double oakcommon_config_get_double(const char *group, const char *key,
								   double fallback);

/**
 * @brief Sets an INT entry (32-bit, §2.1)
 */
void oakcommon_config_set_int(const char *group, const char *key, int v);

/**
 * @brief INT entry as int64 (extension for channel-layout style values)
 */
int64_t oakcommon_config_get_int64(const char *group, const char *key,
								   int64_t fallback);
void oakcommon_config_set_int64(const char *group, const char *key,
								int64_t v);

/**
 * @brief BOOL entry as int 0/1 (extension), fallback semantics as get_int
 */
int oakcommon_config_get_bool(const char *group, const char *key,
							  int fallback);
void oakcommon_config_set_bool(const char *group, const char *key, int v);

/**
 * @brief Sets a DOUBLE entry (extension)
 */
void oakcommon_config_set_double(const char *group, const char *key,
								 double v);

/**
 * @brief Returns the OakCommonConfigEntryType of a key, or a negative
 * OAKCOMMON_E_* error (OAKCOMMON_E_NOT_FOUND when the key is absent)
 */
int oakcommon_config_entry_type(const char *group, const char *key);

/**
 * @brief Registers (or clears, with NULL) the error handler
 */
int oakcommon_config_set_error_handler(OakCommonConfigErrorHandler handler,
									   void *userdata);

#ifdef __cplusplus
}
#endif

#endif // OAK_EDITOR_COMMON_CONFIG_H
