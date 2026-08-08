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

#include "variant.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace olive
{

/**
 * @brief Format a double like QString::number(d) ('g', 6 significant digits).
 */
static std::string format_double(double d)
{
	char buf[64];
	std::snprintf(buf, sizeof(buf), "%g", d);
	return std::string(buf);
}

double Variant::to_double(bool *ok) const
{
	if (is_numeric_kind()) {
		if (ok) {
			*ok = true;
		}
		return numeric_as_double();
	}

	if (kind_ == k_string) {
		char *end = nullptr;
		double d = std::strtod(string_.c_str(), &end);
		bool valid = end != string_.c_str();
		if (ok) {
			*ok = valid;
		}
		return valid ? d : 0.0;
	}

	if (ok) {
		*ok = false;
	}
	return 0.0;
}

float Variant::to_float(bool *ok) const
{
	return float(to_double(ok));
}

int64_t Variant::to_long_long(bool *ok) const
{
	if (is_numeric_kind()) {
		if (ok) {
			*ok = true;
		}
		return numeric_as_int64();
	}

	if (kind_ == k_string) {
		char *end = nullptr;
		long long i = std::strtoll(string_.c_str(), &end, 10);
		bool valid = end != string_.c_str();
		if (ok) {
			*ok = valid;
		}
		return valid ? int64_t(i) : 0;
	}

	if (ok) {
		*ok = false;
	}
	return 0;
}

uint64_t Variant::to_u_long_long(bool *ok) const
{
	if (is_numeric_kind()) {
		if (ok) {
			*ok = true;
		}
		return numeric_as_uint64();
	}

	if (kind_ == k_string) {
		char *end = nullptr;
		unsigned long long i = std::strtoull(string_.c_str(), &end, 10);
		bool valid = end != string_.c_str();
		if (ok) {
			*ok = valid;
		}
		return valid ? uint64_t(i) : 0;
	}

	if (ok) {
		*ok = false;
	}
	return 0;
}

int Variant::to_int(bool *ok) const
{
	return int(to_long_long(ok));
}

unsigned int Variant::to_uint(bool *ok) const
{
	return (unsigned int)(to_u_long_long(ok));
}

bool Variant::to_bool() const
{
	if (is_numeric_kind()) {
		return numeric_as_double() != 0.0;
	}

	if (kind_ == k_string) {
		if (string_ == "true" || string_ == "TRUE") {
			return true;
		}
		if (string_ == "false" || string_ == "FALSE" || string_.empty()) {
			return false;
		}
		// QVariant falls back to numeric interpretation
		bool ok = false;
		double d = to_double(&ok);
		return ok && d != 0.0;
	}

	return false;
}

std::string Variant::to_string() const
{
	switch (kind_) {
	case k_null:
		return std::string();
	case k_bool:
		return bool_ ? std::string("true") : std::string("false");
	case k_int:
		return std::to_string(int_);
	case k_uint:
		return std::to_string(uint_);
	case k_double:
		return format_double(double_);
	case k_string:
		return string_;
	case k_byte_array:
		return std::string(byte_array_.begin(), byte_array_.end());
	case k_string_list:
	case k_custom:
		return std::string();
	}

	return std::string();
}

StringList Variant::to_string_list() const
{
	if (kind_ == k_string_list) {
		return string_list_;
	}

	if (kind_ == k_string) {
		// QString -> QStringList conversion wraps the string in a list
		return StringList{ string_ };
	}

	return StringList();
}

ByteArray Variant::to_byte_array() const
{
	if (kind_ == k_byte_array) {
		return byte_array_;
	}

	if (kind_ == k_string) {
		return ByteArray(string_.begin(), string_.end());
	}

	return ByteArray();
}

bool Variant::operator==(const Variant &rhs) const
{
	if (kind_ == k_null || rhs.kind_ == k_null) {
		return kind_ == rhs.kind_;
	}

	if (is_numeric_kind() && rhs.is_numeric_kind()) {
		// QVariant compares numeric types numerically across kinds
		if (kind_ == k_double || rhs.kind_ == k_double) {
			return numeric_as_double() == rhs.numeric_as_double();
		}
		if (kind_ == k_uint || rhs.kind_ == k_uint) {
			return numeric_as_uint64() == rhs.numeric_as_uint64();
		}
		return numeric_as_int64() == rhs.numeric_as_int64();
	}

	if (kind_ != rhs.kind_) {
		return false;
	}

	switch (kind_) {
	case k_string:
		return string_ == rhs.string_;
	case k_string_list:
		return string_list_ == rhs.string_list_;
	case k_byte_array:
		return byte_array_ == rhs.byte_array_;
	case k_custom:
		return custom_->equals(rhs.custom_.get());
	default:
		return false;
	}
}

/**
 * @brief QByteArray::toBase64() equivalent.
 */
std::string byte_array_to_base64(const ByteArray &data)
{
	static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

	std::string out;
	out.reserve((data.size() + 2) / 3 * 4);

	for (size_t i = 0; i < data.size(); i += 3) {
		uint32_t chunk = uint8_t(data[i]) << 16;
		if (i + 1 < data.size()) {
			chunk |= uint8_t(data[i + 1]) << 8;
		}
		if (i + 2 < data.size()) {
			chunk |= uint8_t(data[i + 2]);
		}

		out += table[(chunk >> 18) & 0x3F];
		out += table[(chunk >> 12) & 0x3F];
		out += (i + 1 < data.size()) ? table[(chunk >> 6) & 0x3F] : '=';
		out += (i + 2 < data.size()) ? table[chunk & 0x3F] : '=';
	}

	return out;
}

/**
 * @brief QByteArray::fromBase64() equivalent.
 */
ByteArray byte_array_from_base64(const std::string &text)
{
	auto decode_char = [](char c) -> int {
		if (c >= 'A' && c <= 'Z') {
			return c - 'A';
		}
		if (c >= 'a' && c <= 'z') {
			return c - 'a' + 26;
		}
		if (c >= '0' && c <= '9') {
			return c - '0' + 52;
		}
		if (c == '+') {
			return 62;
		}
		if (c == '/') {
			return 63;
		}
		return -1;
	};

	ByteArray out;
	int accumulator = 0;
	int bits = 0;

	for (char c : text) {
		if (c == '=' || std::isspace((unsigned char)(c))) {
			continue;
		}
		int v = decode_char(c);
		if (v < 0) {
			continue;
		}
		accumulator = (accumulator << 6) | v;
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			out.push_back(char((accumulator >> bits) & 0xFF));
		}
	}

	return out;
}

}
