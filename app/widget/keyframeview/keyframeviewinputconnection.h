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

#ifndef OAK_KEYFRAMEVIEWINPUTCONNECTION_H
#define OAK_KEYFRAMEVIEWINPUTCONNECTION_H

#include <QObject>

#include "engineeventbridge.h"
#include "node/node.h"
#include "node/param.h"

struct OakEngineKeyframe;

namespace olive
{

class KeyframeView;

class KeyframeViewInputConnection : public QObject {
	Q_OBJECT
public:
	KeyframeViewInputConnection(const NodeKeyframeTrackReference &input,
								KeyframeView *parent);

	const int &get_keyframe_y() const
	{
		return y_;
	}

	void set_keyframe_y(int y);

	enum YBehavior { k_single_row, k_value_is_height };

	void set_y_behavior(YBehavior e);

	const QVector<NodeKeyframe *> &get_keyframes() const
	{
		return input_.input()
			.node()
			->get_keyframe_tracks(input_.input())
			.at(input_.track());
	}

	const QBrush &get_brush() const
	{
		return brush_;
	}

	const NodeKeyframeTrackReference &get_reference() const
	{
		return input_;
	}

	void set_brush(const QBrush &brush);

signals:
	void require_update();

	void type_changed();

private:
	KeyframeView *keyframe_view_;

	NodeKeyframeTrackReference input_;

	int y_;

	YBehavior y_behavior_;

	QBrush brush_;

	EngineEventBridge *bridge_ = nullptr;

private slots:
	void add_keyframe(OakEngineKeyframe *key);

	void remove_keyframe(OakEngineKeyframe *key);

	void keyframe_changed(OakEngineKeyframe *key);

	void keyframe_type_changed(OakEngineKeyframe *key);
};

}

#endif // OAK_KEYFRAMEVIEWINPUTCONNECTION_H
