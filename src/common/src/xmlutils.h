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

#ifndef OAK_XMLUTILS_H
#define OAK_XMLUTILS_H

#include <string>
#include <vector>

namespace olive
{

/**
 * @brief A single XML attribute (name/value pair).
 *
 * De-Qt replacement for QXmlStreamAttribute.
 */
struct XmlStreamAttribute {
	std::string name;  /**< Attribute name. */
	std::string value; /**< Attribute value (entity references resolved). */
};

/**
 * @brief Minimal pull-style streaming XML reader (de-Qt replacement for QXmlStreamReader).
 *
 * Backed by expat. The whole document is parsed at construction time into an
 * internal event list; navigation is done with read_next() and the state
 * accessors, mirroring the QXmlStreamReader call patterns used by the engine
 * (read_next_start_element loop, attribute iteration, read_element_text(),
 * skip_current_element()).
 *
 * Only the subset of XML actually used by Oak project/config files is
 * supported: elements, attributes, character data, comments and processing
 * instructions (the latter two are skipped transparently by expat handler
 * omission). Namespace processing is not performed.
 */
class XmlStreamReader
{
public:
	/**
	 * @brief Token types, mirroring QXmlStreamReader::TokenType subset.
	 */
	enum TokenType {
		Invalid,     /**< Parse error occurred. */
		StartElement,
		EndElement,
		Characters,
		EndDocument
	};

	/**
	 * @brief Construct a reader over a complete XML document.
	 *
	 * @param data The full XML text. Parse errors are reported via
	 *        has_error()/error_string() and make read_next() return Invalid.
	 */
	explicit XmlStreamReader(const std::string &data);

	/**
	 * @brief Advance to the next token.
	 *
	 * @return The new current token type.
	 */
	TokenType read_next();

	TokenType token_type() const { return token_; }
	bool is_start_element() const { return token_ == StartElement; }
	bool is_end_element() const { return token_ == EndElement; }
	bool is_characters() const { return token_ == Characters; }

	/**
	 * @brief Name of the current StartElement/EndElement token.
	 */
	const std::string &name() const { return name_; }

	/**
	 * @brief Text of the current Characters token.
	 */
	const std::string &text() const { return text_; }

	/**
	 * @brief Attributes of the current StartElement token.
	 */
	const std::vector<XmlStreamAttribute> &attributes() const { return attributes_; }

	/**
	 * @brief Read the concatenated character data of the current element.
	 *
	 * Must be called on a StartElement token. Consumes tokens up to and
	 * including the matching EndElement. Mirrors
	 * QXmlStreamReader::readElementText().
	 *
	 * @return The element's text content.
	 */
	std::string read_element_text();

	/**
	 * @brief Skip the current element and all of its children.
	 *
	 * Mirrors QXmlStreamReader::skipCurrentElement().
	 */
	void skip_current_element();

	bool has_error() const { return has_error_; }
	const std::string &error_string() const { return error_string_; }

	/**
	 * @brief One parsed SAX event. Internal; exposed for the expat handlers.
	 */
	struct Event {
		TokenType type;
		std::string name;
		std::string text;
		std::vector<XmlStreamAttribute> attributes;
	};

private:
	std::vector<Event> events_; /**< Parsed event list. */
	size_t pos_;                /**< Index of the next event to consume. */
	TokenType token_;           /**< Current token type. */
	std::string name_;          /**< Current element name. */
	std::string text_;          /**< Current character data. */
	std::vector<XmlStreamAttribute> attributes_; /**< Current attributes. */
	bool has_error_;
	std::string error_string_;
};

/**
 * @brief Minimal streaming XML writer (de-Qt replacement for QXmlStreamWriter).
 *
 * Only the functions actually used by Oak serializers are provided:
 * write_start_element/write_attribute/write_characters/write_text_element/
 * write_end_element/write_end_document. Text and attribute values are escaped
 * for the five predefined XML entities.
 */
class XmlStreamWriter
{
public:
	XmlStreamWriter();

	void write_start_element(const std::string &name);
	void write_attribute(const std::string &name, const std::string &value);
	void write_characters(const std::string &text);
	void write_text_element(const std::string &name, const std::string &text);
	void write_end_element();
	void write_end_document();

	/**
	 * @brief The document written so far.
	 */
	const std::string &output() const { return output_; }

private:
	std::string output_;             /**< Accumulated document text. */
	std::vector<std::string> stack_; /**< Open element name stack. */
	bool open_start_tag_;            /**< A '>' is pending for the open tag. */
};

/**
 * @brief Workaround for read-next-start-element not detecting the end of a document.
 *
 * De-Qt port of the original xml_read_next_start_element(). Reads until a
 * start element is found (returns true), an end element or the end of the
 * document is reached (returns false). The old cancel_atom parameter (an
 * engine/render dependency) has been removed.
 *
 * @param reader The reader to advance.
 * @return true if positioned on a start element, false otherwise.
 */
bool xml_read_next_start_element(XmlStreamReader *reader);

}

#endif // OAK_XMLUTILS_H
