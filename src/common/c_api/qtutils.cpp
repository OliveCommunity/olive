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

#include "common/qtutils.h"

#include "../src/qtutils.h"

int oakcommon_qtutils_ptr_to_value(void *ptr, uint64_t *out_value)
{
	if (!out_value) {
		return OAKCOMMON_E_INVALID;
	}

	try {
		*out_value = static_cast<uint64_t>(olive::QtUtils::ptr_to_value(ptr));
		return OAKCOMMON_OK;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_qtutils_value_to_ptr(uint64_t value, void **out_ptr)
{
	if (!out_ptr) {
		return OAKCOMMON_E_INVALID;
	}

	try {
		*out_ptr =
			olive::QtUtils::value_to_ptr<void>(static_cast<uintptr_t>(value));
		return OAKCOMMON_OK;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_qtutils_get_creation_date(const char *path, int64_t *out_secs)
{
	if (!path || !out_secs) {
		return OAKCOMMON_E_INVALID;
	}

	try {
		std::chrono::system_clock::time_point t =
			olive::QtUtils::get_creation_date(std::filesystem::path(path));

		if (t == std::chrono::system_clock::time_point{}) {
			return OAKCOMMON_E_NOT_FOUND;
		}

		*out_secs = static_cast<int64_t>(
			std::chrono::system_clock::to_time_t(t));
		return OAKCOMMON_OK;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}
