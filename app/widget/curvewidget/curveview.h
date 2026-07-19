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

#ifndef OAK_CURVEVIEW_H
#define OAK_CURVEVIEW_H

#include "node/keyframe.h"
#include "widget/keyframeview/keyframeview.h"
#include "widget/slider/floatslider.h"

namespace olive
{

class CurveView : public KeyframeView {
	Q_OBJECT
public:
	CurveView(QWidget *parent = nullptr);

	void connect_input(const NodeKeyframeTrackReference &ref);

	void disconnect_input(const NodeKeyframeTrackReference &ref);

	void select_keyframes_of_input(const NodeKeyframeTrackReference &ref);

	void set_keyframe_track_color(const NodeKeyframeTrackReference &ref,
							   const QColor &color);

	const QHash<NodeKeyframeTrackReference, KeyframeViewInputConnection *> &
	get_connections() const
	{
		return track_connections_;
	}

public slots:
	void zoom_to_fit();

	void zoom_to_fit_selected();

	void reset_zoom();

protected:
	virtual void drawBackground(QPainter *painter, const QRectF &rect) override;
	virtual void drawForeground(QPainter *painter, const QRectF &rect) override;

	virtual void ContextMenuEvent(Menu &m) override;

	virtual void SceneRectUpdateEvent(QRectF &r) override;

	virtual qreal get_keyframe_scene_y(KeyframeViewInputConnection *track,
									NodeKeyframe *key) override;

	virtual void draw_keyframe(QPainter *painter, NodeKeyframe *key,
							  KeyframeViewInputConnection *track,
							  const QRectF &key_rect) override;

	virtual bool first_chance_mouse_press(QMouseEvent *event) override;
	virtual void first_chance_mouse_move(QMouseEvent *event) override;
	virtual void first_chance_mouse_release(QMouseEvent *event) override;

	virtual void keyframe_drag_start(QMouseEvent *event) override;
	virtual void keyframe_drag_move(QMouseEvent *event, QString &tip) override;
	virtual void keyframe_drag_release(QMouseEvent *event,
									 MultiUndoCommand *command) override;

private:
	void zoom_to_fit_internal(bool selected_only);

	qreal get_item_y_from_keyframe_value(NodeKeyframe *key);
	qreal get_unscaled_item_y_from_keyframe_value(NodeKeyframe *key);

	QPointF ScalePoint(const QPointF &point);

	static FloatSlider::DisplayType
	get_float_display_type_from_keyframe(NodeKeyframe *key);

	static double get_offset_from_keyframe(NodeKeyframe *key);

	void adjust_lines();

	QPointF get_keyframe_position(NodeKeyframe *key);

	static QPointF
	generate_bezier_control_position(const NodeKeyframe::BezierType mode,
								  const QPointF &start_point,
								  const QPointF &scaled_cursor_diff);

	QPointF get_scaled_cursor_pos(const QPointF &cursor_pos);

	QHash<NodeKeyframeTrackReference, QColor> keyframe_colors_;
	QHash<NodeKeyframeTrackReference, KeyframeViewInputConnection *>
		track_connections_;

	int text_padding_;

	int minimum_grid_space_;

	QVector<NodeKeyframeTrackReference> connected_inputs_;

	struct BezierPoint {
		QRectF rect;
		NodeKeyframe *keyframe;
		NodeKeyframe::BezierType type;
	};

	QVector<BezierPoint> bezier_pts_;
	const BezierPoint *dragging_bezier_pt_;

	QPointF dragging_bezier_point_start_;
	QPointF dragging_bezier_point_opposing_start_;
	QPointF drag_start_;

	QVector<QVariant> drag_keyframe_values_;
};

}

#endif // OAK_CURVEVIEW_H
