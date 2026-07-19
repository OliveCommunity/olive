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

#ifndef OAK_TIMECODEMETADATA_H
#define OAK_TIMECODEMETADATA_H

#include <QString>

#include "olive/core/util/rational.h"

namespace olive
{

class TimecodeMetadata {
public:
	struct SourceTime {
		core::Rational time;
		QString source;
		bool valid = false;
	};

	static SourceTime from_timecode_string(const QString &timecode,
										 const core::Rational &timebase);

	static SourceTime from_bwf_time_reference(const QString &time_reference,
										   int sample_rate);
};

}

#endif // OAK_TIMECODEMETADATA_H
