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

#include "timetarget.h"

#include "oakengine/node.h"

namespace olive
{

TimeTargetObject::TimeTargetObject()
	: time_target_(nullptr)
	, path_index_(0)
{
}

OakEngineNode *TimeTargetObject::get_time_target() const
{
	return time_target_;
}

void TimeTargetObject::set_time_target(OakEngineNode *target)
{
	if (time_target_) {
		TimeTargetDisconnectEvent(time_target_);
	}

	time_target_ = target;
	TimeTargetChangedEvent(time_target_);

	if (time_target_) {
		TimeTargetConnectEvent(time_target_);
	}
}

void TimeTargetObject::set_path_index(int index)
{
	path_index_ = index;
}

Rational
TimeTargetObject::get_adjusted_time(OakEngineNode *from, OakEngineNode *to,
								  const Rational &r, int dir) const
{
	if (!from || !to) {
		return r;
	}

	return get_adjusted_time(from, to, TimeRange(r, r), dir).in();
}

TimeRange
TimeTargetObject::get_adjusted_time(OakEngineNode *from, OakEngineNode *to,
								  const TimeRange &r, int dir) const
{
	if (!from || !to) {
		return r;
	}

	int64_t rin_num, rin_den, rout_num, rout_den;
	oakengine_node_transform_time_to(
		from, to,
		dir, path_index_,
		r.in().numerator(), r.in().denominator(),
		r.out().numerator(), r.out().denominator(),
		&rin_num, &rin_den, &rout_num, &rout_den);
	return TimeRange(Rational(rin_num, rin_den), Rational(rout_num, rout_den));
}

/*int TimeTargetObject::GetNumberOfPathAdjustments(Node* from, NodeParam::Type direction) const
{
  if (!time_target_) {
    return 0;
  }

  QList<TimeRange> adjusted = from->TransformTimeTo(TimeRange(), time_target_, direction);

  return adjusted.size();
}*/

}
