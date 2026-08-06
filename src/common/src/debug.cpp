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

#include "debug.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace olive
{

namespace
{

/**
 * @brief Minimum level emitted by log_message(); default k_debug_info.
 */
std::atomic<int> g_log_level{ k_debug_info };

/**
 * @brief Guards g_log_sink (std::function is not atomic).
 */
std::mutex g_sink_mutex;
LogSink g_log_sink;

/**
 * @brief Default sink: stderr + flush, same as debug_handler().
 */
void stderr_sink(const std::string &line)
{
	fputs(line.c_str(), stderr);
	fflush(stderr);
}

} // namespace

int debug_level_name(int level, char *buf, int buf_size)
{
	const char *name;

	switch (level) {
	case k_debug_debug:
		name = "DEBUG";
		break;
	case k_debug_info:
		name = "INFO";
		break;
	case k_debug_warning:
		name = "WARNING";
		break;
	case k_debug_error:
		name = "ERROR";
		break;
	case k_debug_fatal:
		name = "FATAL";
		break;
	default:
		name = "UNKNOWN";
		break;
	}

	int needed = (int)strlen(name) + 1;

	if (buf && buf_size >= needed)
		memcpy(buf, name, needed);
	return needed;
}

std::string format_log_line(int level, const std::string &msg)
{
	char level_name[16];

	debug_level_name(level, level_name, sizeof(level_name));

	std::string line;
	line.reserve(strlen(level_name) + msg.size() + 4);
	line += '[';
	line += level_name;
	line += "] ";
	line += msg;
	line += '\n';
	return line;
}

void debug_handler(int level, const char *msg)
{
	stderr_sink(format_log_line(level, msg ? msg : ""));
}

void set_log_sink(LogSink sink)
{
	std::lock_guard<std::mutex> lock(g_sink_mutex);
	g_log_sink = std::move(sink);
}

void set_log_level(DebugLevel level)
{
	if (level < k_debug_debug || level > k_debug_fatal)
		return;
	g_log_level.store(static_cast<int>(level), std::memory_order_relaxed);
}

DebugLevel get_log_level()
{
	return static_cast<DebugLevel>(g_log_level.load(std::memory_order_relaxed));
}

void log_message(int level, const std::string &msg)
{
	if (level < g_log_level.load(std::memory_order_relaxed))
		return;

	std::string line = format_log_line(level, msg);

	LogSink sink;
	{
		std::lock_guard<std::mutex> lock(g_sink_mutex);
		sink = g_log_sink;
	}
	// Invoke outside the lock so a sink may log re-entrantly.
	if (sink)
		sink(line);
	else
		stderr_sink(line);
}

void log_debug(const std::string &msg)
{
	log_message(k_debug_debug, msg);
}

void log_info(const std::string &msg)
{
	log_message(k_debug_info, msg);
}

void log_warning(const std::string &msg)
{
	log_message(k_debug_warning, msg);
}

void log_critical(const std::string &msg)
{
	log_message(k_debug_error, msg);
}

}
