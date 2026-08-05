#pragma once
// Transitional: QDataStream replacement for the render disk-cache state and
// index files. Mirrors QDataStream's default (big-endian) wire format for the
// subset of types those files use, so existing cache files stay readable:
//   quint32/qint32/qint64 -> big-endian bytes
//   bool                  -> quint8
//   QString               -> quint32 byte length + UTF-16BE code units
//   QUuid                 -> 16 raw bytes in RFC 4122 order
// Reads past EOF yield zeroed values (QDataStream::ReadPastEnd semantics).

#include <cstdint>
#include <cstdio>
#include <string>

namespace olive
{

namespace binarystream_detail
{

inline void append_utf16be(std::string &out, uint32_t cp)
{
	if (cp >= 0x10000) {
		cp -= 0x10000;
		uint16_t hi = 0xD800 + (cp >> 10);
		uint16_t lo = 0xDC00 + (cp & 0x3FF);
		out += char(hi >> 8);
		out += char(hi & 0xFF);
		out += char(lo >> 8);
		out += char(lo & 0xFF);
	} else {
		out += char(cp >> 8);
		out += char(cp & 0xFF);
	}
}

inline std::string utf8_to_utf16be(const std::string &s)
{
	std::string out;
	size_t i = 0;
	while (i < s.size()) {
		unsigned char c = s[i];
		uint32_t cp;
		size_t extra;
		if (c < 0x80) {
			cp = c;
			extra = 0;
		} else if ((c & 0xE0) == 0xC0) {
			cp = c & 0x1F;
			extra = 1;
		} else if ((c & 0xF0) == 0xE0) {
			cp = c & 0x0F;
			extra = 2;
		} else if ((c & 0xF8) == 0xF0) {
			cp = c & 0x07;
			extra = 3;
		} else {
			// Invalid byte, emit U+FFFD like Qt's UTF-8 handling
			cp = 0xFFFD;
			extra = 0;
		}
		for (size_t j = 0; j < extra; j++) {
			if (i + 1 < s.size()) {
				cp = (cp << 6) | (uint8_t(s[i + 1]) & 0x3F);
				i++;
			}
		}
		i++;
		append_utf16be(out, cp);
	}
	return out;
}

inline void append_utf8(std::string &out, uint32_t cp)
{
	if (cp < 0x80) {
		out += char(cp);
	} else if (cp < 0x800) {
		out += char(0xC0 | (cp >> 6));
		out += char(0x80 | (cp & 0x3F));
	} else if (cp < 0x10000) {
		out += char(0xE0 | (cp >> 12));
		out += char(0x80 | ((cp >> 6) & 0x3F));
		out += char(0x80 | (cp & 0x3F));
	} else {
		out += char(0xF0 | (cp >> 18));
		out += char(0x80 | ((cp >> 12) & 0x3F));
		out += char(0x80 | ((cp >> 6) & 0x3F));
		out += char(0x80 | (cp & 0x3F));
	}
}

inline std::string utf16be_to_utf8(const std::string &be)
{
	std::string out;
	size_t units = be.size() / 2;
	for (size_t i = 0; i < units; i++) {
		uint16_t u = (uint8_t(be[i * 2]) << 8) | uint8_t(be[i * 2 + 1]);
		uint32_t cp = u;
		if (u >= 0xD800 && u < 0xDC00 && i + 1 < units) {
			uint16_t lo = (uint8_t(be[i * 2 + 2]) << 8) |
						  uint8_t(be[i * 2 + 3]);
			if (lo >= 0xDC00 && lo < 0xE000) {
				cp = 0x10000 + ((uint32_t(u) - 0xD800) << 10) + (lo - 0xDC00);
				i++;
			}
		}
		append_utf8(out, cp);
	}
	return out;
}

inline int hex_digit(char c)
{
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	if (c >= 'a' && c <= 'f') {
		return c - 'a' + 10;
	}
	if (c >= 'A' && c <= 'F') {
		return c - 'A' + 10;
	}
	return 0;
}

} // namespace binarystream_detail

class BinaryStreamWriter {
public:
	explicit BinaryStreamWriter(std::FILE *f)
		: f_(f)
	{
	}

	BinaryStreamWriter &operator<<(uint32_t v)
	{
		write_be(v, 4);
		return *this;
	}

	BinaryStreamWriter &operator<<(int32_t v)
	{
		write_be(uint32_t(v), 4);
		return *this;
	}

	BinaryStreamWriter &operator<<(int64_t v)
	{
		write_be(uint64_t(v), 8);
		return *this;
	}

	BinaryStreamWriter &operator<<(bool v)
	{
		write_be(v ? 1 : 0, 1);
		return *this;
	}

	// QString: quint32 byte length + UTF-16BE data
	BinaryStreamWriter &operator<<(const std::string &v)
	{
		std::string be = binarystream_detail::utf8_to_utf16be(v);
		write_be(uint32_t(be.size()), 4);
		if (!be.empty()) {
			std::fwrite(be.data(), 1, be.size(), f_);
		}
		return *this;
	}

	// QUuid: 16 raw bytes in RFC 4122 order. Accepts the canonical text form
	// "{8-4-4-4-12}" (braces optional), which is what cache uuids are stored as.
	void write_uuid_text(const std::string &uuid_text)
	{
		uint8_t bytes[16] = { 0 };
		int nibble = 0;
		for (char c : uuid_text) {
			if (c == '{' || c == '}' || c == '-') {
				continue;
			}
			if (nibble >= 32) {
				break;
			}
			int d = binarystream_detail::hex_digit(c);
			if (nibble % 2 == 0) {
				bytes[nibble / 2] = uint8_t(d << 4);
			} else {
				bytes[nibble / 2] |= uint8_t(d);
			}
			nibble++;
		}
		std::fwrite(bytes, 1, 16, f_);
	}

private:
	void write_be(uint64_t v, int bytes)
	{
		uint8_t b[8];
		for (int i = 0; i < bytes; i++) {
			b[i] = uint8_t(v >> ((bytes - 1 - i) * 8));
		}
		std::fwrite(b, 1, bytes, f_);
	}

	std::FILE *f_;
};

class BinaryStreamReader {
public:
	explicit BinaryStreamReader(std::FILE *f)
		: f_(f)
	{
	}

	BinaryStreamReader &operator>>(uint32_t &v)
	{
		v = uint32_t(read_be(4));
		return *this;
	}

	BinaryStreamReader &operator>>(int32_t &v)
	{
		v = int32_t(read_be(4));
		return *this;
	}

	BinaryStreamReader &operator>>(int64_t &v)
	{
		v = int64_t(read_be(8));
		return *this;
	}

	BinaryStreamReader &operator>>(bool &v)
	{
		v = read_be(1) != 0;
		return *this;
	}

	BinaryStreamReader &operator>>(std::string &v)
	{
		uint32_t len = uint32_t(read_be(4));
		v.clear();
		if (len == 0xFFFFFFFF || len == 0) {
			return *this;
		}
		std::string be(len, '\0');
		size_t got = std::fread(&be[0], 1, len, f_);
		be.resize(got);
		v = binarystream_detail::utf16be_to_utf8(be);
		return *this;
	}

	// QUuid: 16 raw bytes -> canonical "{8-4-4-4-12}" lowercase text
	std::string read_uuid_text()
	{
		uint8_t bytes[16] = { 0 };
		std::fread(bytes, 1, 16, f_);

		static const char k_hex[] = "0123456789abcdef";
		std::string out;
		out.reserve(38);
		out += '{';
		for (int i = 0; i < 16; i++) {
			if (i == 4 || i == 6 || i == 8 || i == 10) {
				out += '-';
			}
			out += k_hex[bytes[i] >> 4];
			out += k_hex[bytes[i] & 0xF];
		}
		out += '}';
		return out;
	}

	bool at_end() const
	{
		int c = std::fgetc(f_);
		if (c == EOF) {
			return true;
		}
		std::ungetc(c, f_);
		return false;
	}

private:
	uint64_t read_be(int bytes)
	{
		uint8_t b[8] = { 0 };
		std::fread(b, 1, bytes, f_);
		uint64_t v = 0;
		for (int i = 0; i < bytes; i++) {
			v = (v << 8) | b[i];
		}
		return v;
	}

	std::FILE *f_;
};

}
