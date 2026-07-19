/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2022 Olive Team
  Modifications Copyright (C) 2025 mikesolar

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

#include <QDir>
#include <QString>

#include "common/define.h"

namespace olive
{

/**
 * @brief A collection of static file and directory functions
 */
class FileFunctions {
public:
	/**
   * @brief Returns true if the application is running in portable mode
   *
   * In portable mode, any persistent configuration files should be made in a path relative to the application rather
   * than in the user's home folder.
   */
	static bool is_portable();

	static QString get_unique_file_identifier(const QString &filename);

	static QString get_configuration_location();

	static QString get_application_path();

	static QString get_temp_file_path();

	static bool can_copy_directory_without_overwriting(const QString &source,
												   const QString &dest);

	static void copy_directory(const QString &source, const QString &dest,
							  bool overwrite = false);

	static bool directory_is_valid(const QDir &dir,
								 bool try_to_create_if_not_exists = true);

	/**
   * @brief Ensures a given filename has a certain extension
   *
   * Checks if the filename has the extension provided and appends it if not. The extension is
   * checked case-insensitive. The extension should be provided with no dot (e.g. "ove" rather than
   * ".ove").

   * @return The filename provided either untouched or with the extension appended to it.
   */
	static QString ensure_filename_extension(QString fn,
										   const QString &extension);

	static QString read_file_as_string(const QString &filename);

	/**
   * @brief Returns a temporary filename that can be used while writing rather than the original
   *
   * If overwriting a file, it's safest to write to a new file first and then only replace it at
   * the end so that if the program crashes or the user cancels the save half way through, the
   * original file is still intact.
   *
   * This function returns a slight variant of the filename provided that's guaranteed to not exist
   * and therefore won't overwrite anything important.
   */
	static QString get_safe_temporary_filename(const QString &original);

	/**
   * @brief Renames a file from `from` to `to`, deleting `to` if such a file already exists first
   */
	static bool rename_file_allow_overwrite(const QString &from,
										 const QString &to);

	inline static QString get_formatted_executable_for_platform(QString unformatted)
	{
#ifdef Q_OS_WINDOWS
		unformatted.append(QStringLiteral(".exe"));
#endif

		return unformatted;
	}

	static QString get_auto_recovery_root();
};

}

#endif // OAK_FILEFUNCTIONS_H
