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

#include "qtutils.h"

#include <sys/stat.h>

namespace olive
{

std::chrono::system_clock::time_point
QtUtils::get_creation_date(const std::filesystem::path &path)
{
	struct stat st;

	if (stat(path.c_str(), &st) != 0) {
		return std::chrono::system_clock::time_point{};
	}

#if defined(__APPLE__) || defined(__FreeBSD__)
	// Filesystems that record a birth time expose it as st_birthtimespec;
	// fall back to the metadata change time when it is not set.
	time_t t = st.st_birthtimespec.tv_sec;
	if (t == 0 || t == -1) {
		t = st.st_ctimespec.tv_sec;
	}
	return std::chrono::system_clock::from_time_t(t);
#else
	// Portable fallback: st_ctime (metadata change time), matching the
	// Qt implementation's birthTime()/metadataChangeTime() fallback.
	return std::chrono::system_clock::from_time_t(st.st_ctime);
#endif
}

}
