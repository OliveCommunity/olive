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

#ifndef OAK_EDITOR_DEBUG_H
#define OAK_EDITOR_DEBUG_H

#include "common/error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Severity levels for oakcommon debug output.
 *
 * Mirrors olive::DebugLevel in src/common/src/debug.h.
 */
enum OakDebugLevel {
	OAKCOMMON_DEBUG_DEBUG = 0, /**< Verbose debug message. */
	OAKCOMMON_DEBUG_INFO = 1, /**< Informational message. */
	OAKCOMMON_DEBUG_WARNING = 2, /**< Warning message. */
	OAKCOMMON_DEBUG_ERROR = 3, /**< Error message. */
	OAKCOMMON_DEBUG_FATAL = 4 /**< Fatal error message. */
};

/**
 * @brief Print a debug message to stderr, prefixed with its level.
 *
 * De-Qt replacement for the old Qt message handler. The line is
 * flushed immediately.
 *
 * @param level One of OakDebugLevel; out-of-range values print
 * as "UNKNOWN".
 * @param msg NUL-terminated message; NULL is treated as an empty
 * string.
 * @return OAKCOMMON_OK on success, OAKCOMMON_E_INVALID if msg is NULL.
 */
int oakcommon_debug_log(int level, const char *msg);

/**
 * @brief Copy the printable name of a debug level into buf.
 *
 * Two-segment string getter: if buf is NULL or buf_size is too small,
 * nothing is written.
 *
 * @param level One of OakDebugLevel.
 * @param buf Destination buffer, may be NULL to query the size.
 * @param buf_size Size of buf in bytes.
 * @return Required buffer size in bytes including the terminating NUL
 * (non-negative).
 */
int oakcommon_debug_level_name(int level, char *buf, int buf_size);

/**
 * @brief printf-style filtered log, replacing qDebug()/qInfo()/
 * qWarning()/qCritical() call sites.
 *
 * The message is formatted with vsnprintf into a dynamically sized
 * buffer (arbitrary length, no truncation, no fixed stack buffer) and
 * emitted as "[LEVEL] message\n" unless @p level is below the current
 * filter level (see oakcommon_log_set_level()).
 *
 * @param level One of OakDebugLevel; out-of-range values print
 * as "UNKNOWN" and are never filtered out below FATAL.
 * @param fmt printf-style format string. Must not be NULL.
 * @return OAKCOMMON_OK on success, OAKCOMMON_E_INVALID if fmt is NULL,
 *         OAKCOMMON_E_FAILED if formatting failed.
 */
int oakcommon_log(int level, const char *fmt, ...);

/**
 * @brief Set the minimum level emitted by oakcommon_log().
 *
 * Messages with a lower level are dropped. The default is
 * OAKCOMMON_DEBUG_INFO.
 *
 * @param level One of OakDebugLevel.
 * @return OAKCOMMON_OK on success, OAKCOMMON_E_INVALID if level is
 *         outside the OakDebugLevel range.
 */
int oakcommon_log_set_level(int level);

/**
 * @brief Query the current minimum level emitted by oakcommon_log().
 *
 * @param out_level Receives one of OakDebugLevel. Must not be NULL.
 * @return OAKCOMMON_OK on success, OAKCOMMON_E_INVALID if out_level is
 *         NULL.
 */
int oakcommon_log_get_level(int *out_level);

#ifdef __cplusplus
}
#endif

#endif // OAK_EDITOR_DEBUG_H
