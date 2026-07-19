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

#include "oakcore/stringutils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <regex>
#include <string>
#include <vector>

#include "util/stringutils.h"

namespace
{

char **copy_string_vector(const std::vector<std::string> &v, int *count)
{
	char **arr = static_cast<char **>(malloc(sizeof(char *) * v.size()));
	if (!arr) {
		*count = 0;
		return nullptr;
	}
	for (size_t i = 0; i < v.size(); i++) {
		arr[i] = static_cast<char *>(malloc(v[i].size() + 1));
		if (!arr[i]) {
			for (size_t j = 0; j < i; j++) {
				free(arr[j]);
			}
			free(arr);
			*count = 0;
			return nullptr;
		}
		memcpy(arr[i], v[i].c_str(), v[i].size() + 1);
	}
	*count = int(v.size());
	return arr;
}

} // namespace

extern "C"
{

char **oakcore_stringutils_split(const char *s, char separator, int *count)
{
	const std::vector<std::string> v =
		olive::core::internal::StringUtils::split(s ? s : "", separator);
	return copy_string_vector(v, count);
}

char **oakcore_stringutils_split_regex(const char *s, const char *pattern,
									   int *count)
{
	const std::vector<std::string> v =
		olive::core::internal::StringUtils::split_regex(
			s ? s : "", std::regex(pattern ? pattern : ""));
	return copy_string_vector(v, count);
}

void oakcore_stringutils_free_string_array(char **arr, int count)
{
	if (!arr) {
		return;
	}
	for (int i = 0; i < count; i++) {
		free(arr[i]);
	}
	free(arr);
}

int oakcore_stringutils_to_int(const char *s, int base, int *ok)
{
	bool b = false;
	const int x = olive::core::internal::StringUtils::to_int(s ? s : "", base, &b);
	if (ok) {
		*ok = b ? 1 : 0;
	}
	return x;
}

int oakcore_stringutils_format(char *buf, int buf_size, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	const int r = oakcore_stringutils_format_v(buf, buf_size, fmt, args);
	va_end(args);
	return r;
}

int oakcore_stringutils_format_v(char *buf, int buf_size, const char *fmt,
								 va_list args)
{
	// The implementation class only exposes a variadic format(), so the
	// va_list form applies the same vsnprintf semantics directly here.
	va_list copy;
	va_copy(copy, args);
	const int needed =
		vsnprintf(buf, buf_size > 0 ? size_t(buf_size) : 0, fmt, copy);
	va_end(copy);
	return needed;
}

} // extern "C"
