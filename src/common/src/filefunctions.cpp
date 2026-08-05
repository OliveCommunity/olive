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

#include "filefunctions.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace fs = std::filesystem;

namespace
{

/**
 * @brief FNV-1a hash of a string, returned as lowercase hex
 */
std::string fnv1a_hex(const std::string &data)
{
	uint64_t hash = 14695981039346656037ULL;
	for (unsigned char c : data) {
		hash ^= c;
		hash *= 1099511628211ULL;
	}

	char buf[17];
	snprintf(buf, sizeof(buf), "%016llx",
		 static_cast<unsigned long long>(hash));
	return buf;
}

/**
 * @brief Case-insensitive check whether `s` ends with `suffix`
 */
bool ends_with_case_insensitive(const std::string &s, const std::string &suffix)
{
	if (suffix.size() > s.size()) {
		return false;
	}

	size_t offset = s.size() - suffix.size();
	for (size_t i = 0; i < suffix.size(); i++) {
		char a = s[offset + i];
		char b = suffix[i];
		if (a >= 'A' && a <= 'Z') {
			a += 'a' - 'A';
		}
		if (b >= 'A' && b <= 'Z') {
			b += 'a' - 'A';
		}
		if (a != b) {
			return false;
		}
	}

	return true;
}

} // namespace

std::string FileFunctions::get_unique_file_identifier(
	const std::string &filename)
{
	std::error_code ec;
	fs::path abs = fs::absolute(filename, ec);
	if (ec || !fs::exists(abs, ec)) {
		return std::string();
	}

	auto mtime = fs::last_write_time(abs, ec);
	if (ec) {
		return std::string();
	}

	std::string hash_input = abs.string();
	hash_input.append(std::to_string(
		static_cast<long long>(mtime.time_since_epoch().count())));

	return fnv1a_hex(hash_input);
}

std::string FileFunctions::get_configuration_location()
{
	// Tests and tooling can redirect the configuration (and, since most
	// locations derive from it, the cache/data) root.
	const char *override_dir = getenv("OAK_CONFIG_DIR");
	if (override_dir != nullptr && override_dir[0] != '\0') {
		std::error_code ec;
		fs::create_directories(override_dir, ec);
		return override_dir;
	}

	if (is_portable()) {
		return get_application_path();
	}

	fs::path config_root;
#ifdef __APPLE__
	const char *home = getenv("HOME");
	if (home != nullptr && home[0] != '\0') {
		config_root = fs::path(home) / "Library" / "Application Support";
	}
#else
	const char *xdg_config = getenv("XDG_CONFIG_HOME");
	if (xdg_config != nullptr && xdg_config[0] != '\0') {
		config_root = xdg_config;
	} else {
		const char *home = getenv("HOME");
		if (home != nullptr && home[0] != '\0') {
			config_root = fs::path(home) / ".config";
		}
	}
#endif

	if (config_root.empty()) {
		// Last resort: temp directory
		config_root = fs::temp_directory_path();
	}

	fs::path config_dir = config_root / "oak";
	std::error_code ec;
	fs::create_directories(config_dir, ec);
	return config_dir.string();
}

bool FileFunctions::is_portable()
{
	std::error_code ec;
	return fs::exists(fs::path(get_application_path()) / "portable", ec);
}

std::string FileFunctions::get_application_path()
{
#ifdef __APPLE__
	uint32_t size = 0;
	_NSGetExecutablePath(nullptr, &size);
	std::string buf(size, '\0');
	if (_NSGetExecutablePath(buf.data(), &size) == 0) {
		std::error_code ec;
		fs::path p = fs::weakly_canonical(buf, ec);
		if (!ec) {
			return p.parent_path().string();
		}
	}
#elif defined(__linux__)
	std::error_code ec;
	fs::path p = fs::read_symlink("/proc/self/exe", ec);
	if (!ec) {
		return p.parent_path().string();
	}
#endif

	// Fallback: current working directory
	std::error_code ec;
	fs::path cwd = fs::current_path(ec);
	return ec ? std::string() : cwd.string();
}

std::string FileFunctions::get_temp_file_path()
{
	std::error_code ec;
	fs::path temp_path = fs::temp_directory_path(ec) / "oak";
	if (ec) {
		temp_path = fs::path(".") / "oak-temp";
	}

	// Ensure it exists
	fs::create_directories(temp_path, ec);

	return temp_path.string();
}

bool FileFunctions::can_copy_directory_without_overwriting(
	const std::string &source, const std::string &dest)
{
	std::error_code ec;
	for (const auto &entry : fs::directory_iterator(source, ec)) {
		fs::path dest_equivalent = fs::path(dest) / entry.path().filename();

		if (entry.is_directory(ec)) {
			if (!can_copy_directory_without_overwriting(
				    entry.path().string(),
				    dest_equivalent.string())) {
				return false;
			}
		} else if (fs::exists(dest_equivalent, ec)) {
			return false;
		}
	}

	return true;
}

void FileFunctions::copy_directory(const std::string &source,
				   const std::string &dest, bool overwrite)
{
	std::error_code ec;
	if (!fs::is_directory(source, ec)) {
		fprintf(stderr, "Failed to copy directory, source %s didn't exist\n",
			source.c_str());
		return;
	}

	if (!fs::create_directories(dest, ec) && ec) {
		fprintf(stderr, "Failed to create destination directory %s\n",
			dest.c_str());
		return;
	}

	for (const auto &entry : fs::directory_iterator(source, ec)) {
		fs::path dest_file_path =
			fs::path(dest) / entry.path().filename();

		if (entry.is_directory(ec)) {
			// Copy dir
			copy_directory(entry.path().string(),
				       dest_file_path.string(), overwrite);
		} else {
			// Copy file
			if (overwrite) {
				fs::permissions(dest_file_path,
						fs::perms::owner_write |
							fs::perms::group_write |
							fs::perms::others_write,
						fs::perm_options::add, ec);
				ec.clear();
				fs::remove(dest_file_path, ec);
				ec.clear();
			}

			fs::copy_file(entry.path(), dest_file_path,
				      fs::copy_options::none, ec);
			if (ec) {
				fprintf(stderr,
					"Failed to copy file %s to %s: %s\n",
					entry.path().string().c_str(),
					dest_file_path.string().c_str(),
					ec.message().c_str());
			}
		}
	}
}

bool FileFunctions::directory_is_valid(const std::string &dir,
				       bool try_to_create_if_not_exists)
{
	// Return whether the directory exists, or whether it could be created
	// if it doesn't
	std::error_code ec;
	if (fs::is_directory(dir, ec)) {
		return true;
	}

	return try_to_create_if_not_exists && fs::create_directories(dir, ec);
}

std::string FileFunctions::ensure_filename_extension(
	std::string fn, const std::string &extension)
{
	// No-op if either input is empty
	if (!fn.empty() && !extension.empty()) {
		std::string extension_with_dot = "." + extension;

		if (!ends_with_case_insensitive(fn, extension_with_dot)) {
			fn.append(extension_with_dot);
		}
	}

	return fn;
}

std::string FileFunctions::read_file_as_string(const std::string &filename)
{
	std::ifstream f(filename, std::ios::in | std::ios::binary);
	if (!f.is_open()) {
		return std::string();
	}

	std::ostringstream ss;
	ss << f.rdbuf();
	return ss.str();
}

std::string FileFunctions::get_safe_temporary_filename(
	const std::string &original)
{
	int counter = 0;

	fs::path original_path(original);
	fs::path dir = original_path.parent_path();
	std::string filename = original_path.filename().string();

	// Split off the complete suffix (everything from the first dot), like
	// QFileInfo::completeSuffix()
	std::string basename = filename;
	std::string complete_suffix;
	size_t first_dot = filename.find('.');
	if (first_dot != std::string::npos) {
		basename = filename.substr(0, first_dot);
		complete_suffix = filename.substr(first_dot);
	}

	fs::path temp_abs_path;
	do {
		temp_abs_path =
			dir / (basename + ".tmp" + std::to_string(counter) +
			       complete_suffix);
		counter++;
	} while (fs::exists(temp_abs_path));

	return temp_abs_path.string();
}

bool FileFunctions::rename_file_allow_overwrite(const std::string &from,
						const std::string &to)
{
	std::error_code ec;
	if (fs::exists(to, ec) && !fs::remove(to, ec)) {
		fprintf(stderr, "Couldn't remove existing file %s for overwrite\n",
			to.c_str());
		return false;
	}

	// By this point, we can assume `to` either never existed or has now
	// been deleted
	fs::rename(from, to, ec);
	if (ec) {
		fprintf(stderr, "Failed to rename file %s to %s: %s\n",
			from.c_str(), to.c_str(), ec.message().c_str());
		return false;
	}

	return true;
}

std::string FileFunctions::get_formatted_executable_for_platform(
	std::string unformatted)
{
#ifdef _WIN32
	unformatted.append(".exe");
#endif

	return unformatted;
}

std::string FileFunctions::get_auto_recovery_root()
{
	return (fs::path(get_configuration_location()) / "autorecovery")
		.string();
}
