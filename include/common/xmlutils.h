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
#include "common/handle.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Neutral by-value handle to a streaming XML reader.
 *
 * Ownership/count semantics follow the convention in common/handle.h:
 * init returns a handle whose object has reference count 1,
 * addref(ctx)/release(ctx) adjust it atomically, and release destroys
 * the object at zero. abi_version is always OAKCOMMON_ABI_VERSION.
 */
typedef struct OakXmlReader {
	void *ctx; /**< Opaque pointer to the reference-counted object. */
	void (*addref)(void *ctx); /**< Atomically increments the count. */
	void (*release)(void *ctx); /**< Decrements the count, destroys at 0. */
	uint32_t abi_version; /**< OAKCOMMON_ABI_VERSION. */
} OakXmlReader;

/**
 * @brief Neutral by-value handle to a streaming XML writer.
 *
 * Same ownership/count semantics as OakXmlReader.
 */
typedef struct OakXmlWriter {
	void *ctx; /**< Opaque pointer to the reference-counted object. */
	void (*addref)(void *ctx); /**< Atomically increments the count. */
	void (*release)(void *ctx); /**< Decrements the count, destroys at 0. */
	uint32_t abi_version; /**< OAKCOMMON_ABI_VERSION. */
} OakXmlWriter;

/**
 * @brief Create a streaming XML reader over a complete document.
 *
 * @param data NUL-terminated XML text. Must not be NULL.
 * @return Handle with reference count 1; ctx is NULL on failure
 *         (NULL data, out of memory).
 */
OakXmlReader oakcommon_xml_reader_init(const char *data);

/**
 * @brief Release one reference to a reader.
 *
 * Convenience wrapper around handle.release(handle.ctx): decrements the
 * atomic reference count and destroys the object when it reaches zero.
 * No-op when reader is NULL or reader->ctx is NULL.
 */
void oakcommon_xml_reader_free(OakXmlReader *reader);

/**
 * @brief Advance until the next start element, an end element, or the end
 *        of the document.
 *
 * @param reader Reader handle.
 * @param found Out: 1 if positioned on a start element, 0 otherwise.
 * @return OAKCOMMON_OK or a negative OAKCOMMON_E_* error code.
 */
int oakcommon_xml_reader_read_next_start_element(OakXmlReader reader,
												 int *found);

/**
 * @brief Name of the current element token.
 *
 * @return Required buffer size in bytes (including NUL), or a negative
 *         OAKCOMMON_E_* error code.
 */
int oakcommon_xml_reader_name(OakXmlReader reader, char *buf,
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
int oakcommon_xml_reader_read_element_text(OakXmlReader reader,
										   char *buf, int buf_size);

/**
 * @brief Skip the current element and all of its children.
 *
 * @return OAKCOMMON_OK or a negative OAKCOMMON_E_* error code.
 */
int oakcommon_xml_reader_skip_current_element(OakXmlReader reader);

/**
 * @brief Number of attributes on the current start element.
 *
 * @return OAKCOMMON_OK or a negative OAKCOMMON_E_* error code.
 */
int oakcommon_xml_reader_attribute_count(OakXmlReader reader,
										 int *count);

/**
 * @brief Name of the attribute at @p index on the current start element.
 *
 * @return Required buffer size in bytes (including NUL), OAKCOMMON_E_NOT_FOUND
 *         if @p index is out of range, or another negative OAKCOMMON_E_* code.
 */
int oakcommon_xml_reader_attribute_name(OakXmlReader reader, int index,
										char *buf, int buf_size);

/**
 * @brief Value of the attribute at @p index on the current start element.
 *
 * @return Required buffer size in bytes (including NUL), OAKCOMMON_E_NOT_FOUND
 *         if @p index is out of range, or another negative OAKCOMMON_E_* code.
 */
int oakcommon_xml_reader_attribute_value(OakXmlReader reader,
										 int index, char *buf, int buf_size);

/**
 * @brief Whether the document failed to parse.
 *
 * @return OAKCOMMON_OK or a negative OAKCOMMON_E_* error code.
 */
int oakcommon_xml_reader_has_error(OakXmlReader reader,
								   int *has_error);

/**
 * @brief Create a streaming XML writer.
 *
 * @return Handle with reference count 1; ctx is NULL on failure.
 */
OakXmlWriter oakcommon_xml_writer_init(void);

/**
 * @brief Release one reference to a writer.
 *
 * Convenience wrapper around handle.release(handle.ctx): decrements the
 * atomic reference count and destroys the object when it reaches zero.
 * No-op when writer is NULL or writer->ctx is NULL.
 */
void oakcommon_xml_writer_free(OakXmlWriter *writer);

int oakcommon_xml_writer_write_start_element(OakXmlWriter writer,
											 const char *name);
int oakcommon_xml_writer_write_attribute(OakXmlWriter writer,
										 const char *name, const char *value);
int oakcommon_xml_writer_write_characters(OakXmlWriter writer,
										  const char *text);
int oakcommon_xml_writer_write_text_element(OakXmlWriter writer,
											const char *name,
											const char *text);
int oakcommon_xml_writer_write_end_element(OakXmlWriter writer);
int oakcommon_xml_writer_write_end_document(OakXmlWriter writer);

/**
 * @brief The document written so far.
 *
 * @return Required buffer size in bytes (including NUL), or a negative
 *         OAKCOMMON_E_* error code.
 */
int oakcommon_xml_writer_output(OakXmlWriter writer, char *buf,
								int buf_size);

#ifdef __cplusplus
}
#endif

#endif // OAK_EDITOR_XMLUTILS_H
