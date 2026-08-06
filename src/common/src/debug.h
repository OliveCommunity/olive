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

#ifndef OAK_DEBUG_H
#define OAK_DEBUG_H

#include <functional>
#include <string>

namespace olive
{

/**
 * @brief Severity levels for debug output.
 *
 * Replaces Qt's QtMsgType now that the debug handler no longer depends
 * on QDebug. Values are ordered by ascending severity so that a simple
 * `level < threshold` comparison implements level filtering.
 */
enum DebugLevel {
	k_debug_debug,
	k_debug_info,
	k_debug_warning,
	k_debug_error,
	k_debug_fatal
};

/**
 * @brief Write the printable name of a debug level (e.g. "WARNING") into
 * a caller-provided buffer.
 *
 * Two-stage form: pass `buf == NULL` or a too-small `buf_size` to query
 * the required size. Writes "UNKNOWN" for levels outside the DebugLevel
 * range.
 *
 * @return Required buffer size in bytes, including the terminating NUL.
 */
int debug_level_name(int level, char *buf, int buf_size);

/**
 * @brief Print a debug message to stderr, prefixed with its level.
 *
 * De-Qt replacement for the old Qt message handler: qDebug() output is
 * replaced by fprintf(stderr). A NULL message is treated as an empty
 * string. Lines are always flushed so messages appear immediately.
 *
 * This is the unfiltered low-level writer; use log_message() (or the
 * log_debug()/log_info()/... helpers) for level-filtered logging.
 */
void debug_handler(int level, const char *msg);

/**
 * @brief Format a log line as "[LEVEL] message\n".
 */
std::string format_log_line(int level, const std::string &msg);

/**
 * @brief Destination for filtered log lines.
 *
 * Receives the fully formatted line ("[LEVEL] message\n"). The default
 * sink writes to stderr and flushes, matching debug_handler().
 */
using LogSink = std::function<void(const std::string &line)>;

/**
 * @brief Install a custom log sink (e.g. for tests or log files).
 *
 * Passing an empty LogSink restores the default stderr sink.
 * Thread-safe; the sink is invoked without the internal lock held.
 */
void set_log_sink(LogSink sink);

/**
 * @brief Set the minimum level emitted by log_message().
 *
 * Messages with a lower level are dropped. The default is
 * k_debug_info. Values outside the DebugLevel range are ignored.
 * Thread-safe (atomic store).
 */
void set_log_level(DebugLevel level);

/**
 * @brief The current minimum level emitted by log_message().
 */
DebugLevel get_log_level();

/**
 * @brief Emit a message if `level` passes the current level filter.
 *
 * The formatted line ("[LEVEL] message\n") is handed to the installed
 * sink. Messages below the level set with set_log_level() are dropped.
 */
void log_message(int level, const std::string &msg);

/**
 * @brief Level-filtered convenience wrappers, replacing qDebug(),
 * qInfo(), qWarning() and qCritical(). Callers compose the message
 * themselves (plain std::string concatenation).
 */
void log_debug(const std::string &msg);
void log_info(const std::string &msg);
void log_warning(const std::string &msg);
void log_critical(const std::string &msg);

}

#endif // OAK_DEBUG_H
