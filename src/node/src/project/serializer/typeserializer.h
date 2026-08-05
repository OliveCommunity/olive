/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2023 Olive Team
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

#ifndef OAK_TYPESERIALIZER_H
#define OAK_TYPESERIALIZER_H

#include <olive/core/core.h>

#include "xmlutils.h"

namespace olive
{

using namespace core;

class TypeSerializer {
public:
	TypeSerializer() = default;

	static AudioParams load_audio_params(XmlStreamReader *reader);
	static void save_audio_params(XmlStreamWriter *writer, const AudioParams &a);
};

}

#endif // OAK_TYPESERIALIZER_H
