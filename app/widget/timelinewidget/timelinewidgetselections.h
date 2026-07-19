/***

  Olive - Non-Linear Video Editor
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

#ifndef OAK_TIMELINEWIDGETSELECTIONS_H
#define OAK_TIMELINEWIDGETSELECTIONS_H

#include <QHash>

#include "node/output/track/track.h"

namespace olive
{

class TimelineWidgetSelections : public QHash<Track::Reference, TimeRangeList> {
public:
	TimelineWidgetSelections() = default;

	void shift_time(const Rational &diff);

	void shift_tracks(Track::Type type, int diff);

	void trim_in(const Rational &diff);

	void trim_out(const Rational &diff);

	void subtract(const TimelineWidgetSelections &selections);

	TimelineWidgetSelections
	subtracted(const TimelineWidgetSelections &selections) const
	{
		TimelineWidgetSelections copy = *this;
		copy.subtract(selections);
		return copy;
	}
};

}

#endif // OAK_TIMELINEWIDGETSELECTIONS_H
