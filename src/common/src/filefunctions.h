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

#ifndef OAK_FILEFUNCTIONS_H
#define OAK_FILEFUNCTIONS_H

#include <string>

/**
 * @brief A collection of static file and directory functions
 *
 * Qt-free reimplementation of the former olive::FileFunctions. All paths
 * are UTF-8 std::strings and all filesystem operations use std::filesystem.
 */
class FileFunctions {
public:
	/**
	 * @brief Returns true if the application is running in portable mode
	 *
	 * In portable mode, any persistent configuration files should be made in
	 * a path relative to the application rather than in the user's home
	 * folder.
	 */
	static bool is_portable();

	/**
	 * @brief Returns a deterministic identifier string for a file
	 *
	 * Derived from the absolute path and the last modification time. Returns
	 * an empty string if the file does not exist. Uses FNV-1a hashing rather
	 * than SHA-1; identifiers are only used as cache keys, not for security.
	 */
	static std::string get_unique_file_identifier(const std::string &filename);

	static std::string get_configuration_location();

	static std::string get_application_path();

	static std::string get_temp_file_path();

	static bool can_copy_directory_without_overwriting(
		const std::string &source, const std::string &dest);

	static void copy_directory(const std::string &source,
				   const std::string &dest,
				   bool overwrite = false);

	static bool directory_is_valid(const std::string &dir,
				       bool try_to_create_if_not_exists = true);

	/**
	 * @brief Ensures a given filename has a certain extension
	 *
	 * Checks if the filename has the extension provided and appends it if
	 * not. The extension is checked case-insensitive. The extension should
	 * be provided with no dot (e.g. "ove" rather than ".ove").
	 *
	 * @return The filename provided either untouched or with the extension
	 * appended to it.
	 */
	static std::string ensure_filename_extension(std::string fn,
						     const std::string &extension);

	static std::string read_file_as_string(const std::string &filename);

	/**
	 * @brief Returns a temporary filename that can be used while writing
	 * rather than the original
	 *
	 * If overwriting a file, it's safest to write to a new file first and
	 * then only replace it at the end so that if the program crashes or the
	 * user cancels the save half way through, the original file is still
	 * intact.
	 *
	 * This function returns a slight variant of the filename provided that's
	 * guaranteed to not exist and therefore won't overwrite anything
	 * important.
	 */
	static std::string get_safe_temporary_filename(const std::string &original);

	/**
	 * @brief Renames a file from `from` to `to`, deleting `to` if such a
	 * file already exists first
	 */
	static bool rename_file_allow_overwrite(const std::string &from,
						const std::string &to);

	static std::string get_formatted_executable_for_platform(
		std::string unformatted);

	static std::string get_auto_recovery_root();
};

#endif // OAK_FILEFUNCTIONS_H
