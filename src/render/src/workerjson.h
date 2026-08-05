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

#ifndef OAK_WORKERJSON_H
#define OAK_WORKERJSON_H

// Minimal flat-object JSON for the render-worker control channel, replacing
// QJsonObject/QJsonDocument. Only what the NDJSON protocol needs: string,
// integer, boolean and int-array members, compact serialization compatible
// with QJsonDocument::Compact on the worker side.

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace olive
{
namespace workerjson
{

class Object {
public:
	void set_string(const std::string &key, const std::string &value)
	{
		members_[key] = Value(value);
	}

	void set_int(const std::string &key, int64_t value)
	{
		members_[key] = Value(value);
	}

	void set_bool(const std::string &key, bool value)
	{
		members_[key] = Value(value);
	}

	void set_int_array(const std::string &key, const std::vector<int> &value)
	{
		members_[key] = Value(value);
	}

	bool has(const std::string &key) const
	{
		return members_.find(key) != members_.end();
	}

	std::string get_string(const std::string &key,
						   const std::string &def = std::string()) const
	{
		auto it = members_.find(key);
		return it != members_.end() && it->second.kind_ == k_string ?
				   it->second.string_ :
				   def;
	}

	int64_t get_int(const std::string &key, int64_t def = 0) const
	{
		auto it = members_.find(key);
		if (it == members_.end()) {
			return def;
		}
		if (it->second.kind_ == k_int) {
			return it->second.int_;
		}
		if (it->second.kind_ == k_bool) {
			return it->second.bool_ ? 1 : 0;
		}
		return def;
	}

	bool get_bool(const std::string &key, bool def = false) const
	{
		auto it = members_.find(key);
		if (it == members_.end()) {
			return def;
		}
		if (it->second.kind_ == k_bool) {
			return it->second.bool_;
		}
		if (it->second.kind_ == k_int) {
			return it->second.int_ != 0;
		}
		return def;
	}

	std::vector<int> get_int_array(const std::string &key) const
	{
		auto it = members_.find(key);
		return it != members_.end() && it->second.kind_ == k_array ?
				   it->second.array_ :
				   std::vector<int>();
	}

	std::string to_compact() const
	{
		std::string out;
		out += '{';
		bool first = true;
		for (const auto &kv : members_) {
			if (!first) {
				out += ',';
			}
			first = false;
			write_escaped(out, kv.first);
			out += ':';
			kv.second.write(out);
		}
		out += '}';
		return out;
	}

	static bool parse(const std::string &text, Object *out)
	{
		Parser p(text);
		return p.parse_object(out) && p.at_end();
	}

private:
	enum Kind { k_string, k_int, k_bool, k_array };

	struct Value {
		Kind kind_ = k_int;
		std::string string_;
		int64_t int_ = 0;
		bool bool_ = false;
		std::vector<int> array_;

		Value() = default;
		explicit Value(const std::string &s)
			: kind_(k_string)
			, string_(s)
		{
		}
		explicit Value(int64_t i)
			: kind_(k_int)
			, int_(i)
		{
		}
		explicit Value(bool b)
			: kind_(k_bool)
			, bool_(b)
		{
		}
		explicit Value(const std::vector<int> &a)
			: kind_(k_array)
			, array_(a)
		{
		}

		void write(std::string &out) const
		{
			switch (kind_) {
			case k_string:
				write_escaped(out, string_);
				break;
			case k_int:
				out += std::to_string(int_);
				break;
			case k_bool:
				out += bool_ ? "true" : "false";
				break;
			case k_array:
				out += '[';
				for (size_t i = 0; i < array_.size(); i++) {
					if (i > 0) {
						out += ',';
					}
					out += std::to_string(array_[i]);
				}
				out += ']';
				break;
			}
		}
	};

	static void write_escaped(std::string &out, const std::string &s)
	{
		static const char k_hex[] = "0123456789abcdef";
		out += '"';
		for (char c : s) {
			switch (c) {
			case '"':
				out += "\\\"";
				break;
			case '\\':
				out += "\\\\";
				break;
			case '\b':
				out += "\\b";
				break;
			case '\f':
				out += "\\f";
				break;
			case '\n':
				out += "\\n";
				break;
			case '\r':
				out += "\\r";
				break;
			case '\t':
				out += "\\t";
				break;
			default:
				if (static_cast<unsigned char>(c) < 0x20) {
					out += "\\u00";
					out += k_hex[(c >> 4) & 0xF];
					out += k_hex[c & 0xF];
				} else {
					out += c;
				}
			}
		}
		out += '"';
	}

	class Parser {
	public:
		explicit Parser(const std::string &text)
			: text_(text)
		{
		}

		bool at_end()
		{
			skip_ws();
			return pos_ == text_.size();
		}

		bool parse_object(Object *out)
		{
			skip_ws();
			if (!consume('{')) {
				return false;
			}
			skip_ws();
			if (consume('}')) {
				return true;
			}
			while (true) {
				std::string key;
				if (!parse_string(&key)) {
					return false;
				}
				skip_ws();
				if (!consume(':')) {
					return false;
				}
				if (!parse_value(out, key)) {
					return false;
				}
				skip_ws();
				if (consume('}')) {
					return true;
				}
				if (!consume(',')) {
					return false;
				}
				skip_ws();
			}
		}

	private:
		void skip_ws()
		{
			while (pos_ < text_.size() &&
				   (text_[pos_] == ' ' || text_[pos_] == '\t' ||
					text_[pos_] == '\n' || text_[pos_] == '\r')) {
				pos_++;
			}
		}

		bool consume(char c)
		{
			if (pos_ < text_.size() && text_[pos_] == c) {
				pos_++;
				return true;
			}
			return false;
		}

		bool parse_string(std::string *out)
		{
			skip_ws();
			if (!consume('"')) {
				return false;
			}
			out->clear();
			while (pos_ < text_.size()) {
				char c = text_[pos_++];
				if (c == '"') {
					return true;
				}
				if (c == '\\') {
					if (pos_ >= text_.size()) {
						return false;
					}
					char e = text_[pos_++];
					switch (e) {
					case 'n':
						*out += '\n';
						break;
					case 't':
						*out += '\t';
						break;
					case 'r':
						*out += '\r';
						break;
					case 'b':
						*out += '\b';
						break;
					case 'f':
						*out += '\f';
						break;
					case 'u': {
						// Only BMP escapes in the ASCII range are needed by the
						// protocol; keep the byte for those, skip others.
						if (pos_ + 4 > text_.size()) {
							return false;
						}
						unsigned code = 0;
						for (int i = 0; i < 4; i++) {
							char h = text_[pos_++];
							code <<= 4;
							if (h >= '0' && h <= '9') {
								code |= unsigned(h - '0');
							} else if (h >= 'a' && h <= 'f') {
								code |= unsigned(h - 'a' + 10);
							} else if (h >= 'A' && h <= 'F') {
								code |= unsigned(h - 'A' + 10);
							} else {
								return false;
							}
						}
						if (code < 0x80) {
							*out += char(code);
						} else if (code < 0x800) {
							*out += char(0xC0 | (code >> 6));
							*out += char(0x80 | (code & 0x3F));
						} else {
							*out += char(0xE0 | (code >> 12));
							*out += char(0x80 | ((code >> 6) & 0x3F));
							*out += char(0x80 | (code & 0x3F));
						}
						break;
					}
					default:
						*out += e;
						break;
					}
				} else {
					*out += c;
				}
			}
			return false;
		}

		bool parse_number(int64_t *out)
		{
			skip_ws();
			size_t start = pos_;
			if (pos_ < text_.size() && text_[pos_] == '-') {
				pos_++;
			}
			while (pos_ < text_.size() &&
				   (isdigit(static_cast<unsigned char>(text_[pos_])) ||
					text_[pos_] == '.' || text_[pos_] == 'e' ||
					text_[pos_] == 'E' || text_[pos_] == '+' ||
					text_[pos_] == '-')) {
				pos_++;
			}
			if (start == pos_) {
				return false;
			}
			// Protocol numbers are integers (stored as JSON doubles)
			*out = int64_t(strtod(text_.substr(start, pos_ - start).c_str(),
								  nullptr));
			return true;
		}

		bool parse_value(Object *out, const std::string &key)
		{
			skip_ws();
			if (pos_ >= text_.size()) {
				return false;
			}
			char c = text_[pos_];
			if (c == '"') {
				std::string s;
				if (!parse_string(&s)) {
					return false;
				}
				out->set_string(key, s);
				return true;
			}
			if (c == 't') {
				if (text_.compare(pos_, 4, "true") != 0) {
					return false;
				}
				pos_ += 4;
				out->set_bool(key, true);
				return true;
			}
			if (c == 'f') {
				if (text_.compare(pos_, 5, "false") != 0) {
					return false;
				}
				pos_ += 5;
				out->set_bool(key, false);
				return true;
			}
			if (c == 'n') {
				if (text_.compare(pos_, 4, "null") != 0) {
					return false;
				}
				pos_ += 4;
				out->set_string(key, std::string());
				return true;
			}
			if (c == '[') {
				pos_++;
				std::vector<int> arr;
				skip_ws();
				if (!consume(']')) {
					while (true) {
						int64_t v;
						if (!parse_number(&v)) {
							return false;
						}
						arr.push_back(int(v));
						skip_ws();
						if (consume(']')) {
							break;
						}
						if (!consume(',')) {
							return false;
						}
					}
				}
				out->set_int_array(key, arr);
				return true;
			}
			if (c == '{') {
				// The protocol is flat; skip nested objects defensively.
				int depth = 0;
				do {
					if (text_[pos_] == '{') {
						depth++;
					} else if (text_[pos_] == '}') {
						depth--;
					} else if (text_[pos_] == '"') {
						std::string dummy;
						if (!parse_string(&dummy)) {
							return false;
						}
						continue;
					}
					pos_++;
				} while (depth > 0 && pos_ < text_.size());
				return depth == 0;
			}
			int64_t v;
			if (!parse_number(&v)) {
				return false;
			}
			out->set_int(key, v);
			return true;
		}

		const std::string &text_;
		size_t pos_ = 0;
	};

	std::map<std::string, Value> members_;
};

} // namespace workerjson
} // namespace olive

#endif // OAK_WORKERJSON_H
