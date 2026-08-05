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

#ifndef OAK_COMMON_QTUTILS_H
#define OAK_COMMON_QTUTILS_H

#include <stdint.h>

#include "common/error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Convert a pointer to an integer value
 *
 * @param ptr Pointer to convert (may be NULL, yielding 0).
 * @param out_value Receives the integer representation of ptr.
 * @return OAKCOMMON_OK on success, OAKCOMMON_E_INVALID if out_value is NULL.
 */
int oakcommon_qtutils_ptr_to_value(void *ptr, uint64_t *out_value);

/**
 * @brief Convert an integer produced by oakcommon_qtutils_ptr_to_value() back to a pointer
 *
 * @param value Integer representation of a pointer.
 * @param out_ptr Receives the decoded pointer.
 * @return OAKCOMMON_OK on success, OAKCOMMON_E_INVALID if out_ptr is NULL.
 */
int oakcommon_qtutils_value_to_ptr(uint64_t value, void **out_ptr);

/**
 * @brief Get the creation (birth) time of a file as seconds since the Unix epoch
 *
 * Falls back to the last metadata change time when the filesystem does not
 * record birth times.
 *
 * @param path NUL-terminated filesystem path.
 * @param out_secs Receives the creation time in seconds since the epoch.
 * @return OAKCOMMON_OK on success, OAKCOMMON_E_INVALID for NULL arguments,
 * OAKCOMMON_E_NOT_FOUND if the file does not exist or cannot be stat'ed.
 */
int oakcommon_qtutils_get_creation_date(const char *path, int64_t *out_secs);

#ifdef __cplusplus
}
#endif

#endif // OAK_COMMON_QTUTILS_H
