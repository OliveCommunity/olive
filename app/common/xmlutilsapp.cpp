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

// App-side implementation of xml_read_next_start_element
// Provides a local definition so the app doesn't import this from liboakengine.

#include "common/xmlutils.h"

namespace olive
{

bool xml_read_next_start_element(QXmlStreamReader *reader,
								CancelAtom *cancel_atom)
{
	QXmlStreamReader::TokenType token;

	while ((token = reader->readNext()) != QXmlStreamReader::Invalid &&
		   token != QXmlStreamReader::EndDocument &&
		   (!cancel_atom || !cancel_atom->is_cancelled())) {
		if (reader->isEndElement()) {
			return false;
		} else if (reader->isStartElement()) {
			return true;
		}
	}

	return false;
}

}
