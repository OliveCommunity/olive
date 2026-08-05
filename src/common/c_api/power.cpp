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

#include "../../include/common/power.h"

#include "../src/power.h"

int oakcommon_power_ceil_to_power_of_2(uint32_t value, uint32_t *out)
{
	try {
		if (!out) {
			return OAKCOMMON_E_INVALID;
		}

		*out = olive::ceil_to_power_of_2(value);

		return OAKCOMMON_OK;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_power_floor_to_power_of_2(uint32_t value, uint32_t *out)
{
	try {
		if (!out) {
			return OAKCOMMON_E_INVALID;
		}

		*out = olive::floor_to_power_of_2(value);

		return OAKCOMMON_OK;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}
