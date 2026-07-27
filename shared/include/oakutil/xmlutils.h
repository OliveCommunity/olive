/***

  Oak - Non-Linear Video Editor
  Copyright (C) 2022 Olive Team
  Modifications Copyright (C) 2025 mikesolar

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

#ifndef OAK_XMLUTILS_SHARED_H
#define OAK_XMLUTILS_SHARED_H

#include <QXmlStreamReader>

namespace olive
{

#define XMLAttributeLoop(reader, item) \
	foreach (const QXmlStreamAttribute &item, reader->attributes())

/**
 * @brief Workaround for QXmlStreamReader::readNextStartElement not detecting the end of a document
 *
 * Since Qt's default function doesn't exit at the end of the document, it ends up consistently
 * throwing a "premature end of document" error. We have our own function here that does essentially
 * the same thing but fixes that issue.
 *
 * See also: https://stackoverflow.com/questions/46346450/qt-qxmlstreamreader-always-returns-premature-end-of-document-error
 *
 * @param cancel_atom Optional pointer to a CancelAtom (from engine) for cancellation support.
 *        Pass nullptr when calling from app code without engine dependencies.
 */
bool xml_read_next_start_element(QXmlStreamReader *reader,
								void *cancel_atom = nullptr);

}

#endif // OAK_XMLUTILS_SHARED_H
