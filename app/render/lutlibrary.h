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

#ifndef OAK_LUTLIBRARY_H
#define OAK_LUTLIBRARY_H

#include <QString>
#include <QStringList>

namespace olive
{

/**
 * @brief A global, user-configurable library of LUT files
 *
 * The library is a list of directories (stored in the application config
 * under "LUTLibraryPaths") that are scanned for supported LUT files. LUT
 * nodes can offer the library contents as quick picks instead of forcing
 * the user to browse for a file path on every node.
 */
class LUTLibrary {
public:
	/**
	 * @brief Returns true if the given file suffix is a supported LUT
	 * extension (.cube or .3dl, case-insensitive, leading dot tolerated)
	 */
	static bool is_supported_extension(const QString &suffix);

	/**
	 * @brief The directories that make up the LUT library
	 */
	static QStringList get_directories();

	/**
	 * @brief Replaces the LUT library directories and saves them to the
	 * application config
	 */
	static void set_directories(const QStringList &dirs);

	/**
	 * @brief All supported LUT files found under the library directories
	 *
	 * Directories are scanned recursively. Files in earlier directories
	 * are listed first.
	 */
	static QStringList get_lut_files();
};

}

#endif // OAK_LUTLIBRARY_H
