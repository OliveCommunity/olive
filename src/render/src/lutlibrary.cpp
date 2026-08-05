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

#include "lutlibrary.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

#include "config/config.h"

namespace olive
{

namespace
{
std::string trim(const std::string &s)
{
	const char *ws = " \t\n\r";
	const std::string::size_type first = s.find_first_not_of(ws);
	if (first == std::string::npos) {
		return std::string();
	}
	const std::string::size_type last = s.find_last_not_of(ws);
	return s.substr(first, last - first + 1);
}

std::vector<std::string> split_skip_empty(const std::string &s, char sep)
{
	std::vector<std::string> parts;
	std::string::size_type start = 0;
	while (true) {
		const std::string::size_type pos = s.find(sep, start);
		const std::string part =
			s.substr(start, pos == std::string::npos ? pos : pos - start);
		if (!part.empty()) {
			parts.push_back(part);
		}
		if (pos == std::string::npos) {
			break;
		}
		start = pos + 1;
	}
	return parts;
}
}

const std::vector<std::string> &LUTLibrary::supported_extensions()
{
	// LUT formats OCIO FileTransform can load
	static const std::vector<std::string> extensions = {
		"cube", "3dl", "spi1d",
		"spi3d", "spimtx", "csp",
		"clf", "ctf", "cub",
	};
	return extensions;
}

bool LUTLibrary::is_supported_extension(const std::string &suffix)
{
	std::string s = suffix;
	if (!s.empty() && s.front() == '.') {
		s.erase(0, 1);
	}

	std::transform(s.begin(), s.end(), s.begin(),
				   [](unsigned char c) { return std::tolower(c); });

	const std::vector<std::string> &exts = supported_extensions();
	return std::find(exts.begin(), exts.end(), s) != exts.end();
}

std::vector<std::string> LUTLibrary::get_directories()
{
	const std::string serialized = OAK_CONFIG("LUTLibraryPaths").toString();

	std::vector<std::string> dirs = split_skip_empty(serialized, ';');
	for (std::string &dir : dirs) {
		// QDir::fromNativeSeparators is a no-op off Windows, so only the
		// trim from the original code remains here.
		dir = trim(dir);
	}
	return dirs;
}

void LUTLibrary::set_directories(const std::vector<std::string> &dirs)
{
	std::vector<std::string> cleaned;
	for (const std::string &dir : dirs) {
		const std::string trimmed = trim(dir);
		if (!trimmed.empty() &&
			std::find(cleaned.begin(), cleaned.end(), trimmed) ==
				cleaned.end()) {
			cleaned.push_back(trimmed);
		}
	}

	std::string joined;
	for (size_t i = 0; i < cleaned.size(); i++) {
		if (i > 0) {
			joined += ';';
		}
		joined += cleaned[i];
	}

	Config::current()["LUTLibraryPaths"] = joined;
}

std::vector<std::string> LUTLibrary::get_lut_files()
{
	std::vector<std::string> files;

	for (const std::string &dir : get_directories()) {
		std::error_code ec;
		std::filesystem::recursive_directory_iterator it(
			dir, std::filesystem::directory_options::none, ec);
		const std::filesystem::recursive_directory_iterator end;
		for (; !ec && it != end; it.increment(ec)) {
			if (!it->is_regular_file(ec)) {
				continue;
			}
			// Mirrors the original QDir name filters "*.cube" / "*.3dl"
			// (case-sensitive).
			const std::string ext = it->path().extension().string();
			if (ext == ".cube" || ext == ".3dl") {
				files.push_back(it->path().string());
			}
		}
	}

	return files;
}

}
