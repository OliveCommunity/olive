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

#ifndef OAK_TIMETARGETOBJECT_H
#define OAK_TIMETARGETOBJECT_H

#include "node/output/viewer/viewer.h"

namespace olive
{

class TimeTargetObject {
public:
	TimeTargetObject();

	ViewerOutput *get_time_target() const;
	void set_time_target(ViewerOutput *target);

	void set_path_index(int index);

	Rational get_adjusted_time(Node *from, Node *to, const Rational &r,
							 Node::TransformTimeDirection dir) const;
	TimeRange get_adjusted_time(Node *from, Node *to, const TimeRange &r,
							  Node::TransformTimeDirection dir) const;

	//int GetNumberOfPathAdjustments(Node* from, NodeParam::Type direction) const;

protected:
	virtual void TimeTargetDisconnectEvent(ViewerOutput *)
	{
	}
	virtual void TimeTargetChangedEvent(ViewerOutput *)
	{
	}
	virtual void TimeTargetConnectEvent(ViewerOutput *)
	{
	}

private:
	ViewerOutput *time_target_;

	int path_index_;
};

}

#endif // OAK_TIMETARGETOBJECT_H
