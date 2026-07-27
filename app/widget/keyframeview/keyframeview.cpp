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

#include "keyframeview.h"

#include <QMouseEvent>
#include <QToolTip>
#include <QVBoxLayout>

#include "common/nodevaluehandle.h"
#include "common/oakvaluehelper.h"
#include "oakutil/qtutils.h"
#include "dialog/keyframeproperties/keyframeproperties.h"
#include "keyframehandle.h"
#include "node/node.h"
#include "node/value.h"
#include "oakengine/node.h"
#include "oakengine/serializer.h"
#include "oakengine/undo.h"
#include "widget/menu/menu.h"
#include "widget/menu/menushared.h"

namespace olive
{

#define super TimeBasedView

static bool KeyframeToOakNodeValue(Node *node, NodeKeyframe *key,
                                   oak_node_value *out)
{
	const NodeValue::Type type = node->get_input_data_type(key->input());
	QVector<QVariant> split = node->get_split_value_at_time(
		NodeInput(node, key->input(), key->element()), key->time());
	if (key->track() >= 0 && key->track() < split.size()) {
		split[key->track()] = key->value();
	}
	QVector<oak_node_value> tracks(split.size());
	for (int i = 0; i < split.size(); i++) {
		if (!NodeTrackComponentToOakNodeValue(type, split.at(i), &tracks[i])) {
			return false;
		}
	}
	return oakengine_node_value_combine_tracks(
			   node_value_type_to_c(type), tracks.constData(), tracks.size(),
			   out) == OAKENGINE_OK;
}

KeyframeView::KeyframeView(QWidget *parent)
	: super(parent)
	, selection_manager_(this)
	, autoselect_siblings_(true)
	, max_scroll_(0)
	, first_chance_mouse_event_(false)
{
	setAlignment(Qt::AlignLeft | Qt::AlignTop);
	set_default_drag_mode(RubberBandDrag);
	setContextMenuPolicy(Qt::CustomContextMenu);

	connect(this, &KeyframeView::customContextMenuRequested, this,
			&KeyframeView::show_context_menu);
}

void KeyframeView::delete_selected()
{
	if (!selection_manager_.is_dragging()) {
		QVector<OakEngineKeyframe *> keys;
		foreach (NodeKeyframe *key, get_selected_keyframes()) {
			keys.append(reinterpret_cast<OakEngineKeyframe *>(key));
		}
		oakengine_keyframes_remove_many(
			keys.data(), keys.size(),
			tr("Deleted %1 Keyframe(s)")
				.arg(keys.size())
				.toUtf8()
				.constData());
	}
}

KeyframeView::NodeConnections KeyframeView::add_keyframes_of_node(Node *n)
{
	NodeConnections map;

	foreach (const QString &i, n->inputs()) {
		map.insert(i, add_keyframes_of_input(n, i));
	}

	return map;
}

KeyframeView::InputConnections
KeyframeView::add_keyframes_of_input(Node *on, const QString &oinput)
{
	InputConnections vec;

	OakEngineNode *resolved_node = nullptr;
	char resolved_input[256];
	int resolved_element = 0;
	oakengine_group_resolve_input(
		reinterpret_cast<OakEngineNode *>(on), oinput.toUtf8().constData(), -1,
		&resolved_node, resolved_input, sizeof(resolved_input),
		&resolved_element);
	NodeInput resolved(reinterpret_cast<Node *>(resolved_node),
					   QString::fromUtf8(resolved_input), resolved_element);
	Node *n = resolved.node();
	const QString &input = resolved.input();

	if (n->is_input_keyframable(input)) {
		int arr_sz = n->input_array_size(input);
		vec.resize(arr_sz + 1);
		for (int i = -1; i < arr_sz; i++) {
			vec[i + 1] = add_keyframes_of_element(NodeInput(n, input, i));
		}
	}

	return vec;
}

KeyframeView::ElementConnections
KeyframeView::add_keyframes_of_element(const NodeInput &input)
{
	const QVector<NodeKeyframeTrack> &tracks =
		input.node()->get_keyframe_tracks(input);
	ElementConnections vec(tracks.size());

	for (int i = 0; i < tracks.size(); i++) {
		vec[i] = add_keyframes_of_track(NodeKeyframeTrackReference(input, i));
	}

	return vec;
}

KeyframeViewInputConnection *
KeyframeView::add_keyframes_of_track(const NodeKeyframeTrackReference &ref)
{
	KeyframeViewInputConnection *track =
		new KeyframeViewInputConnection(ref, this);
	connect(track, &KeyframeViewInputConnection::require_update, this,
			&KeyframeView::redraw);
	tracks_.append(track);
	redraw();
	return track;
}

void KeyframeView::remove_keyframes_of_track(
	KeyframeViewInputConnection *connection)
{
	if (tracks_.removeOne(connection)) {
		foreach (NodeKeyframe *key, connection->get_keyframes()) {
			selection_manager_.deselect(key);
		}
		delete connection;
		redraw();
		emit selection_changed();
	}
}

void KeyframeView::select_all()
{
	foreach (KeyframeViewInputConnection *track, tracks_) {
		foreach (NodeKeyframe *key, track->get_keyframes()) {
			select_keyframe(key);
		}
	}
}

void KeyframeView::deselect_all()
{
	selection_manager_.clear_selection();

	redraw();
}

void KeyframeView::clear()
{
	if (!tracks_.isEmpty()) {
		qDeleteAll(tracks_);
		tracks_.clear();
		redraw();
	}

	selection_manager_.clear_selection();
}

void KeyframeView::SelectionManagerSelectEvent(void *obj)
{
	if (autoselect_siblings_) {
		NodeKeyframe *key = static_cast<NodeKeyframe *>(obj);
		QVector<NodeKeyframe *> keys = key->parent()->get_keyframes_at_time(
			key->input(), key->time(), key->element());
		foreach (NodeKeyframe *k, keys) {
			if (k != key) {
				select_keyframe(k);
			}
		}
	}

	emit selection_changed();
}

void KeyframeView::SelectionManagerDeselectEvent(void *obj)
{
	if (autoselect_siblings_) {
		NodeKeyframe *key = static_cast<NodeKeyframe *>(obj);
		QVector<NodeKeyframe *> keys = key->parent()->get_keyframes_at_time(
			key->input(), key->time(), key->element());
		foreach (NodeKeyframe *k, keys) {
			if (k != key) {
				deselect_keyframe(k);
			}
		}
	}

	emit selection_changed();
}

bool KeyframeView::copy_selected(bool cut)
{
	if (!selection_manager_.get_selected_objects().empty()) {
		const auto &keys = selection_manager_.get_selected_objects();
		OakEngineClipboard *cb =
			oakengine_clipboard_create(OAKENGINE_CLIPBOARD_KEYFRAMES,
									  nullptr, nullptr);
		oakengine_clipboard_set_keyframes(
			cb,
			reinterpret_cast<const OakEngineKeyframe *const *>(
				keys.data()),
			static_cast<int>(keys.size()));
		oakengine_clipboard_copy(cb);
		oakengine_clipboard_free(cb);

		if (cut) {
			delete_selected();
		}

		return true;
	}

	return false;
}

bool KeyframeView::paste(
	std::function<Node *(const QString &)> find_node_function)
{
	if (!get_viewer_node()) {
		return false;
	}

	OakEngineClipboard *cb = oakengine_clipboard_create(
		OAKENGINE_CLIPBOARD_KEYFRAMES, nullptr, nullptr);
	int result_code = OAKENGINE_SERIALIZER_NO_DATA;
	oakengine_clipboard_paste(cb, OAKENGINE_CLIPBOARD_KEYFRAMES, nullptr,
							&result_code, nullptr, 0);

	if (result_code == OAKENGINE_SERIALIZER_OK) {
		struct PasteCtx {
			KeyframeView *self;
			void *find_fn;
			void *command;
			Rational min;
			int total;
		};

		PasteCtx ctx;
		ctx.self = this;
		ctx.find_fn = &find_node_function;
		ctx.command = oakengine_undo_command_create_multi();
		ctx.min = RATIONAL_MAX;
		ctx.total = 0;

		// First pass: find minimum time
		oakengine_clipboard_foreach_keyframe(
			cb,
			[](const char *, OakEngineKeyframe *kf, void *userdata) -> int {
				auto *ctx = static_cast<PasteCtx *>(userdata);
				NodeKeyframe *key = reinterpret_cast<NodeKeyframe *>(kf);
				ctx->min = std::min(ctx->min, key->time());
				ctx->total++;
				return 0;
			},
			&ctx);

		ctx.min -= ctx.self->get_viewer_node()->get_playhead();

		// Second pass: process keyframes
		oakengine_clipboard_foreach_keyframe(
			cb,
			[](const char *node_id, OakEngineKeyframe *kf,
			   void *userdata) -> int {
				auto *ctx = static_cast<PasteCtx *>(userdata);
				NodeKeyframe *key = reinterpret_cast<NodeKeyframe *>(kf);
				auto &find_fn =
					*static_cast<std::function<Node *(const QString &)> *>(
						ctx->find_fn);
				Node *node_with_id =
					find_fn(QString::fromUtf8(node_id));

				if (node_with_id) {
					Rational t = key->time() - ctx->min;
					t = ctx->self->get_adjusted_time(
						ctx->self->get_time_target(), node_with_id, t,
						Node::k_transform_towards_input);
					key_set_time_live(key, t);

					if (NodeKeyframe *existing =
							node_with_id->get_keyframe_at_time_on_track(
								key->input(), key->time(), key->track(),
								key->element())) {
						void *rm = oakengine_node_remove_keyframe_command(
							reinterpret_cast<OakEngineKeyframe *>(existing));
						oakengine_undo_command_multi_add_child(
							ctx->command, rm);
					}

					oak_node_value v;
					KeyframeToOakNodeValue(node_with_id, key, &v);
					int tbn = 0, tbd = 0;
					oakengine_node_frame_time_base(
						reinterpret_cast<OakEngineNode *>(node_with_id),
						&tbn, &tbd);
					const int64_t time_ts = Timecode::time_to_timestamp(
						key->time(), Rational(tbn, tbd), Timecode::k_round);
					void *cmd = oakengine_node_insert_keyframe_command(
						reinterpret_cast<OakEngineNode *>(node_with_id),
						key->input().toUtf8().constData(),
						key->element(), key->track(), time_ts, &v,
						NodeKeyframeTypeToFacade(key->type()),
						static_cast<float>(key->bezier_control_in().x()),
						static_cast<float>(key->bezier_control_in().y()),
						static_cast<float>(key->bezier_control_out().x()),
						static_cast<float>(key->bezier_control_out().y()));
					oakengine_undo_command_multi_add_child(ctx->command, cmd);
				} else {
					delete key;
				}

				return 0;
			},
			&ctx);

		oakengine_undo_push(ctx.command,
							tr("Pasted %1 Keyframe(s)")
								.arg(ctx.total)
								.toUtf8()
								.constData());
		oakengine_clipboard_free(cb);
		return true;
	}

	oakengine_clipboard_free(cb);
	return false;
}

void KeyframeView::CatchUpScrollEvent()
{
	super::CatchUpScrollEvent();

	this->selection_manager_.force_drag_update();
}

void KeyframeView::mousePressEvent(QMouseEvent *event)
{
	NodeKeyframe *key_under_cursor =
		selection_manager_.get_object_at_point(event->pos());

	if (hand_press(event) || (!key_under_cursor && playhead_press(event))) {
		return;
	}

	// Do mouse press things
	if (first_chance_mouse_press(event)) {
		first_chance_mouse_event_ = true;
	} else if (NodeKeyframe *initial_key =
				   selection_manager_.mouse_press(event)) {
		selection_manager_.drag_start(initial_key, event, this);
		keyframe_drag_start(event);
	} else {
		selection_manager_.rubber_band_start(event);
	}

	// Update view
	redraw();
}

void KeyframeView::mouseMoveEvent(QMouseEvent *event)
{
	if (hand_move(event) || playhead_move(event)) {
		return;
	}

	if (first_chance_mouse_event_) {
		first_chance_mouse_move(event);
	} else if (selection_manager_.is_dragging()) {
		QString tip;
		keyframe_drag_move(event, tip);
		selection_manager_.drag_move(event->pos(), tip);
	} else if (selection_manager_.is_rubber_banding()) {
		selection_manager_.rubber_band_move(event->pos());
		redraw();
	}

	if (event->buttons()) {
		// Signal cursor pos in case we should scroll to catch up to it
		emit dragged(event->pos().x(), event->pos().y());
	}
}

void KeyframeView::mouseReleaseEvent(QMouseEvent *event)
{
	if (hand_release(event) || playhead_release(event)) {
		return;
	}

	if (first_chance_mouse_event_) {
		first_chance_mouse_release(event);
		first_chance_mouse_event_ = false;
	} else if (selection_manager_.is_dragging()) {
		void *command = oakengine_undo_command_create_multi();
		selection_manager_.drag_stop(command);
		keyframe_drag_release(event, command);
		oakengine_undo_push(
			command, tr("Moved %1 Keyframe(s)")
						 .arg(selection_manager_.get_selected_objects().size()).toUtf8().constData());
	} else if (selection_manager_.is_rubber_banding()) {
		selection_manager_.rubber_band_stop();
		redraw();
		emit selection_changed();
	}

	emit released();
}

int binary_search_first_keyframe_after_or_at(const QVector<NodeKeyframe *> &keys,
									   const Rational &time)
{
	int low = 0;
	int high = keys.size() - 1;

	while (low <= high) {
		int mid = low + (high - low) / 2;
		NodeKeyframe *test_key = keys.at(mid);

		if (test_key->time() == time ||
			(test_key->time() > time &&
			 (mid == 0 || keys.at(mid - 1)->time() < time))) {
			return mid;
		} else if (test_key->time() < time) {
			low = mid + 1;
		} else {
			high = mid - 1;
		}
	}

	return keys.size();
}

void KeyframeView::drawForeground(QPainter *painter, const QRectF &rect)
{
	int key_sz = QtUtils::q_font_metrics_width(fontMetrics(), "Oi");
	int key_rad = key_sz / 2;

	selection_manager_.clear_drawn_objects();

	painter->setRenderHint(QPainter::Antialiasing);

	foreach (KeyframeViewInputConnection *track, tracks_) {
		const QVector<NodeKeyframe *> &keys = track->get_keyframes();

		if (keys.isEmpty()) {
			continue;
		}

		if (!is_y_axis_enabled()) {
			// Filter out if the keyframes are offscreen Y
			qreal y = get_keyframe_scene_y(track, keys.first());
			if (y + key_rad < rect.top() || y - key_rad >= rect.bottom()) {
				continue;
			}
		}

		// Find first keyframe to show with binary search
		Rational left_time = get_unadjusted_keyframe_time(
			keys.first(), scene_to_time(rect.left() - key_sz));
		int using_index = binary_search_first_keyframe_after_or_at(keys, left_time);

		Rational next_key = RATIONAL_MIN;
		NodeKeyframe::Type last_type = NodeKeyframe::k_invalid;
		for (int i = using_index; i < keys.size(); i++) {
			NodeKeyframe *key = keys.at(i);

			if (key->time() < next_key && key->type() == last_type) {
				// This key will be drawn at exactly the same location as the last one and therefore
				// doesn't need to be drawn. See if the next one will be drawn.
				i++;
				if (i == keys.size()) {
					break;
				}

				key = keys.at(i);

				if (key->time() < next_key) {
					// Next key still won't be drawn, so we'll switch to a binary search
					i = binary_search_first_keyframe_after_or_at(keys, next_key);

					if (i == keys.size()) {
						break;
					}

					key = keys.at(i);
				}
			}

			QRectF key_rect(-key_rad, -key_rad, key_sz, key_sz);
			qreal key_x = get_keyframe_scene_x(key);
			key_rect.translate(key_x, get_keyframe_scene_y(track, key));

			if (key_rect.left() >= rect.right()) {
				// Break after last keyframe
				break;
			}

			draw_keyframe(painter, key, track, key_rect);

			next_key = get_unadjusted_keyframe_time(key, scene_to_time(key_x + 1));
			last_type = key->type();
		}
	}

	super::drawForeground(painter, rect);
}

void KeyframeView::draw_keyframe(QPainter *painter, NodeKeyframe *key,
								KeyframeViewInputConnection *track,
								const QRectF &key_rect)
{
	painter->setPen(Qt::black);

	if (is_keyframe_selected(key)) {
		painter->setBrush(palette().highlight());
	} else {
		painter->setBrush(track->get_brush());
	}

	selection_manager_.declare_drawn_object(key, key_rect);

	switch (key->type()) {
	case NodeKeyframe::k_invalid:
		break;
	case NodeKeyframe::k_linear: {
		QPointF points[] = { QPointF(key_rect.center().x(), key_rect.top()),
							 QPointF(key_rect.right(), key_rect.center().y()),
							 QPointF(key_rect.center().x(), key_rect.bottom()),
							 QPointF(key_rect.left(), key_rect.center().y()) };

		painter->drawPolygon(points, 4);
		break;
	}
	case NodeKeyframe::k_bezier:
		painter->drawEllipse(key_rect);
		break;
	case NodeKeyframe::k_hold:
		painter->drawRect(key_rect);
		break;
	}
}

void KeyframeView::ScaleChangedEvent(const double &scale)
{
	super::ScaleChangedEvent(scale);

	redraw();
}

void KeyframeView::TimeTargetChangedEvent(ViewerOutput *v)
{
	redraw();
}

void KeyframeView::TimebaseChangedEvent(const Rational &timebase)
{
	super::TimebaseChangedEvent(timebase);

	selection_manager_.set_timebase(timebase);
}

void KeyframeView::ContextMenuEvent(Menu &m)
{
	Q_UNUSED(m)
}

void KeyframeView::select_keyframe(NodeKeyframe *key)
{
	if (selection_manager_.select(key)) {
		redraw();

		emit selection_changed();
	}
}

void KeyframeView::deselect_keyframe(NodeKeyframe *key)
{
	if (selection_manager_.deselect(key)) {
		redraw();

		emit selection_changed();
	}
}

Rational KeyframeView::get_unadjusted_keyframe_time(NodeKeyframe *key,
												 const Rational &time)
{
	return get_adjusted_time(get_time_target(), key->parent(), time,
						   Node::k_transform_towards_input);
}

Rational KeyframeView::get_adjusted_keyframe_time(NodeKeyframe *key)
{
	return get_adjusted_time(key->parent(), get_time_target(), key->time(),
						   Node::k_transform_towards_output);
}

double KeyframeView::get_keyframe_scene_x(NodeKeyframe *key)
{
	return time_to_scene(get_adjusted_keyframe_time(key));
}

qreal KeyframeView::get_keyframe_scene_y(KeyframeViewInputConnection *track,
									  NodeKeyframe *key)
{
	return mapFromGlobal(QPoint(0, track->get_keyframe_y())).y();
}

void KeyframeView::SceneRectUpdateEvent(QRectF &rect)
{
	rect.setY(0);
	rect.setHeight(max_scroll_);
}

Rational KeyframeView::calculate_new_time_from_screen(const Rational &old_time,
												  double cursor_diff)
{
	return Rational::from_double(old_time.to_double() + cursor_diff);
}

void KeyframeView::show_context_menu()
{
	Menu m;

	MenuShared::instance()->add_items_for_edit_menu(&m, false);

	QAction *linear_key_action = nullptr;
	QAction *bezier_key_action = nullptr;
	QAction *hold_key_action = nullptr;

	if (!get_selected_keyframes().empty()) {
		bool all_keys_are_same_type = true;
		NodeKeyframe::Type type = get_selected_keyframes().front()->type();

		for (size_t i = 1; i < get_selected_keyframes().size(); i++) {
			NodeKeyframe *key_item = get_selected_keyframes().at(i);
			NodeKeyframe *prev_item = get_selected_keyframes().at(i - 1);

			if (key_item->type() != prev_item->type()) {
				all_keys_are_same_type = false;
				break;
			}
		}

		m.addSeparator();

		linear_key_action = m.addAction(tr("Linear"));
		bezier_key_action = m.addAction(tr("Bezier"));
		hold_key_action = m.addAction(tr("Hold"));

		if (all_keys_are_same_type) {
			switch (type) {
			case NodeKeyframe::k_invalid:
				break;
			case NodeKeyframe::k_linear:
				linear_key_action->setChecked(true);
				break;
			case NodeKeyframe::k_bezier:
				bezier_key_action->setChecked(true);
				break;
			case NodeKeyframe::k_hold:
				hold_key_action->setChecked(true);
				break;
			}
		}
	}

	m.addSeparator();

	ContextMenuEvent(m);

	if (!get_selected_keyframes().empty()) {
		m.addSeparator();

		QAction *properties_action = m.addAction(tr("P&roperties"));
		connect(properties_action, &QAction::triggered, this,
				&KeyframeView::show_keyframe_properties_dialog);
	}

	QAction *selected = m.exec(QCursor::pos());

	// Process keyframe type changes
	if (selected) {
		if (selected == linear_key_action || selected == bezier_key_action ||
			selected == hold_key_action) {
			NodeKeyframe::Type new_type;

			if (selected == hold_key_action) {
				new_type = NodeKeyframe::k_hold;
			} else if (selected == bezier_key_action) {
				new_type = NodeKeyframe::k_bezier;
			} else {
				new_type = NodeKeyframe::k_linear;
			}

			// Through the liboakengine C ABI facade: one undoable command
			// per distinct input (usually just one), with the same batch
			// semantics as the old per-keyframe commands.
			struct TypeGroup {
				Node *node;
				QString input;
				int element;
				QVector<int64_t> times;
				QVector<int> tracks;
			};
			QVector<TypeGroup> groups;
			foreach (NodeKeyframe *item, get_selected_keyframes()) {
				int g = 0;
				for (; g < groups.size(); g++) {
					if (groups.at(g).node == item->parent() &&
						groups.at(g).input == item->input() &&
						groups.at(g).element == item->element()) {
						break;
					}
				}
				if (g == groups.size()) {
					groups.append({ item->parent(), item->input(),
									item->element(), {}, {} });
				}
				OakEngineNode *handle =
					reinterpret_cast<OakEngineNode *>(item->parent());
				int tbn = 0, tbd = 0;
				oakengine_node_frame_time_base(handle, &tbn, &tbd);
				groups[g].times.append(Timecode::time_to_timestamp(
					item->time(), Rational(tbn, tbd), Timecode::k_round));
				groups[g].tracks.append(item->track());
			}
			int facade_type = 0;
			if (new_type == NodeKeyframe::k_bezier) {
				facade_type = 1;
			} else if (new_type == NodeKeyframe::k_hold) {
				facade_type = 2;
			}
			foreach (const TypeGroup &g, groups) {
				oakengine_node_keyframes_set_type_many(
					reinterpret_cast<OakEngineNode *>(g.node),
					g.input.toUtf8().constData(), g.element,
					g.times.constData(), g.tracks.data(), g.times.size(),
					facade_type);
			}
		}
	}
}

void KeyframeView::show_keyframe_properties_dialog()
{
	if (!get_selected_keyframes().empty()) {
		KeyframePropertiesDialog kd(get_selected_keyframes(), timebase(), this);
		kd.exec();
	}
}

void KeyframeView::update_rubber_band_for_scroll()
{
	this->selection_manager_.force_drag_update();
}

void KeyframeView::redraw()
{
	viewport()->update();
}

}
