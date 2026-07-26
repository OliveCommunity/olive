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
	, bridge_(new EngineEventBridge(this))
{
	Node *n = input.input().node();

	bridge_->subscribe(reinterpret_cast<void *>(n),
					   OAKENGINE_EVENT_NODE_KEYFRAME_ADDED);
	bridge_->subscribe(reinterpret_cast<void *>(n),
					   OAKENGINE_EVENT_NODE_KEYFRAME_REMOVED);
	bridge_->subscribe(reinterpret_cast<void *>(n),
					   OAKENGINE_EVENT_NODE_KEYFRAME_TIME_CHANGED);
	bridge_->subscribe(reinterpret_cast<void *>(n),
					   OAKENGINE_EVENT_NODE_KEYFRAME_TYPE_CHANGED);
	bridge_->subscribe(reinterpret_cast<void *>(n),
					   OAKENGINE_EVENT_NODE_KEYFRAME_VALUE_CHANGED);

	connect(bridge_, &EngineEventBridge::node_keyframe_added, this,
			[this](OakEngineNode *, OakEngineKeyframe *key,
				   const QString &, int, int) {
				add_keyframe(key);
			});
	connect(bridge_, &EngineEventBridge::node_keyframe_removed, this,
			[this](OakEngineNode *, OakEngineKeyframe *key,
				   const QString &, int, int) {
				remove_keyframe(key);
			});
	connect(bridge_, &EngineEventBridge::node_keyframe_time_changed, this,
			[this](OakEngineNode *, OakEngineKeyframe *key) {
				keyframe_changed(key);
			});
	connect(bridge_, &EngineEventBridge::node_keyframe_type_changed, this,
			[this](OakEngineNode *, OakEngineKeyframe *key) {
				keyframe_changed(key);
			});
	connect(bridge_, &EngineEventBridge::node_keyframe_type_changed, this,
			[this](OakEngineNode *, OakEngineKeyframe *key) {
				keyframe_type_changed(key);
			});
	connect(bridge_, &EngineEventBridge::node_keyframe_value_changed, this,
			[this](OakEngineNode *, OakEngineKeyframe *key) {
				keyframe_changed(key);
			});
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

void KeyframeViewInputConnection::add_keyframe(OakEngineKeyframe *key)
{
	NodeKeyframe *nk = reinterpret_cast<NodeKeyframe *>(key);
	if (nk->key_track_ref() == input_) {
		emit require_update();
	}
}

void KeyframeViewInputConnection::remove_keyframe(OakEngineKeyframe *key)
{
	NodeKeyframe *nk = reinterpret_cast<NodeKeyframe *>(key);
	if (nk->key_track_ref() == input_) {
		emit require_update();
	}
}

void KeyframeViewInputConnection::keyframe_changed(OakEngineKeyframe *key)
{
	NodeKeyframe *nk = reinterpret_cast<NodeKeyframe *>(key);
	if (nk->key_track_ref() == input_) {
		emit require_update();
	}
}

void KeyframeViewInputConnection::keyframe_type_changed(OakEngineKeyframe *key)
{
	NodeKeyframe *nk = reinterpret_cast<NodeKeyframe *>(key);
	if (nk->key_track_ref() == input_) {
		emit type_changed();
	}
}

}
