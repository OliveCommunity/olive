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

#include "common/xmlutils.h"

#include <cstring>
#include <new>

#include "../src/xmlutils.h"
#include "refcounted.h"

namespace
{

/**
 * @brief Reader state boxed behind the handle's ctx pointer.
 */
struct XmlReaderState {
	olive::XmlStreamReader reader;
	std::string cached_text;
	bool has_cached_text = false;

	explicit XmlReaderState(const char *data)
		: reader(data)
	{
	}
};

/**
 * @brief Recover the boxed reader state from a handle (NULL-safe).
 */
XmlReaderState *xr(OakXmlReader reader)
{
	return oakcommon::handle_impl<XmlReaderState>(reader.ctx);
}

/**
 * @brief Recover the boxed writer from a handle (NULL-safe).
 */
olive::XmlStreamWriter *xw(OakXmlWriter writer)
{
	return oakcommon::handle_impl<olive::XmlStreamWriter>(writer.ctx);
}

/**
 * @brief Copy @p value into the two-stage string buffer.
 *
 * @return Required buffer size in bytes (including NUL). The buffer is only
 *         written to when it is large enough.
 */
int copy_string(const std::string &value, char *buf, int buf_size)
{
	int needed = static_cast<int>(value.size()) + 1;
	if (buf && buf_size >= needed) {
		memcpy(buf, value.c_str(), needed);
	}
	return needed;
}

} // namespace

extern "C" {

OakXmlReader oakcommon_xml_reader_init(const char *data)
{
	OakXmlReader h = {};
	if (!data)
		return h;
	try {
		return oakcommon::make_handle<OakXmlReader>(
			XmlReaderState(data));
	} catch (...) {
		OakXmlReader empty = {};
		return empty;
	}
}

void oakcommon_xml_reader_free(OakXmlReader *reader)
{
	oakcommon::free_handle(reader);
}

int oakcommon_xml_reader_read_next_start_element(OakXmlReader reader,
												 int *found)
{
	if (!xr(reader) || !found)
		return OAKCOMMON_E_INVALID;
	try {
		xr(reader)->has_cached_text = false;
		*found = olive::xml_read_next_start_element(&xr(reader)->reader) ? 1 : 0;
		return OAKCOMMON_OK;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_xml_reader_name(OakXmlReader reader, char *buf,
							  int buf_size)
{
	if (!xr(reader))
		return OAKCOMMON_E_INVALID;
	try {
		return copy_string(xr(reader)->reader.name(), buf, buf_size);
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_xml_reader_read_element_text(OakXmlReader reader,
										   char *buf, int buf_size)
{
	if (!xr(reader))
		return OAKCOMMON_E_INVALID;
	try {
		// read_element_text() consumes the stream, so cache the result to
		// keep the two-stage (size query then copy) buffer convention working.
		if (!xr(reader)->has_cached_text) {
			xr(reader)->cached_text = xr(reader)->reader.read_element_text();
			xr(reader)->has_cached_text = true;
		}
		return copy_string(xr(reader)->cached_text, buf, buf_size);
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_xml_reader_skip_current_element(OakXmlReader reader)
{
	if (!xr(reader))
		return OAKCOMMON_E_INVALID;
	try {
		xr(reader)->has_cached_text = false;
		xr(reader)->reader.skip_current_element();
		return OAKCOMMON_OK;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_xml_reader_attribute_count(OakXmlReader reader,
										 int *count)
{
	if (!xr(reader) || !count)
		return OAKCOMMON_E_INVALID;
	try {
		*count = static_cast<int>(xr(reader)->reader.attributes().size());
		return OAKCOMMON_OK;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_xml_reader_attribute_name(OakXmlReader reader, int index,
										char *buf, int buf_size)
{
	if (!xr(reader))
		return OAKCOMMON_E_INVALID;
	try {
		const auto &attrs = xr(reader)->reader.attributes();
		if (index < 0 || index >= static_cast<int>(attrs.size()))
			return OAKCOMMON_E_NOT_FOUND;
		return copy_string(attrs[index].name, buf, buf_size);
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_xml_reader_attribute_value(OakXmlReader reader,
										 int index, char *buf, int buf_size)
{
	if (!xr(reader))
		return OAKCOMMON_E_INVALID;
	try {
		const auto &attrs = xr(reader)->reader.attributes();
		if (index < 0 || index >= static_cast<int>(attrs.size()))
			return OAKCOMMON_E_NOT_FOUND;
		return copy_string(attrs[index].value, buf, buf_size);
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_xml_reader_has_error(OakXmlReader reader,
								   int *has_error)
{
	if (!xr(reader) || !has_error)
		return OAKCOMMON_E_INVALID;
	try {
		*has_error = xr(reader)->reader.has_error() ? 1 : 0;
		return OAKCOMMON_OK;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

OakXmlWriter oakcommon_xml_writer_init(void)
{
	try {
		return oakcommon::make_handle<OakXmlWriter>(
			olive::XmlStreamWriter());
	} catch (...) {
		OakXmlWriter h = {};
		return h;
	}
}

void oakcommon_xml_writer_free(OakXmlWriter *writer)
{
	oakcommon::free_handle(writer);
}

int oakcommon_xml_writer_write_start_element(OakXmlWriter writer,
											 const char *name)
{
	if (!xw(writer) || !name)
		return OAKCOMMON_E_INVALID;
	try {
		xw(writer)->write_start_element(name);
		return OAKCOMMON_OK;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_xml_writer_write_attribute(OakXmlWriter writer,
										 const char *name, const char *value)
{
	if (!xw(writer) || !name || !value)
		return OAKCOMMON_E_INVALID;
	try {
		xw(writer)->write_attribute(name, value);
		return OAKCOMMON_OK;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_xml_writer_write_characters(OakXmlWriter writer,
										  const char *text)
{
	if (!xw(writer) || !text)
		return OAKCOMMON_E_INVALID;
	try {
		xw(writer)->write_characters(text);
		return OAKCOMMON_OK;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_xml_writer_write_text_element(OakXmlWriter writer,
											const char *name,
											const char *text)
{
	if (!xw(writer) || !name || !text)
		return OAKCOMMON_E_INVALID;
	try {
		xw(writer)->write_text_element(name, text);
		return OAKCOMMON_OK;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_xml_writer_write_end_element(OakXmlWriter writer)
{
	if (!xw(writer))
		return OAKCOMMON_E_INVALID;
	try {
		xw(writer)->write_end_element();
		return OAKCOMMON_OK;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_xml_writer_write_end_document(OakXmlWriter writer)
{
	if (!xw(writer))
		return OAKCOMMON_E_INVALID;
	try {
		xw(writer)->write_end_document();
		return OAKCOMMON_OK;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_xml_writer_output(OakXmlWriter writer, char *buf,
								int buf_size)
{
	if (!xw(writer))
		return OAKCOMMON_E_INVALID;
	try {
		return copy_string(xw(writer)->output(), buf, buf_size);
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

} // extern "C"
