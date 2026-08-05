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

#ifndef OAK_NODEINPUTDRAGGER_H
#define OAK_NODEINPUTDRAGGER_H

#include <vector>

#include "keyframe.h"
#include "param.h"
#include "undocommand.h"

namespace olive
{

class NodeInputDragger {
public:
	NodeInputDragger();

	bool is_started() const;

	void start(const NodeKeyframeTrackReference &input, const Rational &time,
			   bool create_key_on_all_tracks = true);

	void drag(Variant value);

	void end(MultiUndoCommand *command);

	static bool is_input_being_dragged()
	{
		return input_being_dragged;
	}

	const Variant &get_start_value() const
	{
		return start_value_;
	}

	const NodeKeyframeTrackReference &get_input() const
	{
		return input_;
	}

	const Rational &get_time() const
	{
		return time_;
	}

private:
	NodeKeyframeTrackReference input_;

	Rational time_;

	Variant start_value_;

	Variant end_value_;

	NodeKeyframe *dragging_key_;
	std::vector<NodeKeyframe *> created_keys_;

	static int input_being_dragged;
};

}

#endif // OAK_NODEINPUTDRAGGER_H
