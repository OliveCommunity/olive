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

#include <olive/core/core.h>

#include "oakengine/node.h"

namespace olive
{

using olive::core::Rational;
using olive::core::TimeRange;

/**
 * @brief Direction constants mirroring the engine's
 * Node::TransformTimeDirection (engine/node/node.h — ordinals must stay in
 * sync; they are passed straight through to
 * oakengine_node_transform_time_to()). Use these where the engine enum is
 * unavailable; the engine enum values convert to int implicitly and remain
 * valid at existing call sites.
 */
enum TransformTimeDirection {
	k_transform_towards_input,
	k_transform_towards_output
};

class TimeTargetObject {
public:
	TimeTargetObject();

	OakEngineNode *get_time_target() const;
	void set_time_target(OakEngineNode *target);

	void set_path_index(int index);

	/**
	 * `dir` mirrors the engine's Node::TransformTimeDirection ordinals
	 * (0 = k_transform_towards_input, 1 = k_transform_towards_output;
	 * engine/node/node.h — update both sides together) and is passed
	 * straight through to oakengine_node_transform_time_to(). Callers may
	 * keep passing the engine enum values, which convert to int implicitly.
	 */
	Rational get_adjusted_time(OakEngineNode *from, OakEngineNode *to,
							 const Rational &r, int dir) const;
	TimeRange get_adjusted_time(OakEngineNode *from, OakEngineNode *to,
							  const TimeRange &r, int dir) const;

	//int GetNumberOfPathAdjustments(Node* from, NodeParam::Type direction) const;

protected:
	virtual void TimeTargetDisconnectEvent(OakEngineNode *)
	{
	}
	virtual void TimeTargetChangedEvent(OakEngineNode *)
	{
	}
	virtual void TimeTargetConnectEvent(OakEngineNode *)
	{
	}

private:
	OakEngineNode *time_target_;

	int path_index_;
};

}

#endif // OAK_TIMETARGETOBJECT_H
