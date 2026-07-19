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

#include "keyframeviewinputconnection.h"

#include "keyframeview.h"

namespace olive
{

KeyframeViewInputConnection::KeyframeViewInputConnection(
	const NodeKeyframeTrackReference &input, KeyframeView *parent)
	: QObject(parent)
	, keyframe_view_(parent)
	, input_(input)
	, y_(0)
	, y_behavior_(k_single_row)
	, brush_(Qt::white)
{
	Node *n = input.input().node();

	connect(n, &Node::keyframe_added, this,
			&KeyframeViewInputConnection::add_keyframe);
	connect(n, &Node::keyframe_removed, this,
			&KeyframeViewInputConnection::remove_keyframe);
	connect(n, &Node::keyframe_time_changed, this,
			&KeyframeViewInputConnection::keyframe_changed);
	connect(n, &Node::keyframe_type_changed, this,
			&KeyframeViewInputConnection::keyframe_changed);
	connect(n, &Node::keyframe_type_changed, this,
			&KeyframeViewInputConnection::keyframe_type_changed);
	connect(n, &Node::keyframe_value_changed, this,
			&KeyframeViewInputConnection::keyframe_changed);
}

void KeyframeViewInputConnection::set_keyframe_y(int y)
{
	if (y_ != y) {
		y_ = y;

		emit require_update();
	}
}

void KeyframeViewInputConnection::set_y_behavior(YBehavior e)
{
	if (y_behavior_ != e) {
		y_behavior_ = e;

		emit require_update();
	}
}

void KeyframeViewInputConnection::set_brush(const QBrush &brush)
{
	if (brush_ != brush) {
		brush_ = brush;

		emit require_update();
	}
}

void KeyframeViewInputConnection::add_keyframe(NodeKeyframe *key)
{
	if (key->key_track_ref() == input_) {
		emit require_update();
	}
}

void KeyframeViewInputConnection::remove_keyframe(NodeKeyframe *key)
{
	if (key->key_track_ref() == input_) {
		emit require_update();
	}
}

void KeyframeViewInputConnection::keyframe_changed(NodeKeyframe *key)
{
	if (key->key_track_ref() == input_) {
		emit require_update();
	}
}

void KeyframeViewInputConnection::keyframe_type_changed(NodeKeyframe *key)
{
	if (key->key_track_ref() == input_) {
		emit type_changed();
	}
}

}
