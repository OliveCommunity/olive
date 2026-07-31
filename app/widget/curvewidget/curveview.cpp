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

#include "curveview.h"

#include <cfloat>
#include <QHash>
#include <QMouseEvent>
#include <QPainterPath>
#include <QScrollBar>
#include <QtMath>

#include "oakutil/decibel.h"
#include "common/oakvaluehelper.h"
#include "oakutil/qtutils.h"
#include "oakengine/node.h"
#include "widget/keyframeview/keyframehandle.h"

namespace olive
{

#define super KeyframeView

namespace
{

// Map a keyframe track's scalar QVariant into the facade POD for the
// input's declared type (the curve view drags numeric tracks). `c_type`
// is the oak_node_value_type of the input (oakengine_node_input_get_type()).
void track_value_to_c(int c_type, const QVariant &value,
					  oak_node_value *out)
{
	memset(out, 0, sizeof(*out));
	switch (c_type) {
	case OAK_NODE_VALUE_INT:
		out->type = OAK_NODE_VALUE_INT;
		out->num = value.toLongLong();
		break;
	case OAK_NODE_VALUE_COMBO:
		out->type = OAK_NODE_VALUE_COMBO;
		out->num = value.toLongLong();
		break;
	case OAK_NODE_VALUE_BOOL:
		out->type = OAK_NODE_VALUE_BOOL;
		out->num = value.toBool() ? 1 : 0;
		break;
	case OAK_NODE_VALUE_RATIONAL: {
		const Rational r = value.value<Rational>();
		out->type = OAK_NODE_VALUE_RATIONAL;
		out->num = r.numerator();
		out->den = r.denominator();
		break;
	}
	case OAK_NODE_VALUE_COLOR:
		out->type = OAK_NODE_VALUE_COLOR;
		out->f[0] = value.toDouble();
		break;
	case OAK_NODE_VALUE_VEC2:
		out->type = OAK_NODE_VALUE_VEC2;
		out->f[0] = value.toDouble();
		break;
	case OAK_NODE_VALUE_VEC3:
		out->type = OAK_NODE_VALUE_VEC3;
		out->f[0] = value.toDouble();
		break;
	case OAK_NODE_VALUE_VEC4:
		out->type = OAK_NODE_VALUE_VEC4;
		out->f[0] = value.toDouble();
		break;
	default:
		out->type = OAK_NODE_VALUE_FLOAT;
		out->f[0] = value.toDouble();
		break;
	}
}

// The oak_node_value_type of the input that owns `key`.
int key_input_c_type(OakEngineKeyframe *key)
{
	const QByteArray input = key_input_id(key).toUtf8();
	return oakengine_node_input_get_type(oakengine_keyframe_get_node(key),
										 input.constData());
}

} // namespace

CurveView::CurveView(QWidget *parent)
	: KeyframeView(parent)
	, dragging_bezier_pt_(nullptr)
{
	setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
	set_y_axis_enabled(true);
	set_auto_select_siblings(false);

	text_padding_ =
		QtUtils::q_font_metrics_width(fontMetrics(), QStringLiteral("i"));

	minimum_grid_space_ =
		QtUtils::q_font_metrics_width(fontMetrics(), QStringLiteral("00000"));
}

void CurveView::connect_input(const oak::KeyframeTrackRef &ref)
{
	if (connected_inputs_.contains(ref)) {
		// Input wasn't connected, do nothing
		return;
	}

	// Add keyframes from track
	KeyframeViewInputConnection *track_con = add_keyframes_of_track(ref);
	track_con->set_brush(keyframe_colors_.value(ref));
	track_connections_.insert(ref, track_con);

	// Signal to CurveWidget to update its bezier/linear/hold buttons if a key type changes
	connect(track_con, &KeyframeViewInputConnection::type_changed, this,
			&CurveView::selection_changed);

	// Append to the list
	connected_inputs_.append(ref);
}

void CurveView::disconnect_input(const oak::KeyframeTrackRef &ref)
{
	if (!connected_inputs_.contains(ref)) {
		// Input wasn't connected, do nothing
		return;
	}

	// Remove keyframes belonging to this element and track
	remove_keyframes_of_track(track_connections_.take(ref));

	// Remove from the list
	connected_inputs_.removeOne(ref);
}

void CurveView::select_keyframes_of_input(const oak::KeyframeTrackRef &ref)
{
	deselect_all();

	if (KeyframeViewInputConnection *con = track_connections_.value(ref)) {
		foreach (const oak::Keyframe &key, con->get_keyframes()) {
			select_keyframe(key);
		}
	}
}

void CurveView::set_keyframe_track_color(const oak::KeyframeTrackRef &ref,
									  const QColor &color)
{
	// Insert color into hashmap
	keyframe_colors_.insert(ref, color);

	if (KeyframeViewInputConnection *con = track_connections_.value(ref)) {
		// Update all keyframes
		con->set_brush(color);
	}
}

void CurveView::drawBackground(QPainter *painter, const QRectF &rect)
{
	if (timebase().isNull()) {
		return;
	}

	painter->setRenderHint(QPainter::Antialiasing);

	QVector<QLine> lines;

	double x_interval = timebase().flipped().to_double();
	double y_interval = 100.0;

	int x_grid_interval, y_grid_interval;

	painter->setPen(QPen(palette().window().color(), 1));

	do {
		x_grid_interval = qRound(x_interval * get_scale() * timebase_dbl());
		x_interval *= 2.0;
	} while (x_grid_interval < minimum_grid_space_);

	do {
		y_grid_interval = qRound(y_interval * get_y_scale());
		y_interval *= 2.0;
	} while (y_grid_interval < minimum_grid_space_);

	int x_start = qCeil(rect.left() / x_grid_interval) * x_grid_interval;
	int y_start = qCeil(rect.top() / y_grid_interval) * y_grid_interval;

	QPointF scene_bottom_left = mapToScene(QPoint(0, qRound(rect.height())));
	QPointF scene_top_right = mapToScene(QPoint(qRound(rect.width()), 0));

	// Add vertical lines
	for (int i = x_start; i < rect.right(); i += x_grid_interval) {
		int value =
			qRound(static_cast<double>(i) / get_scale() / timebase_dbl());
		painter->drawText(i + text_padding_,
						  qRound(scene_bottom_left.y()) - text_padding_,
						  QString::number(value));
		lines.append(QLine(i, qRound(rect.top()), i, qRound(rect.bottom())));
	}

	// Add horizontal lines
	for (int i = y_start; i < rect.bottom(); i += y_grid_interval) {
		int value = qRound(static_cast<double>(i) / get_y_scale());
		painter->drawText(qRound(scene_bottom_left.x()) + text_padding_,
						  i - text_padding_, QString::number(-value));
		lines.append(QLine(qRound(rect.left()), i, qRound(rect.right()), i));
	}

	// Draw grid
	painter->drawLines(lines);

	// Draw keyframe lines
	foreach (const oak::KeyframeTrackRef &ref, connected_inputs_) {
		if (ref.input().is_keyframing()) {
			const QVector<oak::Keyframe> track = ref.keyframes();

			if (!track.isEmpty()) {
				painter->setPen(QPen(keyframe_colors_.value(ref),
									 qMax(1, fontMetrics().height() / 4)));

				// Create a path
				QPainterPath path;

				// Draw straight line leading to first keyframe
				QPointF first_key_pos = get_keyframe_position(track.first());
				path.moveTo(QPointF(scene_bottom_left.x(), first_key_pos.y()));
				path.lineTo(first_key_pos);

				// Draw lines between each keyframe
				for (int i = 1; i < track.size(); i++) {
					const oak::Keyframe &before = track.at(i - 1);
					const oak::Keyframe &after = track.at(i);

					QPointF before_pos = get_keyframe_position(before);
					QPointF after_pos = get_keyframe_position(after);

					if (before.type() == KeyframeTypes::k_facade_hold) {
						// Draw a hold keyframe (basically a right angle)
						path.lineTo(after_pos.x(), before_pos.y());
						path.lineTo(after_pos.x(), after_pos.y());

					} else if (before.type() == KeyframeTypes::k_facade_bezier &&
							   after.type() == KeyframeTypes::k_facade_bezier) {
						// Draw a cubic bezier

						// Cubic beziers have two control points, so we can just use both
						QPointF before_control_point =
							before_pos +
							ScalePoint(before.valid_bezier_point(1));
						QPointF after_control_point =
							after_pos +
							ScalePoint(after.valid_bezier_point(0));

						path.cubicTo(before_control_point, after_control_point,
									 after_pos);

					} else if (before.type() == KeyframeTypes::k_facade_bezier ||
							   after.type() == KeyframeTypes::k_facade_bezier) {
						// Draw a quadratic bezier

						// Quadratic beziers have a single control point, we just have to determine which it is
						QPointF key_anchor;
						QPointF control_point;

						if (before.type() == KeyframeTypes::k_facade_bezier) {
							key_anchor = before_pos;
							control_point = before.valid_bezier_point(1);
						} else {
							key_anchor = after_pos;
							control_point = after.valid_bezier_point(0);
						}

						// Scale control point
						control_point = key_anchor + ScalePoint(control_point);

						// Create the path from both keyframes
						path.quadTo(control_point, after_pos);

					} else {
						// Linear to linear
						path.lineTo(after_pos);
					}
				}

				// Draw straight line leading from end keyframe
				QPointF last_key_pos = get_keyframe_position(track.last());
				path.lineTo(QPointF(scene_top_right.x(), last_key_pos.y()));

				painter->drawPath(path);
			}
		}
	}
}

void CurveView::drawForeground(QPainter *painter, const QRectF &rect)
{
	bezier_pts_.clear();

	super::drawForeground(painter, rect);
}

void CurveView::ContextMenuEvent(Menu &m)
{
	// View settings
	QAction *zoom_fit_action = m.addAction(tr("Zoom to Fit"));
	connect(zoom_fit_action, &QAction::triggered, this, &CurveView::zoom_to_fit);

	QAction *zoom_fit_selected_action = m.addAction(tr("Zoom to Fit Selected"));
	connect(zoom_fit_selected_action, &QAction::triggered, this,
			&CurveView::zoom_to_fit_selected);

	QAction *reset_zoom_action = m.addAction(tr("Reset Zoom"));
	connect(reset_zoom_action, &QAction::triggered, this, &CurveView::reset_zoom);
}

void CurveView::SceneRectUpdateEvent(QRectF &r)
{
	double min_val, max_val;
	bool got_val = false;

	foreach (KeyframeViewInputConnection *con, track_connections_) {
		foreach (const oak::Keyframe &key, con->get_keyframes()) {
			qreal key_y = get_item_y_from_keyframe_value(key);

			if (got_val) {
				min_val = qMin(key_y, min_val);
				max_val = qMax(key_y, max_val);
			} else {
				min_val = key_y;
				max_val = key_y;
				got_val = true;
			}
		}
	}

	if (got_val) {
		r.setTop(min_val - this->height());
		r.setBottom(max_val + this->height());
	}
}

qreal CurveView::get_keyframe_scene_y(KeyframeViewInputConnection *track,
								   const oak::Keyframe &key)
{
	return get_item_y_from_keyframe_value(key);
}

void CurveView::draw_keyframe(QPainter *painter, const oak::Keyframe &key,
							 KeyframeViewInputConnection *track,
							 const QRectF &key_rect)
{
	if (is_keyframe_selected(key) &&
		key.type() == KeyframeTypes::k_facade_bezier) {
		// Draw bezier control points if keyframe is selected
		int control_point_size = QtUtils::q_font_metrics_width(fontMetrics(), "o");
		int half_sz = control_point_size / 2;
		QRectF control_point_rect(-half_sz, -half_sz, control_point_size,
								  control_point_size);

		painter->setPen(palette().text().color());
		painter->setBrush(Qt::NoBrush);

		QRectF cp_in = control_point_rect.translated(
			key_rect.center() + ScalePoint(key.bezier_point(0)));
		QRectF cp_out = control_point_rect.translated(
			key_rect.center() + ScalePoint(key.bezier_point(1)));

		painter->drawLine(key_rect.center(), cp_in.center());
		painter->drawLine(key_rect.center(), cp_out.center());

		painter->drawEllipse(cp_in);
		painter->drawEllipse(cp_out);

		bezier_pts_.append({ cp_in, key.handle(), KeyframeTypes::k_in_handle });
		bezier_pts_.append({ cp_out, key.handle(), KeyframeTypes::k_out_handle });
	}

	super::draw_keyframe(painter, key, track, key_rect);
}

bool CurveView::first_chance_mouse_press(QMouseEvent *event)
{
	dragging_bezier_pt_ = nullptr;
	QPointF scene_pt = mapToScene(event->pos());
	foreach (const BezierPoint &b, bezier_pts_) {
		if (b.rect.contains(scene_pt)) {
			dragging_bezier_pt_ = &b;
			break;
		}
	}

	if (dragging_bezier_pt_) {
		OakEngineKeyframe *key = dragging_bezier_pt_->keyframe;
		dragging_bezier_point_start_ =
			(dragging_bezier_pt_->type == KeyframeTypes::k_in_handle) ?
				key_bezier_point(key, 0) :
				key_bezier_point(key, 1);
		dragging_bezier_point_opposing_start_ =
			(dragging_bezier_pt_->type == KeyframeTypes::k_in_handle) ?
				key_bezier_point(key, 1) :
				key_bezier_point(key, 0);

		drag_start_ = mapToScene(event->pos());
		return true;
	} else {
		return false;
	}
}

void CurveView::first_chance_mouse_move(QMouseEvent *event)
{
	// Calculate cursor difference and scale it
	QPointF scene_pos = mapToScene(event->pos());
	QPointF mouse_diff_scaled = get_scaled_cursor_pos(scene_pos - drag_start_);

	if (event->modifiers() & Qt::ShiftModifier) {
		// If holding shift, only move one axis
		mouse_diff_scaled.setY(0);
	}

	// Flip the mouse Y because bezier control points are drawn bottom to top, not top to bottom
	mouse_diff_scaled.setY(-mouse_diff_scaled.y());

	QPointF new_bezier_pos = generate_bezier_control_position(
		dragging_bezier_pt_->type, dragging_bezier_point_start_,
		mouse_diff_scaled);

	// If the user is NOT holding control, we set the other handle to the exact negative of this handle
	QPointF new_opposing_pos;
	int opposing_type =
		oakengine_keyframe_opposing_bezier_type(dragging_bezier_pt_->type);

	if (!(event->modifiers() & Qt::ControlModifier)) {
		new_opposing_pos = generate_bezier_control_position(
			static_cast<KeyframeTypes::BezierType>(opposing_type),
			dragging_bezier_point_opposing_start_,
			-mouse_diff_scaled);
	} else {
		new_opposing_pos = dragging_bezier_point_opposing_start_;
	}

	oakengine_keyframe_set_bezier_point_live(
		dragging_bezier_pt_->keyframe,
		dragging_bezier_pt_->type,
		new_bezier_pos.x(), new_bezier_pos.y());

	oakengine_keyframe_set_bezier_point_live(
		dragging_bezier_pt_->keyframe,
		opposing_type,
		new_opposing_pos.x(), new_opposing_pos.y());

	redraw();
}

void CurveView::first_chance_mouse_release(QMouseEvent *event)
{
	// Through the liboakengine C ABI facade with the drag-start point(s)
	// as the explicit old values (the drag already live-set the new
	// ones); one undoable command per handle, same as the old
	// KeyframeSetBezierControlPoint children.
	OakEngineKeyframe *key = dragging_bezier_pt_->keyframe;
	OakEngineNode *handle = oakengine_keyframe_get_node(key);
	int tbn = 0, tbd = 0;
	oakengine_node_frame_time_base(handle, &tbn, &tbd);
	const int64_t ts = Timecode::time_to_timestamp(
		key_time(key), Rational(tbn, tbd), Timecode::k_round);
	const QPointF current =
		key_bezier_point(key, dragging_bezier_pt_->type);
	const QByteArray input = key_input_id(key).toUtf8();
	oakengine_node_keyframe_set_bezier_point(
		handle, input.constData(), key_element(key), ts,
		key_track(key),
		(dragging_bezier_pt_->type == KeyframeTypes::k_in_handle) ? 0 : 1,
		current.x(), current.y(), dragging_bezier_point_start_.x(),
		dragging_bezier_point_start_.y());

	if (!(event->modifiers() & Qt::ControlModifier)) {
		int opposing_type =
			oakengine_keyframe_opposing_bezier_type(dragging_bezier_pt_->type);
		const QPointF opposing_current = key_bezier_point(key, opposing_type);
		oakengine_node_keyframe_set_bezier_point(
			handle, input.constData(), key_element(key), ts,
			key_track(key),
			opposing_type,
			opposing_current.x(), opposing_current.y(),
			dragging_bezier_point_opposing_start_.x(),
			dragging_bezier_point_opposing_start_.y());
	}

	dragging_bezier_pt_ = nullptr;
}

void CurveView::keyframe_drag_start(QMouseEvent *event)
{
	drag_keyframe_values_.resize(get_selected_keyframes().size());
	for (size_t i = 0; i < get_selected_keyframes().size(); i++) {
		OakEngineKeyframe *key = get_selected_keyframes().at(i);
		drag_keyframe_values_[i] = OakNodeValueToQVariant(key_value(key));
	}

	drag_start_ = mapToScene(event->pos());
}

void CurveView::keyframe_drag_move(QMouseEvent *event, QString &tip)
{
	if (event->modifiers() & Qt::ShiftModifier) {
		// Lock to X axis only and set original values on all keys
		for (size_t i = 0; i < get_selected_keyframes().size(); i++) {
			OakEngineKeyframe *key = get_selected_keyframes().at(i);
			oak_node_value v;
			track_value_to_c(key_input_c_type(key),
							 drag_keyframe_values_.at(i), &v);
			key_set_value_live(key, v);
		}
		return;
	}

	// Calculate cursor difference
	double scaled_diff =
		(mapToScene(event->pos()).y() - drag_start_.y()) / get_y_scale();

	// Validate movement - ensure no keyframe goes above its max point or below its min point
	for (size_t i = 0; i < get_selected_keyframes().size(); i++) {
		OakEngineKeyframe *key = get_selected_keyframes().at(i);

		FloatSlider::DisplayType display = get_float_display_type_from_keyframe(oak::Keyframe(key));
		OakEngineNode *node = oakengine_keyframe_get_node(key);
		const QByteArray input = key_input_id(key).toUtf8();
		double original_val = FloatSlider::transform_value_to_display(
			drag_keyframe_values_.at(i).toDouble(), display);
		double new_val = FloatSlider::transform_display_to_value(
			original_val - scaled_diff, display);
		double limited = new_val;

		double prop = 0;
		if (oakengine_node_input_get_property_number(
				node, input.constData(), "min", -1, &prop) == OAKENGINE_OK) {
			limited = qMax(limited, prop);
		}

		if (oakengine_node_input_get_property_number(
				node, input.constData(), "max", -1, &prop) == OAKENGINE_OK) {
			limited = qMin(limited, prop);
		}

		if (limited != new_val) {
			scaled_diff = original_val - limited;
		}
	}

	// Set values
	for (size_t i = 0; i < get_selected_keyframes().size(); i++) {
		OakEngineKeyframe *key = get_selected_keyframes().at(i);
		FloatSlider::DisplayType display = get_float_display_type_from_keyframe(oak::Keyframe(key));
		oak_node_value v;
		track_value_to_c(
			key_input_c_type(key),
			FloatSlider::transform_display_to_value(
				FloatSlider::transform_value_to_display(
					drag_keyframe_values_.at(i).toDouble(), display) -
					scaled_diff,
				display),
			&v);
		key_set_value_live(key, v);
	}

	OakEngineKeyframe *tip_item = get_selected_keyframes().front();

	bool ok;
	double num_value = OakNodeValueToQVariant(key_value(tip_item)).toDouble(&ok);

	if (ok) {
		tip = QStringLiteral("%1\n");
		tip.append(FloatSlider::value_to_string(
			num_value + get_offset_from_keyframe(oak::Keyframe(tip_item)),
			get_float_display_type_from_keyframe(oak::Keyframe(tip_item)), 2, true));
	}
}

void CurveView::keyframe_drag_release(QMouseEvent *event,
									void *command)
{
	Q_UNUSED(command) // the facade pushes its own single command below

	// Group the changed keys by owning input and push ONE undoable
	// command per group through the liboakengine C ABI facade, with the
	// drag-start values as the explicit undo values (the drag already
	// live-set the new ones).
	struct ValueGroup {
		OakEngineNode *node;
		QString input;
		int element;
		QVector<int64_t> times;
		QVector<int> tracks;
		std::vector<oak_node_value> values;
		std::vector<oak_node_value> olds;
	};
	QVector<ValueGroup> groups;
	for (size_t i = 0; i < get_selected_keyframes().size(); i++) {
		OakEngineKeyframe *k = get_selected_keyframes().at(i);
		if (qFuzzyCompare(key_value_as_double(k),
						  drag_keyframe_values_.at(i).toDouble())) {
			continue;
		}

		OakEngineNode *node = oakengine_keyframe_get_node(k);
		const QString input = key_input_id(k);
		const int element = key_element(k);

		int g = 0;
		for (; g < groups.size(); g++) {
			if (groups.at(g).node == node &&
				groups.at(g).input == input &&
				groups.at(g).element == element) {
				break;
			}
		}
		if (g == groups.size()) {
			groups.append(
				{ node, input, element, {}, {}, {}, {} });
		}

		int tbn = 0, tbd = 0;
		oakengine_node_frame_time_base(node, &tbn, &tbd);
		groups[g].times.append(Timecode::time_to_timestamp(
			key_time(k), Rational(tbn, tbd), Timecode::k_round));
		groups[g].tracks.append(key_track(k));

		oak_node_value old_v;
		track_value_to_c(key_input_c_type(k), drag_keyframe_values_.at(i),
						 &old_v);
		groups[g].values.push_back(key_value(k));
		groups[g].olds.push_back(old_v);
	}

	foreach (const ValueGroup &g, groups) {
		oakengine_node_keyframes_set_value_many(
			g.node,
			g.input.toUtf8().constData(), g.element, g.times.constData(),
			g.tracks.data(), g.times.size(), g.values.data(),
			g.olds.data());
	}
}

QPointF
CurveView::generate_bezier_control_position(const KeyframeTypes::BezierType mode,
										 const QPointF &start_point,
										 const QPointF &scaled_cursor_diff)
{
	QPointF new_bezier_pos = start_point;

	new_bezier_pos += scaled_cursor_diff;

	// LIMIT bezier handles from overlapping each other
	if (mode == KeyframeTypes::k_in_handle) {
		if (new_bezier_pos.x() > 0) {
			new_bezier_pos.setX(0);
		}
	} else {
		if (new_bezier_pos.x() < 0) {
			new_bezier_pos.setX(0);
		}
	}

	return new_bezier_pos;
}

QPointF CurveView::get_scaled_cursor_pos(const QPointF &cursor_pos)
{
	return QPointF(cursor_pos.x() / get_scale(), cursor_pos.y() / get_y_scale());
}

void CurveView::zoom_to_fit_internal(bool selected_only)
{
	bool got_val = false;

	Rational min_time, max_time;
	double min_val, max_val;

	foreach (KeyframeViewInputConnection *con, track_connections_) {
		foreach (const oak::Keyframe &key, con->get_keyframes()) {
			if (!selected_only || is_keyframe_selected(key)) {
				Rational transformed_time =
					get_adjusted_time(key.node().handle(),
						get_time_target(), key_time(key.handle()),
						k_transform_towards_output);

				qreal key_y = get_unscaled_item_y_from_keyframe_value(key);

				if (got_val) {
					min_time = qMin(transformed_time, min_time);
					max_time = qMax(transformed_time, max_time);

					min_val = qMin(key_y, min_val);
					max_val = qMax(key_y, max_val);
				} else {
					min_time = transformed_time;
					max_time = transformed_time;

					min_val = key_y;
					max_val = key_y;

					got_val = true;
				}
			}
		}
	}

	// Prevent scaling if no keyframes were found
	if (got_val) {
		QRectF desired(QPointF(min_time.to_double(), min_val),
					   QPointF(max_time.to_double(), max_val));

		const double scale_divider = 0.5;
		double scale_half_divider = scale_divider * 0.5;

		double new_x_scale =
			viewport()->width() / desired.width() * scale_divider;
		double new_y_scale;

		if (qFuzzyIsNull(desired.height())) {
			// Catch divide by zero
			new_y_scale = 1.0;
			scale_half_divider = 0.5;
		} else {
			// Use height as normal
			new_y_scale =
				viewport()->height() / desired.height() * scale_divider;
		}

		emit scale_changed(new_x_scale);
		set_y_scale(new_y_scale);

		update_scene_rect();

		int sb_x = desired.left() * new_x_scale -
				   viewport()->width() * scale_half_divider;
		QMetaObject::invokeMethod(horizontalScrollBar(), "setValue",
								  Qt::QueuedConnection, Q_ARG(int, sb_x));

		int sb_y = desired.top() * new_y_scale -
				   viewport()->height() * scale_half_divider;
		QMetaObject::invokeMethod(verticalScrollBar(), "setValue",
								  Qt::QueuedConnection, Q_ARG(int, sb_y));
	}
}

qreal CurveView::get_item_y_from_keyframe_value(const oak::Keyframe &key)
{
	return get_unscaled_item_y_from_keyframe_value(key) * get_y_scale();
}

qreal CurveView::get_unscaled_item_y_from_keyframe_value(const oak::Keyframe &key)
{
	double val = key_value_as_double(key.handle());

	val = FloatSlider::transform_value_to_display(
		val, get_float_display_type_from_keyframe(key));

	val += get_offset_from_keyframe(key);

	return -val;
}

QPointF CurveView::ScalePoint(const QPointF &point)
{
	// Flips Y coordinate because curves are drawn bottom to top
	return QPointF(point.x() * get_scale(), -point.y() * get_y_scale());
}

FloatSlider::DisplayType
CurveView::get_float_display_type_from_keyframe(const oak::Keyframe &key)
{
	// Try to get view from input (which will be normal if unset)
	const QByteArray input = key.input_id().toUtf8();
	double view_type = 0;
	if (oakengine_node_input_get_property_number(
			key.node().handle(), input.constData(), "view", -1,
			&view_type) == OAKENGINE_OK) {
		return static_cast<FloatSlider::DisplayType>(int(view_type));
	}

	// Fallback to normal
	return slider::k_normal;
}

double CurveView::get_offset_from_keyframe(const oak::Keyframe &key)
{
	const QByteArray input = key.input_id().toUtf8();
	double offset = 0;
	if (oakengine_node_input_get_property_number(
			key.node().handle(), input.constData(), "offset", -1,
			&offset) == OAKENGINE_OK) {
		return offset;
	}

	return 0;
}

QPointF CurveView::get_keyframe_position(const oak::Keyframe &key)
{
	return QPointF(get_keyframe_scene_x(key), get_item_y_from_keyframe_value(key));
}

void CurveView::zoom_to_fit()
{
	zoom_to_fit_internal(false);
}

void CurveView::zoom_to_fit_selected()
{
	zoom_to_fit_internal(true);
}

void CurveView::reset_zoom()
{
	emit scale_changed(1.0);
	set_y_scale(1.0);
}

}
