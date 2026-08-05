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

#ifndef OAK_EDITOR_XMLUTILS_H
#define OAK_EDITOR_XMLUTILS_H

#include "common/error.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct OakCommonXmlReader OakCommonXmlReader;
typedef struct OakCommonXmlWriter OakCommonXmlWriter;

/**
 * @brief Create a streaming XML reader over a complete document.
 *
 * @param data NUL-terminated XML text. Must not be NULL.
 * @return A new reader, or NULL on failure (NULL data, out of memory).
 */
OakCommonXmlReader *oakcommon_xml_reader_init(const char *data);

/**
 * @brief Destroy a reader. No-op on NULL.
 */
void oakcommon_xml_reader_free(OakCommonXmlReader *reader);

/**
 * @brief Advance until the next start element, an end element, or the end
 *        of the document.
 *
 * @param reader Reader handle.
 * @param found Out: 1 if positioned on a start element, 0 otherwise.
 * @return OAKCOMMON_OK or a negative OAKCOMMON_E_* error code.
 */
int oakcommon_xml_reader_read_next_start_element(OakCommonXmlReader *reader,
												 int *found);

/**
 * @brief Name of the current element token.
 *
 * @return Required buffer size in bytes (including NUL), or a negative
 *         OAKCOMMON_E_* error code.
 */
int oakcommon_xml_reader_name(OakCommonXmlReader *reader, char *buf,
							  int buf_size);

/**
 * @brief Read the concatenated character data of the current element.
 *
 * Must be called on a start element; consumes up to the matching end
 * element.
 *
 * @return Required buffer size in bytes (including NUL), or a negative
 *         OAKCOMMON_E_* error code.
 */
int oakcommon_xml_reader_read_element_text(OakCommonXmlReader *reader,
										   char *buf, int buf_size);

/**
 * @brief Skip the current element and all of its children.
 *
 * @return OAKCOMMON_OK or a negative OAKCOMMON_E_* error code.
 */
int oakcommon_xml_reader_skip_current_element(OakCommonXmlReader *reader);

/**
 * @brief Number of attributes on the current start element.
 *
 * @return OAKCOMMON_OK or a negative OAKCOMMON_E_* error code.
 */
int oakcommon_xml_reader_attribute_count(OakCommonXmlReader *reader,
										 int *count);

/**
 * @brief Name of the attribute at @p index on the current start element.
 *
 * @return Required buffer size in bytes (including NUL), OAKCOMMON_E_NOT_FOUND
 *         if @p index is out of range, or another negative OAKCOMMON_E_* code.
 */
int oakcommon_xml_reader_attribute_name(OakCommonXmlReader *reader, int index,
										char *buf, int buf_size);

/**
 * @brief Value of the attribute at @p index on the current start element.
 *
 * @return Required buffer size in bytes (including NUL), OAKCOMMON_E_NOT_FOUND
 *         if @p index is out of range, or another negative OAKCOMMON_E_* code.
 */
int oakcommon_xml_reader_attribute_value(OakCommonXmlReader *reader,
										 int index, char *buf, int buf_size);

/**
 * @brief Whether the document failed to parse.
 *
 * @return OAKCOMMON_OK or a negative OAKCOMMON_E_* error code.
 */
int oakcommon_xml_reader_has_error(OakCommonXmlReader *reader,
								   int *has_error);

/**
 * @brief Create a streaming XML writer.
 *
 * @return A new writer, or NULL on failure.
 */
OakCommonXmlWriter *oakcommon_xml_writer_init(void);

/**
 * @brief Destroy a writer. No-op on NULL.
 */
void oakcommon_xml_writer_free(OakCommonXmlWriter *writer);

int oakcommon_xml_writer_write_start_element(OakCommonXmlWriter *writer,
											 const char *name);
int oakcommon_xml_writer_write_attribute(OakCommonXmlWriter *writer,
										 const char *name, const char *value);
int oakcommon_xml_writer_write_characters(OakCommonXmlWriter *writer,
										  const char *text);
int oakcommon_xml_writer_write_text_element(OakCommonXmlWriter *writer,
											const char *name,
											const char *text);
int oakcommon_xml_writer_write_end_element(OakCommonXmlWriter *writer);
int oakcommon_xml_writer_write_end_document(OakCommonXmlWriter *writer);

/**
 * @brief The document written so far.
 *
 * @return Required buffer size in bytes (including NUL), or a negative
 *         OAKCOMMON_E_* error code.
 */
int oakcommon_xml_writer_output(OakCommonXmlWriter *writer, char *buf,
								int buf_size);

#ifdef __cplusplus
}
#endif

#endif // OAK_EDITOR_XMLUTILS_H
