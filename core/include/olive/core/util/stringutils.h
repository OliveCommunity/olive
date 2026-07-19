/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2023 Olive Studios LLC
  Modifications Copyright (C) 2026 Oak Team

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

#ifndef OAK_LIBOLIVECORE_STRINGUTILS_H
#define OAK_LIBOLIVECORE_STRINGUTILS_H

#include <algorithm>
#include <cstdarg>
#include <regex>
#include <vector>
#include <string>

#include "olive/core/oakcore/stringutils.h"

namespace olive::core
{

/**
 * @brief String utility functions
 *
 * Consumer-side wrapper over the liboakcore C ABI: every non-inlined call
 * is forwarded across the C boundary. The public API is unchanged from the
 * original implementation. The class has static members only, so there is
 * no opaque handle.
 */
class StringUtils {
public:
	/**
   * @brief Split a string into a list of strings using a specific delimiter
   *
   * @param s
   *
   * The string to split.
   *
   * @param separator
   *
   * The character to split the string on.
   *
   * @return
   *
   * A vector of strings split by the specified delimiter.
   */
	static std::vector<std::string> split(const std::string &s, char separator)
	{
		int count = 0;
		char **arr = oakcore_stringutils_split(s.c_str(), separator, &count);
		std::vector<std::string> output;
		if (arr) {
			output.reserve(size_t(count));
			for (int i = 0; i < count; i++) {
				output.emplace_back(arr[i]);
			}
			oakcore_stringutils_free_string_array(arr, count);
		}
		return output;
	}

	/**
   * @brief Splits a string into a list of strings using regular expressions.
   *
   * @param s
   *
   * The string to split.
   *
   * @param regex
   *
   * The regular expression to split the string on.
   *
   * @return
   *
   * A vector of strings split wherever the regular expression matched.
   */
	static std::vector<std::string> split_regex(const std::string &s,
												const std::regex &regex)
	{
		// std::regex cannot cross the C ABI, so this overload is implemented
		// inline here with only standard library facilities, exactly like the
		// original. The C ABI exposes the same functionality as
		// oakcore_stringutils_split_regex() taking a pattern string.
		std::vector<std::string> output;

		std::sregex_token_iterator iter(s.begin(), s.end(), regex, -1);
		std::sregex_token_iterator end;
		for (; iter != end; iter++) {
			output.push_back(*iter);
		}

		return output;
	}

	/**
   * @brief Convert a string to int using a bool pointer to determine
   * success rather than an exception
   *
   * @param s
   *
   * The string to parse an int from.
   *
   * @param base
   *
   * The base of the number in the string (usually 10, or 16 for hex).
   *
   * @param ok
   *
   * (Optional) a boolean output parameter specifying whether the conversion was successful or not.
   *
   * @return
   *
   * Either the int parsed from the string, or 0 (with *ok set to false) on parser error.
   */
	static int to_int(const std::string &s, int base, bool *ok = nullptr)
	{
		int c_ok = 0;
		const int x = oakcore_stringutils_to_int(s.c_str(), base, &c_ok);
		if (ok) {
			*ok = (c_ok != 0);
		}
		return x;
	}

	/**
   * @brief Overloaded function
   *
   * @param s
   *
   * The string to parse an int from.
   *
   * @param ok
   *
   * (Optional) a boolean output parameter specifying whether the conversion was successful or not.
   *
   * @return
   *
   * Either the int parsed from the string, or 0 (with *ok set to false) on parser error.
   */
	static int to_int(const std::string &s, bool *ok = nullptr)
	{
		return to_int(s, 10, ok);
	}

	/**
   * @brief Convert a number to a string with left padding
   *
   * Usually used for converting a number to a string with leading zeroes.
   *
   * @param val
   *
   * The number to convert. This is a templated function and will accept any type, e.g.
   * int/long/float/double/etc.
   *
   * @param padding
   *
   * Total desired length of the string. For example, setting this to `2` will ensure the string
   * is at least 2 characters in size, using `c` to pad the left side where necessary.
   *
   * @param c
   *
   * The character to pad with. This defaults to `0` assuming you'll be using this function to
   * create leading zeroes.
   *
   * @return
   *
   * The padded string.
   */
	template <typename T>
	static std::string to_string_leftpad(T val, size_t padding, char c = '0')
	{
		std::string s = std::to_string(val);

		if (s.size() < padding) {
			s.insert(0, padding - s.size(), c);
		}

		return s;
	}

	/**
   * @brief Format a string
   *
   * A sprintf wrapper that returns a std::string.
   *
   * @param fmt
   *
   * The format to use.
   *
   * @return
   *
   * A formatted string in std::string form.
   */
	static std::string format(const char *fmt, ...)
	{
		va_list ap1, ap2;
		va_start(ap1, fmt);

		// Need to duplicate because the va_list is consumed by each C API call
		va_copy(ap2, ap1);

		const int size = oakcore_stringutils_format_v(nullptr, 0, fmt, ap1);

		// Create string with size, adding 1 for the null terminator
		std::string r(size_t(size) + 1, '\0');
		oakcore_stringutils_format_v(r.data(), size + 1, fmt, ap2);

		va_end(ap2);
		va_end(ap1);

		// Pop null terminator
		r.pop_back();

		return r;
	}

	// trim from start (in place)
	static inline void ltrim(std::string &s)
	{
		s.erase(s.begin(),
				std::find_if(s.begin(), s.end(), [](unsigned char ch) {
					return !std::isspace(ch);
				}));
	}

	// trim from end (in place)
	static inline void rtrim(std::string &s)
	{
		s.erase(std::find_if(s.rbegin(), s.rend(),
							 [](unsigned char ch) { return !std::isspace(ch); })
					.base(),
				s.end());
	}

	// trim from both ends (in place)
	static inline void trim(std::string &s)
	{
		rtrim(s);
		ltrim(s);
	}

	// trim from start (copying)
	static inline std::string ltrimmed(std::string s)
	{
		ltrim(s);
		return s;
	}

	// trim from end (copying)
	static inline std::string rtrimmed(std::string s)
	{
		rtrim(s);
		return s;
	}

	// trim from both ends (copying)
	static inline std::string trimmed(std::string s)
	{
		trim(s);
		return s;
	}
};

}

#endif // OAK_LIBOLIVECORE_STRINGUTILS_H
