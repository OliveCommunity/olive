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

#include "seekablewidget.h"

#include <QInputDialog>
#include <QMouseEvent>
#include <QPainter>
#include <QtMath>

#include "oakutil/qtutils.h"
#include "oakutil/oaknode.h"
#include "oakutil/range.h"
#include "core.h"
#include "dialog/markerproperties/markerpropertiesdialog.h"
#include "markerhandle.h"
#include "markerpainting.h"
#include "oakengine/node.h"
#include "oakengine/serializer.h"
#include "oakengine/timeline.h"
#include "oakengine/viewer.h"
#include "oakengine/undo.h"
#include "widget/colorlabelmenu/colorlabelmenu.h"
#include "widget/menu/menushared.h"
#include "widget/timebased/timebasedwidget.h"

namespace olive
{

#define super TimeBasedView

SeekableWidget::SeekableWidget(QWidget *parent)
	: super(parent)
	, markers_(nullptr)
	, workarea_(nullptr)
	, dragging_(false)
	, ignore_next_focus_out_(false)
	, selection_manager_(this)
	, resize_item_(nullptr)
	, marker_top_(0)
	, marker_bottom_(0)
	, marker_editing_enabled_(true)
	, bridge_(new EngineEventBridge(this))
{
	QFontMetrics fm = fontMetrics();

	text_height_ = fm.height();

	// Set width of playhead marker
	playhead_width_ = QtUtils::q_font_metrics_width(fm, "H");

	setContextMenuPolicy(Qt::CustomContextMenu);
	setFocusPolicy(Qt::ClickFocus);
	setMouseTracking(true);

	selection_manager_.set_snap_mask(TimeBasedWidget::k_snap_all);

	set_is_timeline_axes(true);
}

void SeekableWidget::set_markers(OakEngineMarkerList *markers)
{
	// Unsubscribe old marker list events via bridge
	for (int64_t id : marker_list_subs_) {
		bridge_->unsubscribe(id);
	}
	marker_list_subs_.clear();

	if (markers_) {
		selection_manager_.clear_selection();
	}

	markers_ = markers;

	if (markers_) {
		// Subscribe to marker list events via bridge instead of direct marker list signals
		marker_list_subs_.append(bridge_->subscribe(
			markers_,
			OAKENGINE_EVENT_MARKER_LIST_MARKER_ADDED));
		marker_list_subs_.append(bridge_->subscribe(
			markers_,
			OAKENGINE_EVENT_MARKER_LIST_MARKER_REMOVED));
		marker_list_subs_.append(bridge_->subscribe(
			markers_,
			OAKENGINE_EVENT_MARKER_LIST_MARKER_MODIFIED));

		// Wire the bridge signals once — bridge_ outlives individual marker
		// list subscriptions, re-connecting on every set_markers() would
		// stack duplicate viewport updates.
		if (!marker_connects_done_) {
			marker_connects_done_ = true;
			connect(bridge_, &EngineEventBridge::marker_list_marker_added, this,
					[this](OakEngineMarkerList *, OakEngineMarker *) {
						viewport()->update();
					});
			connect(bridge_, &EngineEventBridge::marker_list_marker_removed, this,
					[this](OakEngineMarkerList *, OakEngineMarker *) {
						viewport()->update();
					});
			connect(bridge_, &EngineEventBridge::marker_list_marker_modified, this,
					[this](OakEngineMarkerList *, OakEngineMarker *) {
						viewport()->update();
					});
		}
	}

	viewport()->update();
}

void SeekableWidget::set_work_area(OakEngineWorkarea *workarea)
{
	if (workarea_) {
		selection_manager_.clear_selection();

		if (workarea_range_sub_) {
			bridge_->unsubscribe(workarea_range_sub_);
			workarea_range_sub_ = 0;
		}
		if (workarea_enabled_sub_) {
			bridge_->unsubscribe(workarea_enabled_sub_);
			workarea_enabled_sub_ = 0;
		}
	}

	workarea_ = workarea;

	if (workarea_) {
		workarea_range_sub_ = bridge_->subscribe(
			reinterpret_cast<void *>(workarea_),
			OAKENGINE_EVENT_WORKAREA_RANGE_CHANGED);
		workarea_enabled_sub_ = bridge_->subscribe(
			reinterpret_cast<void *>(workarea_),
			OAKENGINE_EVENT_WORKAREA_ENABLED_CHANGED);
		connect(bridge_, &EngineEventBridge::workarea_range_changed, viewport(),
				static_cast<void (QWidget::*)()>(&QWidget::update));
		connect(bridge_, &EngineEventBridge::workarea_enabled_changed, viewport(),
				static_cast<void (QWidget::*)()>(&QWidget::update));
	}

	viewport()->update();
}

void SeekableWidget::delete_selected()
{
	if (!selection_manager_.is_dragging()) {
		const auto &selected = selection_manager_.get_selected_objects();
		if (selected.empty()) {
			return;
		}

		OakEngineNode *viewer_node =
			get_viewer_node();
		if (oakengine_node_is_sequence(viewer_node)) {
			// Batch removal through the liboakengine C ABI facade (one
			// undoable command). The facade family only wraps sequences;
			// markers of other viewer nodes (e.g. footage viewers) keep
			// the old per-marker command path below.
			QVector<int64_t> times;
			times.reserve(int(selected.size()));
			for (OakEngineMarker *marker : selected) {
				times.append(Timecode::time_to_timestamp(
					marker_time(marker).in(), timebase(), Timecode::k_round));
			}
			oakengine_sequence_marker_remove_many(
				reinterpret_cast<OakEngineSequence *>(viewer_node),
				times.constData(), times.size());
			return;
		}

		// Remove each marker (undoable individually)
		for (auto *m : selected) {
			oakengine_marker_remove(m);
		}
	}
}

bool SeekableWidget::copy_selected(bool cut)
{
	if (!selection_manager_.get_selected_objects().empty()) {
		const auto &selected = selection_manager_.get_selected_objects();
		std::vector<const OakEngineMarker *> markers;
		markers.reserve(selected.size());
		for (auto *m : selected) {
			markers.push_back(m);
		}

		OakEngineClipboard *cb = oakengine_clipboard_create(
			OAKENGINE_CLIPBOARD_MARKERS, nullptr, nullptr);
		oakengine_clipboard_set_markers(
			cb, markers.data(), static_cast<int>(markers.size()));
		oakengine_clipboard_copy(cb);
		oakengine_clipboard_free(cb);

		if (cut) {
			delete_selected();
		}

		return true;
	} else {
		return false;
	}
}

bool SeekableWidget::paste_markers()
{
	OakEngineNode *viewer =
		get_viewer_node();
	OakEngineProject *project = oak::Node(viewer).project().handle();
	OakEngineClipboard *cb = oakengine_clipboard_create(
		OAKENGINE_CLIPBOARD_MARKERS, project, nullptr);
	int result_code;
	oakengine_clipboard_paste(
		cb, OAKENGINE_CLIPBOARD_MARKERS, project, &result_code, nullptr, 0);
	if (result_code == OAKENGINE_OK) {
		int count = oakengine_clipboard_get_loaded_marker_count(cb);
		if (count > 0) {
			// Collect the pasted markers
			std::vector<OakEngineMarker *> markers;
			markers.reserve(count);
			for (int i = 0; i < count; i++) {
				markers.push_back(
					oakengine_clipboard_get_loaded_marker_at(cb, i));
			}

			// Normalize markers to start at playhead
			Rational min = RATIONAL_MAX;
			for (auto it = markers.cbegin(); it != markers.cend(); it++) {
				min = std::min(min, marker_time(*it).in());
			}
			int64_t ph_num = 0, ph_den = 1;
			oakengine_viewer_get_playhead(viewer, &ph_num, &ph_den);
			min -= Rational(int(ph_num), int(ph_den));

			for (auto it = markers.cbegin(); it != markers.cend(); it++) {
				OakEngineMarker *m = *it;

				Rational new_in = marker_time(m).in() - min;
				oakengine_marker_set_time_live(
					m,
					new_in.numerator(), new_in.denominator(),
					new_in.numerator(), new_in.denominator());

				if (OakEngineMarker *existing =
						oakengine_marker_list_marker_at_time(
							markers_, new_in.numerator(),
							new_in.denominator())) {
					oakengine_marker_remove(existing);
				}

				// Re-add the clipboard marker to the list (undoable)
				oakengine_marker_list_add_existing(markers_, m);
			}

			oakengine_clipboard_free(cb);
			return true;
		}
	}

	oakengine_clipboard_free(cb);
	return false;
}

void SeekableWidget::mousePressEvent(QMouseEvent *event)
{
	OakEngineMarker *initial;

	if (hand_press(event)) {
		return;
	} else if (event->modifiers() & Qt::ControlModifier) {
		selection_manager_.rubber_band_start(event);
	} else if (marker_editing_enabled_ &&
			   (initial = selection_manager_.mouse_press(event))) {
		selection_manager_.drag_start(initial, event);
	} else if (resize_item_) {
		// Handle selection, even though we won't be using it for dragging
		if (!(event->modifiers() & Qt::ShiftModifier)) {
			selection_manager_.clear_selection();
		}
		// resize_item_ is either the workarea or a marker (see
		// find_resize_handle); the handles are opaque here, so tell
		// them apart by comparing against workarea_ instead of dynamic_cast.
		if (resize_item_ != workarea_) {
			selection_manager_.select(
				static_cast<OakEngineMarker *>(resize_item_));
		}
		dragging_ = true;
		resize_start_ = mapToScene(event->pos());
	} else if (!selection_manager_.get_object_at_point(event->pos()) &&
			   event->button() == Qt::LeftButton) {
		seek_to_scene_point(mapToScene(event->pos()).x());
		dragging_ = true;

		deselect_all_markers();
	}
}

void SeekableWidget::mouseMoveEvent(QMouseEvent *event)
{
	if (hand_move(event)) {
		return;
	} else if (selection_manager_.is_rubber_banding()) {
		selection_manager_.rubber_band_move(event->pos());
		viewport()->update();
	} else if (selection_manager_.is_dragging()) {
		selection_manager_.drag_move(event->pos());
	} else if (dragging_) {
		QPointF scene = mapToScene(event->pos());
		if (resize_item_) {
			drag_resize_handle(scene);
		} else {
			seek_to_scene_point(scene.x());
		}
	} else {
		// Look for resize points
		if (!last_playhead_shape_.containsPoint(event->pos(),
												Qt::OddEvenFill) &&
			!selection_manager_.get_object_at_point(event->pos()) &&
			find_resize_handle(event)) {
			setCursor(Qt::SizeHorCursor);
		} else {
			unsetCursor();
			clear_resize_handle();
		}
	}

	if (event->buttons()) {
		// Signal cursor pos in case we should scroll to catch up to it
		emit drag_moved(event->pos().x(), event->pos().y());
	}
}

void SeekableWidget::mouseReleaseEvent(QMouseEvent *event)
{
	if (hand_release(event)) {
		return;
	}

	if (selection_manager_.is_rubber_banding()) {
		selection_manager_.rubber_band_stop();
		return;
	}

	if (selection_manager_.is_dragging()) {
		void *command = oakengine_undo_command_create_multi();
		selection_manager_.drag_stop(command);
		oakengine_undo_push(
			command, tr("Moved %1 Marker(s)").arg(selection_manager_.get_selected_objects().size()).toUtf8().constData());
	}

	if (get_snap_service()) {
		get_snap_service()->hide_snaps();
	}

	if (resize_item_) {
		commit_resize_handle();
		resize_item_ = nullptr;
	}

	dragging_ = false;
	emit drag_released();
}

void SeekableWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
	super::mouseDoubleClickEvent(event);

	if (selection_manager_.get_object_at_point(event->pos()) &&
		!selection_manager_.get_selected_objects().empty()) {
		show_marker_properties();
	}
}

void SeekableWidget::focusOutEvent(QFocusEvent *event)
{
	super::focusOutEvent(event);

	if (ignore_next_focus_out_) {
		ignore_next_focus_out_ = false;
	} else {
		// Deselect everything when we lose focus
		deselect_all_markers();
	}
}

void SeekableWidget::draw_markers(QPainter *p, int marker_bottom)
{
	selection_manager_.clear_drawn_objects();

	// Draw markers
	const int marker_count =
		markers_ ? oakengine_marker_list_count(markers_) : 0;
	if (marker_count > 0 && marker_bottom > 0) {
		int lim_left = get_left_limit();
		int lim_right = get_right_limit();

		for (int i = 0; i < marker_count; i++) {
			OakEngineMarker *marker =
				oakengine_marker_list_at(markers_, i);
			const TimeRange range = marker_time(marker);

			int marker_right = time_to_scene(range.out());
			if (marker_right < lim_left) {
				continue;
			}

			int marker_left = time_to_scene(range.in());
			if (marker_left >= lim_right) {
				break;
			}

			int max_marker_right = lim_right;
			{
				// Check if there's a marker next
				if (i + 1 < marker_count) {
					OakEngineMarker *next =
						oakengine_marker_list_at(markers_, i + 1);
					max_marker_right =
						std::min(max_marker_right,
								 int(time_to_scene(marker_time(next).in())));
				}
			}

			QRect marker_rect = MarkerPainting::draw(
				p, QPoint(marker_left, marker_bottom), max_marker_right,
				get_scale(), selection_manager_.is_selected(marker),
				marker_name(marker), marker_color(marker),
				range.in(), range.out());
			marker_top_ = marker_rect.top();
			selection_manager_.declare_drawn_object(marker, marker_rect);
		}
	}

	marker_bottom_ = marker_bottom;
}

void SeekableWidget::draw_work_area(QPainter *p)
{
	// Fetch workarea state through the C ABI (opaque handle on this side)
	int64_t wa_in_num = 0, wa_in_den = 1, wa_out_num = 0, wa_out_den = 1;
	int wa_enabled = 0;
	const bool have_workarea =
		workarea_ &&
		oakengine_workarea_get(
			workarea_, &wa_in_num,
			&wa_in_den, &wa_out_num, &wa_out_den,
			&wa_enabled) == OAKENGINE_OK &&
		wa_enabled;

	// Draw in/out workarea
	if (have_workarea) {
		const Rational wa_in{int(wa_in_num), int(wa_in_den)};
		const Rational wa_out{int(wa_out_num), int(wa_out_den)};

		int lim_left = get_left_limit();
		int lim_right = get_right_limit();

		int workarea_left = qMax(qreal(lim_left), time_to_scene(wa_in));
		int workarea_right;

		if (wa_out == RATIONAL_MAX) {
			workarea_right = lim_right;
		} else {
			workarea_right =
				qMin(qreal(lim_right), time_to_scene(wa_out));
		}

		QColor translucent_highlight = palette().highlight().color();
		translucent_highlight.setAlpha(96);
		p->fillRect(workarea_left, 0, workarea_right - workarea_left, height(),
					translucent_highlight);
	}
}

void SeekableWidget::deselect_all_markers()
{
	selection_manager_.clear_selection();

	viewport()->update();
}

void SeekableWidget::set_marker_color(int c)
{
	QVector<OakEngineMarker *> oak_markers;
	foreach (OakEngineMarker *marker,
			 selection_manager_.get_selected_objects()) {
		oak_markers.append(marker);
	}
	oakengine_marker_set_properties(
		oak_markers.data(), oak_markers.size(), c, nullptr, 0, 0, 0, 0, 0,
		nullptr);
}

void SeekableWidget::show_marker_properties()
{
	MarkerPropertiesDialog mpd(selection_manager_.get_selected_objects(),
							   timebase(), this);
	ignore_next_focus_out_ = true;
	mpd.exec();
}

void SeekableWidget::TimebaseChangedEvent(const Rational &t)
{
	super::TimebaseChangedEvent(t);

	selection_manager_.set_timebase(t);
}

void SeekableWidget::seek_to_scene_point(qreal scene)
{
	if (timebase().isNull()) {
		return;
	}

	Rational playhead_time = qMax(Rational(0), scene_to_time(scene));

	if (Core::instance()->snapping() && get_snap_service()) {
		Rational movement;

		get_snap_service()->snap_point({ playhead_time }, &movement,
									TimeBasedWidget::k_snap_all &
										~TimeBasedWidget::k_snap_to_playhead);

		playhead_time += movement;
	}

	OakEngineNode *viewer =
		get_viewer_node();
	int64_t ph_num = 0, ph_den = 1;
	oakengine_viewer_get_playhead(viewer, &ph_num, &ph_den);
	if (viewer &&
		playhead_time != Rational(int(ph_num), int(ph_den))) {
		oakengine_viewer_set_playhead(
			viewer,
			playhead_time.numerator(), playhead_time.denominator());
	}
}

void SeekableWidget::SelectionManagerSelectEvent(void *obj)
{
	super::SelectionManagerSelectEvent(obj);

	viewport()->update();
}

void SeekableWidget::SelectionManagerDeselectEvent(void *obj)
{
	super::SelectionManagerDeselectEvent(obj);

	viewport()->update();
}

void SeekableWidget::CatchUpScrollEvent()
{
	super::CatchUpScrollEvent();

	this->selection_manager_.force_drag_update();
}

void SeekableWidget::draw_playhead(QPainter *p, int x, int y)
{
	int half_width = playhead_width_ / 2;

	{
		int test = x - this->get_scroll();
		if (test + half_width < 0 || test - half_width > width()) {
			return;
		}
	}

	p->setRenderHint(QPainter::Antialiasing);

	int half_text_height = text_height() / 3;

	last_playhead_shape_ = QPolygon({
		QPoint(x, y),
		QPoint(x - half_width, y - half_text_height),
		QPoint(x - half_width, y - text_height()),
		QPoint(x + 1 + half_width, y - text_height()),
		QPoint(x + 1 + half_width, y - half_text_height),
		QPoint(x + 1, y),
	});

	p->drawPolygon(last_playhead_shape_);

	p->setRenderHint(QPainter::Antialiasing, false);
}

int SeekableWidget::get_left_limit() const
{
	return get_scroll();
}

int SeekableWidget::get_right_limit() const
{
	return get_left_limit() + width();
}

bool SeekableWidget::show_context_menu(const QPoint &p)
{
	if (marker_editing_enabled_ && selection_manager_.get_object_at_point(p) &&
		!selection_manager_.get_selected_objects().empty()) {
		// Show marker-specific menu
		Menu m;

		ColorLabelMenu color_coding_menu;
		connect(&color_coding_menu, &ColorLabelMenu::color_selected, this,
				&SeekableWidget::set_marker_color);
		m.addMenu(&color_coding_menu);

		m.addSeparator();

		MenuShared::instance()->add_items_for_edit_menu(&m, false);

		m.addSeparator();

		QAction *properties_action = m.addAction(tr("Properties"));
		connect(properties_action, &QAction::triggered, this,
				&SeekableWidget::show_marker_properties);

		ignore_next_focus_out_ = true;
		m.exec(mapToGlobal(p));
		return true;
	} else {
		return false;
	}
}

bool SeekableWidget::find_resize_handle(QMouseEvent *event)
{
	if (!marker_editing_enabled_) {
		return false;
	}

	clear_resize_handle();

	QPointF scene = mapToScene(event->pos());
	const int border = 10;
	Rational min = scene_to_time_no_grid(scene.x() - border);
	Rational max = scene_to_time_no_grid(scene.x() + border);

	// Test for workarea (state fetched through the C ABI; the handle is
	// opaque on this side)
	int64_t wa_in_num = 0, wa_in_den = 1, wa_out_num = 0, wa_out_den = 1;
	int wa_enabled = 0;
	const bool have_workarea =
		workarea_ &&
		oakengine_workarea_get(
			workarea_, &wa_in_num,
			&wa_in_den, &wa_out_num, &wa_out_den,
			&wa_enabled) == OAKENGINE_OK &&
		wa_enabled;

	if (have_workarea) {
		const Rational wa_in{int(wa_in_num), int(wa_in_den)};
		const Rational wa_out{int(wa_out_num), int(wa_out_den)};

		if (wa_in >= min && wa_in < max) {
			resize_mode_ = k_resize_in;
		} else if (wa_out >= min && wa_out < max) {
			resize_mode_ = k_resize_out;
		}

		if (resize_mode_ != k_resize_none) {
			resize_item_ = workarea_;
			resize_item_range_ = TimeRange(wa_in, wa_out);
			resize_snap_mask_ = TimeBasedWidget::k_snap_all &
								~TimeBasedWidget::k_snap_to_workarea;
		}
	}

	if (resize_mode_ == k_resize_none &&
		event->pos().y() >= marker_top_ &&
		event->pos().y() < marker_bottom_) {
		if (markers_) {
			// Check for markers
			const int marker_count =
				oakengine_marker_list_count(markers_);
			for (int i = 0; i < marker_count; i++) {
				OakEngineMarker *m =
					oakengine_marker_list_at(markers_, i);
				const TimeRange m_time = marker_time(m);
				if (m_time.in() != m_time.out()) {
					if (m_time.in() >= min && m_time.in() < max) {
						resize_mode_ = k_resize_in;
					} else if (m_time.out() >= min &&
							   m_time.out() < max) {
						resize_mode_ = k_resize_out;
					}

					if (resize_mode_ != k_resize_none) {
						resize_item_ = m;
						resize_item_range_ = m_time;
						resize_snap_mask_ = TimeBasedWidget::k_snap_all;
						break;
					}
				}
			}
		}
	}

	return resize_item_;
}

void SeekableWidget::clear_resize_handle()
{
	resize_item_ = nullptr;
	resize_mode_ = k_resize_none;
}

void SeekableWidget::drag_resize_handle(const QPointF &scene)
{
	qreal diff = scene.x() - resize_start_.x();

	Rational proposed_time;

	if (resize_mode_ == k_resize_in) {
		proposed_time = qMax(Rational(0), qMin(resize_item_range_.out(),
											   resize_item_range_.in() +
												   scene_to_time_no_grid(diff)));
	} else {
		proposed_time =
			qMax(resize_item_range_.in(),
				 resize_item_range_.out() + scene_to_time_no_grid(diff));
	}

	Rational presnap_time = proposed_time;

	if (Core::instance()->snapping() && get_snap_service()) {
		Rational movement;

		get_snap_service()->snap_point({ proposed_time }, &movement,
									resize_snap_mask_);

		proposed_time += movement;
	}

	TimeRange new_range = resize_item_range_;
	if (resize_mode_ == k_resize_in) {
		// Markers should not have the same time as anything else
		// NOTE: This code is largely duplicated from TimeBasedViewSelectionManager::DragMove. Not ideal,
		//       but I'm not sure if there's a good way to re-use that code
		// resize_item_ is either the workarea or a marker; the handles are
		// opaque here, so compare against workarea_ instead of
		// dynamic_cast.
		if (resize_item_ != workarea_) {
			OakEngineMarker *marker =
				static_cast<OakEngineMarker *>(resize_item_);
			if (markers_ &&
				oakengine_marker_list_marker_at_time(
					markers_, proposed_time.numerator(),
					proposed_time.denominator()) != marker) {
				proposed_time = presnap_time;

				if (get_snap_service()) {
					get_snap_service()->hide_snaps();
				}
			}

			while (markers_ &&
				   oakengine_marker_list_marker_at_time(
					   markers_, proposed_time.numerator(),
					   proposed_time.denominator()) != marker &&
				   oakengine_marker_list_marker_at_time(
					   markers_, proposed_time.numerator(),
					   proposed_time.denominator())) {
				proposed_time += Rational(1, 1000);
			}
		}

		new_range.set_in(proposed_time);
	} else {
		new_range.set_out(proposed_time);
	}

	if (resize_item_ != workarea_) {
		oakengine_marker_set_time_live(
			static_cast<OakEngineMarker *>(resize_item_),
			new_range.in().numerator(), new_range.in().denominator(),
			new_range.out().numerator(), new_range.out().denominator());
	} else {
		oakengine_workarea_set_range(
			workarea_,
			new_range.in().numerator(), new_range.in().denominator(),
			new_range.out().numerator(), new_range.out().denominator());
	}
}

void SeekableWidget::commit_resize_handle()
{
	// resize_item_ is either the workarea or a marker (see
	// find_resize_handle); compare against workarea_ instead of
	// dynamic_cast since the handles are opaque here.
	if (resize_item_ != workarea_) {
		OakEngineMarker *marker =
			static_cast<OakEngineMarker *>(resize_item_);
		oakengine_marker_set_properties(
			&marker, 1, -1, nullptr, 1,
			resize_item_range_.in().numerator(),
			resize_item_range_.in().denominator(),
			resize_item_range_.out().numerator(),
			resize_item_range_.out().denominator(),
			nullptr);
	} else {
		void *wa_cmd = oakengine_undo_command_create(
			tr("Changed Workarea Length").toUtf8().constData(),
			nullptr, nullptr, nullptr, nullptr);
		oakengine_undo_push(wa_cmd,
			tr("Changed Workarea Length").toUtf8().constData());
	}
}

}
