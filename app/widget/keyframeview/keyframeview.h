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

#ifndef OAK_KEYFRAMEVIEWBASE_H
#define OAK_KEYFRAMEVIEWBASE_H

#include <functional>

#include "keyframeviewinputconnection.h"
#include "node/keyframe.h"
#include "widget/menu/menu.h"
#include "widget/timebased/timebasedview.h"
#include "widget/timebased/timebasedviewselectionmanager.h"
#include "widget/timetarget/timetarget.h"

namespace olive
{

class KeyframeView : public TimeBasedView, public TimeTargetObject {
	Q_OBJECT
public:
	KeyframeView(QWidget *parent = nullptr);

	void delete_selected();

	using ElementConnections = QVector<KeyframeViewInputConnection *>;
	using InputConnections = QVector<ElementConnections>;
	using NodeConnections = QMap<QString, InputConnections>;

	NodeConnections add_keyframes_of_node(Node *n);

	InputConnections add_keyframes_of_input(Node *n, const QString &input);

	ElementConnections add_keyframes_of_element(const NodeInput &input);

	KeyframeViewInputConnection *
	add_keyframes_of_track(const NodeKeyframeTrackReference &ref);

	void remove_keyframes_of_track(KeyframeViewInputConnection *connection);

	void select_all();

	void deselect_all();

	void clear();

	const std::vector<NodeKeyframe *> &get_selected_keyframes() const
	{
		return selection_manager_.get_selected_objects();
	}

	const QVector<KeyframeViewInputConnection *> &get_keyframe_tracks() const
	{
		return tracks_;
	}

	virtual void SelectionManagerSelectEvent(void *obj) override;
	virtual void SelectionManagerDeselectEvent(void *obj) override;

	void set_max_scroll(int i)
	{
		max_scroll_ = i;
		update_scene_rect();
	}

	bool copy_selected(bool cut);

	bool paste(std::function<Node *(const QString &)> find_node_function);

	virtual void CatchUpScrollEvent() override;

signals:
	void dragged(int current_x, int current_y);

	void selection_changed();

	void released();

protected:
	virtual void mousePressEvent(QMouseEvent *event) override;
	virtual void mouseMoveEvent(QMouseEvent *event) override;
	virtual void mouseReleaseEvent(QMouseEvent *event) override;

	virtual void drawForeground(QPainter *painter, const QRectF &rect) override;

	virtual void draw_keyframe(QPainter *painter, NodeKeyframe *key,
							  KeyframeViewInputConnection *track,
							  const QRectF &key_rect);

	virtual void ScaleChangedEvent(const double &scale) override;

	virtual void TimeTargetChangedEvent(ViewerOutput *v) override;

	virtual void TimebaseChangedEvent(const Rational &timebase) override;

	virtual void ContextMenuEvent(Menu &m);

	virtual bool first_chance_mouse_press(QMouseEvent *event)
	{
		return false;
	}
	virtual void first_chance_mouse_move(QMouseEvent *event)
	{
	}
	virtual void first_chance_mouse_release(QMouseEvent *event)
	{
	}

	virtual void keyframe_drag_start(QMouseEvent *event)
	{
	}
	virtual void keyframe_drag_move(QMouseEvent *event, QString &tip)
	{
	}
	virtual void keyframe_drag_release(QMouseEvent *event,
									 void *command)
	{
	}

	void select_keyframe(NodeKeyframe *key);

	void deselect_keyframe(NodeKeyframe *key);

	bool is_keyframe_selected(NodeKeyframe *key) const
	{
		return selection_manager_.is_selected(key);
	}

	Rational get_unadjusted_keyframe_time(NodeKeyframe *key, const Rational &time);
	Rational get_unadjusted_keyframe_time(NodeKeyframe *key)
	{
		return get_unadjusted_keyframe_time(key, key->time());
	}

	Rational get_adjusted_keyframe_time(NodeKeyframe *key);

	double get_keyframe_scene_x(NodeKeyframe *key);

	virtual qreal get_keyframe_scene_y(KeyframeViewInputConnection *track,
									NodeKeyframe *key);

	void set_auto_select_siblings(bool e)
	{
		autoselect_siblings_ = e;
	}

	virtual void SceneRectUpdateEvent(QRectF &rect) override;

protected slots:
	void redraw();

private:
	Rational calculate_new_time_from_screen(const Rational &old_time,
										double cursor_diff);

	QVector<KeyframeViewInputConnection *> tracks_;

	TimeBasedViewSelectionManager<NodeKeyframe> selection_manager_;

	bool autoselect_siblings_;

	int max_scroll_;

	bool first_chance_mouse_event_;

private slots:
	void show_context_menu();

	void show_keyframe_properties_dialog();

	void update_rubber_band_for_scroll();
};

}

#endif // OAK_KEYFRAMEVIEWBASE_H
