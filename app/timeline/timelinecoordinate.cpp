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

#include "timelinecoordinate.h"

namespace olive
{

TimelineCoordinate::TimelineCoordinate()
	: track_(TrackReference::k_none, 0)
{
}

TimelineCoordinate::TimelineCoordinate(const Rational &frame,
									   const TrackReference &track)
	: frame_(frame)
	, track_(track)
{
}

TimelineCoordinate::TimelineCoordinate(const Rational &frame,
									   const TrackReference::Type &track_type,
									   const int &track_index)
	: frame_(frame)
	, track_(track_type, track_index)
{
}

const Rational &TimelineCoordinate::get_frame() const
{
	return frame_;
}

const TrackReference &TimelineCoordinate::get_track() const
{
	return track_;
}

void TimelineCoordinate::set_frame(const Rational &frame)
{
	frame_ = frame;
}

void TimelineCoordinate::set_track(const TrackReference &track)
{
	track_ = track;
}

}
