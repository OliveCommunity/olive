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

struct OakCommonXmlReader {
	olive::XmlStreamReader reader;
	std::string cached_text;
	bool has_cached_text = false;

	explicit OakCommonXmlReader(const char *data)
		: reader(data)
	{
	}
};

struct OakCommonXmlWriter {
	olive::XmlStreamWriter writer;
};

namespace
{

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

OakCommonXmlReader *oakcommon_xml_reader_init(const char *data)
{
	if (!data)
		return nullptr;
	try {
		return new (std::nothrow) OakCommonXmlReader(data);
	} catch (...) {
		return nullptr;
	}
}

void oakcommon_xml_reader_free(OakCommonXmlReader *reader)
{
	delete reader;
}

int oakcommon_xml_reader_read_next_start_element(OakCommonXmlReader *reader,
												 int *found)
{
	if (!reader || !found)
		return OAKCOMMON_E_INVALID;
	try {
		reader->has_cached_text = false;
		*found = olive::xml_read_next_start_element(&reader->reader) ? 1 : 0;
		return OAKCOMMON_OK;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_xml_reader_name(OakCommonXmlReader *reader, char *buf,
							  int buf_size)
{
	if (!reader)
		return OAKCOMMON_E_INVALID;
	try {
		return copy_string(reader->reader.name(), buf, buf_size);
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_xml_reader_read_element_text(OakCommonXmlReader *reader,
										   char *buf, int buf_size)
{
	if (!reader)
		return OAKCOMMON_E_INVALID;
	try {
		// read_element_text() consumes the stream, so cache the result to
		// keep the two-stage (size query then copy) buffer convention working.
		if (!reader->has_cached_text) {
			reader->cached_text = reader->reader.read_element_text();
			reader->has_cached_text = true;
		}
		return copy_string(reader->cached_text, buf, buf_size);
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_xml_reader_skip_current_element(OakCommonXmlReader *reader)
{
	if (!reader)
		return OAKCOMMON_E_INVALID;
	try {
		reader->has_cached_text = false;
		reader->reader.skip_current_element();
		return OAKCOMMON_OK;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_xml_reader_attribute_count(OakCommonXmlReader *reader,
										 int *count)
{
	if (!reader || !count)
		return OAKCOMMON_E_INVALID;
	try {
		*count = static_cast<int>(reader->reader.attributes().size());
		return OAKCOMMON_OK;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_xml_reader_attribute_name(OakCommonXmlReader *reader, int index,
										char *buf, int buf_size)
{
	if (!reader)
		return OAKCOMMON_E_INVALID;
	try {
		const auto &attrs = reader->reader.attributes();
		if (index < 0 || index >= static_cast<int>(attrs.size()))
			return OAKCOMMON_E_NOT_FOUND;
		return copy_string(attrs[index].name, buf, buf_size);
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_xml_reader_attribute_value(OakCommonXmlReader *reader,
										 int index, char *buf, int buf_size)
{
	if (!reader)
		return OAKCOMMON_E_INVALID;
	try {
		const auto &attrs = reader->reader.attributes();
		if (index < 0 || index >= static_cast<int>(attrs.size()))
			return OAKCOMMON_E_NOT_FOUND;
		return copy_string(attrs[index].value, buf, buf_size);
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_xml_reader_has_error(OakCommonXmlReader *reader,
								   int *has_error)
{
	if (!reader || !has_error)
		return OAKCOMMON_E_INVALID;
	try {
		*has_error = reader->reader.has_error() ? 1 : 0;
		return OAKCOMMON_OK;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

OakCommonXmlWriter *oakcommon_xml_writer_init(void)
{
	try {
		return new (std::nothrow) OakCommonXmlWriter();
	} catch (...) {
		return nullptr;
	}
}

void oakcommon_xml_writer_free(OakCommonXmlWriter *writer)
{
	delete writer;
}

int oakcommon_xml_writer_write_start_element(OakCommonXmlWriter *writer,
											 const char *name)
{
	if (!writer || !name)
		return OAKCOMMON_E_INVALID;
	try {
		writer->writer.write_start_element(name);
		return OAKCOMMON_OK;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_xml_writer_write_attribute(OakCommonXmlWriter *writer,
										 const char *name, const char *value)
{
	if (!writer || !name || !value)
		return OAKCOMMON_E_INVALID;
	try {
		writer->writer.write_attribute(name, value);
		return OAKCOMMON_OK;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_xml_writer_write_characters(OakCommonXmlWriter *writer,
										  const char *text)
{
	if (!writer || !text)
		return OAKCOMMON_E_INVALID;
	try {
		writer->writer.write_characters(text);
		return OAKCOMMON_OK;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_xml_writer_write_text_element(OakCommonXmlWriter *writer,
											const char *name,
											const char *text)
{
	if (!writer || !name || !text)
		return OAKCOMMON_E_INVALID;
	try {
		writer->writer.write_text_element(name, text);
		return OAKCOMMON_OK;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_xml_writer_write_end_element(OakCommonXmlWriter *writer)
{
	if (!writer)
		return OAKCOMMON_E_INVALID;
	try {
		writer->writer.write_end_element();
		return OAKCOMMON_OK;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_xml_writer_write_end_document(OakCommonXmlWriter *writer)
{
	if (!writer)
		return OAKCOMMON_E_INVALID;
	try {
		writer->writer.write_end_document();
		return OAKCOMMON_OK;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_xml_writer_output(OakCommonXmlWriter *writer, char *buf,
								int buf_size)
{
	if (!writer)
		return OAKCOMMON_E_INVALID;
	try {
		return copy_string(writer->writer.output(), buf, buf_size);
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

} // extern "C"
