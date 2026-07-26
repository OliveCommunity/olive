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

#ifndef OAKENGINE_LUT_H
#define OAKENGINE_LUT_H

#include "export.h"
#include "init.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file lut.h
 * @brief C ABI for the global LUT file library (olive::LUTLibrary)
 *
 * A thin facade over the user-configurable list of LUT directories and the
 * supported LUT files discovered under them. The library state is kept in the
 * application config ("LUTLibraryPaths"); this facade only exposes the
 * directory/file list queries and the directory replacement primitive.
 *
 * Conventions match the other facade families:
 *   - 0 (OAKENGINE_OK) / negative OAKENGINE_E_* codes.
 *   - String output uses the buf/size convention.
 *   - Count queries return a non-negative integer, or a negative error code.
 */

/**
 * @brief Number of directories currently in the LUT library.
 */
OAKENGINE_API int oakengine_lut_directory_count(void);

/**
 * @brief Get the directory path at `index` (buf/size convention).
 *
 * Returns the string length on success, or a negative OAKENGINE_E_* code when
 * `index` is out of range.
 */
OAKENGINE_API int oakengine_lut_directory_at(int index, char *buf,
                                             int buf_size);

/**
 * @brief Number of supported LUT files found under the library directories.
 */
OAKENGINE_API int oakengine_lut_file_count(void);

/**
 * @brief Get the full path of the LUT file at `index` (buf/size convention).
 *
 * Files are listed in the order they are discovered; files in earlier
 * directories come first. Returns the string length on success, or a negative
 * OAKENGINE_E_* code when `index` is out of range.
 */
OAKENGINE_API int oakengine_lut_file_at(int index, char *buf, int buf_size);

/**
 * @brief Replace the LUT library directories and persist them to config.
 *
 * `dirs` is an array of `count` NUL-terminated UTF-8 directory paths. Passing
 * `count == 0` clears the library. Returns OAKENGINE_OK or an error code.
 */
OAKENGINE_API int oakengine_lut_set_directories(const char *const *dirs,
                                                int count);

#ifdef __cplusplus
}
#endif

#endif /* OAKENGINE_LUT_H */
