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

#include "xmlutils.h"

#include <cstdio>
#include <cstring>

#include <expat.h>

namespace olive
{

namespace
{

/**
 * @brief Expat user-data used while parsing a document into an event list.
 */
struct ParseContext {
	std::vector<XmlStreamReader::Event> *events;
	bool has_error;
	std::string error_string;
};

void XMLCALL on_start_element(void *user_data, const XML_Char *name,
							  const XML_Char **atts)
{
	ParseContext *ctx = static_cast<ParseContext *>(user_data);
	XmlStreamReader::Event ev;
	ev.type = XmlStreamReader::StartElement;
	ev.name = name;
	for (int i = 0; atts[i]; i += 2)
		ev.attributes.push_back({atts[i], atts[i + 1]});
	ctx->events->push_back(std::move(ev));
}

void XMLCALL on_end_element(void *user_data, const XML_Char *name)
{
	ParseContext *ctx = static_cast<ParseContext *>(user_data);
	XmlStreamReader::Event ev;
	ev.type = XmlStreamReader::EndElement;
	ev.name = name;
	ctx->events->push_back(std::move(ev));
}

void XMLCALL on_characters(void *user_data, const XML_Char *s, int len)
{
	ParseContext *ctx = static_cast<ParseContext *>(user_data);
	if (!len)
		return;
	if (!ctx->events->empty() &&
		ctx->events->back().type == XmlStreamReader::Characters) {
		ctx->events->back().text.append(s, len);
		return;
	}
	XmlStreamReader::Event ev;
	ev.type = XmlStreamReader::Characters;
	ev.text.assign(s, len);
	ctx->events->push_back(std::move(ev));
}

std::string escape_text(const std::string &in)
{
	std::string out;
	out.reserve(in.size());
	for (char c : in) {
		switch (c) {
		case '&': out += "&amp;"; break;
		case '<': out += "&lt;"; break;
		case '>': out += "&gt;"; break;
		default: out += c; break;
		}
	}
	return out;
}

std::string escape_attribute(const std::string &in)
{
	std::string out;
	out.reserve(in.size());
	for (char c : in) {
		switch (c) {
		case '&': out += "&amp;"; break;
		case '<': out += "&lt;"; break;
		case '>': out += "&gt;"; break;
		case '"': out += "&quot;"; break;
		default: out += c; break;
		}
	}
	return out;
}

} // namespace

XmlStreamReader::XmlStreamReader(const std::string &data)
	: pos_(0)
	, token_(Invalid)
	, has_error_(false)
{
	ParseContext ctx{&events_, false, std::string()};

	XML_Parser parser = XML_ParserCreate(nullptr);
	if (!parser) {
		has_error_ = true;
		error_string_ = "failed to create XML parser";
		return;
	}

	XML_SetUserData(parser, &ctx);
	XML_SetElementHandler(parser, on_start_element, on_end_element);
	XML_SetCharacterDataHandler(parser, on_characters);

	if (XML_Parse(parser, data.data(), static_cast<int>(data.size()),
				  XML_TRUE) == XML_STATUS_ERROR) {
		char buf[256];
		snprintf(buf, sizeof(buf), "%s at line %lu",
				 XML_ErrorString(XML_GetErrorCode(parser)),
				 static_cast<unsigned long>(
					 XML_GetCurrentLineNumber(parser)));
		has_error_ = true;
		error_string_ = buf;
	}

	XML_ParserFree(parser);
}

XmlStreamReader::TokenType XmlStreamReader::read_next()
{
	attributes_.clear();
	text_.clear();
	name_.clear();

	if (has_error_) {
		token_ = Invalid;
		return token_;
	}

	if (pos_ >= events_.size()) {
		token_ = EndDocument;
		return token_;
	}

	const Event &ev = events_[pos_++];
	token_ = ev.type;
	name_ = ev.name;
	text_ = ev.text;
	attributes_ = ev.attributes;
	return token_;
}

std::string XmlStreamReader::read_element_text()
{
	if (!is_start_element())
		return std::string();

	std::string result;
	int depth = 1;
	while (true) {
		TokenType t = read_next();
		if (t == Invalid || t == EndDocument)
			break;
		if (t == StartElement) {
			depth++;
		} else if (t == EndElement) {
			if (--depth == 0)
				break;
		} else if (t == Characters && depth == 1) {
			result += text_;
		}
	}
	return result;
}

void XmlStreamReader::skip_current_element()
{
	if (!is_start_element())
		return;

	int depth = 1;
	while (true) {
		TokenType t = read_next();
		if (t == Invalid || t == EndDocument)
			break;
		if (t == StartElement)
			depth++;
		else if (t == EndElement && --depth == 0)
			break;
	}
}

XmlStreamWriter::XmlStreamWriter()
	: open_start_tag_(false)
{
}

void XmlStreamWriter::write_start_element(const std::string &name)
{
	if (open_start_tag_) {
		output_ += '>';
		open_start_tag_ = false;
	}
	output_ += '<';
	output_ += name;
	stack_.push_back(name);
	open_start_tag_ = true;
}

void XmlStreamWriter::write_attribute(const std::string &name,
									  const std::string &value)
{
	if (!open_start_tag_)
		return;
	output_ += ' ';
	output_ += name;
	output_ += "=\"";
	output_ += escape_attribute(value);
	output_ += '"';
}

void XmlStreamWriter::write_characters(const std::string &text)
{
	if (open_start_tag_) {
		output_ += '>';
		open_start_tag_ = false;
	}
	output_ += escape_text(text);
}

void XmlStreamWriter::write_text_element(const std::string &name,
										 const std::string &text)
{
	write_start_element(name);
	write_characters(text);
	write_end_element();
}

void XmlStreamWriter::write_end_element()
{
	if (stack_.empty())
		return;

	const std::string name = stack_.back();
	stack_.pop_back();

	if (open_start_tag_) {
		output_ += "/>";
		open_start_tag_ = false;
	} else {
		output_ += "</";
		output_ += name;
		output_ += '>';
	}
}

void XmlStreamWriter::write_end_document()
{
	while (!stack_.empty())
		write_end_element();
}

bool xml_read_next_start_element(XmlStreamReader *reader)
{
	XmlStreamReader::TokenType token;

	while ((token = reader->read_next()) != XmlStreamReader::Invalid &&
		   token != XmlStreamReader::EndDocument) {
		if (reader->is_end_element())
			return false;
		if (reader->is_start_element())
			return true;
	}

	return false;
}

}
