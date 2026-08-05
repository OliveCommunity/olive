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

#include "common/dropworkflowbehavior.h"

#include <cstring>

#include "../src/dropworkflowbehavior.h"

static const char *behavior_name(int value)
{
	switch (value) {
	case olive::k_dws_ask:
		return "ASK";
	case olive::k_dws_auto:
		return "AUTO";
	case olive::k_dws_manual:
		return "MANUAL";
	case olive::k_dws_disable:
		return "DISABLE";
	}
	return "UNKNOWN";
}

int oakcommon_drop_workflow_behavior_is_valid(int value)
{
	switch (value) {
	case olive::k_dws_ask:
	case olive::k_dws_auto:
	case olive::k_dws_manual:
	case olive::k_dws_disable:
		return 1;
	}
	return 0;
}

int oakcommon_drop_workflow_behavior_name(int value, char *buf, int buf_size)
{
	try {
		const char *name = behavior_name(value);
		int needed = (int)strlen(name) + 1;

		if (buf && buf_size >= needed)
			memcpy(buf, name, needed);
		return needed;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}
