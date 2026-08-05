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

namespace olive
{

/**
 * @brief Severity levels for debug output.
 *
 * Replaces Qt's QtMsgType now that the debug handler no longer depends
 * on QDebug.
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
 */
void debug_handler(int level, const char *msg);

}

#endif // OAK_DEBUG_H
