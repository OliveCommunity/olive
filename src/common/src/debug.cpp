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

#include <cstdio>
#include <cstring>

namespace olive
{

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

void debug_handler(int level, const char *msg)
{
	char level_name[16];

	debug_level_name(level, level_name, sizeof(level_name));
	fprintf(stderr, "[%s] %s\n", level_name, msg ? msg : "");

	// Always flush so debug messages appear immediately, even on
	// platforms that buffer stderr.
	fflush(stderr);
}

}
