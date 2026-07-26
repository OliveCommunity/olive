/*
 * Oak Video Editor - Non-Linear Video Editor
 * Copyright (C) 2025 Olive CE Team
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef OAK_HTMLAPP_H
#define OAK_HTML_H

#include <QTextDocument>
#include <QTextFragment>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

namespace olive
{

/**
 * @brief Functions for converting HTML to QTextDocument and vice versa
 *
 * Qt does contain its own functions for this, however they have some limitations. Some things that
 * we want to support (e.g. kerning/spacing and font stretch) are not implemented in Qt's
 * QTextHtmlExporter and QTextHtmlParser. Additionally, since these functions are not part of Qt's
 * public API, and make many references to other parts of Qt that are not part of the public API,
 * there is no way to subclass or extend their functionality without forking Qt as a whole.
 *
 * Therefore, it became necessary to write a custom class for the conversion so that we can
 * ensure support for the features we need.
 *
 * If someone wishes to extend this class for more feature support, feel free to open a pull
 * request. But this is NOT intended to be an exhaustive HTML implementation, and is primarily
 * designed to store rich text in a standard format for the purpose of text formatting for video.
 */
class Html {
public:
	static QString doc_to_html(const QTextDocument *doc);

	static void html_to_doc(QTextDocument *doc, const QString &html);

private:
	static void write_block(QXmlStreamWriter *writer, const QTextBlock &block);

	static void write_fragment(QXmlStreamWriter *writer,
							  const QTextFragment &fragment);

	static void write_css_property(QString *style, const QString &key,
								 const QStringList &value);
	static void write_css_property(QString *style, const QString &key,
								 const QString &value)
	{
		write_css_property(style, key, QStringList({ value }));
	}

	static void write_char_format(QString *style, const QTextCharFormat &fmt);

	static QTextCharFormat
	read_char_format(const QXmlStreamAttributes &attributes);

	static QTextBlockFormat
	read_block_format(const QXmlStreamAttributes &attributes);

	static void append_string_auto_space(QString *s, const QString &append);

	static QMap<QString, QStringList> get_css_from_style(const QString &s);

	static const QVector<QString> k_block_tags;
};

}

#endif // OAK_HTML_H
