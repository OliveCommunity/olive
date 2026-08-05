/***

  Oak - Non-Linear Video Editor
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

#ifndef OAK_RENDER_PATHS_H
#define OAK_RENDER_PATHS_H

// Qt-free replacements for QCoreApplication::applicationDirPath(),
// applicationPid() and QDir::tempPath() used by the render core.

#include <cstdint>
#include <string>
#include <filesystem>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#endif

namespace olive
{

inline std::string application_dir_path()
{
#if defined(_WIN32)
	char buf[MAX_PATH];
	const DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
	if (n == 0) {
		return std::string();
	}
	return std::filesystem::path(std::string(buf, n)).parent_path().string();
#elif defined(__APPLE__)
	uint32_t size = 0;
	_NSGetExecutablePath(nullptr, &size);
	std::string buf(size, '\0');
	if (_NSGetExecutablePath(buf.data(), &size) != 0) {
		return std::string();
	}
	std::error_code ec;
	const std::string resolved =
		std::filesystem::weakly_canonical(buf.c_str(), ec).string();
	const std::string &use = ec ? buf : resolved;
	return std::filesystem::path(use).parent_path().string();
#else
	char buf[4096];
	const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
	if (n <= 0) {
		return std::string();
	}
	buf[n] = '\0';
	return std::filesystem::path(buf).parent_path().string();
#endif
}

inline int64_t application_pid()
{
#if defined(_WIN32)
	return int64_t(GetCurrentProcessId());
#else
	return int64_t(getpid());
#endif
}

inline std::string temp_dir_path()
{
	std::error_code ec;
	const std::string p = std::filesystem::temp_directory_path(ec).string();
	return ec ? std::string("/tmp") : p;
}

}

#endif // OAK_RENDER_PATHS_H
