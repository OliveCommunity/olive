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

#ifndef OAK_TIMELINECOORDINATE_H
#define OAK_TIMELINECOORDINATE_H

#include <olive/core/util/rational.h>

#include "common/trackreferencehandle.h"

namespace olive
{

using namespace core;

class TimelineCoordinate {
public:
	TimelineCoordinate();
	TimelineCoordinate(const Rational &frame, const TrackReference &track);
	TimelineCoordinate(const Rational &frame, const TrackReference::Type &track_type,
					   const int &track_index);

	const Rational &get_frame() const;
	const TrackReference &get_track() const;

	void set_frame(const Rational &frame);
	void set_track(const TrackReference &track);

private:
	Rational frame_;

	TrackReference track_;
};

}

#endif // OAK_TIMELINECOORDINATE_H
