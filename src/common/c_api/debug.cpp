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
