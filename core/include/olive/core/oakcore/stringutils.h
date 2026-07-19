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

#ifndef OAKCORE_STRINGUTILS_H
#define OAKCORE_STRINGUTILS_H

#include <stdarg.h>

#include "export.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file stringutils.h
 * @brief C ABI for the string utility functions
 *
 * StringUtils consists of static functions only, so there are no objects
 * and no opaque handle. Functions returning string lists hand back a
 * caller-owned char** that must be released with
 * oakcore_stringutils_free_string_array().
 */

/**
 * Splits s on separator and returns a newly allocated array of newly
 * allocated NUL-terminated strings; *count receives the element count.
 * Release the result with oakcore_stringutils_free_string_array().
 * A NULL or empty s yields one empty string, mirroring the C++
 * implementation. Returns NULL only on allocation failure.
 */
OAKCORE_API char **oakcore_stringutils_split(const char *s, char separator,
											 int *count);

/**
 * Splits s wherever the regular expression pattern matches (std::regex
 * ECMAScript syntax). Same ownership and NULL semantics as
 * oakcore_stringutils_split().
 */
OAKCORE_API char **oakcore_stringutils_split_regex(const char *s,
												   const char *pattern,
												   int *count);

/**
 * Releases an array returned by oakcore_stringutils_split() or
 * oakcore_stringutils_split_regex(). Safe to call with arr == NULL.
 */
OAKCORE_API void oakcore_stringutils_free_string_array(char **arr, int count);

/**
 * Parses an int from s in the given base (usually 10, or 16 for hex).
 * Returns the parsed value, or 0 on parser error. ok is an optional output
 * parameter set to 1 on success and 0 on failure.
 */
OAKCORE_API int oakcore_stringutils_to_int(const char *s, int base, int *ok);

/**
 * Formats a string with vsnprintf semantics: writes into buf
 * (NUL-terminated when buf_size > 0) and returns the number of characters
 * that would have been written excluding the NUL, so buf == NULL or a
 * too-small buffer can be used to query the required size.
 */
OAKCORE_API int oakcore_stringutils_format(char *buf, int buf_size,
										   const char *fmt, ...);

/**
 * va_list form of oakcore_stringutils_format(), for forwarding from other
 * variadic functions.
 */
OAKCORE_API int oakcore_stringutils_format_v(char *buf, int buf_size,
											 const char *fmt, va_list args);

#ifdef __cplusplus
}
#endif

#endif /* OAKCORE_STRINGUTILS_H */
