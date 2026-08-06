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

#include "common/debug.h"

#include <cstdarg>
#include <vector>

#include "../src/debug.h"

int oakcommon_debug_log(int level, const char *msg)
{
	if (!msg)
		return OAKCOMMON_E_INVALID;

	try {
		olive::debug_handler(level, msg);
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
	return OAKCOMMON_OK;
}

int oakcommon_debug_level_name(int level, char *buf, int buf_size)
{
	try {
		return olive::debug_level_name(level, buf, buf_size);
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_log(int level, const char *fmt, ...)
{
	if (!fmt)
		return OAKCOMMON_E_INVALID;

	va_list args;
	va_start(args, fmt);

	va_list sizing;
	va_copy(sizing, args);
	int needed = vsnprintf(nullptr, 0, fmt, sizing);
	va_end(sizing);

	if (needed < 0) {
		va_end(args);
		return OAKCOMMON_E_FAILED;
	}

	std::string msg;
	try {
		// Dynamically sized: arbitrary message length, no truncation,
		// no fixed stack buffer.
		std::vector<char> buf(static_cast<size_t>(needed) + 1);
		vsnprintf(buf.data(), buf.size(), fmt, args);
		msg.assign(buf.data(), static_cast<size_t>(needed));
	} catch (...) {
		va_end(args);
		return OAKCOMMON_E_FAILED;
	}
	va_end(args);

	try {
		olive::log_message(level, msg);
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
	return OAKCOMMON_OK;
}

int oakcommon_log_set_level(int level)
{
	if (level < OAKCOMMON_DEBUG_DEBUG || level > OAKCOMMON_DEBUG_FATAL)
		return OAKCOMMON_E_INVALID;

	try {
		olive::set_log_level(static_cast<olive::DebugLevel>(level));
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
	return OAKCOMMON_OK;
}

int oakcommon_log_get_level(int *out_level)
{
	if (!out_level)
		return OAKCOMMON_E_INVALID;

	try {
		*out_level = static_cast<int>(olive::get_log_level());
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
	return OAKCOMMON_OK;
}
